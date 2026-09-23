#include "uitypography.h"
#include "panellayoutcontroller.h"
#include "uicontrols.h"
#include "deferredpanel.h"
#include "panelcompositor.h"
#include "applicationthememanager.h"

#include "insightvisualstyle.h"
#include "roundedicons.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMouseEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr int kResizeHandleHeight = 8;
constexpr int kButtonBarHeight = 42;

QString colorCss(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QString panelLabel(const QString& panelId)
{
    if (panelId == QStringLiteral("problems"))
        return QStringLiteral("Problems");
    if (panelId == QStringLiteral("scopedSearch"))
        return QStringLiteral("Search");
    if (panelId == QStringLiteral("activity"))
        return QStringLiteral("Activity");
    if (panelId == QStringLiteral("rtlHighRiskEdit"))
        return QStringLiteral("Change Preview");
    if (panelId == QStringLiteral("connections"))
        return QStringLiteral("Connections");
    return panelId;
}

QIcon panelIcon(const QString& panelId)
{
    using namespace RoundedIcons;
    if (panelId == QStringLiteral("problems")) return icon(Warning);
    if (panelId == QStringLiteral("scopedSearch")) return icon(Search);
    if (panelId == QStringLiteral("activity")) return icon(Activity);
    if (panelId == QStringLiteral("rtlHighRiskEdit")) return icon(Change);
    if (panelId == QStringLiteral("connections")) return icon(Connections);
    return icon(Info);
}

QString modelIndexPath(const QModelIndex& index)
{
    if (!index.isValid())
        return {};
    QStringList rows;
    QModelIndex cursor = index;
    while (cursor.isValid()) {
        rows.prepend(QString::number(cursor.row()));
        cursor = cursor.parent();
    }
    return rows.join(QLatin1Char('/'));
}

QModelIndex modelIndexFromPath(QAbstractItemModel* model,
                               const QString& path,
                               int column)
{
    if (!model || path.isEmpty())
        return {};
    QModelIndex parent;
    const QStringList rows = path.split(QLatin1Char('/'));
    for (int index = 0; index < rows.size(); ++index) {
        bool ok = false;
        const int row = rows.at(index).toInt(&ok);
        if (!ok || row < 0)
            return {};
        const int targetColumn = index + 1 == rows.size()
            ? qMax(0, column) : 0;
        parent = model->index(row, targetColumn, parent);
        if (!parent.isValid())
            return {};
        if (index + 1 < rows.size() && parent.column() != 0)
            parent = parent.siblingAtColumn(0);
    }
    return parent;
}

}

PanelLayoutController::PanelLayoutController(
    QMainWindow* mainWindow,
    QObject* parent)
    : QObject(parent)
    , window(mainWindow)
{
    animationsEnabledValue = ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela;
    if (window) {
        window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
        window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    }
    buildDrawer();
    if (animationsEnabledValue) compositor = PanelCompositor::forWindow(window);
    if (qApp)
        qApp->installEventFilter(this);
    if (window)
        window->installEventFilter(this);
}

PanelLayoutController::~PanelLayoutController()
{
    if (compositor) compositor->settleFor(this);
    if (qApp)
        qApp->removeEventFilter(this);
}

void PanelLayoutController::buildDrawer()
{
    if (!window || bottomDrawerDock)
        return;

    bottomDrawerDock = new QDockWidget(window);
    bottomDrawerDock->setObjectName(
        QStringLiteral("bottomToolDrawerDock"));
    bottomDrawerDock->setAccessibleName(
        QStringLiteral("Bottom tool drawer"));
    bottomDrawerDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    bottomDrawerDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* titleBar = new QWidget(bottomDrawerDock);
    titleBar->setFixedHeight(0);
    bottomDrawerDock->setTitleBarWidget(titleBar);

    bottomDrawerRoot = new QWidget(bottomDrawerDock);
    bottomDrawerRoot->setObjectName(
        QStringLiteral("bottomToolDrawerRoot"));
    auto* rootLayout = new QVBoxLayout(bottomDrawerRoot);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    bottomResizeHandle = new QWidget(bottomDrawerRoot);
    bottomResizeHandle->setObjectName(
        QStringLiteral("bottomToolDrawerResizeHandle"));
    bottomResizeHandle->setAccessibleName(
        QStringLiteral("Resize bottom panel"));
    bottomResizeHandle->setCursor(Qt::SplitVCursor);
    bottomResizeHandle->setFixedHeight(kResizeHandleHeight);
    bottomResizeHandle->installEventFilter(this);
    rootLayout->addWidget(bottomResizeHandle);

    QStackedWidget* contentStack = nullptr;
    bottomContentSurface = UiControls::pageStack(contentStack, bottomDrawerRoot);
    bottomContentStack = contentStack;
    bottomContentStack->setObjectName(
        QStringLiteral("bottomToolDrawerContent"));
    bottomContentStack->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    rootLayout->addWidget(bottomContentSurface);

    bottomButtonBar = new QFrame(bottomDrawerRoot);
    bottomButtonBar->setObjectName(
        QStringLiteral("bottomToolDrawerButtonBar"));
    bottomButtonBar->setAccessibleName(
        QStringLiteral("Bottom panel buttons"));
    bottomButtonBar->setFixedHeight(kButtonBarHeight);
    auto* buttonLayout = new QHBoxLayout(bottomButtonBar);
    buttonLayout->setObjectName(
        QStringLiteral("bottomToolDrawerButtonLayout"));
    buttonLayout->setContentsMargins(6, 0, 6, 0);
    buttonLayout->setSpacing(0);
    buttonLayout->addStretch(1);
    rootLayout->addWidget(bottomButtonBar);

    bottomDrawerDock->setWidget(bottomDrawerRoot);
    window->addDockWidget(Qt::BottomDockWidgetArea, bottomDrawerDock);

    auto* escapeShortcut = new QShortcut(
        QKeySequence(Qt::Key_Escape), bottomContentStack);
    escapeShortcut->setObjectName(
        QStringLiteral("bottomToolDrawerEscapeShortcut"));
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut,
            &QShortcut::activated,
            this,
            [this]() {
                if (focusIsInsideDrawer())
                    setBottomCollapsed(true);
            });

    updateDrawerStyle();
    collapsed = false;
    applyContentHeight(kDefaultContentHeight);
}

