#include "completionmanager.h"
#include "symbolrelationshipengine.h"
#include "smartrelationshipbuilder.h"

#include <QDateTime>
#include <algorithm>

// 单例实例
std::unique_ptr<CompletionManager> CompletionManager::instance = nullptr;

CompletionManager::CompletionManager()
{
    // 预分配内存以提高性能
    keywordMatchCache.reserve(100);
    keywordScoreCache.reserve(100);
    symbolScoreCache.reserve(200);
    singleMatchCache.reserve(1000);
    singleScoreCache.reserve(1000);
    positionCache.reserve(500);

    // 🚀 NEW: Reserve space for optimized caches
    allSymbolScoreCache.reserve(150);
    allSymbolMatchCache.reserve(150);
    precomputedCompletions.reserve(20);
    precomputedPrefixMatches.reserve(300);
}

CompletionManager::~CompletionManager()
{
}

CompletionManager* CompletionManager::getInstance()
{
    if (!instance) {
        instance = std::unique_ptr<CompletionManager>(new CompletionManager());
    }
    return instance.get();
}

// ===== 🚀 超高性能的符号匹配方法 =====

QVector<QPair<QString, int>> CompletionManager::getScoredAllSymbolMatches(const QString& prefix)
{
    QString cacheKey = QString("all_symbols_%1").arg(prefix);

    // 🚀 智能缓存检查
    if (allSymbolScoreCache.contains(cacheKey) && allSymbolsCacheValid) {
        return allSymbolScoreCache[cacheKey];
    }

    // 🚀 确保所有符号缓存是最新的
    updateAllSymbolsCache();

    // 🚀 使用预计算的符号名称列表进行快速过滤
    QVector<QPair<QString, int>> scoredMatches;
    scoredMatches.reserve(qMin(cachedAllSymbolNames.size(), 50));

    for (const QString& symbolName : qAsConst(cachedAllSymbolNames)) {
        int score = calculateMatchScore(symbolName, prefix);
        if (score > 0) {
            scoredMatches.append(qMakePair(symbolName, score));
        }
    }

    // 🚀 高效排序
    std::sort(scoredMatches.begin(), scoredMatches.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    // 🚀 限制结果数量以提高性能
    if (scoredMatches.size() > 20) {
        scoredMatches = scoredMatches.mid(0, 20);
    }

    // 缓存结果
    allSymbolScoreCache[cacheKey] = scoredMatches;
    return scoredMatches;
}

// 🚀 智能符号匹配方法（使用索引优化）
QVector<QPair<sym_list::SymbolInfo, int>> CompletionManager::getScoredSymbolMatches(
    sym_list::sym_type_e symbolType, const QString& prefix)
{
    updateSymbolCaches();
    QString cacheKey = buildSymbolCacheKey(symbolType, prefix);

    if (symbolScoreCache.contains(cacheKey)) {
        return symbolScoreCache[cacheKey];
    }

    // 🚀 使用高性能索引查找
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByType(symbolType);

    QVector<QPair<sym_list::SymbolInfo, int>> scoredMatches;
    scoredMatches.reserve(qMin(symbols.size(), 30));

    // 🚀 优化的匹配逻辑
    for (const sym_list::SymbolInfo &symbol : qAsConst(symbols)) {
        int score = 0;

        if (prefix.isEmpty()) {
            score = 100; // 空前缀：显示所有符号
        } else {
            const QString lowerText = symbol.symbolName.toLower();
            const QString lowerPrefix = prefix.toLower();

            if (lowerText == lowerPrefix) {
                score = 1000; // 精确匹配
            } else if (lowerText.startsWith(lowerPrefix)) {
                score = 800 + (100 - prefix.length()); // 前缀匹配
            } else if (lowerText.contains(lowerPrefix)) {
                score = 400 + (100 - symbol.symbolName.length()); // 包含匹配
            } else if (matchesAbbreviation(symbol.symbolName, prefix)) {
                score = 200; // 缩写匹配
            }
        }

        if (score > 0) {
            scoredMatches.append(qMakePair(symbol, score));
        }
    }

    // 🚀 高效排序
    std::sort(scoredMatches.begin(), scoredMatches.end(),
              [](const QPair<sym_list::SymbolInfo, int> &a, const QPair<sym_list::SymbolInfo, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first.symbolName < b.first.symbolName;
              });

    // 限制结果数量
    if (scoredMatches.size() > 15) {
        scoredMatches = scoredMatches.mid(0, 15);
    }

    symbolScoreCache[cacheKey] = scoredMatches;
    return scoredMatches;
}

// 🚀 智能缓存更新（避免过度刷新）
void CompletionManager::forceRefreshSymbolCaches()
{
    // 🚀 智能检查：避免不必要的刷新
    if (shouldSkipCacheRefresh()) {
        return;
    }

    // 重置大小检测，强制更新
    lastSymbolDatabaseSize = -1;
    lastSymbolDatabaseHash.clear();

    // 清除所有符号相关缓存
    invalidateSymbolCaches();

    // 立即更新缓存
    updateSymbolCaches();
    updateAllSymbolsCache();

    // 🚀 预计算常用补全
    if (smartCachingEnabled) {
        precomputeFrequentCompletions();
    }
}

// 🚀 更新所有符号缓存
void CompletionManager::updateAllSymbolsCache()
{
    if (allSymbolsCacheValid) return;

    // 🚀 使用高性能方法获取所有唯一符号名称
    sym_list* symbolList = sym_list::getInstance();
    QSet<QString> uniqueNames = symbolList->getUniqueSymbolNames();

    cachedAllSymbolNames = uniqueNames.toList();
    cachedAllSymbolNames.sort(); // 排序以提高查找效率

    // 清空旧的匹配缓存
    allSymbolScoreCache.clear();
    allSymbolMatchCache.clear();

    allSymbolsCacheValid = true;
}

// 🚀 预计算常用补全
void CompletionManager::precomputeFrequentCompletions()
{
    sym_list* symbolList = sym_list::getInstance();

    // 为每种符号类型预计算名称列表
    QList<sym_list::sym_type_e> commonTypes = {
        sym_list::sym_reg, sym_list::sym_wire, sym_list::sym_logic,
        sym_list::sym_module, sym_list::sym_task, sym_list::sym_function
    };

    for (sym_list::sym_type_e symbolType : commonTypes) {
        QStringList names = symbolList->getSymbolNamesByType(symbolType);
        precomputedCompletions[symbolType] = names;
    }

    // 预计算常用前缀的匹配结果
    QStringList commonPrefixes = {"c", "d", "e", "m", "r", "s", "t", "v", "w"};

    for (const QString& prefix : commonPrefixes) {
        QStringList matches;
        for (const QString& name : qAsConst(cachedAllSymbolNames)) {
            if (name.startsWith(prefix, Qt::CaseInsensitive)) {
                matches.append(name);
            }
        }

        if (!matches.isEmpty()) {
            matches.sort();
            precomputedPrefixMatches[prefix] = matches;
        }
    }

    precomputedDataValid = true;
}

// 🚀 智能判断是否应该跳过缓存刷新
bool CompletionManager::shouldSkipCacheRefresh()
{
    if (!smartCachingEnabled) return false;

    sym_list* symbolList = sym_list::getInstance();
    int currentSize = symbolList->getAllSymbols().size();
    QString currentHash = calculateSymbolDatabaseHash();

    // 大小没有变化且内容哈希相同
    bool sizeUnchanged = (currentSize == lastSymbolDatabaseSize);
    bool contentUnchanged = (!lastSymbolDatabaseHash.isEmpty() && currentHash == lastSymbolDatabaseHash);

    if (sizeUnchanged && contentUnchanged) {
        return true;
    }

    // 更新追踪变量
    lastSymbolDatabaseSize = currentSize;
    lastSymbolDatabaseHash = currentHash;

    return false;
}

// 🚀 计算符号数据库内容哈希
QString CompletionManager::calculateSymbolDatabaseHash()
{
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    // 使用符号数量和前几个符号名称生成轻量级哈希
    QString hashInput = QString::number(allSymbols.size());

    int sampleSize = qMin(10, allSymbols.size());
    for (int i = 0; i < sampleSize; ++i) {
        hashInput += allSymbols[i].symbolName;
    }

    return QString::number(qHash(hashInput));
}

// 🚀 启用/禁用智能缓存
void CompletionManager::enableSmartCaching(bool enabled)
{
    smartCachingEnabled = enabled;

    if (enabled) {
        precomputeFrequentCompletions();
    }
}

QStringList CompletionManager::getAllSymbolCompletions(const QString& prefix)
{
    QString cacheKey = QString("all_symbols_list_%1").arg(prefix);

    // 检查简单字符串缓存
    if (allSymbolMatchCache.contains(cacheKey) && allSymbolsCacheValid) {
        return allSymbolMatchCache[cacheKey];
    }

    // 🚀 尝试使用预计算的结果
    if (precomputedDataValid && prefix.length() == 1 && precomputedPrefixMatches.contains(prefix)) {
        QStringList result = precomputedPrefixMatches[prefix];
        if (result.size() > 15) {
            result = result.mid(0, 15);
        }

        allSymbolMatchCache[cacheKey] = result;
        return result;
    }

    // 获取评分匹配
    QVector<QPair<QString, int>> scoredMatches = getScoredAllSymbolMatches(prefix);

    // 提取字符串列表
    QStringList result;
    result.reserve(scoredMatches.size());
    for (const auto &match : qAsConst(scoredMatches)) {
        result.append(match.first);
    }

    // 限制结果数量
    if (result.size() > 15) {
        result = result.mid(0, 15);
    }

    // 缓存字符串结果
    allSymbolMatchCache[cacheKey] = result;

    return result;
}

QStringList CompletionManager::getSymbolCompletions(sym_list::sym_type_e symbolType, const QString& prefix)
{
    // 🚀 尝试使用预计算的结果
    if (precomputedDataValid && prefix.isEmpty() && precomputedCompletions.contains(symbolType)) {
        QStringList result = precomputedCompletions[symbolType];
        if (result.size() > 15) {
            result = result.mid(0, 15);
        }

        return result;
    }

    QVector<QPair<sym_list::SymbolInfo, int>> scoredMatches = getScoredSymbolMatches(symbolType, prefix);

    QStringList result;
    result.reserve(scoredMatches.size());

    for (const auto &match : qAsConst(scoredMatches)) {
        if (!result.contains(match.first.symbolName)) {
            result.append(match.first.symbolName);
        }
    }

    // 限制结果数量
    if (result.size() > 15) {
        result = result.mid(0, 15);
    }

    return result;
}

// ===== 缓存管理功能（优化版） =====

void CompletionManager::invalidateAllCaches()
{
    // 🚀 现有的缓存清理（保持不变）
    keywordMatchCache.clear();
    keywordScoreCache.clear();
    symbolTypeCache.clear();
    symbolScoreCache.clear();
    singleMatchCache.clear();
    singleScoreCache.clear();
    positionCache.clear();

    // 🚀 现有的新增缓存清理
    allSymbolScoreCache.clear();
    allSymbolMatchCache.clear();
    precomputedCompletions.clear();
    precomputedPrefixMatches.clear();

    // 🚀 现有的关系缓存清理
    moduleChildrenCache.clear();
    clockDomainCache.clear();
    resetSignalCache.clear();

    // 🚀 新增：命令模式缓存清理
    invalidateCommandModeCache();

    // 🚀 重置状态标志（保持不变）
    allSymbolsCacheValid = false;
    precomputedDataValid = false;
}

void CompletionManager::invalidateSymbolCaches()
{
    symbolTypeCache.clear();
    symbolScoreCache.clear();

    // 清除所有符号相关缓存
    allSymbolScoreCache.clear();
    allSymbolMatchCache.clear();
    allSymbolsCacheValid = false;

    // 🚀 新增：同时清理命令模式缓存
    invalidateCommandModeCache();

    // 保留预计算数据，除非符号结构发生重大变化
    if (smartCachingEnabled) {
        sym_list* symbolList = sym_list::getInstance();
        int currentSize = symbolList->getAllSymbols().size();

        if (abs(currentSize - lastSymbolDatabaseSize) > cacheInvalidationThreshold) {
            precomputedCompletions.clear();
            precomputedPrefixMatches.clear();
            precomputedDataValid = false;
        }
    } else {
        precomputedCompletions.clear();
        precomputedPrefixMatches.clear();
        precomputedDataValid = false;
    }
}

void CompletionManager::updateSymbolCaches()
{
    sym_list* symbolList = sym_list::getInstance();
    int currentSize = symbolList->getAllSymbols().size();

    // 🚀 智能更新检测
    bool shouldUpdate = (currentSize != lastSymbolDatabaseSize) || symbolTypeCache.isEmpty();

    if (shouldUpdate) {
        invalidateSymbolCaches();
        lastSymbolDatabaseSize = currentSize;

        // 🚀 使用高性能索引方法填充缓存
        symbolTypeCache[sym_list::sym_reg] = symbolList->findSymbolsByType(sym_list::sym_reg);
        symbolTypeCache[sym_list::sym_wire] = symbolList->findSymbolsByType(sym_list::sym_wire);
        symbolTypeCache[sym_list::sym_logic] = symbolList->findSymbolsByType(sym_list::sym_logic);
        symbolTypeCache[sym_list::sym_module] = symbolList->findSymbolsByType(sym_list::sym_module);
        symbolTypeCache[sym_list::sym_task] = symbolList->findSymbolsByType(sym_list::sym_task);
        symbolTypeCache[sym_list::sym_function] = symbolList->findSymbolsByType(sym_list::sym_function);

        // 🚀 标记需要更新所有符号缓存
        allSymbolsCacheValid = false;
    }
}

// ===== 原有的核心匹配功能保持不变 =====

bool CompletionManager::matchesAbbreviation(const QString &text, const QString &abbreviation)
{
    if (abbreviation.isEmpty() || text.isEmpty()) {
        return false;
    }

    // 检查缓存
    QString cacheKey = buildSingleMatchKey(text, abbreviation);
    if (singleMatchCache.contains(cacheKey)) {
        return singleMatchCache[cacheKey];
    }

    const QString lowerText = text.toLower();
    const QString lowerAbbrev = abbreviation.toLower();

    // 前缀匹配优先
    if (lowerText.startsWith(lowerAbbrev)) {
        singleMatchCache[cacheKey] = true;
        return true;
    }

    // 缩写匹配（传入原始文本以支持驼峰命名检测）
    bool result = isValidAbbreviationMatch(text, abbreviation);
    singleMatchCache[cacheKey] = result;
    return result;
}

int CompletionManager::calculateMatchScore(const QString &text, const QString &abbreviation)
{
    if (abbreviation.isEmpty() || text.isEmpty()) {
        return 0;
    }

    // 检查缓存
    QString cacheKey = buildSingleMatchKey(text, abbreviation);
    if (singleScoreCache.contains(cacheKey)) {
        return singleScoreCache[cacheKey];
    }

    const QString lowerText = text.toLower();
    const QString lowerAbbrev = abbreviation.toLower();

    int score = 0;

    // 精确匹配
    if (lowerText == lowerAbbrev) {
        score = 1000;
    }
    // 前缀匹配
    else if (lowerText.startsWith(lowerAbbrev)) {
        score = 800 + (100 - abbreviation.length());
    }
    // 包含匹配
    else if (lowerText.contains(lowerAbbrev)) {
        score = 400 + (100 - text.length());
    }
    // 缩写匹配（传入原始文本以支持驼峰命名检测）
    else if (isValidAbbreviationMatch(text, abbreviation)) {
        QList<int> positions = findAbbreviationPositions(text, abbreviation);
        score = 500;

        // 单词边界奖励
        int wordBoundaryMatches = 0;
        for (int pos : qAsConst(positions)) {
            if (pos == 0 || lowerText[pos - 1] == '_' || lowerText[pos - 1] == ' ') {
                wordBoundaryMatches++;
            }
            // 驼峰命名边界检查
            if (pos > 0 && pos < lowerText.length()) {
                QChar prevChar = text[pos - 1];
                QChar currChar = text[pos];
                if (prevChar.isLower() && currChar.isUpper()) {
                    wordBoundaryMatches++;
                }
            }
        }

        score += wordBoundaryMatches * 50;
        score -= text.length(); // 较短文本的奖励

        // 连续字符奖励
        for (int i = 1; i < positions.size(); i++) {
            if (positions[i] == positions[i-1] + 1) {
                score += 10;
            }
        }
    }

    // 缓存结果
    singleScoreCache[cacheKey] = score;
    return score;
}

// ===== 辅助方法 =====

bool CompletionManager::isValidAbbreviationMatch(const QString &text, const QString &abbreviation)
{
    if (abbreviation.length() > text.length()) {
        return false;
    }

    // 转换为小写用于比较，但保留原始文本用于驼峰命名检测
    const QString lowerText = text.toLower();
    const QString lowerAbbrev = abbreviation.toLower();

    int textPos = 0;
    int abbrevPos = 0;

    while (abbrevPos < lowerAbbrev.length() && textPos < text.length()) {
        QChar abbrevChar = lowerAbbrev[abbrevPos];
        QChar textChar = lowerText[textPos];

        if (abbrevChar == textChar) {
            abbrevPos++;
            textPos++;
        } else {
            // 检查是否是分隔符（下划线、空格或驼峰命名边界）
            bool isSeparator = (text[textPos] == '_' || text[textPos] == ' ');

            if (textPos > 0) {
                QChar prevChar = text[textPos - 1];
                QChar currChar = text[textPos];
                // 驼峰命名边界检测：小写字母后跟大写字母
                if (prevChar.isLower() && currChar.isUpper()) {
                    isSeparator = true;
                }
            }

            // 如果是分隔符，跳过分隔符并检查下一个字符是否匹配
            if (isSeparator && textPos + 1 < text.length()) {
                QChar nextChar = lowerText[textPos + 1];
                if (abbrevChar == nextChar) {
                    textPos++; // 跳过分隔符
                    continue;
                }
            }

            textPos++;
        }
    }

    return abbrevPos == lowerAbbrev.length();
}

QList<int> CompletionManager::findAbbreviationPositions(const QString &text, const QString &abbreviation)
{
    QString cacheKey = buildSingleMatchKey(text, abbreviation) + "_pos";

    if (positionCache.contains(cacheKey)) {
        return positionCache[cacheKey];
    }

    QList<int> positions;
    if (!isValidAbbreviationMatch(text, abbreviation)) {
        positionCache[cacheKey] = positions;
        return positions;
    }

    const QString lowerText = text.toLower();
    const QString lowerAbbrev = abbreviation.toLower();

    int textPos = 0;
    int abbrevPos = 0;

    while (abbrevPos < lowerAbbrev.length() && textPos < lowerText.length()) {
        if (lowerAbbrev[abbrevPos] == lowerText[textPos]) {
            positions.append(textPos);
            abbrevPos++;
        } else {
            // 处理分隔符：跳过分隔符字符
            bool isSeparator = (text[textPos] == '_' || text[textPos] == ' ');
            if (textPos > 0) {
                QChar prevChar = text[textPos - 1];
                QChar currChar = text[textPos];
                if (prevChar.isLower() && currChar.isUpper()) {
                    isSeparator = true;
                }
            }
            if (isSeparator && textPos + 1 < text.length() && 
                lowerAbbrev[abbrevPos] == lowerText[textPos + 1]) {
                textPos++; // 跳过分隔符
                continue;
            }
        }
        textPos++;
    }

    positionCache[cacheKey] = positions;
    return positions;
}

QString CompletionManager::buildSingleMatchKey(const QString &text, const QString &abbreviation)
{
    return QString("%1|%2").arg(text, abbreviation);
}

QString CompletionManager::buildKeywordCacheKey(const QString &prefix)
{
    return QString("kw_%1").arg(prefix);
}

QString CompletionManager::buildSymbolCacheKey(sym_list::sym_type_e symbolType, const QString &prefix)
{
    return QString("sym_%1_%2").arg(static_cast<int>(symbolType)).arg(prefix);
}

void CompletionManager::initializeKeywords()
{
    if (keywordsInitialized) return;

    svKeywords.clear();
    svKeywords << "always" << "always_comb" << "always_ff" << "assign" << "begin" << "end"
               << "module" << "endmodule" << "generate" << "endgenerate" << "if" << "else" << "for"
               << "define" << "ifdef" << "ifndef" << "task" << "endtask" << "initial"
               << "reg" << "wire" << "logic" << "enum" << "localparam" << "parameter"
               << "struct" << "package" << "endpackage" << "interface" << "endinterface"
               << "function" << "endfunction" << "case" << "endcase" << "default"
               << "posedge" << "negedge" << "input" << "output" << "inout";

    keywordsInitialized = true;
}

QVector<QPair<QString, int>> CompletionManager::getScoredKeywordMatches(const QString& prefix)
{
    initializeKeywords(); // 确保关键字已初始化

    QString cacheKey = buildKeywordCacheKey(prefix);

    // 检查缓存
    if (keywordScoreCache.contains(cacheKey)) {
        return keywordScoreCache[cacheKey];
    }

    // 计算匹配结果
    QVector<QPair<QString, int>> scoredMatches = calculateScoredMatches(svKeywords, prefix);

    // 缓存结果
    keywordScoreCache[cacheKey] = scoredMatches;

    return scoredMatches;
}

QStringList CompletionManager::getKeywordCompletions(const QString& prefix)
{
    QString cacheKey = buildKeywordCacheKey(prefix);

    // 检查简单字符串缓存
    if (keywordMatchCache.contains(cacheKey)) {
        return keywordMatchCache[cacheKey];
    }

    // 获取评分匹配
    QVector<QPair<QString, int>> scoredMatches = getScoredKeywordMatches(prefix);

    // 提取字符串列表
    QStringList result;
    result.reserve(scoredMatches.size());
    for (const auto &match : qAsConst(scoredMatches)) {
        result.append(match.first);
    }

    // 限制结果数量
    if (result.size() > 10) {
        result = result.mid(0, 10);
    }

    // 缓存字符串结果
    keywordMatchCache[cacheKey] = result;

    return result;
}

QStringList CompletionManager::getAbbreviationMatches(const QStringList &candidates, const QString &abbreviation)
{
    QVector<QPair<QString, int>> scoredMatches = getScoredKeywordMatches(abbreviation);

    // 只返回匹配的字符串（保持与原始接口兼容）
    QStringList result;
    result.reserve(scoredMatches.size());

    for (const auto &match : qAsConst(scoredMatches)) {
        if (candidates.contains(match.first)) {
            result.append(match.first);
        }
    }

    return result;
}

QVector<QPair<QString, int>> CompletionManager::calculateScoredMatches(const QStringList &candidates, const QString &abbreviation)
{
    QVector<QPair<QString, int>> scoredMatches;
    scoredMatches.reserve(candidates.size());

    for (const QString &candidate : candidates) {
        int score = calculateMatchScore(candidate, abbreviation);
        if (score > 0) {
            scoredMatches.append(qMakePair(candidate, score));
        }
    }

    // 按分数排序
    std::sort(scoredMatches.begin(), scoredMatches.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first; // 相同分数时按字母顺序
              });

    return scoredMatches;
}

