#include "panellayoutcontroller.h"

#include "actionregistry.h"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPoint>
#include <QSizePolicy>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

#include <algorithm>
#include <utility>

PanelLayoutController::PanelLayoutController(
    QMainWindow* mainWindow,
    QObject* parent)
    : QObject(parent)
    , window(mainWindow)
{
}

void PanelLayoutController::setNavigationDock(QDockWidget* dock)
{
    if (navigationDock == dock)
        return;
    navigationDock = dock;
    if (!dock)
        return;

    connect(dock->toggleViewAction(),
            &QAction::toggled,
            this,
            [this, dock](bool) {
                if (applying
                    || focusMode
                    || navigationDock != dock
                    || dock->property(
                           "panelLayoutVisibilityTransient")
                           .toBool()) {
                    return;
                }
                notifyStateChanged();
            });
}

bool PanelLayoutController::registerBottomPanel(
    const QString& panelId,
    QDockWidget* dock)
{
    const QString id = panelId.trimmed();
    if (id.isEmpty() || !dock || entryForId(id) || isBottomPanel(dock))
        return false;

    PanelEntry entry;
    entry.id = id;
    entry.initialTitle = dock->windowTitle();
    entry.dock = dock;
    entry.content = dock->widget();
    if (entry.content) {
        entry.contentMinimumHeight = entry.content->minimumHeight();
        entry.contentMaximumHeight = entry.content->maximumHeight();
    }
    entry.dockFeatures = dock->features();
    panels.append(entry);
    defaultOrder.append(id);
    order.append(id);
    panelOpen.insert(id, !dock->isHidden());
    dock->setProperty("bottomPanelId", id);

    auto* titleBar = new QWidget(dock);
    titleBar->setObjectName(
        QStringLiteral("bottomPanelTitleBar.%1").arg(id));
    titleBar->setFixedHeight(0);
    titleBar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    dock->setTitleBarWidget(titleBar);

    connect(dock->toggleViewAction(),
            &QAction::toggled,
            this,
            [this, id](bool checked) {
                PanelEntry* entry = entryForId(id);
                if (applying
                    || focusMode
                    || (entry
                        && entry->dock
                        && entry->dock->property(
                               "panelLayoutVisibilityTransient")
                               .toBool())) {
                    return;
                }
                panelOpen.insert(id, checked);
                if (checked)
                    activePanel = id;
                notifyStateChanged();
            });
    return true;
}

void PanelLayoutController::finalize()
{
    if (finalized || !window || panels.isEmpty())
        return;
    finalized = true;
    applyOrder();
    bindManagedTabBars();
}

PanelLayoutState PanelLayoutController::layoutState() const
{
    PanelLayoutState result;
    result.bottomPanelOrder = order;
    for (const PanelEntry& entry : panels) {
        if (!panelOpen.value(entry.id, false))
            result.closedBottomPanels.append(entry.id);
        if (pinnedPanels.contains(entry.id))
            result.pinnedBottomPanels.append(entry.id);
    }
    const QString currentActive =
        focusMode ? activePanel : activeBottomPanelId();
    result.activeBottomPanel =
        currentActive.isEmpty() ? activePanel : currentActive;
    result.expandedBottomHeight =
        currentExpandedBottomHeight();
    result.bottomCollapsed = collapsed;
    if (focusMode) {
        result.navigationVisible =
            navigationOpenBeforeFocus;
    } else if (navigationDock
               && navigationDock->property(
                      "panelLayoutVisibilityTransient")
                      .toBool()) {
        result.navigationVisible =
            navigationDock->property(
                "panelLayoutVisibilityBeforeTransient")
                .toBool();
    } else {
        result.navigationVisible =
            navigationDock
            && navigationDock->toggleViewAction()->isChecked();
    }
    result.valid = true;
    return result;
}

