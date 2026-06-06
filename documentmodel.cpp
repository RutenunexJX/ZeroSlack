#include "documentmodel.h"

#include "mycodeeditor.h"

#include <QDir>
#include <QFileInfo>
#include <QTextBlock>
#include <QTextCursor>

DocumentModel::DocumentModel(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<DocumentSnapshot>("DocumentSnapshot");
}

DocumentModel::~DocumentModel() = default;

void DocumentModel::registerEditor(MyCodeEditor* editor)
{
    if (!editor || documentsByEditor.contains(editor))
        return;

    TrackedDocument tracked;
    tracked.editor = editor;
    tracked.snapshot = makeSnapshot(editor);
    documentsByEditor.insert(editor, tracked);
    indexDocument(editor, tracked.snapshot);

    connect(editor, &QObject::destroyed, this, [this, editor]() {
        unregisterEditor(editor);
    });
    connect(editor, &QPlainTextEdit::textChanged, this, [this, editor]() {
        if (!documentsByEditor.contains(editor))
            return;

        TrackedDocument tracked = documentsByEditor.value(editor);
        const DocumentSnapshot previous = tracked.snapshot;
        tracked.snapshot = makeSnapshot(editor, &previous);
        tracked.snapshot.textVersion = previous.textVersion + 1;
        documentsByEditor[editor] = tracked;
        removeIndexes(editor, previous);
        indexDocument(editor, tracked.snapshot);
        emit documentEdited(tracked.snapshot);
    });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
        if (!documentsByEditor.contains(editor))
            return;

        TrackedDocument tracked = documentsByEditor.value(editor);
        const DocumentSnapshot previous = tracked.snapshot;
        tracked.snapshot = makeSnapshot(editor, &previous);
        documentsByEditor[editor] = tracked;
        removeIndexes(editor, previous);
        indexDocument(editor, tracked.snapshot);
        emit cursorChanged(tracked.snapshot);
    });

    emit documentOpened(tracked.snapshot);
}

void DocumentModel::unregisterEditor(MyCodeEditor* editor)
{
    if (!editor || !documentsByEditor.contains(editor))
        return;

    const TrackedDocument tracked = documentsByEditor.take(editor);
    removeIndexes(editor, tracked.snapshot);
    emit documentClosed(tracked.snapshot.documentId, tracked.snapshot.fileName);
}

void DocumentModel::markSaved(MyCodeEditor* editor)
{
    if (!editor)
        return;
    if (!documentsByEditor.contains(editor)) {
        registerEditor(editor);
        return;
    }

    TrackedDocument tracked = documentsByEditor.value(editor);
    const DocumentSnapshot previous = tracked.snapshot;
    tracked.snapshot = makeSnapshot(editor, &previous);
    tracked.snapshot.dirty = false;
    tracked.snapshot.saved = true;
    documentsByEditor[editor] = tracked;
    removeIndexes(editor, previous);
    indexDocument(editor, tracked.snapshot);
    emit documentSaved(tracked.snapshot);
}

void DocumentModel::refreshEditorState(MyCodeEditor* editor)
{
    if (!editor || !documentsByEditor.contains(editor))
        return;

    TrackedDocument tracked = documentsByEditor.value(editor);
    const DocumentSnapshot previous = tracked.snapshot;
    tracked.snapshot = makeSnapshot(editor, &previous);
    documentsByEditor[editor] = tracked;
    removeIndexes(editor, previous);
    indexDocument(editor, tracked.snapshot);
}

QList<DocumentSnapshot> DocumentModel::openDocuments() const
{
    QList<DocumentSnapshot> result;
    result.reserve(documentsByEditor.size());
    for (const TrackedDocument& tracked : documentsByEditor)
        result.append(tracked.snapshot);
    return result;
}

DocumentSnapshot DocumentModel::documentForEditor(MyCodeEditor* editor) const
{
    return documentsByEditor.value(editor).snapshot;
}

DocumentSnapshot DocumentModel::documentForFile(const QString& fileName) const
{
    const QString normalized = normalizedFileName(fileName);
    MyCodeEditor* editor = editorByFileName.value(normalized, nullptr);
    return editor ? documentsByEditor.value(editor).snapshot : DocumentSnapshot();
}

QString DocumentModel::documentText(const QString& documentId) const
{
    MyCodeEditor* editor = editorByDocumentId.value(documentId, nullptr);
    return editor ? editor->toPlainText() : QString();
}

QString DocumentModel::documentIdForEditor(MyCodeEditor* editor) const
{
    if (!editor)
        return QString();

    const QString fileName = normalizedFileName(editor->getFileName());
    if (!fileName.isEmpty())
        return fileName;

    return QStringLiteral("untitled:%1")
        .arg(QString::number(reinterpret_cast<quintptr>(editor), 16));
}

QString DocumentModel::normalizedFileName(const QString& fileName) const
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

DocumentSnapshot DocumentModel::makeSnapshot(MyCodeEditor* editor,
                                             const DocumentSnapshot* previous) const
{
    DocumentSnapshot snapshot;
    if (previous)
        snapshot = *previous;

    snapshot.documentId = documentIdForEditor(editor);
    snapshot.fileName = normalizedFileName(editor->getFileName());
    snapshot.dirty = !editor->checkSaved();
    snapshot.saved = editor->checkSaved();

    const QTextCursor cursor = editor->textCursor();
    snapshot.cursorPosition = cursor.position();
    const QTextBlock block = cursor.block();
    snapshot.cursorLine = block.isValid() ? block.blockNumber() + 1 : 1;
    snapshot.cursorColumn = block.isValid() ? cursor.position() - block.position() + 1 : 1;
    snapshot.currentModuleName = editor->currentModuleName();

    return snapshot;
}

void DocumentModel::indexDocument(MyCodeEditor* editor, const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    editorByDocumentId[snapshot.documentId] = editor;
    if (!snapshot.fileName.isEmpty())
        editorByFileName[snapshot.fileName] = editor;
}

void DocumentModel::removeIndexes(MyCodeEditor* editor, const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    if (editorByDocumentId.value(snapshot.documentId, nullptr) == editor)
        editorByDocumentId.remove(snapshot.documentId);
    if (!snapshot.fileName.isEmpty() && editorByFileName.value(snapshot.fileName, nullptr) == editor)
        editorByFileName.remove(snapshot.fileName);
}
