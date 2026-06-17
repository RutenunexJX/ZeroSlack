#ifndef SEMANTICINDEX_H
#define SEMANTICINDEX_H

#include "syminfo.h"
#include "symbolrelationshipengine.h"
#include "symboltaxonomy.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>

class SemanticIndexSnapshot;
class SlangManager;
class SmartRelationshipBuilder;
class QObject;

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

struct SemanticRelationship {
    int fromId = -1;
    int toId = -1;
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
};

struct SemanticRelationshipResult {
    SemanticRelationship relationship;
    sym_list::SymbolInfo fromSymbol;
    sym_list::SymbolInfo toSymbol;
    SymbolStableKey fromStableKey;
    SymbolStableKey toStableKey;
};

struct SemanticDiagnostic {
    enum Severity {
        Info,
        Warning,
        Error
    };

    QString fileName;
    int line = 0;
    int column = 0;
    QString message;
    Severity severity = Info;
};

struct SemanticSymbolSearchQuery {
    QString text;
    QString fileName;
    QList<sym_list::sym_type_e> types;
    SymbolTaxonomy::SymbolSearchIntent intent = SymbolTaxonomy::SymbolSearchIntent::Any;
    bool caseSensitive = false;
    bool exactMatch = false;
    int maxResults = -1;
};

struct SemanticSymbolSearchResult {
    sym_list::SymbolInfo symbol;
    SymbolStableKey symbolStableKey;
    int score = 0;
};

struct SemanticDefinitionQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    QString structTypeNameForMember;
};

enum class SemanticDefinitionMissReason {
    None,
    EmptySymbolName,
    NoCandidateSymbols,
    NoMatchingName,
    StructMemberTypeMismatch,
    NotVisibleInContext
};

struct SemanticDefinitionResult {
    bool found = false;
    bool localFile = false;
    sym_list::SymbolInfo symbol;
    SymbolStableKey symbolStableKey;
    int inspectedCandidateCount = 0;
    int matchingNameCandidateCount = 0;
    int typeCompatibleCandidateCount = 0;
    int visibleCandidateCount = 0;
    SemanticDefinitionMissReason missReason =
        SemanticDefinitionMissReason::NoCandidateSymbols;
};

SymbolStableKey symbolStableKeyForSymbol(const sym_list::SymbolInfo& symbol);
QString symbolStableKeyText(const SymbolStableKey& key);
QString semanticRelationshipStableKeyText(
    const SemanticRelationship& relationship);

// Thin facade over the current sym_list-backed semantic store.
//
// This is the migration boundary for new code: the first implementation delegates to sym_list
// and existing services, while later versions can swap in snapshots without changing callers.
class SemanticIndex
{
public:
    static SemanticIndex* getInstance();

    explicit SemanticIndex(sym_list* symbolDatabase = nullptr);
    ~SemanticIndex();

    void setSymbolDatabase(sym_list* symbolDatabase);
    sym_list* symbolDatabase() const;
    void setSnapshot(std::shared_ptr<const SemanticIndexSnapshot> snapshot);
    void clearSnapshot();
    std::shared_ptr<const SemanticIndexSnapshot> snapshot() const;
    std::uint64_t snapshotRevision() const;
    SemanticSnapshotToken snapshotToken() const;
    void updateSymbolsForFile(const QString& fileName,
                              const QList<sym_list::SymbolInfo>& symbols,
                              const QString& content);
    void publishCompleteSnapshot(QList<SemanticDiagnostic> diagnostics = {});
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

