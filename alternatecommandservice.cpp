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

bool AlternateCommandService::isKnownCommand(const QString& command) const
{
    const QString normalizedCommand = normalizeCommandInput(command);
    return std::any_of(commandCatalog.cbegin(), commandCatalog.cend(),
                       [&normalizedCommand](const QString& catalogCommand) {
                           return catalogCommand == normalizedCommand;
                       });
}
