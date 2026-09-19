#include "editorgutter.h"
#include <QCursor>
#include "editorfolding.h"

#include "insightvisualstyle.h"

#include "mycodeeditor.h"

#include <QPainter>
#include <QFontMetrics>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>

namespace {
bool betterFoldForLine(const TSFoldRange& candidate, const TSFoldRange& current)
{
    if (current.startLine < 0)
        return true;
    return candidate.endLine > current.endLine;
}

struct FoldLineMapping {
    int line = -1;
    bool survives = false;
};

FoldLineMapping remappedFoldLine(int line, const DocumentChange& change)
{
    if (change.removedLength > 0
        && line > change.startLine
        && line < change.oldEndLine) {
        return {};
    }
    if (line > change.oldEndLine)
        return {qMax(0, line + change.lineDelta), true};
    if (line == change.oldEndLine && change.oldEndLine > change.startLine) {
        return {qMax(change.startLine, line + change.lineDelta), true};
    }
    if (line == change.startLine && change.startColumn == 0
        && change.removedLength == 0 && change.lineDelta > 0) {
        return {line + change.lineDelta, true};
    }
    return {line, true};
}

bool foldRangeFullyDeleted(const TSFoldRange& range,
                           const DocumentChange& change)
{
    if (change.removedLength <= 0
        || change.oldEndLine <= change.startLine) {
        return false;
    }
    const bool startsInsideDeletedLines =
        range.startLine > change.startLine
        || (range.startLine == change.startLine
            && change.startColumn == 0);
    return startsInsideDeletedLines
        && range.endLine < change.oldEndLine;
}

bool rangeIsAfterOldChange(const TSFoldRange& range,
                           const DocumentChange& change)
{
    if (change.oldEndLine > change.startLine)
        return range.startLine >= change.oldEndLine;
    return range.startLine > change.startLine;
}

TSFoldRange remapRangeAfterOldChange(TSFoldRange range,
                                     const DocumentChange& change)
{
    range.startLine = qMax(0, range.startLine + change.lineDelta);
    range.endLine = qMax(range.startLine,
                         range.endLine + change.lineDelta);
    return range;
}

bool rangeTouchesLines(const TSFoldRange& range, int firstLine, int lastLine)
{
    return range.endLine >= firstLine && range.startLine <= lastLine;
}

}

void EditorFoldingController::refresh(MyCodeEditor* editor, const TSDocument* document)
{
    if (!editor || !document)
        return;

    const QList<QPair<int, int>> previousCollapsedRanges =
        collapsedRangesCache;
    ranges = document->foldingRanges();
    QSet<int> validStarts;
    for (const TSFoldRange& range : std::as_const(ranges))
        validStarts.insert(range.startLine);
    for (auto it = collapsedStartLines.begin(); it != collapsedStartLines.end();) {
        if (!validStarts.contains(*it))
            it = collapsedStartLines.erase(it);
        else
            ++it;
    }
    rebuildCollapsedRangesCache();
    if (collapsedRangesCache != previousCollapsedRanges)
        markPresentationChanged(editor);
}

