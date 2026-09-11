#include "formattercursoranchor.h"

#include "mycodeeditor.h"
#include "triviapositionmap.h"

#include <QPlainTextEdit>
#include <QPoint>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>

#include <cmath>

namespace {
int documentLength(const QPlainTextEdit* editor)
{
    if (!editor || !editor->document())
        return 0;
    return qMax(0, editor->document()->characterCount() - 1);
}

FormatterScrollState captureScrollState(const QScrollBar* scrollBar)
{
    FormatterScrollState state;
    if (!scrollBar)
        return state;

    state.value = scrollBar->value();
    state.minimum = scrollBar->minimum();
    state.maximum = scrollBar->maximum();
    const int span = state.maximum - state.minimum;
    if (span > 0) {
        state.ratio =
            static_cast<double>(state.value - state.minimum)
            / static_cast<double>(span);
    }
    return state;
}

void restoreScrollState(QScrollBar* scrollBar,
                        const FormatterScrollState& state,
                        bool preferAbsoluteValue)
{
    if (!scrollBar)
        return;

    int value = state.value;
    const int newSpan = scrollBar->maximum() - scrollBar->minimum();
    const int oldSpan = state.maximum - state.minimum;
    if (!preferAbsoluteValue
        && (scrollBar->minimum() != state.minimum
            || scrollBar->maximum() != state.maximum)
        && newSpan > 0
        && oldSpan > 0) {
        value = scrollBar->minimum()
            + static_cast<int>(std::lround(
                state.ratio * static_cast<double>(newSpan)));
    }
    scrollBar->setValue(qBound(scrollBar->minimum(),
                               value,
                               scrollBar->maximum()));
}

FormatterLogicalPosition capturePosition(
    const FormatterPositionMapper& mapper,
    int position,
    FormatterPositionAffinity affinity)
{
    FormatterLogicalPosition logical =
        mapper.capturePosition(position, affinity);
    logical.affinity = affinity;
    if (logical.absoluteFallback < 0)
        logical.absoluteFallback = position;
    return logical;
}

int proportionalFallback(const FormatterLogicalPosition& logical,
                         int oldDocumentLength,
                         int newDocumentLength)
{
    if (logical.absoluteFallback < 0)
        return 0;
    if (oldDocumentLength <= 0) {
        return qBound(0,
                      logical.absoluteFallback,
                      newDocumentLength);
    }
    const double ratio =
        static_cast<double>(logical.absoluteFallback)
        / static_cast<double>(oldDocumentLength);
    return qBound(
        0,
        static_cast<int>(std::lround(
            ratio * static_cast<double>(newDocumentLength))),
        newDocumentLength);
}

int restorePosition(const FormatterPositionMapper& mapper,
                    const FormatterLogicalPosition& logical,
                    int oldDocumentLength,
                    int newDocumentLength,
                    bool* mapped)
{
    const int restored =
        mapper.restorePosition(logical, newDocumentLength);
    if (restored >= 0) {
        if (mapped)
            *mapped = true;
        return qBound(0, restored, newDocumentLength);
    }
    if (mapped)
        *mapped = false;
    return proportionalFallback(logical,
                                oldDocumentLength,
                                newDocumentLength);
}

int cursorTop(QPlainTextEdit* editor, int position)
{
    if (!editor || !editor->document())
        return 0;
    QTextCursor cursor(editor->document());
    cursor.setPosition(qBound(0,
                              position,
                              documentLength(editor)));
    if (auto* codeEditor = qobject_cast<MyCodeEditor*>(editor))
        return codeEditor->cursorRect(cursor).top();
    return editor->cursorRect(cursor).top();
}

QTextCursor cursorForEditorPosition(QPlainTextEdit* editor,
                                    const QPoint& point)
{
    if (auto* codeEditor = qobject_cast<MyCodeEditor*>(editor))
        return codeEditor->cursorForPosition(point);
    return editor ? editor->cursorForPosition(point) : QTextCursor();
}

void setEditorCursor(QPlainTextEdit* editor, const QTextCursor& cursor)
{
    if (auto* codeEditor = qobject_cast<MyCodeEditor*>(editor))
        codeEditor->setTextCursor(cursor);
    else if (editor)
        editor->setTextCursor(cursor);
}

bool alignTopVisiblePosition(QPlainTextEdit* editor,
                             int position,
                             int targetPixelOffset)
{
    if (!editor)
        return false;
    QScrollBar* scrollBar = editor->verticalScrollBar();
    if (!scrollBar)
        return false;

    const int boundedPosition =
        qBound(0, position, documentLength(editor));
    for (int attempt = 0; attempt < 4; ++attempt) {
        const int currentTop = cursorTop(editor, boundedPosition);
        const int pixelDelta = currentTop - targetPixelOffset;
        if (qAbs(pixelDelta) <= 1)
            return true;

        const int currentValue = scrollBar->value();
        int probeValue =
            currentValue + (pixelDelta > 0 ? 1 : -1);
        probeValue = qBound(scrollBar->minimum(),
                            probeValue,
                            scrollBar->maximum());
        if (probeValue == currentValue) {
            probeValue =
                qBound(scrollBar->minimum(),
                       currentValue + (pixelDelta > 0 ? -1 : 1),
                       scrollBar->maximum());
        }

        int desiredValue = currentValue;
        if (probeValue != currentValue) {
            scrollBar->setValue(probeValue);
            const int probeTop =
                cursorTop(editor, boundedPosition);
            scrollBar->setValue(currentValue);
            const double pixelsPerScrollUnit =
                static_cast<double>(currentTop - probeTop)
                / static_cast<double>(probeValue - currentValue);
            if (qAbs(pixelsPerScrollUnit) > 0.01) {
                desiredValue = currentValue
                    + static_cast<int>(std::lround(
                        static_cast<double>(pixelDelta)
                        / pixelsPerScrollUnit));
            }
        }

        if (desiredValue == currentValue) {
            const int currentVisibleBlock =
                cursorForEditorPosition(editor, QPoint(0, 0))
                    .blockNumber();
            QTextCursor targetCursor(editor->document());
            targetCursor.setPosition(boundedPosition);
            desiredValue = currentValue
                + targetCursor.blockNumber()
                - currentVisibleBlock;
        }

        desiredValue = qBound(scrollBar->minimum(),
                              desiredValue,
                              scrollBar->maximum());
        if (desiredValue == currentValue)
            break;
        scrollBar->setValue(desiredValue);
    }

    return qAbs(cursorTop(editor, boundedPosition)
                - targetPixelOffset)
        <= qMax(2, editor->fontMetrics().height());
}
}

