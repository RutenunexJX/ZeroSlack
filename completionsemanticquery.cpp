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

    if (directModuleContext) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex->refreshStructTypedefEnumForFile(
            query.fileName, query.documentText);

        return semanticIndex->getModuleContextSymbolRecordsByType(
            query.moduleName,
            query.fileName,
            query.commandKind,
            query.prefix);
    }

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    context.prefix = query.prefix;
    context.cursorLine = query.cursorLine;
    context.cursorPosition = query.cursorPosition;
    return semanticIndex->getCommandCompletionSymbolRecords(
        context,
        query.commandKind,
        query.prefix);
}
