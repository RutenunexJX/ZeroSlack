#include "editorviewprojection.h"

#include "mycodeeditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextLayout>

#include <algorithm>
#include <utility>

namespace {
qreal blockHeight(QAbstractTextDocumentLayout* layout,
                  const QTextBlock& block,
                  int fallback)
{
    if (!layout || !block.isValid())
        return qMax(1, fallback);
    return qMax<qreal>(1.0, layout->blockBoundingRect(block).height());
}

QTextLayout::FormatRange selectionRange(
    const QTextCursor& selection,
    const QTextCharFormat& format,
    const QTextBlock& block)
{
    QTextLayout::FormatRange range;
    const int blockStart = block.position();
    const int blockEnd = blockStart + block.length();
    const int selectionStart = qMin(selection.anchor(), selection.position());
    const int selectionEnd = qMax(selection.anchor(), selection.position());
    range.start = qMax(0, selectionStart - blockStart);
    range.length = qMax(0,
                        qMin(blockEnd, selectionEnd)
                            - qMax(blockStart, selectionStart));
    range.format = format;
    return range;
}

}

void EditorViewProjection::invalidate()
{
    dirty = true;
}

void EditorViewProjection::ensure(
    MyCodeEditor* editor,
    const QList<QPair<int, int>>& collapsedRanges)
{
    if (!editor || !editor->document())
        return;
    const int blockCount = editor->document()->blockCount();
    const int viewportHeight = editor->viewport()->height();
    const int viewportWidth = editor->viewport()->width();
    QAbstractTextDocumentLayout* layout = editor->document()->documentLayout();
    const qreal rowHeight = blockHeight(layout,
                                        editor->document()->begin(),
                                        editor->fontMetrics().height());
    const bool wrappingGeometryChanged =
        editor->lineWrapMode() != QPlainTextEdit::NoWrap
        && viewportWidth != cachedViewportWidth;
    if (dirty
        || editor->document() != cachedDocument
        || blockCount != cachedBlockCount
        || !qFuzzyCompare(rowHeight + 1.0, cachedRowHeight + 1.0)
        || wrappingGeometryChanged
        || collapsedRanges != cachedCollapsedRanges) {
        rebuild(editor, collapsedRanges);
    } else {
        cachedViewportHeight = viewportHeight;
        cachedViewportWidth = viewportWidth;
        configureVerticalScrollBar(editor);
    }
}

bool EditorViewProjection::active() const
{
    return projectionActive;
}

bool EditorViewProjection::sourceLineVisible(int sourceLine) const
{
    return sourceLine >= 0
        && sourceLine < sourceToVisibleRows.size()
        && sourceToVisibleRows.at(sourceLine) >= 0;
}

int EditorViewProjection::visibleRowForSourceLine(int sourceLine) const
{
    return sourceLine >= 0 && sourceLine < sourceToVisibleRows.size()
        ? sourceToVisibleRows.at(sourceLine)
        : -1;
}

int EditorViewProjection::sourceLineForVisibleRow(int visibleRow) const
{
    return visibleRow >= 0 && visibleRow < visibleSourceLines.size()
        ? visibleSourceLines.at(visibleRow)
        : -1;
}

int EditorViewProjection::visibleRowCount() const
{
    return visibleSourceLines.size();
}

QTextBlock EditorViewProjection::nextVisibleBlock(
    MyCodeEditor* editor,
    const QTextBlock& block) const
{
    if (!editor || !editor->document() || !block.isValid())
        return {};
    const auto next = std::upper_bound(visibleSourceLines.cbegin(),
                                       visibleSourceLines.cend(),
                                       block.blockNumber());
    if (next == visibleSourceLines.cend())
        return {};
    return editor->document()->findBlockByNumber(*next);
}

