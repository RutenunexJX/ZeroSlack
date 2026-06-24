#include "formatterservice.h"

#include <QStringList>
#include <algorithm>

std::unique_ptr<FormatterService> FormatterService::instance = nullptr;

namespace {
struct DeclarationAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString family;
    QString indent;
    QString prefix;
    QString name;
    QString suffix;
    QString assignmentRhs;
    bool hasAssignment = false;
    QString trailingComment;
};

struct PortAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString indent;
    QString direction;
    QString prefix;
    QString name;
    QString suffix;
    bool trailingComma = false;
    QString trailingComment;
};

struct InstanceMapAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString indent;
    QString name;
    QString expression;
    bool trailingComma = false;
    QString trailingComment;
};

struct CaseItemAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString indent;
    QString label;
    QString suffix;
    QString trailingComment;
};

struct AssignmentAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString indent;
    QString left;
    QString op;
    QString right;
    QString trailingComment;
};

struct CodeCommentParts {
    QString code;
    QString trailingComment;
    bool hasBlockCommentToken = false;
};

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

QString repeatSpaces(int count)
{
    return QString(std::max(0, count), QLatin1Char(' '));
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

bool isDeclarationKeyword(const QString& token)
{
    return token == QStringLiteral("logic")
        || token == QStringLiteral("wire")
        || token == QStringLiteral("reg")
        || token == QStringLiteral("bit")
        || token == QStringLiteral("byte")
        || token == QStringLiteral("shortint")
        || token == QStringLiteral("int")
        || token == QStringLiteral("longint")
        || token == QStringLiteral("integer")
        || token == QStringLiteral("time")
        || token == QStringLiteral("parameter")
        || token == QStringLiteral("localparam");
}

bool isPortDirectionKeyword(const QString& token)
{
    return token == QStringLiteral("input")
        || token == QStringLiteral("output")
        || token == QStringLiteral("inout");
}

bool isForbiddenDeclarationName(const QString& token)
{
    return isOpeningToken(token)
        || isClosingToken(token)
        || token == QStringLiteral("assign")
        || token == QStringLiteral("always")
        || token == QStringLiteral("always_comb")
        || token == QStringLiteral("always_ff")
        || token == QStringLiteral("always_latch")
        || token == QStringLiteral("if")
        || token == QStringLiteral("else")
        || token == QStringLiteral("for")
        || token == QStringLiteral("while")
        || token == QStringLiteral("return")
        || token == QStringLiteral("typedef");
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

bool suppressesDelimiterContinuation(const QStringList& tokens)
{
    if (tokens.isEmpty())
        return false;
    const QString first = tokens.first();
    return first == QStringLiteral("module")
        || first == QStringLiteral("interface")
        || first == QStringLiteral("class")
        || first == QStringLiteral("function")
        || first == QStringLiteral("task");
}

bool startsWithClosingDelimiter(const QString& codeOnly)
{
    const QString trimmed = codeOnly.trimmed();
    if (trimmed.isEmpty())
        return false;
    const QChar first = trimmed.at(0);
    return first == QLatin1Char(')')
        || first == QLatin1Char(']')
        || first == QLatin1Char('}');
}

int delimiterContinuationBalance(const QString& codeOnly)
{
    int balance = 0;
    for (const QChar ch : codeOnly) {
        if (ch == QLatin1Char('(')
            || ch == QLatin1Char('[')
            || ch == QLatin1Char('{')) {
            ++balance;
        } else if (ch == QLatin1Char(')')
                   || ch == QLatin1Char(']')
                   || ch == QLatin1Char('}')) {
            --balance;
        }
    }
    return balance;
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

QString joinLinesPreservingFinalNewline(const QStringList& lines,
                                        bool hadFinalNewline)
{
    QString result = lines.join(QLatin1Char('\n'));
    if (hadFinalNewline)
        result.append(QLatin1Char('\n'));
    return result;
}

int leadingWhitespaceWidth(const QString& line)
{
    int width = 0;
    while (width < line.size() && line.at(width).isSpace())
        ++width;
    return width;
}

int commonLeadingWhitespaceWidth(const QStringList& lines)
{
    int minWidth = -1;
    for (const QString& line : lines) {
        if (!lineHasCode(line))
            continue;
        const int width = leadingWhitespaceWidth(line);
        minWidth = minWidth < 0 ? width : std::min(minWidth, width);
    }
    return std::max(0, minWidth);
}

QString baseIndentText(const QStringList& lines, int width)
{
    if (width <= 0)
        return QString();
    for (const QString& line : lines) {
        if (!lineHasCode(line))
            continue;
        return line.left(std::min(width, static_cast<int>(line.size())));
    }
    return QString();
}

CodeCommentParts splitTrailingLineComment(const QString& line)
{
    CodeCommentParts parts;
    parts.code = line;

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
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            parts.code = line.left(i);
            parts.trailingComment = line.mid(i).trimmed();
            return parts;
        }
        if ((ch == QLatin1Char('/') && next == QLatin1Char('*'))
            || (ch == QLatin1Char('*') && next == QLatin1Char('/'))) {
            parts.hasBlockCommentToken = true;
            return parts;
        }
    }

    return parts;
}

QString appendTrailingComment(const QString& codeLine,
                              const QString& trailingComment,
                              int commentColumn)
{
    if (trailingComment.isEmpty())
        return codeLine;

    const int spaces =
        commentColumn > 0
            ? std::max(2, commentColumn - static_cast<int>(codeLine.size()))
            : 2;
    return codeLine + repeatSpaces(spaces) + trailingComment;
}

int findTopLevelChar(const QString& text, QChar target)
{
    int bracketDepth = 0;
    int parenDepth = 0;
    int braceDepth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('['))
            ++bracketDepth;
        else if (ch == QLatin1Char(']'))
            bracketDepth = std::max(0, bracketDepth - 1);
        else if (ch == QLatin1Char('('))
            ++parenDepth;
        else if (ch == QLatin1Char(')'))
            parenDepth = std::max(0, parenDepth - 1);
        else if (ch == QLatin1Char('{'))
            ++braceDepth;
        else if (ch == QLatin1Char('}'))
            braceDepth = std::max(0, braceDepth - 1);
        else if (ch == target
                 && bracketDepth == 0
                 && parenDepth == 0
                 && braceDepth == 0) {
            return i;
        }
    }
    return -1;
}

