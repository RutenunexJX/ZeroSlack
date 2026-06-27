#include "editorsourcenavigationquery.h"

#include "moduleblockdiagramservice.h"
#include "statetransitiontriggerservice.h"

#include <Qt>

namespace {
StateTransitionTriggerReport stateTransitionTriggerForContext(
    const SourceSymbolActionContext& actionContext)
{
    StateTransitionTriggerQuery query;
    query.symbolName = actionContext.symbolName;
    query.fileName = actionContext.fileName;
    query.moduleName = actionContext.moduleName;
    return StateTransitionTriggerService::getInstance()
        ->triggerForSymbol(query);
}

bool moduleBlockDiagramAvailableForContext(
    const SourceSymbolActionContext& actionContext)
{
    ModuleBlockDiagramQuery query;
    query.moduleName = actionContext.symbolName;
    query.fileName = actionContext.fileName;
    query.maxDepth = 0;
    return ModuleBlockDiagramService::getInstance()
        ->buildModuleBlockDiagram(query)
        .found;
}
}

SourceSymbolActionContext EditorSourceNavigationQuery::sourceSymbolActionContext(
    const EditorSemanticContext& context)
{
    return SourceNavigationService::getInstance()->symbolActionContextAtColumn(
        context.lineText,
        context.column,
        context.fileName,
        context.moduleName);
}

EditorSourceSymbolShortcutState
EditorSourceNavigationQuery::sourceSymbolShortcutState(
    const EditorSourceSymbolShortcutContext& context)
{
    EditorSourceSymbolShortcutState state;
    const Qt::KeyboardModifiers modifiers =
        Qt::KeyboardModifiers::fromInt(context.modifiers);

    if (context.key == Qt::Key_F12 && modifiers.testFlag(Qt::ShiftModifier)) {
        state.matched = true;
        state.acceptEvent = true;
        state.action = SourceSymbolAction::FindReferences;
        state.semanticContext = context.semanticContext;
        return state;
    }

    if (context.key == Qt::Key_R
        && modifiers.testFlag(Qt::ControlModifier)
        && modifiers.testFlag(Qt::ShiftModifier)) {
        state.matched = true;
        state.acceptEvent = true;
        state.action = SourceSymbolAction::ShowRelationships;
        state.semanticContext = context.semanticContext;
        return state;
    }

    return state;
}

EditorSourceSymbolContextMenuState
EditorSourceNavigationQuery::sourceSymbolContextMenuState(
    const EditorSemanticContext& context)
{
    const SourceSymbolActionContext actionContext =
        sourceSymbolActionContext(context);

    EditorSourceSymbolContextMenuState state;
    state.items.append({
        SourceSymbolAction::FindReferences,
        actionContext.available
    });
    state.items.append({
        SourceSymbolAction::ShowRelationships,
        actionContext.available
    });
    state.items.append({
        SourceSymbolAction::ShowSignalKernelGraph,
        actionContext.available
    });
    state.items.append({
        SourceSymbolAction::ShowStateTransitionGraph,
        actionContext.available
            && stateTransitionTriggerForContext(actionContext).available
    });
    state.items.append({
        SourceSymbolAction::ShowModuleBlockDiagram,
        actionContext.available
            && moduleBlockDiagramAvailableForContext(actionContext)
    });
    return state;
}

EditorSourceSymbolActionRequestState
EditorSourceNavigationQuery::sourceSymbolActionRequestState(
    SourceSymbolAction action,
    const EditorSemanticContext& context)
{
    const SourceSymbolActionContext actionContext =
        sourceSymbolActionContext(context);

    EditorSourceSymbolActionRequestState state;
    state.action = action;
    if (!actionContext.available)
        return state;

    if (action == SourceSymbolAction::ShowStateTransitionGraph
        && !stateTransitionTriggerForContext(actionContext).available) {
        return state;
    }
    if (action == SourceSymbolAction::ShowModuleBlockDiagram
        && !moduleBlockDiagramAvailableForContext(actionContext)) {
        return state;
    }

    state.available = true;
    state.symbolName = actionContext.symbolName;
    if (action == SourceSymbolAction::ShowSignalKernelGraph
        && !actionContext.memberAccessPath.isEmpty()
        && !actionContext.memberAccessRootName.isEmpty()) {
        state.symbolName = actionContext.memberAccessRootName;
        state.signalAccessPath = actionContext.memberAccessPath;
    }
    state.fileName = actionContext.fileName;
    state.moduleName = actionContext.moduleName;
    return state;
}

