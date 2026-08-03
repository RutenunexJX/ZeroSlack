#include "editorsourcenavigationquery.h"

#include "moduleblockdiagramservice.h"
#include "semanticindex.h"
#include "statetransitiontriggerservice.h"
#include "symboltaxonomy.h"

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
    SemanticQueryContext context;
    context.fileName = actionContext.fileName;
    context.moduleName = actionContext.moduleName;
    const QList<SemanticSymbolRecord> definitions =
        SemanticIndex::getInstance()->findDefinitionRecords(
            actionContext.symbolName,
            context);
    bool hasModuleDefinition = false;
    for (const SemanticSymbolRecord& record : definitions) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForSymbolRecord(record);
        if (metadata.declarationKind
                == SymbolTaxonomy::DeclarationKind::Module) {
            hasModuleDefinition = true;
            break;
        }
    }
    if (!hasModuleDefinition)
        return false;

    ModuleBlockDiagramQuery query;
    query.moduleName = actionContext.symbolName;
    query.fileName = actionContext.fileName;
    query.maxDepth = 0;
    return ModuleBlockDiagramService::getInstance()
        ->buildModuleBlockDiagram(query)
        .found;
}

bool signalUsageHotspotAvailableForContext(
    const SourceSymbolActionContext& actionContext)
{
    if (!actionContext.available)
        return false;

    SemanticQueryContext context;
    context.fileName = actionContext.fileName;
    context.moduleName = actionContext.moduleName;
    const QList<SemanticSymbolRecord> definitions =
        SemanticIndex::getInstance()->findDefinitionRecords(
            actionContext.symbolName,
            context);
    if (definitions.isEmpty())
        return true;

    for (const SemanticSymbolRecord& record : definitions) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForSymbolRecord(record);
        if (metadata.collectorKind == SymbolTaxonomy::CollectorKind::EnumValue)
            continue;
        if (SymbolTaxonomy::isSignalDeclaration(metadata)
            || SymbolTaxonomy::isPortDeclaration(metadata)
            || metadata.declarationKind
                == SymbolTaxonomy::DeclarationKind::StructMember) {
            return true;
        }
    }
    return false;
}

QString noSymbolActionReason(const EditorSemanticContext& context)
{
    if (context.fileName.isEmpty())
        return QStringLiteral("No source file for symbol navigation");
    return QStringLiteral("No symbol under cursor");
}

bool contextLooksLikeMacroInvocation(const EditorSemanticContext& context,
                                     const SourceSymbolActionContext& actionContext)
{
    if (!actionContext.available)
        return false;
    const SourceIdentifierTarget identifier =
        SourceNavigationService::getInstance()->identifierAtColumn(
            context.lineText,
            context.column);
    return identifier.matched
        && identifier.identifier == actionContext.symbolName
        && identifier.startColumn > 0
        && identifier.startColumn <= context.lineText.size()
        && context.lineText.at(identifier.startColumn - 1) == QLatin1Char('`');
}

QString actionUnavailableReason(
    SourceSymbolAction action,
    const SourceSymbolActionContext& actionContext,
    const EditorSemanticContext& context)
{
    if (!actionContext.available)
        return noSymbolActionReason(context);

    switch (action) {
    case SourceSymbolAction::GoToDefinition:
        if (contextLooksLikeMacroInvocation(context, actionContext)) {
            return QStringLiteral(
                "Macro `%1` was not found in the current file or indexed workspace/include files")
                .arg(actionContext.symbolName);
        }
        return QStringLiteral("Symbol not indexed");
    case SourceSymbolAction::ShowSignalKernelGraph:
        return QString();
    case SourceSymbolAction::ShowSignalUsageHotspot:
        return QStringLiteral(
            "Signal Usage Hotspot requires a signal, port, enum variable, or struct member");
    case SourceSymbolAction::ShowStateTransitionGraph: {
        const StateTransitionTriggerReport trigger =
            stateTransitionTriggerForContext(actionContext);
        return trigger.reasonDisplayName.isEmpty()
            ? QStringLiteral("State transition graph unavailable")
            : trigger.reasonDisplayName;
    }
    case SourceSymbolAction::ShowModuleBlockDiagram:
        return QStringLiteral(
            "Module Block Diagram requires a module name");
    }
    return QStringLiteral("Action unavailable");
}

