#include "editorruntime.h"

#include "mycodeeditor.h"

#include "definitionservice.h"
#include "effectivevalueservice.h"
#include "editorcontextmenumodel.h"
#include "rtlbatcheditservice.h"

#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPolygon>
#include <QPushButton>
#include <QRect>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QToolTip>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

#include "formatterservice.h"
#include "tsdocument.h"

namespace {
constexpr int kManualIndentWidth = 4;
constexpr const char* kDiagnosticsEmptyProperty =
    "zeroslackDiagnosticsSelectionsEmpty";
constexpr const char* kSemanticDecorationsEmptyProperty =
    "zeroslackSemanticDecorationsEmpty";

int diagnosticSeverityRank(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return 2;
    case SemanticDiagnostic::Warning:
        return 1;
    case SemanticDiagnostic::Info:
    default:
        return 0;
    }
}

QColor diagnosticSeverityColor(SemanticDiagnostic::Severity severity)
{
    return severity == SemanticDiagnostic::Error
        ? QColor(QStringLiteral("#EF4444"))
        : QColor(QStringLiteral("#FBBF24"));
}

QString diagnosticSeverityLabel(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

QString insertedDocumentText(QTextDocument* document,
                             int position,
                             int length)
{
    if (!document || length <= 0)
        return QString();
    const int documentEnd = qMax(0, document->characterCount() - 1);
    const int start = qBound(0, position, documentEnd);
    const int end = qBound(start, position + length, documentEnd);
    QTextCursor cursor(document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

bool samePackageAvailability(const EditorPackageToolAvailability& left,
                             const EditorPackageToolAvailability& right)
{
    return left.available == right.available
        && left.packageName == right.packageName
        && left.failureMessage == right.failureMessage;
}

QString wavePreviewScopeKey(const MyCodeEditorState& state,
                            const MyCodeEditor* editor)
{
    const EditorAlwaysScopeTarget always =
        state.currentAlwaysScopeTarget(editor, false);
    if (always.ok()) {
        return QStringLiteral("always:%1:%2:%3")
            .arg(always.startPosition)
            .arg(always.endPosition)
            .arg(always.label);
    }
    const EditorModuleScopeTarget module =
        state.currentModuleScopeTarget(editor, false);
    if (module.ok()) {
        return QStringLiteral("module:%1:%2:%3")
            .arg(module.startPosition)
            .arg(module.endPosition)
            .arg(module.label);
    }
    return QStringLiteral("unavailable:%1").arg(module.failureMessage);
}

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
        editor->cachedDocumentSlice(rangeStart, rangeEnd - rangeStart);
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

} // namespace

void MyCodeEditorState::initializeCore(MyCodeEditor* editor)
{
    semantic.init();
    syntax.init();
    gutter.init(editor);
    identity.set(QString());
    semanticRevisionText.clear();
    inlineFilterTextOverlayActive = false;
    inlineFilterTextOverlayStart = -1;
    inlineFilterTextOverlayOriginalLength = 0;
    inlineFilterTextOverlayOriginalText.clear();
    inlineFilterTextOverlayCurrentText.clear();
    qRegisterMetaType<DocumentChange>("DocumentChange");
    qRegisterMetaType<EditorModeSnapshot>("EditorModeSnapshot");
    editor->setProperty(kDiagnosticsEmptyProperty, true);
    editor->setProperty(kSemanticDecorationsEmptyProperty, true);
    editor->setMouseTracking(true);
    editor->setAcceptDrops(true);
}

void MyCodeEditorState::shutdown()
{
    modes.exitAll(EditorModeExitReason::DocumentClosed);
    templateSlots.shutdown(nullptr);
    ++ghostQueryGeneration;
    if (ghostQueryCancellation)
        ghostQueryCancellation->store(true);
    cancelSignalDefinitionEditor();
    signalSelection.shutdown(nullptr);
    columnMode.shutdown(nullptr);
    sourceNavigation.shutdown();
    gutter.destroy();
}

void MyCodeEditorState::bindEditorModes(MyCodeEditor* editor)
{
    const QPointer<MyCodeEditor> target(editor);
    modes.setChangeHandler(
        [target](const EditorModeSnapshot& snapshot) {
            if (target)
                emit target->editorModeStateChanged(snapshot);
        });
    templateSlots.bind(&modes, &selections, editor);
    signalSelection.bind(
        &modes,
        &selections,
        editor,
        [this, target](int cursorPosition) {
            return resolveSignalSelectionCandidate(
                target,
                cursorPosition);
        });
    columnMode.bind(&modes, editor);
    sourceNavigation.bindModeController(
        &modes,
        editor,
        &selections);
    folding.bindModeController(&modes, editor);
}

EditorModeSnapshot MyCodeEditorState::modeSnapshot() const
{
    return modes.snapshot();
}

void MyCodeEditorState::exitInteractionModes(
    EditorModeExitReason reason)
{
    modes.exitAll(reason);
}


void MyCodeEditorState::attachEditorConnections(MyCodeEditor* editor)
{
    highlightRefresh.attachToEditor(editor, [this, editor]() {
        if (suppressNextCursorPresentation)
            return;
        refreshScopeAndCurrentLineHighlight(editor);
        editorPresentationPending = false;
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
            if (dy != 0) {
                sourceNavigation.handleEditorScrolled(editor, selections);
                sourceNavigation.syncMode();
            }
        });
    QObject::connect(
        editor->document(),
        &QTextDocument::contentsChange,
        editor,
        [this, editor](int position, int charsRemoved, int charsAdded) {
            handleDocumentContentsChange(editor,
                                         position,
                                         charsRemoved,
                                         charsAdded);
        });
    QObject::connect(
        editor,
        &QPlainTextEdit::cursorPositionChanged,
        editor,
        [this, editor]() {
            handleVirtualCursorChanged(editor);
            if (suppressNextCursorPresentation) {
                suppressNextCursorPresentation = false;
                return;
            }
            completionWorkflow.handleCursorPositionChanged();
            handleTemplateSlotCursorChanged(editor);
            refreshDerivedEditorState(editor, true);
        });
}

void MyCodeEditorState::attachToEditor(MyCodeEditor* editor)
{
    initializeCore(editor);
    bindEditorModes(editor);
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
        [this, editor](const QModelIndex& index) {
            auto edit = editor->beginSynchronousEditTransaction();
            completionWorkflow.handleCompletionActivated(index);
        });
    selections.highlightCurrentLine(editor);
    folding.refresh(editor, syntax.tsDocument());
    ++hotPathMetrics.fullFoldingRebuilds;
    refreshDerivedEditorState(editor, false);
    gutter.updateViewportMargins(editor);
}

void MyCodeEditorState::handleDocumentContentsChange(
    MyCodeEditor* editor,
    int position,
    int charsRemoved,
    int charsAdded)
{
    if (!editor || !editor->document())
        return;
    QElapsedTimer documentChangeCoreTimer;
    QElapsedTimer documentChangePhaseTimer;
    if (hotPathTimingEnabled) {
        documentChangeCoreTimer.start();
        documentChangePhaseTimer.start();
    }
    const auto finishDocumentChangePhase =
        [this, &documentChangePhaseTimer](std::uint64_t& destination) {
        if (!hotPathTimingEnabled)
            return;
        destination += static_cast<std::uint64_t>(
            documentChangePhaseTimer.nsecsElapsed());
        documentChangePhaseTimer.restart();
    };

    if (signalDefinitionEditor)
        cancelSignalDefinitionEditor();
    if (modes.isActive(EditorModeId::VirtualCursor))
        clearVirtualCursor(editor);
    if (signalSelection.active()
        || signalSelection.hasSelection()) {
        cancelSignalSelectionMode(editor);
    }
    ++ghostQueryGeneration;
    if (ghostQueryCancellation)
        ghostQueryCancellation->store(true);

    const int oldLength = cachedDocumentLength();
    const int newLength = qMax(0, editor->document()->characterCount() - 1);
    const int boundedPosition = qBound(0, position, oldLength);
    const int removedLength = qBound(0,
                                     charsRemoved,
                                     oldLength - boundedPosition);
    int insertedLength = newLength - (oldLength - removedLength);
    if (insertedLength < 0
        || boundedPosition + insertedLength > newLength) {
        insertedLength = qBound(0,
                                charsAdded,
                                newLength - qMin(boundedPosition,
                                                 newLength));
    }

    DocumentChange change;
    change.position = boundedPosition;
    change.removedLength = removedLength;
    change.removedText = cachedDocumentSlice(
        boundedPosition, removedLength);
    change.insertedText = insertedDocumentText(editor->document(),
                                               boundedPosition,
                                               insertedLength);
    change.oldLength = oldLength;
    change.newLength = newLength;
    if (!change.changesText() && oldLength == newLength)
        return;

    const QTextBlock startBlock = editor->document()->findBlock(
        qBound(0, boundedPosition, newLength));
    change.startLine = startBlock.isValid() ? startBlock.blockNumber() : 0;
    change.startColumn = startBlock.isValid()
        ? boundedPosition - startBlock.position()
        : boundedPosition;
    change.oldEndLine = change.startLine
        + change.removedText.count(QLatin1Char('\n'));
    change.newEndLine = change.startLine
        + change.insertedText.count(QLatin1Char('\n'));
    change.lineDelta = change.newEndLine - change.oldEndLine;

    OccurrenceChangeContext occurrenceContext;
    bool useInlineOccurrenceLine = false;
    int occurrenceNewLineStart = -1;
    QString occurrenceNewLineText;
    bool appliedToInlineOverlay = false;
    if (inlineFilterTextOverlayActive) {
        const int overlayEnd =
            inlineFilterTextOverlayStart
            + inlineFilterTextOverlayCurrentText.size();
        const QTextBlock changedBlock = editor->document()->findBlock(
            qBound(0, change.position, newLength));
        const int newLineStart = changedBlock.isValid()
            ? changedBlock.position()
            : -1;
        const int newLineEnd = changedBlock.isValid()
            ? changedBlock.position() + changedBlock.text().size()
            : -1;
        const bool singleLineChange = changedBlock.isValid()
            && !change.removedText.contains(QLatin1Char('\n'))
            && !change.insertedText.contains(QLatin1Char('\n'))
            && change.position >= newLineStart
            && change.newEnd() <= newLineEnd;
        if (change.position >= inlineFilterTextOverlayStart
            && change.oldEnd() <= overlayEnd
            && singleLineChange) {
            QString nextOverlay =
                inlineFilterTextOverlayCurrentText;
            nextOverlay.replace(
                change.position - inlineFilterTextOverlayStart,
                change.removedLength,
                change.insertedText);
            const int nextLength =
                semanticRevisionText.size()
                - inlineFilterTextOverlayOriginalLength
                + nextOverlay.size();
            if (nextLength == change.newLength) {
                inlineFilterTextOverlayCurrentText =
                    std::move(nextOverlay);
                appliedToInlineOverlay = true;
                ++hotPathMetrics.inlineFilterOverlayEdits;
                occurrenceNewLineStart = newLineStart;
                occurrenceNewLineText = changedBlock.text();
                occurrenceContext =
                    selections.prepareDocumentLineChange(
                        change,
                        newLineStart,
                        newLineEnd - change.characterDelta());
                useInlineOccurrenceLine = true;
            }
        }
    }
    if (!appliedToInlineOverlay) {
        finishInlineFilterTextOverlay();
        occurrenceContext = selections.prepareDocumentChange(
            change, semanticRevisionText);
        semanticRevisionText.replace(change.position,
                                     change.removedLength,
                                     change.insertedText);
    }
    ++semanticTextRevision;
    change.revision = semanticTextRevision;
    if (!diagnostics.isEmpty())
        clearDiagnosticHighlights(editor);
    ++hotPathMetrics.documentChanges;
    suppressNextCursorPresentation = true;
    editorPresentationPending = true;
    finishDocumentChangePhase(
        hotPathMetrics.documentChangePrepareNanoseconds);

    const QList<TSChangedRange> changedRanges =
        syntax.applyDocumentChange(change, semanticRevisionText);
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeSyntaxNanoseconds);
    if (const TSDocument* syntaxDocument = syntax.tsDocument()) {
        const bool fullFoldRebuild = folding.applyDocumentChange(
            editor, syntaxDocument, change, changedRanges);
        if (fullFoldRebuild)
            ++hotPathMetrics.fullFoldingRebuilds;
        else
            ++hotPathMetrics.incrementalFoldingUpdates;
    }
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeFoldingNanoseconds);
    remapSemanticDecorations(editor, change);
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeDecorationNanoseconds);

    const OccurrenceIndexUpdate occurrenceUpdate =
        useInlineOccurrenceLine
        ? selections.applyDocumentLineChange(
              editor,
              change,
              occurrenceContext,
              occurrenceNewLineStart,
              occurrenceNewLineText)
        : selections.applyDocumentChange(editor,
                                         change,
                                         occurrenceContext,
                                         semanticRevisionText);
    if (occurrenceUpdate == OccurrenceIndexUpdate::Full)
        ++hotPathMetrics.occurrenceFullBuilds;
    else if (occurrenceUpdate == OccurrenceIndexUpdate::Incremental)
        ++hotPathMetrics.occurrenceIncrementalUpdates;
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeOccurrenceNanoseconds);

    templateSlots.markPresentationPending();
    handleTemplateSlotContentsChange(editor,
                                     change.position,
                                     change.removedLength,
                                     change.insertedText.size(),
                                     false);
    remapGhostAnnotations(editor, change);
    finishDocumentChangePhase(
        hotPathMetrics.documentChangePresentationNanoseconds);
    refreshDerivedEditorState(editor, false);
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeDerivedStateNanoseconds);
    if (hotPathTimingEnabled) {
        hotPathMetrics.documentChangeCoreNanoseconds +=
            static_cast<std::uint64_t>(
                documentChangeCoreTimer.nsecsElapsed());
    }
    QElapsedTimer dispatchTimer;
    if (hotPathTimingEnabled)
        dispatchTimer.start();
    emit editor->documentChangeApplied(change);
    if (hotPathTimingEnabled) {
        hotPathMetrics.documentChangeDispatchNanoseconds +=
            static_cast<std::uint64_t>(dispatchTimer.nsecsElapsed());
    }
}

