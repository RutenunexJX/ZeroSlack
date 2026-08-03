#include "editorcolumnmodecontroller.h"

#include "annotationlayer.h"
#include "editormodecontroller.h"
#include "mycodeeditor.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <utility>

struct EditorColumnModeController::State
{
    EditorColumnModeController* owner = nullptr;
    EditorModeController* modes = nullptr;
    AnnotationLayer* annotations = nullptr;
    bool columnSelectionDragging = false;
    bool columnSelectionAwaitingEndpoint = false;
    bool columnSelectionDragMoved = false;
    int columnAnchorLine = -1;
    int columnAnchorColumn = -1;
    int columnCurrentLine = -1;
    int columnCurrentColumn = -1;
    int virtualCursorLine = -1;
    int virtualCursorColumn = -1;
    int virtualCursorSavedWidth = 1;
    quint64 presentationGeneration = 0;
};

namespace {
constexpr int kManualIndentWidth = 4;
constexpr const char* kColumnCaretAnnotationSource =
    "column-carets";

bool hasColumnSelection(const EditorColumnModeController::State& state)
{
    return state.modes
        && state.modes->isActive(EditorModeId::ColumnSelection)
        && state.columnAnchorLine >= 0
        && state.columnCurrentLine >= 0
        && state.columnAnchorColumn >= 0
        && state.columnCurrentColumn >= 0;
}

QPair<int, int> lineSpan(const EditorColumnModeController::State& state)
{
    return {qMin(state.columnAnchorLine, state.columnCurrentLine),
            qMax(state.columnAnchorLine, state.columnCurrentLine)};
}

QPair<int, int> columnSpan(const EditorColumnModeController::State& state)
{
    return {qMin(state.columnAnchorColumn, state.columnCurrentColumn),
            qMax(state.columnAnchorColumn, state.columnCurrentColumn)};
}

QStringList normalizedClipboardRows(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList rows = text.split(QLatin1Char('\n'));
    if (rows.size() > 1 && rows.last().isEmpty())
        rows.removeLast();
    return rows;
}

int editorTabStopColumns(const MyCodeEditor* editor)
{
    if (!editor)
        return kManualIndentWidth;
    const QFontMetrics metrics(editor->font());
    const int spaceWidth = qMax(1, metrics.horizontalAdvance(QLatin1Char(' ')));
    return qMax(1, qRound(editor->tabStopDistance() / spaceWidth));
}

int visualAdvanceForChar(QChar ch, int visualColumn, int tabWidth)
{
    if (ch == QLatin1Char('\t')) {
        const int remainder = visualColumn % qMax(1, tabWidth);
        return remainder == 0 ? qMax(1, tabWidth) : qMax(1, tabWidth) - remainder;
    }
    return 1;
}

int visualColumnForOffset(const QString& text, int offset, int tabWidth)
{
    int visual = 0;
    const int boundedOffset = qBound(0, offset, text.size());
    for (int i = 0; i < boundedOffset; ++i)
        visual += visualAdvanceForChar(text.at(i), visual, tabWidth);
    return visual;
}

enum class VisualBoundary {
    Start,
    End
};

int offsetForVisualColumn(const QString& text,
                          int visualColumn,
                          int tabWidth,
                          VisualBoundary boundary)
{
    const int target = qMax(0, visualColumn);
    int visual = 0;
    for (int i = 0; i < text.size(); ++i) {
        const int next =
            visual + visualAdvanceForChar(text.at(i), visual, tabWidth);
        if (target == visual)
            return i;
        if (target > visual && target < next)
            return boundary == VisualBoundary::End ? i + 1 : i;
        if (target == next)
            return i + 1;
        visual = next;
    }
    return text.size();
}

qreal editorSpaceAdvance(const MyCodeEditor* editor)
{
    if (!editor)
        return 1.0;
    const QFontMetricsF metrics(editor->font());
    return qMax<qreal>(
        1.0, metrics.horizontalAdvance(QLatin1Char(' ')));
}

QTextLine blockTextLine(const QTextBlock& block)
{
    QTextLayout* layout =
        block.isValid() ? block.layout() : nullptr;
    if (!layout || layout->lineCount() <= 0)
        return {};
    return layout->lineAt(0);
}

qreal blockTextXForOffset(const QTextBlock& block,
                          int offset)
{
    const QTextLine line = blockTextLine(block);
    if (!line.isValid())
        return -1.0;
    int bounded = qBound(0, offset, block.text().size());
    int zero = 0;
    return line.cursorToX(&bounded, QTextLine::Leading)
        - line.cursorToX(&zero, QTextLine::Leading);
}

int layoutVisualColumnForOffset(
    const MyCodeEditor* editor,
    const QTextBlock& block,
    int offset)
{
    const qreal x = blockTextXForOffset(block, offset);
    if (x < 0.0) {
        return visualColumnForOffset(
            block.text(),
            offset,
            editorTabStopColumns(editor));
    }
    return qMax(0, qRound(x / editorSpaceAdvance(editor)));
}

int layoutOffsetForVisualColumn(
    const MyCodeEditor* editor,
    const QTextBlock& block,
    int visualColumn,
    VisualBoundary boundary)
{
    const QTextLine line = blockTextLine(block);
    if (!line.isValid()) {
        return offsetForVisualColumn(
            block.text(),
            visualColumn,
            editorTabStopColumns(editor),
            boundary);
    }

    int zero = 0;
    const qreal zeroX =
        line.cursorToX(&zero, QTextLine::Leading);
    const qreal targetX =
        zeroX
        + qMax(0, visualColumn)
              * editorSpaceAdvance(editor);
    int offset = line.xToCursor(
        targetX,
        QTextLine::CursorBetweenCharacters);
    offset = qBound(0, offset, block.text().size());
    int probe = offset;
    const qreal offsetX =
        line.cursorToX(&probe, QTextLine::Leading);
    if (boundary == VisualBoundary::Start
        && offsetX > targetX
        && offset > 0) {
        --offset;
    } else if (boundary == VisualBoundary::End
               && offsetX < targetX
               && offset < block.text().size()) {
        ++offset;
    }
    return qBound(0, offset, block.text().size());
}

QString layoutVisualSlice(const MyCodeEditor* editor,
                          const QTextBlock& block,
                          int leftVisual,
                          int rightVisual)
{
    if (!block.isValid() || rightVisual <= leftVisual)
        return QString();
    const int lineEndVisual =
        layoutVisualColumnForOffset(
            editor, block, block.text().size());
    const int boundedLeft =
        qMin(qMax(0, leftVisual), lineEndVisual);
    const int boundedRight =
        qMin(qMax(boundedLeft, rightVisual),
             lineEndVisual);
    if (boundedRight <= boundedLeft)
        return QString();
    const int start = layoutOffsetForVisualColumn(
        editor, block, boundedLeft, VisualBoundary::Start);
    const int end = layoutOffsetForVisualColumn(
        editor, block, boundedRight, VisualBoundary::End);
    return block.text().mid(start, qMax(0, end - start));
}

int visualWidthOfText(const QString& text, int startVisual, int tabWidth)
{
    int visual = qMax(0, startVisual);
    for (const QChar ch : text)
        visual += visualAdvanceForChar(ch, visual, tabWidth);
    return visual - qMax(0, startVisual);
}

int nextTabStopVisual(int visualColumn, int tabWidth)
{
    const int width = qMax(1, tabWidth);
    const int visual = qMax(0, visualColumn);
    const int remainder = visual % width;
    return visual + (remainder == 0 ? width : width - remainder);
}

int previousTabStopVisual(int visualColumn, int tabWidth)
{
    const int width = qMax(1, tabWidth);
    const int visual = qMax(0, visualColumn);
    if (visual <= 0)
        return 0;
    const int remainder = visual % width;
    return remainder == 0 ? visual - width : visual - remainder;
}

void setCaretToVisualColumn(MyCodeEditor* editor, int line, int visualColumn)
{
    if (!editor)
        return;
    const QTextBlock block = editor->document()->findBlockByNumber(line);
    if (!block.isValid())
        return;
    const int offset = layoutOffsetForVisualColumn(
        editor,
        block,
        visualColumn,
        VisualBoundary::Start);
    QTextCursor caret(editor->document());
    caret.setPosition(block.position() + offset);
    editor->setTextCursor(caret);
}

QString columnSelectionClipboardText(MyCodeEditor* editor,
                                     const EditorColumnModeController::State& state)
{
    if (!editor || !hasColumnSelection(state))
        return QString();

    QStringList rows;
    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid()) {
            rows.append(QString());
            continue;
        }
        rows.append(layoutVisualSlice(editor,
                                      block,
                                      leftColumn,
                                      rightColumn));
    }
    return rows.join(QLatin1Char('\n'));
}

