#ifndef INCREMENTALANALYSISPLANSERVICE_H
#define INCREMENTALANALYSISPLANSERVICE_H

#include "semanticanalysisrequest.h"
#include "semanticdependencygraph.h"

class IncrementalAnalysisPlanService
{
public:
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
