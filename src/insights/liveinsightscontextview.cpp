#include "liveinsightscontextview.h"
#include "compactlayout.h"

#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "liveinsightsession.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {
const QString kViewStateSchema =
    QStringLiteral("zeroslack-live-insights-view/v1");

LiveInsightKind kindForIndex(int index)
{
    switch (index) {
    case 1:
        return LiveInsightKind::Module;
    case 2:
        return LiveInsightKind::Hotspot;
    case 3:
        return LiveInsightKind::State;
    default:
        return LiveInsightKind::Kernel;
    }
}

QString phaseText(const LiveInsightSnapshot& snapshot)
{
    switch (snapshot.phase) {
    case LiveInsightPhase::Empty:
        return QStringLiteral("Waiting");
    case LiveInsightPhase::Debouncing:
        return snapshot.hasLastValid
            ? QStringLiteral("Stale · queued")
            : QStringLiteral("Queued");
    case LiveInsightPhase::Building:
        return snapshot.hasLastValid
            ? QStringLiteral("Stale · updating")
            : QStringLiteral("Updating");
    case LiveInsightPhase::Ready:
        return snapshot.stale
            ? QStringLiteral("Stale")
            : QStringLiteral("Current");
    case LiveInsightPhase::HiddenDirty:
        return QStringLiteral("Update pending");
    case LiveInsightPhase::Error:
        return snapshot.hasLastValid
            ? QStringLiteral("Stale · error")
            : QStringLiteral("Error");
    }
    return QStringLiteral("Waiting");
}

InsightStatusTone phaseTone(const LiveInsightSnapshot& snapshot)
{
    if (snapshot.phase == LiveInsightPhase::Error)
        return InsightStatusTone::Error;
    if (snapshot.stale
        || snapshot.phase == LiveInsightPhase::HiddenDirty) {
        return InsightStatusTone::Warning;
    }
    if (snapshot.phase == LiveInsightPhase::Ready)
        return InsightStatusTone::Success;
    return InsightStatusTone::Info;
}

QString summaryText(const LiveInsightSnapshot& snapshot)
{
    QString summary =
        snapshot.payload.value(QStringLiteral("summary")).toString();
    if (summary.trimmed().isEmpty()) {
        summary =
            snapshot.payload.value(QStringLiteral("title")).toString();
    }

    if (snapshot.hasLastValid && snapshot.stale) {
        summary = summary.trimmed().isEmpty()
            ? QStringLiteral("Showing the last valid result.")
            : QStringLiteral("Showing the last valid result.\n\n%1")
                  .arg(summary);
    }
    if (!snapshot.errorText.trimmed().isEmpty()) {
        summary = summary.trimmed().isEmpty()
            ? snapshot.errorText
            : QStringLiteral("%1\n\n%2")
                  .arg(summary, snapshot.errorText);
    }
    if (!summary.trimmed().isEmpty())
        return summary;
    if (snapshot.phase == LiveInsightPhase::HiddenDirty) {
        return QStringLiteral(
            "An update is pending and will run when this insight becomes visible.");
    }
    if (snapshot.phase == LiveInsightPhase::Debouncing)
        return QStringLiteral("Waiting for the current edit burst to settle.");
    if (snapshot.phase == LiveInsightPhase::Building)
        return QStringLiteral("Building the latest semantic insight.");
    return QStringLiteral(
        "Follow the editor or select source context to populate this insight.");
}
}

LiveInsightsContextView::LiveInsightsContextView(
    LiveInsightSession* session,
    QWidget* parent)
    : QWidget(parent)
    , sessionValue(session)
{
    initialize();
}

LiveInsightsContextView::LiveInsightsContextView(
    LiveInsightSession* session,
    LiveInsightKind fixedKind,
    QWidget* parent)
    : QWidget(parent)
    , sessionValue(session)
    , selected(fixedKind)
    , fixedKindValue(true)
{
    initialize();
}

void LiveInsightsContextView::initialize()
{
    setObjectName(QStringLiteral("liveInsightsContextView"));
    buildUi();
    if (sessionValue) {
        connect(
            sessionValue,
            &LiveInsightSession::snapshotChanged,
            this,
            &LiveInsightsContextView::refreshSnapshot);
        for (int index = 0; index < static_cast<int>(cards.size()); ++index) {
            const LiveInsightKind kind = kindForIndex(index);
            refreshSnapshot(kind, sessionValue->snapshot(kind));
        }
    }
    connect(
        &ApplicationThemeManager::instance(),
        &ApplicationThemeManager::themeChanged,
        this,
        [this](ThemeMode) { refreshTheme(); });
    refreshTheme();
}

