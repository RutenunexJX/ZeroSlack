#include "editorruntime.h"

#include "mycodeeditor.h"

#include "rtlbatcheditservice.h"

#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QInputDialog>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QStringList>

#include <utility>

#include "formatterservice.h"
#include "tsdocument.h"

namespace {
constexpr int kMaxPassiveGhostAnnotationCharacters = 2 * 1024 * 1024;
constexpr int kColumnSelectionProperty = QTextFormat::UserProperty + 20;
constexpr int kColumnSelectionMarker = 1020;
constexpr int kManualIndentWidth = 4;

struct TextSpan {
    int start = -1;
    int end = -1;

    bool isValid() const { return start >= 0 && end > start; }
    int length() const { return end - start; }
    bool contains(const TextSpan& other) const
    {
        return isValid() && other.isValid()
            && start <= other.start && end >= other.end;
    }
    bool operator==(const TextSpan& other) const
    {
        return start == other.start && end == other.end;
    }
};

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

bool isSmartIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

bool isSmartIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

TextSpan symbolSpanAt(const QString& text, int position)
{
    if (text.isEmpty())
        return {};

    int pos = qBound(0, position, text.size());
    if (pos >= text.size() || !isSmartIdentifierPart(text.at(pos))) {
        if (pos > 0 && isSmartIdentifierPart(text.at(pos - 1)))
            --pos;
        else
            return {};
    }

    int start = pos;
    while (start > 0 && isSmartIdentifierPart(text.at(start - 1)))
        --start;
    if (start >= text.size() || !isSmartIdentifierStart(text.at(start)))
        return {};

    int end = pos + 1;
    while (end < text.size() && isSmartIdentifierPart(text.at(end)))
        ++end;
    return {start, end};
}

TextSpan identifierSpanEndingAt(const QString& text, int end)
{
    int pos = end - 1;
    if (pos < 0 || pos >= text.size() || !isSmartIdentifierPart(text.at(pos)))
        return {};

    int start = pos;
    while (start > 0 && isSmartIdentifierPart(text.at(start - 1)))
        --start;
    if (!isSmartIdentifierStart(text.at(start)))
        return {};
    return {start, end};
}

TextSpan identifierSpanStartingAt(const QString& text, int start)
{
    if (start < 0 || start >= text.size()
        || !isSmartIdentifierStart(text.at(start))) {
        return {};
    }

    int end = start + 1;
    while (end < text.size() && isSmartIdentifierPart(text.at(end)))
        ++end;
    return {start, end};
}

TextSpan hierarchicalExpressionSpan(const QString& text, TextSpan span)
{
    if (!span.isValid())
        return {};

    TextSpan result = span;
    while (result.start >= 2 && text.at(result.start - 1) == QLatin1Char('.')) {
        const TextSpan previous =
            identifierSpanEndingAt(text, result.start - 1);
        if (!previous.isValid())
            break;
        result.start = previous.start;
    }
    while (result.end + 1 < text.size()
           && text.at(result.end) == QLatin1Char('.')) {
        const TextSpan next =
            identifierSpanStartingAt(text, result.end + 1);
        if (!next.isValid())
            break;
        result.end = next.end;
    }

    return result == span ? TextSpan{} : result;
}

TextSpan parenthesizedContentSpan(const QString& text, TextSpan currentSpan)
{
    if (text.isEmpty())
        return {};

    const int targetStart = currentSpan.isValid()
        ? currentSpan.start
        : qBound(0, currentSpan.start, text.size());
    const int targetEnd = currentSpan.isValid()
        ? currentSpan.end
        : targetStart;

    QList<TextSpan> stack;
    TextSpan best;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('(')) {
            stack.append({i, i + 1});
            continue;
        }
        if (ch != QLatin1Char(')') || stack.isEmpty())
            continue;

        const TextSpan opening = stack.takeLast();
        const TextSpan content{opening.start + 1, i};
        if (!content.isValid())
            continue;
        if (content.start > targetStart || content.end < targetEnd)
            continue;
        if (currentSpan.isValid() && content == currentSpan)
            continue;
        if (!best.isValid() || content.length() < best.length())
            best = content;
    }
    return best;
}

void selectTextSpan(MyCodeEditor* editor, TextSpan span)
{
    if (!editor || !span.isValid())
        return;

    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(span.start);
    cursor.setPosition(span.end, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
}

bool handleSmartSelectionExpansion(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !event
        || event->key() != Qt::Key_W
        || !event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::MetaModifier)) {
        return false;
    }

    const QString text = editor->toPlainText();
    QTextCursor cursor = editor->textCursor();
    const TextSpan current = cursor.hasSelection()
        ? TextSpan{cursor.selectionStart(), cursor.selectionEnd()}
        : TextSpan{cursor.position(), cursor.position()};

    if (!cursor.hasSelection()) {
        const TextSpan symbol = symbolSpanAt(text, cursor.position());
        if (symbol.isValid()) {
            selectTextSpan(editor, symbol);
            event->accept();
            return true;
        }
    } else {
        const TextSpan symbol = symbolSpanAt(text, current.start);
        const TextSpan hierarchy =
            symbol.isValid() && current == symbol
                ? hierarchicalExpressionSpan(text, symbol)
                : hierarchicalExpressionSpan(text, current);
        if (hierarchy.isValid() && hierarchy.contains(current)) {
            selectTextSpan(editor, hierarchy);
            event->accept();
            return true;
        }
    }

    const TextSpan parenthesized = parenthesizedContentSpan(text, current);
    if (parenthesized.isValid()) {
        selectTextSpan(editor, parenthesized);
        event->accept();
        return true;
    }

    return false;
}

bool isStandaloneIdentifierText(const QString& text)
{
    if (text.isEmpty() || !isSmartIdentifierStart(text.at(0)))
        return false;
    for (int i = 1; i < text.size(); ++i) {
        if (!isSmartIdentifierPart(text.at(i)))
            return false;
    }
    return true;
}

QList<TextSpan> identifierOccurrences(const QString& text,
                                      const QString& symbol)
{
    QList<TextSpan> occurrences;
    if (!isStandaloneIdentifierText(symbol))
        return occurrences;

    int pos = 0;
    while (pos >= 0 && pos < text.size()) {
        pos = text.indexOf(symbol, pos, Qt::CaseSensitive);
        if (pos < 0)
            break;

        const int end = pos + symbol.size();
        const bool leftOk = pos == 0 || !isSmartIdentifierPart(text.at(pos - 1));
        const bool rightOk =
            end >= text.size() || !isSmartIdentifierPart(text.at(end));
        if (leftOk && rightOk)
            occurrences.append({pos, end});
        pos = end;
    }
    return occurrences;
}

bool handleSelectedSymbolOccurrenceNavigation(MyCodeEditor* editor,
                                              QKeyEvent* event)
{
    if (!editor || !event
        || !event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::MetaModifier)) {
        return false;
    }

    const bool next = event->key() == Qt::Key_E;
    const bool previous = event->key() == Qt::Key_Q;
    if (!next && !previous)
        return false;

    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection())
        return false;

    const QString symbol = cursor.selectedText();
    if (!isStandaloneIdentifierText(symbol))
        return false;

    const QList<TextSpan> occurrences =
        identifierOccurrences(editor->toPlainText(), symbol);
    if (occurrences.isEmpty())
        return false;

    const int currentStart = cursor.selectionStart();
    TextSpan target = occurrences.constFirst();
    if (next) {
        for (const TextSpan& occurrence : occurrences) {
            if (occurrence.start > currentStart) {
                target = occurrence;
                break;
            }
        }
    } else {
        target = occurrences.constLast();
        for (int i = occurrences.size() - 1; i >= 0; --i) {
            if (occurrences.at(i).start < currentStart) {
                target = occurrences.at(i);
                break;
            }
        }
    }

    selectTextSpan(editor, target);
    editor->centerCursor();
    event->accept();
    return true;
}

bool replaceIdentifierOccurrences(MyCodeEditor* editor,
                                  const QString& oldName,
                                  const QString& newName)
{
    if (!editor || oldName == newName)
        return false;

    const QList<TextSpan> occurrences =
        identifierOccurrences(editor->toPlainText(), oldName);
    if (occurrences.isEmpty())
        return false;

    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    for (int i = occurrences.size() - 1; i >= 0; --i) {
        const TextSpan span = occurrences.at(i);
        cursor.setPosition(span.start);
        cursor.setPosition(span.end, QTextCursor::KeepAnchor);
        cursor.insertText(newName);
    }
    cursor.endEditBlock();
    return true;
}

void selectFirstIdentifierOccurrence(MyCodeEditor* editor,
                                     const QString& name)
{
    if (!editor)
        return;
    const QList<TextSpan> occurrences =
        identifierOccurrences(editor->toPlainText(), name);
    if (!occurrences.isEmpty())
        selectTextSpan(editor, occurrences.constFirst());
}

