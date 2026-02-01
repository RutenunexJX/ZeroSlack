#include "sv_lexer.h"

SVLexer::SVLexer(const QString &line)
    : m_line(line),
      m_pos(0),
      m_state(0)
{
}

void SVLexer::setState(int state)
{
    m_state = state;
}

int SVLexer::getState() const
{
    return m_state;
}

QChar SVLexer::peek(int offset) const
{
    int i = m_pos + offset;
    if (i >= m_line.length())
        return QChar();
    return m_line.at(i);
}

void SVLexer::advance()
{
    if (m_pos < m_line.length())
        ++m_pos;
}

bool SVLexer::atEnd() const
{
    return m_pos >= m_line.length();
}

static bool isSpace(QChar c)
{
    return c.isSpace();
}

static bool isLetterOrUnderscore(QChar c)
{
    return c.isLetter() || c == QLatin1Char('_');
}

static bool isIdentifierChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

static bool isDigit(QChar c)
{
    return c.isDigit();
}

Token SVLexer::nextToken()
{
    if (atEnd()) {
        Token t;
        t.type = TokenType::EOF_SYMBOL;
        t.offset = m_pos;
        t.length = 0;
        return t;
    }

    const int start = m_pos;

    if (m_state == 1) {
        int i = m_pos;
        while (i < m_line.length()) {
            if (m_line.at(i) == QLatin1Char('*') && i + 1 < m_line.length() && m_line.at(i + 1) == QLatin1Char('/')) {
                m_state = 0;
                m_pos = i + 2;
                Token t;
                t.type = TokenType::Comment;
                t.offset = start;
                t.length = m_pos - start;
                return t;
            }
            ++i;
        }
        m_pos = m_line.length();
        Token t;
        t.type = TokenType::Comment;
        t.offset = start;
        t.length = m_pos - start;
        return t;
    }

    if (isSpace(peek())) {
        while (!atEnd() && isSpace(peek()))
            advance();
        Token t;
        t.type = TokenType::Whitespace;
        t.offset = start;
        t.length = m_pos - start;
        return t;
    }

    if (peek() == QLatin1Char('/') && peek(1) == QLatin1Char('/')) {
        m_pos = m_line.length();
        Token t;
        t.type = TokenType::Comment;
        t.offset = start;
        t.length = m_pos - start;
        return t;
    }

    if (peek() == QLatin1Char('/') && peek(1) == QLatin1Char('*')) {
        advance();
        advance();
        while (!atEnd()) {
            if (peek() == QLatin1Char('*') && peek(1) == QLatin1Char('/')) {
                advance();
                advance();
                break;
            }
            advance();
        }
        if (atEnd())
            m_state = 1;
        Token t;
        t.type = TokenType::Comment;
        t.offset = start;
        t.length = m_pos - start;
        return t;
    }

    if (peek() == QLatin1Char('"')) {
        advance();
        while (!atEnd()) {
            if (peek() == QLatin1Char('\\') && peek(1) == QLatin1Char('"')) {
                advance();
                advance();
                continue;
            }
            if (peek() == QLatin1Char('"')) {
                advance();
                break;
            }
            advance();
        }
        Token t;
        t.type = TokenType::String;
        t.offset = start;
        t.length = m_pos - start;
        return t;
    }

    if (isDigit(peek())) {
        while (!atEnd() && isDigit(peek()))
            advance();
        if (!atEnd() && peek() == QLatin1Char('.') && peek(1).isDigit()) {
            advance();
            while (!atEnd() && isDigit(peek()))
                advance();
        }
        Token t;
        t.type = TokenType::Number;
        t.offset = start;
        t.length = m_pos - start;
        return t;
    }

    if (isLetterOrUnderscore(peek())) {
        while (!atEnd() && isIdentifierChar(peek()))
            advance();
        Token t;
        t.type = TokenType::Identifier;
        t.offset = start;
        t.length = m_pos - start;
        return t;
    }

    static const QString operators = QStringLiteral("~!@#$%^&*()-+=|[]{}:;<>,.?/");
    if (operators.contains(peek())) {
        advance();
        Token t;
        t.type = TokenType::Operator;
        t.offset = start;
        t.length = 1;
        return t;
    }

    advance();
    Token t;
    t.type = TokenType::Error;
    t.offset = start;
    t.length = 1;
    return t;
}