void EditorViewProjection::rebuild(
    MyCodeEditor* editor,
    const QList<QPair<int, int>>& collapsedRanges)
{
    const bool wasActive = projectionActive;
    QTextDocument* document = editor->document();
    const int count = document ? document->blockCount() : 0;
    QVector<int> hiddenDelta(count + 1, 0);
    projectionActive = false;
    for (const QPair<int, int>& range : collapsedRanges) {
        const int firstHidden = qBound(0, range.first + 1, count);
        const int afterHidden = qBound(firstHidden, range.second + 1, count);
        if (firstHidden >= afterHidden)
            continue;
        ++hiddenDelta[firstHidden];
        --hiddenDelta[afterHidden];
        projectionActive = true;
    }

    visibleSourceLines.clear();
    sourceToVisibleRows.fill(-1, count);
    visibleRowTops.clear();
    visibleRowHeights.clear();
    visibleSourceLines.reserve(count);
    visibleRowTops.reserve(count);
    visibleRowHeights.reserve(count);

    QAbstractTextDocumentLayout* layout =
        document ? document->documentLayout() : nullptr;
    const int fallbackHeight = qMax(1, editor->fontMetrics().height());
    int hiddenDepth = 0;
    qreal top = 0.0;
    QTextBlock block = document ? document->begin() : QTextBlock();
    for (int sourceLine = 0;
         sourceLine < count && block.isValid();
         ++sourceLine, block = block.next()) {
        hiddenDepth += hiddenDelta.at(sourceLine);
        ++metricValues.sourceRowsVisitedDuringRebuild;
        if (hiddenDepth > 0)
            continue;
        const int row = visibleSourceLines.size();
        const qreal height = blockHeight(layout, block, fallbackHeight);
        sourceToVisibleRows[sourceLine] = row;
        visibleSourceLines.append(sourceLine);
        visibleRowTops.append(top);
        visibleRowHeights.append(height);
        top += height;
    }
    projectedDocumentHeight = top;
    cachedDocument = document;
    cachedBlockCount = count;
    cachedViewportHeight = editor->viewport()->height();
    cachedViewportWidth = editor->viewport()->width();
    cachedRowHeight = blockHeight(layout,
                                  document ? document->begin() : QTextBlock(),
                                  fallbackHeight);
    cachedCollapsedRanges = collapsedRanges;
    dirty = false;
    ++metricValues.rebuilds;
    if (projectionActive)
        configureVerticalScrollBar(editor);
    else if (wasActive)
        restoreCanonicalVerticalScrollBar(editor);
}

void EditorViewProjection::configureVerticalScrollBar(
    MyCodeEditor* editor) const
{
    if (!editor || !projectionActive || !editor->verticalScrollBar())
        return;
    QScrollBar* bar = editor->verticalScrollBar();
    if (visibleRowTops.isEmpty()) {
        if (bar->minimum() != 0 || bar->maximum() != 0
            || bar->value() != 0) {
            const QSignalBlocker blocker(bar);
            bar->setRange(0, 0);
            bar->setValue(0);
        }
        return;
    }
    const qreal targetTop = qMax<qreal>(
        0.0, projectedDocumentHeight - editor->viewport()->height());
    const auto maximumIt = std::upper_bound(visibleRowTops.cbegin(),
                                             visibleRowTops.cend(),
                                             targetTop);
    const int maximum = visibleRowTops.isEmpty()
        ? 0
        : qBound(0,
                 static_cast<int>(maximumIt - visibleRowTops.cbegin()) - 1,
                 visibleRowTops.size() - 1);
    const int boundedValue = qBound(0, bar->value(), maximum);
    const int first = qBound(0, boundedValue, visibleRowTops.size() - 1);
    const qreal pageBottom = visibleRowTops.at(first)
        + editor->viewport()->height();
    const auto pageEnd = std::lower_bound(visibleRowTops.cbegin() + first,
                                           visibleRowTops.cend(),
                                           pageBottom);
    const int pageRows = qMax(
        1, static_cast<int>(pageEnd - (visibleRowTops.cbegin() + first)));
    if (bar->minimum() == 0
        && bar->maximum() == maximum
        && bar->singleStep() == 1
        && bar->pageStep() == pageRows
        && bar->value() == boundedValue) {
        return;
    }
    const QSignalBlocker blocker(bar);
    if (bar->minimum() != 0 || bar->maximum() != maximum)
        bar->setRange(0, maximum);
    if (bar->singleStep() != 1)
        bar->setSingleStep(1);
    if (bar->pageStep() != pageRows)
        bar->setPageStep(pageRows);
    if (bar->value() != boundedValue)
        bar->setValue(boundedValue);
}

