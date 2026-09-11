#include "documentregistry.h"

#include "editorfileidentity.h"
#include "mycodeeditor.h"

#include <QSet>

#include <algorithm>

QList<DocumentSnapshot> DocumentRegistry::snapshots() const
{
    QList<DocumentSnapshot> result;
    QSet<QString> seen;
    for (auto iterator = documents.byEditor.constBegin();
         iterator != documents.byEditor.constEnd();
         ++iterator) {
        const QString key =
            EditorFileIdentity::lookupKey(
                iterator.value().snapshot.documentId);
        if (key.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        result.append(snapshotForEditor(iterator.key()));
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const DocumentSnapshot& left,
           const DocumentSnapshot& right) {
            return left.documentId < right.documentId;
        });
    return result;
}

DocumentSnapshot DocumentRegistry::snapshotForEditor(MyCodeEditor* editor) const
{
    const TrackedDocument* tracked = documents.find(editor);
    if (!tracked)
        return DocumentSnapshot();
    DocumentSnapshot snapshot = tracked->snapshot;
    if (tracked->editor)
        snapshot.text = tracked->editor->cachedDocumentText();
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
