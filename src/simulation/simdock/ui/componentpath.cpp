#include "componentpath.h"
#include <QCoreApplication>
#include <QDir>

namespace simdock {
QString componentDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("components/wave"));
}
}
