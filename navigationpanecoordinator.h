#ifndef NAVIGATIONPANECOORDINATOR_H
#define NAVIGATIONPANECOORDINATOR_H

#include <QDockWidget>

class NavigationManager;
class NavigationWidget;
class QWidget;

class NavigationPaneCoordinator
{
public:
    explicit NavigationPaneCoordinator(QWidget* parent);

    void attachNavigationManager(NavigationManager* manager);
    void toggleVisible();

    QDockWidget* dock() const { return navigationDock; }
    NavigationWidget* widget() const { return navigationWidget; }

private:
    QDockWidget* navigationDock = nullptr;
    NavigationWidget* navigationWidget = nullptr;
};

#endif // NAVIGATIONPANECOORDINATOR_H
