#pragma once
#include <QApplication>
#include <QDockWidget>
#include "roundedicons.h"
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
#include <QToolBar>
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
    WorkspaceChrome(QMainWindow* host, QDockWidget* navigation,
                    std::function<void()> settings)
        : QObject(host), window(host) {
        host->setWindowFlag(Qt::FramelessWindowHint);
        host->setContentsMargins(4, 40, 4, 4);
        // Keep menu actions registered so hiding the menu does not remove shortcuts.
        auto* commands = new QMenu(host);
        for (auto* action : host->menuBar()->actions()) {
            commands->addAction(action);
            if (action->menu()) registerActions(action->menu());
        }
        host->menuBar()->hide();
        auto* rail = new QToolBar(host);
        rail->setObjectName(QStringLiteral("projectRail"));
        rail->setMovable(false);
        rail->setFloatable(false);
        rail->setAllowedAreas(Qt::LeftToolBarArea);
        host->addToolBar(Qt::LeftToolBarArea, rail);
        rail->setIconSize(QSize(22, 22));
        auto* project = new QToolButton(rail);
        project->setObjectName(QStringLiteral("projectRailButton"));
        project->setIcon(host->style()->standardIcon(QStyle::SP_DirIcon));
        project->setToolTip(tr("Project — right-click for workspace commands"));
        project->setAccessibleName(tr("Project"));
        project->setCheckable(true);
        project->setChecked(navigation && navigation->isVisible());
        project->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(project, &QWidget::customContextMenuRequested, commands,
            [commands, project](QPoint point) { commands->popup(project->mapToGlobal(point)); });
        connect(project, &QToolButton::clicked, host, [navigation]() {
            if (navigation) { navigation->setVisible(!navigation->isVisible()); if (navigation->isVisible()) navigation->raise(); }
        });
        if (navigation) connect(navigation, &QDockWidget::visibilityChanged,
                                project, &QToolButton::setChecked);
        rail->addWidget(project);
        auto* spacer = new QWidget(rail);
        spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        rail->addWidget(spacer);
        auto* setting = new QToolButton(rail);
        settingsButton = setting;
        setting->setObjectName(QStringLiteral("settingsRailButton"));
        settingsButton->setIcon(RoundedIcons::icon(RoundedIcons::Settings));
        setting->setToolTip(tr("Settings"));
        setting->setAccessibleName(tr("Settings"));
        connect(setting, &QToolButton::clicked, host, std::move(settings));
        rail->addWidget(setting);
        title = new QFrame(host);
        title->setObjectName(QStringLiteral("workspaceTitleBar"));
        title->setAttribute(Qt::WA_NativeWindow);
        title->setAutoFillBackground(true);
        updateTitleTheme();
        auto* row = new QHBoxLayout(title);
        row->setContentsMargins(8, 0, 2, 0);
        auto* name = new QLabel(host->windowTitle(), title);
        row->addWidget(name, 1);
        connect(host, &QWidget::windowTitleChanged, name, &QLabel::setText);
        for (int i = 0; i < 3; ++i) {
            auto* button = new QToolButton(title);
            button->setIcon(host->style()->standardIcon(i == 0 ? QStyle::SP_TitleBarMinButton : i == 1 ? QStyle::SP_TitleBarMaxButton : QStyle::SP_TitleBarCloseButton));
            if (i == 1) {
                maximizeButton = button;
                updateMaximizeIcon();
            }
            button->setToolTip(i == 0 ? tr("Minimize") : i == 1 ? tr("Maximize / Restore") : tr("Close"));
            if (i == 2) button->setStyleSheet(QStringLiteral("QToolButton:hover { background:#c42b1c; color:white; }"));
            row->addWidget(button);
            connect(button, &QToolButton::clicked, host, [host, i]() {
                if (i == 0) host->showMinimized();
                else if (i == 1) host->isMaximized() ? host->showNormal() : host->showMaximized();
                else host->close();
            });
        }
        title->setGeometry(4, 4, host->width() - 8, 36);
        title->show();
        title->raise();
        qApp->installEventFilter(this);
    }
    ~WorkspaceChrome() override { qApp->removeEventFilter(this); }
protected:
    bool eventFilter(QObject* target, QEvent* event) override {
        auto* widget = qobject_cast<QWidget*>(target);
        if (!widget || widget->window() != window) return false;
        if (target == window && event->type() == QEvent::WindowStateChange)
            updateMaximizeIcon();
        if (target == window && event->type() == QEvent::Resize)
            title->setGeometry(4, 4, window->width() - 8, 36);
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
            if (title->isVisible() && title->geometry().contains(at) && !qobject_cast<QToolButton*>(widget)) {
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
        if (maximizeButton) maximizeButton->setIcon(RoundedIcons::icon(
            window->isMaximized() ? RoundedIcons::Restore : RoundedIcons::Maximize));
    }
    void updateTitleTheme() {
        title->setStyleSheet(QStringLiteral("QFrame#workspaceTitleBar { background-color: %1; }")
            .arg(window->palette().color(QPalette::Window).name()));
    }
    void registerActions(QMenu* menu) {
        for (auto* action : menu->actions()) {
            window->addAction(action);
            if (action->menu()) registerActions(action->menu());
        }
    }
    QMainWindow* window;
    QFrame* title;
    QToolButton* settingsButton = nullptr;
    QToolButton* maximizeButton = nullptr;
};