QVector<QPair<sym_list::SymbolInfo, int>> CompletionManager::calculateScoredSymbolMatches(
    const QList<sym_list::SymbolInfo> &symbols, const QString &abbreviation)
{
    QVector<QPair<sym_list::SymbolInfo, int>> scoredMatches;
    scoredMatches.reserve(symbols.size());

    for (const sym_list::SymbolInfo &symbol : symbols) {
        int score = calculateMatchScore(symbol.symbolName, abbreviation);
        if (score > 0) {
            scoredMatches.append(qMakePair(symbol, score));
        }
    }

    // 按分数排序
    std::sort(scoredMatches.begin(), scoredMatches.end(),
              [](const QPair<sym_list::SymbolInfo, int> &a, const QPair<sym_list::SymbolInfo, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first.symbolName < b.first.symbolName;
              });

    return scoredMatches;
}

void CompletionManager::invalidateKeywordCaches()
{
    keywordMatchCache.clear();
    keywordScoreCache.clear();
}

bool CompletionManager::isSymbolCacheValid()
{
    sym_list* symbolList = sym_list::getInstance();
    return lastSymbolDatabaseSize == symbolList->getAllSymbols().size();
}

void CompletionManager::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    relationshipEngine = engine;

    if (engine && !relationshipBuilder) {
        relationshipBuilder = std::make_unique<SmartRelationshipBuilder>(
            engine, sym_list::getInstance(), nullptr);
    }

    // 标记关系缓存需要更新
    relationshipCacheValid = false;
}

