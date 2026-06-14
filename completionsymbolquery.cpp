#include "completionsymbolquery.h"

#include "symboltaxonomy.h"

#include <QSet>
#include <Qt>

QList<sym_list::SymbolInfo> CompletionSymbolQuery::structMemberSymbols(
    SemanticIndex* semanticIndex,
    const QString& structTypeName,
    const QString& prefix)
{
    QList<sym_list::SymbolInfo> result;
    if (!semanticIndex)
        return result;

    QSet<QString> seenNames;
    const QList<sym_list::SymbolInfo> members =
        semanticIndex->getStructMembers(structTypeName);
    for (const sym_list::SymbolInfo& member : members) {
        if (!nameMatches(member.symbolName, prefix))
            continue;
        const QString key = member.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(member);
    }
    return result;
}

QStringList CompletionSymbolQuery::scopeCompletions(
    SemanticIndex* semanticIndex,
    const QString& fileName,
    int cursorLine,
    const QString& prefix)
{
    QStringList result;
    if (!semanticIndex || fileName.isEmpty() || cursorLine < 0)
        return result;

    const QStringList scopeNames =
        semanticIndex->getScopeSymbolNames(fileName, cursorLine);
    QSet<QString> seenNames;
    for (const QString& name : scopeNames) {
        if (!nameMatches(name, prefix))
            continue;
        const QString key = name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionSymbolQuery::moduleSymbolsByType(
    SemanticIndex* semanticIndex,
    const QString& moduleName,
    sym_list::sym_type_e symbolType,
    const QString& prefix)
{
    QStringList result;
    if (!semanticIndex || moduleName.isEmpty())
        return result;

    return namesFromSymbols(
        semanticIndex->getCommandCompletionSymbols(moduleName, symbolType, prefix));
}

QStringList CompletionSymbolQuery::globalSymbolsByType(
    SemanticIndex* semanticIndex,
    sym_list::sym_type_e symbolType,
    const QString& prefix)
{
    QStringList result;
    if (!semanticIndex || !isGlobalSymbolType(symbolType))
        return result;

    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex->getCommandCompletionSymbols(QString(), symbolType, prefix);
    QSet<QString> seenNames;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        bool global = false;
        if (SymbolTaxonomy::isAlwaysGlobalSymbolInfoType(symbolType)) {
            global = true;
        } else {
            global = symbol.moduleScope.isEmpty();
        }
        if (!global)
            continue;

        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList CompletionSymbolQuery::taskFunctionCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix)
{
    QStringList result;
    if (!semanticIndex)
        return result;

    result.append(namesFromSymbols(
        semanticIndex->getTypedCompletionSymbols(sym_list::sym_task, prefix)));
    result.append(namesFromSymbols(
        semanticIndex->getTypedCompletionSymbols(sym_list::sym_function, prefix)));
    result.removeDuplicates();
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool CompletionSymbolQuery::isGlobalSymbolType(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::isCommandGlobalCompletionType(type);
}
