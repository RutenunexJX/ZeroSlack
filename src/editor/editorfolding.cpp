#include "editorgutter.h"
#include <QCursor>
#include "editorfolding.h"

#include "insightvisualstyle.h"

#include "mycodeeditor.h"

#include <QApplication>
#include <QPainter>
#include <QPointer>
#include <QInputDialog>
#include <QLineEdit>
#include <QFontMetrics>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <limits>

namespace {
QColor alphaColor(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

bool betterFoldForLine(const TSFoldRange& candidate, const TSFoldRange& current)
{
    if (current.startLine < 0)
        return true;
    if (candidate.kind != current.kind)
        return candidate.kind == TSFoldRangeKind::Custom;
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

int fragmentEndColumn(const QString& text, int startColumn)
{
    const int lastNewline = text.lastIndexOf(QLatin1Char('\n'));
    return lastNewline < 0 ? startColumn + text.size()
                           : text.size() - lastNewline - 1;
}

int compareLineColumn(int leftLine,
                      int leftColumn,
                      int rightLine,
                      int rightColumn)
{
    if (leftLine != rightLine)
        return leftLine < rightLine ? -1 : 1;
    if (leftColumn == rightColumn)
        return 0;
    return leftColumn < rightColumn ? -1 : 1;
}

bool remapCustomMarker(TSCustomFoldMarker* marker,
                       const DocumentChange& change)
{
    if (!marker)
        return false;

    const int oldEndColumn = fragmentEndColumn(change.removedText,
                                               change.startColumn);
    const int newEndColumn = fragmentEndColumn(change.insertedText,
                                               change.startColumn);
    const int relativeToStart = compareLineColumn(marker->line,
                                                  marker->column,
                                                  change.startLine,
                                                  change.startColumn);
    if (relativeToStart < 0)
        return true;

    if (change.removedLength == 0) {
        if (marker->line == change.startLine) {
            marker->line = change.newEndLine;
            marker->column = newEndColumn
                + qMax(0, marker->column - change.startColumn);
        } else {
            marker->line = qMax(0, marker->line + change.lineDelta);
        }
        return true;
    }

    const int relativeToOldEnd = compareLineColumn(marker->line,
                                                   marker->column,
                                                   change.oldEndLine,
                                                   oldEndColumn);
    if (relativeToOldEnd < 0)
        return false;
    if (marker->line == change.oldEndLine) {
        marker->line = change.newEndLine;
        marker->column = newEndColumn
            + qMax(0, marker->column - oldEndColumn);
    } else {
        marker->line = qMax(0, marker->line + change.lineDelta);
    }
    return true;
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

QString foldExtentKey(const TSFoldRange& range)
{
    return QStringLiteral("%1:%2")
        .arg(range.startLine)
        .arg(range.endLine);
}

QList<TSFoldRange> customFoldRanges(
    QList<TSCustomFoldMarker> markers)
{
    std::sort(markers.begin(), markers.end(),
              [](const TSCustomFoldMarker& left,
                 const TSCustomFoldMarker& right) {
                  if (left.line != right.line)
                      return left.line < right.line;
                  return left.column < right.column;
              });
    QList<TSCustomFoldMarker> stack;
    QList<TSFoldRange> ranges;
    for (const TSCustomFoldMarker& marker : std::as_const(markers)) {
        if (marker.startsRange) {
            stack.append(marker);
            continue;
        }
        if (stack.isEmpty())
            continue;
        const TSCustomFoldMarker start = stack.takeLast();
        if (marker.line <= start.line)
            continue;
        TSFoldRange range;
        range.startLine = start.line;
        range.endLine = marker.line;
        range.kind = TSFoldRangeKind::Custom;
        range.label = start.label;
        ranges.append(range);
    }
    return ranges;
}
}

void EditorFoldingController::refresh(MyCodeEditor* editor, const TSDocument* document)
{
    if (!editor || !document)
        return;

    const QList<QPair<int, int>> previousCollapsedRanges =
        collapsedRangesCache;
    ranges = document->foldingRanges();
    customMarkers = document->customFoldMarkers();
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
        QList<TSCustomFoldMarker> remappedMarkers;
        remappedMarkers.reserve(customMarkers.size());
        for (TSCustomFoldMarker marker : std::as_const(customMarkers)) {
            if (remapCustomMarker(&marker, change))
                remappedMarkers.append(marker);
        }
        customMarkers = remappedMarkers;
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

        if (oldRange.kind == TSFoldRangeKind::Custom)
            continue;
        if (oldRange.endLine < change.startLine) {
            retainedSyntaxRanges.append(oldRange);
        } else if (rangeIsAfterOldChange(oldRange, change)) {
            retainedSyntaxRanges.append(
                remapRangeAfterOldChange(oldRange, change));
        }
    }

    QList<TSCustomFoldMarker> remappedMarkers;
    remappedMarkers.reserve(customMarkers.size());
    for (TSCustomFoldMarker marker : std::as_const(customMarkers)) {
        if (remapCustomMarker(&marker, change))
            remappedMarkers.append(marker);
    }
    customMarkers = remappedMarkers;

    customMarkers.erase(
        std::remove_if(customMarkers.begin(), customMarkers.end(),
                       [replacementFirstLine, replacementLastLine](
                           const TSCustomFoldMarker& marker) {
                           return marker.line >= replacementFirstLine
                               && marker.line <= replacementLastLine;
                       }),
        customMarkers.end());
    customMarkers.append(
        document->customFoldMarkersForChanges(changedRanges));

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
    ranges.append(customFoldRanges(customMarkers));

    QSet<QString> customExtents;
    for (const TSFoldRange& range : std::as_const(ranges)) {
        if (range.kind == TSFoldRangeKind::Custom)
            customExtents.insert(foldExtentKey(range));
    }
    ranges.erase(
        std::remove_if(ranges.begin(), ranges.end(),
                       [&customExtents](const TSFoldRange& range) {
                           return range.kind == TSFoldRangeKind::Syntax
                               && customExtents.contains(foldExtentKey(range));
                       }),
        ranges.end());
    std::sort(ranges.begin(), ranges.end(), [](const TSFoldRange& left,
                                               const TSFoldRange& right) {
        if (left.startLine != right.startLine)
            return left.startLine < right.startLine;
        if (left.kind != right.kind)
            return left.kind == TSFoldRangeKind::Custom;
        return left.endLine < right.endLine;
    });
    ranges.erase(std::unique(ranges.begin(), ranges.end(),
                             [](const TSFoldRange& left,
                                const TSFoldRange& right) {
                                 return left.startLine == right.startLine
                                     && left.endLine == right.endLine
                                     && left.kind == right.kind;
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
    customMarkers.clear();
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
    return !collapsedStartLines.isEmpty()
        || !customMarkers.isEmpty()
        || (foldRegionMarkModeActive()
            && (foldRegionHoverLine >= 0 || pendingStartLine >= 0));
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

void EditorFoldingController::bindModeController(
    EditorModeController* controller,
    MyCodeEditor* editor)
{
    modeController = controller;
    if (!modeController)
        return;

    const QPointer<MyCodeEditor> target(editor);
    modeController->setExitHandler(
        EditorModeId::FoldRegion,
        [this, target](EditorModeExitReason) {
            resetFoldRegionMode(target);
        });
}

void EditorFoldingController::startFoldRegionMarkMode(MyCodeEditor* editor)
{
    if (modeController
        && !modeController->enter(
            EditorModeId::FoldRegion,
            EditorModeEntryReason::UserAction)) {
        return;
    }
    markMode = FoldRegionMarkMode::WaitingForStart;
    pendingStartLine = -1;
    foldRegionHoverLine = -1;
    updateStatus(editor, QStringLiteral("Fold region: click start line"));
    if (editor)
        editor->viewport()->update();
}

void EditorFoldingController::cancelFoldRegionMarkMode(MyCodeEditor* editor)
{
    if (modeController
        && modeController->isActive(EditorModeId::FoldRegion)) {
        modeController->exit(EditorModeId::FoldRegion,
                             EditorModeExitReason::Canceled);
        return;
    }
    resetFoldRegionMode(editor);
}

void EditorFoldingController::resetFoldRegionMode(MyCodeEditor* editor)
{
    markMode = FoldRegionMarkMode::Inactive;
    pendingStartLine = -1;
    foldRegionHoverLine = -1;
    updateStatus(editor, QString());
    if (editor)
        editor->viewport()->update();
}

bool EditorFoldingController::foldRegionMarkModeActive() const
{
    return modeController
        ? modeController->isActive(EditorModeId::FoldRegion)
        : markMode != FoldRegionMarkMode::Inactive;
}

bool EditorFoldingController::handleFoldRegionHoverLine(
    MyCodeEditor* editor,
    int line)
{
    if (!foldRegionMarkModeActive() || line < 0)
        return false;

    if (foldRegionHoverLine == line)
        return true;

    foldRegionHoverLine = line;
    if (editor)
        editor->viewport()->update();
    return true;
}

bool EditorFoldingController::handleFoldRegionMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!foldRegionMarkModeActive() || !editor || !event)
        return false;

    const int line =
        editor->cursorForPosition(event->position().toPoint()).blockNumber();
    return handleFoldRegionHoverLine(editor, line);
}

bool EditorFoldingController::handleFoldRegionGutterLine(
    MyCodeEditor* editor,
    int line)
{
    if (!foldRegionMarkModeActive() || line < 0)
        return false;

    if (markMode == FoldRegionMarkMode::WaitingForStart) {
        pendingStartLine = line;
        foldRegionHoverLine = line;
        markMode = FoldRegionMarkMode::WaitingForEnd;
        updateStatus(editor, QStringLiteral("Fold region: click end line"));
        if (editor)
            editor->viewport()->update();
        return true;
    }

    int startLine = pendingStartLine;
    int endLine = line;
    if (endLine < startLine)
        std::swap(startLine, endLine);

    bool accepted = false;
    const QString suggestedAlias = defaultAlias();
    const QString alias = QInputDialog::getText(
        editor,
        QStringLiteral("Fold Region"),
        QStringLiteral("Alias"),
        QLineEdit::Normal,
        suggestedAlias,
        &accepted);
    if (!accepted) {
        cancelFoldRegionMarkMode(editor);
        return true;
    }

    insertCustomFoldMarkers(editor,
                            startLine,
                            endLine,
                            alias.isEmpty() ? suggestedAlias : alias);
    if (modeController
        && modeController->isActive(EditorModeId::FoldRegion)) {
        modeController->exit(EditorModeId::FoldRegion,
                             EditorModeExitReason::Completed);
    } else {
        resetFoldRegionMode(editor);
    }
    return true;
}

bool EditorFoldingController::insertCustomFoldMarkers(
    MyCodeEditor* editor,
    int startLine,
    int endLine,
    const QString& alias)
{
    if (!editor || startLine < 0 || endLine < 0)
        return false;
    if (endLine < startLine)
        std::swap(startLine, endLine);

    QTextDocument* doc = editor->document();
    QTextBlock startBlock = doc->findBlockByNumber(startLine);
    QTextBlock endBlock = doc->findBlockByNumber(endLine);
    if (!startBlock.isValid() || !endBlock.isValid())
        return false;

    QString finalAlias = alias.trimmed();
    if (finalAlias.isEmpty())
        finalAlias = defaultAlias();

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    cursor.setPosition(endBlock.position() + endBlock.length() - 1);
    cursor.insertText(QStringLiteral("\n// endfold"));
    cursor.setPosition(startBlock.position());
    cursor.insertText(QStringLiteral("// fold %1\n").arg(finalAlias));
    cursor.endEditBlock();
    return true;
}

bool EditorFoldingController::deleteCustomFoldAtLine(
    MyCodeEditor* editor,
    int line)
{
    return deleteRange(editor, customFoldContainingLine(line));
}

void EditorFoldingController::applyVisibility(MyCodeEditor* editor)
{
    if (!editor)
        return;

    applyVisibilityForLines(editor,
                            0,
                            qMax(0, editor->document()->blockCount() - 1));
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
        if (markMode == FoldRegionMarkMode::WaitingForEnd
            && line == pendingStartLine) {
            painter.save();
            painter.setPen(InsightVisualStyle::theme().accent);
            painter.drawLine(1, top + 1, 1, bottom - 1);
            painter.restore();
        }
        if (foldRegionMarkModeActive()
            && (line == foldRegionHoverLine
                || line == pendingStartLine)) {
            const bool isPendingStart =
                line == pendingStartLine
                && markMode == FoldRegionMarkMode::WaitingForEnd;
            const bool isHoverEnd =
                line == foldRegionHoverLine
                && markMode == FoldRegionMarkMode::WaitingForEnd
                && line != pendingStartLine;
            const QString badge = isPendingStart
                ? QStringLiteral("1")
                : (isHoverEnd ? QStringLiteral("E") : QStringLiteral("S"));
            const QColor badgeColor = isPendingStart
                ? InsightVisualStyle::theme().accent
                : InsightVisualStyle::theme().semantic.read;
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(badgeColor);
            const QRect badgeRect(1, top + 2, 12, qMax(12, bottom - top - 4));
            painter.drawRoundedRect(badgeRect, 4, 4);
            painter.setPen(
                InsightVisualStyle::theme().button.textChecked);
            painter.drawText(badgeRect, Qt::AlignCenter, badge);
            painter.restore();
        }
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

    if (collapsedStartLines.isEmpty()) {
        paintCustomFoldBackgrounds(editor, painter);
        paintFoldRegionPreview(editor, painter);
        return;
    }

    painter.save();
    paintCustomFoldBackgrounds(editor, painter);
    paintFoldRegionPreview(editor, painter);
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
        const QString label = range.kind == TSFoldRangeKind::Custom
            ? QStringLiteral(" ... fold: %1")
                  .arg(range.label.isEmpty()
                           ? QStringLiteral("fold block")
                           : range.label)
            : QStringLiteral(" ...");
        const int textWidth = metrics.horizontalAdvance(block.text());
        const int x = static_cast<int>(rect.left()) + textWidth + 14;
        const int y = static_cast<int>(rect.top())
            + metrics.ascent()
            + qMax(0, static_cast<int>(rect.height()) - metrics.height()) / 2;
        painter.drawText(x, y, label);
    }
    painter.restore();
}

void EditorFoldingController::paintCustomFoldBackgrounds(
    MyCodeEditor* editor,
    QPainter& painter) const
{
    if (!editor)
        return;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(alphaColor(
        InsightVisualStyle::theme().warning, 26));
    for (const TSFoldRange& range : ranges) {
        if (range.kind != TSFoldRangeKind::Custom
            || collapsedStartLines.contains(range.startLine)) {
            continue;
        }
        QRectF firstRect;
        QRectF lastRect;
        for (int line = range.startLine; line <= range.endLine; ++line) {
            QTextBlock block = editor->document()->findBlockByNumber(line);
            if (!block.isValid()
                || !editor->sourceLineVisible(block.blockNumber()))
                continue;
            const QRectF rect =
                editor->blockBoundingGeometry(block).translated(editor->contentOffset());
            if (rect.bottom() < 0 || rect.top() > editor->viewport()->height())
                continue;
            painter.drawRect(QRectF(0, rect.top(), editor->viewport()->width(), rect.height()));
            if (!firstRect.isValid())
                firstRect = rect;
            lastRect = rect;
        }
        if (firstRect.isValid() && lastRect.isValid()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(alphaColor(
                InsightVisualStyle::theme().warning, 120), 1));
            const qreal topY = qBound<qreal>(0,
                                             firstRect.top() + 1,
                                             editor->viewport()->height() - 1);
            const qreal bottomY = qBound<qreal>(0,
                                                lastRect.bottom() - 1,
                                                editor->viewport()->height() - 1);
            painter.drawLine(QPointF(0, topY),
                             QPointF(editor->viewport()->width(), topY));
            painter.drawLine(QPointF(0, bottomY),
                             QPointF(editor->viewport()->width(), bottomY));
            painter.setPen(Qt::NoPen);
            painter.setBrush(alphaColor(
                InsightVisualStyle::theme().warning, 26));
        }
    }
    painter.restore();
}

void EditorFoldingController::paintFoldRegionPreview(
    MyCodeEditor* editor,
    QPainter& painter) const
{
    if (!editor || !foldRegionMarkModeActive())
        return;

    int startLine = -1;
    int endLine = -1;
    if (markMode == FoldRegionMarkMode::WaitingForStart) {
        startLine = foldRegionHoverLine;
        endLine = foldRegionHoverLine;
    } else if (pendingStartLine >= 0) {
        startLine = pendingStartLine;
        endLine = foldRegionHoverLine >= 0
            ? foldRegionHoverLine
            : pendingStartLine;
        if (endLine < startLine)
            std::swap(startLine, endLine);
    }
    if (startLine < 0 || endLine < startLine)
        return;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(alphaColor(
        InsightVisualStyle::theme().semantic.read, 34));
    QRectF firstRect;
    QRectF lastRect;
    for (int line = startLine; line <= endLine; ++line) {
        QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid()
            || !editor->sourceLineVisible(block.blockNumber()))
            continue;
        const QRectF rect =
            editor->blockBoundingGeometry(block).translated(editor->contentOffset());
        if (rect.bottom() < 0 || rect.top() > editor->viewport()->height())
            continue;
        painter.drawRect(QRectF(0, rect.top(), editor->viewport()->width(), rect.height()));
        if (!firstRect.isValid())
            firstRect = rect;
        lastRect = rect;
    }
    if (firstRect.isValid() && lastRect.isValid()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(alphaColor(
            InsightVisualStyle::theme().semantic.read, 150), 2));
        const qreal topY = qBound<qreal>(0,
                                         firstRect.top() + 1,
                                         editor->viewport()->height() - 1);
        const qreal bottomY = qBound<qreal>(0,
                                            lastRect.bottom() - 1,
                                            editor->viewport()->height() - 1);
        painter.drawLine(QPointF(0, topY),
                         QPointF(editor->viewport()->width(), topY));
        painter.drawLine(QPointF(0, bottomY),
                         QPointF(editor->viewport()->width(), bottomY));
    }
    painter.restore();
}

