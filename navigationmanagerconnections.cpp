#include "navigationmanager.h"

#include "navigationservice.h"
#include "navigationwidget.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QAction>
#include <QMenu>

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
                    // Opening an existing workspace file does not change the
                    // file hierarchy; activeDocumentChanged handles highlight.
                    highlightCurrentFileInTree();
                });

        connect(connectedTabManager, &TabManager::tabClosed,
                this, [this](const QString&) {
                    if (connectedWorkspaceManager
                        && connectedWorkspaceManager->isWorkspaceOpen()) {
                        return;
                    }

                    caches.clearFileList();
                    if (currentView == FileHierarchyView)
                        refreshFileHierarchy();
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
        connect(connectedWorkspaceManager,
                &WorkspaceManager::workspaceActivated,
                this,
                [this](int, const QString&, const QString& path) {
                    onWorkspaceChanged(path);
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::workspaceClosed,
                this, [this]() {
                    if (connectedWorkspaceManager
                        && connectedWorkspaceManager->isWorkspaceOpen()) {
                        return;
                    }
                    context.clearCurrentWorkspacePath();
                    caches.clearFileList();
                    caches.clearDesignHierarchy();
                    designHierarchyCacheByScope.clear();
                    caches.designTopModule.clear();
                    caches.designTopInferred = true;
                    if (currentView == DesignHierarchyView) {
                        if (navigationWidget)
                            navigationWidget->clearDesignHierarchy();
                        return;
                    }
                    refreshCurrentView();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::filesScanned,
                this, [this](const QStringList&) {
                    caches.clearFileList();
                    invalidateCurrentDesignHierarchyCache();
                    if (currentView == FileHierarchyView)
                        refreshFileHierarchy();
                    else if (currentView == DesignHierarchyView)
                        refreshDesignHierarchy();
                });

        connect(connectedWorkspaceManager, &WorkspaceManager::fileChanged,
                this, [this](const QString&) {
                    if (currentView == DesignHierarchyView) {
                        invalidateCurrentDesignHierarchyCache();
                        refreshDesignHierarchy();
                    }
                });
    }
}

void NavigationManager::onFileTreeDoubleClicked(const QString& filePath)
{
    navigateToFile(filePath);
}

void NavigationManager::onFileContextMenuRequested(
    const QString& filePath,
    const QPoint& globalPos)
{
    if (!navigationService)
        return;
    const QStringList modules = navigationService->modulesDefinedInFile(filePath);
    if (modules.isEmpty())
        return;

    QMenu menu;
    if (modules.size() == 1) {
        QAction* setTopAction = menu.addAction(QStringLiteral("Set as Design Top"));
        connect(setTopAction, &QAction::triggered, this, [this, modules]() {
            setDesignTop(modules.first());
        });
    } else {
        QMenu* topMenu = menu.addMenu(QStringLiteral("Set as Design Top"));
        for (const QString& moduleName : modules) {
            QAction* action = topMenu->addAction(moduleName);
            connect(action, &QAction::triggered, this, [this, moduleName]() {
                setDesignTop(moduleName);
            });
        }
    }
    menu.exec(globalPos);
}

void NavigationManager::onDesignNodeContextMenuRequested(
    const DesignHierarchyNode& node,
    const QPoint& globalPos)
{
    QMenu menu;
    QAction* instantiationAction =
        menu.addAction(QStringLiteral("Go to Instantiation"));
    instantiationAction->setEnabled(!node.isTop && !node.instanceFile.isEmpty());
    connect(instantiationAction, &QAction::triggered, this, [this, node]() {
        navigateToDesignNodeFile(node.instanceFile, node.instanceLine, node);
    });

    QAction* definitionAction =
        menu.addAction(QStringLiteral("Go to Module Definition"));
    definitionAction->setEnabled(!node.definitionFile.isEmpty());
    connect(definitionAction, &QAction::triggered, this, [this, node]() {
        navigateToDesignNodeFile(node.definitionFile, node.definitionLine, node);
    });

    menu.addSeparator();
    QAction* setTopAction = menu.addAction(QStringLiteral("Set as Design Top"));
    setTopAction->setEnabled(!node.moduleType.isEmpty());
    connect(setTopAction, &QAction::triggered, this, [this, node]() {
        setDesignTop(node.moduleType);
    });
    menu.exec(globalPos);
}

void NavigationManager::onDesignNodeDoubleClicked(const DesignHierarchyNode& node)
{
    if (!node.definitionFile.isEmpty()) {
        navigateToDesignNodeFile(node.definitionFile,
                                 node.definitionLine,
                                 node);
        return;
    }
    if (!node.instanceFile.isEmpty())
        navigateToDesignNodeFile(node.instanceFile,
                                 node.instanceLine,
                                 node);
}

void NavigationManager::navigateToDesignNodeFile(
    const QString& filePath,
    int lineNumber,
    const DesignHierarchyNode& node)
{
    if (filePath.isEmpty())
        return;

    HierarchyInstanceContext instanceContext;
    instanceContext.workspacePath = context.currentWorkspacePath;
    instanceContext.activeTopModule = node.rootModule;
    instanceContext.instancePath = node.instancePath;
    emit instanceNavigationRequested(filePath,
                                     lineNumber,
                                     instanceContext);
}

void NavigationManager::setupConnections()
{
    if (!navigationWidget) return;

    // Wire NavigationWidget signals.
    connect(navigationWidget, SIGNAL(fileDoubleClicked(QString)),
            this, SLOT(onFileTreeDoubleClicked(QString)));

    connect(navigationWidget,
            &NavigationWidget::fileContextMenuRequested,
            this,
            &NavigationManager::onFileContextMenuRequested);

    connect(navigationWidget,
            &NavigationWidget::designNodeContextMenuRequested,
            this,
            &NavigationManager::onDesignNodeContextMenuRequested);

    connect(navigationWidget,
            &NavigationWidget::designNodeDoubleClicked,
            this,
            &NavigationManager::onDesignNodeDoubleClicked);

    connect(navigationWidget,
            &NavigationWidget::clearDesignTopRequested,
            this,
            &NavigationManager::clearDesignTop);

    connect(navigationWidget,
            &NavigationWidget::refreshDesignHierarchyRequested,
            this,
            [this]() {
                refreshDesignHierarchy(true);
            });

    connect(navigationWidget, &NavigationWidget::viewChanged,
            this, &NavigationManager::onViewChanged);

    connect(navigationWidget, &NavigationWidget::searchFilterChanged,
            this, &NavigationManager::setSearchFilter);
}
