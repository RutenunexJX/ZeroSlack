#include "inlinecommandmode.h"

#include "actionregistry.h"

namespace {
bool lexicalPositionInCommentOrString(const QString& text, int position)
{
    bool inString = false;
    bool inBlockComment = false;
    bool escaped = false;

    const int limit = qBound(0, position, text.size());
    for (int i = 0; i < limit; ++i) {
        const QChar ch = text.at(i);
        const QChar next = (i + 1 < text.size()) ? text.at(i + 1) : QChar();

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

        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
            continue;
        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            const int newline = text.indexOf(QLatin1Char('\n'), i + 2);
            if (newline < 0 || newline >= limit)
                return true;
            i = newline;
            continue;
        }
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

bool isSingleSemicolonToken(const QString& token)
{
    return token.startsWith(QLatin1Char(';'))
        && !token.startsWith(QStringLiteral(";;"));
}

bool isProtectedSingleSemicolonSuffix(const QString& text,
                                      int prefixPosition,
                                      const QString& commandToken)
{
    return isSingleSemicolonToken(commandToken)
        && prefixPosition > 0
        && text.at(prefixPosition - 1) == QLatin1Char(';');
}

bool isBetterInlineMatch(const InlineCommandMatch& candidate,
                         const InlineCommandMatch& best)
{
    if (!best.matched)
        return true;
    if (candidate.prefixPosition != best.prefixPosition)
        return candidate.prefixPosition > best.prefixPosition;
    if (candidate.commandToken.size() != best.commandToken.size())
        return candidate.commandToken.size() > best.commandToken.size();
    if (candidate.descriptor.prefix.size() != best.descriptor.prefix.size())
        return candidate.descriptor.prefix.size() > best.descriptor.prefix.size();
    if (candidate.intent != best.intent)
        return candidate.intent == InlineCommandIntent::CodeTemplate;
    return false;
}

QString descriptorCommandToken(const InlineCommandDescriptor& descriptor)
{
    if (!descriptor.label.isEmpty())
        return descriptor.label;
    return descriptor.prefix.trimmed();
}

void considerDescriptorMatch(InlineCommandMatch& best,
                             const QString& textBeforeCursor,
                             const InlineCommandDescriptor& descriptor)
{
    const QString commandToken = descriptorCommandToken(descriptor);
    if (commandToken.isEmpty())
        return;

    const bool acceptsQuery = descriptor.prefix.endsWith(QLatin1Char(' '));
    const int endPosition = textBeforeCursor.size();
    QList<int> starts;

    if (textBeforeCursor.endsWith(commandToken))
        starts.append(endPosition - commandToken.size());

    if (acceptsQuery) {
        const QString queryPrefix = commandToken + QLatin1Char(' ');
        int start = textBeforeCursor.indexOf(queryPrefix);
        while (start >= 0) {
            starts.append(start);
            start = textBeforeCursor.indexOf(queryPrefix, start + 1);
        }
    }

    for (const int start : starts) {
        if (start < 0 || start >= endPosition)
            continue;
        if (isProtectedSingleSemicolonSuffix(textBeforeCursor,
                                            start,
                                            commandToken)) {
            continue;
        }
        const int queryStart = start + commandToken.size();
        if (queryStart < endPosition
            && textBeforeCursor.at(queryStart) != QLatin1Char(' ')) {
            continue;
        }

        InlineCommandMatch candidate;
        candidate.matched = true;
        candidate.intent = descriptor.intent;
        candidate.prefixPosition = start;
        candidate.endPosition = endPosition;
        candidate.commandToken = commandToken;
        candidate.descriptor = descriptor;
        candidate.input = queryStart < endPosition
            ? textBeforeCursor.mid(queryStart + 1)
            : QString();
        if (isBetterInlineMatch(candidate, best))
            best = candidate;
    }
}

void considerInlineMatch(InlineCommandMatch& best,
                         const InlineCommandMatch& candidate)
{
    if (candidate.matched && isBetterInlineMatch(candidate, best))
        best = candidate;
}

InlineCommandDescriptor descriptor(
    const QString& prefix,
    InlineCommandIntent intent,
    CompletionCommandKind kind,
    const QString& label,
    const QString& description,
    const QString& defaultValue)
{
    InlineCommandDescriptor item;
    item.prefix = prefix;
    item.intent = intent;
    item.semanticKind = kind;
    item.label = label;
    item.description = description;
    item.defaultValue = defaultValue;
    return item;
}

CompletionCommandKind completionKindForAdapterKey(
    const QString& adapterKey)
{
    if (adapterKey == QStringLiteral("visibleSymbol"))
        return CompletionCommandKind::VisibleSymbol;
    if (adapterKey == QStringLiteral("reg"))
        return CompletionCommandKind::Reg;
    if (adapterKey == QStringLiteral("wire"))
        return CompletionCommandKind::Wire;
    if (adapterKey == QStringLiteral("logic"))
        return CompletionCommandKind::Logic;
    if (adapterKey == QStringLiteral("module"))
        return CompletionCommandKind::Module;
    if (adapterKey == QStringLiteral("task"))
        return CompletionCommandKind::Task;
    if (adapterKey == QStringLiteral("function"))
        return CompletionCommandKind::Function;
    if (adapterKey == QStringLiteral("interface"))
        return CompletionCommandKind::Interface;
    if (adapterKey == QStringLiteral("package"))
        return CompletionCommandKind::Package;
    if (adapterKey == QStringLiteral("macro"))
        return CompletionCommandKind::Macro;
    if (adapterKey == QStringLiteral("localparam"))
        return CompletionCommandKind::Localparam;
    if (adapterKey == QStringLiteral("parameter"))
        return CompletionCommandKind::Parameter;
    if (adapterKey == QStringLiteral("always"))
        return CompletionCommandKind::AlwaysProcess;
    if (adapterKey == QStringLiteral("continuousAssign"))
        return CompletionCommandKind::ContinuousAssign;
    if (adapterKey == QStringLiteral("typedef"))
        return CompletionCommandKind::Typedef;
    if (adapterKey == QStringLiteral("enumValue"))
        return CompletionCommandKind::EnumValue;
    if (adapterKey == QStringLiteral("enumType"))
        return CompletionCommandKind::EnumType;
    if (adapterKey == QStringLiteral("enumVariable"))
        return CompletionCommandKind::EnumVariable;
    if (adapterKey == QStringLiteral("structMember"))
        return CompletionCommandKind::StructMember;
    if (adapterKey == QStringLiteral("packedStructType"))
        return CompletionCommandKind::PackedStructType;
    if (adapterKey == QStringLiteral("unpackedStructType"))
        return CompletionCommandKind::UnpackedStructType;
    if (adapterKey == QStringLiteral("packedStructVariable"))
        return CompletionCommandKind::PackedStructVariable;
    if (adapterKey == QStringLiteral("unpackedStructVariable"))
        return CompletionCommandKind::UnpackedStructVariable;
    return CompletionCommandKind::User;
}

InlineCommandIntent inlineIntentForAlias(
    const ActionAliasDescriptor& alias)
{
    if (alias.intentKey == QStringLiteral("template"))
        return InlineCommandIntent::CodeTemplate;
    if (alias.intentKey == QStringLiteral("headerInclude"))
        return InlineCommandIntent::HeaderInclude;
    if (alias.intentKey == QStringLiteral("packageImport"))
        return InlineCommandIntent::PackageImport;
    if (alias.intentKey == QStringLiteral("editorAction"))
        return InlineCommandIntent::EditorAction;
    return InlineCommandIntent::SemanticCompletion;
}

QList<InlineCommandDescriptor> inlineDescriptorsForSurface(
    ActionSurface surface,
    const QString& intentKey = QString())
{
    QList<InlineCommandDescriptor> result;
    for (const ActionDescriptor* action :
         actionDescriptorsForSurface(surface)) {
        if (!action)
            continue;
        for (const ActionAliasDescriptor& actionAlias :
             action->aliases) {
            if (actionAlias.surface != surface
                || !actionAlias.triggerAdapter
                || (!intentKey.isEmpty()
                    && actionAlias.intentKey != intentKey)) {
                continue;
            }
            InlineCommandDescriptor item =
                descriptor(actionAlias.token + QLatin1Char(' '),
                           inlineIntentForAlias(actionAlias),
                           completionKindForAdapterKey(
                               actionAlias.adapterKey),
                           actionAlias.token,
                           actionAlias.description.isEmpty()
                               ? action->description
                               : actionAlias.description,
                           actionAlias.defaultValue);
            item.actionId = action->id;
            item.executionRoute = action->executionRoute;
            result.append(item);
        }
    }
    return result;
}

bool isHelpToken(const QString& token, InlineCommandIntent* intent)
{
    if (token == QStringLiteral(";?")) {
        *intent = InlineCommandIntent::SemanticCompletion;
        return true;
    }
    if (token == QStringLiteral(";;?")) {
        *intent = InlineCommandIntent::CodeTemplate;
        return true;
    }
    return false;
}
}

QList<InlineCommandDescriptor> InlineCommandMode::descriptors()
{
    QList<InlineCommandDescriptor> result =
        inlineDescriptorsForSurface(
            ActionSurface::InlineSemantic,
            QStringLiteral("semantic"));
    result.append(
        inlineDescriptorsForSurface(
            ActionSurface::InlineTemplate,
            QStringLiteral("template")));
    result.append(
        inlineDescriptorsForSurface(
            ActionSurface::InlineSemantic,
            QStringLiteral("headerInclude")));
    result.append(
        inlineDescriptorsForSurface(
            ActionSurface::InlineSemantic,
            QStringLiteral("packageImport")));
    return result;
}

QList<InlineCommandDescriptor> InlineCommandMode::descriptorsForIntent(
    InlineCommandIntent intent)
{
    QList<InlineCommandDescriptor> result;
    for (const InlineCommandDescriptor& descriptor : descriptors()) {
        if (descriptor.intent == intent
            && (descriptor.prefix.endsWith(QLatin1Char(' '))
                || descriptor.intent == InlineCommandIntent::EditorAction))
            result.append(descriptor);
    }
    return result;
}

InlineCommandMatch InlineCommandMode::match(const QString& lineUpToCursor)
{
    return matchAbbreviationBeforeCursor(lineUpToCursor);
}

InlineCommandMatch InlineCommandMode::matchAbbreviationBeforeCursor(
    const QString& textBeforeCursor)
{
    return matchAbbreviationBeforeCursor(textBeforeCursor, descriptors());
}

InlineCommandMatch InlineCommandMode::matchAbbreviationBeforeCursor(
    const QString& textBeforeCursor,
    const QList<InlineCommandDescriptor>& registry)
{
    InlineCommandMatch result;

    InlineCommandIntent helpIntent = InlineCommandIntent::SemanticCompletion;
    for (const QString& helpToken :
         {QStringLiteral(";;?"), QStringLiteral(";?")}) {
        const int prefixPosition = textBeforeCursor.lastIndexOf(helpToken);
        if (prefixPosition < 0
            || prefixPosition + helpToken.size() != textBeforeCursor.size()) {
            continue;
        }
        if (isProtectedSingleSemicolonSuffix(textBeforeCursor,
                                            prefixPosition,
                                            helpToken)) {
            continue;
        }
        if (!isHelpToken(helpToken, &helpIntent))
            continue;

        InlineCommandMatch candidate;
        candidate.matched = true;
        candidate.helpRequested = true;
        candidate.intent = helpIntent;
        candidate.prefixPosition = prefixPosition;
        candidate.endPosition = textBeforeCursor.size();
        candidate.commandToken = helpToken;
        candidate.input = QStringLiteral("?");
        const ActionSurface helpSurface =
            helpIntent == InlineCommandIntent::CodeTemplate
            ? ActionSurface::InlineTemplate
            : ActionSurface::InlineSemantic;
        const ActionDescriptor* helpAction =
            findActionByAlias(helpSurface, helpToken);
        candidate.descriptor =
            descriptor(helpToken,
                       helpIntent,
                       CompletionCommandKind::User,
                       helpToken,
                       helpAction ? helpAction->description
                                  : QStringLiteral(
                                        "inline command help"),
                       helpToken);
        if (helpAction) {
            candidate.descriptor.actionId = helpAction->id;
            candidate.descriptor.executionRoute =
                helpAction->executionRoute;
        }
        considerInlineMatch(result, candidate);
    }

    for (const InlineCommandDescriptor& descriptor : registry)
        considerDescriptorMatch(result, textBeforeCursor, descriptor);

    return result;
}

bool InlineCommandMode::isPositionInCommentOrString(const QString& text,
                                                    int position)
{
    return lexicalPositionInCommentOrString(text, position);
}

CommandModeCommand InlineCommandMode::toCommandModeCommand(
    const InlineCommandDescriptor& descriptor)
{
    return {descriptor.prefix,
            descriptor.semanticKind,
            descriptor.description,
            descriptor.defaultValue};
}

QString InlineCommandMode::headerText(const InlineCommandDescriptor& descriptor)
{
    switch (descriptor.intent) {
    case InlineCommandIntent::SemanticCompletion:
        return QStringLiteral(":: COMMAND MODE - %1 ::").arg(descriptor.description);
    case InlineCommandIntent::CodeTemplate:
        return QStringLiteral(":: TEMPLATE MODE - %1 ::").arg(descriptor.description);
    case InlineCommandIntent::EditorAction:
        return QStringLiteral(":: ACTION MODE ::");
    case InlineCommandIntent::HeaderInclude:
        return QStringLiteral(":: HEADER INCLUDE - `include ::");
    case InlineCommandIntent::PackageImport:
        return QStringLiteral(":: PACKAGE IMPORT - import pkg::* ::");
    }
    return QStringLiteral(":: COMMAND MODE ::");
}