void MyCodeEditorState::remapGhostAnnotations(
    MyCodeEditor* editor,
    const DocumentChange& change)
{
    ++hotPathMetrics.ghostRemaps;
    if (ghostAnnotations.isEmpty())
        return;

    QList<GhostAnnotation> remapped;
    remapped.reserve(ghostAnnotations.size());
    bool changed = false;
    for (GhostAnnotation annotation : std::as_const(ghostAnnotations)) {
        const int anchorStart = annotation.anchorPosition;
        const int anchorEnd = anchorStart + annotation.anchorLength;
        const bool insertionIntersects = change.removedLength == 0
            && change.position > anchorStart
            && change.position < anchorEnd;
        const bool replacementIntersects = change.removedLength > 0
            && change.position < qMax(anchorStart + 1, anchorEnd)
            && change.oldEnd() > anchorStart;
        if (insertionIntersects || replacementIntersects) {
            changed = true;
            continue;
        }

        if (anchorStart >= change.oldEnd()) {
            annotation.anchorPosition += change.characterDelta();
            annotation.line = qMax(1, annotation.line + change.lineDelta);
            changed = changed || change.characterDelta() != 0
                || change.lineDelta != 0;
        }
        remapped.append(annotation);
    }
    if (changed) {
        ghostAnnotations = remapped;
        ghostPresentationPending = true;
    }
}

void MyCodeEditorState::remapSemanticDecorations(
    MyCodeEditor* editor,
    const DocumentChange& change)
{
    if (semanticDecorations.isEmpty())
        return;

    QList<SemanticDecoration> remapped;
    remapped.reserve(semanticDecorations.size());
    for (SemanticDecoration decoration : std::as_const(semanticDecorations)) {
        const int start = decoration.startPosition;
        const int end = start + decoration.length;
        const bool insertionIntersects =
            change.removedLength == 0
            && change.position > start
            && change.position < end;
        const bool replacementIntersects =
            change.removedLength > 0
            && change.position < end
            && change.oldEnd() > start;
        if (insertionIntersects || replacementIntersects)
            continue;
        if (change.oldEnd() <= start)
            decoration.startPosition += change.characterDelta();
        if (decoration.isValid())
            remapped.append(std::move(decoration));
    }
    semanticDecorations = std::move(remapped);
    refreshSemanticDecorationPresentation(editor);
}

void MyCodeEditorState::refreshSemanticDecorationPresentation(
    MyCodeEditor* editor)
{
    if (!editor)
        return;

    QList<SemanticDecoration> visible;
    visible.reserve(semanticDecorations.size());
    const TSDocument* document = syntax.tsDocument();
    for (const SemanticDecoration& decoration :
         std::as_const(semanticDecorations)) {
        if (!decoration.isValid())
            continue;
        if (document
            && (document->isCommentAt(decoration.startPosition)
                || document->isCommentAt(
                    decoration.startPosition + decoration.length - 1))) {
            continue;
        }
        visible.append(decoration);
    }
    selections.highlightSemanticDecorations(editor, visible);
    editor->setProperty(kSemanticDecorationsEmptyProperty,
                        visible.isEmpty());
}