QStringList columnSelectionRowTexts(MyCodeEditor* editor,
                                    const EditorColumnModeController::State& state)
{
    if (!editor || !hasColumnSelection(state))
        return {};

    QStringList rows;
    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    rows.reserve(lastLine - firstLine + 1);
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        rows.append(block.isValid()
                        ? layoutVisualSlice(editor,
                                            block,
                                            leftColumn,
                                            rightColumn)
                        : QString());
    }
    return rows;
}

void updateColumnSelectionHighlight(MyCodeEditor* editor,
                                    const EditorColumnModeController::State& state);

void replaceColumnSelectionRows(MyCodeEditor* editor,
                                EditorColumnModeController::State& state,
                                const QStringList& rows,
                                bool pasteMode,
                                bool replaceSelectionArea = true)
{
    if (!editor || !hasColumnSelection(state))
        return;

    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    const bool hasWidth = replaceSelectionArea && rightColumn > leftColumn;
    const bool repeatSingleRow = pasteMode && rows.size() == 1;
    const int tabWidth = editorTabStopColumns(editor);
    int maxInsertedColumns = 0;

    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    for (int line = lastLine; line >= firstLine; --line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid())
            continue;

        const int rowIndex = line - firstLine;
        const QString rowText =
            rows.isEmpty()
                ? QString()
                : (repeatSingleRow
                       ? rows.constFirst()
                       : (rowIndex < rows.size() ? rows.at(rowIndex)
                                                 : QString()));
        maxInsertedColumns =
            qMax(maxInsertedColumns,
                 visualWidthOfText(rowText, leftColumn, tabWidth));

        const QString lineText = block.text();
        const int lineEndVisual =
            layoutVisualColumnForOffset(
                editor, block, lineText.size());
        const int startColumn = layoutOffsetForVisualColumn(
            editor, block, leftColumn, VisualBoundary::Start);
        int endColumn = startColumn;
        if (hasWidth)
            endColumn = layoutOffsetForVisualColumn(
                editor, block, rightColumn, VisualBoundary::End);

        cursor.setPosition(block.position() + startColumn);
        cursor.setPosition(block.position() + qMax(startColumn, endColumn),
                           QTextCursor::KeepAnchor);

        if (pasteMode) {
            const QString padding =
                leftColumn > lineEndVisual
                    ? QString(leftColumn - lineEndVisual, QLatin1Char(' '))
                    : QString();
            cursor.insertText(padding + rowText);
        } else if (endColumn > startColumn) {
            cursor.removeSelectedText();
        }
    }
    cursor.endEditBlock();

    const int collapsedColumn =
        pasteMode ? leftColumn + maxInsertedColumns : leftColumn;
    state.columnAnchorLine = firstLine;
    state.columnCurrentLine = lastLine;
    state.columnAnchorColumn = collapsedColumn;
    state.columnCurrentColumn = collapsedColumn;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragging = false;
    state.columnSelectionDragMoved = false;

    const QTextBlock currentBlock =
        editor->document()->findBlockByNumber(lastLine);
    if (currentBlock.isValid()) {
        setCaretToVisualColumn(editor, lastLine, collapsedColumn);
    }
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->update();
}

