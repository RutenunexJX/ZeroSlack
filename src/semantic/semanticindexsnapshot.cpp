#include "semanticindexsnapshot.h"
#include "semanticanalysisinput.h"
#include "diagnosticpublicationpolicy.h"
#include <QQueue>
#include <algorithm>
#include <utility>

namespace {
QString fileKey(const QString& path) { return SemanticInputCapture::pathKey(path); }
QString recordIdentity(const SemanticSymbolRecord& record)
{
    QString key;
    const QStringList fields{fileKey(record.location.fileName), record.name,
        QString::number(int(record.declarationKind)), record.owner.name,
        record.type.resolvedTypeName};
    for (const QString& field : fields)
        key += QString::number(field.size()) + ':' + field;
    return key;
}
QString relationshipOwner(const SemanticRelationship& relationship)
{
    if (!relationship.evidenceRange.fileName.isEmpty())
        return fileKey(relationship.evidenceRange.fileName);
    if (!relationship.fromStableKey.fileName.isEmpty())
        return fileKey(relationship.fromStableKey.fileName);
    return fileKey(relationship.toStableKey.fileName);
}
QString endpointKey(const SemanticRelationship& relationship)
{
    return QStringLiteral("%1:%2:%3").arg(relationship.fromId)
        .arg(relationship.toId).arg(int(relationship.type));
}

template<class Key>
void changeMembership(SemanticShardDirectory<Key, QSet<QString>>& directory,
                      const Key& key, const QString& file, bool add)
{
    if (!add && !directory.contains(key))
        return;
    directory.mutate(key, [&](QSet<QString>& files) {
        if (add)
            files.insert(file);
        else
            files.remove(file);
        return !files.isEmpty();
    });
}
}

SemanticIndexSnapshot::SemanticIndexSnapshot() = default;

SemanticIndexSnapshot SemanticIndexSnapshot::fromSymbolRecords(
    QList<SemanticSymbolRecord> records, QList<SemanticRelationship> relationships,
    QList<SemanticDiagnostic> diagnostics, QHash<QString, QString> contents)
{
    SemanticIndexSnapshot result;
    QHash<QString, QList<SemanticSymbolRecord>> grouped;
    QStringList files;
    QSet<int> used;
    for (const auto& record : records)
        result.m_nextHandle = qMax(result.m_nextHandle, record.localHandle + 1);
    for (auto& record : records) {
        const QString file = fileKey(record.location.fileName);
        if (!grouped.contains(file))
            files.append(file);
        if (record.localHandle < 0 || used.contains(record.localHandle))
            record.localHandle = result.m_nextHandle++;
        used.insert(record.localHandle);
        grouped[file].append(std::move(record));
    }
    for (const QString& file : files)
        result.replaceSymbolShard(file, grouped.value(file));
    for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
        result.m_fileContents.insert(fileKey(it.key()), it.value());
        // Empty source files still own inputs and diagnostics and must be
        // visible to subsequent scope removal just like files with symbols.
        if (!result.m_symbolsByFile.contains(fileKey(it.key())))
            result.replaceSymbolShard(fileKey(it.key()), {});
    }
    for (const auto& diagnostic : diagnostics)
        result.m_rawDiagnosticsByFile[fileKey(diagnostic.fileName)].append(diagnostic);
    return result.withAdditionalRelationships(relationships);
}