void PanelLayoutController::setNavigationDock(QDockWidget* dock)
{
    navigationDock = dock;
}

bool PanelLayoutController::registerSidePanel(
    const QString& panelId,
    QDockWidget* dock)
{
    const QString id = panelId.trimmed();
    if (id.isEmpty() || !dock)
        return false;
    for (const SidePanelEntry& entry : std::as_const(sidePanels)) {
        if (entry.id == id || entry.dock == dock)
            return false;
    }
    if (window) {
        for (const SidePanelEntry& entry : std::as_const(sidePanels)) {
            if (entry.dock && !entry.dock->isFloating()
                && window->dockWidgetArea(entry.dock) == window->dockWidgetArea(dock))
                window->tabifyDockWidget(entry.dock, dock);
        }
    }
    sidePanels.append({id, dock});
    return true;
}

bool PanelLayoutController::registerBottomPanel(
    const QString& panelId,
    QDockWidget* dock)
{
    const QString id = panelId.trimmed();
    if (id.isEmpty() || !dock || !bottomContentStack
        || entryForId(id) || isBottomPanel(dock)) {
        return false;
    }
    QWidget* content = dock->widget();
    if (!content)
        return false;

    if (window)
        window->removeDockWidget(dock);
    dock->hide();
    dock->setWidget(nullptr);
    content->setParent(bottomContentStack);
    content->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->setProperty("bottomDrawerPanelId", id);
    bottomContentStack->addWidget(content);

    PanelEntry entry;
    entry.id = id;
    entry.label = panelLabel(id);
    entry.initialTitle = dock->windowTitle();
    entry.dock = dock;
    entry.content = content;
    panels.append(entry);
    defaultOrder.append(id);
    buildButton(panels.last());
    setPanelBadge(
        id,
        dock->property("bottomBadgeText").toString(),
        dock->property("bottomBadgeTone").toString());

    if (activePanel.isEmpty()) {
        activePanel = id;
        lastPanel = id;
        bottomContentStack->setCurrentWidget(content);
    }
    dock->setProperty("bottomDrawerPanelId", id);
    connect(dock,
            &QDockWidget::windowTitleChanged,
            this,
            [this, id](const QString&) {
                if (PanelEntry* changed = entryForId(id)) {
                    if (changed->dock) {
                        setPanelBadge(
                            id,
                            changed->dock->property(
                                "bottomBadgeText").toString(),
                            changed->dock->property(
                                "bottomBadgeTone").toString());
                    }
                }
                updateButtons();
            });
    return true;
}

bool PanelLayoutController::registerBottomPanelAlias(
    const QString& alias,
    const QString& panelId)
{
    const QString cleanAlias = alias.trimmed();
    const QString canonical = canonicalPanelId(panelId);
    if (cleanAlias.isEmpty() || cleanAlias == canonical
        || !entryForId(canonical)) {
        return false;
    }
    aliases.insert(cleanAlias, canonical);
    return true;
}

