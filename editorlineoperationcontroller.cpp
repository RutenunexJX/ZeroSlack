#include "editorlineoperationcontroller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace {
struct TextRange
{
    int start = 0;
    int end = 0;

    bool isValid() const
    {
        return start >= 0 && end >= start;
    }

    bool isEmpty() const
    {
        return start == end;
    }
};

struct LineMoveRange
{
    QTextBlock firstBlock;
    QTextBlock lastBlock;
    int selectionStart = -1;
    int selectionEnd = -1;
    int originalLineOffset = 0;
    int originalColumn = 0;
    bool hasSelection = false;
};

EditorLineOperationResult successResult(
    bool changed = false)
{
    EditorLineOperationResult result;
    result.handled = true;
    result.succeeded = true;
    result.documentChanged = changed;
    return result;
}

EditorLineOperationResult failureResult(
    const QString& reason,
    bool handled = true)
{
    EditorLineOperationResult result;
    result.handled = handled;
    result.failureReason = reason;
    return result;
}

int documentEnd(const QTextDocument* document)
{
    return document
        ? qMax(0, document->characterCount() - 1)
        : 0;
}

QTextBlock blockAtCursor(const QTextCursor& cursor)
{
    QTextDocument* document = cursor.document();
    if (!document)
        return {};
    const int position = qBound(
        0, cursor.position(), documentEnd(document));
    return document->findBlock(position);
}

QString plainTextInRange(QTextDocument* document,
                         const TextRange& range)
{
    if (!document || !range.isValid()
        || range.isEmpty()) {
        return {};
    }

    QTextCursor selected(document);
    selected.setPosition(range.start);
    selected.setPosition(
        range.end, QTextCursor::KeepAnchor);
    QString text = selected.selectedText();
    text.replace(QChar::ParagraphSeparator,
                 QLatin1Char('\n'));
    text.replace(QChar::LineSeparator,
                 QLatin1Char('\n'));
    return text;
}

TextRange selectedRange(const QTextCursor& cursor)
{
    return {cursor.selectionStart(),
            cursor.selectionEnd()};
}

TextRange currentLineClipboardRange(
    const QTextCursor& cursor)
{
    QTextDocument* document = cursor.document();
    const QTextBlock block = blockAtCursor(cursor);
    if (!document || !block.isValid())
        return {-1, -1};

    const QTextBlock next = block.next();
    if (next.isValid())
        return {block.position(), next.position()};

    const int end = documentEnd(document);
    if (block.text().isEmpty()
        && block.position() > 0) {
        return {block.position() - 1,
                block.position()};
    }
    return {block.position(), end};
}

TextRange currentLineRemovalRange(
    const QTextCursor& cursor)
{
    QTextDocument* document = cursor.document();
    const QTextBlock block = blockAtCursor(cursor);
    if (!document || !block.isValid())
        return {-1, -1};

    const QTextBlock next = block.next();
    if (next.isValid())
        return {block.position(), next.position()};

    const int end = documentEnd(document);
    if (block.position() > 0)
        return {block.position() - 1, end};
    return {0, end};
}

TextRange selectedWholeLineRange(
    const QTextCursor& cursor)
{
    QTextDocument* document = cursor.document();
    if (!document)
        return {-1, -1};
    if (!cursor.hasSelection())
        return currentLineRemovalRange(cursor);

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock first =
        document->findBlock(selectionStart);
    const int lastProbe = selectionEnd > selectionStart
        ? selectionEnd - 1
        : selectionEnd;
    const QTextBlock last =
        document->findBlock(lastProbe);
    if (!first.isValid() || !last.isValid())
        return {-1, -1};

    const QTextBlock afterLast = last.next();
    if (afterLast.isValid()) {
        return {first.position(),
                afterLast.position()};
    }

    int start = first.position();
    if (first.previous().isValid())
        --start;
    return {start, documentEnd(document)};
}

QClipboard* resolvedClipboard(
    QClipboard* requested)
{
    if (requested)
        return requested;
    if (!QGuiApplication::instance())
        return nullptr;
    return QGuiApplication::clipboard();
}

bool writeClipboardText(QClipboard* clipboard,
                        const QString& text)
{
    QClipboard* target =
        resolvedClipboard(clipboard);
    if (!target)
        return false;
    target->setText(text, QClipboard::Clipboard);
    return true;
}

bool removeRange(QTextCursor& cursor,
                 const TextRange& range)
{
    QTextDocument* document = cursor.document();
    if (!document || !range.isValid()
        || range.isEmpty()) {
        return false;
    }

    QTextCursor edit(document);
    edit.setPosition(range.start);
    edit.setPosition(
        range.end, QTextCursor::KeepAnchor);
    edit.beginEditBlock();
    edit.removeSelectedText();
    edit.endEditBlock();
    cursor = edit;
    return true;
}

bool replaceRange(QTextCursor& cursor,
                  const TextRange& range,
                  const QString& replacement)
{
    QTextDocument* document = cursor.document();
    if (!document || !range.isValid()
        || range.isEmpty()) {
        return false;
    }

    QTextCursor edit(document);
    edit.setPosition(range.start);
    edit.setPosition(
        range.end, QTextCursor::KeepAnchor);
    edit.beginEditBlock();
    edit.insertText(replacement);
    edit.endEditBlock();
    cursor = edit;
    return true;
}

