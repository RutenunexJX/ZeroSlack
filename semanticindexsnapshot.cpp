#include "semanticindexsnapshot.h"

#include <QDir>
#include <QHash>
#include <QSet>
#include <utility>

namespace {
QString normalizedSnapshotFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

struct SnapshotRecordLookup {
    explicit SnapshotRecordLookup(const QList<SemanticSymbolRecord>& records)
    {
        recordByLocalHandle.reserve(records.size());
        localHandleByStableKey.reserve(records.size());
        for (const SemanticSymbolRecord& record : records) {
            if (record.localHandle >= 0)
                recordByLocalHandle.insert(record.localHandle, record);

            const QString stableKey = symbolStableKeyText(record.stableKey);
            if (!stableKey.isEmpty()
                && !localHandleByStableKey.contains(stableKey)) {
                localHandleByStableKey.insert(stableKey, record.localHandle);
            }
        }
    }

    SymbolStableKey stableKeyForLocalHandle(int localHandle) const
    {
        const auto it = recordByLocalHandle.constFind(localHandle);
        return it == recordByLocalHandle.constEnd()
            ? SymbolStableKey()
            : it.value().stableKey;
    }

    int localHandleForStableKey(const SymbolStableKey& key) const
    {
        const QString stableKey = symbolStableKeyText(key);
        if (stableKey.isEmpty())
            return -1;
        return localHandleByStableKey.value(stableKey, -1);
    }

    QHash<int, SemanticSymbolRecord> recordByLocalHandle;
    QHash<QString, int> localHandleByStableKey;
};

void fillRelationshipStableKeys(
    SemanticRelationship* relationship,
    const SnapshotRecordLookup& lookup)
{
    if (!relationship)
        return;

    if (!relationship->fromStableKey.isValid()) {
        relationship->fromStableKey =
            lookup.stableKeyForLocalHandle(relationship->fromId);
    }
    if (!relationship->toStableKey.isValid()) {
        relationship->toStableKey =
            lookup.stableKeyForLocalHandle(relationship->toId);
    }
}

SemanticRelationship rebindRelationshipToSnapshot(
    const SemanticRelationship& relationship,
    const SnapshotRecordLookup& lookup)
{
    SemanticRelationship rebound = relationship;
    fillRelationshipStableKeys(&rebound, lookup);

    const int reboundFromHandle =
        lookup.localHandleForStableKey(rebound.fromStableKey);
    if (reboundFromHandle >= 0)
        rebound.fromId = reboundFromHandle;

    const int reboundToHandle =
        lookup.localHandleForStableKey(rebound.toStableKey);
    if (reboundToHandle >= 0)
        rebound.toId = reboundToHandle;

    fillRelationshipStableKeys(&rebound, lookup);
    return rebound;
}

QString snapshotRelationshipDedupeKey(const SemanticRelationship& relationship)
{
    const QString stableKey =
        semanticRelationshipStableKeyText(relationship);
    if (!stableKey.isEmpty())
        return stableKey;

    return QStringLiteral("local:%1:%2:%3")
        .arg(relationship.fromId)
        .arg(relationship.toId)
        .arg(static_cast<int>(relationship.type));
}

}

SemanticIndexSnapshot::SemanticIndexSnapshot()
    : SemanticIndexSnapshot(FromRecordsTag{},
                            {},
                            {},
                            {},
                            {})
{
}

SemanticIndexSnapshot::SemanticIndexSnapshot(
    FromRecordsTag,
    QList<SemanticSymbolRecord> symbolRecords,
    QList<SemanticRelationship> relationships,
    QList<SemanticDiagnostic> diagnostics,
    QHash<QString, QString> fileContents)
    : m_symbolRecords(std::move(symbolRecords)),
      m_relationships(std::move(relationships)),
      m_diagnostics(std::move(diagnostics)),
      m_fileContents(std::move(fileContents))
{
    rebuildSymbolIndexes();
    const SnapshotRecordLookup lookup(m_symbolRecords);
    for (SemanticRelationship& relationship : m_relationships)
        relationship = rebindRelationshipToSnapshot(relationship, lookup);
}

