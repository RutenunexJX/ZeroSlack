#include "semanticindex.h"

#include <QSet>
#include <algorithm>

namespace {
bool semanticCompletionContextNameMatches(const QString& name, const QString& prefix)
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

QString displayNameForCompletionContextRecord(
    const SemanticSymbolRecord& record)
{
    if (!record.name.isEmpty())
        return record.name;
    return QString();
}

QStringList uniqueSortedCompletionContextSymbolNames(
    const QList<SemanticSymbolRecord>& records)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const SemanticSymbolRecord& record : records) {
        const QString displayName =
            displayNameForCompletionContextRecord(record);
        const QString key = displayName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(displayName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString ownerNameForCompletionContextRecord(
    const SemanticSymbolRecord& record)
{
    return record.owner.name;
}

QString rawTypeTextForCompletionContextRecord(
    const SemanticSymbolRecord& record)
{
    return record.type.rawTypeText;
}

QList<SemanticSymbolRecord> completionContextRecordsByRawKind(
    const QList<SemanticSymbolRecord>& records,
    sym_list::sym_type_e rawKind)
{
    QList<SemanticSymbolRecord> result;
    for (const SemanticSymbolRecord& record : records) {
        if (record.rawCollectorKind == rawKind)
            result.append(record);
    }
    return result;
}

}

QStringList SemanticIndex::getEnumValueCompletionNames(
    const QString& prefix,
    const QString& enumTypeName) const
{
    QList<SemanticSymbolRecord> result;
    const QList<SemanticSymbolRecord> records =
        completionContextRecordsByRawKind(getSymbolRecords(),
                                          sym_list::sym_enum_value);
    for (const SemanticSymbolRecord& record : records) {
        if (!enumTypeName.isEmpty()
            && ownerNameForCompletionContextRecord(record) != enumTypeName)
            continue;
        if (!semanticCompletionContextNameMatches(
                displayNameForCompletionContextRecord(record), prefix))
            continue;
        result.append(record);
    }
    return uniqueSortedCompletionContextSymbolNames(result);
}

QString SemanticIndex::enumTypeForVariable(
    const QString& variableName,
    const QString& moduleName) const
{
    if (!moduleName.isEmpty()) {
        const QList<sym_list::SymbolInfo> moduleSymbols =
            getModuleInternalSymbolsByType(moduleName, sym_list::sym_enum_var);
        for (const sym_list::SymbolInfo& symbol : moduleSymbols) {
            if (symbol.symbolName == variableName)
                return ownerNameForCompletionContextRecord(
                    semanticSymbolRecordForSymbol(symbol));
        }
    }

    const QList<SemanticSymbolRecord> records =
        completionContextRecordsByRawKind(getSymbolRecords(),
                                          sym_list::sym_enum_var);
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == variableName)
            return ownerNameForCompletionContextRecord(record);
    }

    return QString();
}

QStringList SemanticIndex::getModulePortCompletionNames(
    const QString& prefix,
    const QString& moduleTypeName) const
{
    if (moduleTypeName.isEmpty())
        return {};

    bool moduleExists = false;
    const QList<SemanticSymbolRecord> modules =
        completionContextRecordsByRawKind(getSymbolRecords(),
                                          sym_list::sym_module);
    for (const SemanticSymbolRecord& record : modules) {
        if (record.name == moduleTypeName) {
            moduleExists = true;
            break;
        }
    }
    if (!moduleExists)
        return {};

    QList<sym_list::SymbolInfo> portSymbols;
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_wire,
                                                   prefix));
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_reg,
                                                   prefix));
    portSymbols.append(getCommandCompletionSymbols(moduleTypeName,
                                                   sym_list::sym_logic,
                                                   prefix));
    return uniqueSortedCompletionContextSymbolNames(
        semanticSymbolRecordsForSymbols(portSymbols));
}

QString SemanticIndex::getStructTypeForVariable(const QString& variableName,
                                                const QString& moduleName) const
{
    if (variableName.isEmpty())
        return QString();

    QList<SemanticSymbolRecord> structVariables =
        completionContextRecordsByRawKind(getSymbolRecords(),
                                          sym_list::sym_packed_struct_var);
    structVariables.append(
        completionContextRecordsByRawKind(getSymbolRecords(),
                                          sym_list::sym_unpacked_struct_var));

    if (!moduleName.isEmpty()) {
        for (const SemanticSymbolRecord& record : std::as_const(structVariables)) {
            const QString ownerName =
                ownerNameForCompletionContextRecord(record);
            const QString rawTypeText =
                rawTypeTextForCompletionContextRecord(record);
            if (record.name == variableName
                && ownerName == moduleName
                && !rawTypeText.isEmpty()) {
                return rawTypeText;
            }
        }
    }

    for (const SemanticSymbolRecord& record : std::as_const(structVariables)) {
        const QString rawTypeText =
            rawTypeTextForCompletionContextRecord(record);
        if (record.name == variableName && !rawTypeText.isEmpty())
            return rawTypeText;
    }

    return QString();
}

QList<sym_list::SymbolInfo> SemanticIndex::getStructMembers(
    const QString& structTypeName) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<SemanticSymbolRecord> members =
        completionContextRecordsByRawKind(getSymbolRecords(),
                                          sym_list::sym_struct_member);
    for (const SemanticSymbolRecord& record : members) {
        if (!structTypeName.isEmpty()
            && ownerNameForCompletionContextRecord(record) != structTypeName)
            continue;
        result.append(semanticSymbolInfoCarrierForRecord(record));
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const sym_list::SymbolInfo& a,
                        const sym_list::SymbolInfo& b) {
        const int nameCompare = QString::compare(a.symbolName,
                                                 b.symbolName,
                                                 Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        return a.symbolId < b.symbolId;
    });
    return result;
}