SymbolRelationshipEngine* CompletionManager::getRelationshipEngine() const
{
    return relationshipEngine;
}

QVector<QPair<QString, int>> CompletionManager::getSmartCompletions(const QString& prefix,
                                                                  const QString& fileName,
                                                                  int cursorPosition)
{
    QVector<QPair<QString, int>> results;

    if (!relationshipEngine) {
        // 降级到传统补全
        return getScoredAllSymbolMatches(prefix);
    }

    // 🚀 确定当前上下文
    QString currentModule = getCurrentModule(fileName, cursorPosition);
    QString context = "general"; // 可以进一步细化上下文类型

    // 🚀 获取上下文感知的补全
    QStringList contextCompletions = getContextAwareCompletions(prefix, currentModule, context);

    results.reserve(contextCompletions.size());

    // 🚀 为每个补全计算智能评分
    for (const QString& completion : qAsConst(contextCompletions)) {
        int baseScore = calculateMatchScore(completion, prefix);
        int contextScore = calculateContextScore(completion, context);
        int relationshipScore = calculateRelationshipScore(completion, currentModule);
        int scopeScore = calculateScopeScore(completion, currentModule);

        // 🚀 综合评分算法
        int finalScore = baseScore * 0.4 + contextScore * 0.2 +
                        relationshipScore * 0.3 + scopeScore * 0.1;

        results.append(qMakePair(completion, finalScore));
    }

    // 🚀 按综合评分排序
    std::sort(results.begin(), results.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    // 限制结果数量
    if (results.size() > 20) {
        results = results.mid(0, 20);
    }

    return results;
}

QString CompletionManager::extractStructTypeFromContext(const QString &context)
{
    // 查找形如 "variable_name." 的模式
    QRegExp dotPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\.$");
    if (dotPattern.indexIn(context) != -1) {
        QString varName = dotPattern.cap(1);

        // 查找该变量的类型
        sym_list* symList = sym_list::getInstance();
        for (const auto &symbol : symList->getAllSymbols()) {
            if (symbol.symbolName == varName &&
                (symbol.symbolType == sym_list::sym_packed_struct_var ||
                 symbol.symbolType == sym_list::sym_unpacked_struct_var)) {
                return symbol.moduleScope;  // 返回结构体类型名称
            }
        }
    }

    return "";
}

QStringList CompletionManager::getContextAwareCompletions(const QString& prefix,
                                                         const QString& currentModule,
                                                         const QString& context)
{
    QStringList results;

    if (context.contains(".") || context.contains("->")) {
        QString structVarName = extractStructVariableFromContext(context);
        if (!structVarName.isEmpty()) {

            // 获取结构体变量的类型
            QString structTypeName = getStructTypeForVariable(structVarName, currentModule);
            if (!structTypeName.isEmpty()) {
                QStringList members = getStructMemberCompletions(prefix, structTypeName);
                results.append(members);

                // 结构体成员优先级最高，直接返回
                if (!results.isEmpty()) {
                    return results;
                }
            }
        }
    }

    // ========================================================================
    // 2. 枚举值补全 - 检测赋值上下文
    // ========================================================================
    if (context.contains("=") || context.contains("assign") ||
        context.contains("case") || context.contains("if")) {

        QString enumVarName = extractEnumVariableFromContext(context);
        if (!enumVarName.isEmpty()) {

            // 获取枚举变量的类型
            QString enumTypeName = getEnumTypeForVariable(enumVarName, currentModule);
            if (!enumTypeName.isEmpty()) {
                QStringList enumValues = getEnumValueCompletions(prefix, enumTypeName);
                results.append(enumValues);
            }
        }

        // 即使没有找到特定枚举变量，也显示所有匹配的枚举值
        if (results.isEmpty()) {
            results.append(getEnumValueCompletions(prefix, ""));
        }
    }

    // ========================================================================
    // 3. 模块端口补全 - 检测模块实例化上下文
    // ========================================================================
    if (context.contains("(") && (context.contains("module") ||
        context.contains("instantiation"))) {

        QString moduleTypeName = extractModuleTypeFromContext(context);
        if (!moduleTypeName.isEmpty()) {

            QStringList modulePorts = getModulePortCompletions(prefix, moduleTypeName);
            results.append(modulePorts);
        }
    }

    // ========================================================================
    // 4. 时钟域相关补全
    // ========================================================================
    if (context.contains("clk", Qt::CaseInsensitive) ||
        context.contains("clock", Qt::CaseInsensitive) ||
        context.contains("always_ff")) {
        QStringList clockSignals = getClockDomainCompletions(prefix);
        results.append(clockSignals);
    }

    // ========================================================================
    // 5. 复位信号相关补全
    // ========================================================================
    if (context.contains("rst", Qt::CaseInsensitive) ||
        context.contains("reset", Qt::CaseInsensitive) ||
        context.contains("negedge") || context.contains("posedge")) {

        QStringList resetSignals = getResetSignalCompletions(prefix);
        results.append(resetSignals);
    }

    // ========================================================================
    // 6. 基于当前模块的上下文补全
    // ========================================================================
    if (!currentModule.isEmpty()) {
        // 优先显示当前模块内的符号
        QStringList moduleSymbols = getModuleChildrenCompletions(currentModule, prefix);
        results.append(moduleSymbols);

        // 如果关系引擎可用，获取相关符号
        if (relationshipEngine) {
            QStringList relatedSymbols = getRelatedSymbolCompletions(currentModule, prefix);
            results.append(relatedSymbols);
        }
    }

    // ========================================================================
    // 7. 任务和函数补全
    // ========================================================================
    if (context.contains("task") || context.contains("function") ||
        context.contains("call")) {

        QStringList taskFunctions = getTaskFunctionCompletions(prefix);
        results.append(taskFunctions);
    }

    // ========================================================================
    // 8. 类型相关补全 - 根据上下文类型过滤
    // ========================================================================
    if (context.contains("typedef") || context.contains("type")) {
        // 类型定义上下文
        results.append(getGlobalSymbolsByType(sym_list::sym_typedef, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_enum, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_packed_struct, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_unpacked_struct, prefix));
    }

    // ========================================================================
    // 9. 变量声明上下文
    // ========================================================================
    if (context.contains("reg") || context.contains("wire") ||
        context.contains("logic") || context.contains("var")) {

        // 在变量声明上下文中，显示类型信息
        results.append(getSVKeywordCompletions(prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_enum, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_packed_struct, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_unpacked_struct, prefix));
    }

    // ========================================================================
    // 10. 默认补全 - 如果没有特定上下文
    // ========================================================================
    if (results.isEmpty() || context == "general" || context.isEmpty()) {

        // 显示最相关的符号类型
        if (!currentModule.isEmpty()) {
            // 模块内部符号
            results.append(getModuleInternalVariablesByType(currentModule,
                          sym_list::sym_reg, prefix));
            results.append(getModuleInternalVariablesByType(currentModule,
                          sym_list::sym_wire, prefix));
            results.append(getModuleInternalVariablesByType(currentModule,
                          sym_list::sym_logic, prefix));
        }

        // 全局符号
        results.append(getGlobalSymbolsByType(sym_list::sym_module, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_enum, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_packed_struct, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_task, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_function, prefix));

        // 系统关键字
        results.append(getSVKeywordCompletions(prefix));

    }

    // ========================================================================
    // 11. 结果后处理
    // ========================================================================

    // 去重并排序
    results.removeDuplicates();

    // 按匹配质量排序
    QVector<QPair<QString, int>> scoredResults;
    for (const QString& result : results) {
        int score = calculateMatchScore(result, prefix);

        // 上下文加分
        if (!context.isEmpty() && context != "general") {
            score += calculateContextScore(result, context);
        }

        // 当前模块加分
        if (!currentModule.isEmpty()) {
            score += calculateScopeScore(result, currentModule);
        }

        scoredResults.append(qMakePair(result, score));
    }

    // 排序
    std::sort(scoredResults.begin(), scoredResults.end(),
              [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
                  if (a.second != b.second) {
                      return a.second > b.second;  // 按分数降序
                  }
                  return a.first < b.first;        // 按字母序升序
              });

    // 提取排序后的结果
    QStringList finalResults;
    for (const auto& pair : scoredResults) {
        finalResults.append(pair.first);
    }

    // 限制结果数量
    if (finalResults.size() > 50) {
        finalResults = finalResults.mid(0, 50);
    }

    return finalResults;
}


// 从上下文中提取结构体变量名
QString CompletionManager::extractStructVariableFromContext(const QString& context)
{
    // 查找形如 "variable_name." 的模式
    QRegExp dotPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\.$");
    if (dotPattern.indexIn(context) != -1) {
        return dotPattern.cap(1);
    }

    // 查找形如 "variable_name->" 的模式 (如果支持指针操作)
    QRegExp arrowPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\s*->$");
    if (arrowPattern.indexIn(context) != -1) {
        return arrowPattern.cap(1);
    }

    return "";
}

// 从上下文中提取枚举变量名
QString CompletionManager::extractEnumVariableFromContext(const QString& context)
{
    // 查找赋值语句中的变量名
    QRegExp assignPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\s*=");
    if (assignPattern.indexIn(context) != -1) {
        return assignPattern.cap(1);
    }

    // 查找case语句中的变量名
    QRegExp casePattern("case\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\)");
    if (casePattern.indexIn(context) != -1) {
        return casePattern.cap(1);
    }

    // 查找if语句中的变量名
    QRegExp ifPattern("if\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*==");
    if (ifPattern.indexIn(context) != -1) {
        return ifPattern.cap(1);
    }

    return "";
}

// 从上下文中提取模块类型名
QString CompletionManager::extractModuleTypeFromContext(const QString& context)
{
    // 查找模块实例化模式
    QRegExp instPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\s+[a-zA-Z_][a-zA-Z0-9_]*\\s*\\(");
    if (instPattern.indexIn(context) != -1) {
        return instPattern.cap(1);
    }

    return "";
}

// 获取变量的结构体类型
QString CompletionManager::getStructTypeForVariable(const QString& varName,
                                                   const QString& currentModule)
{
    sym_list* symList = sym_list::getInstance();

    // 首先在当前模块中查找
    if (!currentModule.isEmpty()) {
        QList<sym_list::SymbolInfo> moduleSymbols =
            getModuleInternalSymbolsByType(currentModule, sym_list::sym_packed_struct_var, "");
        moduleSymbols.append(
            getModuleInternalSymbolsByType(currentModule, sym_list::sym_unpacked_struct_var, ""));

        for (const auto& symbol : moduleSymbols) {
            if (symbol.symbolName == varName) {
                return symbol.moduleScope;  // moduleScope存储了结构体类型名
            }
        }
    }

    // 在全局范围查找
    for (const auto& symbol : symList->getAllSymbols()) {
        if (symbol.symbolName == varName &&
            (symbol.symbolType == sym_list::sym_packed_struct_var ||
             symbol.symbolType == sym_list::sym_unpacked_struct_var)) {
            return symbol.moduleScope;
        }
    }

    return "";
}

bool CompletionManager::tryParseStructMemberContext(const QString &line,
                                                    QString &outVarName,
                                                    QString &outMemberPrefix)
{
    // 匹配行末的 "结构体变量.成员前缀" 或 "结构体变量."（成员前缀可为空，末尾可有空格）
    QRegExp re("([a-zA-Z_][a-zA-Z0-9_]*)\\.([a-zA-Z0-9_]*)\\s*$");
    if (re.indexIn(line) != -1) {
        outVarName = re.cap(1);
        outMemberPrefix = re.cap(2);
        return true;
    }
    return false;
}

// 获取变量的枚举类型
QString CompletionManager::getEnumTypeForVariable(const QString& varName,
                                                 const QString& currentModule)
{
    sym_list* symList = sym_list::getInstance();

    // 首先在当前模块中查找
    if (!currentModule.isEmpty()) {
        QList<sym_list::SymbolInfo> moduleSymbols =
            getModuleInternalSymbolsByType(currentModule, sym_list::sym_enum_var, "");

        for (const auto& symbol : moduleSymbols) {
            if (symbol.symbolName == varName) {
                return symbol.moduleScope;  // moduleScope存储了枚举类型名
            }
        }
    }

    // 在全局范围查找
    for (const auto& symbol : symList->getAllSymbols()) {
        if (symbol.symbolName == varName &&
            symbol.symbolType == sym_list::sym_enum_var) {
            return symbol.moduleScope;
        }
    }

    return "";
}

// 获取模块端口补全
QStringList CompletionManager::getModulePortCompletions(const QString& prefix,
                                                       const QString& moduleTypeName)
{
    QStringList results;

    if (!relationshipEngine || moduleTypeName.isEmpty()) {
        return results;
    }

    // 查找模块的端口信息
    sym_list* symList = sym_list::getInstance();

    // 找到模块定义
    for (const auto& symbol : symList->getAllSymbols()) {
        if (symbol.symbolType == sym_list::sym_module &&
            symbol.symbolName == moduleTypeName) {

            // 获取该模块内部的端口信息
            QStringList ports = getModuleInternalVariablesByType(moduleTypeName,
                               sym_list::sym_wire, prefix);
            ports.append(getModuleInternalVariablesByType(moduleTypeName,
                        sym_list::sym_reg, prefix));
            ports.append(getModuleInternalVariablesByType(moduleTypeName,
                        sym_list::sym_logic, prefix));

            results.append(ports);
            break;
        }
    }

    return results;
}



QStringList CompletionManager::getSVKeywordCompletions(const QString& prefix)
{
    static QStringList svKeywords = {
        "module", "endmodule", "input", "output", "inout",
        "wire", "reg", "logic", "bit", "byte", "shortint", "int", "longint",
        "always", "always_ff", "always_comb", "initial",
        "assign", "case", "casex", "casez", "default", "endcase",
        "if", "else", "for", "while", "repeat", "forever",
        "task", "function", "endtask", "endfunction",
        "typedef", "enum", "struct", "packed", "unpacked",
        "interface", "endinterface", "modport",
        "generate", "endgenerate", "genvar",
        "parameter", "localparam", "`define", "`include",
        "posedge", "negedge", "and", "or", "not", "xor"
    };

    QStringList results;

    for (const QString& keyword : svKeywords) {
        if (prefix.isEmpty() || matchesAbbreviation(keyword, prefix)) {
            results.append(keyword);
        }
    }

    return results;
}










QStringList CompletionManager::getBasicSymbolCompletions(const QString& prefix)
{
    // 🚀 只返回匹配前缀的符号，但限制数量
    QVector<QPair<QString, int>> scoredMatches = getScoredAllSymbolMatches(prefix);

    QStringList result;
    result.reserve(10);  // 限制为10个

    for (const auto &match : qAsConst(scoredMatches)) {
        result.append(match.first);
        if (result.size() >= 10) break;  // 最多10个
    }

    return result;
}

// 🚀 NEW: 获取模块子符号补全
QStringList CompletionManager::getModuleChildrenCompletions(const QString& moduleName, const QString& prefix)
{
    if (!relationshipEngine || moduleName.isEmpty()) {
        return QStringList();
    }

    // 检查缓存
    QString cacheKey = QString("module_children_%1_%2").arg(moduleName, prefix);
    if (relationshipCacheValid && moduleChildrenCache.contains(cacheKey)) {
        return moduleChildrenCache[cacheKey];
    }

    QStringList results;

    // 🚀 查找模块ID
    int moduleId = findSymbolIdByName(moduleName);
    if (moduleId != -1) {
        // 🚀 获取模块包含的所有符号ID
        QList<int> childrenIds = relationshipEngine->getModuleChildren(moduleId);

        // 🚀 转换为符号名称并过滤
        QStringList childrenNames = getSymbolNamesFromIds(childrenIds);

        for (const QString& childName : childrenNames) {
            if (prefix.isEmpty() || childName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(childName);
            }
        }
    }

    // 缓存结果
    moduleChildrenCache[cacheKey] = results;

    return results;
}

// 🚀 NEW: 获取相关符号补全
QStringList CompletionManager::getRelatedSymbolCompletions(const QString& symbolName, const QString& prefix)
{
    if (!relationshipEngine || symbolName.isEmpty()) {
        return QStringList();
    }

    QString cacheKey = QString("related_%1_%2").arg(symbolName, prefix);
    if (relationshipCacheValid && symbolRelationsCache.contains(cacheKey)) {
        return symbolRelationsCache[cacheKey];
    }

    QStringList results;

    int symbolId = findSymbolIdByName(symbolName);
    if (symbolId != -1) {
        // 🚀 获取各种关系的符号
        QList<int> referencedIds = relationshipEngine->getSymbolDependencies(symbolId);
        QList<int> referencingIds = relationshipEngine->getSymbolReferences(symbolId);

        // 🚀 合并所有相关符号
        QSet<int> allRelatedIds;
        for (int id : referencedIds) allRelatedIds.insert(id);
        for (int id : referencingIds) allRelatedIds.insert(id);

        // 🚀 转换为名称并过滤
        QStringList relatedNames = getSymbolNamesFromIds(allRelatedIds.toList());

        for (const QString& relatedName : relatedNames) {
            if (prefix.isEmpty() || relatedName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(relatedName);
            }
        }
    }

    symbolRelationsCache[cacheKey] = results;
    return results;
}

// 🚀 NEW: 获取符号引用补全
QStringList CompletionManager::getSymbolReferencesCompletions(const QString& symbolName, const QString& prefix)
{
    if (!relationshipEngine || symbolName.isEmpty()) {
        return QStringList();
    }

    QStringList results;

    int symbolId = findSymbolIdByName(symbolName);
    if (symbolId != -1) {
        QList<int> referencingIds = relationshipEngine->getSymbolReferences(symbolId);
        QStringList referencingNames = getSymbolNamesFromIds(referencingIds);

        for (const QString& refName : referencingNames) {
            if (prefix.isEmpty() || refName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(refName);
            }
        }
    }

    return results;
}

// 🚀 NEW: 获取时钟域补全
QStringList CompletionManager::getClockDomainCompletions(const QString& prefix)
{
    if (!relationshipEngine) {
        return QStringList();
    }

    QString cacheKey = QString("clock_domain_%1").arg(prefix);
    if (relationshipCacheValid && clockDomainCache.contains(cacheKey)) {
        return clockDomainCache[cacheKey];
    }

    QStringList results;

    // 🚀 查找所有时钟关系
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        // 🚀 检查是否有时钟关系
        QList<int> clockedModules = relationshipEngine->getRelatedSymbols(
            symbol.symbolId, SymbolRelationshipEngine::CLOCKS, true);

        if (!clockedModules.isEmpty()) {
            QString symbolName = symbol.symbolName;
            if (prefix.isEmpty() || symbolName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(symbolName);
            }
        }
    }

    clockDomainCache[cacheKey] = results;
    return results;
}

// 🚀 NEW: 获取复位信号补全
QStringList CompletionManager::getResetSignalCompletions(const QString& prefix)
{
    if (!relationshipEngine) {
        return QStringList();
    }

    QString cacheKey = QString("reset_signals_%1").arg(prefix);
    if (relationshipCacheValid && resetSignalCache.contains(cacheKey)) {
        return resetSignalCache[cacheKey];
    }

    QStringList results;

    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        // 🚀 检查是否有复位关系
        QList<int> resetModules = relationshipEngine->getRelatedSymbols(
            symbol.symbolId, SymbolRelationshipEngine::RESETS, true);

        if (!resetModules.isEmpty()) {
            QString symbolName = symbol.symbolName;
            if (prefix.isEmpty() || symbolName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(symbolName);
            }
        }
    }

    resetSignalCache[cacheKey] = results;
    return results;
}