void PanelLayoutController::restoreLayoutState(
    const PanelLayoutState& state)
{
    if (!state.valid || panels.isEmpty())
        return;

    if (focusMode)
        setFocusModeActive(false);
    applying = true;

    QStringList restoredOrder;
    for (const QString& id : state.bottomPanelOrder) {
        if (entryForId(id) && !restoredOrder.contains(id))
            restoredOrder.append(id);
    }
    for (const QString& id : std::as_const(defaultOrder)) {
        if (!restoredOrder.contains(id))
            restoredOrder.append(id);
    }
    order = restoredOrder;

    pinnedPanels.clear();
    for (const QString& id : state.pinnedBottomPanels) {
        if (entryForId(id))
            pinnedPanels.insert(id);
    }
    for (PanelEntry& entry : panels)
        applyPinnedFeatures(entry);

    QSet<QString> closed;
    for (const QString& id : state.closedBottomPanels)
        closed.insert(id);
    for (const PanelEntry& entry : panels)
        panelOpen.insert(entry.id, !closed.contains(entry.id));

    activePanel = entryForId(state.activeBottomPanel)
        ? state.activeBottomPanel
        : QString();
    expandedHeight = qMax(64, state.expandedBottomHeight);
    applyOrder();

    if (navigationDock)
        navigationDock->setVisible(state.navigationVisible);
    for (const PanelEntry& entry : panels) {
        if (entry.dock)
            entry.dock->setVisible(panelOpen.value(entry.id, false));
    }
    if (PanelEntry* active = entryForId(activePanel)) {
        if (active->dock && panelOpen.value(activePanel, false))
            active->dock->raise();
    }

    const bool restoreCollapsed = state.bottomCollapsed;
    collapsed = false;
    applyContentCollapse(restoreCollapsed);
    collapsed = restoreCollapsed;
    applying = false;
    bindManagedTabBars();
}

void PanelLayoutController::resetLayout()
{
    if (focusMode)
        setFocusModeActive(false);
    applying = true;
    order = defaultOrder;
    pinnedPanels.clear();
    activePanel = order.isEmpty() ? QString() : order.first();
    expandedHeight = 240;
    applyContentCollapse(false);
    collapsed = false;
    if (navigationDock)
        navigationDock->show();
    for (PanelEntry& entry : panels) {
        panelOpen.insert(entry.id, true);
        applyPinnedFeatures(entry);
    }
    applyOrder();
    for (PanelEntry& entry : panels) {
        if (entry.dock)
            entry.dock->show();
    }
    if (PanelEntry* active = entryForId(activePanel)) {
        if (active->dock)
            active->dock->raise();
    }
    applying = false;
    bindManagedTabBars();
    notifyStateChanged();
}

QStringList PanelLayoutController::bottomPanelIds() const
{
    return order;
}

QString PanelLayoutController::activeBottomPanelId() const
{
    for (const QPointer<QTabBar>& bar : managedTabBars) {
        if (!bar)
            continue;
        const QString id = idForTab(bar, bar->currentIndex());
        if (!id.isEmpty())
            return id;
    }
    return activePanel;
}

bool PanelLayoutController::isBottomPanel(
    const QDockWidget* dock) const
{
    return !panelIdForDock(dock).isEmpty();
}

QString PanelLayoutController::panelIdForDock(
    const QDockWidget* dock) const
{
    if (!dock)
        return QString();
    for (const PanelEntry& entry : panels) {
        if (entry.dock == dock)
            return entry.id;
    }
    return QString();
}

bool PanelLayoutController::isPanelOpen(
    const QString& panelId) const
{
    return entryForId(panelId)
        && panelOpen.value(panelId, false);
}

bool PanelLayoutController::isPanelPinned(
    const QString& panelId) const
{
    return entryForId(panelId)
        && pinnedPanels.contains(panelId);
}

bool PanelLayoutController::closePanel(const QString& panelId)
{
    PanelEntry* entry = entryForId(panelId);
    if (!entry || !entry->dock || pinnedPanels.contains(panelId))
        return false;
    if (focusMode)
        setFocusModeActive(false);
    applying = true;
    entry->dock->hide();
    panelOpen.insert(panelId, false);
    if (activePanel == panelId) {
        activePanel.clear();
        for (const QString& candidate : std::as_const(order)) {
            if (panelOpen.value(candidate, false)) {
                activePanel = candidate;
                break;
            }
        }
    }
    applying = false;
    notifyStateChanged();
    return true;
}

bool PanelLayoutController::restorePanel(const QString& panelId)
{
    PanelEntry* entry = entryForId(panelId);
    if (!entry || !entry->dock)
        return false;
    if (focusMode)
        setFocusModeActive(false);
    applying = true;
    panelOpen.insert(panelId, true);
    activePanel = panelId;
    entry->dock->show();
    entry->dock->raise();
    applying = false;
    bindManagedTabBars();
    notifyStateChanged();
    return true;
}

