#include "semanticruntimecoordinator.h"

#include "analysisscheduler.h"
#include "completionservice.h"
#include "definitionnavigationservice.h"
#include "definitionservice.h"
#include "diagnosticservice.h"
#include "hierarchyservice.h"
#include "navigationservice.h"
#include "referenceservice.h"
#include "relationshipservice.h"
#include "searchservice.h"
#include "semanticindex.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"
#include "symbolrelationshipengine.h"

SemanticRuntimeCoordinator::SemanticRuntimeCoordinator(QObject* parent)
    : QObject(parent)
{
    relationshipEngineInstance = std::make_unique<SymbolRelationshipEngine>(this);
    symbolAnalyzerInstance = std::make_unique<SymbolAnalyzer>(this);

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    configureQueryServices(semanticIndex);
    semanticIndex->attachRelationshipEngine(relationshipEngineInstance.get());

    slangManagerInstance = std::make_unique<SlangManager>();

    relationshipBuilderInstance = semanticIndex->createRelationshipBuilder(
        relationshipEngineInstance.get(), slangManagerInstance.get(), this);
}

SemanticRuntimeCoordinator::~SemanticRuntimeCoordinator()
{
    if (relationshipEngineInstance)
        relationshipEngineInstance->clearAllRelationships();
}

SymbolRelationshipEngine* SemanticRuntimeCoordinator::relationshipEngine() const
{
    return relationshipEngineInstance.get();
}

SmartRelationshipBuilder* SemanticRuntimeCoordinator::relationshipBuilder() const
{
    return relationshipBuilderInstance.get();
}

SlangManager* SemanticRuntimeCoordinator::slangManager() const
{
    return slangManagerInstance.get();
}

void SemanticRuntimeCoordinator::configureScheduler(AnalysisScheduler* scheduler) const
{
    if (!scheduler)
        return;

    scheduler->setSymbolAnalyzer(symbolAnalyzerInstance.get());
    scheduler->setRelationshipEngine(relationshipEngineInstance.get());
    scheduler->setRelationshipBuilder(relationshipBuilderInstance.get());
}

void SemanticRuntimeCoordinator::configureQueryServices(SemanticIndex* semanticIndex) const
{
    CompletionService::getInstance()->setSemanticIndex(semanticIndex);
    DefinitionService::getInstance()->setSemanticIndex(semanticIndex);
    DefinitionNavigationService::getInstance()->setSemanticIndex(semanticIndex);
    DiagnosticService::getInstance()->setSemanticIndex(semanticIndex);
    RelationshipService::getInstance()->setSemanticIndex(semanticIndex);
    ReferenceService::getInstance()->setSemanticIndex(semanticIndex);
    HierarchyService::getInstance()->setSemanticIndex(semanticIndex);
    SearchService::getInstance()->setSemanticIndex(semanticIndex);
    NavigationService::getInstance()->setSemanticIndex(semanticIndex);
}