// 🚀 NEW: 获取作用域内变量补全
QStringList CompletionManager::getVariableCompletionsInScope(const QString& moduleName,
                                                           sym_list::sym_type_e variableType,
                                                           const QString& prefix)
{
    QStringList results;

    if (moduleName.isEmpty()) {
        return getSymbolCompletions(variableType, prefix);
    }

    // 🚀 获取模块内指定类型的变量
    QStringList moduleChildren = getModuleChildrenCompletions(moduleName, prefix);

    sym_list* symbolList = sym_list::getInstance();
    for (const QString& childName : moduleChildren) {
        QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByName(childName);
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolType == variableType) {
                results.append(symbol.symbolName);
                break;
            }
        }
    }

    return results;
}

// 🚀 NEW: 获取Task/Function补全
QStringList CompletionManager::getTaskFunctionCompletions(const QString& prefix)
{
    QStringList results;

    QStringList tasks = getSymbolCompletions(sym_list::sym_task, prefix);
    QStringList functions = getSymbolCompletions(sym_list::sym_function, prefix);

    results.append(tasks);
    results.append(functions);

    return results;
}

// 🚀 NEW: 获取可实例化模块补全
QStringList CompletionManager::getInstantiableModules(const QString& prefix)
{
    return getSymbolCompletions(sym_list::sym_module, prefix);
}