bool hasTopLevelChar(const QString& text, QChar target)
{
    return findTopLevelChar(text, target) >= 0;
}

int findTopLevelCaseItemColon(const QString& text)
{
    int bracketDepth = 0;
    int parenDepth = 0;
    int braceDepth = 0;
    bool inString = false;
    bool escaped = false;
    int ternaryDepth = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const QChar prev = (i > 0) ? text.at(i - 1) : QChar();
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
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('['))
            ++bracketDepth;
        else if (ch == QLatin1Char(']'))
            bracketDepth = std::max(0, bracketDepth - 1);
        else if (ch == QLatin1Char('('))
            ++parenDepth;
        else if (ch == QLatin1Char(')'))
            parenDepth = std::max(0, parenDepth - 1);
        else if (ch == QLatin1Char('{'))
            ++braceDepth;
        else if (ch == QLatin1Char('}'))
            braceDepth = std::max(0, braceDepth - 1);
        else if (ch == QLatin1Char('?')
                 && bracketDepth == 0
                 && parenDepth == 0
                 && braceDepth == 0
                 && !isIdentifierPart(prev)
                 && !isIdentifierPart(next)) {
            ++ternaryDepth;
        } else if (ch == QLatin1Char(':')
                   && bracketDepth == 0
                   && parenDepth == 0
                   && braceDepth == 0) {
            if (prev == QLatin1Char(':') || next == QLatin1Char(':'))
                continue;
            if (ternaryDepth > 0) {
                --ternaryDepth;
                continue;
            }
            return i;
        }
    }
    return -1;
}

int findTopLevelAssignmentOperator(const QString& text, QString* op)
{
    int bracketDepth = 0;
    int parenDepth = 0;
    int braceDepth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const QChar prev = (i > 0) ? text.at(i - 1) : QChar();
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
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('['))
            ++bracketDepth;
        else if (ch == QLatin1Char(']'))
            bracketDepth = std::max(0, bracketDepth - 1);
        else if (ch == QLatin1Char('('))
            ++parenDepth;
        else if (ch == QLatin1Char(')'))
            parenDepth = std::max(0, parenDepth - 1);
        else if (ch == QLatin1Char('{'))
            ++braceDepth;
        else if (ch == QLatin1Char('}'))
            braceDepth = std::max(0, braceDepth - 1);
        else if (bracketDepth == 0
                 && parenDepth == 0
                 && braceDepth == 0) {
            if (ch == QLatin1Char('<') && next == QLatin1Char('=')) {
                if (op)
                    *op = QStringLiteral("<=");
                return i;
            }
            if (ch == QLatin1Char('=')
                && prev != QLatin1Char('=')
                && prev != QLatin1Char('!')
                && prev != QLatin1Char('<')
                && prev != QLatin1Char('>')
                && next != QLatin1Char('=')
                && next != QLatin1Char('>')) {
                if (op)
                    *op = QStringLiteral("=");
                return i;
            }
        }
    }
    return -1;
}

int matchingOpeningBracket(const QString& text, int closingBracket)
{
    int depth = 0;
    for (int i = closingBracket; i >= 0; --i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char(']'))
            ++depth;
        else if (ch == QLatin1Char('[')) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

int matchingClosingParen(const QString& text, int openingParen)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = openingParen; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (ch == QLatin1Char('(')) {
            ++depth;
        } else if (ch == QLatin1Char(')')) {
            --depth;
            if (depth == 0)
                return i;
            if (depth < 0)
                return -1;
        }
    }
    return -1;
}

