#include "wavepreviewservice.h"

#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

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
        QStringLiteral("while"),
        QStringLiteral("wire")
    };
    return keywords.contains(text);
}

void appendUnique(QStringList* values, const QString& value)
{
    if (!values || value.isEmpty() || values->contains(value))
        return;
    values->append(value);
}

void appendUniqueEdgeSignal(QList<WavePreviewEdgeSignal>* values,
                            const QString& signalName,
                            const QString& edge)
{
    if (!values || signalName.isEmpty())
        return;
    for (const WavePreviewEdgeSignal& value : *values) {
        if (value.signalName == signalName && value.edge == edge)
            return;
    }
    WavePreviewEdgeSignal next;
    next.signalName = signalName;
    next.edge = edge;
    values->append(next);
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

QString dottedSignalNameAt(const QList<Token>& tokens,
                           int start,
                           int limit,
                           int* consumedIndex)
{
    if (start < 0 || start >= tokens.size() || start > limit)
        return QString();
    if (tokens.at(start).kind != TokenKind::Identifier
        || tokens.at(start).text.startsWith(QLatin1Char('$'))) {
        return QString();
    }

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

    if (consumedIndex)
        *consumedIndex = pos - 1;
    return name;
}

bool looksLikeResetSignal(const QString& name)
{
    const QString lowered = name.toLower();
    return lowered.contains(QStringLiteral("rst"))
        || lowered.contains(QStringLiteral("reset"));
}

void extractClockResetSignals(const QList<Token>& tokens,
                              int start,
                              int end,
                              QStringList* clockSignals,
                              QStringList* resetSignals,
                              QList<WavePreviewEdgeSignal>* clockEdgeSignals,
                              QList<WavePreviewEdgeSignal>* resetEdgeSignals)
{
    QStringList edgeSignals;
    QList<WavePreviewEdgeSignal> allEdges;
    const int limit = qMin(end, tokens.size() - 1);
    for (int i = qMax(0, start); i <= limit; ++i) {
        const QString edge = tokens.at(i).text;
        if (edge != QStringLiteral("posedge")
            && edge != QStringLiteral("negedge")) {
            continue;
        }

        int consumedIndex = i;
        const QString signal =
            dottedSignalNameAt(tokens, i + 1, limit, &consumedIndex);
        if (signal.isEmpty())
            continue;

        appendUnique(&edgeSignals, signal);
        appendUniqueEdgeSignal(&allEdges, signal, edge);
        if (looksLikeResetSignal(signal)) {
            appendUnique(resetSignals, signal);
            appendUniqueEdgeSignal(resetEdgeSignals, signal, edge);
        } else {
            appendUnique(clockSignals, signal);
            appendUniqueEdgeSignal(clockEdgeSignals, signal, edge);
        }
        i = qMax(i, consumedIndex);
    }

    if (clockSignals && clockSignals->isEmpty()
        && resetSignals && resetSignals->isEmpty()
        && !edgeSignals.isEmpty()) {
        appendUnique(clockSignals, edgeSignals.first());
        if (!allEdges.isEmpty()) {
            appendUniqueEdgeSignal(clockEdgeSignals,
                                   allEdges.first().signalName,
                                   allEdges.first().edge);
        }
    }
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

bool isDirectionTokenText(const QString& text)
{
    return text == QStringLiteral("input")
        || text == QStringLiteral("output")
        || text == QStringLiteral("inout");
}

bool isSignalDeclarationTypeTokenText(const QString& text)
{
    return text == QStringLiteral("logic")
        || text == QStringLiteral("wire")
        || text == QStringLiteral("reg")
        || text == QStringLiteral("bit")
        || text == QStringLiteral("byte")
        || text == QStringLiteral("shortint")
        || text == QStringLiteral("int")
        || text == QStringLiteral("longint")
        || text == QStringLiteral("integer");
}

bool isDeclarationModifierTokenText(const QString& text)
{
    return text == QStringLiteral("signed")
        || text == QStringLiteral("unsigned")
        || text == QStringLiteral("var");
}

bool isSkippedDeclarationRegion(const QList<Token>& tokens, int index)
{
    for (int i = index - 1; i >= 0; --i) {
        const QString text = tokens.at(i).text;
        if (text == QLatin1String(";") || text == QLatin1String(",")
            || text == QLatin1String("(") || text == QLatin1String(")")) {
            return false;
        }
        if (text == QStringLiteral("typedef")
            || text == QStringLiteral("parameter")
            || text == QStringLiteral("localparam")) {
            return true;
        }
        if (text == QStringLiteral("module")
            || text == QStringLiteral("endmodule")
            || isAlwaysToken(tokens.at(i))) {
            return false;
        }
    }
    return false;
}

int declarationEndBeforeDelimiter(const QList<Token>& tokens,
                                  int start,
                                  bool stopOnComma)
{
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    for (int i = start + 1; i < tokens.size(); ++i) {
        const QString text = tokens.at(i).text;
        const bool topLevel =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == 0;

        if (topLevel && text == QLatin1String(";"))
            return i - 1;
        if (topLevel && stopOnComma && text == QLatin1String(","))
            return i - 1;
        if (topLevel && stopOnComma && text == QLatin1String(")"))
            return i - 1;

        if (text == QLatin1String("("))
            ++parenDepth;
        else if (text == QLatin1String(")"))
            parenDepth = qMax(0, parenDepth - 1);
        else if (text == QLatin1String("["))
            ++bracketDepth;
        else if (text == QLatin1String("]"))
            bracketDepth = qMax(0, bracketDepth - 1);
        else if (text == QLatin1String("{"))
            ++braceDepth;
        else if (text == QLatin1String("}"))
            braceDepth = qMax(0, braceDepth - 1);
    }
    return -1;
}

int topLevelAssignmentIndex(const QList<Token>& tokens, int start, int end)
{
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    for (int i = start; i <= end && i < tokens.size(); ++i) {
        const QString text = tokens.at(i).text;
        const bool topLevel =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == 0;
        if (topLevel && text == QLatin1String("="))
            return i;

        if (text == QLatin1String("("))
            ++parenDepth;
        else if (text == QLatin1String(")"))
            parenDepth = qMax(0, parenDepth - 1);
        else if (text == QLatin1String("["))
            ++bracketDepth;
        else if (text == QLatin1String("]"))
            bracketDepth = qMax(0, bracketDepth - 1);
        else if (text == QLatin1String("{"))
            ++braceDepth;
        else if (text == QLatin1String("}"))
            braceDepth = qMax(0, braceDepth - 1);
    }
    return -1;
}

int lastTopLevelSignalNameIndex(const QList<Token>& tokens, int start, int end)
{
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    int candidate = -1;
    const int assignmentIndex = topLevelAssignmentIndex(tokens, start, end);
    const int limit = assignmentIndex >= 0 ? assignmentIndex - 1 : end;
    for (int i = start; i <= limit && i < tokens.size(); ++i) {
        const QString text = tokens.at(i).text;
        const bool topLevel =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == 0;
        if (topLevel && tokens.at(i).kind == TokenKind::Identifier
            && !tokens.at(i).text.startsWith(QLatin1Char('$'))
            && !isKeyword(tokens.at(i).text)
            && !isDeclarationModifierTokenText(tokens.at(i).text)) {
            candidate = i;
        }

        if (text == QLatin1String("("))
            ++parenDepth;
        else if (text == QLatin1String(")"))
            parenDepth = qMax(0, parenDepth - 1);
        else if (text == QLatin1String("["))
            ++bracketDepth;
        else if (text == QLatin1String("]"))
            bracketDepth = qMax(0, bracketDepth - 1);
        else if (text == QLatin1String("{"))
            ++braceDepth;
        else if (text == QLatin1String("}"))
            braceDepth = qMax(0, braceDepth - 1);
    }
    return candidate;
}

struct SignalContextCollection {
    QList<WavePreviewSignalContext> contexts;
    QHash<QString, int> indexes;
};

bool isPortDirectionText(const QString& text)
{
    return text == QStringLiteral("input")
        || text == QStringLiteral("output")
        || text == QStringLiteral("inout");
}

void upsertSignalContext(SignalContextCollection* collection,
                         const WavePreviewSignalContext& context)
{
    if (!collection || !context.isValid())
        return;

    const int existingIndex =
        collection->indexes.value(context.signalName, -1);
    if (existingIndex < 0) {
        collection->indexes.insert(context.signalName,
                                   collection->contexts.size());
        collection->contexts.append(context);
        return;
    }

    WavePreviewSignalContext& existing =
        collection->contexts[existingIndex];
    if ((existing.direction.isEmpty()
         || (existing.direction == QStringLiteral("internal")
             && isPortDirectionText(context.direction)))
        && !context.direction.isEmpty()) {
        existing.direction = context.direction;
    }
    if (existing.typeText.isEmpty() && !context.typeText.isEmpty())
        existing.typeText = context.typeText;
    if (existing.declarationText.isEmpty()
        || existing.declarationText == existing.signalName) {
        existing.declarationText = context.declarationText;
    }
    if (existing.line <= 0 && context.line > 0) {
        existing.line = context.line;
        existing.column = context.column;
    }
}

QString declarationTextForContext(const WavePreviewSignalContext& context)
{
    QString text = context.typeText;
    if (text.isEmpty()) {
        text = context.signalName;
    } else {
        text += QLatin1Char(' ');
        text += context.signalName;
    }
    return text.simplified();
}

void appendDeclarationContext(const QString& text,
                              const QList<Token>& tokens,
                              int segmentStart,
                              int segmentEnd,
                              const QString& direction,
                              int typeStart,
                              const QString& fallbackTypeText,
                              SignalContextCollection* collection,
                              QString* commonTypeText)
{
    if (!collection || segmentStart < 0 || segmentEnd < segmentStart)
        return;

    const int nameIndex =
        lastTopLevelSignalNameIndex(tokens, segmentStart, segmentEnd);
    if (nameIndex < 0)
        return;

    QString typeText;
    if (typeStart >= 0 && typeStart <= nameIndex - 1) {
        typeText = sourceTextBetween(text,
                                     tokens,
                                     typeStart,
                                     nameIndex - 1);
    }
    typeText = typeText.trimmed();
    if (typeText.isEmpty())
        typeText = fallbackTypeText;
    if (commonTypeText && commonTypeText->isEmpty())
        *commonTypeText = typeText;

    WavePreviewSignalContext context;
    context.signalName = tokens.at(nameIndex).text;
    context.direction = direction;
    context.typeText = typeText;
    context.line = tokens.at(nameIndex).line;
    context.column = tokens.at(nameIndex).column;
    context.declarationText = declarationTextForContext(context);
    upsertSignalContext(collection, context);
}

QList<QPair<int, int>> topLevelCommaSegments(const QList<Token>& tokens,
                                             int start,
                                             int end)
{
    QList<QPair<int, int>> segments;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    int segmentStart = start;
    for (int i = start; i <= end && i < tokens.size(); ++i) {
        const QString text = tokens.at(i).text;
        const bool topLevel =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == 0;
        if (topLevel && text == QLatin1String(",")) {
            if (i > segmentStart)
                segments.append({segmentStart, i - 1});
            segmentStart = i + 1;
            continue;
        }

        if (text == QLatin1String("("))
            ++parenDepth;
        else if (text == QLatin1String(")"))
            parenDepth = qMax(0, parenDepth - 1);
        else if (text == QLatin1String("["))
            ++bracketDepth;
        else if (text == QLatin1String("]"))
            bracketDepth = qMax(0, bracketDepth - 1);
        else if (text == QLatin1String("{"))
            ++braceDepth;
        else if (text == QLatin1String("}"))
            braceDepth = qMax(0, braceDepth - 1);
    }
    if (segmentStart <= end)
        segments.append({segmentStart, end});
    return segments;
}

void appendDeclarationContexts(const QString& text,
                               const QList<Token>& tokens,
                               int start,
                               int end,
                               const QString& direction,
                               int typeStart,
                               SignalContextCollection* collection)
{
    QString commonTypeText;
    const QList<QPair<int, int>> segments =
        topLevelCommaSegments(tokens, start, end);
    for (int i = 0; i < segments.size(); ++i) {
        const QPair<int, int>& segment = segments.at(i);
        appendDeclarationContext(text,
                                 tokens,
                                 segment.first,
                                 segment.second,
                                 direction,
                                 i == 0 ? typeStart : -1,
                                 commonTypeText,
                                 collection,
                                 &commonTypeText);
    }
}

SignalContextCollection collectSignalContexts(const QString& text,
                                              const QList<Token>& tokens)
{
    SignalContextCollection collection;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).kind != TokenKind::Identifier)
            continue;
        if (isSkippedDeclarationRegion(tokens, i))
            continue;

        if (isDirectionTokenText(tokens.at(i).text)) {
            const int end = declarationEndBeforeDelimiter(tokens, i, true);
            if (end >= i + 1) {
                appendDeclarationContexts(text,
                                          tokens,
                                          i,
                                          end,
                                          tokens.at(i).text,
                                          i + 1,
                                          &collection);
                i = qMax(i, end);
            }
            continue;
        }

        if (isSignalDeclarationTypeTokenText(tokens.at(i).text)) {
            const int end = declarationEndBeforeDelimiter(tokens, i, false);
            if (end >= i + 1) {
                appendDeclarationContexts(text,
                                          tokens,
                                          i,
                                          end,
                                          QStringLiteral("internal"),
                                          i,
                                          &collection);
                i = qMax(i, end);
            }
        }
    }
    return collection;
}