void PanelLayoutController::buildButton(PanelEntry& entry)
{
    if (!bottomButtonBar)
        return;
    auto* layout = qobject_cast<QHBoxLayout*>(bottomButtonBar->layout());
    if (!layout)
        return;
    auto* button = UiControls::badgedToolButton(bottomButtonBar);
    button->setProperty("panelMotionToggle", true);
    button->setObjectName(
        QStringLiteral("bottomPanelButton_%1").arg(entry.id));
    button->setText(entry.label);
    button->setAccessibleName(entry.label);
    button->setAccessibleDescription(
        QStringLiteral("Open or close the %1 bottom panel. Ctrl+J toggles the last panel.")
            .arg(entry.label));
    QString tooltip = QStringLiteral("%1\nToggle last panel: Ctrl+J")
        .arg(entry.label);
    if (entry.id == QStringLiteral("scopedSearch"))
        tooltip.prepend(QStringLiteral("Search workspace: Ctrl+Shift+F\n"));
    button->setToolTip(tooltip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIcon(
        panelIcon(entry.id));
    button->setIconSize(QSize(20, 20));
    button->setFocusPolicy(Qt::StrongFocus);
    entry.button = button;
    layout->insertWidget(qMax(0, layout->count() - 1), button);
    connect(button,
            &QToolButton::clicked,
            this,
            [this, id = entry.id]() {
                PanelEntry* selected = entryForId(id);
                if (!selected)
                    return;
                if (!focusIsInsideDrawer())
                    focusBeforeDrawer = QApplication::focusWidget();
                if (activePanel == id && !collapsed) {
                    setBottomCollapsed(true);
                } else {
                    activatePanel(*selected, false);
                }
            });
}

void PanelLayoutController::finalize()
{
    if (finalized)
        return;
    finalized = true;
    if (panels.isEmpty()) {
        if (bottomDrawerDock)
            bottomDrawerDock->hide();
        return;
    }
    if (!entryForId(activePanel))
        activePanel = panels.constFirst().id;
    if (!entryForId(lastPanel))
        lastPanel = activePanel;
    PanelEntry* entry = entryForId(activePanel);
    if (entry && entry->content) {
        bottomContentStack->setCurrentWidget(entry->content);
        restorePanelViewState(*entry);
    }
    applyDrawerState(false);
    updateButtons();
}

PanelLayoutState PanelLayoutController::layoutState() const
{
    PanelLayoutState state;
    state.version = PanelLayoutState::kVersion;
    state.bottomPanelOrder = defaultOrder;
    state.activeBottomPanel = activePanel;
    state.lastBottomPanel = lastPanel;
    state.bottomCollapsed = collapsed;
    state.navigationVisible = navigationDock ? navigationDock->isVisible() : true;
    for (const PanelEntry& entry : panels) {
        state.bottomPanelHeights.insert(entry.id, entry.height);
        state.bottomPanelViewStates.insert(
            entry.id,
            entry.content
                ? captureWidgetState(entry.content)
                : entry.viewState);
    }
    const PanelEntry* active = entryForId(activePanel);
    state.expandedBottomHeight = active
        ? active->height : kDefaultContentHeight;
    state.valid = finalized;
    return state;
}

void PanelLayoutController::restoreLayoutState(
    const PanelLayoutState& state)
{
    if (window) {
        window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
        window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    }
    if (!state.valid || panels.isEmpty())
        return;
    applying = true;
    if (window) {
        for (int i = 1; i < sidePanels.size(); ++i) {
            auto* first = sidePanels.at(i - 1).dock.data();
            auto* next = sidePanels.at(i).dock.data();
            if (first && next && !first->isFloating() && !next->isFloating()
                && window->dockWidgetArea(first) == window->dockWidgetArea(next))
                window->tabifyDockWidget(first, next);
        }
    }
    if (window && bottomDrawerDock) {
        for (const PanelEntry& entry : std::as_const(panels)) {
            if (entry.dock) {
                window->removeDockWidget(entry.dock);
                entry.dock->hide();
            }
        }
        window->addDockWidget(
            Qt::BottomDockWidgetArea, bottomDrawerDock);
    }

    const int legacyHeight = qMax(
        kMinimumContentHeight, state.expandedBottomHeight);
    for (PanelEntry& entry : panels) {
        int height = state.bottomPanelHeights.value(
            entry.id, legacyHeight);
        if (entry.id == QStringLiteral("connections")) {
            if (state.bottomPanelHeights.contains(
                    QStringLiteral("instancePairConnection"))) {
                height = state.bottomPanelHeights.value(
                    QStringLiteral("instancePairConnection"));
            } else if (state.bottomPanelHeights.contains(
                           QStringLiteral("multiSignalPropagation"))) {
                height = state.bottomPanelHeights.value(
                    QStringLiteral("multiSignalPropagation"));
            }
        }
        entry.height = qMax(kMinimumContentHeight, height);
        entry.viewState = state.bottomPanelViewStates.value(entry.id);
        if (entry.id == QStringLiteral("connections")
            && entry.viewState.isEmpty()) {
            entry.viewState = state.bottomPanelViewStates.value(
                QStringLiteral("instancePairConnection"));
            if (entry.viewState.isEmpty()) {
                entry.viewState = state.bottomPanelViewStates.value(
                    QStringLiteral("multiSignalPropagation"));
            }
        }
        if (auto* deferred = dynamic_cast<DeferredPanel*>(entry.content.data());
            deferred && !deferred->isCreated()) {
            restoreWidgetState(deferred, entry.viewState);
        }
    }

    QString restoredActive = canonicalPanelId(
        state.activeBottomPanel);
    if (mainAreaRequest && restoredActive == QStringLiteral("connections"))
        restoredActive = QStringLiteral("problems");
    if (!entryForId(restoredActive))
        restoredActive = panels.constFirst().id;
    QString restoredLast = canonicalPanelId(state.lastBottomPanel);
    if (!entryForId(restoredLast))
        restoredLast = restoredActive;
    activePanel = restoredActive;
    lastPanel = restoredLast;
    collapsed = state.bottomCollapsed;
    if (navigationDock)
        navigationDock->setVisible(state.navigationVisible);
    if (PanelEntry* entry = entryForId(activePanel)) {
        if (entry->content)
            bottomContentStack->setCurrentWidget(entry->content);
        restorePanelViewState(*entry);
    }
    applying = false;
    applyDrawerState(false);
    updateButtons();
    notifyStateChanged();
}

void PanelLayoutController::resetLayout()
{
    applying = true;
    if (window) {
        for (int i = 1; i < sidePanels.size(); ++i) {
            auto* first = sidePanels.at(i - 1).dock.data();
            auto* next = sidePanels.at(i).dock.data();
            if (first && next && !first->isFloating() && !next->isFloating()
                && window->dockWidgetArea(first) == window->dockWidgetArea(next))
                window->tabifyDockWidget(first, next);
        }
    }

    for (PanelEntry& entry : panels) {
        entry.height = kDefaultContentHeight;
        entry.viewState.clear();
        if (auto* deferred = dynamic_cast<DeferredPanel*>(entry.content.data());
            deferred && !deferred->isCreated()) {
            restoreWidgetState(deferred, {});
        }
    }
    activePanel = panels.isEmpty()
        ? QString() : panels.constFirst().id;
    lastPanel = activePanel;
    collapsed = false;
    if (navigationDock)
        navigationDock->show();
    for (const SidePanelEntry& entry : std::as_const(sidePanels)) {
        if (entry.dock)
            entry.dock->hide();
    }
    if (PanelEntry* entry = entryForId(activePanel)) {
        if (entry->content)
            bottomContentStack->setCurrentWidget(entry->content);
    }
    applying = false;
    applyDrawerState(false);
    updateButtons();
    notifyStateChanged();
}

QStringList PanelLayoutController::bottomPanelIds() const
{
    return defaultOrder;
}

QString PanelLayoutController::activeBottomPanelId() const
{
    return activePanel;
}

QString PanelLayoutController::lastBottomPanelId() const
{
    return lastPanel;
}

bool PanelLayoutController::isBottomPanel(
    const QDockWidget* dock) const
{
    if (!dock)
        return false;
    return std::any_of(
        panels.cbegin(), panels.cend(),
        [dock](const PanelEntry& entry) {
            return entry.dock == dock;
        });
}

QString PanelLayoutController::panelIdForDock(
    const QDockWidget* dock) const
{
    for (const PanelEntry& entry : panels) {
        if (entry.dock == dock)
            return entry.id;
    }
    return {};
}

bool PanelLayoutController::isPanelOpen(
    const QString& panelId) const
{
    const QString id = canonicalPanelId(panelId);
    return !collapsed && activePanel == id
        && entryForId(id);
}

bool PanelLayoutController::isPanelPinned(const QString&) const
{
    return false;
}

bool PanelLayoutController::closePanel(const QString& panelId)
{
    const QString id = canonicalPanelId(panelId);
    if (!entryForId(id))
        return false;
    if (activePanel == id && !collapsed)
        setBottomCollapsed(true);
    return true;
}

bool PanelLayoutController::restorePanel(const QString& panelId)
{
    PanelEntry* entry = entryForId(panelId);
    if (!entry)
        return false;
    activatePanel(*entry, false);
    return true;
}

bool PanelLayoutController::setPanelPinned(
    const QString& panelId,
    bool)
{
    return entryForId(panelId) != nullptr;
}

bool PanelLayoutController::movePanel(
    const QString& panelId,
    int destinationIndex)
{
    const int current = defaultOrder.indexOf(canonicalPanelId(panelId));
    return current >= 0 && current == destinationIndex;
}

QList<BottomPanelContextAction>
PanelLayoutController::bottomPanelContextActions(
    const QString& panelId) const
{
    if (!entryForId(panelId))
        return {};
    return {
        {QStringLiteral("view.bottomPanel.collapsed"),
         collapsed ? QStringLiteral("Restore Panel")
                   : QStringLiteral("Close Panel"),
         QStringLiteral("ui.bottomPanel.collapsed.toggle"),
         true}
    };
}

void PanelLayoutController::
setRegisteredPanelActionRequestHandler(
    RegisteredPanelActionRequestHandler handler)
{
    registeredPanelActionRequestHandler = std::move(handler);
}

bool PanelLayoutController::requestBottomPanelAction(
    const QString& actionId,
    const QString& panelId,
    QString* failureReason)
{
    if (!entryForId(panelId)) {
        if (failureReason)
            *failureReason = QStringLiteral("Unknown bottom panel.");
        return false;
    }
    if (!registeredPanelActionRequestHandler) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The bottom-panel action registry is unavailable.");
        }
        return false;
    }
    return registeredPanelActionRequestHandler(
        actionId, canonicalPanelId(panelId), failureReason);
}

