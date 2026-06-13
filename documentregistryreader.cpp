#include "documentregistry.h"

#include "mycodeeditor.h"

TrackedDocument DocumentSnapshotReader::capture(
    MyCodeEditor* editor,
    const DocumentSnapshot* previous) const
{
    TrackedDocument tracked;
    tracked.editor = editor;
    if (!editor)
        return tracked;

    if (previous)
        tracked.snapshot = *previous;

    captureFileIdentity(editor, &tracked.snapshot);
    captureCursorState(editor, &tracked.snapshot);

    tracked.text = editor->toPlainText();
    return tracked;
}
