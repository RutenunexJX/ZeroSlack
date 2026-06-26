#include "editorruntime.h"

#include "mycodeeditor.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QRect>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <utility>

#include "formatterservice.h"

namespace {
constexpr int kMaxPassiveGhostAnnotationCharacters = 2 * 1024 * 1024;
constexpr int kColumnSelectionProperty = QTextFormat::UserProperty + 20;
constexpr int kColumnSelectionMarker = 1020;

bool hasCommandModifier(QKeyEvent* event)
{
    if (!event)
        return false;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    return modifiers.testFlag(Qt::ControlModifier)
        || modifiers.testFlag(Qt::AltModifier)
        || modifiers.testFlag(Qt::MetaModifier);
}

bool isUnsignedIntegerText(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

QString rangeFromBracketInnerText(const QString& innerText)
{
    const QString trimmed = innerText.trimmed();
    if (trimmed.isEmpty())
        return QString();

    if (trimmed.contains(QLatin1Char(':')))
        return QStringLiteral("[%1]").arg(trimmed);

    if (isUnsignedIntegerText(trimmed)) {
        const int width = trimmed.toInt();
        if (width <= 0)
            return QString();
        return QStringLiteral("[%1:0]").arg(width - 1);
    }

    return QStringLiteral("[%1 - 1:0]").arg(trimmed);
}

bool bracketRangeAroundCursor(const QTextCursor& cursor,
                              int* rangeStart,
                              int* rangeEnd)
{
    if (!cursor.block().isValid())
        return false;

    const QString line = cursor.block().text();
    const int column = cursor.position() - cursor.block().position();

    int left = -1;
    for (int i = qMin(column - 1, line.size() - 1); i >= 0; --i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char(']'))
            return false;
        if (ch == QLatin1Char('[')) {
            left = i;
            break;
        }
    }
    if (left < 0)
        return false;

    int right = -1;
    for (int i = qMax(column, left + 1); i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('['))
            return false;
        if (ch == QLatin1Char(']')) {
            right = i;
            break;
        }
    }
    if (right <= left + 1)
        return false;

    if (rangeStart)
        *rangeStart = cursor.block().position() + left;
    if (rangeEnd)
        *rangeEnd = cursor.block().position() + right + 1;
    return true;
}

bool handleBracketPairInsertion(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !event || hasCommandModifier(event))
        return false;
    if (event->key() != Qt::Key_BracketLeft
        && event->text() != QStringLiteral("["))
        return false;

    QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection()) {
        cursor.insertText(QStringLiteral("[%1]").arg(cursor.selectedText()));
    } else {
        cursor.insertText(QStringLiteral("[]"));
        cursor.movePosition(QTextCursor::Left);
    }
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

bool selectedFullLineRange(MyCodeEditor* editor, int* rangeStart, int* rangeEnd)
{
    if (!editor)
        return false;

    const QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection() || cursor.selectionEnd() <= cursor.selectionStart())
        return false;

    QTextDocument* document = editor->document();
    if (!document)
        return false;

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock startBlock = document->findBlock(selectionStart);
    const QTextBlock endBlock =
        document->findBlock(std::max(selectionStart, selectionEnd - 1));
    if (!startBlock.isValid() || !endBlock.isValid())
        return false;

    const int start = startBlock.position();
    const int documentEnd = std::max(0, document->characterCount() - 1);
    const int end = std::min(endBlock.position() + endBlock.length(),
                             documentEnd);
    if (end <= start)
        return false;

    if (rangeStart)
        *rangeStart = start;
    if (rangeEnd)
        *rangeEnd = end;
    return true;
}

bool handleBracketRangeTab(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !event || hasCommandModifier(event)
        || event->key() != Qt::Key_Tab)
        return false;

    QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection())
        return false;

    int rangeStart = -1;
    int rangeEnd = -1;
    if (!bracketRangeAroundCursor(cursor, &rangeStart, &rangeEnd))
        return false;

    const QString rangeText =
        editor->document()->toPlainText().mid(rangeStart, rangeEnd - rangeStart);
    const QString expanded =
        rangeFromBracketInnerText(rangeText.mid(1, rangeText.size() - 2));
    if (expanded.isEmpty())
        return false;

    cursor.beginEditBlock();
    cursor.setPosition(rangeStart);
    cursor.setPosition(rangeEnd, QTextCursor::KeepAnchor);
    cursor.insertText(expanded);
    cursor.endEditBlock();
    cursor.setPosition(rangeStart + expanded.size());
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

bool handleBracketRangeAltClick(MyCodeEditor* editor, QMouseEvent* event)
{
    if (!editor || !event
        || event->button() != Qt::LeftButton
        || !event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::ControlModifier))
        return false;

    QTextCursor cursor = editor->cursorForPosition(
        event->position().toPoint());
    int rangeStart = -1;
    int rangeEnd = -1;
    if (!bracketRangeAroundCursor(cursor, &rangeStart, &rangeEnd))
        return false;

    QTextCursor selection = editor->textCursor();
    selection.setPosition(rangeStart + 1);
    selection.setPosition(rangeEnd - 1, QTextCursor::KeepAnchor);
    editor->setTextCursor(selection);
    event->accept();
    return true;
}

