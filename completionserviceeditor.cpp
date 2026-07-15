#include "completionservice.h"

#include "completioncontexthelper.h"
#include "completionsymbolquery.h"

namespace {
bool isIdentifierChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

int trailingIdentifierStart(const QString& text)
{
    int pos = text.size();
    while (pos > 0 && isIdentifierChar(text.at(pos - 1)))
        --pos;
    return pos;
}

bool isPositionInCommentOrString(const QString& line, int position)
{
    bool inString = false;
    bool inBlockComment = false;
    bool escaped = false;

    for (int i = 0; i <= position && i < line.size(); ++i) {
        const QChar ch = line.at(i);
        const QChar next = (i + 1 < line.size()) ? line.at(i + 1) : QChar();

        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }

        if (inBlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/'))
            return true;
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"'))
            inString = true;
    }

    return inString || inBlockComment;
}

bool hasStrongCompletionContext(const QString& lineUpToCursor)
{
    if (lineUpToCursor.endsWith(QLatin1Char('.'))
        || lineUpToCursor.endsWith(QLatin1Char('`'))
        || lineUpToCursor.endsWith(QLatin1Char('$'))
        || lineUpToCursor.endsWith(QStringLiteral("::"))) {
        return true;
    }

    const int identStart = trailingIdentifierStart(lineUpToCursor);
    if (identStart <= 0 || identStart >= lineUpToCursor.size())
        return false;

    const QString beforeIdentifier = lineUpToCursor.left(identStart);
    return beforeIdentifier.endsWith(QLatin1Char('.'))
        || beforeIdentifier.endsWith(QLatin1Char('`'))
        || beforeIdentifier.endsWith(QLatin1Char('$'))
        || beforeIdentifier.endsWith(QStringLiteral("::"));
}
}

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
            || lastChar == QLatin1Char(' ')
            || lastChar == QLatin1Char('-')
            || lastChar == QLatin1Char('.')
            || lastChar == QLatin1Char('/')
            || lastChar == QLatin1Char('\\')
            || lastChar == QLatin1Char('?')
            || lastChar == QLatin1Char(':');
        state.hidePopup = false;
        return state;
    }

    if (isPositionInCommentOrString(
            query.lineUpToCursor,
            query.lineUpToCursor.size() - 1)) {
        state.hidePopup = true;
        return state;
    }

    if (lastChar == QLatin1Char(' ')) {
        state.hidePopup = true;
        return state;
    }

    if (hasStrongCompletionContext(query.lineUpToCursor)) {
        state.continueCompletion = true;
        return state;
    }

    if (!isIdentifierChar(lastChar)) {
        state.hidePopup = true;
        return state;
    }

    // Plain identifier typing is not an implicit completion trigger. Keep
    // automatic completion limited to the strong contexts handled above.
    state.hidePopup = true;
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
