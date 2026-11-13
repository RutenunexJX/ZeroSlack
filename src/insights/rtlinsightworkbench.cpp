#include "rtlinsightworkbench.h"
#include "uicontrols.h"
#include "liveinsightgraphreport.h"
#include "compactlayout.h"

#include "graphexportui.h"
#include "insightviewsurface.h"
#include "insightcanvas.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSignalBlocker>
#include "moduleblockdiagramview.h"
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QElapsedTimer>

#include <utility>

RtlInsightWorkbench::RtlInsightWorkbench(QWidget* parent, bool specialized)
    : QWidget(parent)
    , specializedViews(specialized)
{
    setObjectName(QStringLiteral("rtlInsightWorkbench"));
    auto defaultPlugins = createDefaultInsightViewPlugins();
    for (std::unique_ptr<IInsightViewPlugin>& plugin : defaultPlugins) {
        registerPlugin(std::move(plugin));
    }
    buildUi();
    {
        reportSession = new LiveInsightSession(this);
        configureLiveInsightGraphReports(*reportSession);
        connect(reportSession, &LiveInsightSession::snapshotChanged, this,
            [this](LiveInsightKind, const LiveInsightSnapshot& snapshot) { applyGraphSnapshot(snapshot); });
    }
    setViewKind(InsightWorkbenchViewKind::Kernel);
}

RtlInsightWorkbench::~RtlInsightWorkbench()
{
    delete reportSession;
}

bool RtlInsightWorkbench::registerPlugin(
    std::unique_ptr<IInsightViewPlugin> plugin)
{
    if (!plugin)
        return false;
    const int key = static_cast<int>(plugin->kind());
    if (plugin->pluginId().trimmed().isEmpty()
        || plugins.find(key) != plugins.end())
        return false;
    plugins.emplace(key, std::move(plugin));
    return true;
}

QStringList RtlInsightWorkbench::pluginIds() const
{
    QStringList result;
    for (const auto& [key, plugin] : plugins) {
        Q_UNUSED(key)
        if (plugin)
            result.append(plugin->pluginId());
    }
    result.sort();
    return result;
}

int RtlInsightWorkbench::pluginCount() const
{
    return plugins.size();
}

bool RtlInsightWorkbench::hasPlugin(InsightWorkbenchViewKind kind) const
{
    return pluginForKind(kind) != nullptr;
}

bool RtlInsightWorkbench::setViewKind(InsightWorkbenchViewKind kind)
{
    IInsightViewPlugin* plugin = pluginForKind(kind);
    if (!plugin)
        return false;
    if (currentKind != kind && reportSession) {
        reportKey = {};
        reportSession->cancel(LiveInsightKind::Module);
        reportSession->cancel(LiveInsightKind::State);
    }
    if (canvasValue && currentKind != kind)
        saveCurrentViewState();
    const bool moduleSurface = kind == InsightWorkbenchViewKind::Block;
    if (moduleSurface && !blockView) {
        blockView = new ModuleBlockDiagramView(this);
        static_cast<QVBoxLayout*>(layout())->addWidget(blockView, 1);
        blockView->setNavigationHandler([this](const ModuleBlockDiagramNode& node, bool instance) {
            if (moduleNavigationHandler) moduleNavigationHandler(node, instance);
            else if (navigationHandler) {
                const auto& link = instance ? node.instanceCodeLink : node.definitionCodeLink;
                if (!link.fileName.isEmpty()) navigationHandler(link.fileName, link.line, link.column);
            }
        });
    }
    if (blockView) blockView->setVisible(moduleSurface);
    if (!moduleSurface && moduleInstancePicker) moduleInstancePicker->hide();
    const bool nativeSurface=kind!=InsightWorkbenchViewKind::Block && (specializedViews
        || (kind!=InsightWorkbenchViewKind::StateTransition && !buildOverrides.contains(static_cast<int>(kind))));
    if (nativeSurface && (!surface || currentKind != kind)) {
        surface.reset();
        surface = std::make_unique<InsightViewSurface>(kind, this);
        surface->setNavigationHandler(navigationHandler);
        surface->setStatusHandler(statusHandler);
        surface->setDesignHandler(designHandler);
        static_cast<QVBoxLayout*>(layout())->addWidget(surface->widget(), 1);
    }
    if(!nativeSurface)surface.reset();
    if(canvasValue)canvasValue->setVisible(!nativeSurface && !moduleSurface);
    if(statusLabel)statusLabel->setVisible(!nativeSurface);
    for(auto* control:{static_cast<QWidget*>(searchEdit),static_cast<QWidget*>(panButton),static_cast<QWidget*>(minimapButton)})
        if(control)control->setVisible(!nativeSurface && !moduleSurface);
    for(auto* control:findChildren<QPushButton*>())
        if(control->property("genericGraphControl").toBool())control->setVisible(!nativeSurface && !moduleSurface);
    currentKind = kind;
    if (titleLabel) {
        titleLabel->setText(
            QStringLiteral("RTL Insight Workbench / %1")
                .arg(plugin->displayName()));
    }
    if (canvasValue)
        restoreCurrentViewState();
    if (!currentContext.documentId.trimmed().isEmpty()
        || !currentContext.fileName.trimmed().isEmpty()) {
        refresh();
    }
    return true;
}