void MyCodeEditorState::refreshDerivedEditorState(
    MyCodeEditor* editor,
    bool allowWavePreviewSignal)
{
    if (!editor)
        return;

    const EditorPackageToolAvailability availability =
        currentPackageToolAvailability(editor);
    if (!packageToolAvailabilityInitialized
        || !samePackageAvailability(availability,
                                    lastPackageToolAvailability)) {
        lastPackageToolAvailability = availability;
        packageToolAvailabilityInitialized = true;
        emit editor->packageToolAvailabilityChanged(availability);
    }

    const QString scopeKey = wavePreviewScopeKey(*this, editor);
    if (!allowWavePreviewSignal) {
        lastWavePreviewScopeKey = scopeKey;
        return;
    }

    if (lastWavePreviewScopeKey.isEmpty()) {
        lastWavePreviewScopeKey = scopeKey;
        emit editor->wavePreviewScopeChanged();
    } else if (scopeKey != lastWavePreviewScopeKey) {
        lastWavePreviewScopeKey = scopeKey;
        emit editor->wavePreviewScopeChanged();
    }
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
    const MyCodeEditor* editor,
    bool allowLargeFileScopeBuild) const
{
    EditorAlwaysScopeTarget result;
    if (!editor) {
        result.failureMessage = QStringLiteral("No document selected.");
        return result;
    }

    const QTextCursor cursor = editor->textCursor();
    const QString& syntaxText =
        allowLargeFileScopeBuild && inlineFilterTextOverlayActive
        ? editor->cachedDocumentText()
        : semanticRevisionText;
    const int syntaxTextLength = cachedDocumentLength();
    const TSAlwaysScopeTarget target =
        syntax.alwaysScopeTargetAt(cursor.position(),
                                   cursor.hasSelection()
                                       ? cursor.selectionStart()
                                       : -1,
                                   cursor.hasSelection()
                                       ? cursor.selectionEnd()
                                       : -1,
                                   syntaxText,
                                   syntaxTextLength,
                                   allowLargeFileScopeBuild);
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
    const MyCodeEditor* editor,
    bool allowLargeFileScopeBuild) const
{
    EditorModuleScopeTarget result;
    if (!editor) {
        result.failureMessage = QStringLiteral("No document selected.");
        return result;
    }

    const QTextCursor cursor = editor->textCursor();
    const QString& syntaxText =
        allowLargeFileScopeBuild && inlineFilterTextOverlayActive
        ? editor->cachedDocumentText()
        : semanticRevisionText;
    const int syntaxTextLength = cachedDocumentLength();
    const TSModuleScopeTarget target =
        syntax.moduleScopeTargetAt(cursor.position(),
                                   cursor.hasSelection()
                                       ? cursor.selectionStart()
                                       : -1,
                                   cursor.hasSelection()
                                       ? cursor.selectionEnd()
                                       : -1,
                                   syntaxText,
                                   syntaxTextLength,
                                   allowLargeFileScopeBuild);
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
        false,
        semanticDocumentRevision());
    context.packageName = syntax.packageNameAt(semanticPosition);
    if (includeDocumentText)
        context.documentText = editor->cachedDocumentText();
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
    sourceNavigation.syncMode();
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
    sourceNavigation.syncMode();
}


void MyCodeEditorState::startTemplateSlotMode(
    MyCodeEditor* editor,
    int insertionStart,
    int insertedLength,
    const CodeTemplateSlotList& slotMetadata)
{
    templateSlots.start(editor,
                        insertionStart,
                        insertedLength,
                        slotMetadata);
}

bool MyCodeEditorState::templateSlotModeActive() const
{
    return templateSlots.active();
}

int MyCodeEditorState::templateSlotModeActiveIndex() const
{
    return templateSlots.activeIndex();
}

int MyCodeEditorState::templateSlotModeSlotCount() const
{
    return templateSlots.slotCount();
}

bool MyCodeEditorState::templateSlotModeBlinkOn() const
{
    return templateSlots.blinkOn();
}

bool MyCodeEditorState::columnSelectionActiveForCommand() const
{
    return columnMode.selectionActive();
}

bool MyCodeEditorState::virtualCursorActiveForTest() const
{
    return columnMode.virtualCursorActive();
}

int MyCodeEditorState::virtualCursorLineForTest() const
{
    return columnMode.virtualCursorLine();
}

int MyCodeEditorState::virtualCursorColumnForTest() const
{
    return columnMode.virtualCursorColumn();
}

void MyCodeEditorState::clearVirtualCursor(
    MyCodeEditor* editor)
{
    columnMode.clearVirtualCursor(editor);
}

void MyCodeEditorState::handleVirtualCursorChanged(
    MyCodeEditor* editor)
{
    columnMode.handleVirtualCursorChanged(editor);
}

void MyCodeEditorState::prepareVirtualCursorInput(
    MyCodeEditor* editor)
{
    columnMode.prepareVirtualCursorInput(editor);
}

bool MyCodeEditorState::handleVirtualCursorKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (templateSlotModeActive())
        return false;
    return columnMode.handleVirtualCursorKeyPress(
        editor,
        event);
}

QStringList MyCodeEditorState::columnSelectionRowTexts(
    MyCodeEditor* editor) const
{
    return columnMode.selectedRows(editor);
}

bool MyCodeEditorState::applyColumnSelectionRowTexts(
    MyCodeEditor* editor,
    const QStringList& rows,
    bool replaceSelection,
    QString* message)
{
    return columnMode.applyRows(
        editor,
        rows,
        replaceSelection,
        message);
}

void MyCodeEditorState::clearTemplateSlotMode(
    MyCodeEditor* editor,
    const QString& message,
    bool updatePresentation)
{
    templateSlots.clear(editor,
                        message,
                        updatePresentation);
}

bool MyCodeEditorState::handleTemplateSlotKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    return templateSlots.handleKeyPress(editor, event);
}

void MyCodeEditorState::handleTemplateSlotContentsChange(
    MyCodeEditor* editor,
    int position,
    int charsRemoved,
    int charsAdded,
    bool updatePresentation)
{
    templateSlots.handleContentsChange(
        editor,
        position,
        charsRemoved,
        charsAdded,
        updatePresentation);
}

void MyCodeEditorState::handleTemplateSlotCursorChanged(
    MyCodeEditor* editor)
{
    templateSlots.handleCursorChanged(editor);
}

bool MyCodeEditorState::handleKeyPress(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !event)
        return false;

    if (event->key() == Qt::Key_Escape) {
        const EditorModeId escapeMode = modes.escapeTarget();
        switch (escapeMode) {
        case EditorModeId::SignalSelection:
            cancelSignalSelectionMode(editor);
            emit editor->editorStatusMessageRequested(
                QStringLiteral("Signal selection canceled"));
            break;
        case EditorModeId::FoldRegion:
            folding.cancelFoldRegionMarkMode(editor);
            break;
        case EditorModeId::FoldShelf:
            folding.cancelFoldShelfMode(editor);
            break;
        case EditorModeId::TemplateSlots:
            handleTemplateSlotKeyPress(editor, event);
            return true;
        case EditorModeId::VirtualCursor:
            clearVirtualCursor(editor);
            break;
        case EditorModeId::InlineCandidates:
        case EditorModeId::CompletionCandidates:
            if (!completionWorkflow.handleCompletionPopupKey(event)) {
                modes.exit(escapeMode,
                           EditorModeExitReason::Canceled);
            }
            break;
        case EditorModeId::ColumnSelection:
            columnMode.clearSelection(editor);
            break;
        case EditorModeId::SourceNavigation:
            sourceNavigation.handleEscape(editor, selections);
            modes.exit(EditorModeId::SourceNavigation,
                       EditorModeExitReason::Canceled);
            break;
        case EditorModeId::None:
            break;
        }
        if (escapeMode != EditorModeId::None) {
            event->accept();
            return true;
        }
    }

    const EditorModeId primaryMode = modes.primaryMode();
    if (primaryMode == EditorModeId::SignalSelection) {
        event->accept();
        return true;
    }
    if (primaryMode == EditorModeId::FoldRegion) {
        event->accept();
        return true;
    }
    if (primaryMode == EditorModeId::FoldShelf
        && !event->text().isEmpty()
        && !event->modifiers().testFlag(Qt::ControlModifier)) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Fold Shelf: drag custom fold blocks"));
        event->accept();
        return true;
    }

    if (primaryMode == EditorModeId::TemplateSlots
        && handleTemplateSlotKeyPress(editor, event)) {
        return true;
    }

    if ((primaryMode == EditorModeId::VirtualCursor
         || primaryMode == EditorModeId::None)
        && handleVirtualCursorKeyPress(editor, event)) {
        return true;
    }

    if (event->key() == Qt::Key_Escape
        && sourceNavigation.handleEscape(editor, selections)) {
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

    if (handleMoveLineBlock(
            editor,
            event,
            modes.isActive(EditorModeId::ColumnSelection))) {
        return true;
    }

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
        sourceNavigation.syncMode();
        return true;
    }

    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return true;
    }

    if (columnMode.handleClipboard(editor, event))
        return true;

    if (columnMode.handleSelectionNavigation(
            editor,
            event)) {
        return true;
    }

    if (columnMode.handleSelectionKeyInput(
            editor,
            event)) {
        return true;
    }

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

    if (completionWorkflow.handleCompletionPopupKey(event))
        return true;

    if (event->key() == Qt::Key_Tab
        && event->modifiers() == Qt::NoModifier) {
        QTextCursor cursor = editor->textCursor();
        cursor.insertText(QStringLiteral("    "));
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }

    return false;
}

