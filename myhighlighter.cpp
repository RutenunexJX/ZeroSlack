#include "myhighlighter.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QElapsedTimer>

#include <cstdio>

namespace {
int lineCommentStartOutsideString(const QString& text)
{
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i + 1 < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inString = true;
            continue;
        }

        if (ch == QLatin1Char('/') && text.at(i + 1) == QLatin1Char('/'))
            return i;
    }
    return -1;
}
}

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
    QElapsedTimer blockTimer;
    blockTimer.start();
    const bool trace = qEnvironmentVariableIsSet(
        "ZEROSLACK_EDITOR_LIFECYCLE_TRACE");
    if (trace) {
        std::fprintf(stderr, "lifecycle.highlight.enter\n");
        std::fflush(stderr);
    }
    if (!m_tsdoc)
        return;
    if (m_tsdoc->text().isEmpty() && !text.isEmpty())
        return;

    // The editor has already synced m_tsdoc to the current document (its contentsChange slot is
    // connected before this highlighter, so it runs first). Just read spans for this block.
    const int blockStart = currentBlock().position();
    const QVector<HlSpan> spans = m_tsdoc->highlightSpans(blockStart, text.length());
    if (trace) {
        std::fprintf(stderr, "lifecycle.highlight.spans\n");
        std::fflush(stderr);
    }
    for (const HlSpan& s : spans) {
        if (const QTextCharFormat* f = formatFor(s.category))
            setFormat(s.start, s.length, *f);
    }

    const int lineCommentStart = lineCommentStartOutsideString(text);
    if (lineCommentStart >= 0)
        setFormat(lineCommentStart,
                  text.size() - lineCommentStart,
                  commentFormat);

    // Propagate multi-line block-comment state so following blocks re-highlight when a /* */ opens.
    setCurrentBlockState(m_tsdoc->blockEndCommentState(blockStart, text.length()));
    m_tsdoc->recordHighlightBlockForTest(
        static_cast<std::uint64_t>(blockTimer.nsecsElapsed()));
    if (trace) {
        std::fprintf(stderr, "lifecycle.highlight.exit\n");
        std::fflush(stderr);
    }
}