void SemanticIndexSnapshot::replaceSymbolShard(const QString& file,
                                               QList<SemanticSymbolRecord> records)
{
    const auto old = m_symbolsByFile.value(file);
    if (old) {
        for (auto it = old->byName.cbegin(); it != old->byName.cend(); ++it)
            changeMembership(m_filesByName, it.key(), file, false);
        for (auto it = old->byOwner.cbegin(); it != old->byOwner.cend(); ++it)
            changeMembership(m_filesByOwner, it.key(), file, false);
        for (auto it = old->byKind.cbegin(); it != old->byKind.cend(); ++it)
            changeMembership(m_filesByKind, it.key(), file, false);
        for (const auto& record : old->records)
            m_recordsByHandle.remove(record.localHandle);
        for (const QString& target : old->referencedFiles)
            changeMembership(m_symbolFilesByReference, target, file, false);
        m_symbolCount -= old->records.size();
    } else {
        m_fileOrder.append(file);
    }
    auto shard = std::make_shared<SymbolShard>();
    shard->records = std::move(records);
    for (int i = 0; i < shard->records.size(); ++i) {
        const auto& record = shard->records.at(i);
        shard->byName[record.name].append(i);
        shard->byOwner[record.owner.name].append(i);
        shard->byKind[int(record.declarationKind)].append(i);
        const auto stable = symbolStableKeyText(record.stableKey);
        if (!stable.isEmpty() && !shard->byStableKey.contains(stable))
            shard->byStableKey.insert(stable, i);
        m_recordsByHandle.insert(record.localHandle, {file, i});
        if (record.owner.stableKey.isValid())
            shard->referencedFiles.insert(fileKey(record.owner.stableKey.fileName));
        if (record.type.stableKey.isValid())
            shard->referencedFiles.insert(fileKey(record.type.stableKey.fileName));
        m_nextHandle = qMax(m_nextHandle, record.localHandle + 1);
    }
    for (auto it = shard->byName.cbegin(); it != shard->byName.cend(); ++it)
        changeMembership(m_filesByName, it.key(), file, true);
    for (auto it = shard->byOwner.cbegin(); it != shard->byOwner.cend(); ++it)
        changeMembership(m_filesByOwner, it.key(), file, true);
    for (auto it = shard->byKind.cbegin(); it != shard->byKind.cend(); ++it)
        changeMembership(m_filesByKind, it.key(), file, true);
    for (const QString& target : shard->referencedFiles)
        changeMembership(m_symbolFilesByReference, target, file, true);
    m_symbolCount += shard->records.size();
    m_symbolsByFile.insert(file, std::move(shard));
}

void SemanticIndexSnapshot::replaceRelationshipShard(
    const QString& owner, const QList<SemanticRelationship>& records)
{
    const auto old = m_relationshipsByOwner.value(owner);
    if (old) {
        for (const QString& file : old->endpointFiles)
            changeMembership(m_relationshipOwnersByEndpoint, file, owner, false);
        for (const auto& relationship : old->records) {
            if (relationship.fromId == relationship.toId)
                continue;
            const QString key = endpointKey(relationship);
            const int remaining = m_edgeMultiplicity.value(key) - 1;
            if (remaining <= 0) {
                m_edgeMultiplicity.remove(key);
                --m_relationshipEndpointCount;
            } else {
                m_edgeMultiplicity.insert(key, remaining);
            }
        }
        m_relationshipCount -= old->records.size();
    }
    auto shard = std::make_shared<RelationshipShard>();
    QSet<QString> seen;
    for (const auto& relationship : records) {
        auto rebound = rebindRelationship(relationship);
        if (rebound.fromId < 0 || rebound.toId < 0
            || !getSymbolRecordByStableKey(rebound.fromStableKey).stableKey.isValid()
            || !getSymbolRecordByStableKey(rebound.toStableKey).stableKey.isValid())
            continue;
        const QString key = semanticRelationshipStableKeyText(rebound);
        if (key.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        const int index = shard->records.size();
        shard->outgoing[symbolStableKeyText(rebound.fromStableKey)].append(index);
        shard->incoming[symbolStableKeyText(rebound.toStableKey)].append(index);
        shard->endpointFiles.insert(fileKey(rebound.fromStableKey.fileName));
        shard->endpointFiles.insert(fileKey(rebound.toStableKey.fileName));
        if (rebound.fromId != rebound.toId) {
            const QString edge = endpointKey(rebound);
            const int count = m_edgeMultiplicity.value(edge);
            if (count == 0)
                ++m_relationshipEndpointCount;
            m_edgeMultiplicity.insert(edge, count + 1);
        }
        shard->records.append(std::move(rebound));
    }
    for (const QString& file : shard->endpointFiles)
        changeMembership(m_relationshipOwnersByEndpoint, file, owner, true);
    m_relationshipCount += shard->records.size();
    if (shard->records.isEmpty())
        m_relationshipsByOwner.remove(owner);
    else
        m_relationshipsByOwner.insert(owner, std::move(shard));
}

SemanticIndexSnapshot SemanticIndexSnapshot::withAdditionalRelationships(
    const QList<SemanticRelationship>& relationships) const
{
    SemanticIndexSnapshot result = *this;
    result.resetViews();
    QHash<QString, QList<SemanticRelationship>> grouped;
    for (const auto& relationship : relationships) {
        const auto rebound = result.rebindRelationship(relationship);
        const QString owner = relationshipOwner(rebound);
        if (!grouped.contains(owner))
            grouped.insert(owner, result.relationshipsOwnedByFile(owner));
        grouped[owner].append(rebound);
    }
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it)
        result.replaceRelationshipShard(it.key(), it.value());
    return result;
}