// 🚀 NEW: 关系缓存管理
void CompletionManager::invalidateRelationshipCaches()
{
    moduleChildrenCache.clear();
    symbolRelationsCache.clear();
    clockDomainCache.clear();
    resetSignalCache.clear();
    symbolToModuleCache.clear();
    relationshipCacheValid = false;
}

void CompletionManager::refreshRelationshipData()
{
    if (relationshipEngine) {
        invalidateRelationshipCaches();
        updateRelationshipCaches();
    }
}

// 🚀 NEW: 辅助方法实现

QString CompletionManager::getCurrentModule(const QString& fileName, int cursorPosition)
{
    if (fileName.isEmpty() || cursorPosition < 0) {
        return QString();
    }

    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> fileSymbols = symbolList->findSymbolsByFileName(fileName);

    // 🚀 过滤出模块符号并按位置排序
    QList<sym_list::SymbolInfo> modules;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_list::sym_module) {
            modules.append(symbol);
        }
    }

    if (modules.isEmpty()) {
        return QString();
    }

    // 按开始位置排序
    std::sort(modules.begin(), modules.end(),
              [](const sym_list::SymbolInfo& a, const sym_list::SymbolInfo& b) {
                  return a.position < b.position;
              });

    // 🚀 需要更精确地检测模块边界
    // 这里需要找到 endmodule 的位置
    QString currentModuleName = findModuleAtPosition(modules, cursorPosition, fileName);

    return currentModuleName;
}
QStringList CompletionManager::getSymbolNamesFromIds(const QList<int>& symbolIds)
{
    QStringList names;
    names.reserve(symbolIds.size());

    sym_list* symbolList = sym_list::getInstance();
    for (int symbolId : symbolIds) {
        sym_list::SymbolInfo symbol = symbolList->getSymbolById(symbolId);
        if (symbol.symbolId != -1) {
            names.append(symbol.symbolName);
        }
    }

    return names;
}

