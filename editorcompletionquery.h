#ifndef EDITORCOMPLETIONQUERY_H
#define EDITORCOMPLETIONQUERY_H

#include "editorsemanticcontextservice.h"

class EditorCompletionQueryHelper
{
public:
    static CompletionTriggerQuery completionTriggerQuery(
        const EditorSemanticContext& context);
    static CompletionTriggerState completionTriggerState(
        const EditorSemanticContext& context);
    static EditorCompletionTextChangeState completionTextChangeState(
        const EditorSemanticContext& context);
    static CompletionQuery completionQuery(
        const QString& prefix,
        const EditorSemanticContext& context);
    static QStringList completionNames(
        const QString& prefix,
        const EditorSemanticContext& context);
    static CommandModeCompletionQuery commandModeCompletionQuery(
        const EditorSemanticContext& context);
    static CommandModeCompletionState commandModeCompletionState(
        const EditorSemanticContext& context);
    static EditorCommandModeCompletionRefreshState
        commandModeCompletionRefreshState(
            const EditorSemanticContext& context,
            bool exitedByDoubleSpace);
    static CommandModeInputState commandModeInputState(
        const EditorSemanticContext& context);
    static CommandModeMatch commandModeMatch(
        const EditorSemanticContext& context);
    static EditorCompletionQuery editorCompletionQuery(
        const EditorSemanticContext& context);
    static EditorCompletionState editorCompletionState(
        const EditorSemanticContext& context);
    static CompletionActivationState completionActivationState(
        const EditorCompletionActivationContext& context);
    static CompletionActivationState completionActivationState(
        const CompletionActivationQuery& query);
    static CompletionPopupKeyState completionPopupKeyState(
        const EditorCompletionPopupKeyContext& context);
    static CompletionPopupKeyState completionPopupKeyState(
        const CompletionPopupKeyQuery& query);
};

#endif // EDITORCOMPLETIONQUERY_H
