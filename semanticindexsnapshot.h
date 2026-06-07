#ifndef SEMANTICINDEXSNAPSHOT_H
#define SEMANTICINDEXSNAPSHOT_H

#include "semanticindex.h"

#include <QList>
#include <QString>

class SemanticIndexSnapshot
{
public:
    SemanticIndexSnapshot(QList<sym_list::SymbolInfo> symbols = {},
                          QList<SemanticRelationship> relationships = {},
                          QList<SemanticDiagnostic> diagnostics = {});

    static SemanticIndexSnapshot fromSymbolDatabase(sym_list* symbolDatabase);

    QList<sym_list::SymbolInfo> getSymbols(const QString& fileName = QString()) const;
    QList<sym_list::SymbolInfo> getSymbolsByType(sym_list::sym_type_e type) const;
    sym_list::SymbolInfo getSymbolById(int symbolId) const;
    QList<sym_list::SymbolInfo> findDefinitions(
        const QString& name,
        const SemanticQueryContext& context = {}) const;
    int findSymbolId(const QString& name,
                     const SemanticQueryContext& context = {}) const;

    QList<SemanticRelationship> getRelationships(int symbolId, bool outgoing = true) const;
    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;

private:
    QList<sym_list::SymbolInfo> m_symbols;
    QList<SemanticRelationship> m_relationships;
    QList<SemanticDiagnostic> m_diagnostics;

    QList<sym_list::SymbolInfo> sortedDefinitions(
        const QList<sym_list::SymbolInfo>& symbols,
        const SemanticQueryContext& context) const;
};

#endif // SEMANTICINDEXSNAPSHOT_H
