#ifndef SEMANTICINDEXSNAPSHOT_H
#define SEMANTICINDEXSNAPSHOT_H

#include <zeroslack/semantic/semanticindex.h>
#include <zeroslack/semantic/semanticsharddirectory.h>
#include <memory>
#include <mutex>

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
    QList<SemanticSymbolRecord> getSymbolRecordsByName(
        const QString& name) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByName(
        const QString& name, const QString& fileName) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByOwner(
        const QString& ownerName) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByDeclarationKind(
        SymbolTaxonomy::DeclarationKind declarationKind) const;
    SemanticAnalysisBandReport analysisBandReport(
        const QString& fileName = QString()) const;
    SemanticSymbolRecord getSymbolRecordByStableKey(
        const SymbolStableKey& key) const;
    QList<SemanticSymbolRecord> findDefinitionRecords(
        const QString& name,
        const SemanticQueryContext& context = {}) const;
    QString getCachedFileContent(const QString& fileName) const;
    struct CachedFileSource {
        bool exists = false;
        QString fileKey;
        QString text;
    };
    // Exact/lexical paths use the existing content key. On a miss, resolve
    // only the requested identity and consult this publication's alias index.
    // An empty cached source is present; ambiguous physical aliases are not
    // selected without an exact key. Returned QString values own their data.
    CachedFileSource cachedFileSource(const QString& fileName) const;
    QStringList getScopeSymbolNames(const QString& fileName, int cursorLine) const;
    SemanticRelationship rebindRelationship(
        const SemanticRelationship& relationship) const;
    SemanticIndexSnapshot withAdditionalRelationships(
        const QList<SemanticRelationship>& relationships) const;
    SemanticIndexSnapshot withReplacedDiagnostics(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics) const;
    // Raw diagnostics belong to this immutable publication. A display limit
    // derives a view and never discards values needed by later deltas/remaps.
    // Zero exposes the uncapped compatibility view.
    SemanticIndexSnapshot withDiagnosticDisplayLimit(int maxDiagnostics) const;
    QList<SemanticDiagnostic> rawDiagnostics(const QString& fileName = QString()) const;
    int diagnosticDisplayLimit() const;
    int rawDiagnosticCount() const;
    int suppressedDiagnosticCount() const;
    SemanticIndexSnapshot withReplacedFiles(
        const QList<SemanticFileSymbolUpdate>& updates,
        const QStringList& diagnosticFiles,
        const QList<SemanticDiagnostic>& diagnostics,
        const QStringList& relationshipFiles = {},
        const QList<SemanticRelationship>& relationships = {}) const;
    SemanticIndexSnapshot withRelationshipsReplacingFiles(
        const QStringList& fileNames,
        const QList<SemanticRelationship>& relationships) const;

    QList<SemanticRelationship> relationshipsForStableKey(
        const SymbolStableKey& key,
        bool outgoing = true) const;
    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;
    QList<SemanticRelationship> relationships() const;
    QList<SemanticDiagnostic> diagnostics() const;
    QHash<QString, QString> fileContents() const;
    const QHash<QString, QString>& fileContentsView() const;
    // Whole-workspace views are lazy compatibility adapters. Incremental
    // workers and queries use immutable per-file shards instead.
    const QList<SemanticSymbolRecord>& symbolRecordsView() const;
    const QList<SemanticRelationship>& relationshipsView() const;
    int symbolRecordCount() const { return m_symbolCount; }
    int nextAvailableLocalHandle() const { return m_nextHandle; }
    int relationshipCount() const { return m_relationshipCount; }
    int relationshipEndpointCount() const { return m_relationshipEndpointCount; }
    QStringList symbolFiles() const { return m_fileOrder; }
    QStringList relationshipOwnerFiles() const { return m_relationshipsByOwner.keys(); }
    int symbolRecordCount(const QString& fileName) const;
    QList<SemanticRelationship> relationshipsOwnedByFile(const QString& fileName) const;
    QStringList relationshipOwnersTouchingFiles(const QStringList& files) const;
    qsizetype logicalBytes() const;

private:
    // Numeric handles belong only to this publication. The legacy engine is
    // a read projection of this owner; public clients use stable keys.
    friend class SymbolRelationshipEngine;
    SemanticSymbolRecord recordForProjectionHandle(int handle) const;
    struct SymbolShard {
        QList<SemanticSymbolRecord> records;
        QHash<QString, QList<int>> byName, byOwner;
        QHash<int, QList<int>> byKind;
        QHash<QString, int> byStableKey;
        QSet<QString> referencedFiles;
    };
    struct RelationshipShard {
        QList<SemanticRelationship> records;
        QHash<QString, QList<int>> outgoing, incoming;
        QSet<QString> endpointFiles;
    };
    struct RecordAddress {
        QString file;
        int index = -1;
    };
    struct FlatViews {
        std::once_flag symbolsOnce, relationshipsOnce;
        QList<SemanticSymbolRecord> symbols;
        QList<SemanticRelationship> relationships;
    };
    struct DiagnosticView {
        int limit = 0;
        int producedCount = 0;
        QList<SemanticDiagnostic> diagnostics;
        QHash<QString, QList<SemanticDiagnostic>> byFile;
    };
    QHash<QString, std::shared_ptr<const SymbolShard>> m_symbolsByFile;
    QHash<QString, std::shared_ptr<const RelationshipShard>> m_relationshipsByOwner;
    SemanticShardDirectory<QString, QSet<QString>> m_filesByName, m_filesByOwner;
    SemanticShardDirectory<int, QSet<QString>> m_filesByKind;
    SemanticShardDirectory<int, RecordAddress> m_recordsByHandle;
    SemanticShardDirectory<QString, QSet<QString>> m_symbolFilesByReference;
    SemanticShardDirectory<QString, QSet<QString>> m_relationshipOwnersByEndpoint;
    SemanticShardDirectory<QString, int> m_edgeMultiplicity;
    QHash<QString, QList<SemanticDiagnostic>> m_rawDiagnosticsByFile;
    std::shared_ptr<const DiagnosticView> m_diagnosticView;
    QHash<QString, QString> m_fileContents;
    // Only non-canonical keys need alias entries. Bind identities when their
    // source is published/replaced, never by scanning inputs during a query.
    QHash<QString, QString> m_contentIdentityByAlias;
    QHash<QString, QSet<QString>> m_contentAliasesByIdentity;
    QStringList m_fileOrder;
    int m_symbolCount = 0;
    int m_relationshipCount = 0;
    int m_relationshipEndpointCount = 0;
    int m_nextHandle = 1;
    std::shared_ptr<FlatViews> m_flatViews = std::make_shared<FlatViews>();

    void replaceSymbolShard(const QString& file, QList<SemanticSymbolRecord> records);
    void replaceFileContent(const QString& file, const QString& text, const QString& identity);
    void removeFileContent(const QString& file);
    void replaceRelationshipShard(const QString& owner, const QList<SemanticRelationship>& records);
    void rebuildDiagnosticView(int maxDiagnostics);
    void resetViews() { m_flatViews = std::make_shared<FlatViews>(); }
    QList<SemanticSymbolRecord> sortedDefinitionRecords(
        const QList<SemanticSymbolRecord>& records,
        const SemanticQueryContext& context) const;
};

#endif // SEMANTICINDEXSNAPSHOT_H
