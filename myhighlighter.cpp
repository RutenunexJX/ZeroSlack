#include "myhighlighter.h"
#include <QTextDocument>
#include <QTextBlock>

MyHighlighter::MyHighlighter(QTextDocument *parent, const TSDocument *tsdoc)
    : QSyntaxHighlighter(parent), m_tsdoc(tsdoc)
{
    initFormats();
}

void MyHighlighter::initFormats()
{
    keywordFormat.setForeground(QColor("#C678DD"));
    keywordFormat.setFontWeight(QFont::Bold);

    commentFormat.setForeground(QColor("#7F848E"));
    commentFormat.setFontItalic(true);

    numberFormat.setForeground(QColor("#D19A66"));

    stringFormat.setForeground(QColor("#98C379"));

    errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    errorFormat.setUnderlineColor(QColor("#EF4444"));
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
    if (!m_tsdoc)
        return;

    // The editor has already synced m_tsdoc to the current document (its contentsChange slot is
    // connected before this highlighter, so it runs first). Just read spans for this block.
    const int blockStart = currentBlock().position();
    const QVector<HlSpan> spans = m_tsdoc->highlightSpans(blockStart, text.length());
    for (const HlSpan& s : spans) {
        if (const QTextCharFormat* f = formatFor(s.category))
            setFormat(s.start, s.length, *f);
    }

    // Propagate multi-line block-comment state so following blocks re-highlight when a /* */ opens.
    setCurrentBlockState(m_tsdoc->blockEndCommentState(blockStart, text.length()));
}