bool FormatterLogicalPosition::hasTokenIdentity() const
{
    return !tokenIdentity.isEmpty() && tokenOrdinal >= 0;
}

FormatterTriviaPositionMapper::FormatterTriviaPositionMapper(
    const QString& oldText,
    const QString& newText)
    : positionMap(
          std::make_unique<TriviaPositionMap>(
              oldText,
              newText,
              TriviaPositionLeafPolicy::
                  IncludeComments))
{
}

FormatterTriviaPositionMapper::~FormatterTriviaPositionMapper() =
    default;

FormatterLogicalPosition
FormatterTriviaPositionMapper::capturePosition(
    int oldPosition,
    FormatterPositionAffinity affinity) const
{
    FormatterLogicalPosition logical;
    logical.absoluteFallback = oldPosition;
    logical.affinity = affinity;
    if (!positionMap)
        return logical;

    const TriviaLogicalPosition trivia =
        positionMap->capture(
            oldPosition,
            affinity == FormatterPositionAffinity::Trailing);
    logical.tokenIdentity = trivia.tokenIdentity;
    logical.tokenOrdinal = trivia.tokenOrdinal;
    logical.tokenOffset = trivia.tokenOffset;
    return logical;
}

int FormatterTriviaPositionMapper::restorePosition(
    const FormatterLogicalPosition& position,
    int newDocumentLength) const
{
    if (!positionMap
        || !positionMap->isCompatible()
        || position.absoluteFallback < 0) {
        return -1;
    }

    const bool endAffinity =
        position.affinity == FormatterPositionAffinity::Trailing;
    const TriviaLogicalPosition captured =
        positionMap->capture(position.absoluteFallback,
                             endAffinity);
    if (position.hasTokenIdentity()
        && (captured.tokenIdentity != position.tokenIdentity
            || captured.tokenOrdinal != position.tokenOrdinal
            || captured.tokenOffset != position.tokenOffset)) {
        return -1;
    }
    return qBound(
        0,
        positionMap->map(position.absoluteFallback,
                         endAffinity),
        newDocumentLength);
}