void MyCodeEditorState::beginSynchronousEditTransaction()
{
    ++synchronousEditTransactionDepth;
}

void MyCodeEditorState::endSynchronousEditTransaction(MyCodeEditor* editor)
{
    if (synchronousEditTransactionDepth <= 0)
        return;
    --synchronousEditTransactionDepth;
    if (synchronousEditTransactionDepth != 0)
        return;
    ++completedSynchronousEditTransactions;
    finishEditorInput(editor);
}

EditorSynchronousEditState
MyCodeEditorState::synchronousEditStateForTest() const
{
    EditorSynchronousEditState editState;
    editState.transactionDepth = synchronousEditTransactionDepth;
    editState.presentationPending = editorPresentationPending;
    editState.cursorPresentationSuppressed = suppressNextCursorPresentation;
    editState.completedTransactionCount = completedSynchronousEditTransactions;
    return editState;
}

void MyCodeEditorState::finishEditorInput(MyCodeEditor* editor)
{
    suppressNextCursorPresentation = false;
    if (!editorPresentationPending)
        return;
    sourceNavigation.handleEditorContentChanged(editor, selections);
    sourceNavigation.syncMode();
    templateSlots.flushPendingPresentation(editor);
    if (ghostPresentationPending) {
        setGhostAnnotations(editor, ghostAnnotations);
        ghostPresentationPending = false;
    }
    refreshScopeAndCurrentLineHighlight(editor);
    editorPresentationPending = false;
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
    if (!editor || !event)
        return false;

    const int y = static_cast<int>(event->position().y());
    if (y < 0 || y >= editor->viewport()->height())
        return false;

    ++hotPathMetrics.gutterBlockProbes;
    const QTextBlock block = editor->cursorForPosition(QPoint(0, y)).block();
    if (!block.isValid() || !block.isVisible())
        return false;
    const EditorBlockGeometry geometry = editor->blockGeometry(
        block.blockNumber());
    if (y < geometry.top || y > geometry.top + geometry.height)
        return false;

    if (folding.foldRegionMarkModeActive())
        return folding.handleFoldRegionGutterLine(editor,
                                                  block.blockNumber());
    if (event->position().x() > 14)
        return false;
    return folding.toggleFoldAtLine(editor, block.blockNumber());
}

bool MyCodeEditorState::handleGutterMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!editor || !event)
        return false;

    const int y = static_cast<int>(event->position().y());
    if (y < 0 || y >= editor->viewport()->height())
        return false;

    ++hotPathMetrics.gutterBlockProbes;
    const QTextBlock block = editor->cursorForPosition(QPoint(0, y)).block();
    if (!block.isValid() || !block.isVisible())
        return false;
    const EditorBlockGeometry geometry = editor->blockGeometry(
        block.blockNumber());
    if (y < geometry.top || y > geometry.top + geometry.height)
        return false;

    const qreal x = event->position().x();
    if (x >= 14.0 && x <= 28.0
        && diagnosticSeverityByLine.contains(block.blockNumber())) {
        const QString tooltip =
            diagnosticTooltipForLine(block.blockNumber());
        if (!tooltip.isEmpty()) {
            QStringList richRows;
            const QStringList rows = tooltip.split(QLatin1Char('\n'));
            richRows.reserve(rows.size());
            for (const QString& row : rows)
                richRows.append(row.toHtmlEscaped());
            QToolTip::showText(
                event->globalPosition().toPoint(),
                richRows.join(QStringLiteral("<br>")),
                editor);
        }
        return true;
    }
    if (x >= 14.0 && x <= 28.0)
        QToolTip::hideText();

    const bool handled =
        folding.handleFoldRegionHoverLine(editor, block.blockNumber());
    if (handled)
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
    return handled;
}

void MyCodeEditorState::paintGutterDecorations(
    MyCodeEditor* editor,
    QPainter& painter,
    const QRect& rect) const
{
    folding.paintGutter(editor, painter, rect);
    if (!editor || diagnosticSeverityByLine.isEmpty())
        return;

    QTextBlock block = editor->firstVisibleBlock();
    int top = static_cast<int>(
        editor->blockBoundingGeometry(block)
            .translated(editor->contentOffset())
            .top());
    int bottom =
        top + static_cast<int>(editor->blockBoundingRect(block).height());
    while (block.isValid() && top <= rect.bottom()) {
        const auto severity =
            diagnosticSeverityByLine.constFind(block.blockNumber());
        if (severity != diagnosticSeverityByLine.constEnd()) {
            const int middle = top + (bottom - top) / 2;
            const QPolygon triangle{
                QPoint(21, middle - 6),
                QPoint(15, middle + 5),
                QPoint(27, middle + 5)
            };
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(diagnosticSeverityColor(severity.value()));
            painter.drawPolygon(triangle);
            painter.setPen(Qt::white);
            QFont iconFont = painter.font();
            iconFont.setBold(true);
            iconFont.setPixelSize(9);
            painter.setFont(iconFont);
            painter.drawText(
                QRect(15, middle - 5, 12, 10),
                Qt::AlignCenter,
                QStringLiteral("!"));
            painter.restore();
        }

        block = block.next();
        top = bottom;
        bottom =
            top + static_cast<int>(editor->blockBoundingRect(block).height());
    }
}

void MyCodeEditorState::paintDiagnosticOverview(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    if (!editor
        || !event
        || diagnosticSeverityByLine.isEmpty()
        || editor->blockCount() <= 0) {
        return;
    }

    QPainter painter(editor->viewport());
    const int markerWidth = 5;
    const int markerHeight = 4;
    const int x = qMax(0, editor->viewport()->width() - markerWidth);
    const int availableHeight =
        qMax(1, editor->viewport()->height() - markerHeight);
    const int denominator = qMax(1, editor->blockCount() - 1);
    for (auto iterator = diagnosticSeverityByLine.constBegin();
         iterator != diagnosticSeverityByLine.constEnd();
         ++iterator) {
        const int y = qRound(
            static_cast<qreal>(qBound(0,
                                     iterator.key(),
                                     editor->blockCount() - 1))
            / denominator
            * availableHeight);
        const QRect marker(x, y, markerWidth, markerHeight);
        if (!event->rect().intersects(marker))
            continue;
        painter.fillRect(marker,
                         diagnosticSeverityColor(iterator.value()));
    }
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
        const TSDocument* syntaxDocument = syntax.tsDocument();
        if (syntaxDocument
            && (syntaxDocument->isCommentAt(anchorPosition)
                || (annotation.anchorLength > 0
                    && syntaxDocument->isCommentAt(
                        qMin(documentEnd,
                             anchorPosition
                                 + annotation.anchorLength - 1))))) {
            continue;
        }
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
    columnMode.paint(editor, event);
}

void MyCodeEditorState::handleContextMenu(
    MyCodeEditor* editor,
    QContextMenuEvent* event)
{
    if (signalSelection.active()
        && editor && event) {
        signalSelection.completeForContextMenu();
        const QTextCursor contextCursor =
            editor->cursorForPosition(event->pos());
        QMenu menu(editor);
        EditorContextMenuRequest request;
        request.actionContext.workspacePath =
            hierarchyInstance.workspacePath;
        request.actionContext.fileName = identity.current();
        request.actionContext.moduleName =
            currentModuleName(editor);
        request.actionContext.semanticState =
            !semanticRevisionText.isEmpty()
                && semanticRevisionText
                       == editor->toPlainText()
                ? EditorActionSemanticState::Current
                : EditorActionSemanticState::Stale;
        request.actionContext.resolvedHierarchy =
            hierarchyInstance;
        EditorContextMenuCapability capability;
        capability.actionId =
            QStringLiteral("refactor.createAssignmentQueue");
        capability.executable =
            signalSelection.hasSelection();
        capability.unavailableReason =
            QStringLiteral("Select at least one signal.");
        request.capabilities.append(capability);
        const EditorContextMenuModel model =
            buildEditorContextMenuModel(request);
        QMenu* refactorMenu =
            menu.addMenu(QStringLiteral("Refactor"));
        refactorMenu->setObjectName(
            QStringLiteral("editorContextMenu.refactor"));
        const EditorContextMenuItem item =
            !model.sections.isEmpty()
                && !model.sections.first().items.isEmpty()
                ? model.sections.first().items.first()
                : EditorContextMenuItem();
        QAction* createQueue =
            refactorMenu->addAction(item.text);
        createQueue->setObjectName(
            QStringLiteral("refactor.createAssignmentQueue"));
        createQueue->setEnabled(item.enabled);
        createQueue->setProperty(
            "visibleReason", item.visibleReason);
        if (!item.visibleReason.isEmpty())
            createQueue->setStatusTip(item.visibleReason);
        bool actionTriggered = false;
        QObject::connect(
            createQueue,
            &QAction::triggered,
            editor,
            [this,
             editor,
             position = contextCursor.position(),
             &actionTriggered]() {
                actionTriggered = true;
                QString message;
                if (!createAssignmentQueueAt(
                        editor, position, &message)
                    && !message.isEmpty()) {
                    emit editor->editorStatusMessageRequested(message);
                }
            });
        menu.exec(event->globalPos());
        if (!actionTriggered)
            cancelSignalSelectionMode(editor);
        event->accept();
        return;
    }
    sourceNavigation.handleContextMenu(
        editor,
        event,
        sourceContextProvider(editor));
}

