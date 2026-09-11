#ifndef GRAPHEXPORTUI_H
#define GRAPHEXPORTUI_H

#include "actionregistry.h"
#include "graphexportservice.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QString>
#include <QWidget>

#include <functional>
#include <utility>

namespace GraphExportUi {

inline constexpr const char* kActionIdProperty =
    "zeroSlack.actionRegistryId";
inline constexpr const char* kExecutionRouteProperty =
    "zeroSlack.actionExecutionRoute";

using AvailabilityProvider = std::function<bool()>;
using Exporter = std::function<GraphExportResult(
    const QString&,
    const GraphExportOptions&)>;
using StatusMessageHandler =
    std::function<void(const QString&, int)>;

inline QString chooseOutputPath(QWidget* parent,
                                const QString& title,
                                const QString& suggestedBaseName)
{
    const QString filters =
        QStringLiteral("SVG image (*.svg);;PDF document (*.pdf);;"
                       "High-resolution PNG image (*.png)");
    QString selectedFilter;
    QString outputPath = QFileDialog::getSaveFileName(
        parent,
        title,
        suggestedBaseName,
        filters,
        &selectedFilter);
    if (outputPath.isEmpty() || !QFileInfo(outputPath).suffix().isEmpty())
        return outputPath;

    QString suffix = QStringLiteral(".svg");
    if (selectedFilter.startsWith(QStringLiteral("PDF")))
        suffix = QStringLiteral(".pdf");
    else if (selectedFilter.startsWith(QStringLiteral("High-resolution PNG")))
        suffix = QStringLiteral(".png");
    return outputPath + suffix;
}

inline QString statusMessage(const QString& graphName,
                             const GraphExportResult& result)
{
    if (result.success) {
        return QStringLiteral("%1 completed: %2")
            .arg(graphName, QDir::toNativeSeparators(result.outputPath));
    }
    const QString reason = result.failureReason.trimmed().isEmpty()
        ? GraphExportService::failureName(result.failure)
        : result.failureReason.trimmed();
    return QStringLiteral("%1 failed [%2]: %3")
        .arg(graphName,
             GraphExportService::failureName(result.failure),
             reason);
}

class ExecutionHost final : public ActionExecutionHost
{
public:
    explicit ExecutionHost(Exporter exporter)
        : exporter(std::move(exporter))
    {
    }

    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override
    {
        ActionExecutionResult execution;
        execution.handled = true;
        if (!descriptor.hasSurface(ActionSurface::GraphPanel)
            || !descriptor.executionRoute.startsWith(
                QStringLiteral("graphExport."))) {
            execution.failureReason =
                QStringLiteral("Action is not a graph export route.");
            return execution;
        }
        if (!exporter) {
            execution.failureReason =
                QStringLiteral("Graph export executor is unavailable.");
            return execution;
        }

        const QString outputPath =
            invocation.parameters.value(
                descriptor.parameterModel.name).toString().trimmed();
        if (outputPath.isEmpty()) {
            execution.failureReason =
                QStringLiteral("An output path is required.");
            return execution;
        }

        GraphExportOptions options;
        const int pixelWidth = invocation.parameters.value(
            QStringLiteral("pixelWidth")).toInt();
        const int pixelHeight = invocation.parameters.value(
            QStringLiteral("pixelHeight")).toInt();
        if (pixelWidth > 0 && pixelHeight > 0)
            options.targetPixelSize = QSize(pixelWidth, pixelHeight);
        const qreal dpi = invocation.parameters.value(
            QStringLiteral("dpi")).toDouble();
        if (dpi > 0.0)
            options.dpi = dpi;

        const GraphExportResult exportResult =
            exporter(outputPath, options);
        execution.succeeded = exportResult.success;
        execution.message =
            statusMessage(descriptor.canonicalName, exportResult);
        if (!exportResult.success) {
            execution.failureReason =
                exportResult.failureReason.trimmed().isEmpty()
                ? GraphExportService::failureName(
                      exportResult.failure)
                : exportResult.failureReason.trimmed();
        }
        execution.output.insert(
            QStringLiteral("format"),
            static_cast<int>(exportResult.format));
        execution.output.insert(
            QStringLiteral("failure"),
            static_cast<int>(exportResult.failure));
        execution.output.insert(
            QStringLiteral("failureName"),
            GraphExportService::failureName(exportResult.failure));
        execution.output.insert(
            QStringLiteral("outputPath"),
            exportResult.outputPath);
        execution.output.insert(
            QStringLiteral("outputWidth"),
            exportResult.outputPixelSize.width());
        execution.output.insert(
            QStringLiteral("outputHeight"),
            exportResult.outputPixelSize.height());
        execution.output.insert(
            QStringLiteral("bytesWritten"),
            static_cast<qlonglong>(
                exportResult.bytesWritten));
        return execution;
    }

private:
    Exporter exporter;
};

inline ActionAvailabilityState availabilityFor(
    const ActionDescriptor& descriptor,
    bool graphContentAvailable)
{
    ActionAvailabilityContext context;
    context.graphContentAvailable = graphContentAvailable;
    return evaluateActionAvailability(descriptor, context);
}

inline void updateActionAvailability(
    QAction* action,
    bool graphContentAvailable)
{
    if (!action)
        return;
    const QString actionId =
        action->property(kActionIdProperty).toString();
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor)
        return;
    const ActionAvailabilityState availability =
        availabilityFor(*descriptor, graphContentAvailable);
    action->setEnabled(availability.executable);
    action->setToolTip(
        availability.executable
            ? descriptor->description
            : QStringLiteral("%1\n%2")
                  .arg(descriptor->description,
                       availability.reason));
}

