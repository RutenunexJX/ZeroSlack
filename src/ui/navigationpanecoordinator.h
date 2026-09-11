#ifndef NAVIGATIONPANECOORDINATOR_H
#define NAVIGATIONPANECOORDINATOR_H

#include <QDockWidget>
#include <QString>

class NavigationManager;
class NavigationWidget;
class TabManager;
class QWidget;
class WorkspaceManager;

class NavigationPaneCoordinator
{
public:
    explicit NavigationPaneCoordinator(QWidget* parent);

    void attachNavigationManager(NavigationManager* manager);
    void connectNavigationInputs(TabManager* tabManager,
                                 WorkspaceManager* workspaceManager);
    void toggleVisible();
    void showFiles();
    void showDesign();
    void showSearch();
    QString filesSearchQuery() const;
    QString designSearchQuery() const;
    void setSearchQueries(const QString& filesQuery,
                          const QString& designQuery);

    QDockWidget* dock() const { return navigationDock; }

private:
    void showDock();

    QDockWidget* navigationDock = nullptr;
    NavigationWidget* navigationWidget = nullptr;
    NavigationManager* navigationManager = nullptr;
};

#endif // NAVIGATIONPANECOORDINATOR_H
