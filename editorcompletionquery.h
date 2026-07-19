#ifndef EDITORCOMPLETIONQUERY_H
#define EDITORCOMPLETIONQUERY_H

#include "editorsemanticcontextservice.h"

class EditorCompletionQueryHelper
{
public:
    static CommandModeCompletionQuery commandModeCompletionQuery(
        const EditorSemanticContext& context);
    static CommandModeCompletionState commandModeCompletionState(
        const EditorSemanticContext& context);
    static CommandModeInputState commandModeInputState(
        const EditorSemanticContext& context);
    static CommandModeMatch commandModeMatch(
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
