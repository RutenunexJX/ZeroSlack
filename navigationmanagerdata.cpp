#include "navigationmanager.h"

#include "navigationservice.h"
#include "tabmanager.h"
#include "workspacemanager.h"

void NavigationManager::updateFileHierarchyData()
{
    if (!caches.fileList.isEmpty() && !shouldRefreshCache()) {
        return; // Use cached data.
    }

    caches.fileList = getSystemVerilogFiles();

    // Apply the search filter.
    if (!context.searchFilter.isEmpty()) {
        caches.fileList = filterFiles(caches.fileList, context.searchFilter);
    }
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

bool NavigationManager::shouldRefreshCache() const
{
    // Workspace mode owns the file list.
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return caches.fileList.isEmpty();
    }

    // Without a workspace, derive the file list from open tabs.
    if (connectedTabManager) {
        QStringList openFiles = connectedTabManager->getOpenSystemVerilogFiles();
        return caches.fileList != openFiles;
    }

    return true;
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
