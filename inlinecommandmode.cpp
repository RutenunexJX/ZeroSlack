#include "inlinecommandmode.h"

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

QList<InlineCommandDescriptor> semanticDescriptors()
{
    return {
        descriptor(QStringLiteral(";r "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Reg, QStringLiteral(";r"), QStringLiteral("reg variables"), QStringLiteral("reg")),
        descriptor(QStringLiteral(";w "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Wire, QStringLiteral(";w"), QStringLiteral("wire variables"), QStringLiteral("wire")),
        descriptor(QStringLiteral(";l "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Logic, QStringLiteral(";l"), QStringLiteral("logic variables"), QStringLiteral("logic")),
        descriptor(QStringLiteral(";m "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Module, QStringLiteral(";m"), QStringLiteral("module instantiations"), QStringLiteral("module_name u_module_name (\n);")),
        descriptor(QStringLiteral(";t "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Task, QStringLiteral(";t"), QStringLiteral("tasks"), QStringLiteral("task")),
        descriptor(QStringLiteral(";f "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Function, QStringLiteral(";f"), QStringLiteral("functions"), QStringLiteral("function")),
        descriptor(QStringLiteral(";i "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Interface, QStringLiteral(";i"), QStringLiteral("interfaces"), QStringLiteral("interface")),
        descriptor(QStringLiteral(";d "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Macro, QStringLiteral(";d"), QStringLiteral("macro definitions"), QStringLiteral("`define")),
        descriptor(QStringLiteral(";lp "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Localparam, QStringLiteral(";lp"), QStringLiteral("localparam declarations"), QStringLiteral("localparam")),
        descriptor(QStringLiteral(";p "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Parameter, QStringLiteral(";p"), QStringLiteral("parameter declarations"), QStringLiteral("parameter")),
        descriptor(QStringLiteral(";a "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::AlwaysProcess, QStringLiteral(";a"), QStringLiteral("always blocks"), QStringLiteral("always")),
        descriptor(QStringLiteral(";c "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::ContinuousAssign, QStringLiteral(";c"), QStringLiteral("continuous assignments"), QStringLiteral("assign")),
        descriptor(QStringLiteral(";u "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Typedef, QStringLiteral(";u"), QStringLiteral("type definitions"), QStringLiteral("typedef")),
        descriptor(QStringLiteral(";ee "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::EnumValue, QStringLiteral(";ee"), QStringLiteral("enum values"), QStringLiteral("enum_value")),
        descriptor(QStringLiteral(";ne "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::EnumType, QStringLiteral(";ne"), QStringLiteral("enum types"), QStringLiteral("enum")),
        descriptor(QStringLiteral(";e "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::EnumVariable, QStringLiteral(";e"), QStringLiteral("enum variables"), QStringLiteral("enum_var")),
        descriptor(QStringLiteral(";sm "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::StructMember, QStringLiteral(";sm"), QStringLiteral("struct members"), QStringLiteral("member")),
        descriptor(QStringLiteral(";nsp "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::PackedStructType, QStringLiteral(";nsp"), QStringLiteral("packed struct types"), QStringLiteral("struct")),
        descriptor(QStringLiteral(";ns "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::UnpackedStructType, QStringLiteral(";ns"), QStringLiteral("unpacked struct types"), QStringLiteral("struct")),
        descriptor(QStringLiteral(";sp "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::PackedStructVariable, QStringLiteral(";sp"), QStringLiteral("packed struct variables"), QStringLiteral("struct")),
        descriptor(QStringLiteral(";s "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::UnpackedStructVariable, QStringLiteral(";s"), QStringLiteral("unpacked struct variables"), QStringLiteral("struct")),
    };
}

QList<InlineCommandDescriptor> templateDescriptors()
{
    QList<InlineCommandDescriptor> result;
    for (const InlineCommandDescriptor& item : semanticDescriptors()) {
        InlineCommandDescriptor next = item;
        next.prefix = QStringLiteral(";%1").arg(item.prefix);
        next.intent = InlineCommandIntent::CodeTemplate;
        next.label = QStringLiteral(";%1").arg(item.label);
        if (next.label == QStringLiteral(";;m")) {
            next.description = QStringLiteral("module template");
            next.defaultValue = QStringLiteral("`timescale 1ns / 1ps\n"
                                               "module name(\n"
                                               ");\n"
                                               "endmodule");
        }
        result.append(next);
    }
    return result;
}

QList<InlineCommandDescriptor> headerIncludeDescriptors()
{
    return {
        descriptor(QStringLiteral(";h "),
                   InlineCommandIntent::HeaderInclude,
                   CompletionCommandKind::User,
                   QStringLiteral(";h"),
                   QStringLiteral("header includes"),
                   QStringLiteral("`include \"...\""))
    };
}

QList<InlineCommandDescriptor> packageImportDescriptors()
{
    return {
        descriptor(QStringLiteral(";pk "),
                   InlineCommandIntent::PackageImport,
                   CompletionCommandKind::Package,
                   QStringLiteral(";pk"),
                   QStringLiteral("package imports"),
                   QStringLiteral("import package_name::*;"))
    };
}

QList<InlineCommandDescriptor> actionDescriptors()
{
    return {};
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
    QList<InlineCommandDescriptor> result = semanticDescriptors();
    result.append(templateDescriptors());
    result.append(headerIncludeDescriptors());
    result.append(packageImportDescriptors());
    result.append(actionDescriptors());
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
        candidate.descriptor = descriptor(helpToken,
                                          helpIntent,
                                          CompletionCommandKind::User,
                                          helpToken,
                                          QStringLiteral("inline command help"),
                                          helpToken);
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
