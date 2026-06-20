#include "semanticcollectoradapter.h"

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
