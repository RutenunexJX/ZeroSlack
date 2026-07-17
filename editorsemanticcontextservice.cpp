#include "editorsemanticcontextservice.h"

#include "editorcompletionquery.h"
#include "editorsourcenavigationquery.h"
#include "definitionpreviewservice.h"
#include "symbolhoverservice.h"

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
    return EditorSourceNavigationQuery::sourceSymbolActionContext(context);
}

EditorSourceSymbolShortcutState
EditorSemanticContextService::sourceSymbolShortcutState(
    const EditorSourceSymbolShortcutContext& context) const
{
    return EditorSourceNavigationQuery::sourceSymbolShortcutState(context);
}

EditorSourceSymbolContextMenuState
EditorSemanticContextService::sourceSymbolContextMenuState(
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::sourceSymbolContextMenuState(context);
}

EditorSourceSymbolActionRequestState
EditorSemanticContextService::sourceSymbolActionRequestState(
    SourceSymbolAction action,
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::sourceSymbolActionRequestState(
        action,
        context);
}

SourceEditorNavigationTarget EditorSemanticContextService::sourceNavigationTarget(
    const EditorSemanticContext& context,
    const std::function<bool(const QString&)>& canResolveIdentifier) const
{
    return EditorSourceNavigationQuery::sourceNavigationTarget(
        context,
        canResolveIdentifier);
}

SourceEditorNavigationTarget
EditorSemanticContextService::definitionSourceNavigationTarget(
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::definitionSourceNavigationTarget(context);
}

EditorSourceNavigationTarget
EditorSemanticContextService::editorSourceNavigationTarget(
    const EditorSemanticContext& context,
    int blockPosition) const
{
    return EditorSourceNavigationQuery::editorSourceNavigationTarget(
        context,
        blockPosition);
}

EditorSourceNavigationClickState
EditorSemanticContextService::sourceNavigationClickState(
    const EditorSourceNavigationTarget& target) const
{
    return EditorSourceNavigationQuery::sourceNavigationClickState(target);
}

SourceIdentifierTarget EditorSemanticContextService::sourceIdentifierTarget(
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::sourceIdentifierTarget(context);
}

DefinitionNavigationQuery EditorSemanticContextService::definitionNavigationQuery(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::definitionNavigationQuery(
        symbolName,
        context);
}

DefinitionNavigationTarget EditorSemanticContextService::resolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::resolveDefinitionTarget(
        symbolName,
        context);
}

bool EditorSemanticContextService::canResolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::canResolveDefinitionTarget(
        symbolName,
        context);
}

QString EditorSemanticContextService::definitionTooltipText(
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    return EditorSourceNavigationQuery::definitionTooltipText(
        symbolName,
        context);
}

SymbolHoverReport EditorSemanticContextService::symbolHoverReport(
    const EditorSemanticContext& context) const
{
    return SymbolHoverService::getInstance()->hoverForContext(context);
}

DefinitionPreviewReport EditorSemanticContextService::definitionPreviewReport(
    const EditorSemanticContext& context) const
{
    return DefinitionPreviewService::getInstance()->previewForContext(context);
}

CompletionTriggerQuery EditorSemanticContextService::completionTriggerQuery(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::completionTriggerQuery(context);
}

CompletionTriggerState EditorSemanticContextService::completionTriggerState(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::completionTriggerState(context);
}

EditorCompletionTextChangeState
EditorSemanticContextService::completionTextChangeState(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::completionTextChangeState(context);
}

CompletionQuery EditorSemanticContextService::completionQuery(
    const QString& prefix,
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::completionQuery(prefix, context);
}

QStringList EditorSemanticContextService::completionNames(
    const QString& prefix,
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::completionNames(prefix, context);
}

CommandModeCompletionQuery
EditorSemanticContextService::commandModeCompletionQuery(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::commandModeCompletionQuery(context);
}

CommandModeCompletionState EditorSemanticContextService::commandModeCompletionState(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::commandModeCompletionState(context);
}

CommandModeInputState EditorSemanticContextService::commandModeInputState(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::commandModeInputState(context);
}

CommandModeMatch EditorSemanticContextService::commandModeMatch(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::commandModeMatch(context);
}

EditorCompletionQuery EditorSemanticContextService::editorCompletionQuery(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::editorCompletionQuery(context);
}

EditorCompletionState EditorSemanticContextService::editorCompletionState(
    const EditorSemanticContext& context) const
{
    return EditorCompletionQueryHelper::editorCompletionState(context);
}

CompletionActivationState EditorSemanticContextService::completionActivationState(
    const EditorCompletionActivationContext& context) const
{
    return EditorCompletionQueryHelper::completionActivationState(context);
}

CompletionActivationState EditorSemanticContextService::completionActivationState(
    const CompletionActivationQuery& query) const
{
    return EditorCompletionQueryHelper::completionActivationState(query);
}

CompletionPopupKeyState EditorSemanticContextService::completionPopupKeyState(
    const EditorCompletionPopupKeyContext& context) const
{
    return EditorCompletionQueryHelper::completionPopupKeyState(context);
}

CompletionPopupKeyState EditorSemanticContextService::completionPopupKeyState(
    const CompletionPopupKeyQuery& query) const
{
    return EditorCompletionQueryHelper::completionPopupKeyState(query);
}
