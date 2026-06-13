#include "navigationmanager.h"

#include "navigationwidget.h"
#include "tabmanager.h"
#include "workspacemanager.h"

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
