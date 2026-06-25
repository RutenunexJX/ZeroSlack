#include "navigationmanager.h"

#include "navigationservice.h"
#include "tabmanager.h"
#include "workspacemanager.h"

bool NavigationManager::updateFileHierarchyData()
{
    if (!shouldRefreshCache())
        return false;

    const QStringList files = getSystemVerilogFiles();
    const bool changed = !caches.fileListValid || caches.fileList != files;
    caches.fileList = files;
    caches.fileListValid = true;
    if (changed)
        caches.fileHierarchyValid = false;
    return changed;
}

bool NavigationManager::updateModuleHierarchyData()
{
    if (!navigationService)
        return false;

    if (caches.moduleHierarchyValid
        && caches.moduleHierarchyFilter == context.searchFilter) {
        return false;
    }

    NavigationModuleQuery query;
    query.filter = context.searchFilter;
    caches.moduleHierarchy = navigationService->findModuleHierarchy(query);
    caches.moduleHierarchyFilter = context.searchFilter;
    caches.moduleHierarchyValid = true;
    return true;
}

bool NavigationManager::updateSymbolHierarchyData()
{
    if (!navigationService)
        return false;

    if (caches.symbolOutlineValid
        && caches.symbolOutlineFileName == context.currentFileName
        && caches.symbolOutlineFilter == context.searchFilter) {
        return false;
    }

    NavigationSymbolOutlineQuery query;
    query.fileName = context.currentFileName;
    query.filter = context.searchFilter;
    caches.symbolOutline = navigationService->findSymbolOutline(query);
    caches.symbolOutlineFileName = context.currentFileName;
    caches.symbolOutlineFilter = context.searchFilter;
    caches.symbolOutlineValid = true;
    return true;
}

bool NavigationManager::updateDesignHierarchyData(bool force)
{
    if (!navigationService)
        return false;

    const std::uint64_t snapshotRevision =
        navigationService->semanticSnapshotRevision();
    if (caches.designTopInferred || caches.designTopModule.isEmpty()) {
        const QString inferredTop = navigationService->inferDesignTopModule();
        if (caches.designTopModule != inferredTop) {
            caches.designTopModule = inferredTop;
            caches.designHierarchyValid = false;
        }
        caches.designTopInferred = true;
    }

    if (caches.designTopModule.isEmpty()) {
        const bool changed =
            force
            || !caches.designHierarchyValid
            || caches.designSnapshotGeneration != snapshotRevision
            || !caches.designHierarchy.topModule.isEmpty();
        caches.designHierarchy = {};
        caches.designHierarchy.snapshotGeneration = snapshotRevision;
        caches.designSnapshotGeneration = snapshotRevision;
        caches.designHierarchyValid = true;
        return changed;
    }

    if (!force && caches.designHierarchyValid
        && caches.designHierarchy.topModule == caches.designTopModule
        && caches.designSnapshotGeneration == snapshotRevision) {
        return false;
    }

    caches.designHierarchy = navigationService->findDesignHierarchy(caches.designTopModule);
    caches.designSnapshotGeneration = snapshotRevision;
    if (caches.designHierarchy.snapshotGeneration == 0)
        caches.designHierarchy.snapshotGeneration = snapshotRevision;
    caches.designHierarchyValid = true;
    return true;
}

bool NavigationManager::shouldRefreshCache() const
{
    // Workspace mode owns the file list.
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return !caches.fileListValid;
    }

    // Without a workspace, derive the file list from open tabs.
    if (connectedTabManager) {
        QStringList openFiles = connectedTabManager->getOpenSystemVerilogFiles();
        return !caches.fileListValid || caches.fileList != openFiles;
    }

    return !caches.fileListValid || !caches.fileList.isEmpty();
}

QStringList NavigationManager::getSystemVerilogFiles() const
{
    // Prefer workspace files.
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return connectedWorkspaceManager->getSystemVerilogFiles();
    }

    // Otherwise use open tab files.
    if (connectedTabManager) {
        return connectedTabManager->getOpenSystemVerilogFiles();
    }

    return QStringList();
}

QStringList NavigationManager::filterFiles(const QStringList& files, const QString& filter) const
{
    if (filter.isEmpty()) return files;

    QStringList filteredFiles;
    filteredFiles.reserve(files.size());

    for (const QString& file : files) {
        if (file.contains(filter, Qt::CaseInsensitive)) {
            filteredFiles.append(file);
        }
    }

    return filteredFiles;
}
