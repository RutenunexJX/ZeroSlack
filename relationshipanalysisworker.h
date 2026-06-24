#ifndef RELATIONSHIPANALYSISWORKER_H
#define RELATIONSHIPANALYSISWORKER_H

#include "projectmodel.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"

#include <QPair>
#include <QString>
#include <QtGlobal>
#include <QVector>
#include <memory>

struct WorkspaceRelationshipAnalysisResult {
    QVector<QPair<QString, QVector<RelationshipToAdd>>> fileRelationships;
    SemanticSnapshotToken baseSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
    bool cancelled = false;
    int totalFiles = 0;
    int processedFiles = 0;
    int relationshipCount = 0;
    qint64 elapsedMs = -1;
};

struct SingleFileRelationshipAnalysisResult {
    QString fileName;
    QVector<RelationshipToAdd> relationships;
    SemanticSnapshotToken baseSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
};

class RelationshipAnalysisWorker
{
public:
    static SingleFileRelationshipAnalysisResult analyzeSingleFile(
        SmartRelationshipBuilder* relationshipBuilder,
        const QString& fileName,
        const QString& content,
        const SemanticSnapshotToken& baseSnapshot);

    static WorkspaceRelationshipAnalysisResult analyzeWorkspace(
        SmartRelationshipBuilder* relationshipBuilder,
        const ProjectSnapshot& project,
        const SemanticSnapshotToken& baseSnapshot);
};

#endif // RELATIONSHIPANALYSISWORKER_H
