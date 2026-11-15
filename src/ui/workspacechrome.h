#include "uicontrols.h"
#include "uiwindowchrome.h"
#include <memory>
#pragma once
#include <QApplication>
#include "windowsnapchrome.h"
#include "navigationpanecoordinator.h"
#include "workspaceswitcher.h"
#include "compactlayout.h"
#include <QDockWidget>
#include "roundedicons.h"
#include "uitypography.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPointer>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QWindow>
#include <functional>

class WorkspaceChrome final : public QObject {
public:
    WorkspaceChrome(QMainWindow* host, NavigationPaneCoordinator* navigationPane,
                    std::function<void()> settings, WorkspaceSwitcher* workspaces = nullptr,
                    QWidget* accessory = nullptr, QWidget* modes = nullptr,
                    std::function<QWidget*()> editorRegion = {})
        : QObject(host), window(host), modeGroup(modes), editorRegion(std::move(editorRegion)) {
        QDockWidget* navigation = navigationPane
            ? navigationPane->dock() : nullptr;
        host->setWindowFlag(Qt::FramelessWindowHint);
        host->setContentsMargins(4, 40, 4, 4);
        // Keep menu actions registered so hiding the menu does not remove shortcuts.
        auto* commands = UiControls::menu(host);
        for (auto* action : host->menuBar()->actions()) {
            commands->addAction(action);
            if (action->menu()) registerActions(action->menu());
        }
        host->menuBar()->hide();
        auto* header = new QFrame(navigation ? static_cast<QWidget*>(navigation) : host);
        if (!navigation) header->hide();
        header->setObjectName(QStringLiteral("projectSidebarHeader"));
        auto* headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(8, 8, 8, 8);
        headerLayout->setSpacing(4);
        auto* controls = new QHBoxLayout;
        headerLayout->addLayout(controls);
        controls->setContentsMargins(0, 0, 0, 0);
        controls->setSpacing(6);
        auto* project = UiControls::railButton(header);
        project->setObjectName(QStringLiteral("projectRailButton"));
        project->setIcon(RoundedIcons::icon(RoundedIcons::Workspaces));
        project->setIconSize(QSize(RoundedIcons::SidebarSize, RoundedIcons::SidebarSize));
        project->setToolButtonStyle(Qt::ToolButtonIconOnly);
        UiTypography::apply(project, UiTypography::Role::Body);
        project->setToolTip(tr("Project commands"));
        project->setAccessibleName(tr("Project"));
        project->setMenu(commands);
        project->setPopupMode(QToolButton::InstantPopup);
        project->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(project, &QWidget::customContextMenuRequested, commands,
            [commands, project](QPoint point) { commands->popup(project->mapToGlobal(point)); });
        controls->addWidget(project);
        auto* setting = UiControls::railButton(header);
        settingsButton = setting;
        setting->setObjectName(QStringLiteral("settingsRailButton"));
        settingsButton->setIcon(RoundedIcons::icon(RoundedIcons::Settings));
        setting->setIconSize(QSize(RoundedIcons::SidebarSize, RoundedIcons::SidebarSize));
        setting->setToolTip(tr("Settings"));
        setting->setAccessibleName(tr("Settings"));
        connect(setting, &QToolButton::clicked, host, std::move(settings));
        controls->addWidget(setting);
        controls->addStretch();
        auto* collapse = UiControls::railButton(header);
        collapse->setObjectName(QStringLiteral("collapseProjectSidebarButton"));
        collapse->setIcon(RoundedIcons::icon(RoundedIcons::Sidebar));
        collapse->setIconSize(QSize(RoundedIcons::SidebarSize, RoundedIcons::SidebarSize));
        collapse->setToolTip(tr("Collapse sidebar (Ctrl+1)"));
        collapse->setAccessibleName(tr("Collapse sidebar"));
        controls->addWidget(collapse);
        if (navigation) {
            navigationPane->setHeaderWidget(header);
            connect(collapse, &QToolButton::clicked, navigationPane,
                    [navigationPane]() {
                        navigationPane->setExpanded(false);
                    });
        }
        workspaceStrip = workspaces ? workspaces->createStrip(host) : nullptr;
        const auto titleBar = UiWindowChrome::createTitleBar(host, workspaceStrip, nullptr, true);
        title = titleBar.widget;
        titleLabel = titleBar.label;
        maximizeButton = titleBar.maximize;
        windowButtons = {titleBar.minimize, titleBar.maximize, titleBar.close};
        if (workspaceStrip) {
            auto* workspaceRow = qobject_cast<QHBoxLayout*>(workspaceStrip->layout());
            workspaceRow->insertWidget(0, titleBar.sidebar);
            // Keep a useful tab viewport; additional workspaces use its existing scroll buttons.
            if (auto* tabs=workspaceStrip->findChild<QTabBar*>(QStringLiteral("workspaceIconTabs")))
                tabs->setMaximumWidth(220);
        }
        if (modeGroup) {
            modeGroup->setParent(title);
            modeGroup->show();
            markGroup(modeGroup);
        }
        if (accessory) {
            auto* row=qobject_cast<QHBoxLayout*>(accessory->layout());
            if (workspaceStrip) row->insertWidget(1, workspaceStrip);
            for (int i=0;i<row->count();++i) if(auto* group=row->itemAt(i)->widget()) {
                group->setProperty("titlebarInteractiveGroup",true);
                if (group==workspaceStrip || group->findChild<QAbstractButton*>(QStringLiteral("startSimulation")))
                    markGroup(group);
            }
            titleAccessory=new CompactOverflowToolbar(row,QStringLiteral("titlebar"),title);
            auto* groups=qobject_cast<QHBoxLayout*>(titleAccessory->layout());
            groups->setSpacing(12);
            groups->addStretch(1);
            titleAccessory->show();
            accessory->deleteLater();
        }
        updateMaximizeIcon();
        host->setContentsMargins(4, title->height() + 4, 4, 4);
        updateTitleTheme();
        connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
                this, [this](ThemeMode) { updateTitleTheme(); });
        auto* expand = titleBar.sidebar;
        expand->setVisible(navigation && navigation->isHidden());
        if (navigation) {
            connect(expand, &QToolButton::clicked, navigationPane,
                    [navigationPane]() {
                navigationPane->setExpanded(true);
            });
            connect(navigationPane, &NavigationPaneCoordinator::expandedChanged, expand,
                    [expand](bool visible) { expand->setVisible(!visible); });
        }
        updateTitleGeometry();
        title->show();
        title->raise();
        qApp->installEventFilter(this);
        new WindowSnapChrome(host, maximizeButton);
    }
    ~WorkspaceChrome() override { qApp->removeEventFilter(this); }
