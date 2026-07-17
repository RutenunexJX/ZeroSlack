#ifndef SEMANTICINDEX_H
#define SEMANTICINDEX_H

#include "semanticsourcerange.h"
#include "symbolrelationshipengine.h"
#include "symboltaxonomy.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>

class SemanticIndexSnapshot;
class SlangManager;
class SmartRelationshipBuilder;
class QObject;
enum class CompletionCommandKind;

struct SemanticSnapshotToken {
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    std::uint64_t revision = 0;

    bool isValid() const { return snapshot != nullptr; }
};

struct SemanticQueryContext {
    QString fileName;
    QString moduleName;
    QString prefix;
    int cursorLine = -1;      // 1-based
    int cursorPosition = -1;  // QTextDocument position, when available
    QSet<QString> importedPackageNames;
};

struct SymbolStableKey {
    QString fileName;
    QString symbolName;
    SymbolTaxonomy::DeclarationKind declarationKind =
        SymbolTaxonomy::DeclarationKind::Unknown;
    QString ownerScope;

    bool isValid() const;
    QString toString() const;
    bool operator==(const SymbolStableKey& other) const;
};

struct SemanticSymbolLocation {
    QString fileName;
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
    int position = 0;
    int length = 0;

    bool isValid() const;
};

struct SemanticSymbolOwner {
    SymbolTaxonomy::SymbolOwnerScope kind =
        SymbolTaxonomy::SymbolOwnerScope::Unknown;
    QString name;
    SymbolStableKey stableKey;
    bool interfaceLike = false;

    bool isValid() const;
};

struct SemanticSymbolTypeReference {
    QString rawTypeText;
    QString resolvedTypeName;
    SymbolTaxonomy::DeclarationKind resolvedTypeKind =
        SymbolTaxonomy::DeclarationKind::Unknown;
    QString modportName;
    SymbolStableKey stableKey;

    bool isValid() const;
};

struct SemanticAnalysisBandMetadata {
    QString label;
    QString displayName;
    bool priority = false;
    int publicationCheckpoint = 0;

    bool isValid() const;
};

struct SemanticAnalysisBandReportItem {
    QString label;
    QString displayName;
    bool priority = false;
    int publicationCheckpoint = 0;
    int symbolCount = 0;
    int fileCount = 0;
    QStringList files;

    bool isValid() const;
};

struct SemanticAnalysisBandReport {
    QList<SemanticAnalysisBandReportItem> bands;
    int totalSymbolCount = 0;
    int totalFileCount = 0;

    QString summaryText() const;
};

struct SemanticSymbolRecord {
    SymbolStableKey stableKey;
    int localHandle = -1;
    QString name;
    SemanticSymbolLocation location;
    SymbolTaxonomy::DeclarationKind declarationKind =
        SymbolTaxonomy::DeclarationKind::Unknown;
    SymbolTaxonomy::SymbolUsageRole usageRole =
        SymbolTaxonomy::SymbolUsageRole::Unknown;
    SymbolTaxonomy::SymbolVisibility visibility =
        SymbolTaxonomy::SymbolVisibility::Unknown;
    SymbolTaxonomy::SourceRole sourceRole =
        SymbolTaxonomy::SourceRole::Unknown;
    SymbolTaxonomy::CollectorKind collectorKind =
        SymbolTaxonomy::CollectorKind::User;
    SemanticSymbolOwner owner;
    SemanticSymbolTypeReference type;
    SemanticAnalysisBandMetadata analysisBand;

    bool isValid() const;
};

enum class RelationshipProvenance {
    Unknown,
    SlangExtracted,
    Inferred,
    LexicalFallback,
    OpenDocument,
    Workspace,
    FeatureGenerated
};

struct SemanticRelationship {
    int fromId = -1;
    int toId = -1;
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
    QString fromAccessPath;
    QString toAccessPath;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    QString evidenceText;
    SemanticSourceRange evidenceRange;
};

struct SemanticRelationshipResult {
    SemanticRelationship relationship;
    SemanticSymbolRecord fromSymbolRecord;
    SemanticSymbolRecord toSymbolRecord;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
    QString fromAccessPath;
    QString toAccessPath;
    RelationshipProvenance provenance = RelationshipProvenance::Unknown;
    int confidence = 0;
    QString evidenceText;
    SemanticSourceRange evidenceRange;
};

