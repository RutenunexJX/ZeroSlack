#include "semanticruntimecoordinator.h"

#include "analysisscheduler.h"
#include "clockresetdomainservice.h"
#include "completionservice.h"
#include "definitionnavigationservice.h"
#include "definitionservice.h"
#include "diagnosticservice.h"
#include "fsmgraphservice.h"
#include "hierarchyservice.h"
#include "modulebriefservice.h"
#include "navigationservice.h"
#include "referenceservice.h"
#include "relationshipservice.h"
#include "searchservice.h"
#include "scopebandservice.h"
#include "semanticdiffservice.h"
#include "semanticindex.h"
#include "signaljourneyservice.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"
#include "symbolrelationshipengine.h"

void SemanticRuntimeCoordinator::configureScheduler(AnalysisScheduler* scheduler) const
{
    if (!scheduler)
        return;

    scheduler->setSymbolAnalyzer(symbolAnalyzerInstance.get());
    scheduler->setRelationshipEngine(relationshipEngineInstance.get());
    scheduler->setRelationshipBuilder(relationshipBuilderInstance.get());
}

void SemanticRuntimeCoordinator::configureQueryServices(
    SemanticIndex* semanticIndex) const
{
    CompletionService::getInstance()->setSemanticIndex(semanticIndex);
    DefinitionService::getInstance()->setSemanticIndex(semanticIndex);
    DefinitionNavigationService::getInstance()->setSemanticIndex(semanticIndex);
    DiagnosticService::getInstance()->setSemanticIndex(semanticIndex);
    RelationshipService::getInstance()->setSemanticIndex(semanticIndex);
    ReferenceService::getInstance()->setSemanticIndex(semanticIndex);
    HierarchyService::getInstance()->setSemanticIndex(semanticIndex);
    SearchService::getInstance()->setSemanticIndex(semanticIndex);
    ScopeBandService::getInstance()->setSemanticIndex(semanticIndex);
    NavigationService::getInstance()->setSemanticIndex(semanticIndex);
    ModuleBriefService::getInstance()->setSemanticIndex(semanticIndex);
    SignalJourneyService::getInstance()->setSemanticIndex(semanticIndex);
    ClockResetDomainService::getInstance()->setSemanticIndex(semanticIndex);
    FsmGraphService::getInstance()->setSemanticIndex(semanticIndex);
    SemanticDiffService::getInstance()->setSemanticIndex(semanticIndex);
}
