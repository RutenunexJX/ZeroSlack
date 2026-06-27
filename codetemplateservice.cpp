#include "codetemplateservice.h"

#include <Qt>
#include <memory>

namespace {
std::unique_ptr<CodeTemplateService> s_instance;

CodeTemplateItem makeItem(const QString& token,
                          const QString& label,
                          const QString& description,
                          const QString& defaultValue)
{
    CodeTemplateItem item;
    item.commandToken = token;
    item.label = label;
    item.description = description;
    item.defaultValue = defaultValue;
    return item;
}

CodeTemplateSlot makeSlot(const QString& name, int start, int length)
{
    CodeTemplateSlot slot;
    slot.name = name;
    slot.start = start;
    slot.length = length;
    return slot;
}

bool isIdentifierStart(QChar ch)
{
    return ch == QLatin1Char('$') || ch == QLatin1Char('_')
        || ch.isLetter();
}

bool isIdentifierPart(QChar ch)
{
    return isIdentifierStart(ch) || ch.isDigit();
}

bool isIdentifierLike(const QString& text)
{
    if (text.isEmpty())
        return false;
    if (text.startsWith(QLatin1Char('\\')))
        return text.size() > 1;
    if (!isIdentifierStart(text.at(0)))
        return false;
    for (int i = 1; i < text.size(); ++i) {
        if (!isIdentifierPart(text.at(i)))
            return false;
    }
    return true;
}

bool isUnsignedInteger(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

QStringList splitTemplateArgs(const QString& seedText)
{
    QStringList tokens;
    QString current;
    int bracketDepth = 0;
    for (const QChar ch : seedText) {
        if (ch.isSpace() && bracketDepth == 0) {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
            continue;
        }

        if (ch == QLatin1Char('['))
            ++bracketDepth;
        else if (ch == QLatin1Char(']'))
            bracketDepth = qMax(0, bracketDepth - 1);
        current.append(ch);
    }
    if (!current.isEmpty())
        tokens.append(current);
    return tokens;
}

QString dimensionFromToken(const QString& token, bool* matched)
{
    if (matched)
        *matched = false;
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty())
        return QString();

    if (trimmed.startsWith(QLatin1Char('['))
        && trimmed.endsWith(QLatin1Char(']'))
        && trimmed.size() > 2) {
        if (matched)
            *matched = true;
        return trimmed;
    }

    if (trimmed.startsWith(QLatin1Char(':')) && trimmed.size() > 1) {
        if (matched)
            *matched = true;
        const QString expr = trimmed.mid(1).trimmed();
        if (expr.contains(QLatin1Char(':')))
            return QStringLiteral("[%1]").arg(expr);
        return QStringLiteral("[%1 - 1:0]").arg(expr);
    }

    if (!isUnsignedInteger(trimmed))
        return QString();

    if (matched)
        *matched = true;
    const int width = trimmed.toInt();
    if (width <= 0)
        return QString();
    return QStringLiteral("[%1:0]").arg(width - 1);
}

bool isSignalDeclarationTemplate(const QString& commandToken)
{
    return commandToken == QStringLiteral(";;l")
        || commandToken == QStringLiteral(";;w")
        || commandToken == QStringLiteral(";;r");
}

QString signalKeywordForTemplate(const QString& commandToken)
{
    if (commandToken == QStringLiteral(";;w"))
        return QStringLiteral("wire");
    if (commandToken == QStringLiteral(";;r"))
        return QStringLiteral("reg");
    return QStringLiteral("logic");
}

CodeTemplateItem expandSignalDeclarationTemplate(CodeTemplateItem item,
                                                 const QString& seedText)
{
    const QStringList tokens = splitTemplateArgs(seedText);
    QStringList packedDimensions;
    QStringList unpackedDimensions;
    QString name;
    bool signedDeclaration = false;
    bool nameSeen = false;

    for (const QString& token : tokens) {
        if (token == QStringLiteral("-s")) {
            signedDeclaration = true;
            continue;
        }

        bool dimensionMatched = false;
        const QString dimension = dimensionFromToken(token, &dimensionMatched);
        if (dimensionMatched) {
            if (!dimension.isEmpty()) {
                if (nameSeen)
                    unpackedDimensions.append(dimension);
                else
                    packedDimensions.append(dimension);
            }
            continue;
        }

        if (!nameSeen && isIdentifierLike(token)) {
            name = token;
            nameSeen = true;
        }
    }

    if (name.isEmpty())
        name = QStringLiteral("sig");

    QString text = signalKeywordForTemplate(item.commandToken);
    if (signedDeclaration)
        text += QStringLiteral(" signed");
    if (!packedDimensions.isEmpty()) {
        text += QLatin1Char(' ');
        text += packedDimensions.join(QString());
    }
    text += QLatin1Char(' ');
    item.selectionStart = text.size();
    item.selectionLength = name.size();
    text += name;
    if (!unpackedDimensions.isEmpty()) {
        text += QLatin1Char(' ');
        text += unpackedDimensions.join(QString());
    }
    text += QLatin1Char(';');

    item.insertText = text;
    item.defaultValue = text;
    return item;
}

bool isParameterDeclarationTemplate(const QString& commandToken)
{
    return commandToken == QStringLiteral(";;p")
        || commandToken == QStringLiteral(";;lp");
}

QString parameterKeywordForTemplate(const QString& commandToken)
{
    return commandToken == QStringLiteral(";;lp")
        ? QStringLiteral("localparam")
        : QStringLiteral("parameter");
}

QStringList parameterTypeNames()
{
    return {
        QStringLiteral("int"),
        QStringLiteral("integer"),
        QStringLiteral("logic"),
        QStringLiteral("bit"),
        QStringLiteral("byte"),
        QStringLiteral("shortint"),
        QStringLiteral("longint"),
    };
}

bool fuzzyMatchInOrder(const QString& candidate, const QString& needle)
{
    if (needle.isEmpty())
        return true;

    int candidateIndex = 0;
    const QString loweredCandidate = candidate.toLower();
    const QString loweredNeedle = needle.toLower();
    for (const QChar needleChar : loweredNeedle) {
        bool found = false;
        while (candidateIndex < loweredCandidate.size()) {
            if (loweredCandidate.at(candidateIndex) == needleChar) {
                found = true;
                ++candidateIndex;
                break;
            }
            ++candidateIndex;
        }
        if (!found)
            return false;
    }
    return true;
}

bool parameterTypeCompletionPrefix(const QString& seedText,
                                   QString* typePrefix)
{
    const QStringList tokens = splitTemplateArgs(seedText);
    if (tokens.size() != 1)
        return false;
    if (!seedText.isEmpty() && seedText.at(seedText.size() - 1).isSpace())
        return false;

    const QString token = tokens.first().trimmed();
    if (!token.startsWith(QLatin1Char('-')))
        return false;

    if (typePrefix)
        *typePrefix = token.mid(1);
    return true;
}

QString parameterTypeFromToken(const QString& token)
{
    if (!token.startsWith(QLatin1Char('-')) || token.size() <= 1)
        return QString();

    const QString requested = token.mid(1);
    for (const QString& typeName : parameterTypeNames()) {
        if (typeName.compare(requested, Qt::CaseInsensitive) == 0)
            return typeName;
    }
    return QString();
}

QList<CodeTemplateItem> parameterTypeCompletionItems(
    const CodeTemplateItem& baseItem,
    const QString& typePrefix)
{
    QList<CodeTemplateItem> result;
    for (const QString& typeName : parameterTypeNames()) {
        if (!fuzzyMatchInOrder(typeName, typePrefix))
            continue;

        CodeTemplateItem item = baseItem;
        item.label = typeName;
        item.description = QStringLiteral("parameter type");
        item.insertText =
            QStringLiteral("%1 -%2 ").arg(baseItem.commandToken, typeName);
        item.defaultValue = item.insertText;
        result.append(item);
    }
    return result;
}

CodeTemplateItem expandParameterDeclarationTemplate(CodeTemplateItem item,
                                                    const QString& seedText)
{
    const QStringList tokens = splitTemplateArgs(seedText);
    QStringList packedDimensions;
    QStringList unpackedDimensions;
    QString typeName;
    QString name;
    bool nameSeen = false;

    for (const QString& token : tokens) {
        if (!nameSeen) {
            const QString parameterType = parameterTypeFromToken(token);
            if (!parameterType.isEmpty()) {
                typeName = parameterType;
                continue;
            }
        }

        bool dimensionMatched = false;
        const QString dimension = dimensionFromToken(token, &dimensionMatched);
        if (dimensionMatched) {
            if (!dimension.isEmpty()) {
                if (nameSeen)
                    unpackedDimensions.append(dimension);
                else
                    packedDimensions.append(dimension);
            }
            continue;
        }

        if (!nameSeen && isIdentifierLike(token)) {
            name = token;
            nameSeen = true;
        }
    }

    if (name.isEmpty())
        name = QStringLiteral("PARAM");

    QString text = parameterKeywordForTemplate(item.commandToken);
    if (!typeName.isEmpty()) {
        text += QLatin1Char(' ');
        text += typeName;
    }
    if (!packedDimensions.isEmpty()) {
        text += QLatin1Char(' ');
        text += packedDimensions.join(QString());
    }
    text += QLatin1Char(' ');
    const int nameStart = text.size();
    text += name;
    if (!unpackedDimensions.isEmpty()) {
        text += QLatin1Char(' ');
        text += unpackedDimensions.join(QString());
        text += QStringLiteral(" = '{};");
        const int braceIndex = text.indexOf(QStringLiteral("{}"));
        item.selectionStart = nameStart;
        item.selectionLength = name.size();
        item.templateSlots.append(makeSlot(
            QStringLiteral("name"),
            nameStart,
            name.size()));
        item.templateSlots.append(makeSlot(
            QStringLiteral("value"),
            braceIndex >= 0 ? braceIndex + 1 : text.size() - 2,
            0));
    } else {
        text += QStringLiteral(" = ");
        const int valueStart = text.size();
        item.selectionStart = nameStart;
        item.selectionLength = name.size();
        text += QLatin1Char(';');
        item.templateSlots.append(makeSlot(
            QStringLiteral("name"),
            nameStart,
            name.size()));
        item.templateSlots.append(makeSlot(
            QStringLiteral("value"),
            valueStart,
            0));
    }

    item.insertText = text;
    item.defaultValue = text;
    return item;
}

}

