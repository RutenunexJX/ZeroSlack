#include "semanticindex.h"

#include "semanticindexsnapshot.h"

#include <utility>

void SemanticIndex::setSnapshot(std::shared_ptr<const SemanticIndexSnapshot> snapshot)
{
    m_snapshot = std::move(snapshot);
    ++m_snapshotRevision;
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
        SemanticIndexSnapshot::fromSymbolDatabase(symbolDatabase(), std::move(diagnostics))));
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
        SemanticIndexSnapshot::fromSymbolDatabase(symbolDatabase(), diagnostics));
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
        SemanticIndexSnapshot::fromSymbolDatabase(symbolDatabase(), mergedDiagnostics));
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

bool SemanticIndex::publishSnapshotIfCurrent(
    std::shared_ptr<const SemanticIndexSnapshot> expectedCurrentSnapshot,
    std::shared_ptr<const SemanticIndexSnapshot> nextSnapshot)
{
    return publishSnapshotIfCurrent(
        {std::move(expectedCurrentSnapshot), snapshotRevision()},
        std::move(nextSnapshot));
}
