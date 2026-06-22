#include "completionservice.h"

#include "codetemplateservice.h"
#include "completioncommandmode.h"
#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "inlinecommandmode.h"
#include "symboltaxonomy.h"

namespace {
QString editorActionToken(const InlineCommandDescriptor& descriptor)
{
    return descriptor.label.startsWith(QStringLiteral(";:"))
        ? descriptor.label.mid(2)
        : descriptor.label;
}

QList<CodeTemplateItem> matchingEditorActions(const QString& prefix)
{
    QList<CodeTemplateItem> items;
    for (const InlineCommandDescriptor& descriptor :
         InlineCommandMode::descriptorsForIntent(InlineCommandIntent::EditorAction)) {
        const QString token = editorActionToken(descriptor);
        if (!prefix.isEmpty()
            && !token.startsWith(prefix, Qt::CaseInsensitive)) {
            continue;
        }

        CodeTemplateItem item;
        item.commandToken = descriptor.label;
        item.label = token;
        item.description = descriptor.description.isEmpty()
            ? QStringLiteral("reserved editor action")
            : descriptor.description;
        item.defaultValue = descriptor.defaultValue;
        item.insertText = descriptor.defaultValue;
        items.append(item);
    }
    return items;
}
}

QList<CommandModeCommand> CompletionService::commandModeCommands() const
{
    return CompletionCommandMode::commands();
}

CommandModeMatch CompletionService::matchCommandMode(const QString& lineUpToCursor) const
{
    return CompletionCommandMode::matchCommandMode(lineUpToCursor);
}

CommandModeInputState CompletionService::commandModeInputState(
    const QString& lineUpToCursor) const
{
    return CompletionCommandMode::inputState(lineUpToCursor);
}

CommandModeCompletionState CompletionService::commandModeCompletionState(
    const CommandModeCompletionQuery& query) const
{
    const CommandModeInputState inputState =
        commandModeInputState(query.lineUpToCursor);

    CommandModeCompletionState state;
    if (!inputState.matched)
        return state;

    state.matched = true;
    state.helpRequested = inputState.helpRequested;
    state.exitRequested = inputState.exitRequested;
    state.intent = inputState.intent;
    state.prefixPosition = inputState.prefixPosition;
    state.input = inputState.input;
    state.completionPrefix = inputState.input.trimmed();
    state.command = inputState.command;
    state.descriptor = inputState.descriptor;
    state.commandKind = inputState.command.kind;
    state.headerText = InlineCommandMode::headerText(inputState.descriptor);

    if (state.helpRequested) {
        state.helpDescriptors =
            InlineCommandMode::descriptorsForIntent(state.intent);
        if (state.intent == InlineCommandIntent::SemanticCompletion)
            state.helpCommands = commandModeCommands();
        state.showCompletions = true;
        return state;
    }

    if (state.exitRequested)
        return state;

    if (state.intent == InlineCommandIntent::CodeTemplate) {
        state.templateItems =
            CodeTemplateService::getInstance()->matchingTemplates(
                state.descriptor.label,
                state.completionPrefix);
        state.showCompletions = true;
        return state;
    }

    if (state.intent == InlineCommandIntent::EditorAction) {
        state.templateItems = matchingEditorActions(state.completionPrefix);
        state.showCompletions = true;
        return state;
    }

    CommandCompletionQuery completionQuery;
    completionQuery.prefix = state.completionPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.documentText = query.documentText;
    completionQuery.commandKind = state.command.kind;

    state.symbolRecords = findCommandCompletionSymbolRecords(completionQuery);
    if (state.symbolRecords.isEmpty()
        && CompletionCommandMode::requiresModuleContext(state.command.kind)
        && completionQuery.moduleName.isEmpty()) {
        state.hidePopup = true;
        return state;
    }

    state.showCompletions = true;
    state.symbolStableKeys.reserve(state.symbolRecords.size());
    for (const SemanticSymbolRecord& record : state.symbolRecords) {
        state.symbolStableKeys.append(record.stableKey);
    }
    return state;
}

CompletionActivationState CompletionService::completionActivationState(
    const CompletionActivationQuery& query) const
{
    return CompletionCommandMode::activationState(query);
}

CompletionPopupKeyState CompletionService::completionPopupKeyState(
    const CompletionPopupKeyQuery& query) const
{
    return CompletionCommandMode::popupKeyState(query);
}

CommandSymbolPresentation CompletionService::commandSymbolPresentation(
    CompletionCommandKind kind) const
{
    return CompletionCommandMode::symbolPresentation(kind);
}

CommandSymbolCompletionItem CompletionService::commandSymbolCompletionItem(
    const SemanticSymbolRecord& record,
    CompletionCommandKind requestedKind,
    const QString& prefix) const
{
    return CompletionCommandMode::symbolCompletionItem(record, requestedKind, prefix);
}

QStringList CompletionService::findCommandCompletions(const CommandCompletionQuery& query) const
{
    return CompletionSymbolQuery::namesFromRecords(
        findCommandCompletionSymbolRecords(query));
}

QList<SemanticSymbolRecord> CompletionService::findCommandCompletionSymbolRecords(
    const CommandCompletionQuery& query) const
{
    return CompletionSemanticQuery::commandSymbolRecords(semanticIndex(), query);
}