QString semanticDirectionForRecord(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (record.collectorKind) {
    case CollectorKind::PortInput:
        return QStringLiteral("input");
    case CollectorKind::PortOutput:
        return QStringLiteral("output");
    case CollectorKind::PortInout:
        return QStringLiteral("inout");
    default:
        break;
    }

    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    if (SymbolTaxonomy::isSignalDeclaration(metadata))
        return QStringLiteral("internal");
    return QString();
}

QString semanticTypeTextForRecord(const SemanticSymbolRecord& record)
{
    if (!record.type.rawTypeText.isEmpty())
        return record.type.rawTypeText;
    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;
    return SymbolTaxonomy::symbolTypeLabel(semanticMetadataForSymbolRecord(record));
}

bool isWavePreviewSemanticContextCandidate(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    return SymbolTaxonomy::isPortDeclaration(metadata)
        || SymbolTaxonomy::isSignalDeclaration(metadata);
}

WavePreviewSignalContext semanticContextForRecord(
    const SemanticSymbolRecord& record)
{
    WavePreviewSignalContext context;
    if (!record.isValid() || !isWavePreviewSemanticContextCandidate(record))
        return context;

    context.signalName = record.name;
    context.direction = semanticDirectionForRecord(record);
    context.typeText = semanticTypeTextForRecord(record);
    context.line = record.location.startLine;
    context.column = record.location.startColumn;
    context.declarationText = declarationTextForContext(context);
    return context;
}

QStringList referencedSignalNames(const WavePreviewReport& report)
{
    QStringList names;
    for (const WavePreviewLane& lane : report.lanes) {
        appendUnique(&names, lane.signalName);
        for (const WavePreviewAssignment& assignment : lane.assignments) {
            appendUnique(&names, assignment.target);
            for (const QString& source : assignment.sourceSignals)
                appendUnique(&names, source);
        }
    }
    return names;
}

