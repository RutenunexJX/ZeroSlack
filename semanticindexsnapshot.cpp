#include "semanticindexsnapshot.h"

#include <QDir>
#include <QSet>
#include <utility>

namespace {
QString normalizedSnapshotFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QList<SymbolRelationshipEngine::RelationType> snapshotRelationshipTypes()
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

int snapshotLocalHandleForRecord(const SemanticSymbolRecord& record)
{
    return record.localHandle;
}

SemanticSymbolRecord snapshotRecordByLocalHandle(
    const QList<SemanticSymbolRecord>& records,
    int localHandle)
{
    for (const SemanticSymbolRecord& record : records) {
        if (snapshotLocalHandleForRecord(record) == localHandle)
            return record;
    }
    return {};
}

void fillRelationshipStableKeys(
    SemanticRelationship* relationship,
    const QList<SemanticSymbolRecord>& records)
{
    if (!relationship)
        return;

    if (!relationship->fromStableKey.isValid()) {
        relationship->fromStableKey =
            snapshotRecordByLocalHandle(
                records, relationship->fromId).stableKey;
    }
    if (!relationship->toStableKey.isValid()) {
        relationship->toStableKey =
            snapshotRecordByLocalHandle(
                records, relationship->toId).stableKey;
    }
}

int snapshotLocalHandleByStableKey(
    const QList<SemanticSymbolRecord>& records,
    const SymbolStableKey& key)
{
    if (!key.isValid())
        return -1;

    for (const SemanticSymbolRecord& record : records) {
        if (record.stableKey == key)
            return snapshotLocalHandleForRecord(record);
    }
    return -1;
}

SemanticRelationship rebindRelationshipToSnapshot(
    const SemanticRelationship& relationship,
    const QList<SemanticSymbolRecord>& records)
{
    SemanticRelationship rebound = relationship;
    fillRelationshipStableKeys(&rebound, records);

    const int reboundFromHandle =
        snapshotLocalHandleByStableKey(records, rebound.fromStableKey);
    if (reboundFromHandle >= 0)
        rebound.fromId = reboundFromHandle;

    const int reboundToHandle =
        snapshotLocalHandleByStableKey(records, rebound.toStableKey);
    if (reboundToHandle >= 0)
        rebound.toId = reboundToHandle;

    fillRelationshipStableKeys(&rebound, records);
    return rebound;
}

QString snapshotRelationshipDedupeKey(
    const SemanticRelationship& relationship,
    const QList<SemanticSymbolRecord>& records)
{
    SemanticRelationship keyedRelationship =
        rebindRelationshipToSnapshot(relationship, records);

    const QString stableKey =
        semanticRelationshipStableKeyText(keyedRelationship);
    if (!stableKey.isEmpty())
        return stableKey;

    return QStringLiteral("local:%1:%2:%3")
        .arg(keyedRelationship.fromId)
        .arg(keyedRelationship.toId)
        .arg(static_cast<int>(keyedRelationship.type));
}

QList<SemanticSymbolRecord> snapshotRecordsFromSymbols(
    const QList<sym_list::SymbolInfo>& symbols)
{
    const QSet<QString> packageScopes =
        SymbolTaxonomy::packageScopeNames(symbols);
    return semanticSymbolRecordsForSymbols(symbols, packageScopes);
}

}

SemanticIndexSnapshot::SemanticIndexSnapshot(
    QList<sym_list::SymbolInfo> symbols,
    QList<SemanticRelationship> relationships,
    QList<SemanticDiagnostic> diagnostics,
    QHash<QString, QString> fileContents)
    : SemanticIndexSnapshot(FromRecordsTag{},
                            snapshotRecordsFromSymbols(symbols),
                            std::move(relationships),
                            std::move(diagnostics),
                            std::move(fileContents))
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
    for (SemanticRelationship& relationship : m_relationships)
        relationship = rebindRelationshipToSnapshot(relationship, m_symbolRecords);
}

SemanticIndexSnapshot SemanticIndexSnapshot::fromSymbolDatabase(
    sym_list* symbolDatabase,
    QList<SemanticDiagnostic> diagnostics)
{
    if (!symbolDatabase)
        return SemanticIndexSnapshot({}, {}, std::move(diagnostics));

    const QList<sym_list::SymbolInfo> symbols = symbolDatabase->getAllSymbols();
    QList<SemanticRelationship> relationships;
    SymbolRelationshipEngine* engine = symbolDatabase->getRelationshipEngine();
    if (engine) {
        QSet<QString> seen;
        for (const sym_list::SymbolInfo& symbol : symbols) {
            const int symbolHandle = symbol.symbolId;
            if (symbolHandle < 0)
                continue;
            for (SymbolRelationshipEngine::RelationType type : snapshotRelationshipTypes()) {
                const QList<int> related = engine->getRelatedSymbols(symbolHandle,
                                                                      type,
                                                                      true);
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
                        relationship.provenance = RelationshipProvenance::Inferred;
                        relationship.confidence = metadata.confidence;
                        relationship.evidenceText = metadata.context;
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
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.fileName.isEmpty() || seenFiles.contains(symbol.fileName))
            continue;
        seenFiles.insert(symbol.fileName);
        fileContents.insert(symbol.fileName, symbolDatabase->getCachedFileContent(symbol.fileName));
    }

    return SemanticIndexSnapshot(symbols, relationships, std::move(diagnostics), fileContents);
}

SemanticIndexSnapshot SemanticIndexSnapshot::withAdditionalRelationships(
    const QList<SemanticRelationship>& relationships) const
{
    QList<SemanticRelationship> merged = m_relationships;
    QSet<QString> seen;
    for (const SemanticRelationship& relationship : std::as_const(merged)) {
        const QString key =
            snapshotRelationshipDedupeKey(relationship, m_symbolRecords);
        if (!key.isEmpty())
            seen.insert(key);
    }

    for (const SemanticRelationship& relationship : relationships) {
        const SemanticRelationship rebound = rebindRelationship(relationship);
        if (rebound.fromId < 0 || rebound.toId < 0)
            continue;
        const QString key =
            snapshotRelationshipDedupeKey(rebound, m_symbolRecords);
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
