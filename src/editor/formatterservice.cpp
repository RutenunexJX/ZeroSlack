#include "formatterservice.h"
#include "structuredwhitespaceformatter.h"
#include "tsdocument.h"

#include <QStringList>
#include <algorithm>
#include <utility>

std::unique_ptr<FormatterService> FormatterService::instance = nullptr;

namespace {
struct DeclarationAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString family;
    QString indent;
    QString prefix;
    QString packedDimensions;
    QString name;
    QString suffix;
    QString assignmentRhs;
    QString terminator = QStringLiteral(";");
    bool hasAssignment = false;
    QString trailingComment;
};

struct EnumItemAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString indent;
    QString name;
    QString value;
    bool hasValue = false;
    bool trailingComma = false;
    QString trailingComment;
};

struct AssignmentAlignmentLine {
    bool valid = false;
    int indentWidth = 0;
    QString indent;
    QString left;
    QString leftBase;
    QString leftSuffix;
    QString op;
    QString right;
    bool hasTernary = false;
    QString ternaryCondition;
    QString ternaryTrueExpression;
    QString ternaryFalseExpression;
    QString ternaryTrueBase;
    QString ternaryTrueSuffix;
    QString ternaryFalseBase;
    QString ternaryFalseSuffix;
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
        || token == QStringLiteral("program")
        || token == QStringLiteral("primitive")
        || token == QStringLiteral("checker")
        || token == QStringLiteral("class")
        || token == QStringLiteral("function")
        || token == QStringLiteral("task")
        || token == QStringLiteral("generate")
        || token == QStringLiteral("clocking")
        || token == QStringLiteral("covergroup")
        || token == QStringLiteral("property")
        || token == QStringLiteral("sequence")
        || token == QStringLiteral("specify")
        || token == QStringLiteral("table")
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
        || token == QStringLiteral("endprogram")
        || token == QStringLiteral("endprimitive")
        || token == QStringLiteral("endchecker")
        || token == QStringLiteral("endclass")
        || token == QStringLiteral("endfunction")
        || token == QStringLiteral("endtask")
        || token == QStringLiteral("endgenerate")
        || token == QStringLiteral("endclocking")
        || token == QStringLiteral("endgroup")
        || token == QStringLiteral("endproperty")
        || token == QStringLiteral("endsequence")
        || token == QStringLiteral("endspecify")
        || token == QStringLiteral("endtable")
        || token == QStringLiteral("endcase")
        || token == QStringLiteral("join")
        || token == QStringLiteral("join_any")
        || token == QStringLiteral("join_none");
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

bool startsWithLeadingContinuationOperator(const QString& code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
        return false;

    const QChar first = trimmed.at(0);
    if (first == QLatin1Char('?')
        || first == QLatin1Char(':')
        || first == QLatin1Char('+')
        || first == QLatin1Char('-')
        || first == QLatin1Char('*')
        || first == QLatin1Char('/')
        || first == QLatin1Char('%')
        || first == QLatin1Char('&')
        || first == QLatin1Char('|')
        || first == QLatin1Char('^')) {
        return true;
    }

    if (trimmed.size() < 2)
        return false;
    const QString firstTwo = trimmed.left(2);
    return firstTwo == QStringLiteral("&&")
        || firstTwo == QStringLiteral("||")
        || firstTwo == QStringLiteral("==")
        || firstTwo == QStringLiteral("!=")
        || firstTwo == QStringLiteral("<=")
        || firstTwo == QStringLiteral(">=");
}

bool endsStatement(const QString& code)
{
    return code.trimmed().endsWith(QLatin1Char(';'));
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

QList<int> unmatchedOpeningParenColumns(const QString& codeOnly,
                                        int* externalClosingParens)
{
    QList<int> openings;
    int closes = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < codeOnly.size(); ++i) {
        const QChar ch = codeOnly.at(i);
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
            openings.append(i);
        } else if (ch == QLatin1Char(')')) {
            if (!openings.isEmpty())
                openings.removeLast();
            else
                ++closes;
        }
    }

