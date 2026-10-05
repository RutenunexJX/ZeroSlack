#include "documentregistry.h"

#include "mycodeeditor.h"
#include "editorfileidentity.h"

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
        return EditorFileIdentity::physicalPath(fileName);

    return QStringLiteral("untitled:%1")
        .arg(QString::number(reinterpret_cast<quintptr>(editor), 16));
}

void DocumentSnapshotReader::captureFileIdentity(
    MyCodeEditor* editor,
    DocumentSnapshot* snapshot) const
{
    if (!editor || !snapshot)
        return;

    const auto fileName = normalizedFileName(editor->documentFileName());
    const auto sharedId = editor->property("sharedDocumentId").toString();
    if (!sharedId.isEmpty())
        snapshot->documentId = sharedId;
    else if (snapshot->documentId.isEmpty() || snapshot->fileName != fileName)
        snapshot->documentId = documentIdForEditor(editor);
    snapshot->fileName = fileName;
}
