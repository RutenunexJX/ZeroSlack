#include "completioncommandmode.h"

#include "completionmatcher.h"

#include <Qt>

QList<CommandModeCommand> CompletionCommandMode::commands()
{
    return {
        {QStringLiteral("r "), sym_list::sym_reg, QStringLiteral("reg variables"), QStringLiteral("reg")},
        {QStringLiteral("w "), sym_list::sym_wire, QStringLiteral("wire variables"), QStringLiteral("wire")},
        {QStringLiteral("l "), sym_list::sym_logic, QStringLiteral("logic variables"), QStringLiteral("logic")},
        {QStringLiteral("m "), sym_list::sym_module, QStringLiteral("modules"), QStringLiteral("module")},
        {QStringLiteral("t "), sym_list::sym_task, QStringLiteral("tasks"), QStringLiteral("task")},
        {QStringLiteral("f "), sym_list::sym_function, QStringLiteral("functions"), QStringLiteral("function")},
        {QStringLiteral("i "), sym_list::sym_interface, QStringLiteral("interfaces"), QStringLiteral("interface")},
        {QStringLiteral("d "), sym_list::sym_def_define, QStringLiteral("macro definitions"), QStringLiteral("`define")},
        {QStringLiteral("lp "), sym_list::sym_localparam, QStringLiteral("localparam declarations"), QStringLiteral("localparam")},
        {QStringLiteral("p "), sym_list::sym_parameter, QStringLiteral("parameter declarations"), QStringLiteral("parameter")},
        {QStringLiteral("a "), sym_list::sym_always, QStringLiteral("always blocks"), QStringLiteral("always")},
        {QStringLiteral("c "), sym_list::sym_assign, QStringLiteral("continuous assignments"), QStringLiteral("assign")},
        {QStringLiteral("u "), sym_list::sym_typedef, QStringLiteral("type definitions"), QStringLiteral("typedef")},
        {QStringLiteral("ee "), sym_list::sym_enum_value, QStringLiteral("enum values"), QStringLiteral("enum_value")},
        {QStringLiteral("ne "), sym_list::sym_enum, QStringLiteral("enum types"), QStringLiteral("enum")},
        {QStringLiteral("e "), sym_list::sym_enum_var, QStringLiteral("enum variables"), QStringLiteral("enum_var")},
        {QStringLiteral("sm "), sym_list::sym_struct_member, QStringLiteral("struct members"), QStringLiteral("member")},
        {QStringLiteral("nsp "), sym_list::sym_packed_struct, QStringLiteral("packed struct types"), QStringLiteral("struct")},
        {QStringLiteral("ns "), sym_list::sym_unpacked_struct, QStringLiteral("unpacked struct types"), QStringLiteral("struct")},
        {QStringLiteral("sp "), sym_list::sym_packed_struct_var, QStringLiteral("packed struct variables"), QStringLiteral("struct")},
        {QStringLiteral("s "), sym_list::sym_unpacked_struct_var, QStringLiteral("unpacked struct variables"), QStringLiteral("struct")},
    };
}

CommandModeMatch CompletionCommandMode::matchCommandMode(
    const QString& lineUpToCursor)
{
    CommandModeMatch result;
    for (const CommandModeCommand& command : commands()) {
        const int prefixPosition = lineUpToCursor.lastIndexOf(command.prefix);
        if (prefixPosition < 0)
            continue;

        const QString beforePrefix = lineUpToCursor.left(prefixPosition).trimmed();
        if (!beforePrefix.isEmpty())
            continue;

        result.matched = true;
        result.prefixPosition = prefixPosition;
        result.command = command;
        result.input = lineUpToCursor.mid(prefixPosition + command.prefix.length());
        return result;
    }
    return result;
}

CommandModeInputState CompletionCommandMode::inputState(
    const QString& lineUpToCursor)
{
    const CommandModeMatch match = matchCommandMode(lineUpToCursor);
    CommandModeInputState state;
    if (!match.matched)
        return state;

    state.matched = true;
    state.prefixPosition = match.prefixPosition;
    state.input = match.input;
    state.command = match.command;
    state.exitRequested =
        lineUpToCursor.size() >= 2
        && lineUpToCursor.right(2) == QStringLiteral("  ");
    return state;
}

CommandSymbolPresentation CompletionCommandMode::symbolPresentation(
    sym_list::sym_type_e symbolType)
{
    for (const CommandModeCommand& command : commands()) {
        if (command.symbolType == symbolType) {
            CommandSymbolPresentation presentation;
            presentation.defaultValue = command.defaultValue;
            presentation.typeDescription = command.description;
            return presentation;
        }
    }

    CommandSymbolPresentation presentation;
    presentation.defaultValue = QStringLiteral("symbol");
    presentation.typeDescription = QStringLiteral("symbols");
    return presentation;
}

