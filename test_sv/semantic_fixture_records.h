#ifndef TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
#define TEST_SV_SEMANTIC_FIXTURE_RECORDS_H

#include "semanticindex.h"
#include "symboltaxonomylegacy.h"

#include <QList>
#include <QSet>

static SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes = {})
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol, packageScopes);

    SemanticSymbolRecord record;
    record.localHandle = symbol.symbolId;
    record.name = symbol.symbolName;
    record.location.fileName = symbol.fileName;
    record.location.startLine = symbol.startLine;
    record.location.startColumn = symbol.startColumn;
    record.location.endLine = symbol.endLine;
    record.location.endColumn = symbol.endColumn;
    record.location.position = symbol.position;
    record.location.length = symbol.length;
    record.declarationKind = metadata.declarationKind;
    record.usageRole = metadata.usageRole;
    record.visibility = metadata.visibility;
    record.sourceRole = metadata.sourceRole;
    record.collectorKind = metadata.collectorKind;
    record.owner.kind = metadata.ownerScope;
    record.owner.name = symbol.moduleScope;
    record.owner.interfaceLike = metadata.interfaceLikeOwner;
    record.type.rawTypeText = symbol.dataType;

    record.stableKey.fileName = record.location.fileName;
    record.stableKey.symbolName = record.name;
    record.stableKey.declarationKind = record.declarationKind;
    record.stableKey.ownerScope = record.owner.name;
    return record;
}

static QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols,
    const QSet<QString>& packageScopes = {})
{
    QList<SemanticSymbolRecord> records;
    records.reserve(symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols)
        records.append(semanticSymbolRecordForSymbol(symbol, packageScopes));
    return records;
}

#endif // TEST_SV_SEMANTIC_FIXTURE_RECORDS_H