bool MyCodeEditorState::editInstanceSlotsAt(
    MyCodeEditor* editor,
    int cursorPosition,
    QString* message)
{
    if (!editor || !editor->document()) {
        if (message)
            *message = QStringLiteral("No editor document");
        return false;
    }

    const TSInstantiationTarget target =
        syntax.instantiationAt(cursorPosition);
    if (!target.ok()) {
        if (message)
            *message = QStringLiteral("No complete module instantiation");
        return false;
    }

    CodeTemplateSlotList metadata;
    const auto appendSlots =
        [&metadata, &target](const QList<TSExpressionSlot>& source,
                             const QString& prefix) {
            for (int index = 0; index < source.size(); ++index) {
                const TSExpressionSlot& expression = source.at(index);
                if (!expression.ok())
                    continue;
                CodeTemplateSlot slot;
                slot.name = expression.name.isEmpty()
                    ? QStringLiteral("%1 %2")
                          .arg(prefix)
                          .arg(index + 1)
                    : expression.name;
                slot.start = expression.startChar - target.startChar;
                slot.length =
                    expression.endChar - expression.startChar;
                metadata.append(slot);
            }
        };
    appendSlots(target.parameterActuals,
                QStringLiteral("parameter"));
    appendSlots(target.portActuals,
                QStringLiteral("port"));
    if (metadata.isEmpty()) {
        if (message)
            *message = QStringLiteral("Instance has no editable actuals");
        return false;
    }

    startTemplateSlotMode(editor,
                          target.startChar,
                          target.endChar - target.startChar,
                          metadata);
    if (!templateSlotModeActive()) {
        if (message)
            *message = QStringLiteral("Instance slot ranges are invalid");
        return false;
    }
    if (message)
        *message = QStringLiteral("Editing instance slots");
    return true;
}

QString MyCodeEditorState::signalDefinitionCandidateAt(
    const MyCodeEditor* editor,
    int cursorPosition,
    QString* failureReason) const
{
    const auto fail = [failureReason](const QString& reason) {
        if (failureReason)
            *failureReason = reason;
        return QString();
    };
    if (!editor || !editor->document())
        return fail(QStringLiteral("No editor document"));

    const TSUndefinedSignalContext context =
        syntax.undefinedSignalContextAt(cursorPosition);
    if (!context.ok())
        return fail(QStringLiteral("Identifier is not an undeclared signal context"));

    SemanticIndex* semanticIndex =
        SemanticIndex::getInstance();
    const QString analyzedText =
        semanticIndex
            ? semanticIndex->getCachedFileContent(
                  identity.current())
            : QString();
    const QString moduleName =
        currentModuleNameAt(
            context.identifier.startChar);
    if (analyzedText.isEmpty()
        || moduleName.isEmpty()) {
        return fail(QStringLiteral(
            "No analyzed module baseline is available"));
    }

    bool analyzedModuleFound = false;
    const QList<SemanticSymbolRecord> moduleRecords =
        semanticIndex->getSymbolRecordsByName(moduleName);
    for (const SemanticSymbolRecord& record : moduleRecords) {
        if (!EditorFileIdentity::same(
                record.location.fileName,
                identity.current())) {
            continue;
        }
        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))) {
            analyzedModuleFound = true;
            break;
        }
    }
    if (!analyzedModuleFound) {
        return fail(QStringLiteral(
            "The current module has no analyzed semantic baseline"));
    }

    const EditorSemanticContext identifierContext =
        semanticContextForPosition(
            editor,
            context.identifier.startChar,
            false);
    DefinitionQuery existingQuery;
    existingQuery.symbolName =
        context.identifier.text;
    existingQuery.fileName =
        identifierContext.fileName;
    existingQuery.moduleName =
        identifierContext.moduleName;
    existingQuery.linePrefixBeforeCursor =
        identifierContext.lineUpToCursor;
    existingQuery.cursorLine =
        identifierContext.cursorLine;
    existingQuery.cursorColumn =
        identifierContext.column;
    const DefinitionResult existingDefinition =
        DefinitionService::getInstance()->resolveDefinition(
            existingQuery);
    if (existingDefinition.found) {
        return fail(QStringLiteral(
            "Identifier already resolves to an analyzed declaration"));
    }

    if (context.kind
        == TSUndefinedSignalContextKind::
            ProceduralAssignmentLhs) {
        if (failureReason)
            failureReason->clear();
        return QStringLiteral("logic %1;")
            .arg(context.identifier.text);
    }

    if (context.kind
        != TSUndefinedSignalContextKind::NamedPortActual
        || context.formalName.isEmpty()
        || !context.instantiation.ok()) {
        return fail(QStringLiteral(
            "No exact instance-port context"));
    }
    if (!hierarchyInstance.isBound()) {
        return fail(QStringLiteral(
            "No current hierarchy instance context is bound"));
    }

    const EditorSemanticContext formalContext =
        semanticContextForPosition(
            editor, context.formalStartChar, false);
    DefinitionQuery definitionQuery;
    definitionQuery.symbolName = context.formalName;
    definitionQuery.fileName = formalContext.fileName;
    definitionQuery.moduleName = formalContext.moduleName;
    definitionQuery.linePrefixBeforeCursor =
        formalContext.lineUpToCursor;
    definitionQuery.cursorLine = formalContext.cursorLine;
    definitionQuery.cursorColumn = formalContext.column;
    const DefinitionResult definition =
        DefinitionService::getInstance()->resolveDefinition(
            definitionQuery);
    if (!definition.found
        || !SymbolTaxonomy::isPortDeclaration(
            semanticMetadataForSymbolRecord(
                definition.symbolRecord))) {
        return fail(QStringLiteral(
            "Slang did not resolve the exact formal port"));
    }

    HierarchyInstanceContext childContext = hierarchyInstance;
    if (childContext.isBound()) {
        if (!childContext.instancePath.endsWith(
                QLatin1Char('.'))) {
            childContext.instancePath += QLatin1Char('.');
        }
        childContext.instancePath +=
            context.instantiation.instanceName;
    }

    EffectiveValueQuery valueQuery;
    valueQuery.symbol = definition.symbolRecord;
    valueQuery.instanceContext = childContext;
    if (EditorFileIdentity::same(
            definition.symbolRecord.location.fileName,
            identity.current())) {
        const SemanticSymbolLocation& location =
            definition.symbolRecord.location;
        const bool unchangedDeclarationAnchor =
            location.position >= 0
            && location.length > 0
            && location.position + location.length
                <= analyzedText.size()
            && location.position + location.length
                <= semanticRevisionText.size()
            && analyzedText.mid(
                   location.position,
                   location.length)
               == semanticRevisionText.mid(
                   location.position,
                   location.length);
        if (analyzedText != semanticRevisionText
            && !unchangedDeclarationAnchor) {
            return fail(QStringLiteral(
                "The formal declaration changed since analysis"));
        }
        if (analyzedText == semanticRevisionText) {
            valueQuery.documentText =
                semanticRevisionText;
            valueQuery.documentRevision =
                semanticDocumentRevision();
        }
    }
    const EffectiveValueResult effective =
        EffectiveValueService::getInstance()->resolve(
            valueQuery);
    if (!effective.current()) {
        return fail(
            effective.failureReason.isEmpty()
                ? QStringLiteral(
                      "Current Slang formal-port type is unavailable")
                : effective.failureReason);
    }

    QString typeText = effective.resolvedTypeText.trimmed();
    QString unpacked =
        effective.unpackedDimensionsText.trimmed();
    if (typeText.isEmpty()) {
        return fail(QStringLiteral(
            "Current Slang formal-port type is unavailable"));
    }
    if (!unpacked.isEmpty()
        && typeText.endsWith(unpacked)) {
        typeText.chop(unpacked.size());
        typeText = typeText.trimmed();
    }
    const QString packed =
        effective.packedDimensionsText.trimmed();
    if (!packed.isEmpty() && !typeText.contains(packed)) {
        if (!typeText.isEmpty())
            typeText += QLatin1Char(' ');
        typeText += packed;
    }

    QString declaration = typeText;
    if (!declaration.isEmpty())
        declaration += QLatin1Char(' ');
    declaration += context.identifier.text;
    if (!unpacked.isEmpty()) {
        declaration += QLatin1Char(' ');
        declaration += unpacked;
    }
    declaration += QLatin1Char(';');
    if (failureReason)
        failureReason->clear();
    return declaration;
}

