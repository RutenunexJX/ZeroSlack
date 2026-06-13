#include "documentregistry.h"

#include "mycodeeditor.h"

#include <QDir>
#include <QFileInfo>
#include <QTextBlock>
#include <QTextCursor>

QString DocumentSnapshotReader::normalizedFileName(
    const QString& fileName) const
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString DocumentSnapshotReader::documentIdForEditor(MyCodeEditor* editor) const
{
    if (!editor)
        return QString();

    const QString fileName = normalizedFileName(editor->documentFileName());
    if (!fileName.isEmpty())
        return fileName;

    return QStringLiteral("untitled:%1")
        .arg(QString::number(reinterpret_cast<quintptr>(editor), 16));
}

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

    tracked.snapshot.fileName = normalizedFileName(editor->documentFileName());
    tracked.snapshot.documentId = tracked.snapshot.fileName.isEmpty()
        ? tracked.snapshot.documentId
        : tracked.snapshot.fileName;

    const QTextCursor cursor = editor->textCursor();
    const QTextBlock block = cursor.block();
    tracked.snapshot.cursorPosition = cursor.position();
    tracked.snapshot.cursorLine = block.isValid() ? block.blockNumber() + 1 : 1;
    tracked.snapshot.cursorColumn = block.isValid()
        ? cursor.position() - block.position() + 1
        : 1;
    tracked.snapshot.currentModuleName = editor->currentModuleName();

    if (tracked.snapshot.documentId.isEmpty())
        tracked.snapshot.documentId = documentIdForEditor(editor);
    tracked.text = editor->toPlainText();
    return tracked;
}
