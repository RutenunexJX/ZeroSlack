#include "documentregistry.h"

#include "mycodeeditor.h"

#include <limits>

TrackedDocument DocumentSnapshotReader::capture(
    MyCodeEditor* editor,
    const TrackedDocument* previous) const
{
    TrackedDocument tracked;
    if (!editor)
        return tracked;

    if (previous)
        tracked = *previous;
    tracked.editor = editor;

    captureFileIdentity(editor, &tracked.snapshot);
    captureCursorState(editor, &tracked.snapshot);

    // DocumentSnapshot::textVersion is shared by editor queries and semantic
    // publication. It must only follow source-text changes: QTextDocument's
    // own revision also advances for appearance / highlighting changes.
    tracked.snapshot.textVersion = static_cast<int>(
        qMin<std::uint64_t>(editor->semanticDocumentRevision(),
                            static_cast<std::uint64_t>(
                                std::numeric_limits<int>::max())));
    if (!previous) {
        const QString& currentText = editor->cachedDocumentText();
        recordDocumentTextCopy(currentText.size());
        tracked.snapshot.text = QString(currentText.constData(),
                                        currentText.size());
        tracked.snapshot.savedTextVersion = tracked.snapshot.textVersion;
    }
    return tracked;
}