QString CompletionManager::findModuleAtPosition(
    const QList<sym_list::SymbolInfo>& modules,
    int cursorPosition,
    const QString& fileName)
{
    // 🚀 读取文件内容以精确查找 endmodule 位置
    QFile file(fileName);
    QString fileContent;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fileContent = file.readAll();
    }

    for (const auto& module : modules) {
        if (cursorPosition >= module.position) {
            // 🔧 FIX: 查找对应的 endmodule 位置
            int moduleEndPosition = findEndModulePosition(fileContent, module);

            if (moduleEndPosition == -1) {
                // 使用下一个模块开始位置作为边界
                moduleEndPosition = getNextModulePosition(modules, module);
            }

            // 🔧 FIX: 检查光标是否在模块范围内
            if (cursorPosition < moduleEndPosition) {
                return module.symbolName;
            }
        }
    }

    return QString(); // 不在任何模块内
}

int CompletionManager::findSymbolIdByName(const QString& symbolName)
{
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByName(symbolName);

    if (!symbols.isEmpty()) {
        return symbols.first().symbolId;
    }

    return -1;
}

void CompletionManager::updateRelationshipCaches()
{
    if (relationshipCacheValid || !relationshipEngine) {
        return;
    }

    // 🚀 构建符号到模块的映射缓存
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (!symbol.moduleScope.isEmpty()) {
            symbolToModuleCache[symbol.symbolName] = symbol.moduleScope;
        }
    }

    relationshipCacheValid = true;
}

QStringList CompletionManager::filterCompletionsByContext(const QStringList& completions,
                                                        const QString& context)
{
    // 🚀 根据上下文过滤补全建议
    if (context == "assignment") {
        // 在赋值上下文中，优先显示变量
        QStringList filtered;
        sym_list* symbolList = sym_list::getInstance();

        for (const QString& completion : completions) {
            QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByName(completion);
            for (const sym_list::SymbolInfo& symbol : qAsConst(symbols)) {
                if (symbol.symbolType == sym_list::sym_reg ||
                    symbol.symbolType == sym_list::sym_wire ||
                    symbol.symbolType == sym_list::sym_logic) {
                    filtered.append(completion);
                    break;
                }
            }
        }

        return filtered;
    }

    return completions; // 默认不过滤
}

int CompletionManager::calculateContextScore(const QString& symbol, const QString& context)
{
    // 🚀 根据上下文计算额外评分
    if (context == "clock" && symbol.contains("clk", Qt::CaseInsensitive)) {
        return 50;
    }

    if (context == "reset" && symbol.contains(QRegExp("rst|reset", Qt::CaseInsensitive))) {
        return 50;
    }

    return 0;
}

