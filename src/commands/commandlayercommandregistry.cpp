#include "commandlayercommandregistry.h"

#include "actionregistry.h"

#include <QStringList>
#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

namespace {
QString normalizedCommandText(const QString& text)
{
    QString normalized;
    normalized.reserve(text.size());
    for (const QChar ch : text.toCaseFolded()) {
        if (ch.isLetterOrNumber())
            normalized.append(ch);
    }
    return normalized;
}

QStringList commandWords(const QString& name)
{
    QStringList words;
    QString word;
    for (const QChar ch : name.toCaseFolded()) {
        if (ch.isLetterOrNumber()) {
            word.append(ch);
        } else if (!word.isEmpty()) {
            words.append(word);
            word.clear();
        }
    }
    if (!word.isEmpty())
        words.append(word);
    return words;
}

bool wordAbbreviationChunkMatches(const QString& word,
                                  const QString& chunk)
{
    if (word.isEmpty() || chunk.isEmpty()
        || word.at(0) != chunk.at(0)) {
        return false;
    }
    int wordIndex = 1;
    for (int i = 1; i < chunk.size(); ++i) {
        wordIndex = word.indexOf(chunk.at(i), wordIndex);
        if (wordIndex < 0)
            return false;
        ++wordIndex;
    }
    return true;
}

bool isWordPrefixAbbreviation(const QStringList& words,
                              const QString& query)
{
    if (words.isEmpty() || query.isEmpty()
        || query.size() < words.size()) {
        return false;
    }

    std::function<bool(int, int)> matchFrom =
        [&](int wordIndex, int queryIndex) {
            if (wordIndex == words.size())
                return queryIndex == query.size();

            const int remainingWords = words.size() - wordIndex - 1;
            const int available = query.size() - queryIndex;
            const int maxLength = qMin(words.at(wordIndex).size(),
                                       available - remainingWords);
            for (int length = 1; length <= maxLength; ++length) {
                if (wordAbbreviationChunkMatches(
                        words.at(wordIndex),
                        query.mid(queryIndex, length))
                    && matchFrom(wordIndex + 1,
                                 queryIndex + length)) {
                    return true;
                }
            }
            return false;
        };
    return matchFrom(0, 0);
}

bool isExactWordInitialism(const QStringList& words, const QString& query)
{
    if (words.size() != query.size())
        return false;
    for (int i = 0; i < words.size(); ++i) {
        if (words.at(i).isEmpty() || words.at(i).at(0) != query.at(i))
            return false;
    }
    return true;
}

bool subsequenceMatch(const QString& command,
                      const QString& query,
                      int* skippedCharacters)
{
    int commandIndex = 0;
    int previousMatch = -1;
    int skipped = 0;
    for (const QChar queryChar : query) {
        const int match = command.indexOf(queryChar, commandIndex);
        if (match < 0)
            return false;
        if (previousMatch >= 0)
            skipped += match - previousMatch - 1;
        else
            skipped += match;
        previousMatch = match;
        commandIndex = match + 1;
    }
    skipped += command.size() - commandIndex;
    if (skippedCharacters)
        *skippedCharacters = skipped;
    return true;
}

void setReason(QString* reason, const QString& message)
{
    if (reason)
        *reason = message;
}

ActionExecutionResult failedExecution(const QString& reason)
{
    ActionExecutionResult result;
    result.handled = true;
    result.failureReason = reason;
    return result;
}

} // namespace

bool CommandLayerActionExecutionHost::bindRoute(
    const QString& route,
    RouteHandler handler,
    QString* failureReason)
{
    const QString normalized = route.trimmed();
    if (normalized.isEmpty()) {
        setReason(failureReason,
                  QStringLiteral("Command Layer route is empty"));
        return false;
    }
    if (!handler) {
        setReason(
            failureReason,
            QStringLiteral("Command Layer route has no handler: %1")
                .arg(normalized));
        return false;
    }
    if (routeHandlers.contains(normalized)) {
        setReason(
            failureReason,
            QStringLiteral("Duplicate Command Layer route: %1")
                .arg(normalized));
        return false;
    }
    routeHandlers.insert(normalized, std::move(handler));
    if (failureReason)
        failureReason->clear();
    return true;
}

bool CommandLayerActionExecutionHost::hasRoute(
    const QString& route) const
{
    return routeHandlers.contains(route.trimmed());
}

void CommandLayerActionExecutionHost::setFallbackHost(
    ActionExecutionHost* host)
{
    fallbackHost =
        host == this ? nullptr : host;
}

bool CommandLayerActionExecutionHost::hasFallbackHost() const
{
    return fallbackHost != nullptr;
}

