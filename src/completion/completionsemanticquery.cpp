#include "completionsemanticquery.h"

#include "completioncommandkindadapter.h"

QList<SemanticSymbolRecord> CompletionSemanticQuery::commandSymbolRecords(
    SemanticIndex* semanticIndex,
    const CommandCompletionQuery& query)
{
    return commandSymbolRecordGroups(semanticIndex, query,
                                     {query.commandKind}).value(0);
}

QList<QList<SemanticSymbolRecord>> CompletionSemanticQuery::commandSymbolRecordGroups(
    SemanticIndex* semanticIndex,
    const CommandCompletionQuery& query,
    const QList<CompletionCommandKind>& commandKinds)
{
    QList<QList<SemanticSymbolRecord>> results(commandKinds.size());
    if (!semanticIndex || commandKinds.isEmpty())
        return results;

    QList<CompletionCommandKind> moduleKinds, scopedKinds;
    QList<qsizetype> modulePositions, scopedPositions;
    for (qsizetype i = 0; i < commandKinds.size(); ++i) {
        const bool directModuleContext =
            completionCommandKindRequiresModuleContext(commandKinds.at(i));
        if (directModuleContext && !query.moduleName.isEmpty()) {
            moduleKinds.append(commandKinds.at(i));
            modulePositions.append(i);
        } else if (!directModuleContext || !query.packageName.isEmpty()) {
            scopedKinds.append(commandKinds.at(i));
            scopedPositions.append(i);
        }
    }
    if (!moduleKinds.isEmpty()) {
        semanticIndex->refreshStructTypedefEnumForFile(
            query.fileName, query.documentText);
        const auto groups = semanticIndex->getModuleContextSymbolRecordGroups(
            query.moduleName, query.fileName, moduleKinds, query.prefix);
        for (qsizetype i = 0; i < modulePositions.size(); ++i)
            results[modulePositions.at(i)] = groups.at(i);
    }

    if (scopedKinds.isEmpty())
        return results;
    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    context.packageName = query.packageName;
    context.prefix = query.prefix;
    context.cursorLine = query.cursorLine;
    context.cursorPosition = query.cursorPosition;
    const auto groups = semanticIndex->getCommandCompletionSymbolRecordGroups(
        context, scopedKinds, query.prefix);
    for (qsizetype i = 0; i < scopedPositions.size(); ++i)
        results[scopedPositions.at(i)] = groups.at(i);
    return results;
}
