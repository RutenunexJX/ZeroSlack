#pragma once
#include "fsmgraphservice.h"
#include "moduleblockdiagramservice.h"
#include <zeroslack/semantic/signalfeedbackservice.h>
#include <insightgraph/projection.h>
#include <insightgraph/heatmap.h>
#include <insightgraph/trace.h>

namespace InsightGraphAdapter {
insightgraph::FsmGraphData fsm(const FsmGraph& graph);
insightgraph::HierarchyData hierarchy(const ModuleBlockDiagramReport& report);
QList<insightgraph::HeatEvidence> heat(const QList<SignalTraceEvidence>& evidence);
insightgraph::TraceStatus status(SignalTraceStatus value);
insightgraph::TraceProjection tracePage(const SignalTracePage& page, const QString& parentPath);
QList<insightgraph::FeedbackSummary> feedback(const SignalFeedbackReport& report);
}
