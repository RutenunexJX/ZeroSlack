#ifndef SV_TREESITTER_PARSER_H
#define SV_TREESITTER_PARSER_H

#include "syminfo.h"
#include <QString>
#include <QList>
#include <QStack>

extern "C" {
#include <tree_sitter/api.h>
}

class SVTreeSitterParser
{
public:
    SVTreeSitterParser();
    /** Constructor for main symbol extraction path: content and fileName are stored for parseSymbols(). */
    SVTreeSitterParser(const QString &content, const QString &fileName);
    ~SVTreeSitterParser();

    void parse(const QString &content);
    /** Returns symbols with fileName and moduleScope filled; parses first if needed. */
    QList<sym_list::SymbolInfo> parseSymbols();
    QList<sym_list::SymbolInfo> getSymbols();

    /** Take comment regions (for sym_list::commentRegions). May return empty list until comment extraction is implemented. */
    QList<sym_list::CommentRegion> takeComments();

private:
    void visitNode(TSNode node, QStack<QString> &scopeStack, QList<sym_list::SymbolInfo> &out, const QByteArray &utf8);

    /** Points to process-wide shared TSParser (never owned; do not delete in destructor). */
    TSParser *m_parser;
    TSTree *m_tree;
    QString m_currentContent;
    QString m_currentFileName;
    QList<sym_list::CommentRegion> m_commentRegions;
};

#endif // SV_TREESITTER_PARSER_H