LiveInsightsContextView::~LiveInsightsContextView()
{
    if (sessionValue) {
        sessionValue->setConsumerVisible(
            this, selected, false);
    }
}

LiveInsightSession* LiveInsightsContextView::session() const
{
    return sessionValue;
}

LiveInsightKind LiveInsightsContextView::selectedKind() const
{
    return selected;
}

void LiveInsightsContextView::setSelectedKind(LiveInsightKind kind)
{
    const int index = indexForKind(kind);
    if (index < 0 || (fixedKindValue && kind != selected))
        return;
    if (selected == kind) {
        if (cards.at(index).button)
            cards.at(index).button->setChecked(true);
        if (contentStack)
            contentStack->setCurrentIndex(index);
        return;
    }

    const LiveInsightKind previous = selected;
    selected = kind;
    if (cards.at(index).button)
        cards.at(index).button->setChecked(true);
    if (contentStack)
        contentStack->setCurrentIndex(index);
    updateSessionVisibility(previous, selected);
    emit selectedKindChanged(selected);
}

bool LiveInsightsContextView::followEditor() const
{
    return followCheck && followCheck->isChecked();
}

void LiveInsightsContextView::setFollowEditor(bool follow)
{
    if (!followCheck)
        return;
    const bool changed = followCheck->isChecked() != follow;
    followCheck->setChecked(follow);
    if (follow && !changed)
        refreshAllFromSession();
}

bool LiveInsightsContextView::pinned() const
{
    return pinToggle && pinToggle->isChecked();
}

void LiveInsightsContextView::setPinned(bool pinnedValue)
{
    if (!pinToggle)
        return;
    const bool changed = pinToggle->isChecked() != pinnedValue;
    {
        const QSignalBlocker blocker(pinToggle);
        pinToggle->setChecked(pinnedValue);
    }
    pinToggle->setText(
        pinnedValue ? QStringLiteral("Pinned")
                    : QStringLiteral("Pin"));
    pinToggle->setToolTip(
        pinnedValue
            ? QStringLiteral("Keep Live Insights pinned to the workspace")
            : QStringLiteral("Pin Live Insights to the workspace"));
    if (changed)
        emit pinnedChanged(pinnedValue);
}

QString LiveInsightsContextView::workspaceId() const
{
    return workspaceIdValue;
}

void LiveInsightsContextView::setWorkspaceId(
    const QString& workspaceId)
{
    workspaceIdValue = workspaceId;
}

void LiveInsightsContextView::setFullViewHandler(
    FullViewHandler handler)
{
    fullViewHandler = std::move(handler);
}

void LiveInsightsContextView::setToolContextSource(
    ToolContextSource source)
{
    toolContextSource = std::move(source);
    // renderSurface builds the surface only once the section is actually
    // visible, so installing a source on a collapsed section stays cheap.
    renderSurface();
}

LiveInsightToolPage* LiveInsightsContextView::surfaceForTest() const
{
    return surfaceValue;
}

bool LiveInsightsContextView::surfaceEnabled() const
{
    // Only a fixed-kind section owns one insight, so only it can be replaced
    // by that insight's real surface. The multi-kind view stays a compact
    // board over all four and keeps its summaries.
    return fixedKindValue && static_cast<bool>(toolContextSource);
}

void LiveInsightsContextView::ensureSurface()
{
    if (surfaceValue || !surfaceEnabled() || !contentStack)
        return;
    const int index = indexForKind(selected);
    auto* page = contentStack->widget(index);
    if (!page || !page->layout())
        return;
    surfaceValue = new LiveInsightToolPage(selected, page);
    surfaceValue->setObjectName(
        QStringLiteral("liveInsightSurface_%1")
            .arg(liveInsightKindId(selected)));
    surfaceValue->setCompactChrome(true);
    if (QLabel* summary = cards.at(index).summary)
        summary->hide();
    page->layout()->addWidget(surfaceValue);
    // The surface nests its own main-window layout. Inserting it after the
    // section already has a size leaves that nested layout on the geometry it
    // was built with, so re-activate it once this event round settles.
    QTimer::singleShot(0, this, [this]() {
        if (!surfaceValue)
            return;
        const auto layouts = surfaceValue->findChildren<QLayout*>();
        for (auto* nested : layouts) {
            nested->invalidate();
            nested->activate();
        }
        if (QLayout* own = layout()) {
            own->invalidate();
            own->activate();
        }
        updateGeometry();
    });
}

