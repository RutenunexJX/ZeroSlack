#include "navigationpanecoordinator.h"

#include "navigationmanager.h"
#include "navigationwidget.h"
#include "applicationthememanager.h"
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaNavigationBar.h"
#include "ElaDockWidget.h"
#endif

#include <QEvent>
#include <QApplication>
#include <QKeyEvent>
#include <QMainWindow>
#include <QLayout>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <QVBoxLayout>

class NavigationViewport final : public QWidget
{
public:
    explicit NavigationViewport(QWidget* parent = nullptr)
        : QWidget(parent), content(new QWidget(this))
    {
        setMinimumWidth(0);
        content->setObjectName(QStringLiteral("navigationSlidingContent"));
        contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(0);
    }

    void setContentWidth(int width)
    {
        contentWidth = width;
        content->setFixedWidth(width);
        updateContentGeometry();
    }

    void addBody(QWidget* body) { contentLayout->addWidget(body, 1); }
    void addHeader(QWidget* header) { contentLayout->insertWidget(0, header); }
    QSize sizeHint() const override { return QSize(contentWidth, 300); }
    QSize minimumSizeHint() const override { return QSize(0, 0); }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        updateContentGeometry();
    }

private:
    void updateContentGeometry()
    {
        content->setGeometry(width() - contentWidth, 0,
                             contentWidth, height());
    }

    QWidget* content = nullptr;
    QVBoxLayout* contentLayout = nullptr;
    int contentWidth = 280;
};

NavigationPaneCoordinator::NavigationPaneCoordinator(QWidget* parent)
    : QObject(parent)
{
#ifdef ZEROSLACK_ENABLE_ELA
    navigationDock = new ElaDockWidget("Navigation", parent);
#else
    navigationDock = new QDockWidget("Navigation", parent);
#endif
    navigationDock->setObjectName(QStringLiteral("navigationDock"));
    auto* emptyTitle = new QWidget(navigationDock);
    emptyTitle->setFixedHeight(0);
    emptyTitle->setMinimumWidth(0);
    navigationDock->setTitleBarWidget(emptyTitle);
    navigationDock->setFeatures(QDockWidget::DockWidgetClosable);
    navigationDock->setAllowedAreas(Qt::LeftDockWidgetArea);
#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        elaNavigationBar = new ElaNavigationBar(navigationDock);
        elaNavigationBar->setObjectName(QStringLiteral("navigationElaBar"));
        elaNavigationBar->setCustomContentWidthRange(200, 400);
        navigationWidget = new NavigationWidget;
        navigationWidget->setMinimumWidth(180);
        elaNavigationBar->setCustomContent(navigationWidget);
        elaNavigationBar->setNavigationBarWidth(280);
        navigationDock->setWidget(elaNavigationBar);
        connect(elaNavigationBar, &ElaNavigationBar::displayModeChanged, this,
                [this](ElaNavigationType::NavigationDisplayMode mode) {
            if (overlayParent) return;
            if (mode != ElaNavigationType::Minimal && !elaNavigationBar->isDisplayModeAnimating()) navigationDock->show();
        });
        connect(elaNavigationBar, &ElaNavigationBar::displayModeTransitionFinished, this,
                [this](ElaNavigationType::NavigationDisplayMode mode) {
            if (overlayParent) return;
            changingPlacement = true;
            if (mode == ElaNavigationType::Minimal) navigationDock->hide();
            else if (mode == ElaNavigationType::Maximal) {
                navigationDock->show();
                if (auto* window = qobject_cast<QMainWindow*>(navigationDock->parentWidget()))
                    window->resizeDocks({navigationDock}, {elaNavigationBar->getNavigationBarWidth()}, Qt::Horizontal);
            }
            changingPlacement = false;
        });
        connect(elaNavigationBar, &ElaNavigationBar::overlayTransitionFinished, this,
                [this](bool open) { if (!open) finishOverlay(); });
        qApp->installEventFilter(this);
        return;
    }
