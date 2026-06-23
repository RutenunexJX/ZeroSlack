#include "wavepreviewservice.h"

#include <QHash>
#include <QSet>

#include <algorithm>

std::unique_ptr<WavePreviewService> WavePreviewService::instance = nullptr;

namespace {
enum class TokenKind {
    Identifier,
    Number,
    StringLiteral,
    Symbol
};

struct Token {
    TokenKind kind = TokenKind::Symbol;
    QString text;
    int start = 0;
    int end = 0;
    int line = 1;
    int column = 1;
};

bool isAsciiAlpha(QChar ch)
{
    const ushort value = ch.unicode();
    return (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z');
}

bool isAsciiDigit(QChar ch)
{
    const ushort value = ch.unicode();
    return value >= '0' && value <= '9';
}

bool isIdentifierStart(QChar ch)
{
    return ch == QLatin1Char('_')
        || ch == QLatin1Char('$')
        || isAsciiAlpha(ch);
}

bool isIdentifierPart(QChar ch)
{
    return isIdentifierStart(ch) || isAsciiDigit(ch);
}

bool isNumberPart(QChar ch)
{
    return isIdentifierPart(ch)
        || ch == QLatin1Char('\'')
        || ch == QLatin1Char('_')
        || ch == QLatin1Char('?');
}

void advancePosition(QChar ch, int* line, int* column)
{
    if (ch == QLatin1Char('\n')) {
        ++(*line);
        *column = 1;
        return;
    }
    if (ch != QLatin1Char('\r'))
        ++(*column);
}

void appendToken(QList<Token>* tokens,
                 TokenKind kind,
                 const QString& text,
                 int start,
                 int end,
                 int line,
                 int column)
{
    if (!tokens || text.isEmpty())
        return;
    Token token;
    token.kind = kind;
    token.text = text;
    token.start = start;
    token.end = end;
    token.line = line;
    token.column = column;
    tokens->append(token);
}

QList<Token> tokenize(const QString& text)
{
    QList<Token> tokens;
    int line = 1;
    int column = 1;
    int pos = 0;
    while (pos < text.size()) {
        const QChar ch = text.at(pos);
        const QChar next = (pos + 1 < text.size()) ? text.at(pos + 1) : QChar();

        if (ch.isSpace()) {
            advancePosition(ch, &line, &column);
            ++pos;
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            while (pos < text.size() && text.at(pos) != QLatin1Char('\n')) {
                advancePosition(text.at(pos), &line, &column);
                ++pos;
            }
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            advancePosition(ch, &line, &column);
            advancePosition(next, &line, &column);
            pos += 2;
            while (pos < text.size()) {
                const QChar current = text.at(pos);
                const QChar after =
                    (pos + 1 < text.size()) ? text.at(pos + 1) : QChar();
                advancePosition(current, &line, &column);
                ++pos;
                if (current == QLatin1Char('*') && after == QLatin1Char('/')) {
                    advancePosition(after, &line, &column);
                    ++pos;
                    break;
                }
            }
            continue;
        }

        if (ch == QLatin1Char('"')) {
            const int start = pos;
            const int startLine = line;
            const int startColumn = column;
            bool escaped = false;
            advancePosition(ch, &line, &column);
            ++pos;
            while (pos < text.size()) {
                const QChar current = text.at(pos);
                advancePosition(current, &line, &column);
                ++pos;
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (current == QLatin1Char('\\')) {
                    escaped = true;
                    continue;
                }
                if (current == QLatin1Char('"'))
                    break;
            }
            appendToken(&tokens,
                        TokenKind::StringLiteral,
                        text.mid(start, pos - start),
                        start,
                        pos,
                        startLine,
                        startColumn);
            continue;
        }

        if (isIdentifierStart(ch)) {
            const int start = pos;
            const int startLine = line;
            const int startColumn = column;
            while (pos < text.size() && isIdentifierPart(text.at(pos))) {
                advancePosition(text.at(pos), &line, &column);
                ++pos;
            }
            appendToken(&tokens,
                        TokenKind::Identifier,
                        text.mid(start, pos - start),
                        start,
                        pos,
                        startLine,
                        startColumn);
            continue;
        }

        if (isAsciiDigit(ch)) {
            const int start = pos;
            const int startLine = line;
            const int startColumn = column;
            while (pos < text.size() && isNumberPart(text.at(pos))) {
                advancePosition(text.at(pos), &line, &column);
                ++pos;
            }
            appendToken(&tokens,
                        TokenKind::Number,
                        text.mid(start, pos - start),
                        start,
                        pos,
                        startLine,
                        startColumn);
            continue;
        }

        const int start = pos;
        const int startLine = line;
        const int startColumn = column;
        QString symbol;
        symbol.append(ch);
        if ((ch == QLatin1Char('<') || ch == QLatin1Char('>')
             || ch == QLatin1Char('=') || ch == QLatin1Char('!'))
            && next == QLatin1Char('=')) {
            symbol.append(next);
            advancePosition(ch, &line, &column);
            advancePosition(next, &line, &column);
            pos += 2;
        } else if ((ch == QLatin1Char('&') && next == QLatin1Char('&'))
                   || (ch == QLatin1Char('|') && next == QLatin1Char('|'))
                   || (ch == QLatin1Char('+') && next == QLatin1Char('+'))
                   || (ch == QLatin1Char('-') && next == QLatin1Char('-'))
                   || (ch == QLatin1Char(':') && next == QLatin1Char(':'))) {
            symbol.append(next);
            advancePosition(ch, &line, &column);
            advancePosition(next, &line, &column);
            pos += 2;
        } else {
            advancePosition(ch, &line, &column);
            ++pos;
        }
        appendToken(&tokens,
                    TokenKind::Symbol,
                    symbol,
                    start,
                    pos,
                    startLine,
                    startColumn);
    }
    return tokens;
}

bool isIdentifierToken(const Token& token, const QString& text)
{
    return token.kind == TokenKind::Identifier && token.text == text;
}

bool isAlwaysToken(const Token& token)
{
    if (token.kind != TokenKind::Identifier)
        return false;
    return token.text == QStringLiteral("always")
        || token.text == QStringLiteral("always_comb")
        || token.text == QStringLiteral("always_ff")
        || token.text == QStringLiteral("always_latch");
}

bool isCaseToken(const Token& token)
{
    if (token.kind != TokenKind::Identifier)
        return false;
    return token.text == QStringLiteral("case")
        || token.text == QStringLiteral("casex")
        || token.text == QStringLiteral("casez");
}

bool isKeyword(const QString& text)
{
    static const QSet<QString> keywords{
        QStringLiteral("always"),
        QStringLiteral("always_comb"),
        QStringLiteral("always_ff"),
        QStringLiteral("always_latch"),
        QStringLiteral("and"),
        QStringLiteral("assign"),
        QStringLiteral("begin"),
        QStringLiteral("bit"),
        QStringLiteral("case"),
        QStringLiteral("casex"),
        QStringLiteral("casez"),
        QStringLiteral("class"),
        QStringLiteral("default"),
        QStringLiteral("else"),
        QStringLiteral("end"),
        QStringLiteral("endcase"),
        QStringLiteral("endclass"),
        QStringLiteral("endfunction"),
        QStringLiteral("endmodule"),
        QStringLiteral("endpackage"),
        QStringLiteral("endtask"),
        QStringLiteral("enum"),
        QStringLiteral("for"),
        QStringLiteral("foreach"),
        QStringLiteral("function"),
        QStringLiteral("generate"),
        QStringLiteral("genvar"),
        QStringLiteral("if"),
        QStringLiteral("input"),
        QStringLiteral("inout"),
        QStringLiteral("int"),
        QStringLiteral("integer"),
        QStringLiteral("localparam"),
        QStringLiteral("logic"),
        QStringLiteral("module"),
        QStringLiteral("negedge"),
        QStringLiteral("or"),
        QStringLiteral("output"),
        QStringLiteral("package"),
        QStringLiteral("parameter"),
        QStringLiteral("posedge"),
        QStringLiteral("reg"),
        QStringLiteral("repeat"),
        QStringLiteral("return"),
        QStringLiteral("signed"),
        QStringLiteral("struct"),
        QStringLiteral("task"),
        QStringLiteral("typedef"),
        QStringLiteral("unique"),
        QStringLiteral("unsigned"),
        QStringLiteral("wire")
    };
    return keywords.contains(text);
}

int nextSemicolon(const QList<Token>& tokens, int from, int limit)
{
    const int end = qMin(limit, tokens.size() - 1);
    for (int i = qMax(0, from); i <= end; ++i) {
        if (tokens.at(i).text == QLatin1String(";"))
            return i;
    }
    return -1;
}

int matchingSymbol(const QList<Token>& tokens,
                   int openIndex,
                   const QString& openText,
                   const QString& closeText,
                   int limit)
{
    if (openIndex < 0 || openIndex >= tokens.size()
        || tokens.at(openIndex).text != openText) {
        return -1;
    }
    int depth = 0;
    const int end = qMin(limit, tokens.size() - 1);
    for (int i = openIndex; i <= end; ++i) {
        if (tokens.at(i).text == openText)
            ++depth;
        else if (tokens.at(i).text == closeText) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

int matchingBeginEnd(const QList<Token>& tokens, int beginIndex, int limit)
{
    if (beginIndex < 0 || beginIndex >= tokens.size()
        || !isIdentifierToken(tokens.at(beginIndex), QStringLiteral("begin"))) {
        return -1;
    }
    int depth = 0;
    const int end = qMin(limit, tokens.size() - 1);
    for (int i = beginIndex; i <= end; ++i) {
        if (isIdentifierToken(tokens.at(i), QStringLiteral("begin")))
            ++depth;
        else if (isIdentifierToken(tokens.at(i), QStringLiteral("end"))) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

int matchingCaseEnd(const QList<Token>& tokens, int caseIndex, int limit)
{
    if (caseIndex < 0 || caseIndex >= tokens.size()
        || !isCaseToken(tokens.at(caseIndex))) {
        return -1;
    }

    int depth = 0;
    const int end = qMin(limit, tokens.size() - 1);
    for (int i = caseIndex; i <= end; ++i) {
        if (isCaseToken(tokens.at(i))) {
            ++depth;
        } else if (isIdentifierToken(tokens.at(i), QStringLiteral("endcase"))) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

int skipBracketSelects(const QList<Token>& tokens, int index, int limit)
{
    int pos = index;
    while (pos <= limit && tokens.at(pos).text == QLatin1String("[")) {
        const int close = matchingSymbol(tokens,
                                         pos,
                                         QStringLiteral("["),
                                         QStringLiteral("]"),
                                         limit);
        if (close < 0)
            return pos;
        pos = close + 1;
    }
    return pos;
}

bool parseLvalueAt(const QList<Token>& tokens,
                   int start,
                   int limit,
                   QString* target,
                   int* operatorIndex)
{
    if (start < 0 || start >= tokens.size() || start > limit)
        return false;
    if (tokens.at(start).kind != TokenKind::Identifier)
        return false;
    if (tokens.at(start).text.startsWith(QLatin1Char('$'))
        || isKeyword(tokens.at(start).text)) {
        return false;
    }
    if (start > 0 && tokens.at(start - 1).text == QLatin1String("."))
        return false;

    QString name = tokens.at(start).text;
    int pos = start + 1;
    pos = skipBracketSelects(tokens, pos, limit);
    while (pos + 1 <= limit
           && tokens.at(pos).text == QLatin1String(".")
           && tokens.at(pos + 1).kind == TokenKind::Identifier
           && !tokens.at(pos + 1).text.startsWith(QLatin1Char('$'))) {
        name += QLatin1Char('.');
        name += tokens.at(pos + 1).text;
        pos += 2;
        pos = skipBracketSelects(tokens, pos, limit);
    }

    if (pos > limit)
        return false;
    const QString op = tokens.at(pos).text;
    if (op != QLatin1String("=") && op != QLatin1String("<="))
        return false;

    if (target)
        *target = name;
    if (operatorIndex)
        *operatorIndex = pos;
    return true;
}

QStringList sourceSignalsBetween(const QList<Token>& tokens,
                                 int from,
                                 int to,
                                 const QString& target)
{
    QStringList refs;
    QSet<QString> seen;
    const int end = qMin(to, tokens.size());
    int pos = qMax(0, from);
    while (pos < end) {
        const Token& token = tokens.at(pos);
        if (token.kind != TokenKind::Identifier
            || token.text.startsWith(QLatin1Char('$'))
            || isKeyword(token.text)
            || (pos > 0 && tokens.at(pos - 1).text == QLatin1String("."))) {
            ++pos;
            continue;
        }

        QString name = token.text;
        int next = pos + 1;
        while (next + 1 < end
               && tokens.at(next).text == QLatin1String(".")
               && tokens.at(next + 1).kind == TokenKind::Identifier) {
            name += QLatin1Char('.');
            name += tokens.at(next + 1).text;
            next += 2;
        }
        if (name != target && !seen.contains(name)) {
            seen.insert(name);
            refs.append(name);
        }
        pos = qMax(next, pos + 1);
    }
    return refs;
}

QString expressionBetween(const QString& text,
                          const QList<Token>& tokens,
                          int operatorIndex,
                          int semicolonIndex)
{
    if (operatorIndex < 0 || semicolonIndex < 0
        || operatorIndex >= tokens.size() || semicolonIndex >= tokens.size()) {
        return QString();
    }
    const int start = tokens.at(operatorIndex).end;
    const int end = tokens.at(semicolonIndex).start;
    if (start < 0 || end < start || end > text.size())
        return QString();
    return text.mid(start, end - start).trimmed();
}

QString sourceTextBetween(const QString& text,
                          const QList<Token>& tokens,
                          int from,
                          int to)
{
    if (from < 0 || to < from || from >= tokens.size() || to >= tokens.size())
        return QString();

    const int start = tokens.at(from).start;
    const int end = tokens.at(to).end;
    if (start < 0 || end < start || end > text.size())
        return QString();
    return text.mid(start, end - start).simplified();
}

bool statementBodyRange(const QList<Token>& tokens,
                        int start,
                        int limit,
                        int* bodyStart,
                        int* bodyEnd)
{
    if (start < 0 || start >= tokens.size() || start > limit)
        return false;

    if (isIdentifierToken(tokens.at(start), QStringLiteral("begin"))) {
        const int end = matchingBeginEnd(tokens, start, limit);
        if (end < 0)
            return false;
        if (bodyStart)
            *bodyStart = start + 1;
        if (bodyEnd)
            *bodyEnd = end - 1;
        return true;
    }

    const int end = nextSemicolon(tokens, start, limit);
    if (end < 0)
        return false;
    if (bodyStart)
        *bodyStart = start;
    if (bodyEnd)
        *bodyEnd = end;
    return true;
}

QString ifConditionTextForToken(const QString& text,
                                const QList<Token>& tokens,
                                int ifIndex,
                                int limit,
                                int* conditionEnd)
{
    if (ifIndex < 0 || ifIndex >= tokens.size()
        || !isIdentifierToken(tokens.at(ifIndex), QStringLiteral("if"))) {
        return QString();
    }
    int openIndex = -1;
    for (int i = ifIndex + 1; i <= limit && i < tokens.size(); ++i) {
        if (tokens.at(i).text == QLatin1String("(")) {
            openIndex = i;
            break;
        }
        if (tokens.at(i).text == QLatin1String(";"))
            return QString();
    }
    if (openIndex < 0)
        return QString();
    const int closeIndex = matchingSymbol(tokens,
                                          openIndex,
                                          QStringLiteral("("),
                                          QStringLiteral(")"),
                                          limit);
    if (closeIndex < 0)
        return QString();
    if (conditionEnd)
        *conditionEnd = closeIndex;
    return sourceTextBetween(text, tokens, openIndex + 1, closeIndex - 1);
}

QString ifGuardTextForAssignment(const QString& text,
                                 const QList<Token>& tokens,
                                 int from,
                                 int assignmentIndex,
                                 int limit)
{
    QStringList guards;
    const int start = qMax(0, from);
    const int end = qMin(assignmentIndex - 1, tokens.size() - 1);
    for (int i = start; i <= end; ++i) {
        if (!isIdentifierToken(tokens.at(i), QStringLiteral("if")))
            continue;

        int conditionEnd = -1;
        const QString condition =
            ifConditionTextForToken(text, tokens, i, limit, &conditionEnd);
        if (condition.isEmpty() || conditionEnd < 0)
            continue;

        int bodyStart = -1;
        int bodyEnd = -1;
        if (!statementBodyRange(tokens,
                                conditionEnd + 1,
                                limit,
                                &bodyStart,
                                &bodyEnd)) {
            continue;
        }
        if (assignmentIndex >= bodyStart && assignmentIndex <= bodyEnd)
            guards.append(QStringLiteral("if %1").arg(condition));
    }

    for (int i = start; i <= end; ++i) {
        if (!isIdentifierToken(tokens.at(i), QStringLiteral("else")))
            continue;
        if (i + 1 < tokens.size()
            && isIdentifierToken(tokens.at(i + 1), QStringLiteral("if"))) {
            continue;
        }

        int bodyStart = -1;
        int bodyEnd = -1;
        if (!statementBodyRange(tokens,
                                i + 1,
                                limit,
                                &bodyStart,
                                &bodyEnd)) {
            continue;
        }
        if (assignmentIndex >= bodyStart && assignmentIndex <= bodyEnd)
            guards.append(QStringLiteral("else"));
    }

    guards.removeDuplicates();
    return guards.join(QStringLiteral(" && "));
}

QString caseLabelForAssignment(const QString& text,
                               const QList<Token>& tokens,
                               int caseIndex,
                               int assignmentIndex,
                               int limit)
{
    int openIndex = -1;
    for (int i = caseIndex + 1; i <= limit && i < tokens.size(); ++i) {
        if (tokens.at(i).text == QLatin1String("(")) {
            openIndex = i;
            break;
        }
        if (tokens.at(i).text == QLatin1String(";"))
            return QString();
    }
    if (openIndex < 0)
        return QString();

    const int selectorEnd = matchingSymbol(tokens,
                                           openIndex,
                                           QStringLiteral("("),
                                           QStringLiteral(")"),
                                           limit);
    const int caseEnd = matchingCaseEnd(tokens, caseIndex, limit);
    if (selectorEnd < 0 || caseEnd < 0
        || assignmentIndex <= selectorEnd
        || assignmentIndex >= caseEnd) {
        return QString();
    }

    const QString selector =
        sourceTextBetween(text, tokens, openIndex + 1, selectorEnd - 1);
    int labelStartCandidate = selectorEnd + 1;
    int currentLabelStart = -1;
    int currentLabelEnd = -1;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    int beginDepth = 0;
    for (int i = selectorEnd + 1; i < caseEnd && i < assignmentIndex; ++i) {
        const QString tokenText = tokens.at(i).text;
        const bool topLevel =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == 0
            && beginDepth == 0;

        if (topLevel && tokenText == QLatin1String(":")) {
            currentLabelStart = labelStartCandidate;
            currentLabelEnd = i - 1;
        } else if (topLevel && tokenText == QLatin1String(";")) {
            labelStartCandidate = i + 1;
        } else if (topLevel
                   && isIdentifierToken(tokens.at(i), QStringLiteral("end"))) {
            labelStartCandidate = i + 1;
        }

        if (tokenText == QLatin1String("("))
            ++parenDepth;
        else if (tokenText == QLatin1String(")"))
            parenDepth = qMax(0, parenDepth - 1);
        else if (tokenText == QLatin1String("["))
            ++bracketDepth;
        else if (tokenText == QLatin1String("]"))
            bracketDepth = qMax(0, bracketDepth - 1);
        else if (tokenText == QLatin1String("{"))
            ++braceDepth;
        else if (tokenText == QLatin1String("}"))
            braceDepth = qMax(0, braceDepth - 1);
        else if (isIdentifierToken(tokens.at(i), QStringLiteral("begin")))
            ++beginDepth;
        else if (isIdentifierToken(tokens.at(i), QStringLiteral("end")))
            beginDepth = qMax(0, beginDepth - 1);
    }

    if (currentLabelStart < 0 || currentLabelEnd < currentLabelStart)
        return QString();

    const QString label =
        sourceTextBetween(text, tokens, currentLabelStart, currentLabelEnd);
    if (label.isEmpty())
        return QString();
    return selector.isEmpty()
        ? QStringLiteral("case %1").arg(label)
        : QStringLiteral("case %1: %2").arg(selector, label);
}

QString caseGuardTextForAssignment(const QString& text,
                                   const QList<Token>& tokens,
                                   int from,
                                   int assignmentIndex,
                                   int limit)
{
    QStringList guards;
    const int start = qMax(0, from);
    const int end = qMin(assignmentIndex - 1, tokens.size() - 1);
    for (int i = start; i <= end; ++i) {
        if (!isCaseToken(tokens.at(i)))
            continue;
        const QString label =
            caseLabelForAssignment(text, tokens, i, assignmentIndex, limit);
        if (!label.isEmpty())
            guards.append(label);
    }
    guards.removeDuplicates();
    return guards.join(QStringLiteral(" && "));
}

QString guardTextForAssignment(const QString& text,
                               const QList<Token>& tokens,
                               int from,
                               int assignmentIndex,
                               int limit)
{
    QStringList guards;
    const QString ifGuard =
        ifGuardTextForAssignment(text, tokens, from, assignmentIndex, limit);
    if (!ifGuard.isEmpty())
        guards.append(ifGuard);
    const QString caseGuard =
        caseGuardTextForAssignment(text, tokens, from, assignmentIndex, limit);
    if (!caseGuard.isEmpty())
        guards.append(caseGuard);
    guards.removeDuplicates();
    return guards.join(QStringLiteral(" && "));
}

WavePreviewBlockKind kindForAlways(const QList<Token>& tokens,
                                   int start,
                                   int end)
{
    const QString always = tokens.at(start).text;
    if (always == QStringLiteral("always_comb"))
        return WavePreviewBlockKind::AlwaysComb;
    if (always == QStringLiteral("always_ff"))
        return WavePreviewBlockKind::AlwaysFf;
    if (always == QStringLiteral("always_latch"))
        return WavePreviewBlockKind::AlwaysLatch;

    for (int i = start + 1; i <= end && i < tokens.size(); ++i) {
        if (tokens.at(i).text == QStringLiteral("posedge")
            || tokens.at(i).text == QStringLiteral("negedge")) {
            return WavePreviewBlockKind::AlwaysClocked;
        }
    }
    return WavePreviewBlockKind::AlwaysLevel;
}

QString triggerTextForAlways(const QString& text,
                             const QList<Token>& tokens,
                             int start,
                             int end)
{
    for (int i = start + 1; i <= end && i < tokens.size(); ++i) {
        if (tokens.at(i).text != QLatin1String("@"))
            continue;
        int triggerEnd = i;
        if (i + 1 <= end && tokens.at(i + 1).text == QLatin1String("(")) {
            const int close = matchingSymbol(tokens,
                                             i + 1,
                                             QStringLiteral("("),
                                             QStringLiteral(")"),
                                             end);
            if (close >= 0)
                triggerEnd = close;
        }
        const int startPos = tokens.at(i).start;
        const int endPos = tokens.at(triggerEnd).end;
        if (startPos >= 0 && endPos >= startPos && endPos <= text.size())
            return text.mid(startPos, endPos - startPos).trimmed();
    }
    return QString();
}

int firstBeginForAlways(const QList<Token>& tokens, int start, int limit)
{
    int parenDepth = 0;
    const int end = qMin(limit, tokens.size() - 1);
    for (int i = start + 1; i <= end; ++i) {
        const QString text = tokens.at(i).text;
        if (text == QLatin1String("(")) {
            ++parenDepth;
            continue;
        }
        if (text == QLatin1String(")")) {
            parenDepth = qMax(0, parenDepth - 1);
            continue;
        }
        if (parenDepth == 0
            && isIdentifierToken(tokens.at(i), QStringLiteral("begin"))) {
            return i;
        }
        if (parenDepth == 0 && text == QLatin1String(";"))
            return -1;
    }
    return -1;
}

WavePreviewAssignment makeAssignment(const QString& text,
                                     const QList<Token>& tokens,
                                     int lvalueIndex,
                                     int operatorIndex,
                                     int semicolonIndex,
                                     const QString& target,
                                     int blockIndex,
                                     const WavePreviewBlock& block)
{
    WavePreviewAssignment assignment;
    assignment.kind = tokens.at(operatorIndex).text == QLatin1String("<=")
        ? WavePreviewAssignmentKind::NonBlocking
        : WavePreviewAssignmentKind::Blocking;
    assignment.target = target;
    assignment.expression =
        expressionBetween(text, tokens, operatorIndex, semicolonIndex);
    assignment.sourceSignals =
        sourceSignalsBetween(tokens,
                             operatorIndex + 1,
                             semicolonIndex,
                             target);
    assignment.trigger = block.trigger;
    assignment.blockIndex = blockIndex;
    assignment.cycleOffset = block.isClocked() ? 1 : 0;
    assignment.line = tokens.at(lvalueIndex).line;
    assignment.column = tokens.at(lvalueIndex).column;
    assignment.startPosition = tokens.at(lvalueIndex).start;
    assignment.endPosition = tokens.at(semicolonIndex).end;
    return assignment;
}

QList<WavePreviewAssignment> assignmentsInRange(const QString& text,
                                                const QList<Token>& tokens,
                                                int from,
                                                int to,
                                                int blockIndex,
                                                const WavePreviewBlock& block)
{
    QList<WavePreviewAssignment> assignments;
    const int end = qMin(to, tokens.size() - 1);
    int pos = qMax(0, from);
    while (pos <= end) {
        QString target;
        int operatorIndex = -1;
        if (!parseLvalueAt(tokens, pos, end, &target, &operatorIndex)) {
            ++pos;
            continue;
        }

        const int semicolonIndex = nextSemicolon(tokens, operatorIndex + 1, end);
        if (semicolonIndex < 0) {
            ++pos;
            continue;
        }

        WavePreviewAssignment assignment =
            makeAssignment(text,
                           tokens,
                           pos,
                           operatorIndex,
                           semicolonIndex,
                           target,
                           blockIndex,
                           block);
        assignment.guardText =
            guardTextForAssignment(text, tokens, from, pos, end);
        if (assignment.isValid())
            assignments.append(assignment);
        pos = semicolonIndex + 1;
    }
    return assignments;
}

bool parseContinuousAssign(const QString& text,
                           const QList<Token>& tokens,
                           int assignIndex,
                           WavePreviewAssignment* assignment)
{
    if (!assignment || assignIndex < 0 || assignIndex >= tokens.size())
        return false;
    const int semicolonIndex =
        nextSemicolon(tokens, assignIndex + 1, tokens.size() - 1);
    if (semicolonIndex < 0)
        return false;

    QString target;
    int operatorIndex = -1;
    int lvalueIndex = -1;
    for (int i = assignIndex + 1; i < semicolonIndex; ++i) {
        if (parseLvalueAt(tokens, i, semicolonIndex - 1, &target, &operatorIndex)) {
            lvalueIndex = i;
            break;
        }
    }
    if (target.isEmpty() || operatorIndex < 0 || lvalueIndex < 0)
        return false;

    WavePreviewBlock continuousBlock;
    continuousBlock.kind = WavePreviewBlockKind::AlwaysLevel;
    continuousBlock.trigger = QStringLiteral("continuous");

    *assignment = makeAssignment(text,
                                 tokens,
                                 lvalueIndex,
                                 operatorIndex,
                                 semicolonIndex,
                                 target,
                                 -1,
                                 continuousBlock);
    assignment->kind = WavePreviewAssignmentKind::Continuous;
    assignment->cycleOffset = 0;
    assignment->trigger = QStringLiteral("continuous");
    return assignment->isValid();
}

bool parseAlwaysBlock(const QString& text,
                      const QList<Token>& tokens,
                      int alwaysIndex,
                      WavePreviewBlock* block,
                      QList<WavePreviewAssignment>* assignments,
                      int* consumedIndex)
{
    if (!block || !assignments || alwaysIndex < 0 || alwaysIndex >= tokens.size())
        return false;

    const int beginIndex =
        firstBeginForAlways(tokens, alwaysIndex, tokens.size() - 1);
    int endIndex = -1;
    int assignmentStart = alwaysIndex + 1;
    if (beginIndex >= 0) {
        endIndex = matchingBeginEnd(tokens, beginIndex, tokens.size() - 1);
        assignmentStart = beginIndex + 1;
    } else {
        endIndex = nextSemicolon(tokens, alwaysIndex + 1, tokens.size() - 1);
    }
    if (endIndex < 0)
        return false;

    block->kind = kindForAlways(tokens, alwaysIndex, endIndex);
    block->trigger = triggerTextForAlways(text, tokens, alwaysIndex, endIndex);
    block->startLine = tokens.at(alwaysIndex).line;
    block->endLine = tokens.at(endIndex).line;
    block->startPosition = tokens.at(alwaysIndex).start;
    block->endPosition = tokens.at(endIndex).end;

    const int blockIndex = -1;
    *assignments = assignmentsInRange(text,
                                      tokens,
                                      assignmentStart,
                                      endIndex,
                                      blockIndex,
                                      *block);
    block->assignmentCount = assignments->size();
    if (consumedIndex)
        *consumedIndex = endIndex;
    return true;
}

void appendLaneAssignment(QList<WavePreviewLane>* lanes,
                          QHash<QString, int>* laneIndexes,
                          const WavePreviewAssignment& assignment)
{
    if (!lanes || !laneIndexes || !assignment.isValid())
        return;
    int laneIndex = laneIndexes->value(assignment.target, -1);
    if (laneIndex < 0) {
        WavePreviewLane lane;
        lane.signalName = assignment.target;
        laneIndex = lanes->size();
        laneIndexes->insert(assignment.target, laneIndex);
        lanes->append(lane);
    }
    (*lanes)[laneIndex].assignments.append(assignment);
}

void fixBlockIndexes(QList<WavePreviewAssignment>* assignments, int blockIndex)
{
    if (!assignments)
        return;
    for (WavePreviewAssignment& assignment : *assignments)
        assignment.blockIndex = blockIndex;
}
}

WavePreviewService* WavePreviewService::getInstance()
{
    if (!instance)
        instance = std::make_unique<WavePreviewService>();
    return instance.get();
}

WavePreviewReport WavePreviewService::previewForDocument(
    const WavePreviewQuery& query) const
{
    WavePreviewReport report;
    if (query.documentText.trimmed().isEmpty())
        return report;

    const QList<Token> tokens = tokenize(query.documentText);
    QHash<QString, int> laneIndexes;
    int pos = 0;
    while (pos < tokens.size()) {
        const Token& token = tokens.at(pos);
        if (isIdentifierToken(token, QStringLiteral("assign"))) {
            WavePreviewAssignment assignment;
            if (parseContinuousAssign(query.documentText, tokens, pos, &assignment)) {
                appendLaneAssignment(&report.lanes, &laneIndexes, assignment);
                ++report.assignmentCount;
            }
            ++pos;
            continue;
        }

        if (isAlwaysToken(token)) {
            WavePreviewBlock block;
            QList<WavePreviewAssignment> assignments;
            int consumedIndex = pos;
            if (parseAlwaysBlock(query.documentText,
                                 tokens,
                                 pos,
                                 &block,
                                 &assignments,
                                 &consumedIndex)) {
                const int blockIndex = report.blocks.size();
                fixBlockIndexes(&assignments, blockIndex);
                block.assignmentCount = assignments.size();
                report.blocks.append(block);
                for (const WavePreviewAssignment& assignment : assignments) {
                    appendLaneAssignment(&report.lanes, &laneIndexes, assignment);
                    ++report.assignmentCount;
                }
                pos = qMax(pos + 1, consumedIndex + 1);
                continue;
            }
        }

        ++pos;
    }

    report.lanes.erase(
        std::remove_if(report.lanes.begin(),
                       report.lanes.end(),
                       [](const WavePreviewLane& lane) {
                           return !lane.isValid();
                       }),
        report.lanes.end());
    report.available = report.assignmentCount > 0;
    return report;
}
