#include "documentmodel.h"

#include "documentsessionstate.h"
#include "mycodeeditor.h"

#include <QPlainTextEdit>

void DocumentModel::connectEditorSignals(MyCodeEditor* editor)
{
    if (!editor || editorSignalConnections.contains(editor))
        return;

    QList<QMetaObject::Connection> connections;
    connections.append(connect(
        editor,
        &QObject::destroyed,
        this,
        [this, editor]() {
            unregisterEditor(editor);
        }));
    connections.append(connect(
        editor,
        &MyCodeEditor::documentChangeApplied,
        this,
        [this, editor](const DocumentChange& change) {
            handleEditorDocumentChange(editor, change);
        }));
    connections.append(connect(
        editor,
        &QPlainTextEdit::cursorPositionChanged,
        this,
        [this, editor]() {
            handleEditorCursorChanged(editor);
        }));
    connections.append(connect(
        editor,
        &MyCodeEditor::fileNameChanged,
        this,
        [this, editor]() {
            handleEditorFileNameChanged(editor);
        }));
    editorSignalConnections.insert(editor, connections);
}

void DocumentModel::disconnectEditorSignals(MyCodeEditor* editor)
{
    const QList<QMetaObject::Connection> connections =
        editorSignalConnections.take(editor);
    for (const QMetaObject::Connection& connection : connections)
        disconnect(connection);
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