ActionExecutionResult
CommandLayerActionExecutionHost::executeActionRoute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    const QString route = descriptor.executionRoute.trimmed();
    const auto handler = routeHandlers.constFind(route);
    ActionExecutionResult result;
    if (handler == routeHandlers.cend()) {
        if (!fallbackHost) {
            return failedExecution(
                QStringLiteral(
                    "No Command Layer execution handler for route: %1")
                    .arg(route.isEmpty()
                             ? QStringLiteral("<empty>")
                             : route));
        }
        result = fallbackHost->executeActionRoute(
            descriptor, invocation);
    } else {
        result = (*handler)(descriptor, invocation);
    }

    if (!result.handled) {
        return failedExecution(
            QStringLiteral(
                "Command Layer route did not handle action %1: %2")
                .arg(descriptor.id, route));
    }
    if (!result.succeeded
        && result.failureReason.trimmed().isEmpty()) {
        result.failureReason = result.message.trimmed().isEmpty()
            ? QStringLiteral("Command Layer action failed: %1")
                  .arg(descriptor.id)
            : result.message;
    }
    return result;
}

const QList<CommandLayerCommandMetadata>& commandLayerCommandRegistry()
{
    static const QList<CommandLayerCommandMetadata> registry = [] {
        QList<CommandLayerCommandMetadata> result;
        for (const ActionDescriptor* descriptor :
             actionDescriptorsForSurface(ActionSurface::CommandLayer)) {
            if (!descriptor)
                continue;
            const ActionAliasDescriptor* commandAlias =
                findActionAlias(*descriptor,
                                ActionSurface::CommandLayer);
            if (!commandAlias || !commandAlias->triggerAdapter)
                continue;
            CommandLayerCommandMetadata metadata;
            metadata.name = commandAlias->token;
            metadata.description = descriptor->description;
            metadata.inputKind =
                descriptor->parameterModel.kind
                        == ActionParameterKind::PositiveInteger
                ? CommandLayerCommandInputKind::PositiveInteger
                : CommandLayerCommandInputKind::Fixed;
            metadata.actionId = descriptor->id;
            metadata.executionRoute =
                descriptor->executionRoute;
            result.append(metadata);
        }
        return result;
    }();
    return registry;
}

const CommandLayerCommandMetadata* findCommandLayerCommand(
    const QString& canonicalName)
{
    const QString normalized = normalizedCommandText(canonicalName);
    for (const CommandLayerCommandMetadata& command :
         commandLayerCommandRegistry()) {
        if (normalizedCommandText(command.name) == normalized)
            return &command;
    }
    return nullptr;
}

bool validateCommandLayerCommandRegistry(
    const QList<CommandLayerCommandMetadata>& registry,
    QString* reason)
{
    if (registry.isEmpty()) {
        setReason(reason, QStringLiteral("Command Layer registry is empty"));
        return false;
    }

    for (int i = 0; i < registry.size(); ++i) {
        const CommandLayerCommandMetadata& left = registry.at(i);
        const QString normalized = normalizedCommandText(left.name);
        if (normalized.isEmpty()) {
            setReason(reason,
                      QStringLiteral("Command Layer command name is empty"));
            return false;
        }
        if (left.description.trimmed().isEmpty()) {
            setReason(reason,
                      QStringLiteral("Command Layer command has no description: %1")
                          .arg(left.name));
            return false;
        }
        const ActionDescriptor* descriptor =
            findActionById(left.actionId);
        if (!descriptor
            || left.executionRoute != descriptor->executionRoute
            || !findActionAlias(*descriptor,
                                ActionSurface::CommandLayer,
                                left.name)) {
            setReason(reason,
                      QStringLiteral(
                          "Command Layer adapter is not registered: %1")
                          .arg(left.name));
            return false;
        }
        for (int j = i + 1; j < registry.size(); ++j) {
            const CommandLayerCommandMetadata& right = registry.at(j);
            if (normalized == normalizedCommandText(right.name)) {
                setReason(reason,
                          QStringLiteral("Duplicate Command Layer command: %1")
                              .arg(left.name));
                return false;
            }
            if (left.actionId == right.actionId) {
                setReason(reason,
                          QStringLiteral("Duplicate Command Layer action: %1")
                              .arg(left.name));
                return false;
            }
        }
    }

    if (reason)
        reason->clear();
    return true;
}

bool commandLayerCommandRegistryIsValid(QString* reason)
{
    return validateCommandLayerCommandRegistry(commandLayerCommandRegistry(),
                                               reason);
}

