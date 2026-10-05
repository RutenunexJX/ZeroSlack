#include "liveinsightgraphreport.h"
#include "semanticindexsnapshot.h"

#include <QElapsedTimer>
#include <stdexcept>

LiveInsightGraphReportPtr buildLiveInsightGraphReport(
    const LiveInsightGraphInput& input, const LiveInsightCancellationToken& cancellation)
{
    if (!input.semantic.isValid())
        throw std::runtime_error("Semantic analysis is not ready.");
    const auto cancelled = [cancellation] { return cancellation.isCancellationRequested(); };
    auto report = std::make_shared<LiveInsightGraphReport>();
    report->mode = input.mode;
    QElapsedTimer timer;
    timer.start();
    switch (input.mode) {
    case LiveInsightGraphMode::Module: {
        ModuleBlockDiagramQuery query;
        query.fileName = input.fileName;
        query.moduleName = input.moduleName;
        query.maxDepth = input.maxDepth;
        query.isCancelled = cancelled;
        report->value = ModuleBlockDiagramService(input.semantic).buildModuleBlockDiagram(query);
        break;
    }
    case LiveInsightGraphMode::Fsm: {
        FsmGraphQuery query;
        query.fileName = input.fileName;
        query.moduleName = input.moduleName;
        query.isCancelled = cancelled;
        report->value = FsmGraphService(input.semantic).buildFsmGraph(query);
        break;
    }
    case LiveInsightGraphMode::StateTransition: {
        StateTransitionGraphQuery query;
        query.fileName = input.fileName;
        query.moduleName = input.moduleName;
        query.symbolName = input.signalName;
        query.isCancelled = cancelled;
        report->value = StateTransitionGraphService(input.semantic).buildStateTransitionGraph(query);
        break;
    }
    }
    report->computationNs = timer.nsecsElapsed();
    return report;
}

namespace {
LiveInsightBuildResult buildReport(const LiveInsightBuildRequest& request,
                                  const LiveInsightCancellationToken& cancellation)
{
    if (cancellation.isCancellationRequested())
        return LiveInsightBuildResult::cancellation(request);
    const auto input = request.input.value(QStringLiteral("graphInput")).value<LiveInsightGraphInput>();
    const auto report = buildLiveInsightGraphReport(input, cancellation);
    if (cancellation.isCancellationRequested())
        return LiveInsightBuildResult::cancellation(request);
    return LiveInsightBuildResult::success(request, {
        {QStringLiteral("graphReport"), QVariant::fromValue(report)},
        {QStringLiteral("computationNs"), report->computationNs}});
}
}

void configureLiveInsightGraphReports(LiveInsightSession& session)
{
    session.setBuilder(LiveInsightKind::Module, buildReport);
    session.setBuilder(LiveInsightKind::State, buildReport);
}

LiveInsightRequestKey liveInsightGraphRequestKey(
    const LiveInsightGraphInput& input, const QString& workspaceId,
    const QString& documentId, quint64 documentRevision)
{
    LiveInsightRequestKey key;
    key.kind = input.mode == LiveInsightGraphMode::Module ? LiveInsightKind::Module : LiveInsightKind::State;
    key.workspaceId = workspaceId.isEmpty() ? QStringLiteral("standalone") : workspaceId;
    key.documentId = documentId.isEmpty() ? input.fileName : documentId;
    key.documentRevision = documentRevision;
    key.semanticRevision = input.semantic.revision;
    key.contextKey = QStringLiteral("%1|%2|%3|%4|%5").arg(static_cast<int>(input.mode))
        .arg(input.fileName, input.moduleName, input.signalName).arg(input.maxDepth);
    return key;
}
