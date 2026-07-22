#include "documentmodel.h"

#include "documentsessionstate.h"
#include "mycodeeditor.h"

#include <QPlainTextEdit>

void DocumentModel::connectEditorSignals(MyCodeEditor* editor)
{
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        unregisterEditor(editor);
    });
    connect(editor,
            &MyCodeEditor::documentChangeApplied,
            this,
            [this, editor](const DocumentChange& change) {
                handleEditorDocumentChange(editor, change);
            });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
        handleEditorCursorChanged(editor);
    });
    connect(editor, &MyCodeEditor::fileNameChanged, this, [this, editor]() {
        handleEditorFileNameChanged(editor);
    });
}

void DocumentModel::handleEditorDocumentChange(
    MyCodeEditor* editor,
    const DocumentChange& change)
{
    DocumentSnapshot snapshot;
    if (state->applyChange(editor, change, &snapshot))
        emit documentEdited(snapshot);
}

void DocumentModel::handleEditorCursorChanged(MyCodeEditor* editor)
{
    DocumentSnapshot snapshot;
    state->refreshCursor(editor, &snapshot);
}

void DocumentModel::handleEditorFileNameChanged(MyCodeEditor* editor)
{
    state->refreshFileName(editor);
}
