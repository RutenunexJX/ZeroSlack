#ifndef SEMANTICINDEX_H
#define SEMANTICINDEX_H

#include "syminfo.h"
#include "symbolrelationshipengine.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

class SemanticIndexSnapshot;
class SlangManager;
class SmartRelationshipBuilder;
class QObject;

struct SemanticQueryContext {
    QString fileName;
    QString moduleName;
    QString prefix;
    int cursorLine = -1;      // 1-based
    int cursorPosition = -1;  // QTextDocument position, when available
};

struct SemanticRelationship {
    int fromId = -1;
    int toId = -1;
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
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
    std::shared_ptr<const SemanticIndexSnapshot> beginRelationshipAnalysisSnapshot();
    std::shared_ptr<const SemanticIndexSnapshot> snapshotWithAdditionalRelationships(
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
        const QList<SemanticRelationship>& relationships) const;
    bool publishSnapshotIfCurrent(
        std::shared_ptr<const SemanticIndexSnapshot> expectedCurrentSnapshot,
        std::shared_ptr<const SemanticIndexSnapshot> nextSnapshot);

    QList<sym_list::SymbolInfo> getSymbols(const QString& fileName = QString()) const;
    QList<sym_list::SymbolInfo> getSymbolsByType(sym_list::sym_type_e type) const;
    sym_list::SymbolInfo getSymbolById(int symbolId) const;
    int findSymbolId(const QString& name,
                     const SemanticQueryContext& context = {}) const;
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

    QList<SemanticDiagnostic> getDiagnostics(const QString& fileName = QString()) const;

private:
    sym_list* m_symbolDatabase = nullptr;
    std::shared_ptr<const SemanticIndexSnapshot> m_snapshot;
    static std::unique_ptr<SemanticIndex> instance;

    QList<sym_list::SymbolInfo> sortedDefinitions(const QList<sym_list::SymbolInfo>& symbols,
                                                  const SemanticQueryContext& context) const;
    QList<SymbolRelationshipEngine::RelationType> relationshipTypes() const;
};

#endif // SEMANTICINDEX_H