bool selectedRangeBoundSpan(const QString& selected,
                            bool rightBound,
                            int* boundStart,
                            int* boundEnd)
{
    int colonIndex = -1;
    for (int i = 0; i < selected.size(); ++i) {
        if (selected.at(i) == QLatin1Char(':')) {
            colonIndex = i;
            break;
        }
    }
    if (colonIndex < 0)
        return false;

    int start = 0;
    int stop = selected.size();
    if (rightBound) {
        start = colonIndex + 1;
    } else {
        stop = colonIndex;
    }

    while (start < stop && selected.at(start).isSpace())
        ++start;

    int end = stop;
    while (end > start && selected.at(end - 1).isSpace())
        --end;

    if (end == start)
        return false;

    if (boundStart)
        *boundStart = start;
    if (boundEnd)
        *boundEnd = end;
    return true;
}

bool parseNonNegativeInteger(const QString& text, int* value)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    for (const QChar ch : trimmed) {
        if (!ch.isDigit())
            return false;
    }

    bool ok = false;
    const int parsed = trimmed.toInt(&ok);
    if (!ok)
        return false;

    if (value)
        *value = parsed;
    return true;
}

bool simpleRangeExpression(const QString& expression)
{
    const QString trimmed = expression.trimmed();
    if (trimmed.isEmpty())
        return false;

    for (const QChar ch : trimmed) {
        if (!(ch.isLetterOrNumber()
              || ch == QLatin1Char('_')
              || ch == QLatin1Char('$'))) {
            return false;
        }
    }
    return true;
}

bool wrappedByOuterParentheses(const QString& expression)
{
    const QString trimmed = expression.trimmed();
    if (!trimmed.startsWith(QLatin1Char('('))
        || !trimmed.endsWith(QLatin1Char(')'))) {
        return false;
    }

    int depth = 0;
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (ch == QLatin1Char('('))
            ++depth;
        else if (ch == QLatin1Char(')'))
            --depth;
        if (depth == 0 && i < trimmed.size() - 1)
            return false;
        if (depth < 0)
            return false;
    }
    return depth == 0;
}

QString adjustedExpressionBound(const QString& expression, bool increment)
{
    const QString trimmed = expression.trimmed();
    const QString base =
        simpleRangeExpression(trimmed) || wrappedByOuterParentheses(trimmed)
            ? trimmed
            : QStringLiteral("(%1)").arg(trimmed);
    return base + (increment ? QStringLiteral("+1") : QStringLiteral("-1"));
}

bool adjustSelectedRangeBound(MyCodeEditor* editor,
                              QKeyEvent* event)
{
    if (!editor || !event || hasCommandModifier(event))
        return false;
    const bool increment = event->key() == Qt::Key_Up;
    const bool decrement = event->key() == Qt::Key_Down;
    if (!increment && !decrement)
        return false;

    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection())
        return false;

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    if (selectionStart <= 0
        || selectionEnd >= editor->document()->characterCount())
        return false;
    if (editor->document()->characterAt(selectionStart - 1)
            != QLatin1Char('[')
        || editor->document()->characterAt(selectionEnd)
            != QLatin1Char(']')) {
        return false;
    }

    const QString selected = cursor.selectedText();
    const bool rightBound =
        event->modifiers().testFlag(Qt::ShiftModifier);
    int boundStart = -1;
    int boundEnd = -1;
    if (!selectedRangeBoundSpan(selected, rightBound, &boundStart, &boundEnd))
        return false;

    const QString currentText =
        selected.mid(boundStart, boundEnd - boundStart);
    int current = 0;
    QString nextText;
    if (parseNonNegativeInteger(currentText, &current)) {
        int lowerBound = 0;
        if (!rightBound) {
            int rightValue = 0;
            int rightStart = -1;
            int rightEnd = -1;
            if (selectedRangeBoundSpan(selected, true, &rightStart, &rightEnd)
                && parseNonNegativeInteger(
                    selected.mid(rightStart, rightEnd - rightStart),
                    &rightValue)) {
                lowerBound = rightValue;
            }
        }
        const int next = increment
            ? current + 1
            : qMax(lowerBound, current - 1);
        nextText = QString::number(next);
    } else {
        nextText = adjustedExpressionBound(currentText, increment);
    }

    const QString replacement =
        selected.left(boundStart)
        + nextText
        + selected.mid(boundEnd);

    cursor.insertText(replacement);
    QTextCursor reselection = editor->textCursor();
    reselection.setPosition(selectionStart);
    reselection.setPosition(selectionStart + replacement.size(),
                            QTextCursor::KeepAnchor);
    editor->setTextCursor(reselection);
    event->accept();
    return true;
}

bool hasColumnSelection(const MyCodeEditorState& state)
{
    return state.columnSelectionActive
        && state.columnAnchorLine >= 0
        && state.columnCurrentLine >= 0
        && state.columnAnchorColumn >= 0
        && state.columnCurrentColumn >= 0;
}

