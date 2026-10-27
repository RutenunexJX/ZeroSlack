#include "actionregistry.h"
#include "testuistyle.h"
#include "applicationthememanager.h"
#include "graphexportservice.h"
#include "graphexportui.h"
#include "rtlinsightspanelcoordinator.h"
#include "signal_trace_test_fixture.h"
#include <QTreeWidget>


#include <QAction>
#include <QApplication>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QLineF>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QTemporaryDir>
#include <QSettings>
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
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    if (!initializeUiStyleForTest()) return 3;
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


    SignalTraceTestFixture fixture;
    expect("production trace fixture analyzed",fixture.build("module top(input wire s,output wire a,b);assign a=s;assign b=s;endmodule\n"));
    host.resize(1120,760);host.show();SignalTracePanel trace(&host);
    expect("native signal trace ready",fixture.show(trace,"s"));
    trace.setFocusSearchText("assign");const auto* root=trace.traceTree(SignalTraceDirection::Upstream)->topLevelItem(0);
    for(auto mode:{ThemeMode::Dark,ThemeMode::Light}){
        ApplicationThemeManager::instance().setMode(mode);trace.refreshThemePresentation();QApplication::processEvents();
        expect("theme preserves native model and target without rebuilding",!trace.busy() && trace.traceTree(SignalTraceDirection::Upstream)->topLevelItem(0)==root && trace.focusSearchText()=="assign");
    }
    for(bool heat:{false,true}){
        trace.setHeatmapMode(heat);
        for(const auto& suffix:{"svg","pdf","png"}){
            GraphExportOptions options;options.targetPixelSize={1400,900};options.dpi=300;
            const auto path=temporary.filePath(QString("trace-%1.%2").arg(heat?"heatmap":"tree",suffix));
            const auto exported=trace.exportGraph(path,options);
            expect("native view exports SVG/PDF/PNG with requested dimensions",exported.success && exported.outputPixelSize==QSize(1400,900) && exported.bytesWritten>0 && QFile::exists(path));
        }
    }
    const auto csvPath=temporary.filePath("heatmap.csv");QString error;
    expect("heatmap exports its filtered data model",trace.exportHeatmap(csvPath,&error));QFile csv(csvPath);
    expect("CSV equals current model",csv.open(QIODevice::ReadOnly) && csv.readAll()==trace.heatModel()->csv());
    expect("native export failure remains structured",trace.exportGraph(temporary.filePath("missing/trace.svg")).failure==GraphExportFailure::OutputOpenFailed);
    for(const auto* id:{ActionIds::GraphExportSignalKernel,ActionIds::GraphExportUsageHotspotTrack,ActionIds::GraphExportUsageHotspotMatrix}){
        const auto* descriptor=findActionById(id);expect("retired export remains discoverable without a dead panel button",descriptor && descriptor->aliases.isEmpty());
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