bool PanelLayoutController::isBottomCollapsed() const
{
    return collapsed;
}

void PanelLayoutController::setBottomCollapsed(bool shouldCollapse)
{
    if (panels.isEmpty() || collapsed == shouldCollapse)
        return;
    if (PanelEntry* entry = entryForId(activePanel))
        capturePanelViewState(*entry);
    collapsed = shouldCollapse;
    if (!collapsed && activePanel.isEmpty())
        activePanel = !lastPanel.isEmpty()
            ? lastPanel : panels.constFirst().id;
    if (!collapsed)
        lastPanel = activePanel;
    applyDrawerState(true);
    updateButtons();
    if (collapsed)
        restoreEditorFocus();
    notifyStateChanged();
}

void PanelLayoutController::toggleBottomCollapsed()
{
    setBottomCollapsed(!collapsed);
}

int PanelLayoutController::panelHeight(const QString& panelId) const
{
    const PanelEntry* entry = entryForId(panelId);
    return entry ? boundedContentHeight(entry->height) : 0;
}

bool PanelLayoutController::setPanelHeight(
    const QString& panelId,
    int height)
{
    if (compositor) compositor->settleFor(this);
    PanelEntry* entry = entryForId(panelId);
    if (!entry)
        return false;
    const int bounded = boundedContentHeight(height);
    if (entry->height == bounded
        && (activePanel != entry->id || collapsed)) {
        return true;
    }
    entry->height = bounded;
    if (activePanel == entry->id && !collapsed)
        applyContentHeight(bounded);
    notifyStateChanged();
    return true;
}