    if (externalClosingParens)
        *externalClosingParens = closes;
    return openings;
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

QStringList lineEndingSequences(const QString& text)
{
    QStringList sequences;
    for (int position = 0; position < text.size(); ++position) {
        if (text.at(position) != QLatin1Char('\n'))
            continue;
        sequences.append(
            position > 0
                    && text.at(position - 1) == QLatin1Char('\r')
                ? QStringLiteral("\r\n")
                : QStringLiteral("\n"));
    }
    return sequences;
}

QString restoreOriginalLineEndings(const QString& formatted,
                                   const QString& original)
{
    const QStringList originalEndings =
        lineEndingSequences(original);
    if (originalEndings.isEmpty())
        return formatted;

    QStringList lines;
    QString current;
    for (int position = 0; position < formatted.size(); ++position) {
        const QChar ch = formatted.at(position);
        if (ch == QLatin1Char('\r')
            && position + 1 < formatted.size()
            && formatted.at(position + 1) == QLatin1Char('\n')) {
            continue;
        }
        if (ch == QLatin1Char('\n')) {
            lines.append(current);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    lines.append(current);

    const int boundaryCount =
        std::max(0, static_cast<int>(lines.size()) - 1);
    if (boundaryCount == 0)
        return formatted;

    QStringList targetEndings;
    if (originalEndings.size() == boundaryCount) {
        targetEndings = originalEndings;
    } else {
        const bool uniformCrlf =
            std::all_of(
                originalEndings.cbegin(),
                originalEndings.cend(),
                [](const QString& ending) {
                    return ending == QStringLiteral("\r\n");
                });
        targetEndings.fill(
            uniformCrlf ? QStringLiteral("\r\n")
                        : QStringLiteral("\n"),
            boundaryCount);
    }

    QString result;
    result.reserve(
        formatted.size()
        + (targetEndings.first().size() - 1) * boundaryCount);
    for (int line = 0; line < lines.size(); ++line) {
        result.append(lines.at(line));
        if (line < targetEndings.size())
            result.append(targetEndings.at(line));
    }
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

QString normalizeBracketEdgeWhitespace(const QString& text)
{
    QString normalized;
    normalized.reserve(text.size());
    bool inString = false;
    bool escaped = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inString) {
            normalized.append(ch);
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
            normalized.append(ch);
            continue;
        }
        if (ch == QLatin1Char('[')) {
            int trailing = normalized.size();
            while (trailing > 0
                   && (normalized.at(trailing - 1) == QLatin1Char(' ')
                       || normalized.at(trailing - 1) == QLatin1Char('\t'))) {
                --trailing;
            }
            if (trailing > 0
                && normalized.at(trailing - 1) == QLatin1Char(']')) {
                normalized.truncate(trailing);
            }
            normalized.append(ch);
            while (i + 1 < text.size()
                   && (text.at(i + 1) == QLatin1Char(' ')
                       || text.at(i + 1) == QLatin1Char('\t'))) {
                ++i;
            }
            continue;
        }
        if (ch == QLatin1Char(']')) {
            while (!normalized.isEmpty()
                   && (normalized.back() == QLatin1Char(' ')
                       || normalized.back() == QLatin1Char('\t'))) {
                normalized.chop(1);
            }
        }
        normalized.append(ch);
    }
    return normalized;
}

bool splitTopLevelTernary(const QString& text,
                          QString* condition,
                          QString* trueExpression,
                          QString* falseExpression,
                          int* questionPosition = nullptr,
                          int* colonPosition = nullptr)
{
    int bracketDepth = 0;
    int parenDepth = 0;
    int braceDepth = 0;
    int nestedTernaryDepth = 0;
    int questionIndex = -1;
    bool inString = false;
    bool escaped = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const QChar prev = i > 0 ? text.at(i - 1) : QChar();
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
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
                 && braceDepth == 0
                 && ch == QLatin1Char('?')) {
            if (questionIndex < 0)
                questionIndex = i;
            else
                ++nestedTernaryDepth;
        } else if (questionIndex >= 0
                   && bracketDepth == 0
                   && parenDepth == 0
                   && braceDepth == 0
                   && ch == QLatin1Char(':')
                   && prev != QLatin1Char(':')
                   && next != QLatin1Char(':')) {
            if (nestedTernaryDepth > 0) {
                --nestedTernaryDepth;
                continue;
            }
            const QString left = text.left(questionIndex).trimmed();
            const QString middle =
                text.mid(questionIndex + 1, i - questionIndex - 1).trimmed();
            const QString right = text.mid(i + 1).trimmed();
            if (left.isEmpty() || middle.isEmpty() || right.isEmpty())
                return false;
            if (condition)
                *condition = left;
            if (trueExpression)
                *trueExpression = middle;
            if (falseExpression)
                *falseExpression = right;
            if (questionPosition)
                *questionPosition = questionIndex;
            if (colonPosition)
                *colonPosition = i;
            return true;
        }
    }
    return false;
}

int continuationOperatorAnchorColumn(const QString& line)
{
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken)
        return -1;

    QString op;
    const int opIndex = findTopLevelAssignmentOperator(parts.code, &op);
    if (opIndex < 0 || op.isEmpty() || endsStatement(parts.code))
        return -1;

    int anchor = opIndex + op.size();
    while (anchor < parts.code.size() && parts.code.at(anchor).isSpace())
        ++anchor;
    if (anchor >= parts.code.size())
        anchor = opIndex + op.size() + 1;
    return anchor;
}

