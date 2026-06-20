#include "semanticcollectoradapter.h"
#include "symboltaxonomylegacy.h"

#include <QDir>
#include <QFileInfo>

namespace {
QString normalizedStableKeyFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}
}

SymbolStableKey symbolStableKeyForSymbol(const sym_list::SymbolInfo& symbol)
{
    SymbolStableKey key;
    if (symbol.symbolId < 0 && symbol.symbolName.isEmpty())
        return key;

    key.fileName = normalizedStableKeyFileName(symbol.fileName);
    key.symbolName = symbol.symbolName;
    key.declarationKind = SymbolTaxonomy::semanticMetadata(symbol).declarationKind;
    key.ownerScope = symbol.moduleScope;
    return key;
}

SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol)
{
    return semanticSymbolRecordForSymbol(symbol, {});
}

SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes)
{
    SemanticSymbolRecord record;
    if (symbol.symbolId < 0 && symbol.symbolName.isEmpty())
        return record;

    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol, packageScopes);

    record.stableKey = symbolStableKeyForSymbol(symbol);
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
    record.rawCollectorKind = metadata.rawCollectorKind;
    record.owner.kind = metadata.ownerScope;
    record.owner.name = symbol.moduleScope;
    record.owner.interfaceLike = metadata.interfaceLikeOwner;
    record.type.rawTypeText = symbol.dataType;
    record.type.resolvedTypeName = SymbolTaxonomy::interfaceTypeName(symbol);
    record.type.modportName = SymbolTaxonomy::interfaceModportName(symbol);
    if (!record.type.resolvedTypeName.isEmpty())
        record.type.resolvedTypeKind =
            SymbolTaxonomy::DeclarationKind::Interface;
    return record;
}

QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols)
{
    return semanticSymbolRecordsForSymbols(symbols, {});
}

QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols,
    const QSet<QString>& packageScopes)
{
    QList<SemanticSymbolRecord> records;
    records.reserve(symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const SemanticSymbolRecord record =
            semanticSymbolRecordForSymbol(symbol, packageScopes);
        if (record.isValid())
            records.append(record);
    }
    return records;
}

QList<SemanticSymbolRecord> semanticSymbolRecordsForCollectedSymbols(
    const QList<sym_list::SymbolInfo>& symbols)
{
    return semanticSymbolRecordsForSymbols(
        symbols,
        SymbolTaxonomy::packageScopeNames(symbols));
}

namespace {
sym_list::SymbolInfo symbolInfoForSemanticRecord(
    const SemanticSymbolRecord& record)
{
    sym_list::SymbolInfo symbol;
    symbol.symbolId = record.localHandle;
    symbol.symbolName = record.name;
    symbol.symbolType = SymbolTaxonomy::legacySymbolType(record.rawCollectorKind);
    symbol.fileName = record.location.fileName;
    symbol.startLine = record.location.startLine;
    symbol.startColumn = record.location.startColumn;
    symbol.endLine = record.location.endLine;
    symbol.endColumn = record.location.endColumn;
    symbol.position = record.location.position;
    symbol.length = record.location.length;
    symbol.moduleScope = record.owner.name;
    symbol.dataType = record.type.rawTypeText;
    return symbol;
}

}

void updateSymbolDatabaseRecordsForFile(
    sym_list* database,
    const QString& fileName,
    const QList<SemanticSymbolRecord>& records,
    const QString& content)
{
    if (!database)
        return;

    QList<sym_list::SymbolInfo> symbols;
    symbols.reserve(records.size());
    for (const SemanticSymbolRecord& record : records) {
        if (record.isValid())
            symbols.append(symbolInfoForSemanticRecord(record));
    }

    database->setSymbolsForFile(
        fileName,
        symbols,
        content);
}

QList<SemanticSymbolRecord> semanticSymbolRecordsForDatabase(
    sym_list* database,
    const QString& fileName)
{
    if (!database)
        return {};

    const QList<sym_list::SymbolInfo> allSymbols = database->getAllSymbols();
    const QSet<QString> packageScopes =
        SymbolTaxonomy::packageScopeNames(allSymbols);
    if (fileName.isEmpty()) {
        return semanticSymbolRecordsForSymbols(allSymbols, packageScopes);
    }

    QList<SemanticSymbolRecord> records;
    const QString normalizedTarget = normalizedStableKeyFileName(fileName);
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (normalizedStableKeyFileName(symbol.fileName) == normalizedTarget)
            records.append(semanticSymbolRecordForSymbol(symbol, packageScopes));
    }
    return records;
}

QList<SemanticSymbolRecord> semanticSymbolRecordsForDatabaseExcludingFiles(
    sym_list* database,
    const QSet<QString>& normalizedFileNames)
{
    QList<SemanticSymbolRecord> records;
    if (!database)
        return records;

    const QList<sym_list::SymbolInfo> allSymbols = database->getAllSymbols();
    const QSet<QString> packageScopes =
        SymbolTaxonomy::packageScopeNames(allSymbols);
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        const QString normalized = normalizedStableKeyFileName(symbol.fileName);
        if (!normalized.isEmpty() && !normalizedFileNames.contains(normalized))
            records.append(semanticSymbolRecordForSymbol(symbol, packageScopes));
    }
    return records;
}
