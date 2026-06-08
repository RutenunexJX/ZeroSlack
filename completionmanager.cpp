#include "completionmanager.h"
#include "relationshipservice.h"
#include "semanticindex.h"
#include "symbolrelationshipengine.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <utility>

std::unique_ptr<CompletionManager> CompletionManager::instance = nullptr;

CompletionManager::CompletionManager()
{
    keywordMatchCache.reserve(100);
    keywordScoreCache.reserve(100);
    symbolScoreCache.reserve(200);
    singleMatchCache.reserve(1000);
    singleScoreCache.reserve(1000);
    positionCache.reserve(500);
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

QList<sym_list::SymbolInfo> CompletionManager::getAllSemanticSymbols() const
{
    return SemanticIndex::getInstance()->getSymbols();
}

QList<sym_list::SymbolInfo> CompletionManager::getSemanticSymbolsByType(
    sym_list::sym_type_e symbolType) const
{
    return SemanticIndex::getInstance()->getSymbolsByType(symbolType);
}

QList<sym_list::SymbolInfo> CompletionManager::getSemanticSymbolsForFile(
    const QString& fileName) const
{
    return SemanticIndex::getInstance()->getSymbols(fileName);
}

QList<sym_list::SymbolInfo> CompletionManager::findSemanticDefinitions(
    const QString& symbolName) const
{
    return SemanticIndex::getInstance()->findDefinitions(symbolName);
}

sym_list::SymbolInfo CompletionManager::getSemanticSymbolById(int symbolId) const
{
    return SemanticIndex::getInstance()->getSymbolById(symbolId);
}

int CompletionManager::findSemanticSymbolId(const QString& symbolName) const
{
    return SemanticIndex::getInstance()->findSymbolId(symbolName);
}

QString CompletionManager::getSemanticCachedFileContent(const QString& fileName) const
{
    return SemanticIndex::getInstance()->getCachedFileContent(fileName);
}

QStringList CompletionManager::getSemanticScopeSymbolNames(
    const QString& fileName,
    int cursorLine) const
{
    return SemanticIndex::getInstance()->getScopeSymbolNames(fileName, cursorLine);
}

bool CompletionManager::isSemanticValidModuleName(const QString& moduleName) const
{
    return SemanticIndex::getInstance()->isValidModuleName(moduleName);
}

QVector<QPair<QString, int>> CompletionManager::getScoredAllSymbolMatches(const QString& prefix)
{
    QString cacheKey = QString("all_symbols_%1").arg(prefix);

    if (allSymbolScoreCache.contains(cacheKey) && allSymbolsCacheValid) {
        return allSymbolScoreCache[cacheKey];
    }

    updateAllSymbolsCache();

    QVector<QPair<QString, int>> scoredMatches;
    scoredMatches.reserve(qMin(cachedAllSymbolNames.size(), 50));

    for (const QString& symbolName : std::as_const(cachedAllSymbolNames)) {
        int score = calculateMatchScore(symbolName, prefix);
        if (score > 0) {
            scoredMatches.append(qMakePair(symbolName, score));
        }
    }

    std::sort(scoredMatches.begin(), scoredMatches.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    if (scoredMatches.size() > 20) {
        scoredMatches = scoredMatches.mid(0, 20);
    }

    allSymbolScoreCache[cacheKey] = scoredMatches;
    return scoredMatches;
}

QVector<QPair<sym_list::SymbolInfo, int>> CompletionManager::getScoredSymbolMatches(
    sym_list::sym_type_e symbolType, const QString& prefix)
{
    updateSymbolCaches();
    QString cacheKey = buildSymbolCacheKey(symbolType, prefix);

    if (symbolScoreCache.contains(cacheKey)) {
        return symbolScoreCache[cacheKey];
    }

    QList<sym_list::SymbolInfo> symbols = getSemanticSymbolsByType(symbolType);

    if (symbolType == sym_list::sym_enum) {
        QSet<int> seenIds;
        for (const sym_list::SymbolInfo &s : symbols)
            seenIds.insert(s.symbolId);
        QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();
        for (const sym_list::SymbolInfo &s : allSymbols) {
            if (s.symbolType == sym_list::sym_typedef && s.dataType == QLatin1String("enum") && !seenIds.contains(s.symbolId)) {
                seenIds.insert(s.symbolId);
                symbols.append(s);
            }
        }
    }

    QVector<QPair<sym_list::SymbolInfo, int>> scoredMatches;
    scoredMatches.reserve(qMin(symbols.size(), 30));

    for (const sym_list::SymbolInfo &symbol : std::as_const(symbols)) {
        int score = 0;

        if (prefix.isEmpty()) {
            score = 100;
        } else {
            const QString lowerText = symbol.symbolName.toLower();
            const QString lowerPrefix = prefix.toLower();

            if (lowerText == lowerPrefix) {
                score = 1000;
            } else if (lowerText.startsWith(lowerPrefix)) {
                score = 800 + (100 - prefix.length());
            } else if (lowerText.contains(lowerPrefix)) {
                score = 400 + (100 - symbol.symbolName.length());
            } else if (matchesAbbreviation(symbol.symbolName, prefix)) {
                score = 200;
            }
        }

        if (score > 0) {
            scoredMatches.append(qMakePair(symbol, score));
        }
    }

    std::sort(scoredMatches.begin(), scoredMatches.end(),
              [](const QPair<sym_list::SymbolInfo, int> &a, const QPair<sym_list::SymbolInfo, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first.symbolName < b.first.symbolName;
              });

    if (scoredMatches.size() > 15) {
        scoredMatches = scoredMatches.mid(0, 15);
    }

    symbolScoreCache[cacheKey] = scoredMatches;
    return scoredMatches;
}

void CompletionManager::forceRefreshSymbolCaches()
{
    if (shouldSkipCacheRefresh()) {
        return;
    }

    lastSymbolDatabaseSize = -1;
    lastSymbolDatabaseHash.clear();
    invalidateSymbolCaches();
    updateSymbolCaches();
    updateAllSymbolsCache();

    if (smartCachingEnabled) {
        precomputeFrequentCompletions();
    }
}

void CompletionManager::updateAllSymbolsCache()
{
    if (allSymbolsCacheValid) return;

    QSet<QString> uniqueNames;
    const QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (!symbol.symbolName.isEmpty())
            uniqueNames.insert(symbol.symbolName);
    }

    cachedAllSymbolNames = QStringList(uniqueNames.begin(), uniqueNames.end());
    cachedAllSymbolNames.sort();

    allSymbolScoreCache.clear();
    allSymbolMatchCache.clear();

    allSymbolsCacheValid = true;
}

void CompletionManager::precomputeFrequentCompletions()
{
    QList<sym_list::sym_type_e> commonTypes = {
        sym_list::sym_reg, sym_list::sym_wire, sym_list::sym_logic,
        sym_list::sym_module, sym_list::sym_task, sym_list::sym_function
    };

    for (sym_list::sym_type_e symbolType : commonTypes) {
        QStringList names;
        const QList<sym_list::SymbolInfo> symbols = getSemanticSymbolsByType(symbolType);
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (!symbol.symbolName.isEmpty())
                names.append(symbol.symbolName);
        }
        precomputedCompletions[symbolType] = names;
    }

    QStringList commonPrefixes = {"c", "d", "e", "m", "r", "s", "t", "v", "w"};

    for (const QString& prefix : commonPrefixes) {
        QStringList matches;
        for (const QString& name : std::as_const(cachedAllSymbolNames)) {
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

bool CompletionManager::shouldSkipCacheRefresh()
{
    if (!smartCachingEnabled) return false;

    int currentSize = getAllSemanticSymbols().size();
    QString currentHash = calculateSymbolDatabaseHash();

    bool sizeUnchanged = (currentSize == lastSymbolDatabaseSize);
    bool contentUnchanged = (!lastSymbolDatabaseHash.isEmpty() && currentHash == lastSymbolDatabaseHash);

    if (sizeUnchanged && contentUnchanged) {
        return true;
    }

    lastSymbolDatabaseSize = currentSize;
    lastSymbolDatabaseHash = currentHash;

    return false;
}

QString CompletionManager::calculateSymbolDatabaseHash()
{
    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

    QString hashInput = QString::number(allSymbols.size());

    int sampleSize = qMin(10, allSymbols.size());
    for (int i = 0; i < sampleSize; ++i) {
        hashInput += allSymbols[i].symbolName;
    }

    return QString::number(qHash(hashInput));
}

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

    if (allSymbolMatchCache.contains(cacheKey) && allSymbolsCacheValid) {
        return allSymbolMatchCache[cacheKey];
    }

    if (precomputedDataValid && prefix.length() == 1 && precomputedPrefixMatches.contains(prefix)) {
        QStringList result = precomputedPrefixMatches[prefix];
        if (result.size() > 15) {
            result = result.mid(0, 15);
        }

        allSymbolMatchCache[cacheKey] = result;
        return result;
    }

    QVector<QPair<QString, int>> scoredMatches = getScoredAllSymbolMatches(prefix);

    QStringList result;
    result.reserve(scoredMatches.size());
    for (const auto &match : std::as_const(scoredMatches)) {
        result.append(match.first);
    }

    if (result.size() > 15) {
        result = result.mid(0, 15);
    }

    allSymbolMatchCache[cacheKey] = result;

    return result;
}

QStringList CompletionManager::getSymbolCompletions(sym_list::sym_type_e symbolType, const QString& prefix)
{
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

    for (const auto &match : std::as_const(scoredMatches)) {
        if (!result.contains(match.first.symbolName)) {
            result.append(match.first.symbolName);
        }
    }

    if (result.size() > 15) {
        result = result.mid(0, 15);
    }

    return result;
}

void CompletionManager::invalidateAllCaches()
{
    keywordMatchCache.clear();
    keywordScoreCache.clear();
    symbolTypeCache.clear();
    symbolScoreCache.clear();
    singleMatchCache.clear();
    singleScoreCache.clear();
    positionCache.clear();

    allSymbolScoreCache.clear();
    allSymbolMatchCache.clear();
    precomputedCompletions.clear();
    precomputedPrefixMatches.clear();

    moduleChildrenCache.clear();
    clockDomainCache.clear();
    resetSignalCache.clear();

    invalidateCommandModeCache();

    allSymbolsCacheValid = false;
    precomputedDataValid = false;
}

void CompletionManager::invalidateSymbolCaches()
{
    symbolTypeCache.clear();
    symbolScoreCache.clear();

    allSymbolScoreCache.clear();
    allSymbolMatchCache.clear();
    allSymbolsCacheValid = false;

    invalidateCommandModeCache();

    if (smartCachingEnabled) {
        int currentSize = getAllSemanticSymbols().size();

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
    int currentSize = getAllSemanticSymbols().size();

    bool shouldUpdate = (currentSize != lastSymbolDatabaseSize) || symbolTypeCache.isEmpty();

    if (shouldUpdate) {
        invalidateSymbolCaches();
        lastSymbolDatabaseSize = currentSize;

        symbolTypeCache[sym_list::sym_reg] = getSemanticSymbolsByType(sym_list::sym_reg);
        symbolTypeCache[sym_list::sym_wire] = getSemanticSymbolsByType(sym_list::sym_wire);
        symbolTypeCache[sym_list::sym_logic] = getSemanticSymbolsByType(sym_list::sym_logic);
        symbolTypeCache[sym_list::sym_module] = getSemanticSymbolsByType(sym_list::sym_module);
        symbolTypeCache[sym_list::sym_task] = getSemanticSymbolsByType(sym_list::sym_task);
        symbolTypeCache[sym_list::sym_function] = getSemanticSymbolsByType(sym_list::sym_function);

        allSymbolsCacheValid = false;
    }
}

bool CompletionManager::matchesAbbreviation(const QString &text, const QString &abbreviation)
{
    if (abbreviation.isEmpty() || text.isEmpty()) {
        return false;
    }

    QString cacheKey = buildSingleMatchKey(text, abbreviation);
    if (singleMatchCache.contains(cacheKey)) {
        return singleMatchCache[cacheKey];
    }

    const QString lowerText = text.toLower();
    const QString lowerAbbrev = abbreviation.toLower();

    if (lowerText.startsWith(lowerAbbrev)) {
        singleMatchCache[cacheKey] = true;
        return true;
    }

    bool result = isValidAbbreviationMatch(text, abbreviation);
    singleMatchCache[cacheKey] = result;
    return result;
}

int CompletionManager::calculateMatchScore(const QString &text, const QString &abbreviation)
{
    if (abbreviation.isEmpty() || text.isEmpty()) {
        return 0;
    }

    QString cacheKey = buildSingleMatchKey(text, abbreviation);
    if (singleScoreCache.contains(cacheKey)) {
        return singleScoreCache[cacheKey];
    }

    const QString lowerText = text.toLower();
    const QString lowerAbbrev = abbreviation.toLower();

    int score = 0;

    if (lowerText == lowerAbbrev) {
        score = 1000;
    }
    else if (lowerText.startsWith(lowerAbbrev)) {
        score = 800 + (100 - abbreviation.length());
    }
    else if (lowerText.contains(lowerAbbrev)) {
        score = 400 + (100 - text.length());
    }
    else if (isValidAbbreviationMatch(text, abbreviation)) {
        QList<int> positions = findAbbreviationPositions(text, abbreviation);
        score = 500;

        int wordBoundaryMatches = 0;
        for (int pos : std::as_const(positions)) {
            if (pos == 0 || lowerText[pos - 1] == '_' || lowerText[pos - 1] == ' ') {
                wordBoundaryMatches++;
            }
            if (pos > 0 && pos < lowerText.length()) {
                QChar prevChar = text[pos - 1];
                QChar currChar = text[pos];
                if (prevChar.isLower() && currChar.isUpper()) {
                    wordBoundaryMatches++;
                }
            }
        }

        score += wordBoundaryMatches * 50;
        score -= text.length();

        for (int i = 1; i < positions.size(); i++) {
            if (positions[i] == positions[i-1] + 1) {
                score += 10;
            }
        }
    }

    singleScoreCache[cacheKey] = score;
    return score;
}

bool CompletionManager::isValidAbbreviationMatch(const QString &text, const QString &abbreviation)
{
    if (abbreviation.length() > text.length()) {
        return false;
    }

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
            bool isSeparator = (text[textPos] == '_' || text[textPos] == ' ');

            if (textPos > 0) {
                QChar prevChar = text[textPos - 1];
                QChar currChar = text[textPos];
                if (prevChar.isLower() && currChar.isUpper()) {
                    isSeparator = true;
                }
            }

            if (isSeparator && textPos + 1 < text.length()) {
                QChar nextChar = lowerText[textPos + 1];
                if (abbrevChar == nextChar) {
                    textPos++;
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
                textPos++;
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
    initializeKeywords();

    QString cacheKey = buildKeywordCacheKey(prefix);

    if (keywordScoreCache.contains(cacheKey)) {
        return keywordScoreCache[cacheKey];
    }

    QVector<QPair<QString, int>> scoredMatches = calculateScoredMatches(svKeywords, prefix);

    keywordScoreCache[cacheKey] = scoredMatches;

    return scoredMatches;
}

QStringList CompletionManager::getKeywordCompletions(const QString& prefix)
{
    QString cacheKey = buildKeywordCacheKey(prefix);

    if (keywordMatchCache.contains(cacheKey)) {
        return keywordMatchCache[cacheKey];
    }

    QVector<QPair<QString, int>> scoredMatches = getScoredKeywordMatches(prefix);

    QStringList result;
    result.reserve(scoredMatches.size());
    for (const auto &match : std::as_const(scoredMatches)) {
        result.append(match.first);
    }

    if (result.size() > 10) {
        result = result.mid(0, 10);
    }

    keywordMatchCache[cacheKey] = result;

    return result;
}

QStringList CompletionManager::getAbbreviationMatches(const QStringList &candidates, const QString &abbreviation)
{
    QVector<QPair<QString, int>> scoredMatches = getScoredKeywordMatches(abbreviation);

    QStringList result;
    result.reserve(scoredMatches.size());

    for (const auto &match : std::as_const(scoredMatches)) {
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

    std::sort(scoredMatches.begin(), scoredMatches.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
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
    return lastSymbolDatabaseSize == getAllSemanticSymbols().size();
}

void CompletionManager::setSlangManager(SlangManager* slangManager)
{
    m_slangManager = slangManager;
}

void CompletionManager::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    relationshipEngine = engine;

    if (engine && !relationshipBuilder) {
        relationshipBuilder = SemanticIndex::getInstance()->createRelationshipBuilder(
            engine, m_slangManager, nullptr);
    }

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
        return getScoredAllSymbolMatches(prefix);
    }

    QString currentModule = getCurrentModule(fileName, cursorPosition);
    QString context = "general";

    QStringList contextCompletions = getContextAwareCompletions(prefix, currentModule, context);

    results.reserve(contextCompletions.size());

    for (const QString& completion : std::as_const(contextCompletions)) {
        int baseScore = calculateMatchScore(completion, prefix);
        int contextScore = calculateContextScore(completion, context);
        int relationshipScore = calculateRelationshipScore(completion, currentModule);
        int scopeScore = calculateScopeScore(completion, currentModule);

        int finalScore = baseScore * 0.4 + contextScore * 0.2 +
                        relationshipScore * 0.3 + scopeScore * 0.1;

        results.append(qMakePair(completion, finalScore));
    }

    std::sort(results.begin(), results.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    if (results.size() > 20) {
        results = results.mid(0, 20);
    }

    return results;
}

QString CompletionManager::extractStructTypeFromContext(const QString &context)
{
    static const QRegularExpression dotPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\.$");
    QRegularExpressionMatch m = dotPattern.match(context);
    if (m.hasMatch()) {
        QString varName = m.captured(1);

        const QList<sym_list::SymbolInfo> symbols = getAllSemanticSymbols();
        for (const auto &symbol : symbols) {
            if (symbol.symbolName == varName &&
                (symbol.symbolType == sym_list::sym_packed_struct_var ||
                 symbol.symbolType == sym_list::sym_unpacked_struct_var)) {
                if (!symbol.dataType.isEmpty())
                    return symbol.dataType;
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

            QString structTypeName = getStructTypeForVariable(structVarName, currentModule);
            if (!structTypeName.isEmpty()) {
                QStringList members = getStructMemberCompletions(prefix, structTypeName);
                results.append(members);

                if (!results.isEmpty()) {
                    return results;
                }
            }
        }
    }

    if (context.contains("=") || context.contains("assign") ||
        context.contains("case") || context.contains("if")) {

        QString enumVarName = extractEnumVariableFromContext(context);
        if (!enumVarName.isEmpty()) {

            QString enumTypeName = getEnumTypeForVariable(enumVarName, currentModule);
            if (!enumTypeName.isEmpty()) {
                QStringList enumValues = getEnumValueCompletions(prefix, enumTypeName);
                results.append(enumValues);
            }
        }

        if (results.isEmpty()) {
            results.append(getEnumValueCompletions(prefix, ""));
        }
    }

    if (context.contains("(") && (context.contains("module") ||
        context.contains("instantiation"))) {

        QString moduleTypeName = extractModuleTypeFromContext(context);
        if (!moduleTypeName.isEmpty()) {

            QStringList modulePorts = getModulePortCompletions(prefix, moduleTypeName);
            results.append(modulePorts);
        }
    }

    if (context.contains("clk", Qt::CaseInsensitive) ||
        context.contains("clock", Qt::CaseInsensitive) ||
        context.contains("always_ff")) {
        QStringList clockSignals = getClockDomainCompletions(prefix);
        results.append(clockSignals);
    }

    if (context.contains("rst", Qt::CaseInsensitive) ||
        context.contains("reset", Qt::CaseInsensitive) ||
        context.contains("negedge") || context.contains("posedge")) {

        QStringList resetSignals = getResetSignalCompletions(prefix);
        results.append(resetSignals);
    }

    if (!currentModule.isEmpty()) {
        QStringList moduleSymbols = getModuleChildrenCompletions(currentModule, prefix);
        results.append(moduleSymbols);

        if (relationshipEngine) {
            QStringList relatedSymbols = getRelatedSymbolCompletions(currentModule, prefix);
            results.append(relatedSymbols);
        }
    }

    if (context.contains("task") || context.contains("function") ||
        context.contains("call")) {

        QStringList taskFunctions = getTaskFunctionCompletions(prefix);
        results.append(taskFunctions);
    }

    if (context.contains("typedef") || context.contains("type")) {
        results.append(getGlobalSymbolsByType(sym_list::sym_typedef, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_enum, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_packed_struct, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_unpacked_struct, prefix));
    }

    if (context.contains("reg") || context.contains("wire") ||
        context.contains("logic") || context.contains("var")) {

        results.append(getSVKeywordCompletions(prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_enum, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_packed_struct, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_unpacked_struct, prefix));
    }

    if (results.isEmpty() || context == "general" || context.isEmpty()) {

        if (!currentModule.isEmpty()) {
            results.append(getModuleInternalVariablesByType(currentModule,
                          sym_list::sym_reg, prefix));
            results.append(getModuleInternalVariablesByType(currentModule,
                          sym_list::sym_wire, prefix));
            results.append(getModuleInternalVariablesByType(currentModule,
                          sym_list::sym_logic, prefix));
        }

        results.append(getGlobalSymbolsByType(sym_list::sym_module, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_enum, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_packed_struct, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_task, prefix));
        results.append(getGlobalSymbolsByType(sym_list::sym_function, prefix));

        results.append(getSVKeywordCompletions(prefix));

    }

    results.removeDuplicates();

    QVector<QPair<QString, int>> scoredResults;
    for (const QString& result : results) {
        int score = calculateMatchScore(result, prefix);

        if (!context.isEmpty() && context != "general") {
            score += calculateContextScore(result, context);
        }

        if (!currentModule.isEmpty()) {
            score += calculateScopeScore(result, currentModule);
        }

        scoredResults.append(qMakePair(result, score));
    }

    std::sort(scoredResults.begin(), scoredResults.end(),
              [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    QStringList finalResults;
    for (const auto& pair : scoredResults) {
        finalResults.append(pair.first);
    }

    if (finalResults.size() > 50) {
        finalResults = finalResults.mid(0, 50);
    }

    return finalResults;
}

QString CompletionManager::extractStructVariableFromContext(const QString& context)
{
    // Matches "var." and "var[0]." and "var[i][j]."; group 1 captures the variable name
    static const QRegularExpression dotPattern("([a-zA-Z_][a-zA-Z0-9_]*)(?:\\s*\\[[^\\]]*\\])*\\s*\\.$");
    QRegularExpressionMatch m = dotPattern.match(context);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    static const QRegularExpression arrowPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\s*->$");
    m = arrowPattern.match(context);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    return "";
}

QString CompletionManager::extractEnumVariableFromContext(const QString& context)
{
    static const QRegularExpression assignPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\s*=");
    QRegularExpressionMatch m = assignPattern.match(context);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    static const QRegularExpression casePattern("case\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\)");
    m = casePattern.match(context);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    static const QRegularExpression ifPattern("if\\s*\\(\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*==");
    m = ifPattern.match(context);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    return "";
}

QString CompletionManager::extractModuleTypeFromContext(const QString& context)
{
    static const QRegularExpression instPattern("([a-zA-Z_][a-zA-Z0-9_]*)\\s+[a-zA-Z_][a-zA-Z0-9_]*\\s*\\(");
    QRegularExpressionMatch m = instPattern.match(context);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    return "";
}

QString CompletionManager::getStructTypeForVariable(const QString& varName,
                                                   const QString& currentModule)
{
    QString result;
    if (!currentModule.isEmpty()) {
        QList<sym_list::SymbolInfo> moduleSymbols =
            getModuleInternalSymbolsByType(currentModule, sym_list::sym_packed_struct_var, "");
        moduleSymbols.append(
            getModuleInternalSymbolsByType(currentModule, sym_list::sym_unpacked_struct_var, ""));

        for (const auto& symbol : moduleSymbols) {
            if (symbol.symbolName == varName && !symbol.dataType.isEmpty()) {
                result = symbol.dataType;
                break;
            }
        }
    }

    if (result.isEmpty()) {
        const QList<sym_list::SymbolInfo> symbols = getAllSemanticSymbols();
        for (const auto& symbol : symbols) {
            if (symbol.symbolName == varName &&
                (symbol.symbolType == sym_list::sym_packed_struct_var ||
                 symbol.symbolType == sym_list::sym_unpacked_struct_var)) {
                if (!symbol.dataType.isEmpty()) {
                    result = symbol.dataType;
                    break;
                }
            }
        }
    }

    return result;
}

bool CompletionManager::tryParseStructMemberContext(const QString &line,
                                                    QString &outVarName,
                                                    QString &outMemberPrefix)
{
    static const QRegularExpression re("([a-zA-Z_][a-zA-Z0-9_]*)\\.([a-zA-Z0-9_]*)\\s*$");
    QRegularExpressionMatch m = re.match(line);
    if (m.hasMatch()) {
        outVarName = m.captured(1);
        outMemberPrefix = m.captured(2);
        return true;
    }
    return false;
}

QString CompletionManager::getEnumTypeForVariable(const QString& varName,
                                                 const QString& currentModule)
{
    if (!currentModule.isEmpty()) {
        QList<sym_list::SymbolInfo> moduleSymbols =
            getModuleInternalSymbolsByType(currentModule, sym_list::sym_enum_var, "");

        for (const auto& symbol : moduleSymbols) {
            if (symbol.symbolName == varName) {
                return symbol.moduleScope;
            }
        }
    }

    const QList<sym_list::SymbolInfo> symbols = getSemanticSymbolsByType(sym_list::sym_enum_var);
    for (const auto& symbol : symbols) {
        if (symbol.symbolName == varName) {
            return symbol.moduleScope;
        }
    }

    return "";
}

QStringList CompletionManager::getModulePortCompletions(const QString& prefix,
                                                       const QString& moduleTypeName)
{
    QStringList results;

    if (!relationshipEngine || moduleTypeName.isEmpty()) {
        return results;
    }

    const QList<sym_list::SymbolInfo> symbols = getSemanticSymbolsByType(sym_list::sym_module);
    for (const auto& symbol : symbols) {
        if (symbol.symbolName == moduleTypeName) {

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
    QVector<QPair<QString, int>> scoredMatches = getScoredAllSymbolMatches(prefix);

    QStringList result;
    result.reserve(10);

    for (const auto &match : std::as_const(scoredMatches)) {
        result.append(match.first);
        if (result.size() >= 10) break;
    }

    return result;
}

QStringList CompletionManager::getModuleChildrenCompletions(const QString& moduleName, const QString& prefix)
{
    if (!relationshipEngine || moduleName.isEmpty()) {
        return QStringList();
    }

    QString cacheKey = QString("module_children_%1_%2").arg(moduleName, prefix);
    if (relationshipCacheValid && moduleChildrenCache.contains(cacheKey)) {
        return moduleChildrenCache[cacheKey];
    }

    QStringList results;

    int moduleId = findSymbolIdByName(moduleName);
    if (moduleId != -1) {
        RelationshipQuery query;
        query.symbolId = moduleId;
        query.outgoing = true;
        query.types = {SymbolRelationshipEngine::CONTAINS};
        QList<int> childrenIds = RelationshipService::getInstance()->findRelatedSymbolIds(query);

        QStringList childrenNames = getSymbolNamesFromIds(childrenIds);

        for (const QString& childName : childrenNames) {
            if (prefix.isEmpty() || childName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(childName);
            }
        }
    }

    moduleChildrenCache[cacheKey] = results;

    return results;
}

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
        RelationshipQuery referencedQuery;
        referencedQuery.symbolId = symbolId;
        referencedQuery.outgoing = true;
        referencedQuery.types = {SymbolRelationshipEngine::REFERENCES};
        QList<int> referencedIds =
            RelationshipService::getInstance()->findRelatedSymbolIds(referencedQuery);

        RelationshipQuery referencingQuery = referencedQuery;
        referencingQuery.outgoing = false;
        QList<int> referencingIds =
            RelationshipService::getInstance()->findRelatedSymbolIds(referencingQuery);

        QSet<int> allRelatedIds;
        for (int id : referencedIds) allRelatedIds.insert(id);
        for (int id : referencingIds) allRelatedIds.insert(id);

        QStringList relatedNames = getSymbolNamesFromIds(QList<int>(allRelatedIds.begin(), allRelatedIds.end()));

        for (const QString& relatedName : relatedNames) {
            if (prefix.isEmpty() || relatedName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(relatedName);
            }
        }
    }

    symbolRelationsCache[cacheKey] = results;
    return results;
}

QStringList CompletionManager::getSymbolReferencesCompletions(const QString& symbolName, const QString& prefix)
{
    if (!relationshipEngine || symbolName.isEmpty()) {
        return QStringList();
    }

    QStringList results;

    int symbolId = findSymbolIdByName(symbolName);
    if (symbolId != -1) {
        RelationshipQuery query;
        query.symbolId = symbolId;
        query.outgoing = false;
        query.types = {SymbolRelationshipEngine::REFERENCES};
        QList<int> referencingIds = RelationshipService::getInstance()->findRelatedSymbolIds(query);
        QStringList referencingNames = getSymbolNamesFromIds(referencingIds);

        for (const QString& refName : referencingNames) {
            if (prefix.isEmpty() || refName.startsWith(prefix, Qt::CaseInsensitive)) {
                results.append(refName);
            }
        }
    }

    return results;
}

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

    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        RelationshipQuery query;
        query.symbolId = symbol.symbolId;
        query.outgoing = true;
        query.types = {SymbolRelationshipEngine::CLOCKS};
        QList<int> clockedModules = RelationshipService::getInstance()->findRelatedSymbolIds(query);

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

    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        RelationshipQuery query;
        query.symbolId = symbol.symbolId;
        query.outgoing = true;
        query.types = {SymbolRelationshipEngine::RESETS};
        QList<int> resetModules = RelationshipService::getInstance()->findRelatedSymbolIds(query);

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

QStringList CompletionManager::getVariableCompletionsInScope(const QString& moduleName,
                                                           sym_list::sym_type_e variableType,
                                                           const QString& prefix)
{
    QStringList results;

    if (moduleName.isEmpty()) {
        return getSymbolCompletions(variableType, prefix);
    }

    QStringList moduleChildren = getModuleChildrenCompletions(moduleName, prefix);

    for (const QString& childName : moduleChildren) {
        QList<sym_list::SymbolInfo> symbols = findSemanticDefinitions(childName);
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolType == variableType) {
                results.append(symbol.symbolName);
                break;
            }
        }
    }

    return results;
}

QStringList CompletionManager::getTaskFunctionCompletions(const QString& prefix)
{
    QStringList results;

    QStringList tasks = getSymbolCompletions(sym_list::sym_task, prefix);
    QStringList functions = getSymbolCompletions(sym_list::sym_function, prefix);

    results.append(tasks);
    results.append(functions);

    return results;
}

QStringList CompletionManager::getInstantiableModules(const QString& prefix)
{
    return getSymbolCompletions(sym_list::sym_module, prefix);
}

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

QString CompletionManager::getCurrentModule(const QString& fileName, int cursorPosition)
{
    if (fileName.isEmpty() || cursorPosition < 0) {
        return QString();
    }

    QList<sym_list::SymbolInfo> fileSymbols = getSemanticSymbolsForFile(fileName);

    QList<sym_list::SymbolInfo> modules;
    for (const sym_list::SymbolInfo& symbol : fileSymbols) {
        if (symbol.symbolType == sym_list::sym_module) {
            modules.append(symbol);
        }
    }

    if (modules.isEmpty()) {
        return QString();
    }

    std::sort(modules.begin(), modules.end(),
              [](const sym_list::SymbolInfo& a, const sym_list::SymbolInfo& b) {
                  return a.position < b.position;
              });

    QString content = getSemanticCachedFileContent(fileName);
    QString currentModuleName = findModuleAtPosition(modules, cursorPosition, fileName, content);

    return currentModuleName;
}

QStringList CompletionManager::getCompletions(const QString& prefix, const QString& cursorFile, int cursorLine)
{
    QStringList result;
    if (cursorFile.isEmpty()) return result;

    const QStringList scopeNames = getSemanticScopeSymbolNames(cursorFile, cursorLine);
    for (const QString& name : scopeNames) {
        if (prefix.isEmpty() || matchesAbbreviation(name, prefix))
            result.append(name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionManager::getSymbolNamesFromIds(const QList<int>& symbolIds)
{
    QStringList names;
    names.reserve(symbolIds.size());

    for (int symbolId : symbolIds) {
        sym_list::SymbolInfo symbol = getSemanticSymbolById(symbolId);
        if (symbol.symbolId != -1) {
            names.append(symbol.symbolName);
        }
    }

    return names;
}

QString CompletionManager::findModuleAtPosition(
    const QList<sym_list::SymbolInfo>& modules,
    int cursorPosition,
    const QString& fileName,
    const QString& fileContent)
{
    QString content = fileContent;
    if (content.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            content = file.readAll();
        }
    }
    if (content.isEmpty()) {
        return QString();
    }

    // 0-based cursor line from position
    int cursorLine = 0;
    int pos = 0;
    while (pos < cursorPosition && pos < content.length()) {
        if (content[pos] == QLatin1Char('\n')) cursorLine++;
        pos++;
    }

    for (const auto& module : modules) {
        if (cursorPosition < module.position) continue;
        if (!isSemanticValidModuleName(module.symbolName)) continue;

        if (module.endLine > 0) {
            if (cursorLine >= module.startLine && cursorLine <= module.endLine)
                return module.symbolName;
        } else {
            int moduleEndPosition = findEndModulePosition(content, module);
            if (moduleEndPosition >= 0 && cursorPosition < moduleEndPosition)
                return module.symbolName;
        }
    }

    return QString();
}

int CompletionManager::findSymbolIdByName(const QString& symbolName)
{
    return findSemanticSymbolId(symbolName);
}

void CompletionManager::updateRelationshipCaches()
{
    if (relationshipCacheValid || !relationshipEngine)
        return;

    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

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
    if (context == "assignment") {
        QStringList filtered;
        for (const QString& completion : completions) {
            QList<sym_list::SymbolInfo> symbols = findSemanticDefinitions(completion);
            for (const sym_list::SymbolInfo& symbol : std::as_const(symbols)) {
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

    return completions;
}

int CompletionManager::calculateContextScore(const QString& symbol, const QString& context)
{
    if (context == "clock" && symbol.contains("clk", Qt::CaseInsensitive)) {
        return 50;
    }

    if (context == "reset" && symbol.contains(QRegularExpression("rst|reset", QRegularExpression::CaseInsensitiveOption))) {
        return 50;
    }

    return 0;
}

int CompletionManager::calculateRelationshipScore(const QString& symbol, const QString& currentContext)
{
    if (!relationshipEngine || currentContext.isEmpty()) {
        return 0;
    }

    int symbolId = findSymbolIdByName(symbol);
    int contextId = findSymbolIdByName(currentContext);

    if (symbolId != -1 && contextId != -1) {
        RelationshipService* relationships = RelationshipService::getInstance();
        if (relationships->hasRelationship(contextId, symbolId, SymbolRelationshipEngine::CONTAINS)) {
            return 40;
        }

        if (relationships->hasRelationship(symbolId, contextId, SymbolRelationshipEngine::REFERENCES) ||
            relationships->hasRelationship(contextId, symbolId, SymbolRelationshipEngine::REFERENCES)) {
            return 30;
        }

        if (relationships->hasRelationship(symbolId, contextId, SymbolRelationshipEngine::CALLS) ||
            relationships->hasRelationship(contextId, symbolId, SymbolRelationshipEngine::CALLS)) {
            return 25;
        }
    }

    return 0;
}

int CompletionManager::calculateScopeScore(const QString& symbol, const QString& currentModule)
{
    if (currentModule.isEmpty()) {
        return 0;
    }

    if (symbolToModuleCache.contains(symbol) &&
        symbolToModuleCache[symbol] == currentModule) {
        return 20;
    }

    return 0;
}

int CompletionManager::calculateUsageFrequencyScore(const QString& symbol)
{
    Q_UNUSED(symbol)
    return 0;
}

QStringList CompletionManager::getModuleInternalVariables(const QString& moduleName, const QString& prefix)
{
    if (moduleName.isEmpty()) {
        return QStringList();
    }

    QStringList results;
    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        if (symbol.moduleScope == moduleName &&
            isInternalVariableType(symbol.symbolType)) {

            if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix))
                results.append(symbol.symbolName);
        }
    }

    if (results.isEmpty() && relationshipEngine) {
        int moduleId = findSymbolIdByName(moduleName);
        if (moduleId != -1) {
            RelationshipQuery query;
            query.symbolId = moduleId;
            query.outgoing = true;
            query.types = {SymbolRelationshipEngine::CONTAINS};
            QList<int> childrenIds = RelationshipService::getInstance()->findRelatedSymbolIds(query);
            for (int childId : childrenIds) {
                sym_list::SymbolInfo symbol = getSemanticSymbolById(childId);
                if (symbol.symbolId != -1 && isInternalVariableType(symbol.symbolType)) {
                    if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix))
                        results.append(symbol.symbolName);
                }
            }
        }
    }

    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);
    return results;
}

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
    QList<sym_list::sym_type_e> globalTypes = {
        sym_list::sym_module,
        sym_list::sym_task,
        sym_list::sym_function,
        sym_list::sym_interface,
        sym_list::sym_package
    };

    for (sym_list::sym_type_e type : globalTypes) {
        QList<sym_list::SymbolInfo> symbols = getSemanticSymbolsByType(type);

        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix))
                results.append(symbol.symbolName);
        }
    }

    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);
    return results;
}

QStringList CompletionManager::getModuleInternalVariablesByType(const QString& moduleName,
                                                               sym_list::sym_type_e symbolType,
                                                               const QString& prefix) {
    QStringList results;
    if (moduleName.isEmpty()) {
        return results;
    }

    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

    int matchedCount = 0;
    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool isCorrectModule = (symbol.moduleScope == moduleName);
        bool isCorrectType = (symbol.symbolType == symbolType);
        if (symbolType == sym_list::sym_enum && !isCorrectType) {
            isCorrectType = (symbol.symbolType == sym_list::sym_typedef && symbol.dataType == QLatin1String("enum"));
        }
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
    return INT_MAX;
}

int CompletionManager::findEndModulePosition(
    const QString& fileContent,
    const sym_list::SymbolInfo& moduleSymbol)
{
    int searchStart = moduleSymbol.position;
    int moduleDepth = 0;
    bool foundModule = false;

    static const QRegularExpression moduleStartPattern("\\bmodule\\s+");
    static const QRegularExpression moduleEndPattern("\\bendmodule\\b");

    int pos = searchStart;
    while (pos < fileContent.length()) {
        QRegularExpressionMatch startMatch = moduleStartPattern.match(fileContent, pos);
        QRegularExpressionMatch endMatch = moduleEndPattern.match(fileContent, pos);
        int nextModuleStart = startMatch.hasMatch() ? startMatch.capturedStart(0) : -1;
        int nextModuleEnd = endMatch.hasMatch() ? endMatch.capturedStart(0) : -1;

        if (nextModuleStart != -1 &&
            (nextModuleEnd == -1 || nextModuleStart < nextModuleEnd)) {
            if (foundModule || nextModuleStart == moduleSymbol.position) {
                moduleDepth++;
                foundModule = true;
            }
            pos = nextModuleStart + startMatch.capturedLength(0);
        } else if (nextModuleEnd != -1) {
            if (foundModule) {
                moduleDepth--;
                if (moduleDepth == 0) {
                    return nextModuleEnd + endMatch.capturedLength(0);
                }
            }
            pos = nextModuleEnd + endMatch.capturedLength(0);
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
    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

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
        sym_list::sym_enum
    };

    if (!globalSymbolTypes.contains(symbolType) && 
        symbolType != sym_list::sym_packed_struct && 
        symbolType != sym_list::sym_unpacked_struct) {
        return results;
    }

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool typeMatches = (symbol.symbolType == symbolType);
        if (symbolType == sym_list::sym_enum && !typeMatches) {
            typeMatches = (symbol.symbolType == sym_list::sym_typedef && symbol.dataType == QLatin1String("enum"));
        }

        if (typeMatches) {
            bool isGlobalSymbol = false;

            if (symbolType == sym_list::sym_module ||
                symbolType == sym_list::sym_interface ||
                symbolType == sym_list::sym_package) {
                isGlobalSymbol = true;
            } else {
                isGlobalSymbol = symbol.moduleScope.isEmpty();
            }

            if (isGlobalSymbol) {
                if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                    results.append(symbol.symbolName);
                }
            }
        }
    }

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
        case sym_list::sym_always_latch: return "always_latch";
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
    case sym_list::sym_always_ff:  return "always_ff";
    case sym_list::sym_always_comb: return "always_comb";
    case sym_list::sym_always_latch: return "always_latch";
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
    const QString& prefix,
    bool useRelationshipFallback)
{
    if (moduleName.isEmpty()) {
        return QList<sym_list::SymbolInfo>();
    }

    QList<sym_list::SymbolInfo> results;
    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

    sym_list::SymbolInfo moduleSymbol;
    bool foundModule = false;
    for (const sym_list::SymbolInfo& sym : allSymbols) {
        if (sym.symbolType == sym_list::sym_module && sym.symbolName == moduleName) {
            moduleSymbol = sym;
            foundModule = true;
            break;
        }
    }

    int moduleEndLineExclusive = INT_MAX;
    if (foundModule) {
        QList<sym_list::SymbolInfo> fileModules;
        for (const sym_list::SymbolInfo& sym : allSymbols) {
            if (sym.symbolType == sym_list::sym_module && sym.fileName == moduleSymbol.fileName) {
                fileModules.append(sym);
            }
        }
        std::sort(fileModules.begin(), fileModules.end(),
                  [](const sym_list::SymbolInfo& a, const sym_list::SymbolInfo& b) {
                      return a.startLine < b.startLine;
                  });
        for (int i = 0; i < fileModules.size(); ++i) {
            if (fileModules[i].symbolId == moduleSymbol.symbolId && i + 1 < fileModules.size()) {
                moduleEndLineExclusive = fileModules[i + 1].startLine;
                break;
            }
        }
    }

    for (const sym_list::SymbolInfo& symbol : allSymbols) {
        bool isCorrectType = isSymbolTypeMatchCommand(symbol.symbolType, symbolType);
        bool isCorrectModule = false;

        if (symbolType == sym_list::sym_packed_struct ||
            symbolType == sym_list::sym_unpacked_struct) {
            if (foundModule && symbol.fileName == moduleSymbol.fileName &&
                symbol.startLine > moduleSymbol.startLine &&
                symbol.startLine < moduleEndLineExclusive) {
                isCorrectModule = true;
            }
        }
        else if (symbolType == sym_list::sym_packed_struct_var ||
                 symbolType == sym_list::sym_unpacked_struct_var) {
            if (foundModule && symbol.fileName == moduleSymbol.fileName &&
                symbol.startLine > moduleSymbol.startLine &&
                symbol.startLine < moduleEndLineExclusive) {
                isCorrectModule = true;
            }
        } else {
            isCorrectModule = (symbol.moduleScope == moduleName);
        }

        if (isCorrectModule && isCorrectType) {
            if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                results.append(symbol);
            }
        }
    }

    if (useRelationshipFallback && results.isEmpty() && relationshipEngine) {
        int moduleId = findSymbolIdByName(moduleName);
        if (moduleId != -1) {
            RelationshipQuery query;
            query.symbolId = moduleId;
            query.outgoing = true;
            query.types = {SymbolRelationshipEngine::CONTAINS};
            QList<int> childrenIds = RelationshipService::getInstance()->findRelatedSymbolIds(query);

            for (int childId : childrenIds) {
                sym_list::SymbolInfo symbol = getSemanticSymbolById(childId);
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

QList<sym_list::SymbolInfo> CompletionManager::getModuleContextSymbolsByType(
    const QString& moduleName,
    const QString& fileName,
    sym_list::sym_type_e symbolType,
    const QString& prefix)
{
    QList<sym_list::SymbolInfo> results;
    if (moduleName.isEmpty() || fileName.isEmpty()) {
        return results;
    }

    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

    results = getModuleInternalSymbolsByType(moduleName, symbolType, prefix);

    sym_list::SymbolInfo moduleSymbol;
    int moduleStartLine = 0;
    int moduleEndLineExclusive = INT_MAX;
    for (const sym_list::SymbolInfo& sym : allSymbols) {
        if (sym.symbolType == sym_list::sym_module && sym.symbolName == moduleName && sym.fileName == fileName) {
            moduleSymbol = sym;
            moduleStartLine = sym.startLine;
            break;
        }
    }
    for (const sym_list::SymbolInfo& sym : allSymbols) {
        if (sym.symbolType != sym_list::sym_module || sym.fileName != fileName) continue;
        if (sym.symbolId == moduleSymbol.symbolId) continue;
        if (sym.startLine > moduleStartLine) {
            moduleEndLineExclusive = sym.startLine;
            break;
        }
    }

    QString fileContent = getSemanticCachedFileContent(fileName);
    if (fileContent.isEmpty()) {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return results;
        }
        fileContent = QString::fromUtf8(file.readAll());
        file.close();
    }

    QSet<int> seenIds;
    for (const sym_list::SymbolInfo& s : results) {
        seenIds.insert(s.symbolId);
    }

    QString baseDir = QFileInfo(fileName).absolutePath();

    static const QRegularExpression includeRegex("`include\\s+\"([^\"]+)\"");
    int lineNum = 1;
    int lineStart = 0;
    while (lineStart < fileContent.length()) {
        int lineEnd = fileContent.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = fileContent.length();
        if (lineNum >= moduleStartLine && lineNum < moduleEndLineExclusive) {
            QString line = fileContent.mid(lineStart, lineEnd - lineStart);
            QRegularExpressionMatch m = includeRegex.match(line);
            if (m.hasMatch()) {
                QString incPath = m.captured(1).trimmed();
                QString absPath = QDir(baseDir).absoluteFilePath(incPath);
                QList<sym_list::SymbolInfo> incSymbols = getSemanticSymbolsForFile(absPath);
                for (const sym_list::SymbolInfo& s : incSymbols) {
                    if (!isSymbolTypeMatchCommand(s.symbolType, symbolType)) continue;
                    if (!prefix.isEmpty() && !matchesAbbreviation(s.symbolName, prefix)) continue;
                    if (seenIds.contains(s.symbolId)) continue;
                    seenIds.insert(s.symbolId);
                    results.append(s);
                }
            }
        }
        lineNum++;
        lineStart = (lineEnd < fileContent.length()) ? lineEnd + 1 : fileContent.length();
    }

    static const QRegularExpression importStarRegex("import\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*::\\s*\\*\\s*;");
    static const QRegularExpression importSymRegex("import\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*::\\s*([a-zA-Z_][a-zA-Z0-9_]*)\\s*;");
    lineNum = 1;
    lineStart = 0;
    QSet<QString> packagesStar;
    QHash<QString, QSet<QString>> packagesSyms;
    while (lineStart < fileContent.length()) {
        int lineEnd = fileContent.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = fileContent.length();
        if (lineNum >= moduleStartLine && lineNum < moduleEndLineExclusive) {
            QString line = fileContent.mid(lineStart, lineEnd - lineStart);
            QRegularExpressionMatch mStar = importStarRegex.match(line);
            if (mStar.hasMatch()) {
                packagesStar.insert(mStar.captured(1).trimmed());
            } else {
                QRegularExpressionMatch mSym = importSymRegex.match(line);
                if (mSym.hasMatch()) {
                    QString pkg = mSym.captured(1).trimmed();
                    QString symName = mSym.captured(2).trimmed();
                    packagesSyms[pkg].insert(symName);
                }
            }
        }
        lineNum++;
        lineStart = (lineEnd < fileContent.length()) ? lineEnd + 1 : fileContent.length();
    }
    for (const sym_list::SymbolInfo& s : allSymbols) {
        if (!isSymbolTypeMatchCommand(s.symbolType, symbolType)) continue;
        if (!prefix.isEmpty() && !matchesAbbreviation(s.symbolName, prefix)) continue;
        bool addFromPackage = false;
        if (packagesStar.contains(s.moduleScope)) {
            addFromPackage = true;
        } else {
            auto it = packagesSyms.find(s.moduleScope);
            if (it != packagesSyms.end() && it->contains(s.symbolName)) {
                addFromPackage = true;
            }
        }
        if (addFromPackage && !seenIds.contains(s.symbolId)) {
            seenIds.insert(s.symbolId);
            results.append(s);
        }
    }

    std::sort(results.begin(), results.end(), [](const sym_list::SymbolInfo& a, const sym_list::SymbolInfo& b) {
        if (a.symbolName != b.symbolName) return a.symbolName.compare(b.symbolName, Qt::CaseInsensitive) < 0;
        if (a.startLine != b.startLine) return a.startLine < b.startLine;
        return a.fileName < b.fileName;
    });
    return results;
}

QList<sym_list::SymbolInfo> CompletionManager::getGlobalSymbolsByType_Info(sym_list::sym_type_e symbolType,
                                                                           const QString& prefix)
{
    QList<sym_list::SymbolInfo> results;
    QList<sym_list::SymbolInfo> allSymbols = getAllSemanticSymbols();

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
                isGlobalSymbol = true;
            } else if (symbolType == sym_list::sym_packed_struct_var ||
                       symbolType == sym_list::sym_unpacked_struct_var) {
                isGlobalSymbol = symbol.moduleScope.isEmpty();
            } else {
                isGlobalSymbol = symbol.moduleScope.isEmpty();
            }

            if (isGlobalSymbol) {
                if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
                    results.append(symbol);
                }
            }
        }
    }

    return results;
}

QStringList CompletionManager::getEnumValueCompletions(const QString& prefix,
                                                      const QString& enumTypeName)
{
    QStringList results;

    const QList<sym_list::SymbolInfo> symbols =
        getSemanticSymbolsByType(sym_list::sym_enum_value);
    for (const auto& symbol : symbols) {
        if (!enumTypeName.isEmpty() && symbol.moduleScope != enumTypeName) {
            continue;
        }

        if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
            results.append(symbol.symbolName);
        }
    }

    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);
    return results;
}

QStringList CompletionManager::getStructMemberCompletions(const QString& prefix,
                                                         const QString& structTypeName)
{
    QStringList results;

    const QList<sym_list::SymbolInfo> symbols =
        getSemanticSymbolsByType(sym_list::sym_struct_member);
    for (const auto& symbol : symbols) {
        if (!structTypeName.isEmpty() && symbol.moduleScope != structTypeName) {
            continue;
        }

        if (prefix.isEmpty() || matchesAbbreviation(symbol.symbolName, prefix)) {
            results.append(symbol.symbolName);
        }
    }

    results.removeDuplicates();
    results.sort(Qt::CaseInsensitive);
    return results;
}

