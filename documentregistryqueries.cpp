#include "documentregistry.h"

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