bool PanelLayoutController::resetPanelHeight(
    const QString& panelId)
{
    return setPanelHeight(panelId, kDefaultContentHeight);
}

int PanelLayoutController::maximumContentHeight() const
{
    const int available = window
        ? window->contentsRect().height() : 900;
    return qMax(kMinimumContentHeight,
                available * kMaximumHeightPercent / 100);
}

void PanelLayoutController::setPanelBadge(
    const QString& panelId,
    const QString& text,
    const QString& tone)
{
    PanelEntry* entry = entryForId(panelId);
    if (!entry)
        return;
    const QString cleanText = text.trimmed();
    const QString cleanTone = tone.trimmed().toLower();
    if (entry->badgeText == cleanText
        && entry->badgeTone == cleanTone) {
        return;
    }
    entry->badgeText = cleanText;
    entry->badgeTone = cleanTone;
    if (entry->button) {
        UiControls::setToolButtonBadge(entry->button, cleanText, cleanTone);
        entry->button->setAccessibleDescription(
            cleanText.isEmpty()
                ? QStringLiteral(
                      "Open or close the %1 bottom panel. Ctrl+J toggles the last panel.")
                      .arg(entry->label)
                : QStringLiteral("%1 status: %2")
                      .arg(entry->label, cleanText));
    }
}

QString PanelLayoutController::panelBadgeText(
    const QString& panelId) const
{
    const PanelEntry* entry = entryForId(panelId);
    return entry ? entry->badgeText : QString();
}

QString PanelLayoutController::panelBadgeTone(
    const QString& panelId) const
{
    const PanelEntry* entry = entryForId(panelId);
    return entry ? entry->badgeTone : QString();
}

void PanelLayoutController::bindManagedTabBars()
{
    // Kept as a compatibility no-op for pre-drawer session restore code.
}

void PanelLayoutController::setStateChangedHandler(
    std::function<void()> handler)
{
    stateChangedHandler = std::move(handler);
}

void PanelLayoutController::setAnimationsEnabled(bool enabled)
{
    animationsEnabledValue = enabled;
    if (!enabled && compositor && compositor->isActiveFor(this)) {
        compositor->settle();
        applyDrawerState(false);
    }
}

bool PanelLayoutController::animationsEnabled() const
{
    return animationsEnabledValue;
}

QDockWidget* PanelLayoutController::drawerDock() const
{
    return bottomDrawerDock;
}

QWidget* PanelLayoutController::drawerContent() const
{
    return bottomContentStack;
}

QWidget* PanelLayoutController::resizeHandle() const
{
    return bottomResizeHandle;
}

QWidget* PanelLayoutController::buttonBar() const
{
    return bottomButtonBar;
}

QToolButton* PanelLayoutController::buttonForPanel(
    const QString& panelId) const
{
    const PanelEntry* entry = entryForId(panelId);
    return entry ? entry->button : nullptr;
}

