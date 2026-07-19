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

QString fileContentLookupKey(const QString& fileName)
{
    QString key = normalizedFileKey(fileName);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
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
    const SemanticSnapshotToken& baseSnapshot,
    std::uint64_t requestGeneration,
    const QString& projectKey)
{
    QElapsedTimer elapsed;
    elapsed.start();
    WorkspaceRelationshipAnalysisResult result;
    result.requestGeneration = requestGeneration;
    result.projectKey = projectKey;
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
    QElapsedTimer stageTimer;
    stageTimer.start();
    QHash<QString, QString> snapshotContentsByKey;
    if (baseSnapshot.snapshot) {
        const QHash<QString, QString> snapshotContents =
            baseSnapshot.snapshot->fileContents();
        for (auto it = snapshotContents.constBegin();
             it != snapshotContents.constEnd();
             ++it) {
            if (relationshipBuilder->isCancelled())
                return finishCancelled();
            const QString lookupKey = fileContentLookupKey(it.key());
            if (!lookupKey.isEmpty())
                snapshotContentsByKey.insert(lookupKey, it.value());
        }
    }

    // Materialize one immutable source set for both Slang extraction and the
    // per-file relationship computation. A captured snapshot is authoritative
    // for files it contains, including an intentionally empty unsaved buffer.
    QHash<QString, QString> workspaceContents;
    workspaceContents.reserve(svFiles.size());
    for (const QString& filePath : svFiles) {
        if (relationshipBuilder->isCancelled())
            return finishCancelled();
        const QString normalizedPath = normalizedFileKey(filePath);
        const QString lookupKey = fileContentLookupKey(filePath);
        if (normalizedPath.isEmpty() || lookupKey.isEmpty())
            continue;

        const auto snapshotIt = snapshotContentsByKey.constFind(lookupKey);
        if (snapshotIt != snapshotContentsByKey.constEnd()) {
            workspaceContents.insert(normalizedPath, snapshotIt.value());
            continue;
        }

        stageTimer.restart();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.fileReadMs += stageTimer.elapsed();
            continue;
        }
        workspaceContents.insert(normalizedPath, QTextStream(&file).readAll());
        result.fileReadMs += stageTimer.elapsed();
    }

    stageTimer.restart();
    const QHash<QString, RelationshipExtractionInfo> relationshipInfoByFile =
        relationshipBuilder->extractOverlayWorkspaceRelationshipInfo(
            workspaceContents,
            project.includeDirs,
            project.defines,
            svFiles);
    result.extractionMs = stageTimer.elapsed();
    if (relationshipBuilder->isCancelled())
        return finishCancelled();
    result.fileRelationships.reserve(svFiles.size());
    QList<SemanticRelationship> newRelationships;
    for (const QString& filePath : svFiles) {
        if (relationshipBuilder->isCancelled())
            return finishCancelled();
        const QString sourceKey = normalizedFileKey(filePath);
        const auto contentIt = workspaceContents.constFind(sourceKey);
        if (contentIt == workspaceContents.constEnd())
            continue;
        const QString& content = contentIt.value();
        const QList<SemanticSymbolRecord> fileSymbols =
            fileSymbolRecords(baseSnapshot, filePath);
        const auto infoIt =
            relationshipInfoByFile.constFind(sourceKey);
        const RelationshipExtractionInfo* relationshipInfo =
            infoIt == relationshipInfoByFile.constEnd() ? nullptr : &infoIt.value();
        stageTimer.restart();
        const QVector<RelationshipToAdd> relationships =
            relationshipBuilder->computeRelationships(
                filePath,
                content,
                fileSymbols,
                baseSnapshot.snapshot.get(),
                project.includeDirs,
                project.defines,
                relationshipInfo);
        result.computeMs += stageTimer.elapsed();
        if (relationshipBuilder->isCancelled())
            return finishCancelled();
        stageTimer.restart();
        const QList<SemanticRelationship> semanticRelationships =
            toSemanticRelationships(relationships);
        result.fileRelationships.append({filePath, relationships});
        ++result.processedFiles;
        result.relationshipCount += semanticRelationships.size();
        newRelationships.append(semanticRelationships);
        result.conversionMs += stageTimer.elapsed();
    }
    stageTimer.restart();
    result.semanticSnapshot =
        SemanticIndex::getInstance()->snapshotWithAdditionalRelationships(
            baseSnapshot.snapshot,
            newRelationships);
    result.snapshotMergeMs = stageTimer.elapsed();
    return finish();
}