#endif
    viewport = new NavigationViewport(navigationDock);
    viewport->setContentWidth(expandedWidth);
    navigationWidget = new NavigationWidget(viewport);
    viewport->addBody(navigationWidget);
    navigationDock->setWidget(viewport);
    navigationDock->setMinimumWidth(200);
    navigationDock->setMaximumWidth(400);
    navigationWidget->setMinimumWidth(180);
    navigationDock->resize(expandedWidth, navigationDock->height());
    navigationDock->installEventFilter(this);

    widthAnimation = new QVariantAnimation(this);
    widthAnimation->setDuration(180);
    widthAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(widthAnimation, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                const int width = qMax(0, value.toInt());
                navigationDock->setMaximumWidth(width);
                if (auto* window = qobject_cast<QMainWindow*>(
                        navigationDock->parentWidget())) {
                    window->resizeDocks({navigationDock}, {qMax(1, width)},
                                        Qt::Horizontal);
                }
            });
    connect(widthAnimation, &QVariantAnimation::finished,
            this, [this]() {
                if (!expanded)
                    navigationDock->hide();
                navigationDock->setMaximumWidth(400);
                navigationDock->setMinimumWidth(200);
                if (expanded) {
                    if (auto* window = qobject_cast<QMainWindow*>(
                            navigationDock->parentWidget())) {
                        window->resizeDocks({navigationDock},
                                            {expandedWidth}, Qt::Horizontal);
                    }
                }
                transitioning = false;
            });
    connect(navigationDock, &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
                if (!transitioning)
                    expanded = visible;
            });
}

NavigationPaneCoordinator::~NavigationPaneCoordinator()
{
    qApp->removeEventFilter(this);
    finishOverlay();
}

void NavigationPaneCoordinator::finishOverlay()
{
#ifdef ZEROSLACK_ENABLE_ELA
    if (!navigationDock || !elaNavigationBar || !overlayParent) return;
    changingPlacement = true;
    const QRect area(0, 0, elaNavigationBar->getNavigationBarWidth(), overlayParent->height());
    overlayParent.clear();
    elaNavigationBar->setOverlayExpanded(false, area, false);
    navigationDock->setWidget(elaNavigationBar);
    navigationDock->hide();
    elaNavigationBar->show();
    changingPlacement = false;
#endif
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
    setExpanded(!isExpanded());
}

bool NavigationPaneCoordinator::isExpanded() const
{
    if (!navigationDock || !navigationWidget) return false;
#ifdef ZEROSLACK_ENABLE_ELA
    if (elaNavigationBar) return overlayParent ? elaNavigationBar->isOverlayExpanded()
        : elaNavigationBar->getDisplayMode() == ElaNavigationType::Maximal;
#endif
    return expanded;
}

bool NavigationPaneCoordinator::isAnimating() const
{
    if (!navigationDock || !navigationWidget) return false;
#ifdef ZEROSLACK_ENABLE_ELA
    if (elaNavigationBar) return elaNavigationBar->isOverlayAnimating() || elaNavigationBar->isDisplayModeAnimating();
#endif
    return transitioning;
}

void NavigationPaneCoordinator::setExpanded(bool open, bool animate)
{
    // An overlay is reparented into the central widget, which can be destroyed
    // before this sibling coordinator during window teardown or replacement.
    if (!navigationDock || !navigationWidget) return;
#ifdef ZEROSLACK_ENABLE_ELA
    if (elaNavigationBar) {
        auto* window = qobject_cast<QMainWindow*>(navigationDock->parentWidget());
        const bool useOverlay = overlayParent || (open && animate && window && window->isVisible()
            && window->width() < 850 && window->centralWidget() && navigationDock->isHidden());
        if (useOverlay) {
            if (!overlayParent) {
                changingPlacement = true;
                overlayParent = window->centralWidget();
                elaNavigationBar->setParent(overlayParent);
                navigationDock->hide();
                changingPlacement = false;
            }
            const QRect area(0, 0, qMin(elaNavigationBar->getNavigationBarWidth(), overlayParent->width()),
                             overlayParent->height());
            emit expandedChanged(open);
            elaNavigationBar->setOverlayExpanded(open, area, animate);
            return;
        }
        changingPlacement = true;
        if (open) navigationDock->show();
        elaNavigationBar->setDisplayMode(open ? ElaNavigationType::Maximal : ElaNavigationType::Minimal,
                                        animate && window && window->isVisible());
        if (open && (!animate || !elaNavigationBar->isDisplayModeAnimating())) navigationDock->show();
        if (!open && !elaNavigationBar->isDisplayModeAnimating()) navigationDock->hide();
        changingPlacement = false;
        emit expandedChanged(open);
        return;
    }
#endif
    if (!navigationDock
        || (expanded == open && !transitioning
            && navigationDock->isVisible() == open))
        return;
    if (widthAnimation->state() == QAbstractAnimation::Running)
        widthAnimation->stop();

    const int currentWidth = navigationDock->isVisible()
        ? navigationDock->width() : 0;
    if (navigationDock->isVisible() && !transitioning
        && currentWidth >= 180) {
        expandedWidth = qBound(200, currentWidth, 400);
        viewport->setContentWidth(expandedWidth);
    }
    expanded = open;
    auto* window = qobject_cast<QMainWindow*>(navigationDock->parentWidget());
    if (!animate || !window || !window->isVisible()) {
        transitioning = true;
        navigationDock->setMaximumWidth(400);
        navigationDock->setMinimumWidth(200);
        navigationDock->setVisible(open);
        if (open && window)
            window->resizeDocks({navigationDock}, {expandedWidth},
                                Qt::Horizontal);
        transitioning = false;
        return;
    }

    transitioning = true;
    navigationDock->setMinimumWidth(0);
    if (open) {
        navigationDock->setMaximumWidth(qMax(1, currentWidth));
        navigationDock->show();
        navigationDock->raise();
    }
    widthAnimation->setStartValue(open ? qMax(1, currentWidth)
                                       : currentWidth);
    widthAnimation->setEndValue(open ? expandedWidth : 0);
    widthAnimation->start();
}