bool PanelLayoutController::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if (!event)
        return QObject::eventFilter(watched, event);

    if (watched == bottomButtonBar && event->type() == QEvent::LayoutRequest
        && bottomButtonBar->layout()) {
        const int height = qMax(kButtonBarHeight, bottomButtonBar->layout()->sizeHint().height() + 6);
        if (bottomButtonBar->height() != height) {
            bottomButtonBar->setFixedHeight(height);
            applyContentHeight(bottomContentStack->height());
        }
    }

    if (watched == bottomResizeHandle) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                resetPanelHeight(activePanel);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton
                && !collapsed) {
                dragging = true;
                dragStartGlobalY =
                    qRound(mouseEvent->globalPosition().y());
                dragStartHeight = panelHeight(activePanel);
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && dragging) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const int globalY =
                qRound(mouseEvent->globalPosition().y());
            setPanelHeight(
                activePanel,
                dragStartHeight + dragStartGlobalY - globalY);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease
                   && dragging) {
            dragging = false;
            return true;
        }
    }

    if (event->type() == QEvent::DynamicPropertyChange) {
        auto* propertyEvent =
            static_cast<QDynamicPropertyChangeEvent*>(event);
        if (propertyEvent->propertyName() == "bottomBadgeText"
            || propertyEvent->propertyName() == "bottomBadgeTone") {
            auto* dock = qobject_cast<QDockWidget*>(watched);
            for (PanelEntry& entry : panels) {
                if (entry.dock != dock)
                    continue;
                setPanelBadge(
                    entry.id,
                    dock->property("bottomBadgeText").toString(),
                    dock->property("bottomBadgeTone").toString());
                break;
            }
        }
    }

    if (event->type() == QEvent::FocusIn) {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        if (widget && bottomDrawerRoot
            && widget != bottomDrawerRoot
            && !bottomDrawerRoot->isAncestorOf(widget)) {
            focusBeforeDrawer = widget;
        }
    }
    if (watched == window && event->type() == QEvent::Resize
        && !collapsed && !applyingDrawerGeometry) {
        if (PanelEntry* entry = entryForId(activePanel))
            applyContentHeight(boundedContentHeight(entry->height));
    }
    if ((watched == window || watched == bottomDrawerRoot)
        && (event->type() == QEvent::PaletteChange
            || event->type() == QEvent::StyleChange)
        && !applyingDrawerStyle) {
        updateDrawerStyle();
    }
    return QObject::eventFilter(watched, event);
}

