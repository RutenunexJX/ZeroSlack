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
    QString terminator = QStringLiteral(";");
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

bool isContextualOpeningToken(const QStringList& tokens, int index)
{
    if (index < 0 || index >= tokens.size())
        return false;

    const QString token = tokens.at(index);
    if (token == QStringLiteral("program")
        || token == QStringLiteral("primitive")
        || token == QStringLiteral("checker")
        || token == QStringLiteral("covergroup")
        || token == QStringLiteral("property")
        || token == QStringLiteral("sequence")
        || token == QStringLiteral("specify")
        || token == QStringLiteral("table")) {
        return index == 0;
    }
    if (token == QStringLiteral("clocking")) {
        return index == 0
            || (index == 1 && tokens.first() == QStringLiteral("default"));
    }
    if (token == QStringLiteral("fork"))
        return index == 0;
    return isOpeningToken(token);
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

int countOpeningTokens(const QStringList& tokens)
{
    int count = 0;
    for (int i = 0; i < tokens.size(); ++i) {
        if (isContextualOpeningToken(tokens, i))
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

int delimiterContinuationBalance(const QString& codeOnly);
bool lineHasCode(const QString& line);

bool containsToken(const QStringList& tokens, const QString& needle)
{
    for (const QString& token : tokens) {
        if (token == needle)
            return true;
    }
    return false;
}

bool isSingleStatementControlHeaderKeyword(const QString& token)
{
    return token == QStringLiteral("if")
        || token == QStringLiteral("for")
        || token == QStringLiteral("foreach")
        || token == QStringLiteral("while")
        || token == QStringLiteral("repeat")
        || token == QStringLiteral("forever")
        || token == QStringLiteral("always")
        || token == QStringLiteral("always_comb")
        || token == QStringLiteral("always_ff")
        || token == QStringLiteral("always_latch")
        || token == QStringLiteral("initial")
        || token == QStringLiteral("final")
        || token == QStringLiteral("else");
}

bool hasSingleStatementHeaderBlockToken(const QStringList& tokens)
{
    return containsToken(tokens, QStringLiteral("begin"))
        || containsToken(tokens, QStringLiteral("fork"))
        || containsToken(tokens, QStringLiteral("case"))
        || containsToken(tokens, QStringLiteral("casex"))
        || containsToken(tokens, QStringLiteral("casez"));
}

bool isSingleStatementControlHeader(const QString& codeOnly)
{
    const QString trimmed = codeOnly.trimmed();
    if (trimmed.isEmpty()
        || trimmed.endsWith(QLatin1Char(';'))
        || startsWithPreprocessor(trimmed)
        || delimiterContinuationBalance(trimmed) != 0) {
        return false;
    }

    const QStringList tokens = codeTokens(trimmed);
    if (tokens.isEmpty())
        return false;
    if (hasSingleStatementHeaderBlockToken(tokens))
        return false;

    return isSingleStatementControlHeaderKeyword(tokens.first());
}

bool startsSingleStatementControlHeader(const QString& codeOnly)
{
    const QString trimmed = codeOnly.trimmed();
    if (trimmed.isEmpty()
        || trimmed.endsWith(QLatin1Char(';'))
        || startsWithPreprocessor(trimmed)
        || delimiterContinuationBalance(trimmed) <= 0) {
        return false;
    }

    const QStringList tokens = codeTokens(trimmed);
    if (tokens.isEmpty())
        return false;
    if (hasSingleStatementHeaderBlockToken(tokens))
        return false;

    return isSingleStatementControlHeaderKeyword(tokens.first());
}

int singleStatementControlHeaderEndIndex(const QStringList& lines,
                                         int startIndex,
                                         const QString& startCode)
{
    if (isSingleStatementControlHeader(startCode))
        return startIndex;
    if (!startsSingleStatementControlHeader(startCode))
        return -1;

    int balance = delimiterContinuationBalance(startCode);
    bool inBlockComment = false;
    for (int i = startIndex + 1; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        if (!lineHasCode(line))
            return -1;
        if (startsWithPreprocessor(line))
            return -1;

        const QString codeOnly = codeOnlyLine(line, &inBlockComment);
        const QString trimmed = codeOnly.trimmed();
        if (inBlockComment
            || trimmed.isEmpty()
            || trimmed.endsWith(QLatin1Char(';'))) {
            return -1;
        }

        const QStringList tokens = codeTokens(trimmed);
        if (hasSingleStatementHeaderBlockToken(tokens))
            return -1;

        balance += delimiterContinuationBalance(trimmed);
        if (balance == 0)
            return i;
        if (balance < 0)
            return -1;
    }

    return -1;
}

bool startsWithElseOrClosingToken(const QString& codeOnly)
{
    const QStringList tokens = codeTokens(codeOnly);
    if (tokens.isEmpty())
        return false;
    return tokens.first() == QStringLiteral("else")
        || isClosingToken(tokens.first());
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
        if (terminator != QStringLiteral(";"))
            return parsed;
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
    parsed.terminator = terminator;
    parsed.hasAssignment = equalIndex >= 0;
    if (parsed.hasAssignment)
        parsed.assignmentRhs = codeBody.mid(equalIndex + 1).trimmed();
    parsed.trailingComment = parts.trailingComment;
    return parsed;
}

QString buildAlignedDeclarationCodeLine(const DeclarationAlignmentLine& line,
                                        int maxPrefixWidth,
                                        int maxNameWidth,
                                        int maxBeforeAssignmentWidth,
                                        bool alignAssignment)
{
    QString content = line.prefix
        + repeatSpaces(maxPrefixWidth - line.prefix.size() + 1)
        + line.name;
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
            + line.assignmentRhs
            + line.terminator;
    } else {
        content += line.terminator;
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
    int maxNameWidth = 0;
    int assignmentCount = 0;
    for (const DeclarationAlignmentLine& line : block) {
        maxPrefixWidth = std::max(maxPrefixWidth,
                                  static_cast<int>(line.prefix.size()));
        maxNameWidth = std::max(maxNameWidth,
                                static_cast<int>(line.name.size()));
        if (line.hasAssignment)
            ++assignmentCount;
    }

    int maxBeforeAssignmentWidth = 0;
    for (const DeclarationAlignmentLine& line : block) {
        QString content = line.prefix
            + repeatSpaces(maxPrefixWidth - line.prefix.size() + 1)
            + line.name;
        if (!line.suffix.isEmpty()) {
            content += repeatSpaces(maxNameWidth - line.name.size() + 1);
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
                                            maxNameWidth,
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

void indentSingleStatementBodyLines(QStringList* lines, int indentWidth)
{
    if (!lines)
        return;

    bool inBlockComment = false;
    for (int i = 0; i < lines->size(); ++i) {
        const QString headerLine = lines->at(i);
        if (!lineHasCode(headerLine) || startsWithPreprocessor(headerLine))
            continue;

        const QString headerCode = codeOnlyLine(headerLine, &inBlockComment);
        const int headerEndIndex =
            singleStatementControlHeaderEndIndex(*lines, i, headerCode);
        if (headerEndIndex < 0)
            continue;

        const int headerIndent = leadingWhitespaceWidth(headerLine);
        for (int bodyIndex = headerEndIndex + 1;
             bodyIndex < lines->size();
             ++bodyIndex) {
            const QString bodyLine = lines->at(bodyIndex);
            if (!lineHasCode(bodyLine))
                continue;
            if (startsWithPreprocessor(bodyLine))
                break;

            bool bodyBlockComment = false;
            const QString bodyCode =
                codeOnlyLine(bodyLine, &bodyBlockComment);
            if (bodyCode.trimmed().isEmpty())
                continue;
            if (bodyBlockComment
                || startsWithElseOrClosingToken(bodyCode)) {
                break;
            }

            (*lines)[bodyIndex] =
                indentation(headerIndent / std::max(1, indentWidth) + 1,
                            indentWidth)
                + stripLeadingWhitespace(bodyLine);
            break;
        }
        i = headerEndIndex;
    }
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

bool isLabelOnlyCaseItemLine(const QString& codeOnly)
{
    const QString trimmed = codeOnly.trimmed();
    if (trimmed.isEmpty())
        return false;

    const int colonIndex = findTopLevelCaseItemColon(trimmed);
    if (colonIndex <= 0)
        return false;
    return trimmed.mid(colonIndex + 1).trimmed().isEmpty();
}

bool isSimpleCaseItemBodyLine(const QString& codeOnly)
{
    const QString trimmed = codeOnly.trimmed();
    if (trimmed.isEmpty()
        || startsWithPreprocessor(trimmed)
        || !trimmed.endsWith(QLatin1Char(';'))) {
        return false;
    }

    const QStringList tokens = codeTokens(trimmed);
    if (tokens.isEmpty())
        return false;
    if (isClosingToken(tokens.first()))
        return false;
    if (hasSingleStatementHeaderBlockToken(tokens))
        return false;
    return true;
}

void indentCaseItemBodyLines(QStringList* lines, int indentWidth)
{
    if (!lines)
        return;

    QList<int> caseItemIndentStack;
    int pendingBodyIndent = -1;
    bool inBlockComment = false;
    for (int i = 0; i < lines->size(); ++i) {
        const QString line = lines->at(i);
        if (!lineHasCode(line))
            continue;
        if (startsWithPreprocessor(line)) {
            pendingBodyIndent = -1;
            continue;
        }

        const QString codeOnly = codeOnlyLine(line, &inBlockComment);
        const QString trimmed = codeOnly.trimmed();
        if (trimmed.isEmpty())
            continue;

        const QStringList tokens = codeTokens(codeOnly);
        const bool closingCase = isCaseClosingLine(tokens);
        const bool caseItem =
            !caseItemIndentStack.isEmpty()
            && leadingWhitespaceWidth(line) == caseItemIndentStack.last()
            && parseCaseItemAlignmentLine(line).valid;

        if (pendingBodyIndent >= 0 && !closingCase && !caseItem) {
            if (isSimpleCaseItemBodyLine(codeOnly)) {
                (*lines)[i] =
                    repeatSpaces(pendingBodyIndent)
                    + stripLeadingWhitespace(line);
            }
            pendingBodyIndent = -1;
        } else if (pendingBodyIndent >= 0 && (closingCase || caseItem)) {
            pendingBodyIndent = -1;
        }

        if (closingCase) {
            if (!caseItemIndentStack.isEmpty())
                caseItemIndentStack.removeLast();
            continue;
        }

        if (caseItem) {
            pendingBodyIndent =
                isLabelOnlyCaseItemLine(codeOnly)
                    ? leadingWhitespaceWidth(line) + std::max(1, indentWidth)
                    : -1;
        }

        if (isCaseOpeningLine(tokens)) {
            pendingBodyIndent = -1;
            caseItemIndentStack.append(
                leadingWhitespaceWidth(line) + std::max(1, indentWidth));
        }
    }
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
    if (code.isEmpty()
        || code.contains(QLatin1Char('{'))
        || code.contains(QLatin1Char('}'))
        || code.endsWith(QLatin1Char(';'))) {
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
        options.alignEnumItems = false;
        options.alignAssignments = false;
        options.alignContinuationOperators = false;
        options.alignCallArgumentContinuations = false;
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
    int assignmentRhsContinuationLevel = 0;
    bool inBlockComment = false;

    for (const QString& line : lines) {
        if (!lineHasCode(line)) {
            formatted.append(QString());
            continue;
        }

        if (startsWithPreprocessor(line) && options.preservePreprocessorIndent) {
            assignmentRhsContinuationLevel = 0;
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
        const int displayAssignmentContinuation =
            options.indentAssignmentRhsContinuations
                ? assignmentRhsContinuationLevel
                : 0;
        const int displayIndent =
            std::max(0, indentLevel - leadingClosingTokens(tokens))
            + displayContinuation
            + displayAssignmentContinuation;
        const QString body = stripLeadingWhitespace(line);
        formatted.append(indentation(displayIndent, options.indentWidth) + body);

        const bool startsAssignmentRhsContinuation =
            options.indentAssignmentRhsContinuations
            && endsWithTopLevelAssignmentOperator(codeOnly);
        if (options.indentAssignmentRhsContinuations
            && assignmentRhsContinuationLevel > 0
            && endsStatement(codeOnly)) {
            assignmentRhsContinuationLevel = 0;
        }
        if (startsAssignmentRhsContinuation)
            assignmentRhsContinuationLevel = 1;

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

    if (options.indentSingleStatementBodies)
        indentSingleStatementBodyLines(&formatted, options.indentWidth);
    if (options.indentCaseItemBodies)
        indentCaseItemBodyLines(&formatted, options.indentWidth);

    if (options.alignDeclarationBlocks)
        alignDeclarationBlocks(&formatted);
    if (options.alignPortLists)
        alignPortListBlocks(&formatted);
    if (options.alignInstanceMaps)
        alignInstanceMapBlocks(&formatted);
    if (options.alignCaseItems)
        alignCaseItemBlocks(&formatted, options.indentWidth);
    if (options.alignEnumItems)
        alignEnumItemBlocks(&formatted, options.indentWidth);
    if (options.alignAssignments)
        alignAssignmentBlocks(&formatted);
    if (options.alignContinuationOperators)
        alignContinuationOperatorLines(&formatted);
    if (options.alignCallArgumentContinuations)
        alignCallArgumentContinuationLines(&formatted);

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