bool promptForRenameName(MyCodeEditor* editor,
                         const QString& title,
                         const QString& label,
                         const QString& currentName,
                         QString* outName)
{
    if (outName)
        outName->clear();

    bool accepted = false;
    const QString newName = QInputDialog::getText(
        editor,
        title,
        label,
        QLineEdit::Normal,
        currentName,
        &accepted).trimmed();
    if (!accepted)
        return false;

    if (!isStandaloneIdentifierText(newName)) {
        QMessageBox::warning(
            editor,
            title,
            QStringLiteral("Enter a valid SystemVerilog identifier."));
        return false;
    }

    if (outName)
        *outName = newName;
    return true;
}

enum class RenameConflictChoice {
    Cancel,
    Force,
    RenameConflictFirst
};

RenameConflictChoice promptRenameConflictChoice(MyCodeEditor* editor,
                                                const QString& newName)
{
    QMessageBox box(editor);
    box.setWindowTitle(QStringLiteral("Rename Symbol"));
    box.setIcon(QMessageBox::Warning);
    box.setText(
        QStringLiteral("The name \"%1\" already exists in this file.")
            .arg(newName));
    QAbstractButton* forceButton =
        box.addButton(QStringLiteral("Force rename"),
                      QMessageBox::AcceptRole);
    QAbstractButton* renameConflictButton =
        box.addButton(QStringLiteral("Rename conflicting definition first"),
                      QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == forceButton)
        return RenameConflictChoice::Force;
    if (box.clickedButton() == renameConflictButton)
        return RenameConflictChoice::RenameConflictFirst;
    return RenameConflictChoice::Cancel;
}

bool handleSafeRename(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !event
        || event->key() != Qt::Key_R
        || !event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::ShiftModifier)
        || event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::MetaModifier)) {
        return false;
    }

    const QString text = editor->toPlainText();
    QTextCursor cursor = editor->textCursor();
    TextSpan symbolSpan = cursor.hasSelection()
        ? TextSpan{cursor.selectionStart(), cursor.selectionEnd()}
        : symbolSpanAt(text, cursor.position());
    if (!symbolSpan.isValid())
        return false;

    const QString oldName = text.mid(symbolSpan.start, symbolSpan.length());
    if (!isStandaloneIdentifierText(oldName))
        return false;

    bool handledByCoordinator = false;
    emit editor->safeRenameRequested(
        oldName,
        editor->editorSemanticContextForPosition(symbolSpan.start, true),
        &handledByCoordinator);
    if (handledByCoordinator) {
        event->accept();
        return true;
    }

    QString newName;
    if (!promptForRenameName(editor,
                             QStringLiteral("Rename Symbol"),
                             QStringLiteral("New name"),
                             oldName,
                             &newName)) {
        event->accept();
        return true;
    }
    if (newName == oldName) {
        event->accept();
        return true;
    }

    if (!identifierOccurrences(text, newName).isEmpty()) {
        const RenameConflictChoice choice =
            promptRenameConflictChoice(editor, newName);
        if (choice == RenameConflictChoice::Cancel) {
            event->accept();
            return true;
        }

        if (choice == RenameConflictChoice::RenameConflictFirst) {
            QString conflictReplacement;
            if (!promptForRenameName(
                    editor,
                    QStringLiteral("Rename Conflicting Definition"),
                    QStringLiteral("Temporary name"),
                    newName + QStringLiteral("_renamed"),
                    &conflictReplacement)) {
                event->accept();
                return true;
            }
            if (conflictReplacement == oldName
                || conflictReplacement == newName
                || !identifierOccurrences(editor->toPlainText(),
                                          conflictReplacement).isEmpty()) {
                QMessageBox::warning(
                    editor,
                    QStringLiteral("Rename Conflicting Definition"),
                    QStringLiteral("Choose a unique temporary name."));
                event->accept();
                return true;
            }
            replaceIdentifierOccurrences(editor, newName, conflictReplacement);
        }
    }

    replaceIdentifierOccurrences(editor, oldName, newName);
    selectFirstIdentifierOccurrence(editor, newName);
    event->accept();
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

bool isPlainCtrlShortcut(QKeyEvent* event, int key)
{
    if (!event || event->key() != key)
        return false;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    return modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::ShiftModifier)
        && !modifiers.testFlag(Qt::AltModifier)
        && !modifiers.testFlag(Qt::MetaModifier);
}

bool isPlainAltShortcut(QKeyEvent* event, int key)
{
    if (!event || event->key() != key)
        return false;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    return modifiers.testFlag(Qt::AltModifier)
        && !modifiers.testFlag(Qt::ShiftModifier)
        && !modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::MetaModifier);
}

bool handleDuplicateSelectionOrLine(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !isPlainCtrlShortcut(event, Qt::Key_D))
        return false;

    QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection()) {
        const int start = cursor.selectionStart();
        const int end = cursor.selectionEnd();
        const QString selected =
            editor->toPlainText().mid(start, end - start);
        cursor.beginEditBlock();
        cursor.setPosition(end);
        cursor.insertText(selected);
        cursor.endEditBlock();
        cursor.setPosition(end);
        cursor.setPosition(end + selected.size(), QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }

    const QTextBlock block = cursor.block();
    if (!block.isValid())
        return false;

    const QString lineText = block.text();
    const int column = qMax(0, cursor.position() - block.position());
    const int insertPos = block.position() + lineText.size();
    cursor.beginEditBlock();
    cursor.setPosition(insertPos);
    cursor.insertText(QStringLiteral("\n") + lineText);
    cursor.endEditBlock();

    QTextCursor duplicated(editor->document());
    duplicated.setPosition(insertPos + 1 + qMin(column, lineText.size()));
    editor->setTextCursor(duplicated);
    event->accept();
    return true;
}

struct LineBlockMoveRange {
    QTextBlock firstBlock;
    QTextBlock lastBlock;
    int selectionStart = -1;
    int selectionEnd = -1;
    int originalLineOffset = 0;
    int originalColumn = 0;
    bool hasSelection = false;
};

LineBlockMoveRange lineBlockMoveRange(MyCodeEditor* editor)
{
    LineBlockMoveRange range;
    if (!editor || !editor->document())
        return range;

    const QTextCursor cursor = editor->textCursor();
    range.hasSelection = cursor.hasSelection()
        && cursor.selectionEnd() > cursor.selectionStart();
    range.selectionStart = cursor.selectionStart();
    range.selectionEnd = cursor.selectionEnd();

    if (!range.hasSelection) {
        range.firstBlock = cursor.block();
        range.lastBlock = cursor.block();
        range.originalLineOffset = 0;
        range.originalColumn =
            cursor.block().isValid()
                ? qMax(0, cursor.position() - cursor.block().position())
                : 0;
        return range;
    }

    int adjustedEnd = range.selectionEnd;
    const QTextBlock endAtBlock =
        editor->document()->findBlock(range.selectionEnd);
    if (endAtBlock.isValid()
        && range.selectionEnd == endAtBlock.position()
        && range.selectionEnd > range.selectionStart) {
        --adjustedEnd;
    } else {
        --adjustedEnd;
    }

    range.firstBlock = editor->document()->findBlock(range.selectionStart);
    range.lastBlock =
        editor->document()->findBlock(qMax(range.selectionStart, adjustedEnd));
    if (range.firstBlock.isValid() && cursor.block().isValid()) {
        range.originalLineOffset =
            qMax(0, cursor.block().blockNumber()
                     - range.firstBlock.blockNumber());
        range.originalColumn =
            qMax(0, cursor.position() - cursor.block().position());
    }
    return range;
}

int blockRangeEndInPlainText(QTextDocument* document, const QTextBlock& block)
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

void restoreMovedLineCursor(MyCodeEditor* editor,
                            const LineBlockMoveRange& range,
                            int movedFirstLine,
                            int positionShift)
{
    if (!editor)
        return;

    if (range.hasSelection) {
        QTextCursor selection(editor->document());
        selection.setPosition(qMax(0, range.selectionStart + positionShift));
        selection.setPosition(qMax(0, range.selectionEnd + positionShift),
                              QTextCursor::KeepAnchor);
        editor->setTextCursor(selection);
        return;
    }

    const QTextBlock targetBlock =
        editor->document()->findBlockByNumber(movedFirstLine
                                              + range.originalLineOffset);
    if (!targetBlock.isValid())
        return;

    QTextCursor next(editor->document());
    next.setPosition(targetBlock.position()
                     + qMin(range.originalColumn, targetBlock.text().size()));
    editor->setTextCursor(next);
}

