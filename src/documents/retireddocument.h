#pragma once

#include <QFileInfo>
#include <QString>

namespace RetiredDocument {
inline bool isBlockDocument(const QString& path) {
    return QFileInfo(path).suffix().compare(QStringLiteral("zblock"), Qt::CaseInsensitive) == 0;
}
inline QString message() {
    return QStringLiteral("Block documents (.zblock) are no longer supported. The original file and recovery data have been preserved.");
}
}
