#include "documentsessionstate.h"

#include "mycodeeditor.h"

#include <limits>

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
    const QList<MyCodeEditor*> views =
        registry.editorsForDocumentId(
            previous.snapshot.documentId);
    TrackedDocument tracked;
    for (MyCodeEditor* view : views) {
        const TrackedDocument viewPrevious =
            registry.value(view);
        TrackedDocument viewTracked =
            snapshotReader.capture(view, &viewPrevious);
        if (!registry.markSaved(view, &viewTracked))
            continue;
        if (view == editor)
            tracked = viewTracked;
    }
    if (tracked.snapshot.documentId.isEmpty())
        return result;

    result.saved = true;
    result.snapshot = tracked.snapshot;
    const QString& text = editor->cachedDocumentText();
    recordDocumentTextCopy(text.size());
    result.snapshot.text = QString(text.constData(), text.size());
    return result;
}

bool DocumentSessionState::applyChange(
    MyCodeEditor* editor,
    const DocumentChange& change,
    DocumentSnapshot* editedSnapshot)
{
    if (editedSnapshot)
        *editedSnapshot = DocumentSnapshot();

    TrackedDocument* tracked = registry.find(editor);
    if (!tracked || !editor || !change.changesText())
        return false;
    if (change.position < 0 || change.removedLength < 0
        || change.position + change.removedLength > change.oldLength
        || change.newLength
               != change.oldLength - change.removedLength
                      + change.insertedText.size()) {
        return false;
    }

    const int nextVersion = static_cast<int>(
        qMin<std::uint64_t>(change.revision,
                            static_cast<std::uint64_t>(
                                std::numeric_limits<int>::max())));
    if (tracked->snapshot.textVersion >= nextVersion
        && tracked->snapshot.dirty) {
        return false;
    }
    const QList<MyCodeEditor*> views =
        registry.editorsForDocumentId(
            tracked->snapshot.documentId);
    for (MyCodeEditor* view : views) {
        TrackedDocument* viewTracked = registry.find(view);
        if (!viewTracked)
            continue;
        viewTracked->snapshot.text.clear();
        viewTracked->snapshot.textVersion = nextVersion;
        viewTracked->snapshot.dirty = true;
        viewTracked->snapshot.saved = false;
    }
    if (editedSnapshot)
        *editedSnapshot = tracked->snapshot;
    return true;
}
