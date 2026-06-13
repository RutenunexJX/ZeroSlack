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

void NavigationManager::updateModuleHierarchyData()
{
    if (!navigationService)
        return;

    NavigationModuleQuery query;
    query.filter = context.searchFilter;
    caches.moduleHierarchy = navigationService->findModuleHierarchy(query);
}

void NavigationManager::updateSymbolHierarchyData()
{
    if (!navigationService)
        return;

    NavigationSymbolOutlineQuery query;
    query.fileName = context.currentFileName;
    query.filter = context.searchFilter;
    caches.symbolOutline = navigationService->findSymbolOutline(query);
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
