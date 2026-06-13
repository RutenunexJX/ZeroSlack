#include "documentsessionstate.h"

#include "mycodeeditor.h"

bool DocumentSessionState::contains(MyCodeEditor* editor) const
{
    return registry.contains(editor);
}

bool DocumentSessionState::registerEditor(
    MyCodeEditor* editor,
    const QString& fileName,
    DocumentSnapshot* openedSnapshot)
{
    if (openedSnapshot)
        *openedSnapshot = DocumentSnapshot();

    if (!editor || registry.contains(editor))
        return false;

    if (!fileName.isEmpty())
        editor->setDocumentFileName(fileName);

    TrackedDocument tracked = snapshotReader.capture(editor);
    registry.add(editor, tracked);
    if (openedSnapshot)
        *openedSnapshot = tracked.snapshot;
    return true;
}

DocumentCloseResult DocumentSessionState::unregisterEditor(
    MyCodeEditor* editor)
{
    DocumentCloseResult result;
    if (!editor || !registry.contains(editor))
        return result;

    const TrackedDocument tracked = registry.take(editor);
    result.closed = true;
    result.documentId = tracked.snapshot.documentId;
    result.fileName = tracked.snapshot.fileName;
    return result;
}

DocumentFileNameResult DocumentSessionState::setDocumentFileName(
    MyCodeEditor* editor,
    const QString& fileName)
{
    DocumentFileNameResult result;
    if (!editor)
        return result;

    if (!registry.contains(editor)) {
        result.opened = registerEditor(editor, fileName, &result.snapshot);
        return result;
    }

    editor->setDocumentFileName(fileName);
    result.snapshot = refreshTrackedDocument(editor);
    return result;
}

DocumentSaveResult DocumentSessionState::markSaved(MyCodeEditor* editor)
{
    DocumentSaveResult result;
    if (!editor)
        return result;

    if (!registry.contains(editor)) {
        result.opened = registerEditor(editor, QString(), &result.snapshot);
        return result;
    }

    const DocumentSnapshot previous = registry.value(editor).snapshot;
    TrackedDocument tracked = snapshotReader.capture(editor, &previous);
    if (!registry.markSaved(editor, &tracked))
        return result;

    result.saved = true;
    result.snapshot = tracked.snapshot;
    return result;
}

void DocumentSessionState::refreshEditorState(MyCodeEditor* editor)
{
    if (!editor || !registry.contains(editor))
        return;

    refreshTrackedDocument(editor);
}

bool DocumentSessionState::markEdited(
    MyCodeEditor* editor,
    DocumentSnapshot* editedSnapshot)
{
    if (editedSnapshot)
        *editedSnapshot = DocumentSnapshot();

    const TrackedDocument* tracked = registry.find(editor);
    if (!tracked)
        return false;

    const DocumentSnapshot previous = tracked->snapshot;
    DocumentSnapshot snapshot = refreshTrackedDocument(editor);
    if (!registry.markEdited(editor, previous, &snapshot))
        return false;

    if (editedSnapshot)
        *editedSnapshot = snapshot;
    return true;
}

bool DocumentSessionState::refreshCursor(
    MyCodeEditor* editor,
    DocumentSnapshot* snapshot)
{
    if (snapshot)
        *snapshot = DocumentSnapshot();

    if (!registry.contains(editor))
        return false;

    const DocumentSnapshot refreshed = refreshTrackedDocument(editor);
    if (snapshot)
        *snapshot = refreshed;
    return true;
}

void DocumentSessionState::refreshFileName(MyCodeEditor* editor)
{
    refreshTrackedDocument(editor);
}

DocumentSnapshot DocumentSessionState::refreshTrackedDocument(
    MyCodeEditor* editor)
{
    if (!editor || !registry.contains(editor))
        return DocumentSnapshot();

    TrackedDocument tracked = registry.value(editor);
    const DocumentSnapshot previous = tracked.snapshot;
    tracked = snapshotReader.capture(editor, &previous);
    return registry.replace(editor, tracked, previous);
}
