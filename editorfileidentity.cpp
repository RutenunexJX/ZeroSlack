#include "editorfileidentity.h"

#include <QDir>
#include <QFileInfo>

QString EditorFileIdentity::normalized(QString fileName)
{
    return fileName.isEmpty()
        ? QString()
        : QDir::cleanPath(QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

bool EditorFileIdentity::set(QString nextFileName)
{
    const QString normalizedFileName = normalized(nextFileName);
    if (fileName == normalizedFileName)
        return false;

    fileName = normalizedFileName;
    return true;
}

QString EditorFileIdentity::current() const
{
    return fileName;
}