QPair<int, int> lineSpan(const MyCodeEditorState& state)
{
    return {qMin(state.columnAnchorLine, state.columnCurrentLine),
            qMax(state.columnAnchorLine, state.columnCurrentLine)};
}

QPair<int, int> columnSpan(const MyCodeEditorState& state)
{
    return {qMin(state.columnAnchorColumn, state.columnCurrentColumn),
            qMax(state.columnAnchorColumn, state.columnCurrentColumn)};
}

void removeColumnSelections(MyCodeEditor* editor)
{
    if (!editor)
        return;

    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    selections.erase(
        std::remove_if(selections.begin(),
                       selections.end(),
                       [](const QTextEdit::ExtraSelection& selection) {
                           return selection.format
                                      .property(kColumnSelectionProperty)
                                      .toInt()
                                  == kColumnSelectionMarker;
                       }),
        selections.end());
    editor->setExtraSelections(selections);
}

void clearColumnSelection(MyCodeEditor* editor, MyCodeEditorState& state)
{
    state.columnSelectionActive = false;
    state.columnSelectionDragging = false;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragMoved = false;
    state.columnAnchorLine = -1;
    state.columnAnchorColumn = -1;
    state.columnCurrentLine = -1;
    state.columnCurrentColumn = -1;
    removeColumnSelections(editor);
    if (editor)
        editor->viewport()->setCursor(Qt::IBeamCursor);
}

void updateColumnSelectionHighlight(MyCodeEditor* editor,
                                    const MyCodeEditorState& state)
{
    if (!editor)
        return;

    removeColumnSelections(editor);
    if (!hasColumnSelection(state))
        return;

    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid())
            continue;

        const int lineLength = block.text().size();
        const int startColumn = qMin(leftColumn, lineLength);
        const int visibleEndColumn =
            qMin(qMax(rightColumn, leftColumn + 1), lineLength);
        if (visibleEndColumn <= startColumn)
            continue;

        QTextCursor cursor(block);
        cursor.setPosition(block.position() + startColumn);
        cursor.setPosition(block.position() + visibleEndColumn,
                           QTextCursor::KeepAnchor);

        QTextEdit::ExtraSelection selection;
        selection.cursor = cursor;
        selection.format.setBackground(QColor(37, 99, 235, 80));
        selection.format.setProperty(kColumnSelectionProperty,
                                     kColumnSelectionMarker);
        selections.append(selection);
    }
    editor->setExtraSelections(selections);
}

void setColumnPointFromCursor(const QTextCursor& cursor,
                              int* line,
                              int* column)
{
    if (!cursor.block().isValid())
        return;
    if (line)
        *line = cursor.block().blockNumber();
    if (column)
        *column = qMax(0, cursor.position() - cursor.block().position());
}

