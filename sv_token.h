#ifndef SV_TOKEN_H
#define SV_TOKEN_H

#include <QString>

enum class TokenType {
    Keyword,
    Comment,
    Error,
    Identifier,
    Whitespace,
    Number,
    String,
    EOF_SYMBOL
};

struct Token {
    TokenType type = TokenType::EOF_SYMBOL;
    int offset = 0;
    int length = 0;
};

#endif // SV_TOKEN_H
