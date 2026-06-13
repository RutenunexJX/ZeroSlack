#include "completionservice.h"

#include "completioncontexthelper.h"
#include "completionsymbolquery.h"

EditorCompletionState CompletionService::editorCompletionState(
    const EditorCompletionQuery& query) const
{
    EditorCompletionState state;

    QString variableName;
    QString memberPrefix;
    const QString lineForParse = query.lineUpToCursor.trimmed();
    if (CompletionContextHelper::tryParseStructMember(
            lineForParse, variableName, memberPrefix)) {
        const QString structTypeName =
            getStructTypeForVariable(variableName, query.moduleName);
        if (!structTypeName.isEmpty()) {
            CompletionQuery completionQuery;
            completionQuery.prefix = memberPrefix;
            completionQuery.fileName = query.fileName;
            completionQuery.moduleName = query.moduleName;
            completionQuery.structTypeNameForMember = structTypeName;
            completionQuery.cursorLine = query.cursorLine;
            completionQuery.cursorPosition = query.cursorPosition;

            state.available = true;
            state.prefix = memberPrefix;
            state.replacementStartColumn =
                query.lineUpToCursor.lastIndexOf(QLatin1Char('.')) + 1;
            state.completion = findCompletionResult(completionQuery);
            return state;
        }
    }

    if (query.wordPrefix.length() < 1)
        return state;

    CompletionQuery completionQuery;
    completionQuery.prefix = query.wordPrefix;
    completionQuery.fileName = query.fileName;
    completionQuery.moduleName = query.moduleName;
    completionQuery.cursorLine = query.cursorLine;
    completionQuery.cursorPosition = query.cursorPosition;

    state.available = true;
    state.prefix = query.wordPrefix;
    state.replacementStartColumn =
        qMax(0, query.lineUpToCursor.size() - query.wordPrefix.size());
    state.completion = findCompletionResult(completionQuery);
    return state;
}

CompletionTriggerState CompletionService::completionTriggerState(
    const CompletionTriggerQuery& query) const
{
    CompletionTriggerState state;

    if (query.lineUpToCursor.isEmpty()) {
        state.hidePopup = !query.commandModeActive;
        return state;
    }

    const QChar lastChar = query.lineUpToCursor.back();
    if (query.commandModeActive) {
        state.continueCompletion =
            lastChar.isLetterOrNumber()
            || lastChar == QLatin1Char('_')
            || lastChar == QLatin1Char(' ');
        state.hidePopup = false;
        return state;
    }

    if (lastChar.isLetterOrNumber()
        || lastChar == QLatin1Char('_')
        || lastChar == QLatin1Char('.')) {
        state.continueCompletion = true;
        return state;
    }

    if (lastChar != QLatin1Char(' ')) {
        state.hidePopup = true;
        return state;
    }

    const QString lineBeforeSpace =
        query.lineUpToCursor.left(query.lineUpToCursor.size() - 1).trimmed();
    QString variableName;
    QString memberPrefix;
    if (!CompletionContextHelper::tryParseStructMember(
            lineBeforeSpace, variableName, memberPrefix)) {
        state.hidePopup = true;
        return state;
    }

    state.continueCompletion =
        !getStructTypeForVariable(variableName, query.moduleName).isEmpty();
    state.hidePopup = !state.continueCompletion;
    return state;
}

bool CompletionService::shouldContinueCompletion(
    const CompletionTriggerQuery& query) const
{
    return completionTriggerState(query).continueCompletion;
}

QStringList CompletionService::findScopeCompletions(const CompletionQuery& query) const
{
    return CompletionSymbolQuery::scopeCompletions(
        semanticIndex(), query.fileName, query.cursorLine, query.prefix);
}