void enrichSignalContextsFromSnapshot(
    const WavePreviewQuery& query,
    const WavePreviewReport& report,
    SignalContextCollection* collection)
{
    if (!collection || !query.semanticSnapshot)
        return;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    for (const QString& name : referencedSignalNames(report)) {
        if (name.isEmpty())
            continue;
        const QList<SemanticSymbolRecord> candidates =
            query.semanticSnapshot->findDefinitionRecords(name, context);
        for (const SemanticSymbolRecord& candidate : candidates) {
            const WavePreviewSignalContext semanticContext =
                semanticContextForRecord(candidate);
            if (!semanticContext.isValid())
                continue;
            upsertSignalContext(collection, semanticContext);
            break;
        }
    }
}

void applySignalContextsToReport(const SignalContextCollection& collection,
                                 WavePreviewReport* report)
{
    if (!report)
        return;
    QHash<QString, WavePreviewSignalContext> contextsByName;
    for (const WavePreviewSignalContext& context : collection.contexts)
        contextsByName.insert(context.signalName, context);

    report->signalContexts = collection.contexts;
    for (WavePreviewLane& lane : report->lanes) {
        const WavePreviewSignalContext context =
            contextsByName.value(lane.signalName);
        if (context.isValid())
            lane.context = context;
    }
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

bool isLoopGuardToken(const Token& token)
{
    if (token.kind != TokenKind::Identifier)
        return false;
    return token.text == QStringLiteral("for")
        || token.text == QStringLiteral("foreach")
        || token.text == QStringLiteral("while")
        || token.text == QStringLiteral("repeat");
}

QString loopHeaderTextForToken(const QString& text,
                               const QList<Token>& tokens,
                               int loopIndex,
                               int limit,
                               int* headerEnd)
{
    if (loopIndex < 0 || loopIndex >= tokens.size()
        || !isLoopGuardToken(tokens.at(loopIndex))) {
        return QString();
    }

    int openIndex = -1;
    for (int i = loopIndex + 1; i <= limit && i < tokens.size(); ++i) {
        if (tokens.at(i).text == QLatin1String("(")) {
            openIndex = i;
            break;
        }
        if (tokens.at(i).text == QLatin1String(";")
            || isIdentifierToken(tokens.at(i), QStringLiteral("begin"))) {
            return QString();
        }
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

    if (headerEnd)
        *headerEnd = closeIndex;

    const QString header =
        sourceTextBetween(text, tokens, openIndex + 1, closeIndex - 1);
    return header.isEmpty()
        ? tokens.at(loopIndex).text
        : QStringLiteral("%1 %2").arg(tokens.at(loopIndex).text, header);
}

QString loopGuardTextForAssignment(const QString& text,
                                   const QList<Token>& tokens,
                                   int from,
                                   int assignmentIndex,
                                   int limit)
{
    QStringList guards;
    const int start = qMax(0, from);
    const int end = qMin(assignmentIndex - 1, tokens.size() - 1);
    for (int i = start; i <= end; ++i) {
        if (!isLoopGuardToken(tokens.at(i)))
            continue;

        int headerEnd = -1;
        const QString header =
            loopHeaderTextForToken(text, tokens, i, limit, &headerEnd);
        if (header.isEmpty() || headerEnd < 0)
            continue;

        int bodyStart = -1;
        int bodyEnd = -1;
        if (!statementBodyRange(tokens,
                                headerEnd + 1,
                                limit,
                                &bodyStart,
                                &bodyEnd)) {
            continue;
        }
        if (assignmentIndex >= bodyStart && assignmentIndex <= bodyEnd)
            guards.append(header);
    }

    guards.removeDuplicates();
    return guards.join(QStringLiteral(" && "));
}

QString ternaryGuardTextForExpression(const QString& text,
                                      const QList<Token>& tokens,
                                      int from,
                                      int to)
{
    const int start = qMax(0, from);
    const int end = qMin(to, tokens.size() - 1);
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    int questionIndex = -1;
    int nestedTernaryDepth = 0;
    bool matchedColon = false;

    for (int i = start; i <= end; ++i) {
        const QString tokenText = tokens.at(i).text;
        const bool topLevel =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == 0;

        if (topLevel && tokenText == QLatin1String("?")) {
            if (questionIndex < 0) {
                questionIndex = i;
            } else {
                ++nestedTernaryDepth;
            }
        } else if (topLevel && tokenText == QLatin1String(":")) {
            if (questionIndex >= 0 && nestedTernaryDepth == 0) {
                matchedColon = true;
                break;
            }
            if (nestedTernaryDepth > 0)
                --nestedTernaryDepth;
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
    }

    if (questionIndex <= start || !matchedColon)
        return QString();

    const QString condition =
        sourceTextBetween(text, tokens, start, questionIndex - 1);
    return condition.isEmpty()
        ? QString()
        : QStringLiteral("?: %1").arg(condition);
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
    const QString loopGuard =
        loopGuardTextForAssignment(text, tokens, from, assignmentIndex, limit);
    if (!loopGuard.isEmpty())
        guards.append(loopGuard);
    guards.removeDuplicates();
    return guards.join(QStringLiteral(" && "));
}

QString combinedGuardText(const QStringList& guards)
{
    QStringList values;
    for (const QString& guard : guards) {
        if (!guard.isEmpty() && !values.contains(guard))
            values.append(guard);
    }
    return values.join(QStringLiteral(" && "));
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
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    while (pos <= end) {
        const QString tokenText = tokens.at(pos).text;
        const bool topLevel =
            parenDepth == 0 && bracketDepth == 0 && braceDepth == 0;
        QString target;
        int operatorIndex = -1;
        if (!topLevel
            || !parseLvalueAt(tokens, pos, end, &target, &operatorIndex)) {
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
            combinedGuardText({
                guardTextForAssignment(text, tokens, from, pos, end),
                ternaryGuardTextForExpression(text,
                                              tokens,
                                              operatorIndex + 1,
                                              semicolonIndex - 1)
            });
        if (assignment.isValid())
            assignments.append(assignment);
        pos = semicolonIndex + 1;
        parenDepth = 0;
        bracketDepth = 0;
        braceDepth = 0;
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
    assignment->guardText =
        ternaryGuardTextForExpression(text,
                                      tokens,
                                      operatorIndex + 1,
                                      semicolonIndex - 1);
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
    extractClockResetSignals(tokens,
                             alwaysIndex,
                             endIndex,
                             &block->clockSignals,
                             &block->resetSignals,
                             &block->clockEdgeSignals,
                             &block->resetEdgeSignals);
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
                          const QHash<QString, WavePreviewSignalContext>* contexts,
                          const WavePreviewAssignment& assignment)
{
    if (!lanes || !laneIndexes || !assignment.isValid())
        return;
    int laneIndex = laneIndexes->value(assignment.target, -1);
    if (laneIndex < 0) {
        WavePreviewLane lane;
        lane.signalName = assignment.target;
        if (contexts)
            lane.context = contexts->value(assignment.target);
        laneIndex = lanes->size();
        laneIndexes->insert(assignment.target, laneIndex);
        lanes->append(lane);
    }
    (*lanes)[laneIndex].assignments.append(assignment);
}

WavePreviewLaneSummary summaryForLane(const WavePreviewLane& lane)
{
    WavePreviewLaneSummary summary;
    summary.eventCount = lane.assignments.size();

    QSet<QString> sourceSignals;
    QSet<int> blockIndexes;
    for (const WavePreviewAssignment& assignment : lane.assignments) {
        summary.maxCycleOffset =
            std::max(summary.maxCycleOffset, assignment.cycleOffset);
        for (const QString& source : assignment.sourceSignals) {
            if (!source.isEmpty())
                sourceSignals.insert(source);
        }
        if (assignment.blockIndex >= 0)
            blockIndexes.insert(assignment.blockIndex);
        if (!assignment.guardText.isEmpty()
            && !summary.guardTexts.contains(assignment.guardText)) {
            summary.guardTexts.append(assignment.guardText);
        }

        if (assignment.kind == WavePreviewAssignmentKind::Continuous) {
            ++summary.continuousEventCount;
            summary.hasContinuousEvent = true;
        } else if (assignment.kind == WavePreviewAssignmentKind::NonBlocking) {
            ++summary.sequentialEventCount;
            summary.hasSequentialEvent = true;
        } else {
            ++summary.combinationalEventCount;
            summary.hasCombinationalEvent = true;
        }
    }

    summary.sourceSignalCount = sourceSignals.size();
    summary.blockCount = blockIndexes.size();
    return summary;
}

void includeActivityAssignment(WavePreviewActivitySummary* summary,
                               const WavePreviewAssignment& assignment)
{
    if (!summary)
        return;

    ++summary->eventCount;
    if (assignment.kind == WavePreviewAssignmentKind::Continuous) {
        ++summary->continuousEventCount;
        summary->hasContinuousEvent = true;
    } else if (assignment.kind == WavePreviewAssignmentKind::NonBlocking) {
        ++summary->sequentialEventCount;
        summary->hasSequentialEvent = true;
    } else {
        ++summary->combinationalEventCount;
        summary->hasCombinationalEvent = true;
    }
}

WavePreviewActivitySummary activitySummaryForLanes(
    const QList<WavePreviewLane>& lanes)
{
    WavePreviewActivitySummary summary;
    for (const WavePreviewLane& lane : lanes) {
        for (const WavePreviewAssignment& assignment : lane.assignments)
            includeActivityAssignment(&summary, assignment);
    }
    return summary;
}

QStringList activityKindsForLane(const WavePreviewLaneSummary& summary)
{
    QStringList parts;
    if (summary.hasContinuousEvent)
        parts.append(QStringLiteral("assign"));
    if (summary.hasCombinationalEvent)
        parts.append(QStringLiteral("comb"));
    if (summary.hasSequentialEvent)
        parts.append(QStringLiteral("seq"));
    return parts;
}

void appendLaneWarning(WavePreviewLane* lane,
                       QStringList* warnings,
                       const QString& warning)
{
    if (warning.isEmpty())
        return;
    if (warnings && !warnings->contains(warning))
        warnings->append(warning);
    if (lane && !lane->summary.warningTexts.contains(warning))
        lane->summary.warningTexts.append(warning);
}

QStringList applyWarningTextsForLanes(QList<WavePreviewLane>* lanes)
{
    QStringList warnings;
    if (!lanes)
        return warnings;

    for (WavePreviewLane& lane : *lanes) {
        if (!lane.summary.isValid())
            continue;

        const QStringList activityKinds =
            activityKindsForLane(lane.summary);
        if (activityKinds.size() > 1) {
            appendLaneWarning(
                &lane,
                &warnings,
                QStringLiteral(
                    "signal %1 mixes %2 activity; Wave Preview does not resolve writer priority")
                    .arg(lane.signalName,
                         activityKinds.join(QStringLiteral("/"))));
        }

        if (lane.summary.blockCount > 1) {
            appendLaneWarning(
                &lane,
                &warnings,
                QStringLiteral(
                    "signal %1 is assigned from %2 procedural blocks; inspect block ownership before trusting lane timing")
                    .arg(lane.signalName)
                    .arg(lane.summary.blockCount));
        }
    }
    return warnings;
}

void refreshLaneSummaries(QList<WavePreviewLane>* lanes)
{
    if (!lanes)
        return;
    for (WavePreviewLane& lane : *lanes)
        lane.summary = summaryForLane(lane);
}

void fixBlockIndexes(QList<WavePreviewAssignment>* assignments, int blockIndex)
{
    if (!assignments)
        return;
    for (WavePreviewAssignment& assignment : *assignments)
        assignment.blockIndex = blockIndex;
}

struct TraceValue {
    qint64 value = 0;
    int width = 1;
    bool unknown = true;

    static TraceValue known(qint64 nextValue, int nextWidth = 1)
    {
        TraceValue result;
        result.value = nextValue;
        result.width = qMax(1, nextWidth);
        result.unknown = false;
        return result.masked();
    }

    static TraceValue x(int nextWidth = 1)
    {
        TraceValue result;
        result.width = qMax(1, nextWidth);
        result.unknown = true;
        return result;
    }

    TraceValue masked() const
    {
        TraceValue result = *this;
        result.width = qMax(1, result.width);
        if (!result.unknown && result.width > 0 && result.width < 63) {
            const qint64 mask = (qint64(1) << result.width) - 1;
            result.value &= mask;
        }
        return result;
    }

    bool truthy(bool* knownTruth = nullptr) const
    {
        if (unknown) {
            if (knownTruth)
                *knownTruth = false;
            return false;
        }
        if (knownTruth)
            *knownTruth = true;
        return value != 0;
    }

    QString text() const
    {
        if (unknown)
            return QStringLiteral("x");
        return QString::number(value);
    }
};

struct TraceEvalContext {
    QHash<QString, TraceValue> values;
    QHash<QString, TraceValue> parameters;
    QHash<QString, int> widths;
    QStringList warnings;
};

int widthFromTypeText(const QString& typeText)
{
    const int left = typeText.indexOf(QLatin1Char('['));
    const int colon = typeText.indexOf(QLatin1Char(':'), left + 1);
    const int right = typeText.indexOf(QLatin1Char(']'), colon + 1);
    if (left < 0 || colon < 0 || right < 0)
        return 1;

    bool okA = false;
    bool okB = false;
    const int a = typeText.mid(left + 1, colon - left - 1).trimmed().toInt(&okA);
    const int b = typeText.mid(colon + 1, right - colon - 1).trimmed().toInt(&okB);
    if (!okA || !okB)
        return 1;
    return qMax(1, qAbs(a - b) + 1);
}

qint64 parseBasedDigits(QString digits, int base, bool* ok)
{
    digits.remove(QLatin1Char('_'));
    digits = digits.toLower();
    if (digits.isEmpty()) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (digits.contains(QLatin1Char('x')) || digits.contains(QLatin1Char('z'))
        || digits.contains(QLatin1Char('?'))) {
        if (ok)
            *ok = false;
        return 0;
    }
    bool localOk = false;
    const qint64 value = digits.toLongLong(&localOk, base);
    if (ok)
        *ok = localOk;
    return value;
}

TraceValue literalValue(const QString& rawText)
{
    QString text = rawText.trimmed();
    text.remove(QLatin1Char('_'));
    if (text.isEmpty())
        return TraceValue::x();

    const int apostrophe = text.indexOf(QLatin1Char('\''));
    if (apostrophe >= 0) {
        bool widthOk = false;
        int width = text.left(apostrophe).toInt(&widthOk);
        if (!widthOk || width <= 0)
            width = 1;

        QString rest = text.mid(apostrophe + 1);
        if (!rest.isEmpty()
            && (rest.front() == QLatin1Char('s')
                || rest.front() == QLatin1Char('S'))) {
            rest.remove(0, 1);
        }
        if (rest.isEmpty())
            return TraceValue::x(width);

        const QChar baseChar = rest.front().toLower();
        rest.remove(0, 1);
        int base = 10;
        if (baseChar == QLatin1Char('b'))
            base = 2;
        else if (baseChar == QLatin1Char('h'))
            base = 16;
        else if (baseChar == QLatin1Char('o'))
            base = 8;
        else if (baseChar != QLatin1Char('d')) {
            rest.prepend(baseChar);
            base = 10;
        }

        bool ok = false;
        const qint64 value = parseBasedDigits(rest, base, &ok);
        return ok ? TraceValue::known(value, width) : TraceValue::x(width);
    }

    bool ok = false;
    const qint64 value = text.toLongLong(&ok, 10);
    return ok ? TraceValue::known(value, 32) : TraceValue::x(32);
}

bool isUnaryOperator(const QString& text)
{
    return text == QLatin1String("!")
        || text == QLatin1String("~")
        || text == QLatin1String("+")
        || text == QLatin1String("-");
}

TraceValue mergeBinaryWidth(const TraceValue& lhs, const TraceValue& rhs)
{
    return TraceValue::x(qMax(lhs.width, rhs.width));
}

TraceValue applyUnaryOperator(const QString& op, const TraceValue& value)
{
    if (op == QLatin1String("+"))
        return value;
    if (value.unknown)
        return TraceValue::x(value.width);
    if (op == QLatin1String("-"))
        return TraceValue::known(-value.value, value.width);
    if (op == QLatin1String("!"))
        return TraceValue::known(value.value == 0 ? 1 : 0, 1);
    if (op == QLatin1String("~")) {
        const int width = qMax(1, value.width);
        if (width >= 63)
            return TraceValue::known(~value.value, width);
        const qint64 mask = (qint64(1) << width) - 1;
        return TraceValue::known((~value.value) & mask, width);
    }
    return TraceValue::x(value.width);
}

TraceValue applyBinaryOperator(const QString& op,
                               const TraceValue& lhs,
                               const TraceValue& rhs)
{
    const int width = qMax(lhs.width, rhs.width);
    if (lhs.unknown || rhs.unknown)
        return (op == QLatin1String("&&") || op == QLatin1String("||")
                || op == QLatin1String("==") || op == QLatin1String("!=")
                || op == QLatin1String(">=") || op == QLatin1String("<=")
                || op == QLatin1String(">") || op == QLatin1String("<"))
            ? TraceValue::x(1)
            : TraceValue::x(width);

    if (op == QLatin1String("+"))
        return TraceValue::known(lhs.value + rhs.value, width);
    if (op == QLatin1String("-"))
        return TraceValue::known(lhs.value - rhs.value, width);
    if (op == QLatin1String("&"))
        return TraceValue::known(lhs.value & rhs.value, width);
    if (op == QLatin1String("|"))
        return TraceValue::known(lhs.value | rhs.value, width);
    if (op == QLatin1String("^"))
        return TraceValue::known(lhs.value ^ rhs.value, width);
    if (op == QLatin1String("&&"))
        return TraceValue::known((lhs.value != 0 && rhs.value != 0) ? 1 : 0, 1);
    if (op == QLatin1String("||"))
        return TraceValue::known((lhs.value != 0 || rhs.value != 0) ? 1 : 0, 1);
    if (op == QLatin1String("=="))
        return TraceValue::known(lhs.value == rhs.value ? 1 : 0, 1);
    if (op == QLatin1String("!="))
        return TraceValue::known(lhs.value != rhs.value ? 1 : 0, 1);
    if (op == QLatin1String(">="))
        return TraceValue::known(lhs.value >= rhs.value ? 1 : 0, 1);
    if (op == QLatin1String("<="))
        return TraceValue::known(lhs.value <= rhs.value ? 1 : 0, 1);
    if (op == QLatin1String(">"))
        return TraceValue::known(lhs.value > rhs.value ? 1 : 0, 1);
    if (op == QLatin1String("<"))
        return TraceValue::known(lhs.value < rhs.value ? 1 : 0, 1);
    return mergeBinaryWidth(lhs, rhs);
}

class TraceExpressionParser
{
public:
    TraceExpressionParser(const QList<Token>& tokens,
                          int start,
                          int end,
                          const TraceEvalContext& context)
        : tokens(tokens)
        , pos(qMax(0, start))
        , end(qMin(end, tokens.size() - 1))
        , context(context)
    {
    }

    TraceValue parse()
    {
        return parseLogicalOr();
    }

private:
    const QList<Token>& tokens;
    int pos = 0;
    int end = -1;
    const TraceEvalContext& context;

    bool atEnd() const { return pos > end || pos >= tokens.size(); }
    QString currentText() const { return atEnd() ? QString() : tokens.at(pos).text; }

    TraceValue parseLogicalOr()
    {
        TraceValue value = parseLogicalAnd();
        while (!atEnd() && currentText() == QLatin1String("||")) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseLogicalAnd());
        }
        return value;
    }

    TraceValue parseLogicalAnd()
    {
        TraceValue value = parseBitwiseOr();
        while (!atEnd() && currentText() == QLatin1String("&&")) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseBitwiseOr());
        }
        return value;
    }

    TraceValue parseBitwiseOr()
    {
        TraceValue value = parseBitwiseXor();
        while (!atEnd() && currentText() == QLatin1String("|")) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseBitwiseXor());
        }
        return value;
    }

    TraceValue parseBitwiseXor()
    {
        TraceValue value = parseBitwiseAnd();
        while (!atEnd() && currentText() == QLatin1String("^")) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseBitwiseAnd());
        }
        return value;
    }

    TraceValue parseBitwiseAnd()
    {
        TraceValue value = parseEquality();
        while (!atEnd() && currentText() == QLatin1String("&")) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseEquality());
        }
        return value;
    }

    TraceValue parseEquality()
    {
        TraceValue value = parseRelational();
        while (!atEnd()
               && (currentText() == QLatin1String("==")
                   || currentText() == QLatin1String("!="))) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseRelational());
        }
        return value;
    }

    TraceValue parseRelational()
    {
        TraceValue value = parseAdditive();
        while (!atEnd()
               && (currentText() == QLatin1String(">=")
                   || currentText() == QLatin1String("<=")
                   || currentText() == QLatin1String(">")
                   || currentText() == QLatin1String("<"))) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseAdditive());
        }
        return value;
    }

    TraceValue parseAdditive()
    {
        TraceValue value = parseUnary();
        while (!atEnd()
               && (currentText() == QLatin1String("+")
                   || currentText() == QLatin1String("-"))) {
            const QString op = currentText();
            ++pos;
            value = applyBinaryOperator(op, value, parseUnary());
        }
        return value;
    }

    TraceValue parseUnary()
    {
        if (!atEnd() && isUnaryOperator(currentText())) {
            const QString op = currentText();
            ++pos;
            return applyUnaryOperator(op, parseUnary());
        }
        return parsePrimary();
    }

    TraceValue parsePrimary()
    {
        if (atEnd())
            return TraceValue::x();

        const Token token = tokens.at(pos);
        if (token.text == QLatin1String("(")) {
            ++pos;
            TraceValue value = parseLogicalOr();
            if (!atEnd() && currentText() == QLatin1String(")"))
                ++pos;
            return value;
        }

        if (token.kind == TokenKind::Number) {
            ++pos;
            return literalValue(token.text);
        }

        if (token.text == QLatin1String("'") && pos + 1 <= end) {
            const QString literal =
                token.text + tokens.at(pos + 1).text;
            pos += 2;
            return literalValue(literal);
        }

        if (token.kind == TokenKind::Identifier) {
            QString name = token.text;
            ++pos;
            if (!atEnd() && currentText() == QLatin1String("[")) {
                const int close = matchingSymbol(tokens,
                                                 pos,
                                                 QStringLiteral("["),
                                                 QStringLiteral("]"),
                                                 end);
                if (close >= 0)
                    pos = close + 1;
            }
            if (context.values.contains(name))
                return context.values.value(name);
            if (context.parameters.contains(name))
                return context.parameters.value(name);
            return TraceValue::x(context.widths.value(name, 1));
        }

        ++pos;
        return TraceValue::x();
    }
};

