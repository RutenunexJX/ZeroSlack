#include "editorstructuralinputcontroller.h"

#include "editorsyntaxstate.h"
#include "editorlexicalboundary.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

#include <QKeyEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextCharFormat>

namespace {
// A nonvisual document property follows QTextDocument's own undo/redo history.
// It is never serialized in RTL source or applied to user-authored terminators.
constexpr int kGeneratedEndProperty = QTextFormat::UserProperty + 0x5356;

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

bool identifierCharacter(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('$');
}

int indentLength(const QString& text)
{
    int length = 0;
    while (length < text.size()
           && (text.at(length) == QLatin1Char(' ') || text.at(length) == QLatin1Char('\t')))
        ++length;
    return length;
}
}

bool EditorStructuralInputController::handleKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event,
    const EditorSyntaxState& syntax)
{
    if (!editor || !editor->canApplyInsertion())
        return false;
    return handleStructuralEnter(editor, event, syntax)
        || handleKeywordBoundary(editor, event, syntax)
        || handlePairInput(editor, event, syntax);
}

bool EditorStructuralInputController::alignClosingKeyword(
    MyCodeEditor* editor, QTextCursor* cursor, const EditorSyntaxState& syntax)
{
    if (!editor || !cursor || cursor->hasSelection() || !syntax.tsDocument())
        return false;
    const QTextBlock block = cursor->block();
    const QString text = block.text();
    const int column = cursor->position() - block.position();
    const int whitespace = indentLength(text);
    const QString keyword = text.mid(whitespace, column - whitespace);
    if (keyword != QLatin1String("end") && keyword != QLatin1String("endcase")
        && keyword != QLatin1String("endmodule") && keyword != QLatin1String("else"))
        return false;
    if (column < text.size() && identifierCharacter(text.at(column)))
        return false;
    QString indent;
    if (!syntax.tsDocument()->structuralClosingIndent(
            block.position() + whitespace, keyword, &indent))
        return false;

    // Only a marked generated keyword can be reused. The marker is restored by
    // undo together with its text, including when the user edits after undo.
    int duplicateEnd = -1;
    if (keyword == QLatin1String("end")) {
        const int documentEnd = editor->document()->characterCount() - 1;
        int next = cursor->position();
        while (next < documentEnd && editor->document()->characterAt(next).isSpace())
            ++next;
        if (next + 3 <= documentEnd) {
            QTextCursor generated(editor->document());
            generated.setPosition(next);
            generated.setPosition(next + 3, QTextCursor::KeepAnchor);
            bool marked = generated.selectedText() == QLatin1String("end")
                && (next + 3 == documentEnd
                    || !identifierCharacter(editor->document()->characterAt(next + 3)));
            generated.setPosition(next + 1);
            generated.setPosition(next + 2, QTextCursor::KeepAnchor);
            marked = marked && generated.charFormat().boolProperty(kGeneratedEndProperty);
            if (marked)
                duplicateEnd = next + 3;
        }
    }
    if (indent == text.left(whitespace) && duplicateEnd < 0)
        return false;
    cursor->beginEditBlock();
    if (duplicateEnd >= 0) {
        QTextCursor duplicate = *cursor;
        duplicate.setPosition(duplicateEnd, QTextCursor::KeepAnchor);
        duplicate.removeSelectedText();
    }
    cursor->setPosition(block.position());
    cursor->setPosition(block.position() + whitespace, QTextCursor::KeepAnchor);
    cursor->insertText(indent);
    cursor->setPosition(block.position() + indent.size() + keyword.size());
    cursor->endEditBlock();
    return true;
}