bool PanelLayoutController::setPanelPinned(
    const QString& panelId,
    bool pinned)
{
    PanelEntry* entry = entryForId(panelId);
    if (!entry)
        return false;
    if (pinned == pinnedPanels.contains(panelId))
        return true;
    if (pinned)
        pinnedPanels.insert(panelId);
    else
        pinnedPanels.remove(panelId);
    applyPinnedFeatures(*entry);
    updateTabCloseButtons();
    notifyStateChanged();
    return true;
}

bool PanelLayoutController::movePanel(
    const QString& panelId,
    int destinationIndex)
{
    const int sourceIndex = order.indexOf(panelId);
    if (sourceIndex < 0 || destinationIndex < 0
        || destinationIndex >= order.size()) {
        return false;
    }
    if (sourceIndex == destinationIndex)
        return true;
    order.move(sourceIndex, destinationIndex);
    applyOrder();
    bindManagedTabBars();
    notifyStateChanged();
    return true;
}

bool PanelLayoutController::isBottomCollapsed() const
{
    return collapsed;
}

void PanelLayoutController::setBottomCollapsed(bool shouldCollapse)
{
    if (collapsed == shouldCollapse)
        return;
    if (focusMode)
        setFocusModeActive(false);

    if (shouldCollapse) {
        expandedHeight = currentExpandedBottomHeight();
    }
    applyContentCollapse(shouldCollapse);
    collapsed = shouldCollapse;
    notifyStateChanged();
}

void PanelLayoutController::toggleBottomCollapsed()
{
    setBottomCollapsed(!collapsed);
}

bool PanelLayoutController::isFocusModeActive() const
{
    return focusMode;
}

void PanelLayoutController::setFocusModeActive(bool active)
{
    if (focusMode == active)
        return;

    applying = true;
    if (active) {
        const QString currentActive =
            activeBottomPanelId();
        if (!currentActive.isEmpty())
            activePanel = currentActive;
        navigationOpenBeforeFocus =
            navigationDock
            && navigationDock->toggleViewAction()->isChecked();
        panelOpenBeforeFocus.clear();
        for (const PanelEntry& entry : panels) {
            const bool open =
                entry.dock
                && entry.dock->toggleViewAction()->isChecked();
            panelOpenBeforeFocus.insert(entry.id, open);
            panelOpen.insert(entry.id, open);
        }
        focusMode = true;
        if (navigationDock)
            navigationDock->hide();
        for (PanelEntry& entry : panels) {
            if (entry.dock)
                entry.dock->hide();
        }
    } else {
        if (navigationDock)
            navigationDock->setVisible(navigationOpenBeforeFocus);
        for (PanelEntry& entry : panels) {
            const bool wasOpen =
                panelOpenBeforeFocus.value(
                    entry.id,
                    panelOpen.value(entry.id, false));
            panelOpen.insert(entry.id, wasOpen);
            if (entry.dock)
                entry.dock->setVisible(wasOpen);
        }
        if (PanelEntry* activeEntry = entryForId(activePanel)) {
            if (activeEntry->dock
                && panelOpen.value(activePanel, false)) {
                activeEntry->dock->raise();
            }
        }
        panelOpenBeforeFocus.clear();
        focusMode = false;
    }
    applying = false;
}

void PanelLayoutController::toggleFocusMode()
{
    setFocusModeActive(!focusMode);
}

void PanelLayoutController::bindManagedTabBars()
{
    if (!window)
        return;

    const QList<QTabBar*> bars = window->findChildren<QTabBar*>();
    for (QTabBar* bar : bars) {
        if (!bar)
            continue;
        int managedCount = 0;
        for (int index = 0; index < bar->count(); ++index) {
            if (!idForTab(bar, index).isEmpty())
                ++managedCount;
        }
        if (managedCount < 2)
            continue;

        if (!managedTabBars.contains(bar))
            managedTabBars.append(bar);
        bar->setObjectName(QStringLiteral("bottomPanelTabBar"));
        bar->setMovable(true);
        bar->setTabsClosable(true);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        if (!bar->property("bottomPanelConnectionsBound").toBool()) {
            bar->setProperty("bottomPanelConnectionsBound", true);
            connect(bar,
                    &QTabBar::tabMoved,
                    this,
                    [this, bar](int, int) {
                        if (applying)
                            return;
                        synchronizeOrderFromTabBar(bar);
                    });
            connect(bar,
                    &QTabBar::currentChanged,
                    this,
                    [this, bar](int index) {
                        if (applying)
                            return;
                        const QString id = idForTab(bar, index);
                        if (id.isEmpty() || activePanel == id)
                            return;
                        activePanel = id;
                        notifyStateChanged();
                    });
            connect(bar,
                    &QTabBar::tabCloseRequested,
                    this,
                    [this, bar](int index) {
                        closePanel(idForTab(bar, index));
                    });
            connect(bar,
                    &QTabBar::customContextMenuRequested,
                    this,
                    [this, bar](const QPoint& position) {
                        showTabContextMenu(bar, position);
                    });
        }
        synchronizeOrderFromTabBar(bar);
    }
    updateTabCloseButtons();
}