TraceValue evaluateTraceExpression(const QList<Token>& tokens,
                                   int start,
                                   int end,
                                   const TraceEvalContext& context)
{
    if (start < 0 || end < start || start >= tokens.size())
        return TraceValue::x();
    TraceExpressionParser parser(tokens, start, end, context);
    return parser.parse();
}

struct TraceAlwaysRange {
    WavePreviewBlock block;
    int startToken = -1;
    int bodyStart = -1;
    int bodyEnd = -1;
};

int tokenIndexForStartPosition(const QList<Token>& tokens, int startPosition)
{
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).start == startPosition)
            return i;
    }
    return -1;
}

int tokenIndexForEndPosition(const QList<Token>& tokens, int endPosition)
{
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).end == endPosition)
            return i;
    }
    return -1;
}

TraceAlwaysRange traceRangeForBlock(const QString& text,
                                    const QList<Token>& tokens,
                                    const WavePreviewBlock& block)
{
    TraceAlwaysRange range;
    range.block = block;
    range.startToken = tokenIndexForStartPosition(tokens, block.startPosition);
    if (range.startToken < 0)
        return range;

    const int endToken = tokenIndexForEndPosition(tokens, block.endPosition);
    const int limit = endToken >= 0 ? endToken : tokens.size() - 1;
    WavePreviewBlock parsedBlock;
    QList<WavePreviewAssignment> ignoredAssignments;
    int consumedIndex = -1;
    if (!parseAlwaysBlock(text,
                          tokens,
                          range.startToken,
                          &parsedBlock,
                          &ignoredAssignments,
                          &consumedIndex)) {
        return range;
    }

    const int beginIndex = firstBeginForAlways(tokens, range.startToken, limit);
    if (beginIndex >= 0) {
        range.bodyStart = beginIndex + 1;
        range.bodyEnd = qMax(range.bodyStart - 1, consumedIndex - 1);
    } else {
        range.bodyStart = range.startToken + 1;
        range.bodyEnd = consumedIndex;
    }
    return range;
}

