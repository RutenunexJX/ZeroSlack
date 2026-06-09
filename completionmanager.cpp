#include "completionmanager.h"
#include "completionservice.h"
#include "searchservice.h"
#include "semanticindex.h"
#include "symbolrelationshipengine.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"

#include <QDateTime>
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
    precomputedCompletions.reserve(20);
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
    SearchQuery query;

    QList<sym_list::SymbolInfo> symbols;
    const QList<SearchResult> results = SearchService::getInstance()->findSymbols(query);
    symbols.reserve(results.size());
    for (const SearchResult& result : results)
        symbols.append(result.symbol);
    return symbols;
}

QList<sym_list::SymbolInfo> CompletionManager::getSemanticSymbolsByType(
    sym_list::sym_type_e symbolType) const
{
    SearchQuery query;
    query.types = {symbolType};

    QList<sym_list::SymbolInfo> symbols;
    const QList<SearchResult> results = SearchService::getInstance()->findSymbols(query);
    symbols.reserve(results.size());
    for (const SearchResult& result : results)
        symbols.append(result.symbol);
    return symbols;
}

QList<sym_list::SymbolInfo> CompletionManager::getSemanticSymbolsForCommandType(
    sym_list::sym_type_e symbolType) const
{
    QList<sym_list::SymbolInfo> symbols = getSemanticSymbolsByType(symbolType);
    if (symbolType != sym_list::sym_enum)
        return symbols;

    QSet<int> seenIds;
    for (const sym_list::SymbolInfo& symbol : std::as_const(symbols))
        seenIds.insert(symbol.symbolId);

    const QList<sym_list::SymbolInfo> typedefs =
        getSemanticSymbolsByType(sym_list::sym_typedef);
    for (const sym_list::SymbolInfo& symbol : typedefs) {
        if (symbol.dataType != QLatin1String("enum") || seenIds.contains(symbol.symbolId))
            continue;
        seenIds.insert(symbol.symbolId);
        symbols.append(symbol);
    }
    return symbols;
}

QVector<QPair<QString, int>> CompletionManager::getScoredAllSymbolMatches(const QString& prefix)
{
    return CompletionService::getInstance()->findScoredAllSymbolCompletions(prefix);
}

QVector<QPair<sym_list::SymbolInfo, int>> CompletionManager::getScoredSymbolMatches(
    sym_list::sym_type_e symbolType, const QString& prefix)
{
    updateSymbolCaches();
    QString cacheKey = buildSymbolCacheKey(symbolType, prefix);

    if (symbolScoreCache.contains(cacheKey)) {
        return symbolScoreCache[cacheKey];
    }

    QList<sym_list::SymbolInfo> symbols = getSemanticSymbolsForCommandType(symbolType);

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

    if (smartCachingEnabled) {
        precomputeFrequentCompletions();
    }
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
    return CompletionService::getInstance()->findAllSymbolCompletions(prefix);
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

    precomputedCompletions.clear();

    moduleChildrenCache.clear();
    clockDomainCache.clear();
    resetSignalCache.clear();

    invalidateCommandModeCache();

    precomputedDataValid = false;
}