bool beginColumnSelection(MyCodeEditor* editor,
                          QMouseEvent* event,
                          MyCodeEditorState& state)
{
    if (!editor || !event
        || event->button() != Qt::LeftButton
        || !event->modifiers().testFlag(Qt::ShiftModifier)
        || !event->modifiers().testFlag(Qt::AltModifier)) {
        return false;
    }

    const QTextCursor cursor =
        editor->cursorForPosition(event->position().toPoint());
    if (state.columnSelectionActive
        && state.columnSelectionAwaitingEndpoint) {
        setColumnPointFromCursor(cursor,
                                 &state.columnCurrentLine,
                                 &state.columnCurrentColumn);
        state.columnSelectionAwaitingEndpoint = false;
        state.columnSelectionDragging = false;
        state.columnSelectionDragMoved = false;
        updateColumnSelectionHighlight(editor, state);
        editor->viewport()->setCursor(Qt::CrossCursor);
        editor->viewport()->update();
        event->accept();
        return true;
    }

    state.columnSelectionActive = true;
    state.columnSelectionDragging = true;
    state.columnSelectionAwaitingEndpoint = true;
    state.columnSelectionDragMoved = false;
    setColumnPointFromCursor(cursor,
                             &state.columnAnchorLine,
                             &state.columnAnchorColumn);
    state.columnCurrentLine = state.columnAnchorLine;
    state.columnCurrentColumn = state.columnAnchorColumn;
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->setCursor(Qt::CrossCursor);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool updateColumnSelectionDrag(MyCodeEditor* editor,
                               QMouseEvent* event,
                               MyCodeEditorState& state)
{
    if (!editor || !event || !state.columnSelectionDragging)
        return false;
    if (!event->buttons().testFlag(Qt::LeftButton))
        return false;

    const QTextCursor cursor =
        editor->cursorForPosition(event->position().toPoint());
    state.columnSelectionDragMoved = true;
    state.columnSelectionAwaitingEndpoint = false;
    setColumnPointFromCursor(cursor,
                             &state.columnCurrentLine,
                             &state.columnCurrentColumn);
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool endColumnSelectionDrag(MyCodeEditor* editor,
                            QMouseEvent* event,
                            MyCodeEditorState& state)
{
    if (!editor || !event || !state.columnSelectionDragging)
        return false;

    if (state.columnSelectionDragMoved) {
        const QTextCursor cursor =
            editor->cursorForPosition(event->position().toPoint());
        setColumnPointFromCursor(cursor,
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
                                   MyCodeEditorState& state)
{
    if (!editor || !event || !hasColumnSelection(state))
        return false;
    if (event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::MetaModifier)) {
        return false;
    }
    if (event->key() == Qt::Key_Return
        || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Tab
        || event->key() == Qt::Key_Escape) {
        return false;
    }

    const bool backspace = event->key() == Qt::Key_Backspace;
    const bool deleteKey = event->key() == Qt::Key_Delete;
    const bool printable = !event->text().isEmpty()
        && !backspace
        && !deleteKey;
    if (!printable && !backspace && !deleteKey)
        return false;

    const QString text = printable ? event->text() : QString();
    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    const bool hasWidth = rightColumn > leftColumn;
    const int editColumn = backspace && !hasWidth
        ? qMax(0, leftColumn - 1)
        : leftColumn;
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    for (int line = lastLine; line >= firstLine; --line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid())
            continue;

        const int lineLength = block.text().size();
        int startColumn = qMin(editColumn, lineLength);
        int endColumn = startColumn;
        if (hasWidth) {
            startColumn = qMin(leftColumn, lineLength);
            endColumn = qMin(rightColumn, lineLength);
        } else if (deleteKey && leftColumn < lineLength) {
            startColumn = leftColumn;
            endColumn = leftColumn + 1;
        } else if (backspace && leftColumn > 0 && editColumn < lineLength) {
            startColumn = editColumn;
            endColumn = qMin(leftColumn, lineLength);
        }

        cursor.setPosition(block.position() + startColumn);
        cursor.setPosition(block.position() + qMax(startColumn, endColumn),
                           QTextCursor::KeepAnchor);
        if (printable) {
            const QString padding =
                leftColumn > lineLength
                    ? QString(leftColumn - lineLength, QLatin1Char(' '))
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
        : (printable ? leftColumn + text.size() : editColumn);
    state.columnAnchorColumn = collapsedColumn;
    state.columnCurrentColumn = state.columnAnchorColumn;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragging = false;
    state.columnSelectionDragMoved = false;
    const QTextBlock currentBlock =
        editor->document()->findBlockByNumber(lastLine);
    if (currentBlock.isValid()) {
        QTextCursor caret(editor->document());
        caret.setPosition(currentBlock.position()
                          + qMin(state.columnCurrentColumn,
                                 currentBlock.text().size()));
        editor->setTextCursor(caret);
    }
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool handleColumnSelectionNavigation(MyCodeEditor* editor,
                                     QKeyEvent* event,
                                     MyCodeEditorState& state)
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
    editor->viewport()->update();
    event->accept();
    return true;
}

void paintColumnSelectionOverlay(MyCodeEditor* editor,
                                 const MyCodeEditorState& state,
                                 QPaintEvent* event)
{
    if (!editor || !event || !hasColumnSelection(state))
        return;

    const auto [leftColumn, rightColumn] = columnSpan(state);
    if (leftColumn != rightColumn)
        return;

    const auto [firstLine, lastLine] = lineSpan(state);
    QPainter painter(editor->viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(QColor(255, 87, 34));
    pen.setWidth(2);
    painter.setPen(pen);

    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid() || !block.isVisible())
            continue;

        const int column = qMin(leftColumn, block.text().size());
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + column);
        const QRect rect = editor->cursorRect(cursor);
        if (!event->rect().intersects(rect.adjusted(-4, -2, 4, 2)))
            continue;

        painter.drawLine(rect.left(),
                         rect.top() + 1,
                         rect.left(),
                         rect.bottom() - 1);
    }
}
}

void MyCodeEditorState::initializeCore(MyCodeEditor* editor)
{
    semantic.init();
    syntax.init();
    gutter.init(editor);
    identity.set(QString());
    editor->setMouseTracking(true);
    editor->setAcceptDrops(true);
}

void MyCodeEditorState::shutdown()
{
    sourceNavigation.shutdown();
    gutter.destroy();
}

void MyCodeEditorState::attachEditorConnections(MyCodeEditor* editor)
{
    highlightRefresh.attachToEditor(editor, [this, editor]() {
        refreshScopeAndCurrentLineHighlight(editor);
    });
    QObject::connect(
        editor,
        &QPlainTextEdit::blockCountChanged,
        editor,
        [this, editor]() {
            gutter.updateViewportMargins(editor);
        });
    QObject::connect(
        editor,
        &QPlainTextEdit::updateRequest,
        editor,
        [this, editor](const QRect& rect, int dy) {
            gutter.handleUpdateRequest(editor, rect, dy);
            if (dy != 0)
                sourceNavigation.handleEditorScrolled(editor, selections);
        });
    QObject::connect(
        editor,
        &QPlainTextEdit::textChanged,
        editor,
        [this, editor]() {
            sourceNavigation.handleEditorContentChanged(editor, selections);
            folding.refresh(editor, syntax.tsDocument());
            refreshGhostAnnotations(editor);
        });
}

void MyCodeEditorState::attachToEditor(MyCodeEditor* editor)
{
    initializeCore(editor);
    attachEditorConnections(editor);
    appearance.apply(editor);
    syntax.attachToEditor(editor);
    completionWorkflow.bind(
        editor,
        &completion,
        &modes,
        &selections,
        [this, editor](int cursorPosition, bool includeDocumentText) {
            return semanticContextForPosition(
                editor,
                cursorPosition,
                includeDocumentText);
        },
        [this](int charPos) {
            return currentModuleNameAt(charPos);
        },
        [this]() {
            return semanticService();
        });
    completion.attachToEditor(
        editor,
        [this]() {
            completionWorkflow.handleAutoCompleteTimer();
        },
        [this](const QModelIndex& index) {
            completionWorkflow.handleCompletionActivated(index);
        },
        [this]() {
            completionWorkflow.handleTextChanged();
        });
    selections.highlightCurrentLine(editor);
    folding.refresh(editor, syntax.tsDocument());
    gutter.updateViewportMargins(editor);
}

void MyCodeEditorState::applyAppearanceSettings(
    MyCodeEditor* editor,
    const EditorAppearanceOptions& options)
{
    appearance.apply(editor, options);
    gutter.updateViewportMargins(editor);
    handleResize(editor);
}

EditorSemanticContextService* MyCodeEditorState::semanticService() const
{
    return semantic.contextService();
}

EditorSourceContextProvider MyCodeEditorState::sourceContextProvider(
    const MyCodeEditor* editor) const
{
    return [this, editor](int cursorPosition, bool includeDocumentText) {
        return semanticContextForPosition(
            editor,
            cursorPosition,
            includeDocumentText);
    };
}

QString MyCodeEditorState::currentModuleNameAt(int charPos) const
{
    return syntax.moduleNameAt(charPos);
}

QString MyCodeEditorState::currentModuleName(const MyCodeEditor* editor) const
{
    return currentModuleNameAt(editor->textCursor().position());
}

EditorSemanticContext MyCodeEditorState::semanticContextForPosition(
    const MyCodeEditor* editor,
    int cursorPosition,
    bool includeDocumentText) const
{
    const int semanticPosition = cursorPosition >= 0
        ? cursorPosition
        : editor->textCursor().position();

    return semantic.contextForDocument(
        editor->document(),
        identity.current(),
        currentModuleNameAt(semanticPosition),
        semanticPosition,
        includeDocumentText);
}

void MyCodeEditorState::handleControlKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    sourceNavigation.handleControlKeyPress(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
}

void MyCodeEditorState::handleControlKeyRelease(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    sourceNavigation.handleControlKeyRelease(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
}

bool MyCodeEditorState::handleKeyPress(MyCodeEditor* editor, QKeyEvent* event)
{
    if (folding.foldRegionMarkModeActive()) {
        if (event->key() == Qt::Key_Escape)
            folding.cancelFoldRegionMarkMode(editor);
        event->accept();
        return true;
    }

    if (folding.foldShelfModeActive() && event->key() == Qt::Key_Escape) {
        folding.cancelFoldShelfMode(editor);
        event->accept();
        return true;
    }
    if (folding.foldShelfModeActive()
        && !event->text().isEmpty()
        && !event->modifiers().testFlag(Qt::ControlModifier)) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Fold Shelf: drag custom fold blocks"));
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Escape
        && sourceNavigation.handleEscape(editor, selections)) {
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Escape && columnSelectionActive) {
        clearColumnSelection(editor, *this);
        event->accept();
        return true;
    }

    handleControlKeyPress(editor, event);

    if (sourceNavigation.handleSourceSymbolShortcut(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor))) {
        return true;
    }

    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return true;
    }

    if (handleColumnSelectionNavigation(editor, event, *this))
        return true;

    if (handleColumnSelectionKeyInput(editor, event, *this))
        return true;

    if (adjustSelectedRangeBound(editor, event))
        return true;

    if (handleBracketRangeTab(editor, event))
        return true;

    if (handleBracketPairInsertion(editor, event))
        return true;

    if (modes.alternateModeActive) {
        completionWorkflow.handleAlternateModeKey(event);
        return true;
    }

    return completionWorkflow.handleCompletionPopupKey(event);
}

