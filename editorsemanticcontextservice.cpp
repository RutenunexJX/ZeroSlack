#include "editorsemanticcontextservice.h"

std::unique_ptr<EditorSemanticContextService>
    EditorSemanticContextService::instance = nullptr;

EditorSemanticContextService* EditorSemanticContextService::getInstance()
{
    if (!instance)
        instance = std::make_unique<EditorSemanticContextService>();
    return instance.get();
}

EditorSemanticContextService::EditorSemanticContextService() = default;

EditorSemanticContextService::~EditorSemanticContextService() = default;

SourceSymbolActionContext EditorSemanticContextService::sourceSymbolActionContext(
    const EditorSemanticContext& context) const
{
    return SourceNavigationService::getInstance()->symbolActionContextAtColumn(
        context.lineText,
        context.column,
        context.fileName,
        context.moduleName);
}

DefinitionNavigationQuery EditorSemanticContextService::definitionNavigationQuery(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    DefinitionNavigationContext navigationContext;
    navigationContext.symbolName = symbolName;
    navigationContext.fileName = context.fileName;
    navigationContext.moduleName = context.moduleName;
    navigationContext.lineText = context.lineText;
    navigationContext.column = context.column;
    return DefinitionNavigationService::getInstance()
        ->navigationQueryForContext(navigationContext);
}

CompletionTriggerQuery EditorSemanticContextService::completionTriggerQuery(
    const EditorSemanticContext& context) const
{
    CompletionTriggerQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.moduleName = context.moduleName;
    query.commandModeActive = context.commandModeActive;
    return query;
}

CompletionQuery EditorSemanticContextService::completionQuery(
    const QString& prefix,
    const EditorSemanticContext& context) const
{
    CompletionQuery query;
    query.prefix = prefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    return query;
}

CommandModeCompletionQuery
EditorSemanticContextService::commandModeCompletionQuery(
    const EditorSemanticContext& context) const
{
    CommandModeCompletionQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.documentText = context.documentText;
    return query;
}

EditorCompletionQuery EditorSemanticContextService::editorCompletionQuery(
    const EditorSemanticContext& context) const
{
    EditorCompletionQuery query;
    query.lineUpToCursor = context.lineUpToCursor;
    query.wordPrefix = context.wordPrefix;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.cursorLine = context.cursorLine;
    query.cursorPosition = context.cursorPosition;
    return query;
}
