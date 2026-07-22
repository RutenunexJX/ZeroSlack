#ifndef COMMANDLAYERCOMMANDREGISTRY_H
#define COMMANDLAYERCOMMANDREGISTRY_H

#include <QList>
#include <QString>

enum class CommandLayerCommandInputKind {
    Fixed,
    PositiveInteger
};

enum class CommandLayerCommandId {
    GoLine,
    GoModule,
    GoPackage,
    GoEndmodule,
    AddSignal,
    AddParameter,
    AddPort,
    ClearRight,
    SelectBeginEnd,
    Help
};

struct CommandLayerCommandMetadata {
    QString name;
    QString description;
    CommandLayerCommandInputKind inputKind =
        CommandLayerCommandInputKind::Fixed;
    CommandLayerCommandId id = CommandLayerCommandId::Help;
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
QString commandLayerMatchRankName(CommandLayerMatchRank rank);

#endif // COMMANDLAYERCOMMANDREGISTRY_H