bool MyCodeEditorState::handleKeyRelease(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    handleControlKeyRelease(editor, event);
    return event->key() != Qt::Key_Shift && modes.alternateModeActive;
}

bool MyCodeEditorState::handleDragEnter(
    MyCodeEditor* editor,
    QDragEnterEvent* event)
{
    return folding.handleFoldShelfDragEnter(editor, event);
}

bool MyCodeEditorState::handleDragMove(
    MyCodeEditor* editor,
    QDragMoveEvent* event)
{
    return folding.handleFoldShelfDragMove(editor, event);
}

bool MyCodeEditorState::handleDrop(MyCodeEditor* editor, QDropEvent* event)
{
    return folding.handleFoldShelfDrop(editor, event);
}

void MyCodeEditorState::handleResize(MyCodeEditor* editor) const
{
    gutter.resizeTo(editor, editor->contentsRect());
}

bool MyCodeEditorState::handleGutterMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!event)
        return false;

    QTextBlock block = editor->firstVisibleBlock();
    int top = static_cast<int>(editor->blockBoundingGeometry(block)
                                   .translated(editor->contentOffset())
                                   .top());
    int bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    const int y = static_cast<int>(event->position().y());
    while (block.isValid()) {
        if (y >= top && y <= bottom) {
            if (folding.foldRegionMarkModeActive())
                return folding.handleFoldRegionGutterLine(editor, block.blockNumber());
            if (event->position().x() > 14)
                return false;
            return folding.toggleFoldAtLine(editor, block.blockNumber());
        }
        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    }
    return false;
}

