#include "navigationmanager.h"
#include "navigationservice.h"
#include "navigationwidget.h"

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent)
{
    navigationService = NavigationService::getInstance();
    caches.reserveDefaults();
}

NavigationManager::~NavigationManager()
{
}

void NavigationManager::setNavigationWidget(NavigationWidget* widget)
{
    if (navigationWidget == widget) return;

    navigationWidget = widget;
    if (navigationWidget) {
        setupConnections();
        refreshCurrentView();
    }
}

void NavigationManager::setNavigationService(NavigationService* service)
{
    navigationService = service ? service : NavigationService::getInstance();
    caches.clearModuleHierarchy();
    caches.clearSymbolOutline();
    refreshCurrentView();
}

void NavigationManager::refreshFileHierarchy()
{
    if (!shouldRefreshCache()) {
        caches.clearFileList();
    }

    updateFileHierarchyData();

    if (navigationWidget) {
        navigationWidget->updateFileHierarchy(caches.fileList);
    }

    emit dataRefreshed(FileHierarchyView);
}

void NavigationManager::refreshModuleHierarchy()
{
    updateModuleHierarchyData();

    if (navigationWidget) {
        navigationWidget->updateModuleHierarchy(caches.moduleHierarchy);
    }

    emit dataRefreshed(ModuleHierarchyView);
}

void NavigationManager::refreshSymbolHierarchy()
{
    updateSymbolHierarchyData();

    if (navigationWidget) {
        navigationWidget->updateSymbolHierarchy(caches.symbolOutline);
    }

    emit dataRefreshed(SymbolHierarchyView);
}

void NavigationManager::refreshCurrentView()
{
    switch (currentView) {
    case FileHierarchyView:
        refreshFileHierarchy();
        break;
    case ModuleHierarchyView:
        refreshModuleHierarchy();
        break;
    case SymbolHierarchyView:
        refreshSymbolHierarchy();
        break;
    }
}

void NavigationManager::navigateToFile(const QString& filePath, int lineNumber)
{
    if (filePath.isEmpty()) return;

    emit navigationRequested(filePath, lineNumber);
}

void NavigationManager::navigateToSymbol(const sym_list::SymbolInfo& symbol)
{
    emit symbolNavigationRequested(symbol);
}

void NavigationManager::navigateToModule(const QString& moduleName)
{
    if (moduleName.isEmpty()) return;
    if (!navigationService) return;

    const NavigationModuleTarget target =
        navigationService->resolveModuleTarget(moduleName);
    if (target.found)
        navigateToSymbol(target.symbol);
}

void NavigationManager::setSearchFilter(const QString& filter)
{
    context.setSearchFilter(filter);

    // Reapply the current search filter.
    refreshCurrentView();
}

void NavigationManager::clearSearchFilter()
{
    context.clearSearchFilter();
    refreshCurrentView();
}

void NavigationManager::highlightCurrentFileInTree()
{
    if (!navigationWidget || context.currentFileName.isEmpty()) return;

    navigationWidget->highlightFile(context.currentFileName);
}

void NavigationManager::syncWithActiveEditor()
{
    highlightCurrentFileInTree();

    // Keep the symbol view aligned with the active editor.
    if (currentView == SymbolHierarchyView
        && !context.currentFileName.isEmpty()) {
        refreshSymbolHierarchy();
    }
}

void NavigationManager::onTabChanged(const QString& fileName)
{
    context.setCurrentFileName(fileName);
    syncWithActiveEditor();
}

void NavigationManager::onWorkspaceChanged(const QString& workspacePath)
{
    context.setCurrentWorkspacePath(workspacePath);

    // Clear caches and refresh the active view.
    caches.clearAll();

    refreshCurrentView();
}

void NavigationManager::onViewChanged(int index)
{
    setActiveView(static_cast<NavigationView>(index));
}

void NavigationManager::onSearchFilterChanged(const QString& filter)
{
    setSearchFilter(filter);
}

void NavigationManager::onSymbolAnalysisCompleted(const QString& fileName, int symbolCount)
{
    Q_UNUSED(symbolCount)

    // Symbol analysis can update either the full module graph or the current symbol outline.
    switch (currentView) {
    case FileHierarchyView:
        // The file list is unchanged.
        break;
    case ModuleHierarchyView: {
        caches.clearModuleHierarchy();
        refreshModuleHierarchy();
        break;
    }
    case SymbolHierarchyView:
        // The symbol view follows the current file.
        if (context.currentFileName == fileName) {
            caches.clearSymbolOutline();
            refreshSymbolHierarchy();
        }
        break;
    }
}

void NavigationManager::onBatchSymbolAnalysisCompleted(
    int filesAnalyzed,
    int totalSymbols)
{
    Q_UNUSED(filesAnalyzed)
    Q_UNUSED(totalSymbols)

    // Batch analysis can change module hierarchy and symbol outline data.
    if (currentView == ModuleHierarchyView || currentView == SymbolHierarchyView) {
        caches.clearSymbolOutline();
        caches.clearModuleHierarchy();
        refreshCurrentView();
    }
}

void NavigationManager::setActiveView(NavigationView view)
{
    if (currentView == view) return;

    currentView = view;
    emit viewChanged(currentView);

    // Refresh the newly active view.
    refreshCurrentView();
}
