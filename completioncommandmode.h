#ifndef COMPLETIONCOMMANDMODE_H
#define COMPLETIONCOMMANDMODE_H

#include "completionservice.h"

class CompletionCommandMode
{
public:
    static QList<CommandModeCommand> commands();
    static CommandModeMatch matchCommandMode(const QString& lineUpToCursor);
    static CommandModeInputState inputState(const QString& lineUpToCursor);
    static CommandSymbolPresentation symbolPresentation(
        sym_list::sym_type_e symbolType);
    static CommandSymbolCompletionItem symbolCompletionItem(
        const SemanticSymbolRecord& record,
        sym_list::sym_type_e requestedType,
        const QString& prefix = QString());
    static CompletionActivationState activationState(
        const CompletionActivationQuery& query);
    static CompletionPopupKeyState popupKeyState(
        const CompletionPopupKeyQuery& query);
};

#endif // COMPLETIONCOMMANDMODE_H