void MyCodeEditorState::cancelSignalDefinitionEditor()
{
    if (signalDefinitionEditor)
        signalDefinitionEditor->deleteLater();
    signalDefinitionEditor.clear();
    signalDefinitionIdentifierStart = -1;
    signalDefinitionDocumentRevision = 0;
    signalDefinitionFileName.clear();
}

bool MyCodeEditorState::beginSignalDefinitionEditor(
    MyCodeEditor* editor,
    int cursorPosition,
    QString* failureReason)
{
    const QString candidate =
        signalDefinitionCandidateAt(
            editor, cursorPosition, failureReason);
    if (candidate.isEmpty())
        return false;

    const TSUndefinedSignalContext context =
        syntax.undefinedSignalContextAt(cursorPosition);
    if (!context.ok())
        return false;

    cancelSignalDefinitionEditor();
    QLineEdit* lineEdit = new QLineEdit(editor->viewport());
    signalDefinitionEditor = lineEdit;
    signalDefinitionIdentifierStart =
        context.identifier.startChar;
    signalDefinitionDocumentRevision =
        semanticDocumentRevision();
    signalDefinitionFileName = identity.current();
    lineEdit->setObjectName(
        QStringLiteral("signalDefinitionInlineEditor"));
    lineEdit->setText(candidate);
    lineEdit->setToolTip(
        QStringLiteral("Enter: create signal definition; Esc: cancel"));
    lineEdit->setStyleSheet(
        QStringLiteral(
            "QLineEdit {"
            " border: 1px solid palette(highlight);"
            " border-radius: 3px;"
            " padding: 3px 6px;"
            " background: palette(base);"
            " color: palette(text);"
            "}"));

    QTextCursor anchor(editor->document());
    anchor.setPosition(context.identifier.startChar);
    const QRect anchorRect = editor->cursorRect(anchor);
    const int desiredWidth = qBound(
        220,
        lineEdit->fontMetrics().horizontalAdvance(candidate)
            + 32,
        qMax(220, editor->viewport()->width() - 12));
    const int height = lineEdit->sizeHint().height();
    int x = qBound(4,
                   anchorRect.left(),
                   qMax(4,
                        editor->viewport()->width()
                            - desiredWidth - 4));
    int y = anchorRect.bottom() + 3;
    if (y + height > editor->viewport()->height() - 4)
        y = qMax(4, anchorRect.top() - height - 3);
    lineEdit->setGeometry(x, y, desiredWidth, height);

    QObject::connect(
        lineEdit,
        &QLineEdit::returnPressed,
        editor,
        [this, editor, lineEdit]() {
            QString reason;
            if (!confirmSignalDefinition(
                    editor, lineEdit->text(), &reason)
                && !reason.isEmpty()) {
                emit editor->editorStatusMessageRequested(reason);
            }
        });
    QShortcut* escape = new QShortcut(
        QKeySequence(Qt::Key_Escape), lineEdit);
    QObject::connect(escape,
                     &QShortcut::activated,
                     lineEdit,
                     [this]() {
        cancelSignalDefinitionEditor();
    });
    lineEdit->show();
    lineEdit->setFocus(Qt::PopupFocusReason);
    lineEdit->selectAll();
    return true;
}

bool MyCodeEditorState::confirmSignalDefinition(
    MyCodeEditor* editor,
    const QString& declaration,
    QString* failureReason)
{
    const auto fail = [failureReason](const QString& reason) {
        if (failureReason)
            *failureReason = reason;
        return false;
    };
    if (!editor || !editor->document())
        return fail(QStringLiteral("No editor document"));
    if (signalDefinitionIdentifierStart < 0) {
        return fail(QStringLiteral(
            "No pending signal definition"));
    }
    if (signalDefinitionDocumentRevision
            != semanticDocumentRevision()
        || !EditorFileIdentity::same(
            signalDefinitionFileName, identity.current())) {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "Signal definition context is stale"));
    }

    const TSUndefinedSignalContext context =
        syntax.undefinedSignalContextAt(
            signalDefinitionIdentifierStart);
    if (!context.ok()
        || context.identifier.startChar
            != signalDefinitionIdentifierStart) {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "Signal definition context is stale"));
    }

    const QString declarationText = declaration.trimmed();
    if (declarationText.isEmpty())
        return fail(QStringLiteral("Signal declaration is empty"));
    const TSSignalInsertTarget target =
        syntax.signalInsertTargetAt(
            signalDefinitionIdentifierStart);
    if (!target.ok()) {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "No clear signal declaration section"));
    }

    QString insertionText;
    if (target.insertText.startsWith(
            QLatin1Char('\n'))) {
        insertionText =
            target.insertText + declarationText;
    } else if (target.insertText.endsWith(
                   QLatin1Char('\n'))) {
        insertionText =
            target.insertText.left(
                target.insertText.size() - 1)
            + declarationText
            + QLatin1Char('\n');
    } else {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "Invalid signal declaration anchor"));
    }

    const QTextCursor original = editor->textCursor();
    const int originalPosition = original.position();
    const int originalAnchor = original.anchor();
    const int verticalScroll =
        editor->verticalScrollBar()->value();
    const int horizontalScroll =
        editor->horizontalScrollBar()->value();
    const int insertPosition = target.insertChar;
    cancelSignalDefinitionEditor();

    QTextCursor insertion(editor->document());
    insertion.beginEditBlock();
    insertion.setPosition(insertPosition);
    insertion.insertText(insertionText);
    insertion.endEditBlock();

    const auto adjustedPosition =
        [insertPosition,
         delta = insertionText.size()](int position) {
            return position >= insertPosition
                ? position + delta : position;
        };
    QTextCursor restored(editor->document());
    restored.setPosition(
        adjustedPosition(originalAnchor));
    restored.setPosition(
        adjustedPosition(originalPosition),
        QTextCursor::KeepAnchor);
    editor->setTextCursor(restored);
    editor->verticalScrollBar()->setValue(verticalScroll);
    editor->horizontalScrollBar()->setValue(horizontalScroll);

    if (failureReason)
        failureReason->clear();
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Created signal definition for %1")
            .arg(context.identifier.text));
    return true;
}

bool MyCodeEditorState::startSignalSelectionMode(
    MyCodeEditor* editor,
    QString* message)
{
    cancelSignalDefinitionEditor();
    return signalSelection.start(editor, message);
}

void MyCodeEditorState::cancelSignalSelectionMode(
    MyCodeEditor* editor)
{
    signalSelection.cancel(editor);
}

bool MyCodeEditorState::signalSelectionModeActive() const
{
    return signalSelection.active();
}

QStringList MyCodeEditorState::selectedSignalNames() const
{
    return signalSelection.selectedNames();
}


bool MyCodeEditorState::toggleSignalSelectionAt(
    MyCodeEditor* editor,
    int cursorPosition,
    bool toggle,
    bool desiredState)
{
    return signalSelection.toggleAt(
        editor,
        cursorPosition,
        toggle,
        desiredState);
}

bool MyCodeEditorState::createAssignmentQueueAt(
    MyCodeEditor* editor,
    int cursorPosition,
    QString* message)
{
    if (!editor || !editor->document()) {
        if (message)
            *message = QStringLiteral("No editor document");
        return false;
    }
    const QStringList ordered =
        signalSelection.selectedNames();
    if (ordered.isEmpty()) {
        if (message)
            *message = QStringLiteral("No signals selected");
        cancelSignalSelectionMode(editor);
        return false;
    }

    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    QTextBlock block = editor->document()->findBlock(
        qBound(0, cursorPosition, documentEnd));
    if (!block.isValid()) {
        if (message)
            *message = QStringLiteral("Invalid insertion context");
        cancelSignalSelectionMode(editor);
        return false;
    }
    const QString blockText = block.text();
    int indentLength = 0;
    while (indentLength < blockText.size()
           && (blockText.at(indentLength)
                   == QLatin1Char(' ')
               || blockText.at(indentLength)
                   == QLatin1Char('\t'))) {
        ++indentLength;
    }
    const QString indent =
        blockText.left(indentLength);

    QString insertionText;
    CodeTemplateSlotList metadata;
    for (int index = 0; index < ordered.size(); ++index) {
        insertionText += indent;
        insertionText += ordered.at(index);
        insertionText += QStringLiteral(" <= ");
        CodeTemplateSlot slot;
        slot.name = QStringLiteral("rhs %1").arg(index + 1);
        slot.start = insertionText.size();
        slot.length = 0;
        metadata.append(slot);
        insertionText += QStringLiteral(";\n");
    }
    const int insertionStart = block.position();

    cancelSignalSelectionMode(editor);
    clearTemplateSlotMode(editor);
    QTextCursor insertion(editor->document());
    insertion.beginEditBlock();
    insertion.setPosition(insertionStart);
    insertion.insertText(insertionText);
    insertion.endEditBlock();
    startTemplateSlotMode(editor,
                          insertionStart,
                          insertionText.size(),
                          metadata);
    if (message) {
        *message = QStringLiteral(
            "Created assignment queue for %1 signals")
            .arg(ordered.size());
    }
    emit editor->editorStatusMessageRequested(
        QStringLiteral(
            "Created assignment queue for %1 signals")
            .arg(ordered.size()));
    return templateSlotModeActive();
}