bool endsWithTopLevelAssignmentOperator(const QString& codeOnly)
{
    const QString code = codeOnly.trimmed();
    if (code.isEmpty()
        || code.endsWith(QLatin1Char(';'))
        || startsWithPreprocessor(code)) {
        return false;
    }

    QString op;
    const int opIndex = findTopLevelAssignmentOperator(code, &op);
    if (opIndex <= 0 || op.isEmpty())
        return false;

    const QString left = code.left(opIndex).trimmed();
    const QString right = code.mid(opIndex + op.size()).trimmed();
    if (left.isEmpty() || !right.isEmpty())
        return false;

    const QStringList leftTokens = codeTokens(left);
    if (leftTokens.isEmpty())
        return false;
    const QString firstToken = leftTokens.first();
    if (firstToken == QStringLiteral("if")
        || firstToken == QStringLiteral("else")
        || firstToken == QStringLiteral("for")
        || firstToken == QStringLiteral("while")
        || firstToken == QStringLiteral("return")
        || firstToken == QStringLiteral("case")
        || firstToken == QStringLiteral("default")
        || firstToken == QStringLiteral("typedef")) {
        return false;
    }
    return true;
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
    QString terminator;
    QString codeBody = code;
    if (codeBody.endsWith(QLatin1Char(';'))) {
        terminator = QStringLiteral(";");
        codeBody.chop(1);
    } else if (codeBody.endsWith(QLatin1Char(','))) {
        terminator = QStringLiteral(",");
        codeBody.chop(1);
    }
    codeBody = codeBody.trimmed();
    if (codeBody.isEmpty())
        return parsed;

    const int equalIndex = findTopLevelChar(codeBody, QLatin1Char('='));
    const QString left =
        (equalIndex >= 0
             ? codeBody.left(equalIndex)
             : codeBody)
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

    const QString rawPrefix = left.left(nameStart).trimmed();
    const QString suffix = left.mid(suffixStart).trimmed();
    if (rawPrefix.isEmpty())
        return parsed;

    int packedStart = rawPrefix.size();
    int packedEnd = rawPrefix.size() - 1;
    while (packedEnd >= 0
           && rawPrefix.at(packedEnd) == QLatin1Char(']')) {
        const int bracketStart = matchingOpeningBracket(rawPrefix, packedEnd);
        if (bracketStart < 0)
            return parsed;
        packedStart = bracketStart;
        packedEnd = bracketStart - 1;
        while (packedEnd >= 0 && rawPrefix.at(packedEnd).isSpace())
            --packedEnd;
    }
    QString prefix = rawPrefix.left(packedStart).simplified();
    QString packedDimensions = rawPrefix.mid(packedStart).simplified();
    packedDimensions.replace(QStringLiteral("] ["), QStringLiteral("]["));
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
        if (terminator != QStringLiteral(";"))
            return parsed;
        family = QStringLiteral("signal");
    } else {
        const QString declaration = codeBody + terminator;
        if (terminator != QStringLiteral(";")
            || !TSDocument::isSingleSignalDeclaration(
                declaration, name, false)) {
            return parsed;
        }
        family = QStringLiteral("signal");
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
    parsed.packedDimensions = packedDimensions;
    parsed.name = name;
    parsed.suffix = suffix;
    parsed.terminator = terminator;
    parsed.hasAssignment = equalIndex >= 0;
    if (parsed.hasAssignment)
        parsed.assignmentRhs = codeBody.mid(equalIndex + 1).trimmed();
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedDeclarationCodeLine(const DeclarationAlignmentLine& line,
                                        int maxPrefixWidth,
                                        int maxPackedWidth,
                                        bool alignPackedDimensions,
                                        int maxNameWidth,
                                        int maxBeforeAssignmentWidth,
                                        bool alignAssignment,
                                        int semicolonColumn)
{
    QString content = line.prefix;
    if (alignPackedDimensions) {
        content += repeatSpaces(maxPrefixWidth - line.prefix.size() + 1)
            + line.packedDimensions
            + repeatSpaces(maxPackedWidth
                           - line.packedDimensions.size() + 1);
    } else {
        content += repeatSpaces(maxPrefixWidth - line.prefix.size() + 1);
    }
    content += line.name;
    if (!line.suffix.isEmpty()) {
        content += repeatSpaces(maxNameWidth - line.name.size() + 1);
        content += line.suffix;
    }

    if (line.hasAssignment) {
        const int spacesBeforeAssignment =
            alignAssignment
                ? maxBeforeAssignmentWidth - content.size() + 1
                : 1;
        content += repeatSpaces(spacesBeforeAssignment)
            + QStringLiteral("= ")
            + line.assignmentRhs;
    }

    const int contentEndColumn =
        line.indent.size() + content.size();
    if (line.terminator == QStringLiteral(";")
        && semicolonColumn > contentEndColumn) {
        content += repeatSpaces(
            semicolonColumn - contentEndColumn);
    }
    content += line.terminator;

    return line.indent + content;
}

void flushDeclarationAlignmentBlock(QStringList* lines,
                                    const QList<int>& blockIndexes,
                                    const QList<DeclarationAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxPrefixWidth = 0;
    int maxPackedWidth = 0;
    bool alignPackedDimensions = false;
    int maxNameWidth = 0;
    int assignmentCount = 0;
    for (const DeclarationAlignmentLine& line : block) {
        maxPrefixWidth = std::max(maxPrefixWidth,
                                  static_cast<int>(line.prefix.size()));
        maxPackedWidth = std::max(
            maxPackedWidth,
            static_cast<int>(line.packedDimensions.size()));
        alignPackedDimensions = alignPackedDimensions
            || !line.packedDimensions.isEmpty();
        maxNameWidth = std::max(maxNameWidth,
                                static_cast<int>(line.name.size()));
        if (line.hasAssignment)
            ++assignmentCount;
    }

    int maxBeforeAssignmentWidth = 0;
    for (const DeclarationAlignmentLine& line : block) {
        QString content = line.prefix;
        if (alignPackedDimensions) {
            content += repeatSpaces(maxPrefixWidth - line.prefix.size() + 1)
                + line.packedDimensions
                + repeatSpaces(maxPackedWidth
                               - line.packedDimensions.size() + 1);
        } else {
            content += repeatSpaces(maxPrefixWidth - line.prefix.size() + 1);
        }
        content += line.name;
        if (!line.suffix.isEmpty()) {
            content += repeatSpaces(maxNameWidth - line.name.size() + 1);
            content += line.suffix;
        }
        maxBeforeAssignmentWidth =
            std::max(maxBeforeAssignmentWidth,
                     static_cast<int>(content.size()));
    }

    const bool alignAssignment = assignmentCount >= 2;
    int semicolonColumn = 0;
    for (const DeclarationAlignmentLine& line : block) {
        if (line.terminator != QStringLiteral(";"))
            continue;
        const QString preliminary =
            buildAlignedDeclarationCodeLine(
                line,
                maxPrefixWidth,
                maxPackedWidth,
                alignPackedDimensions,
                maxNameWidth,
                maxBeforeAssignmentWidth,
                alignAssignment,
                0);
        semicolonColumn = std::max(
            semicolonColumn,
            static_cast<int>(preliminary.size()) - 1);
    }
    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (int i = 0; i < block.size(); ++i) {
        const QString codeLine =
            buildAlignedDeclarationCodeLine(block.at(i),
                                            maxPrefixWidth,
                                            maxPackedWidth,
                                            alignPackedDimensions,
                                            maxNameWidth,
                                            maxBeforeAssignmentWidth,
                                            alignAssignment,
                                            semicolonColumn);
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
                || block.last().family != parsed.family
                || block.last().terminator != parsed.terminator)) {
            flush();
        }

        blockIndexes.append(i);
        block.append(parsed);
    }

    flush();
}

