#ifndef SV_LEXER_H
#define SV_LEXER_H

#include "sv_token.h"
#include <QString>

class SVLexer
{
public:
    explicit SVLexer(const QString &line);

    void setState(int state);
    int getState() const;

    Token nextToken();

private:
    QString m_line;
    int m_pos;
    int m_state;  // 0 = normal, 1 = inside block comment

    QChar peek(int offset = 0) const;
    void advance();
    bool atEnd() const;
};

#endif // SV_LEXER_H