EditorStructuralContextMenuState
MyCodeEditorState::structuralContextMenuState(
    const MyCodeEditor* editor,
    int cursorPosition) const
{
    EditorStructuralContextMenuState state;
    if (!editor)
        return state;

    QString signalFailure;
    const QString signalCandidate =
        signalDefinitionCandidateAt(
            editor, cursorPosition, &signalFailure);
    const TSInstantiationTarget target =
        syntax.instantiationAt(cursorPosition);
    const bool hasInstanceSlots =
        target.ok()
        && (!target.parameterActuals.isEmpty()
            || !target.portActuals.isEmpty());
    state.signalDefinitionAvailable =
        !signalCandidate.isEmpty();
    state.instanceSlotsAvailable = hasInstanceSlots;
    return state;
}

bool MyCodeEditorState::handleMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (signalSelection.handleMousePress(
            editor,
            event)) {
        return true;
    }

    if (templateSlotModeActive() && editor && event) {
        const QTextCursor targetCursor =
            editor->cursorForPosition(event->position().toPoint());
        if (!templateSlots.containsPosition(
                targetCursor.position())) {
            clearTemplateSlotMode(editor);
        }
    }

    if (folding.handleFoldShelfMousePress(editor, event))
        return true;

    if (columnMode.beginSelection(editor, event))
        return true;

    if (modes.isActive(EditorModeId::ColumnSelection)
        && event
        && event->button() == Qt::LeftButton
        && !(event->modifiers().testFlag(Qt::ShiftModifier)
             && event->modifiers().testFlag(Qt::AltModifier))) {
        columnMode.clearSelection(editor);
    }

    if (columnMode.handlePlainVirtualCursorClick(
            editor,
            event)) {
        return true;
    }
    if (modes.isActive(EditorModeId::VirtualCursor)
        && event
        && event->button() == Qt::LeftButton) {
        clearVirtualCursor(editor);
    }

    if (handleBracketRangeAltClick(editor, event))
        return true;

    const bool handled = sourceNavigation.handleMousePress(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
    sourceNavigation.syncMode();
    return handled;
}

bool MyCodeEditorState::handleMouseDoubleClick(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    const bool handled = sourceNavigation.handleMouseDoubleClick(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
    sourceNavigation.syncMode();
    return handled;
}

bool MyCodeEditorState::handleMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (signalSelection.handleMouseMove(
            editor,
            event)) {
        return true;
    }

    if (columnMode.updateSelectionDrag(editor, event))
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
        sourceNavigation.syncMode();
        return true;
    }
    sourceNavigation.syncMode();
    return false;
}

bool MyCodeEditorState::handleMouseRelease(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (signalSelection.handleMouseRelease(
            editor,
            event)) {
        return true;
    }

    if (sourceNavigation.handleMouseRelease(editor, event)) {
        sourceNavigation.syncMode();
        return true;
    }
    sourceNavigation.syncMode();

    return columnMode.endSelectionDrag(editor, event);
}

void MyCodeEditorState::handleLeaveEvent(MyCodeEditor* editor)
{
    sourceNavigation.handleLeave(editor, selections);
    sourceNavigation.syncMode();
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

QString MyCodeEditorState::materializeDocumentText(
    const MyCodeEditor* editor)
{
    ++hotPathMetrics.fullTextMaterializations;
    return editor ? editor->QPlainTextEdit::toPlainText() : QString();
}

const QString& MyCodeEditorState::cachedDocumentText()
{
    if (inlineFilterTextOverlayActive)
        ++hotPathMetrics.inlineFilterOverlayForcedTextReads;
    finishInlineFilterTextOverlay();
    return semanticRevisionText;
}

int MyCodeEditorState::cachedDocumentLength() const
{
    if (!inlineFilterTextOverlayActive)
        return semanticRevisionText.size();
    return semanticRevisionText.size()
        - inlineFilterTextOverlayOriginalLength
        + inlineFilterTextOverlayCurrentText.size();
}

QString MyCodeEditorState::cachedDocumentSlice(int position, int length)
{
    const int documentLength = cachedDocumentLength();
    const int boundedPosition = qBound(0, position, documentLength);
    const int boundedLength = qBound(0,
                                     length,
                                     documentLength - boundedPosition);
    ++hotPathMetrics.cachedTextSliceReads;
    hotPathMetrics.cachedTextSliceCharacters +=
        static_cast<std::uint64_t>(boundedLength);
    if (!inlineFilterTextOverlayActive)
        return semanticRevisionText.mid(boundedPosition, boundedLength);

    const int requestEnd = boundedPosition + boundedLength;
    const int overlayStart = inlineFilterTextOverlayStart;
    const int overlayEnd =
        overlayStart + inlineFilterTextOverlayCurrentText.size();
    QString result;
    result.reserve(boundedLength);
    int cursor = boundedPosition;
    if (cursor < overlayStart) {
        const int beforeEnd = qMin(requestEnd, overlayStart);
        result += semanticRevisionText.mid(
            cursor, beforeEnd - cursor);
        cursor = beforeEnd;
    }
    if (cursor < requestEnd && cursor < overlayEnd) {
        const int currentStart = qMax(cursor, overlayStart);
        const int currentEnd = qMin(requestEnd, overlayEnd);
        result += inlineFilterTextOverlayCurrentText.mid(
            currentStart - overlayStart,
            currentEnd - currentStart);
        cursor = currentEnd;
    }
    if (cursor < requestEnd) {
        const int baseStart =
            cursor
            - inlineFilterTextOverlayCurrentText.size()
            + inlineFilterTextOverlayOriginalLength;
        result += semanticRevisionText.mid(
            baseStart, requestEnd - cursor);
    }
    return result;
}

bool MyCodeEditorState::beginInlineFilterTextOverlay(
    int startPosition,
    int endPosition)
{
    if (inlineFilterTextOverlayActive) {
        return startPosition == inlineFilterTextOverlayStart
            && endPosition
                   == inlineFilterTextOverlayStart
                       + inlineFilterTextOverlayCurrentText.size();
    }
    if (!syntax.usesLargeFileScopedSyntax()
        || startPosition < 0
        || endPosition < startPosition
        || endPosition > semanticRevisionText.size()) {
        return false;
    }

    inlineFilterTextOverlayActive = true;
    inlineFilterTextOverlayStart = startPosition;
    inlineFilterTextOverlayOriginalLength =
        endPosition - startPosition;
    inlineFilterTextOverlayOriginalText =
        semanticRevisionText.mid(
            startPosition,
            inlineFilterTextOverlayOriginalLength);
    inlineFilterTextOverlayCurrentText =
        inlineFilterTextOverlayOriginalText;
    ++hotPathMetrics.inlineFilterOverlaySessions;
    return true;
}

void MyCodeEditorState::finishInlineFilterTextOverlay()
{
    if (!inlineFilterTextOverlayActive)
        return;
    if (inlineFilterTextOverlayCurrentText
        != inlineFilterTextOverlayOriginalText) {
        semanticRevisionText.replace(
            inlineFilterTextOverlayStart,
            inlineFilterTextOverlayOriginalLength,
            inlineFilterTextOverlayCurrentText);
        ++hotPathMetrics.inlineFilterOverlayMaterializations;
    }
    inlineFilterTextOverlayActive = false;
    inlineFilterTextOverlayStart = -1;
    inlineFilterTextOverlayOriginalLength = 0;
    inlineFilterTextOverlayOriginalText.clear();
    inlineFilterTextOverlayCurrentText.clear();
}

void MyCodeEditorState::acceptLoadedTextAsSemanticBaseline(
    const MyCodeEditor* editor)
{
    Q_UNUSED(editor)
    semanticTextRevision = 0;
}

EditorHotPathMetrics MyCodeEditorState::hotPathMetricsForTest() const
{
    return hotPathMetrics;
}
bool MyCodeEditorState::inlineFilterTextOverlayActiveForTest() const
{
    return inlineFilterTextOverlayActive;
}


EditorOccurrenceIndexStats
MyCodeEditorState::occurrenceIndexStatsForTest() const
{
    return selections.occurrenceIndexStatsForTest();
}

QList<int> MyCodeEditorState::occurrencePositionsForTest(
    const QString& word) const
{
    return selections.occurrencePositionsForTest(word);
}

void MyCodeEditorState::resetHotPathMetricsForTest()
{
    hotPathMetrics = {};
    hotPathTimingEnabled = true;
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

QList<GhostAnnotation> MyCodeEditorState::ghostAnnotationsForTest() const
{
    return ghostAnnotations;
}

QString MyCodeEditorState::syntaxTextForTest() const
{
    const TSDocument* document = syntax.tsDocument();
    return document ? document->text() : QString();
}
EditorLargeFileSyntaxScopeSnapshot
MyCodeEditorState::largeFileSyntaxScopeForTest() const
{
    return syntax.largeFileScopeSnapshotForTest();
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
    modes.exit(EditorModeId::InlineCandidates,
               EditorModeExitReason::Canceled);
    modes.exit(EditorModeId::CompletionCandidates,
               EditorModeExitReason::Canceled);
    if (!modes.exit(EditorModeId::SourceNavigation,
                    EditorModeExitReason::Canceled)) {
        sourceNavigation.closeForEditor(editor, selections);
    }
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

    modes.exitAll(EditorModeExitReason::FileIdentityChanged);
    cancelSignalDefinitionEditor();
    diagnosticComputationRevision = 0;
    clearDiagnosticHighlights(editor);
    refreshGhostAnnotations(editor);
    emit editor->fileNameChanged(identity.current());
}

QString MyCodeEditorState::documentFileName() const
{
    return identity.current();
}

void MyCodeEditorState::setDiagnosticHighlights(
    MyCodeEditor* editor,
    const QList<SemanticDiagnostic>& incomingDiagnostics)
{
    if (!editor)
        return;

    std::uint64_t incomingComputationRevision = 0;
    for (const SemanticDiagnostic& diagnostic : incomingDiagnostics) {
        incomingComputationRevision =
            std::max(incomingComputationRevision,
                     diagnostic.computationRevision);
    }
    if (incomingComputationRevision != 0
        && incomingComputationRevision
            < diagnosticComputationRevision) {
        return;
    }
    if (incomingComputationRevision != 0) {
        diagnosticComputationRevision =
            incomingComputationRevision;
    }

    QList<SemanticDiagnostic> currentDiagnostics;
    currentDiagnostics.reserve(incomingDiagnostics.size());
    for (const SemanticDiagnostic& diagnostic : incomingDiagnostics) {
        if (diagnostic.documentRevision != 0
            && diagnostic.documentRevision
                != semanticDocumentRevision()) {
            continue;
        }
        currentDiagnostics.append(diagnostic);
    }

    if (currentDiagnostics.isEmpty()
        && diagnostics.isEmpty()
        && editor->property(kDiagnosticsEmptyProperty).toBool()) {
        return;
    }

    diagnostics = std::move(currentDiagnostics);
    diagnosticIndexesByLine.clear();
    diagnosticSeverityByLine.clear();
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    for (int index = 0; index < diagnostics.size(); ++index) {
        const SemanticDiagnostic& diagnostic = diagnostics.at(index);
        QSet<int> lines;
        for (const SemanticSourceRange& range : diagnostic.ranges) {
            if (range.position < 0 || range.length <= 0)
                continue;
            const int start = qBound(0, range.position, documentEnd);
            const int finalCharacter = qBound(
                start,
                range.position + range.length - 1,
                documentEnd);
            const QTextBlock startBlock =
                editor->document()->findBlock(start);
            const QTextBlock endBlock =
                editor->document()->findBlock(finalCharacter);
            if (!startBlock.isValid() || !endBlock.isValid())
                continue;
            for (int line = startBlock.blockNumber();
                 line <= endBlock.blockNumber();
                 ++line) {
                lines.insert(line);
            }
        }
        if (lines.isEmpty() && diagnostic.line > 0)
            lines.insert(diagnostic.line - 1);

        for (const int line : std::as_const(lines)) {
            diagnosticIndexesByLine[line].append(index);
            const auto existing =
                diagnosticSeverityByLine.constFind(line);
            if (existing == diagnosticSeverityByLine.constEnd()
                || diagnosticSeverityRank(diagnostic.severity)
                    > diagnosticSeverityRank(existing.value())) {
                diagnosticSeverityByLine.insert(
                    line, diagnostic.severity);
            }
        }
    }

    selections.highlightDiagnostics(editor, diagnostics);
    editor->setProperty(kDiagnosticsEmptyProperty, diagnostics.isEmpty());
    gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
    editor->viewport()->update();
}

void MyCodeEditorState::clearDiagnosticHighlights(MyCodeEditor* editor)
{
    if (!editor)
        return;
    diagnostics.clear();
    diagnosticIndexesByLine.clear();
    diagnosticSeverityByLine.clear();
    selections.highlightDiagnostics(editor, {});
    editor->setProperty(kDiagnosticsEmptyProperty, true);
    gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
    editor->viewport()->update();
}

QString MyCodeEditorState::diagnosticTooltipForLine(
    int zeroBasedLine) const
{
    const QList<int> indexes =
        diagnosticIndexesByLine.value(zeroBasedLine);
    QList<const SemanticDiagnostic*> ordered;
    ordered.reserve(indexes.size());
    for (const int index : indexes) {
        if (index >= 0 && index < diagnostics.size())
            ordered.append(&diagnostics.at(index));
    }
    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const SemanticDiagnostic* left,
           const SemanticDiagnostic* right) {
            return diagnosticSeverityRank(left->severity)
                > diagnosticSeverityRank(right->severity);
        });

    QStringList rows;
    rows.reserve(ordered.size());
    for (const SemanticDiagnostic* diagnostic : std::as_const(ordered)) {
        rows.append(
            QStringLiteral("%1: %2")
                .arg(diagnosticSeverityLabel(diagnostic->severity),
                     diagnostic->message));
    }
    return rows.join(QLatin1Char('\n'));
}