InsightWorkbenchViewKind RtlInsightWorkbench::viewKind() const
{
    return currentKind;
}

QString RtlInsightWorkbench::currentPluginId() const
{
    IInsightViewPlugin* plugin = pluginForKind(currentKind);
    return plugin ? plugin->pluginId() : QString();
}

void RtlInsightWorkbench::setContext(const InsightViewContext& contextValue)
{
    const bool sameDocument = currentContext.workspaceId == contextValue.workspaceId
        && currentContext.projectId == contextValue.projectId
        && currentContext.documentId == contextValue.documentId
        && currentContext.fileName == contextValue.fileName;
    if (!sameDocument && surface && surface->trace())
        surface->trace()->clearTarget();
    if (specializedViews && sameDocument) {
        if (contextValue.documentRevision < currentContext.documentRevision
            || (contextValue.documentRevision == currentContext.documentRevision
                && contextValue.semanticRevision < currentContext.semanticRevision)) return;
        if (contextValue.documentRevision == currentContext.documentRevision
            && contextValue.semanticRevision == currentContext.semanticRevision
            && contextValue.moduleName == currentContext.moduleName
            && contextValue.instancePath == currentContext.instancePath
            && contextValue.rootIdentity == currentContext.rootIdentity
            && contextValue.configurationIdentity == currentContext.configurationIdentity
            && contextValue.signalName == currentContext.signalName
            && contextValue.signalAccessPath == currentContext.signalAccessPath) return;
    }
    if (!sameDocument || currentContext.moduleName != contextValue.moduleName
        || currentContext.signalName != contextValue.signalName
        || currentContext.signalAccessPath != contextValue.signalAccessPath) surfaceNeedsFit = true;
    currentContext = contextValue;
    refresh();
}

InsightViewContext RtlInsightWorkbench::context() const
{
    return currentContext;
}

bool RtlInsightWorkbench::refresh()
{
    if (surface) {
        surface->setContext(currentContext);
        fitNewSurface();
        return true;
    }
    IInsightViewPlugin* plugin = pluginForKind(currentKind);
    if (!plugin || !canvasValue)
        return false;
    saveCurrentViewState();
    const BuildOverride overrideBuilder =
        buildOverrides.value(static_cast<int>(currentKind));
    if (!overrideBuilder && reportSession
        && (dynamic_cast<ModuleBlockInsightViewPlugin*>(plugin)
            || dynamic_cast<StateTransitionInsightViewPlugin*>(plugin))) {
        LiveInsightGraphInput input;
        input.mode = currentKind == InsightWorkbenchViewKind::Block ? LiveInsightGraphMode::Module
            : currentContext.signalName.isEmpty() ? LiveInsightGraphMode::Fsm : LiveInsightGraphMode::StateTransition;
        input.semantic = currentContext.semantic.isValid() ? currentContext.semantic : SemanticIndex::getInstance()->snapshotToken();
        input.fileName = currentContext.fileName;
        input.moduleName = currentContext.moduleName;
        input.signalName = currentContext.signalName;
        if (input.mode == LiveInsightGraphMode::Module) {
            input.rootIdentity = currentContext.rootIdentity;
            input.instancePath = currentContext.instancePath;
            input.configurationIdentity = currentContext.configurationIdentity;
        }
        const auto key = liveInsightGraphRequestKey(input, currentContext.workspaceId,
            currentContext.documentId, currentContext.documentRevision);
        if (reportKey != key) {
            appliedReportGeneration = 0;
            if (currentKind != InsightWorkbenchViewKind::Block) {
                currentUpdate = core.update(currentPluginId(), {});
                canvasValue->applyUpdate(currentUpdate);
            }
            statusLabel->setText(QStringLiteral("Analyzing module…"));
            statusLabel->show();
        }
        reportKey = key;
        reportSession->setConsumerVisible(this, key.kind, isVisible());
        reportSession->requestUpdate(key, {{QStringLiteral("graphInput"), QVariant::fromValue(input)}});
        applyGraphSnapshot(reportSession->snapshot(key.kind));
        return true;
    }
    const InsightViewBuildResult result = overrideBuilder
        ? overrideBuilder(currentContext)
        : plugin->build(currentContext);
    currentUpdate = core.update(plugin->pluginId(), result.draft);
    canvasValue->applyUpdate(currentUpdate);
    restoreCurrentViewState();
    updatePresentation(result);
    return result.available;
}

