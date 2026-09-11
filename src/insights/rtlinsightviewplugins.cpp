#include "rtlinsightviewplugins.h"

#include "fsmgraphservice.h"
#include "moduleblockdiagramservice.h"
#include "signalkernelgraphservice.h"
#include "signalusagehotspotservice.h"
#include "statetransitiongraphservice.h"

#include <utility>

namespace {
void stampDraft(InsightGraphDraft& draft,
                const InsightViewContext& context,
                const QString& pluginId)
{
    draft.workspaceId = context.workspaceId;
    draft.documentId = context.documentId;
    draft.documentRevision = context.documentRevision;
    draft.semanticRevision = context.semanticRevision;
    if (draft.contextKey.trimmed().isEmpty()) {
        draft.contextKey = QStringLiteral("%1|%2|%3")
            .arg(pluginId, context.moduleName, context.signalName);
    }
    draft.metadata.insert(QStringLiteral("pluginId"), pluginId);
    draft.metadata.insert(QStringLiteral("fileName"), context.fileName);
    draft.metadata.insert(QStringLiteral("moduleName"), context.moduleName);
    draft.metadata.insert(QStringLiteral("signalName"), context.signalName);
}

InsightViewBuildResult unavailable(const InsightViewContext& context,
                                   const QString& pluginId,
                                   const QString& layoutHint,
                                   const QString& reason)
{
    InsightViewBuildResult result;
    result.draft.layoutHint = layoutHint;
    result.errorText = reason;
    result.summary = reason;
    result.draft.metadata.insert(QStringLiteral("found"), false);
    result.draft.metadata.insert(QStringLiteral("notFoundReason"), reason);
    stampDraft(result.draft, context, pluginId);
    return result;
}
}

QString insightWorkbenchViewKindId(InsightWorkbenchViewKind kind)
{
    switch (kind) {
    case InsightWorkbenchViewKind::Kernel:
        return QStringLiteral("kernel");
    case InsightWorkbenchViewKind::Block:
        return QStringLiteral("block");
    case InsightWorkbenchViewKind::Hotspot:
        return QStringLiteral("hotspot");
    case InsightWorkbenchViewKind::StateTransition:
        return QStringLiteral("state");
    }
    return {};
}

QString insightWorkbenchViewDisplayName(InsightWorkbenchViewKind kind)
{
    switch (kind) {
    case InsightWorkbenchViewKind::Kernel:
        return QStringLiteral("Signal Kernel Graph");
    case InsightWorkbenchViewKind::Block:
        return QStringLiteral("Module Block Diagram");
    case InsightWorkbenchViewKind::Hotspot:
        return QStringLiteral("Signal Hotspot");
    case InsightWorkbenchViewKind::StateTransition:
        return QStringLiteral("State Transition Graph");
    }
    return {};
}

InsightWorkbenchViewKind SignalKernelInsightViewPlugin::kind() const
{
    return InsightWorkbenchViewKind::Kernel;
}

QString SignalKernelInsightViewPlugin::pluginId() const
{
    return insightWorkbenchViewKindId(kind());
}

QString SignalKernelInsightViewPlugin::displayName() const
{
    return insightWorkbenchViewDisplayName(kind());
}

QString SignalKernelInsightViewPlugin::iconKey() const
{
    return QStringLiteral("rtl-insight-kernel");
}

InsightViewBuildResult SignalKernelInsightViewPlugin::build(
    const InsightViewContext& context) const
{
    if (context.signalName.trimmed().isEmpty()) {
        return unavailable(
            context,
            pluginId(),
            QStringLiteral("kernel"),
            QStringLiteral("Select a signal to build its kernel graph."));
    }
    SignalKernelGraphQuery query;
    query.signalName = context.signalName;
    query.signalAccessPath = context.signalAccessPath;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    const SignalKernelGraphReport report =
        SignalKernelGraphService::getInstance()->buildSignalKernelGraph(query);
    InsightViewBuildResult result;
    result.draft = InsightGraphCore::fromSignalKernelGraph(report);
    stampDraft(result.draft, context, pluginId());
    result.available = report.found;
    result.errorText = report.found ? QString() : report.notFoundReasonDisplayName;
    result.summary = report.found
        ? QStringLiteral("%1 inputs, %2 outputs")
              .arg(report.inputs.size())
              .arg(report.outputs.size())
        : result.errorText;
    return result;
}

InsightWorkbenchViewKind ModuleBlockInsightViewPlugin::kind() const
{
    return InsightWorkbenchViewKind::Block;
}

QString ModuleBlockInsightViewPlugin::pluginId() const
{
    return insightWorkbenchViewKindId(kind());
}

QString ModuleBlockInsightViewPlugin::displayName() const
{
    return insightWorkbenchViewDisplayName(kind());
}

QString ModuleBlockInsightViewPlugin::iconKey() const
{
    return QStringLiteral("rtl-insight-block");
}