void EditorViewProjection::restoreCanonicalVerticalScrollBar(
    MyCodeEditor* editor) const
{
    if (!editor || !editor->verticalScrollBar() || !editor->document())
        return;
    QScrollBar* bar = editor->verticalScrollBar();
    QAbstractTextDocumentLayout* layout = editor->document()->documentLayout();
    const qreal rowHeight = blockHeight(layout,
                                        editor->document()->begin(),
                                        editor->fontMetrics().height());
    const int pageRows = qMax(
        1, static_cast<int>(editor->viewport()->height() / rowHeight));
    const int maximum = qMax(
        0, editor->document()->blockCount() - pageRows);
    const int boundedValue = qBound(0, bar->value(), maximum);
    if (bar->minimum() == 0
        && bar->maximum() == maximum
        && bar->singleStep() == 1
        && bar->pageStep() == pageRows
        && bar->value() == boundedValue) {
        return;
    }
    const QSignalBlocker blocker(bar);
    if (bar->minimum() != 0 || bar->maximum() != maximum)
        bar->setRange(0, maximum);
    if (bar->singleStep() != 1)
        bar->setSingleStep(1);
    if (bar->pageStep() != pageRows)
        bar->setPageStep(pageRows);
    if (bar->value() != boundedValue)
        bar->setValue(boundedValue);
}

int EditorViewProjection::firstVisibleRow(MyCodeEditor* editor) const
{
    if (!editor || visibleSourceLines.isEmpty())
        return 0;
    const QScrollBar* bar = editor->verticalScrollBar();
    return qBound(0,
                  bar ? bar->value() : 0,
                  visibleSourceLines.size() - 1);
}

int EditorViewProjection::rowAtViewportY(MyCodeEditor* editor, int y) const
{
    if (visibleSourceLines.isEmpty())
        return -1;
    const int first = firstVisibleRow(editor);
    const qreal documentY = visibleRowTops.at(first) + qMax(0, y);
    auto it = std::upper_bound(visibleRowTops.cbegin(),
                               visibleRowTops.cend(),
                               documentY);
    int row = static_cast<int>(it - visibleRowTops.cbegin()) - 1;
    return qBound(first, row, visibleSourceLines.size() - 1);
}

QTextBlock EditorViewProjection::firstVisibleBlock(
    MyCodeEditor* editor) const
{
    if (!editor || !editor->document() || visibleSourceLines.isEmpty())
        return {};
    return editor->document()->findBlockByNumber(
        visibleSourceLines.at(firstVisibleRow(editor)));
}

EditorProjectionGeometry EditorViewProjection::blockGeometry(
    MyCodeEditor* editor,
    int sourceLine) const
{
    EditorProjectionGeometry result;
    if (!editor || visibleSourceLines.isEmpty())
        return result;
    const int row = visibleRowForSourceLine(sourceLine);
    const int first = firstVisibleRow(editor);
    if (row >= 0) {
        result.top = visibleRowTops.at(row) - visibleRowTops.at(first);
        result.height = visibleRowHeights.at(row);
        return result;
    }
    const auto next = std::lower_bound(visibleSourceLines.cbegin(),
                                       visibleSourceLines.cend(),
                                       sourceLine);
    const int nextRow = static_cast<int>(next - visibleSourceLines.cbegin());
    result.top = nextRow < visibleRowTops.size()
        ? visibleRowTops.at(nextRow) - visibleRowTops.at(first)
        : projectedDocumentHeight - visibleRowTops.at(first);
    return result;
}

