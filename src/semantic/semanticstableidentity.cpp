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

    QStringList canonicalParts{
        QStringLiteral("v1"),
        QString::number(kind),
        QString::number(ownerKind),
        owner,
        name,
    };
    QString stability = QStringLiteral("semantic");
    if (owner.isEmpty()) {
        canonicalParts.append(relativeFile);
        stability = QStringLiteral("file-semantic");
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

    identity.canonicalIdentity = canonicalParts.join(QLatin1Char('\n'));
    identity.stableId = digestId(QStringLiteral("zsym-v1-"),
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
