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

void DocumentModel::DocumentIndexes::add(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    byDocumentId[snapshot.documentId] = editor;
    if (!snapshot.fileName.isEmpty())
        byFileName[snapshot.fileName] = editor;
}

void DocumentModel::DocumentIndexes::remove(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    if (byDocumentId.value(snapshot.documentId, nullptr) == editor)
        byDocumentId.remove(snapshot.documentId);
    if (!snapshot.fileName.isEmpty()
        && byFileName.value(snapshot.fileName, nullptr) == editor) {
        byFileName.remove(snapshot.fileName);
    }
}

MyCodeEditor* DocumentModel::DocumentIndexes::editorForDocumentId(
    const QString& documentId) const
{
    return byDocumentId.value(documentId, nullptr);
}

MyCodeEditor* DocumentModel::DocumentIndexes::editorForFileName(
    const QString& fileName) const
{
    return byFileName.value(fileName, nullptr);
}

bool DocumentModel::DocumentStore::contains(MyCodeEditor* editor) const
{
    return byEditor.contains(editor);
}

void DocumentModel::DocumentStore::insert(
    MyCodeEditor* editor,
    const TrackedDocument& tracked)
{
    byEditor.insert(editor, tracked);
}

DocumentModel::TrackedDocument DocumentModel::DocumentStore::take(
    MyCodeEditor* editor)
{
    return byEditor.take(editor);
}

DocumentModel::TrackedDocument DocumentModel::DocumentStore::value(
    MyCodeEditor* editor) const
{
    return byEditor.value(editor);
}

DocumentModel::TrackedDocument* DocumentModel::DocumentStore::find(
    MyCodeEditor* editor)
{
    auto it = byEditor.find(editor);
    return it == byEditor.end() ? nullptr : &it.value();
}

const DocumentModel::TrackedDocument* DocumentModel::DocumentStore::find(
    MyCodeEditor* editor) const
{
    auto it = byEditor.constFind(editor);
    return it == byEditor.constEnd() ? nullptr : &it.value();
}

bool DocumentModel::DocumentStore::updateSnapshot(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    TrackedDocument* tracked = find(editor);
    if (!tracked)
        return false;

    tracked->snapshot = snapshot;
    return true;
}

bool DocumentModel::DocumentStore::markEdited(
    MyCodeEditor* editor,
    const DocumentSnapshot& previous,
    DocumentSnapshot* snapshot)
{
    if (!snapshot)
        return false;

    snapshot->textVersion = previous.textVersion + 1;
    snapshot->dirty = true;
    snapshot->saved = false;
    return updateSnapshot(editor, *snapshot);
}

bool DocumentModel::DocumentStore::markSaved(
    MyCodeEditor* editor,
    TrackedDocument* tracked)
{
    if (!tracked)
        return false;

    tracked->snapshot.dirty = false;
    tracked->snapshot.saved = true;
    tracked->snapshot.savedTextVersion = tracked->snapshot.textVersion;
    insert(editor, *tracked);
    return true;
}

QList<DocumentSnapshot> DocumentModel::DocumentStore::snapshots() const
{
    QList<DocumentSnapshot> result;
    result.reserve(byEditor.size());
    for (const TrackedDocument& tracked : byEditor)
        result.append(tracked.snapshot);
    return result;
}

QString DocumentModel::DocumentStore::textForEditor(MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = find(editor);
    return tracked ? tracked->text : QString();
}

bool DocumentModel::DocumentRegistry::contains(MyCodeEditor* editor) const
{
    return documents.contains(editor);
}

void DocumentModel::DocumentRegistry::add(
    MyCodeEditor* editor,
    const TrackedDocument& tracked)
{
    documents.insert(editor, tracked);
    indexes.add(editor, tracked.snapshot);
}

DocumentModel::TrackedDocument DocumentModel::DocumentRegistry::take(
    MyCodeEditor* editor)
{
    const TrackedDocument tracked = documents.take(editor);
    indexes.remove(editor, tracked.snapshot);
    return tracked;
}

DocumentModel::TrackedDocument DocumentModel::DocumentRegistry::value(
    MyCodeEditor* editor) const
{
    return documents.value(editor);
}

DocumentModel::TrackedDocument* DocumentModel::DocumentRegistry::find(
    MyCodeEditor* editor)
{
    return documents.find(editor);
}