bool MyCodeEditorState::handleGutterMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!editor || !event)
        return false;

    QTextBlock block = editor->firstVisibleBlock();
    int top = static_cast<int>(editor->blockBoundingGeometry(block)
                                   .translated(editor->contentOffset())
                                   .top());
    int bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    const int y = static_cast<int>(event->position().y());
    while (block.isValid()) {
        if (y >= top && y <= bottom) {
            const bool handled =
                folding.handleFoldRegionHoverLine(editor, block.blockNumber());
            if (handled)
                gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
            return handled;
        }
        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(editor->blockBoundingRect(block).height());
    }
    return false;
}

void MyCodeEditorState::paintGutterDecorations(
    MyCodeEditor* editor,
    QPainter& painter,
    const QRect& rect) const
{
    folding.paintGutter(editor, painter, rect);
}

void MyCodeEditorState::paintFoldPlaceholders(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    Q_UNUSED(event)
    QPainter painter(editor->viewport());
    folding.paintPlaceholders(editor, painter);
}

void MyCodeEditorState::paintGhostAnnotations(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    Q_UNUSED(event)
    if (!editor || ghostAnnotations.isEmpty())
        return;

    QTextDocument* textDocument = editor->document();
    if (!textDocument)
        return;

    QPainter painter(editor->viewport());
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont ghostFont = editor->font();
    ghostFont.setItalic(true);
    painter.setFont(ghostFont);

    QColor color = editor->palette().color(QPalette::Text);
    color.setAlpha(72);
    painter.setPen(color);

    const QFontMetrics metrics(painter.font());
    const int documentEnd = qMax(0, textDocument->characterCount() - 1);
    const int viewportWidth = editor->viewport()->width();
    const int viewportHeight = editor->viewport()->height();
    QHash<int, int> rightLineEndX;

    auto lineEndX = [editor, textDocument, documentEnd](const QTextBlock& block) {
        const int blockTextEnd =
            qBound(0, block.position() + block.text().size(), documentEnd);
        QTextCursor endCursor(textDocument);
        endCursor.setPosition(blockTextEnd);
        return editor->cursorRect(endCursor).right() + 12;
    };

    for (const GhostAnnotation& annotation : ghostAnnotations) {
        if (!annotation.isValid())
            continue;

        const int anchorPosition =
            qBound(0, annotation.anchorPosition, documentEnd);
        QTextBlock block = textDocument->findBlock(anchorPosition);
        if (!block.isValid() || !block.isVisible())
            continue;

        QTextCursor cursor(textDocument);
        cursor.setPosition(anchorPosition);
        const QRect anchorRect = editor->cursorRect(cursor);
        if (anchorRect.bottom() < 0 || anchorRect.top() > viewportHeight)
            continue;

        const int textWidth = metrics.horizontalAdvance(annotation.text);
        int x = -1;
        const int line =
            annotation.line > 0 ? annotation.line : block.blockNumber() + 1;
        if (annotation.placement == GhostAnnotationPlacement::LeftOfAnchor) {
            x = anchorRect.left() - textWidth - 8;
            if (x < 2)
                continue;
        } else {
            x = lineEndX(block);
            const int previousEnd = rightLineEndX.value(line, x);
            if (x < previousEnd + 12)
                x = previousEnd + 12;
        }

        if (x < 2 || x >= viewportWidth - 4)
            continue;
        if (x + textWidth > viewportWidth - 4)
            continue;
        if (annotation.placement != GhostAnnotationPlacement::LeftOfAnchor)
            rightLineEndX.insert(line, x + textWidth);

        const int baseline =
            anchorRect.top()
            + (anchorRect.height() + metrics.ascent() - metrics.descent()) / 2;
        painter.drawText(x, baseline, annotation.text);
    }
}

void MyCodeEditorState::paintColumnSelection(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    paintColumnSelectionOverlay(editor, *this, event);
}

void MyCodeEditorState::handleContextMenu(
    MyCodeEditor* editor,
    QContextMenuEvent* event)
{
    sourceNavigation.handleContextMenu(
        editor,
        event,
        sourceContextProvider(editor));
}

bool MyCodeEditorState::handleMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (folding.handleFoldShelfMousePress(editor, event))
        return true;

    if (beginColumnSelection(editor, event, *this))
        return true;

    if (columnSelectionActive
        && event
        && event->button() == Qt::LeftButton
        && !(event->modifiers().testFlag(Qt::ShiftModifier)
             && event->modifiers().testFlag(Qt::AltModifier))) {
        clearColumnSelection(editor, *this);
    }

    if (handleBracketRangeAltClick(editor, event))
        return true;

    return sourceNavigation.handleMousePress(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
}

