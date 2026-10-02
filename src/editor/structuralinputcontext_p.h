#pragma once

#include <QString>
#include <QVector>

// Tolerant context for unfinished RTL, returning only indentation and openers.
// This does not format a document or replace Tree-sitter syntax analysis.
namespace StructuralInput {
struct Token {
    QString text;
    QString indent;
    int start = 0;
    int end = 0;
    int line = 0;
};

inline bool identifierChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('$');
}

inline QString closerFor(const QString& word)
{
    if (word == QLatin1String("(")) return QStringLiteral(")");
    if (word == QLatin1String("[")) return QStringLiteral("]");
    if (word == QLatin1String("{")) return QStringLiteral("}");
    if (word == QLatin1String("begin")) return QStringLiteral("end");
    if (word == QLatin1String("case") || word == QLatin1String("casez")
        || word == QLatin1String("casex") || word == QLatin1String("randcase"))
        return QStringLiteral("endcase");
    if (word == QLatin1String("module")) return QStringLiteral("endmodule");
    return {};
}

struct Tokens {
    QVector<Token> items;
    bool openLiteral = false;
    bool lineComment = false;

    explicit Tokens(const QString& text)
    {
        int index = 0;
        QString indent;
        bool lineStart = true;
        int line = 0;
        const auto advance = [&]() {
            const QChar ch = text.at(index++);
            if (ch == QLatin1Char('\n')) {
                lineStart = true;
                indent.clear();
                ++line;
            } else if (lineStart
                       && (ch == QLatin1Char(' ') || ch == QLatin1Char('\t'))) {
                indent += ch;
            } else {
                lineStart = false;
            }
        };
        while (index < text.size()) {
            if (text.at(index).isSpace()) {
                advance();
                continue;
            }
            if (text.mid(index, 2) == QLatin1String("//")) {
                while (index < text.size() && text.at(index) != QLatin1Char('\n'))
                    advance();
                lineComment = index == text.size();
                continue;
            }
            if (text.mid(index, 2) == QLatin1String("/*")) {
                advance();
                advance();
                while (index < text.size()
                       && text.mid(index, 2) != QLatin1String("*/"))
                    advance();
                if (index == text.size()) {
                    openLiteral = true;
                    break;
                }
                advance();
                advance();
                continue;
            }
            const int start = index;
            const QString tokenIndent = indent;
            const int tokenLine = line;
            QString word;
            if (text.at(index) == QLatin1Char('"')) {
                advance();
                bool closed = false;
                while (index < text.size()) {
                    const QChar ch = text.at(index);
                    advance();
                    if (ch == QLatin1Char('\\') && index < text.size())
                        advance();
                    else if (ch == QLatin1Char('"')) {
                        closed = true;
                        break;
                    }
                }
                openLiteral = !closed;
                word = QStringLiteral("<string>");
            } else if (text.at(index) == QLatin1Char('\\')) {
                while (index < text.size() && !text.at(index).isSpace())
                    advance();
                word = QStringLiteral("<escaped-identifier>");
            } else if (identifierChar(text.at(index))
                       || text.at(index) == QLatin1Char(0x60)
                       || text.at(index) == QLatin1Char('\'')) {
                advance();
                while (index < text.size()
                       && (identifierChar(text.at(index))
                           || text.at(index) == QLatin1Char('\'')))
                    advance();
                word = text.mid(start, index - start);
            } else {
                advance();
                word = text.mid(start, 1);
                if (word == QLatin1String(":") && index < text.size()
                    && text.at(index) == QLatin1Char(':')) {
                    advance();
                    word = QStringLiteral("::");
                }
            }
            items.append({word, tokenIndent, start, index, tokenLine});
        }
    }

    QVector<int> openers() const
    {
        QVector<int> stack;
        for (int i = 0; i < items.size(); ++i) {
            const QString& word = items.at(i).text;
            if (!closerFor(word).isEmpty()) {
                stack.append(i);
            } else if (word == QLatin1String(")") || word == QLatin1String("]")
                       || word == QLatin1String("}") || word == QLatin1String("end")
                       || word == QLatin1String("endcase")
                       || word == QLatin1String("endmodule")) {
                for (int j = stack.size() - 1; j >= 0; --j) {
                    if (closerFor(items.at(stack.at(j)).text) == word) {
                        stack.resize(j);
                        break;
                    }
                }
            }
        }
        return stack;
    }
};

class Context {
public:
    explicit Context(const QString& text, int width)
        : tokens(text), step(qMax(1, width), QLatin1Char(' '))
    {
        while (at < tokens.items.size()) {
            const int before = at;
            statement(0);
            if (at == before)
                ++at;
        }
        const auto stack = tokens.openers();
        for (auto it = stack.crbegin(); it != stack.crend(); ++it) {
            const Token& token = tokens.items.at(*it);
            if (token.text == QLatin1String("(") || token.text == QLatin1String("[")
                || token.text == QLatin1String("{")) {
                nextIndent = token.indent + step;
                // Once the user has chosen a continuation indent, keep it.
                // Nested subexpressions must not make later list items drift.
                for (int index = *it + 1; index < tokens.items.size(); ++index) {
                    const Token& item = tokens.items.at(index);
                    if (item.line > token.line && item.indent.size() > token.indent.size()) {
                        nextIndent = item.indent;
                        break;
                    }
                }
                break;
            }
        }
    }