DeclarationAlignmentLine parseDeclarationAlignmentLine(const QString& line)
{
    DeclarationAlignmentLine parsed;
    if (!lineHasCode(line) || startsWithPreprocessor(line))
        return parsed;
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken || !lineHasCode(parts.code))
        return parsed;

    const int indentWidth = leadingWhitespaceWidth(parts.code);
    const QString indent = parts.code.left(indentWidth);
    const QString code = parts.code.mid(indentWidth).trimmed();
    if (!code.endsWith(QLatin1Char(';')))
        return parsed;

    const QString codeWithoutSemicolon =
        code.left(code.size() - 1).trimmed();
    if (codeWithoutSemicolon.isEmpty())
        return parsed;

    const int equalIndex = findTopLevelChar(codeWithoutSemicolon, QLatin1Char('='));
    const QString left =
        (equalIndex >= 0
             ? codeWithoutSemicolon.left(equalIndex)
             : codeWithoutSemicolon)
            .trimmed();
    if (left.isEmpty() || hasTopLevelChar(left, QLatin1Char(',')))
        return parsed;

    int leftEnd = left.size() - 1;
    while (leftEnd >= 0 && left.at(leftEnd).isSpace())
        --leftEnd;

    int suffixStart = leftEnd + 1;
    while (leftEnd >= 0 && left.at(leftEnd) == QLatin1Char(']')) {
        const int bracketStart = matchingOpeningBracket(left, leftEnd);
        if (bracketStart < 0)
            return parsed;
        suffixStart = bracketStart;
        leftEnd = bracketStart - 1;
        while (leftEnd >= 0 && left.at(leftEnd).isSpace())
            --leftEnd;
    }

    if (leftEnd < 0 || !isIdentifierPart(left.at(leftEnd)))
        return parsed;

    int nameStart = leftEnd;
    while (nameStart >= 0 && isIdentifierPart(left.at(nameStart)))
        --nameStart;
    ++nameStart;

    const QString name = left.mid(nameStart, leftEnd - nameStart + 1);
    if (name.isEmpty() || isForbiddenDeclarationName(name))
        return parsed;

    const QString prefix = left.left(nameStart).trimmed();
    const QString suffix = left.mid(suffixStart).trimmed();
    if (prefix.isEmpty())
        return parsed;

    const QStringList prefixTokens = codeTokens(prefix);
    if (prefixTokens.isEmpty())
        return parsed;

    const QString firstToken = prefixTokens.first();
    QString family;
    if (firstToken == QStringLiteral("typedef"))
        return parsed;
    if (firstToken == QStringLiteral("parameter")
        || firstToken == QStringLiteral("localparam")) {
        family = QStringLiteral("parameter");
    } else if (isDeclarationKeyword(firstToken)
               || isPortDirectionKeyword(firstToken)) {
        family = QStringLiteral("signal");
    } else {
        return parsed;
    }

    if (isPortDirectionKeyword(firstToken)
        && prefixTokens.size() > 1
        && !isDeclarationKeyword(prefixTokens.at(1))) {
        return parsed;
    }

    parsed.valid = true;
    parsed.indentWidth = indentWidth;
    parsed.family = family;
    parsed.indent = indent;
    parsed.prefix = prefix;
    parsed.name = name;
    parsed.suffix = suffix;
    parsed.hasAssignment = equalIndex >= 0;
    if (parsed.hasAssignment)
        parsed.assignmentRhs = codeWithoutSemicolon.mid(equalIndex + 1).trimmed();
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedDeclarationCodeLine(const DeclarationAlignmentLine& line,
                                        int maxPrefixWidth,
                                        int maxBeforeAssignmentWidth,
                                        bool alignAssignment)
{
    QString content = line.prefix
        + repeatSpaces(maxPrefixWidth - line.prefix.size() + 1)
        + line.name;
    if (!line.suffix.isEmpty()) {
        content += QLatin1Char(' ');
        content += line.suffix;
    }

    if (line.hasAssignment) {
        const int spacesBeforeAssignment =
            alignAssignment
                ? maxBeforeAssignmentWidth - content.size() + 1
                : 1;
        content += repeatSpaces(spacesBeforeAssignment)
            + QStringLiteral("= ")
            + line.assignmentRhs
            + QLatin1Char(';');
    } else {
        content += QLatin1Char(';');
    }

    return line.indent + content;
}

