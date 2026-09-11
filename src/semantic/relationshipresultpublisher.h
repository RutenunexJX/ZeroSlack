#ifndef RELATIONSHIPRESULTPUBLISHER_H
#define RELATIONSHIPRESULTPUBLISHER_H

#include "zeroslackexport.h"

#include "relationshipanalysisworker.h"

#include <QObject>
#include <QPointer>

class QTimer;
class SymbolRelationshipEngine;

class ZEROSLACK_API RelationshipResultPublisher : public QObject
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
    QPointer<SymbolRelationshipEngine> relationshipEngine;
    QTimer* relationshipRefreshTimer = nullptr;

    void scheduleRelationshipDataRefresh();
    void stopRelationshipDataRefresh();
};

#endif // RELATIONSHIPRESULTPUBLISHER_H