int CompletionManager::calculateRelationshipScore(const QString& symbol, const QString& currentContext)
{
    if (!relationshipEngine || currentContext.isEmpty()) {
        return 0;
    }

    // 🚀 如果符号与当前上下文有关系，给予额外评分
    int symbolId = findSymbolIdByName(symbol);
    int contextId = findSymbolIdByName(currentContext);

    if (symbolId != -1 && contextId != -1) {
        // 检查各种关系类型
        if (relationshipEngine->hasRelationship(contextId, symbolId, SymbolRelationshipEngine::CONTAINS)) {
            return 40; // 包含关系评分最高
        }

        if (relationshipEngine->hasRelationship(symbolId, contextId, SymbolRelationshipEngine::REFERENCES) ||
            relationshipEngine->hasRelationship(contextId, symbolId, SymbolRelationshipEngine::REFERENCES)) {
            return 30; // 引用关系评分中等
        }

        if (relationshipEngine->hasRelationship(symbolId, contextId, SymbolRelationshipEngine::CALLS) ||
            relationshipEngine->hasRelationship(contextId, symbolId, SymbolRelationshipEngine::CALLS)) {
            return 25; // 调用关系评分
        }
    }

    return 0;
}

int CompletionManager::calculateScopeScore(const QString& symbol, const QString& currentModule)
{
    if (currentModule.isEmpty()) {
        return 0;
    }

    // 🚀 如果符号在当前模块作用域内，给予额外评分
    if (symbolToModuleCache.contains(symbol) &&
        symbolToModuleCache[symbol] == currentModule) {
        return 20;
    }

    return 0;
}

int CompletionManager::calculateUsageFrequencyScore(const QString& symbol)
{
    // 🚀 TODO: 实现基于使用频率的评分
    // 这需要统计符号的使用频率历史数据
    Q_UNUSED(symbol)
    return 0;
}

QStringList CompletionManager::getModuleInternalVariables(const QString& moduleName, const QString& prefix)
{
    if (moduleName.isEmpty()) {
        return QStringList();
    }

    QStringList results;
    sym_list* symbolList = sym_list::getInstance();

    // 🚀 方法1：通过 moduleScope 字段过滤
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    int logicCount = 0;
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (symbol.symbolType == sym_list::sym_logic) {
            logicCount++;
            if (logicCount >= 10) break; // 只显示前10个避免输出太多
        }
    }

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        // 检查是否属于指定模块且为内部变量类型
        if (symbol.moduleScope == moduleName &&
            isInternalVariableType(symbol.symbolType)) {

            // 前缀匹配
            if (prefix.isEmpty() ||
                symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(symbol.symbolName);
            }
        }
    }

    // 🚀 方法2：如果 moduleScope 字段为空，使用关系引擎
    if (results.isEmpty() && relationshipEngine) {
        int moduleId = findSymbolIdByName(moduleName);
        if (moduleId != -1) {
            QList<int> childrenIds = relationshipEngine->getModuleChildren(moduleId);

            for (int childId : childrenIds) {
                sym_list::SymbolInfo symbol = symbolList->getSymbolById(childId);
                if (symbol.symbolId != -1 && isInternalVariableType(symbol.symbolType)) {
                    if (prefix.isEmpty() ||
                        symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive)) {
                        results.append(symbol.symbolName);
                    }
                }
            }
        }
    }

    // 去重并排序
    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);

    return results;
}

// 🚀 判断是否为内部变量类型
bool CompletionManager::isInternalVariableType(sym_list::sym_type_e symbolType)
{
    return symbolType == sym_list::sym_reg ||
           symbolType == sym_list::sym_wire ||
           symbolType == sym_list::sym_logic ||
           symbolType == sym_list::sym_localparam ||
           symbolType == sym_list::sym_parameter;
}

QStringList CompletionManager::getGlobalSymbolCompletions(const QString& prefix)
{
    QStringList results;
    sym_list* symbolList = sym_list::getInstance();

    // 🚀 只返回模块声明、任务、函数等全局符号
    QList<sym_list::sym_type_e> globalTypes = {
        sym_list::sym_module,
        sym_list::sym_task,
        sym_list::sym_function,
        sym_list::sym_interface,
        sym_list::sym_package
    };

    for (sym_list::sym_type_e type : globalTypes) {
        QList<sym_list::SymbolInfo> symbols = symbolList->findSymbolsByType(type);

        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (prefix.isEmpty() ||
                symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(symbol.symbolName);
            }
        }
    }

    // 去重并排序
    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);

    // 限制数量避免过多
    if (results.size() > 15) {
        results = results.mid(0, 15);
    }

    return results;
}

QStringList CompletionManager::getModuleInternalVariablesByType(const QString& moduleName,
                                                               sym_list::sym_type_e symbolType,
                                                               const QString& prefix) {
    QStringList results;
    sym_list* symbolList = sym_list::getInstance();

    if (moduleName.isEmpty()) {
        return results;
    }

    // 🔧 FIX: 强化过滤逻辑，添加额外验证
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    int matchedCount = 0;
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        // 严格的过滤条件
        bool isCorrectModule = (symbol.moduleScope == moduleName);
        bool isCorrectType = (symbol.symbolType == symbolType);
        // 🚀 使用模糊匹配功能（支持前缀匹配、包含匹配和缩写匹配）
        bool matchesPrefix = (prefix.isEmpty() ||
                             matchesAbbreviation(symbol.symbolName, prefix));

        if (isCorrectModule && isCorrectType && matchesPrefix) {
            results.append(symbol.symbolName);
            matchedCount++;
        }
    }

    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);
    return results;
}

int CompletionManager::getNextModulePosition(const QList<sym_list::SymbolInfo>& modules,
                                            const sym_list::SymbolInfo& currentModule)
{
    for (int i = 0; i < modules.size(); ++i) {
        if (modules[i].symbolId == currentModule.symbolId && i < modules.size() - 1) {
            return modules[i + 1].position;
        }
    }
    return INT_MAX; // 如果是最后一个模块，返回最大值
}

int CompletionManager::findEndModulePosition(
    const QString& fileContent,
    const sym_list::SymbolInfo& moduleSymbol)
{
    int searchStart = moduleSymbol.position;
    int moduleDepth = 0;
    bool foundModule = false;

    QRegExp moduleStartPattern("\\bmodule\\s+");
    QRegExp moduleEndPattern("\\bendmodule\\b");

    int pos = searchStart;
    while (pos < fileContent.length()) {
        int nextModuleStart = moduleStartPattern.indexIn(fileContent, pos);
        int nextModuleEnd = moduleEndPattern.indexIn(fileContent, pos);

        if (nextModuleStart != -1 &&
            (nextModuleEnd == -1 || nextModuleStart < nextModuleEnd)) {
            // 处理嵌套模块
            if (foundModule || nextModuleStart == moduleSymbol.position) {
                moduleDepth++;
                foundModule = true;
            }
            pos = nextModuleStart + moduleStartPattern.matchedLength();
        } else if (nextModuleEnd != -1) {
            if (foundModule) {
                moduleDepth--;
                if (moduleDepth == 0) {
                    return nextModuleEnd + moduleEndPattern.matchedLength();
                }
            }
            pos = nextModuleEnd + moduleEndPattern.matchedLength();
        } else {
            break;
        }
    }

    return -1;
}

QStringList CompletionManager::getGlobalSymbolsByType(sym_list::sym_type_e symbolType,
                                                     const QString& prefix)
{
    QStringList results;
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    // 🔧 FIX: 全局符号类型定义
    QList<sym_list::sym_type_e> globalSymbolTypes = {
        sym_list::sym_module,
        sym_list::sym_task,
        sym_list::sym_function,
        sym_list::sym_interface,
        sym_list::sym_package,
        sym_list::sym_typedef,
        sym_list::sym_def_define,
        sym_list::sym_packed_struct,
        sym_list::sym_unpacked_struct
    };

    // 🔧 FIX: 检查是否为全局符号类型（struct类型也是全局的）
    if (!globalSymbolTypes.contains(symbolType) && 
        symbolType != sym_list::sym_packed_struct && 
        symbolType != sym_list::sym_unpacked_struct) {
        return results;
    }

    int foundCount = 0;
    int totalSymbolsOfType = 0;

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        // 🔧 FIX: 统计指定类型的所有符号
        if (symbol.symbolType == symbolType) {
            totalSymbolsOfType++;
        }

        // 🔧 FIX: 只返回指定类型的全局符号
        if (symbol.symbolType == symbolType) {
            // 🔧 FIX: 全局符号应该没有 moduleScope 或者 moduleScope 为空
            // 对于某些符号类型（如 module, interface），它们本身就是顶级声明
            bool isGlobalSymbol = false;

            if (symbolType == sym_list::sym_module ||
                symbolType == sym_list::sym_interface ||
                symbolType == sym_list::sym_package) {
                // 这些类型本身就是全局的
                isGlobalSymbol = true;
            } else {
                // 其他类型需要检查是否在模块外部声明
                isGlobalSymbol = symbol.moduleScope.isEmpty();
            }

            if (isGlobalSymbol) {
                // 🚀 使用模糊匹配功能（支持前缀匹配、包含匹配和缩写匹配）
                if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                    results.append(symbol.symbolName);
                    foundCount++;
                }
            }
        }
    }

    // 去重并排序
    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);


    return results;
}


