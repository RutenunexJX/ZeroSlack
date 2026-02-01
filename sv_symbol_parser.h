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
    SVSymbolParser(const QString &content, const QString &fileName,
                   const QSet<QString> &predefinedTypes = QSet<QString>());

    QList<sym_list::SymbolInfo> parse();

    /** 取走注释区域列表并清空内部缓存，由 syminfo 写入 commentRegions */
    QList<sym_list::CommentRegion> takeComments();

private:
    void tokenize();
    void advance();
    const SVToken *peek() const;
    bool match(TokenType type, const QString &text = QString()) const;
    bool checkKeyword(const QString &text) const;
    bool isAtEnd() const;

    void parseModule();
    void parsePortList(const QString &moduleName);
    void parseTypedef();
    QString parseEnum(const QString &typeName);
    void parseStruct(const QString &typeName, bool isPacked, bool trailingIsInlineVar = false);
    /** 模块内 struct/union { ... } var_name; 无 typedef，消费整块并产出 struct/成员/变量符号 */
    void parseStructDecl();
    void parseVarDecl(const QString &typeName, sym_list::sym_type_e type);
    void parseAlways(const QString &keyword);
    void parseAssign();
    /** typeName = 模块类型；instanceNameFromModule 非空时表示已从流中读过实例名，当前在 '('，并传入实例名所在行/列 */
    void parseInstantiation(const QString &typeName, const QString &instanceNameFromModule = QString(),
                            int instLine = -1, int instCol = -1);

    QString m_content;
    QString m_fileName;
    QVector<SVToken> m_tokens;
    int m_pos = 0;
    QStringList m_scopeStack;
    QSet<QString> m_knownTypes;

    QList<sym_list::SymbolInfo> m_symbols;
    QVector<int> m_lineStarts;
    QList<sym_list::CommentRegion> m_comments;

    static QSet<QString> structuralKeywords();
};

#endif // SV_SYMBOL_PARSER_H
