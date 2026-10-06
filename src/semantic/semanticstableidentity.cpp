#include "semanticstableidentity.h"

#include "semanticindex.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString normalizedPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return {};
    QString result = QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

QString workspaceRelativePath(const QString& workspaceRoot,
                              const QString& fileName)
{
    const QString root = normalizedPath(workspaceRoot);
    const QString file = normalizedPath(fileName);
    if (root.isEmpty() || file.isEmpty())
        return file;
    if (file == root || file.startsWith(root + QLatin1Char('/'))) {
        QString relative = QDir(root).relativeFilePath(file);
        relative = QDir::cleanPath(QDir::fromNativeSeparators(relative));
#ifdef Q_OS_WIN
        relative = relative.toCaseFolded();
#endif
        return relative;
    }
    return file;
}

QString digestId(const QString& prefix, const QString& value)
{
    const QByteArray digest = QCryptographicHash::hash(
        value.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    return prefix + QString::fromLatin1(digest);
}

QString encodeIdentityParts(const QStringList& parts)
{
    QString encoded;
    for (const auto& part : parts)
        encoded += QString::number(part.size()) + QLatin1Char(':') + part;
    return encoded;
}

// ownerScope is the length-prefixed declaration tuple written by
// semanticDeclarationKey, or a legacy display name at the root. Decode every
// ancestor before relativizing: hashing the opaque tuple would retain absolute
// paths (and their lengths) when a workspace moves.
QString portableOwnerScope(const QString& scope, const QString& workspaceRoot)
{
    QStringList parts;
    qsizetype offset = 0;
    for (int index = 0; index < 4; ++index) {
        const qsizetype colon = scope.indexOf(QLatin1Char(':'), offset);
        if (colon < 0)
            break;
        bool valid = false;
        const qlonglong size = scope.mid(offset, colon - offset).toLongLong(&valid);
        if (!valid || size < 0 || size > scope.size() - colon - 1)
            break;
        parts.append(scope.mid(colon + 1, size));
        offset = colon + 1 + size;
    }
    if (parts.size() != 4 || offset != scope.size()
        || !QFileInfo(parts[0]).isAbsolute()) {
        return encodeIdentityParts({QStringLiteral("name"), scope});
    }
    return encodeIdentityParts({QStringLiteral("declaration"),
        workspaceRelativePath(workspaceRoot, parts[0]),
        portableOwnerScope(parts[1], workspaceRoot), parts[2], parts[3]});
}

bool isPositionSensitiveKind(SymbolTaxonomy::DeclarationKind kind)
{
    using Kind = SymbolTaxonomy::DeclarationKind;
    return kind == Kind::Process
        || kind == Kind::Generate
        || kind == Kind::Constraint
        || kind == Kind::Unknown
        || kind == Kind::User;
}

} // namespace

SymbolStableKey semanticDeclarationKey(const SemanticSymbolRecord& record)
{
    QString scope = record.owner.name;
    if (record.owner.stableKey.isValid()) {
        const auto& owner = record.owner.stableKey;
        scope = encodeIdentityParts({normalizedPath(owner.fileName),
            owner.ownerScope, owner.symbolName, QString::number(int(owner.declarationKind))});
    }
    return {normalizedPath(record.location.fileName), record.name, record.declarationKind,
            scope, record.location.position, record.location.length};
}

SemanticStableIdentity semanticStableIdentity(
    const SemanticSymbolRecord& record,
    const QString& workspaceRoot)
{
    SemanticStableIdentity identity;
    if (!record.isValid())
        return identity;

    const int kind = static_cast<int>(record.declarationKind);
    const int ownerKind = static_cast<int>(record.owner.kind);
    const QString owner = record.owner.name.trimmed();
    const QString name = record.name.trimmed();
    const QString relativeFile = workspaceRelativePath(
        workspaceRoot, record.location.fileName);

    const bool qualifiedOwner = record.owner.stableKey.isValid();
    const auto& ownerKey = record.owner.stableKey;
    const QString ownerIdentity = qualifiedOwner
        ? encodeIdentityParts({QStringLiteral("declaration"),
            workspaceRelativePath(workspaceRoot, ownerKey.fileName),
            portableOwnerScope(ownerKey.ownerScope, workspaceRoot),
            ownerKey.symbolName, QString::number(int(ownerKey.declarationKind))})
        : encodeIdentityParts({QStringLiteral("name"), owner});
    QStringList canonicalParts{
        QStringLiteral("v2"),
        QString::number(kind),
        QString::number(ownerKind),
        relativeFile,
        ownerIdentity,
        name,
    };
    QString stability = qualifiedOwner ? QStringLiteral("semantic")
                                       : QStringLiteral("file-semantic");
    // A package records its own display scope. Other legacy owned records
    // without declaration identity cannot promise cross-snapshot uniqueness.
    if (!qualifiedOwner && !owner.isEmpty()
        && !(record.declarationKind == SymbolTaxonomy::DeclarationKind::Package
             && owner == name)) {
        canonicalParts.append(QString::number(record.location.position));
        stability = QStringLiteral("snapshot");
    }
    if (isPositionSensitiveKind(record.declarationKind)) {
        const QString declaration =
            record.presentation.declarationText.trimmed();
        if (!declaration.isEmpty()) {
            canonicalParts.append(QString::fromLatin1(
                QCryptographicHash::hash(declaration.toUtf8(),
                                         QCryptographicHash::Sha256)
                    .toHex().left(16)));
            stability = QStringLiteral("structural");
        } else {
            canonicalParts.append(QString::number(
                record.location.position));
            stability = QStringLiteral("snapshot");
        }
    }

    identity.canonicalIdentity = encodeIdentityParts(canonicalParts);
    identity.stableId = digestId(QStringLiteral("zsym-v2-"),
                                 identity.canonicalIdentity);
    identity.exactId = digestId(
        QStringLiteral("zexact-v1-"),
        record.stableKey.toString());
    identity.stability = stability;
    return identity;
}

QString semanticStableSymbolUri(const SemanticSymbolRecord& record,
                                const QString& workspaceRoot)
{
    const SemanticStableIdentity identity =
        semanticStableIdentity(record, workspaceRoot);
    if (!identity.isValid())
        return {};

    QUrl url;
    url.setScheme(QStringLiteral("zeroslack"));
    url.setHost(QStringLiteral("symbol"));
    url.setPath(QLatin1Char('/') + identity.stableId);
    if (!workspaceRoot.trimmed().isEmpty()) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("workspace"),
                           QFileInfo(workspaceRoot).absoluteFilePath());
        url.setQuery(query);
    }
    return url.toString(QUrl::FullyEncoded);
}
