#include "editorruntime.h"

#include "mycodeeditor.h"

#include "commodecommandregistry.h"
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

bool isBacktickKey(QKeyEvent* event)
{
    return event
        && (event->key() == Qt::Key_QuoteLeft
            || event->text() == QStringLiteral("`"));
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

QString columnSelectionClipboardText(MyCodeEditor* editor,
                                     const MyCodeEditorState& state)
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
        const QString lineText = block.text();
        const int startColumn = qMin(leftColumn, lineText.size());
        const int endColumn = qMin(rightColumn, lineText.size());
        rows.append(endColumn > startColumn
                        ? lineText.mid(startColumn, endColumn - startColumn)
                        : QString());
    }
    return rows.join(QLatin1Char('\n'));
}

void updateColumnSelectionHighlight(MyCodeEditor* editor,
                                    const MyCodeEditorState& state);

void replaceColumnSelectionRows(MyCodeEditor* editor,
                                MyCodeEditorState& state,
                                const QStringList& rows,
                                bool pasteMode)
{
    if (!editor || !hasColumnSelection(state))
        return;

    const auto [firstLine, lastLine] = lineSpan(state);
    const auto [leftColumn, rightColumn] = columnSpan(state);
    const bool hasWidth = rightColumn > leftColumn;
    const bool repeatSingleRow = pasteMode && rows.size() == 1;
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
        maxInsertedColumns = qMax(maxInsertedColumns, rowText.size());

        const int lineLength = block.text().size();
        const int startColumn = qMin(leftColumn, lineLength);
        int endColumn = startColumn;
        if (hasWidth)
            endColumn = qMin(rightColumn, lineLength);

        cursor.setPosition(block.position() + startColumn);
        cursor.setPosition(block.position() + qMax(startColumn, endColumn),
                           QTextCursor::KeepAnchor);

        if (pasteMode) {
            const QString padding =
                leftColumn > lineLength
                    ? QString(leftColumn - lineLength, QLatin1Char(' '))
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
        QTextCursor caret(editor->document());
        caret.setPosition(currentBlock.position()
                          + qMin(collapsedColumn,
                                 currentBlock.text().size()));
        editor->setTextCursor(caret);
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

    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    for (int line = firstLine; line <= lastLine; ++line) {
        const QTextBlock block = editor->document()->findBlockByNumber(line);
        if (!block.isValid())
            continue;

        const int lineLength = block.text().size();
        const int startColumn = qMin(leftColumn, lineLength);
        const int visibleEndColumn = qMin(rightColumn, lineLength);
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
    if (state.columnSelectionActive) {
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

    const QTextCursor anchor = editor->textCursor();
    state.columnSelectionActive = true;
    state.columnSelectionDragging = false;
    state.columnSelectionAwaitingEndpoint = false;
    state.columnSelectionDragMoved = false;
    setColumnPointFromCursor(anchor,
                             &state.columnAnchorLine,
                             &state.columnAnchorColumn);
    setColumnPointFromCursor(cursor,
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
        QRect rect = editor->cursorRect(cursor);
        if (leftColumn > block.text().size()) {
            const QFontMetrics metrics(editor->font());
            rect.translate(metrics.horizontalAdvance(QLatin1Char(' '))
                               * (leftColumn - block.text().size()),
                           0);
        }
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

bool MyCodeEditorState::executeComPortAppend(MyCodeEditor* editor,
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

bool MyCodeEditorState::executeComSignalInsert(MyCodeEditor* editor,
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

bool MyCodeEditorState::executeComInstanceInsert(MyCodeEditor* editor,
                                                 QString* message)
{
    if (!editor)
        return false;

    const TSInstanceInsertTarget target =
        syntax.instanceInsertTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message = target.status == TSInstanceInsertStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No clear instance insert point");
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

bool MyCodeEditorState::executeComAssignInsert(MyCodeEditor* editor,
                                               QString* message)
{
    if (!editor)
        return false;

    const TSAssignInsertTarget target =
        syntax.assignInsertTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message = target.status == TSAssignInsertStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No clear assign insert point");
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

bool MyCodeEditorState::executeComParameterInsert(MyCodeEditor* editor,
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

bool MyCodeEditorState::executeComModuleEndInsert(MyCodeEditor* editor,
                                                  QString* message)
{
    if (!editor)
        return false;

    const TSModuleEndInsertTarget target =
        syntax.moduleEndInsertTargetAt(editor->textCursor().position());
    if (!target.ok()) {
        if (message) {
            *message = target.status == TSModuleEndInsertStatus::NoCurrentModule
                ? QStringLiteral("No current module")
                : QStringLiteral("No clear module end point");
        }
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(target.replaceStartChar);
    cursor.setPosition(target.replaceEndChar, QTextCursor::KeepAnchor);
    cursor.insertText(target.replacementText);
    cursor.endEditBlock();

    QTextCursor caret = editor->textCursor();
    caret.setPosition(target.caretCharAfterEdit);
    editor->setTextCursor(caret);
    return true;
}

bool MyCodeEditorState::comModeActive() const
{
    return modes.comModeActive;
}

QString MyCodeEditorState::comModeBuffer() const
{
    return modes.comBuffer;
}

void MyCodeEditorState::publishComModeState(
    MyCodeEditor* editor,
    const QString& message) const
{
    if (!editor)
        return;
    emit editor->comModeStateChanged(modes.comModeActive,
                                     modes.comBuffer,
                                     message);
}

void MyCodeEditorState::enterComMode(MyCodeEditor* editor,
                                     const QString& message)
{
    clearTemplateSlotMode(editor);
    modes.setComModeActive(true);
    modes.clearComBuffer();
    publishComModeState(editor, message);
}

void MyCodeEditorState::exitComMode(MyCodeEditor* editor)
{
    modes.setComModeActive(false);
    publishComModeState(editor);
}

void MyCodeEditorState::showComModeMessage(MyCodeEditor* editor,
                                           const QString& message)
{
    if (!modes.comModeActive)
        return;
    modes.clearComBuffer();
    publishComModeState(editor, message);
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

namespace {
bool handleComModeKeyPress(MyCodeEditor* editor,
                           QKeyEvent* event,
                           MyCodeEditorState& state)
{
    if (!editor || !event || !state.modes.comModeActive)
        return false;

    auto accept = [event]() {
        event->accept();
        return true;
    };

    if (isBacktickKey(event)) {
        state.exitComMode(editor);
        return accept();
    }

    if (event->key() == Qt::Key_Escape) {
        state.modes.clearComBuffer();
        state.publishComModeState(editor);
        return accept();
    }

    if (event->key() == Qt::Key_Backspace) {
        if (!state.modes.comBuffer.isEmpty())
            state.modes.setComBuffer(
                state.modes.comBuffer.left(state.modes.comBuffer.size() - 1));
        state.publishComModeState(editor);
        return accept();
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QString buffer = state.modes.comBuffer;
        state.modes.clearComBuffer();
        if (isComModeLineBuffer(buffer)) {
            const int moduleLine = buffer.mid(1).toInt();
            if (moduleLine <= 0) {
                const QString message =
                    QStringLiteral("Line number must be >= 1");
                state.publishComModeState(editor, message);
                emit editor->editorStatusMessageRequested(message);
            } else {
                state.publishComModeState(editor);
                emit editor->comRelativeLineRequested(moduleLine);
            }
        } else if (!buffer.isEmpty()) {
            const QString message = comModeCommandFailureMessage(buffer);
            state.publishComModeState(editor, message);
            emit editor->editorStatusMessageRequested(message);
        } else {
            state.publishComModeState(editor);
        }
        return accept();
    }

    if (hasCommandModifier(event))
        return accept();

    const QString text = event->text();
    if (text.isEmpty())
        return accept();

    const QChar ch = text.at(0);
    if (!ch.isPrint())
        return accept();

    const QString nextBuffer = state.modes.comBuffer + text;
    const QString executableCommand = executableComModeCommand(nextBuffer);
    if (!executableCommand.isEmpty()) {
        state.modes.clearComBuffer();
        state.publishComModeState(editor);
        emit editor->comCommandRequested(executableCommand);
        return accept();
    }

    if (isComModeBufferPrefix(nextBuffer)) {
        state.modes.setComBuffer(nextBuffer);
        state.publishComModeState(editor);
        return accept();
    }

    state.modes.clearComBuffer();
    const QString message = comModeCommandFailureMessage(nextBuffer);
    state.publishComModeState(editor, message);
    emit editor->editorStatusMessageRequested(message);
    return accept();
}

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

bool templateSlotCursorInsideActiveRange(
    MyCodeEditor* editor,
    const MyCodeEditorState& state)
{
    if (!editor || !state.templateSlotModeActive())
        return false;
    if (state.templateSlotActiveIndex < 0
        || state.templateSlotActiveIndex >= state.templateSlotRanges.size()) {
        return false;
    }

    const MyCodeEditorState::TemplateSlotRange slot =
        state.templateSlotRanges.at(state.templateSlotActiveIndex);
    QTextCursor cursor = editor->textCursor();
    const int selectionStart = cursor.hasSelection()
        ? cursor.selectionStart()
        : cursor.position();
    const int selectionEnd = cursor.hasSelection()
        ? cursor.selectionEnd()
        : cursor.position();
    return selectionStart >= slot.start && selectionEnd <= slot.end;
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
    editor->setTextCursor(cursor);
    state.selections.highlightTemplateSlots(
        editor,
        templateSlotHighlightRanges(state),
        state.templateSlotActiveIndex);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Slot %1/%2")
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

void MyCodeEditorState::clearTemplateSlotMode(MyCodeEditor* editor,
                                              const QString& message)
{
    const bool wasActive = templateSlotModeActive()
        || !templateSlotRanges.isEmpty();
    templateSlotRanges.clear();
    templateSlotActiveIndex = -1;
    templateSlotSessionStart = -1;
    templateSlotSessionEnd = -1;
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
    if (event->key() != Qt::Key_Tab
        || (modifiers != Qt::NoModifier
            && modifiers != Qt::ShiftModifier)) {
        return false;
    }

    if (modifiers == Qt::ShiftModifier) {
        selectTemplateSlot(
            editor,
            *this,
            qMax(0, templateSlotActiveIndex - 1));
        event->accept();
        return true;
    }

    if (templateSlotActiveIndex + 1 >= templateSlotRanges.size()) {
        const TemplateSlotRange finalSlot =
            templateSlotRanges.at(templateSlotActiveIndex);
        QTextCursor cursor(editor->document());
        cursor.setPosition(finalSlot.end);
        editor->setTextCursor(cursor);
        clearTemplateSlotMode(editor, QStringLiteral("Slot Mode complete"));
        event->accept();
        return true;
    }

    selectTemplateSlot(editor, *this, templateSlotActiveIndex + 1);
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
    for (int i = templateSlotActiveIndex + 1;
         i < templateSlotRanges.size();
         ++i) {
        templateSlotRanges[i].start += delta;
        templateSlotRanges[i].end += delta;
    }
    templateSlotSessionEnd += delta;
    selections.highlightTemplateSlots(
        editor,
        templateSlotHighlightRanges(*this),
        templateSlotActiveIndex);
}

void MyCodeEditorState::handleTemplateSlotCursorChanged(MyCodeEditor* editor)
{
    if (!templateSlotModeActive())
        return;
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

    if (handleComModeKeyPress(editor, event, *this))
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

    if (event->key() == Qt::Key_Escape) {
        enterComMode(editor);
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

    if (handleBracketRangeTab(editor, event))
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
    if (templateSlotModeActive() && editor && event) {
        const QTextCursor targetCursor =
            editor->cursorForPosition(event->position().toPoint());
        const TemplateSlotRange activeSlot =
            templateSlotRanges.at(templateSlotActiveIndex);
        if (targetCursor.position() < activeSlot.start
            || targetCursor.position() > activeSlot.end) {
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

bool MyCodeEditorState::clearSelectedAssignmentRhs(MyCodeEditor* editor)
{
    if (!editor || !editor->document())
        return false;

    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Select assignments to clear RHS"));
        return false;
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QString selectedText =
        editor->toPlainText().mid(selectionStart,
                                  selectionEnd - selectionStart);
    const RtlClearAssignmentRhsReport report =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{selectedText, selectionStart});
    if (!report.canApply()) {
        emit editor->editorStatusMessageRequested(
            report.failureReason.isEmpty()
                ? QStringLiteral("No assignment RHS found")
                : report.failureReason);
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
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Cleared RHS for %1 assignment%2")
            .arg(report.edits.size())
            .arg(report.edits.size() == 1 ? QString() : QStringLiteral("s")));
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
