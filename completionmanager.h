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

class SymbolRelationshipEngine; // 🚀 NEW: 前向声明
class SmartRelationshipBuilder;  // 🚀 NEW: 前向声明

class CompletionManager
{
public:
    // 单例模式
    static CompletionManager* getInstance();
    ~CompletionManager();

    // 🚀 NEW: 关系引擎设置
    void setRelationshipEngine(SymbolRelationshipEngine* engine);
    SymbolRelationshipEngine* getRelationshipEngine() const;

    // 核心匹配功能
    bool matchesAbbreviation(const QString &text, const QString &abbreviation);
    QStringList getAbbreviationMatches(const QStringList &candidates, const QString &abbreviation);
    int calculateMatchScore(const QString &text, const QString &abbreviation);

    // 位置查找功能
    QList<int> findAbbreviationPositions(const QString &text, const QString &abbreviation);

    // 🚀 ENHANCED: 智能上下文感知补全
    QVector<QPair<QString, int>> getSmartCompletions(const QString& prefix,
                                                    const QString& fileName = "",
                                                    int cursorPosition = -1);
    QStringList getContextAwareCompletions(const QString& prefix,
                                          const QString& currentModule = "",
                                          const QString& context = "");

    // 高效的批量匹配（带缓存）
    QVector<QPair<QString, int>> getScoredKeywordMatches(const QString& prefix);
    QVector<QPair<sym_list::SymbolInfo, int>> getScoredSymbolMatches(
        sym_list::sym_type_e symbolType, const QString& prefix);

    QVector<QPair<QString, int>> getScoredAllSymbolMatches(const QString& prefix);
    QStringList getAllSymbolCompletions(const QString& prefix);

    QStringList getKeywordCompletions(const QString& prefix);
    QStringList getSymbolCompletions(sym_list::sym_type_e symbolType, const QString& prefix);

    // 🚀 NEW: 关系感知的补全方法
    QStringList getModuleChildrenCompletions(const QString& moduleName, const QString& prefix = "");
    QStringList getRelatedSymbolCompletions(const QString& symbolName, const QString& prefix = "");
    QStringList getSymbolReferencesCompletions(const QString& symbolName, const QString& prefix = "");
    QStringList getClockDomainCompletions(const QString& prefix = "");
    QStringList getResetSignalCompletions(const QString& prefix = "");

    // 🚀 NEW: 高级智能补全
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

    // 🚀 NEW: 关系缓存管理
    void invalidateRelationshipCaches();
    void refreshRelationshipData();

    QString getCurrentModule(const QString& fileName, int cursorPosition);
    QStringList getModuleInternalVariables(const QString& moduleName, const QString& prefix);
    QStringList getGlobalSymbolCompletions(const QString& prefix);


    // 🚀 新增：根据符号类型获取模块内部变量
    QStringList getModuleInternalVariablesByType(const QString& moduleName,
                                                sym_list::sym_type_e symbolType,
                                                const QString& prefix = "");

    // 🚀 新增：根据符号类型获取全局符号
    QStringList getGlobalSymbolsByType(sym_list::sym_type_e symbolType,
                                      const QString& prefix = "");


    QList<sym_list::SymbolInfo> getModuleInternalSymbolsByType(const QString& moduleName,
                                                              sym_list::sym_type_e symbolType,
                                                              const QString& prefix = "");

    QList<sym_list::SymbolInfo> getGlobalSymbolsByType_Info(sym_list::sym_type_e symbolType,
                                                            const QString& prefix = "");
    int findEndModulePosition(const QString &fileContent, const sym_list::SymbolInfo &moduleSymbol);
    void invalidateCommandModeCache();


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

    // 单个匹配结果缓存
    QHash<QString, bool> singleMatchCache;
    QHash<QString, int> singleScoreCache;
    QHash<QString, QList<int>> positionCache;

    // 🚀 智能缓存控制
    bool smartCachingEnabled = true;
    int cacheInvalidationThreshold = 100;

    // 🚀 NEW: 关系引擎相关
    SymbolRelationshipEngine* relationshipEngine = nullptr;
    std::unique_ptr<SmartRelationshipBuilder> relationshipBuilder;

