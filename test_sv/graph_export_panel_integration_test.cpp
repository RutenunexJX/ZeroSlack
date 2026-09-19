#include "actionregistry.h"
#include "applicationthememanager.h"
#include "graphexportservice.h"
#include "graphexportui.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "signalusagehotspotpanel.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QLineF>
#include <QPushButton>
#include <QTemporaryDir>
#include <QWidget>
#include <QTransform>

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

SignalKernelGraphReport signalKernelThemeFixture()
{
    SignalKernelGraphReport report;
    report.found = true;
    report.kernel.id = 0;
    report.kernel.role = SignalKernelGraphNodeRole::Kernel;
    report.kernel.displayName = QStringLiteral("tracked_signal");
    report.kernel.moduleDisplayName = QStringLiteral("graph_view_module");

    SignalKernelGraphNode input;
    input.id = 1;
    input.role = SignalKernelGraphNodeRole::Input;
    input.inputLane = SignalKernelGraphInputLane::Data;
    input.displayName = QStringLiteral("source_a");
    input.moduleDisplayName = QStringLiteral("source_module");
    report.inputs = {input};

    SignalKernelGraphNode outputA;
    outputA.id = 2;
    outputA.role = SignalKernelGraphNodeRole::Output;
    outputA.displayName = QStringLiteral("sink_a");
    outputA.moduleDisplayName = QStringLiteral("sink_module");
    SignalKernelGraphNode outputB = outputA;
    outputB.id = 3;
    outputB.displayName = QStringLiteral("sink_b");
    report.outputs = {outputA, outputB};
    report.edges = {{1, 0, QStringLiteral("read")},
                    {0, 2, QStringLiteral("write")},
                    {0, 3, QStringLiteral("write")}};

    SignalKernelGraphFanoutGroup group;
    group.id = 7;
    group.role = SignalKernelGraphNodeRole::Output;
    group.groupKey = QStringLiteral("output:sink_module");
    group.displayName = QStringLiteral("sink_module outputs");
    group.moduleName = QStringLiteral("sink_module");
    group.nodeIds = {2, 3};
    group.nodeCount = 2;
    group.totalRoleNodeCount = 2;
    group.highFanout = true;
    report.outputFanoutGroups = {group};
    return report;
}

QColor selectedRectBrush(QGraphicsScene* scene)
{
    if (!scene)
        return {};
    for (QGraphicsItem* item : scene->selectedItems()) {
        if (auto* rect = dynamic_cast<QGraphicsRectItem*>(item))
            return rect->brush().color();
    }
    return {};
}

