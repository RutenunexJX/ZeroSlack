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
    SemanticIndexSnapshot();

    static SemanticIndexSnapshot fromSymbolRecords(
        QList<SemanticSymbolRecord> symbolRecords,
        QList<SemanticRelationship> relationships = {},
        QList<SemanticDiagnostic> diagnostics = {},
        QHash<QString, QString> fileContents = {});

    QList<SemanticSymbolRecord> getSymbolRecords(
        const QString& fileName = QString()) const;
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

    QList<SemanticRelationship> relationshipsForStableKey(
        const SymbolStableKey& key,
        bool outgoing = true) const;
    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;
    QList<SemanticRelationship> relationships() const;
    QList<SemanticDiagnostic> diagnostics() const;
    QHash<QString, QString> fileContents() const;

private:
    struct FromRecordsTag {};

    SemanticIndexSnapshot(FromRecordsTag,
                          QList<SemanticSymbolRecord> symbolRecords,
                          QList<SemanticRelationship> relationships,
                          QList<SemanticDiagnostic> diagnostics,
                          QHash<QString, QString> fileContents);

    QList<SemanticSymbolRecord> m_symbolRecords;
    QList<SemanticRelationship> m_relationships;
    QList<SemanticDiagnostic> m_diagnostics;
    QHash<QString, QString> m_fileContents;

    QList<SemanticSymbolRecord> sortedDefinitionRecords(
        const QList<SemanticSymbolRecord>& records,
        const SemanticQueryContext& context) const;
};

#endif // SEMANTICINDEXSNAPSHOT_H