    // 🚀 NEW: 关系感知缓存
    QHash<QString, QStringList> moduleChildrenCache;        // 模块名 -> 子符号列表
    QHash<QString, QStringList> symbolRelationsCache;      // 符号名 -> 相关符号列表
    QHash<QString, QStringList> clockDomainCache;          // 时钟域缓存
    QHash<QString, QStringList> resetSignalCache;          // 复位信号缓存
    QHash<QString, QString> symbolToModuleCache;           // 符号 -> 所属模块映射
    bool relationshipCacheValid = false;

    // ===== 辅助方法 =====
    void initializeKeywords();
    void updateSymbolCaches();
    bool isSymbolCacheValid();
    QString calculateSymbolDatabaseHash();

    QString buildSingleMatchKey(const QString &text, const QString &abbreviation);
    QString buildKeywordCacheKey(const QString &prefix);
    QString buildSymbolCacheKey(sym_list::sym_type_e symbolType, const QString &prefix);

    // 🚀 优化的匹配方法
    QVector<QPair<QString, int>> calculateScoredMatches(const QStringList &candidates, const QString &abbreviation);
    QVector<QPair<sym_list::SymbolInfo, int>> calculateScoredSymbolMatches(
        const QList<sym_list::SymbolInfo> &symbols, const QString &abbreviation);

    // 🚀 NEW: 预计算和智能缓存方法
    void updatePrecomputedCompletions();
    void updateAllSymbolsCache();
    QStringList getAllSymbolNamesFromDatabase();

    bool shouldSkipCacheRefresh();

    // 🚀 NEW: 关系感知的辅助方法
    QStringList getSymbolNamesFromIds(const QList<int>& symbolIds);
    int findSymbolIdByName(const QString& symbolName);
    void updateRelationshipCaches();
    QStringList filterCompletionsByContext(const QStringList& completions,
                                         const QString& context);
    int calculateContextScore(const QString& symbol, const QString& context);

    // 🚀 NEW: 智能评分算法
    int calculateRelationshipScore(const QString& symbol, const QString& currentContext);
    int calculateScopeScore(const QString& symbol, const QString& currentModule);
    int calculateUsageFrequencyScore(const QString& symbol);
    QStringList getBasicSymbolCompletions(const QString &prefix);

    bool isInternalVariableType(sym_list::sym_type_e symbolType);
    QString findModuleAtPosition(const QList<sym_list::SymbolInfo>& modules,
                               int cursorPosition,
                               const QString& fileName);

    // 🚀 新增：检查符号类型是否匹配命令
    bool isSymbolTypeMatchCommand(sym_list::sym_type_e symbolType,
                                 sym_list::sym_type_e commandType);


    // 🚀 新增：获取符号类型的字符串表示
    QString getSymbolTypeString(sym_list::sym_type_e symbolType);

    // 🚀 新增：命令模式专用缓存
    mutable QHash<QString, QStringList> commandModeCache;
    mutable bool commandModeCacheValid = false;

    QString getSymbolTypeName(sym_list::sym_type_e symbolType);

    int getNextModulePosition(const QList<struct sym_list::SymbolInfo>& modules,
                              const struct sym_list::SymbolInfo& currentModule);
    QStringList getEnumValueCompletions(const QString &prefix, const QString &enumTypeName);
    QStringList getStructMemberCompletions(const QString &prefix, const QString &structTypeName);
    QString extractStructTypeFromContext(const QString &context);
    QStringList getModulePortCompletions(const QString &prefix, const QString &moduleTypeName);
    QString getEnumTypeForVariable(const QString &varName, const QString &currentModule);
    QString getStructTypeForVariable(const QString &varName, const QString &currentModule);
    QString extractModuleTypeFromContext(const QString &context);
    QString extractEnumVariableFromContext(const QString &context);
    QString extractStructVariableFromContext(const QString &context);
    QStringList getSVKeywordCompletions(const QString &prefix);
};

#endif // COMPLETIONMANAGER_H
