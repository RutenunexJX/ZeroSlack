#include "semanticindexsnapshot.h"

#include <QDir>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <utility>

namespace {
QString normalizedSnapshotFileName(const QString& fileName)
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

    bool containsStableKey(const SymbolStableKey& key) const
    {
        return localHandleForStableKey(key) >= 0;
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

bool sameStableIdentityIgnoringPosition(const SymbolStableKey& previous,
                                        const SymbolStableKey& current)
{
    return previous.isValid() && current.isValid()
        && normalizedSnapshotFileName(previous.fileName)
            == normalizedSnapshotFileName(current.fileName)
        && previous.symbolName == current.symbolName
        && previous.declarationKind == current.declarationKind
        && previous.ownerScope == current.ownerScope
        && previous.sourceLength == current.sourceLength;
}

void rebindRelationshipEndpoint(SymbolStableKey* stableKey,
                                int* localHandle,
                                const SnapshotRecordLookup& lookup)
{
    if (!stableKey || !localHandle)
        return;
    const int stableHandle = lookup.localHandleForStableKey(*stableKey);
    if (stableHandle >= 0) {
        *localHandle = stableHandle;
        return;
    }
    const SymbolStableKey handleKey =
        lookup.stableKeyForLocalHandle(*localHandle);
    if (!handleKey.isValid())
        return;
    if (!stableKey->isValid()
        || sameStableIdentityIgnoringPosition(*stableKey, handleKey)) {
        *stableKey = handleKey;
    }
}

SemanticRelationship rebindRelationshipToSnapshot(
    const SemanticRelationship& relationship,
    const SnapshotRecordLookup& lookup)
{
    SemanticRelationship rebound = relationship;
    fillRelationshipStableKeys(&rebound, lookup);

    rebindRelationshipEndpoint(&rebound.fromStableKey,
                               &rebound.fromId,
                               lookup);
    rebindRelationshipEndpoint(&rebound.toStableKey,
                               &rebound.toId,
                               lookup);

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
    QList<SemanticRelationship> reboundRelationships;
    reboundRelationships.reserve(m_relationships.size());
    QSet<QString> seenRelationships;
    seenRelationships.reserve(m_relationships.size());
    for (const SemanticRelationship& relationship : std::as_const(m_relationships)) {
        const SemanticRelationship rebound =
            rebindRelationshipToSnapshot(relationship, lookup);
        if (!lookup.containsStableKey(rebound.fromStableKey)
            || !lookup.containsStableKey(rebound.toStableKey)) {
            continue;
        }
        const QString key = snapshotRelationshipDedupeKey(rebound);
        if (key.isEmpty() || seenRelationships.contains(key))
            continue;
        seenRelationships.insert(key);
        reboundRelationships.append(rebound);
    }
    m_relationships = std::move(reboundRelationships);
    rebuildRelationshipIndexes();
}

QString snapshotRecordIdentity(const SemanticSymbolRecord& record)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(normalizedSnapshotFileName(record.location.fileName),
             record.name,
             QString::number(static_cast<int>(record.declarationKind)),
             record.owner.name,
             record.type.resolvedTypeName);
}

QString relationshipOwnerFile(const SemanticRelationship& relationship)
{
    QString owner = normalizedSnapshotFileName(
        relationship.evidenceRange.fileName);
    if (!owner.isEmpty())
        return owner;

    // Evidence-free legacy relationships are owned by their source endpoint.
    // The target endpoint is only a fallback when source provenance is absent.
    owner = normalizedSnapshotFileName(
        relationship.fromStableKey.fileName);
    if (!owner.isEmpty())
        return owner;
    return normalizedSnapshotFileName(relationship.toStableKey.fileName);
}

bool relationshipOwnedByFiles(const SemanticRelationship& relationship,
                              const QSet<QString>& files)
{
    if (files.isEmpty())
        return false;
    return files.contains(relationshipOwnerFile(relationship));
}

QSet<QString> normalizedSnapshotFiles(const QStringList& fileNames)
{
    QSet<QString> result;
    for (const QString& fileName : fileNames) {
        const QString normalized = normalizedSnapshotFileName(fileName);
        if (!normalized.isEmpty())
            result.insert(normalized);
    }
    return result;
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
    m_symbolRecordIndexesByOwner.clear();
    m_symbolRecordIndexesByDeclarationKind.clear();
    m_symbolRecordIndexByStableKey.clear();
    m_symbolRecordIndexByLocalHandle.clear();

    m_symbolRecordIndexesByFile.reserve(m_symbolRecords.size());
    m_symbolRecordIndexesByName.reserve(m_symbolRecords.size());
    m_symbolRecordIndexesByOwner.reserve(m_symbolRecords.size());
    m_symbolRecordIndexesByDeclarationKind.reserve(m_symbolRecords.size());
    m_symbolRecordIndexByStableKey.reserve(m_symbolRecords.size());
    m_symbolRecordIndexByLocalHandle.reserve(m_symbolRecords.size());

    for (int i = 0; i < m_symbolRecords.size(); ++i) {
        const SemanticSymbolRecord& record = m_symbolRecords.at(i);
        const QString fileName = normalizedSnapshotFileName(record.location.fileName);
        if (!fileName.isEmpty())
            m_symbolRecordIndexesByFile[fileName].append(i);

        if (!record.name.isEmpty())
            m_symbolRecordIndexesByName[record.name].append(i);

        m_symbolRecordIndexesByOwner[record.owner.name].append(i);
        m_symbolRecordIndexesByDeclarationKind[
            static_cast<int>(record.declarationKind)].append(i);

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

void SemanticIndexSnapshot::rebuildRelationshipIndexes()
{
    m_relationshipIndexesByFromStableKey.clear();
    m_relationshipIndexesByToStableKey.clear();
    m_relationshipIndexesByFromStableKey.reserve(m_relationships.size());
    m_relationshipIndexesByToStableKey.reserve(m_relationships.size());

    for (int i = 0; i < m_relationships.size(); ++i) {
        const SemanticRelationship& relationship = m_relationships.at(i);
        const QString fromKey = symbolStableKeyText(relationship.fromStableKey);
        if (!fromKey.isEmpty())
            m_relationshipIndexesByFromStableKey[fromKey].append(i);

        const QString toKey = symbolStableKeyText(relationship.toStableKey);
        if (!toKey.isEmpty())
            m_relationshipIndexesByToStableKey[toKey].append(i);
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

    SemanticIndexSnapshot next;
    next.m_symbolRecords = m_symbolRecords;
    next.m_symbolRecordIndexesByFile = m_symbolRecordIndexesByFile;
    next.m_symbolRecordIndexesByName = m_symbolRecordIndexesByName;
    next.m_symbolRecordIndexesByOwner = m_symbolRecordIndexesByOwner;
    next.m_symbolRecordIndexesByDeclarationKind =
        m_symbolRecordIndexesByDeclarationKind;
    next.m_symbolRecordIndexByStableKey = m_symbolRecordIndexByStableKey;
    next.m_symbolRecordIndexByLocalHandle = m_symbolRecordIndexByLocalHandle;
    next.m_relationships = std::move(merged);
    next.m_diagnostics = m_diagnostics;
    next.m_fileContents = m_fileContents;
    next.rebuildRelationshipIndexes();
    return next;
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

SemanticIndexSnapshot SemanticIndexSnapshot::withReplacedFiles(
    const QList<SemanticFileSymbolUpdate>& updates,
    const QStringList& diagnosticFiles,
    const QList<SemanticDiagnostic>& diagnostics,
    const QStringList& relationshipFiles,
    const QList<SemanticRelationship>& relationships) const
{
    QStringList updatedFileNames;
    updatedFileNames.reserve(updates.size());
    for (const SemanticFileSymbolUpdate& update : updates)
        updatedFileNames.append(update.fileName);
    const QSet<QString> updatedFiles =
        normalizedSnapshotFiles(updatedFileNames);

    QHash<QString, QQueue<int>> reusableHandles;
    int nextHandle = 1;
    for (const SemanticSymbolRecord& record : m_symbolRecords) {
        nextHandle = qMax(nextHandle, record.localHandle + 1);
        if (updatedFiles.contains(
                normalizedSnapshotFileName(record.location.fileName))) {
            reusableHandles[snapshotRecordIdentity(record)].enqueue(
                record.localHandle);
        }
    }

    QList<SemanticSymbolRecord> mergedRecords;
    QSet<int> usedHandles;
    mergedRecords.reserve(m_symbolRecords.size());
    for (const SemanticSymbolRecord& record : m_symbolRecords) {
        if (updatedFiles.contains(
                normalizedSnapshotFileName(record.location.fileName))) {
            continue;
        }
        mergedRecords.append(record);
        if (record.localHandle >= 0)
            usedHandles.insert(record.localHandle);
    }

    for (const SemanticFileSymbolUpdate& update : updates) {
        for (SemanticSymbolRecord record : update.symbolRecords) {
            QQueue<int>& candidates =
                reusableHandles[snapshotRecordIdentity(record)];
            int handle = -1;
            while (!candidates.isEmpty() && handle < 0) {
                const int candidate = candidates.dequeue();
                if (candidate >= 0 && !usedHandles.contains(candidate))
                    handle = candidate;
            }
            while (handle < 0 && usedHandles.contains(nextHandle))
                ++nextHandle;
            if (handle < 0)
                handle = nextHandle++;
            record.localHandle = handle;
            usedHandles.insert(handle);
            mergedRecords.append(std::move(record));
        }
    }

    QHash<QString, QString> mergedContents = m_fileContents;
    for (const SemanticFileSymbolUpdate& update : updates) {
        bool replaced = false;
        for (auto it = mergedContents.begin(); it != mergedContents.end(); ++it) {
            if (normalizedSnapshotFileName(it.key())
                == normalizedSnapshotFileName(update.fileName)) {
                it.value() = update.content;
                replaced = true;
            }
        }
        if (!replaced)
            mergedContents.insert(update.fileName, update.content);
    }

    const QSet<QString> diagnosticTargets =
        normalizedSnapshotFiles(diagnosticFiles);
    QList<SemanticDiagnostic> mergedDiagnostics;
    for (const SemanticDiagnostic& diagnostic : m_diagnostics) {
        if (!diagnosticTargets.contains(
                normalizedSnapshotFileName(diagnostic.fileName))) {
            mergedDiagnostics.append(diagnostic);
        }
    }
    mergedDiagnostics.append(diagnostics);

    const QSet<QString> relationshipTargets =
        normalizedSnapshotFiles(relationshipFiles);
    QList<SemanticRelationship> mergedRelationships;
    for (const SemanticRelationship& relationship : m_relationships) {
        if (!relationshipOwnedByFiles(relationship, relationshipTargets))
            mergedRelationships.append(relationship);
    }
    mergedRelationships.append(relationships);

    return SemanticIndexSnapshot(FromRecordsTag{},
                                 std::move(mergedRecords),
                                 std::move(mergedRelationships),
                                 std::move(mergedDiagnostics),
                                 std::move(mergedContents));
}

SemanticIndexSnapshot
SemanticIndexSnapshot::withRelationshipsReplacingFiles(
    const QStringList& fileNames,
    const QList<SemanticRelationship>& relationships) const
{
    const QSet<QString> targets = normalizedSnapshotFiles(fileNames);
    QList<SemanticRelationship> merged;
    merged.reserve(m_relationships.size() + relationships.size());
    for (const SemanticRelationship& relationship : m_relationships) {
        if (!relationshipOwnedByFiles(relationship, targets))
            merged.append(relationship);
    }
    merged.append(relationships);
    return SemanticIndexSnapshot(FromRecordsTag{},
                                 m_symbolRecords,
                                 std::move(merged),
                                 m_diagnostics,
                                 m_fileContents);
}
