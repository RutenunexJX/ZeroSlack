#include "documentregistry.h"

#include "mycodeeditor.h"

#include <QDir>
#include <QFileInfo>

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

    const QString sharedDocumentId =
        editor->property("sharedDocumentId").toString();
    if (!sharedDocumentId.isEmpty())
        return sharedDocumentId;

    const QString fileName = normalizedFileName(editor->documentFileName());
    if (!fileName.isEmpty())
        return fileName;

    return QStringLiteral("untitled:%1")
        .arg(QString::number(reinterpret_cast<quintptr>(editor), 16));
}

void DocumentSnapshotReader::captureFileIdentity(
    MyCodeEditor* editor,
    DocumentSnapshot* snapshot) const
{
    if (!editor || !snapshot)
        return;

    snapshot->fileName = normalizedFileName(editor->documentFileName());
    snapshot->documentId = snapshot->fileName.isEmpty()
        ? snapshot->documentId
        : snapshot->fileName;
    if (snapshot->documentId.isEmpty())
        snapshot->documentId = documentIdForEditor(editor);
}
