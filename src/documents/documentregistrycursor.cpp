#include "documentregistry.h"

#include "mycodeeditor.h"

#include <QTextBlock>
#include <QTextCursor>

void DocumentSnapshotReader::captureCursorState(
    MyCodeEditor* editor,
    DocumentSnapshot* snapshot) const
{
    if (!editor || !snapshot)
        return;

    const QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    snapshot->cursorPosition = cursor.position();
    snapshot->cursorLine = block.isValid() ? block.blockNumber() + 1 : 1;
    snapshot->cursorColumn = block.isValid()
        ? cursor.position() - block.position() + 1
        : 1;
    snapshot->currentModuleName = editor->currentModuleName();
}
