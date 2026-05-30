#include "myhighlighter.h"
#include <QTextDocument>
#include <QTextBlock>

MyHighlighter::MyHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    initFormats();
}

void MyHighlighter::initFormats()
{
    keywordFormat.setForeground(Qt::darkMagenta);
    keywordFormat.setFontWeight(QFont::Bold);

    commentFormat.setForeground(Qt::darkGreen);

    numberFormat.setForeground(QColor(250, 80, 50));

    stringFormat.setForeground(QColor(0, 180, 180));

    errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    errorFormat.setUnderlineColor(Qt::red);
}

const QTextCharFormat* MyHighlighter::formatFor(HlCategory category) const
{
    switch (category) {
    case HlCategory::Keyword: return &keywordFormat;
    case HlCategory::Comment: return &commentFormat;
    case HlCategory::Number:  return &numberFormat;
    case HlCategory::String:  return &stringFormat;
    // Operators / identifiers render with the default text format (matches prior behavior).
    default:                  return nullptr;
    }
}

void MyHighlighter::highlightBlock(const QString &text)
{
    QTextDocument* doc = document();
    if (!doc)
        return;

    // Keep the tree-sitter model in sync with the document. A document edit bumps revision();
    // re-parse once per highlight pass (the first highlightBlock of the pass), then every block in
    // the pass reads the fresh tree. (Incremental ts_tree_edit is a later optimization.)
    if (doc->revision() != m_parsedRevision) {
        m_tsdoc.setText(doc->toPlainText());
        m_parsedRevision = doc->revision();
    }

    const int blockStart = currentBlock().position();
    const QVector<HlSpan> spans = m_tsdoc.highlightSpans(blockStart, text.length());
    for (const HlSpan& s : spans) {
        if (const QTextCharFormat* f = formatFor(s.category))
            setFormat(s.start, s.length, *f);
    }

    // Propagate multi-line block-comment state so following blocks re-highlight when a /* */ opens.
    setCurrentBlockState(m_tsdoc.blockEndCommentState(blockStart, text.length()));
}
