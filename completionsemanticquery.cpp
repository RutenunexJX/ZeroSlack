#include "completionsemanticquery.h"

#include "completioncommandkindadapter.h"

QList<SemanticSymbolRecord> CompletionSemanticQuery::commandSymbolRecords(
    SemanticIndex* semanticIndex,
    const CommandCompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    const bool directModuleContext =
        completionCommandKindRequiresModuleContext(query.commandKind);

    if (directModuleContext && !query.moduleName.isEmpty()) {
        semanticIndex->refreshStructTypedefEnumForFile(
            query.fileName, query.documentText);

        return semanticIndex->getModuleContextSymbolRecordsByType(
            query.moduleName,
            query.fileName,
            query.commandKind,
            query.prefix);
    }
    if (directModuleContext && query.packageName.isEmpty())
        return {};

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    context.packageName = query.packageName;
    context.prefix = query.prefix;
    context.cursorLine = query.cursorLine;
    context.cursorPosition = query.cursorPosition;
    return semanticIndex->getCommandCompletionSymbolRecords(
        context,
        query.commandKind,
        query.prefix);
}
