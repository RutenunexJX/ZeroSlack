#include "semanticruntimecoordinator.h"

#include "completionmanager.h"
#include "semanticindex.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "symbolrelationshipengine.h"

SemanticRuntimeCoordinator::SemanticRuntimeCoordinator(QObject* parent)
    : QObject(parent)
{
    relationshipEngineInstance = std::make_unique<SymbolRelationshipEngine>(this);

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->attachRelationshipEngine(relationshipEngineInstance.get());

    slangManagerInstance = std::make_unique<SlangManager>();

    CompletionManager* completionManager = CompletionManager::getInstance();
    completionManager->setRelationshipEngine(relationshipEngineInstance.get());

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