LiveInsightToolContext LiveInsightsContextView::effectiveContext() const
{
    LiveInsightToolContext context =
        toolContextSource ? toolContextSource() : LiveInsightToolContext{};
    if (targetOverrideActive) {
        if (!targetOverride.moduleName.trimmed().isEmpty())
            context.moduleName = targetOverride.moduleName;
        context.signalName = targetOverride.signalName;
        context.signalAccessPath = targetOverride.signalAccessPath;
        // Keep the selected scope with the pinned target context.
        if (targetOverride.scopeStartPosition >= 0
            && targetOverride.scopeEndPosition
                   > targetOverride.scopeStartPosition) {
            context.scopeLabel = targetOverride.scopeLabel;
            context.scopeStartPosition = targetOverride.scopeStartPosition;
            context.scopeEndPosition = targetOverride.scopeEndPosition;
            context.scopeStartLineZeroBased =
                targetOverride.scopeStartLineZeroBased;
        }
    }
    return context;
}

bool LiveInsightsContextView::contextHasTarget(
    const LiveInsightToolContext& context) const
{
    switch (selected) {
    case LiveInsightKind::Kernel:
    case LiveInsightKind::Hotspot:
        return !context.signalName.trimmed().isEmpty();
    case LiveInsightKind::Module:
    case LiveInsightKind::State:
        return !context.moduleName.trimmed().isEmpty();
    }
    return false;
}

void LiveInsightsContextView::rememberTarget(
    const LiveInsightToolContext& context)
{
    TargetCandidate candidate;
    candidate.moduleName = context.moduleName.trimmed();
    candidate.signalName = context.signalName.trimmed();
    candidate.signalAccessPath = context.signalAccessPath.trimmed();
    switch (selected) {
    case LiveInsightKind::Kernel:
    case LiveInsightKind::Hotspot:
        candidate.label = candidate.signalName;
        break;
    case LiveInsightKind::Module:
    case LiveInsightKind::State:
        candidate.label = candidate.moduleName;
        break;
    }
    if (candidate.label.isEmpty())
        return;
    recentTargets.removeAll(candidate);
    recentTargets.prepend(candidate);
    while (recentTargets.size() > 3)
        recentTargets.removeLast();
}

QList<LiveInsightsContextView::TargetCandidate>
LiveInsightsContextView::candidateTargets() const
{
    QList<TargetCandidate> candidates;
    const LiveInsightToolContext current = effectiveContext();
    // The editor context is a candidate exactly when the section is not
    // already showing it: frozen by Follow Editor, or pinned elsewhere.
    if (toolContextSource) {
        const LiveInsightToolContext editorContext = toolContextSource();
        if (contextHasTarget(editorContext)) {
            TargetCandidate candidate;
            candidate.moduleName = editorContext.moduleName.trimmed();
            candidate.signalName = editorContext.signalName.trimmed();
            candidate.signalAccessPath =
                editorContext.signalAccessPath.trimmed();
            candidate.label = candidate.signalName.isEmpty()
                ? candidate.moduleName
                : candidate.signalName;
            if (!candidate.label.isEmpty()
                && !(candidate.moduleName == current.moduleName.trimmed()
                     && candidate.signalName == current.signalName.trimmed())) {
                candidates.append(candidate);
            }
        }
    }
    for (const TargetCandidate& candidate : recentTargets) {
        if (candidates.contains(candidate))
            continue;
        if (candidate.moduleName == current.moduleName.trimmed()
            && candidate.signalName == current.signalName.trimmed()) {
            continue;
        }
        candidates.append(candidate);
    }
    return candidates;
}

bool LiveInsightsContextView::applyTargetCandidate(
    const TargetCandidate& candidate)
{
    if (candidate.label.trimmed().isEmpty())
        return false;
    targetOverride = candidate;
    targetOverrideActive = true;
    // A chosen target is a target, not a subscription: stop following the
    // cursor so the choice survives the next editor move.
    setFollowEditor(false);
    renderSurface();
    publishSectionScope();
    return true;
}

