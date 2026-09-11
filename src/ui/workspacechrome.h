#pragma once
#include <QApplication>
#include "windowsnapchrome.h"
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
        auto* header = new QFrame(navigation);
        header->setObjectName(QStringLiteral("projectSidebarHeader"));
        auto* controls = new QHBoxLayout(header);
        controls->setContentsMargins(8, 8, 8, 8);
        controls->setSpacing(6);
        auto* project = new QToolButton(header);
        project->setObjectName(QStringLiteral("projectRailButton"));
        project->setIcon(RoundedIcons::icon(RoundedIcons::Folder));
        project->setIconSize(QSize(20, 20));
        project->setText(tr("Project"));
        project->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        UiTypography::apply(project, UiTypography::Role::Body);
        project->setToolTip(tr("Project commands"));
        project->setAccessibleName(tr("Project"));
        project->setCheckable(true);
        project->setChecked(true);
        project->setMenu(commands);
        project->setPopupMode(QToolButton::InstantPopup);
        project->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(project, &QWidget::customContextMenuRequested, commands,
            [commands, project](QPoint point) { commands->popup(project->mapToGlobal(point)); });
        controls->addWidget(project);
        auto* setting = new QToolButton(header);
        settingsButton = setting;
        setting->setObjectName(QStringLiteral("settingsRailButton"));
        settingsButton->setIcon(RoundedIcons::icon(RoundedIcons::Settings));
        setting->setIconSize(QSize(20, 20));
        setting->setToolTip(tr("Settings"));
        setting->setAccessibleName(tr("Settings"));
        connect(setting, &QToolButton::clicked, host, std::move(settings));
        controls->addWidget(setting);
        controls->addStretch();
        auto* collapse = new QToolButton(header);
        collapse->setObjectName(QStringLiteral("collapseProjectSidebarButton"));
        collapse->setIcon(RoundedIcons::icon(RoundedIcons::Sidebar));
        collapse->setIconSize(QSize(20, 20));
        collapse->setToolTip(tr("Collapse sidebar (Ctrl+1)"));
        collapse->setAccessibleName(tr("Collapse sidebar"));
        controls->addWidget(collapse);
        if (navigation) {
            navigation->setTitleBarWidget(header);
            connect(collapse, &QToolButton::clicked, navigation, &QWidget::hide);
        }
        title = new QFrame(host);
        title->setObjectName(QStringLiteral("workspaceTitleBar"));

        title->setAutoFillBackground(true);
        updateTitleTheme();
        auto* row = new QHBoxLayout(title);
        row->setContentsMargins(8, 0, 2, 0);
        auto* expand = new QToolButton(title);
        expand->setObjectName(QStringLiteral("expandProjectSidebarButton"));
        expand->setIcon(RoundedIcons::icon(RoundedIcons::Sidebar));
        expand->setIconSize(QSize(20, 20));
        expand->setToolTip(tr("Expand sidebar (Ctrl+1)"));
        expand->setAccessibleName(tr("Expand sidebar"));
        expand->setVisible(navigation && navigation->isHidden());
        if (navigation) {
            connect(expand, &QToolButton::clicked, navigation, [navigation] {
                navigation->show();
                navigation->raise();
            });
            connect(navigation, &QDockWidget::visibilityChanged, expand,
                    [expand](bool visible) { expand->setVisible(!visible); });
        }
        row->addWidget(expand);
        auto* name = new QLabel(host->windowTitle(), title);
        UiTypography::apply(name, UiTypography::Role::Body);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setMinimumWidth(0);
        name->setObjectName(QStringLiteral("workspaceFilePath"));
        name->setToolTip(host->windowFilePath() + tr("\nRight-click to copy the path or reveal the file"));
        name->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(name, &QWidget::customContextMenuRequested, this, [this, name](QPoint point) {
            QMenu menu(name);
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
        row->addWidget(name, 1);
        connect(host, &QWidget::windowTitleChanged, name, &QLabel::setText);
        for (int i = 0; i < 3; ++i) {
            auto* button = new QToolButton(title);
            button->setObjectName(i == 0 ? QStringLiteral("windowMinimizeButton")
                : i == 1 ? QStringLiteral("windowMaximizeButton") : QStringLiteral("windowCloseButton"));
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
