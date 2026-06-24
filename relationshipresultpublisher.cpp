#include "relationshipresultpublisher.h"

#include "semanticindex.h"
#include "symbolrelationshipengine.h"

#include <QTimer>

RelationshipResultPublisher::RelationshipResultPublisher(QObject* parent)
    : QObject(parent)
{
    relationshipRefreshTimer = new QTimer(this);
    relationshipRefreshTimer->setSingleShot(true);
    relationshipRefreshTimer->setInterval(400);
    connect(relationshipRefreshTimer, &QTimer::timeout, this, [this]() {
        emit relationshipDataRefreshRequested();
    });
}

void RelationshipResultPublisher::setRelationshipEngine(
    SymbolRelationshipEngine* engine)
{
    if (relationshipEngine == engine)
        return;
    if (relationshipEngine)
        disconnect(relationshipEngine, nullptr, this, nullptr);

    relationshipEngine = engine;
    if (!relationshipEngine) {
        stopRelationshipDataRefresh();
        return;
    }

    connect(relationshipEngine,
            &SymbolRelationshipEngine::relationshipAdded,
            this,
            [this](int, int, SymbolRelationshipEngine::RelationType) {
                emit relationshipDataInvalidated();
                scheduleRelationshipDataRefresh();
            });
    connect(relationshipEngine,
            &SymbolRelationshipEngine::relationshipsCleared,
            this,
            [this]() {
                stopRelationshipDataRefresh();
                emit relationshipDataInvalidated();
                emit relationshipDataRefreshRequested();
            });
}

bool RelationshipResultPublisher::applySingleFileResult(
    const SingleFileRelationshipAnalysisResult& result)
{
    if (!relationshipEngine)
        return false;

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    if (!semanticIndex->publishSnapshotIfCurrent(result.baseSnapshot,
                                                 result.semanticSnapshot)) {
        return false;
    }

    relationshipEngine->beginUpdate();
    for (const RelationshipToAdd& relationship : result.relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        relationshipEngine->addRelationship(relationship.fromId,
                                            relationship.toId,
                                            relationship.type,
                                            relationship.context,
                                            relationship.confidence,
                                            relationship.evidenceRange);
    }
    relationshipEngine->endUpdate();

    scheduleRelationshipDataRefresh();
    return true;
}

bool RelationshipResultPublisher::applyWorkspaceResult(
    const WorkspaceRelationshipAnalysisResult& result)
{
    if (!relationshipEngine || result.cancelled)
        return false;

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    if (!semanticIndex->publishSnapshotIfCurrent(result.baseSnapshot,
                                                 result.semanticSnapshot)) {
        return false;
    }

    relationshipEngine->beginUpdate();
    for (const auto& pair : result.fileRelationships) {
        for (const RelationshipToAdd& relationship : pair.second) {
            if (relationship.fromId < 0 || relationship.toId < 0)
                continue;
            relationshipEngine->addRelationship(relationship.fromId,
                                                relationship.toId,
                                                relationship.type,
                                                relationship.context,
                                                relationship.confidence,
                                                relationship.evidenceRange);
        }
    }
    relationshipEngine->endUpdate();

    scheduleRelationshipDataRefresh();
    return true;
}

void RelationshipResultPublisher::clearAllRelationships()
{
    if (relationshipEngine) {
        relationshipEngine->clearAllRelationships();
        return;
    }

    stopRelationshipDataRefresh();
    emit relationshipDataInvalidated();
    emit relationshipDataRefreshRequested();
}

void RelationshipResultPublisher::invalidateFileRelationships(
    const QString& fileName)
{
    if (relationshipEngine && !fileName.isEmpty())
        relationshipEngine->invalidateFileRelationships(fileName);
}

void RelationshipResultPublisher::scheduleRelationshipDataRefresh()
{
    if (relationshipRefreshTimer)
        relationshipRefreshTimer->start();
}

void RelationshipResultPublisher::stopRelationshipDataRefresh()
{
    if (relationshipRefreshTimer)
        relationshipRefreshTimer->stop();
}
