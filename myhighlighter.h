#ifndef MYHIGHLIGHTER_H
#define MYHIGHLIGHTER_H

#include "zeroslackexport.h"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include "tsdocument.h"

class QTextDocument;

// Syntax highlighter backed by the real-time Tree-sitter layer. It does NOT own or parse the tree;
// the owning MyCodeEditor keeps a TSDocument in sync (incrementally, on contentsChange, before this
// highlighter runs) and passes it in. highlightBlock just reads tree-sitter highlight spans, so it
// is error-tolerant (works on half-typed code) and correct with non-ASCII (UTF-16 offsets).
class ZEROSLACK_API MyHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    MyHighlighter(QTextDocument *parent, const TSDocument *tsdoc);

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

    const TSDocument *m_tsdoc = nullptr;   // owned by the editor; kept in sync before we run
};

#endif // MYHIGHLIGHTER_H