void flushDeclarationAlignmentBlock(QStringList* lines,
                                    const QList<int>& blockIndexes,
                                    const QList<DeclarationAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxPrefixWidth = 0;
    int assignmentCount = 0;
    for (const DeclarationAlignmentLine& line : block) {
        maxPrefixWidth = std::max(maxPrefixWidth,
                                  static_cast<int>(line.prefix.size()));
        if (line.hasAssignment)
            ++assignmentCount;
    }

    int maxBeforeAssignmentWidth = 0;
    for (const DeclarationAlignmentLine& line : block) {
        QString content = line.prefix
            + repeatSpaces(maxPrefixWidth - line.prefix.size() + 1)
            + line.name;
        if (!line.suffix.isEmpty()) {
            content += QLatin1Char(' ');
            content += line.suffix;
        }
        maxBeforeAssignmentWidth =
            std::max(maxBeforeAssignmentWidth,
                     static_cast<int>(content.size()));
    }

    const bool alignAssignment = assignmentCount >= 2;
    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (int i = 0; i < block.size(); ++i) {
        const QString codeLine =
            buildAlignedDeclarationCodeLine(block.at(i),
                                            maxPrefixWidth,
                                            maxBeforeAssignmentWidth,
                                            alignAssignment);
        codeLines.append(codeLine);
        maxCodeLineWidth =
            std::max(maxCodeLineWidth, static_cast<int>(codeLine.size()));
        if (!block.at(i).trailingComment.isEmpty())
            hasTrailingComment = true;
    }

    const int commentColumn = hasTrailingComment ? maxCodeLineWidth + 2 : 0;
    for (int i = 0; i < block.size(); ++i) {
        (*lines)[blockIndexes.at(i)] =
            appendTrailingComment(codeLines.at(i),
                                  block.at(i).trailingComment,
                                  commentColumn);
    }
}

void alignDeclarationBlocks(QStringList* lines)
{
    if (!lines)
        return;

    QList<int> blockIndexes;
    QList<DeclarationAlignmentLine> block;

    auto flush = [&]() {
        flushDeclarationAlignmentBlock(lines, blockIndexes, block);
        blockIndexes.clear();
        block.clear();
    };

    for (int i = 0; i < lines->size(); ++i) {
        const DeclarationAlignmentLine parsed =
            parseDeclarationAlignmentLine(lines->at(i));
        if (!parsed.valid) {
            flush();
            continue;
        }

        if (!block.isEmpty()
            && (block.last().indentWidth != parsed.indentWidth
                || block.last().family != parsed.family)) {
            flush();
        }

        blockIndexes.append(i);
        block.append(parsed);
    }

    flush();
}

bool startsWithWholeWord(const QString& text, const QString& word)
{
    if (!text.startsWith(word))
        return false;
    if (text.size() == word.size())
        return true;
    return !isIdentifierPart(text.at(word.size()));
}

PortAlignmentLine parsePortAlignmentLine(const QString& line)
{
    PortAlignmentLine parsed;
    if (!lineHasCode(line) || startsWithPreprocessor(line))
        return parsed;
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken || !lineHasCode(parts.code))
        return parsed;

    const int indentWidth = leadingWhitespaceWidth(parts.code);
    const QString indent = parts.code.left(indentWidth);
    QString code = parts.code.mid(indentWidth).trimmed();
    if (code.isEmpty()
        || code.endsWith(QLatin1Char(';'))
        || code.contains(QLatin1Char(')'))
        || hasTopLevelChar(code, QLatin1Char('='))) {
        return parsed;
    }

    bool trailingComma = false;
    if (code.endsWith(QLatin1Char(','))) {
        trailingComma = true;
        code.chop(1);
        code = code.trimmed();
    }
    if (code.isEmpty() || hasTopLevelChar(code, QLatin1Char(',')))
        return parsed;

    const QStringList tokens = codeTokens(code);
    if (tokens.isEmpty() || !isPortDirectionKeyword(tokens.first()))
        return parsed;
    const QString direction = tokens.first();
    if (!startsWithWholeWord(code, direction))
        return parsed;

    const QString left = code.mid(direction.size()).trimmed();
    if (left.isEmpty())
        return parsed;

    int leftEnd = left.size() - 1;
    while (leftEnd >= 0 && left.at(leftEnd).isSpace())
        --leftEnd;

    int suffixStart = leftEnd + 1;
    while (leftEnd >= 0 && left.at(leftEnd) == QLatin1Char(']')) {
        const int bracketStart = matchingOpeningBracket(left, leftEnd);
        if (bracketStart < 0)
            return parsed;
        suffixStart = bracketStart;
        leftEnd = bracketStart - 1;
        while (leftEnd >= 0 && left.at(leftEnd).isSpace())
            --leftEnd;
    }

    if (leftEnd < 0 || !isIdentifierPart(left.at(leftEnd)))
        return parsed;

    int nameStart = leftEnd;
    while (nameStart >= 0 && isIdentifierPart(left.at(nameStart)))
        --nameStart;
    ++nameStart;

    const QString name = left.mid(nameStart, leftEnd - nameStart + 1);
    if (name.isEmpty() || isForbiddenDeclarationName(name))
        return parsed;

    parsed.valid = true;
    parsed.indentWidth = indentWidth;
    parsed.indent = indent;
    parsed.direction = direction;
    parsed.prefix = left.left(nameStart).trimmed();
    parsed.name = name;
    parsed.suffix = left.mid(suffixStart).trimmed();
    parsed.trailingComma = trailingComma;
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedPortCodeLine(const PortAlignmentLine& line,
                                 int maxDirectionWidth,
                                 int maxPrefixWidth)
{
    QString content = line.direction
        + repeatSpaces(maxDirectionWidth - line.direction.size() + 1);
    if (maxPrefixWidth > 0) {
        if (!line.prefix.isEmpty()) {
            content += line.prefix;
            content += repeatSpaces(maxPrefixWidth - line.prefix.size() + 1);
        } else {
            content += repeatSpaces(maxPrefixWidth + 1);
        }
    }
    content += line.name;
    if (!line.suffix.isEmpty()) {
        content += QLatin1Char(' ');
        content += line.suffix;
    }
    if (line.trailingComma)
        content += QLatin1Char(',');
    return line.indent + content;
}

