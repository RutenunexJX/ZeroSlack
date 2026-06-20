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
QList<sym_list::SymbolInfo> symbolInfosForSemanticRecords(
    const QList<SemanticSymbolRecord>& records);
void updateSymbolDatabaseRecordsForFile(
    sym_list* database,
    const QString& fileName,
    const QList<SemanticSymbolRecord>& records,
    const QString& content);
QList<SemanticSymbolRecord> semanticSymbolRecordsForDatabase(
    sym_list* database,
    const QString& fileName = QString());
QList<SemanticSymbolRecord> semanticSymbolRecordsForDatabaseByName(
    sym_list* database,
    const QString& symbolName);
QList<SemanticSymbolRecord> semanticSymbolRecordsForDatabaseExcludingFiles(
    sym_list* database,
    const QSet<QString>& normalizedFileNames);

#endif // SEMANTICCOLLECTORADAPTER_H