void CompletionManager::invalidateSymbolCaches()
{
    symbolTypeCache.clear();
    symbolScoreCache.clear();

    invalidateCommandModeCache();

    if (smartCachingEnabled) {
        int currentSize = getAllSemanticSymbols().size();

        if (abs(currentSize - lastSymbolDatabaseSize) > cacheInvalidationThreshold) {
            precomputedCompletions.clear();
            precomputedDataValid = false;
        }
    } else {
        precomputedCompletions.clear();
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
    return CompletionService::getInstance()->findSmartCompletions(
        prefix,
        fileName,
        cursorPosition,
        relationshipEngine != nullptr);
}

QStringList CompletionManager::getContextAwareCompletions(const QString& prefix,
                                                         const QString& currentModule,
                                                         const QString& context)
{
    ContextCompletionQuery query;
    query.prefix = prefix;
    query.currentModule = currentModule;
    query.context = context;
    query.relationshipCompletionsEnabled = relationshipEngine != nullptr;
    return CompletionService::getInstance()->findContextAwareCompletions(query);
}

QString CompletionManager::getStructTypeForVariable(const QString& varName,
                                                   const QString& currentModule)
{
    return CompletionService::getInstance()->getStructTypeForVariable(
        varName, currentModule);
}

bool CompletionManager::tryParseStructMemberContext(const QString &line,
                                                    QString &outVarName,
                                                    QString &outMemberPrefix)
{
    return CompletionService::getInstance()->tryParseStructMemberContext(
        line, outVarName, outMemberPrefix);
}

QStringList CompletionManager::getModuleChildrenCompletions(const QString& moduleName, const QString& prefix)
{
    if (moduleName.isEmpty())
        return QStringList();

    QString cacheKey = QString("module_children_%1_%2").arg(moduleName, prefix);
    if (relationshipCacheValid && moduleChildrenCache.contains(cacheKey)) {
        return moduleChildrenCache[cacheKey];
    }

    const QStringList results =
        CompletionService::getInstance()->findModuleChildCompletions(moduleName, prefix);
    moduleChildrenCache[cacheKey] = results;
    return results;
}

QStringList CompletionManager::getRelatedSymbolCompletions(const QString& symbolName, const QString& prefix)
{
    if (symbolName.isEmpty())
        return QStringList();

    QString cacheKey = QString("related_%1_%2").arg(symbolName, prefix);
    if (relationshipCacheValid && symbolRelationsCache.contains(cacheKey)) {
        return symbolRelationsCache[cacheKey];
    }

    const QStringList results =
        CompletionService::getInstance()->findRelatedSymbolCompletions(symbolName, prefix);
    symbolRelationsCache[cacheKey] = results;
    return results;
}

QStringList CompletionManager::getSymbolReferencesCompletions(const QString& symbolName, const QString& prefix)
{
    return CompletionService::getInstance()->findSymbolReferenceCompletions(symbolName, prefix);
}

QStringList CompletionManager::getClockDomainCompletions(const QString& prefix)
{
    QString cacheKey = QString("clock_domain_%1").arg(prefix);
    if (relationshipCacheValid && clockDomainCache.contains(cacheKey)) {
        return clockDomainCache[cacheKey];
    }

    const QStringList results =
        CompletionService::getInstance()->findClockDomainCompletions(prefix);
    clockDomainCache[cacheKey] = results;
    return results;
}

QStringList CompletionManager::getResetSignalCompletions(const QString& prefix)
{
    QString cacheKey = QString("reset_signals_%1").arg(prefix);
    if (relationshipCacheValid && resetSignalCache.contains(cacheKey)) {
        return resetSignalCache[cacheKey];
    }

    const QStringList results =
        CompletionService::getInstance()->findResetSignalCompletions(prefix);
    resetSignalCache[cacheKey] = results;
    return results;
}

QStringList CompletionManager::getVariableCompletionsInScope(const QString& moduleName,
                                                           sym_list::sym_type_e variableType,
                                                           const QString& prefix)
{
    return CompletionService::getInstance()->findVariableCompletionsInScope(
        moduleName, variableType, prefix);
}

QStringList CompletionManager::getTaskFunctionCompletions(const QString& prefix)
{
    return CompletionService::getInstance()->findTaskFunctionCompletions(prefix);
}

QStringList CompletionManager::getInstantiableModules(const QString& prefix)
{
    return CompletionService::getInstance()->findInstantiableModuleCompletions(prefix);
}

void CompletionManager::invalidateRelationshipCaches()
{
    moduleChildrenCache.clear();
    symbolRelationsCache.clear();
    clockDomainCache.clear();
    resetSignalCache.clear();
    relationshipCacheValid = false;
}

void CompletionManager::refreshRelationshipData()
{
    if (relationshipEngine) {
        invalidateRelationshipCaches();
        relationshipCacheValid = true;
    }
}

QString CompletionManager::getCurrentModule(const QString& fileName, int cursorPosition)
{
    return CompletionService::getInstance()->currentModuleAt(fileName, cursorPosition);
}

QStringList CompletionManager::getCompletions(const QString& prefix, const QString& cursorFile, int cursorLine)
{
    CompletionQuery query;
    query.prefix = prefix;
    query.fileName = cursorFile;
    query.cursorLine = cursorLine;
    return CompletionService::getInstance()->findScopeCompletions(query);
}

QStringList CompletionManager::getModuleInternalVariables(const QString& moduleName, const QString& prefix)
{
    return CompletionService::getInstance()->findModuleInternalVariableCompletions(
        moduleName, prefix);
}

QStringList CompletionManager::getGlobalSymbolCompletions(const QString& prefix)
{
    return CompletionService::getInstance()->findGlobalSymbolCompletions(prefix);
}

QStringList CompletionManager::getModuleInternalVariablesByType(const QString& moduleName,
                                                               sym_list::sym_type_e symbolType,
                                                               const QString& prefix) {
    return CompletionService::getInstance()->findModuleSymbolsByType(
        moduleName, symbolType, prefix);
}

QStringList CompletionManager::getGlobalSymbolsByType(sym_list::sym_type_e symbolType,
                                                     const QString& prefix)
{
    return CompletionService::getInstance()->findGlobalSymbolsByType(symbolType, prefix);
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
    return CompletionService::getInstance()->findModuleInternalSymbolInfosByType(
        moduleName, symbolType, prefix, useRelationshipFallback);
}

QList<sym_list::SymbolInfo> CompletionManager::getModuleContextSymbolsByType(
    const QString& moduleName,
    const QString& fileName,
    sym_list::sym_type_e symbolType,
    const QString& prefix)
{
    return CompletionService::getInstance()->findModuleContextSymbolInfosByType(
        moduleName, fileName, symbolType, prefix);
}

QList<sym_list::SymbolInfo> CompletionManager::getGlobalSymbolsByType_Info(sym_list::sym_type_e symbolType,
                                                                           const QString& prefix)
{
    return CompletionService::getInstance()->findGlobalSymbolInfosByType(
        symbolType, prefix);
}

QStringList CompletionManager::getStructMemberCompletions(const QString& prefix,
                                                         const QString& structTypeName)
{
    return CompletionService::getInstance()->findStructMemberCompletions(
        prefix, structTypeName);
}

