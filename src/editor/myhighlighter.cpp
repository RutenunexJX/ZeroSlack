#include "myhighlighter.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
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
    deferredRefresh.setSingleShot(true);
    connect(&deferredRefresh, &QTimer::timeout, this, [this] {
        QElapsedTimer slice;
        slice.start();
        int blocks = 0;
        while (nextRefreshBlock >= 0 && blocks++ < 64) {
            const auto block = document()->findBlockByNumber(nextRefreshBlock);
            if (!block.isValid()) { nextRefreshBlock = lastRefreshBlock = -1; break; }
            if (nextRefreshBlock >= lastRefreshBlock)
                nextRefreshBlock = lastRefreshBlock = -1;
            else
                ++nextRefreshBlock;
            applyingRefresh = true;
            rehighlightBlock(block);
            applyingRefresh = false;
            if (slice.nsecsElapsed() >= 2'000'000)
                break;
        }
        if (nextRefreshBlock >= 0)
            deferredRefresh.start(1);
    });
    QObject::connect(
        &ApplicationThemeManager::instance(),
        &ApplicationThemeManager::themeChanged,
        this,
        [this](ThemeMode) {
            initFormats();
            if (document() && document()->blockCount() <= 64)
                rehighlight();
            else
                requestDeferredRefresh();
        });
    connect(parent, &QTextDocument::contentsChange, this,
        [this](int position, int removed, int added) {
            if (removed || added)
                requestDeferredRefresh(position, position + added);
        });
    requestDeferredRefresh();
}

MyHighlighter::~MyHighlighter()
{
    deferredRefresh.stop();
    // QSyntaxHighlighter's base destructor edits the document's formats and
    // can emit contentsChange. Disconnect while our timer/blocks still live.
    if (document()) disconnect(document(), nullptr, this, nullptr);
    setDocument(nullptr);
}

void MyHighlighter::rehighlight()
{
    // Preserve the explicit synchronous API. Automatic document updates and
    // large theme changes use the bounded range queue above.
    applyingRefresh = true;
    QSyntaxHighlighter::rehighlight();
    applyingRefresh = false;
}

void MyHighlighter::requestDeferredRefresh()
{
    if (document())
        requestDeferredRefresh(0, document()->characterCount() - 1);
}

void MyHighlighter::requestDeferredRefresh(int firstCharacter, int lastCharacter)
{
    if (!document())
        return;
    const int end = qMax(0, document()->characterCount() - 1);
    const auto first = document()->findBlock(qBound(0, firstCharacter, end));
    const auto last = document()->findBlock(qBound(0, lastCharacter, end));
    // QTextDocument::clear invalidates cached QTextBlock nodes before its
    // contentsChange callbacks finish. Retain line numbers between slices
    // and only inspect newly resolved blocks from the current document.
    if (!first.isValid() || !last.isValid()) return;
    const int firstLine = first.blockNumber(), lastLine = last.blockNumber();
    const int blockCount = document()->blockCount();
    const int delta = blockCount - refreshBlockCount;
    if (nextRefreshBlock > firstLine)
        nextRefreshBlock = qMax(firstLine, nextRefreshBlock + delta);
    if (lastRefreshBlock >= firstLine)
        lastRefreshBlock = qMax(firstLine, lastRefreshBlock + delta);
    refreshBlockCount = blockCount;
    nextRefreshBlock = nextRefreshBlock < 0 ? firstLine : qMin(nextRefreshBlock, firstLine);
    lastRefreshBlock = qMin(blockCount - 1, qMax(lastRefreshBlock, lastLine));
    if (!deferredRefresh.isActive())
        deferredRefresh.start(0);
}

void MyHighlighter::initFormats()
{
    const InsightSyntaxTokens& syntax =
        InsightVisualStyle::theme().syntax;

    keywordFormat = QTextCharFormat();
    commentFormat = QTextCharFormat();
    numberFormat = QTextCharFormat();
    stringFormat = QTextCharFormat();
    errorFormat = QTextCharFormat();

    keywordFormat.setForeground(syntax.keyword);
    keywordFormat.setFontWeight(QFont::Bold);

    commentFormat.setForeground(syntax.comment);
    commentFormat.setFontItalic(true);

    numberFormat.setForeground(syntax.number);

    stringFormat.setForeground(syntax.string);

    errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    errorFormat.setUnderlineColor(syntax.errorUnderline);
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
    // Qt also calls this for its initial whole-document pass and changed
    // blocks. Only our bounded refresh reads the syntax tree. In particular,
    // Qt's block-state propagation must not turn one rehighlightBlock into
    // an unbounded multiline-comment refresh.
    if (!applyingRefresh)
        return;
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

    // Tree-sitter supplies multiline context; its changed ranges schedule
    // following blocks explicitly, without Qt's synchronous state cascade.
    m_tsdoc->recordHighlightBlockForTest(
        static_cast<std::uint64_t>(blockTimer.nsecsElapsed()));
    if (trace) {
        std::fprintf(stderr, "lifecycle.highlight.exit\n");
        std::fflush(stderr);
    }
}