QString CompletionManager::getSymbolTypeName(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
        case sym_list::sym_logic: return "logic";
        case sym_list::sym_reg: return "reg";
        case sym_list::sym_wire: return "wire";
        case sym_list::sym_localparam: return "localparam";
        case sym_list::sym_parameter: return "parameter";
        case sym_list::sym_module: return "module";
        case sym_list::sym_task: return "task";
        case sym_list::sym_function: return "function";
        case sym_list::sym_interface: return "interface";
        case sym_list::sym_interface_modport: return "interface_modport";
        case sym_list::sym_packed_struct: return "packed_struct";
        case sym_list::sym_unpacked_struct: return "unpacked_struct";
        case sym_list::sym_enum: return "enum";
        case sym_list::sym_typedef: return "typedef";
        case sym_list::sym_def_define: return "define";
        case sym_list::sym_def_ifdef: return "ifdef";
        case sym_list::sym_def_ifndef: return "ifndef";
        case sym_list::sym_always: return "always";
        case sym_list::sym_always_ff: return "always_ff";
        case sym_list::sym_always_comb: return "always_comb";
        case sym_list::sym_assign: return "assign";
        case sym_list::sym_xilinx_constraint: return "xilinx_constraint";
        case sym_list::sym_package: return "package";
        case sym_list::sym_user: return "user";
        default: return "unknown";
    }
}

bool CompletionManager::isSymbolTypeMatchCommand(sym_list::sym_type_e symbolType,
                                                sym_list::sym_type_e commandType)
{
    // 🚀 精确匹配：命令类型必须与符号类型完全一致
    return symbolType == commandType;
}

QString CompletionManager::getSymbolTypeString(sym_list::sym_type_e symbolType)
{
    switch (symbolType) {
    case sym_list::sym_reg:        return "reg";
    case sym_list::sym_wire:       return "wire";
    case sym_list::sym_logic:      return "logic";
    case sym_list::sym_module:     return "module";
    case sym_list::sym_task:       return "task";
    case sym_list::sym_function:   return "function";
    case sym_list::sym_always:     return "always";
    case sym_list::sym_assign:     return "assign";
    case sym_list::sym_typedef:    return "typedef";
    default:                       return QString("unknown_%1").arg(static_cast<int>(symbolType));
    }
}

void CompletionManager::invalidateCommandModeCache()
{
    commandModeCache.clear();
    commandModeCacheValid = false;
}

QList<sym_list::SymbolInfo> CompletionManager::getModuleInternalSymbolsByType(
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix)
{
    if (moduleName.isEmpty()) {
        return QList<sym_list::SymbolInfo>();
    }

    QList<sym_list::SymbolInfo> results;
    sym_list* symbolList = sym_list::getInstance();

    // 🚀 直接搜索并返回 SymbolInfo，避免字符串转换
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    // 找到模块符号以获取模块的行范围（用于struct变量的判断）
    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& sym : allSymbols) {
        if (sym.symbolType == sym_list::sym_module && sym.symbolName == moduleName) {
            moduleSymbol = sym;
            foundModule = true;
            break;
        }
    }

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        // 过滤条件
        bool isCorrectType = isSymbolTypeMatchCommand(symbol.symbolType, symbolType);
        bool isCorrectModule = false;
        
        // 对于struct类型，它们是全局的，在模块内查询时应该返回空
        if (symbolType == sym_list::sym_packed_struct || 
            symbolType == sym_list::sym_unpacked_struct) {
            continue; // struct类型应该在全局查询，不在模块内查询
        }
        // 对于struct变量，它们的moduleScope存储的是struct类型名，仅按“同文件且在模块起始行之后”纳入，
        // 不依赖 moduleEndLine，避免 endmodule 定位或缓存不一致导致 r_elec_level/r_elec_out 等被漏掉
        else if (symbolType == sym_list::sym_packed_struct_var || 
                 symbolType == sym_list::sym_unpacked_struct_var) {
            if (foundModule && symbol.fileName == moduleSymbol.fileName &&
                symbol.startLine > moduleSymbol.startLine) {
                isCorrectModule = true;
            }
        } else {
            // 对于其他类型，使用moduleScope判断
            isCorrectModule = (symbol.moduleScope == moduleName);
        }

        if (isCorrectModule && isCorrectType) {
            // 使用模糊匹配功能（支持前缀匹配、包含匹配和缩写匹配）
            if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                results.append(symbol);
            }
        }
    }

    // 🚀 如果 moduleScope 为空，使用关系引擎
    if (results.isEmpty() && relationshipEngine) {
        int moduleId = findSymbolIdByName(moduleName);
        if (moduleId != -1) {
            QList<int> childrenIds = relationshipEngine->getModuleChildren(moduleId);

            for (int childId : childrenIds) {
                sym_list::SymbolInfo symbol = symbolList->getSymbolById(childId);
                if (symbol.symbolId != -1 &&
                    isSymbolTypeMatchCommand(symbol.symbolType, symbolType)) {

                    if (prefix.isEmpty() ||
                        symbol.symbolName.startsWith(prefix, Qt::CaseInsensitive)) {
                        results.append(symbol);
                    }
                }
            }
        }
    }

    return results;
}

QList<sym_list::SymbolInfo> CompletionManager::getGlobalSymbolsByType_Info(sym_list::sym_type_e symbolType,
                                                                           const QString& prefix)
{
    QList<sym_list::SymbolInfo> results;
    sym_list* symbolList = sym_list::getInstance();
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();

    // 支持struct类型和struct变量
    QList<sym_list::sym_type_e> globalSymbolTypes = {
        sym_list::sym_module,
        sym_list::sym_task,
        sym_list::sym_function,
        sym_list::sym_interface,
        sym_list::sym_package,
        sym_list::sym_typedef,
        sym_list::sym_def_define,
        sym_list::sym_packed_struct,
        sym_list::sym_unpacked_struct,
        sym_list::sym_packed_struct_var,
        sym_list::sym_unpacked_struct_var
    };

    // 检查是否为支持的符号类型
    if (!globalSymbolTypes.contains(symbolType)) {
        return results;
    }

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (symbol.symbolType == symbolType) {
            bool isGlobalSymbol = false;

            if (symbolType == sym_list::sym_module ||
                symbolType == sym_list::sym_interface ||
                symbolType == sym_list::sym_package ||
                symbolType == sym_list::sym_packed_struct ||
                symbolType == sym_list::sym_unpacked_struct) {
                // 这些类型本身就是全局的
                isGlobalSymbol = true;
            } else if (symbolType == sym_list::sym_packed_struct_var ||
                       symbolType == sym_list::sym_unpacked_struct_var) {
                // struct变量：返回所有struct变量（不管在哪个模块内）
                // 因为用户在模块外输入时，应该能看到所有模块的struct变量
                isGlobalSymbol = true;
            } else {
                // 其他类型需要检查是否在模块外部声明
                isGlobalSymbol = symbol.moduleScope.isEmpty();
            }

            if (isGlobalSymbol) {
                // 使用模糊匹配功能
                if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                    results.append(symbol);
                }
            }
        }
    }

    return results;
}

// 获取枚举值补全
QStringList CompletionManager::getEnumValueCompletions(const QString& prefix,
                                                      const QString& enumTypeName)
{
    QStringList results;
    sym_list* symList = sym_list::getInstance();

    for (const auto& symbol : symList->getAllSymbols()) {
        if (symbol.symbolType == sym_list::sym_enum_value) {
            // 如果指定了枚举类型，只返回该类型的值
            if (!enumTypeName.isEmpty() && symbol.moduleScope != enumTypeName) {
                continue;
            }

            if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                results.append(symbol.symbolName);
            }
        }
    }

    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);
    return results;
}

// 获取结构体成员补全
QStringList CompletionManager::getStructMemberCompletions(const QString& prefix,
                                                         const QString& structTypeName)
{
    QStringList results;
    sym_list* symList = sym_list::getInstance();

    for (const auto& symbol : symList->getAllSymbols()) {
        if (symbol.symbolType == sym_list::sym_struct_member) {
            // 如果指定了结构体类型，只返回该类型的成员
            if (!structTypeName.isEmpty() && symbol.moduleScope != structTypeName) {
                continue;
            }

            if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                results.append(symbol.symbolName);
            }
        }
    }

    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);
    return results;
}

