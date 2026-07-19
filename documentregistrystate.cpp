#include "documentregistry.h"

bool DocumentStore::markEdited(
    MyCodeEditor* editor,
    const DocumentSnapshot& previous,
    DocumentSnapshot* snapshot)
{
    if (!snapshot)
        return false;

    // DocumentSnapshotReader has already copied the authoritative source-text
    // revision into this snapshot. A real edit must retain the preceding saved
    // token until the document is saved again.
    snapshot->savedTextVersion = previous.savedTextVersion;
    snapshot->dirty = true;
    snapshot->saved = false;
    if (!updateSnapshot(editor, *snapshot))
        return false;
    if (TrackedDocument* tracked = find(editor))
        tracked->contentChangePending = false;
    return true;
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
    tracked->contentChangePending = false;
    insert(editor, *tracked);
    return true;
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