    bool closingIndent(const QString& closing, QString* indent) const
    {
        if (tokens.openLiteral || tokens.lineComment || !reliable)
            return false;
        if (closing == QLatin1String("else") && pendingIf >= 0) {
            *indent = tokens.items.at(pendingIf).indent;
            return true;
        }
        const auto stack = tokens.openers();
        for (auto it = stack.crbegin(); it != stack.crend(); ++it) {
            if (closerFor(tokens.items.at(*it).text) == closing) {
                *indent = tokens.items.at(*it).indent;
                return true;
            }
        }
        return false;
    }

    Tokens tokens;
    QString nextIndent;
    bool reliable = true;

private:
    QString step;
    int at = 0;
    int pendingIf = -1;

    bool is(const char* word) const
    {
        return at < tokens.items.size()
            && tokens.items.at(at).text == QLatin1String(word);
    }

    bool group()
    {
        QVector<QString> closes{closerFor(tokens.items.at(at++).text)};
        while (at < tokens.items.size() && !closes.isEmpty()) {
            const QString word = tokens.items.at(at++).text;
            if (word == QLatin1String("(") || word == QLatin1String("[")
                || word == QLatin1String("{"))
                closes.append(closerFor(word));
            else if (word == closes.last())
                closes.removeLast();
        }
        return closes.isEmpty();
    }

    void optionalBlockName()
    {
        if (is(":")) {
            ++at;
            if (at < tokens.items.size())
                ++at;
        }
    }

    bool sequence(const char* closing, int depth)
    {
        while (at < tokens.items.size() && !is(closing)) {
            if (is("end") || is("endcase") || is("endmodule"))
                return false;
            if (!statement(depth + 1))
                return false;
        }
        return is(closing);
    }

    bool statement(int depth)
    {
        if (at >= tokens.items.size())
            return false;
        if (depth > 128) {
            reliable = false;
            at = tokens.items.size();
            return false;
        }
        const int start = at;
        const Token token = tokens.items.at(at++);
        const QString base = token.indent;
        nextIndent = base;
        if ((token.text == QLatin1String("unique") || token.text == QLatin1String("unique0")
             || token.text == QLatin1String("priority"))
            && (is("if") || is("case") || is("casez") || is("casex")))
            return statement(depth + 1);
        if (token.text == QLatin1String("module")) {
            nextIndent = base + step;
            while (at < tokens.items.size() && !is(";")) {
                if (is("(") || is("[") || is("{")) {
                    if (!group()) return false;
                } else {
                    ++at;
                }
            }
            if (!is(";")) return false;
            ++at;
            if (!sequence("endmodule", depth)) return false;
            ++at;
            optionalBlockName();
            nextIndent = base;
            pendingIf = -1;
            return true;
        }
        if (token.text == QLatin1String("begin")) {
            optionalBlockName();
            nextIndent = base + step;
            if (!sequence("end", depth)) return false;
            ++at;
            optionalBlockName();
            nextIndent = base;
            pendingIf = -1;
            return true;
        }
        if (token.text == QLatin1String("if")) {
            nextIndent = base + step;
            if (!is("(") || !group()) return false;
            if (!statement(depth + 1)) return false;
            nextIndent = base;
            if (is("else")) {
                ++at;
                nextIndent = base + step;
                if (!statement(depth + 1)) return false;
                nextIndent = base;
            } else if (at == tokens.items.size()) {
                pendingIf = qMax(pendingIf, start);
            }
            return true;
        }
        if (token.text == QLatin1String("always")
            || token.text == QLatin1String("always_ff")
            || token.text == QLatin1String("always_comb")
            || token.text == QLatin1String("always_latch")
            || token.text == QLatin1String("initial")
            || token.text == QLatin1String("final")
            || token.text == QLatin1String("else")) {
            nextIndent = base + step;
            if (is("@") || is("#")) {
                ++at;
                if (is("(")) {
                    if (!group()) return false;
                } else if (at < tokens.items.size()) {
                    ++at;
                }
            }
            if (!statement(depth + 1)) return false;
            nextIndent = base;
            return true;
        }
        if (closerFor(token.text) == QLatin1String("endcase")) {
            nextIndent = base + step;
            if (token.text != QLatin1String("randcase")
                && (!is("(") || !group()))
                return false;
            while (at < tokens.items.size() && !is("endcase")) {
                pendingIf = -1;
                const QString labelIndent = tokens.items.at(at).indent;
                int ternaries = 0;
                while (at < tokens.items.size()) {
                    if (is("(") || is("[") || is("{")) {
                        if (!group()) return false;
                    } else if (is(":") && ternaries == 0) {
                        break;
                    } else {
                        if (is("?")) ++ternaries;
                        if (is(":")) --ternaries;
                        if (is("end") || is("endmodule")) return false;
                        ++at;
                    }
                }
                if (!is(":")) return false;
                ++at;
                nextIndent = labelIndent + step;
                if (!statement(depth + 1)) return false;
                nextIndent = labelIndent;
            }
            if (!is("endcase")) return false;
            ++at;
            nextIndent = base;
            pendingIf = -1;
            return true;
        }
        if (token.text == QLatin1String(";"))
            return true;
        while (at < tokens.items.size()) {
            if (is(";")) {
                nextIndent = tokens.items.at(at).indent;
                ++at;
                return true;
            }
            if (is("end") || is("endcase") || is("endmodule"))
                return false;
            if (is("(") || is("[") || is("{")) {
                if (!group()) return false;
            } else {
                ++at;
            }
        }
        if (at > start + 1)
            nextIndent = tokens.items.at(at - 1).indent;
        return false;
    }
};
} // namespace StructuralInput
