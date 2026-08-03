#ifndef COMMANDLAYERCOMMANDREGISTRY_H
#define COMMANDLAYERCOMMANDREGISTRY_H

#include "actionregistry.h"

#include <QHash>
#include <QList>
#include <QString>

#include <functional>

enum class CommandLayerCommandInputKind {
    Fixed,
    PositiveInteger
};

struct CommandLayerCommandMetadata {
    QString name;
    QString description;
    CommandLayerCommandInputKind inputKind =
        CommandLayerCommandInputKind::Fixed;
    QString actionId;
    QString executionRoute;
};

enum class CommandLayerMatchRank {
    Subsequence = 1,
    WordPrefix = 2,
    Prefix = 3,
    Exact = 4
};

struct CommandLayerCommandMatch {
    CommandLayerCommandMetadata command;
    CommandLayerMatchRank rank = CommandLayerMatchRank::Subsequence;
    int skippedCharacters = 0;
    int registryIndex = -1;
};

enum class CommandLayerLineParseState {
    NotLineQuery,
    Valid,
    Invalid
};

struct CommandLayerLineParseResult {
    CommandLayerLineParseState state =
        CommandLayerLineParseState::NotLineQuery;
    int line = -1;
    QString failureReason;
};

class CommandLayerActionExecutionHost final : public ActionExecutionHost
{
public:
    using RouteHandler = std::function<ActionExecutionResult(
        const ActionDescriptor&,
        const ActionInvocation&)>;

    bool bindRoute(const QString& route,
                   RouteHandler handler,
                   QString* failureReason = nullptr);
    bool hasRoute(const QString& route) const;
    void setFallbackHost(ActionExecutionHost* host);
    bool hasFallbackHost() const;

    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override;

private:
    QHash<QString, RouteHandler> routeHandlers;
    ActionExecutionHost* fallbackHost = nullptr;
};

const QList<CommandLayerCommandMetadata>& commandLayerCommandRegistry();
const CommandLayerCommandMetadata* findCommandLayerCommand(
    const QString& canonicalName);
bool validateCommandLayerCommandRegistry(
    const QList<CommandLayerCommandMetadata>& registry,
    QString* reason = nullptr);
bool commandLayerCommandRegistryIsValid(QString* reason = nullptr);
QList<CommandLayerCommandMatch> commandLayerCommandMatches(
    const QString& query);
CommandLayerLineParseResult parseCommandLayerLineQuery(
    const QString& query);
ActionExecutionResult executeCommandLayerCommand(
    const CommandLayerCommandMetadata& command,
    ActionExecutionHost& host,
    const ActionInvocation& invocation = {});
QString commandLayerMatchRankName(CommandLayerMatchRank rank);

#endif // COMMANDLAYERCOMMANDREGISTRY_H
