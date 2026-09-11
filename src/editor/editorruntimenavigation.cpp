#include "editorruntime.h"

#include "editorlexicalboundary.h"
#include "mycodeeditor.h"

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace {
Qt::KeyboardModifiers relevantModifiers(const QKeyEvent* event)
{
    return event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
}

struct LineBoundaries
{
    int contentStart = 0;
    int contentEnd = 0;
    int physicalStart = 0;
    int physicalEnd = 0;
};

LineBoundaries lineBoundaries(const QTextBlock& block)
{
    LineBoundaries result;
    if (!block.isValid())
        return result;

    const QString line = block.text();
    int contentStartColumn = 0;
    while (contentStartColumn < line.size()
           && (line.at(contentStartColumn) == QLatin1Char(' ')
               || line.at(contentStartColumn) == QLatin1Char('\t'))) {
        ++contentStartColumn;
    }
    int contentEndColumn = line.size();
    while (contentEndColumn > contentStartColumn
           && (line.at(contentEndColumn - 1) == QLatin1Char(' ')
               || line.at(contentEndColumn - 1) == QLatin1Char('\t'))) {
        --contentEndColumn;
    }

    result.physicalStart = block.position();
    result.physicalEnd = block.position() + line.size();
    result.contentStart = block.position() + contentStartColumn;
    result.contentEnd = block.position() + contentEndColumn;
    return result;
}
}

