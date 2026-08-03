#include "actionregistry.h"
#include "graphexportservice.h"
#include "graphexportui.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "signalusagehotspotpanel.h"
#include "wavepreviewpanelcoordinator.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QPushButton>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdio>
#include <functional>

namespace {

int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

void addFixture(QGraphicsScene* scene, const QString& label)
{
    if (!scene)
        return;
    scene->clear();
    scene->addRect(QRectF(12.0, 18.0, 240.0, 110.0));
    QGraphicsSimpleTextItem* text = scene->addSimpleText(label);
    text->setPos(34.0, 54.0);
}

SignalUsageHotspotReport hotspotGraphViewFixture()
{
    SignalUsageHotspotItem item;
    item.role = SignalUsageHotspotRole::Read;
    item.moduleName = QStringLiteral("graph_view_module");
    item.fileName = QStringLiteral("graph_view.sv");
    item.line = 12;
    item.column = 5;
    item.endLine = 12;
    item.endColumn = 16;
    item.snippet = QStringLiteral("sample <= tracked_signal;");
    item.roleReasonDisplayName = QStringLiteral("read");

    SignalUsageHotspotTrackPosition position;
    position.itemIndex = 0;
    position.role = item.role;
    position.roleDisplayName = item.roleReasonDisplayName;
    position.line = item.line;
    position.column = item.column;
    position.endLine = item.endLine;
    position.endColumn = item.endColumn;

    SignalUsageHotspotTrackLane lane;
    lane.moduleName = item.moduleName;
    lane.fileName = item.fileName;
    lane.startLine = 1;
    lane.endLine = 40;
    lane.count = 1;
    lane.positions = {position};

    SignalUsageHotspotReport report;
    report.found = true;
    report.declarationDisplayName =
        QStringLiteral("tracked_signal");
    report.items = {item};
    report.trackLanes = {lane};
    return report;
}

void verifyRegistryBinding(const QString& prefix,
                           QAction* action,
                           const QString& actionId)
{
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    const ActionAliasDescriptor graphAlias =
        descriptor
        ? descriptor->aliasForSurface(ActionSurface::GraphPanel)
        : ActionAliasDescriptor();
    const QByteArray sourceName =
        QStringLiteral("%1 UI action is generated from its registry descriptor")
            .arg(prefix)
            .toUtf8();
    expect(sourceName.constData(),
           descriptor
               && action
               && descriptor->hasSurface(ActionSurface::GraphPanel)
               && action->property(
                      GraphExportUi::kActionIdProperty).toString()
                      == descriptor->id
               && action->property(
                      GraphExportUi::kExecutionRouteProperty).toString()
                      == descriptor->executionRoute
               && action->text() == graphAlias.label
               && action->objectName() == graphAlias.adapterKey
               && action->toolTip().startsWith(
                      descriptor->description));

    ActionAvailabilityContext unavailableContext;
    const ActionAvailabilityState unavailable =
        descriptor
        ? evaluateActionAvailability(
              *descriptor,
              unavailableContext)
        : ActionAvailabilityState();
    ActionAvailabilityContext availableContext;
    availableContext.graphContentAvailable = true;
    const ActionAvailabilityState available =
        descriptor
        ? evaluateActionAvailability(
              *descriptor,
              availableContext)
        : ActionAvailabilityState();
    GraphExportUi::updateActionAvailability(action, true);
    const bool enabledWithContent = action && action->isEnabled();
    GraphExportUi::updateActionAvailability(action, false);
    const bool disabledWithoutContent =
        action && !action->isEnabled();
    const QByteArray availabilityName =
        QStringLiteral("%1 availability and failure reason come from registry")
            .arg(prefix)
            .toUtf8();
    expect(availabilityName.constData(),
           descriptor
               && (descriptor->requirementMask
                   & ActionRequirements::GraphContent)
               && !unavailable.executable
               && !unavailable.reason.isEmpty()
               && available.executable
               && enabledWithContent
               && disabledWithoutContent);
}

void verifyFormats(const QString& prefix,
                   QAction* action,
                   const QString& actionId,
                   const GraphExportUi::Exporter& exporter,
                   const QTemporaryDir& temporary)
{
    verifyRegistryBinding(prefix, action, actionId);
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor)
        return;

