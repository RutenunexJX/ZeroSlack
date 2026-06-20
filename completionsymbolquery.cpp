#include "completionsymbolquery.h"

#include <QSet>
#include <Qt>

QList<SemanticSymbolRecord> CompletionSymbolQuery::structMemberRecords(
    SemanticIndex* semanticIndex,
    const QString& structTypeName,
    const QString& prefix)
{
    QList<SemanticSymbolRecord> result;
    if (!semanticIndex)
        return result;

    QSet<QString> seenNames;
    const QList<SemanticSymbolRecord> members =
        semanticIndex->getStructMemberRecords(structTypeName);
    for (const SemanticSymbolRecord& member : members) {
        if (!nameMatches(member.name, prefix))
            continue;
        const QString key = member.name.toCaseFolded();
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