void PanelLayoutController::setStateChangedHandler(
    std::function<void()> handler)
{
    stateChangedHandler = std::move(handler);
}

PanelLayoutController::PanelEntry*
PanelLayoutController::entryForId(const QString& panelId)
{
    for (PanelEntry& entry : panels) {
        if (entry.id == panelId)
            return &entry;
    }
    return nullptr;
}

const PanelLayoutController::PanelEntry*
PanelLayoutController::entryForId(const QString& panelId) const
{
    for (const PanelEntry& entry : panels) {
        if (entry.id == panelId)
            return &entry;
    }
    return nullptr;
}

QString PanelLayoutController::idForTab(
    const QTabBar* bar,
    int index) const
{
    if (!bar || index < 0 || index >= bar->count())
        return QString();
    const QString title = bar->tabText(index);
    for (const PanelEntry& entry : panels) {
        if (entry.dock && entry.dock->windowTitle() == title)
            return entry.id;
    }
    return QString();
}

int PanelLayoutController::currentExpandedBottomHeight() const
{
    if (collapsed)
        return expandedHeight;

    for (const QString& id : order) {
        const PanelEntry* entry = entryForId(id);
        if (!entry
            || !entry->dock
            || !panelOpen.value(id, false)) {
            continue;
        }
        if (entry->dock->property(
                "panelLayoutVisibilityTransient")
                .toBool()) {
            const int savedHeight =
                entry->dock->property(
                    "panelLayoutHeightBeforeTransient")
                    .toInt();
            if (savedHeight > 64)
                return savedHeight;
        }
        const int height = entry->dock->height();
        if (height > 64)
            return height;
    }
    return expandedHeight;
}

void PanelLayoutController::applyOrder()
{
    if (!window || order.isEmpty())
        return;

    const bool wasApplying = applying;
    applying = true;
    QHash<QString, bool> savedOpen = panelOpen;
    QList<QPointer<QDockWidget>> orderedDocks;
    orderedDocks.reserve(order.size());
    for (const QString& id : std::as_const(order)) {
        const PanelEntry* entry = entryForId(id);
        if (entry && entry->dock)
            orderedDocks.append(entry->dock);
    }

    QDockWidget* first = nullptr;
    for (const QPointer<QDockWidget>& dock : orderedDocks) {
        if (!dock)
            continue;
        if (window->dockWidgetArea(dock)
            != Qt::BottomDockWidgetArea) {
            window->addDockWidget(
                Qt::BottomDockWidgetArea,
                dock);
        }
        if (!first)
            first = dock;
        else
            window->tabifyDockWidget(first, dock);
    }
    window->setTabPosition(
        Qt::BottomDockWidgetArea,
        QTabWidget::South);
    for (const PanelEntry& entry : panels) {
        if (entry.dock)
            entry.dock->setVisible(savedOpen.value(entry.id, false));
    }
    panelOpen = savedOpen;
    applyTabBarOrder();
    if (PanelEntry* active = entryForId(activePanel)) {
        if (active->dock && panelOpen.value(activePanel, false))
            active->dock->raise();
    }
    applying = wasApplying;
}