void NavigationPaneCoordinator::setHeaderWidget(QWidget* header)
{
#ifdef ZEROSLACK_ENABLE_ELA
    if (elaNavigationBar) {
        elaNavigationBar->setCustomHeader(header);
        return;
    }
#endif
    if (viewport && header)
        viewport->addHeader(header);
}

bool NavigationPaneCoordinator::eventFilter(QObject* watched, QEvent* event)
{
    if (!navigationDock || !navigationWidget) return QObject::eventFilter(watched, event);
#ifdef ZEROSLACK_ENABLE_ELA
    if (elaNavigationBar && overlayParent && !changingPlacement) {
        auto* target = qobject_cast<QWidget*>(watched);
        if (watched == overlayParent && event->type() == QEvent::Resize) {
            elaNavigationBar->setOverlayExpanded(elaNavigationBar->isOverlayExpanded(),
                QRect(0, 0, qMin(elaNavigationBar->getNavigationBarWidth(), overlayParent->width()),
                      overlayParent->height()), false);
        } else if (target && target->window() == navigationDock->window()
                   && event->type() == QEvent::MouseButtonPress
                   && target != elaNavigationBar && !elaNavigationBar->isAncestorOf(target)) {
            setExpanded(false);
        } else if (target && target->window() == navigationDock->window()
                   && event->type() == QEvent::KeyPress
                   && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setExpanded(false);
            return true;
        }
    }
    if (elaNavigationBar && watched == navigationDock) {
        if (changingPlacement) return QObject::eventFilter(watched, event);
        // A zero-width dock reports visibilityChanged(false) even after show().
        // Observe explicit dock visibility, not the visible area or its parent.
        if (event->type() == QEvent::Show && !elaNavigationBar->isDisplayModeAnimating()) {
            if (overlayParent) {
                finishOverlay();
                changingPlacement = true;
                navigationDock->show();
                changingPlacement = false;
            }
            // restoreState() can assign a saved dock size before its first show.
            if (elaNavigationBar->getDisplayMode() == ElaNavigationType::Maximal
                && navigationDock->width() >= 200 && navigationDock->width() <= 400)
                elaNavigationBar->setNavigationBarWidth(navigationDock->width());
            elaNavigationBar->setDisplayMode(ElaNavigationType::Maximal, false);
            if (auto* window = qobject_cast<QMainWindow*>(navigationDock->parentWidget()))
                window->resizeDocks({navigationDock}, {elaNavigationBar->getNavigationBarWidth()}, Qt::Horizontal);
            emit expandedChanged(true);
        }
        else if (event->type() == QEvent::Hide && navigationDock->isHidden()) {
            elaNavigationBar->setDisplayMode(ElaNavigationType::Minimal, false);
            emit expandedChanged(false);
        }
        return QObject::eventFilter(watched, event);
    }
#endif
    if (watched == navigationDock && event->type() == QEvent::Resize
        && !transitioning && navigationDock->isVisible()) {
        const int width = navigationDock->width();
        if (width >= 200 && width <= 400 && width != expandedWidth) {
            expandedWidth = width;
            viewport->setContentWidth(width);
        }
    }
    return QObject::eventFilter(watched, event);
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
    setExpanded(true, false);
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
    setExpanded(true);
    if (navigationDock)
        navigationDock->activateWindow();
}
