#include "semanticindexmodulecontexthelpers.h"

#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace semantic_index_module_context {

namespace {
SymbolTaxonomy::SemanticMetadata moduleContextMetadataForRecord(
    const SemanticSymbolRecord& record)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}
}

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
    return SymbolTaxonomy::commandSymbolTypeMatches(
        symbolType,
        commandType,
        dataType);
}

bool moduleContextSymbolTypeMatches(const sym_list::SymbolInfo& symbol,
                                    sym_list::sym_type_e commandType)
{
    return moduleContextSymbolTypeMatches(
        semanticSymbolRecordForSymbol(symbol),
        commandType);
}

bool moduleContextSymbolTypeMatches(const SemanticSymbolRecord& record,
                                    sym_list::sym_type_e commandType)
{
    return SymbolTaxonomy::commandSymbolTypeMatches(
        moduleContextMetadataForRecord(record),
        commandType,
        record.type.rawTypeText);
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
    return SymbolTaxonomy::isModuleRangeType(type);
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