    QList<sym_list::SymbolInfo> getSymbols(const QString& fileName = QString()) const;
    QList<sym_list::SymbolInfo> getSymbolsByType(sym_list::sym_type_e type) const;
    QList<SemanticSymbolSearchResult> searchSymbols(
        const SemanticSymbolSearchQuery& query) const;
    QList<sym_list::SymbolInfo> getModuleCompletionSymbols(
        const QString& moduleName,
        const QString& prefix = QString()) const;
    QList<sym_list::SymbolInfo> getGlobalCompletionSymbols(
        const QString& prefix = QString()) const;
    QList<sym_list::SymbolInfo> getCommandCompletionSymbols(
        const QString& moduleName,
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString()) const;
    QStringList getCompletionSymbolNames() const;
    QList<sym_list::SymbolInfo> getTypedCompletionSymbols(
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString()) const;
    QList<sym_list::SymbolInfo> getGlobalSymbolInfosByType(
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString()) const;
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
    sym_list::SymbolInfo getSymbolById(int symbolId) const;
    sym_list::SymbolInfo getSymbolByStableKey(const SymbolStableKey& key) const;
    SemanticDefinitionResult resolveDefinition(
        const SemanticDefinitionQuery& query) const;
    QList<sym_list::SymbolInfo> findDefinitionSymbols(
        const SemanticDefinitionQuery& query) const;
    int findSymbolId(const QString& name,
                     const SemanticQueryContext& context = {}) const;
    int findSymbolId(const SymbolStableKey& key) const;
    QString getCachedFileContent(const QString& fileName) const;
    QStringList getScopeSymbolNames(const QString& fileName, int cursorLine) const;
    QString getStructTypeForVariable(const QString& variableName,
                                     const QString& moduleName = QString()) const;
    QList<sym_list::SymbolInfo> getStructMembers(
        const QString& structTypeName = QString()) const;
    QList<sym_list::SymbolInfo> getModuleInternalSymbolsByType(
        const QString& moduleName,
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString(),
        bool useRelationshipFallback = true) const;
    QList<sym_list::SymbolInfo> getModuleContextSymbolsByType(
        const QString& moduleName,
        const QString& fileName,
        sym_list::sym_type_e symbolType,
        const QString& prefix = QString()) const;
    QString currentModuleAt(const QString& fileName, int cursorPosition) const;
    bool hasRelationshipFacts() const;
    int scopeScoreForSymbol(const QString& symbolName,
                            const QString& moduleName) const;
    bool isValidModuleName(const QString& name) const;
    int findEndModuleLine(const QString& fileName,
                          const sym_list::SymbolInfo& moduleSymbol) const;
    bool contentAffectsSymbols(const QString& fileName, const QString& content) const;
    void refreshStructTypedefEnumForFile(const QString& fileName, const QString& content);
    void attachRelationshipEngine(SymbolRelationshipEngine* engine) const;
    std::unique_ptr<SmartRelationshipBuilder> createRelationshipBuilder(
        SymbolRelationshipEngine* engine,
        SlangManager* slangManager,
        QObject* parent = nullptr) const;

    QList<sym_list::SymbolInfo> findDefinitions(const QString& name,
                                                const SemanticQueryContext& context = {}) const;
    QStringList findCompletions(const SemanticQueryContext& context) const;

    QList<SemanticRelationship> getRelationships(int symbolId, bool outgoing = true) const;
    QList<SemanticRelationship> getRelationships(const QString& scopeName,
                                                 bool outgoing = true) const;
    QList<SemanticRelationshipResult> getRelationshipResults(
        int symbolId,
        bool outgoing = true) const;
    QList<SemanticRelationshipResult> getRelationshipResults(
        const QString& scopeName,
        bool outgoing = true) const;

    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;

private:
    sym_list* m_symbolDatabase = nullptr;
    std::shared_ptr<const SemanticIndexSnapshot> m_snapshot;
    std::uint64_t m_snapshotRevision = 0;
    static std::unique_ptr<SemanticIndex> instance;

    QList<sym_list::SymbolInfo> sortedDefinitions(const QList<sym_list::SymbolInfo>& symbols,
                                                  const SemanticQueryContext& context) const;
    SemanticDefinitionResult bestDefinitionFromCandidates(
        const QList<sym_list::SymbolInfo>& candidates,
        const SemanticDefinitionQuery& query,
        bool localFile) const;
    QList<SymbolRelationshipEngine::RelationType> relationshipTypes() const;
};

#endif // SEMANTICINDEX_H