void LiveInsightsContextView::setTargetPickRequest(
    TargetPickRequest request)
{
    targetPickRequest = std::move(request);
    publishSectionScope();
}

bool LiveInsightsContextView::requestScopePick()
{
    if (!targetPickRequest)
        return false;
    const QPointer<LiveInsightsContextView> guard(this);
    return targetPickRequest(
        selected,
        [guard](const TargetCandidate& candidate) {
            // The section that asked may have been closed while the user was
            // picking; a stale reply must not resurrect it.
            if (guard)
                guard->applyTargetCandidate(candidate);
        });
}

void LiveInsightsContextView::publishSectionScope()
{
    // Only a fixed-kind section has one target to re-pick, and only a host
    // that can run the picker should show the chip at all.
    if (!fixedKindValue || !targetPickRequest) {
        setProperty("contextScopeText", QString());
        setProperty("contextScopeTooltip", QString());
        return;
    }
    const LiveInsightToolContext context = effectiveContext();
    QString label;
    switch (selected) {
    case LiveInsightKind::Kernel:
    case LiveInsightKind::Hotspot:
        label = context.signalName.trimmed();
        break;
    case LiveInsightKind::Module:
    case LiveInsightKind::State:
        label = context.moduleName.trimmed();
        break;
    }
    setProperty("contextScopeText",
                label.isEmpty() ? QStringLiteral("Pick target") : label);
    setProperty(
        "contextScopeTooltip",
        label.isEmpty()
            ? QStringLiteral("Pick this insight's target in the editor")
            : QStringLiteral("Target: %1 — click to pick another in the editor")
                  .arg(label));
}

QWidget* LiveInsightsContextView::emptyStateForTest() const
{
    return emptyStateValue;
}

