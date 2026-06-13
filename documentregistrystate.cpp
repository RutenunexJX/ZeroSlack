#include "documentregistry.h"

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
