#ifndef RELATIONSHIPRESULTPUBLISHER_H
#define RELATIONSHIPRESULTPUBLISHER_H

#include "relationshipanalysisworker.h"

#include <QObject>

class QTimer;
class SymbolRelationshipEngine;

class RelationshipResultPublisher : public QObject
{
    Q_OBJECT

public:
    explicit RelationshipResultPublisher(QObject* parent = nullptr);

    void setRelationshipEngine(SymbolRelationshipEngine* engine);
    bool applySingleFileResult(const SingleFileRelationshipAnalysisResult& result);
    bool applyWorkspaceResult(const WorkspaceRelationshipAnalysisResult& result);
    void clearAllRelationships();
    void invalidateFileRelationships(const QString& fileName);

signals:
    void relationshipDataInvalidated();
    void relationshipDataRefreshRequested();

private:
    SymbolRelationshipEngine* relationshipEngine = nullptr;
    QTimer* relationshipRefreshTimer = nullptr;

    void scheduleRelationshipDataRefresh();
    void stopRelationshipDataRefresh();
};

#endif // RELATIONSHIPRESULTPUBLISHER_H
