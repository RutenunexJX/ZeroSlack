#include "documentmodel.h"

#include "documentsessionstate.h"

DocumentModel::DocumentModel(QObject* parent)
    : QObject(parent)
    , state(std::make_unique<DocumentSessionState>())
{
    qRegisterMetaType<DocumentSnapshot>("DocumentSnapshot");
}

DocumentModel::~DocumentModel() = default;

void DocumentModel::registerEditor(MyCodeEditor* editor, const QString& fileName)
{
    DocumentSnapshot snapshot;
    if (state->registerEditor(editor, fileName, &snapshot)) {
        connectEditorSignals(editor);
        emit documentOpened(snapshot);
    }
}

void DocumentModel::unregisterEditor(MyCodeEditor* editor)
{
    const DocumentCloseResult result = state->unregisterEditor(editor);
    if (result.closed)
        emit documentClosed(result.documentId, result.fileName);
}

void DocumentModel::setDocumentFileName(MyCodeEditor* editor, const QString& fileName)
{
    const DocumentFileNameResult result =
        state->setDocumentFileName(editor, fileName);
    if (result.opened) {
        connectEditorSignals(editor);
        emit documentOpened(result.snapshot);
    }
}

void DocumentModel::markSaved(MyCodeEditor* editor)
{
    const DocumentSaveResult result = state->markSaved(editor);
    if (result.opened) {
        connectEditorSignals(editor);
        emit documentOpened(result.snapshot);
    }
    if (result.saved)
        emit documentSaved(result.snapshot);
}

void DocumentModel::refreshEditorState(MyCodeEditor* editor)
{
    state->refreshEditorState(editor);
}

QList<DocumentSnapshot> DocumentModel::openDocuments() const
{
    const QList<DocumentSnapshot> snapshots = state->openDocuments();
    for (const DocumentSnapshot& snapshot : snapshots) {
        if (MyCodeEditor* editor = state->editorForFile(snapshot.fileName))
            state->refreshEditorState(editor);
    }
    return state->openDocuments();
}

DocumentSnapshot DocumentModel::documentForEditor(MyCodeEditor* editor) const
{
    state->refreshEditorState(editor);
    return state->documentForEditor(editor);
}

DocumentSnapshot DocumentModel::documentForFile(const QString& fileName) const
{
    if (MyCodeEditor* editor = state->editorForFile(fileName))
        state->refreshEditorState(editor);
    return state->documentForFile(fileName);
}

MyCodeEditor* DocumentModel::editorForFile(const QString& fileName) const
{
    return state->editorForFile(fileName);
}

QString DocumentModel::documentText(const QString& documentId) const
{
    return state->documentText(documentId);
}

QString DocumentModel::documentTextForFile(const QString& fileName) const
{
    return state->documentTextForFile(fileName);
}

QString DocumentModel::documentTextForEditor(MyCodeEditor* editor) const
{
    return state->documentTextForEditor(editor);
}
