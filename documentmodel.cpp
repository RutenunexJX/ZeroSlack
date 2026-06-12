#include "documentmodel.h"

#include "mycodeeditor.h"

#include <QPlainTextEdit>

DocumentModel::DocumentModel(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<DocumentSnapshot>("DocumentSnapshot");
}

DocumentModel::~DocumentModel() = default;

void DocumentModel::registerEditor(MyCodeEditor* editor, const QString& fileName)
{
    if (!editor || registry.contains(editor))
        return;

    if (!fileName.isEmpty())
        editor->setDocumentFileName(fileName);

    TrackedDocument tracked = snapshotReader.capture(editor);
    registry.add(editor, tracked);

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
    const TrackedDocument* tracked = registry.find(editor);
    if (!tracked)
        return;

    const DocumentSnapshot previous = tracked->snapshot;
    DocumentSnapshot snapshot = refreshTrackedDocument(editor);
    registry.markEdited(editor, previous, &snapshot);
    emit documentEdited(snapshot);
}

void DocumentModel::handleEditorCursorChanged(MyCodeEditor* editor)
{
    if (!registry.contains(editor))
        return;

    emit cursorChanged(refreshTrackedDocument(editor));
}

void DocumentModel::handleEditorFileNameChanged(MyCodeEditor* editor)
{
    refreshTrackedDocument(editor);
}

void DocumentModel::unregisterEditor(MyCodeEditor* editor)
{
    if (!editor || !registry.contains(editor))
        return;

    const TrackedDocument tracked = registry.take(editor);
    emit documentClosed(tracked.snapshot.documentId, tracked.snapshot.fileName);
}

void DocumentModel::setDocumentFileName(MyCodeEditor* editor, const QString& fileName)
{
    if (!editor)
        return;

    if (!registry.contains(editor)) {
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
    if (!registry.contains(editor)) {
        registerEditor(editor);
        return;
    }

    const DocumentSnapshot previous = registry.value(editor).snapshot;
    TrackedDocument tracked = snapshotReader.capture(editor, &previous);
    registry.markSaved(editor, &tracked);
    emit documentSaved(tracked.snapshot);
}

void DocumentModel::refreshEditorState(MyCodeEditor* editor)
{
    if (!editor || !registry.contains(editor))
        return;

    refreshTrackedDocument(editor);
}

QList<DocumentSnapshot> DocumentModel::openDocuments() const
{
    return registry.snapshots();
}

DocumentSnapshot DocumentModel::documentForEditor(MyCodeEditor* editor) const
{
    return registry.snapshotForEditor(editor);
}

DocumentSnapshot DocumentModel::documentForFile(const QString& fileName) const
{
    const QString normalized = snapshotReader.normalizedFileName(fileName);
    return registry.snapshotForFile(normalized);
}

MyCodeEditor* DocumentModel::editorForFile(const QString& fileName) const
{
    return registry.editorForFile(snapshotReader.normalizedFileName(fileName));
}

QString DocumentModel::documentText(const QString& documentId) const
{
    return registry.textForDocumentId(documentId);
}

QString DocumentModel::documentTextForFile(const QString& fileName) const
{
    const QString normalized = snapshotReader.normalizedFileName(fileName);
    return registry.textForFile(normalized);
}

QString DocumentModel::documentTextForEditor(MyCodeEditor* editor) const
{
    return registry.textForEditor(editor);
}

DocumentSnapshot DocumentModel::refreshTrackedDocument(MyCodeEditor* editor)
{
    if (!editor || !registry.contains(editor))
        return DocumentSnapshot();

    TrackedDocument tracked = registry.value(editor);
    const DocumentSnapshot previous = tracked.snapshot;
    tracked = snapshotReader.capture(editor, &previous);
    return registry.replace(editor, tracked, previous);
}