QRectF EditorViewProjection::blockBoundingGeometry(
    MyCodeEditor* editor,
    const QTextBlock& block) const
{
    const int row = visibleRowForSourceLine(block.blockNumber());
    if (row < 0)
        return QRectF(0.0, blockGeometry(editor, block.blockNumber()).top,
                      editor ? editor->viewport()->width() : 0, 0.0);
    return QRectF(0.0,
                  visibleRowTops.at(row),
                  editor ? editor->viewport()->width() : 0,
                  visibleRowHeights.at(row));
}

QRectF EditorViewProjection::blockBoundingRect(
    MyCodeEditor* editor,
    const QTextBlock& block) const
{
    const int row = visibleRowForSourceLine(block.blockNumber());
    return QRectF(0.0,
                  0.0,
                  editor ? editor->viewport()->width() : 0,
                  row >= 0 ? visibleRowHeights.at(row) : 0.0);
}

QPointF EditorViewProjection::contentOffset(MyCodeEditor* editor) const
{
    if (!editor || visibleSourceLines.isEmpty())
        return {};
    const int first = firstVisibleRow(editor);
    const int horizontal = editor->horizontalScrollBar()
        ? editor->horizontalScrollBar()->value() : 0;
    return QPointF(-horizontal, -visibleRowTops.at(first));
}

qreal EditorViewProjection::documentHeight() const
{
    return projectedDocumentHeight;
}

QTextCursor EditorViewProjection::cursorForPosition(
    MyCodeEditor* editor,
    const QPoint& position) const
{
    if (!editor || !editor->document())
        return {};
    const int row = rowAtViewportY(editor, position.y());
    const int sourceLine = sourceLineForVisibleRow(row);
    QTextBlock block = editor->document()->findBlockByNumber(sourceLine);
    if (!block.isValid())
        return QTextCursor(editor->document());
    QTextLayout* layout = block.layout();
    int offset = 0;
    if (layout && layout->lineCount() > 0) {
        const EditorProjectionGeometry geometry =
            blockGeometry(editor, sourceLine);
        const qreal localY = qMax<qreal>(0.0, position.y() - geometry.top);
        QTextLine line = layout->lineAt(0);
        for (int index = 0; index < layout->lineCount(); ++index) {
            const QTextLine candidate = layout->lineAt(index);
            if (localY >= candidate.y()
                && localY < candidate.y() + candidate.height()) {
                line = candidate;
                break;
            }
        }
        const int horizontal = editor->horizontalScrollBar()
            ? editor->horizontalScrollBar()->value() : 0;
        offset = line.xToCursor(position.x() + horizontal,
                                QTextLine::CursorBetweenCharacters);
    }
    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + qBound(0, offset, block.text().size()));
    return cursor;
}

QRect EditorViewProjection::cursorRect(
    MyCodeEditor* editor,
    const QTextCursor& cursor) const
{
    if (!editor || !editor->document() || cursor.isNull())
        return {};
    const QTextBlock block = cursor.block();
    int row = visibleRowForSourceLine(block.blockNumber());
    if (row < 0)
        return {};
    QTextLayout* layout = block.layout();
    const int offset = qBound(0,
                              cursor.position() - block.position(),
                              block.text().size());
    QTextLine line = layout ? layout->lineForTextPosition(offset) : QTextLine();
    if (!line.isValid() && layout && layout->lineCount() > 0)
        line = layout->lineAt(0);
    const int first = firstVisibleRow(editor);
    const int horizontal = editor->horizontalScrollBar()
        ? editor->horizontalScrollBar()->value() : 0;
    int adjustedOffset = offset;
    const qreal x = line.isValid()
        ? line.cursorToX(&adjustedOffset, QTextLine::Leading) - horizontal
        : -horizontal;
    const qreal y = visibleRowTops.at(row) - visibleRowTops.at(first)
        + (line.isValid() ? line.y() : 0.0);
    const qreal height = line.isValid()
        ? line.height() : visibleRowHeights.at(row);
    return QRect(qRound(x), qRound(y), editor->cursorWidth(), qRound(height));
}

