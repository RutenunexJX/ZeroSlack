#include "navigationmanager.h"

#include "navigationservice.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {
QString normalizedNavigationCachePath(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()))
        .toCaseFolded();
}

QStringList normalizedNavigationCacheFiles(const QStringList& files)
{
    QSet<QString> seen;
    QStringList result;
    result.reserve(files.size());
    for (const QString& file : files) {
        const QString normalized = normalizedNavigationCachePath(file);
        if (normalized.isEmpty() || seen.contains(normalized))
            continue;
        seen.insert(normalized);
        result.append(normalized);
    }
    std::sort(result.begin(), result.end(), [](const QString& lhs, const QString& rhs) {
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });
    return result;
}
}

void NavigationManager::NavigationContext::setCurrentFileName(
    const QString& fileName)
{
    currentFileName = fileName;
}

void NavigationManager::NavigationContext::setCurrentWorkspacePath(
    const QString& workspacePath)
{
    currentWorkspacePath = workspacePath;
}

void NavigationManager::NavigationContext::clearCurrentWorkspacePath()
{
    currentWorkspacePath.clear();
}

void NavigationManager::NavigationContext::setSearchFilter(
    NavigationView view,
    const QString& filter)
{
    if (view == DesignHierarchyView)
        designSearchFilter = filter.trimmed();
    else
        fileSearchFilter = filter.trimmed();
}

QString NavigationManager::NavigationContext::searchFilter(
    NavigationView view) const
{
    return view == DesignHierarchyView
        ? designSearchFilter
        : fileSearchFilter;
}

void NavigationManager::NavigationCaches::clearFileList()
{
    fileList.clear();
    fileHierarchyFilter.clear();
    fileListValid = false;
    fileHierarchyValid = false;
}

void NavigationManager::NavigationCaches::clearDesignHierarchy()
{
    designHierarchy = {};
    designRootModules.clear();
    designFileScope.clear();
    designStructureFingerprint.clear();
    designSnapshotGeneration = 0;
    designHierarchyValid = false;
}

QString NavigationManager::designHierarchyCacheKey() const
{
    const QString workspaceKey =
        normalizedNavigationCachePath(context.currentWorkspacePath);
    if (!workspaceKey.isEmpty())
        return QStringLiteral("workspace:%1").arg(workspaceKey);

    const QStringList files =
        normalizedNavigationCacheFiles(getSystemVerilogFiles());
    if (files.isEmpty())
        return QString();
    return QStringLiteral("files:%1").arg(files.join(QLatin1Char('\n')));
}

void NavigationManager::saveDesignHierarchyCache()
{
    const QString key = designHierarchyCacheKey();
    if (key.isEmpty())
        return;
    if (!caches.designHierarchyValid
        && caches.designTopInferred
        && caches.designTopModule.isEmpty()) {
        return;
    }

    DesignHierarchyCacheEntry entry;
    entry.hierarchy = caches.designHierarchy;
    entry.topModule = caches.designTopModule;
    entry.rootModules = caches.designRootModules;
    entry.fileScope = caches.designFileScope;
    entry.structureFingerprint = caches.designStructureFingerprint;
    entry.snapshotGeneration = caches.designSnapshotGeneration;
    entry.hierarchyValid = caches.designHierarchyValid;
    entry.topInferred = caches.designTopInferred;
    designHierarchyCacheByScope.insert(key, entry);
}

void NavigationManager::restoreDesignHierarchyCache()
{
    caches.clearDesignHierarchy();
    caches.designTopModule.clear();
    caches.designTopInferred = true;

    const QString key = designHierarchyCacheKey();
    if (key.isEmpty())
        return;

    const auto it = designHierarchyCacheByScope.constFind(key);
    if (it == designHierarchyCacheByScope.constEnd())
        return;

    const DesignHierarchyCacheEntry entry = it.value();
    caches.designHierarchy = entry.hierarchy;
    caches.designTopModule = entry.topModule;
    caches.designRootModules = entry.rootModules;
    caches.designFileScope = entry.fileScope;
    caches.designStructureFingerprint = entry.structureFingerprint;
    caches.designSnapshotGeneration = entry.snapshotGeneration;
    caches.designHierarchyValid = entry.hierarchyValid;
    caches.designTopInferred = entry.topInferred;
}

void NavigationManager::invalidateCurrentDesignHierarchyCache()
{
    const QString key = designHierarchyCacheKey();
    if (!key.isEmpty())
        designHierarchyCacheByScope.remove(key);
    // Keep the last report and its structural fingerprint available until the
    // next authoritative snapshot arrives. A symbol/presentation publication
    // can invalidate the transaction without changing the module-instance
    // graph; discarding the report here forced an unnecessary tree rebuild.
    caches.designHierarchyValid = false;
}