void RtlInsightWorkbench::setDesignHandler(std::function<void(const QString&,const QString&)> handler)
{
    designHandler=std::move(handler);
    if(surface)surface->setDesignHandler(designHandler);
}

void RtlInsightWorkbench::setNavigationHandler(
    std::function<bool(const QString&, int, int)> handler)
{
    navigationHandler = std::move(handler);
    if (surface) surface->setNavigationHandler(navigationHandler);
}

InsightGraphCore* RtlInsightWorkbench::graphCore()
{
    return &core;
}

InsightCanvas* RtlInsightWorkbench::canvas() const
{
    return canvasValue;
}

InsightGraphUpdate RtlInsightWorkbench::lastUpdate() const
{
    return currentUpdate;
}

QString RtlInsightWorkbench::statusText() const
{
    return statusLabel ? statusLabel->text() : QString();
}

GraphExportResult RtlInsightWorkbench::exportCurrentGraph(
    const QString& outputPath,
    const GraphExportOptions& options) const
{
    if (currentKind == InsightWorkbenchViewKind::Block && blockView)
        return GraphExportService::exportWidget(blockView->findChild<QWidget*>("moduleBlockCanvas"), outputPath, options);
    if (surface) return surface->exportGraph(outputPath, options);
    return canvasValue
        ? canvasValue->exportGraph(outputPath, options)
        : GraphExportResult{};
}

void RtlInsightWorkbench::setBuildOverrideForTest(
    InsightWorkbenchViewKind kind,
    BuildOverride builder)
{
    const int key = static_cast<int>(kind);
    if (builder)
        buildOverrides.insert(key, std::move(builder));
    else
        buildOverrides.remove(key);
    if(currentKind==kind)setViewKind(kind);
}

QPushButton* RtlInsightWorkbench::detachButtonForTest() const
{
    return detachButton;
}

void RtlInsightWorkbench::setCompactChrome(bool compact)
{
    compactChrome = compact;
    if (compact && layout()) {
        layout()->setContentsMargins(0, 0, 0, 0);
        layout()->setSpacing(0);
    }
    if (surface) {
        if (auto* panel = surface->widget()->widget(); panel && panel->layout() && compact) {
            panel->layout()->setContentsMargins(0, 0, 0, 0);
            panel->layout()->setSpacing(0);
        }
    }
    if (titleLabel)
        titleLabel->setVisible(!compact);
    if (detachButton)
        detachButton->setVisible(!compact);
}

void RtlInsightWorkbench::fitGraph()
{
    if (currentKind == InsightWorkbenchViewKind::Block && blockView) blockView->fit();
    else if (surface) surface->fit();
    else if (canvasValue) canvasValue->fit();
}

IInsightViewPlugin* RtlInsightWorkbench::pluginForKind(
    InsightWorkbenchViewKind kind) const
{
    const auto it = plugins.find(static_cast<int>(kind));
    return it == plugins.end() ? nullptr : it->second.get();
}