void flushPortAlignmentBlock(QStringList* lines,
                             const QList<int>& blockIndexes,
                             const QList<PortAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxDirectionWidth = 0;
    int maxPrefixWidth = 0;
    for (const PortAlignmentLine& line : block) {
        maxDirectionWidth =
            std::max(maxDirectionWidth,
                     static_cast<int>(line.direction.size()));
        maxPrefixWidth =
            std::max(maxPrefixWidth,
                     static_cast<int>(line.prefix.size()));
    }

    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (int i = 0; i < block.size(); ++i) {
        const QString codeLine =
            buildAlignedPortCodeLine(block.at(i),
                                     maxDirectionWidth,
                                     maxPrefixWidth);
        codeLines.append(codeLine);
        maxCodeLineWidth =
            std::max(maxCodeLineWidth, static_cast<int>(codeLine.size()));
        if (!block.at(i).trailingComment.isEmpty())
            hasTrailingComment = true;
    }

    const int commentColumn = hasTrailingComment ? maxCodeLineWidth + 2 : 0;
    for (int i = 0; i < block.size(); ++i) {
        (*lines)[blockIndexes.at(i)] =
            appendTrailingComment(codeLines.at(i),
                                  block.at(i).trailingComment,
                                  commentColumn);
    }
}

void alignPortListBlocks(QStringList* lines)
{
    if (!lines)
        return;

    QList<int> blockIndexes;
    QList<PortAlignmentLine> block;

    auto flush = [&]() {
        flushPortAlignmentBlock(lines, blockIndexes, block);
        blockIndexes.clear();
        block.clear();
    };

    for (int i = 0; i < lines->size(); ++i) {
        const PortAlignmentLine parsed = parsePortAlignmentLine(lines->at(i));
        if (!parsed.valid) {
            flush();
            continue;
        }
        if (!block.isEmpty()
            && block.last().indentWidth != parsed.indentWidth) {
            flush();
        }
        blockIndexes.append(i);
        block.append(parsed);
    }

    flush();
}

InstanceMapAlignmentLine parseInstanceMapAlignmentLine(const QString& line)
{
    InstanceMapAlignmentLine parsed;
    if (!lineHasCode(line) || startsWithPreprocessor(line))
        return parsed;
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken || !lineHasCode(parts.code))
        return parsed;

    const int indentWidth = leadingWhitespaceWidth(parts.code);
    const QString indent = parts.code.left(indentWidth);
    QString code = parts.code.mid(indentWidth).trimmed();
    if (code.size() < 4 || !code.startsWith(QLatin1Char('.')))
        return parsed;

    bool trailingComma = false;
    if (code.endsWith(QLatin1Char(','))) {
        trailingComma = true;
        code.chop(1);
        code = code.trimmed();
    }

    if (code.size() < 3 || code.endsWith(QLatin1Char(';')))
        return parsed;

    int nameStart = 1;
    if (!isIdentifierStart(code.at(nameStart)))
        return parsed;
    int nameEnd = nameStart + 1;
    while (nameEnd < code.size() && isIdentifierPart(code.at(nameEnd)))
        ++nameEnd;

    int openParen = nameEnd;
    while (openParen < code.size() && code.at(openParen).isSpace())
        ++openParen;
    if (openParen >= code.size() || code.at(openParen) != QLatin1Char('('))
        return parsed;

    const int closeParen = matchingClosingParen(code, openParen);
    if (closeParen < 0 || closeParen != code.size() - 1)
        return parsed;

    parsed.valid = true;
    parsed.indentWidth = indentWidth;
    parsed.indent = indent;
    parsed.name = code.mid(nameStart, nameEnd - nameStart);
    parsed.expression = code.mid(openParen + 1, closeParen - openParen - 1);
    parsed.trailingComma = trailingComma;
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedInstanceMapCodeLine(const InstanceMapAlignmentLine& line,
                                        int maxNameWidth)
{
    QString content = QLatin1Char('.')
        + line.name
        + repeatSpaces(maxNameWidth - line.name.size() + 1)
        + QLatin1Char('(')
        + line.expression
        + QLatin1Char(')');
    if (line.trailingComma)
        content += QLatin1Char(',');
    return line.indent + content;
}