QList<CommandLayerCommandMatch> commandLayerCommandMatches(
    const QString& query)
{
    QList<CommandLayerCommandMatch> matches;
    if (!commandLayerCommandRegistryIsValid())
        return matches;

    const QString normalizedQuery = normalizedCommandText(query);
    const QList<CommandLayerCommandMetadata>& registry =
        commandLayerCommandRegistry();
    for (int i = 0; i < registry.size(); ++i) {
        const CommandLayerCommandMetadata& command = registry.at(i);
        const QString normalizedName = normalizedCommandText(command.name);
        CommandLayerCommandMatch match;
        match.command = command;
        match.registryIndex = i;

        if (normalizedQuery.isEmpty()) {
            match.rank = CommandLayerMatchRank::Prefix;
            match.skippedCharacters = normalizedName.size();
        } else if (normalizedName == normalizedQuery) {
            match.rank = CommandLayerMatchRank::Exact;
        } else if (normalizedName.startsWith(normalizedQuery)) {
            match.rank = CommandLayerMatchRank::Prefix;
            match.skippedCharacters =
                normalizedName.size() - normalizedQuery.size();
        } else if (const QStringList words = commandWords(command.name);
                   isWordPrefixAbbreviation(words, normalizedQuery)) {
            match.rank = CommandLayerMatchRank::WordPrefix;
            match.exactWordInitials =
                isExactWordInitialism(words, normalizedQuery);
            match.skippedCharacters =
                normalizedName.size() - normalizedQuery.size();
        } else {
            int skipped = 0;
            if (!subsequenceMatch(normalizedName,
                                  normalizedQuery,
                                  &skipped)) {
                continue;
            }
            match.rank = CommandLayerMatchRank::Subsequence;
            match.skippedCharacters = skipped;
        }
        matches.append(match);
    }

    std::stable_sort(matches.begin(), matches.end(),
                     [](const CommandLayerCommandMatch& left,
                        const CommandLayerCommandMatch& right) {
        if (left.rank != right.rank) {
            return static_cast<int>(left.rank)
                > static_cast<int>(right.rank);
        }
        if (left.exactWordInitials != right.exactWordInitials)
            return left.exactWordInitials;
        if (left.skippedCharacters != right.skippedCharacters)
            return left.skippedCharacters < right.skippedCharacters;
        return left.registryIndex < right.registryIndex;
    });
    if (!matches.isEmpty()) {
        const CommandLayerMatchRank strongestRank = matches.first().rank;
        while (!matches.isEmpty()
               && matches.constLast().rank != strongestRank) {
            matches.removeLast();
        }
    }
    return matches;
}

CommandLayerLineParseResult parseCommandLayerLineQuery(
    const QString& query)
{
    CommandLayerLineParseResult result;
    const QString input = query.trimmed().toCaseFolded();
    int offset = 0;
    if (input.startsWith(QStringLiteral("go"))) {
        offset = 2;
    } else if (input.startsWith(QLatin1Char('g'))) {
        offset = 1;
    } else {
        return result;
    }

    while (offset < input.size() && input.at(offset).isSpace())
        ++offset;
    if (offset >= input.size())
        return result;

    QString numberText;
    if (input.at(offset) == QLatin1Char('+')
        || input.at(offset) == QLatin1Char('-')) {
        numberText.append(input.at(offset));
        ++offset;
        while (offset < input.size() && input.at(offset).isSpace())
            ++offset;
    }
    const int digitStart = offset;
    while (offset < input.size() && input.at(offset).isDigit()) {
        numberText.append(input.at(offset));
        ++offset;
    }
    if (digitStart == offset || offset != input.size())
        return result;

    result.state = CommandLayerLineParseState::Invalid;
    bool ok = false;
    const qlonglong line = numberText.toLongLong(&ok);
    if (!ok || line > std::numeric_limits<int>::max()) {
        result.failureReason = QStringLiteral("Line number is too large");
        return result;
    }
    if (line < 1) {
        result.failureReason = QStringLiteral("Line number must be >= 1");
        return result;
    }

    result.state = CommandLayerLineParseState::Valid;
    result.line = static_cast<int>(line);
    return result;
}

ActionExecutionResult executeCommandLayerCommand(
    const CommandLayerCommandMetadata& command,
    ActionExecutionHost& host,
    const ActionInvocation& invocation)
{
    const ActionDescriptor* descriptor =
        findActionById(command.actionId);
    if (!descriptor) {
        return failedExecution(
            QStringLiteral(
                "Command Layer action is not registered: %1")
                .arg(command.actionId.isEmpty()
                         ? QStringLiteral("<empty>")
                         : command.actionId));
    }
    if (!findActionAlias(*descriptor,
                         ActionSurface::CommandLayer,
                         command.name)) {
        return failedExecution(
            QStringLiteral(
                "Action is not registered for this Command Layer command: %1")
                .arg(command.name));
    }
    if (command.executionRoute != descriptor->executionRoute) {
        return failedExecution(
            QStringLiteral(
                "Command Layer route is stale for action %1")
                .arg(command.actionId));
    }
    return executeAction(*descriptor, host, invocation);
}

QString commandLayerMatchRankName(CommandLayerMatchRank rank)
{
    switch (rank) {
    case CommandLayerMatchRank::Exact:
        return QStringLiteral("exact");
    case CommandLayerMatchRank::Prefix:
        return QStringLiteral("prefix");
    case CommandLayerMatchRank::WordPrefix:
        return QStringLiteral("word prefix");
    case CommandLayerMatchRank::Subsequence:
        return QStringLiteral("subsequence");
    }
    return QStringLiteral("subsequence");
}