protected:
    bool eventFilter(QObject* target, QEvent* event) override {
        auto* widget = qobject_cast<QWidget*>(target);
        // QWidget closes the native window after QMainWindow's layout is destroyed.
        if (!widget || widget->window() != window || !qobject_cast<QMainWindow*>(window)) return false;
        if (target == window && event->type() == QEvent::WindowStateChange)
            updateMaximizeIcon();
        if (event->type()==QEvent::Resize || event->type()==QEvent::Move
            || event->type()==QEvent::Show || event->type()==QEvent::Hide
            || event->type()==QEvent::ParentChange || event->type()==QEvent::LayoutRequest) {
            auto* anchor=alignmentAnchor();
            if (widget==window || widget==titleAccessory || widget==workspaceStrip
                || widget==anchor || (anchor && widget->isAncestorOf(anchor))) scheduleTitleGeometry();
        }
        if (target == window && event->type() == QEvent::PaletteChange) {
            if (settingsButton)
                settingsButton->setIcon(RoundedIcons::icon(RoundedIcons::Settings));
            updateTitleTheme();
        }
        if (widget != title && !title->isAncestorOf(widget)
            && (event->type() == QEvent::ZOrderChange || event->type() == QEvent::Show)
            && title->isVisible()) title->raise();
        if (widget != window && widget != title && !title->isAncestorOf(widget)) return false;
        for (auto* at=widget;at && at!=title;at=at->parentWidget())
            if (at->property("titlebarInteractiveGroup").toBool() || qobject_cast<QAbstractButton*>(at)) return false;
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton || !window->windowHandle()) return false;
            const QPoint at = window->mapFromGlobal(mouse->globalPosition().toPoint());
            Qt::Edges edges;
            if (!window->isMaximized()) {
                if (at.x() < 4) edges |= Qt::LeftEdge;
                if (at.x() >= window->width() - 4) edges |= Qt::RightEdge;
                if (at.y() < 4) edges |= Qt::TopEdge;
                if (at.y() >= window->height() - 4) edges |= Qt::BottomEdge;
            }
            if (edges && window->windowHandle()->startSystemResize(edges)) { return true; }
            if (title->isVisible() && title->geometry().contains(at) && !qobject_cast<QAbstractButton*>(widget)) {
                if (event->type() == QEvent::MouseButtonDblClick)
                    window->isMaximized() ? window->showNormal() : window->showMaximized();
                else window->windowHandle()->startSystemMove();
                return true;
            }
        }
        return false;
    }
