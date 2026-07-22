#include "documentsessionstate.h"

QList<DocumentSnapshot> DocumentSessionState::openDocuments() const
{
    return registry.snapshots();
}

DocumentSnapshot DocumentSessionState::documentForEditor(
    MyCodeEditor* editor) const
{
    return registry.snapshotForEditor(editor);
}

DocumentSnapshot DocumentSessionState::documentForFile(
    const QString& fileName) const
{
    const QString normalized = snapshotReader.normalizedFileName(fileName);
    return registry.snapshotForFile(normalized);
}

DocumentSnapshot DocumentSessionState::documentMetadataForEditor(
    MyCodeEditor* editor) const
{
    return registry.metadataForEditor(editor);
}

MyCodeEditor* DocumentSessionState::editorForFile(
    const QString& fileName) const
{
    return registry.editorForFile(
        snapshotReader.normalizedFileName(fileName));
}

QString DocumentSessionState::documentText(const QString& documentId) const
{
    return registry.textForDocumentId(documentId);
}

QString DocumentSessionState::documentTextForFile(
    const QString& fileName) const
{
    const QString normalized = snapshotReader.normalizedFileName(fileName);
    return registry.textForFile(normalized);
}

QString DocumentSessionState::documentTextForEditor(
    MyCodeEditor* editor) const
{
    return registry.textForEditor(editor);
}