void collectTraceWidths(const SignalContextCollection& contexts,
                        TraceEvalContext* context)
{
    if (!context)
        return;
    for (const WavePreviewSignalContext& signal : contexts.contexts) {
        if (!signal.isValid())
            continue;
        context->widths.insert(signal.signalName,
                               widthFromTypeText(signal.typeText));
    }
}

void collectTraceParameters(const QList<Token>& tokens,
                            TraceEvalContext* context)
{
    if (!context)
        return;
    for (int i = 0; i < tokens.size(); ++i) {
        if (!isIdentifierToken(tokens.at(i), QStringLiteral("parameter"))
            && !isIdentifierToken(tokens.at(i), QStringLiteral("localparam"))) {
            continue;
        }
        const int end = declarationEndBeforeDelimiter(tokens, i, true);
        if (end < i + 1)
            continue;
        const QList<QPair<int, int>> segments =
            topLevelCommaSegments(tokens, i + 1, end);
        for (const QPair<int, int>& segment : segments) {
            const int assignmentIndex =
                topLevelAssignmentIndex(tokens, segment.first, segment.second);
            if (assignmentIndex < 0)
                continue;
            const int nameIndex =
                lastTopLevelSignalNameIndex(tokens,
                                            segment.first,
                                            assignmentIndex - 1);
            if (nameIndex < 0)
                continue;
            const TraceValue value =
                evaluateTraceExpression(tokens,
                                        assignmentIndex + 1,
                                        segment.second,
                                        *context);
            context->parameters.insert(tokens.at(nameIndex).text, value);
        }
    }
}