PanelLayoutController::PanelEntry*
PanelLayoutController::entryForId(const QString& panelId)
{
    const QString id = canonicalPanelId(panelId);
    for (PanelEntry& entry : panels) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

const PanelLayoutController::PanelEntry*
PanelLayoutController::entryForId(const QString& panelId) const
{
    const QString id = canonicalPanelId(panelId);
    for (const PanelEntry& entry : panels) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

QString PanelLayoutController::canonicalPanelId(
    const QString& panelId) const
{
    const QString id = panelId.trimmed();
    return aliases.value(id, id);
}

int PanelLayoutController::boundedContentHeight(int height) const
{
    return qBound(kMinimumContentHeight,
                  height,
                  maximumContentHeight());
}

int PanelLayoutController::visibleContentHeight() const
{
    if (!bottomContentStack || !bottomContentStack->isVisible())
        return 0;
    return qMax(0, bottomContentStack->height());
}

void PanelLayoutController::activatePanel(
    PanelEntry& entry,
    bool moveFocus)
{
    if (activePanel != entry.id && compositor) compositor->settleFor(this);
    if (entry.id == QStringLiteral("connections") && mainAreaRequest && entry.content) {
        mainAreaRequest(entry.content, bottomContentStack);
        return;
    }
    if (PanelEntry* current = entryForId(activePanel))
        capturePanelViewState(*current);
    activePanel = entry.id;
    lastPanel = entry.id;
    collapsed = false;
    if (bottomContentStack && entry.content)
        UiControls::selectPage(bottomContentStack, bottomContentStack->indexOf(entry.content),
                              animationsEnabledValue && visibleContentHeight() > 0);
    restorePanelViewState(entry);
    applyDrawerState(true);
    updateButtons();
    if (moveFocus && entry.content)
        entry.content->setFocus(Qt::ShortcutFocusReason);
    notifyStateChanged();
}

void PanelLayoutController::applyDrawerState(bool animate)
{
    if (!bottomDrawerDock || panels.isEmpty())
        return;
    PanelEntry* entry = entryForId(activePanel);
    if (!collapsed && !entry) return;
    const bool wasOpen = visibleContentHeight() > 0;
    const int target = collapsed ? 0 : boundedContentHeight(entry->height);
    const auto apply = [this, target] {
        bottomDrawerDock->show();
        bottomContentSurface->setVisible(target > 0);
        bottomResizeHandle->setVisible(target > 0);
        applyContentHeight(target);
    };
    if (animate && animationsEnabledValue && compositor && window->isVisible()
        && (wasOpen != !collapsed || compositor->isActiveFor(this))) {
        compositor->reveal(this, Qt::BottomEdge, !collapsed, [this] {
            return QRect(bottomDrawerRoot->mapTo(window, QPoint()),
                         QSize(bottomDrawerRoot->width(), bottomDrawerRoot->height() - bottomButtonBar->height()));
        }, apply, bottomButtonBar->height());
    } else {
        if (compositor) compositor->settle();
        apply();
    }
}

void PanelLayoutController::applyContentHeight(
    int height,
    bool settleDock)
{
    if (!bottomContentStack || !bottomDrawerDock
        || applyingDrawerGeometry)
        return;
    applyingDrawerGeometry = true;
    const int safeHeight = qMax(0, height);
    bottomContentStack->setMinimumHeight(safeHeight);
    bottomContentStack->setMaximumHeight(safeHeight);
    bottomContentSurface->setFixedHeight(safeHeight);
    const int handleHeight = safeHeight > 0 ? kResizeHandleHeight : 0;
    const int totalHeight = safeHeight + handleHeight + bottomButtonBar->height();
    bottomDrawerDock->setMinimumHeight(totalHeight);
    bottomDrawerDock->setMaximumHeight(totalHeight);
    if (settleDock) {
        bottomDrawerRoot->updateGeometry();
        bottomDrawerDock->updateGeometry();
    }
    if (settleDock && window && bottomDrawerDock->isVisible()) {
        window->resizeDocks(
            {bottomDrawerDock}, {totalHeight}, Qt::Vertical);
    }
    applyingDrawerGeometry = false;
}

void PanelLayoutController::updateButtons()
{
    const bool expanded = !collapsed;
    for (PanelEntry& entry : panels) {
        if (!entry.button)
            continue;
        entry.button->setVisible(entry.id == QStringLiteral("problems")
            || entry.id == QStringLiteral("activity")
            || (entry.id != QStringLiteral("connections") && expanded && entry.id == activePanel));
        entry.button->setChecked(expanded && entry.id == activePanel);
        entry.button->setProperty(
            "activePanel", expanded && entry.id == activePanel);
        entry.button->setAccessibleDescription(
            entry.badgeText.isEmpty()
                ? QStringLiteral("%1 the %2 bottom panel. Ctrl+J toggles the last panel.")
                      .arg(expanded && entry.id == activePanel
                               ? QStringLiteral("Close")
                               : QStringLiteral("Open"),
                           entry.label)
                : QStringLiteral("%1 status: %2")
                      .arg(entry.label, entry.badgeText));
    }
}

void PanelLayoutController::updateDrawerStyle()
{
    if (!bottomDrawerRoot || applyingDrawerStyle)
        return;
    applyingDrawerStyle = true;
    const InsightTheme& theme = InsightVisualStyle::theme();
    const QColor background = theme.surface.panel;
    const QColor text = theme.textPrimary;
    const QColor border = InsightVisualStyle::subtleBorder(background, text);
    const QColor hover = theme.button.backgroundHover;
    const QColor active = theme.itemView.selectedBackground;
    const QColor accent = theme.focus.ring;
    bottomDrawerRoot->setStyleSheet(QStringLiteral(
        "QWidget#bottomToolDrawerRoot { background: %1; }"
        "QWidget#bottomToolDrawerResizeHandle {"
        " background: %1; border-top: 1px solid %2; }"
        "QWidget#bottomToolDrawerResizeHandle:hover { background: %3; }"
        "QFrame#bottomToolDrawerButtonBar {"
        " background: %1; border-top: 1px solid %2; }"
        "QToolButton[zeroslackElaControl=false] { color: %4; border: 0; border-radius: 8px;"
        " padding: 0 11px 0 9px; min-height: 30px; }"
        "QToolButton[zeroslackElaControl=false]:hover { background: %3; }"
        "QToolButton[zeroslackElaControl=false]:checked { background: %5; font-weight: 600;"
        " border-bottom: 2px solid %6; }"
        "QToolButton[zeroslackElaControl=false]:focus { outline: none;"
        " border: %7px solid %6; }"
        "QToolButton[zeroslackElaControl=false]:checked:focus { border: %7px solid %6;"
        " border-bottom: 2px solid %6; }"
    ).arg(colorCss(background),
          colorCss(border),
          colorCss(hover),
          colorCss(text),
          colorCss(active),
          colorCss(accent))
        .arg(theme.focus.width));
    applyingDrawerStyle = false;
}

void PanelLayoutController::capturePanelViewState(
    PanelEntry& entry) const
{
    if (entry.content)
        entry.viewState = captureWidgetState(entry.content);
}

void PanelLayoutController::restorePanelViewState(PanelEntry& entry)
{
    if (entry.content && !entry.viewState.isEmpty())
        restoreWidgetState(entry.content, entry.viewState);
}

QVariantMap PanelLayoutController::captureWidgetState(
    QWidget* root) const
{
    QVariantMap state;
    if (!root)
        return state;
    if (auto* deferred = dynamic_cast<DeferredPanel*>(root);
        deferred && !deferred->isCreated()) {
        return root->property("pendingPanelViewState").toMap();
    }

    QVariantMap tabs;
    QList<QTabWidget*> tabWidgets = root->findChildren<QTabWidget*>();
    if (auto* self = qobject_cast<QTabWidget*>(root))
        tabWidgets.prepend(self);
    for (QTabWidget* tabsWidget : std::as_const(tabWidgets)) {
        if (tabsWidget && !tabsWidget->objectName().isEmpty())
            tabs.insert(tabsWidget->objectName(), tabsWidget->currentIndex());
    }
    if (!tabs.isEmpty())
        state.insert(QStringLiteral("tabs"), tabs);

    QVariantMap scrolls;
    QList<QAbstractScrollArea*> scrollAreas =
        root->findChildren<QAbstractScrollArea*>();
    if (auto* self = qobject_cast<QAbstractScrollArea*>(root))
        scrollAreas.prepend(self);
    for (QAbstractScrollArea* area : std::as_const(scrollAreas)) {
        if (!area || area->objectName().isEmpty())
            continue;
        QVariantMap scroll;
        scroll.insert(QStringLiteral("vertical"),
                      area->verticalScrollBar()->value());
        scroll.insert(QStringLiteral("horizontal"),
                      area->horizontalScrollBar()->value());
        scrolls.insert(area->objectName(), scroll);
    }
    if (!scrolls.isEmpty())
        state.insert(QStringLiteral("scrolls"), scrolls);

    QVariantMap selections;
    QList<QAbstractItemView*> views = root->findChildren<QAbstractItemView*>();
    if (auto* self = qobject_cast<QAbstractItemView*>(root))
        views.prepend(self);
    for (QAbstractItemView* view : std::as_const(views)) {
        if (!view || view->objectName().isEmpty()
            || !view->currentIndex().isValid()) {
            continue;
        }
        QVariantMap selection;
        selection.insert(QStringLiteral("path"),
                         modelIndexPath(view->currentIndex()));
        selection.insert(QStringLiteral("column"),
                         view->currentIndex().column());
        selections.insert(view->objectName(), selection);
    }
    if (!selections.isEmpty())
        state.insert(QStringLiteral("selections"), selections);
    return state;
}

void PanelLayoutController::restoreWidgetState(
    QWidget* root,
    const QVariantMap& state)
{
    if (!root)
        return;
    if (auto* deferred = dynamic_cast<DeferredPanel*>(root);
        deferred && !deferred->isCreated()) {
        root->setProperty("pendingPanelViewState", state);
        deferred->afterCreated([owner = QPointer<PanelLayoutController>(this),
                                target = QPointer<QWidget>(root)] {
            if (owner && target) {
                owner->restoreWidgetState(
                    target, target->property("pendingPanelViewState").toMap());
                target->setProperty("pendingPanelViewState", QVariant());
            }
        });
        return;
    }
    const QVariantMap tabs = state.value(QStringLiteral("tabs")).toMap();
    for (auto iterator = tabs.cbegin(); iterator != tabs.cend(); ++iterator) {
        QTabWidget* widget = root->objectName() == iterator.key()
            ? qobject_cast<QTabWidget*>(root)
            : root->findChild<QTabWidget*>(iterator.key());
        if (widget)
            widget->setCurrentIndex(
                qBound(0, iterator.value().toInt(),
                       qMax(0, widget->count() - 1)));
    }

    const QVariantMap scrolls =
        state.value(QStringLiteral("scrolls")).toMap();
    for (auto iterator = scrolls.cbegin();
         iterator != scrolls.cend(); ++iterator) {
        QAbstractScrollArea* area = root->objectName() == iterator.key()
            ? qobject_cast<QAbstractScrollArea*>(root)
            : root->findChild<QAbstractScrollArea*>(iterator.key());
        if (!area)
            continue;
        const QVariantMap scroll = iterator.value().toMap();
        area->verticalScrollBar()->setValue(
            scroll.value(QStringLiteral("vertical")).toInt());
        area->horizontalScrollBar()->setValue(
            scroll.value(QStringLiteral("horizontal")).toInt());
    }

    const QVariantMap selections =
        state.value(QStringLiteral("selections")).toMap();
    for (auto iterator = selections.cbegin();
         iterator != selections.cend(); ++iterator) {
        QAbstractItemView* view = root->objectName() == iterator.key()
            ? qobject_cast<QAbstractItemView*>(root)
            : root->findChild<QAbstractItemView*>(iterator.key());
        if (!view || !view->model())
            continue;
        const QVariantMap selection = iterator.value().toMap();
        const QModelIndex index = modelIndexFromPath(
            view->model(),
            selection.value(QStringLiteral("path")).toString(),
            selection.value(QStringLiteral("column")).toInt());
        if (index.isValid()) {
            view->setCurrentIndex(index);
            view->scrollTo(index);
        }
    }
}

void PanelLayoutController::restoreEditorFocus()
{
    if (focusBeforeDrawer && focusBeforeDrawer->isVisible()) {
        focusBeforeDrawer->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (window && window->centralWidget())
        window->centralWidget()->setFocus(Qt::OtherFocusReason);
}

bool PanelLayoutController::focusIsInsideDrawer() const
{
    QWidget* focus = QApplication::focusWidget();
    return focus && bottomDrawerRoot
        && (focus == bottomDrawerRoot
            || bottomDrawerRoot->isAncestorOf(focus));
}

void PanelLayoutController::notifyStateChanged()
{
    if (!applying && stateChangedHandler)
        stateChangedHandler();
}