void PanelLayoutController::applyTabBarOrder()
{
    if (!window || order.size() < 2)
        return;

    const QList<QTabBar*> bars =
        window->findChildren<QTabBar*>();
    for (QTabBar* bar : bars) {
        if (!bar)
            continue;
        int managedCount = 0;
        for (int index = 0; index < bar->count(); ++index) {
            if (!idForTab(bar, index).isEmpty())
                ++managedCount;
        }
        if (managedCount < 2)
            continue;

        int destination = 0;
        for (const QString& id : std::as_const(order)) {
            int source = -1;
            for (int index = destination;
                 index < bar->count();
                 ++index) {
                if (idForTab(bar, index) == id) {
                    source = index;
                    break;
                }
            }
            if (source < 0)
                continue;
            if (source != destination)
                bar->moveTab(source, destination);
            ++destination;
        }
    }
}

void PanelLayoutController::applyPinnedFeatures(PanelEntry& entry)
{
    if (!entry.dock)
        return;
    QDockWidget::DockWidgetFeatures features = entry.dockFeatures;
    if (pinnedPanels.contains(entry.id))
        features &= ~QDockWidget::DockWidgetClosable;
    entry.dock->setFeatures(features);
}

void PanelLayoutController::applyContentCollapse(bool shouldCollapse)
{
    PanelEntry* activeEntry = entryForId(activePanel);
    QDockWidget* resizeTarget =
        activeEntry
            && activeEntry->dock
            && panelOpen.value(activeEntry->id, false)
        ? activeEntry->dock.data()
        : nullptr;
    QDockWidget* fallbackTarget = nullptr;
    for (PanelEntry& entry : panels) {
        if (!entry.dock)
            continue;
        if (!fallbackTarget)
            fallbackTarget = entry.dock;
        if (!resizeTarget
            && panelOpen.value(entry.id, false)
            && entry.dock->toggleViewAction()->isChecked()) {
            resizeTarget = entry.dock;
        }
        QWidget* content = entry.dock->widget();
        if (content && content != entry.content)
            entry.content = content;
        if (entry.content) {
            if (shouldCollapse) {
                entry.content->setMinimumHeight(0);
                entry.content->setMaximumHeight(0);
            } else {
                entry.content->setMinimumHeight(
                    entry.contentMinimumHeight);
                entry.content->setMaximumHeight(
                    entry.contentMaximumHeight);
            }
        }
    }
    if (!resizeTarget)
        resizeTarget = fallbackTarget;
    if (window && resizeTarget) {
        int requestedHeight =
            shouldCollapse ? 1 : expandedHeight;
        if (!shouldCollapse) {
            const QList<QTabBar*> bars =
                window->findChildren<QTabBar*>();
            for (QTabBar* bar : bars) {
                if (!bar || !bar->isVisible())
                    continue;
                int managedCount = 0;
                for (int index = 0;
                     index < bar->count();
                     ++index) {
                    if (!idForTab(bar, index).isEmpty())
                        ++managedCount;
                }
                if (managedCount >= 2) {
                    requestedHeight += bar->height();
                    break;
                }
            }
        }
        window->resizeDocks(
            {resizeTarget},
            {requestedHeight},
            Qt::Vertical);
    }
}

void PanelLayoutController::updateTabCloseButtons()
{
    for (const QPointer<QTabBar>& bar : managedTabBars) {
        if (!bar)
            continue;
        for (int index = 0; index < bar->count(); ++index) {
            const QString id = idForTab(bar, index);
            QWidget* button =
                bar->tabButton(index, QTabBar::RightSide);
            if (button && !id.isEmpty())
                button->setVisible(!pinnedPanels.contains(id));
            if (!id.isEmpty()) {
                bar->setTabToolTip(
                    index,
                    pinnedPanels.contains(id)
                        ? QStringLiteral("%1 (Pinned)")
                              .arg(bar->tabText(index))
                        : bar->tabText(index));
            }
        }
    }
}

void PanelLayoutController::synchronizeOrderFromTabBar(QTabBar* bar)
{
    if (!bar || applying)
        return;
    QStringList tabOrder;
    for (int index = 0; index < bar->count(); ++index) {
        const QString id = idForTab(bar, index);
        if (!id.isEmpty() && !tabOrder.contains(id))
            tabOrder.append(id);
    }
    if (tabOrder.size() < 2)
        return;
    for (const QString& id : std::as_const(order)) {
        if (!tabOrder.contains(id))
            tabOrder.append(id);
    }
    if (tabOrder != order) {
        order = tabOrder;
        notifyStateChanged();
    }
    const QString current = idForTab(bar, bar->currentIndex());
    if (!current.isEmpty())
        activePanel = current;
}

