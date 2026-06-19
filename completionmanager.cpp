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

QStringList CompletionManager::getAllSymbolCompletions(const QString& prefix)
{
    return CompletionService::getInstance()->findAllSymbolCompletions(prefix);
}

QStringList CompletionManager::getSymbolCompletions(sym_list::sym_type_e symbolType, const QString& prefix)
{
    return CompletionService::getInstance()->findSymbolCompletionsByType(
        symbolType, prefix);
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

QVector<QPair<QString, int>> CompletionManager::getSmartCompletions(const QString& prefix,
                                                                  const QString& fileName,
                                                                  int cursorPosition)
{
    return CompletionService::getInstance()->findSmartCompletions(
        prefix,
        fileName,
        cursorPosition);
}

QStringList CompletionManager::getContextAwareCompletions(const QString& prefix,
                                                         const QString& currentModule,
                                                         const QString& context)
{
    ContextCompletionQuery query;
    query.prefix = prefix;
    query.currentModule = currentModule;
    query.context = context;
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


QStringList CompletionManager::getStructMemberCompletions(const QString& prefix,
                                                         const QString& structTypeName)
{
    return CompletionService::getInstance()->findStructMemberCompletions(
        prefix, structTypeName);
}

