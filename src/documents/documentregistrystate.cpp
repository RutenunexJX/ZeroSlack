#include "documentregistry.h"

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

bool DocumentRegistry::markSaved(
    MyCodeEditor* editor,
    TrackedDocument* tracked)
{
    return documents.markSaved(editor, tracked);
}