    struct FormatCase {
        const char* suffix;
        GraphExportFormat format;
    };
    const FormatCase cases[] = {
        {"svg", GraphExportFormat::Svg},
        {"pdf", GraphExportFormat::Pdf},
        {"png", GraphExportFormat::Png}
    };

    for (const FormatCase& formatCase : cases) {
        const QString path = temporary.filePath(
            QStringLiteral("%1.%2")
                .arg(prefix, QString::fromLatin1(formatCase.suffix)));
        ActionInvocation invocation;
        invocation.parameters.insert(
            descriptor->parameterModel.name,
            path);
        invocation.parameters.insert(
            QStringLiteral("pixelWidth"),
            1400);
        invocation.parameters.insert(
            QStringLiteral("pixelHeight"),
            900);
        invocation.parameters.insert(
            QStringLiteral("dpi"),
            300.0);
        const ActionExecutionResult result =
            GraphExportUi::executeRegistryAction(
                *descriptor,
                invocation,
                exporter);
        const QByteArray checkName =
            QStringLiteral("%1 exports %2 through registry execution")
                .arg(prefix, QString::fromLatin1(formatCase.suffix))
                .toUtf8();
        expect(checkName.constData(),
               result.handled
                   && result.succeeded
                   && result.failureReason.isEmpty()
                   && result.output.value(
                          QStringLiteral("failure")).toInt()
                          == static_cast<int>(
                              GraphExportFailure::None)
                   && result.output.value(
                          QStringLiteral("format")).toInt()
                          == static_cast<int>(formatCase.format)
                   && result.output.value(
                          QStringLiteral("outputWidth")).toInt()
                          == 1400
                   && result.output.value(
                          QStringLiteral("outputHeight")).toInt()
                          == 900
                   && result.output.value(
                          QStringLiteral("bytesWritten")).toLongLong()
                          > 0
                   && QFile::exists(path));
    }

