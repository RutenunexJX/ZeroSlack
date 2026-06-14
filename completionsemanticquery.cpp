#include "completionsemanticquery.h"

#include "completionservice.h"
#include "symboltaxonomy.h"

QList<sym_list::SymbolInfo> CompletionSemanticQuery::commandSymbols(
    SemanticIndex* semanticIndex,
    const CommandCompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    const bool useSymbolInfoDirectly =
        SymbolTaxonomy::isDirectModuleContextCompletionRequest(query.symbolType);

    if (useSymbolInfoDirectly) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex->refreshStructTypedefEnumForFile(
            query.fileName, query.documentText);

        return semanticIndex->getModuleContextSymbolsByType(
            query.moduleName,
            query.fileName,
            query.symbolType,
            query.prefix);
    }

    return semanticIndex->getCommandCompletionSymbols(
        query.moduleName,
        query.symbolType,
        query.prefix);
}

QStringList CompletionSemanticQuery::enumValueCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& enumTypeName)
{
    return semanticIndex
        ? semanticIndex->getEnumValueCompletionNames(prefix, enumTypeName)
        : QStringList();
}

QString CompletionSemanticQuery::enumTypeForVariable(
    SemanticIndex* semanticIndex,
    const QString& variableName,
    const QString& moduleName)
{
    return semanticIndex
        ? semanticIndex->enumTypeForVariable(variableName, moduleName)
        : QString();
}

QStringList CompletionSemanticQuery::modulePortCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& moduleTypeName)
{
    return semanticIndex
        ? semanticIndex->getModulePortCompletionNames(prefix, moduleTypeName)
        : QStringList();
}

QList<sym_list::SymbolInfo>
CompletionSemanticQuery::moduleInternalSymbolInfosByType(
    SemanticIndex* semanticIndex,
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    bool useRelationshipFallback)
{
    return semanticIndex
        ? semanticIndex->getModuleInternalSymbolsByType(
              moduleName,
              symbolType,
              prefix,
              useRelationshipFallback)
        : QList<sym_list::SymbolInfo>();
}

QList<sym_list::SymbolInfo> CompletionSemanticQuery::moduleContextSymbolInfosByType(
    SemanticIndex* semanticIndex,
    const QString& moduleName,
    const QString& fileName,
    sym_list::sym_type_e symbolType,
    const QString& prefix)
{
    return semanticIndex
        ? semanticIndex->getModuleContextSymbolsByType(
              moduleName,
              fileName,
              symbolType,
              prefix)
        : QList<sym_list::SymbolInfo>();
}

QList<sym_list::SymbolInfo> CompletionSemanticQuery::globalSymbolInfosByType(
    SemanticIndex* semanticIndex,
    sym_list::sym_type_e symbolType,
    const QString& prefix)
{
    return semanticIndex
        ? semanticIndex->getGlobalSymbolInfosByType(symbolType, prefix)
        : QList<sym_list::SymbolInfo>();
}

QString CompletionSemanticQuery::currentModuleAt(
    SemanticIndex* semanticIndex,
    const QString& fileName,
    int cursorPosition)
{
    return semanticIndex
        ? semanticIndex->currentModuleAt(fileName, cursorPosition)
        : QString();
}

QString CompletionSemanticQuery::structTypeForVariable(
    SemanticIndex* semanticIndex,
    const QString& variableName,
    const QString& moduleName)
{
    return semanticIndex
        ? semanticIndex->getStructTypeForVariable(variableName, moduleName)
        : QString();
}
