#pragma once
#include "zeroslackexport.h"
#include <insightgraph/graph.h>

struct SymbolStableKey;
struct SignalKernelGraphReport;
struct ModuleBlockDiagramReport;
struct SignalUsageHotspotReport;
struct StateTransitionGraphReport;
struct FsmGraphReport;

class ZEROSLACK_API InsightGraphCore : public insightgraph::GraphStore
{
public:
    using insightgraph::GraphStore::stableSymbolId;
    static QString stableSymbolId(const SymbolStableKey& key);
    static InsightGraphDraft fromSignalKernelGraph(const SignalKernelGraphReport& report);
    static InsightGraphDraft fromModuleBlockDiagram(const ModuleBlockDiagramReport& report);
    static InsightGraphDraft fromSignalUsageHotspot(const SignalUsageHotspotReport& report);
    static InsightGraphDraft fromStateTransitionGraph(const StateTransitionGraphReport& report);
    static InsightGraphDraft fromFsmGraph(const FsmGraphReport& report);
};
