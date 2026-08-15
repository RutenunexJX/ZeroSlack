#include "editorstructuralinputcontroller.h"

#include "editorsyntaxstate.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

#include <QKeyEvent>
#include <QTextCursor>
#include <QTextDocument>

namespace {
bool hasCommandModifier(const QKeyEvent* event)
{
    if (!event)
        return false;
    const Qt::KeyboardModifiers modifiers =
        event->modifiers();
    return modifiers.testFlag(Qt::ControlModifier)
        || modifiers.testFlag(Qt::AltModifier)
        || modifiers.testFlag(Qt::MetaModifier);
}

QChar matchingCloser(QChar opening)
{
    if (opening == QLatin1Char('('))
        return QLatin1Char(')');
    if (opening == QLatin1Char('['))
        return QLatin1Char(']');
    if (opening == QLatin1Char('{'))
        return QLatin1Char('}');
    if (opening == QLatin1Char('"'))
        return QLatin1Char('"');
    return {};
}

bool closingCharacter(QChar value)
{
    return value == QLatin1Char(')')
        || value == QLatin1Char(']')
        || value == QLatin1Char('}')
        || value == QLatin1Char('"');
}

bool syntaxLiteralAt(const TSDocument* document,
                     int position)
{
    if (!document || position < 0)
        return false;
    return document->isCommentAt(position)
        || document->isStringAt(position);
}
}

bool EditorStructuralInputController::handleKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event,
    const EditorSyntaxState& syntax) const
{
    return handleStructuralEnter(editor, event, syntax)
        || handlePairInput(editor, event, syntax);
}

bool EditorStructuralInputController::handleStructuralEnter(
    MyCodeEditor* editor,
    QKeyEvent* event,
    const EditorSyntaxState& syntax) const
{
    if (!editor
        || !event
        || hasCommandModifier(event)
        || (event->key() != Qt::Key_Return
            && event->key() != Qt::Key_Enter)) {
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection())
        return false;
    const int originalPosition = cursor.position();
    const TSStructuralNewlineTarget target =
        syntax.structuralNewlineTargetAt(
            originalPosition, 4);
    if (!target.ok())
        return false;

    int insertionStart = originalPosition;
    const QTextBlock block = cursor.block();
    if (block.isValid()) {
        const int column = qMax(
            0, originalPosition - block.position());
        const QString blockText = block.text();
        int leadingWhitespace = 0;
        while (leadingWhitespace < blockText.size()
               && (blockText.at(leadingWhitespace)
                       == QLatin1Char(' ')
                   || blockText.at(leadingWhitespace)
                          == QLatin1Char('\t'))) {
            ++leadingWhitespace;
        }
        if (column <= leadingWhitespace) {
            insertionStart = block.position();
            cursor.setPosition(insertionStart);
            cursor.setPosition(originalPosition,
                               QTextCursor::KeepAnchor);
        }
    }

    cursor.beginEditBlock();
    cursor.insertText(target.insertionText);
    cursor.setPosition(
        insertionStart + target.caretOffset);
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

bool EditorStructuralInputController::handlePairInput(
    MyCodeEditor* editor,
    QKeyEvent* event,
    const EditorSyntaxState& syntax) const
{
    if (!editor
        || !event
        || hasCommandModifier(event)
        || event->text().size() != 1) {
        return false;
    }

    const QChar typed = event->text().at(0);
    const QChar closer = matchingCloser(typed);
    if (closer.isNull() && !closingCharacter(typed))
        return false;

    QTextCursor cursor = editor->textCursor();
    QTextDocument* textDocument = editor->document();
    if (!textDocument)
        return false;
    const int documentEnd =
        qMax(0, textDocument->characterCount() - 1);
    const int position =
        qBound(0, cursor.position(), documentEnd);
    const TSDocument* syntaxDocument =
        syntax.tsDocument();

    if (!cursor.hasSelection()
        && closingCharacter(typed)
        && position < documentEnd
        && textDocument->characterAt(position) == typed) {
        cursor.movePosition(QTextCursor::Right);
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }

    if (closer.isNull())
        return false;
    if (!cursor.hasSelection()) {
        const int probe =
            position > 0 ? position - 1 : position;
        if (syntaxLiteralAt(syntaxDocument, probe))
            return false;
    }

    QString selected;
    if (cursor.hasSelection()) {
        selected = cursor.selectedText();
        selected.replace(QChar::ParagraphSeparator,
                         QLatin1Char('\n'));
    }
    cursor.beginEditBlock();
    cursor.insertText(QString(typed)
                      + selected
                      + QString(closer));
    if (selected.isEmpty())
        cursor.movePosition(QTextCursor::Left);
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}