LineMoveRange lineMoveRange(const QTextCursor& cursor)
{
    LineMoveRange range;
    QTextDocument* document = cursor.document();
    if (!document)
        return range;

    range.hasSelection = cursor.hasSelection()
        && cursor.selectionEnd() > cursor.selectionStart();
    range.selectionStart = cursor.selectionStart();
    range.selectionEnd = cursor.selectionEnd();
    if (!range.hasSelection) {
        range.firstBlock = cursor.block();
        range.lastBlock = cursor.block();
        range.originalColumn = cursor.block().isValid()
            ? qMax(0,
                   cursor.position()
                       - cursor.block().position())
            : 0;
        return range;
    }

    const int adjustedEnd = qMax(
        range.selectionStart,
        range.selectionEnd - 1);
    range.firstBlock =
        document->findBlock(range.selectionStart);
    range.lastBlock =
        document->findBlock(adjustedEnd);
    if (range.firstBlock.isValid()
        && cursor.block().isValid()) {
        range.originalLineOffset = qMax(
            0,
            cursor.block().blockNumber()
                - range.firstBlock.blockNumber());
        range.originalColumn = qMax(
            0,
            cursor.position()
                - cursor.block().position());
    }
    return range;
}

int lineMoveBlockEnd(
    QTextDocument* document,
    const QTextBlock& block)
{
    if (!document || !block.isValid())
        return -1;
    const int plainTextLength =
        qMax(0, document->characterCount() - 1);
    int end = block.position() + block.text().size();
    if (end < plainTextLength)
        ++end;
    return end;
}

void restoreLineMoveCursor(
    QTextCursor& cursor,
    const LineMoveRange& range,
    int movedFirstLine,
    int positionShift)
{
    QTextDocument* document = cursor.document();
    if (!document)
        return;
    if (range.hasSelection) {
        QTextCursor selection(document);
        selection.setPosition(
            qMax(0,
                 range.selectionStart + positionShift));
        selection.setPosition(
            qMax(0,
                 range.selectionEnd + positionShift),
            QTextCursor::KeepAnchor);
        cursor = selection;
        return;
    }

    const QTextBlock targetBlock =
        document->findBlockByNumber(
            movedFirstLine + range.originalLineOffset);
    if (!targetBlock.isValid())
        return;
    QTextCursor restored(document);
    restored.setPosition(
        targetBlock.position()
        + qMin(range.originalColumn,
               targetBlock.text().size()));
    cursor = restored;
}
}

EditorLineOperationResult
EditorLineOperationController::execute(
    EditorLineOperation operation,
    QTextCursor& cursor,
    QClipboard* clipboard) const
{
    switch (operation) {
    case EditorLineOperation::Copy:
        return copy(cursor, clipboard);
    case EditorLineOperation::Cut:
        return cut(cursor, clipboard);
    case EditorLineOperation::DeleteLines:
        return deleteLines(cursor);
    case EditorLineOperation::JoinWithNextLine:
        return joinLines(cursor);
    case EditorLineOperation::MoveLinesUp:
        return moveLines(cursor, true);
    case EditorLineOperation::MoveLinesDown:
        return moveLines(cursor, false);
    }
    return failureResult(
        QStringLiteral("Unknown line operation."),
        false);
}

EditorLineOperationResult
EditorLineOperationController::copy(
    QTextCursor& cursor,
    QClipboard* clipboard) const
{
    QTextDocument* document = cursor.document();
    if (!document) {
        return failureResult(
            QStringLiteral(
                "No text document is available."));
    }

    const TextRange range = cursor.hasSelection()
        ? selectedRange(cursor)
        : currentLineClipboardRange(cursor);
    if (!range.isValid()) {
        return failureResult(
            QStringLiteral(
                "The current line is unavailable."));
    }

    const QString text =
        plainTextInRange(document, range);
    if (!writeClipboardText(clipboard, text)) {
        return failureResult(
            QStringLiteral(
                "The system clipboard is unavailable."));
    }

    EditorLineOperationResult result =
        successResult();
    result.clipboardText = text;
    return result;
}

EditorLineOperationResult
EditorLineOperationController::cut(
    QTextCursor& cursor,
    QClipboard* clipboard) const
{
    QTextDocument* document = cursor.document();
    if (!document) {
        return failureResult(
            QStringLiteral(
                "No text document is available."));
    }

    const bool hadSelection = cursor.hasSelection();
    const TextRange clipboardRange = hadSelection
        ? selectedRange(cursor)
        : currentLineClipboardRange(cursor);
    const TextRange removalRange = hadSelection
        ? selectedRange(cursor)
        : currentLineRemovalRange(cursor);
    if (!clipboardRange.isValid()
        || !removalRange.isValid()) {
        return failureResult(
            QStringLiteral(
                "The current text range is unavailable."));
    }

    const QString text =
        plainTextInRange(document, clipboardRange);
    if (!writeClipboardText(clipboard, text)) {
        return failureResult(
            QStringLiteral(
                "The system clipboard is unavailable."));
    }

    const bool changed =
        removeRange(cursor, removalRange);
    EditorLineOperationResult result =
        successResult(changed);
    result.clipboardText = text;
    return result;
}

