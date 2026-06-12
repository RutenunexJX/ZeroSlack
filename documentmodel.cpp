#include "documentmodel.h"

#include "documentregistry.h"
#include "mycodeeditor.h"

#include <QPlainTextEdit>

struct DocumentModelState {
    DocumentRegistry registry;
    DocumentSnapshotReader snapshotReader;
};

DocumentModel::DocumentModel(QObject* parent)
    : QObject(parent)
    , state(std::make_unique<DocumentModelState>())
{
    qRegisterMetaType<DocumentSnapshot>("DocumentSnapshot");
}

DocumentModel::~DocumentModel() = default;

void DocumentModel::registerEditor(MyCodeEditor* editor, const QString& fileName)
{
    if (!editor || state->registry.contains(editor))
        return;

    if (!fileName.isEmpty())
        editor->setDocumentFileName(fileName);

    TrackedDocument tracked = state->snapshotReader.capture(editor);
    state->registry.add(editor, tracked);

    connectEditorSignals(editor);
    emit documentOpened(tracked.snapshot);
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
    const TrackedDocument* tracked = state->registry.find(editor);
    if (!tracked)
        return;

    const DocumentSnapshot previous = tracked->snapshot;
    DocumentSnapshot snapshot = refreshTrackedDocument(editor);
    state->registry.markEdited(editor, previous, &snapshot);
    emit documentEdited(snapshot);
}

void DocumentModel::handleEditorCursorChanged(MyCodeEditor* editor)
{
    if (!state->registry.contains(editor))
        return;

    emit cursorChanged(refreshTrackedDocument(editor));
}

void DocumentModel::handleEditorFileNameChanged(MyCodeEditor* editor)
{
    refreshTrackedDocument(editor);
}

void DocumentModel::unregisterEditor(MyCodeEditor* editor)
{
    if (!editor || !state->registry.contains(editor))
        return;

    const TrackedDocument tracked = state->registry.take(editor);
    emit documentClosed(tracked.snapshot.documentId, tracked.snapshot.fileName);
}

void DocumentModel::setDocumentFileName(MyCodeEditor* editor, const QString& fileName)
{
    if (!editor)
        return;

    if (!state->registry.contains(editor)) {
        registerEditor(editor, fileName);
        return;
    }

    editor->setDocumentFileName(fileName);
    refreshTrackedDocument(editor);
}

void DocumentModel::markSaved(MyCodeEditor* editor)
{
    if (!editor)
        return;
    if (!state->registry.contains(editor)) {
        registerEditor(editor);
        return;
    }

    const DocumentSnapshot previous = state->registry.value(editor).snapshot;
    TrackedDocument tracked = state->snapshotReader.capture(editor, &previous);
    state->registry.markSaved(editor, &tracked);
    emit documentSaved(tracked.snapshot);
}

void DocumentModel::refreshEditorState(MyCodeEditor* editor)
{
    if (!editor || !state->registry.contains(editor))
        return;

    refreshTrackedDocument(editor);
}

QList<DocumentSnapshot> DocumentModel::openDocuments() const
{
    return state->registry.snapshots();
}

DocumentSnapshot DocumentModel::documentForEditor(MyCodeEditor* editor) const
{
    return state->registry.snapshotForEditor(editor);
}

DocumentSnapshot DocumentModel::documentForFile(const QString& fileName) const
{
    const QString normalized = state->snapshotReader.normalizedFileName(fileName);
    return state->registry.snapshotForFile(normalized);
}

MyCodeEditor* DocumentModel::editorForFile(const QString& fileName) const
{
    return state->registry.editorForFile(
        state->snapshotReader.normalizedFileName(fileName));
}

QString DocumentModel::documentText(const QString& documentId) const
{
    return state->registry.textForDocumentId(documentId);
}

QString DocumentModel::documentTextForFile(const QString& fileName) const
{
    const QString normalized = state->snapshotReader.normalizedFileName(fileName);
    return state->registry.textForFile(normalized);
}

QString DocumentModel::documentTextForEditor(MyCodeEditor* editor) const
{
    return state->registry.textForEditor(editor);
}

DocumentSnapshot DocumentModel::refreshTrackedDocument(MyCodeEditor* editor)
{
    if (!editor || !state->registry.contains(editor))
        return DocumentSnapshot();

    TrackedDocument tracked = state->registry.value(editor);
    const DocumentSnapshot previous = tracked.snapshot;
    tracked = state->snapshotReader.capture(editor, &previous);
    return state->registry.replace(editor, tracked, previous);
}