void flushInstanceMapAlignmentBlock(QStringList* lines,
                                    const QList<int>& blockIndexes,
                                    const QList<InstanceMapAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxNameWidth = 0;
    for (const InstanceMapAlignmentLine& line : block) {
        maxNameWidth =
            std::max(maxNameWidth,
                     static_cast<int>(line.name.size()));
    }

    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (int i = 0; i < block.size(); ++i) {
        const QString codeLine =
            buildAlignedInstanceMapCodeLine(block.at(i), maxNameWidth);
        codeLines.append(codeLine);
        maxCodeLineWidth =
            std::max(maxCodeLineWidth, static_cast<int>(codeLine.size()));
        if (!block.at(i).trailingComment.isEmpty())
            hasTrailingComment = true;
    }

    const int commentColumn = hasTrailingComment ? maxCodeLineWidth + 2 : 0;
    for (int i = 0; i < block.size(); ++i) {
        (*lines)[blockIndexes.at(i)] =
            appendTrailingComment(codeLines.at(i),
                                  block.at(i).trailingComment,
                                  commentColumn);
    }
}

void alignInstanceMapBlocks(QStringList* lines)
{
    if (!lines)
        return;

    QList<int> blockIndexes;
    QList<InstanceMapAlignmentLine> block;

    auto flush = [&]() {
        flushInstanceMapAlignmentBlock(lines, blockIndexes, block);
        blockIndexes.clear();
        block.clear();
    };

    for (int i = 0; i < lines->size(); ++i) {
        const InstanceMapAlignmentLine parsed =
            parseInstanceMapAlignmentLine(lines->at(i));
        if (!parsed.valid) {
            flush();
            continue;
        }
        if (!block.isEmpty()
            && block.last().indentWidth != parsed.indentWidth) {
            flush();
        }
        blockIndexes.append(i);
        block.append(parsed);
    }

    flush();
}

bool isCaseOpeningLine(const QStringList& tokens)
{
    for (const QString& token : tokens) {
        if (token == QStringLiteral("case")
            || token == QStringLiteral("casex")
            || token == QStringLiteral("casez")) {
            return true;
        }
        if (token != QStringLiteral("unique")
            && token != QStringLiteral("unique0")
            && token != QStringLiteral("priority")) {
            return false;
        }
    }
    return false;
}

bool isCaseClosingLine(const QStringList& tokens)
{
    return !tokens.isEmpty()
        && tokens.first() == QStringLiteral("endcase");
}

CaseItemAlignmentLine parseCaseItemAlignmentLine(const QString& line)
{
    CaseItemAlignmentLine parsed;
    if (!lineHasCode(line) || startsWithPreprocessor(line))
        return parsed;
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken || !lineHasCode(parts.code))
        return parsed;

    const int indentWidth = leadingWhitespaceWidth(parts.code);
    const QString indent = parts.code.left(indentWidth);
    const QString code = parts.code.mid(indentWidth).trimmed();
    if (code.isEmpty())
        return parsed;

    const int colonIndex = findTopLevelCaseItemColon(code);
    if (colonIndex <= 0)
        return parsed;

    const QString label = code.left(colonIndex).trimmed();
    const QString suffix = code.mid(colonIndex + 1).trimmed();
    if (label.isEmpty())
        return parsed;

    const QStringList labelTokens = codeTokens(label);
    if (labelTokens.isEmpty())
        return parsed;
    const QString firstToken = labelTokens.first();
    if (isOpeningToken(firstToken)
        || isClosingToken(firstToken)
        || firstToken == QStringLiteral("if")
        || firstToken == QStringLiteral("else")
        || firstToken == QStringLiteral("for")
        || firstToken == QStringLiteral("while")) {
        return parsed;
    }

    parsed.valid = true;
    parsed.indentWidth = indentWidth;
    parsed.indent = indent;
    parsed.label = label;
    parsed.suffix = suffix;
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedCaseItemCodeLine(const CaseItemAlignmentLine& line,
                                     int maxLabelWidth)
{
    QString content = line.label
        + repeatSpaces(maxLabelWidth - line.label.size())
        + QLatin1Char(':');
    if (!line.suffix.isEmpty()) {
        content += QLatin1Char(' ');
        content += line.suffix;
    }
    return line.indent + content;
}

void flushCaseItemAlignmentBlock(QStringList* lines,
                                 const QList<int>& blockIndexes,
                                 const QList<CaseItemAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxLabelWidth = 0;
    for (const CaseItemAlignmentLine& line : block) {
        maxLabelWidth =
            std::max(maxLabelWidth,
                     static_cast<int>(line.label.size()));
    }

    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (const CaseItemAlignmentLine& line : block) {
        const QString codeLine =
            buildAlignedCaseItemCodeLine(line, maxLabelWidth);
        codeLines.append(codeLine);
        maxCodeLineWidth =
            std::max(maxCodeLineWidth, static_cast<int>(codeLine.size()));
        if (!line.trailingComment.isEmpty())
            hasTrailingComment = true;
    }

    const int commentColumn = hasTrailingComment ? maxCodeLineWidth + 2 : 0;
    for (int i = 0; i < block.size(); ++i) {
        (*lines)[blockIndexes.at(i)] =
            appendTrailingComment(codeLines.at(i),
                                  block.at(i).trailingComment,
                                  commentColumn);
    }
}