void LiveInsightsContextView::refreshEmptyState(
    const LiveInsightToolContext& context)
{
    const bool hasTarget = contextHasTarget(context);
    if (hasTarget && !emptyStateValue)
        return;
    if (!emptyStateValue) {
        auto* page = contentStack
            ? contentStack->widget(indexForKind(selected))
            : nullptr;
        auto* column = page
            ? qobject_cast<QVBoxLayout*>(page->layout())
            : nullptr;
        if (!column)
            return;
        emptyStateValue = new QWidget(page);
        emptyStateValue->setObjectName(
            QStringLiteral("liveInsightEmptyState_%1")
                .arg(liveInsightKindId(selected)));
        auto* rows = new QVBoxLayout(emptyStateValue);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(4);
        emptyStateList = rows;
        column->insertWidget(0, emptyStateValue);
    }
    emptyStateValue->setVisible(!hasTarget);
    if (surfaceValue)
        surfaceValue->setVisible(hasTarget);
    if (hasTarget || !emptyStateList)
        return;

    while (QLayoutItem* item = emptyStateList->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Detach before scheduling deletion: a rebuild triggered from a
            // candidate's own click must not leave the clicked button both
            // alive and findable as a child.
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    const QList<TargetCandidate> candidates = candidateTargets();
    for (const TargetCandidate& candidate : candidates) {
        auto* row = new QPushButton(candidate.label, emptyStateValue);
        row->setObjectName(QStringLiteral("liveInsightCandidate"));
        row->setToolTip(
            candidate.moduleName.isEmpty()
                ? candidate.label
                : QStringLiteral("%1 · %2")
                      .arg(candidate.moduleName, candidate.label));
        InsightVisualStyle::applyToolbarButton(row);
        connect(row, &QPushButton::clicked, this,
                [this, candidate]() { applyTargetCandidate(candidate); });
        emptyStateList->addWidget(row);
    }
    if (targetPickRequest) {
        // The header chip is the usual entry; this one also covers a section
        // dragged out into a floating window, which has no section header.
        auto* pick = new QPushButton(
            QStringLiteral("Pick in editor…"), emptyStateValue);
        pick->setObjectName(QStringLiteral("liveInsightPickTarget"));
        pick->setToolTip(
            QStringLiteral("Blink the targets this insight can render"));
        InsightVisualStyle::applyToolbarButton(pick);
        connect(pick, &QPushButton::clicked, this,
                [this]() { requestScopePick(); });
        emptyStateList->addWidget(pick);
    }
    auto* hint = new QLabel(emptyStateValue);
    hint->setObjectName(QStringLiteral("liveInsightEmptyHint"));
    hint->setWordWrap(true);
    hint->setText(candidates.isEmpty()
                      ? QStringLiteral(
                            "Select a target in the editor to populate this insight.")
                      : QStringLiteral("Or select a target in the editor."));
    InsightVisualStyle::applyLabel(hint);
    emptyStateList->addWidget(hint);
}

void LiveInsightsContextView::renderSurface()
{
    if (!surfaceEnabled())
        return;
    if (!isVisible()) {
        // Building a workbench for a collapsed or hidden section wastes the
        // work; remember that it owes a render instead.
        surfaceRenderPending = true;
        return;
    }
    const LiveInsightToolContext context = effectiveContext();
    if (!contextHasTarget(context)) {
        // Nothing to render: offer the targets this section can reach instead
        // of building a workbench that would only draw an empty frame.
        surfaceRenderPending = false;
        refreshEmptyState(context);
        return;
    }
    ensureSurface();
    if (!surfaceValue)
        return;
    surfaceRenderPending = false;
    refreshEmptyState(context);
    surfaceValue->setContext(context);
    rememberTarget(context);
}

QVariantMap LiveInsightsContextView::saveState() const
{
    return {
        {QStringLiteral("schema"), kViewStateSchema},
        {QStringLiteral("kind"), liveInsightKindId(selected)},
        {QStringLiteral("followEditor"), followEditor()},
        {QStringLiteral("pinned"), pinned()}
    };
}

void LiveInsightsContextView::restoreState(
    const QVariantMap& state)
{
    const QString schema =
        state.value(QStringLiteral("schema")).toString();
    if (!schema.isEmpty() && schema != kViewStateSchema)
        return;

    if (state.contains(QStringLiteral("followEditor"))) {
        setFollowEditor(
            state.value(QStringLiteral("followEditor")).toBool());
    }
    if (state.contains(QStringLiteral("pinned"))) {
        setPinned(state.value(QStringLiteral("pinned")).toBool());
    }
    LiveInsightKind restoredKind = selected;
    if (liveInsightKindFromId(
            state.value(QStringLiteral("kind")).toString(),
            &restoredKind)) {
        setSelectedKind(restoredKind);
    }
}

QPushButton* LiveInsightsContextView::kindButton(
    LiveInsightKind kind) const
{
    const int index = indexForKind(kind);
    return index >= 0 ? cards.at(index).button : nullptr;
}

QLabel* LiveInsightsContextView::kindStatusLabel(
    LiveInsightKind kind) const
{
    const int index = indexForKind(kind);
    return index >= 0 ? cards.at(index).status : nullptr;
}

QLabel* LiveInsightsContextView::kindSummaryLabel(
    LiveInsightKind kind) const
{
    const int index = indexForKind(kind);
    return index >= 0 ? cards.at(index).summary : nullptr;
}

QCheckBox* LiveInsightsContextView::followEditorCheckBox() const
{
    return followCheck;
}

QPushButton* LiveInsightsContextView::pinButton() const
{
    return pinToggle;
}

QPushButton* LiveInsightsContextView::openFullViewButton() const
{
    return fullViewButton;
}

bool LiveInsightsContextView::hasFixedKind() const
{
    return fixedKindValue;
}

void LiveInsightsContextView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (sessionValue) {
        sessionValue->setConsumerVisible(
            this, selected, followEditor());
    }
    if (surfaceRenderPending || (surfaceEnabled() && !surfaceValue))
        renderSurface();
}

void LiveInsightsContextView::hideEvent(QHideEvent* event)
{
    if (sessionValue) {
        sessionValue->setConsumerVisible(
            this, selected, false);
    }
    QWidget::hideEvent(event);
}

int LiveInsightsContextView::indexForKind(LiveInsightKind kind)
{
    switch (kind) {
    case LiveInsightKind::Kernel:
        return 0;
    case LiveInsightKind::Module:
        return 1;
    case LiveInsightKind::Hotspot:
        return 2;
    case LiveInsightKind::State:
        return 3;
    }
    return 0;
}