void clearColumnSelection(MyCodeEditor* editor, EditorColumnModeController::State& state)
{
    const bool wasActive =
        state.modes
        && state.modes->isActive(
            EditorModeId::ColumnSelection);
    state.columnSelectionDragging = false;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragMoved = false;
    state.columnAnchorLine = -1;
    state.columnAnchorColumn = -1;
    state.columnCurrentLine = -1;
    state.columnCurrentColumn = -1;
    if (state.owner)
        state.owner->publishVisibleAnnotations(editor);
    if (editor) {
        editor->viewport()->setCursor(Qt::IBeamCursor);
        editor->viewport()->update();
    }
    if (wasActive) {
        state.modes->exit(EditorModeId::ColumnSelection,
                         EditorModeExitReason::Canceled);
    }
}

void updateColumnSelectionHighlight(MyCodeEditor* editor,
                                     const EditorColumnModeController::State& state)
{
    if (!editor)
        return;

    if (state.owner)
        state.owner->publishVisibleAnnotations(editor);
    editor->viewport()->update();
}

void setColumnPointFromCursor(const MyCodeEditor* editor,
                              const QTextCursor& cursor,
                              int* line,
                              int* column)
{
    if (!cursor.block().isValid())
        return;
    if (line)
        *line = cursor.block().blockNumber();
    if (column) {
        *column = layoutVisualColumnForOffset(
            editor,
            cursor.block(),
            qMax(0,
                 cursor.position()
                     - cursor.block().position()));
    }
}

bool columnPointFromMouse(MyCodeEditor* editor,
                          const QPoint& position,
                          int* line,
                          int* column,
                          bool* beyondLineEnd = nullptr)
{
    if (!editor || !editor->document())
        return false;
    const QTextCursor rowCursor =
        editor->cursorForPosition(QPoint(0, position.y()));
    const QTextBlock block = rowCursor.block();
    if (!block.isValid() || !block.isVisible())
        return false;
    const EditorBlockGeometry geometry =
        editor->blockGeometry(block.blockNumber());
    if (position.y() < geometry.top
        || position.y() > geometry.top + geometry.height) {
        return false;
    }

    QTextCursor startCursor(block);
    startCursor.setPosition(block.position());
    QTextCursor endCursor(block);
    endCursor.setPosition(
        block.position() + block.text().size());
    const qreal startX =
        editor->cursorRect(startCursor).left();
    const qreal endX =
        editor->cursorRect(endCursor).left();
    const qreal space = editorSpaceAdvance(editor);
    const int targetColumn = qMax(
        0, qRound((position.x() - startX) / space));
    if (line)
        *line = block.blockNumber();
    if (column)
        *column = targetColumn;
    if (beyondLineEnd) {
        *beyondLineEnd =
            position.x() > endX + space * 0.25;
    }
    return true;
}

