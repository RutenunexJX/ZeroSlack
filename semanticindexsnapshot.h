#ifndef SEMANTICINDEXSNAPSHOT_H
#define SEMANTICINDEXSNAPSHOT_H

#include "semanticindex.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class SemanticIndexSnapshot
{
public:
    SemanticIndexSnapshot(QList<sym_list::SymbolInfo> symbols = {},
                          QList<SemanticRelationship> relationships = {},
                          QList<SemanticDiagnostic> diagnostics = {},
                          QHash<QString, QString> fileContents = {});

    static SemanticIndexSnapshot fromSymbolDatabase(
        sym_list* symbolDatabase,
        QList<SemanticDiagnostic> diagnostics = {});

    QList<sym_list::SymbolInfo> getSymbols(const QString& fileName = QString()) const;
    QList<SemanticSymbolRecord> getSymbolRecords(
        const QString& fileName = QString()) const;
    QList<sym_list::SymbolInfo> getSymbolsByType(sym_list::sym_type_e type) const;
    SemanticSymbolRecord getSymbolRecordByStableKey(
        const SymbolStableKey& key) const;
    QList<SemanticSymbolRecord> findDefinitionRecords(
        const QString& name,
        const SemanticQueryContext& context = {}) const;
    QString getCachedFileContent(const QString& fileName) const;
    QStringList getScopeSymbolNames(const QString& fileName, int cursorLine) const;
    SemanticRelationship rebindRelationship(
        const SemanticRelationship& relationship) const;
    SemanticIndexSnapshot withAdditionalRelationships(
        const QList<SemanticRelationship>& relationships) const;
    SemanticIndexSnapshot withReplacedDiagnostics(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics) const;

    QList<SemanticRelationship> getRelationships(
        const SymbolStableKey& key,
        bool outgoing = true) const;
    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;
    QList<SemanticRelationship> relationships() const;
    QList<SemanticDiagnostic> diagnostics() const;
    QHash<QString, QString> fileContents() const;

private:
    QList<sym_list::SymbolInfo> m_symbols;
    QList<SemanticRelationship> m_relationships;
    QList<SemanticDiagnostic> m_diagnostics;
    QHash<QString, QString> m_fileContents;

    QList<SemanticSymbolRecord> sortedDefinitionRecords(
        const QList<SemanticSymbolRecord>& records,
        const SemanticQueryContext& context) const;
};

#endif // SEMANTICINDEXSNAPSHOT_H