void collectTraceInitialValues(const QList<Token>& tokens,
                               const SignalContextCollection& contexts,
                               TraceEvalContext* context)
{
    if (!context)
        return;
    for (const WavePreviewSignalContext& signal : contexts.contexts) {
        if (!signal.isValid())
            continue;
        const int width = context->widths.value(signal.signalName, 1);
        context->values.insert(signal.signalName, TraceValue::x(width));
    }

    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).kind != TokenKind::Identifier)
            continue;
        if (!isDirectionTokenText(tokens.at(i).text)
            && !isSignalDeclarationTypeTokenText(tokens.at(i).text)) {
            continue;
        }

        const bool isPort = isDirectionTokenText(tokens.at(i).text);
        const int end = declarationEndBeforeDelimiter(tokens, i, isPort);
        if (end < i + 1)
            continue;
        const QList<QPair<int, int>> segments =
            topLevelCommaSegments(tokens, i, end);
        for (const QPair<int, int>& segment : segments) {
            const int assignmentIndex =
                topLevelAssignmentIndex(tokens, segment.first, segment.second);
            if (assignmentIndex < 0)
                continue;
            const int nameIndex =
                lastTopLevelSignalNameIndex(tokens,
                                            segment.first,
                                            assignmentIndex - 1);
            if (nameIndex < 0)
                continue;
            const QString name = tokens.at(nameIndex).text;
            const int width = context->widths.value(name, 1);
            TraceValue value =
                evaluateTraceExpression(tokens,
                                        assignmentIndex + 1,
                                        segment.second,
                                        *context);
            value.width = width;
            context->values.insert(name, value.masked());
        }
    }
}

int expressionEndBeforeSemicolon(const QList<Token>& tokens,
                                 int expressionStart,
                                 int limit)
{
    const int semicolon = nextSemicolon(tokens, expressionStart, limit);
    return semicolon > expressionStart ? semicolon - 1 : -1;
}

void executeTraceStatementRange(const QList<Token>& tokens,
                                int start,
                                int end,
                                TraceEvalContext* context,
                                QHash<QString, TraceValue>* pending);

void applyTraceAssignment(const QList<Token>& tokens,
                          int lvalueIndex,
                          int operatorIndex,
                          int semicolonIndex,
                          TraceEvalContext* context,
                          QHash<QString, TraceValue>* pending)
{
    if (!context || !pending || lvalueIndex < 0 || operatorIndex < 0
        || semicolonIndex < 0) {
        return;
    }
    const QString target = tokens.at(lvalueIndex).text;
    TraceValue value = evaluateTraceExpression(tokens,
                                               operatorIndex + 1,
                                               semicolonIndex - 1,
                                               *context);
    value.width = context->widths.value(target, value.width);
    value = value.masked();
    if (tokens.at(operatorIndex).text == QLatin1String("<=")) {
        pending->insert(target, value);
    } else {
        context->values.insert(target, value);
    }
}

int elseIndexAfterBody(const QList<Token>& tokens, int bodyEnd, int limit)
{
    const int candidate = bodyEnd + 1;
    if (candidate <= limit && candidate < tokens.size()
        && isIdentifierToken(tokens.at(candidate), QStringLiteral("else"))) {
        return candidate;
    }
    return -1;
}

void executeTraceIf(const QList<Token>& tokens,
                    int ifIndex,
                    int limit,
                    TraceEvalContext* context,
                    QHash<QString, TraceValue>* pending,
                    int* consumedIndex)
{
    int conditionEnd = -1;
    const QString ignoredText;
    const QString conditionText =
        ifConditionTextForToken(ignoredText,
                                tokens,
                                ifIndex,
                                limit,
                                &conditionEnd);
    Q_UNUSED(conditionText);
    if (conditionEnd < 0) {
        if (consumedIndex)
            *consumedIndex = ifIndex;
        return;
    }
    int bodyStart = -1;
    int bodyEnd = -1;
    if (!statementBodyRange(tokens,
                            conditionEnd + 1,
                            limit,
                            &bodyStart,
                            &bodyEnd)) {
        if (consumedIndex)
            *consumedIndex = conditionEnd;
        return;
    }

    const TraceValue condition =
        evaluateTraceExpression(tokens, ifIndex + 2, conditionEnd - 1, *context);
    bool known = false;
    const bool takeIf = condition.truthy(&known);
    const int elseIndex = elseIndexAfterBody(tokens, bodyEnd, limit);
    int consumed = bodyEnd;
    if (elseIndex >= 0) {
        int elseStart = -1;
        int elseEnd = -1;
        if (statementBodyRange(tokens,
                               elseIndex + 1,
                               limit,
                               &elseStart,
                               &elseEnd)) {
            consumed = elseEnd;
            if (known && !takeIf)
                executeTraceStatementRange(tokens,
                                           elseStart,
                                           elseEnd,
                                           context,
                                           pending);
        }
    }
    if (known && takeIf)
        executeTraceStatementRange(tokens, bodyStart, bodyEnd, context, pending);
    if (!known) {
        context->warnings.append(
            QStringLiteral("unknown branch condition near line %1")
                .arg(tokens.at(ifIndex).line));
    }
    if (consumedIndex)
        *consumedIndex = consumed;
}