QList<int> MyCodeEditorState::diagnosticOverviewLinesForTest() const
{
    QList<int> lines = diagnosticSeverityByLine.keys();
    std::sort(lines.begin(), lines.end());
    return lines;
}

SemanticDiagnostic::Severity
MyCodeEditorState::diagnosticSeverityForLineForTest(
    int zeroBasedLine,
    bool* available) const
{
    const auto found =
        diagnosticSeverityByLine.constFind(zeroBasedLine);
    if (available)
        *available = found != diagnosticSeverityByLine.constEnd();
    return found == diagnosticSeverityByLine.constEnd()
        ? SemanticDiagnostic::Info
        : found.value();
}

void MyCodeEditorState::setSemanticDecorations(
    MyCodeEditor* editor,
    const QList<SemanticDecoration>& decorations)
{
    if (!editor)
        return;
    if (decorations.isEmpty()
        && semanticDecorations.isEmpty()
        && editor->property(kSemanticDecorationsEmptyProperty).toBool()) {
        return;
    }
    semanticDecorations = decorations;
    refreshSemanticDecorationPresentation(editor);
}

void MyCodeEditorState::refreshGhostAnnotations(MyCodeEditor* editor)
{
    ++ghostQueryGeneration;
    if (ghostQueryCancellation)
        ghostQueryCancellation->store(true);
    if (!editor || identity.current().isEmpty()) {
        setGhostAnnotations(editor, {});
        return;
    }

    GhostAnnotationQuery query;
    query.fileName = identity.current();
    query.documentText = editor->cachedDocumentText();
    query.instanceContext = hierarchyInstance;
    query.documentRevision = semanticDocumentRevision();
    ++hotPathMetrics.fullGhostQueries;

    const std::uint64_t generation = ghostQueryGeneration;
    const std::shared_ptr<std::atomic_bool> cancellation =
        std::make_shared<std::atomic_bool>(false);
    ghostQueryCancellation = cancellation;
    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        SemanticIndex::getInstance()->snapshot();
    const std::shared_ptr<const EffectiveValueService::DocumentSnapshot>
        valueSnapshot = EffectiveValueService::getInstance()
                            ->snapshotForDocument(query.fileName);

    auto* watcher = new QFutureWatcher<GhostAnnotationReport>(editor);
    QObject::connect(
        watcher,
        &QFutureWatcher<GhostAnnotationReport>::finished,
        editor,
        [this, editor, watcher, generation, cancellation, query]() {
            const GhostAnnotationReport report = watcher->result();
            watcher->deleteLater();
            if (cancellation->load()
                || generation != ghostQueryGeneration
                || identity.current() != query.fileName
                || semanticDocumentRevision() != query.documentRevision) {
                return;
            }
            setGhostAnnotations(editor, report.annotations);
        });
    watcher->setFuture(QtConcurrent::run(
        [query, snapshot, valueSnapshot, cancellation]() {
            GhostAnnotationReport report;
            if (cancellation->load() || !snapshot)
                return report;
            SemanticIndex localIndex;
            localIndex.setSnapshot(snapshot);
            EffectiveValueService localValues(&localIndex, valueSnapshot);
            GhostAnnotationService service(&localIndex, &localValues);
            report = service.annotationsForDocument(query);
            if (cancellation->load())
                report.annotations.clear();
            return report;
        }));
}

void MyCodeEditorState::setGhostAnnotations(
    MyCodeEditor* editor,
    const QList<GhostAnnotation>& annotations)
{
    ghostAnnotations = annotations;
    ghostPresentationPending = false;
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
