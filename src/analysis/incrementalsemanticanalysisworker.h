#ifndef INCREMENTALSEMANTICANALYSISWORKER_H
#define INCREMENTALSEMANTICANALYSISWORKER_H

#include "symbolanalyzer.h"

#include <functional>
#include <memory>

class SemanticIndexSnapshot;

class IncrementalSemanticAnalysisWorker
{
public:
    static WorkspaceAnalysisResult analyze(
        const SemanticAnalysisRequest& request,
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
        const SemanticDependencyGraph& dependencyGraph,
        const std::function<bool()>& isCancelled,
        std::shared_ptr<const SemanticAnalysisInput> baseInput = {},
        std::shared_ptr<const PublishedWorkspaceSemanticState> retainedState = {});
};

#endif // INCREMENTALSEMANTICANALYSISWORKER_H