void alignCaseItemBlocks(QStringList* lines, int indentWidth)
{
    if (!lines)
        return;

    QList<int> caseItemIndentStack;
    QList<int> blockIndexes;
    QList<CaseItemAlignmentLine> block;

    auto flush = [&]() {
        flushCaseItemAlignmentBlock(lines, blockIndexes, block);
        blockIndexes.clear();
        block.clear();
    };

    bool inBlockComment = false;
    for (int i = 0; i < lines->size(); ++i) {
        const QString line = lines->at(i);
        const QString codeOnly = codeOnlyLine(line, &inBlockComment);
        const QStringList tokens = codeTokens(codeOnly);

        if (isCaseClosingLine(tokens)) {
            flush();
            if (!caseItemIndentStack.isEmpty())
                caseItemIndentStack.removeLast();
        }

        const int expectedIndent =
            caseItemIndentStack.isEmpty() ? -1 : caseItemIndentStack.last();
        const CaseItemAlignmentLine parsed =
            expectedIndent >= 0
                && leadingWhitespaceWidth(line) == expectedIndent
                ? parseCaseItemAlignmentLine(line)
                : CaseItemAlignmentLine();
        if (parsed.valid) {
            if (!block.isEmpty()
                && block.last().indentWidth != parsed.indentWidth) {
                flush();
            }
            blockIndexes.append(i);
            block.append(parsed);
        } else {
            flush();
        }

        if (isCaseOpeningLine(tokens)) {
            flush();
            caseItemIndentStack.append(
                leadingWhitespaceWidth(line) + std::max(1, indentWidth));
        }
    }

    flush();
}

bool isForbiddenAssignmentStarter(const QString& token)
{
    return isOpeningToken(token)
        || isClosingToken(token)
        || isDeclarationKeyword(token)
        || isPortDirectionKeyword(token)
        || token == QStringLiteral("if")
        || token == QStringLiteral("else")
        || token == QStringLiteral("for")
        || token == QStringLiteral("while")
        || token == QStringLiteral("return")
        || token == QStringLiteral("case")
        || token == QStringLiteral("default")
        || token == QStringLiteral("typedef")
        || token == QStringLiteral("function")
        || token == QStringLiteral("task");
}

AssignmentAlignmentLine parseAssignmentAlignmentLine(const QString& line)
{
    AssignmentAlignmentLine parsed;
    if (!lineHasCode(line) || startsWithPreprocessor(line))
        return parsed;
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken || !lineHasCode(parts.code))
        return parsed;

    const int indentWidth = leadingWhitespaceWidth(parts.code);
    const QString indent = parts.code.left(indentWidth);
    const QString code = parts.code.mid(indentWidth).trimmed();
    if (!code.endsWith(QLatin1Char(';')))
        return parsed;

    const QString codeWithoutSemicolon =
        code.left(code.size() - 1).trimmed();
    if (codeWithoutSemicolon.isEmpty())
        return parsed;

    QString op;
    const int opIndex = findTopLevelAssignmentOperator(
        codeWithoutSemicolon,
        &op);
    if (opIndex <= 0 || op.isEmpty())
        return parsed;

    const QString left =
        codeWithoutSemicolon.left(opIndex).trimmed();
    const QString right =
        codeWithoutSemicolon.mid(opIndex + op.size()).trimmed();
    if (left.isEmpty()
        || right.isEmpty()
        || hasTopLevelChar(left, QLatin1Char(','))
        || findTopLevelCaseItemColon(left) >= 0) {
        return parsed;
    }

    const QStringList leftTokens = codeTokens(left);
    if (leftTokens.isEmpty())
        return parsed;
    const QString firstToken = leftTokens.first();
    if (firstToken == QStringLiteral("assign")) {
        if (leftTokens.size() < 2
            || isForbiddenAssignmentStarter(leftTokens.at(1))) {
            return parsed;
        }
    } else if (isForbiddenAssignmentStarter(firstToken)) {
        return parsed;
    }

    parsed.valid = true;
    parsed.indentWidth = indentWidth;
    parsed.indent = indent;
    parsed.left = left;
    parsed.op = op;
    parsed.right = right;
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedAssignmentCodeLine(const AssignmentAlignmentLine& line,
                                       int maxLeftWidth)
{
    return line.indent
        + line.left
        + repeatSpaces(maxLeftWidth - line.left.size() + 1)
        + line.op
        + QLatin1Char(' ')
        + line.right
        + QLatin1Char(';');
}

void flushAssignmentAlignmentBlock(QStringList* lines,
                                   const QList<int>& blockIndexes,
                                   const QList<AssignmentAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxLeftWidth = 0;
    for (const AssignmentAlignmentLine& line : block) {
        maxLeftWidth =
            std::max(maxLeftWidth,
                     static_cast<int>(line.left.size()));
    }

    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (const AssignmentAlignmentLine& line : block) {
        const QString codeLine =
            buildAlignedAssignmentCodeLine(line, maxLeftWidth);
        codeLines.append(codeLine);
        maxCodeLineWidth =
            std::max(maxCodeLineWidth, static_cast<int>(codeLine.size()));
        if (!line.trailingComment.isEmpty())
            hasTrailingComment = true;
    }

    const int commentColumn = hasTrailingComment ? maxCodeLineWidth + 2 : 0;
    for (int i = 0; i < block.size(); ++i) {
        (*lines)[blockIndexes.at(i)] =
            appendTrailingComment(codeLines.at(i),
                                  block.at(i).trailingComment,
                                  commentColumn);
    }
}

