#include "semanticindex.h"

#include "semanticindexsnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <utility>

namespace {
QString normalizedPublicationFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

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

QHash<QString, QString> normalizedFileContents(
    const QHash<QString, QString>& fileContents)
{
    QHash<QString, QString> normalized;
    normalized.reserve(fileContents.size());
    for (auto it = fileContents.constBegin(); it != fileContents.constEnd(); ++it) {
        const QString fileName = normalizedPublicationFileName(it.key());
        if (fileName.isEmpty())
            continue;
        QString content = it.value();
        if (!content.isEmpty() && content.front() == QChar(u'\ufeff'))
            content.remove(0, 1);
        content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        content.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        normalized.insert(fileName, std::move(content));
    }
    return normalized;
}

QSet<QString> changedSnapshotFiles(
    const SemanticIndexSnapshot* previousSnapshot,
    const QHash<QString, QString>& currentFileContents)
{
    QSet<QString> changed;
    if (!previousSnapshot)
        return changed;

    const QHash<QString, QString> previousContents =
        normalizedFileContents(previousSnapshot->fileContents());
    const QHash<QString, QString> currentContents =
        normalizedFileContents(currentFileContents);
    for (auto it = previousContents.constBegin();
         it != previousContents.constEnd();
         ++it) {
        const auto current = currentContents.constFind(it.key());
        // Missing content is unknown, not evidence of a dirty edit. Removed
        // declarations are rejected separately by stable endpoint validation.
        if (current != currentContents.constEnd() && current.value() != it.value())
            changed.insert(it.key());
    }
    return changed;
}

bool relationshipTouchesChangedFile(
    const SemanticRelationship& relationship,
    const QSet<QString>& changedFiles)
{
    if (changedFiles.isEmpty())
        return false;

    const QString fromFile =
        normalizedPublicationFileName(relationship.fromStableKey.fileName);
    const QString toFile =
        normalizedPublicationFileName(relationship.toStableKey.fileName);
    const QString evidenceFile =
        normalizedPublicationFileName(relationship.evidenceRange.fileName);
    return (!fromFile.isEmpty() && changedFiles.contains(fromFile))
        || (!toFile.isEmpty() && changedFiles.contains(toFile))
        || (!evidenceFile.isEmpty() && changedFiles.contains(evidenceFile));
}

QString relationshipEndpointKey(const SemanticRelationship& relationship)
{
    const QString fromKey = symbolStableKeyText(relationship.fromStableKey);
    const QString toKey = symbolStableKeyText(relationship.toStableKey);
    if (fromKey.isEmpty() || toKey.isEmpty())
        return QString();
    return QStringLiteral("%1|%2|%3")
        .arg(QString::number(static_cast<int>(relationship.type)),
             fromKey,
             toKey);
}

SemanticIndexSnapshot publicationSnapshotFromSemanticRecords(
    const SemanticIndex* index,
    QList<SemanticDiagnostic> diagnostics,
    QHash<QString, QString> fileContents)
{
    if (!index) {
        return SemanticIndexSnapshot::fromSymbolRecords(
            {},
            {},
            std::move(diagnostics),
            std::move(fileContents));
    }

    const QList<SemanticSymbolRecord> symbolRecords =
        index->getSymbolRecords();

    // The caller supplies known captured/native inputs, including empty files.
    // Do not infer content from symbol presence or fall back to a removed input.
    const std::shared_ptr<const SemanticIndexSnapshot> previousSnapshot =
        index->snapshot();
    const QSet<QString> changedFiles =
        changedSnapshotFiles(previousSnapshot.get(), fileContents);

    QHash<int, SymbolStableKey> stableKeyByHandle;
    QSet<QString> currentStableKeys;
    stableKeyByHandle.reserve(symbolRecords.size());
    currentStableKeys.reserve(symbolRecords.size());
    for (const SemanticSymbolRecord& record : symbolRecords) {
        if (record.localHandle >= 0)
            stableKeyByHandle.insert(record.localHandle, record.stableKey);
        const QString stableKey = symbolStableKeyText(record.stableKey);
        if (!stableKey.isEmpty())
            currentStableKeys.insert(stableKey);
    }

    QList<SemanticRelationship> relationships;
    QSet<QString> seenRelationships;
    QSet<QString> authoritativeEndpoints;
    auto appendRelationship =
        [&relationships,
         &seenRelationships,
         &authoritativeEndpoints,
         &currentStableKeys,
         &changedFiles](SemanticRelationship relationship,
                        bool derivedFromEngine) {
            const QString fromKey =
                symbolStableKeyText(relationship.fromStableKey);
            const QString toKey =
                symbolStableKeyText(relationship.toStableKey);
            if (fromKey.isEmpty() || toKey.isEmpty()
                || !currentStableKeys.contains(fromKey)
                || !currentStableKeys.contains(toKey)
                || relationshipTouchesChangedFile(relationship,
                                                  changedFiles)) {
                return;
            }

            const QString endpointKey = relationshipEndpointKey(relationship);
            if (derivedFromEngine
                && authoritativeEndpoints.contains(endpointKey)) {
                return;
            }
            const QString key = semanticRelationshipStableKeyText(relationship);
            if (key.isEmpty() || seenRelationships.contains(key))
                return;
            seenRelationships.insert(key);
            authoritativeEndpoints.insert(endpointKey);
            relationships.append(std::move(relationship));
        };

    // Published stable-key relationships are authoritative across symbol
    // handle churn. A clean overlay therefore rebinds them to the new record
    // handles instead of reconstructing them from a stale engine graph.
    if (previousSnapshot) {
        for (const SemanticRelationship& relationship
             : previousSnapshot->relationships()) {
            appendRelationship(relationship, false);
        }
    }

    // The engine is a derived mirror and may contribute newly computed edges,
    // but it is not allowed to erase or supersede the authoritative snapshot.
    if (SymbolRelationshipEngine* engine = index->relationshipEngine()) {
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
                    relationship.fromStableKey = record.stableKey;
                    relationship.toStableKey =
                        stableKeyByHandle.value(relatedHandle);
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
                    appendRelationship(std::move(relationship), true);
                }
            }
        }
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
    m_preparedAnalysisBandReport = {};
    m_preparedAnalysisBandReportValid = false;
    ++m_snapshotRevision;
    if (m_snapshot) {
        if (m_relationshipEngine) {
            m_relationshipEngine->replaceRelationshipsFromSnapshot(
                m_snapshot->getSymbolRecords(),
                m_snapshot->relationships());
        }
        if (m_publicationObserver)
            m_publicationObserver(m_snapshotRevision, m_snapshot->symbolRecordCount(), m_snapshot->relationshipCount(), {});
    }
}

