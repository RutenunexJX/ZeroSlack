#include "uicontrols.h"
#include "uiwindowchrome.h"
#include <memory>
#pragma once
#include <QApplication>
#include "windowsnapchrome.h"
#include "navigationpanecoordinator.h"
#include "workspaceswitcher.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
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

// Preserve coordinator-owned content when a central tool tab closes.
class BorrowedPanelPage final : public QWidget {
public:
    BorrowedPanelPage(QWidget* content, QWidget* home)
        : contentValue(content), homeValue(home) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(content);
        content->show();
    }
    ~BorrowedPanelPage() override {
        if (!contentValue || !homeValue) return;
        contentValue->hide();
        if (auto* dock = qobject_cast<QDockWidget*>(homeValue.data()))
            dock->setWidget(contentValue);
        else if (auto* stack = qobject_cast<QStackedWidget*>(homeValue.data()))
            stack->addWidget(contentValue);
        else contentValue->setParent(homeValue);
    }
private:
    QPointer<QWidget> contentValue, homeValue;
};

class WorkspaceChrome final : public QObject {
public:
    WorkspaceChrome(QMainWindow* host, NavigationPaneCoordinator* navigationPane,
                    std::function<void()> settings, WorkspaceSwitcher* workspaces = nullptr)
        : QObject(host), window(host) {
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
        project->setIcon(RoundedIcons::icon(RoundedIcons::Folder));
        project->setIconSize(QSize(20, 20));
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
        setting->setIconSize(QSize(20, 20));
        setting->setToolTip(tr("Settings"));
        setting->setAccessibleName(tr("Settings"));
        connect(setting, &QToolButton::clicked, host, std::move(settings));
        controls->addWidget(setting);
        controls->addStretch();
        auto* collapse = UiControls::railButton(header);
        collapse->setObjectName(QStringLiteral("collapseProjectSidebarButton"));
        collapse->setIcon(RoundedIcons::icon(RoundedIcons::Sidebar));
        collapse->setIconSize(QSize(20, 20));
        collapse->setToolTip(tr("Collapse sidebar (Ctrl+1)"));
        collapse->setAccessibleName(tr("Collapse sidebar"));
        controls->addWidget(collapse);
        if (workspaces)
            headerLayout->addWidget(workspaces->createButton(header,
                QStringLiteral("sidebarWorkspaceSwitcher")));
        if (navigation) {
            navigationPane->setHeaderWidget(header);
            connect(collapse, &QToolButton::clicked, navigationPane,
                    [navigationPane]() {
                        navigationPane->setExpanded(false);
                    });
        }
        auto* titlePicker = workspaces ? workspaces->createButton(host,
            QStringLiteral("titleWorkspaceSwitcher")) : nullptr;
        if (titlePicker) {
            titlePicker->setFixedWidth(190);
            titlePicker->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        }
        const auto titleBar = UiWindowChrome::createTitleBar(host, titlePicker);
        title = titleBar.widget;
        maximizeButton = titleBar.maximize;
        updateMaximizeIcon();
        host->setContentsMargins(4, title->height() + 4, 4, 4);
        updateTitleTheme();
        auto* expand = titleBar.sidebar;
        expand->setVisible(navigation && navigation->isHidden());
        if (titlePicker) titlePicker->setVisible(navigation && navigation->isHidden());
        if (navigation) {
            connect(expand, &QToolButton::clicked, navigationPane,
                    [navigationPane]() {
                navigationPane->setExpanded(true);
            });
            connect(navigation, &QDockWidget::visibilityChanged, expand,
                    [expand](bool visible) { expand->setVisible(!visible); });
            if (titlePicker)
                connect(navigation, &QDockWidget::visibilityChanged, titlePicker,
                        [titlePicker](bool visible) { titlePicker->setVisible(!visible); });
        }
        auto* name = titleBar.label;
        name->setToolTip(host->windowFilePath() + tr("\nRight-click to copy the path or reveal the file"));
        name->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(name, &QWidget::customContextMenuRequested, this, [this, name](QPoint point) {
            std::unique_ptr<QMenu> menuOwner(UiControls::menu(name));
            QMenu& menu = *menuOwner;
            const QString path = window->windowFilePath();
            auto* copy = menu.addAction(tr("Copy full path"));
            auto* reveal = menu.addAction(tr("Reveal in Explorer"));
            copy->setEnabled(!path.isEmpty());
            reveal->setEnabled(QFileInfo::exists(path));
            auto* selected = menu.exec(name->mapToGlobal(point));
            if (selected == copy) QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
            else if (selected == reveal) {
#ifdef Q_OS_WIN
                QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
            }
        });
        connect(host, &QWidget::windowTitleChanged, name, [host, name] { name->setToolTip(host->windowFilePath() + tr("\nRight-click to copy the path or reveal the file")); });
        title->setGeometry(4, 4, host->width() - 8, title->height());
        title->show();
        title->raise();
        qApp->installEventFilter(this);
        new WindowSnapChrome(host, maximizeButton);
    }
    ~WorkspaceChrome() override { qApp->removeEventFilter(this); }
protected:
    bool eventFilter(QObject* target, QEvent* event) override {
        auto* widget = qobject_cast<QWidget*>(target);
        if (!widget || widget->window() != window) return false;
        if (target == window && event->type() == QEvent::WindowStateChange)
            updateMaximizeIcon();
        if (target == window && event->type() == QEvent::Resize)
            title->setGeometry(4, 4, window->width() - 8, title->height());
        if (target == window && event->type() == QEvent::PaletteChange) {
            settingsButton->setIcon(RoundedIcons::icon(RoundedIcons::Settings));
            updateTitleTheme();
        }
        if (widget != title && !title->isAncestorOf(widget)
            && (event->type() == QEvent::ZOrderChange || event->type() == QEvent::Show)
            && title->isVisible()) title->raise();
        if (widget != window && widget != title && !title->isAncestorOf(widget)) return false;
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
    void updateMaximizeIcon() {
        if (title && title->property("zeroslackElaControl").toBool()) return;
        if (maximizeButton) maximizeButton->setIcon(RoundedIcons::icon(
            window->isMaximized() ? RoundedIcons::Restore : RoundedIcons::Maximize));
    }
    void updateTitleTheme() {
        if (title->property("zeroslackElaControl").toBool()) {
            title->setPalette(window->palette());
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
            .arg(base.name(), hover.name()));
    }
    void registerActions(QMenu* menu) {
        for (auto* action : menu->actions()) {
            window->addAction(action);
            if (action->menu()) registerActions(action->menu());
        }
    }
    QMainWindow* window;
    QWidget* title = nullptr;
    QToolButton* settingsButton = nullptr;
    QToolButton* maximizeButton = nullptr;
};
