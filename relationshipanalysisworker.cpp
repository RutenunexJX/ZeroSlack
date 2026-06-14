#include "relationshipanalysisworker.h"

#include "semanticindex.h"

#include <QFile>
#include <QTextStream>

namespace {
QList<SemanticRelationship> toSemanticRelationships(
    const QVector<RelationshipToAdd>& relationships)
{
    QList<SemanticRelationship> result;
    result.reserve(relationships.size());
    for (const RelationshipToAdd& relationship : relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        result.append({relationship.fromId, relationship.toId, relationship.type});
    }
    return result;
}
}

SingleFileRelationshipAnalysisResult RelationshipAnalysisWorker::analyzeSingleFile(
    SmartRelationshipBuilder* relationshipBuilder,
    const QString& fileName,
    const QString& content,
    const SemanticSnapshotToken& baseSnapshot)
{
    SingleFileRelationshipAnalysisResult result;
    result.fileName = fileName;
    result.baseSnapshot = baseSnapshot;
    result.semanticSnapshot = baseSnapshot.snapshot;
    if (!relationshipBuilder || !baseSnapshot.isValid())
        return result;

    const QList<sym_list::SymbolInfo> fileSymbols =
        baseSnapshot.snapshot->getSymbols(fileName);
    result.relationships =
        relationshipBuilder->computeRelationships(
            fileName, content, fileSymbols, baseSnapshot.snapshot.get());

    result.semanticSnapshot =
        SemanticIndex::getInstance()->snapshotWithAdditionalRelationships(
            baseSnapshot.snapshot,
            toSemanticRelationships(result.relationships));
    return result;
}

WorkspaceRelationshipAnalysisResult RelationshipAnalysisWorker::analyzeWorkspace(
    SmartRelationshipBuilder* relationshipBuilder,
    const ProjectSnapshot& project,
    const SemanticSnapshotToken& baseSnapshot)
{
    WorkspaceRelationshipAnalysisResult result;
    result.baseSnapshot = baseSnapshot;
    result.semanticSnapshot = baseSnapshot.snapshot;
    result.totalFiles = project.systemVerilogFiles.size();
    if (!relationshipBuilder)
        return result;

    relationshipBuilder->resetCancellation();
    const QStringList svFiles = project.systemVerilogFiles;
    result.fileRelationships.reserve(svFiles.size());
    QList<SemanticRelationship> newRelationships;
    for (const QString& filePath : svFiles) {
        if (relationshipBuilder->isCancelled())
            break;
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString content = QTextStream(&file).readAll();
        const QList<sym_list::SymbolInfo> fileSymbols =
            baseSnapshot.isValid()
                ? baseSnapshot.snapshot->getSymbols(filePath)
                : QList<sym_list::SymbolInfo>();
        const QVector<RelationshipToAdd> relationships =
            relationshipBuilder->computeRelationships(
                filePath,
                content,
                fileSymbols,
                baseSnapshot.snapshot.get(),
                project.includeDirs,
                project.defines);
        result.fileRelationships.append({filePath, relationships});
        newRelationships.append(toSemanticRelationships(relationships));
    }
    result.semanticSnapshot =
        SemanticIndex::getInstance()->snapshotWithAdditionalRelationships(
            baseSnapshot.snapshot,
            newRelationships);
    return result;
}
