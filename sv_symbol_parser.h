#ifndef SV_SYMBOL_PARSER_H
#define SV_SYMBOL_PARSER_H

#include "syminfo.h"
#include "sv_lexer.h"
#include "sv_token.h"
#include <QString>
#include <QVector>
#include <QList>
#include <QSet>

struct SVToken {
    Token token;
    int line = 0;   // 0-based line number
    int col = 0;    // column offset within line
};

class SVSymbolParser
{
public:
    SVSymbolParser(const QString &content, const QString &fileName);

    QList<sym_list::SymbolInfo> parse();

private:
    void tokenize();
    void advance();
    const SVToken *peek() const;
    bool match(TokenType type, const QString &text = QString()) const;
    bool checkKeyword(const QString &text) const;
    bool isAtEnd() const;

    void parseModule();
    void parsePortList(const QString &moduleName);

    QString m_content;
    QString m_fileName;
    QVector<SVToken> m_tokens;
    int m_pos = 0;
    QStringList m_scopeStack;

    QList<sym_list::SymbolInfo> m_symbols;
    QVector<int> m_lineStarts;

    static QSet<QString> structuralKeywords();
};

#endif // SV_SYMBOL_PARSER_H