bool handleMoveLineBlock(MyCodeEditor* editor,
                         QKeyEvent* event,
                         bool columnSelectionActive)
{
    if (!editor
        || (!isPlainAltShortcut(event, Qt::Key_Up)
            && !isPlainAltShortcut(event, Qt::Key_Down))) {
        return false;
    }

    if (columnSelectionActive) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Column selection: Alt+Up/Down is disabled"));
        event->accept();
        return true;
    }

    const bool moveUp = event->key() == Qt::Key_Up;
    QTextDocument* document = editor->document();
    const QString text = editor->toPlainText();
    const LineBlockMoveRange range = lineBlockMoveRange(editor);
    if (!document || !range.firstBlock.isValid() || !range.lastBlock.isValid())
        return false;
    if (moveUp && !range.firstBlock.previous().isValid()) {
        event->accept();
        return true;
    }

    const int start = range.firstBlock.position();
    const int end = blockRangeEndInPlainText(document, range.lastBlock);
    if (start < 0 || end <= start || end > text.size())
        return false;

    const QString movedText = text.mid(start, end - start);
    QTextCursor cursor(document);
    if (moveUp) {
        const QTextBlock previousBlock = range.firstBlock.previous();
        const int previousStart = previousBlock.position();
        const int previousLength = start - previousStart;
        cursor.beginEditBlock();
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.setPosition(previousStart);
        cursor.insertText(movedText);
        cursor.endEditBlock();
        restoreMovedLineCursor(editor,
                               range,
                               previousBlock.blockNumber(),
                               -previousLength);
    } else {
        if (end >= text.size()) {
            event->accept();
            return true;
        }
        const QTextBlock nextBlock = range.lastBlock.next();
        if (!nextBlock.isValid()) {
            event->accept();
            return true;
        }
        const int nextEnd = blockRangeEndInPlainText(document, nextBlock);
        if (nextEnd <= end || nextEnd > text.size())
            return false;
        const int nextLength = nextEnd - end;
        cursor.beginEditBlock();
        cursor.setPosition(nextEnd);
        cursor.insertText(movedText);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.endEditBlock();
        restoreMovedLineCursor(editor,
                               range,
                               range.firstBlock.blockNumber() + 1,
                               nextLength);
    }

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

struct TouchedLineRange {
    int firstLine = -1;
    int lastLine = -1;
    int currentLine = -1;
    int currentColumn = 0;
    bool hadSelection = false;
};

TouchedLineRange touchedLineRange(MyCodeEditor* editor)
{
    TouchedLineRange range;
    if (!editor || !editor->document())
        return range;

    const QTextCursor cursor = editor->textCursor();
    range.currentLine = cursor.block().blockNumber();
    range.currentColumn =
        cursor.block().isValid()
            ? qMax(0, cursor.position() - cursor.block().position())
            : 0;
    range.hadSelection =
        cursor.hasSelection() && cursor.selectionEnd() > cursor.selectionStart();

    if (!range.hadSelection) {
        range.firstLine = range.currentLine;
        range.lastLine = range.currentLine;
        return range;
    }

    const int selectionStart = cursor.selectionStart();
    int adjustedEnd = cursor.selectionEnd();
    const QTextBlock endAtBlock =
        editor->document()->findBlock(adjustedEnd);
    if (endAtBlock.isValid()
        && adjustedEnd == endAtBlock.position()
        && adjustedEnd > selectionStart) {
        --adjustedEnd;
    } else {
        --adjustedEnd;
    }

    const QTextBlock firstBlock =
        editor->document()->findBlock(selectionStart);
    const QTextBlock lastBlock =
        editor->document()->findBlock(qMax(selectionStart, adjustedEnd));
    if (!firstBlock.isValid() || !lastBlock.isValid())
        return {};

    range.firstLine = firstBlock.blockNumber();
    range.lastLine = lastBlock.blockNumber();
    return range;
}

int leadingWhitespaceCount(const QString& text)
{
    int count = 0;
    while (count < text.size() && text.at(count).isSpace())
        ++count;
    return count;
}

int lineEndPosition(QTextDocument* document, int line)
{
    if (!document)
        return -1;
    const QTextBlock block = document->findBlockByNumber(line);
    if (!block.isValid())
        return -1;
    return block.position() + block.text().size();
}

void restoreLineActionCursor(MyCodeEditor* editor,
                             const TouchedLineRange& range,
                             const QList<int>& removedByLine,
                             bool commentAction)
{
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return;

    QTextDocument* document = editor->document();
    if (!document)
        return;

    if (range.hadSelection) {
        const QTextBlock firstBlock =
            document->findBlockByNumber(range.firstLine);
        if (!firstBlock.isValid())
            return;
        const int end = lineEndPosition(document, range.lastLine);
        if (end < firstBlock.position())
            return;
        QTextCursor cursor(document);
        cursor.setPosition(firstBlock.position());
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        return;
    }

    const QTextBlock currentBlock =
        document->findBlockByNumber(range.currentLine);
    if (!currentBlock.isValid())
        return;

    int nextColumn = range.currentColumn;
    if (commentAction) {
        const int indent = leadingWhitespaceCount(currentBlock.text());
        if (nextColumn >= indent)
            nextColumn += 3;
    } else if (range.currentLine >= range.firstLine
               && range.currentLine <= range.lastLine) {
        const int removed =
            removedByLine.value(range.currentLine - range.firstLine);
        if (removed > 0) {
            const int indent = leadingWhitespaceCount(currentBlock.text());
            if (nextColumn >= indent + removed)
                nextColumn -= removed;
            else if (nextColumn > indent)
                nextColumn = indent;
        }
    }

    QTextCursor cursor(document);
    cursor.setPosition(currentBlock.position()
                       + qMin(nextColumn, currentBlock.text().size()));
    editor->setTextCursor(cursor);
}

bool applyLineComment(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid())
            continue;
        const int insertPos =
            block.position() + leadingWhitespaceCount(block.text());
        cursor.setPosition(insertPos);
        cursor.insertText(QStringLiteral("// "));
    }
    cursor.endEditBlock();

    restoreLineActionCursor(editor, range, {}, true);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Commented %1 line%2")
            .arg(range.lastLine - range.firstLine + 1)
            .arg(range.lastLine == range.firstLine ? QString()
                                                   : QStringLiteral("s")));
    return true;
}

bool applyLineUncomment(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    QList<int> removedByLine;
    removedByLine.reserve(range.lastLine - range.firstLine + 1);

    int changedLines = 0;
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid()) {
            removedByLine.prepend(0);
            continue;
        }

        const QString text = block.text();
        const int indent = leadingWhitespaceCount(text);
        int removed = 0;
        if (text.mid(indent).startsWith(QStringLiteral("//"))) {
            removed = 2;
            if (indent + removed < text.size()
                && text.at(indent + removed) == QLatin1Char(' ')) {
                ++removed;
            }
            cursor.setPosition(block.position() + indent);
            cursor.setPosition(block.position() + indent + removed,
                               QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            ++changedLines;
        }
        removedByLine.prepend(removed);
    }
    cursor.endEditBlock();

    restoreLineActionCursor(editor, range, removedByLine, false);
    emit editor->editorStatusMessageRequested(
        changedLines > 0
            ? QStringLiteral("Uncommented %1 line%2")
                  .arg(changedLines)
                  .arg(changedLines == 1 ? QString()
                                         : QStringLiteral("s"))
            : QStringLiteral("No line comments to uncomment"));
    return changedLines > 0;
}

int leadingUnindentCount(const QString& text)
{
    if (text.startsWith(QLatin1Char('\t')))
        return 1;

    int count = 0;
    while (count < text.size()
           && count < kManualIndentWidth
           && text.at(count) == QLatin1Char(' ')) {
        ++count;
    }
    return count;
}

void restoreLinePrefixActionCursor(MyCodeEditor* editor,
                                   const TouchedLineRange& range,
                                   const QList<int>& insertedByLine,
                                   const QList<int>& removedByLine)
{
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return;

    QTextDocument* document = editor->document();
    if (!document)
        return;

    if (range.hadSelection) {
        const QTextBlock firstBlock =
            document->findBlockByNumber(range.firstLine);
        if (!firstBlock.isValid())
            return;
        const int end = lineEndPosition(document, range.lastLine);
        if (end < firstBlock.position())
            return;
        QTextCursor cursor(document);
        cursor.setPosition(firstBlock.position());
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        return;
    }

    const QTextBlock currentBlock =
        document->findBlockByNumber(range.currentLine);
    if (!currentBlock.isValid())
        return;

    const int index = range.currentLine - range.firstLine;
    const int inserted = insertedByLine.value(index);
    const int removed = removedByLine.value(index);
    int nextColumn = range.currentColumn + inserted;
    if (removed > 0)
        nextColumn = range.currentColumn >= removed
            ? range.currentColumn - removed
            : 0;

    QTextCursor cursor(document);
    cursor.setPosition(currentBlock.position()
                       + qMin(nextColumn, currentBlock.text().size()));
    editor->setTextCursor(cursor);
}

