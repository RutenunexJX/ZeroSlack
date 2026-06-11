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

SourceEditorNavigationTarget EditorSemanticContextService::sourceNavigationTarget(
    const EditorSemanticContext& context,
    const std::function<bool(const QString&)>& canResolveIdentifier) const
{
    return SourceNavigationService::getInstance()->editorNavigationTargetAtColumn(
        context.lineText,
        context.column,
        canResolveIdentifier);
}

SourceEditorNavigationTarget
EditorSemanticContextService::definitionSourceNavigationTarget(
    const EditorSemanticContext& context) const
{
    return sourceNavigationTarget(
        context,
        [this, context](const QString& symbolName) {
            return canResolveDefinitionTarget(symbolName, context);
        });
}

SourceIdentifierTarget EditorSemanticContextService::sourceIdentifierTarget(
    const EditorSemanticContext& context) const
{
    return SourceNavigationService::getInstance()->identifierAtColumn(
        context.lineText,
        context.column);
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

DefinitionNavigationTarget EditorSemanticContextService::resolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return DefinitionNavigationService::getInstance()->resolveTarget(
        definitionNavigationQuery(symbolName, context));
}

bool EditorSemanticContextService::canResolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return DefinitionNavigationService::getInstance()->canResolveTarget(
        definitionNavigationQuery(symbolName, context));
}

QString EditorSemanticContextService::definitionTooltipText(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return DefinitionNavigationService::getInstance()->tooltipText(
        definitionNavigationQuery(symbolName, context));
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

CompletionTriggerState EditorSemanticContextService::completionTriggerState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->completionTriggerState(
        completionTriggerQuery(context));
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

QStringList EditorSemanticContextService::completionNames(
    const QString& prefix,
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()
        ->findCompletionResult(completionQuery(prefix, context))
        .names;
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

CommandModeCompletionState EditorSemanticContextService::commandModeCompletionState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->commandModeCompletionState(
        commandModeCompletionQuery(context));
}

CommandModeInputState EditorSemanticContextService::commandModeInputState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->commandModeInputState(
        context.lineUpToCursor);
}

CommandModeMatch EditorSemanticContextService::commandModeMatch(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->matchCommandMode(
        context.lineUpToCursor);
}

AlternateCommandCompletionState
EditorSemanticContextService::alternateCommandCompletionState(
    const QString& input) const
{
    return AlternateCommandService::getInstance()->completionState(input);
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

EditorCompletionState EditorSemanticContextService::editorCompletionState(
    const EditorSemanticContext& context) const
{
    return CompletionService::getInstance()->editorCompletionState(
        editorCompletionQuery(context));
}

CompletionActivationState EditorSemanticContextService::completionActivationState(
    const CompletionActivationQuery& query) const
{
    return CompletionService::getInstance()->completionActivationState(query);
}

CompletionPopupKeyState EditorSemanticContextService::completionPopupKeyState(
    const CompletionPopupKeyQuery& query) const
{
    return CompletionService::getInstance()->completionPopupKeyState(query);
}