bool handlePlainVirtualCursorClick(
    MyCodeEditor* editor,
    QMouseEvent* event,
    EditorColumnModeController::State& state)
{
    if (!editor || !event
        || event->button() != Qt::LeftButton
        || (event->modifiers() != Qt::NoModifier
            && event->modifiers() != Qt::AltModifier)) {
        return false;
    }

    int line = -1;
    int column = -1;
    bool beyond = false;
    if (!columnPointFromMouse(
            editor,
            event->position().toPoint(),
            &line,
            &column,
            &beyond)) {
        state.owner->clearVirtualCursor(editor);
        return false;
    }
    if (!beyond) {
        state.owner->clearVirtualCursor(editor);
        return false;
    }

    const QTextBlock block =
        editor->document()->findBlockByNumber(line);
    if (!block.isValid())
        return false;
    const int lineEndColumn =
        layoutVisualColumnForOffset(
            editor, block, block.text().size());
    if (column <= lineEndColumn)
        column = lineEndColumn + 1;

    state.owner->clearVirtualCursor(editor);
    QTextCursor cursor(block);
    cursor.setPosition(
        block.position() + block.text().size());
    editor->setTextCursor(cursor);
    state.virtualCursorSavedWidth =
        qMax(1, editor->cursorWidth());
    state.modes->enter(EditorModeId::VirtualCursor,
                      EditorModeEntryReason::MouseGesture);
    state.virtualCursorLine = line;
    state.virtualCursorColumn = column;
    state.modes->updatePresentation(
        EditorModeId::VirtualCursor,
        QStringLiteral("Virtual column %1")
            .arg(column + 1),
        QStringLiteral(
             "Type to materialize; arrows move; Esc cancels"));
    editor->setCursorWidth(0);
    if (state.owner)
        state.owner->publishVisibleAnnotations(editor);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool beginColumnSelection(MyCodeEditor* editor,
                          QMouseEvent* event,
                          EditorColumnModeController::State& state)
{
    if (!editor || !event
        || event->button() != Qt::LeftButton
        || !event->modifiers().testFlag(Qt::ShiftModifier)
        || !event->modifiers().testFlag(Qt::AltModifier)) {
        return false;
    }

    int currentLine = -1;
    int currentColumn = -1;
    if (!columnPointFromMouse(
            editor,
            event->position().toPoint(),
            &currentLine,
            &currentColumn)) {
        return false;
    }
    if (state.modes->isActive(EditorModeId::ColumnSelection)) {
        state.columnCurrentLine = currentLine;
        state.columnCurrentColumn = currentColumn;
        state.columnSelectionAwaitingEndpoint = false;
        state.columnSelectionDragging = false;
        state.columnSelectionDragMoved = false;
        state.modes->updatePresentation(
            EditorModeId::ColumnSelection,
            QStringLiteral("Column selection"),
            QStringLiteral(
                "Type, navigate, or use clipboard; Esc cancels"));
        updateColumnSelectionHighlight(editor, state);
        editor->viewport()->setCursor(Qt::CrossCursor);
        editor->viewport()->update();
        event->accept();
        return true;
    }

    const bool virtualAnchor =
        state.modes->isActive(EditorModeId::VirtualCursor);
    const int virtualAnchorLine = state.virtualCursorLine;
    const int virtualAnchorColumn = state.virtualCursorColumn;
    const QTextCursor anchor = editor->textCursor();
    state.modes->enter(EditorModeId::ColumnSelection,
                      EditorModeEntryReason::MouseGesture);
    state.columnSelectionDragging = false;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragMoved = false;
    if (virtualAnchor) {
        state.columnAnchorLine = virtualAnchorLine;
        state.columnAnchorColumn = virtualAnchorColumn;
    } else {
        setColumnPointFromCursor(editor,
                                 anchor,
                                 &state.columnAnchorLine,
                                 &state.columnAnchorColumn);
    }
    state.columnCurrentLine = currentLine;
    state.columnCurrentColumn = currentColumn;
    state.owner->clearVirtualCursor(editor);
    state.modes->updatePresentation(
        EditorModeId::ColumnSelection,
        QStringLiteral("Column selection"),
        QStringLiteral(
            "Type, navigate, or use clipboard; Esc cancels"));
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->setCursor(Qt::CrossCursor);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool handleColumnSelectionClipboard(MyCodeEditor* editor,
                                    QKeyEvent* event,
                                    EditorColumnModeController::State& state)
{
    if (!editor || !event || !hasColumnSelection(state))
        return false;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (!modifiers.testFlag(Qt::ControlModifier)
        || modifiers.testFlag(Qt::ShiftModifier)
        || modifiers.testFlag(Qt::AltModifier)
        || modifiers.testFlag(Qt::MetaModifier)) {
        return false;
    }

    const bool copy = event->key() == Qt::Key_C;
    const bool cut = event->key() == Qt::Key_X;
    const bool paste = event->key() == Qt::Key_V;
    if (!copy && !cut && !paste)
        return false;

    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard)
        return false;

    if (copy || cut) {
        clipboard->setText(columnSelectionClipboardText(editor, state));
        if (cut) {
            const auto [firstLine, lastLine] = lineSpan(state);
            replaceColumnSelectionRows(
                editor,
                state,
                QStringList(lastLine - firstLine + 1, QString()),
                false);
        }
        event->accept();
        return true;
    }

    replaceColumnSelectionRows(
        editor,
        state,
        normalizedClipboardRows(clipboard->text()),
        true);
    event->accept();
    return true;
}

bool updateColumnSelectionDrag(MyCodeEditor* editor,
                               QMouseEvent* event,
                               EditorColumnModeController::State& state)
{
    if (!editor || !event || !state.columnSelectionDragging)
        return false;
    if (!event->buttons().testFlag(Qt::LeftButton))
        return false;

    state.columnSelectionDragMoved = true;
    state.columnSelectionAwaitingEndpoint = false;
    columnPointFromMouse(
        editor,
        event->position().toPoint(),
        &state.columnCurrentLine,
        &state.columnCurrentColumn);
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool endColumnSelectionDrag(MyCodeEditor* editor,
                            QMouseEvent* event,
                            EditorColumnModeController::State& state)
{
    if (!editor || !event || !state.columnSelectionDragging)
        return false;

    if (state.columnSelectionDragMoved) {
        columnPointFromMouse(
            editor,
            event->position().toPoint(),
            &state.columnCurrentLine,
            &state.columnCurrentColumn);
        state.columnSelectionAwaitingEndpoint = false;
        updateColumnSelectionHighlight(editor, state);
    }
    state.columnSelectionDragging = false;
    state.columnSelectionDragMoved = false;
    editor->viewport()->update();
    event->accept();
    return true;
}

bool handleColumnSelectionKeyInput(MyCodeEditor* editor,
                                   QKeyEvent* event,
                                   EditorColumnModeController::State& state)
{
    if (!editor || !event || !hasColumnSelection(state))
        return false;
    if (event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::MetaModifier)
        || event->modifiers().testFlag(Qt::AltModifier)) {
        return false;
    }
    const Qt::KeyboardModifiers textModifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    const bool forwardTab = event->key() == Qt::Key_Tab
        && textModifiers == Qt::NoModifier;
    const bool backwardTab = event->key() == Qt::Key_Backtab
        || (event->key() == Qt::Key_Tab
            && textModifiers == Qt::ShiftModifier);
    if (event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Escape) {
        return false;
    }

    const bool backspace = event->key() == Qt::Key_Backspace;
    const bool deleteKey = event->key() == Qt::Key_Delete;
    const bool printable = !event->text().isEmpty()
        && !backspace
        && !deleteKey
        && !forwardTab
        && !backwardTab;
    if (!printable && !backspace && !deleteKey && !forwardTab && !backwardTab)
        return false;

    const int tabWidth = editorTabStopColumns(editor);
    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    const bool hasWidth = rightColumn > leftColumn;
    const int backwardTargetColumn =
        backwardTab ? previousTabStopVisual(leftColumn, tabWidth) : leftColumn;
    const int editColumn =
        backwardTab
            ? backwardTargetColumn
            : (backspace && !hasWidth ? qMax(0, leftColumn - 1) : leftColumn);
    QString text;
    if (printable)
        text = event->text();
    else if (forwardTab)
        text = QString(nextTabStopVisual(leftColumn, tabWidth) - leftColumn,
                       QLatin1Char(' '));
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    for (int line = lastLine; line >= firstLine; --line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid())
            continue;

        const QString lineText = block.text();
        const int lineEndVisual =
            layoutVisualColumnForOffset(
                editor, block, lineText.size());
        int startColumn = layoutOffsetForVisualColumn(
            editor, block, editColumn, VisualBoundary::Start);
        int endColumn = startColumn;
        if (hasWidth) {
            startColumn = layoutOffsetForVisualColumn(
                editor, block, leftColumn, VisualBoundary::Start);
            endColumn = layoutOffsetForVisualColumn(
                editor, block, rightColumn, VisualBoundary::End);
        } else if (deleteKey && leftColumn < lineEndVisual) {
            startColumn = layoutOffsetForVisualColumn(
                editor, block, leftColumn, VisualBoundary::Start);
            endColumn = layoutOffsetForVisualColumn(
                editor, block, leftColumn + 1, VisualBoundary::End);
        } else if (backspace && leftColumn > 0 && editColumn < lineEndVisual) {
            startColumn = layoutOffsetForVisualColumn(
                editor, block, editColumn, VisualBoundary::Start);
            endColumn = layoutOffsetForVisualColumn(
                editor, block, leftColumn, VisualBoundary::End);
        } else if (backwardTab && leftColumn > backwardTargetColumn) {
            startColumn = layoutOffsetForVisualColumn(
                editor,
                block,
                backwardTargetColumn,
                VisualBoundary::Start);
            endColumn = layoutOffsetForVisualColumn(
                editor, block, leftColumn, VisualBoundary::End);
        }

        cursor.setPosition(block.position() + startColumn);
        cursor.setPosition(block.position() + qMax(startColumn, endColumn),
                           QTextCursor::KeepAnchor);
        if (printable || forwardTab) {
            const QString padding =
                leftColumn > lineEndVisual
                    ? QString(leftColumn - lineEndVisual, QLatin1Char(' '))
                    : QString();
            cursor.insertText(padding + text);
        } else if (endColumn > startColumn) {
            cursor.removeSelectedText();
        }
    }
    cursor.endEditBlock();

    const bool wasRectangularSelection = leftColumn != rightColumn;
    const int collapsedColumn = wasRectangularSelection
        ? leftColumn
        : ((printable || forwardTab)
               ? leftColumn + visualWidthOfText(text, leftColumn, tabWidth)
               : editColumn);
    state.columnAnchorColumn = collapsedColumn;
    state.columnCurrentColumn = state.columnAnchorColumn;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragging = false;
    state.columnSelectionDragMoved = false;
    const QTextBlock currentBlock =
        editor->document()->findBlockByNumber(lastLine);
    if (currentBlock.isValid()) {
        setCaretToVisualColumn(editor, lastLine, state.columnCurrentColumn);
    }
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool handleColumnSelectionNavigation(MyCodeEditor* editor,
                                     QKeyEvent* event,
                                     EditorColumnModeController::State& state)
{
    if (!editor || !event || !hasColumnSelection(state))
        return false;

    const int key = event->key();
    const bool vertical =
        key == Qt::Key_Up || key == Qt::Key_Down;
    const bool horizontal =
        key == Qt::Key_Left || key == Qt::Key_Right;
    if (!vertical && !horizontal)
        return false;

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool adjustSelection =
        modifiers.testFlag(Qt::ShiftModifier)
        && modifiers.testFlag(Qt::AltModifier)
        && !modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::MetaModifier);
    const bool moveSelection =
        !modifiers.testFlag(Qt::ShiftModifier)
        && !modifiers.testFlag(Qt::AltModifier)
        && !modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::MetaModifier);
    if (!adjustSelection && !moveSelection)
        return false;

    const int lastLine = qMax(0, editor->document()->blockCount() - 1);
    const int lineDelta =
        key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0;
    const int columnDelta =
        key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0;

    if (adjustSelection) {
        state.columnCurrentLine =
            qBound(0, state.columnCurrentLine + lineDelta, lastLine);
        state.columnCurrentColumn =
            qMax(0, state.columnCurrentColumn + columnDelta);
    } else {
        const auto [firstLine, lastSelectedLine] = lineSpan(state);
        if ((lineDelta < 0 && firstLine <= 0)
            || (lineDelta > 0 && lastSelectedLine >= lastLine)) {
            event->accept();
            return true;
        }
        const auto [leftColumn, rightColumn] = columnSpan(state);
        if (columnDelta < 0 && leftColumn <= 0) {
            event->accept();
            return true;
        }

        state.columnAnchorLine += lineDelta;
        state.columnCurrentLine += lineDelta;
        state.columnAnchorColumn = qMax(0, state.columnAnchorColumn + columnDelta);
        state.columnCurrentColumn = qMax(0, state.columnCurrentColumn + columnDelta);
        Q_UNUSED(rightColumn)
    }

    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragging = false;
    state.columnSelectionDragMoved = false;
    updateColumnSelectionHighlight(editor, state);
    setCaretToVisualColumn(editor,
                           state.columnCurrentLine,
                           state.columnCurrentColumn);
    editor->viewport()->update();
    event->accept();
    return true;
}

}


