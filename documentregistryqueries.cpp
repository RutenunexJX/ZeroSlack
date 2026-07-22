#include "documentregistry.h"

#include "mycodeeditor.h"

QList<DocumentSnapshot> DocumentRegistry::snapshots() const
{
    return documents.snapshots();
}

DocumentSnapshot DocumentRegistry::snapshotForEditor(MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = documents.find(editor);
    if (!tracked)
        return DocumentSnapshot();
    DocumentSnapshot snapshot = tracked->snapshot;
    if (tracked->editor) {
        const QString& text = tracked->editor->cachedDocumentText();
        recordDocumentTextCopy(text.size());
        snapshot.text = QString(text.constData(), text.size());
    }
    return snapshot;
}

DocumentSnapshot DocumentRegistry::metadataForEditor(
    MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = documents.find(editor);
    if (!tracked)
        return DocumentSnapshot();
    DocumentSnapshot metadata = tracked->snapshot;
    metadata.text.clear();
    return metadata;
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