SemanticIndexSnapshot SemanticIndexSnapshot::fromSymbolRecords(
    QList<SemanticSymbolRecord> symbolRecords,
    QList<SemanticRelationship> relationships,
    QList<SemanticDiagnostic> diagnostics,
    QHash<QString, QString> fileContents)
{
    return SemanticIndexSnapshot(FromRecordsTag{},
                                 std::move(symbolRecords),
                                 std::move(relationships),
                                 std::move(diagnostics),
                                 std::move(fileContents));
}

void SemanticIndexSnapshot::rebuildSymbolIndexes()
{
    m_symbolRecordIndexesByFile.clear();
    m_symbolRecordIndexesByName.clear();
    m_symbolRecordIndexByStableKey.clear();
    m_symbolRecordIndexByLocalHandle.clear();

    m_symbolRecordIndexesByFile.reserve(m_symbolRecords.size());
    m_symbolRecordIndexesByName.reserve(m_symbolRecords.size());
    m_symbolRecordIndexByStableKey.reserve(m_symbolRecords.size());
    m_symbolRecordIndexByLocalHandle.reserve(m_symbolRecords.size());

    for (int i = 0; i < m_symbolRecords.size(); ++i) {
        const SemanticSymbolRecord& record = m_symbolRecords.at(i);
        const QString fileName = normalizedSnapshotFileName(record.location.fileName);
        if (!fileName.isEmpty())
            m_symbolRecordIndexesByFile[fileName].append(i);

        if (!record.name.isEmpty())
            m_symbolRecordIndexesByName[record.name].append(i);

        const QString stableKey = symbolStableKeyText(record.stableKey);
        if (!stableKey.isEmpty()
            && !m_symbolRecordIndexByStableKey.contains(stableKey)) {
            m_symbolRecordIndexByStableKey.insert(stableKey, i);
        }

        if (record.localHandle >= 0
            && !m_symbolRecordIndexByLocalHandle.contains(record.localHandle)) {
            m_symbolRecordIndexByLocalHandle.insert(record.localHandle, i);
        }
    }
}

SemanticIndexSnapshot SemanticIndexSnapshot::withAdditionalRelationships(
    const QList<SemanticRelationship>& relationships) const
{
    QList<SemanticRelationship> merged = m_relationships;
    merged.reserve(m_relationships.size() + relationships.size());
    const SnapshotRecordLookup lookup(m_symbolRecords);
    QSet<QString> seen;
    seen.reserve(m_relationships.size() + relationships.size());
    for (const SemanticRelationship& relationship : std::as_const(merged)) {
        const QString key =
            snapshotRelationshipDedupeKey(relationship);
        if (!key.isEmpty())
            seen.insert(key);
    }

    for (const SemanticRelationship& relationship : relationships) {
        const SemanticRelationship rebound =
            rebindRelationshipToSnapshot(relationship, lookup);
        if (rebound.fromId < 0 || rebound.toId < 0)
            continue;
        const QString key =
            snapshotRelationshipDedupeKey(rebound);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        merged.append(rebound);
    }

    return SemanticIndexSnapshot(FromRecordsTag{},
                                 m_symbolRecords,
                                 merged,
                                 m_diagnostics,
                                 m_fileContents);
}

SemanticIndexSnapshot SemanticIndexSnapshot::withReplacedDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics) const
{
    if (fileNames.isEmpty())
        return SemanticIndexSnapshot(FromRecordsTag{},
                                     m_symbolRecords,
                                     m_relationships,
                                     diagnostics,
                                     m_fileContents);

    QSet<QString> targetFiles;
    for (const QString& fileName : fileNames) {
        const QString normalized = normalizedSnapshotFileName(fileName);
        if (!normalized.isEmpty())
            targetFiles.insert(normalized);
    }

    QList<SemanticDiagnostic> merged;
    for (const SemanticDiagnostic& diagnostic : m_diagnostics) {
        const QString normalized = normalizedSnapshotFileName(diagnostic.fileName);
        if (!normalized.isEmpty() && targetFiles.contains(normalized))
            continue;
        merged.append(diagnostic);
    }
    merged.append(diagnostics);

    return SemanticIndexSnapshot(FromRecordsTag{},
                                 m_symbolRecords,
                                 m_relationships,
                                 merged,
                                 m_fileContents);
}