bool EditorFoldingController::applyDocumentChange(
    MyCodeEditor* editor,
    const TSDocument* document,
    const DocumentChange& change,
    const QList<TSChangedRange>& changedRanges)
{
    if (!editor || !document)
        return false;

    const QList<QPair<int, int>> previousCollapsedRanges =
        collapsedRangesCache;

    // Inline candidate text is a transient, single-line non-language
    // overlay. Its edited Tree-sitter snapshot remains positional only until
    // the session ends, so existing fold facts are authoritative during the
    // overlay. Re-querying that intentionally unparsed tree can select the
    // root node and turn a local keystroke into a full-tree traversal.
    if (document->hasDeferredSyntaxEdits()
        && change.lineDelta == 0) {
        return false;
    }

    const bool hadCollapsedRanges = !collapsedStartLines.isEmpty();
    int replacementFirstLine = qMin(change.startLine, change.newEndLine);
    int replacementLastLine = qMax(change.startLine, change.newEndLine);
    for (const TSChangedRange& range : changedRanges) {
        replacementFirstLine = qMin(replacementFirstLine, range.startLine);
        replacementLastLine = qMax(replacementLastLine, range.endLine);
    }
    int firstLine = replacementFirstLine;
    int lastLine = replacementLastLine;
    QSet<int> collapsedCandidates;
    QList<TSFoldRange> retainedSyntaxRanges;
    retainedSyntaxRanges.reserve(ranges.size());
    for (const TSFoldRange& oldRange : std::as_const(ranges)) {
        const bool wasCollapsed =
            collapsedStartLines.contains(oldRange.startLine);
        const bool fullyDeleted = foldRangeFullyDeleted(oldRange, change);
        if (wasCollapsed) {
            const FoldLineMapping mappedStart =
                remappedFoldLine(oldRange.startLine, change);
            const FoldLineMapping mappedEnd =
                remappedFoldLine(oldRange.endLine, change);
            firstLine = qMin(firstLine,
                             mappedStart.survives
                                 ? mappedStart.line
                                 : change.startLine);
            lastLine = qMax(lastLine,
                            mappedEnd.survives
                                ? mappedEnd.line
                                : change.newEndLine);
            if (!fullyDeleted && mappedStart.survives)
                collapsedCandidates.insert(mappedStart.line);
        }

        if (oldRange.endLine < change.startLine) {
            retainedSyntaxRanges.append(oldRange);
        } else if (rangeIsAfterOldChange(oldRange, change)) {
            retainedSyntaxRanges.append(
                remapRangeAfterOldChange(oldRange, change));
        }
    }

    retainedSyntaxRanges.erase(
        std::remove_if(retainedSyntaxRanges.begin(),
                       retainedSyntaxRanges.end(),
                       [replacementFirstLine,
                        replacementLastLine](const TSFoldRange& range) {
                           return rangeTouchesLines(range,
                                                    replacementFirstLine,
                                                    replacementLastLine);
                       }),
        retainedSyntaxRanges.end());
    ranges = retainedSyntaxRanges;
    ranges.append(document->syntaxFoldingRangesForChanges(changedRanges));
    std::sort(ranges.begin(), ranges.end(), [](const TSFoldRange& left,
                                               const TSFoldRange& right) {
        if (left.startLine != right.startLine)
            return left.startLine < right.startLine;
        return left.endLine < right.endLine;
    });
    ranges.erase(std::unique(ranges.begin(), ranges.end(),
                             [](const TSFoldRange& left,
                                const TSFoldRange& right) {
                                 return left.startLine == right.startLine
                                     && left.endLine == right.endLine;
                             }),
                 ranges.end());

    QSet<int> validStarts;
    for (const TSFoldRange& range : std::as_const(ranges))
        validStarts.insert(range.startLine);
    collapsedCandidates.intersect(validStarts);
    collapsedStartLines = collapsedCandidates;
    for (const TSFoldRange& range : std::as_const(ranges)) {
        if (!collapsedStartLines.contains(range.startLine))
            continue;
        firstLine = qMin(firstLine, range.startLine);
        lastLine = qMax(lastLine, range.endLine);
    }
    if (!hadCollapsedRanges)
        return false;
    rebuildCollapsedRangesCache();
    if (collapsedRangesCache != previousCollapsedRanges) {
        applyVisibilityForLines(
            editor,
            qMax(0, firstLine - 1),
            qMin(qMax(0, editor->document()->blockCount() - 1),
                 lastLine + 1));
    } else {
        editor->viewport()->update();
    }
    return false;
}

bool EditorFoldingController::hasFoldAtLine(int line) const
{
    return foldAtLine(line).startLine >= 0;
}

bool EditorFoldingController::isCollapsedAtLine(int line) const
{
    return collapsedStartLines.contains(line);
}

bool EditorFoldingController::isLineVisible(int line) const
{
    if (line < 0)
        return false;
    for (const TSFoldRange& range : ranges) {
        if (collapsedStartLines.contains(range.startLine)
            && line > range.startLine
            && line <= range.endLine) {
            return false;
        }
    }
    return true;
}