EditorColumnModeController::EditorColumnModeController()
    : state(std::make_unique<State>())
{
    state->owner = this;
}

EditorColumnModeController::~EditorColumnModeController() =
    default;

void EditorColumnModeController::bind(
    EditorModeController* modes,
    AnnotationLayer* annotations,
    MyCodeEditor* editor)
{
    state->modes = modes;
    state->annotations = annotations;
    if (!modes)
        return;

    const QPointer<MyCodeEditor> target(editor);
    modes->setExitHandler(
        EditorModeId::ColumnSelection,
        [this, target](EditorModeExitReason) {
            clearSelection(target);
        });
    modes->setExitHandler(
        EditorModeId::VirtualCursor,
        [this, target](EditorModeExitReason) {
            clearVirtualCursor(target);
        });
}

void EditorColumnModeController::shutdown(
    MyCodeEditor* editor)
{
    clearSelection(editor);
    clearVirtualCursor(editor);
    state->modes = nullptr;
    state->annotations = nullptr;
}

bool EditorColumnModeController::selectionActive() const
{
    return hasColumnSelection(*state);
}

bool EditorColumnModeController::virtualCursorActive() const
{
    return state->modes
        && state->modes->isActive(
            EditorModeId::VirtualCursor);
}

int EditorColumnModeController::virtualCursorLine() const
{
    return state->virtualCursorLine;
}