SemanticIndexRetirementPayload SemanticIndex::installPreparedSnapshot(
    std::shared_ptr<const SemanticIndexSnapshot> snapshot,
    const QStringList& changedFiles,
    const QHash<QString, QSet<int>>& relationshipHandlesByFile,
    const QList<SemanticRelationship>& relationships,
    bool relationshipDeltaPrepared,
    std::shared_ptr<
        SymbolRelationshipEngine::PreparedRelationshipState>
        relationshipState,
    const SemanticAnalysisBandReport& analysisBandReport,
    bool notify)
{
    // Quiet commit requires the worker-prepared query state; legacy adapters
    // may build relationships only outside a publication transaction.
    if (!snapshot || (!notify && !relationshipState))
        return {};

    SemanticIndexRetirementPayload retired;
    const bool relationshipsInstalledQuietly = bool(relationshipState);
    retired.snapshot = std::exchange(m_snapshot, std::move(snapshot));
    m_preparedAnalysisBandReport = analysisBandReport;
    m_preparedAnalysisBandReportValid = true;
    ++m_snapshotRevision;
    if (m_relationshipEngine) {
        if (relationshipState) {
            retired.relationshipState =
                m_relationshipEngine->installPreparedRelationshipState(
                    std::move(relationshipState), false);
        } else if (relationshipDeltaPrepared) {
            m_relationshipEngine->replacePreparedRelationshipsForFiles(
                relationshipHandlesByFile,
                relationships,
                changedFiles);
        } else {
            m_relationshipEngine->replaceRelationshipsForFilesFromSnapshot(
                m_snapshot->getSymbolRecords(),
                m_snapshot->relationships(),
                changedFiles);
        }
    }
    if (notify)
        notifyPreparedSnapshotPublished(m_snapshotRevision, changedFiles, relationshipsInstalledQuietly);
    return retired;
}