bool MyCodeEditorState::handleMouseDoubleClick(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    return sourceNavigation.handleMouseDoubleClick(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
}

bool MyCodeEditorState::handleMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (updateColumnSelectionDrag(editor, event, *this))
        return true;

    if (folding.handleFoldRegionMouseMove(editor, event))
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);

    folding.handleFoldShelfHover(editor, event);
    if (folding.handleFoldShelfMouseMove(editor, event))
        return true;

    sourceNavigation.handleMouseMove(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
    return false;
}

bool MyCodeEditorState::handleMouseRelease(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    return endColumnSelectionDrag(editor, event, *this);
}

void MyCodeEditorState::handleLeaveEvent(MyCodeEditor* editor)
{
    sourceNavigation.handleLeave(editor, selections);
}

void MyCodeEditorState::refreshScopeAndCurrentLineHighlight(
    MyCodeEditor* editor)
{
    selections.highlightCurrentSymbolReferences(editor);
    selections.highlightCurrentLine(editor);
}

void MyCodeEditorState::setAlternateModeEnabled(bool enabled)
{
    modes.setAlternateModeEnabled(enabled);
}

void MyCodeEditorState::setIncludeFileProvider(
    EditorCompletionWorkflow::IncludeFileProvider provider)
{
    completionWorkflow.setIncludeFileProvider(std::move(provider));
}

void MyCodeEditorState::setIncludeNewHeaderCreator(
    EditorCompletionWorkflow::IncludeNewHeaderCreator creator)
{
    completionWorkflow.setIncludeNewHeaderCreator(std::move(creator));
}

void MyCodeEditorState::executeAlternateModeCommand(const QString& command)
{
    completionWorkflow.executeAlternateModeCommand(command);
}

void MyCodeEditorState::executeEditorActionCommand(
    MyCodeEditor* editor,
    const QString& command)
{
    if (command == QStringLiteral("format_document"))
        formatDocument(editor);
    else if (command == QStringLiteral("format_selection"))
        formatSelection(editor);
}

void MyCodeEditorState::setFormatterProfile(FormatterProfile profile)
{
    currentFormatterProfile = profile;
}

FormatterProfile MyCodeEditorState::formatterProfile() const
{
    return currentFormatterProfile;
}

void MyCodeEditorState::setFormatOnSaveEnabled(bool enabled)
{
    currentFormatOnSaveEnabled = enabled;
}

bool MyCodeEditorState::formatOnSaveEnabled() const
{
    return currentFormatOnSaveEnabled;
}

void MyCodeEditorState::formatDocument(MyCodeEditor* editor)
{
    if (!editor)
        return;

    const FormatterReport report =
        FormatterService::getInstance()->formatDocument(
            editor->toPlainText(),
            currentFormatterProfile);
    if (!report.changed) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Document already formatted"));
        return;
    }

    QTextCursor cursor = editor->textCursor();
    const int oldPosition = cursor.position();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(report.formattedText);
    cursor.endEditBlock();

    QTextCursor nextCursor = editor->textCursor();
    nextCursor.setPosition(qMin(oldPosition, editor->document()->characterCount() - 1));
    editor->setTextCursor(nextCursor);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Formatted document (%1 lines, %2)")
            .arg(report.formattedLines)
            .arg(FormatterService::profileDisplayName(currentFormatterProfile)));
}

bool MyCodeEditorState::formatDocumentForSave(MyCodeEditor* editor)
{
    if (!editor || !currentFormatOnSaveEnabled)
        return false;

    const FormatterReport report =
        FormatterService::getInstance()->formatDocument(
            editor->toPlainText(),
            currentFormatterProfile);
    if (!report.changed)
        return false;

    QTextCursor cursor = editor->textCursor();
    const int oldPosition = cursor.position();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(report.formattedText);
    cursor.endEditBlock();

    QTextCursor nextCursor = editor->textCursor();
    nextCursor.setPosition(
        qMin(oldPosition, editor->document()->characterCount() - 1));
    editor->setTextCursor(nextCursor);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Formatted document on save (%1 lines, %2)")
            .arg(report.formattedLines)
            .arg(FormatterService::profileDisplayName(currentFormatterProfile)));
    return true;
}

void MyCodeEditorState::formatSelection(MyCodeEditor* editor)
{
    if (!editor)
        return;

    int rangeStart = -1;
    int rangeEnd = -1;
    if (!selectedFullLineRange(editor, &rangeStart, &rangeEnd)) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("No selection to format"));
        return;
    }

    const QString selectedText =
        editor->toPlainText().mid(rangeStart, rangeEnd - rangeStart);
    const FormatterReport report =
        FormatterService::getInstance()->formatSelection(
            selectedText,
            currentFormatterProfile);
    if (!report.changed) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Selection already formatted"));
        return;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(rangeStart);
    cursor.setPosition(rangeEnd, QTextCursor::KeepAnchor);
    cursor.insertText(report.formattedText);
    cursor.endEditBlock();

    QTextCursor nextCursor = editor->textCursor();
    nextCursor.setPosition(rangeStart);
    nextCursor.setPosition(rangeStart + report.formattedText.size(),
                           QTextCursor::KeepAnchor);
    editor->setTextCursor(nextCursor);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Formatted selection (%1 lines, %2)")
            .arg(report.formattedLines)
            .arg(FormatterService::profileDisplayName(currentFormatterProfile)));
}

