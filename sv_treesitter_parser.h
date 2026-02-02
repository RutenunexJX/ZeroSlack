#ifndef SV_TREESITTER_PARSER_H
#define SV_TREESITTER_PARSER_H

#include "syminfo.h"
#include <QString>
#include <QList>

extern "C" {
#include <tree_sitter/api.h>
}

class SVTreeSitterParser
{
public:
    SVTreeSitterParser();
    ~SVTreeSitterParser();

    void parse(const QString &content);
    QList<sym_list::SymbolInfo> getSymbols();

private:
    TSParser *m_parser;
    TSTree *m_tree;
    QString m_currentContent;
};

#endif // SV_TREESITTER_PARSER_H