SemanticIndexSnapshot SemanticIndexSnapshot::withReplacedDiagnostics(
    const QStringList& files, const QList<SemanticDiagnostic>& diagnostics) const
{
    SemanticIndexSnapshot result = *this;
    if (files.isEmpty())
        result.m_rawDiagnosticsByFile.clear();
    else
        for (const QString& file : files)
            if (!file.isEmpty())
                result.m_rawDiagnosticsByFile.remove(fileKey(file));
    for (const auto& diagnostic : diagnostics)
        result.m_rawDiagnosticsByFile[fileKey(diagnostic.fileName)].append(diagnostic);
    result.rebuildDiagnosticView(diagnosticDisplayLimit());
    return result;
}

SemanticIndexSnapshot SemanticIndexSnapshot::withDiagnosticDisplayLimit(int maxDiagnostics) const
{
    SemanticIndexSnapshot result = *this;
    if (diagnosticDisplayLimit() != qMax(0, maxDiagnostics))
        result.rebuildDiagnosticView(maxDiagnostics);
    return result;
}

void SemanticIndexSnapshot::rebuildDiagnosticView(int maxDiagnostics)
{
    m_diagnosticView.reset();
    // Zero denotes the uncapped compatibility view used before publication.
    if (maxDiagnostics <= 0)
        return;
    const auto selection = DiagnosticPublicationPolicy::select(rawDiagnostics(), maxDiagnostics);
    auto view = std::make_shared<DiagnosticView>();
    view->limit = maxDiagnostics;
    view->producedCount = selection.producedCount;
    view->diagnostics = selection.diagnostics;
    for (const auto& diagnostic : view->diagnostics)
        view->byFile[fileKey(diagnostic.fileName)].append(diagnostic);
    m_diagnosticView = std::move(view);
}

SemanticIndexSnapshot SemanticIndexSnapshot::withReplacedFiles(
    const QList<SemanticFileSymbolUpdate>& updates,
    const QStringList& diagnosticFiles, const QList<SemanticDiagnostic>& diagnostics,
    const QStringList& relationshipFiles, const QList<SemanticRelationship>& relationships) const
{
    SemanticIndexSnapshot result = *this;
    result.resetViews();
    // Rebuild the derived view once, after all input and diagnostic changes.
    result.m_diagnosticView.reset();
    QStringList changedFiles;
    QSet<QString> removedFiles;
    for (const auto& update : updates)
        changedFiles.append(update.fileName);
    const QStringList touchingOwners = relationshipOwnersTouchingFiles(changedFiles);
    QSet<QString> referencingFiles;
    QHash<QString, SymbolStableKey> relocatedKeys;
    for (const auto& update : updates) {
        const QString file = fileKey(update.fileName);
        QHash<QString, QQueue<int>> reusable;
        for (const auto& old : getSymbolRecords(file))
            reusable[recordIdentity(old)].enqueue(old.localHandle);
        auto records = update.removed ? QList<SemanticSymbolRecord>{} : update.symbolRecords;
        for (auto& record : records) {
            const auto handles = reusable.isEmpty() ? reusable.end()
                : reusable.find(recordIdentity(record));
            record.localHandle = handles == reusable.end() || handles->isEmpty()
                ? result.m_nextHandle++ : handles->dequeue();
        }
        result.replaceSymbolShard(file, std::move(records));
        if (update.removed) {
            removedFiles.insert(file);
            result.m_symbolsByFile.remove(file);
            result.m_fileOrder.removeAll(file);
            result.m_fileContents.remove(file);
            result.m_rawDiagnosticsByFile.remove(file);
            result.replaceRelationshipShard(file, {});
        } else {
            removedFiles.remove(file);
            if (!result.m_fileContents.contains(file)
                || result.m_fileContents.value(file) != update.content)
                result.m_rawDiagnosticsByFile.remove(file);
            result.m_fileContents.insert(file, update.content);
        }
        referencingFiles |= m_symbolFilesByReference.value(file);
        for (const auto& old : getSymbolRecords(file)) {
            const auto current = result.getSymbolRecordByLocalHandle(old.localHandle);
            // A missing target removes the reference. A reused handle has the
            // same declaration identity and can carry a relocated source key.
            relocatedKeys.insert(symbolStableKeyText(old.stableKey), current.stableKey);
        }
    }
    for (const QString& file : referencingFiles) {
        auto records = result.getSymbolRecords(file);
        bool changed = false;
        for (auto& record : records) {
            for (SymbolStableKey* key : {&record.owner.stableKey, &record.type.stableKey}) {
                const auto oldKey = symbolStableKeyText(*key);
                const auto moved = relocatedKeys.constFind(oldKey);
                if (moved != relocatedKeys.cend() && symbolStableKeyText(moved.value()) != oldKey) {
                    *key = moved.value();
                    changed = true;
                }
            }
        }
        if (changed)
            result.replaceSymbolShard(file, std::move(records));
    }
    QSet<QString> replacedOwners;
    for (const QString& file : relationshipFiles)
        replacedOwners.insert(fileKey(file));
    // Only relationship owners referencing a changed endpoint need rebinding.
    // Shared shards unrelated to the changed files keep both data and indexes.
    for (const QString& owner : touchingOwners)
        if (!replacedOwners.contains(owner))
            result.replaceRelationshipShard(owner, relationshipsOwnedByFile(owner));
    result = result.withRelationshipsReplacingFiles(relationshipFiles, relationships);
    if (!diagnosticFiles.isEmpty() || !diagnostics.isEmpty())
        result = result.withReplacedDiagnostics(diagnosticFiles, diagnostics);
    // An explicitly removed input cannot be revived by a diagnostic delta.
    for (const QString& file : removedFiles)
        result.m_rawDiagnosticsByFile.remove(file);
    result.rebuildDiagnosticView(diagnosticDisplayLimit());
    return result;
}