EditorLineOperationResult
EditorLineOperationController::deleteLines(
    QTextCursor& cursor) const
{
    if (!cursor.document()) {
        return failureResult(
            QStringLiteral(
                "No text document is available."));
    }

    const TextRange range =
        selectedWholeLineRange(cursor);
    if (!range.isValid()) {
        return failureResult(
            QStringLiteral(
                "The selected lines are unavailable."));
    }
    return successResult(removeRange(cursor, range));
}

EditorLineOperationResult
EditorLineOperationController::joinLines(
    QTextCursor& cursor) const
{
    QTextDocument* document = cursor.document();
    if (!document) {
        return failureResult(
            QStringLiteral(
                "No text document is available."));
    }

    QTextBlock first =
        document->findBlock(cursor.selectionStart());
    QTextBlock last = first;
    if (cursor.hasSelection()) {
        const int lastProbe =
            qMax(cursor.selectionStart(),
                 cursor.selectionEnd() - 1);
        last = document->findBlock(lastProbe);
    } else if (first.isValid()) {
        last = first.next();
    }
    if (!first.isValid()
        || !last.isValid()
        || first == last) {
        return failureResult(
            QStringLiteral(
                "At least two logical lines are required."));
    }

    QString joined = first.text();
    int firstBoundaryPosition = -1;
    QTextBlock block = first.next();
    while (block.isValid()) {
        int leftLength = joined.size();
        while (leftLength > 0
               && joined.at(leftLength - 1).isSpace()) {
            --leftLength;
        }
        const QString nextText = block.text();
        int rightOffset = 0;
        while (rightOffset < nextText.size()
               && nextText.at(rightOffset).isSpace()) {
            ++rightOffset;
        }
        joined.truncate(leftLength);
        if (leftLength > 0
            && rightOffset < nextText.size()) {
            joined.append(QLatin1Char(' '));
        }
        if (firstBoundaryPosition < 0)
            firstBoundaryPosition = joined.size();
        joined += nextText.mid(rightOffset);
        if (block == last)
            break;
        block = block.next();
    }
    if (block != last) {
        return failureResult(
            QStringLiteral(
                "The selected logical-line range is invalid."));
    }

    const int firstPosition = first.position();
    const TextRange replacementRange = {
        firstPosition,
        last.position() + last.length() - 1,
    };
    const bool changed =
        replaceRange(cursor,
                     replacementRange,
                     joined);
    if (changed && firstBoundaryPosition >= 0) {
        cursor.setPosition(
            firstPosition + firstBoundaryPosition);
    }
    return successResult(changed);
}

EditorLineOperationResult
EditorLineOperationController::joinWithNextLine(
    QTextCursor& cursor) const
{
    return joinLines(cursor);
}

EditorLineOperationResult
EditorLineOperationController::moveLines(
    QTextCursor& cursor,
    bool up) const
{
    QTextDocument* document = cursor.document();
    const LineMoveRange range = lineMoveRange(cursor);
    if (!document
        || !range.firstBlock.isValid()
        || !range.lastBlock.isValid()) {
        return failureResult(
            QStringLiteral(
                "The selected logical lines are unavailable."));
    }
    if ((up && !range.firstBlock.previous().isValid())
        || (!up && !range.lastBlock.next().isValid())) {
        return successResult();
    }

    const QString text = document->toPlainText();
    const int start = range.firstBlock.position();
    const int end = lineMoveBlockEnd(
        document, range.lastBlock);
    if (start < 0 || end <= start || end > text.size()) {
        return failureResult(
            QStringLiteral(
                "The selected logical-line range is invalid."));
    }

    const QString movedText = text.mid(
        start, end - start);
    QTextCursor edit(document);
    if (up) {
        const QTextBlock previousBlock =
            range.firstBlock.previous();
        const int previousStart =
            previousBlock.position();
        const int previousLength =
            start - previousStart;
        edit.beginEditBlock();
        edit.setPosition(start);
        edit.setPosition(
            end, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        edit.setPosition(previousStart);
        edit.insertText(movedText);
        edit.endEditBlock();
        restoreLineMoveCursor(
            cursor,
            range,
            previousBlock.blockNumber(),
            -previousLength);
    } else {
        const QTextBlock nextBlock =
            range.lastBlock.next();
        const int nextEnd = lineMoveBlockEnd(
            document, nextBlock);
        if (nextEnd <= end || nextEnd > text.size()) {
            return failureResult(
                QStringLiteral(
                    "The next logical line is unavailable."));
        }
        const int nextLength = nextEnd - end;
        edit.beginEditBlock();
        edit.setPosition(nextEnd);
        edit.insertText(movedText);
        edit.setPosition(start);
        edit.setPosition(
            end, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        edit.endEditBlock();
        restoreLineMoveCursor(
            cursor,
            range,
            range.firstBlock.blockNumber() + 1,
            nextLength);
    }
    return successResult(true);
}