SourceEditorNavigationTarget EditorSourceNavigationQuery::sourceNavigationTarget(
    const EditorSemanticContext& context,
    const std::function<bool(const QString&)>& canResolveIdentifier)
{
    return SourceNavigationService::getInstance()->editorNavigationTargetAtColumn(
        context.lineText,
        context.column,
        canResolveIdentifier);
}

SourceEditorNavigationTarget
EditorSourceNavigationQuery::definitionSourceNavigationTarget(
    const EditorSemanticContext& context)
{
    return sourceNavigationTarget(
        context,
        [context](const QString& symbolName) {
            return canResolveDefinitionTarget(symbolName, context);
        });
}

EditorSourceNavigationTarget
EditorSourceNavigationQuery::editorSourceNavigationTarget(
    const EditorSemanticContext& context,
    int blockPosition)
{
    EditorSourceNavigationTarget editorTarget;
    const SourceEditorNavigationTarget sourceTarget =
        definitionSourceNavigationTarget(context);
    if (!sourceTarget.matched)
        return editorTarget;

    editorTarget.matched = true;
    editorTarget.jumpable = sourceTarget.jumpable;
    editorTarget.includeTarget = sourceTarget.includeTarget;
    editorTarget.identifierTarget = sourceTarget.identifierTarget;
    editorTarget.text = sourceTarget.text;
    editorTarget.startPos = blockPosition + sourceTarget.startColumn;
    editorTarget.endPos = blockPosition + sourceTarget.endColumn;
    editorTarget.cursorPosition = blockPosition + sourceTarget.cursorColumn;
    return editorTarget;
}

EditorSourceNavigationClickState
EditorSourceNavigationQuery::sourceNavigationClickState(
    const EditorSourceNavigationTarget& target)
{
    EditorSourceNavigationClickState state;
    if (!target.matched || target.text.isEmpty())
        return state;

    state.text = target.text;
    state.acceptEvent = true;
    if (target.includeTarget) {
        state.action = EditorSourceNavigationClickAction::OpenInclude;
        return state;
    }

    state.action = EditorSourceNavigationClickAction::NavigateToDefinition;
    state.contextCursorPosition =
        target.identifierTarget ? target.cursorPosition : -1;
    return state;
}

SourceIdentifierTarget EditorSourceNavigationQuery::sourceIdentifierTarget(
    const EditorSemanticContext& context)
{
    return SourceNavigationService::getInstance()->identifierAtColumn(
        context.lineText,
        context.column);
}

DefinitionNavigationQuery EditorSourceNavigationQuery::definitionNavigationQuery(
    const QString& symbolName,
    const EditorSemanticContext& context)
{
    DefinitionNavigationContext navigationContext;
    navigationContext.symbolName = symbolName;
    navigationContext.fileName = context.fileName;
    navigationContext.moduleName = context.moduleName;
    navigationContext.lineText = context.lineText;
    navigationContext.cursorLine = context.cursorLine;
    navigationContext.column = context.column;
    return DefinitionNavigationService::getInstance()
        ->navigationQueryForContext(navigationContext);
}

DefinitionNavigationTarget EditorSourceNavigationQuery::resolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context)
{
    return DefinitionNavigationService::getInstance()->resolveTarget(
        definitionNavigationQuery(symbolName, context));
}

bool EditorSourceNavigationQuery::canResolveDefinitionTarget(
    const QString& symbolName,
    const EditorSemanticContext& context)
{
    return DefinitionNavigationService::getInstance()->canResolveTarget(
        definitionNavigationQuery(symbolName, context));
}

QString EditorSourceNavigationQuery::definitionTooltipText(
    const QString& symbolName,
    const EditorSemanticContext& context)
{
    return DefinitionNavigationService::getInstance()->tooltipText(
        definitionNavigationQuery(symbolName, context));
}