private:
    static void markGroup(QWidget* group) {
        group->setProperty("titlebarGroup",true);
        group->setProperty("titlebarInteractiveGroup",true);
        group->setAttribute(Qt::WA_StyledBackground);
    }
    QWidget* alignmentAnchor() const {
        auto* anchor=editorRegion ? editorRegion() : nullptr;
        return anchor && anchor->window()==window && anchor->isVisible() ? anchor : window->centralWidget();
    }
    void scheduleTitleGeometry() {
        if (geometryPending) return;
        geometryPending=true;
        QTimer::singleShot(0,this,[this] {geometryPending=false;updateTitleGeometry();});
    }
    void updateTitleGeometry() {
        title->setGeometry(4, 4, window->width() - 8, title->height());
        int right=title->width();
        for (auto it=windowButtons.crbegin();it!=windowButtons.crend();++it) if(auto* button=*it) {
            right-=button->width();
            button->setGeometry(right,(title->height()-button->height())/2,button->width(),button->height());
        }
        auto* anchor=alignmentAnchor();
        const int left=anchor ? anchor->mapTo(window,QPoint()).x()-title->x() : 0;
        const int modesWidth=modeGroup ? modeGroup->sizeHint().width() : 0;
        if (modeGroup) modeGroup->setGeometry(left,0,modesWidth,title->height());
        if (titleLabel) {
            titleLabel->setVisible(left>titleLabel->sizeHint().width()+16);
            titleLabel->setGeometry(8,0,qMax(0,left-20),title->height());
        }
        if (titleAccessory) {
            const int x=left+modesWidth+12;
            titleAccessory->setGeometry(x,0,qMax(0,right-x-12),title->height());
        }
    }
    void updateMaximizeIcon() {
        if (title && title->property("zeroslackElaControl").toBool()) return;
        if (maximizeButton) maximizeButton->setIcon(RoundedIcons::icon(
            window->isMaximized() ? RoundedIcons::Restore : RoundedIcons::Maximize));
    }
    void updateTitleTheme() {
        if (title->property("zeroslackElaControl").toBool()) {
            title->setPalette(QApplication::palette());
            title->setStyleSheet(QStringLiteral("QWidget#workspaceTitleBar { background-color: %1; }")
                .arg(QApplication::palette().color(QPalette::Window).name())+groupStyle());
            return;
        }
        const QPalette palette = window->palette();
        const QColor base = palette.color(QPalette::Window);
        const QColor text = palette.color(QPalette::WindowText);
        const QColor hover = QColor::fromRgbF(
            base.redF() * 0.86 + text.redF() * 0.14,
            base.greenF() * 0.86 + text.greenF() * 0.14,
            base.blueF() * 0.86 + text.blueF() * 0.14);
        title->setStyleSheet(QStringLiteral(
            "QFrame#workspaceTitleBar { background-color: %1; }"
            "QFrame#workspaceTitleBar QToolButton:hover, QFrame#workspaceTitleBar QToolButton[nativeHovered=true] { background: %2; }")
            .arg(base.name(), hover.name())+groupStyle());
    }
    QString groupStyle() const {
        const auto base=QApplication::palette().color(QPalette::Window);
        const auto ink=QApplication::palette().color(QPalette::WindowText);
        const auto tint=QColor::fromRgbF(base.redF()*.96+ink.redF()*.04,
            base.greenF()*.96+ink.greenF()*.04,base.blueF()*.96+ink.blueF()*.04);
        return QStringLiteral("QWidget[titlebarGroup=true] { background-color:%1; border:0; border-radius:6px; }").arg(tint.name());
    }
    void registerActions(QMenu* menu) {
        for (auto* action : menu->actions()) {
            window->addAction(action);
            if (action->menu()) registerActions(action->menu());
        }
    }
    QMainWindow* window;
    QWidget* title = nullptr;
    QPointer<QLabel> titleLabel;
    QWidget* workspaceStrip = nullptr;
    QPointer<QWidget> titleAccessory;
    QPointer<QWidget> modeGroup;
    QList<QAbstractButton*> windowButtons;
    std::function<QWidget*()> editorRegion;
    bool geometryPending=false;
    QPointer<QToolButton> settingsButton;
    QToolButton* maximizeButton = nullptr;
};
