#include "relationshipanalysisworker.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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
        SemanticRelationship item;
        item.fromId = relationship.fromId;
        item.toId = relationship.toId;
        item.type = relationship.type;
        item.confidence = relationship.confidence;
        item.evidenceText = relationship.context;
        item.evidenceRange = relationship.evidenceRange;
        item.fromAccessPath = relationship.fromAccessPath;
        item.toAccessPath = relationship.toAccessPath;
        item.provenance = RelationshipProvenance::Inferred;
        result.append(item);
    }
    return result;
}

QList<SemanticSymbolRecord> fileSymbolRecords(
    const SemanticSnapshotToken& snapshotToken,
    const QString& fileName)
{
    if (!snapshotToken.isValid())
        return {};
    return snapshotToken.snapshot->getSymbolRecords(fileName);
}

QString normalizedFileKey(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

void markCancelled(WorkspaceRelationshipAnalysisResult* result)
{
    if (!result)
        return;
    result->cancelled = true;
    result->fileRelationships.clear();
    result->semanticSnapshot = result->baseSnapshot.snapshot;
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

    const QList<SemanticSymbolRecord> fileSymbols =
        fileSymbolRecords(baseSnapshot, fileName);
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
    QElapsedTimer elapsed;
    elapsed.start();
    WorkspaceRelationshipAnalysisResult result;
    result.baseSnapshot = baseSnapshot;
    result.semanticSnapshot = baseSnapshot.snapshot;
    result.totalFiles = project.systemVerilogFiles.size();
    auto finish = [&]() {
        result.elapsedMs = elapsed.elapsed();
        return result;
    };
    auto finishCancelled = [&]() {
        markCancelled(&result);
        result.elapsedMs = elapsed.elapsed();
        return result;
    };

    if (!relationshipBuilder)
        return finish();
    if (relationshipBuilder->isCancelled())
        return finishCancelled();

    const QStringList svFiles = project.systemVerilogFiles;
    const QHash<QString, RelationshipExtractionInfo> relationshipInfoByFile =
        relationshipBuilder->extractWorkspaceRelationshipInfo(svFiles,
                                                              project.includeDirs,
                                                              project.defines);
    if (relationshipBuilder->isCancelled())
        return finishCancelled();
    result.fileRelationships.reserve(svFiles.size());
    QList<SemanticRelationship> newRelationships;
    for (const QString& filePath : svFiles) {
        if (relationshipBuilder->isCancelled())
            return finishCancelled();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString content = QTextStream(&file).readAll();
        const QList<SemanticSymbolRecord> fileSymbols =
            fileSymbolRecords(baseSnapshot, filePath);
        const auto infoIt =
            relationshipInfoByFile.constFind(normalizedFileKey(filePath));
        const RelationshipExtractionInfo* relationshipInfo =
            infoIt == relationshipInfoByFile.constEnd() ? nullptr : &infoIt.value();
        const QVector<RelationshipToAdd> relationships =
            relationshipBuilder->computeRelationships(
                filePath,
                content,
                fileSymbols,
                baseSnapshot.snapshot.get(),
                project.includeDirs,
                project.defines,
                relationshipInfo);
        if (relationshipBuilder->isCancelled())
            return finishCancelled();
        const QList<SemanticRelationship> semanticRelationships =
            toSemanticRelationships(relationships);
        result.fileRelationships.append({filePath, relationships});
        ++result.processedFiles;
        result.relationshipCount += semanticRelationships.size();
        newRelationships.append(semanticRelationships);
    }
    result.semanticSnapshot =
        SemanticIndex::getInstance()->snapshotWithAdditionalRelationships(
            baseSnapshot.snapshot,
            newRelationships);
    return finish();
}
