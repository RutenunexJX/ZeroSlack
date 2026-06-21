#include "completioncommandmode.h"

#include "completionmatcher.h"

#include <Qt>

namespace {
bool isCommandSafePrefix(const QString& text)
{
    for (const QChar ch : text) {
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            return false;
    }
    return true;
}

bool isPositionInCommentOrString(const QString& line, int position)
{
    bool inString = false;
    bool inBlockComment = false;
    bool escaped = false;

    for (int i = 0; i < position && i < line.size(); ++i) {
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

CommandModeMatch helpMatch(const QString& lineUpToCursor)
{
    CommandModeMatch result;
    const int prefixPosition = lineUpToCursor.lastIndexOf(QStringLiteral(";?"));
    if (prefixPosition < 0 || prefixPosition + 2 != lineUpToCursor.size())
        return result;
    if (!isCommandSafePrefix(lineUpToCursor.left(prefixPosition)))
        return result;
    if (isPositionInCommentOrString(lineUpToCursor, prefixPosition))
        return result;

    result.matched = true;
    result.helpRequested = true;
    result.prefixPosition = prefixPosition;
    result.input = QStringLiteral("?");
    result.command = {
        QStringLiteral(";?"),
        CompletionCommandKind::User,
        QStringLiteral("command help"),
        QStringLiteral(";?")
    };
    return result;
}
}

QList<CommandModeCommand> CompletionCommandMode::commands()
{
    return {
        {QStringLiteral(";r "), CompletionCommandKind::Reg, QStringLiteral("reg variables"), QStringLiteral("reg")},
        {QStringLiteral(";w "), CompletionCommandKind::Wire, QStringLiteral("wire variables"), QStringLiteral("wire")},
        {QStringLiteral(";l "), CompletionCommandKind::Logic, QStringLiteral("logic variables"), QStringLiteral("logic")},
        {QStringLiteral(";m "), CompletionCommandKind::Module, QStringLiteral("modules"), QStringLiteral("module")},
        {QStringLiteral(";t "), CompletionCommandKind::Task, QStringLiteral("tasks"), QStringLiteral("task")},
        {QStringLiteral(";f "), CompletionCommandKind::Function, QStringLiteral("functions"), QStringLiteral("function")},
        {QStringLiteral(";i "), CompletionCommandKind::Interface, QStringLiteral("interfaces"), QStringLiteral("interface")},
        {QStringLiteral(";d "), CompletionCommandKind::Macro, QStringLiteral("macro definitions"), QStringLiteral("`define")},
        {QStringLiteral(";lp "), CompletionCommandKind::Localparam, QStringLiteral("localparam declarations"), QStringLiteral("localparam")},
        {QStringLiteral(";p "), CompletionCommandKind::Parameter, QStringLiteral("parameter declarations"), QStringLiteral("parameter")},
        {QStringLiteral(";a "), CompletionCommandKind::AlwaysProcess, QStringLiteral("always blocks"), QStringLiteral("always")},
        {QStringLiteral(";c "), CompletionCommandKind::ContinuousAssign, QStringLiteral("continuous assignments"), QStringLiteral("assign")},
        {QStringLiteral(";u "), CompletionCommandKind::Typedef, QStringLiteral("type definitions"), QStringLiteral("typedef")},
        {QStringLiteral(";ee "), CompletionCommandKind::EnumValue, QStringLiteral("enum values"), QStringLiteral("enum_value")},
        {QStringLiteral(";ne "), CompletionCommandKind::EnumType, QStringLiteral("enum types"), QStringLiteral("enum")},
        {QStringLiteral(";e "), CompletionCommandKind::EnumVariable, QStringLiteral("enum variables"), QStringLiteral("enum_var")},
        {QStringLiteral(";sm "), CompletionCommandKind::StructMember, QStringLiteral("struct members"), QStringLiteral("member")},
        {QStringLiteral(";nsp "), CompletionCommandKind::PackedStructType, QStringLiteral("packed struct types"), QStringLiteral("struct")},
        {QStringLiteral(";ns "), CompletionCommandKind::UnpackedStructType, QStringLiteral("unpacked struct types"), QStringLiteral("struct")},
        {QStringLiteral(";sp "), CompletionCommandKind::PackedStructVariable, QStringLiteral("packed struct variables"), QStringLiteral("struct")},
        {QStringLiteral(";s "), CompletionCommandKind::UnpackedStructVariable, QStringLiteral("unpacked struct variables"), QStringLiteral("struct")},
    };
}

CommandModeMatch CompletionCommandMode::matchCommandMode(
    const QString& lineUpToCursor)
{
    CommandModeMatch result;
    const CommandModeMatch help = helpMatch(lineUpToCursor);
    if (help.matched)
        return help;

    for (const CommandModeCommand& command : commands()) {
        const int prefixPosition = lineUpToCursor.lastIndexOf(command.prefix);
        if (prefixPosition < 0)
            continue;

        if (!isCommandSafePrefix(lineUpToCursor.left(prefixPosition)))
            continue;
        if (isPositionInCommentOrString(lineUpToCursor, prefixPosition))
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
    state.helpRequested = match.helpRequested;
    state.prefixPosition = match.prefixPosition;
    state.input = match.input;
    state.command = match.command;
    return state;
}

CommandSymbolPresentation CompletionCommandMode::symbolPresentation(
    CompletionCommandKind kind)
{
    for (const CommandModeCommand& command : commands()) {
        if (command.kind == kind) {
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
    const SemanticSymbolRecord& record,
    CompletionCommandKind requestedKind,
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
    item.description = symbolPresentation(requestedKind)
        .typeDescription
        .split(' ')
        .value(0);

    if (requestedKind == CompletionCommandKind::PackedStructVariable
        || requestedKind == CompletionCommandKind::UnpackedStructVariable) {
        const QString structTypeName = item.symbolRecord.owner.name;
        item.text = structTypeName.isEmpty()
            ? symbolName
            : QStringLiteral("%1(%2)").arg(symbolName, structTypeName);
        item.uniqueKey = QStringLiteral("%1:%2").arg(symbolName, structTypeName);
    } else if (requestedKind == CompletionCommandKind::EnumValue) {
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

bool CompletionCommandMode::requiresModuleContext(CompletionCommandKind kind)
{
    switch (kind) {
    case CompletionCommandKind::Reg:
    case CompletionCommandKind::Wire:
    case CompletionCommandKind::Logic:
    case CompletionCommandKind::Localparam:
    case CompletionCommandKind::Parameter:
    case CompletionCommandKind::AlwaysProcess:
    case CompletionCommandKind::ContinuousAssign:
    case CompletionCommandKind::EnumVariable:
    case CompletionCommandKind::StructMember:
    case CompletionCommandKind::PackedStructVariable:
    case CompletionCommandKind::UnpackedStructVariable:
        return true;
    case CompletionCommandKind::User:
    case CompletionCommandKind::Module:
    case CompletionCommandKind::Task:
    case CompletionCommandKind::Function:
    case CompletionCommandKind::Interface:
    case CompletionCommandKind::Package:
    case CompletionCommandKind::Macro:
    case CompletionCommandKind::Typedef:
    case CompletionCommandKind::EnumValue:
    case CompletionCommandKind::EnumType:
    case CompletionCommandKind::PackedStructType:
    case CompletionCommandKind::UnpackedStructType:
        return false;
    }
    return false;
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
        state.action = CompletionActivationAction::ReplaceCommandInput;
        state.text = query.defaultValue.isEmpty()
            ? query.itemText
            : query.defaultValue;
        state.clearCommandMode =
            !state.text.startsWith(QLatin1Char(';'));
        state.hidePopup = state.clearCommandMode;
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
        if (query.mode == CompletionActivationMode::AlternateMode) {
            state.action = CompletionPopupKeyAction::HidePopupAndClearAlternate;
        } else if (query.mode == CompletionActivationMode::CommandMode) {
            state.action = CompletionPopupKeyAction::HidePopupAndClearCommand;
        } else {
            state.action = CompletionPopupKeyAction::HidePopup;
        }
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