void EditorFoldingController::revealLine(MyCodeEditor* editor, int line)
{
    bool changed = false;
    for (auto it = collapsedStartLines.begin();
         it != collapsedStartLines.end();) {
        const TSFoldRange range = foldAtLine(*it);
        if (range.startLine >= 0
            && line > range.startLine
            && line <= range.endLine) {
            it = collapsedStartLines.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        markPresentationChanged(editor);
}

EditorFoldViewState EditorFoldingController::captureViewState(
    MyCodeEditor* editor) const
{
    EditorFoldViewState state;
    if (!editor || !editor->document())
        return state;

    QList<int> starts = collapsedStartLines.values();
    std::sort(starts.begin(), starts.end());
    QTextDocument* document = editor->document();
    for (int startLine : std::as_const(starts)) {
        const TSFoldRange range = foldAtLine(startLine);
        const QTextBlock startBlock =
            document->findBlockByNumber(range.startLine);
        const QTextBlock endBlock =
            document->findBlockByNumber(range.endLine);
        if (range.startLine < 0
            || range.endLine <= range.startLine
            || !startBlock.isValid()
            || !endBlock.isValid()) {
            continue;
        }
        EditorFoldAnchorState fold;
        fold.startAnchor = QTextCursor(document);
        fold.startAnchor.setPosition(startBlock.position());
        fold.endAnchor = QTextCursor(document);
        fold.endAnchor.setPosition(
            endBlock.position() + qMax(0, endBlock.length() - 1));
        fold.endAnchor.setKeepPositionOnInsert(true);
        fold.fallbackStartLine = range.startLine;
        fold.fallbackEndLine = range.endLine;
        state.collapsedRanges.append(fold);
    }
    return state;
}

void EditorFoldingController::restoreViewState(
    MyCodeEditor* editor,
    const EditorFoldViewState& state)
{
    collapsedStartLines.clear();
    if (!editor || !editor->document()) {
        markPresentationChanged(editor);
        return;
    }

    QTextDocument* document = editor->document();
    for (const EditorFoldAnchorState& fold : state.collapsedRanges) {
        int startLine = fold.fallbackStartLine;
        int endLine = fold.fallbackEndLine;
        if (!fold.startAnchor.isNull()
            && fold.startAnchor.document() == document) {
            startLine = fold.startAnchor.blockNumber();
        }
        if (!fold.endAnchor.isNull()
            && fold.endAnchor.document() == document) {
            endLine = fold.endAnchor.blockNumber();
        }

        TSFoldRange best;
        for (const TSFoldRange& candidate : std::as_const(ranges)) {
            if (candidate.startLine != startLine)
                continue;
            if (best.startLine < 0
                || qAbs(candidate.endLine - endLine)
                       < qAbs(best.endLine - endLine)) {
                best = candidate;
            }
        }
        if (best.startLine >= 0 && best.endLine > best.startLine)
            collapsedStartLines.insert(best.startLine);
    }
    const int cursorLine = editor->textCursor().blockNumber();
    if (!isLineVisible(cursorLine)) {
        for (const TSFoldRange& range : std::as_const(ranges)) {
            if (!collapsedStartLines.contains(range.startLine)
                || cursorLine <= range.startLine
                || cursorLine > range.endLine) {
                continue;
            }
            const QTextBlock startBlock =
                document->findBlockByNumber(range.startLine);
            if (startBlock.isValid()) {
                editor->QPlainTextEdit::setTextCursor(
                    QTextCursor(startBlock));
            }
            break;
        }
    }
    markPresentationChanged(editor);
}

void EditorFoldingController::resetForDocumentChange(
    MyCodeEditor* editor)
{
    ranges.clear();
    collapsedStartLines.clear();
    markPresentationChanged(editor);
}

const QList<QPair<int, int>>&
EditorFoldingController::collapsedLineRanges() const
{
    return collapsedRangesCache;
}

bool EditorFoldingController::hasPaintOverlay() const
{
    return !collapsedStartLines.isEmpty();
}

void EditorFoldingController::rebuildCollapsedRangesCache()
{
    QList<QPair<int, int>> result;
    for (int startLine : collapsedStartLines) {
        const TSFoldRange range = foldAtLine(startLine);
        if (range.startLine >= 0 && range.endLine > range.startLine)
            result.append(qMakePair(range.startLine, range.endLine));
    }
    std::sort(result.begin(), result.end());
    QList<QPair<int, int>> normalized;
    normalized.reserve(result.size());
    for (const QPair<int, int>& range : std::as_const(result)) {
        if (normalized.isEmpty()
            || range.first > normalized.last().second) {
            normalized.append(range);
        } else {
            normalized.last().second = qMax(
                normalized.last().second, range.second);
        }
    }
    collapsedRangesCache = std::move(normalized);
}

void EditorFoldingController::markPresentationChanged(
    MyCodeEditor* editor)
{
    rebuildCollapsedRangesCache();
    if (!editor)
        return;
    editor->invalidateViewProjection();
    editor->viewport()->update();
}

TSFoldRange EditorFoldingController::foldAtLine(int line) const
{
    TSFoldRange best;
    for (const TSFoldRange& range : ranges) {
        if (range.startLine == line && betterFoldForLine(range, best))
            best = range;
    }
    return best;
}

bool EditorFoldingController::toggleFoldAtLine(MyCodeEditor* editor, int line)
{
    const TSFoldRange range = foldAtLine(line);
    if (range.startLine < 0 || range.endLine <= range.startLine)
        return false;

    if (collapsedStartLines.contains(range.startLine)) {
        collapsedStartLines.remove(range.startLine);
    } else {
        collapsedStartLines.insert(range.startLine);
        const QTextCursor current = editor->textCursor();
        if (current.blockNumber() > range.startLine
            && current.blockNumber() <= range.endLine) {
            QTextBlock startBlock = editor->document()
                                        ->findBlockByNumber(range.startLine);
            if (startBlock.isValid())
                editor->QPlainTextEdit::setTextCursor(QTextCursor(startBlock));
        }
    }
    applyVisibilityForLines(editor, range.startLine, range.endLine);
    return true;
}

void EditorFoldingController::applyVisibilityForLines(
    MyCodeEditor* editor,
    int startLine,
    int endLine)
{
    Q_UNUSED(startLine)
    Q_UNUSED(endLine)
    markPresentationChanged(editor);
}

void EditorFoldingController::paintGutter(
    MyCodeEditor* editor,
    QPainter& painter,
    const QRect& rect) const
{
    if (!editor)
        return;

    painter.save();
    painter.translate(EditorGutter::foldLeft(editor), 0);
    QTextBlock block = editor->firstVisibleBlock();
    int top = static_cast<int>(editor->blockBoundingGeometry(block)
                                   .translated(editor->contentOffset())
                                   .top());
    int bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    while (block.isValid() && top <= rect.bottom()) {
        const int line = block.blockNumber();
        if (hasFoldAtLine(line) && (isCollapsedAtLine(line)
            || editor->rect().contains(editor->mapFromGlobal(QCursor::pos())))) {
            const bool collapsed = isCollapsedAtLine(line);
            const int midY = top + (bottom - top) / 2;
            QPolygon triangle;
            if (collapsed) {
                triangle << QPoint(5, midY - 4)
                         << QPoint(5, midY + 4)
                         << QPoint(10, midY);
            } else {
                triangle << QPoint(4, midY - 3)
                         << QPoint(11, midY - 3)
                         << QPoint(7, midY + 4);
            }
            painter.save();
            painter.setBrush(
                InsightVisualStyle::theme().textMuted);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle);
            painter.restore();
        }

        block = editor->nextVisibleBlock(block);
        top = bottom;
        bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    }
    painter.restore();
}

void EditorFoldingController::paintPlaceholders(
    MyCodeEditor* editor,
    QPainter& painter) const
{
    if (!editor)
        return;
    if (!hasPaintOverlay()) {
        return;
    }

    painter.save();
    painter.setPen(InsightVisualStyle::theme().textMuted);
    const QFontMetrics metrics(editor->font());
    for (int startLine : collapsedStartLines) {
        const TSFoldRange range = foldAtLine(startLine);
        if (range.startLine < 0)
            continue;
        QTextBlock block = editor->document()->findBlockByNumber(startLine);
        if (!block.isValid()
            || !editor->sourceLineVisible(block.blockNumber()))
            continue;
        const QRectF rect = editor->blockBoundingGeometry(block)
                                .translated(editor->contentOffset());
        if (rect.bottom() < 0 || rect.top() > editor->viewport()->height())
            continue;
        const QString label = QStringLiteral(" ...");
        const int textWidth = metrics.horizontalAdvance(block.text());
        const int x = static_cast<int>(rect.left()) + textWidth + 14;
        const int y = static_cast<int>(rect.top())
            + metrics.ascent()
            + qMax(0, static_cast<int>(rect.height()) - metrics.height()) / 2;
        painter.drawText(x, y, label);
    }
    painter.restore();
}