int EditorColumnModeController::virtualCursorColumn() const
{
    return state->virtualCursorColumn;
}

EditorColumnModeSnapshot
EditorColumnModeController::snapshotForTest() const
{
    EditorColumnModeSnapshot snapshot;
    snapshot.selectionActive =
        hasColumnSelection(*state);
    snapshot.anchorLine = state->columnAnchorLine;
    snapshot.anchorColumn =
        state->columnAnchorColumn;
    snapshot.currentLine =
        state->columnCurrentLine;
    snapshot.currentColumn =
        state->columnCurrentColumn;
    return snapshot;
}

QStringList EditorColumnModeController::selectedRows(
    MyCodeEditor* editor) const
{
    return columnSelectionRowTexts(editor, *state);
}

bool EditorColumnModeController::applyRows(
    MyCodeEditor* editor,
    const QStringList& rows,
    bool replaceSelection,
    QString* message)
{
    if (!editor || !hasColumnSelection(*state)) {
        if (message)
            *message = QStringLiteral("No column selection");
        return false;
    }
    replaceColumnSelectionRows(
        editor,
        *state,
        rows,
        true,
        replaceSelection);
    if (message)
        message->clear();
    return true;
}

void EditorColumnModeController::publishVisibleAnnotations(
    MyCodeEditor* editor,
    int firstVisibleLine,
    int lastVisibleLine)
{
    if (!state->annotations)
        return;

    const bool columnMode = hasColumnSelection(*state);
    const bool virtualMode =
        state->modes
        && state->modes->isActive(
            EditorModeId::VirtualCursor)
        && state->virtualCursorLine >= 0
        && state->virtualCursorColumn >= 0;
    if (!editor
        || !editor->document()
        || (!columnMode && !virtualMode)) {
        state->annotations->removeSource(
            QString::fromLatin1(
                kColumnCaretAnnotationSource));
        return;
    }

    const int finalDocumentLine =
        qMax(0, editor->blockCount() - 1);
    int visibleFirst = firstVisibleLine;
    int visibleLast = lastVisibleLine;
    if (visibleFirst < 0 || visibleLast < visibleFirst) {
        const QTextBlock first =
            editor->cursorForPosition(QPoint(0, 0)).block();
        QTextBlock last =
            editor->cursorForPosition(
                QPoint(
                    0,
                    qMax(
                        0,
                        editor->viewport()->height() - 1)))
                .block();
        if (!first.isValid()) {
            state->annotations->removeSource(
                QString::fromLatin1(
                    kColumnCaretAnnotationSource));
            return;
        }
        if (!last.isValid()
            || last.blockNumber()
                   < first.blockNumber()) {
            last = first;
        }
        visibleFirst = first.blockNumber();
        visibleLast = last.blockNumber();
    }
    visibleFirst =
        qBound(0, visibleFirst, finalDocumentLine);
    visibleLast =
        qBound(visibleFirst,
               visibleLast,
               finalDocumentLine);

    int firstLine = state->virtualCursorLine;
    int lastLine = state->virtualCursorLine;
    int targetColumn = state->virtualCursorColumn;
    int activeLine = state->virtualCursorLine;
    int selectionLeft = targetColumn;
    int selectionRight = targetColumn;
    if (columnMode) {
        const auto lines = lineSpan(*state);
        firstLine = lines.first;
        lastLine = lines.second;
        targetColumn = state->columnCurrentColumn;
        activeLine = state->columnCurrentLine;
        const auto columns = columnSpan(*state);
        selectionLeft = columns.first;
        selectionRight = columns.second;
    }

    firstLine = qMax(firstLine, visibleFirst);
    lastLine = qMin(lastLine, visibleLast);
    if (firstLine > lastLine) {
        state->annotations->removeSource(
            QString::fromLatin1(
                kColumnCaretAnnotationSource));
        return;
    }

    ++state->presentationGeneration;
    QList<EditorAnnotation> annotations;
    annotations.reserve(lastLine - firstLine + 1);
    for (int line = firstLine;
         line <= lastLine;
         ++line) {
        const QTextBlock block =
            editor->document()->findBlockByNumber(line);
        if (!block.isValid() || !block.isVisible())
            continue;

        const int lineEndColumn =
            layoutVisualColumnForOffset(
                editor,
                block,
                block.text().size());
        const int rangeStartOffset =
            layoutOffsetForVisualColumn(
                editor,
                block,
                qMin(selectionLeft, lineEndColumn),
                VisualBoundary::Start);
        const int rangeEndOffset =
            layoutOffsetForVisualColumn(
                editor,
                block,
                qMin(selectionRight, lineEndColumn),
                VisualBoundary::End);

        EditorAnnotation annotation;
        annotation.kind =
            EditorAnnotationKind::ColumnCaret;
        annotation.placement =
            EditorAnnotationPlacement::Overlay;
        annotation.range.startPosition =
            block.position() + rangeStartOffset;
        annotation.range.endPosition =
            block.position()
            + qMax(rangeStartOffset,
                   rangeEndOffset);
        annotation.range.firstLine = line;
        annotation.range.lastLine = line;
        annotation.semanticKey =
            QStringLiteral("column-caret:%1")
                .arg(line);
        annotation.priority =
            AnnotationLayer::defaultPriority(
                annotation.kind)
            + (line == activeLine ? 5 : 0);
        annotation.sourceGeneration =
            state->presentationGeneration;
        annotation.visualColumn = targetColumn;
        annotation.visualRangeStartColumn =
            selectionLeft;
        annotation.visualRangeEndColumn =
            selectionRight;
        annotation.active = line == activeLine;
        annotations.append(std::move(annotation));
    }

    if (annotations.isEmpty()) {
        state->annotations->removeSource(
            QString::fromLatin1(
                kColumnCaretAnnotationSource));
    } else {
        state->annotations->setSourceAnnotations(
            QString::fromLatin1(
                kColumnCaretAnnotationSource),
            annotations);
    }
}