struct SemanticDiagnostic {
    enum Severity {
        Info,
        Warning,
        Error
    };

    enum Owner {
        UnknownOwner,
        SlangCompiler,
        SemanticIndexOwner
    };

    QString fileName;
    int line = 0;
    int column = 0;
    QString message;
    Severity severity = Info;
    Owner owner = UnknownOwner;
};

struct SemanticFileSymbolUpdate {
    QString fileName;
    QList<SemanticSymbolRecord> symbolRecords;
    QString content;
};

struct SemanticSymbolSearchQuery {
    QString text;
    QString fileName;
    QList<SymbolTaxonomy::DeclarationKind> declarationKinds;
    SymbolTaxonomy::SymbolSearchIntent intent = SymbolTaxonomy::SymbolSearchIntent::Any;
    bool caseSensitive = false;
    bool exactMatch = false;
    int maxResults = -1;
};

struct SemanticSymbolSearchResult {
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    int score = 0;
};

struct SemanticDefinitionQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    QString structTypeNameForMember;
    int cursorLine = -1;
    int cursorPosition = -1;
    QSet<QString> importedPackageNames;
};

enum class SemanticDefinitionMissReason {
    None,
    EmptySymbolName,
    NoCandidateSymbols,
    NoMatchingName,
    StructMemberTypeMismatch,
    NotVisibleInContext,
    AmbiguousImportedPackageSymbol
};

struct SemanticDefinitionResult {
    bool found = false;
    bool localFile = false;
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    int inspectedCandidateCount = 0;
    int matchingNameCandidateCount = 0;
    int typeCompatibleCandidateCount = 0;
    int visibleCandidateCount = 0;
    SemanticDefinitionMissReason missReason =
        SemanticDefinitionMissReason::NoCandidateSymbols;
};

SymbolTaxonomy::SemanticMetadata semanticMetadataForSymbolRecord(
    const SemanticSymbolRecord& record);
QString semanticAnalysisBandDisplayName(
    const SemanticAnalysisBandMetadata& metadata);
SemanticAnalysisBandReport semanticAnalysisBandReportForRecords(
    const QList<SemanticSymbolRecord>& records);
int semanticAnalysisBandSortPriority(
    const SemanticAnalysisBandMetadata& metadata);
int semanticSymbolAnalysisBandSortPriority(
    const SemanticSymbolRecord& record);
QString symbolStableKeyText(const SymbolStableKey& key);
QString semanticRelationshipStableKeyText(
    const SemanticRelationship& relationship);

// Thin facade over the semantic-native store and published snapshots.
class SemanticIndex
{
public:
    static SemanticIndex* getInstance();

    SemanticIndex();
    ~SemanticIndex();

    void setSnapshot(std::shared_ptr<const SemanticIndexSnapshot> snapshot);
    void clearSnapshot();
    std::shared_ptr<const SemanticIndexSnapshot> snapshot() const;
    std::uint64_t snapshotRevision() const;
    SemanticSnapshotToken snapshotToken() const;
    void updateSymbolRecordsForFile(
        const QString& fileName,
        const QList<SemanticSymbolRecord>& records,
        const QString& content);
    void updateSymbolRecordsForFiles(
        const QList<SemanticFileSymbolUpdate>& updates,
        bool buildRelationships = true);
    void publishSnapshotReplacingDiagnostics(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics);
    std::shared_ptr<const SemanticIndexSnapshot> captureSnapshotPreservingDiagnostics() const;
    std::shared_ptr<const SemanticIndexSnapshot> captureSnapshotReplacingDiagnostics(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics) const;
    SemanticSnapshotToken beginRelationshipAnalysisSnapshot();
    std::shared_ptr<const SemanticIndexSnapshot> snapshotWithAdditionalRelationships(
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
        const QList<SemanticRelationship>& relationships) const;
    bool publishSnapshotIfCurrent(
        const SemanticSnapshotToken& expectedCurrentSnapshot,
        std::shared_ptr<const SemanticIndexSnapshot> nextSnapshot);
    void setWorkspaceFileAnalysisBands(
        const QHash<QString, SemanticAnalysisBandMetadata>& bands);
    void clearWorkspaceFileAnalysisBands();
    SemanticAnalysisBandMetadata analysisBandForFile(
        const QString& fileName) const;
    SemanticAnalysisBandReport analysisBandReport(
        const QString& fileName = QString()) const;

