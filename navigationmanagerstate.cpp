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

void NavigationManager::NavigationContext::clearCurrentFileName()
{
    currentFileName.clear();
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

void NavigationManager::NavigationContext::setSearchFilter(const QString& filter)
{
    searchFilter = filter.trimmed();
}

void NavigationManager::NavigationContext::clearSearchFilter()
{
    searchFilter.clear();
}

void NavigationManager::NavigationCaches::reserveDefaults()
{
    fileList.reserve(100);
    moduleHierarchy.reserve(50);
    symbolOutline.reserve(10);
}

void NavigationManager::NavigationCaches::clearFileList()
{
    fileList.clear();
    fileHierarchyFilter.clear();
    fileListValid = false;
    fileHierarchyValid = false;
}

void NavigationManager::NavigationCaches::clearModuleHierarchy()
{
    moduleHierarchy.clear();
    moduleHierarchyFilter.clear();
    moduleHierarchyValid = false;
}

void NavigationManager::NavigationCaches::clearSymbolOutline()
{
    symbolOutline.clear();
    symbolOutlineFileName.clear();
    symbolOutlineFilter.clear();
    symbolOutlineValid = false;
}

void NavigationManager::NavigationCaches::clearDesignHierarchy()
{
    designHierarchy = {};
    designRootModules.clear();
    designFileScope.clear();
    designSnapshotGeneration = 0;
    designHierarchyValid = false;
}

void NavigationManager::NavigationCaches::clearAll()
{
    clearFileList();
    clearModuleHierarchy();
    clearSymbolOutline();
    clearDesignHierarchy();
    designTopModule.clear();
    designTopInferred = true;
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
    caches.designSnapshotGeneration = entry.snapshotGeneration;
    caches.designHierarchyValid = entry.hierarchyValid;
    caches.designTopInferred = entry.topInferred;

    if (navigationService && caches.designHierarchyValid) {
        const std::uint64_t snapshotRevision =
            navigationService->semanticSnapshotRevision();
        caches.designSnapshotGeneration = snapshotRevision;
        caches.designHierarchy.snapshotGeneration = snapshotRevision;
    }
}

void NavigationManager::invalidateCurrentDesignHierarchyCache()
{
    const QString key = designHierarchyCacheKey();
    if (!key.isEmpty())
        designHierarchyCacheByScope.remove(key);
    caches.clearDesignHierarchy();
}
