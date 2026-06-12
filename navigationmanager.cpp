#include "navigationmanager.h"
#include "navigationwidget.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "navigationservice.h"
#include <utility>

NavigationManager::NavigationManager(QObject *parent)
    : QObject(parent)
{
    navigationService = NavigationService::getInstance();
    caches.reserveDefaults();
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
}

void NavigationManager::NavigationCaches::clearModuleHierarchy()
{
    moduleHierarchy.clear();
}

void NavigationManager::NavigationCaches::clearSymbolOutline()
{
    symbolOutline.clear();
}

void NavigationManager::NavigationCaches::clearAll()
{
    clearFileList();
    clearModuleHierarchy();
    clearSymbolOutline();
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

void NavigationManager::connectToTabManager(TabManager* tabManager)
{
    if (connectedTabManager == tabManager) return;

    // Drop connections from the previous manager.
    if (connectedTabManager) {
        disconnect(connectedTabManager, nullptr, this, nullptr);
    }

    connectedTabManager = tabManager;

    if (connectedTabManager) {
        // Wire tab lifecycle events into navigation refreshes.
        connect(connectedTabManager, &TabManager::activeDocumentChanged,
                this, [this](const DocumentSnapshot& document) {
                    context.setCurrentFileName(document.fileName);
                    onTabChanged(context.currentFileName);
                });

        connect(connectedTabManager, &TabManager::tabCreated,
                this, [this](MyCodeEditor*) {
                    // New tabs can change the file tree.
                    if (currentView == FileHierarchyView) {
                        refreshFileHierarchy();
                    }
                });

        connect(connectedTabManager, &TabManager::tabClosed,
                this, [this](const QString&) {
                    // Closed tabs can remove entries from the current view.
                    refreshCurrentView();
                });
    }
}

void NavigationManager::connectToWorkspaceManager(WorkspaceManager* workspaceManager)
{
    if (connectedWorkspaceManager == workspaceManager) return;

    // Drop connections from the previous manager.
    if (connectedWorkspaceManager) {
        disconnect(connectedWorkspaceManager, nullptr, this, nullptr);
    }

    connectedWorkspaceManager = workspaceManager;

    if (connectedWorkspaceManager) {
        // Wire workspace events into navigation refreshes.
        connect(connectedWorkspaceManager, &WorkspaceManager::workspaceOpened,
                this, &NavigationManager::onWorkspaceChanged);

        connect(connectedWorkspaceManager, &WorkspaceManager::workspaceClosed,
                this, [this]() {
                    context.clearCurrentWorkspacePath();
                    caches.clearFileList();
                    caches.clearModuleHierarchy();
                    refreshCurrentView();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::filesScanned,
                this, [this](const QStringList&) {
                    // A rescan can affect every navigation view.
                    caches.clearFileList();
                    caches.clearModuleHierarchy();
                    refreshCurrentView();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::fileChanged,
                this, [this](const QString& filePath) {
                    // Module hierarchy depends on global relationship edges, so refresh it as a whole.
                    if (currentView == ModuleHierarchyView) {
                        caches.clearModuleHierarchy();
                        refreshModuleHierarchy();
                    } else if (currentView == SymbolHierarchyView
                               && context.currentFileName == filePath) {
                        caches.clearSymbolOutline();
                        refreshSymbolHierarchy();
                    }
                });
    }
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

void NavigationManager::onFileTreeDoubleClicked(const QString& filePath)
{
    navigateToFile(filePath);
}

void NavigationManager::onSymbolTreeDoubleClicked(const sym_list::SymbolInfo& symbol)
{
    navigateToSymbol(symbol);
}

void NavigationManager::onModuleTreeDoubleClicked(const QString& moduleName)
{
    navigateToModule(moduleName);
}

void NavigationManager::setupConnections()
{
    if (!navigationWidget) return;

    // Wire NavigationWidget signals.
    connect(navigationWidget, SIGNAL(fileDoubleClicked(QString)),
            this, SLOT(onFileTreeDoubleClicked(QString)));

    connect(navigationWidget, SIGNAL(symbolDoubleClicked(sym_list::SymbolInfo)),
            this, SLOT(onSymbolTreeDoubleClicked(sym_list::SymbolInfo)));

    connect(navigationWidget, SIGNAL(moduleDoubleClicked(QString)),
            this, SLOT(onModuleTreeDoubleClicked(QString)));

    connect(navigationWidget, SIGNAL(viewChanged(int)),
            this, SLOT(onViewChanged(int)));

    connect(navigationWidget, &NavigationWidget::viewChanged,
            this, [this](int index) {
                setActiveView(static_cast<NavigationView>(index));
            });

    connect(navigationWidget, &NavigationWidget::searchFilterChanged,
            this, &NavigationManager::setSearchFilter);
}

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