void SemanticIndex::notifyPreparedSnapshotPublished(
    std::uint64_t revision, const QStringList& changedFiles, bool notifyRelationships)
{
    if (m_snapshotRevision != revision || !m_snapshot)
        return;
    if (notifyRelationships && m_relationshipEngine)
        emit m_relationshipEngine->relationshipsReplaced();
    if (m_snapshotRevision != revision || !m_snapshot)
        return;
    if (m_publicationObserver)
        m_publicationObserver(m_snapshotRevision, m_snapshot->symbolRecordCount(), m_snapshot->relationshipCount(), changedFiles);
}

void SemanticIndex::clearSnapshot()
{
    m_snapshot.reset();
    m_preparedAnalysisBandReport = {};
    m_preparedAnalysisBandReportValid = false;
    ++m_snapshotRevision;
}

void SemanticIndex::clearSemanticState()
{
    clearSnapshot();
    clearWorkspaceFileAnalysisBands();
    if (m_relationshipEngine)
        m_relationshipEngine->clearAllRelationships();
}

std::shared_ptr<const SemanticIndexSnapshot> SemanticIndex::snapshot() const
{
    return m_snapshot;
}

bool SemanticIndex::hasSymbolRecords() const
{
    const std::shared_ptr<const SemanticIndexSnapshot> current = snapshot();
    return current && current->symbolRecordCount() > 0;
}

std::uint64_t SemanticIndex::snapshotRevision() const
{
    return m_snapshotRevision;
}

SemanticSnapshotToken SemanticIndex::snapshotToken() const
{
    return {m_snapshot, m_snapshotRevision};
}

void SemanticIndex::publishSnapshotReplacingDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics,
    int diagnosticDisplayLimit)
{
    setSnapshot(captureSnapshotReplacingDiagnostics(fileNames, diagnostics, diagnosticDisplayLimit));
}

SemanticIndexSnapshot SemanticIndex::captureSnapshotWithRawDiagnostics() const
{
    if (!m_relationshipEngine)
        return m_snapshot ? *m_snapshot : SemanticIndexSnapshot{};
    return publicationSnapshotFromSemanticRecords(this,
        m_snapshot ? m_snapshot->rawDiagnostics() : QList<SemanticDiagnostic>{},
        m_snapshot ? m_snapshot->fileContents() : QHash<QString, QString>{});
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::captureSnapshotPreservingDiagnostics() const
{
    return std::make_shared<const SemanticIndexSnapshot>(
        captureSnapshotWithRawDiagnostics().withDiagnosticDisplayLimit(
            m_snapshot ? m_snapshot->diagnosticDisplayLimit() : 0));
}

std::shared_ptr<const SemanticIndexSnapshot>
SemanticIndex::captureSnapshotReplacingDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics,
    int diagnosticDisplayLimit) const
{
    const int limit = diagnosticDisplayLimit < 0
        ? (m_snapshot ? m_snapshot->diagnosticDisplayLimit() : 0) : diagnosticDisplayLimit;
    return std::make_shared<const SemanticIndexSnapshot>(
        captureSnapshotWithRawDiagnostics().withReplacedDiagnostics(fileNames, diagnostics)
            .withDiagnosticDisplayLimit(limit));
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