CodeTemplateService* CodeTemplateService::getInstance()
{
    if (!s_instance)
        s_instance = std::make_unique<CodeTemplateService>();
    return s_instance.get();
}

QList<CodeTemplateItem> CodeTemplateService::catalog() const
{
    return {
        makeItem(QStringLiteral(";;l"), QStringLiteral("logic"), QStringLiteral("logic declaration"), QStringLiteral("logic signal;")),
        makeItem(QStringLiteral(";;w"), QStringLiteral("wire"), QStringLiteral("wire declaration"), QStringLiteral("wire signal;")),
        makeItem(QStringLiteral(";;r"), QStringLiteral("reg"), QStringLiteral("reg declaration"), QStringLiteral("reg signal;")),
        makeItem(QStringLiteral(";;p"), QStringLiteral("parameter"), QStringLiteral("parameter declaration"), QStringLiteral("parameter NAME = ;")),
        makeItem(QStringLiteral(";;lp"), QStringLiteral("localparam"), QStringLiteral("localparam declaration"), QStringLiteral("localparam NAME = ;")),
        makeItem(QStringLiteral(";;c"), QStringLiteral("assign"), QStringLiteral("continuous assignment"), QStringLiteral("assign lhs = rhs;")),
        makeItem(QStringLiteral(";;a"), QStringLiteral("always"), QStringLiteral("always process"), QStringLiteral("always_comb begin\nend")),
        makeItem(QStringLiteral(";;m"), QStringLiteral("module"), QStringLiteral("module template"), QStringLiteral("`timescale 1ns / 1ps\nmodule name(\n);\nendmodule")),
        makeItem(QStringLiteral(";;i"), QStringLiteral("interface"), QStringLiteral("interface skeleton"), QStringLiteral("interface name();\nendinterface")),
        makeItem(QStringLiteral(";;t"), QStringLiteral("task"), QStringLiteral("task skeleton"), QStringLiteral("task automatic name();\nendtask")),
        makeItem(QStringLiteral(";;f"), QStringLiteral("function"), QStringLiteral("function skeleton"), QStringLiteral("function automatic void name();\nendfunction")),
        makeItem(QStringLiteral(";;ne"), QStringLiteral("enum type"), QStringLiteral("typedef enum"), QStringLiteral("typedef enum logic [0:0] {\n} name_e;")),
        makeItem(QStringLiteral(";;nsp"), QStringLiteral("packed struct"), QStringLiteral("packed struct type"), QStringLiteral("typedef struct packed {\n} name_t;")),
        makeItem(QStringLiteral(";;ns"), QStringLiteral("unpacked struct"), QStringLiteral("unpacked struct type"), QStringLiteral("typedef struct {\n} name_t;")),
        makeItem(QStringLiteral(";;d"), QStringLiteral("define"), QStringLiteral("define / ifdef block"), QStringLiteral("`define NAME\n`ifdef NAME\n`endif")),
    };
}

