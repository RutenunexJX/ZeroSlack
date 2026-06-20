#include "completionsymbolquery.h"

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
