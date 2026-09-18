#ifndef NAVIGATIONPANECOORDINATOR_H
#define NAVIGATIONPANECOORDINATOR_H

#include <QDockWidget>
#include <QObject>
#include <QString>

class NavigationManager;
class NavigationWidget;
class TabManager;
class QWidget;
class WorkspaceManager;
class NavigationViewport;
class QVariantAnimation;
class QEvent;

class NavigationPaneCoordinator : public QObject
{
public:
    explicit NavigationPaneCoordinator(QWidget* parent);

    void attachNavigationManager(NavigationManager* manager);
    void connectNavigationInputs(TabManager* tabManager,
                                 WorkspaceManager* workspaceManager);
    void toggleVisible();
    void setExpanded(bool expanded, bool animate = true);
    bool isExpanded() const { return expanded; }
    bool isAnimating() const { return transitioning; }
    void setHeaderWidget(QWidget* header);
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
    bool eventFilter(QObject* watched, QEvent* event) override;

    QDockWidget* navigationDock = nullptr;
    NavigationViewport* viewport = nullptr;
    NavigationWidget* navigationWidget = nullptr;
    NavigationManager* navigationManager = nullptr;
    QVariantAnimation* widthAnimation = nullptr;
    int expandedWidth = 280;
    bool expanded = true;
    bool transitioning = false;
};

#endif // NAVIGATIONPANECOORDINATOR_H