EditorSourceSymbolMenuItemState sourceSymbolMenuItem(
    SourceSymbolAction action,
    bool enabled,
    const QString& disabledReason = QString())
{
    EditorSourceSymbolMenuItemState item;
    item.action = action;
    item.enabled = enabled;
    item.disabledReason = enabled ? QString() : disabledReason;
    return item;
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

    if (context.key == Qt::Key_F12
        && modifiers == Qt::NoModifier) {
        state.matched = true;
        state.acceptEvent = true;
        state.action = SourceSymbolAction::GoToDefinition;
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
    const StateTransitionTriggerReport stateTransitionTrigger =
        actionContext.available
            ? stateTransitionTriggerForContext(actionContext)
            : StateTransitionTriggerReport();
    const bool moduleBlockAvailable = actionContext.available
        && moduleBlockDiagramAvailableForContext(actionContext);
    const bool signalUsageHotspotAvailable =
        signalUsageHotspotAvailableForContext(actionContext);

    EditorSourceSymbolContextMenuState state;
    state.items.append(sourceSymbolMenuItem(
        SourceSymbolAction::ShowSignalKernelGraph,
        actionContext.available,
        actionUnavailableReason(
            SourceSymbolAction::ShowSignalKernelGraph,
            actionContext,
            context)));
    state.items.append(sourceSymbolMenuItem(
        SourceSymbolAction::ShowSignalUsageHotspot,
        signalUsageHotspotAvailable,
        actionUnavailableReason(
            SourceSymbolAction::ShowSignalUsageHotspot,
            actionContext,
            context)));
    state.items.append(sourceSymbolMenuItem(
        SourceSymbolAction::ShowStateTransitionGraph,
        actionContext.available && stateTransitionTrigger.available,
        actionContext.available
            ? stateTransitionTrigger.reasonDisplayName
            : actionUnavailableReason(
                SourceSymbolAction::ShowStateTransitionGraph,
                actionContext,
                context)));
    state.items.append(sourceSymbolMenuItem(
        SourceSymbolAction::ShowModuleBlockDiagram,
        moduleBlockAvailable,
        actionUnavailableReason(
            SourceSymbolAction::ShowModuleBlockDiagram,
            actionContext,
            context)));
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
    if (!actionContext.available) {
        state.unavailableReason =
            actionUnavailableReason(action, actionContext, context);
        return state;
    }

    if (action == SourceSymbolAction::GoToDefinition
        && !canResolveDefinitionTarget(actionContext.symbolName, context)) {
        state.unavailableReason =
            actionUnavailableReason(action, actionContext, context);
        return state;
    }
    if (action == SourceSymbolAction::ShowStateTransitionGraph
        && !stateTransitionTriggerForContext(actionContext).available) {
        state.unavailableReason =
            actionUnavailableReason(action, actionContext, context);
        return state;
    }
    if (action == SourceSymbolAction::ShowSignalUsageHotspot
        && !signalUsageHotspotAvailableForContext(actionContext)) {
        state.unavailableReason =
            actionUnavailableReason(action, actionContext, context);
        return state;
    }
    if (action == SourceSymbolAction::ShowModuleBlockDiagram
        && !moduleBlockDiagramAvailableForContext(actionContext)) {
        state.unavailableReason =
            actionUnavailableReason(action, actionContext, context);
        return state;
    }

    state.available = true;
    state.symbolName = actionContext.symbolName;
    if ((action == SourceSymbolAction::ShowSignalKernelGraph
         || action == SourceSymbolAction::ShowSignalUsageHotspot)
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
