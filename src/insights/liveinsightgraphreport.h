#pragma once

#include "liveinsightsession.h"
#include "moduleblockdiagramservice.h"
#include "fsmgraphservice.h"
#include "statetransitiongraphservice.h"

#include <variant>

enum class LiveInsightGraphMode { Module, Fsm, StateTransition };

// Captured on the host thread. Workers own this immutable publication and do
// not consult the active editor, global index, or a QWidget.
struct LiveInsightGraphInput {
    LiveInsightGraphMode mode = LiveInsightGraphMode::Module;
    SemanticSnapshotToken semantic;
    QString fileName;
    QString moduleName;
    QString signalName;
    int maxDepth = -1;

    bool operator==(const LiveInsightGraphInput& other) const {
        return mode == other.mode && semantic.snapshot == other.semantic.snapshot
            && semantic.revision == other.semantic.revision
            && fileName == other.fileName && moduleName == other.moduleName
            && signalName == other.signalName && maxDepth == other.maxDepth;
    }
};

struct LiveInsightGraphReport {
    LiveInsightGraphMode mode = LiveInsightGraphMode::Module;
    std::variant<ModuleBlockDiagramReport, FsmGraphReport, StateTransitionGraphReport> value;
    qint64 computationNs = 0;
};
using LiveInsightGraphReportPtr = std::shared_ptr<const LiveInsightGraphReport>;

ZEROSLACK_API LiveInsightGraphReportPtr buildLiveInsightGraphReport(
    const LiveInsightGraphInput& input,
    const LiveInsightCancellationToken& cancellation = {});
ZEROSLACK_API void configureLiveInsightGraphReports(LiveInsightSession& session);
ZEROSLACK_API LiveInsightRequestKey liveInsightGraphRequestKey(
    const LiveInsightGraphInput& input, const QString& workspaceId,
    const QString& documentId, quint64 documentRevision);

Q_DECLARE_METATYPE(LiveInsightGraphInput)
Q_DECLARE_METATYPE(LiveInsightGraphReportPtr)
