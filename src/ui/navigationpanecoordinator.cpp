#include "navigationpanecoordinator.h"

#include "navigationmanager.h"
#include "navigationwidget.h"

#include <QEvent>
#include <QMainWindow>
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
    navigationDock = new QDockWidget("Navigation", parent);
    navigationDock->setObjectName(QStringLiteral("navigationDock"));
    viewport = new NavigationViewport(navigationDock);
    viewport->setContentWidth(expandedWidth);
    navigationWidget = new NavigationWidget(viewport);
    viewport->addBody(navigationWidget);
    navigationDock->setWidget(viewport);
    auto* emptyTitle = new QWidget(navigationDock);
    emptyTitle->setFixedHeight(0);
    emptyTitle->setMinimumWidth(0);
    navigationDock->setTitleBarWidget(emptyTitle);
    navigationDock->setFeatures(QDockWidget::DockWidgetClosable);
    navigationDock->setAllowedAreas(Qt::LeftDockWidgetArea);

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
    setExpanded(!expanded);
}

void NavigationPaneCoordinator::setExpanded(bool open, bool animate)
{
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
        transitioning = false;
        navigationDock->setMaximumWidth(400);
        navigationDock->setMinimumWidth(200);
        navigationDock->setVisible(open);
        if (open && window)
            window->resizeDocks({navigationDock}, {expandedWidth},
                                Qt::Horizontal);
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
    if (viewport && header)
        viewport->addHeader(header);
}

bool NavigationPaneCoordinator::eventFilter(QObject* watched, QEvent* event)
{
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
    setExpanded(true);
    if (navigationDock)
        navigationDock->activateWindow();
}
