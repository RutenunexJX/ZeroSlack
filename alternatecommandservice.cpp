#include "alternatecommandservice.h"

#include <algorithm>

std::unique_ptr<AlternateCommandService> AlternateCommandService::instance = nullptr;

AlternateCommandService* AlternateCommandService::getInstance()
{
    if (!instance)
        instance = std::make_unique<AlternateCommandService>();
    return instance.get();
}

AlternateCommandService::AlternateCommandService()
    : commandCatalog{
        QStringLiteral("save"),
        QStringLiteral("save_as"),
        QStringLiteral("open"),
        QStringLiteral("new"),
        QStringLiteral("close"),
        QStringLiteral("copy"),
        QStringLiteral("paste"),
        QStringLiteral("cut"),
        QStringLiteral("undo"),
        QStringLiteral("redo"),
        QStringLiteral("find"),
        QStringLiteral("replace"),
        QStringLiteral("goto_line"),
        QStringLiteral("select_all"),
        QStringLiteral("comment"),
        QStringLiteral("uncomment"),
        QStringLiteral("indent"),
        QStringLiteral("unindent")}
{
}

AlternateCommandService::~AlternateCommandService() = default;

QString AlternateCommandService::normalizeCommandInput(const QString& input) const
{
    return input.trimmed().toLower();
}

QStringList AlternateCommandService::commands() const
{
    return commandCatalog;
}

QStringList AlternateCommandService::matchingCommands(const QString& filter) const
{
    const QString normalizedFilter = normalizeCommandInput(filter);
    if (normalizedFilter.isEmpty())
        return commandCatalog;

    QStringList matches;
    for (const QString& command : commandCatalog) {
        if (command.startsWith(normalizedFilter, Qt::CaseInsensitive))
            matches << command;
    }
    return matches;
}

AlternateCommandCompletionState AlternateCommandService::completionState(
    const QString& input) const
{
    AlternateCommandCompletionState state;
    state.normalizedInput = normalizeCommandInput(input);
    state.matches = matchingCommands(state.normalizedInput);
    state.showCompletions = !state.matches.isEmpty();
    return state;
}

bool AlternateCommandService::isKnownCommand(const QString& command) const
{
    const QString normalizedCommand = normalizeCommandInput(command);
    return std::any_of(commandCatalog.cbegin(), commandCatalog.cend(),
                       [&normalizedCommand](const QString& catalogCommand) {
                           return catalogCommand == normalizedCommand;
                       });
}

AlternateCommandAction AlternateCommandService::commandAction(
    const QString& command) const
{
    const QString normalizedCommand = normalizeCommandInput(command);
    if (normalizedCommand == QStringLiteral("save"))
        return AlternateCommandAction::Save;
    if (normalizedCommand == QStringLiteral("save_as"))
        return AlternateCommandAction::SaveAs;
    if (normalizedCommand == QStringLiteral("open"))
        return AlternateCommandAction::Open;
    if (normalizedCommand == QStringLiteral("new"))
        return AlternateCommandAction::NewFile;
    if (normalizedCommand == QStringLiteral("close"))
        return AlternateCommandAction::Close;
    if (normalizedCommand == QStringLiteral("copy"))
        return AlternateCommandAction::Copy;
    if (normalizedCommand == QStringLiteral("paste"))
        return AlternateCommandAction::Paste;
    if (normalizedCommand == QStringLiteral("cut"))
        return AlternateCommandAction::Cut;
    if (normalizedCommand == QStringLiteral("undo"))
        return AlternateCommandAction::Undo;
    if (normalizedCommand == QStringLiteral("redo"))
        return AlternateCommandAction::Redo;
    if (normalizedCommand == QStringLiteral("find"))
        return AlternateCommandAction::Find;
    if (normalizedCommand == QStringLiteral("replace"))
        return AlternateCommandAction::Replace;
    if (normalizedCommand == QStringLiteral("goto_line"))
        return AlternateCommandAction::GotoLine;
    if (normalizedCommand == QStringLiteral("select_all"))
        return AlternateCommandAction::SelectAll;
    if (normalizedCommand == QStringLiteral("comment"))
        return AlternateCommandAction::Comment;
    if (normalizedCommand == QStringLiteral("uncomment"))
        return AlternateCommandAction::Uncomment;
    if (normalizedCommand == QStringLiteral("indent"))
        return AlternateCommandAction::Indent;
    if (normalizedCommand == QStringLiteral("unindent"))
        return AlternateCommandAction::Unindent;
    return AlternateCommandAction::None;
}
