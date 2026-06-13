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

bool moduleContextSymbolTypeMatches(sym_list::sym_type_e symbolType,
                                    sym_list::sym_type_e commandType,
                                    const QString& dataType)
{
    if (symbolType == commandType)
        return true;
    return commandType == sym_list::sym_enum
        && symbolType == sym_list::sym_typedef
        && dataType == QLatin1String("enum");
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

bool isModuleRangeSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
}

void sortModuleContextSymbols(QList<sym_list::SymbolInfo>& symbols)
{
    std::stable_sort(symbols.begin(), symbols.end(),
                     [](const sym_list::SymbolInfo& a,
                        const sym_list::SymbolInfo& b) {
        const int nameCompare = QString::compare(a.symbolName,
                                                 b.symbolName,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        return a.symbolId < b.symbolId;
    });
}

} // namespace semantic_index_module_context