void RtlInsightWorkbench::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(4);
    titleLabel = new CompactTitleLabel(this);
    titleLabel->setObjectName(QStringLiteral("rtlInsightWorkbenchTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    toolbar->addWidget(titleLabel);
    toolbar->addStretch(1);

    searchEdit = UiControls::lineEdit(this);
    searchEdit->setObjectName(QStringLiteral("rtlInsightWorkbenchSearch"));
    searchEdit->setPlaceholderText(QStringLiteral("Search graph"));
    searchEdit->setMaximumWidth(220);
    InsightVisualStyle::applySearchField(searchEdit);
    toolbar->addWidget(searchEdit);

    auto* zoomOut = UiControls::pushButton(QStringLiteral("-"), this);
    auto* fitButton = UiControls::pushButton(QStringLiteral("Fit"), this);
    auto* zoomIn = UiControls::pushButton(QStringLiteral("+"), this);
    panButton = UiControls::toolButton(this);
    panButton->setText(QStringLiteral("Pan"));
    panButton->setCheckable(true);
    panButton->setObjectName(QStringLiteral("rtlInsightWorkbenchPan"));
    minimapButton = UiControls::toolButton(this);
    minimapButton->setText(QStringLiteral("Map"));
    minimapButton->setCheckable(true);
    minimapButton->setChecked(true);
    minimapButton->setObjectName(QStringLiteral("rtlInsightWorkbenchMinimap"));
    auto* exportButton = UiControls::pushButton(QStringLiteral("Export"), this);
    for (auto* control : {zoomOut, fitButton, zoomIn, exportButton}) control->setProperty("genericGraphControl", true);
    exportButton->setObjectName(QStringLiteral("rtlInsightWorkbenchExport"));
    detachButton = UiControls::pushButton(QStringLiteral("Detach"), this);
    detachButton->setObjectName(QStringLiteral("rtlInsightWorkbenchDetach"));
    for (QPushButton* button : {zoomOut, fitButton, zoomIn, exportButton, detachButton})
        InsightVisualStyle::applyToolbarButton(button);
    toolbar->addWidget(zoomOut);
    toolbar->addWidget(fitButton);
    toolbar->addWidget(zoomIn);
    toolbar->addWidget(panButton);
    toolbar->addWidget(minimapButton);
    toolbar->addWidget(exportButton);
    toolbar->addWidget(detachButton);
    root->addLayout(toolbar);


    statusLabel = UiControls::label(this);
    statusLabel->setObjectName(QStringLiteral("rtlInsightWorkbenchStatus"));
    statusLabel->setWordWrap(true);
    InsightVisualStyle::applyLabel(statusLabel);
    root->addWidget(statusLabel);

    moduleInstancePicker = UiControls::comboBox(this);
    moduleInstancePicker->setObjectName(QStringLiteral("moduleInstancePicker"));
    moduleInstancePicker->setAccessibleName(tr("Hierarchy instance"));
    moduleInstancePicker->hide(); root->addWidget(moduleInstancePicker);
    connect(moduleInstancePicker, &QComboBox::activated, this, [this](int row) {
        if (row < 1 || !reportSession) return;
        const auto snapshot = reportSession->snapshot(LiveInsightKind::Module);
        if (snapshot.stale || snapshot.phase != LiveInsightPhase::Ready || snapshot.publishedKey != reportKey
            || snapshot.publishedGeneration != appliedReportGeneration) return;
        const auto publication = snapshot.payload.value(QStringLiteral("graphReport")).value<LiveInsightGraphReportPtr>();
        if (!publication || publication->mode != LiveInsightGraphMode::Module) return;
        const auto& candidates = std::get<ModuleBlockDiagramReport>(publication->value).candidates;
        if (row > candidates.size()) return;
        const auto selected = candidates[row-1];
        if (moduleTargetHandler) moduleTargetHandler(selected);
        else {
            auto context = currentContext;
            context.fileName = selected.definitionFile; context.moduleName = selected.moduleType;
            context.rootIdentity = selected.rootId; context.instancePath = selected.instancePath;
            context.configurationIdentity = selected.configurationId; setContext(context);
        }
    });

    canvasValue = new InsightCanvas(this);
    canvasValue->setActivationHandler(
        [this](const InsightGraphNode& node) {
            if (navigationHandler && !node.sourceFile.trimmed().isEmpty())
                navigationHandler(node.sourceFile, qMax(1, node.sourceLine), 1);
        });
    root->addWidget(canvasValue, 1);

    QObject::connect(searchEdit, &QLineEdit::textChanged, this,
                     [this](const QString& text) {
                         if (canvasValue)
                             canvasValue->setSearchText(text);
                         viewStates[static_cast<int>(currentKind)].searchText = text;
                     });
    QObject::connect(zoomOut, &QPushButton::clicked, this,
                     [this]() { canvasValue->zoomOut(); });
    QObject::connect(zoomIn, &QPushButton::clicked, this,
                     [this]() { canvasValue->zoomIn(); });
    QObject::connect(fitButton, &QPushButton::clicked, this,
                     [this]() { canvasValue->fit(); });
    QObject::connect(panButton, &QToolButton::toggled, this,
                     [this](bool enabled) { canvasValue->setPanMode(enabled); });
    QObject::connect(minimapButton, &QToolButton::toggled, this,
                     [this](bool visible) { canvasValue->setMinimapVisible(visible); });
    QObject::connect(exportButton, &QPushButton::clicked, this,
                     [this]() {
                         const QString path = QFileDialog::getSaveFileName(
                             this,
                             QStringLiteral("Export RTL Insight"),
                             {},
                             QStringLiteral("SVG (*.svg);;PNG (*.png);;PDF (*.pdf)"));
                         if (!path.isEmpty())
                             exportCurrentGraph(path);
                     });
    CompactFlowLayout::replaceRows(root);
    InsightVisualStyle::applyPanel(this);
}

void RtlInsightWorkbench::saveCurrentViewState()
{
    if (!canvasValue)
        return;
    ViewState& state = viewStates[static_cast<int>(currentKind)];
    state.searchText = searchEdit ? searchEdit->text() : QString();
    state.selectedNodeIds = canvasValue->selectedNodeIds();
    state.zoom = canvasValue->zoomFactor();
    state.minimapVisible = canvasValue->isMinimapVisible();
    state.panMode = canvasValue->panMode();
}

void RtlInsightWorkbench::restoreCurrentViewState()
{
    if (!canvasValue)
        return;
    const ViewState state = viewStates.value(static_cast<int>(currentKind));
    if (searchEdit) {
        const QSignalBlocker blocker(searchEdit);
        searchEdit->setText(state.searchText);
    }
    canvasValue->setSearchText(state.searchText);
    canvasValue->selectNodeIds(state.selectedNodeIds);
    canvasValue->setMinimapVisible(state.minimapVisible);
    canvasValue->setPanMode(state.panMode);
    if (panButton) {
        const QSignalBlocker blocker(panButton);
        panButton->setChecked(state.panMode);
    }
    if (minimapButton) {
        const QSignalBlocker blocker(minimapButton);
        minimapButton->setChecked(state.minimapVisible);
    }
    if (canvasValue->view()) {
        canvasValue->view()->resetView();
        if (state.zoom > 0.0 && !qFuzzyCompare(state.zoom, 1.0))
            canvasValue->view()->zoomBy(state.zoom);
    }
}

void RtlInsightWorkbench::updatePresentation(
    const InsightViewBuildResult& result)
{
    if (!statusLabel)
        return;
    const QString text = !result.summary.trimmed().isEmpty()
        ? result.summary
        : result.errorText;
    statusLabel->setText(text);
    statusLabel->setToolTip(result.errorText);
    statusLabel->setStyleSheet(
        InsightVisualStyle::statusChipStyleSheet(
            result.available ? InsightStatusTone::Success
                             : InsightStatusTone::Warning,
            statusLabel->objectName()));
}

void RtlInsightWorkbench::setStatusHandler(std::function<void(const QString&, int)> handler)
{ statusHandler = std::move(handler); if (surface) surface->setStatusHandler(statusHandler); }
RtlInsightsPanelCoordinator* RtlInsightWorkbench::rtlSurfaceForTest() const
{ return surface ? surface->rtl() : nullptr; }
SignalTracePanel* RtlInsightWorkbench::traceSurface() const
{ return surface ? surface->trace() : nullptr; }

void RtlInsightWorkbench::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (reportSession && reportKey.isValid()) reportSession->setConsumerVisible(this, reportKey.kind, true);
    fitNewSurface();
}

