#ifndef COMPLETIONCOMMANDMODE_H
#define COMPLETIONCOMMANDMODE_H

#include "completionservice.h"

class CompletionCommandMode
{
public:
    static QList<CommandModeCommand> commands();
    static CommandModeMatch matchCommandMode(const QString& lineUpToCursor);
    static CommandSymbolPresentation symbolPresentation(
        CompletionCommandKind kind);
    static CommandSymbolCompletionItem symbolCompletionItem(
        const SemanticSymbolRecord& record,
        CompletionCommandKind requestedKind,
        const QString& prefix = QString());
    static bool requiresModuleContext(CompletionCommandKind kind);
    static CompletionActivationState activationState(
        const CompletionActivationQuery& query);
    static CompletionPopupKeyState popupKeyState(
        const CompletionPopupKeyQuery& query);
};

#endif // COMPLETIONCOMMANDMODE_H
