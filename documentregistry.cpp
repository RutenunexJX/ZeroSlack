#include "documentregistry.h"

#include "mycodeeditor.h"

#include <QDir>
#include <QFileInfo>
#include <QTextBlock>
#include <QTextCursor>

void DocumentIndexes::add(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    if (!editor)
        return;

    byDocumentId[snapshot.documentId] = editor;
    if (!snapshot.fileName.isEmpty())
        byFileName[snapshot.fileName] = editor;
}

void DocumentIndexes::remove(
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

MyCodeEditor* DocumentIndexes::editorForDocumentId(
    const QString& documentId) const
{
    return byDocumentId.value(documentId, nullptr);
}

MyCodeEditor* DocumentIndexes::editorForFileName(
    const QString& fileName) const
{
    return byFileName.value(fileName, nullptr);
}

bool DocumentStore::contains(MyCodeEditor* editor) const
{
    return byEditor.contains(editor);
}

void DocumentStore::insert(
    MyCodeEditor* editor,
    const TrackedDocument& tracked)
{
    byEditor.insert(editor, tracked);
}

TrackedDocument DocumentStore::take(MyCodeEditor* editor)
{
    return byEditor.take(editor);
}

TrackedDocument DocumentStore::value(MyCodeEditor* editor) const
{
    return byEditor.value(editor);
}

TrackedDocument* DocumentStore::find(MyCodeEditor* editor)
{
    auto it = byEditor.find(editor);
    return it == byEditor.end() ? nullptr : &it.value();
}

const TrackedDocument* DocumentStore::find(MyCodeEditor* editor) const
{
    auto it = byEditor.constFind(editor);
    return it == byEditor.constEnd() ? nullptr : &it.value();
}

bool DocumentStore::updateSnapshot(
    MyCodeEditor* editor,
    const DocumentSnapshot& snapshot)
{
    TrackedDocument* tracked = find(editor);
    if (!tracked)
        return false;

    tracked->snapshot = snapshot;
    return true;
}

bool DocumentStore::markEdited(
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

bool DocumentStore::markSaved(
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

QList<DocumentSnapshot> DocumentStore::snapshots() const
{
    QList<DocumentSnapshot> result;
    result.reserve(byEditor.size());
    for (const TrackedDocument& tracked : byEditor)
        result.append(tracked.snapshot);
    return result;
}

QString DocumentStore::textForEditor(MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = find(editor);
    return tracked ? tracked->text : QString();
}

bool DocumentRegistry::contains(MyCodeEditor* editor) const
{
    return documents.contains(editor);
}

void DocumentRegistry::add(
    MyCodeEditor* editor,
    const TrackedDocument& tracked)
{
    documents.insert(editor, tracked);
    indexes.add(editor, tracked.snapshot);
}

TrackedDocument DocumentRegistry::take(MyCodeEditor* editor)
{
    const TrackedDocument tracked = documents.take(editor);
    indexes.remove(editor, tracked.snapshot);
    return tracked;
}

TrackedDocument DocumentRegistry::value(MyCodeEditor* editor) const
{
    return documents.value(editor);
}

TrackedDocument* DocumentRegistry::find(MyCodeEditor* editor)
{
    return documents.find(editor);
}

const TrackedDocument* DocumentRegistry::find(MyCodeEditor* editor) const
{
    return documents.find(editor);
}

bool DocumentRegistry::markEdited(
    MyCodeEditor* editor,
    const DocumentSnapshot& previous,
    DocumentSnapshot* snapshot)
{
    return documents.markEdited(editor, previous, snapshot);
}

bool DocumentRegistry::markSaved(
    MyCodeEditor* editor,
    TrackedDocument* tracked)
{
    return documents.markSaved(editor, tracked);
}

DocumentSnapshot DocumentRegistry::replace(
    MyCodeEditor* editor,
    const TrackedDocument& tracked,
    const DocumentSnapshot& previous)
{
    documents.insert(editor, tracked);
    indexes.remove(editor, previous);
    indexes.add(editor, tracked.snapshot);
    return tracked.snapshot;
}

QList<DocumentSnapshot> DocumentRegistry::snapshots() const
{
    return documents.snapshots();
}

DocumentSnapshot DocumentRegistry::snapshotForEditor(MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = documents.find(editor);
    return tracked ? tracked->snapshot : DocumentSnapshot();
}

DocumentSnapshot DocumentRegistry::snapshotForFile(
    const QString& fileName) const
{
    MyCodeEditor* editor = indexes.editorForFileName(fileName);
    return snapshotForEditor(editor);
}

MyCodeEditor* DocumentRegistry::editorForFile(const QString& fileName) const
{
    return indexes.editorForFileName(fileName);
}

QString DocumentRegistry::textForDocumentId(const QString& documentId) const
{
    MyCodeEditor* editor = indexes.editorForDocumentId(documentId);
    return documents.textForEditor(editor);
}

QString DocumentRegistry::textForFile(const QString& fileName) const
{
    return documents.textForEditor(indexes.editorForFileName(fileName));
}

QString DocumentRegistry::textForEditor(MyCodeEditor* editor) const
{
    return documents.textForEditor(editor);
}

QString DocumentSnapshotReader::normalizedFileName(
    const QString& fileName) const
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString DocumentSnapshotReader::documentIdForEditor(MyCodeEditor* editor) const
{
    if (!editor)
        return QString();

    const QString fileName = normalizedFileName(editor->documentFileName());
    if (!fileName.isEmpty())
        return fileName;

    return QStringLiteral("untitled:%1")
        .arg(QString::number(reinterpret_cast<quintptr>(editor), 16));
}

TrackedDocument DocumentSnapshotReader::capture(
    MyCodeEditor* editor,
    const DocumentSnapshot* previous) const
{
    TrackedDocument tracked;
    tracked.editor = editor;
    if (!editor)
        return tracked;

    if (previous)
        tracked.snapshot = *previous;

    tracked.snapshot.fileName = normalizedFileName(editor->documentFileName());
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