bool isEnumOpeningLine(const QStringList& tokens, const QString& codeOnly)
{
    bool hasEnum = false;
    for (const QString& token : tokens) {
        if (token == QStringLiteral("enum")) {
            hasEnum = true;
            break;
        }
    }
    return hasEnum
        && codeOnly.contains(QLatin1Char('{'))
        && delimiterContinuationBalance(codeOnly) > 0;
}

EnumItemAlignmentLine parseEnumItemAlignmentLine(const QString& line)
{
    EnumItemAlignmentLine parsed;
    if (!lineHasCode(line) || startsWithPreprocessor(line))
        return parsed;
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken || !lineHasCode(parts.code))
        return parsed;

    const int indentWidth = leadingWhitespaceWidth(parts.code);
    const QString indent = parts.code.left(indentWidth);
    QString code = parts.code.mid(indentWidth).trimmed();
    if (code.isEmpty() || code.endsWith(QLatin1Char(';'))) {
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

    const int equalIndex = findTopLevelChar(code, QLatin1Char('='));
    const QString left =
        (equalIndex >= 0 ? code.left(equalIndex) : code).trimmed();
    const QString value =
        equalIndex >= 0 ? code.mid(equalIndex + 1).trimmed() : QString();
    if (left.isEmpty() || (equalIndex >= 0 && value.isEmpty()))
        return parsed;

    if (!isIdentifierStart(left.at(0)))
        return parsed;
    for (int i = 1; i < left.size(); ++i) {
        if (!isIdentifierPart(left.at(i)))
            return parsed;
    }
    if (isForbiddenDeclarationName(left)
        || isDeclarationKeyword(left)
        || isPortDirectionKeyword(left)
        || left == QStringLiteral("default")) {
        return parsed;
    }

    parsed.valid = true;
    parsed.indentWidth = indentWidth;
    parsed.indent = indent;
    parsed.name = left;
    parsed.value = value;
    parsed.hasValue = equalIndex >= 0;
    parsed.trailingComma = trailingComma;
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedEnumItemCodeLine(const EnumItemAlignmentLine& line,
                                     int maxNameWidth,
                                     bool alignValue)
{
    QString content = line.name;
    if (line.hasValue) {
        content += repeatSpaces(
            alignValue ? maxNameWidth - line.name.size() + 1 : 1);
        content += QStringLiteral("= ");
        content += line.value;
    }
    if (line.trailingComma)
        content += QLatin1Char(',');
    return line.indent + content;
}

void flushEnumItemAlignmentBlock(QStringList* lines,
                                 const QList<int>& blockIndexes,
                                 const QList<EnumItemAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxNameWidth = 0;
    int valueCount = 0;
    for (const EnumItemAlignmentLine& line : block) {
        maxNameWidth =
            std::max(maxNameWidth,
                     static_cast<int>(line.name.size()));
        if (line.hasValue)
            ++valueCount;
    }

    const bool alignValue = valueCount >= 2;
    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (const EnumItemAlignmentLine& line : block) {
        const QString codeLine =
            buildAlignedEnumItemCodeLine(line, maxNameWidth, alignValue);
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

void alignEnumItemBlocks(QStringList* lines, int indentWidth)
{
    if (!lines)
        return;

    int enumDepth = 0;
    int expectedIndent = -1;
    QList<int> blockIndexes;
    QList<EnumItemAlignmentLine> block;

    auto flush = [&]() {
        flushEnumItemAlignmentBlock(lines, blockIndexes, block);
        blockIndexes.clear();
        block.clear();
    };

    bool inBlockComment = false;
    for (int i = 0; i < lines->size(); ++i) {
        const QString line = lines->at(i);
        const QString codeOnly = codeOnlyLine(line, &inBlockComment);
        const QStringList tokens = codeTokens(codeOnly);

        if (enumDepth == 0) {
            if (isEnumOpeningLine(tokens, codeOnly)) {
                flush();
                enumDepth = std::max(0, delimiterContinuationBalance(codeOnly));
                expectedIndent =
                    leadingWhitespaceWidth(line) + std::max(1, indentWidth);
                if (enumDepth == 0)
                    expectedIndent = -1;
            }
            continue;
        }

        const bool closingLine = startsWithClosingDelimiter(codeOnly);
        if (closingLine)
            flush();

        const EnumItemAlignmentLine parsed =
            !closingLine
                && expectedIndent >= 0
                && leadingWhitespaceWidth(line) == expectedIndent
                ? parseEnumItemAlignmentLine(line)
                : EnumItemAlignmentLine();
        if (parsed.valid) {
            blockIndexes.append(i);
            block.append(parsed);
        } else {
            flush();
        }

        enumDepth =
            std::max(0, enumDepth + delimiterContinuationBalance(codeOnly));
        if (enumDepth == 0) {
            flush();
            expectedIndent = -1;
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

QPair<int, int> trimmedBounds(const QString& text, int start, int end)
{
    int boundedStart = qBound(0, start, text.size());
    int boundedEnd = qBound(boundedStart, end, text.size());
    while (boundedStart < boundedEnd && text.at(boundedStart).isSpace())
        ++boundedStart;
    while (boundedEnd > boundedStart
           && text.at(boundedEnd - 1).isSpace()) {
        --boundedEnd;
    }
    return {boundedStart, boundedEnd};
}

void splitSyntaxSuffix(const TSDocument* syntax,
                       int absoluteStart,
                       int absoluteEnd,
                       const QString& fallback,
                       QString* base,
                       QString* suffix)
{
    if (base)
        *base = fallback;
    if (suffix)
        suffix->clear();
    if (!syntax || !base || !suffix)
        return;
    const TSExpressionSuffixTarget target =
        syntax->expressionSuffixTarget(absoluteStart, absoluteEnd);
    if (!target.hasSuffix())
        return;
    *base = normalizeBracketEdgeWhitespace(
        syntax->text().mid(
            target.baseStartChar,
            target.baseEndChar - target.baseStartChar));
    *suffix = normalizeBracketEdgeWhitespace(
        syntax->text().mid(
            target.suffixStartChar,
            target.suffixEndChar - target.suffixStartChar));
}

AssignmentAlignmentLine parseAssignmentAlignmentLine(
    const QString& line,
    int absoluteLineStart,
    const TSDocument* syntax)
{
    AssignmentAlignmentLine parsed;
    if (!lineHasCode(line) || startsWithPreprocessor(line))
        return parsed;
    const CodeCommentParts parts = splitTrailingLineComment(line);
    if (parts.hasBlockCommentToken || !lineHasCode(parts.code))
        return parsed;

    const int indentWidth = leadingWhitespaceWidth(parts.code);
    const QString indent = parts.code.left(indentWidth);
    const QPair<int, int> codeBounds =
        trimmedBounds(parts.code, indentWidth, parts.code.size());
    const QString code = parts.code.mid(
        codeBounds.first,
        codeBounds.second - codeBounds.first);
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

    const QPair<int, int> leftBounds = trimmedBounds(
        codeWithoutSemicolon, 0, opIndex);
    const QPair<int, int> rightBounds = trimmedBounds(
        codeWithoutSemicolon,
        opIndex + op.size(),
        codeWithoutSemicolon.size());
    const QString left = normalizeBracketEdgeWhitespace(
        codeWithoutSemicolon.mid(
            leftBounds.first,
            leftBounds.second - leftBounds.first));
    const QString rightRaw = codeWithoutSemicolon.mid(
        rightBounds.first,
        rightBounds.second - rightBounds.first);
    const QString right = normalizeBracketEdgeWhitespace(rightRaw);
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
    const int expressionBase = absoluteLineStart + codeBounds.first;
    splitSyntaxSuffix(
        syntax,
        expressionBase + leftBounds.first,
        expressionBase + leftBounds.second,
        left,
        &parsed.leftBase,
        &parsed.leftSuffix);
    parsed.op = op;
    parsed.right = right;
    int questionPosition = -1;
    int colonPosition = -1;
    parsed.hasTernary = splitTopLevelTernary(
        rightRaw,
        &parsed.ternaryCondition,
        &parsed.ternaryTrueExpression,
        &parsed.ternaryFalseExpression,
        &questionPosition,
        &colonPosition);
    parsed.ternaryCondition = normalizeBracketEdgeWhitespace(
        parsed.ternaryCondition);
    parsed.ternaryTrueExpression = normalizeBracketEdgeWhitespace(
        parsed.ternaryTrueExpression);
    parsed.ternaryFalseExpression = normalizeBracketEdgeWhitespace(
        parsed.ternaryFalseExpression);
    parsed.ternaryTrueBase = parsed.ternaryTrueExpression;
    parsed.ternaryFalseBase = parsed.ternaryFalseExpression;
    if (parsed.hasTernary) {
        const QPair<int, int> trueBounds = trimmedBounds(
            rightRaw, questionPosition + 1, colonPosition);
        const QPair<int, int> falseBounds = trimmedBounds(
            rightRaw, colonPosition + 1, rightRaw.size());
        const int rightAbsoluteStart =
            expressionBase + rightBounds.first;
        splitSyntaxSuffix(
            syntax,
            rightAbsoluteStart + trueBounds.first,
            rightAbsoluteStart + trueBounds.second,
            parsed.ternaryTrueExpression,
            &parsed.ternaryTrueBase,
            &parsed.ternaryTrueSuffix);
        splitSyntaxSuffix(
            syntax,
            rightAbsoluteStart + falseBounds.first,
            rightAbsoluteStart + falseBounds.second,
            parsed.ternaryFalseExpression,
            &parsed.ternaryFalseBase,
            &parsed.ternaryFalseSuffix);
    }
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedAssignmentCodeLine(const AssignmentAlignmentLine& line,
                                       int maxLeftBaseWidth,
                                       int maxLeftSuffixWidth,
                                       int questionColumn,
                                       int maxTrueBaseWidth,
                                       int maxTrueSuffixWidth,
                                       int colonColumn,
                                       int maxFalseBaseWidth,
                                       int maxFalseSuffixWidth,
                                       int semicolonColumn)
{
    const auto alignedSuffixExpression = [](
            const QString& base,
            const QString& suffix,
            int maxBaseWidth,
            int maxSuffixWidth) {
        return base
            + repeatSpaces(maxBaseWidth - base.size())
            + suffix
            + repeatSpaces(maxSuffixWidth - suffix.size());
    };
    QString codeLine = line.indent
        + alignedSuffixExpression(
            line.leftBase,
            line.leftSuffix,
            maxLeftBaseWidth,
            maxLeftSuffixWidth)
        + QLatin1Char(' ')
        + line.op
        + QLatin1Char(' ');
    if (line.hasTernary) {
        codeLine += line.ternaryCondition;
        if (questionColumn > codeLine.size())
        codeLine += repeatSpaces(questionColumn - codeLine.size());
        codeLine += QStringLiteral(" ? ");
        codeLine += alignedSuffixExpression(
            line.ternaryTrueBase,
            line.ternaryTrueSuffix,
            maxTrueBaseWidth,
            maxTrueSuffixWidth);
        if (colonColumn > codeLine.size())
            codeLine += repeatSpaces(colonColumn - codeLine.size());
        codeLine += QStringLiteral(" : ");
        codeLine += alignedSuffixExpression(
            line.ternaryFalseBase,
            line.ternaryFalseSuffix,
            maxFalseBaseWidth,
            maxFalseSuffixWidth);
    } else {
        codeLine += line.right;
    }
    if (semicolonColumn > codeLine.size()) {
        codeLine += repeatSpaces(
            semicolonColumn - codeLine.size());
    }
    codeLine += QLatin1Char(';');
    return codeLine;
}

void flushAssignmentAlignmentBlock(QStringList* lines,
                                   const QList<int>& blockIndexes,
                                   const QList<AssignmentAlignmentLine>& block)
{
    if (!lines || block.size() < 2)
        return;

    int maxLeftBaseWidth = 0;
    int maxLeftSuffixWidth = 0;
    for (const AssignmentAlignmentLine& line : block) {
        maxLeftBaseWidth = std::max(
            maxLeftBaseWidth,
            static_cast<int>(line.leftBase.size()));
        maxLeftSuffixWidth = std::max(
            maxLeftSuffixWidth,
            static_cast<int>(line.leftSuffix.size()));
    }

    int questionColumn = 0;
    for (const AssignmentAlignmentLine& line : block) {
        if (!line.hasTernary)
            continue;
        const int prefixWidth = line.indent.size()
            + maxLeftBaseWidth + maxLeftSuffixWidth + 1
            + line.op.size() + 1;
        questionColumn = std::max(
            questionColumn,
            prefixWidth + static_cast<int>(line.ternaryCondition.size()));
    }

    int maxTrueBaseWidth = 0;
    int maxTrueSuffixWidth = 0;
    int maxFalseBaseWidth = 0;
    int maxFalseSuffixWidth = 0;
    for (const AssignmentAlignmentLine& line : block) {
        if (!line.hasTernary)
            continue;
        maxTrueBaseWidth = std::max(
            maxTrueBaseWidth,
            static_cast<int>(line.ternaryTrueBase.size()));
        maxTrueSuffixWidth = std::max(
            maxTrueSuffixWidth,
            static_cast<int>(line.ternaryTrueSuffix.size()));
        maxFalseBaseWidth = std::max(
            maxFalseBaseWidth,
            static_cast<int>(line.ternaryFalseBase.size()));
        maxFalseSuffixWidth = std::max(
            maxFalseSuffixWidth,
            static_cast<int>(line.ternaryFalseSuffix.size()));
    }
    const int colonColumn = questionColumn + 3
        + maxTrueBaseWidth + maxTrueSuffixWidth;

    int semicolonColumn = 0;
    for (const AssignmentAlignmentLine& line : block) {
        const QString preliminary =
            buildAlignedAssignmentCodeLine(
                line,
                maxLeftBaseWidth,
                maxLeftSuffixWidth,
                questionColumn,
                maxTrueBaseWidth,
                maxTrueSuffixWidth,
                colonColumn,
                maxFalseBaseWidth,
                maxFalseSuffixWidth,
                0);
        semicolonColumn = std::max(
            semicolonColumn,
            static_cast<int>(preliminary.size()) - 1);
    }

    QStringList codeLines;
    codeLines.reserve(block.size());
    int maxCodeLineWidth = 0;
    bool hasTrailingComment = false;
    for (const AssignmentAlignmentLine& line : block) {
        const QString codeLine =
            buildAlignedAssignmentCodeLine(
                line,
                maxLeftBaseWidth,
                maxLeftSuffixWidth,
                questionColumn,
                maxTrueBaseWidth,
                maxTrueSuffixWidth,
                colonColumn,
                maxFalseBaseWidth,
                maxFalseSuffixWidth,
                semicolonColumn);
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

    const QString syntaxText = lines->join(QLatin1Char('\n'));
    TSDocument syntax;
    syntax.setText(syntaxText);
    QList<AssignmentAlignmentLine> parsedLines;
    parsedLines.reserve(lines->size());
    int absoluteLineStart = 0;
    for (const QString& line : std::as_const(*lines)) {
        parsedLines.append(parseAssignmentAlignmentLine(
            line, absoluteLineStart, &syntax));
        absoluteLineStart += line.size() + 1;
    }

    QList<int> blockIndexes;
    QList<AssignmentAlignmentLine> block;

    auto flush = [&]() {
        flushAssignmentAlignmentBlock(lines, blockIndexes, block);
        blockIndexes.clear();
        block.clear();
    };

    for (int i = 0; i < lines->size(); ++i) {
        const AssignmentAlignmentLine parsed = parsedLines.at(i);
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

void alignContinuationOperatorLines(QStringList* lines)
{
    if (!lines)
        return;

    int operatorAnchor = -1;
    bool inBlockComment = false;
    for (int i = 0; i < lines->size(); ++i) {
        const QString line = lines->at(i);
        if (!lineHasCode(line)) {
            operatorAnchor = -1;
            continue;
        }
        if (startsWithPreprocessor(line)) {
            operatorAnchor = -1;
            continue;
        }

        const QString codeOnly = codeOnlyLine(line, &inBlockComment);
        if (codeOnly.trimmed().isEmpty()) {
            operatorAnchor = -1;
            continue;
        }

        const bool leadingOperator =
            startsWithLeadingContinuationOperator(codeOnly);
        if (leadingOperator) {
            if (operatorAnchor >= 0) {
                (*lines)[i] =
                    repeatSpaces(operatorAnchor)
                    + stripLeadingWhitespace(line);
            }
            if (endsStatement(codeOnly))
                operatorAnchor = -1;
            continue;
        }

        operatorAnchor = continuationOperatorAnchorColumn(line);
    }
}

void alignCallArgumentContinuationLines(QStringList* lines)
{
    if (!lines)
        return;

    QList<int> anchorStack;
    bool inBlockComment = false;
    for (int i = 0; i < lines->size(); ++i) {
        const QString line = lines->at(i);
        if (!lineHasCode(line)) {
            anchorStack.clear();
            continue;
        }
        if (startsWithPreprocessor(line)) {
            anchorStack.clear();
            continue;
        }

        const QString codeOnly = codeOnlyLine(line, &inBlockComment);
        const QString trimmed = codeOnly.trimmed();
        if (trimmed.isEmpty()) {
            anchorStack.clear();
            continue;
        }

        const QStringList tokens = codeTokens(codeOnly);
        const QString firstToken =
            tokens.isEmpty() ? QString() : tokens.first();
        const bool controlHeader =
            firstToken == QStringLiteral("if")
            || firstToken == QStringLiteral("else")
            || firstToken == QStringLiteral("for")
            || firstToken == QStringLiteral("foreach")
            || firstToken == QStringLiteral("while")
            || firstToken == QStringLiteral("repeat")
            || firstToken == QStringLiteral("always")
            || firstToken == QStringLiteral("always_comb")
            || firstToken == QStringLiteral("always_ff")
            || firstToken == QStringLiteral("always_latch");
        const bool structuralHeader =
            suppressesDelimiterContinuation(tokens) || controlHeader;
        const bool closingDelimiter = startsWithClosingDelimiter(codeOnly);
        const bool leadingOperator =
            startsWithLeadingContinuationOperator(codeOnly);
        const bool namedAssociation =
            trimmed.startsWith(QLatin1Char('.'));
        int alignmentDelta = 0;
        if (!anchorStack.isEmpty()
            && !closingDelimiter
            && !leadingOperator
            && !namedAssociation) {
            alignmentDelta =
                anchorStack.last() - leadingWhitespaceWidth(line);
            (*lines)[i] =
                repeatSpaces(anchorStack.last())
                + stripLeadingWhitespace(line);
        }

        int externalClosingParens = 0;
        const QList<int> openings =
            unmatchedOpeningParenColumns(codeOnly, &externalClosingParens);
        for (int close = 0;
             close < externalClosingParens && !anchorStack.isEmpty();
             ++close) {
            anchorStack.removeLast();
        }

        if (structuralHeader)
            continue;

        for (const int opening : openings)
            anchorStack.append(opening + alignmentDelta + 1);
    }
}

void indentNonStructuralContinuations(QStringList* lines,
                                     const FormatterOptions& options,
                                     const QList<
                                         StructuredWhitespaceFormatter::LineRange>&
                                         protectedRanges)
{
    if (!lines)
        return;

    int delimiterDepth = 0;
    int assignmentRhsDepth = 0;
    int protectedRangeIndex = 0;
    bool inBlockComment = false;
    for (int index = 0; index < lines->size(); ++index) {
        while (protectedRangeIndex < protectedRanges.size()
               && protectedRanges.at(protectedRangeIndex).lastLine
                      < index) {
            ++protectedRangeIndex;
        }
        const bool protectedLine =
            protectedRangeIndex < protectedRanges.size()
            && protectedRanges.at(protectedRangeIndex).valid()
            && index
                   >= protectedRanges.at(
                       protectedRangeIndex).firstLine
            && index
                   <= protectedRanges.at(
                       protectedRangeIndex).lastLine;
        if (protectedLine) {
            delimiterDepth = 0;
            assignmentRhsDepth = 0;
            inBlockComment = false;
            continue;
        }
        const QString line = lines->at(index);
        if (!lineHasCode(line))
            continue;
        if (startsWithPreprocessor(line)
            && options.preservePreprocessorIndent) {
            assignmentRhsDepth = 0;
            continue;
        }

        const QString codeOnly =
            codeOnlyLine(line, &inBlockComment);
        if (codeOnly.trimmed().isEmpty())
            continue;

        const int delimiterIndent =
            options.indentContinuationLines
                ? std::max(
                    0,
                    delimiterDepth
                        - (startsWithClosingDelimiter(codeOnly) ? 1 : 0))
                : 0;
        const int rhsIndent =
            options.indentAssignmentRhsContinuations
                ? assignmentRhsDepth
                : 0;
        const int structuralIndent =
            leadingWhitespaceWidth(line);
        (*lines)[index] =
            repeatSpaces(
                structuralIndent
                + (delimiterIndent + rhsIndent)
                    * std::max(1, options.indentWidth))
            + stripLeadingWhitespace(line);

        const bool startsAssignmentRhsContinuation =
            options.indentAssignmentRhsContinuations
            && endsWithTopLevelAssignmentOperator(codeOnly);
        if (options.indentAssignmentRhsContinuations
            && assignmentRhsDepth > 0
            && endsStatement(codeOnly)) {
            assignmentRhsDepth = 0;
        }
        if (startsAssignmentRhsContinuation)
            assignmentRhsDepth = 1;

        if (options.indentContinuationLines) {
            delimiterDepth =
                std::max(
                    0,
                    delimiterDepth
                        + delimiterContinuationBalance(codeOnly));
        }
    }
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

    QStringList diagnostics;
    QString stageDiagnostic;
    const QString normalizedText =
        StructuredWhitespaceFormatter::
            normalizeLexicalWhitespaceTabs(
                text, 4, &stageDiagnostic);
    if (!stageDiagnostic.isEmpty())
        diagnostics.append(stageDiagnostic);
    const QString structurallyIndentedText =
        StructuredWhitespaceFormatter::
            formatStructuralIndentation(
                normalizedText,
                options.indentWidth,
                options.indentSingleStatementBodies,
                options.indentCaseItemBodies,
                options.alignCaseItems,
                options.preservePreprocessorIndent,
                &stageDiagnostic);
    if (!stageDiagnostic.isEmpty())
        diagnostics.append(stageDiagnostic);
    const bool hadFinalNewline =
        structurallyIndentedText.endsWith(QLatin1Char('\n'));
    QStringList lines = splitLines(structurallyIndentedText);
    if (hadFinalNewline && !lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    QStringList originalLines = splitLines(normalizedText);
    if (hadFinalNewline
        && !originalLines.isEmpty()
        && originalLines.last().isEmpty()) {
        originalLines.removeLast();
    }

    QList<StructuredWhitespaceFormatter::LineRange>
        conservativeRanges =
            StructuredWhitespaceFormatter::syntaxErrorLineRanges(
                normalizedText);
    if (!conservativeRanges.isEmpty()) {
        diagnostics.append(QStringLiteral(
            "Formatting preserved syntax-error regions."));
    }
    QStringList formatted = lines;
    indentNonStructuralContinuations(
        &formatted, options, conservativeRanges);

    if (options.alignDeclarationBlocks)
        alignDeclarationBlocks(&formatted);
    if (options.alignEnumItems)
        alignEnumItemBlocks(&formatted, options.indentWidth);
    if (options.alignAssignments)
        alignAssignmentBlocks(&formatted);
    if (options.alignContinuationOperators)
        alignContinuationOperatorLines(&formatted);
    if (options.alignCallArgumentContinuations)
        alignCallArgumentContinuationLines(&formatted);

    if (options.alignPortLists || options.alignInstanceMaps) {
        const QList<StructuredWhitespaceFormatter::LineRange>
            structuredConservativeRanges =
            StructuredWhitespaceFormatter::
                conservativeLineRanges(
                    normalizedText, options.indentWidth);
        if (!structuredConservativeRanges.isEmpty()) {
            diagnostics.append(QStringLiteral(
                "Formatting preserved structurally incomplete regions."));
            conservativeRanges.append(
                structuredConservativeRanges);
        }
    }
    if (formatted.size() != originalLines.size()) {
        report.formattedText = normalizedText;
        report.changed = report.formattedText != text;
        report.formattedLines = originalLines.size();
        report.outcome = FormatterOutcome::ConservativeFallback;
        diagnostics.append(QStringLiteral(
            "Formatting was limited to lexical whitespace because the structural pass changed the line count."));
        report.diagnostic = diagnostics.join(QLatin1Char(' '));
        return report;
    }
    for (const auto& range : conservativeRanges) {
        if (!range.valid() || originalLines.isEmpty())
            continue;
        const int first =
            qBound(0, range.firstLine, originalLines.size() - 1);
        const int last =
            qBound(first,
                   range.lastLine,
                   originalLines.size() - 1);
        for (int line = first; line <= last; ++line)
            formatted[line] = originalLines.at(line);
    }

    report.formattedText =
        joinLinesPreservingFinalNewline(formatted, hadFinalNewline);
    if (options.alignPortLists || options.alignInstanceMaps) {
        report.formattedText = StructuredWhitespaceFormatter::format(
            report.formattedText,
            options.indentWidth,
            &stageDiagnostic);
        if (!stageDiagnostic.isEmpty())
            diagnostics.append(stageDiagnostic);
    }
    report.formattedText =
        restoreOriginalLineEndings(report.formattedText, text);
    if (!StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
            text,
            report.formattedText)) {
        report.formattedText = normalizedText;
        diagnostics.append(QStringLiteral(
            "Formatting was limited to lexical whitespace because the structural result changed the token stream."));
    }
    report.changed = report.formattedText != text;
    report.formattedLines = formatted.size();
    report.diagnostic = diagnostics.join(QLatin1Char(' '));
    if (!report.diagnostic.isEmpty()) {
        report.outcome = FormatterOutcome::ConservativeFallback;
    } else {
        report.outcome = report.changed
            ? FormatterOutcome::Applied
            : FormatterOutcome::Unchanged;
    }
    return report;
}

FormatterReport FormatterService::formatSnippet(
    const QString& text,
    const FormatterOptions& options) const
{
    FormatterReport report;
    if (text.isEmpty())
        return report;

    QString selectionDiagnostic;
    const QString normalizedText =
        StructuredWhitespaceFormatter::
            normalizeLexicalWhitespaceTabs(
                text, 4, &selectionDiagnostic);
    if (!selectionDiagnostic.isEmpty()) {
        report.outcome = FormatterOutcome::ConservativeFallback;
        report.diagnostic = selectionDiagnostic;
    }
    const bool hadFinalNewline =
        normalizedText.endsWith(QLatin1Char('\n'));
    QStringList lines = splitLines(normalizedText);
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
    if (inner.outcome == FormatterOutcome::ConservativeFallback) {
        report.outcome = inner.outcome;
        if (!report.diagnostic.isEmpty())
            report.diagnostic += QLatin1Char(' ');
        report.diagnostic += inner.diagnostic;
    }
    QStringList formatted = splitLines(inner.formattedText);
    if (hadFinalNewline && !formatted.isEmpty() && formatted.last().isEmpty())
        formatted.removeLast();

    for (QString& line : formatted) {
        if (lineHasCode(line))
            line.prepend(baseIndent);
    }

    report.formattedText =
        joinLinesPreservingFinalNewline(formatted, hadFinalNewline);
    report.formattedText =
        restoreOriginalLineEndings(report.formattedText, text);
    if (!StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
            text,
            report.formattedText)) {
        report.formattedText = normalizedText;
        report.outcome = FormatterOutcome::ConservativeFallback;
        if (!report.diagnostic.isEmpty())
            report.diagnostic += QLatin1Char(' ');
        report.diagnostic += QStringLiteral(
            "Snippet formatting was limited to lexical whitespace because the result changed the token stream.");
    }
    report.changed = report.formattedText != text;
    report.formattedLines = formatted.size();
    if (report.outcome != FormatterOutcome::ConservativeFallback) {
        report.outcome = report.changed
            ? FormatterOutcome::Applied
            : FormatterOutcome::Unchanged;
    }
    return report;
}