void executeTraceStatementRange(const QList<Token>& tokens,
                                int start,
                                int end,
                                TraceEvalContext* context,
                                QHash<QString, TraceValue>* pending)
{
    if (!context || !pending || start < 0 || end < start)
        return;
    const int limit = qMin(end, tokens.size() - 1);
    int pos = qMax(0, start);
    while (pos <= limit) {
        if (isIdentifierToken(tokens.at(pos), QStringLiteral("if"))) {
            int consumed = pos;
            executeTraceIf(tokens, pos, limit, context, pending, &consumed);
            pos = qMax(pos + 1, consumed + 1);
            continue;
        }
        if (isIdentifierToken(tokens.at(pos), QStringLiteral("begin"))) {
            const int bodyEnd = matchingBeginEnd(tokens, pos, limit);
            if (bodyEnd > pos) {
                executeTraceStatementRange(tokens,
                                           pos + 1,
                                           bodyEnd - 1,
                                           context,
                                           pending);
                pos = bodyEnd + 1;
                continue;
            }
        }

        QString target;
        int operatorIndex = -1;
        if (parseLvalueAt(tokens, pos, limit, &target, &operatorIndex)) {
            const int semicolon =
                nextSemicolon(tokens, operatorIndex + 1, limit);
            if (semicolon > operatorIndex) {
                applyTraceAssignment(tokens,
                                     pos,
                                     operatorIndex,
                                     semicolon,
                                     context,
                                     pending);
                pos = semicolon + 1;
                continue;
            }
        }
        ++pos;
    }
}

void executeContinuousAssignments(const QList<Token>& tokens,
                                  const QList<WavePreviewLane>& lanes,
                                  TraceEvalContext* context)
{
    if (!context)
        return;
    for (const WavePreviewLane& lane : lanes) {
        for (const WavePreviewAssignment& assignment : lane.assignments) {
            if (assignment.kind != WavePreviewAssignmentKind::Continuous)
                continue;
            const int startToken =
                tokenIndexForStartPosition(tokens, assignment.startPosition);
            const int endToken =
                tokenIndexForEndPosition(tokens, assignment.endPosition);
            if (startToken < 0 || endToken < 0)
                continue;
            QString target;
            int operatorIndex = -1;
            if (!parseLvalueAt(tokens,
                               startToken,
                               endToken,
                               &target,
                               &operatorIndex)) {
                continue;
            }
            TraceValue value =
                evaluateTraceExpression(tokens,
                                        operatorIndex + 1,
                                        endToken - 1,
                                        *context);
            value.width = context->widths.value(target, value.width);
            context->values.insert(target, value.masked());
        }
    }
}

QStringList traceSignalNames(const WavePreviewReport& report)
{
    QStringList names;
    for (const WavePreviewBlock& block : report.blocks) {
        for (const QString& clock : block.clockSignals)
            appendUnique(&names, clock);
    }
    for (const WavePreviewLane& lane : report.lanes) {
        appendUnique(&names, lane.signalName);
        for (const WavePreviewAssignment& assignment : lane.assignments) {
            for (const QString& source : assignment.sourceSignals)
                appendUnique(&names, source);
        }
    }
    return names;
}

void appendTraceSamples(QHash<QString, QStringList>* samples,
                        const QStringList& names,
                        const TraceEvalContext& context,
                        int cycle)
{
    if (!samples)
        return;
    for (const QString& name : names) {
        if (name.isEmpty())
            continue;
        const TraceValue value =
            name == QStringLiteral("__cycle_clock__")
                ? TraceValue::known(cycle % 2, 1)
                : context.values.value(name,
                                       TraceValue::x(context.widths.value(name, 1)));
        (*samples)[name].append(value.text());
    }
}

WavePreviewTraceReport buildTraceReport(const QString& text,
                                        const QList<Token>& tokens,
                                        const SignalContextCollection& contexts,
                                        const WavePreviewReport& report)
{
    WavePreviewTraceReport trace;
    if (report.blocks.isEmpty() && report.lanes.isEmpty())
        return trace;

    TraceEvalContext context;
    collectTraceWidths(contexts, &context);
    collectTraceParameters(tokens, &context);
    collectTraceInitialValues(tokens, contexts, &context);

    QList<TraceAlwaysRange> sequentialBlocks;
    QList<TraceAlwaysRange> combinationalBlocks;
    for (const WavePreviewBlock& block : report.blocks) {
        const TraceAlwaysRange range = traceRangeForBlock(text, tokens, block);
        if (range.bodyStart < 0 || range.bodyEnd < range.bodyStart)
            continue;
        if (block.isClocked())
            sequentialBlocks.append(range);
        else
            combinationalBlocks.append(range);
    }

    const QStringList names = traceSignalNames(report);
    if (names.isEmpty())
        return trace;

    constexpr int kTraceCycles = 8;
    QHash<QString, QStringList> samples;
    auto settleCombinational = [&]() {
        executeContinuousAssignments(tokens, report.lanes, &context);
        for (const TraceAlwaysRange& block : combinationalBlocks) {
            QHash<QString, TraceValue> ignoredPending;
            executeTraceStatementRange(tokens,
                                       block.bodyStart,
                                       block.bodyEnd,
                                       &context,
                                       &ignoredPending);
        }
    };

    settleCombinational();
    appendTraceSamples(&samples, names, context, 0);
    for (int cycle = 1; cycle <= kTraceCycles; ++cycle) {
        QHash<QString, TraceValue> pending;
        for (const TraceAlwaysRange& block : sequentialBlocks) {
            executeTraceStatementRange(tokens,
                                       block.bodyStart,
                                       block.bodyEnd,
                                       &context,
                                       &pending);
        }
        for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
            context.values.insert(it.key(), it.value());
        settleCombinational();
        appendTraceSamples(&samples, names, context, cycle);
    }

    trace.cycleCount = kTraceCycles;
    trace.available = true;
    trace.warnings = context.warnings;
    trace.warnings.removeDuplicates();
    for (const QString& name : names) {
        WavePreviewTraceSignal signal;
        signal.signalName = name;
        signal.width = context.widths.value(name, 1);
        signal.values = samples.value(name);
        signal.clock = false;
        for (const WavePreviewBlock& block : report.blocks) {
            if (block.clockSignals.contains(name)) {
                signal.clock = true;
                break;
            }
        }
        if (signal.isValid())
            trace.traceSignals.append(signal);
    }
    trace.available = !trace.traceSignals.isEmpty();
    return trace;
}

struct ScopeRange {
    int start = -1;
    int end = -1;

    bool isValid() const
    {
        return start >= 0 && end > start;
    }
};

ScopeRange scopeRangeForQuery(const WavePreviewQuery& query)
{
    ScopeRange range;
    if (!query.hasScope())
        return range;

    const int textSize = query.documentText.size();
    range.start = std::max(0, std::min(query.scopeStartPosition, textSize));
    range.end = std::max(0, std::min(query.scopeEndPosition, textSize));
    if (range.end < range.start)
        std::swap(range.start, range.end);
    if (!range.isValid()) {
        range.start = -1;
        range.end = -1;
    }
    return range;
}

bool rangeIntersectsScope(int start, int end, const ScopeRange& scope)
{
    if (!scope.isValid())
        return true;
    return end > scope.start && start < scope.end;
}

bool assignmentMatchesScope(const WavePreviewAssignment& assignment,
                            const ScopeRange& scope)
{
    return rangeIntersectsScope(assignment.startPosition,
                                assignment.endPosition,
                                scope);
}

bool blockMatchesScope(const WavePreviewBlock& block, const ScopeRange& scope)
{
    return rangeIntersectsScope(block.startPosition, block.endPosition, scope);
}