InsightViewBuildResult ModuleBlockInsightViewPlugin::build(
    const InsightViewContext& context) const
{
    if (context.moduleName.trimmed().isEmpty()) {
        return unavailable(
            context,
            pluginId(),
            QStringLiteral("block"),
            QStringLiteral("Select a module to build its block diagram."));
    }
    ModuleBlockDiagramQuery query;
    query.moduleName = context.moduleName;
    query.fileName = context.fileName;
    query.maxDepth = 2;
    const ModuleBlockDiagramReport report =
        ModuleBlockDiagramService::getInstance()->buildModuleBlockDiagram(query);
    InsightViewBuildResult result;
    result.draft = InsightGraphCore::fromModuleBlockDiagram(report);
    stampDraft(result.draft, context, pluginId());
    result.available = report.found;
    result.errorText = report.found ? QString() : report.notFoundReasonDisplayName;
    result.summary = report.found
        ? QStringLiteral("%1 modules, %2 connections")
              .arg(report.moduleCount)
              .arg(report.edgeCount)
        : result.errorText;
    return result;
}

InsightWorkbenchViewKind SignalHotspotInsightViewPlugin::kind() const
{
    return InsightWorkbenchViewKind::Hotspot;
}

QString SignalHotspotInsightViewPlugin::pluginId() const
{
    return insightWorkbenchViewKindId(kind());
}

QString SignalHotspotInsightViewPlugin::displayName() const
{
    return insightWorkbenchViewDisplayName(kind());
}

QString SignalHotspotInsightViewPlugin::iconKey() const
{
    return QStringLiteral("rtl-insight-hotspot");
}

InsightViewBuildResult SignalHotspotInsightViewPlugin::build(
    const InsightViewContext& context) const
{
    if (context.signalName.trimmed().isEmpty()) {
        return unavailable(
            context,
            pluginId(),
            QStringLiteral("hotspot"),
            QStringLiteral("Select a signal to calculate hotspot evidence."));
    }
    SignalUsageHotspotQuery query;
    query.signalName = context.signalName;
    query.signalAccessPath = context.signalAccessPath;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    const SignalUsageHotspotReport report =
        SignalUsageHotspotService::getInstance()->buildSignalUsageHotspot(query);
    InsightViewBuildResult result;
    result.draft = InsightGraphCore::fromSignalUsageHotspot(report);
    stampDraft(result.draft, context, pluginId());
    result.available = report.found;
    result.errorText = report.found ? QString() : report.notFoundReasonDisplayName;
    result.summary = report.found
        ? QStringLiteral("%1 semantic uses across %2 tracks")
              .arg(report.items.size())
              .arg(report.trackLanes.size())
        : result.errorText;
    return result;
}

InsightWorkbenchViewKind StateTransitionInsightViewPlugin::kind() const
{
    return InsightWorkbenchViewKind::StateTransition;
}

QString StateTransitionInsightViewPlugin::pluginId() const
{
    return insightWorkbenchViewKindId(kind());
}

QString StateTransitionInsightViewPlugin::displayName() const
{
    return insightWorkbenchViewDisplayName(kind());
}

QString StateTransitionInsightViewPlugin::iconKey() const
{
    return QStringLiteral("rtl-insight-state");
}

InsightViewBuildResult StateTransitionInsightViewPlugin::build(
    const InsightViewContext& context) const
{
    if (context.moduleName.trimmed().isEmpty()) {
        return unavailable(
            context,
            pluginId(),
            QStringLiteral("state"),
            QStringLiteral("Select an RTL module to discover its FSM."));
    }
    InsightViewBuildResult result;
    if (!context.signalName.trimmed().isEmpty()) {
        StateTransitionGraphQuery query;
        query.symbolName = context.signalName;
        query.fileName = context.fileName;
        query.moduleName = context.moduleName;
        const StateTransitionGraphReport report =
            StateTransitionGraphService::getInstance()
                ->buildStateTransitionGraph(query);
        result.draft = InsightGraphCore::fromStateTransitionGraph(report);
        result.available = report.found;
        result.errorText = report.found ? QString() : report.notFoundReasonDisplayName;
        result.summary = report.found
            ? QStringLiteral("%1 states, %2 transitions")
                  .arg(report.stateCount)
                  .arg(report.transitionCount)
            : result.errorText;
    } else {
        FsmGraphQuery query;
        query.fileName = context.fileName;
        query.moduleName = context.moduleName;
        const FsmGraphReport report =
            FsmGraphService::getInstance()->buildFsmGraph(query);
        result.draft = InsightGraphCore::fromFsmGraph(report);
        result.available = report.found;
        result.errorText = report.found ? QString() : report.notFoundReasonDisplayName;
        int stateCount = 0;
        int transitionCount = 0;
        for (const FsmGraph& graph : report.graphs) {
            stateCount += graph.stateCount;
            transitionCount += graph.transitionCount;
        }
        result.summary = report.found
            ? QStringLiteral("%1 states, %2 transitions")
                  .arg(stateCount)
                  .arg(transitionCount)
            : result.errorText;
    }
    stampDraft(result.draft, context, pluginId());
    return result;
}

std::vector<std::unique_ptr<IInsightViewPlugin>>
createDefaultInsightViewPlugins()
{
    std::vector<std::unique_ptr<IInsightViewPlugin>> result;
    result.emplace_back(std::make_unique<SignalKernelInsightViewPlugin>());
    result.emplace_back(std::make_unique<ModuleBlockInsightViewPlugin>());
    result.emplace_back(std::make_unique<SignalHotspotInsightViewPlugin>());
    result.emplace_back(std::make_unique<StateTransitionInsightViewPlugin>());
    return result;
}