void LiveInsightsContextView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(6);
    auto* title = new CompactTitleLabel(this);
    title->setText(QStringLiteral("RTL Insight Workbench"));
    title->setObjectName(QStringLiteral("liveInsightsContextTitle"));
    InsightVisualStyle::applyTitleLabel(title);
    titleRow->addWidget(title, 1);

    pinToggle = new QPushButton(QStringLiteral("Pin"), this);
    pinToggle->setObjectName(QStringLiteral("liveInsightsPin"));
    pinToggle->setCheckable(true);
    InsightVisualStyle::applyToolbarButton(pinToggle);
    titleRow->addWidget(pinToggle);

    fullViewButton = new QPushButton(
        QStringLiteral("Open Full View"), this);
    fullViewButton->setObjectName(
        QStringLiteral("liveInsightsOpenFullView"));
    fullViewButton->setToolTip(
        QStringLiteral("Open the selected insight beside the editor"));
    InsightVisualStyle::applyToolbarButton(fullViewButton);
    titleRow->addWidget(fullViewButton);
    root->addLayout(titleRow);

    followCheck = new QCheckBox(
        QStringLiteral("Follow Editor"), this);
    followCheck->setObjectName(
        QStringLiteral("liveInsightsFollowEditor"));
    followCheck->setChecked(true);
    followCheck->setToolTip(
        QStringLiteral("Track the active editor context"));
    InsightVisualStyle::applySegmentedCheckBox(followCheck);
    root->addWidget(followCheck);

    auto* cardGrid = new QGridLayout;
    cardGrid->setContentsMargins(0, 0, 0, 0);
    cardGrid->setHorizontalSpacing(6);
    cardGrid->setVerticalSpacing(6);
    auto* buttonGroup = new QButtonGroup(this);
    buttonGroup->setExclusive(true);

    contentStack = new QStackedWidget(this);
    contentStack->setObjectName(
        QStringLiteral("liveInsightsContentStack"));
    for (int index = 0; index < static_cast<int>(cards.size()); ++index) {
        const LiveInsightKind kind = kindForIndex(index);
        const QString kindId = liveInsightKindId(kind);
        auto* card = new QFrame(this);
        card->setObjectName(
            QStringLiteral("liveInsightCard_%1").arg(kindId));
        card->setFrameShape(QFrame::NoFrame);
        InsightVisualStyle::applyPanel(card);
        card->setVisible(!fixedKindValue || kind == selected);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(5, 5, 5, 5);
        cardLayout->setSpacing(3);

        auto* button = new QPushButton(
            liveInsightKindDisplayName(kind), card);
        button->setObjectName(
            QStringLiteral("liveInsightKind_%1").arg(kindId));
        button->setCheckable(true);
        button->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Preferred);
        InsightVisualStyle::applyToolbarButton(button);
        buttonGroup->addButton(button, index);
        cardLayout->addWidget(button);

        auto* status = new QLabel(QStringLiteral("Waiting"), card);
        status->setObjectName(
            QStringLiteral("liveInsightStatus_%1").arg(kindId));
        status->setAlignment(Qt::AlignCenter);
        status->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Preferred);
        cardLayout->addWidget(status);
        cardGrid->addWidget(card, index / 2, index % 2);

        auto* page = new QWidget(contentStack);
        page->setObjectName(
            QStringLiteral("liveInsightPage_%1").arg(kindId));
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(8, 8, 8, 8);
        auto* summary = new QLabel(page);
        summary->setObjectName(
            QStringLiteral("liveInsightSummary_%1").arg(kindId));
        summary->setWordWrap(true);
        summary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        InsightVisualStyle::applyLabel(summary);
        pageLayout->addWidget(summary, 1);
        contentStack->addWidget(page);

        cards.at(index) = {button, status, summary};
        connect(
            button,
            &QPushButton::clicked,
            this,
            [this, kind]() { setSelectedKind(kind); });
    }
    const int selectedIndex = indexForKind(selected);
    if (selectedIndex >= 0) {
        cards.at(selectedIndex).button->setChecked(true);
        contentStack->setCurrentIndex(selectedIndex);
    }
    root->addLayout(cardGrid);
    root->addWidget(contentStack, 1);

    connect(
        followCheck,
        &QCheckBox::toggled,
        this,
        [this](bool follow) {
            if (sessionValue && isVisible()) {
                sessionValue->setConsumerVisible(
                    this, selected, follow);
            }
            if (follow)
                refreshAllFromSession();
            emit followEditorChanged(follow);
        });
    connect(
        pinToggle,
        &QPushButton::toggled,
        this,
        [this](bool checked) {
            pinToggle->setText(
                checked ? QStringLiteral("Pinned")
                        : QStringLiteral("Pin"));
            emit pinnedChanged(checked);
            emit pinStateChangeRequested(checked);
        });
    connect(
        fullViewButton,
        &QPushButton::clicked,
        this,
        [this]() {
            if (fullViewHandler)
                fullViewHandler(selected);
            emit openFullViewRequested(selected);
        });

    if (fixedKindValue) {
        // A fixed-kind section is named by its own section header, which also
        // carries the full-view entry and the freshness chip. Repeating them
        // in the body only pushes the real content down.
        title->hide();
        fullViewButton->hide();
        // Two remaining controls do not deserve two rows.
        root->removeWidget(followCheck);
        titleRow->insertWidget(0, followCheck);
        for (const CardWidgets& card : cards) {
            if (card.button && card.button->parentWidget())
                card.button->parentWidget()->hide();
        }
    }

    // Peek may be clamped below a provider's preferred width in a narrow
    // editor. Keep the compact navigation surface flexible so its controls
    // remain reachable instead of forcing the host outside the editor region.
    CompactFlowLayout::replaceRows(root);
    InsightVisualStyle::applyPanel(this);
}

