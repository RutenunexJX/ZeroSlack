#ifndef COMPLETIONMANAGER_H
#define COMPLETIONMANAGER_H

#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>
#include <QPair>
#include <QList>
#include <QSet>
#include <memory>
#include "syminfo.h"
#include "scope_tree.h"

class SymbolRelationshipEngine;
class SlangManager;
class SmartRelationshipBuilder;

class CompletionManager
{
public:
    static CompletionManager* getInstance();
    ~CompletionManager();

    void setRelationshipEngine(SymbolRelationshipEngine* engine);
    void setSlangManager(SlangManager* slangManager);
    SymbolRelationshipEngine* getRelationshipEngine() const;

    bool matchesAbbreviation(const QString &text, const QString &abbreviation);
    QStringList getAbbreviationMatches(const QStringList &candidates, const QString &abbreviation);
    int calculateMatchScore(const QString &text, const QString &abbreviation);

    QList<int> findAbbreviationPositions(const QString &text, const QString &abbreviation);

    QVector<QPair<QString, int>> getSmartCompletions(const QString& prefix,
                                                    const QString& fileName = "",
                                                    int cursorPosition = -1);
    QStringList getContextAwareCompletions(const QString& prefix,
                                          const QString& currentModule = "",
                                          const QString& context = "");

    QVector<QPair<QString, int>> getScoredKeywordMatches(const QString& prefix);
    QVector<QPair<sym_list::SymbolInfo, int>> getScoredSymbolMatches(
        sym_list::sym_type_e symbolType, const QString& prefix);

    QVector<QPair<QString, int>> getScoredAllSymbolMatches(const QString& prefix);
    QStringList getAllSymbolCompletions(const QString& prefix);

    QStringList getKeywordCompletions(const QString& prefix);
    QStringList getSymbolCompletions(sym_list::sym_type_e symbolType, const QString& prefix);

    QStringList getModuleChildrenCompletions(const QString& moduleName, const QString& prefix = "");
    QStringList getRelatedSymbolCompletions(const QString& symbolName, const QString& prefix = "");
    QStringList getSymbolReferencesCompletions(const QString& symbolName, const QString& prefix = "");
    QStringList getClockDomainCompletions(const QString& prefix = "");
    QStringList getResetSignalCompletions(const QString& prefix = "");

    QStringList getVariableCompletionsInScope(const QString& moduleName,
                                             sym_list::sym_type_e variableType,
                                             const QString& prefix = "");
    QStringList getTaskFunctionCompletions(const QString& prefix = "");
    QStringList getInstantiableModules(const QString& prefix = "");

    void invalidateAllCaches();
    void invalidateSymbolCaches();
    void invalidateKeywordCaches();
    void forceRefreshSymbolCaches();

    void precomputeFrequentCompletions();
    void enableSmartCaching(bool enabled = true);
    bool isSmartCachingEnabled() const { return smartCachingEnabled; }

    void invalidateRelationshipCaches();
    void refreshRelationshipData();

    QString getCurrentModule(const QString& fileName, int cursorPosition);

    QStringList getCompletions(const QString& prefix, const QString& cursorFile, int cursorLine);

    QStringList getModuleInternalVariables(const QString& moduleName, const QString& prefix);
    QStringList getGlobalSymbolCompletions(const QString& prefix);


    QStringList getModuleInternalVariablesByType(const QString& moduleName,
                                                sym_list::sym_type_e symbolType,
                                                const QString& prefix = "");

    QStringList getGlobalSymbolsByType(sym_list::sym_type_e symbolType,
                                      const QString& prefix = "");


    QList<sym_list::SymbolInfo> getModuleInternalSymbolsByType(const QString& moduleName,
                                                              sym_list::sym_type_e symbolType,
                                                              const QString& prefix = "",
                                                              bool useRelationshipFallback = true);

    QList<sym_list::SymbolInfo> getModuleContextSymbolsByType(const QString& moduleName,
                                                              const QString& fileName,
                                                              sym_list::sym_type_e symbolType,
                                                              const QString& prefix = "");

    QList<sym_list::SymbolInfo> getGlobalSymbolsByType_Info(sym_list::sym_type_e symbolType,
                                                            const QString& prefix = "");
    void invalidateCommandModeCache();

    QString getStructTypeForVariable(const QString &varName, const QString &currentModule);
    QStringList getStructMemberCompletions(const QString &prefix, const QString &structTypeName);
    bool tryParseStructMemberContext(const QString &line, QString &outVarName, QString &outMemberPrefix);

private:
    CompletionManager();

    static std::unique_ptr<CompletionManager> instance;

