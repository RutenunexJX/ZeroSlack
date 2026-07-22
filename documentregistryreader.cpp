#include "documentregistry.h"

#include "mycodeeditor.h"

#include <limits>

TrackedDocument DocumentSnapshotReader::capture(
    MyCodeEditor* editor,
    const TrackedDocument* previous) const
{
    TrackedDocument tracked;
    tracked.editor = editor;
    if (!editor)
        return tracked;

    if (previous)
        tracked.snapshot = previous->snapshot;

    captureFileIdentity(editor, &tracked.snapshot);
    captureCursorState(editor, &tracked.snapshot);

    // DocumentSnapshot::textVersion is shared by editor queries and semantic
    // publication. It must only follow source-text changes: QTextDocument's
    // own revision also advances for appearance / highlighting changes.
    tracked.snapshot.textVersion = static_cast<int>(
        qMin<std::uint64_t>(editor->semanticDocumentRevision(),
                            static_cast<std::uint64_t>(
                                std::numeric_limits<int>::max())));
    const QString currentText = editor->toPlainText();
    tracked.snapshot.text = currentText;
    const bool contentChanged = previous && previous->text != currentText;
    tracked.contentChangePending = previous
        && (previous->contentChangePending || contentChanged);
    // A cursorPositionChanged notification can precede textChanged for the
    // same edit. Compare the authoritative cached text instead of relying on
    // Qt's modified flag or signal order. Pure setup / highlighting revisions
    // may advance a clean baseline; any content change must preserve it until
    // DocumentStore::markEdited records the dirty transition.
    if (!previous
        || (!tracked.contentChangePending
            && previous->snapshot.saved
            && !previous->snapshot.dirty)) {
        tracked.snapshot.savedTextVersion = tracked.snapshot.textVersion;
    }

    tracked.text = currentText;
    return tracked;
}
