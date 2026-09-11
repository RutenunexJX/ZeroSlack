#include "rtlinsightworkbench.h"

#include "graphexportui.h"
#include "insightviewsurface.h"
#include "insightcanvas.h"
#include "insightgraphview.h"
#include "insightvisualstyle.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QShowEvent>

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
    setViewKind(InsightWorkbenchViewKind::Kernel);
}

RtlInsightWorkbench::~RtlInsightWorkbench() = default;

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
    if (canvasValue && currentKind != kind)
        saveCurrentViewState();
    if (specializedViews && (!surface || currentKind != kind)) {
        surface.reset();
        surface = std::make_unique<InsightViewSurface>(kind, this);
        surface->setNavigationHandler(navigationHandler);
        surface->setStatusHandler(statusHandler);
        static_cast<QVBoxLayout*>(layout())->addWidget(surface->widget(), 1);
    }
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
        && currentContext.documentId == contextValue.documentId
        && currentContext.fileName == contextValue.fileName;
    if (specializedViews && sameDocument) {
        if (contextValue.documentRevision < currentContext.documentRevision
            || (contextValue.documentRevision == currentContext.documentRevision
                && contextValue.semanticRevision < currentContext.semanticRevision)) return;
        if (contextValue.documentRevision == currentContext.documentRevision
            && contextValue.semanticRevision == currentContext.semanticRevision
            && contextValue.moduleName == currentContext.moduleName
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
    const InsightViewBuildResult result = overrideBuilder
        ? overrideBuilder(currentContext)
        : plugin->build(currentContext);
    currentUpdate = core.update(plugin->pluginId(), result.draft);
    canvasValue->applyUpdate(currentUpdate);
    restoreCurrentViewState();
    updatePresentation(result);
    return result.available;
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
}

QPushButton* RtlInsightWorkbench::detachButtonForTest() const
{
    return detachButton;
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
    titleLabel = new QLabel(this);
    titleLabel->setObjectName(QStringLiteral("rtlInsightWorkbenchTitle"));
    InsightVisualStyle::applyTitleLabel(titleLabel);
    toolbar->addWidget(titleLabel);
    toolbar->addStretch(1);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("rtlInsightWorkbenchSearch"));
    searchEdit->setPlaceholderText(QStringLiteral("Search graph"));
    searchEdit->setMaximumWidth(220);
    InsightVisualStyle::applySearchField(searchEdit);
    toolbar->addWidget(searchEdit);

    auto* zoomOut = new QPushButton(QStringLiteral("-"), this);
    auto* fitButton = new QPushButton(QStringLiteral("Fit"), this);
    auto* zoomIn = new QPushButton(QStringLiteral("+"), this);
    panButton = new QToolButton(this);
    panButton->setText(QStringLiteral("Pan"));
    panButton->setCheckable(true);
    panButton->setObjectName(QStringLiteral("rtlInsightWorkbenchPan"));
    minimapButton = new QToolButton(this);
    minimapButton->setText(QStringLiteral("Map"));
    minimapButton->setCheckable(true);
    minimapButton->setChecked(true);
    minimapButton->setObjectName(QStringLiteral("rtlInsightWorkbenchMinimap"));
    auto* exportButton = new QPushButton(QStringLiteral("Export"), this);
    exportButton->setObjectName(QStringLiteral("rtlInsightWorkbenchExport"));
    detachButton = new QPushButton(QStringLiteral("Detach"), this);
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
    if (specializedViews) {
        for (int i = 0; i < toolbar->count(); ++i) {
            QWidget* control = toolbar->itemAt(i)->widget();
            if (control && control != titleLabel && control != detachButton) control->hide();
        }
        return;
    }


    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("rtlInsightWorkbenchStatus"));
    statusLabel->setWordWrap(true);
    InsightVisualStyle::applyLabel(statusLabel);
    root->addWidget(statusLabel);

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
SignalKernelGraphPanelCoordinator* RtlInsightWorkbench::kernelSurfaceForTest() const
{ return surface ? surface->kernel() : nullptr; }

void RtlInsightWorkbench::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    fitNewSurface();
}

void RtlInsightWorkbench::fitNewSurface()
{
    if (!surface || !surfaceNeedsFit || !isVisible()) return;
    QTimer::singleShot(0, this, [this] {
        if (surface && surfaceNeedsFit && isVisible()) {
            surface->fit();
            surfaceNeedsFit = false;
        }
    });
}
