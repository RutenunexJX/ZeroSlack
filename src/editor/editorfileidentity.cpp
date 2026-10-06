#include "editorfileidentity.h"

#include <QDir>
#include <QFileInfo>

#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
QString cleanedAbsolutePath(const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

QString finalExistingPath(const QString& absolutePath)
{
#ifdef Q_OS_WIN
    const QString nativePath =
        QDir::toNativeSeparators(absolutePath);
    const HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(nativePath.utf16()),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        const DWORD flags =
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
        const DWORD required =
            GetFinalPathNameByHandleW(
                handle, nullptr, 0, flags);
        if (required > 0) {
            std::vector<wchar_t> buffer(
                static_cast<std::size_t>(required) + 1u,
                L'\0');
            const DWORD written =
                GetFinalPathNameByHandleW(
                    handle,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    flags);
            CloseHandle(handle);
            if (written > 0 && written < buffer.size()) {
                QString resolved =
                    QString::fromWCharArray(
                        buffer.data(),
                        static_cast<qsizetype>(written));
                const QString extendedUncPrefix =
                    QStringLiteral("\\\\?\\UNC\\");
                const QString extendedPrefix =
                    QStringLiteral("\\\\?\\");
                if (resolved.startsWith(
                        extendedUncPrefix,
                        Qt::CaseInsensitive)) {
                    resolved =
                        QStringLiteral("\\\\")
                        + resolved.mid(
                            extendedUncPrefix.size());
                } else if (resolved.startsWith(
                               extendedPrefix,
                               Qt::CaseInsensitive)) {
                    resolved.remove(
                        0, extendedPrefix.size());
                }
                return QDir::cleanPath(
                    QDir::fromNativeSeparators(
                        resolved));
            }
        } else {
            CloseHandle(handle);
        }
    }
#endif

    const QString canonical =
        QFileInfo(absolutePath).canonicalFilePath();
    return canonical.isEmpty()
        ? QString()
        : QDir::cleanPath(
              QDir::fromNativeSeparators(canonical));
}

QString resolvedIdentityPath(const QString& fileName)
{
    const QString absolute = cleanedAbsolutePath(fileName);
    if (absolute.isEmpty())
        return {};

    const QString canonical = finalExistingPath(absolute);
    if (!canonical.isEmpty()) {
        return QDir::cleanPath(
            QDir::fromNativeSeparators(canonical));
    }

    // Preserve one identity for not-yet-created files reached through a
    // symlink or Windows junction by resolving the deepest existing parent.
    QStringList missingComponents;
    QFileInfo probe(absolute);
    while (!probe.exists()) {
        const QString name = probe.fileName();
        if (name.isEmpty())
            break;
        missingComponents.prepend(name);
        const QString parentPath = probe.dir().absolutePath();
        if (parentPath == probe.absoluteFilePath())
            break;
        probe.setFile(parentPath);
    }

    const QString parentCanonical =
        finalExistingPath(probe.absoluteFilePath());
    if (parentCanonical.isEmpty())
        return absolute;

    QString resolved = QDir::cleanPath(
        QDir::fromNativeSeparators(parentCanonical));
    for (const QString& component : std::as_const(missingComponents))
        resolved = QDir(resolved).absoluteFilePath(component);
    return QDir::cleanPath(resolved);
}
}

QString EditorFileIdentity::normalized(QString fileName)
{
    return cleanedAbsolutePath(fileName);
}

QString EditorFileIdentity::lookupKey(QString fileName)
{
    QString key = resolvedIdentityPath(fileName);
    if (key.isEmpty())
        return key;
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

QHash<QString, QString> EditorFileIdentity::lookupKeys(const QStringList& fileNames)
{
    QHash<QString, QString> identities;
    QHash<QString, QString> parents;
    for (const QString& fileName : fileNames) {
        const QString absolute = cleanedAbsolutePath(fileName);
        const QFileInfo file(absolute);
        const QString leaf = file.fileName();
        bool nativeLeafAlias = leaf.contains(QLatin1Char('~'))
            || leaf.contains(QLatin1Char(':')) || leaf.endsWith(QLatin1Char('.'))
            || leaf.endsWith(QLatin1Char(' '));
#ifdef Q_OS_WIN
        // Native case comparison can differ from QString for non-ASCII names.
        // Let the full resolver return the actual stored leaf in that case.
        for (const QChar character : leaf)
            nativeLeafAlias |= character.unicode() > 127;
        const QString native = QDir::toNativeSeparators(absolute);
        const DWORD attributes = GetFileAttributesW(
            reinterpret_cast<LPCWSTR>(native.utf16()));
        const bool regularFile = attributes != INVALID_FILE_ATTRIBUTES
            && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
        const bool regularFile = file.isFile() && !file.isSymLink();
#endif
        // A leaf alias can rename the file itself. Missing entries also need
        // the existing deepest-parent semantics. A Windows DOS short name
        // must expand the leaf, not merely its parent directory.
        if (absolute.isEmpty() || !regularFile
            || nativeLeafAlias) {
            identities.insert(fileName, lookupKey(fileName));
            continue;
        }
        const QString parent = file.absolutePath();
        auto resolved = parents.constFind(parent);
        if (resolved == parents.cend()) {
            parents.insert(parent, finalExistingPath(parent));
            resolved = parents.constFind(parent);
        }
        if (resolved->isEmpty()) {
            identities.insert(fileName, lookupKey(fileName));
            continue;
        }
        QString identity = QDir::cleanPath(QDir(resolved.value()).filePath(leaf));
#ifdef Q_OS_WIN
        identity = identity.toCaseFolded();
#endif
        identities.insert(fileName, identity);
    }
    return identities;
}

QString EditorFileIdentity::physicalPath(QString fileName)
{
    return resolvedIdentityPath(fileName);
}

bool EditorFileIdentity::same(const QString& lhs, const QString& rhs)
{
    const QString lhsKey = lookupKey(lhs);
    return !lhsKey.isEmpty() && lhsKey == lookupKey(rhs);
}

bool EditorFileIdentity::set(QString nextFileName)
{
    const QString normalizedFileName = normalized(nextFileName);
    if (fileName == normalizedFileName)
        return false;

    fileName = normalizedFileName;
    return true;
}

QString EditorFileIdentity::current() const
{
    return fileName;
}