void PanelLayoutController::showTabContextMenu(
    QTabBar* bar,
    const QPoint& position)
{
    if (!bar)
        return;
    const int index = bar->tabAt(position);
    const QString id = idForTab(bar, index);
    if (id.isEmpty())
        return;

    QMenu menu(bar);
    for (const BottomPanelContextAction& item :
         bottomPanelContextActions(id)) {
        QAction* action = menu.addAction(item.label);
        action->setObjectName(
            QStringLiteral("panelContext.%1")
                .arg(item.actionId));
        action->setProperty(
            "actionId", item.actionId);
        action->setProperty(
            "executionRoute",
            item.executionRoute);
        action->setProperty("panelId", id);
        action->setEnabled(item.enabled);
    }
    QAction* selected = menu.exec(bar->mapToGlobal(position));
    if (!selected)
        return;
    const QString actionId =
        selected->property("actionId").toString();
    if (!actionId.isEmpty())
        requestBottomPanelAction(actionId, id);
}

QList<BottomPanelContextAction>
PanelLayoutController::bottomPanelContextActions(
    const QString& panelId) const
{
    const PanelEntry* entry = entryForId(panelId);
    if (!entry || !entry->dock)
        return {};
    const bool pinned =
        pinnedPanels.contains(panelId);
    struct Spec {
        const char* actionId;
        bool enabled;
    };
    const Spec specs[] = {
        {ActionIds::ViewBottomPanelPinned, true},
        {ActionIds::ViewBottomPanelClose, !pinned},
    };
    QList<BottomPanelContextAction> result;
    result.reserve(
        static_cast<qsizetype>(
            sizeof(specs) / sizeof(specs[0])));
    for (const Spec& spec : specs) {
        const ActionDescriptor* descriptor =
            findActionById(
                QString::fromLatin1(spec.actionId));
        if (!descriptor
            || !descriptor->hasSurface(
                ActionSurface::PanelContextMenu)) {
            continue;
        }
        const ActionAliasDescriptor panelAlias =
            descriptor->aliasForSurface(
                ActionSurface::PanelContextMenu);
        BottomPanelContextAction item;
        item.actionId = descriptor->id;
        item.label = panelAlias.label.isEmpty()
            ? descriptor->canonicalName
            : panelAlias.label;
        if (pinned
            && item.actionId
                   == QString::fromLatin1(
                       ActionIds::ViewBottomPanelPinned)) {
            item.label = QStringLiteral("Unpin Page");
        }
        item.executionRoute =
            descriptor->executionRoute;
        item.enabled = spec.enabled;
        result.append(item);
    }
    return result;
}

void PanelLayoutController::
setRegisteredPanelActionRequestHandler(
    RegisteredPanelActionRequestHandler handler)
{
    registeredPanelActionRequestHandler =
        std::move(handler);
}

bool PanelLayoutController::requestBottomPanelAction(
    const QString& actionId,
    const QString& panelId,
    QString* failureReason)
{
    const QList<BottomPanelContextAction> actions =
        bottomPanelContextActions(panelId);
    const auto selected = std::find_if(
        actions.cbegin(),
        actions.cend(),
        [&actionId](
            const BottomPanelContextAction& action) {
            return action.actionId == actionId;
        });
    if (selected == actions.cend()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Unknown bottom-panel Action: %1")
                                     .arg(actionId);
        }
        return false;
    }
    if (!selected->enabled) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The selected bottom page is pinned.");
        }
        return false;
    }
    if (registeredPanelActionRequestHandler) {
        return registeredPanelActionRequestHandler(
            actionId, panelId, failureReason);
    }
    bool succeeded = false;
    if (actionId
        == QString::fromLatin1(
            ActionIds::ViewBottomPanelPinned)) {
        succeeded = setPanelPinned(
            panelId,
            !isPanelPinned(panelId));
    } else if (actionId
               == QString::fromLatin1(
                   ActionIds::ViewBottomPanelClose)) {
        succeeded = closePanel(panelId);
    }
    if (failureReason) {
        if (succeeded) {
            failureReason->clear();
        } else {
            *failureReason = QStringLiteral(
                "The bottom-panel Action could not be applied.");
        }
    }
    return succeeded;
}

void PanelLayoutController::notifyStateChanged()
{
    if (!applying && stateChangedHandler)
        stateChangedHandler();
}