void alignAssignmentBlocks(QStringList* lines)
{
    if (!lines)
        return;

    QList<int> blockIndexes;
    QList<AssignmentAlignmentLine> block;

    auto flush = [&]() {
        flushAssignmentAlignmentBlock(lines, blockIndexes, block);
        blockIndexes.clear();
        block.clear();
    };

    for (int i = 0; i < lines->size(); ++i) {
        const AssignmentAlignmentLine parsed =
            parseAssignmentAlignmentLine(lines->at(i));
        if (!parsed.valid) {
            flush();
            continue;
        }
        if (!block.isEmpty()
            && block.last().indentWidth != parsed.indentWidth) {
            flush();
        }
        blockIndexes.append(i);
        block.append(parsed);
    }

    flush();
}
}

FormatterService* FormatterService::getInstance()
{
    if (!instance)
        instance = std::make_unique<FormatterService>();
    return instance.get();
}

FormatterOptions FormatterService::optionsForProfile(FormatterProfile profile)
{
    FormatterOptions options;
    if (profile == FormatterProfile::IndentOnly) {
        options.alignDeclarationBlocks = false;
        options.alignPortLists = false;
        options.alignInstanceMaps = false;
        options.alignCaseItems = false;
        options.alignAssignments = false;
    }
    return options;
}

QString FormatterService::profileDisplayName(FormatterProfile profile)
{
    switch (profile) {
    case FormatterProfile::IndentOnly:
        return QStringLiteral("Indent Only");
    case FormatterProfile::Structured:
        return QStringLiteral("Structured");
    }
    return QStringLiteral("Structured");
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
    int continuationLevel = 0;
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

        const int displayContinuation =
            options.indentContinuationLines
                ? std::max(0,
                           continuationLevel
                               - (startsWithClosingDelimiter(codeOnly) ? 1 : 0))
                : 0;
        const int displayIndent =
            std::max(0, indentLevel - leadingClosingTokens(tokens))
            + displayContinuation;
        const QString body = stripLeadingWhitespace(line);
        formatted.append(indentation(displayIndent, options.indentWidth) + body);

        indentLevel += countOpeningTokens(tokens);
        indentLevel -= countClosingTokens(tokens);
        indentLevel = std::max(0, indentLevel);
        if (options.indentContinuationLines
            && !suppressesDelimiterContinuation(tokens)) {
            continuationLevel =
                std::max(0,
                         continuationLevel
                             + delimiterContinuationBalance(codeOnly));
        }
    }

    if (options.alignDeclarationBlocks)
        alignDeclarationBlocks(&formatted);
    if (options.alignPortLists)
        alignPortListBlocks(&formatted);
    if (options.alignInstanceMaps)
        alignInstanceMapBlocks(&formatted);
    if (options.alignCaseItems)
        alignCaseItemBlocks(&formatted, options.indentWidth);
    if (options.alignAssignments)
        alignAssignmentBlocks(&formatted);

    report.formattedText =
        joinLinesPreservingFinalNewline(formatted, hadFinalNewline);
    report.changed = report.formattedText != text;
    report.formattedLines = formatted.size();
    return report;
}

FormatterReport FormatterService::formatDocument(
    const QString& text,
    FormatterProfile profile) const
{
    return formatDocument(text, optionsForProfile(profile));
}

FormatterReport FormatterService::formatSelection(
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

    const int baseWidth = commonLeadingWhitespaceWidth(lines);
    const QString baseIndent = baseIndentText(lines, baseWidth);
    QStringList dedented;
    dedented.reserve(lines.size());
    for (const QString& line : lines) {
        if (!lineHasCode(line)) {
            dedented.append(QString());
            continue;
        }
        dedented.append(line.mid(std::min(baseWidth,
                                          static_cast<int>(line.size()))));
    }

    const QString dedentedText =
        joinLinesPreservingFinalNewline(dedented, hadFinalNewline);
    const FormatterReport inner = formatDocument(dedentedText, options);
    QStringList formatted = splitLines(inner.formattedText);
    if (hadFinalNewline && !formatted.isEmpty() && formatted.last().isEmpty())
        formatted.removeLast();

    for (QString& line : formatted) {
        if (lineHasCode(line))
            line.prepend(baseIndent);
    }

    report.formattedText =
        joinLinesPreservingFinalNewline(formatted, hadFinalNewline);
    report.changed = report.formattedText != text;
    report.formattedLines = formatted.size();
    return report;
}

FormatterReport FormatterService::formatSelection(
    const QString& text,
    FormatterProfile profile) const
{
    return formatSelection(text, optionsForProfile(profile));
}