    QList<SemanticSymbolRecord> getSymbolRecords(
        const QString& fileName = QString()) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByName(
        const QString& name) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByOwner(
        const QString& ownerName) const;
    QList<SemanticSymbolRecord> getSymbolRecordsByDeclarationKind(
        SymbolTaxonomy::DeclarationKind declarationKind) const;
    QList<SemanticSymbolSearchResult> searchSymbols(
        const SemanticSymbolSearchQuery& query) const;
    QList<SemanticSymbolRecord> getModuleCompletionSymbolRecords(
        const QString& moduleName,
        const QString& prefix = QString()) const;
    QList<SemanticSymbolRecord> getModuleCompletionSymbolRecords(
        const SemanticQueryContext& context) const;
    QList<SemanticSymbolRecord> getGlobalCompletionSymbolRecords(
        const QString& prefix = QString()) const;
    QList<SemanticSymbolRecord> getCommandCompletionSymbolRecords(
        const QString& moduleName,
        CompletionCommandKind commandKind,
        const QString& prefix = QString()) const;
    QList<SemanticSymbolRecord> getCommandCompletionSymbolRecords(
        const SemanticQueryContext& context,
        CompletionCommandKind commandKind,
        const QString& prefix = QString()) const;
    QStringList getCompletionSymbolNames() const;
    QStringList getEnumValueCompletionNames(
        const QString& prefix = QString(),
        const QString& enumTypeName = QString()) const;
    QString enumTypeForVariable(
        const QString& variableName,
        const QString& moduleName = QString()) const;
    QStringList getModulePortCompletionNames(
        const QString& prefix,
        const QString& moduleTypeName) const;
    QStringList getRelationshipCompletionNames(
        const QString& symbolName,
        const QList<SymbolRelationshipEngine::RelationType>& types,
        bool outgoing,
        const QString& prefix = QString()) const;
    QStringList getBidirectionalRelationshipCompletionNames(
        const QString& symbolName,
        const QList<SymbolRelationshipEngine::RelationType>& types,
        const QString& prefix = QString()) const;
    QStringList getSymbolsWithOutgoingRelationshipCompletionNames(
        SymbolRelationshipEngine::RelationType type,
        const QString& prefix = QString()) const;
    SemanticSymbolRecord getSymbolRecordByStableKey(
        const SymbolStableKey& key) const;
    QList<SemanticSymbolRecord> findDefinitionRecords(
        const QString& name,
        const SemanticQueryContext& context = {}) const;
    SemanticDefinitionResult resolveDefinition(
        const SemanticDefinitionQuery& query) const;
    QString getCachedFileContent(const QString& fileName) const;
    QStringList getScopeSymbolNames(const QString& fileName, int cursorLine) const;
    QSet<QString> activeImportedPackageNames(
        const SemanticQueryContext& context) const;
    bool packageVisibleRecordImported(
        const SemanticSymbolRecord& record,
        const SemanticQueryContext& context) const;
    QString getStructTypeForVariable(const QString& variableName,
                                     const QString& moduleName = QString()) const;
    QList<SemanticSymbolRecord> getStructMemberRecords(
        const QString& structTypeName = QString()) const;
    QList<SemanticSymbolRecord> getModuleInternalSymbolRecordsByType(
        const QString& moduleName,
        CompletionCommandKind commandKind,
        const QString& prefix = QString(),
        bool useRelationshipFallback = true) const;
    QList<SemanticSymbolRecord> getModuleContextSymbolRecordsByType(
        const QString& moduleName,
        const QString& fileName,
        CompletionCommandKind commandKind,
        const QString& prefix = QString()) const;
    QString currentModuleAt(const QString& fileName, int cursorPosition) const;
    bool hasRelationshipFacts() const;
    int scopeScoreForSymbol(const QString& symbolName,
                            const QString& moduleName) const;
    bool isValidModuleName(const QString& name) const;
    int findEndModuleLine(const QString& fileName,
                          const SemanticSymbolRecord& moduleRecord) const;
    bool contentAffectsSymbols(const QString& fileName, const QString& content) const;
    void refreshStructTypedefEnumForFile(const QString& fileName, const QString& content);
    void attachRelationshipEngine(SymbolRelationshipEngine* engine);
    SymbolRelationshipEngine* relationshipEngine() const;
    std::unique_ptr<SmartRelationshipBuilder> createRelationshipBuilder(
        SymbolRelationshipEngine* engine,
        SlangManager* slangManager,
        QObject* parent = nullptr) const;