void EditorColumnModeController::clearSelection(
    MyCodeEditor* editor)
{
    clearColumnSelection(editor, *state);
}

bool EditorColumnModeController::beginSelection(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    return beginColumnSelection(editor, event, *state);
}

bool EditorColumnModeController::
    handlePlainVirtualCursorClick(
        MyCodeEditor* editor,
        QMouseEvent* event)
{
    return ::handlePlainVirtualCursorClick(
        editor,
        event,
        *state);
}

bool EditorColumnModeController::handleClipboard(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    return handleColumnSelectionClipboard(
        editor,
        event,
        *state);
}

bool EditorColumnModeController::
    handleSelectionNavigation(
        MyCodeEditor* editor,
        QKeyEvent* event)
{
    return handleColumnSelectionNavigation(
        editor,
        event,
        *state);
}

bool EditorColumnModeController::handleSelectionKeyInput(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    return handleColumnSelectionKeyInput(
        editor,
        event,
        *state);
}

bool EditorColumnModeController::updateSelectionDrag(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    return updateColumnSelectionDrag(
        editor,
        event,
        *state);
}

bool EditorColumnModeController::endSelectionDrag(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    return endColumnSelectionDrag(
        editor,
        event,
        *state);
}

void EditorColumnModeController::clearVirtualCursor(
    MyCodeEditor* editor)
{
    const bool controllerActive =
        state->modes
        && state->modes->isActive(
            EditorModeId::VirtualCursor);
    const bool hadVirtualState =
        controllerActive
        || state->virtualCursorLine >= 0
        || state->virtualCursorColumn >= 0;
    state->virtualCursorLine = -1;
    state->virtualCursorColumn = -1;
    publishVisibleAnnotations(editor);
    if (editor && hadVirtualState) {
        editor->setCursorWidth(
            qMax(1, state->virtualCursorSavedWidth));
        editor->viewport()->update();
    }
    state->virtualCursorSavedWidth = 1;
    if (controllerActive) {
        state->modes->exit(
            EditorModeId::VirtualCursor,
            EditorModeExitReason::Canceled);
    }
}