void RtlInsightWorkbench::hideEvent(QHideEvent* event)
{
    if (reportSession && reportKey.isValid()) reportSession->setConsumerVisible(this, reportKey.kind, false);
    QWidget::hideEvent(event);
}

void RtlInsightWorkbench::applyGraphSnapshot(const LiveInsightSnapshot& snapshot)
{
    if (!reportKey.isValid() || snapshot.requestedKey != reportKey || !canvasValue) return;
    if (currentKind == InsightWorkbenchViewKind::Block && blockView) {
        const bool ready = snapshot.phase == LiveInsightPhase::Ready && !snapshot.stale && snapshot.publishedKey == reportKey;
        blockView->setSourceNavigationEnabled(ready);
        if (!ready) {
            moduleInstancePicker->hide();
            statusLabel->setText(snapshot.phase == LiveInsightPhase::Error ? snapshot.errorText : tr("Updating module…"));
            statusLabel->show();
            return;
        }
        if (snapshot.publishedGeneration == appliedReportGeneration) return;
        const auto publication = snapshot.payload.value(QStringLiteral("graphReport")).value<LiveInsightGraphReportPtr>();
        if (!publication || publication->mode != LiveInsightGraphMode::Module) return;
        const auto& report = std::get<ModuleBlockDiagramReport>(publication->value);
        appliedReportGeneration = snapshot.publishedGeneration;
        moduleInstancePicker->clear();
        if (!report.candidates.isEmpty()) {
            moduleInstancePicker->addItem(tr("Select an instance…"));
            for (const auto& candidate : report.candidates)
                moduleInstancePicker->addItem(candidate.instancePath + QStringLiteral(" · ")
                    + candidate.definitionFile + QStringLiteral(" · ") + candidate.configurationId.left(8));
        }
        moduleInstancePicker->setVisible(!report.candidates.isEmpty());
        if (!report.found) { statusLabel->setText(report.notFoundReasonDisplayName); statusLabel->show(); blockView->setSourceNavigationEnabled(false); return; }
        blockView->setReport(report, currentContext.workspaceId);
        statusLabel->clear(); statusLabel->hide();
        setProperty("liveInsightReportBuildNs", publication->computationNs);
        return;
    }
    canvasValue->setEnabled(snapshot.phase == LiveInsightPhase::Ready && !snapshot.stale);
    if (snapshot.phase == LiveInsightPhase::Error) {
        currentUpdate = core.update(currentPluginId(), {});
        canvasValue->applyUpdate(currentUpdate);
        if (statusLabel) { statusLabel->setText(snapshot.errorText); statusLabel->show(); }
        return;
    }
    if (snapshot.phase != LiveInsightPhase::Ready || snapshot.stale
        || snapshot.publishedKey != reportKey || snapshot.publishedGeneration == appliedReportGeneration) return;
    const auto report = snapshot.payload.value(QStringLiteral("graphReport")).value<LiveInsightGraphReportPtr>();
    if (!report) return;
    const auto result = insightViewFromGraphReport(*report, currentContext, currentPluginId());
    appliedReportGeneration = snapshot.publishedGeneration;
    QElapsedTimer timer;
    timer.start();
    currentUpdate = core.update(currentPluginId(), result.draft);
    canvasValue->applyUpdate(currentUpdate);
    restoreCurrentViewState();
    updatePresentation(result);
    fitNewSurface();
    setProperty("liveInsightReportBuildNs", report->computationNs);
    setProperty("liveInsightApplyNs", timer.nsecsElapsed());
}

void RtlInsightWorkbench::setModuleNavigationHandler(std::function<void(const ModuleBlockDiagramNode&, bool)> handler)
{ moduleNavigationHandler = std::move(handler); }
void RtlInsightWorkbench::setModuleTargetHandler(std::function<void(const DesignHierarchyNode&)> handler)
{ moduleTargetHandler = std::move(handler); }

void RtlInsightWorkbench::fitNewSurface()
{
    // Kernel owns first-visible fitting and cancels it on user interaction.
    // A host timer must not overwrite the panel's restored or newly panned view.
    if (surface && surface->trace()) return;
    if (!surfaceNeedsFit || !isVisible()) return;
    QTimer::singleShot(0, this, [this] {
        if (surface && !surface->trace() && surfaceNeedsFit && isVisible()) {
            surface->fit();
            surfaceNeedsFit = false;
        } else if (!surface && canvasValue && appliedReportGeneration && surfaceNeedsFit && isVisible()) {
            canvasValue->fit();
            surfaceNeedsFit = false;
        }
    });
}

LiveInsightSession* RtlInsightWorkbench::graphSession() const
{
    auto* panel = surface ? surface->rtl() : nullptr;
    return panel ? panel->graphSession() : reportSession;
}
