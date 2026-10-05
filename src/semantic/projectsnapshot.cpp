#include "projectsnapshot.h"
#include <QDataStream>
#include <QDir>
#include <QIODevice>
#include <QFileInfo>

QString ProjectSnapshot::semanticIdentity() const
{
    auto pathKey = [](const QString& path) {
        if (path.isEmpty())
            return QString();
        QString key = QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
        key = key.toCaseFolded();
#endif
        return key;
    };
    auto pathKeys = [&](const QStringList& paths) {
        QStringList keys;
        for (const QString& path : paths)
            keys.append(pathKey(path));
        return keys;
    };
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << pathKey(workspaceRoot) << pathKeys(systemVerilogFiles)
           << pathKeys(includeDirs) << topModule << fileExtensions
           << pathKeys(ignoredPaths) << sourceDiscoveryComplete;
    QStringList keys = defines.keys();
    keys.sort(Qt::CaseSensitive);
    for (const QString& key : keys)
        stream << key << defines.value(key);
    return QString::fromLatin1(bytes.toBase64());
}

QStringList ProjectSnapshot::filesForSourceRole(
    SymbolTaxonomy::SourceRole role) const
{
    QStringList files;
    for (auto it = sourceRoles.cbegin(); it != sourceRoles.cend(); ++it) {
        if (it.value() == role)
            files.append(it.key());
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

QStringList ProjectSnapshot::designSourceFiles() const
{
    return filesForSourceRole(SymbolTaxonomy::SourceRole::DesignSource);
}

QStringList ProjectSnapshot::headerSourceFiles() const
{
    QStringList files;
    for (auto it = sourceRoles.cbegin(); it != sourceRoles.cend(); ++it) {
        if (SymbolTaxonomy::isHeaderSourceRole(it.value()))
            files.append(it.key());
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