bool MyCodeEditorState::handleLexicalNavigationOrDeletion(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (!editor || !event)
        return false;
    const Qt::KeyboardModifiers modifiers = relevantModifiers(event);
    const bool extend = modifiers
        == (Qt::ControlModifier | Qt::ShiftModifier);
    const bool controlOnly = modifiers == Qt::ControlModifier;
    const bool horizontalMove = event->key() == Qt::Key_Left
        || event->key() == Qt::Key_Right;
    const bool deletion = event->key() == Qt::Key_Backspace
        || event->key() == Qt::Key_Delete;
    if ((!horizontalMove || (!controlOnly && !extend))
        && (!deletion || !controlOnly)) {
        return false;
    }

    const QString& text = editor->cachedDocumentText();
    QTextCursor cursor = editor->textCursor();
    if (horizontalMove) {
        const int target = event->key() == Qt::Key_Left
            ? EditorLexicalBoundary::moveLeft(text, cursor.position())
            : EditorLexicalBoundary::moveRight(text, cursor.position());
        if (extend) {
            const int anchor = cursor.anchor();
            cursor.setPosition(anchor);
            cursor.setPosition(target, QTextCursor::KeepAnchor);
        } else {
            cursor.setPosition(target);
        }
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }

    if (cursor.hasSelection()) {
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        cursor.endEditBlock();
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }
    const EditorLexicalBoundary::Range range =
        event->key() == Qt::Key_Backspace
        ? EditorLexicalBoundary::deleteBackward(text, cursor.position())
        : EditorLexicalBoundary::deleteForward(text, cursor.position());
    if (!range.isValid())
        return false;
    cursor.beginEditBlock();
    cursor.setPosition(range.start);
    cursor.setPosition(range.end, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

bool MyCodeEditorState::handleStructuralNavigation(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (!editor || !event)
        return false;
    const Qt::KeyboardModifiers modifiers = relevantModifiers(event);
    const bool extend = modifiers
        == (Qt::ControlModifier
            | Qt::AltModifier
            | Qt::ShiftModifier);
    if (modifiers != (Qt::ControlModifier | Qt::AltModifier)
        && !extend) {
        return false;
    }

    TSStructuralNavigationDirection direction;
    switch (event->key()) {
    case Qt::Key_Left:
        direction = TSStructuralNavigationDirection::PreviousField;
        break;
    case Qt::Key_Right:
        direction = TSStructuralNavigationDirection::NextField;
        break;
    case Qt::Key_Up:
        direction = TSStructuralNavigationDirection::PreviousItem;
        break;
    case Qt::Key_Down:
        direction = TSStructuralNavigationDirection::NextItem;
        break;
    default:
        return false;
    }

    syntax.flushPendingEdits();
    const QTextCursor current = editor->textCursor();
    const TSStructuralNavigationTarget target =
        syntax.structuralNavigationTargetAt(current.position(), direction);
    QTextCursor moved = current;
    if (target.ok()) {
        const bool backwards =
            direction == TSStructuralNavigationDirection::PreviousField
            || direction == TSStructuralNavigationDirection::PreviousItem;
        if (extend) {
            moved.setPosition(current.anchor());
            moved.setPosition(backwards ? target.startChar : target.endChar,
                              QTextCursor::KeepAnchor);
        } else {
            moved.setPosition(target.startChar);
        }
        editor->setTextCursor(moved);
        editor->ensureCursorVisible();
        editor->flashRange(target.startChar, target.endChar);
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Left
        || event->key() == Qt::Key_Right) {
        const QString& text = editor->cachedDocumentText();
        const int targetPosition = event->key() == Qt::Key_Left
            ? EditorLexicalBoundary::moveLeft(text, current.position())
            : EditorLexicalBoundary::moveRight(text, current.position());
        if (extend) {
            moved.setPosition(current.anchor());
            moved.setPosition(targetPosition, QTextCursor::KeepAnchor);
        } else {
            moved.setPosition(targetPosition);
        }
    } else {
        const QTextCursor::MoveOperation operation =
            event->key() == Qt::Key_Up ? QTextCursor::Up : QTextCursor::Down;
        moved.movePosition(operation,
                           extend ? QTextCursor::KeepAnchor
                                  : QTextCursor::MoveAnchor);
    }
    editor->setTextCursor(moved);
    editor->ensureCursorVisible();
    event->accept();
    return true;
}

bool MyCodeEditorState::handleLineBoundarySelection(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (!editor || !event
        || relevantModifiers(event) != Qt::AltModifier
        || (event->key() != Qt::Key_Left
            && event->key() != Qt::Key_Right)) {
        return false;
    }

    QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    const LineBoundaries boundaries = lineBoundaries(block);
    if (lineBoundarySelectionAnchor < boundaries.physicalStart
        || lineBoundarySelectionAnchor > boundaries.physicalEnd
        || cursor.anchor() != lineBoundarySelectionAnchor) {
        lineBoundarySelectionAnchor = cursor.position();
        lineBoundarySelectionDirection = 0;
        lineBoundarySelectionAtPhysicalEdge = false;
    }

    const int direction = event->key() == Qt::Key_Left ? -1 : 1;
    const int contentTarget = direction < 0
        ? boundaries.contentStart : boundaries.contentEnd;
    const int physicalTarget = direction < 0
        ? boundaries.physicalStart : boundaries.physicalEnd;
    const bool repeatDirection =
        lineBoundarySelectionDirection == direction
        && cursor.position() == contentTarget;
    const int target = repeatDirection ? physicalTarget : contentTarget;

    cursor.setPosition(lineBoundarySelectionAnchor);
    cursor.setPosition(target, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    lineBoundarySelectionDirection = direction;
    lineBoundarySelectionAtPhysicalEdge = target == physicalTarget;
    event->accept();
    return true;
}

bool MyCodeEditorState::handleMultiCursorLineBoundarySelection(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (!editor || !event || !multiCursor.active()
        || relevantModifiers(event) != Qt::AltModifier
        || (event->key() != Qt::Key_Left
            && event->key() != Qt::Key_Right)) {
        return false;
    }

    const EditorMultiCursorSnapshot snapshot = multiCursor.snapshot();
    bool anchorsRemainValid =
        multiLineBoundarySelectionAnchors.size() == snapshot.carets.size();
    if (anchorsRemainValid) {
        for (int index = 0; index < snapshot.carets.size(); ++index) {
            const EditorMultiCursorCaret& caret = snapshot.carets.at(index);
            const int anchor = multiLineBoundarySelectionAnchors.at(index);
            const QTextBlock block =
                editor->document()->findBlock(caret.position);
            if (!block.isValid() || caret.anchor != anchor
                || anchor < block.position()
                || anchor > block.position() + block.text().size()) {
                anchorsRemainValid = false;
                break;
            }
        }
    }
    if (!anchorsRemainValid) {
        multiLineBoundarySelectionAnchors.clear();
        multiLineBoundarySelectionAnchors.reserve(snapshot.carets.size());
        for (const EditorMultiCursorCaret& caret : snapshot.carets)
            multiLineBoundarySelectionAnchors.append(caret.position);
        multiLineBoundarySelectionDirection = 0;
    }

    const int direction = event->key() == Qt::Key_Left ? -1 : 1;
    QList<EditorMultiCursorCaret> movedCarets;
    movedCarets.reserve(snapshot.carets.size());
    for (int index = 0; index < snapshot.carets.size(); ++index) {
        const EditorMultiCursorCaret& caret = snapshot.carets.at(index);
        const QTextBlock block =
            editor->document()->findBlock(caret.position);
        const LineBoundaries boundaries = lineBoundaries(block);
        const int contentTarget = direction < 0
            ? boundaries.contentStart : boundaries.contentEnd;
        const int physicalTarget = direction < 0
            ? boundaries.physicalStart : boundaries.physicalEnd;
        const bool repeated =
            multiLineBoundarySelectionDirection == direction
            && caret.position == contentTarget;
        movedCarets.append(EditorMultiCursorCaret{
            multiLineBoundarySelectionAnchors.at(index),
            repeated ? physicalTarget : contentTarget,
            -1,
        });
    }
    multiCursor.setCarets(movedCarets, snapshot.primaryIndex);
    multiLineBoundarySelectionDirection = direction;
    event->accept();
    return true;
}
