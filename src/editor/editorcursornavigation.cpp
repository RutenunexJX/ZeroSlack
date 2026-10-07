#include "editorcursornavigation.h"

#include "mycodeeditor.h"
#include "sourcenavigationservice.h"

#include <QCursor>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QWidget>

void EditorCursorNavigation::moveMouseToCursor(MyCodeEditor* editor) const
{
    if (QGuiApplication::mouseButtons() != Qt::NoButton)
        return;

    if (editor->viewport() && editor->viewport()->isVisible()) {
        QCursor::setPos(
            editor->viewport()->mapToGlobal(
                editor->cursorRect().center()));
    }
}

void EditorCursorNavigation::applyLineTarget(
    MyCodeEditor* editor,
    const SourceLineNavigationTarget& target) const
{
    const QTextBlock block = editor->document()->findBlockByNumber(
        qBound(0, target.lineMoves, editor->document()->blockCount() - 1));
    if (!block.isValid())
        return;
    // Source coordinates use logical lines and UTF-16 offsets, whereas Down
    // follows visual rows and Right follows grapheme boundaries.
    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + qBound(0, target.columnMoves, block.length() - 1));
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
    moveMouseToCursor(editor);
}
