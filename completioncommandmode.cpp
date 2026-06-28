#include "completioncommandmode.h"

#include "completionmatcher.h"
#include "inlinecommandmode.h"

#include <Qt>

namespace {
QString moduleInstanceStem(const QString& moduleName)
{
    QString stem;
    for (const QChar ch : moduleName) {
        if (ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('$')) {
            stem.append(ch);
        } else if (!stem.endsWith(QLatin1Char('_'))) {
            stem.append(QLatin1Char('_'));
        }
    }

    while (stem.startsWith(QLatin1Char('_')))
        stem.remove(0, 1);
    while (stem.endsWith(QLatin1Char('_')))
        stem.chop(1);
    if (stem.isEmpty())
        stem = QStringLiteral("module");
    if (stem.at(0).isDigit())
        stem.prepend(QStringLiteral("module_"));
    return stem;
}

QString moduleInstantiationText(const QString& moduleName)
{
    return QStringLiteral("%1 u_%2 (\n);")
        .arg(moduleName, moduleInstanceStem(moduleName));
}
}

QList<CommandModeCommand> CompletionCommandMode::commands()
{
    QList<CommandModeCommand> result;
    for (const InlineCommandDescriptor& descriptor :
         InlineCommandMode::descriptorsForIntent(
             InlineCommandIntent::SemanticCompletion)) {
        result.append(InlineCommandMode::toCommandModeCommand(descriptor));
    }
    return result;
}

CommandModeMatch CompletionCommandMode::matchCommandMode(
    const QString& lineUpToCursor)
{
    CommandModeMatch result;
    const InlineCommandMatch match = InlineCommandMode::match(lineUpToCursor);
    if (!match.matched)
        return result;

    result.matched = true;
    result.helpRequested = match.helpRequested;
    result.intent = match.intent;
    result.prefixPosition = match.prefixPosition;
    result.input = match.input;
    result.descriptor = match.descriptor;
    result.command = InlineCommandMode::toCommandModeCommand(match.descriptor);
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
    state.intent = match.intent;
    state.prefixPosition = match.prefixPosition;
    state.input = match.input;
    state.command = match.command;
    state.descriptor = match.descriptor;
    return state;
}

CommandSymbolPresentation CompletionCommandMode::symbolPresentation(
    CompletionCommandKind kind)
{
    if (kind == CompletionCommandKind::Package) {
        CommandSymbolPresentation presentation;
        presentation.defaultValue = QStringLiteral("import package_name::*;");
        presentation.typeDescription = QStringLiteral("package imports");
        return presentation;
    }

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
    item.analysisBand = item.symbolRecord.analysisBand;
    item.analysisBandDisplayName =
        semanticAnalysisBandDisplayName(item.symbolRecord.analysisBand);
    item.defaultValue = symbolName;
    item.description = symbolPresentation(requestedKind)
        .typeDescription
        .split(' ')
        .value(0);

    if (requestedKind == CompletionCommandKind::Module) {
        item.defaultValue = moduleInstantiationText(symbolName);
        item.description = QStringLiteral("module instantiation");
        item.text = symbolName;
        item.uniqueKey = symbolName;
    } else if (requestedKind == CompletionCommandKind::Package) {
        item.defaultValue = QStringLiteral("import %1::*;").arg(symbolName);
        item.description = QStringLiteral("package import");
        item.text = symbolName;
        item.uniqueKey = symbolName;
    } else if (requestedKind == CompletionCommandKind::PackedStructVariable
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
    case CompletionActivationMode::CommandMode:
        state.action = CompletionActivationAction::ReplaceCommandInput;
        state.text = query.defaultValue.isEmpty()
            ? query.itemText
            : query.defaultValue;
        state.selectionStart = query.selectionStart;
        state.selectionLength = query.selectionLength;
        state.templateSlots = query.templateSlots;
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
        if (query.mode == CompletionActivationMode::CommandMode) {
            state.action = CompletionPopupKeyAction::HidePopupAndClearCommand;
        } else {
            state.action = CompletionPopupKeyAction::HidePopup;
        }
        return state;
    case Qt::Key_Backspace:
        return state;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        state.action = query.hasRows
            ? CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable
            : CompletionPopupKeyAction::Consume;
        return state;
    case Qt::Key_Tab:
        state.action = query.hasRows
            ? CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable
            : CompletionPopupKeyAction::Consume;
        return state;
    default:
        return state;
    }
}