    const QString invalidPath = temporary.filePath(
        QStringLiteral("missing/%1.svg").arg(prefix));
    ActionInvocation invalidInvocation;
    invalidInvocation.parameters.insert(
        descriptor->parameterModel.name,
        invalidPath);
    const ActionExecutionResult failure =
        GraphExportUi::executeRegistryAction(
            *descriptor,
            invalidInvocation,
            exporter);
    const QByteArray checkName =
        QStringLiteral("%1 preserves structured export failures")
            .arg(prefix)
            .toUtf8();
    expect(checkName.constData(),
           failure.handled
               && !failure.succeeded
               && failure.output.value(
                      QStringLiteral("failure")).toInt()
                      == static_cast<int>(
                          GraphExportFailure::OutputOpenFailed)
               && !failure.failureReason.isEmpty()
               && !QFile::exists(invalidPath));
}

}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    expect("temporary export directory is available", temporary.isValid());
    if (!temporary.isValid())
        return 1;

    QWidget host;

    RtlInsightsPanelCoordinator rtlInsights(&host);
    addFixture(rtlInsights.graphView()->scene(),
               QStringLiteral("FSM / module block"));
    verifyFormats(
        QStringLiteral("rtl-graph"),
        rtlInsights.graphExportAction(),
        QString::fromLatin1(
            ActionIds::GraphExportRtlInsights),
        [&rtlInsights](const QString& path,
                       const GraphExportOptions& options) {
            return rtlInsights.exportCurrentGraph(path, options);
        },
        temporary);

    SignalKernelGraphPanelCoordinator signalKernel(&host);
    addFixture(signalKernel.view()->scene(),
               QStringLiteral("Signal kernel"));
    verifyFormats(
        QStringLiteral("signal-kernel"),
        signalKernel.graphExportAction(),
        QString::fromLatin1(
            ActionIds::GraphExportSignalKernel),
        [&signalKernel](const QString& path,
                        const GraphExportOptions& options) {
            return signalKernel.exportGraph(path, options);
        },
        temporary);

    SignalUsageHotspotPanel hotspot(&host);
    hotspot.renderReportForTest(
        hotspotGraphViewFixture());
    hotspot.setCurrentEditorLocation(
        QStringLiteral("graph_view.sv"),
        12);
    const QList<QAction*> graphViewActions =
        hotspot.graphViewActionsForTest();
    bool graphViewMetadataComplete =
        graphViewActions.size() == 5;
    for (QAction* action : graphViewActions) {
        const ActionDescriptor* descriptor =
            action
            ? findActionById(
                  action->property(
                      GraphExportUi::kActionIdProperty)
                      .toString())
            : nullptr;
        graphViewMetadataComplete =
            graphViewMetadataComplete
            && descriptor
            && descriptor->executionRoute.startsWith(
                   QStringLiteral(
                       "insight.graphView."))
            && action->property(
                   GraphExportUi::kExecutionRouteProperty)
                   .toString()
                   == descriptor->executionRoute
            && action->text()
                   == descriptor->aliasForSurface(
                       ActionSurface::GraphPanel).label
            && action->isEnabled();
    }
    expect("Usage Hotspot graph view controls use Registry metadata",
           graphViewMetadataComplete);
    QPushButton* zoomInButton =
        hotspot.findChild<QPushButton*>(
            QStringLiteral(
                "signalUsageHotspotZoomInButton"));
    expect("Usage Hotspot toolbar button shares the Zoom In Action",
           zoomInButton
               && zoomInButton->property(
                      GraphExportUi::kActionIdProperty)
                      .toString()
                      == QString::fromLatin1(
                          ActionIds::GraphViewZoomIn)
               && zoomInButton->text()
                      == QStringLiteral("Zoom In"));
    resetApplicationActionExecutionHistory();
    const ActionExecutionResult resetView =
        hotspot.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewResetLayout));
    hotspot.focusZoomIn();
    const bool zoomedIn =
        hotspot.trackZoomFactorForTest() > 1.0;
    const ActionExecutionResult zoomedOut =
        hotspot.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewZoomOut));
    const ActionExecutionResult centered =
        hotspot.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewCenterCurrent));
    const ActionExecutionResult fitted =
        hotspot.triggerGraphViewActionForTest(
            QString::fromLatin1(
                ActionIds::GraphViewFit));
    expect("Usage Hotspot Reset Layout executes its Registry route",
           resetView.succeeded);
    expect("Usage Hotspot Focus Zoom In executes its Registry route",
           zoomedIn);
    expect("Usage Hotspot Zoom Out executes its Registry route",
           zoomedOut.succeeded);
    expect("Usage Hotspot Center Current executes its Registry route",
           centered.succeeded);
    expect("Usage Hotspot Fit executes its Registry route",
           fitted.succeeded);
    expect("Usage Hotspot graph view Actions preserve repeat history",
           !applicationActionExecutionHistory()
                .hasRepeatableAction());
    auto* trackView = hotspot.findChild<QGraphicsView*>(
        QStringLiteral("signalUsageHotspotTrackView"));
    auto* matrixView = hotspot.findChild<QGraphicsView*>(
        QStringLiteral("signalUsageHotspotMatrixView"));
    addFixture(trackView ? trackView->scene() : nullptr,
               QStringLiteral("Usage track"));
    addFixture(matrixView ? matrixView->scene() : nullptr,
               QStringLiteral("Usage matrix"));
    verifyFormats(
        QStringLiteral("hotspot-track"),
        hotspot.graphExportAction(
            SignalUsageHotspotExportSurface::Track),
        QString::fromLatin1(
            ActionIds::GraphExportUsageHotspotTrack),
        [&hotspot](const QString& path,
                   const GraphExportOptions& options) {
            return hotspot.exportGraph(
                SignalUsageHotspotExportSurface::Track,
                path,
                options);
        },
        temporary);
    verifyFormats(
        QStringLiteral("hotspot-matrix"),
        hotspot.graphExportAction(
            SignalUsageHotspotExportSurface::Matrix),
        QString::fromLatin1(
            ActionIds::GraphExportUsageHotspotMatrix),
        [&hotspot](const QString& path,
                   const GraphExportOptions& options) {
            return hotspot.exportGraph(
                SignalUsageHotspotExportSurface::Matrix,
                path,
                options);
        },
        temporary);

    WavePreviewPanelCoordinator wavePreview(&host);
    wavePreview.canvas()->resize(720, 240);
    verifyFormats(
        QStringLiteral("wave-preview"),
        wavePreview.previewExportAction(),
        QString::fromLatin1(
            ActionIds::GraphExportWavePreview),
        [&wavePreview](const QString& path,
                       const GraphExportOptions& options) {
            return wavePreview.exportPreview(path, options);
        },
        temporary);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
