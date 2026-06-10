#ifndef NAVIGATIONPANECOORDINATOR_H
#define NAVIGATIONPANECOORDINATOR_H

#include <QDockWidget>

class NavigationManager;
class NavigationWidget;
class SymbolAnalyzer;
class TabManager;
class QWidget;
class WorkspaceManager;

class NavigationPaneCoordinator
{
public:
    explicit NavigationPaneCoordinator(QWidget* parent);

    void attachNavigationManager(NavigationManager* manager);
    void connectNavigationInputs(TabManager* tabManager,
                                 WorkspaceManager* workspaceManager,
                                 SymbolAnalyzer* symbolAnalyzer);
    void toggleVisible();

    QDockWidget* dock() const { return navigationDock; }
    NavigationWidget* widget() const { return navigationWidget; }

private:
    QDockWidget* navigationDock = nullptr;
    NavigationWidget* navigationWidget = nullptr;
    NavigationManager* navigationManager = nullptr;
};

#endif // NAVIGATIONPANECOORDINATOR_H
