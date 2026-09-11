#include "navigationpanecoordinator.h"

#include "navigationmanager.h"
#include "navigationwidget.h"

NavigationPaneCoordinator::NavigationPaneCoordinator(QWidget* parent)
{
    navigationWidget = new NavigationWidget(parent);

    navigationDock = new QDockWidget("Navigation", parent);
    navigationDock->setObjectName(QStringLiteral("navigationDock"));
    navigationDock->setWidget(navigationWidget);
    navigationDock->setFeatures(QDockWidget::DockWidgetClosable);
    navigationDock->setAllowedAreas(Qt::LeftDockWidgetArea);

    navigationDock->setMinimumWidth(200);
    navigationDock->setMaximumWidth(400);
    navigationWidget->setMinimumWidth(180);
}

void NavigationPaneCoordinator::attachNavigationManager(NavigationManager* manager)
{
    navigationManager = manager;
    if (manager)
        manager->setNavigationWidget(navigationWidget);
}

void NavigationPaneCoordinator::connectNavigationInputs(
    TabManager* tabManager,
    WorkspaceManager* workspaceManager)
{
    if (!navigationManager)
        return;

    navigationManager->connectToTabManager(tabManager);
    navigationManager->connectToWorkspaceManager(workspaceManager);
}

void NavigationPaneCoordinator::toggleVisible()
{
    if (!navigationDock)
        return;

    if (navigationDock->isVisible()) {
        navigationDock->hide();
        return;
    }

    showDock();
}

void NavigationPaneCoordinator::showFiles()
{
    if (navigationWidget)
        navigationWidget->setActiveTab(NavigationWidget::FileTab);
    showDock();
}

void NavigationPaneCoordinator::showDesign()
{
    if (navigationWidget)
        navigationWidget->setActiveTab(NavigationWidget::DesignTab);
    showDock();
}

void NavigationPaneCoordinator::showSearch()
{
    showDock();
    if (navigationWidget)
        navigationWidget->focusSearch();
}

QString NavigationPaneCoordinator::filesSearchQuery() const
{
    return navigationWidget
        ? navigationWidget->searchFilter(NavigationWidget::FileTab)
        : QString();
}

QString NavigationPaneCoordinator::designSearchQuery() const
{
    return navigationWidget
        ? navigationWidget->searchFilter(NavigationWidget::DesignTab)
        : QString();
}

void NavigationPaneCoordinator::setSearchQueries(
    const QString& filesQuery,
    const QString& designQuery)
{
    if (!navigationWidget)
        return;
    navigationWidget->setSearchFilter(NavigationWidget::FileTab,
                                      filesQuery);
    navigationWidget->setSearchFilter(NavigationWidget::DesignTab,
                                      designQuery);
}

void NavigationPaneCoordinator::showDock()
{
    if (!navigationDock)
        return;

    navigationDock->show();
    navigationDock->raise();
    navigationDock->activateWindow();
}
