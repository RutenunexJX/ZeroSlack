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

QString displayNameForCompletionContextSymbol(
    const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    if (!record.name.isEmpty())
        return record.name;
    return symbol.symbolName;
}

QStringList uniqueSortedCompletionContextSymbolNames(
    const QList<sym_list::SymbolInfo>& symbols)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString displayName =
            displayNameForCompletionContextSymbol(symbol);
        const QString key = displayName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(displayName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString ownerNameForCompletionContextSymbol(
    const sym_list::SymbolInfo& symbol)
{
    return semanticSymbolRecordForSymbol(symbol).owner.name;
}

QString rawTypeTextForCompletionContextSymbol(
    const sym_list::SymbolInfo& symbol)
{
    return semanticSymbolRecordForSymbol(symbol).type.rawTypeText;
}

}

QStringList SemanticIndex::getEnumValueCompletionNames(
    const QString& prefix,
    const QString& enumTypeName) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> symbols =
        getSymbolsByType(sym_list::sym_enum_value);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!enumTypeName.isEmpty()
            && ownerNameForCompletionContextSymbol(symbol) != enumTypeName)
            continue;
        if (!semanticCompletionContextNameMatches(
                displayNameForCompletionContextSymbol(symbol), prefix))
            continue;
        result.append(symbol);
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
                return ownerNameForCompletionContextSymbol(symbol);
        }
    }

    const QList<sym_list::SymbolInfo> symbols =
        getSymbolsByType(sym_list::sym_enum_var);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolName == variableName)
            return ownerNameForCompletionContextSymbol(symbol);
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
    const QList<sym_list::SymbolInfo> modules =
        getSymbolsByType(sym_list::sym_module);
    for (const sym_list::SymbolInfo& symbol : modules) {
        if (symbol.symbolName == moduleTypeName) {
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
    return uniqueSortedCompletionContextSymbolNames(portSymbols);
}

QString SemanticIndex::getStructTypeForVariable(const QString& variableName,
                                                const QString& moduleName) const
{
    if (variableName.isEmpty())
        return QString();

    QList<sym_list::SymbolInfo> structVariables =
        getSymbolsByType(sym_list::sym_packed_struct_var);
    structVariables.append(getSymbolsByType(sym_list::sym_unpacked_struct_var));

    if (!moduleName.isEmpty()) {
        for (const sym_list::SymbolInfo& symbol : std::as_const(structVariables)) {
            const QString ownerName =
                ownerNameForCompletionContextSymbol(symbol);
            const QString rawTypeText =
                rawTypeTextForCompletionContextSymbol(symbol);
            if (symbol.symbolName == variableName
                && ownerName == moduleName
                && !rawTypeText.isEmpty()) {
                return rawTypeText;
            }
        }
    }

    for (const sym_list::SymbolInfo& symbol : std::as_const(structVariables)) {
        const QString rawTypeText =
            rawTypeTextForCompletionContextSymbol(symbol);
        if (symbol.symbolName == variableName && !rawTypeText.isEmpty())
            return rawTypeText;
    }

    return QString();
}

QList<sym_list::SymbolInfo> SemanticIndex::getStructMembers(
    const QString& structTypeName) const
{
    QList<sym_list::SymbolInfo> result;
    const QList<sym_list::SymbolInfo> members =
        getSymbolsByType(sym_list::sym_struct_member);
    for (const sym_list::SymbolInfo& symbol : members) {
        if (!structTypeName.isEmpty()
            && ownerNameForCompletionContextSymbol(symbol) != structTypeName)
            continue;
        result.append(symbol);
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
