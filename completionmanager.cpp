#include "completionmanager.h"
#include "completionservice.h"

std::unique_ptr<CompletionManager> CompletionManager::instance = nullptr;

CompletionManager::CompletionManager()
{
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

QVector<QPair<QString, int>> CompletionManager::getScoredAllSymbolMatches(const QString& prefix)
{
    return CompletionService::getInstance()->findScoredAllSymbolCompletions(prefix);
}

QVector<QPair<sym_list::SymbolInfo, int>> CompletionManager::getScoredSymbolMatches(
    sym_list::sym_type_e symbolType, const QString& prefix)
{
    return CompletionService::getInstance()->findScoredSymbolCompletionsByType(
        symbolType, prefix);
}

void CompletionManager::forceRefreshSymbolCaches()
{
    invalidateSymbolCaches();
}

void CompletionManager::precomputeFrequentCompletions()
{
}

void CompletionManager::enableSmartCaching(bool enabled)
{
    smartCachingEnabled = enabled;

    if (enabled)
        precomputeFrequentCompletions();
}

QStringList CompletionManager::getAllSymbolCompletions(const QString& prefix)
{
    return CompletionService::getInstance()->findAllSymbolCompletions(prefix);
}

QStringList CompletionManager::getSymbolCompletions(sym_list::sym_type_e symbolType, const QString& prefix)
{
    return CompletionService::getInstance()->findSymbolCompletionsByType(
        symbolType, prefix);
}

void CompletionManager::invalidateAllCaches()
{
    invalidateCommandModeCache();
}

void CompletionManager::invalidateSymbolCaches()
{
    invalidateCommandModeCache();
}

bool CompletionManager::matchesAbbreviation(const QString &text, const QString &abbreviation)
{
    return CompletionService::getInstance()->matchesCompletionAbbreviation(
        text, abbreviation);
}

int CompletionManager::calculateMatchScore(const QString &text, const QString &abbreviation)
{
    return CompletionService::getInstance()->calculateCompletionMatchScore(
        text, abbreviation);
}

QList<int> CompletionManager::findAbbreviationPositions(const QString &text, const QString &abbreviation)
{
    return CompletionService::getInstance()->findCompletionAbbreviationPositions(
        text, abbreviation);
}

QVector<QPair<QString, int>> CompletionManager::getScoredKeywordMatches(const QString& prefix)
{
    return CompletionService::getInstance()->findScoredKeywordCompletions(prefix);
}

QStringList CompletionManager::getKeywordCompletions(const QString& prefix)
{
    return CompletionService::getInstance()->findKeywordCompletions(prefix);
}

QStringList CompletionManager::getAbbreviationMatches(const QStringList &candidates, const QString &abbreviation)
{
    return CompletionService::getInstance()->findKeywordAbbreviationMatches(
        candidates, abbreviation);
}

void CompletionManager::invalidateKeywordCaches()
{
}

void CompletionManager::setSlangManager(SlangManager* slangManager)
{
    (void)slangManager;
}

void CompletionManager::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    relationshipEngine = engine;
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
    return CompletionService::getInstance()->findModuleChildCompletions(
        moduleName, prefix);
}

QStringList CompletionManager::getRelatedSymbolCompletions(const QString& symbolName, const QString& prefix)
{
    return CompletionService::getInstance()->findRelatedSymbolCompletions(
        symbolName, prefix);
}

QStringList CompletionManager::getSymbolReferencesCompletions(const QString& symbolName, const QString& prefix)
{
    return CompletionService::getInstance()->findSymbolReferenceCompletions(symbolName, prefix);
}

QStringList CompletionManager::getClockDomainCompletions(const QString& prefix)
{
    return CompletionService::getInstance()->findClockDomainCompletions(prefix);
}

QStringList CompletionManager::getResetSignalCompletions(const QString& prefix)
{
    return CompletionService::getInstance()->findResetSignalCompletions(prefix);
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
}

void CompletionManager::refreshRelationshipData()
{
    invalidateRelationshipCaches();
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


void CompletionManager::invalidateCommandModeCache()
{
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