QList<CodeTemplateItem> CodeTemplateService::matchingTemplates(
    const QString& commandToken,
    const QString& seedText) const
{
    QList<CodeTemplateItem> result;
    for (CodeTemplateItem item : catalog()) {
        if (!commandToken.isEmpty()
            && item.commandToken.compare(commandToken, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (isSignalDeclarationTemplate(item.commandToken)) {
            result.append(expandSignalDeclarationTemplate(item, seedText));
            continue;
        }
        if (isParameterDeclarationTemplate(item.commandToken)) {
            QString typePrefix;
            if (parameterTypeCompletionPrefix(seedText, &typePrefix)) {
                result.append(parameterTypeCompletionItems(item, typePrefix));
                continue;
            }

            result.append(expandParameterDeclarationTemplate(item, seedText));
            continue;
        }

        item.insertText = expandTemplate(item.commandToken, seedText);
        item.defaultValue = item.insertText;
        result.append(item);
    }
    return result;
}

CodeTemplateItem CodeTemplateService::templateForCommand(
    const QString& commandToken,
    const QString& seedText) const
{
    const QList<CodeTemplateItem> matches =
        matchingTemplates(commandToken, seedText);
    return matches.isEmpty() ? CodeTemplateItem() : matches.first();
}

QString CodeTemplateService::seededName(
    const QString& seedText,
    const QString& fallback) const
{
    const QString trimmed = seedText.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QString CodeTemplateService::expandTemplate(
    const QString& commandToken,
    const QString& seedText) const
{
    const QString name = seededName(seedText, QStringLiteral("name"));
    const QString upper = seededName(seedText, QStringLiteral("NAME")).toUpper();

    if (isSignalDeclarationTemplate(commandToken))
        return expandSignalDeclarationTemplate(
                   makeItem(commandToken,
                            signalKeywordForTemplate(commandToken),
                            QStringLiteral("signal declaration"),
                            QStringLiteral("signal sig;")),
                   seedText)
            .insertText;
    if (isParameterDeclarationTemplate(commandToken))
        return expandParameterDeclarationTemplate(
                   makeItem(commandToken,
                            parameterKeywordForTemplate(commandToken),
                            QStringLiteral("parameter declaration"),
                            QStringLiteral("parameter NAME = ;")),
                   seedText)
            .insertText;
    if (commandToken == QStringLiteral(";;c"))
        return seedText.trimmed().isEmpty()
            ? QStringLiteral("assign lhs = rhs;")
            : QStringLiteral("assign %1 = rhs;").arg(name);
    if (commandToken == QStringLiteral(";;a"))
        return QStringLiteral("always_ff @(posedge clk or negedge rst_n) begin\n"
                              "    if (!rst_n) begin\n"
                              "    end else begin\n"
                              "    end\n"
                              "end");
    if (commandToken == QStringLiteral(";;m"))
        return QStringLiteral("`timescale 1ns / 1ps\n"
                              "module %1(\n"
                              ");\n"
                              "endmodule").arg(name);
    if (commandToken == QStringLiteral(";;i"))
        return QStringLiteral("interface %1(\n"
                              ");\n"
                              "endinterface").arg(name);
    if (commandToken == QStringLiteral(";;t"))
        return QStringLiteral("task automatic %1();\n"
                              "endtask").arg(name);
    if (commandToken == QStringLiteral(";;f"))
        return QStringLiteral("function automatic void %1();\n"
                              "endfunction").arg(name);
    if (commandToken == QStringLiteral(";;ne"))
        return QStringLiteral("typedef enum logic [0:0] {\n"
                              "} %1_e;").arg(name);
    if (commandToken == QStringLiteral(";;nsp"))
        return QStringLiteral("typedef struct packed {\n"
                              "} %1_t;").arg(name);
    if (commandToken == QStringLiteral(";;ns"))
        return QStringLiteral("typedef struct {\n"
                              "} %1_t;").arg(name);
    if (commandToken == QStringLiteral(";;d"))
        return QStringLiteral("`define %1\n"
                              "`ifdef %1\n"
                              "`endif").arg(upper);

    return QString();
}
