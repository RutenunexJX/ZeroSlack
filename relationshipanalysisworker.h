#ifndef RELATIONSHIPANALYSISWORKER_H
#define RELATIONSHIPANALYSISWORKER_H

#include "projectmodel.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"

#include <QPair>
#include <QString>
#include <QVector>
#include <memory>

struct WorkspaceRelationshipAnalysisResult {
    QVector<QPair<QString, QVector<RelationshipToAdd>>> fileRelationships;
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
    int totalFiles = 0;
};

struct SingleFileRelationshipAnalysisResult {
    QString fileName;
    QVector<RelationshipToAdd> relationships;
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
};

class RelationshipAnalysisWorker
{
public:
    static SingleFileRelationshipAnalysisResult analyzeSingleFile(
        SmartRelationshipBuilder* relationshipBuilder,
        const QString& fileName,
        const QString& content,
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot);

    static WorkspaceRelationshipAnalysisResult analyzeWorkspace(
        SmartRelationshipBuilder* relationshipBuilder,
        const ProjectSnapshot& project,
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot);
};

#endif // RELATIONSHIPANALYSISWORKER_H