    bool isValidAbbreviationMatch(const QString &text, const QString &abbreviation);

    QStringList svKeywords;
    bool keywordsInitialized = false;
    QHash<QString, QStringList> keywordMatchCache;
    QHash<QString, QVector<QPair<QString, int>>> keywordScoreCache;

    QHash<sym_list::sym_type_e, QList<sym_list::SymbolInfo>> symbolTypeCache;
    QHash<QString, QVector<QPair<sym_list::SymbolInfo, int>>> symbolScoreCache;
    int lastSymbolDatabaseSize = 0;
    QString lastSymbolDatabaseHash;

    QHash<sym_list::sym_type_e, QStringList> precomputedCompletions;
    QHash<QString, QStringList> precomputedPrefixMatches;
    bool precomputedDataValid = false;

    QHash<QString, QVector<QPair<QString, int>>> allSymbolScoreCache;
    QHash<QString, QStringList> allSymbolMatchCache;
    QStringList cachedAllSymbolNames;
    bool allSymbolsCacheValid = false;

    QHash<QString, bool> singleMatchCache;
    QHash<QString, int> singleScoreCache;
    QHash<QString, QList<int>> positionCache;

    bool smartCachingEnabled = true;
    int cacheInvalidationThreshold = 100;

    SymbolRelationshipEngine* relationshipEngine = nullptr;
    SlangManager* m_slangManager = nullptr;
    std::unique_ptr<SmartRelationshipBuilder> relationshipBuilder;

    QHash<QString, QStringList> moduleChildrenCache;
    QHash<QString, QStringList> symbolRelationsCache;
    QHash<QString, QStringList> clockDomainCache;
    QHash<QString, QStringList> resetSignalCache;
    QHash<QString, QString> symbolToModuleCache;
    bool relationshipCacheValid = false;

    void initializeKeywords();
    void updateSymbolCaches();
    bool isSymbolCacheValid();
    QString calculateSymbolDatabaseHash();

    QString buildSingleMatchKey(const QString &text, const QString &abbreviation);
    QString buildKeywordCacheKey(const QString &prefix);
    QString buildSymbolCacheKey(sym_list::sym_type_e symbolType, const QString &prefix);

    QVector<QPair<QString, int>> calculateScoredMatches(const QStringList &candidates, const QString &abbreviation);
    QVector<QPair<sym_list::SymbolInfo, int>> calculateScoredSymbolMatches(
        const QList<sym_list::SymbolInfo> &symbols, const QString &abbreviation);

    void updatePrecomputedCompletions();
    void updateAllSymbolsCache();
    QStringList getAllSymbolNamesFromDatabase();

    QList<sym_list::SymbolInfo> getAllSemanticSymbols() const;
    QList<sym_list::SymbolInfo> getSemanticSymbolsByType(sym_list::sym_type_e symbolType) const;
    QList<sym_list::SymbolInfo> getSemanticSymbolsForCommandType(sym_list::sym_type_e symbolType) const;
    QList<sym_list::SymbolInfo> findSemanticDefinitions(const QString& symbolName) const;
    sym_list::SymbolInfo getSemanticSymbolById(int symbolId) const;
    int findSemanticSymbolId(const QString& symbolName) const;

    bool shouldSkipCacheRefresh();

    QStringList getSymbolNamesFromIds(const QList<int>& symbolIds);
    void updateRelationshipCaches();
    QStringList filterCompletionsByContext(const QStringList& completions,
                                         const QString& context);
    int calculateContextScore(const QString& symbol, const QString& context);

    int calculateRelationshipScore(const QString& symbol, const QString& currentContext);
    int calculateScopeScore(const QString& symbol, const QString& currentModule);
    int calculateUsageFrequencyScore(const QString& symbol);
    QStringList getBasicSymbolCompletions(const QString &prefix);

    QString getSymbolTypeString(sym_list::sym_type_e symbolType);

    mutable QHash<QString, QStringList> commandModeCache;
    mutable bool commandModeCacheValid = false;

    QString getSymbolTypeName(sym_list::sym_type_e symbolType);

    QStringList getEnumValueCompletions(const QString &prefix, const QString &enumTypeName);
    QString extractStructTypeFromContext(const QString &context);
    QStringList getModulePortCompletions(const QString &prefix, const QString &moduleTypeName);
    QString getEnumTypeForVariable(const QString &varName, const QString &currentModule);
    QString extractModuleTypeFromContext(const QString &context);
    QString extractEnumVariableFromContext(const QString &context);
    QString extractStructVariableFromContext(const QString &context);
    QStringList getSVKeywordCompletions(const QString &prefix);
};

#endif // COMPLETIONMANAGER_H