QString lineRangeText(int startLine, int endLine)
{
    if (startLine <= 0)
        return QStringLiteral("unknown line");
    if (endLine <= startLine)
        return QStringLiteral("line %1").arg(startLine);
    return QStringLiteral("lines %1-%2").arg(startLine).arg(endLine);
}

QString noLaneWarningForBlock(const WavePreviewBlock& block)
{
    return QStringLiteral(
               "selected always block at %1 has no recognized assignment lanes; Wave Preview reports it as unsupported rather than silent empty")
        .arg(lineRangeText(block.startLine, block.endLine));
}

QString parseFailureWarningForAlways(const Token& token)
{
    return QStringLiteral(
               "unsupported always block near line %1: could not parse statement body")
        .arg(qMax(1, token.line));
}

bool queryLooksLikeProcessScope(const WavePreviewQuery& query,
                                const ScopeRange& scope)
{
    if (!scope.isValid())
        return false;

    const QString label = query.scopeLabel.toLower();
    if (label.contains(QStringLiteral("always"))
        || label.contains(QStringLiteral("process"))) {
        return true;
    }

    const QString scopedText =
        query.documentText.mid(scope.start, scope.end - scope.start).toLower();
    return scopedText.contains(QStringLiteral("always"))
        || scopedText.contains(QStringLiteral("always_ff"))
        || scopedText.contains(QStringLiteral("always_comb"))
        || scopedText.contains(QStringLiteral("always_latch"));
}

QString emptyProcessScopeWarning(const WavePreviewReport& report)
{
    return QStringLiteral(
               "selected process scope at %1 has no recognized Wave Preview process body; source may be commented out, preprocessor-only, or otherwise unsupported")
        .arg(lineRangeText(report.scopeStartLine, report.scopeEndLine));
}

int lineForPosition(const QString& text, int position)
{
    const int textSize = static_cast<int>(text.size());
    const int bounded = std::max(0, std::min(position, textSize));
    int line = 1;
    for (int i = 0; i < bounded; ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            ++line;
    }
    return line;
}

void applyScopeMetadata(const WavePreviewQuery& query,
                        const ScopeRange& scope,
                        WavePreviewReport* report)
{
    if (!report || !scope.isValid())
        return;

    report->scoped = true;
    report->scopeStartLine = lineForPosition(query.documentText, scope.start);
    report->scopeEndLine = lineForPosition(query.documentText, scope.end - 1);
    report->scopeLabel =
        query.scopeLabel.isEmpty()
            ? QStringLiteral("selected lines %1-%2")
                  .arg(report->scopeStartLine)
                  .arg(report->scopeEndLine)
            : query.scopeLabel;
}

QString edgeSignalListKey(const QList<WavePreviewEdgeSignal>& values,
                          const QStringList& fallbackSignals)
{
    if (values.isEmpty())
        return fallbackSignals.join(QChar(0x1f));
    QStringList parts;
    for (const WavePreviewEdgeSignal& value : values)
        parts.append(value.label());
    return parts.join(QChar(0x1f));
}

QString clockResetGroupKey(const WavePreviewBlock& block)
{
    return edgeSignalListKey(block.clockEdgeSignals, block.clockSignals)
        + QStringLiteral("|")
        + edgeSignalListKey(block.resetEdgeSignals, block.resetSignals);
}

QList<WavePreviewClockResetGroup> clockResetGroupsForBlocks(
    const QList<WavePreviewBlock>& blocks)
{
    QList<WavePreviewClockResetGroup> groups;
    QHash<QString, int> groupIndexes;
    for (int i = 0; i < blocks.size(); ++i) {
        const WavePreviewBlock& block = blocks.at(i);
        if (block.clockSignals.isEmpty() && block.resetSignals.isEmpty())
            continue;

        const QString key = clockResetGroupKey(block);
        int groupIndex = groupIndexes.value(key, -1);
        if (groupIndex < 0) {
            WavePreviewClockResetGroup group;
            group.clockSignals = block.clockSignals;
            group.resetSignals = block.resetSignals;
            group.clockEdgeSignals = block.clockEdgeSignals;
            group.resetEdgeSignals = block.resetEdgeSignals;
            groupIndex = groups.size();
            groupIndexes.insert(key, groupIndex);
            groups.append(group);
        }

        groups[groupIndex].blockIndexes.append(i);
        groups[groupIndex].assignmentCount += block.assignmentCount;
    }

    groups.erase(
        std::remove_if(groups.begin(),
                       groups.end(),
                       [](const WavePreviewClockResetGroup& group) {
                           return !group.isValid();
                       }),
        groups.end());
    return groups;
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
    const ScopeRange scope = scopeRangeForQuery(query);
    applyScopeMetadata(query, scope, &report);
    if (query.documentText.trimmed().isEmpty())
        return report;

    const QList<Token> tokens = tokenize(query.documentText);
    SignalContextCollection signalContextCollection =
        collectSignalContexts(query.documentText, tokens);
    QHash<QString, WavePreviewSignalContext> contextsByName;
    for (const WavePreviewSignalContext& context : signalContextCollection.contexts)
        contextsByName.insert(context.signalName, context);

    QHash<QString, int> laneIndexes;
    int pos = 0;
    while (pos < tokens.size()) {
        const Token& token = tokens.at(pos);
        if (isIdentifierToken(token, QStringLiteral("assign"))) {
            WavePreviewAssignment assignment;
            if (parseContinuousAssign(query.documentText, tokens, pos, &assignment)
                && assignmentMatchesScope(assignment, scope)) {
                appendLaneAssignment(&report.lanes,
                                     &laneIndexes,
                                     &contextsByName,
                                     assignment);
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
                QList<WavePreviewAssignment> scopedAssignments;
                for (const WavePreviewAssignment& assignment : assignments) {
                    if (assignmentMatchesScope(assignment, scope))
                        scopedAssignments.append(assignment);
                }

                if (!scopedAssignments.isEmpty()) {
                    const int blockIndex = report.blocks.size();
                    fixBlockIndexes(&scopedAssignments, blockIndex);
                    block.assignmentCount = scopedAssignments.size();
                    report.blocks.append(block);
                } else if (blockMatchesScope(block, scope)) {
                    appendUnique(&report.warnings, noLaneWarningForBlock(block));
                }

                for (const WavePreviewAssignment& assignment : scopedAssignments) {
                    appendLaneAssignment(&report.lanes,
                                         &laneIndexes,
                                         &contextsByName,
                                         assignment);
                    ++report.assignmentCount;
                }
                pos = qMax(pos + 1, consumedIndex + 1);
                continue;
            } else if (rangeIntersectsScope(token.start, token.end, scope)) {
                appendUnique(&report.warnings, parseFailureWarningForAlways(token));
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
    enrichSignalContextsFromSnapshot(query, report, &signalContextCollection);
    applySignalContextsToReport(signalContextCollection, &report);
    report.clockResetGroups = clockResetGroupsForBlocks(report.blocks);
    refreshLaneSummaries(&report.lanes);
    report.activitySummary = activitySummaryForLanes(report.lanes);
    const QStringList structuralWarnings = report.warnings;
    report.warnings = applyWarningTextsForLanes(&report.lanes);
    for (const QString& warning : structuralWarnings)
        appendUnique(&report.warnings, warning);
    report.trace = buildTraceReport(query.documentText,
                                    tokens,
                                    signalContextCollection,
                                    report);
    for (const QString& warning : report.trace.warnings) {
        if (!report.warnings.contains(warning))
            report.warnings.append(warning);
    }
    if (report.assignmentCount == 0
        && report.lanes.isEmpty()
        && report.warnings.isEmpty()
        && queryLooksLikeProcessScope(query, scope)) {
        appendUnique(&report.warnings, emptyProcessScopeWarning(report));
    }
    report.available = report.assignmentCount > 0;
    return report;
}