CommandSymbolCompletionItem CompletionCommandMode::symbolCompletionItem(
    const sym_list::SymbolInfo& symbol,
    sym_list::sym_type_e requestedType,
    const QString& prefix)
{
    return symbolCompletionItem(
        semanticSymbolRecordForSymbol(symbol),
        requestedType,
        prefix);
}

CommandSymbolCompletionItem CompletionCommandMode::symbolCompletionItem(
    const SemanticSymbolRecord& record,
    sym_list::sym_type_e requestedType,
    const QString& prefix)
{
    const QString symbolName = record.name;
    CommandSymbolCompletionItem item;
    item.symbolRecord = record;
    item.symbolStableKey = item.symbolRecord.stableKey.isValid()
        ? item.symbolRecord.stableKey
        : SymbolStableKey();
    item.declarationKind = item.symbolRecord.declarationKind;
    item.usageRole = item.symbolRecord.usageRole;
    item.ownerScope = item.symbolRecord.owner.kind;
    item.sourceRole = item.symbolRecord.sourceRole;
    item.defaultValue = symbolName;
    item.description = symbolPresentation(requestedType)
        .typeDescription
        .split(' ')
        .value(0);

    if (requestedType == sym_list::sym_packed_struct_var
        || requestedType == sym_list::sym_unpacked_struct_var) {
        const QString structTypeName = item.symbolRecord.owner.name;
        item.text = structTypeName.isEmpty()
            ? symbolName
            : QStringLiteral("%1(%2)").arg(symbolName, structTypeName);
        item.uniqueKey = QStringLiteral("%1:%2").arg(symbolName, structTypeName);
    } else if (requestedType == sym_list::sym_enum_value) {
        item.text = symbolName;
        item.description = item.symbolRecord.type.rawTypeText.isEmpty()
            ? QStringLiteral("enum")
            : item.symbolRecord.type.rawTypeText;
        item.uniqueKey = symbolName;
    } else {
        item.text = symbolName;
        item.uniqueKey = symbolName;
    }

    item.score = CompletionMatcher::completionItemScore(symbolName, prefix);
    return item;
}

CompletionActivationState CompletionCommandMode::activationState(
    const CompletionActivationQuery& query)
{
    CompletionActivationState state;
    if (!query.selectable)
        return state;

    switch (query.mode) {
    case CompletionActivationMode::AlternateMode:
        state.action = CompletionActivationAction::ExecuteAlternateCommand;
        state.text = query.itemText;
        return state;
    case CompletionActivationMode::CommandMode:
        state.action = CompletionActivationAction::ReplaceLine;
        state.text = query.defaultValue.isEmpty()
            ? query.itemText
            : query.defaultValue;
        state.clearCommandMode = true;
        state.hidePopup = true;
        return state;
    case CompletionActivationMode::EditorWord:
        state.action = CompletionActivationAction::ReplaceWord;
        state.text = query.itemText;
        state.hidePopup = true;
        return state;
    }

    return state;
}

CompletionPopupKeyState CompletionCommandMode::popupKeyState(
    const CompletionPopupKeyQuery& query)
{
    CompletionPopupKeyState state;

    switch (query.key) {
    case Qt::Key_Down:
    case Qt::Key_Up:
        state.action = CompletionPopupKeyAction::ForwardToPopup;
        return state;
    case Qt::Key_Escape:
        state.action = query.mode == CompletionActivationMode::AlternateMode
            ? CompletionPopupKeyAction::HidePopupAndClearAlternate
            : CompletionPopupKeyAction::HidePopup;
        return state;
    case Qt::Key_Backspace:
        if (query.mode == CompletionActivationMode::AlternateMode) {
            state.action = query.alternateBufferEmpty
                ? CompletionPopupKeyAction::HidePopup
                : CompletionPopupKeyAction::BackspaceAlternateInput;
        }
        return state;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (query.mode == CompletionActivationMode::AlternateMode) {
            state.action = query.currentIndexValid
                ? CompletionPopupKeyAction::ActivateCurrent
                : CompletionPopupKeyAction::Consume;
        } else {
            state.action = query.hasRows
                ? CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable
                : CompletionPopupKeyAction::Consume;
        }
        return state;
    case Qt::Key_Tab:
        if (query.mode != CompletionActivationMode::AlternateMode) {
            state.action = query.hasRows
                ? CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable
                : CompletionPopupKeyAction::Consume;
        }
        return state;
    default:
        return state;
    }
}