void MyCodeEditorState::startFoldRegionMarkMode(MyCodeEditor* editor)
{
    folding.startFoldRegionMarkMode(editor);
    if (editor)
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
}

void MyCodeEditorState::cancelFoldRegionMarkMode(MyCodeEditor* editor)
{
    folding.cancelFoldRegionMarkMode(editor);
    if (editor)
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
}

bool MyCodeEditorState::foldRegionMarkModeActive() const
{
    return folding.foldRegionMarkModeActive();
}

void MyCodeEditorState::startFoldShelfMode(MyCodeEditor* editor)
{
    folding.startFoldShelfMode(editor);
    if (editor)
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
}

void MyCodeEditorState::cancelFoldShelfMode(MyCodeEditor* editor)
{
    folding.cancelFoldShelfMode(editor);
    if (editor)
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
}

bool MyCodeEditorState::foldShelfModeActive() const
{
    return folding.foldShelfModeActive();
}

bool MyCodeEditorState::insertCustomFoldMarkers(
    MyCodeEditor* editor,
    int startLine,
    int endLine,
    const QString& alias)
{
    return folding.insertCustomFoldMarkers(editor, startLine, endLine, alias);
}

FoldShelfItem MyCodeEditorState::foldShelfItemAtLine(
    MyCodeEditor* editor,
    int line,
    FoldShelfOriginKind origin) const
{
    return folding.foldShelfItemAtLine(editor, line, origin);
}

bool MyCodeEditorState::deleteCustomFoldAtLine(MyCodeEditor* editor, int line)
{
    return folding.deleteCustomFoldAtLine(editor, line);
}

bool MyCodeEditorState::insertFoldShelfItemAtLine(
    MyCodeEditor* editor,
    const FoldShelfItem& item,
    int line)
{
    return folding.insertShelfItemAtLine(editor, item, line);
}

void MyCodeEditorState::setSemanticContextService(
    EditorSemanticContextService* service)
{
    semantic.setService(service);
}

EditorBlockGeometry MyCodeEditorState::blockGeometry(
    const MyCodeEditor* editor,
    int blockNumber) const
{
    return geometry.blockGeometry(editor, blockNumber);
}

qreal MyCodeEditorState::documentHeightPx(const MyCodeEditor* editor) const
{
    return geometry.documentHeightPx(editor);
}

void MyCodeEditorState::setDocumentFileName(
    MyCodeEditor* editor,
    QString fileName)
{
    if (!identity.set(fileName))
        return;

    emit editor->fileNameChanged(identity.current());
}

QString MyCodeEditorState::documentFileName() const
{
    return identity.current();
}

void MyCodeEditorState::setDiagnosticHighlights(
    MyCodeEditor* editor,
    const QList<SemanticDiagnostic>& diagnostics)
{
    selections.highlightDiagnostics(editor, diagnostics);
}

void MyCodeEditorState::setSemanticDecorations(
    MyCodeEditor* editor,
    const QList<SemanticDecoration>& decorations)
{
    selections.highlightSemanticDecorations(editor, decorations);
}

void MyCodeEditorState::refreshGhostAnnotations(MyCodeEditor* editor)
{
    if (!editor || identity.current().isEmpty()) {
        setGhostAnnotations(editor, {});
        return;
    }
    if (editor->document()->characterCount()
        > kMaxPassiveGhostAnnotationCharacters) {
        setGhostAnnotations(editor, {});
        return;
    }

    GhostAnnotationQuery query;
    query.fileName = identity.current();
    query.documentText = editor->toPlainText();
    setGhostAnnotations(
        editor,
        GhostAnnotationService::getInstance()
            ->annotationsForDocument(query)
            .annotations);
}

void MyCodeEditorState::setGhostAnnotations(
    MyCodeEditor* editor,
    const QList<GhostAnnotation>& annotations)
{
    ghostAnnotations = annotations;
    if (editor)
        editor->viewport()->update();
}

void MyCodeEditorState::highlightSearchMatches(
    MyCodeEditor* editor,
    const QString& text,
    bool caseSensitive)
{
    selections.highlightSearchMatches(editor, text, caseSensitive);
}

void MyCodeEditorState::clearSearchMatches(MyCodeEditor* editor)
{
    selections.clearSearchMatches(editor);
}

void MyCodeEditorState::flashLine(MyCodeEditor* editor, int lineNumber)
{
    selections.flashLine(editor, lineNumber);
}

void MyCodeEditorState::applyLineNavigationTarget(
    MyCodeEditor* editor,
    const SourceLineNavigationTarget& target)
{
    cursorNavigation.applyLineTarget(editor, target);
    selections.flashLine(editor);
}