bool FormatterTriviaPositionMapper::isCompatible() const
{
    return positionMap && positionMap->isCompatible();
}

bool FormatterCursorAnchor::capture(
    QPlainTextEdit* editor,
    const FormatterPositionMapper& mapper)
{
    capturedState = FormatterCursorAnchorState();
    if (!editor || !editor->document())
        return false;

    const QTextCursor cursor = editor->textCursor();
    capturedState.oldDocumentLength = documentLength(editor);
    capturedState.hasSelection = cursor.hasSelection();

    FormatterPositionAffinity cursorAffinity =
        FormatterPositionAffinity::Leading;
    FormatterPositionAffinity anchorAffinity =
        FormatterPositionAffinity::Leading;
    if (cursor.hasSelection()) {
        if (cursor.position() < cursor.anchor()) {
            cursorAffinity = FormatterPositionAffinity::Leading;
            anchorAffinity = FormatterPositionAffinity::Trailing;
        } else {
            cursorAffinity = FormatterPositionAffinity::Trailing;
            anchorAffinity = FormatterPositionAffinity::Leading;
        }
    }

    capturedState.cursorPosition =
        capturePosition(mapper,
                        cursor.position(),
                        cursorAffinity);
    capturedState.selectionAnchor =
        capturePosition(mapper,
                        cursor.anchor(),
                        anchorAffinity);

    const QTextCursor topVisibleCursor =
        cursorForEditorPosition(editor, QPoint(0, 0));
    capturedState.topVisiblePosition =
        capturePosition(mapper,
                        topVisibleCursor.position(),
                        FormatterPositionAffinity::Leading);
    capturedState.topVisiblePixelOffset =
        cursorTop(editor, topVisibleCursor.position());
    capturedState.verticalScroll =
        captureScrollState(editor->verticalScrollBar());
    capturedState.horizontalScroll =
        captureScrollState(editor->horizontalScrollBar());
    capturedState.valid = true;
    return true;
}

FormatterCursorRestoreResult FormatterCursorAnchor::restore(
    QPlainTextEdit* editor,
    const FormatterPositionMapper& mapper) const
{
    FormatterCursorRestoreResult result;
    if (!capturedState.valid || !editor || !editor->document())
        return result;

    const int newDocumentLength = documentLength(editor);
    const int cursorPosition =
        restorePosition(mapper,
                        capturedState.cursorPosition,
                        capturedState.oldDocumentLength,
                        newDocumentLength,
                        &result.cursorMapped);
    const int selectionAnchor =
        restorePosition(mapper,
                        capturedState.selectionAnchor,
                        capturedState.oldDocumentLength,
                        newDocumentLength,
                        &result.selectionAnchorMapped);
    bool viewportPositionMapped = false;
    const int topVisiblePosition =
        restorePosition(mapper,
                        capturedState.topVisiblePosition,
                        capturedState.oldDocumentLength,
                        newDocumentLength,
                        &viewportPositionMapped);

    restoreScrollState(editor->verticalScrollBar(),
                       capturedState.verticalScroll,
                       false);
    restoreScrollState(editor->horizontalScrollBar(),
                       capturedState.horizontalScroll,
                       true);
    if (viewportPositionMapped) {
        result.viewportMapped =
            alignTopVisiblePosition(
                editor,
                topVisiblePosition,
                capturedState.topVisiblePixelOffset);
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(selectionAnchor);
    cursor.setPosition(cursorPosition, QTextCursor::KeepAnchor);
    setEditorCursor(editor, cursor);

    // setTextCursor can scroll to the active endpoint. Reapply the horizontal
    // offset and then align the logical top anchor once more.
    restoreScrollState(editor->horizontalScrollBar(),
                       capturedState.horizontalScroll,
                       true);
    if (viewportPositionMapped) {
        result.viewportMapped =
            alignTopVisiblePosition(
                editor,
                topVisiblePosition,
                capturedState.topVisiblePixelOffset);
    } else {
        restoreScrollState(editor->verticalScrollBar(),
                           capturedState.verticalScroll,
                           false);
    }
    return result;
}

bool FormatterCursorAnchor::isValid() const
{
    return capturedState.valid;
}

const FormatterCursorAnchorState& FormatterCursorAnchor::state() const
{
    return capturedState;
}
