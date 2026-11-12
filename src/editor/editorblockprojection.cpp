#include "editorblockprojection.h"
#include "documentblockmodel.h"
#include "editorblockpreview.h"
#include "blocknotestyle.h"
#include "mycodeeditor.h"
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QTextLayout>
#include <QSet>
#include <QtMath>
#include <algorithm>

void EditorBlockProjection::ensure(MyCodeEditor* editor, const QList<QPair<int,int>>& folds) {
    auto* model = DocumentBlockModel::get(editor->document());
    if (std::none_of(model->items().cbegin(), model->items().cend(), [](const auto& block) { return !block.detached; })) {
        enabled = false; runs.clear(); gaps.clear(); heights.clear(); noteHeights.clear(); return;
    }
    const bool wasEnabled = enabled;
    const int previousSource = wasEnabled ? sourceForRow(rowAtY(offset(editor))) : editor->QPlainTextEdit::firstVisibleBlock().blockNumber();
    const bool layoutChanged = document != editor->document() || fontKey != editor->font().toString()
        || width != editor->viewport()->width() || styleRevision != BlockNoteStyle::instance().revision();
    if (layoutChanged || sourceCount != editor->document()->blockCount() || collapsed != folds) heights.clear();
    if (!dirty && !layoutChanged && sourceCount == editor->document()->blockCount()
        && modelRevision == model->revision() && collapsed == folds && enabled) {
        configureScrollBar(editor); return;
    }
    document = editor->document(); sourceCount = document->blockCount(); width = editor->viewport()->width();
    fontKey = editor->font().toString(); estimate = qMax(1, editor->fontMetrics().height());
    modelRevision = model->revision(); collapsed = folds;
    styleRevision = BlockNoteStyle::instance().revision();
    QList<QPair<int,int>> hidden;
    for (auto range : folds) {
        range.first = qBound(0, range.first + 1, sourceCount);
        range.second = qBound(range.first, range.second + 1, sourceCount);
        if (range.second > range.first) hidden.append(range);
    }
    std::sort(hidden.begin(), hidden.end());
    QList<QPair<int,int>> merged;
    for (const auto& range : hidden) {
        if (!merged.isEmpty() && merged.last().second >= range.first) merged.last().second = qMax(merged.last().second, range.second);
        else merged.append(range);
    }
    runs.clear(); count = 0; int from = 0;
    for (const auto& range : merged) {
        if (range.first > from) { runs.append({from, range.first - from, count}); count += range.first - from; }
        from = range.second;
    }
    if (from < sourceCount) { runs.append({from, sourceCount - from, count}); count += sourceCount - from; }
    gaps.clear();
    QSet<QString> measuredNotes;
    for (const auto& block : model->items()) {
        if (block.detached) continue;
        const int position = qBound(0, block.end > block.start ? block.end - 1 : block.end, document->characterCount() - 1);
        int line = block.missing ? sourceCount - 1 : document->findBlock(position).blockNumber();
        const bool hiddenAnchor = rowForSource(line) < 0;
        if (hiddenAnchor) for (const auto& range : merged) if (range.first <= line && line < range.second) { line = qMax(0, range.first - 1); break; }
        const int row = rowForSource(line);
        if (row < 0) continue;
        qreal blockHeight = block.collapsed || hiddenAnchor ? 36 : block.height + 36;
        const auto autoText=block.type=="text" ? block.text : block.autoHeight ? EditorBlockPreview::linkOnlyText(block) : QString();
        if (!block.collapsed && !hiddenAnchor && block.autoHeight && (block.type=="text" || !autoText.isEmpty())) {
            measuredNotes.insert(block.id);
            auto& measured = noteHeights[block.id];
            if (measured.revision != block.revision || measured.style != styleRevision || measured.width != width
                || !qFuzzyCompare(measured.dpr, editor->devicePixelRatioF())) {
                measured = {block.revision, styleRevision, width, editor->devicePixelRatioF(),
                    qMax<qreal>(52, BlockNoteStyle::measure(autoText, width - 40,
                        BlockNoteStyle::instance().appearance(block.appearance)) + 24)};
            }
            blockHeight = measured.height;
        }
        gaps.append({block.id, line, row, block.order, 0, blockHeight, hiddenAnchor});
    }
    for (auto it = noteHeights.begin(); it != noteHeights.end();)
        if (!measuredNotes.contains(it.key())) it = noteHeights.erase(it); else ++it;
    std::stable_sort(gaps.begin(), gaps.end(), [](const auto& a, const auto& b) { return a.row != b.row ? a.row < b.row : a.order < b.order; });
    int lastRow = -1; qreal within = 0;
    for (auto& gap : gaps) { if (gap.row != lastRow) within = 0; gap.offset = within; within += gap.height; lastRow = gap.row; }
    enabled = true; dirty = false; configureScrollBar(editor);
    if (!wasEnabled) editor->verticalScrollBar()->setValue(qRound(rowTop(qMax(0, rowForSource(previousSource)))));
}
int EditorBlockProjection::rowForSource(int source) const {
    const auto it = std::upper_bound(runs.cbegin(), runs.cend(), source, [](int line, const Run& run) { return line < run.source; });
    if (it == runs.cbegin()) return -1;
    const auto& run = *(it - 1); return source < run.source + run.rows ? run.firstRow + source - run.source : -1;
}
int EditorBlockProjection::sourceForRow(int row) const {
    const auto it = std::upper_bound(runs.cbegin(), runs.cend(), row, [](int value, const Run& run) { return value < run.firstRow; });
    if (it == runs.cbegin()) return -1;
    const auto& run = *(it - 1); return row < run.firstRow + run.rows ? run.source + row - run.firstRow : -1;
}
qreal EditorBlockProjection::rowTop(int row) const {
    qreal top = row * estimate;
    for (const auto& gap : gaps) { if (gap.row >= row) break; top += gap.height; }
    for (auto it = heights.cbegin(); it != heights.cend() && it.key() < row; ++it) top += it->height - estimate;
    return top;
}
qreal EditorBlockProjection::rowHeight(int row) const { const auto it = heights.constFind(row); return it == heights.cend() ? estimate : it->height; }
qreal EditorBlockProjection::height() const { return rowTop(count); }
int EditorBlockProjection::rowAtY(qreal y) const {
    int first = 0, last = count;
    while (first < last) { const int middle = (first + last) / 2; if (rowTop(middle) <= y) first = middle + 1; else last = middle; }
    return qBound(0, first - 1, qMax(0, count - 1));
}
qreal EditorBlockProjection::offset(MyCodeEditor* editor) const { return editor->verticalScrollBar()->value(); }
void EditorBlockProjection::configureScrollBar(MyCodeEditor* editor) const {
    if (!enabled) return;
    auto* bar = editor->verticalScrollBar(); const QSignalBlocker guard(bar);
    bar->setRange(0, qMax(0, qCeil(height() - editor->viewport()->height())));
    bar->setSingleStep(qMax(1, qRound(estimate))); bar->setPageStep(editor->viewport()->height());
}
void EditorBlockProjection::measureVisible(MyCodeEditor* editor) {
    const int first = rowAtY(offset(editor));
    const qreal bottom = offset(editor) + editor->viewport()->height() * 2;
    for (int row = first; row < count && rowTop(row) <= bottom; ++row) {
        const auto block = document->findBlockByNumber(sourceForRow(row));
        const auto previous = heights.constFind(row);
        if (previous != heights.cend() && previous->revision == block.revision()
            && block.layout() && block.layout()->lineCount() > 0) continue;
        const qreal value = qMax<qreal>(1, document->documentLayout()->blockBoundingRect(block).height());
        heights[row] = {value, block.revision()}; ++measured;
    }
    configureScrollBar(editor);
}
void EditorBlockProjection::ensureCursorVisible(MyCodeEditor* editor, bool center) const {
    const int row = rowForSource(editor->textCursor().blockNumber()); if (row < 0) return;
    qreal next = offset(editor), top = rowTop(row), bottom = top + rowHeight(row);
    if (center) next = top - editor->viewport()->height() / 2;
    else if (top < next) next = top;
    else if (bottom > next + editor->viewport()->height()) next = bottom - editor->viewport()->height();
    editor->verticalScrollBar()->setValue(qRound(next));
}
QRectF EditorBlockProjection::gapRect(MyCodeEditor* editor, const QString& id) const {
    for (const auto& gap : gaps) if (gap.id == id)
        return {4, rowTop(gap.row) + rowHeight(gap.row) + gap.offset - offset(editor), qreal(qMax(0, editor->viewport()->width() - 8)), gap.height};
    return {};
}
QList<QPair<QString,QRectF>> EditorBlockProjection::visibleGaps(MyCodeEditor* editor, int prefetch) const {
    QList<QPair<QString,QRectF>> result;
    for (const auto& gap : gaps) {
        const QRectF rect(4, rowTop(gap.row) + rowHeight(gap.row) + gap.offset - offset(editor), qMax(0, editor->viewport()->width() - 8), gap.height);
        if (rect.bottom() >= -prefetch && rect.top() <= editor->viewport()->height() + prefetch) result.append({gap.id, rect});
    }
    return result;
}
QString EditorBlockProjection::adjacentBlock(int sourceLine, bool forward) const {
    const int row = rowForSource(sourceLine) - (forward ? 0 : 1);
    QString result;
    for (const auto& gap : gaps) if (gap.row == row) { result = gap.id; if (forward) break; }
    return result;
}
QString EditorBlockProjection::neighboringBlock(const QString& id, bool forward) const {
    for (int i = 0; i < gaps.size(); ++i) if (gaps[i].id == id) {
        const int next = i + (forward ? 1 : -1);
        if (next >= 0 && next < gaps.size() && gaps[next].row == gaps[i].row) return gaps[next].id;
        break;
    }
    return {};
}
int EditorBlockProjection::sourceLineForBlock(const QString& id) const {
    for (const auto& gap : gaps) if (gap.id == id) return gap.sourceLine;
    return -1;
}
