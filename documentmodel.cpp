#include "documentmodel.h"

#include "documentsessionstate.h"
#include "mycodeeditor.h"

#include <QPlainTextEdit>

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

void DocumentModel::connectEditorSignals(MyCodeEditor* editor)
{
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        unregisterEditor(editor);
    });
    connect(editor, &QPlainTextEdit::textChanged, this, [this, editor]() {
        handleEditorTextChanged(editor);
    });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
        handleEditorCursorChanged(editor);
    });
    connect(editor, &MyCodeEditor::fileNameChanged, this, [this, editor]() {
        handleEditorFileNameChanged(editor);
    });
}

void DocumentModel::handleEditorTextChanged(MyCodeEditor* editor)
{
    DocumentSnapshot snapshot;
    if (state->markEdited(editor, &snapshot))
        emit documentEdited(snapshot);
}

void DocumentModel::handleEditorCursorChanged(MyCodeEditor* editor)
{
    DocumentSnapshot snapshot;
    if (state->refreshCursor(editor, &snapshot))
        emit cursorChanged(snapshot);
}

void DocumentModel::handleEditorFileNameChanged(MyCodeEditor* editor)
{
    state->refreshFileName(editor);
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
    return state->openDocuments();
}

DocumentSnapshot DocumentModel::documentForEditor(MyCodeEditor* editor) const
{
    return state->documentForEditor(editor);
}

DocumentSnapshot DocumentModel::documentForFile(const QString& fileName) const
{
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
