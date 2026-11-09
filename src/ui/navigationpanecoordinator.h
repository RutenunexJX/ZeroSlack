#ifndef NAVIGATIONPANECOORDINATOR_H
#define NAVIGATIONPANECOORDINATOR_H

#include <QDockWidget>
#include <QObject>
#include <QPointer>
#include <QString>
#include "zeroslackexport.h"

class EditorSplitController;
class QTimer;
class NavigationManager;
class NavigationWidget;
class TabManager;
class QWidget;
class WorkspaceManager;
class NavigationViewport;
class QEvent;
class ElaNavigationBar;

class ZEROSLACK_API NavigationPaneCoordinator : public QObject
{
    Q_OBJECT
public:
    explicit NavigationPaneCoordinator(QWidget* parent);
    ~NavigationPaneCoordinator() override;

    void attachNavigationManager(NavigationManager* manager);
    void connectNavigationInputs(TabManager* tabManager,
                                 WorkspaceManager* workspaceManager);
    void toggleVisible();
    void setExpanded(bool expanded, bool animate = true);
    bool isExpanded() const;
    bool isAnimating() const;
    void setHeaderWidget(QWidget* header);
    void showFiles();
    void showDesign();
    void showSearch();
    int activeTab() const;
    void setActiveTab(int tab, bool activate = true);
    QString filesSearchQuery() const;
    QString designSearchQuery() const;
    void setSearchQueries(const QString& filesQuery,
                          const QString& designQuery);

    QDockWidget* dock() const { return navigationDock; }

signals:
    void expandedChanged(bool expanded);

private:
    void showDock();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QPointer<QDockWidget> navigationDock;
    QPointer<NavigationViewport> viewport;
    QPointer<ElaNavigationBar> elaNavigationBar;
    QPointer<QWidget> overlayParent;
    bool changingPlacement = false;
    void finishOverlay();
    QPointer<NavigationWidget> navigationWidget;
    NavigationManager* navigationManager = nullptr;
    QPointer<EditorSplitController> pageRegistry;
    QTimer* tabHoverTimer = nullptr;
    bool dragOpened = false;
    QPointer<QWidget> draggedPage;
    int hoverTicks = 0;
    int expandedWidth = 280;
    bool expanded = true;
    bool transitioning = false;
};

#endif // NAVIGATIONPANECOORDINATOR_H
