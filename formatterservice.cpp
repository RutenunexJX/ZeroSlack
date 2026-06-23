#include "formatterservice.h"

#include <QStringList>
#include <algorithm>

std::unique_ptr<FormatterService> FormatterService::instance = nullptr;

namespace {
bool isIdentifierStart(QChar ch)
{
    return ch == QLatin1Char('_')
        || ch == QLatin1Char('$')
        || ch.isLetter();
}

bool isIdentifierPart(QChar ch)
{
    return isIdentifierStart(ch) || ch.isDigit();
}

QString stripLeadingWhitespace(const QString& line)
{
    int start = 0;
    while (start < line.size() && line.at(start).isSpace())
        ++start;
    return line.mid(start);
}

QString indentation(int level, int width)
{
    return QString(std::max(0, level) * std::max(1, width), QLatin1Char(' '));
}

bool startsWithPreprocessor(const QString& line)
{
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QLatin1Char('`'));
}

QString codeOnlyLine(const QString& line, bool* inBlockComment)
{
    QString result;
    result.reserve(line.size());
    bool inString = false;
    bool escaped = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        const QChar next = (i + 1 < line.size()) ? line.at(i + 1) : QChar();

        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            result.append(QLatin1Char(' '));
            continue;
        }

        if (inBlockComment && *inBlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                *inBlockComment = false;
                result.append(QStringLiteral("  "));
                ++i;
            } else {
                result.append(QLatin1Char(' '));
            }
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/'))
            break;
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            if (inBlockComment)
                *inBlockComment = true;
            result.append(QStringLiteral("  "));
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
            result.append(QLatin1Char(' '));
            continue;
        }

        result.append(ch);
    }
    return result;
}

QStringList codeTokens(const QString& code)
{
    QStringList tokens;
    int pos = 0;
    while (pos < code.size()) {
        if (!isIdentifierStart(code.at(pos))) {
            ++pos;
            continue;
        }

        const int start = pos;
        ++pos;
        while (pos < code.size() && isIdentifierPart(code.at(pos)))
            ++pos;
        tokens.append(code.mid(start, pos - start));
    }
    return tokens;
}

bool isOpeningToken(const QString& token)
{
    return token == QStringLiteral("module")
        || token == QStringLiteral("interface")
        || token == QStringLiteral("package")
        || token == QStringLiteral("class")
        || token == QStringLiteral("function")
        || token == QStringLiteral("task")
        || token == QStringLiteral("generate")
        || token == QStringLiteral("begin")
        || token == QStringLiteral("case")
        || token == QStringLiteral("casex")
        || token == QStringLiteral("casez")
        || token == QStringLiteral("fork");
}

bool isClosingToken(const QString& token)
{
    return token == QStringLiteral("end")
        || token == QStringLiteral("endmodule")
        || token == QStringLiteral("endinterface")
        || token == QStringLiteral("endpackage")
        || token == QStringLiteral("endclass")
        || token == QStringLiteral("endfunction")
        || token == QStringLiteral("endtask")
        || token == QStringLiteral("endgenerate")
        || token == QStringLiteral("endcase")
        || token == QStringLiteral("join")
        || token == QStringLiteral("join_any")
        || token == QStringLiteral("join_none");
}

int countOpeningTokens(const QStringList& tokens)
{
    int count = 0;
    for (const QString& token : tokens) {
        if (isOpeningToken(token))
            ++count;
    }
    return count;
}

int countClosingTokens(const QStringList& tokens)
{
    int count = 0;
    for (const QString& token : tokens) {
        if (isClosingToken(token))
            ++count;
    }
    return count;
}

int leadingClosingTokens(const QStringList& tokens)
{
    int count = 0;
    for (const QString& token : tokens) {
        if (!isClosingToken(token))
            break;
        ++count;
    }
    return count;
}

bool lineHasCode(const QString& line)
{
    for (const QChar ch : line) {
        if (!ch.isSpace())
            return true;
    }
    return false;
}

QStringList splitLines(const QString& text)
{
    QStringList lines;
    QString current;
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\n')) {
            lines.append(current);
            current.clear();
        } else if (ch != QLatin1Char('\r')) {
            current.append(ch);
        }
    }
    lines.append(current);
    return lines;
}
}

FormatterService* FormatterService::getInstance()
{
    if (!instance)
        instance = std::make_unique<FormatterService>();
    return instance.get();
}

FormatterReport FormatterService::formatDocument(
    const QString& text,
    const FormatterOptions& options) const
{
    FormatterReport report;
    if (text.isEmpty())
        return report;

    const bool hadFinalNewline = text.endsWith(QLatin1Char('\n'));
    QStringList lines = splitLines(text);
    if (hadFinalNewline && !lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QStringList formatted;
    formatted.reserve(lines.size());
    int indentLevel = 0;
    bool inBlockComment = false;

    for (const QString& line : lines) {
        if (!lineHasCode(line)) {
            formatted.append(QString());
            continue;
        }

        if (startsWithPreprocessor(line) && options.preservePreprocessorIndent) {
            formatted.append(line);
            continue;
        }

        const QString codeOnly = codeOnlyLine(line, &inBlockComment);
        const QStringList tokens = codeTokens(codeOnly);
        if (codeOnly.trimmed().isEmpty()) {
            formatted.append(line);
            continue;
        }

        const int displayIndent =
            std::max(0, indentLevel - leadingClosingTokens(tokens));
        const QString body = stripLeadingWhitespace(line);
        formatted.append(indentation(displayIndent, options.indentWidth) + body);

        indentLevel += countOpeningTokens(tokens);
        indentLevel -= countClosingTokens(tokens);
        indentLevel = std::max(0, indentLevel);
    }

    report.formattedText = formatted.join(QLatin1Char('\n'));
    if (hadFinalNewline)
        report.formattedText.append(QLatin1Char('\n'));
    report.changed = report.formattedText != text;
    report.formattedLines = formatted.size();
    return report;
}