    QStringList findCompletions(const SemanticQueryContext& context) const;

    QList<SemanticRelationship> relationshipsForStableKey(
        const SymbolStableKey& key,
        bool outgoing = true) const;
    QList<SemanticRelationshipResult> getRelationshipResults(
        const SymbolStableKey& key,
        bool outgoing = true) const;

    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;

private:
    SymbolRelationshipEngine* m_relationshipEngine = nullptr;
    std::shared_ptr<const SemanticIndexSnapshot> m_snapshot;
    std::uint64_t m_snapshotRevision = 0;
    struct NativeFileState {
        QString contentHash;
        QString symbolRelevantHash;
        int lastAnalyzedLineCount = 0;
    };
    QList<SemanticSymbolRecord> m_nativeSymbolRecords;
    QHash<QString, QList<int>> m_nativeRecordIndexesByFile;
    QHash<QString, QList<int>> m_nativeRecordIndexesByName;
    QHash<QString, QList<int>> m_nativeRecordIndexesByOwner;
    QHash<int, QList<int>> m_nativeRecordIndexesByDeclarationKind;
    QHash<QString, QString> m_nativeFileContents;
    QHash<QString, NativeFileState> m_nativeFileStates;
    QHash<QString, int> m_nativeStableKeyIndexes;
    QHash<QString, QSet<int>> m_nativeRecordHandlesByAnalysisFile;
    QSet<QString> m_nativeCoveredFiles;
    QHash<QString, SemanticAnalysisBandMetadata>
        m_workspaceFileAnalysisBands;
    int m_nextNativeLocalHandle = 1;
    static std::unique_ptr<SemanticIndex> instance;

    void replaceNativeSymbolRecordsForFile(
        const QString& fileName,
        const QList<SemanticSymbolRecord>& records,
        const QString& content,
        bool rebuildIndexes = true,
        bool updateIndexesIncrementally = true);
    void rebuildNativeStoreIndexes();
    void appendNativeStoreIndexForRecord(int index);
    QList<SemanticSymbolRecord> nativeSymbolRecords(
        const QString& fileName = QString()) const;
    QList<SemanticSymbolRecord> nativeSymbolRecordsByName(
        const QString& name) const;
    QList<SemanticSymbolRecord> nativeSymbolRecordsByOwner(
        const QString& ownerName) const;
    QList<SemanticSymbolRecord> nativeSymbolRecordsByDeclarationKind(
        SymbolTaxonomy::DeclarationKind declarationKind) const;
    SemanticSymbolRecord nativeSymbolRecordByStableKey(
        const SymbolStableKey& key) const;
    bool hasNativeFileCoverage(const QString& fileName) const;
    bool hasNativeCachedFileContent(const QString& fileName) const;
    QString nativeCachedFileContent(const QString& fileName) const;
    void updateNativeFileState(const QString& fileName, const QString& content);
    bool hasNativeFileState(const QString& fileName) const;
    bool nativeContentAffectsSymbols(const QString& fileName, const QString& content) const;
    SemanticSymbolRecord recordWithAnalysisBand(
        SemanticSymbolRecord record) const;
    QList<SemanticSymbolRecord> recordsWithAnalysisBands(
        QList<SemanticSymbolRecord> records) const;

    SemanticDefinitionResult bestDefinitionFromCandidates(
        const QList<SemanticSymbolRecord>& candidates,
        const SemanticDefinitionQuery& query,
        bool localFile) const;
    QList<SymbolRelationshipEngine::RelationType> relationshipTypes() const;
};

#endif // SEMANTICINDEX_H
