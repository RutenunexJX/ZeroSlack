#ifndef INCREMENTALANALYSISPLANSERVICE_H
#define INCREMENTALANALYSISPLANSERVICE_H

#include "semanticanalysisrequest.h"
#include "semanticdependencygraph.h"

class IncrementalAnalysisPlanService
{
public:
    IncrementalAnalysisPlan planChanges(
        const SemanticAnalysisRequest& request,
        const QHash<QString, SemanticChangeClassification>& classifications,
        const SemanticDependencyGraph& previousGraph,
        const SemanticDependencyGraph& nextGraph) const;
    IncrementalAnalysisPlan plan(
        const SemanticAnalysisRequest& request,
        const SemanticChangeClassification& classification,
        const SemanticDependencyGraph& graph) const;

    IncrementalAnalysisPlan plan(
        const SemanticAnalysisRequest& request,
        const SemanticChangeClassification& classification,
        const SemanticDependencyGraph& previousGraph,
        const SemanticDependencyGraph& nextGraph) const;
};

#endif // INCREMENTALANALYSISPLANSERVICE_H