bool applyLineIndent(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    QList<int> insertedByLine;
    insertedByLine.reserve(range.lastLine - range.firstLine + 1);

    int changedLines = 0;
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid()) {
            insertedByLine.prepend(0);
            continue;
        }
        cursor.setPosition(block.position());
        cursor.insertText(QString(kManualIndentWidth, QLatin1Char(' ')));
        insertedByLine.prepend(kManualIndentWidth);
        ++changedLines;
    }
    cursor.endEditBlock();

    restoreLinePrefixActionCursor(editor, range, insertedByLine, {});
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Indented %1 line%2")
            .arg(changedLines)
            .arg(changedLines == 1 ? QString() : QStringLiteral("s")));
    return changedLines > 0;
}

bool applyLineUnindent(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    QList<int> removedByLine;
    removedByLine.reserve(range.lastLine - range.firstLine + 1);

    int changedLines = 0;
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid()) {
            removedByLine.prepend(0);
            continue;
        }

        const int removed = leadingUnindentCount(block.text());
        if (removed > 0) {
            cursor.setPosition(block.position());
            cursor.setPosition(block.position() + removed,
                               QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            ++changedLines;
        }
        removedByLine.prepend(removed);
    }
    cursor.endEditBlock();

    restoreLinePrefixActionCursor(editor, range, {}, removedByLine);
    emit editor->editorStatusMessageRequested(
        changedLines > 0
            ? QStringLiteral("Unindented %1 line%2")
                  .arg(changedLines)
                  .arg(changedLines == 1 ? QString()
                                         : QStringLiteral("s"))
            : QStringLiteral("No indentation to remove"));
    return changedLines > 0;
}

bool isCtrlBracketShortcut(QKeyEvent* event, int key)
{
    if (!event || event->key() != key)
        return false;

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    return modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::ShiftModifier)
        && !modifiers.testFlag(Qt::AltModifier)
        && !modifiers.testFlag(Qt::MetaModifier);
}

bool handleLineIndentShortcut(MyCodeEditor* editor, QKeyEvent* event)
{
    if (isCtrlBracketShortcut(event, Qt::Key_BracketRight)) {
        applyLineIndent(editor);
        event->accept();
        return true;
    }
    if (isCtrlBracketShortcut(event, Qt::Key_BracketLeft)) {
        applyLineUnindent(editor);
        event->accept();
        return true;
    }
    return false;
}

bool isCtrlSlashShortcut(QKeyEvent* event, bool shiftRequired)
{
    if (!event
        || (event->key() != Qt::Key_Slash
            && event->key() != Qt::Key_Question)) {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    return modifiers.testFlag(Qt::ControlModifier)
        && modifiers.testFlag(Qt::ShiftModifier) == shiftRequired
        && !modifiers.testFlag(Qt::AltModifier)
        && !modifiers.testFlag(Qt::MetaModifier);
}

bool handleLineCommentShortcut(MyCodeEditor* editor, QKeyEvent* event)
{
    if (isCtrlSlashShortcut(event, false)) {
        applyLineComment(editor);
        event->accept();
        return true;
    }
    if (isCtrlSlashShortcut(event, true)) {
        applyLineUncomment(editor);
        event->accept();
        return true;
    }
    return false;
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

QString visualSlice(const QString& text,
                    int leftVisual,
                    int rightVisual,
                    int tabWidth)
{
    const int left = qMax(0, leftVisual);
    const int right = qMax(left, rightVisual);
    if (right <= left)
        return QString();

    QString result;
    int visual = 0;
    for (const QChar ch : text) {
        const int next = visual + visualAdvanceForChar(ch, visual, tabWidth);
        if (next <= left) {
            visual = next;
            continue;
        }
        if (visual >= right)
            break;

        const int segmentStart = qMax(left, visual);
        const int segmentEnd = qMin(right, next);
        if (segmentEnd > segmentStart) {
            if (ch == QLatin1Char('\t')
                || segmentStart != visual
                || segmentEnd != next) {
                result += QString(segmentEnd - segmentStart,
                                  QLatin1Char(' '));
            } else {
                result += ch;
            }
        }
        visual = next;
    }
    if (right > visual && right > left)
        result += QString(right - qMax(left, visual), QLatin1Char(' '));
    return result;
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
    const int tabWidth = editorTabStopColumns(editor);
    const int offset = offsetForVisualColumn(block.text(),
                                             visualColumn,
                                             tabWidth,
                                             VisualBoundary::Start);
    QTextCursor caret(editor->document());
    caret.setPosition(block.position() + offset);
    editor->setTextCursor(caret);
}

QString columnSelectionClipboardText(MyCodeEditor* editor,
                                     const MyCodeEditorState& state)
{
    if (!editor || !hasColumnSelection(state))
        return QString();

    QStringList rows;
    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    const int tabWidth = editorTabStopColumns(editor);
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid()) {
            rows.append(QString());
            continue;
        }
        rows.append(visualSlice(block.text(),
                                leftColumn,
                                rightColumn,
                                tabWidth));
    }
    return rows.join(QLatin1Char('\n'));
}

QStringList columnSelectionRowTexts(MyCodeEditor* editor,
                                    const MyCodeEditorState& state)
{
    if (!editor || !hasColumnSelection(state))
        return {};

    QStringList rows;
    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    const int tabWidth = editorTabStopColumns(editor);
    rows.reserve(lastLine - firstLine + 1);
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        rows.append(block.isValid()
                        ? visualSlice(block.text(),
                                      leftColumn,
                                      rightColumn,
                                      tabWidth)
                        : QString());
    }
    return rows;
}

void updateColumnSelectionHighlight(MyCodeEditor* editor,
                                    const MyCodeEditorState& state);

