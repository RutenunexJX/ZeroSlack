#include "relationshipanalysisworker.h"
#include "semanticanalysisinput.h"

#include <QElapsedTimer>

namespace {
QVector<RelationshipToAdd> publishedRelationships(
    const SemanticSnapshotToken& token, const QString& fileName)
{
    QVector<RelationshipToAdd> result;
    if (!token.snapshot)
        return result;
    const auto relationships = token.snapshot->relationshipsOwnedByFile(
        SemanticInputCapture::pathKey(fileName));
    result.reserve(relationships.size());
    for (const auto& relationship : relationships) {
        RelationshipToAdd item;
        item.fromId = relationship.fromId;
        item.toId = relationship.toId;
        item.type = relationship.type;
        item.context = relationship.evidenceText;
        item.confidence = relationship.confidence;
        item.evidenceRange = relationship.evidenceRange;
        item.fromAccessPath = relationship.fromAccessPath;
        item.toAccessPath = relationship.toAccessPath;
        item.exactValueForward = relationship.exactValueForward;
        result.append(std::move(item));
    }
    return result;
}
}

// Retained source-compatible adapters. Compilation and ownership live in the
// unified semantic worker. These methods only project that immutable output;
// neither disk reads nor a per-file Slang fallback are permitted here.
SingleFileRelationshipAnalysisResult RelationshipAnalysisWorker::analyzeSingleFile(
    SmartRelationshipBuilder* builder, const QString& fileName,
    const QString& content, const SemanticSnapshotToken& publication)
{
    SingleFileRelationshipAnalysisResult result;
    result.fileName = fileName;
    result.baseSnapshot = publication;
    result.semanticSnapshot = publication.snapshot;
    if ((builder && builder->isCancelled()) || !publication.snapshot)
        return result;
    const auto& contents = publication.snapshot->fileContentsView();
    const auto found = contents.constFind(SemanticInputCapture::pathKey(fileName));
    if (found == contents.cend() || found.value() != content)
        return result;
    result.relationships = publishedRelationships(publication, fileName);
    return result;
}

WorkspaceRelationshipAnalysisResult RelationshipAnalysisWorker::analyzeWorkspace(
    SmartRelationshipBuilder* builder, const ProjectSnapshot& project,
    const SemanticSnapshotToken& publication, std::uint64_t generation,
    const QString& projectKey, const std::function<bool()>& isCancelled)
{
    QElapsedTimer timer;
    timer.start();
    WorkspaceRelationshipAnalysisResult result;
    result.requestGeneration = generation;
    result.projectKey = projectKey;
    result.baseSnapshot = publication;
    result.semanticSnapshot = publication.snapshot;
    result.totalFiles = project.systemVerilogFiles.size();
    for (const QString& file : project.systemVerilogFiles) {
        if ((builder && builder->isCancelled()) || (isCancelled && isCancelled())) {
            result.cancelled = true;
            break;
        }
        auto relationships = publishedRelationships(publication, file);
        result.relationshipCount += relationships.size();
        result.fileRelationships.append({file, std::move(relationships)});
        ++result.processedFiles;
    }
    result.elapsedMs = timer.elapsed();
    return result;
}
