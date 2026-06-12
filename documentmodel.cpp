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
    tracked = makeTrackedDocument(editor);
    documentsByEditor.insert(editor, tracked);
    indexDocument(editor, tracked.snapshot);

    connect(editor, &QObject::destroyed, this, [this, editor]() {
        unregisterEditor(editor);
    });
    connect(editor, &QPlainTextEdit::textChanged, this, [this, editor]() {
        if (!documentsByEditor.contains(editor))
            return;

        const DocumentSnapshot previous = documentsByEditor.value(editor).snapshot;
        DocumentSnapshot snapshot = refreshTrackedDocument(editor);
        snapshot.textVersion = previous.textVersion + 1;
        snapshot.dirty = true;
        snapshot.saved = false;
        documentsByEditor[editor].snapshot = snapshot;
        emit documentEdited(snapshot);
    });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor]() {
        if (!documentsByEditor.contains(editor))
            return;

        emit cursorChanged(refreshTrackedDocument(editor));
    });
    connect(editor, &MyCodeEditor::fileNameChanged, this, [this, editor]() {
        refreshTrackedDocument(editor);
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

    const DocumentSnapshot previous = documentsByEditor.value(editor).snapshot;
    TrackedDocument tracked = makeTrackedDocument(editor, &previous);
    tracked.snapshot.dirty = false;
    tracked.snapshot.saved = true;
    tracked.snapshot.savedTextVersion = tracked.snapshot.textVersion;
    documentsByEditor[editor] = tracked;
    emit documentSaved(tracked.snapshot);
}

void DocumentModel::refreshEditorState(MyCodeEditor* editor)
{
    if (!editor || !documentsByEditor.contains(editor))
        return;

    refreshTrackedDocument(editor);
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
    if (!editor || !documentsByEditor.contains(editor))
        return DocumentSnapshot();
    return documentsByEditor.value(editor).snapshot;
}

DocumentSnapshot DocumentModel::documentForFile(const QString& fileName) const
{
    const QString normalized = normalizedFileName(fileName);
    MyCodeEditor* editor = editorByFileName.value(normalized, nullptr);
    return editor ? documentsByEditor.value(editor).snapshot : DocumentSnapshot();
}

MyCodeEditor* DocumentModel::editorForFile(const QString& fileName) const
{
    return editorByFileName.value(normalizedFileName(fileName), nullptr);
}

QString DocumentModel::documentText(const QString& documentId) const
{
    MyCodeEditor* editor = editorByDocumentId.value(documentId, nullptr);
    return editor && documentsByEditor.contains(editor)
        ? documentsByEditor.value(editor).text
        : QString();
}

QString DocumentModel::documentTextForFile(const QString& fileName) const
{
    const DocumentSnapshot snapshot = documentForFile(fileName);
    return snapshot.documentId.isEmpty()
        ? QString()
        : documentText(snapshot.documentId);
}

QString DocumentModel::documentTextForEditor(MyCodeEditor* editor) const
{
    if (!editor || !documentsByEditor.contains(editor))
        return QString();
    return documentsByEditor.value(editor).text;
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

DocumentModel::TrackedDocument DocumentModel::makeTrackedDocument(
    MyCodeEditor* editor,
    const DocumentSnapshot* previous) const
{
    TrackedDocument tracked;
    tracked.editor = editor;
    if (!editor)
        return tracked;

    if (previous)
        tracked.snapshot = *previous;

    tracked.snapshot.fileName = normalizedFileName(editor->getFileName());
    tracked.snapshot.documentId = tracked.snapshot.fileName.isEmpty()
        ? tracked.snapshot.documentId
        : tracked.snapshot.fileName;
    const QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    tracked.snapshot.cursorPosition = cursor.position();
    tracked.snapshot.cursorLine = block.isValid() ? block.blockNumber() + 1 : 1;
    tracked.snapshot.cursorColumn = block.isValid()
        ? cursor.position() - block.position() + 1
        : 1;
    tracked.snapshot.currentModuleName = editor->currentModuleName();

    if (tracked.snapshot.documentId.isEmpty())
        tracked.snapshot.documentId = documentIdForEditor(editor);
    tracked.text = editor->toPlainText();
    return tracked;
}

DocumentSnapshot DocumentModel::refreshTrackedDocument(MyCodeEditor* editor)
{
    if (!editor || !documentsByEditor.contains(editor))
        return DocumentSnapshot();

    TrackedDocument tracked = documentsByEditor.value(editor);
    const DocumentSnapshot previous = tracked.snapshot;
    tracked = makeTrackedDocument(editor, &previous);
    documentsByEditor[editor] = tracked;
    removeIndexes(editor, previous);
    indexDocument(editor, tracked.snapshot);
    return tracked.snapshot;
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