bool EditorStructuralInputController::handleKeywordBoundary(
    MyCodeEditor* editor, QKeyEvent* event, const EditorSyntaxState& syntax)
{
    if (!editor || !event || hasCommandModifier(event) || event->text().size() != 1)
        return false;
    const QChar ch = event->text().at(0);
    if (!ch.isPrint() || identifierCharacter(ch))
        return false;
    QTextCursor cursor = editor->textCursor();
    if (!alignClosingKeyword(editor, &cursor, syntax))
        return false;
    cursor.joinPreviousEditBlock();
    cursor.insertText(event->text());
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

bool EditorStructuralInputController::handleStructuralEnter(
    MyCodeEditor* editor,
    QKeyEvent* event,
    const EditorSyntaxState& syntax)
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
    const bool aligned = alignClosingKeyword(editor, &cursor, syntax);
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

    if (aligned)
        cursor.joinPreviousEditBlock();
    else
        cursor.beginEditBlock();
    cursor.insertText(target.insertionText);
    cursor.setPosition(
        insertionStart + target.caretOffset);
    if (target.insertedClosingKeyword) {
        QTextCursor generated(editor->document());
        const int end = insertionStart + target.insertionText.size();
        // Mark only the interior character so typing after "end" cannot inherit
        // the marker and accidentally turn user text into a generated keyword.
        generated.setPosition(end - 2);
        generated.setPosition(end - 1, QTextCursor::KeepAnchor);
        QTextCharFormat format = generated.charFormat();
        format.setProperty(kGeneratedEndProperty, true);
        generated.setCharFormat(format);
    }
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

bool EditorStructuralInputController::handlePairInput(
    MyCodeEditor* editor,
    QKeyEvent* event,
    const EditorSyntaxState& syntax)
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

    if (!cursor.hasSelection() && closer.isNull() && syntaxDocument
        && cursor.position() - cursor.block().position()
               == indentLength(cursor.block().text())) {
        QString indent;
        if (syntaxDocument->structuralClosingIndent(position, QString(typed), &indent)) {
            const int blockStart = cursor.block().position();
            cursor.beginEditBlock();
            cursor.setPosition(blockStart);
            cursor.setPosition(position, QTextCursor::KeepAnchor);
            cursor.insertText(indent);
            if (!skipTrackedCloser(editor, &cursor, typed))
                cursor.insertText(QString(typed));
            cursor.endEditBlock();
            editor->setTextCursor(cursor);
            event->accept();
            return true;
        }
    }

    if (!cursor.hasSelection()
        && closingCharacter(typed)
        && skipTrackedCloser(editor, &cursor, typed)) {
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

        if (typed == QLatin1Char('(')) {
            const TSIdentifierTarget identifier =
                syntax.identifierAt(position);
            if (identifier.ok()
                && position == identifier.startChar) {
                cursor.insertText(QString(typed));
                editor->setTextCursor(cursor);
                event->accept();
                return true;
            }
            if (identifier.ok()
                && position > identifier.startChar
                && position < identifier.endChar) {
                const TSExpressionAtomTarget atom =
                    syntax.expressionAtomAt(position);
                int start = atom.ok()
                    ? atom.startChar : identifier.startChar;
                int end = atom.ok()
                    ? atom.endChar : identifier.endChar;
                if (start < 0 || end <= start) {
                    const EditorLexicalBoundary::Range lexical =
                        EditorLexicalBoundary::identifierAt(
                            editor->cachedDocumentText(), position);
                    start = lexical.start;
                    end = lexical.end;
                }
                cursor.setPosition(start);
                cursor.setPosition(end,
                                   QTextCursor::KeepAnchor);
                const QString text = cursor.selectedText();
                cursor.beginEditBlock();
                cursor.insertText(QString(typed)
                                  + text
                                  + QString(closer));
                cursor.endEditBlock();
                editor->setTextCursor(cursor);
                event->accept();
                return true;
            }
        }
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
    if (selected.isEmpty()) {
        cursor.movePosition(QTextCursor::Left);
        trackCloser(textDocument, cursor.position(), closer);
    }
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

void EditorStructuralInputController::pruneTrackedClosers()
{
    for (auto it = trackedClosers.begin(); it != trackedClosers.end();) {
        QTextDocument* document = it->document.data();
        const int position = it->cursor.position();
        const int documentEnd = document
            ? qMax(0, document->characterCount() - 1)
            : 0;
        if (!document
            || position < 0
            || position >= documentEnd
            || it->endCursor.position() != position + 1
            || document->characterAt(position) != it->value) {
            it = trackedClosers.erase(it);
        } else {
            ++it;
        }
    }
}

bool EditorStructuralInputController::skipTrackedCloser(
    MyCodeEditor* editor,
    QTextCursor* cursor,
    QChar typed)
{
    if (!editor || !cursor || !editor->document())
        return false;
    pruneTrackedClosers();
    for (auto it = trackedClosers.begin(); it != trackedClosers.end(); ++it) {
        if (it->document != editor->document()
            || it->cursor.position() != cursor->position()
            || it->value != typed) {
            continue;
        }
        // Skipping moves the caret; it does not remove the generated character.
        // Keep its cursor so undo/redo of indentation can move it back with the
        // text. Only prune a record when its document/character is no longer valid.
        cursor->movePosition(QTextCursor::Right);
        editor->setTextCursor(*cursor);
        return true;
    }
    return false;
}

void EditorStructuralInputController::trackCloser(
    QTextDocument* document,
    int position,
    QChar value)
{
    if (!document || value.isNull())
        return;
    pruneTrackedClosers();
    QTextCursor cursor(document);
    cursor.setPosition(qBound(0,
                              position,
                              qMax(0, document->characterCount() - 1)));
    cursor.setKeepPositionOnInsert(false);
    // Opposite insertion affinities delimit the actual generated character:
    // edits on either side move the range, but deletion/replacement collapses
    // or reverses it instead of transferring provenance to a neighboring closer.
    QTextCursor endCursor(document);
    endCursor.setPosition(qMin(cursor.position() + 1,
                              qMax(0, document->characterCount() - 1)));
    endCursor.setKeepPositionOnInsert(true);
    trackedClosers.append({document, cursor, endCursor, value});
}
