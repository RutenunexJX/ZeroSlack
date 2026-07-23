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

QString EditorFileIdentity::lookupKey(QString fileName)
{
    QString key = normalized(fileName);
    if (key.isEmpty())
        return key;
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

bool EditorFileIdentity::same(const QString& lhs, const QString& rhs)
{
    const QString lhsKey = lookupKey(lhs);
    return !lhsKey.isEmpty() && lhsKey == lookupKey(rhs);
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
