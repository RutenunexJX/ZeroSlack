#include "documentsessionstate.h"

#include "mycodeeditor.h"

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
