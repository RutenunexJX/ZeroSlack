#pragma once
#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPointer>
#include <QPainter>
#include <cmath>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
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
        host->setContentsMargins(4, 4, 4, 4);
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
        updateSettingsIcon();
        setting->setToolTip(tr("Settings"));
        setting->setAccessibleName(tr("Settings"));
        connect(setting, &QToolButton::clicked, host, std::move(settings));
        rail->addWidget(setting);
        title = new QFrame(host);
        title->setObjectName(QStringLiteral("autoHideTitleBar"));
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
            button->setToolTip(i == 0 ? tr("Minimize") : i == 1 ? tr("Maximize / Restore") : tr("Close"));
            if (i == 2) button->setStyleSheet(QStringLiteral("QToolButton:hover { background:#c42b1c; color:white; }"));
            row->addWidget(button);
            connect(button, &QToolButton::clicked, host, [host, i]() {
                if (i == 0) host->showMinimized();
                else if (i == 1) host->isMaximized() ? host->showNormal() : host->showMaximized();
                else host->close();
            });
        }
        title->setGeometry(0, 0, host->width(), 32);
        title->raise();
        idle.start();
        auto* timer = new QTimer(this);
        timer->setInterval(100);
        connect(timer, &QTimer::timeout, this, [this]() {
            if (!window->isVisible() || window->isMinimized()) return;
            if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) { idle.restart(); return; }
            title->setGeometry(0, 0, window->width(), 32);
            if (title->isVisible()) title->raise();
            const QPoint at = window->mapFromGlobal(QCursor::pos());
            const bool inside = window->rect().contains(at);
            const bool near = inside && at.y() < 4;
            const bool over = inside && title->isVisible() && at.y() < title->height();
            if (QApplication::mouseButtons() == Qt::NoButton) systemGesture = false;
            if (near || over || systemGesture) {
                idle.restart();
                if (near || over) { title->show(); title->raise(); }
            } else if (idle.elapsed() >= 3000) title->hide();
        });
        timer->start();
        qApp->installEventFilter(this);
    }
    ~WorkspaceChrome() override { qApp->removeEventFilter(this); }
protected:
    bool eventFilter(QObject* target, QEvent* event) override {
        auto* widget = qobject_cast<QWidget*>(target);
        if (!widget || widget->window() != window) return false;
        if (target == window && event->type() == QEvent::PaletteChange) {
            updateSettingsIcon();
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
            if (edges && window->windowHandle()->startSystemResize(edges)) { systemGesture = true; return true; }
            if (title->isVisible() && at.y() < 32 && !qobject_cast<QToolButton*>(widget)) {
                if (event->type() == QEvent::MouseButtonDblClick)
                    window->isMaximized() ? window->showNormal() : window->showMaximized();
                else systemGesture = window->windowHandle()->startSystemMove();
                return true;
            }
        }
        return false;
    }
private:
    void updateTitleTheme() {
        title->setStyleSheet(QStringLiteral("QFrame#autoHideTitleBar { background-color: %1; }")
            .arg(window->palette().color(QPalette::Window).name()));
    }
    void updateSettingsIcon() {
        if (!settingsButton) return;
        const qreal scale = window->devicePixelRatioF();
        QPixmap pixels(qRound(24 * scale), qRound(24 * scale));
        pixels.setDevicePixelRatio(scale);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(window->palette().color(QPalette::ButtonText), 1.7));
        QPolygonF gear;
        for (int i = 0; i < 32; ++i) {
            const qreal angle = i * 6.283185307179586 / 32;
            const qreal radius = (i % 4 == 0 || i % 4 == 3) ? 9.5 : 7.5;
            gear << QPointF(12 + std::cos(angle) * radius, 12 + std::sin(angle) * radius);
        }
        painter.drawPolygon(gear);
        painter.drawEllipse(QPointF(12, 12), 3, 3);
        settingsButton->setIcon(QIcon(pixels));
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
    QElapsedTimer idle;
    bool systemGesture = false;
};
