#ifndef MYHIGHLIGHTER_H
#define MYHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include "tsdocument.h"

class QTextDocument;

// Syntax highlighter backed by the real-time Tree-sitter layer (TSDocument). It keeps a per-editor
// live parse tree in sync with the QTextDocument and colors each block from tree-sitter highlight
// spans. Error-tolerant (works on half-typed code) and correct with non-ASCII (UTF-16 offsets).
class MyHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MyHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    void initFormats();
    const QTextCharFormat* formatFor(HlCategory category) const;

    QTextCharFormat keywordFormat;
    QTextCharFormat commentFormat;
    QTextCharFormat numberFormat;
    QTextCharFormat stringFormat;
    QTextCharFormat errorFormat;

    TSDocument m_tsdoc;          // live tree-sitter model for this document
    int m_parsedRevision = -1;   // QTextDocument::revision() the tree was last parsed at
};

#endif // MYHIGHLIGHTER_H