void EditorColumnModeController::
    handleVirtualCursorChanged(
        MyCodeEditor* editor)
{
    if (!virtualCursorActive() || !editor)
        return;

    const QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    const bool stillAtLineEnd =
        !cursor.hasSelection()
        && block.isValid()
        && block.blockNumber()
               == state->virtualCursorLine
        && cursor.position()
               == block.position()
                      + block.text().size();
    if (!stillAtLineEnd)
        clearVirtualCursor(editor);
}

void EditorColumnModeController::prepareVirtualCursorInput(
    MyCodeEditor* editor)
{
    if (!virtualCursorActive()
        || !editor
        || !editor->document()) {
        return;
    }
    const QTextBlock block =
        editor->document()->findBlockByNumber(
            state->virtualCursorLine);
    if (!block.isValid()) {
        clearVirtualCursor(editor);
        return;
    }
    const int lineEndColumn =
        layoutVisualColumnForOffset(
            editor,
            block,
            block.text().size());
    const int paddingLength =
        qMax(
            0,
            state->virtualCursorColumn
                - lineEndColumn);
    const int insertionPosition =
        block.position() + block.text().size();
    clearVirtualCursor(editor);
    QTextCursor cursor(editor->document());
    cursor.setPosition(insertionPosition);
    if (paddingLength > 0) {
        cursor.insertText(
            QString(
                paddingLength,
                QLatin1Char(' ')));
    }
    editor->setTextCursor(cursor);
}

bool EditorColumnModeController::
    handleVirtualCursorKeyPress(
        MyCodeEditor* editor,
        QKeyEvent* event)
{
    if (!editor || !event || !editor->document())
        return false;

    const Qt::KeyboardModifiers relevantModifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    if (!virtualCursorActive()) {
        if (selectionActive()
            || !state->modes) {
            return false;
        }
        if (event->key() != Qt::Key_Right
            || relevantModifiers
                   != Qt::NoModifier) {
            return false;
        }
        const QTextCursor cursor =
            editor->textCursor();
        const QTextBlock block = cursor.block();
        if (cursor.hasSelection()
            || !block.isValid()
            || cursor.position()
                   != block.position()
                          + block.text().size()) {
            return false;
        }
        state->virtualCursorSavedWidth =
            qMax(1, editor->cursorWidth());
        state->modes->enter(
            EditorModeId::VirtualCursor,
            EditorModeEntryReason::KeyboardGesture);
        state->virtualCursorLine =
            block.blockNumber();
        state->virtualCursorColumn =
            layoutVisualColumnForOffset(
                editor,
                block,
                block.text().size())
            + 1;
        state->modes->updatePresentation(
            EditorModeId::VirtualCursor,
            QStringLiteral("Virtual column %1")
                .arg(
                    state->virtualCursorColumn
                    + 1),
            QStringLiteral(
                 "Type to materialize; arrows move; Esc cancels"));
        editor->setCursorWidth(0);
        publishVisibleAnnotations(editor);
        editor->viewport()->update();
        event->accept();
        return true;
    }

    const QTextBlock block =
        editor->document()->findBlockByNumber(
            state->virtualCursorLine);
    if (!block.isValid()) {
        clearVirtualCursor(editor);
        return false;
    }
    const int lineEndColumn =
        layoutVisualColumnForOffset(
            editor,
            block,
            block.text().size());

    if (relevantModifiers == Qt::NoModifier
        && (event->key() == Qt::Key_Left
            || event->key()
                   == Qt::Key_Backspace)) {
        --state->virtualCursorColumn;
        if (state->virtualCursorColumn
            <= lineEndColumn) {
            clearVirtualCursor(editor);
        } else {
            state->modes->updatePresentation(
                EditorModeId::VirtualCursor,
                QStringLiteral("Virtual column %1")
                    .arg(
                        state->virtualCursorColumn
                        + 1),
                QStringLiteral(
                     "Type to materialize; arrows move; Esc cancels"));
            publishVisibleAnnotations(editor);
            editor->viewport()->update();
        }
        event->accept();
        return true;
    }
    if (relevantModifiers == Qt::NoModifier
        && event->key() == Qt::Key_Right) {
        ++state->virtualCursorColumn;
        state->modes->updatePresentation(
            EditorModeId::VirtualCursor,
            QStringLiteral("Virtual column %1")
                .arg(
                    state->virtualCursorColumn
                    + 1),
            QStringLiteral(
                 "Type to materialize; arrows move; Esc cancels"));
        publishVisibleAnnotations(editor);
        editor->viewport()->update();
        event->accept();
        return true;
    }
    if (relevantModifiers == Qt::NoModifier
        && event->key() == Qt::Key_Delete) {
        event->accept();
        return true;
    }

    const bool plainControlShortcut =
        relevantModifiers == Qt::ControlModifier;
    const bool paste =
        event->matches(QKeySequence::Paste)
        || (plainControlShortcut
            && event->key() == Qt::Key_V);
    const bool printable =
        relevantModifiers == Qt::NoModifier
        && !event->text().isEmpty();
    const bool textControl =
        relevantModifiers == Qt::NoModifier
        && (event->key() == Qt::Key_Tab
            || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter);
    if (paste || printable || textControl) {
        prepareVirtualCursorInput(editor);
        return false;
    }

    const bool copy =
        event->matches(QKeySequence::Copy)
        || (plainControlShortcut
            && event->key() == Qt::Key_C);
    if (copy) {
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Control
        || event->key() == Qt::Key_Shift
        || event->key() == Qt::Key_Alt
        || event->key() == Qt::Key_Meta) {
        return false;
    }

    clearVirtualCursor(editor);
    return false;
}
