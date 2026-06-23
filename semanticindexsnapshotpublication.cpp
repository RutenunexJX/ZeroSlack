#include "semanticindex.h"

#include "activitylogservice.h"
#include "semanticindexsnapshot.h"

#include <QSet>
#include <utility>

namespace {
QList<SymbolRelationshipEngine::RelationType> snapshotPublicationRelationshipTypes()
{
    return {
        SymbolRelationshipEngine::CONTAINS,
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::INHERITS,
        SymbolRelationshipEngine::IMPLEMENTS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
        SymbolRelationshipEngine::GENERATES,
        SymbolRelationshipEngine::CONSTRAINS,
    };
}

SemanticIndexSnapshot publicationSnapshotFromSemanticRecords(
    const SemanticIndex* index,
    QList<SemanticDiagnostic> diagnostics)
{
    if (!index) {
        return SemanticIndexSnapshot::fromSymbolRecords(
            {},
            {},
            std::move(diagnostics),
            {});
    }

    const QList<SemanticSymbolRecord> symbolRecords =
        index->getSymbolRecords();

    QList<SemanticRelationship> relationships;
    if (SymbolRelationshipEngine* engine = index->relationshipEngine()) {
        QSet<QString> seen;
        for (const SemanticSymbolRecord& record : symbolRecords) {
            const int symbolHandle = record.localHandle;
            if (symbolHandle < 0)
                continue;
            for (SymbolRelationshipEngine::RelationType type
                 : snapshotPublicationRelationshipTypes()) {
                const QList<int> related =
                    engine->getRelatedSymbols(symbolHandle, type, true);
                for (int relatedHandle : related) {
                    SemanticRelationship relationship;
                    relationship.fromId = symbolHandle;
                    relationship.toId = relatedHandle;
                    relationship.type = type;
                    const SymbolRelationshipEngine::RelationshipEdgeMetadata metadata =
                        engine->getRelationshipMetadata(
                            relationship.fromId,
                            relationship.toId,
                            relationship.type);
                    if (metadata.found) {
                        relationship.provenance =
                            RelationshipProvenance::Inferred;
                        relationship.confidence = metadata.confidence;
                        relationship.evidenceText = metadata.context;
                        relationship.evidenceRange = metadata.evidenceRange;
                    }

                    const QString key = QStringLiteral("%1:%2:%3")
                                            .arg(relationship.fromId)
                                            .arg(relationship.toId)
                                            .arg(static_cast<int>(relationship.type));
                    if (seen.contains(key))
                        continue;
                    seen.insert(key);
                    relationships.append(relationship);
                }
            }
        }
    }

    QHash<QString, QString> fileContents;
    QSet<QString> seenFiles;
    for (const SemanticSymbolRecord& record : symbolRecords) {
        const QString fileName = record.location.fileName;
        if (fileName.isEmpty() || seenFiles.contains(fileName))
            continue;
        seenFiles.insert(fileName);
        fileContents.insert(
            fileName,
            index->getCachedFileContent(fileName));
    }

    return SemanticIndexSnapshot::fromSymbolRecords(
        symbolRecords,
        relationships,
        std::move(diagnostics),
        fileContents);
}
}

void SemanticIndex::setSnapshot(std::shared_ptr<const SemanticIndexSnapshot> snapshot)
{
    m_snapshot = std::move(snapshot);
    ++m_snapshotRevision;
    if (m_snapshot) {
        ActivityLogService::getInstance()->append(
            QStringLiteral("SemanticIndex"),
            ActivityLogLevel::Info,
            QStringLiteral("Published snapshot gen=%1 symbols=%2 relationships=%3")
                .arg(m_snapshotRevision)
                .arg(m_snapshot->getSymbolRecords().size())
                .arg(m_snapshot->relationships().size()));
    }
}

void SemanticIndex::clearSnapshot()
{
    m_snapshot.reset();
    ++m_snapshotRevision;
}

std::shared_ptr<const SemanticIndexSnapshot> SemanticIndex::snapshot() const
{
    return m_snapshot;
}

std::uint64_t SemanticIndex::snapshotRevision() const
{
    return m_snapshotRevision;
}

SemanticSnapshotToken SemanticIndex::snapshotToken() const
{
    return {m_snapshot, m_snapshotRevision};
}

void SemanticIndex::publishCompleteSnapshot(QList<SemanticDiagnostic> diagnostics)
{
    setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        publicationSnapshotFromSemanticRecords(this, std::move(diagnostics))));
}

void SemanticIndex::publishSnapshotReplacingDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics)
{
    setSnapshot(captureSnapshotReplacingDiagnostics(fileNames, diagnostics));
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::captureSnapshotPreservingDiagnostics() const
{
    const QList<SemanticDiagnostic> diagnostics =
        m_snapshot ? m_snapshot->diagnostics() : QList<SemanticDiagnostic>();
    return std::make_shared<const SemanticIndexSnapshot>(
        publicationSnapshotFromSemanticRecords(this, diagnostics));
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::captureSnapshotReplacingDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics) const
{
    QList<SemanticDiagnostic> mergedDiagnostics = diagnostics;
    if (m_snapshot) {
        mergedDiagnostics =
            m_snapshot->withReplacedDiagnostics(fileNames, diagnostics).diagnostics();
    }
    return std::make_shared<const SemanticIndexSnapshot>(
        publicationSnapshotFromSemanticRecords(this, mergedDiagnostics));
}

SemanticSnapshotToken
SemanticIndex::beginRelationshipAnalysisSnapshot()
{
    if (m_snapshot)
        return snapshotToken();

    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot =
        captureSnapshotPreservingDiagnostics();
    setSnapshot(baseSnapshot);
    return snapshotToken();
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::snapshotWithAdditionalRelationships(
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
    const QList<SemanticRelationship>& relationships) const
{
    if (!baseSnapshot)
        return baseSnapshot;
    return std::make_shared<const SemanticIndexSnapshot>(
        baseSnapshot->withAdditionalRelationships(relationships));
}

bool SemanticIndex::publishSnapshotIfCurrent(
    const SemanticSnapshotToken& expectedCurrentSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot> nextSnapshot)
{
    if (expectedCurrentSnapshot.isValid()
        && (snapshot() != expectedCurrentSnapshot.snapshot
            || snapshotRevision() != expectedCurrentSnapshot.revision)) {
        return false;
    }
    if (nextSnapshot)
        setSnapshot(std::move(nextSnapshot));
    return true;
}