SemanticIndexSnapshot SemanticIndexSnapshot::withRelationshipsReplacingFiles(
    const QStringList& files, const QList<SemanticRelationship>& relationships) const
{
    SemanticIndexSnapshot result = *this;
    result.resetViews();
    QHash<QString, QList<SemanticRelationship>> grouped;
    for (const QString& file : files)
        grouped[fileKey(file)];
    for (const auto& relationship : relationships) {
        const auto rebound = result.rebindRelationship(relationship);
        const QString owner = relationshipOwner(rebound);
        if (!grouped.contains(owner))
            grouped.insert(owner, result.relationshipsOwnedByFile(owner));
        grouped[owner].append(rebound);
    }
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it)
        result.replaceRelationshipShard(it.key(), it.value());
    return result;
}

const QList<SemanticSymbolRecord>& SemanticIndexSnapshot::symbolRecordsView() const
{
    std::call_once(m_flatViews->symbolsOnce, [this] {
        m_flatViews->symbols.reserve(m_symbolCount);
        for (const QString& file : m_fileOrder)
            m_flatViews->symbols.append(m_symbolsByFile.value(file)->records);
    });
    return m_flatViews->symbols;
}

const QList<SemanticRelationship>& SemanticIndexSnapshot::relationshipsView() const
{
    std::call_once(m_flatViews->relationshipsOnce, [this] {
        m_flatViews->relationships.reserve(m_relationshipCount);
        QStringList owners = m_relationshipsByOwner.keys();
        owners.sort(Qt::CaseSensitive);
        for (const QString& owner : owners)
            m_flatViews->relationships.append(m_relationshipsByOwner.value(owner)->records);
    });
    return m_flatViews->relationships;
}

int SemanticIndexSnapshot::symbolRecordCount(const QString& file) const
{
    const auto shard = m_symbolsByFile.value(fileKey(file));
    return shard ? shard->records.size() : 0;
}

QList<SemanticRelationship> SemanticIndexSnapshot::relationshipsOwnedByFile(const QString& file) const
{
    const auto shard = m_relationshipsByOwner.value(fileKey(file));
    return shard ? shard->records : QList<SemanticRelationship>{};
}

QStringList SemanticIndexSnapshot::relationshipOwnersTouchingFiles(const QStringList& files) const
{
    QSet<QString> owners;
    for (const QString& file : files)
        owners |= m_relationshipOwnersByEndpoint.value(fileKey(file));
    QStringList result = owners.values();
    result.sort(Qt::CaseSensitive);
    return result;
}

qsizetype SemanticIndexSnapshot::logicalBytes() const
{
    // Explicit retention accounting units, not a claim about allocator RSS.
    qsizetype bytes = qsizetype(m_symbolCount) * 2048 + qsizetype(m_relationshipCount) * 512;
    for (auto it = m_fileContents.cbegin(); it != m_fileContents.cend(); ++it)
        bytes += (it.key().size() + it.value().size()) * qsizetype(sizeof(QChar));
    for (auto it = m_rawDiagnosticsByFile.cbegin(); it != m_rawDiagnosticsByFile.cend(); ++it)
        bytes += it.value().size() * 1024;
    if (m_diagnosticView)
        bytes += m_diagnosticView->diagnostics.size() * 2048;
    return bytes;
}