void LiveInsightsContextView::refreshSnapshot(
    LiveInsightKind kind,
    const LiveInsightSnapshot& snapshot)
{
    const int index = indexForKind(kind);
    if (index < 0 || (fixedKindValue && kind != selected))
        return;
    if (!followEditor() && hasRenderedSnapshot.at(index))
        return;
    renderedSnapshots.at(index) = snapshot;
    hasRenderedSnapshot.at(index) = true;
    renderSnapshot(kind, snapshot);
    if (kind == selected)
        renderSurface();
}

void LiveInsightsContextView::renderSnapshot(
    LiveInsightKind kind,
    const LiveInsightSnapshot& snapshot)
{
    CardWidgets& card = cards.at(indexForKind(kind));
    if (!card.status || !card.summary)
        return;
    card.status->setText(phaseText(snapshot));
    card.status->setToolTip(snapshot.errorText);
    card.status->setStyleSheet(
        InsightVisualStyle::statusChipStyleSheet(
            phaseTone(snapshot), card.status->objectName()));
    card.summary->setText(summaryText(snapshot));
    if (fixedKindValue && kind == selected) {
        publishSectionStatus(snapshot);
        publishSectionScope();
    }
}

void LiveInsightsContextView::publishSectionStatus(
    const LiveInsightSnapshot& snapshot)
{
    // The section header renders these; a host that does not read them simply
    // shows no chip.
    QStringList tooltip;
    const QString provenance =
        snapshot.payload.value(QStringLiteral("provenance")).toString().trimmed();
    if (!provenance.isEmpty())
        tooltip.append(provenance);
    const QString summary = summaryText(snapshot).trimmed();
    if (!summary.isEmpty())
        tooltip.append(summary);
    setProperty("contextStatusText", phaseText(snapshot));
    setProperty("contextStatusTooltip", tooltip.join(QStringLiteral("\n")));
}

void LiveInsightsContextView::refreshTheme()
{
    for (int index = 0; index < static_cast<int>(cards.size()); ++index) {
        const LiveInsightKind kind = kindForIndex(index);
        if (hasRenderedSnapshot.at(index)) {
            renderSnapshot(kind, renderedSnapshots.at(index));
            continue;
        }
        LiveInsightSnapshot initial;
        initial.kind = kind;
        if (sessionValue)
            initial = sessionValue->snapshot(kind);
        renderedSnapshots.at(index) = initial;
        hasRenderedSnapshot.at(index) = true;
        renderSnapshot(kind, initial);
    }
    update();
}

void LiveInsightsContextView::refreshAllFromSession()
{
    if (!sessionValue)
        return;
    for (int index = 0; index < static_cast<int>(cards.size()); ++index) {
        const LiveInsightKind kind = kindForIndex(index);
        const LiveInsightSnapshot snapshot = sessionValue->snapshot(kind);
        renderedSnapshots.at(index) = snapshot;
        hasRenderedSnapshot.at(index) = true;
        renderSnapshot(kind, snapshot);
    }
    renderSurface();
}

void LiveInsightsContextView::updateSessionVisibility(
    LiveInsightKind previousKind,
    LiveInsightKind nextKind)
{
    if (!sessionValue || !isVisible() || !followEditor())
        return;
    sessionValue->setConsumerVisible(
        this, previousKind, false);
    sessionValue->setConsumerVisible(
        this, nextKind, true);
}
