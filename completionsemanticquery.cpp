#include "completionsemanticquery.h"

#include "completionservice.h"
#include "symboltaxonomy.h"

QList<SemanticSymbolRecord> CompletionSemanticQuery::commandSymbolRecords(
    SemanticIndex* semanticIndex,
    const CommandCompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    const bool directModuleContext =
        SymbolTaxonomy::isDirectModuleContextCompletionRequest(
            query.rawCollectorKind);

    if (directModuleContext) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex->refreshStructTypedefEnumForFile(
            query.fileName, query.documentText);

        return semanticSymbolRecordsForSymbols(
            semanticIndex->getModuleContextSymbolsByType(
                query.moduleName,
                query.fileName,
                query.rawCollectorKind,
                query.prefix));
    }

    return semanticIndex->getCommandCompletionSymbolRecords(
        query.moduleName,
        query.rawCollectorKind,
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