const DocumentModel::TrackedDocument* DocumentModel::DocumentRegistry::find(
    MyCodeEditor* editor) const
{
    return documents.find(editor);
}

bool DocumentModel::DocumentRegistry::markEdited(
    MyCodeEditor* editor,
    const DocumentSnapshot& previous,
    DocumentSnapshot* snapshot)
{
    return documents.markEdited(editor, previous, snapshot);
}

bool DocumentModel::DocumentRegistry::markSaved(
    MyCodeEditor* editor,
    TrackedDocument* tracked)
{
    return documents.markSaved(editor, tracked);
}

DocumentSnapshot DocumentModel::DocumentRegistry::replace(
    MyCodeEditor* editor,
    const TrackedDocument& tracked,
    const DocumentSnapshot& previous)
{
    documents.insert(editor, tracked);
    indexes.remove(editor, previous);
    indexes.add(editor, tracked.snapshot);
    return tracked.snapshot;
}

QList<DocumentSnapshot> DocumentModel::DocumentRegistry::snapshots() const
{
    return documents.snapshots();
}

DocumentSnapshot DocumentModel::DocumentRegistry::snapshotForEditor(
    MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = documents.find(editor);
    return tracked ? tracked->snapshot : DocumentSnapshot();
}

DocumentSnapshot DocumentModel::DocumentRegistry::snapshotForFile(
    const QString& fileName) const
{
    MyCodeEditor* editor = indexes.editorForFileName(fileName);
    return snapshotForEditor(editor);
}

MyCodeEditor* DocumentModel::DocumentRegistry::editorForFile(
    const QString& fileName) const
{
    return indexes.editorForFileName(fileName);
}

QString DocumentModel::DocumentRegistry::textForDocumentId(
    const QString& documentId) const
{
    MyCodeEditor* editor = indexes.editorForDocumentId(documentId);
    return documents.textForEditor(editor);
}

QString DocumentModel::DocumentRegistry::textForEditor(
    MyCodeEditor* editor) const
{
    return documents.textForEditor(editor);
}

QString DocumentModel::DocumentSnapshotReader::normalizedFileName(
    const QString& fileName) const
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString DocumentModel::DocumentSnapshotReader::documentIdForEditor(
    MyCodeEditor* editor) const
{
    if (!editor)
        return QString();

    const QString fileName = normalizedFileName(editor->getFileName());
    if (!fileName.isEmpty())
        return fileName;

    return QStringLiteral("untitled:%1")
        .arg(QString::number(reinterpret_cast<quintptr>(editor), 16));
}

DocumentModel::TrackedDocument DocumentModel::DocumentSnapshotReader::capture(
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

void DocumentModel::registerEditor(MyCodeEditor* editor, const QString& fileName)
{
    if (!editor || registry.contains(editor))
        return;

    if (!fileName.isEmpty())
        editor->setFileName(fileName);

    TrackedDocument tracked;
    tracked = makeTrackedDocument(editor);
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

    editor->setFileName(fileName);
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
    TrackedDocument tracked = makeTrackedDocument(editor, &previous);
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
    const QString normalized = normalizedFileName(fileName);
    return registry.snapshotForFile(normalized);
}

MyCodeEditor* DocumentModel::editorForFile(const QString& fileName) const
{
    return registry.editorForFile(normalizedFileName(fileName));
}

QString DocumentModel::documentText(const QString& documentId) const
{
    return registry.textForDocumentId(documentId);
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
    return registry.textForEditor(editor);
}

QString DocumentModel::normalizedFileName(const QString& fileName) const
{
    return snapshotReader.normalizedFileName(fileName);
}

DocumentModel::TrackedDocument DocumentModel::makeTrackedDocument(
    MyCodeEditor* editor,
    const DocumentSnapshot* previous) const
{
    return snapshotReader.capture(editor, previous);
}

DocumentSnapshot DocumentModel::refreshTrackedDocument(MyCodeEditor* editor)
{
    if (!editor || !registry.contains(editor))
        return DocumentSnapshot();

    TrackedDocument tracked = registry.value(editor);
    const DocumentSnapshot previous = tracked.snapshot;
    tracked = makeTrackedDocument(editor, &previous);
    return registry.replace(editor, tracked, previous);
}
