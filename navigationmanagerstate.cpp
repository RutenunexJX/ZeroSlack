#include "navigationmanager.h"

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

void NavigationManager::NavigationCaches::clearAll()
{
    clearFileList();
    clearModuleHierarchy();
    clearSymbolOutline();
}
