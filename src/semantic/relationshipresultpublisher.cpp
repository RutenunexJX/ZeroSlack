#include "relationshipresultpublisher.h"

#include "semanticindex.h"
#include "symbolrelationshipengine.h"
#include "symbolanalyzer.h"

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
    connect(relationshipEngine,
            &SymbolRelationshipEngine::relationshipsReplaced,
            this,
            [this]() {
                emit relationshipDataInvalidated();
                scheduleRelationshipDataRefresh();
            });
}

bool RelationshipResultPublisher::applySingleFileResult(
    const SingleFileRelationshipAnalysisResult& result, SymbolAnalyzer* owner)
{
    const auto current = SemanticIndex::getInstance()->snapshotToken();
    if (!relationshipEngine || !result.semanticSnapshot
        || current.snapshot != result.semanticSnapshot
        || current.snapshot != result.baseSnapshot.snapshot
        || current.revision != result.baseSnapshot.revision)
        return false;
    // The main engine already owns this exact transaction. A compatibility
    // consumer can attach the same immutable query view; its old state is
    // reclaimed by the common analyzer, without a second compilation/publication.
    if (relationshipEngine != SemanticIndex::getInstance()->relationshipEngine()
        && (!owner || !owner->bindPublishedRelationships(relationshipEngine, current)))
        return false;
    scheduleRelationshipDataRefresh();
    return true;
}

bool RelationshipResultPublisher::applyWorkspaceResult(
    const WorkspaceRelationshipAnalysisResult& result, SymbolAnalyzer* owner)
{
    if (result.cancelled)
        return false;
    SingleFileRelationshipAnalysisResult publication;
    publication.baseSnapshot = result.baseSnapshot;
    publication.semanticSnapshot = result.semanticSnapshot;
    return applySingleFileResult(publication, owner);
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
