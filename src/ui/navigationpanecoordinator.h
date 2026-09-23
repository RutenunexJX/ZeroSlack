#ifndef NAVIGATIONPANECOORDINATOR_H
#define NAVIGATIONPANECOORDINATOR_H

#include <QDockWidget>
#include <QObject>
#include <QPointer>
#include <QString>
#include "zeroslackexport.h"

class NavigationManager;
class NavigationWidget;
class TabManager;
class QWidget;
class WorkspaceManager;
class NavigationViewport;
class QVariantAnimation;
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

    QDockWidget* navigationDock = nullptr;
    NavigationViewport* viewport = nullptr;
    ElaNavigationBar* elaNavigationBar = nullptr;
    QPointer<QWidget> overlayParent;
    bool changingPlacement = false;
    void finishOverlay();
    NavigationWidget* navigationWidget = nullptr;
    NavigationManager* navigationManager = nullptr;
    QVariantAnimation* widthAnimation = nullptr;
    int expandedWidth = 280;
    bool expanded = true;
    bool transitioning = false;
};

#endif // NAVIGATIONPANECOORDINATOR_H