QColor sceneTextColor(QGraphicsScene* scene,
                      const QString& text)
{
    if (!scene)
        return {};
    for (QGraphicsItem* item : scene->items()) {
        auto* label =
            dynamic_cast<QGraphicsSimpleTextItem*>(item);
        if (label && label->text().contains(text))
            return label->brush().color();
    }
    return {};
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
    host.resize(1100, 760);
    host.show();
    QApplication::processEvents();
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    signalKernel.renderReportForTest(
        signalKernelThemeFixture());
    signalKernel.setGraphSearchTextForTest(
        QStringLiteral("sink_b"));
    QGraphicsView* kernelView = signalKernel.view();
    if (kernelView) {
        kernelView->setTransform(
            QTransform::fromScale(1.29, 1.29));
        kernelView->centerOn(
            kernelView->scene()->sceneRect().center()
                + QPointF(17.0, 9.0));
        for (QGraphicsItem* item : kernelView->scene()->items()) {
            if (item
                && item->flags().testFlag(
                    QGraphicsItem::ItemIsSelectable)) {
                item->setSelected(true);
                break;
            }
        }
    }
    const QTransform kernelLightTransform = kernelView
        ? kernelView->transform() : QTransform();
    const QPointF kernelLightCenter = kernelView
        ? kernelView->mapToScene(
              kernelView->viewport()->rect().center())
        : QPointF();
    const QColor kernelLightBrush = kernelView
        ? selectedRectBrush(kernelView->scene()) : QColor();
    const int kernelCollapsedGroups =
        signalKernel.collapsedFanoutGroupCountForTest();
    const int kernelSearchMatches =
        signalKernel.searchMatchCountForTest();
    const quint64 kernelBuildRequests =
        signalKernel.graphBuildRequestCountForTest();

    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    signalKernel.refreshThemePresentation();
    QApplication::processEvents();
    const QPointF kernelDarkCenter = kernelView
        ? kernelView->mapToScene(
              kernelView->viewport()->rect().center())
        : QPointF();
    const QColor kernelDarkBrush = kernelView
        ? selectedRectBrush(kernelView->scene()) : QColor();
    expect("Signal Kernel theme refresh consumes cached report without service build",
           signalKernel.graphBuildRequestCountForTest()
                   == kernelBuildRequests);
    expect("Signal Kernel theme refresh preserves transform, center, selection, search, and collapse",
           kernelView
               && kernelView->transform()
                      == kernelLightTransform
               && QLineF(kernelLightCenter,
                         kernelDarkCenter).length() < 1.0
               && kernelView->scene()->selectedItems().size() == 1
               && signalKernel.focusSearchText()
                      == QStringLiteral("sink_b")
               && signalKernel.searchMatchCountForTest()
                      == kernelSearchMatches
               && signalKernel.collapsedFanoutGroupCountForTest()
                      == kernelCollapsedGroups);
    expect("Signal Kernel cached graph is recolored in Dark mode",
           kernelLightBrush.isValid()
               && kernelDarkBrush.isValid()
               && kernelLightBrush != kernelDarkBrush);

    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    signalKernel.refreshThemePresentation();
    QApplication::processEvents();
    expect("Signal Kernel Light-Dark-Light restores visual tokens without service build",
           kernelView
               && selectedRectBrush(kernelView->scene())
                      == kernelLightBrush
               && signalKernel.graphBuildRequestCountForTest()
                      == kernelBuildRequests);
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
    hotspot.setFocusSearchText(
        QStringLiteral("tracked_signal"));
    hotspot.selectMatrixCellForTest(
        SignalUsageHotspotRole::Read,
        QStringLiteral("graph_view_module"),
        QStringLiteral("graph_view.sv"));
    // Finish the initial scene/splitter layout before recording the Light
    // viewport baseline. The later event pump must measure theme work only.
    QApplication::processEvents();
    auto* themedTrackView = hotspot.findChild<QGraphicsView*>(
        QStringLiteral("signalUsageHotspotTrackView"));
    auto* themedMatrixView = hotspot.findChild<QGraphicsView*>(
        QStringLiteral("signalUsageHotspotMatrixView"));
    if (themedTrackView) {
        themedTrackView->setTransform(
            QTransform::fromScale(1.18, 1.18));
        themedTrackView->centerOn(
            themedTrackView->scene()->sceneRect().center());
    }
    if (themedMatrixView) {
        themedMatrixView->setTransform(
            QTransform::fromScale(0.91, 0.91));
        themedMatrixView->centerOn(
            themedMatrixView->scene()->sceneRect().center());
    }
    const QTransform hotspotTrackTransform = themedTrackView
        ? themedTrackView->transform() : QTransform();
    const QTransform hotspotMatrixTransform = themedMatrixView
        ? themedMatrixView->transform() : QTransform();
    const QPointF hotspotTrackCenter = themedTrackView
        ? themedTrackView->mapToScene(
              themedTrackView->viewport()->rect().center())
        : QPointF();
    const QPointF hotspotMatrixCenter = themedMatrixView
        ? themedMatrixView->mapToScene(
              themedMatrixView->viewport()->rect().center())
        : QPointF();
    const QSize hotspotTrackViewportSize = themedTrackView
        ? themedTrackView->viewport()->size() : QSize();
    const QSize hotspotMatrixViewportSize = themedMatrixView
        ? themedMatrixView->viewport()->size() : QSize();
    const QColor hotspotLightText = themedTrackView
        ? sceneTextColor(themedTrackView->scene(),
                         QStringLiteral("Signal Usage Map"))
        : QColor();
    const quint64 hotspotBuildRequests =
        hotspot.reportBuildRequestCountForTest();

    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    QApplication::processEvents();
    const QColor hotspotDarkText = themedTrackView
        ? sceneTextColor(themedTrackView->scene(),
                         QStringLiteral("Signal Usage Map"))
        : QColor();
    expect("Usage Hotspot theme refresh consumes cached report without service build",
           hotspot.reportBuildRequestCountForTest()
                   == hotspotBuildRequests);
    expect("Usage Hotspot theme refresh preserves search and selected usage",
           hotspot.focusSearchText()
                   == QStringLiteral("tracked_signal")
               && hotspot.selectedItemIndexForTest() == 0);
    const QTransform hotspotDarkTrackTransform = themedTrackView
        ? themedTrackView->transform() : QTransform();
    const QTransform hotspotDarkMatrixTransform = themedMatrixView
        ? themedMatrixView->transform() : QTransform();
    const QPointF hotspotDarkTrackCenter = themedTrackView
        ? themedTrackView->mapToScene(
              themedTrackView->viewport()->rect().center())
        : QPointF();
    const QPointF hotspotDarkMatrixCenter = themedMatrixView
        ? themedMatrixView->mapToScene(
              themedMatrixView->viewport()->rect().center())
        : QPointF();
    const QSize hotspotDarkTrackViewportSize = themedTrackView
        ? themedTrackView->viewport()->size() : QSize();
    const QSize hotspotDarkMatrixViewportSize = themedMatrixView
        ? themedMatrixView->viewport()->size() : QSize();
    const qreal hotspotTrackCenterDelta =
        QLineF(hotspotTrackCenter,
               hotspotDarkTrackCenter).length();
    const qreal hotspotMatrixCenterDelta =
        QLineF(hotspotMatrixCenter,
               hotspotDarkMatrixCenter).length();
    const bool hotspotPresentationPreserved =
        themedTrackView
        && themedMatrixView
        && hotspotDarkTrackTransform == hotspotTrackTransform
        && hotspotDarkMatrixTransform == hotspotMatrixTransform
        && hotspotTrackCenterDelta < 1.0
        && hotspotMatrixCenterDelta < 1.0;
    if (!hotspotPresentationPreserved) {
        std::fprintf(
            stderr,
            "Hotspot presentation mismatch: "
            "trackScale %.6f->%.6f trackSize %dx%d->%dx%d "
            "trackCenter (%.3f,%.3f)->(%.3f,%.3f) delta=%.3f "
            "matrixScale %.6f->%.6f matrixSize %dx%d->%dx%d "
            "matrixCenter (%.3f,%.3f)->(%.3f,%.3f) delta=%.3f\n",
            hotspotTrackTransform.m11(),
            hotspotDarkTrackTransform.m11(),
            hotspotTrackViewportSize.width(),
            hotspotTrackViewportSize.height(),
            hotspotDarkTrackViewportSize.width(),
            hotspotDarkTrackViewportSize.height(),
            hotspotTrackCenter.x(),
            hotspotTrackCenter.y(),
            hotspotDarkTrackCenter.x(),
            hotspotDarkTrackCenter.y(),
            hotspotTrackCenterDelta,
            hotspotMatrixTransform.m11(),
            hotspotDarkMatrixTransform.m11(),
            hotspotMatrixViewportSize.width(),
            hotspotMatrixViewportSize.height(),
            hotspotDarkMatrixViewportSize.width(),
            hotspotDarkMatrixViewportSize.height(),
            hotspotMatrixCenter.x(),
            hotspotMatrixCenter.y(),
            hotspotDarkMatrixCenter.x(),
            hotspotDarkMatrixCenter.y(),
            hotspotMatrixCenterDelta);
    }
    expect("Usage Hotspot theme refresh preserves both graph transforms and centers",
           hotspotPresentationPreserved);
    expect("Usage Hotspot cached scenes are recolored in Dark mode",
           hotspotLightText.isValid()
               && hotspotDarkText.isValid()
               && hotspotLightText != hotspotDarkText);

    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    QApplication::processEvents();
    expect("Usage Hotspot Light-Dark-Light restores scene tokens without service build",
           themedTrackView
               && sceneTextColor(
                      themedTrackView->scene(),
                      QStringLiteral("Signal Usage Map"))
                      == hotspotLightText
               && hotspot.reportBuildRequestCountForTest()
                      == hotspotBuildRequests);
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

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