inline ActionExecutionResult executeRegistryAction(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation,
    const Exporter& exporter)
{
    ExecutionHost host(exporter);
    return executeAction(descriptor, host, invocation);
}

inline QAction* bindRegistryAction(
    QWidget* owner,
    const QString& actionId,
    AvailabilityProvider availabilityProvider,
    Exporter exporter,
    StatusMessageHandler statusMessageHandler)
{
    if (!owner)
        return nullptr;
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor
        || !descriptor->hasSurface(ActionSurface::GraphPanel)) {
        return nullptr;
    }
    const ActionAliasDescriptor graphAlias =
        descriptor->aliasForSurface(ActionSurface::GraphPanel);
    const QString label = graphAlias.label.trimmed().isEmpty()
        ? descriptor->canonicalName
        : graphAlias.label.trimmed();
    auto* action = new QAction(label, owner);
    action->setObjectName(graphAlias.adapterKey);
    action->setProperty(kActionIdProperty, descriptor->id);
    action->setProperty(
        kExecutionRouteProperty,
        descriptor->executionRoute);
    action->setToolTip(descriptor->description);
    updateActionAvailability(
        action,
        availabilityProvider && availabilityProvider());

    QObject::connect(
        action,
        &QAction::triggered,
        owner,
        [action,
         descriptor,
         owner,
         availabilityProvider = std::move(availabilityProvider),
         exporter = std::move(exporter),
         statusMessageHandler =
             std::move(statusMessageHandler)]() {
            const bool graphContentAvailable =
                availabilityProvider && availabilityProvider();
            const ActionAvailabilityState availability =
                availabilityFor(
                    *descriptor,
                    graphContentAvailable);
            updateActionAvailability(
                action,
                graphContentAvailable);
            if (!availability.executable) {
                if (statusMessageHandler) {
                    statusMessageHandler(
                        QStringLiteral("%1: %2")
                            .arg(descriptor->canonicalName,
                                 availability.reason),
                        6000);
                }
                return;
            }

            const ActionAliasDescriptor graphAlias =
                descriptor->aliasForSurface(
                    ActionSurface::GraphPanel);
            const QString outputPath = chooseOutputPath(
                owner,
                descriptor->canonicalName,
                graphAlias.defaultValue);
            if (outputPath.isEmpty())
                return;

            ActionInvocation invocation;
            invocation.parameters.insert(
                descriptor->parameterModel.name,
                outputPath);
            const ActionExecutionResult result =
                executeRegistryAction(
                    *descriptor,
                    invocation,
                    exporter);
            if (statusMessageHandler) {
                statusMessageHandler(
                    result.message.trimmed().isEmpty()
                        ? result.failureReason
                        : result.message,
                    result.succeeded ? 3000 : 6000);
            }
        });
    return action;
}

}

#endif // GRAPHEXPORTUI_H
