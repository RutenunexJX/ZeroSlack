#include "semanticindexmodulecontexthelpers.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace semantic_index_module_context {

QString normalizedModuleContextFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool moduleContextNameMatches(const QString& name, const QString& prefix)
{
    if (prefix.isEmpty())
        return true;
    if (name.isEmpty())
        return false;

    const QString lowerName = name.toLower();
    const QString lowerPrefix = prefix.toLower();
    if (lowerName.startsWith(lowerPrefix))
        return true;

    int namePos = 0;
    int prefixPos = 0;
    while (prefixPos < lowerPrefix.length() && namePos < lowerName.length()) {
        if (lowerPrefix.at(prefixPos) == lowerName.at(namePos))
            ++prefixPos;
        ++namePos;
    }
    return prefixPos == lowerPrefix.length();
}

void sortModuleContextSymbolRecords(QList<SemanticSymbolRecord>& records)
{
    std::stable_sort(records.begin(), records.end(),
                     [](const SemanticSymbolRecord& a,
                        const SemanticSymbolRecord& b) {
        const int nameCompare = QString::compare(a.name,
                                                 b.name,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.location.startLine != b.location.startLine)
            return a.location.startLine < b.location.startLine;
        if (a.location.fileName != b.location.fileName)
            return a.location.fileName < b.location.fileName;
        return a.localHandle < b.localHandle;
    });
}

} // namespace semantic_index_module_context
