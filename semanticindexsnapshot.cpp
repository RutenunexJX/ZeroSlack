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

sym_list::SymbolInfo snapshotSymbolByLocalHandle(
    const QList<sym_list::SymbolInfo>& symbols,
    int localHandle)
{
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolId == localHandle)
            return symbol;
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

void fillRelationshipStableKeys(
    SemanticRelationship* relationship,
    const QList<sym_list::SymbolInfo>& symbols)
{
    if (!relationship)
        return;

    if (!relationship->fromStableKey.isValid()) {
        relationship->fromStableKey =
            symbolStableKeyForSymbol(
                snapshotSymbolByLocalHandle(symbols, relationship->fromId));
    }
    if (!relationship->toStableKey.isValid()) {
        relationship->toStableKey =
            symbolStableKeyForSymbol(
                snapshotSymbolByLocalHandle(symbols, relationship->toId));
    }
}

int snapshotLocalHandleByStableKey(
    const QList<sym_list::SymbolInfo>& symbols,
    const SymbolStableKey& key)
{
    if (!key.isValid())
        return -1;

    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbolStableKeyForSymbol(symbol) == key)
            return symbol.symbolId;
    }
    return -1;
}

SemanticRelationship rebindRelationshipToSnapshot(
    const SemanticRelationship& relationship,
    const QList<sym_list::SymbolInfo>& symbols)
{
    SemanticRelationship rebound = relationship;
    fillRelationshipStableKeys(&rebound, symbols);

    const int reboundFromHandle =
        snapshotLocalHandleByStableKey(symbols, rebound.fromStableKey);
    if (reboundFromHandle >= 0)
        rebound.fromId = reboundFromHandle;

    const int reboundToHandle =
        snapshotLocalHandleByStableKey(symbols, rebound.toStableKey);
    if (reboundToHandle >= 0)
        rebound.toId = reboundToHandle;

    fillRelationshipStableKeys(&rebound, symbols);
    return rebound;
}

QString snapshotRelationshipDedupeKey(
    const SemanticRelationship& relationship,
    const QList<sym_list::SymbolInfo>& symbols)
{
    SemanticRelationship keyedRelationship =
        rebindRelationshipToSnapshot(relationship, symbols);

    const QString stableKey =
        semanticRelationshipStableKeyText(keyedRelationship);
    if (!stableKey.isEmpty())
        return stableKey;

    return QStringLiteral("local:%1:%2:%3")
        .arg(keyedRelationship.fromId)
        .arg(keyedRelationship.toId)
        .arg(static_cast<int>(keyedRelationship.type));
}

QList<sym_list::SymbolInfo> symbolsWithSemanticMetadata(
    QList<sym_list::SymbolInfo> symbols)
{
    const QSet<QString> packageScopes =
        SymbolTaxonomy::packageScopeNames(symbols);
    for (sym_list::SymbolInfo& symbol : symbols) {
        if (!symbol.hasSemanticMetadata)
            SymbolTaxonomy::attachSemanticMetadata(&symbol, packageScopes);
    }
    return symbols;
}

}

SemanticIndexSnapshot::SemanticIndexSnapshot(
    QList<sym_list::SymbolInfo> symbols,
    QList<SemanticRelationship> relationships,
    QList<SemanticDiagnostic> diagnostics,
    QHash<QString, QString> fileContents)
    : m_symbols(symbolsWithSemanticMetadata(std::move(symbols))),
      m_relationships(std::move(relationships)),
      m_diagnostics(std::move(diagnostics)),
      m_fileContents(std::move(fileContents))
{
    for (SemanticRelationship& relationship : m_relationships)
        relationship = rebindRelationshipToSnapshot(relationship, m_symbols);
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
            if (symbol.symbolId < 0)
                continue;
            for (SymbolRelationshipEngine::RelationType type : snapshotRelationshipTypes()) {
                const QList<int> related = engine->getRelatedSymbols(symbol.symbolId, type, true);
                for (int relatedHandle : related) {
                    SemanticRelationship relationship;
                    relationship.fromId = symbol.symbolId;
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
        const QString key = snapshotRelationshipDedupeKey(relationship, m_symbols);
        if (!key.isEmpty())
            seen.insert(key);
    }

    for (const SemanticRelationship& relationship : relationships) {
        const SemanticRelationship rebound = rebindRelationship(relationship);
        if (rebound.fromId < 0 || rebound.toId < 0)
            continue;
        const QString key = snapshotRelationshipDedupeKey(rebound, m_symbols);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        merged.append(rebound);
    }

    return SemanticIndexSnapshot(m_symbols, merged, m_diagnostics, m_fileContents);
}

SemanticIndexSnapshot SemanticIndexSnapshot::withReplacedDiagnostics(
    const QStringList& fileNames,
    const QList<SemanticDiagnostic>& diagnostics) const
{
    if (fileNames.isEmpty())
        return SemanticIndexSnapshot(m_symbols, m_relationships, diagnostics, m_fileContents);

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

    return SemanticIndexSnapshot(m_symbols, m_relationships, merged, m_fileContents);
}
