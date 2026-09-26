#include "workspaceswitcher.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "tabmanager.h"
#include "editorfileidentity.h"
#include "roundedicons.h"
#include "uicontrols.h"
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>

WorkspaceSwitcher::WorkspaceSwitcher(WorkspaceManager* workspaces, TabManager* tabs,
                                     WorkspaceSessionCoordinator* sessions,
                                     std::function<void()> openWorkspace, QWidget* parent)
    : QObject(parent), workspaces(workspaces), tabs(tabs), sessions(sessions),
      openWorkspace(std::move(openWorkspace))
{
    connect(workspaces, &WorkspaceManager::workspaceListChanged, this, &WorkspaceSwitcher::refresh);
    connect(workspaces, &WorkspaceManager::workspaceActivated, this, &WorkspaceSwitcher::refresh);
    connect(workspaces, &WorkspaceManager::workspaceClosed, this, &WorkspaceSwitcher::refresh);
    connect(tabs, &TabManager::workspaceSessionStateChanged, this, &WorkspaceSwitcher::refresh);
}

QWidget* WorkspaceSwitcher::createStrip(QWidget* parent)
{
    auto* strip = new QWidget(parent);
    strip->setObjectName(QStringLiteral("workspaceIconStrip"));
    strip->setMinimumWidth(96);
    strip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto* layout = new QHBoxLayout(strip);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto* bar = UiControls::tabBar(strip);
    bar->setObjectName(QStringLiteral("workspaceIconTabs"));
    bar->setAccessibleName(tr("Open workspaces"));
    bar->setExpanding(false);
    bar->setDrawBase(false);
    bar->setUsesScrollButtons(true);
    bar->setElideMode(Qt::ElideNone);
    bar->setIconSize(QSize(20, 20));
    bar->setMinimumWidth(48);
    bar->setFocusPolicy(Qt::StrongFocus);
    bar->setContextMenuPolicy(Qt::CustomContextMenu);
    bar->installEventFilter(this);
    bars.append(bar);
    layout->addWidget(bar, 1);
    connect(bar, &QTabBar::currentChanged, this, [this, bar](int index) {
        if (index >= 0) requestWorkspace(bar->tabData(index).toString(), false);
    });
    connect(bar, &QWidget::customContextMenuRequested, this,
            [this, bar](const QPoint& point) { showContextMenu(bar, point); });

    auto* open = UiControls::railButton(strip);
    open->setObjectName(QStringLiteral("openWorkspaceButton"));
    open->setIcon(RoundedIcons::icon(RoundedIcons::ZoomIn));
    open->setToolTip(tr("Open workspace…"));
    open->setAccessibleName(open->toolTip());
    layout->addWidget(open);
    connect(open, &QToolButton::clicked, this, [this] {
        if (openWorkspace) openWorkspace();
    });
    refresh();
    return strip;
}

void WorkspaceSwitcher::refresh()
{
    if (!workspaces) return;
    const auto entries = workspaces->workspaceEntries();
    for (const auto& bar : bars) {
        if (!bar) continue;
        const QSignalBlocker blocker(bar);
        bool rebuild = bar->count() != entries.size();
        for (int i = 0; !rebuild && i < entries.size(); ++i)
            rebuild = bar->tabData(i).toString() != entries.at(i).path;
        if (rebuild) {
            while (bar->count()) bar->removeTab(bar->count() - 1);
            for (const auto& entry : entries) {
                const int index = bar->addTab(RoundedIcons::icon(RoundedIcons::Folder), QString());
                bar->setTabData(index, entry.path);
            }
        }
        for (int i = 0; i < entries.size(); ++i) {
            const auto& entry = entries.at(i);
            const bool dirty = tabs && tabs->workspaceHasUnsavedChanges(entry.path);
            const QString caption = tr("%1\n%2%3").arg(entry.alias,
                QDir::toNativeSeparators(entry.path), dirty ? tr("\nUnsaved changes") : QString());
            bar->setTabToolTip(i, caption);
            bar->setAccessibleTabName(i, caption);
            bar->setTabWhatsThis(i, tr("Click to switch. Right-click or middle-click to close this workspace."));
        }
        bar->setCurrentIndex(workspaces->activeWorkspaceIndex());
        bar->setVisible(!entries.isEmpty());
    }
}

void WorkspaceSwitcher::requestWorkspace(const QString& path, bool close)
{
    // Resolve identity when dispatched: an earlier close can change every index.
    QTimer::singleShot(0, this, [this, path, close] {
        if (!workspaces || !sessions) return;
        const auto entries = workspaces->workspaceEntries();
        for (int i = 0; i < entries.size(); ++i) {
            if (!EditorFileIdentity::same(entries.at(i).path, path)) continue;
            if (close) sessions->closeWorkspace(i);
            else sessions->switchWorkspace(i);
            refresh();
            return;
        }
        refresh();
    });
}

void WorkspaceSwitcher::showContextMenu(QTabBar* bar, const QPoint& point)
{
    const int index = bar->tabAt(point);
    if (index < 0) return;
    const QString path = bar->tabData(index).toString();
    auto* menu = UiControls::menu(bar);
    menu->setObjectName(QStringLiteral("workspaceIconContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* close = menu->addAction(RoundedIcons::icon(RoundedIcons::Close), tr("Close workspace"));
    close->setObjectName(QStringLiteral("closeWorkspaceAction"));
    connect(close, &QAction::triggered, this, [this, path] { requestWorkspace(path, true); });
    menu->popup(bar->mapToGlobal(point));
}

bool WorkspaceSwitcher::eventFilter(QObject* object, QEvent* event)
{
    auto* bar = qobject_cast<QTabBar*>(object);
    if (!bar) return QObject::eventFilter(object, event);
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const int index = bar->tabAt(mouse->position().toPoint());
        if (mouse->button() == Qt::MiddleButton && index >= 0) {
            requestWorkspace(bar->tabData(index).toString(), true);
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Delete || key->matches(QKeySequence::Close))
            && bar->currentIndex() >= 0) {
            requestWorkspace(bar->tabData(bar->currentIndex()).toString(), true);
            return true;
        }
    }
    return QObject::eventFilter(object, event);
}
