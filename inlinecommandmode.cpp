#include "inlinecommandmode.h"

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
        descriptor(QStringLiteral(";m "), InlineCommandIntent::SemanticCompletion, CompletionCommandKind::Module, QStringLiteral(";m"), QStringLiteral("modules"), QStringLiteral("module")),
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
        result.append(next);
    }
    return result;
}

QList<InlineCommandDescriptor> actionDescriptors()
{
    return {
        descriptor(QStringLiteral(";:?"), InlineCommandIntent::EditorAction, CompletionCommandKind::User, QStringLiteral(";:?"), QStringLiteral("editor action help"), QStringLiteral(";:?")),
        descriptor(QStringLiteral(";:fd"), InlineCommandIntent::EditorAction, CompletionCommandKind::User, QStringLiteral(";:fd"), QStringLiteral("create custom fold region"), QStringLiteral(";:fd")),
        descriptor(QStringLiteral(";:fds"), InlineCommandIntent::EditorAction, CompletionCommandKind::User, QStringLiteral(";:fds"), QStringLiteral("open fold block shelf"), QStringLiteral(";:fds")),
        descriptor(QStringLiteral(";:refs "), InlineCommandIntent::EditorAction, CompletionCommandKind::User, QStringLiteral(";:refs"), QStringLiteral("find references action"), QStringLiteral(";:refs ")),
    };
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
    if (token == QStringLiteral(";:?")) {
        *intent = InlineCommandIntent::EditorAction;
        return true;
    }
    return false;
}
}

QList<InlineCommandDescriptor> InlineCommandMode::descriptors()
{
    QList<InlineCommandDescriptor> result = semanticDescriptors();
    result.append(templateDescriptors());
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
    InlineCommandMatch result;

    InlineCommandIntent helpIntent = InlineCommandIntent::SemanticCompletion;
    for (const QString& helpToken :
         {QStringLiteral(";:?"), QStringLiteral(";;?"), QStringLiteral(";?")}) {
        const int prefixPosition = lineUpToCursor.lastIndexOf(helpToken);
        if (prefixPosition < 0 || prefixPosition + helpToken.size() != lineUpToCursor.size())
            continue;
        if (!isCommandSafePrefix(lineUpToCursor.left(prefixPosition)))
            continue;
        if (isPositionInCommentOrString(lineUpToCursor, prefixPosition))
            continue;
        if (!isHelpToken(helpToken, &helpIntent))
            continue;

        result.matched = true;
        result.helpRequested = true;
        result.intent = helpIntent;
        result.prefixPosition = prefixPosition;
        result.commandToken = helpToken;
        result.input = QStringLiteral("?");
        result.descriptor = descriptor(helpToken,
                                       helpIntent,
                                       CompletionCommandKind::User,
                                       helpToken,
                                       QStringLiteral("inline command help"),
                                       helpToken);
        return result;
    }

    const QList<InlineCommandDescriptor> allDescriptors = descriptors();
    for (const InlineCommandDescriptor& descriptor : allDescriptors) {
        if (descriptor.prefix.endsWith(QLatin1Char(' ')))
            continue;
        const int prefixPosition = lineUpToCursor.lastIndexOf(descriptor.prefix);
        if (prefixPosition < 0 || prefixPosition + descriptor.prefix.size() != lineUpToCursor.size())
            continue;
        if (!isCommandSafePrefix(lineUpToCursor.left(prefixPosition)))
            continue;
        if (isPositionInCommentOrString(lineUpToCursor, prefixPosition))
            continue;

        result.matched = true;
        result.intent = descriptor.intent;
        result.prefixPosition = prefixPosition;
        result.commandToken = descriptor.label;
        result.descriptor = descriptor;
        result.input = QString();
        return result;
    }

    for (const InlineCommandDescriptor& descriptor : allDescriptors) {
        if (!descriptor.prefix.endsWith(QLatin1Char(' ')))
            continue;
        const int prefixPosition = lineUpToCursor.lastIndexOf(descriptor.prefix);
        if (prefixPosition < 0)
            continue;
        if (!isCommandSafePrefix(lineUpToCursor.left(prefixPosition)))
            continue;
        if (isPositionInCommentOrString(lineUpToCursor, prefixPosition))
            continue;

        result.matched = true;
        result.intent = descriptor.intent;
        result.prefixPosition = prefixPosition;
        result.commandToken = descriptor.label;
        result.descriptor = descriptor;
        result.input = lineUpToCursor.mid(prefixPosition + descriptor.prefix.length());
        return result;
    }

    return result;
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
    }
    return QStringLiteral(":: COMMAND MODE ::");
}
