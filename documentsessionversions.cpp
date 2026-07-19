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

    const TrackedDocument previous = registry.value(editor);
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

    const TrackedDocument previous = *tracked;
    DocumentSnapshot snapshot = refreshTrackedDocument(editor);
    const TrackedDocument* refreshed = registry.find(editor);
    if (!refreshed || !refreshed->contentChangePending)
        return false;
    if (!registry.markEdited(editor, previous.snapshot, &snapshot))
        return false;

    if (editedSnapshot)
        *editedSnapshot = snapshot;
    return true;
}