void replaceColumnSelectionRows(MyCodeEditor* editor,
                                MyCodeEditorState& state,
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
            visualColumnForOffset(lineText, lineText.size(), tabWidth);
        const int startColumn = offsetForVisualColumn(lineText,
                                                      leftColumn,
                                                      tabWidth,
                                                      VisualBoundary::Start);
        int endColumn = startColumn;
        if (hasWidth)
            endColumn = offsetForVisualColumn(lineText,
                                              rightColumn,
                                              tabWidth,
                                              VisualBoundary::End);

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
    if (leftColumn == rightColumn)
        return;

    const int tabWidth = editorTabStopColumns(editor);
    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid())
            continue;

        const QString lineText = block.text();
        const int startColumn = offsetForVisualColumn(lineText,
                                                      leftColumn,
                                                      tabWidth,
                                                      VisualBoundary::Start);
        const int visibleEndColumn = offsetForVisualColumn(lineText,
                                                           rightColumn,
                                                           tabWidth,
                                                           VisualBoundary::End);
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
        *column = visualColumnForOffset(
            cursor.block().text(),
            qMax(0, cursor.position() - cursor.block().position()),
            editorTabStopColumns(editor));
    }
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
    if (state.columnSelectionActive) {
        setColumnPointFromCursor(editor,
                                 cursor,
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

    const QTextCursor anchor = editor->textCursor();
    state.columnSelectionActive = true;
    state.columnSelectionDragging = false;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragMoved = false;
    setColumnPointFromCursor(editor,
                             anchor,
                             &state.columnAnchorLine,
                             &state.columnAnchorColumn);
    setColumnPointFromCursor(editor,
                             cursor,
                             &state.columnCurrentLine,
                             &state.columnCurrentColumn);
    updateColumnSelectionHighlight(editor, state);
    editor->viewport()->setCursor(Qt::CrossCursor);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool handleColumnSelectionClipboard(MyCodeEditor* editor,
                                    QKeyEvent* event,
                                    MyCodeEditorState& state)
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
    setColumnPointFromCursor(editor,
                             cursor,
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
        setColumnPointFromCursor(editor,
                                 cursor,
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
            visualColumnForOffset(lineText, lineText.size(), tabWidth);
        int startColumn = offsetForVisualColumn(lineText,
                                                editColumn,
                                                tabWidth,
                                                VisualBoundary::Start);
        int endColumn = startColumn;
        if (hasWidth) {
            startColumn = offsetForVisualColumn(lineText,
                                                leftColumn,
                                                tabWidth,
                                                VisualBoundary::Start);
            endColumn = offsetForVisualColumn(lineText,
                                              rightColumn,
                                              tabWidth,
                                              VisualBoundary::End);
        } else if (deleteKey && leftColumn < lineEndVisual) {
            startColumn = offsetForVisualColumn(lineText,
                                                leftColumn,
                                                tabWidth,
                                                VisualBoundary::Start);
            endColumn = offsetForVisualColumn(lineText,
                                              leftColumn + 1,
                                              tabWidth,
                                              VisualBoundary::End);
        } else if (backspace && leftColumn > 0 && editColumn < lineEndVisual) {
            startColumn = offsetForVisualColumn(lineText,
                                                editColumn,
                                                tabWidth,
                                                VisualBoundary::Start);
            endColumn = offsetForVisualColumn(lineText,
                                              leftColumn,
                                              tabWidth,
                                              VisualBoundary::End);
        } else if (backwardTab && leftColumn > backwardTargetColumn) {
            startColumn = offsetForVisualColumn(lineText,
                                                backwardTargetColumn,
                                                tabWidth,
                                                VisualBoundary::Start);
            endColumn = offsetForVisualColumn(lineText,
                                              leftColumn,
                                              tabWidth,
                                              VisualBoundary::End);
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
    setCaretToVisualColumn(editor,
                           state.columnCurrentLine,
                           state.columnCurrentColumn);
    editor->viewport()->update();
    event->accept();
    return true;
}

int xForVisualColumn(MyCodeEditor* editor,
                     const QTextBlock& block,
                     int visualColumn)
{
    if (!editor || !block.isValid())
        return 0;

    const int tabWidth = editorTabStopColumns(editor);
    const QString text = block.text();
    const int offset = offsetForVisualColumn(text,
                                             visualColumn,
                                             tabWidth,
                                             VisualBoundary::Start);
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + offset);
    QRect rect = editor->cursorRect(cursor);
    const int offsetVisual = visualColumnForOffset(text, offset, tabWidth);
    if (visualColumn > offsetVisual) {
        const QFontMetrics metrics(editor->font());
        rect.translate(metrics.horizontalAdvance(QLatin1Char(' '))
                           * (visualColumn - offsetVisual),
                       0);
    }
    return rect.left();
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

        QTextCursor cursor(block);
        cursor.setPosition(block.position());
        QRect rect = editor->cursorRect(cursor);
        rect.moveLeft(xForVisualColumn(editor, block, leftColumn));
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
    semanticRevisionText = editor ? editor->toPlainText() : QString();
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
            const QString currentText = editor->toPlainText();
            if (currentText != semanticRevisionText) {
                semanticRevisionText = currentText;
                ++semanticTextRevision;
            }
            sourceNavigation.handleEditorContentChanged(editor, selections);
            folding.refresh(editor, syntax.tsDocument());
            refreshGhostAnnotations(editor);
        });
    QObject::connect(
        editor->document(),
        &QTextDocument::contentsChange,
        editor,
        [this, editor](int position, int charsRemoved, int charsAdded) {
            handleTemplateSlotContentsChange(
                editor,
                position,
                charsRemoved,
                charsAdded);
        });
    QObject::connect(
        editor,
        &QPlainTextEdit::cursorPositionChanged,
        editor,
        [this, editor]() {
            completionWorkflow.handleCursorPositionChanged();
            QTimer::singleShot(0, editor, [this, editor]() {
                handleTemplateSlotCursorChanged(editor);
            });
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
        [this](const QModelIndex& index) {
            completionWorkflow.handleCompletionActivated(index);
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

EditorAlwaysScopeTarget MyCodeEditorState::currentAlwaysScopeTarget(
    const MyCodeEditor* editor) const
{
    EditorAlwaysScopeTarget result;
    if (!editor) {
        result.failureMessage = QStringLiteral("No document selected.");
        return result;
    }

    const QTextCursor cursor = editor->textCursor();
    const TSAlwaysScopeTarget target =
        syntax.alwaysScopeTargetAt(cursor.position(),
                                   cursor.hasSelection()
                                       ? cursor.selectionStart()
                                       : -1,
                                   cursor.hasSelection()
                                       ? cursor.selectionEnd()
                                       : -1);
    if (!target.ok()) {
        result.failureMessage =
            target.status == TSAlwaysScopeStatus::AmbiguousSelection
                ? QStringLiteral("Select only one always block to preview.")
                : QStringLiteral("Place the cursor in an always block to preview.");
        return result;
    }

    result.available = true;
    result.startPosition = target.startChar;
    result.endPosition = target.endChar;
    result.startLine = target.startLine;
    result.endLine = target.endLine;
    result.label = target.label;
    return result;
}

EditorModuleScopeTarget MyCodeEditorState::currentModuleScopeTarget(
    const MyCodeEditor* editor) const
{
    EditorModuleScopeTarget result;
    if (!editor) {
        result.failureMessage = QStringLiteral("No document selected.");
        return result;
    }

    const QTextCursor cursor = editor->textCursor();
    const TSModuleScopeTarget target =
        syntax.moduleScopeTargetAt(cursor.position(),
                                   cursor.hasSelection()
                                       ? cursor.selectionStart()
                                       : -1,
                                   cursor.hasSelection()
                                       ? cursor.selectionEnd()
                                       : -1);
    if (!target.ok()) {
        result.failureMessage =
            target.status == TSModuleScopeStatus::AmbiguousSelection
                ? QStringLiteral("Select only one module to preview.")
                : QStringLiteral("Place the cursor in a module or always block to preview.");
        return result;
    }

    result.available = true;
    result.startPosition = target.startChar;
    result.endPosition = target.endChar;
    result.startLine = target.startLine;
    result.endLine = target.endLine;
    result.moduleName = target.moduleName;
    result.label = target.label;
    return result;
}

bool MyCodeEditorState::addPortRow(MyCodeEditor* editor,
                                   QString* message)
{
    if (!editor)
        return false;

    const TSPortAppendTarget target =
        syntax.portAppendTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message = target.status == TSPortAppendStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No clear port append point");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(target.insertText);
    if (target.needsTrailingComma
        && target.trailingCommaInsertChar >= 0) {
        cursor.setPosition(target.trailingCommaInsertChar);
        cursor.insertText(QStringLiteral(","));
    }
    cursor.endEditBlock();

    QTextCursor caret = editor->textCursor();
    caret.setPosition(target.caretCharAfterEdit);
    editor->setTextCursor(caret);
    return true;
}

bool MyCodeEditorState::addSignalRow(MyCodeEditor* editor,
                                     QString* message)
{
    if (!editor)
        return false;

    const TSSignalInsertTarget target =
        syntax.signalInsertTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message = target.status == TSSignalInsertStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No clear signal insert point");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(target.insertText);
    cursor.endEditBlock();

    QTextCursor caret = editor->textCursor();
    caret.setPosition(target.caretCharAfterEdit);
    editor->setTextCursor(caret);
    return true;
}

bool MyCodeEditorState::addParameterRow(MyCodeEditor* editor,
                                        QString* message)
{
    if (!editor)
        return false;

    const TSParameterInsertTarget target =
        syntax.parameterInsertTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message =
                target.status
                    == TSParameterInsertStatus::NoCurrentParameterScope
                ? QStringLiteral("No current parameter scope")
                : QStringLiteral("No clear parameter insert point");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(target.insertText);
    if (target.needsTrailingComma
        && target.trailingCommaInsertChar >= 0) {
        cursor.setPosition(target.trailingCommaInsertChar);
        cursor.insertText(QStringLiteral(","));
    }
    cursor.endEditBlock();

    QTextCursor caret = editor->textCursor();
    caret.setPosition(target.caretCharAfterEdit);
    editor->setTextCursor(caret);
    return true;
}

namespace {
QString packageToolFailureMessage(TSPackageToolInsertStatus status)
{
    switch (status) {
    case TSPackageToolInsertStatus::NoCurrentPackage:
        return QStringLiteral("No current package");
    case TSPackageToolInsertStatus::InsideRtlScope:
        return QStringLiteral(
            "Package tools are unavailable inside module/interface scope");
    case TSPackageToolInsertStatus::PackageHasSyntaxError:
        return QStringLiteral("Current package has syntax errors");
    case TSPackageToolInsertStatus::NoEndpackage:
        return QStringLiteral("No endpackage found");
    case TSPackageToolInsertStatus::NoClearPackageInsertPoint:
        return QStringLiteral("No clear package insert point");
    case TSPackageToolInsertStatus::Ok:
        return QString();
    }
    return QStringLiteral("No clear package insert point");
}
} // namespace

EditorPackageToolAvailability MyCodeEditorState::currentPackageToolAvailability(
    const MyCodeEditor* editor) const
{
    EditorPackageToolAvailability availability;
    if (!editor)
        return availability;

    const TSPackageToolInsertTarget target =
        syntax.packageToolInsertTargetAt(editor->textCursor().position(),
                                         PackageToolKind::Parameter);
    availability.available = target.ok();
    availability.packageName = target.packageName;
    availability.failureMessage = packageToolFailureMessage(target.status);
    return availability;
}

bool MyCodeEditorState::executePackageToolInsert(MyCodeEditor* editor,
                                                 PackageToolKind kind,
                                                 QString* message)
{
    if (!editor)
        return false;

    const TSPackageToolInsertTarget target =
        syntax.packageToolInsertTargetAt(editor->textCursor().position(), kind);
    if (!target.ok()) {
        const QString failure = packageToolFailureMessage(target.status);
        if (message)
            *message = failure;
        emit editor->editorStatusMessageRequested(failure);
        return false;
    }

    const PackageToolService service;
    const CodeTemplateItem packageTemplate =
        service.templateForInsertion(kind,
                                     target.lineIndent,
                                     target.insertAfterLine);
    if (packageTemplate.insertText.isEmpty()) {
        const QString failure = QStringLiteral("No package template available");
        if (message)
            *message = failure;
        emit editor->editorStatusMessageRequested(failure);
        return false;
    }

    clearTemplateSlotMode(editor);
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.insertChar);
    cursor.insertText(packageTemplate.insertText);
    cursor.endEditBlock();

    startTemplateSlotMode(editor,
                          target.insertChar,
                          packageTemplate.insertText.size(),
                          packageTemplate.templateSlots);

    const QString packageName =
        target.packageName.isEmpty()
            ? QStringLiteral("package")
            : QStringLiteral("package %1").arg(target.packageName);
    const QString success =
        QStringLiteral("Inserted %1 in %2")
            .arg(PackageToolService::labelForKind(kind), packageName);
    if (message)
        *message = success;
    emit editor->editorStatusMessageRequested(success);
    return true;
}

bool MyCodeEditorState::goToFinalEndmodule(MyCodeEditor* editor,
                                                  QString* message)
{
    if (!editor)
        return false;

    const TSModuleEndNavigationTarget target =
        syntax.moduleEndNavigationTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message =
                target.status
                    == TSModuleEndNavigationStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No endmodule found");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(target.caretChar);
    editor->setTextCursor(cursor);
    editor->ensureCursorVisible();
    if (message)
        *message = QStringLiteral("Moved to final endmodule");
    return true;
}
bool MyCodeEditorState::selectInsideBeginEnd(MyCodeEditor* editor,
                                             QString* message)
{
    if (!editor || !editor->document())
        return false;

    const TSBeginEndInsideTarget target =
        syntax.beginEndInsideTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message =
                target.status == TSBeginEndInsideStatus::EmptyBeginEndBlock
                    ? QStringLiteral("No begin-end body")
                    : QStringLiteral("No begin-end block");
        }
        return false;
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(target.startChar);
    cursor.setPosition(target.endChar, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    if (message) {
        *message = QStringLiteral("Selected inside begin-end");
    }
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Selected inside begin-end lines %1-%2")
            .arg(target.startLine + 1)
            .arg(target.endLine + 1));
    return true;
}

EditorSemanticContext MyCodeEditorState::semanticContextForPosition(
    const MyCodeEditor* editor,
    int cursorPosition,
    bool includeDocumentText) const
{
    const int semanticPosition = cursorPosition >= 0
        ? cursorPosition
        : editor->textCursor().position();

    EditorSemanticContext context = semantic.contextForDocument(
        editor->document(),
        identity.current(),
        currentModuleNameAt(semanticPosition),
        semanticPosition,
        includeDocumentText,
        semanticDocumentRevision());
    context.hierarchyInstance = hierarchyInstance;
    return context;
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

namespace {
QList<QPair<int, int>> templateSlotHighlightRanges(
    const MyCodeEditorState& state)
{
    QList<QPair<int, int>> ranges;
    ranges.reserve(state.templateSlotRanges.size());
    for (const MyCodeEditorState::TemplateSlotRange& slot :
         state.templateSlotRanges) {
        ranges.append(qMakePair(slot.start, qMax(0, slot.end - slot.start)));
    }
    return ranges;
}

int templateSlotIndexForCursor(
    MyCodeEditor* editor,
    const MyCodeEditorState& state)
{
    if (!editor || !state.templateSlotModeActive())
        return -1;

    QTextCursor cursor = editor->textCursor();
    const int selectionStart = cursor.hasSelection()
        ? cursor.selectionStart()
        : cursor.position();
    const int selectionEnd = cursor.hasSelection()
        ? cursor.selectionEnd()
        : cursor.position();
    for (int i = 0; i < state.templateSlotRanges.size(); ++i) {
        const MyCodeEditorState::TemplateSlotRange slot =
            state.templateSlotRanges.at(i);
        if (selectionStart >= slot.start && selectionEnd <= slot.end)
            return i;
    }
    return -1;
}

bool templateSlotCursorInsideActiveRange(
    MyCodeEditor* editor,
    const MyCodeEditorState& state)
{
    return templateSlotIndexForCursor(editor, state)
        == state.templateSlotActiveIndex;
}

bool templateSlotPositionInsideAnyRange(
    const MyCodeEditorState& state,
    int position)
{
    if (!state.templateSlotModeActive())
        return false;
    for (const MyCodeEditorState::TemplateSlotRange& slot :
         state.templateSlotRanges) {
        if (position >= slot.start && position <= slot.end)
            return true;
    }
    return false;
}

void refreshTemplateSlotHighlights(MyCodeEditor* editor,
                                   MyCodeEditorState& state)
{
    if (!editor)
        return;
    state.selections.highlightTemplateSlots(
        editor,
        templateSlotHighlightRanges(state),
        state.templateSlotActiveIndex,
        state.templateSlotBlinkOn);
}

void stopTemplateSlotBlinkTimer(MyCodeEditor* editor,
                                MyCodeEditorState& state)
{
    Q_UNUSED(editor)
    if (!state.templateSlotBlinkTimer)
        return;
    state.templateSlotBlinkTimer->stop();
    state.templateSlotBlinkTimer->deleteLater();
    state.templateSlotBlinkTimer = nullptr;
}

void ensureTemplateSlotBlinkTimer(MyCodeEditor* editor,
                                  MyCodeEditorState& state)
{
    if (!editor || state.templateSlotBlinkTimer)
        return;

    state.templateSlotBlinkOn = true;
    state.templateSlotBlinkTimer = new QTimer(editor);
    state.templateSlotBlinkTimer->setInterval(500);
    QObject::connect(
        state.templateSlotBlinkTimer,
        &QTimer::timeout,
        editor,
        [&state, editor]() {
            if (!state.templateSlotModeActive()) {
                stopTemplateSlotBlinkTimer(editor, state);
                return;
            }
            state.templateSlotBlinkOn = !state.templateSlotBlinkOn;
            refreshTemplateSlotHighlights(editor, state);
        });
    state.templateSlotBlinkTimer->start();
}

void selectTemplateSlot(MyCodeEditor* editor,
                        MyCodeEditorState& state,
                        int index)
{
    if (!editor || index < 0 || index >= state.templateSlotRanges.size())
        return;

    state.templateSlotActiveIndex = index;
    const MyCodeEditorState::TemplateSlotRange slot =
        state.templateSlotRanges.at(index);
    QTextCursor cursor(editor->document());
    cursor.setPosition(slot.start);
    if (slot.end > slot.start)
        cursor.setPosition(slot.end, QTextCursor::KeepAnchor);
    state.templateSlotIgnoreNextCursorCheck = true;
    editor->setTextCursor(cursor);
    refreshTemplateSlotHighlights(editor, state);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("SLOT %1/%2")
            .arg(index + 1)
            .arg(state.templateSlotRanges.size()));
}
} // namespace

void MyCodeEditorState::startTemplateSlotMode(
    MyCodeEditor* editor,
    int insertionStart,
    int insertedLength,
    const CodeTemplateSlotList& slotMetadata)
{
    clearTemplateSlotMode(editor);
    if (!editor || !editor->document() || insertedLength < 0
        || slotMetadata.isEmpty())
        return;

    const int docEnd = qMax(0, editor->document()->characterCount() - 1);
    if (insertionStart < 0 || insertionStart + insertedLength > docEnd)
        return;

    for (const CodeTemplateSlot& slotInfo : slotMetadata) {
        if (slotInfo.start < 0 || slotInfo.length < 0
            || slotInfo.start + slotInfo.length > insertedLength) {
            continue;
        }

        TemplateSlotRange range;
        range.name = slotInfo.name;
        range.start = insertionStart + slotInfo.start;
        range.end = range.start + slotInfo.length;
        if (range.start < 0 || range.end < range.start || range.end > docEnd)
            continue;
        templateSlotRanges.append(range);
    }

    if (templateSlotRanges.isEmpty()) {
        clearTemplateSlotMode(editor);
        return;
    }

    templateSlotSessionStart = insertionStart;
    templateSlotSessionEnd = insertionStart + insertedLength;
    selectTemplateSlot(editor, *this, 0);
    ensureTemplateSlotBlinkTimer(editor, *this);
}

bool MyCodeEditorState::templateSlotModeActive() const
{
    return templateSlotActiveIndex >= 0
        && templateSlotActiveIndex < templateSlotRanges.size();
}

int MyCodeEditorState::templateSlotModeActiveIndex() const
{
    return templateSlotActiveIndex;
}

int MyCodeEditorState::templateSlotModeSlotCount() const
{
    return templateSlotRanges.size();
}

bool MyCodeEditorState::templateSlotModeBlinkOn() const
{
    return templateSlotBlinkOn;
}

bool MyCodeEditorState::columnSelectionActiveForCommand() const
{
    return hasColumnSelection(*this);
}

QStringList MyCodeEditorState::columnSelectionRowTexts(
    MyCodeEditor* editor) const
{
    return ::columnSelectionRowTexts(editor, *this);
}

bool MyCodeEditorState::applyColumnSelectionRowTexts(
    MyCodeEditor* editor,
    const QStringList& rows,
    bool replaceSelection,
    QString* message)
{
    if (!editor || !hasColumnSelection(*this)) {
        if (message)
            *message = QStringLiteral("No column selection");
        return false;
    }
    replaceColumnSelectionRows(editor, *this, rows, true, replaceSelection);
    if (message)
        message->clear();
    return true;
}

void MyCodeEditorState::clearTemplateSlotMode(MyCodeEditor* editor,
                                              const QString& message)
{
    const bool wasActive = templateSlotModeActive()
        || !templateSlotRanges.isEmpty();
    stopTemplateSlotBlinkTimer(editor, *this);
    templateSlotRanges.clear();
    templateSlotActiveIndex = -1;
    templateSlotSessionStart = -1;
    templateSlotSessionEnd = -1;
    templateSlotBlinkOn = true;
    templateSlotIgnoreNextCursorCheck = false;
    if (editor)
        selections.clearTemplateSlots(editor);
    if (wasActive && editor && !message.isEmpty())
        emit editor->editorStatusMessageRequested(message);
}

bool MyCodeEditorState::handleTemplateSlotKeyPress(MyCodeEditor* editor,
                                                   QKeyEvent* event)
{
    if (!editor || !event || !templateSlotModeActive())
        return false;

    const int cursorSlotIndex = templateSlotIndexForCursor(editor, *this);
    if (cursorSlotIndex >= 0 && cursorSlotIndex != templateSlotActiveIndex) {
        templateSlotActiveIndex = cursorSlotIndex;
        refreshTemplateSlotHighlights(editor, *this);
    }
    if (!templateSlotCursorInsideActiveRange(editor, *this)) {
        clearTemplateSlotMode(editor);
        return false;
    }

    if (event->key() == Qt::Key_Escape) {
        clearTemplateSlotMode(editor, QStringLiteral("Slot Mode canceled"));
        event->accept();
        return true;
    }

    const Qt::KeyboardModifiers modifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    const bool isForwardTab = event->key() == Qt::Key_Tab
        && modifiers == Qt::NoModifier;
    const bool isBackwardTab =
        event->key() == Qt::Key_Backtab
        || (event->key() == Qt::Key_Tab
            && modifiers == Qt::ShiftModifier);
    if ((!isForwardTab && !isBackwardTab)
        || (modifiers != Qt::NoModifier
            && modifiers != Qt::ShiftModifier)) {
        return false;
    }

    if (isBackwardTab) {
        const int count = templateSlotRanges.size();
        selectTemplateSlot(editor,
                           *this,
                           (templateSlotActiveIndex - 1 + count) % count);
        event->accept();
        return true;
    }

    selectTemplateSlot(editor,
                       *this,
                       (templateSlotActiveIndex + 1)
                           % templateSlotRanges.size());
    event->accept();
    return true;
}

void MyCodeEditorState::handleTemplateSlotContentsChange(
    MyCodeEditor* editor,
    int position,
    int charsRemoved,
    int charsAdded)
{
    if (!editor || !templateSlotModeActive())
        return;

    const int changeStart = position;
    const int changeEnd = position + charsRemoved;
    if (changeStart < templateSlotSessionStart
        || changeStart > templateSlotSessionEnd) {
        clearTemplateSlotMode(editor);
        return;
    }

    TemplateSlotRange& activeSlot =
        templateSlotRanges[templateSlotActiveIndex];
    if (changeStart < activeSlot.start || changeEnd > activeSlot.end) {
        clearTemplateSlotMode(editor);
        return;
    }

    const int delta = charsAdded - charsRemoved;
    activeSlot.end += delta;
    if (activeSlot.end < activeSlot.start) {
        clearTemplateSlotMode(editor);
        return;
    }
    for (int i = 0; i < templateSlotRanges.size(); ++i) {
        if (i == templateSlotActiveIndex)
            continue;
        if (templateSlotRanges.at(i).start < changeEnd)
            continue;
        templateSlotRanges[i].start += delta;
        templateSlotRanges[i].end += delta;
    }
    templateSlotSessionEnd += delta;
    refreshTemplateSlotHighlights(editor, *this);
}

void MyCodeEditorState::handleTemplateSlotCursorChanged(MyCodeEditor* editor)
{
    if (!templateSlotModeActive())
        return;
    if (templateSlotIgnoreNextCursorCheck) {
        templateSlotIgnoreNextCursorCheck = false;
        return;
    }
    const int cursorSlotIndex = templateSlotIndexForCursor(editor, *this);
    if (cursorSlotIndex >= 0) {
        if (cursorSlotIndex != templateSlotActiveIndex) {
            templateSlotActiveIndex = cursorSlotIndex;
            refreshTemplateSlotHighlights(editor, *this);
            emit editor->editorStatusMessageRequested(
                QStringLiteral("SLOT %1/%2")
                    .arg(templateSlotActiveIndex + 1)
                    .arg(templateSlotRanges.size()));
        }
        return;
    }
    if (!templateSlotCursorInsideActiveRange(editor, *this))
        clearTemplateSlotMode(editor);
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

    if (handleTemplateSlotKeyPress(editor, event))
        return true;

    if (event->key() == Qt::Key_Escape
        && completionWorkflow.handleCompletionPopupKey(event)) {
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

    if (event->key() == Qt::Key_Escape && editor->textCursor().hasSelection()) {
        QTextCursor cursor = editor->textCursor();
        cursor.clearSelection();
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }

    if (handleSmartSelectionExpansion(editor, event))
        return true;

    if (handleSelectedSymbolOccurrenceNavigation(editor, event))
        return true;

    if (handleSafeRename(editor, event))
        return true;

    if (handleDuplicateSelectionOrLine(editor, event))
        return true;

    if (handleMoveLineBlock(editor, event, columnSelectionActive))
        return true;

    handleControlKeyPress(editor, event);

    if (handleLineCommentShortcut(editor, event))
        return true;

    if (handleLineIndentShortcut(editor, event))
        return true;

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

    if (handleColumnSelectionClipboard(editor, event, *this))
        return true;

    if (handleColumnSelectionNavigation(editor, event, *this))
        return true;

    if (handleColumnSelectionKeyInput(editor, event, *this))
        return true;

    if (adjustSelectedRangeBound(editor, event))
        return true;

    if (completionWorkflow.handleCompletionPopupKey(event))
        return true;

    if (handleBracketRangeTab(editor, event))
        return true;

    if (completionWorkflow.handleInlineAbbreviationTab(event))
        return true;

    if (handleBracketPairInsertion(editor, event))
        return true;

    return completionWorkflow.handleCompletionPopupKey(event);
}

bool MyCodeEditorState::handleKeyRelease(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    handleControlKeyRelease(editor, event);
    Q_UNUSED(event)
    return false;
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
    gutter.updateViewportMargins(editor);
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
    QHash<int, qreal> rightLineEndX;
    constexpr qreal kLineTailSpacing = 8.0;

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

        const int textWidth = metrics.horizontalAdvance(annotation.text);
        qreal x = -1;
        qreal baseline = 0;
        qreal visualTop = anchorRect.top();
        qreal visualBottom = anchorRect.bottom();
        const int line =
            annotation.line > 0 ? annotation.line : block.blockNumber() + 1;
        if (annotation.placement == GhostAnnotationPlacement::LeftOfAnchor) {
            x = anchorRect.left() - textWidth - 8;
            if (x < 2)
                continue;
            baseline = anchorRect.top()
                + (anchorRect.height() + metrics.ascent()
                   - metrics.descent()) / 2.0;
        } else {
            const EditorCodeLineTailGeometry tail =
                geometry.codeLineTailGeometry(editor, block.blockNumber());
            if (!tail.valid)
                continue;
            x = tail.textRight + kLineTailSpacing;
            const qreal previousEnd = rightLineEndX.value(line, x);
            if (x < previousEnd + kLineTailSpacing)
                x = previousEnd + kLineTailSpacing;
            baseline = tail.baseline;
            visualTop = tail.top;
            visualBottom = tail.top + tail.height;
        }

        if (visualBottom < 0 || visualTop > viewportHeight)
            continue;
        if (x >= viewportWidth - 4 || x + textWidth <= 2)
            continue;
        if (annotation.kind != GhostAnnotationKind::FormalPort
            && (x < 2 || x + textWidth > viewportWidth - 4)) {
            continue;
        }
        if (annotation.placement != GhostAnnotationPlacement::LeftOfAnchor)
            rightLineEndX.insert(line, x + textWidth);

        QColor annotationColor = color;
        if (annotation.kind == GhostAnnotationKind::FormalPort)
            annotationColor.setAlpha(96);
        painter.setPen(annotationColor);
        painter.drawText(QPointF(x, baseline), annotation.text);
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
    if (templateSlotModeActive() && editor && event) {
        const QTextCursor targetCursor =
            editor->cursorForPosition(event->position().toPoint());
        if (!templateSlotPositionInsideAnyRange(*this,
                                                targetCursor.position())) {
            clearTemplateSlotMode(editor);
        }
    }

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

    if (sourceNavigation.handleMouseMove(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections)) {
        return true;
    }
    return false;
}

bool MyCodeEditorState::handleMouseRelease(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (sourceNavigation.handleMouseRelease(editor, event))
        return true;

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

void MyCodeEditorState::refreshSemanticPresentation(MyCodeEditor* editor)
{
    if (!editor)
        return;

    refreshScopeAndCurrentLineHighlight(editor);
    refreshGhostAnnotations(editor);
}

std::uint64_t MyCodeEditorState::semanticDocumentRevision() const
{
    return semanticTextRevision;
}

void MyCodeEditorState::acceptLoadedTextAsSemanticBaseline(
    const MyCodeEditor* editor)
{
    semanticRevisionText = editor ? editor->toPlainText() : QString();
    semanticTextRevision = 0;
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

void MyCodeEditorState::commentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineComment(editor);
}

void MyCodeEditorState::uncommentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineUncomment(editor);
}

void MyCodeEditorState::indentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineIndent(editor);
}

void MyCodeEditorState::unindentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineUnindent(editor);
}

namespace {
enum class CurrentStatementScanState {
    Normal,
    LineComment,
    BlockComment,
    String
};

int topLevelSemicolonEndFrom(const QString& text, int start)
{
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    CurrentStatementScanState state = CurrentStatementScanState::Normal;

    for (int i = qBound(0, start, text.size()); i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();

        if (state == CurrentStatementScanState::LineComment) {
            if (ch == QLatin1Char('\n'))
                state = CurrentStatementScanState::Normal;
            continue;
        }
        if (state == CurrentStatementScanState::BlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = CurrentStatementScanState::Normal;
                ++i;
            }
            continue;
        }
        if (state == CurrentStatementScanState::String) {
            if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (ch == QLatin1Char('"'))
                state = CurrentStatementScanState::Normal;
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            state = CurrentStatementScanState::LineComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            state = CurrentStatementScanState::BlockComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            state = CurrentStatementScanState::String;
            continue;
        }

        if (ch == QLatin1Char('(')) {
            ++parenDepth;
            continue;
        }
        if (ch == QLatin1Char(')')) {
            parenDepth = qMax(0, parenDepth - 1);
            continue;
        }
        if (ch == QLatin1Char('[')) {
            ++bracketDepth;
            continue;
        }
        if (ch == QLatin1Char(']')) {
            bracketDepth = qMax(0, bracketDepth - 1);
            continue;
        }
        if (ch == QLatin1Char('{')) {
            ++braceDepth;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            braceDepth = qMax(0, braceDepth - 1);
            continue;
        }

        if (ch == QLatin1Char(';') && parenDepth == 0
            && bracketDepth == 0 && braceDepth == 0) {
            return i + 1;
        }
    }
    return -1;
}

bool currentAssignmentRange(MyCodeEditor* editor,
                            int cursorPosition,
                            int* rangeStart,
                            int* rangeEnd)
{
    if (!editor || !editor->document() || !rangeStart || !rangeEnd)
        return false;

    const QString documentText = editor->toPlainText();
    QTextBlock block = editor->document()->findBlock(cursorPosition);
    if (!block.isValid())
        return false;

    constexpr int kMaxLookbackLines = 40;
    for (int lookedBack = 0;
         lookedBack < kMaxLookbackLines && block.isValid();
         ++lookedBack, block = block.previous()) {
        const int candidateStart = block.position();
        const int candidateEnd =
            topLevelSemicolonEndFrom(documentText, candidateStart);
        if (candidateEnd <= cursorPosition)
            continue;
        if (candidateEnd < 0)
            continue;

        const QString candidateText =
            documentText.mid(candidateStart, candidateEnd - candidateStart);
        const RtlClearAssignmentRhsReport report =
            RtlBatchEditService::getInstance()->planClearAssignmentRhs(
                RtlClearAssignmentRhsQuery{candidateText, candidateStart});
        if (!report.canApply() || report.edits.size() != 1)
            continue;

        *rangeStart = candidateStart;
        *rangeEnd = candidateEnd;
        return true;
    }
    return false;
}

bool selectedCompleteLineRange(MyCodeEditor* editor,
                               const QTextCursor& cursor,
                               int* rangeStart,
                               int* rangeEnd)
{
    if (!editor || !editor->document() || !cursor.hasSelection()
        || !rangeStart || !rangeEnd) {
        return false;
    }

    const int selectionStart = cursor.selectionStart();
    int adjustedEnd = cursor.selectionEnd();
    if (adjustedEnd <= selectionStart)
        return false;

    const QTextBlock endAtBlock =
        editor->document()->findBlock(adjustedEnd);
    if (endAtBlock.isValid()
        && adjustedEnd == endAtBlock.position()
        && adjustedEnd > selectionStart) {
        --adjustedEnd;
    } else {
        --adjustedEnd;
    }

    const QTextBlock firstBlock =
        editor->document()->findBlock(selectionStart);
    const QTextBlock lastBlock =
        editor->document()->findBlock(qMax(selectionStart, adjustedEnd));
    if (!firstBlock.isValid() || !lastBlock.isValid())
        return false;

    *rangeStart = firstBlock.position();
    *rangeEnd = lineEndPosition(editor->document(), lastBlock.blockNumber());
    return *rangeEnd >= *rangeStart;
}

void publishClearRhsFailure(MyCodeEditor* editor,
                            QString* message,
                            const QString& failure)
{
    const QString resolved = failure.isEmpty()
        ? QStringLiteral("No assignment RHS found")
        : failure;
    if (message)
        *message = resolved;
    if (editor)
        emit editor->editorStatusMessageRequested(resolved);
}
} // namespace

bool MyCodeEditorState::clearSelectedAssignmentRhs(MyCodeEditor* editor,
                                                   QString* message)
{
    if (!editor || !editor->document())
        return false;

    QTextCursor cursor = editor->textCursor();
    int selectionStart = cursor.selectionStart();
    int selectionEnd = cursor.selectionEnd();
    if (cursor.hasSelection()) {
        if (!selectedCompleteLineRange(editor,
                                       cursor,
                                       &selectionStart,
                                       &selectionEnd)) {
            publishClearRhsFailure(editor,
                                   message,
                                   QStringLiteral("No assignment RHS found"));
            return false;
        }
    } else {
        if (!currentAssignmentRange(editor,
                                    cursor.position(),
                                    &selectionStart,
                                    &selectionEnd)) {
            publishClearRhsFailure(editor,
                                   message,
                                   QStringLiteral("No assignment RHS found"));
            return false;
        }
    }

    const QString selectedText =
        editor->toPlainText().mid(selectionStart,
                                  selectionEnd - selectionStart);
    const RtlClearAssignmentRhsReport report =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{selectedText, selectionStart});
    if (!report.canApply()) {
        publishClearRhsFailure(editor, message, report.failureReason);
        return false;
    }

    clearTemplateSlotMode(editor);
    cursor.beginEditBlock();
    cursor.setPosition(selectionStart);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    cursor.insertText(report.replacementText);
    cursor.endEditBlock();

    startTemplateSlotMode(editor,
                          selectionStart,
                          report.replacementText.size(),
                          report.templateSlots);
    const QString successMessage =
        QStringLiteral("Cleared RHS for %1 assignment%2")
            .arg(report.edits.size())
            .arg(report.edits.size() == 1 ? QString() : QStringLiteral("s"));
    if (message)
        *message = successMessage;
    emit editor->editorStatusMessageRequested(successMessage);
    return true;
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

void MyCodeEditorState::setHierarchyInstanceContext(
    const HierarchyInstanceContext& context)
{
    hierarchyInstance = context;
}

HierarchyInstanceContext MyCodeEditorState::hierarchyInstanceContext() const
{
    return hierarchyInstance;
}

void MyCodeEditorState::closeSemanticPopup(MyCodeEditor* editor)
{
    sourceNavigation.closeForEditor(editor, selections);
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

    refreshGhostAnnotations(editor);
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
    query.instanceContext = hierarchyInstance;
    query.documentRevision = semanticDocumentRevision();
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
    if (editor) {
        gutter.updateViewportMargins(editor);
        handleResize(editor);
        gutter.handleUpdateRequest(editor,
                                   editor->viewport()->rect(),
                                   0);
        editor->viewport()->update();
    }
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
