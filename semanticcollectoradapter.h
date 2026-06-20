#ifndef SEMANTICCOLLECTORADAPTER_H
#define SEMANTICCOLLECTORADAPTER_H

#include "semanticindex.h"

SymbolStableKey symbolStableKeyForSymbol(const sym_list::SymbolInfo& symbol);
SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol);
SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes);
QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols);
QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols,
    const QSet<QString>& packageScopes);

#endif // SEMANTICCOLLECTORADAPTER_H
