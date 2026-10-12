#include "ownedpaths.h"
#include "workspace.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace simdock {
bool plainPathWithin(const QString& parent, const QString& path)
{
    const auto base = QDir::cleanPath(QFileInfo(parent).absoluteFilePath());
    const auto target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    const auto prefix = base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/');
    if (!target.startsWith(prefix, sensitivity) || target.compare(base, sensitivity) == 0 || !insideWorkspace(base, target)) return false;
    auto cursor = target;
    while (true) {
        const QFileInfo info(cursor);
        if (info.isSymLink() || info.isJunction()) return false;
#ifdef Q_OS_WIN
        const auto native = QDir::toNativeSeparators(cursor);
        const auto attributes = GetFileAttributesW(reinterpret_cast<const wchar_t*>(native.utf16()));
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
        const auto next = info.dir().absolutePath();
        if (next == cursor) break;
        cursor = next;
    }
    return true;
}
static bool plainTree(const QString& parent, const QString& path)
{
    if (!plainPathWithin(parent, path)) return false;
    if (!QFileInfo(path).isDir()) return true;
    for (const auto& entry : QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        if (!plainTree(parent, entry.absoluteFilePath())) return false;
    return true;
}
bool removeOwnedTree(const QString& parent, const QString& path, QString* error)
{
    if (!QFileInfo::exists(path)) return true;
    if (!plainTree(parent, path)) {
        *error = QStringLiteral("Preserved an unsafe or linked managed path: %1").arg(path); return false;
    }
    if (!QDir(path).removeRecursively()) {
        *error = QStringLiteral("Could not release managed files: %1").arg(path); return false;
    }
    return true;
}
}