void EditorFoldingController::updateStatus(
    MyCodeEditor* editor,
    const QString& message) const
{
    if (modeController
        && modeController->isActive(EditorModeId::FoldRegion)
        && !message.isEmpty()) {
        modeController->updatePresentation(
            EditorModeId::FoldRegion,
            message,
            QStringLiteral("Click gutter lines to define the region; "
                           "Esc cancels"));
    }
    if (editor)
        emit editor->editorStatusMessageRequested(message);
}

QString EditorFoldingController::defaultAlias()
{
    return QStringLiteral("fold_block_%1").arg(defaultAliasCounter++);
}

TSFoldRange EditorFoldingController::customFoldContainingLine(int line) const
{
    TSFoldRange best;
    int bestSpan = std::numeric_limits<int>::max();
    for (const TSFoldRange& range : ranges) {
        if (range.kind != TSFoldRangeKind::Custom
            || line < range.startLine
            || line > range.endLine) {
            continue;
        }
        const int span = range.endLine - range.startLine;
        if (span < bestSpan) {
            best = range;
            bestSpan = span;
        }
    }
    return best;
}

QString EditorFoldingController::rangeText(
    MyCodeEditor* editor,
    const TSFoldRange& range) const
{
    if (!editor || range.startLine < 0 || range.endLine < range.startLine)
        return QString();

    QTextBlock startBlock =
        editor->document()->findBlockByNumber(range.startLine);
    QTextBlock endBlock =
        editor->document()->findBlockByNumber(range.endLine);
    if (!startBlock.isValid() || !endBlock.isValid())
        return QString();

    QTextCursor cursor(editor->document());
    cursor.setPosition(startBlock.position());
    const int endPosition =
        qMin(editor->document()->characterCount() - 1,
             endBlock.position() + endBlock.length());
    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
    return cursor.selectedText().replace(QChar::ParagraphSeparator,
                                         QLatin1Char('\n'));
}

bool EditorFoldingController::deleteRange(
    MyCodeEditor* editor,
    const TSFoldRange& range)
{
    if (!editor || range.startLine < 0 || range.endLine < range.startLine)
        return false;

    QTextBlock startBlock =
        editor->document()->findBlockByNumber(range.startLine);
    QTextBlock endBlock =
        editor->document()->findBlockByNumber(range.endLine);
    if (!startBlock.isValid() || !endBlock.isValid())
        return false;

    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(startBlock.position());
    const int endPosition =
        qMin(editor->document()->characterCount() - 1,
             endBlock.position() + endBlock.length());
    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    return true;
}