void EditorViewProjection::ensureCursorVisible(
    MyCodeEditor* editor,
    bool center) const
{
    if (!editor || !projectionActive || visibleSourceLines.isEmpty())
        return;
    const int row = visibleRowForSourceLine(
        editor->textCursor().blockNumber());
    if (row < 0)
        return;
    QScrollBar* bar = editor->verticalScrollBar();
    const int page = qMax(1, bar->pageStep());
    int next = bar->value();
    if (center)
        next = row - page / 2;
    else if (row < next)
        next = row;
    else if (row >= next + page)
        next = row - page + 1;
    bar->setValue(qBound(bar->minimum(), next, bar->maximum()));
}

void EditorViewProjection::paint(
    MyCodeEditor* editor,
    QPaintEvent* event)
{
    if (!editor || !event || !projectionActive)
        return;
    QPainter painter(editor->viewport());
    painter.setClipRect(event->rect());
    painter.setPen(editor->palette().color(QPalette::Text));

    const int first = firstVisibleRow(editor);
    const qreal originTop = visibleRowTops.at(first);
    const int horizontal = editor->horizontalScrollBar()
        ? editor->horizontalScrollBar()->value() : 0;
    const QList<QTextEdit::ExtraSelection> extras = editor->extraSelections();
    const QTextCursor mainSelection = editor->textCursor();
    for (int row = first; row < visibleSourceLines.size(); ++row) {
        const qreal top = visibleRowTops.at(row) - originTop;
        const qreal height = visibleRowHeights.at(row);
        if (top > event->rect().bottom())
            break;
        if (top + height < event->rect().top())
            continue;
        QTextBlock block = editor->document()->findBlockByNumber(
            visibleSourceLines.at(row));
        QTextLayout* layout = block.isValid() ? block.layout() : nullptr;
        if (!layout)
            continue;

        const QRectF rowRect(0.0, top, editor->viewport()->width(), height);
        if (block.blockFormat().background().style() != Qt::NoBrush)
            painter.fillRect(rowRect, block.blockFormat().background());

        QVector<QTextLayout::FormatRange> selections;
        for (const QTextEdit::ExtraSelection& extra : extras) {
            const int blockStart = block.position();
            const int blockEnd = blockStart + block.length();
            const int start = qMin(extra.cursor.anchor(), extra.cursor.position());
            const int end = qMax(extra.cursor.anchor(), extra.cursor.position());
            const bool fullWidth = extra.format.property(
                QTextFormat::FullWidthSelection).toBool();
            if (fullWidth
                && extra.cursor.blockNumber() == block.blockNumber()
                && extra.format.background().style() != Qt::NoBrush) {
                painter.fillRect(rowRect, extra.format.background());
            }
            if (end <= blockStart || start >= blockEnd)
                continue;
            const QTextLayout::FormatRange range =
                selectionRange(extra.cursor, extra.format, block);
            if (range.length > 0)
                selections.append(range);
        }
        if (mainSelection.hasSelection()) {
            const int blockStart = block.position();
            const int blockEnd = blockStart + block.length();
            const int start = qMin(mainSelection.anchor(), mainSelection.position());
            const int end = qMax(mainSelection.anchor(), mainSelection.position());
            if (end > blockStart && start < blockEnd) {
                QTextCharFormat format;
                format.setBackground(
                    editor->palette().color(QPalette::Highlight));
                format.setForeground(
                    editor->palette().color(QPalette::HighlightedText));
                const QTextLayout::FormatRange range =
                    selectionRange(mainSelection, format, block);
                if (range.length > 0)
                    selections.append(range);
            }
        }

        const QPointF origin(-horizontal, top);
        painter.setPen(editor->palette().color(QPalette::Text));
        layout->draw(&painter, origin, selections, event->rect());
        if (editor->hasFocus()
            && !mainSelection.hasSelection()
            && mainSelection.blockNumber() == block.blockNumber()) {
            const int offset = qBound(
                0,
                mainSelection.position() - block.position(),
                block.text().size());
            layout->drawCursor(&painter,
                               origin,
                               offset,
                               editor->cursorWidth());
        }
        ++metricValues.rowsPainted;
    }
}

EditorViewProjectionMetrics EditorViewProjection::metrics() const
{
    return metricValues;
}
