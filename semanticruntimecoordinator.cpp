#include "semanticruntimecoordinator.h"

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
    if (relationshipEngineInstance) {
        relationshipEngineInstance->clearAllRelationships();
        SemanticIndex* semanticIndex = SemanticIndex::getInstance();
        if (semanticIndex->relationshipEngine()
            == relationshipEngineInstance.get()) {
            semanticIndex->attachRelationshipEngine(nullptr);
        }
    }
}

SymbolRelationshipEngine* SemanticRuntimeCoordinator::relationshipEngine() const
{
    return relationshipEngineInstance.get();
}

SmartRelationshipBuilder* SemanticRuntimeCoordinator::relationshipBuilder() const
{
    return relationshipBuilderInstance.get();
}
