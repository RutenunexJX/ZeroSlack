#include "completionsymbolquery.h"

#include "completionmatcher.h"

#include <QSet>
#include <Qt>
#include <algorithm>

QVector<QPair<QString, int>> CompletionSymbolQuery::scoredNames(
    const QStringList& names,
    const QString& prefix,
    int maxResults)
{
    QVector<QPair<QString, int>> scored;
    scored.reserve(qMin(names.size(), maxResults > 0 ? maxResults : names.size()));
    for (const QString& name : names) {
        const int score = CompletionMatcher::calculateContextMatchScore(name, prefix);
        if (score > 0)
            scored.append(qMakePair(name, score));
    }

    std::sort(scored.begin(), scored.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    if (maxResults > 0 && scored.size() > maxResults)
        scored = scored.mid(0, maxResults);

    return scored;
}

QVector<QPair<sym_list::SymbolInfo, int>>
CompletionSymbolQuery::scoredTypedSymbols(
    SemanticIndex* semanticIndex,
    sym_list::sym_type_e symbolType,
    const QString& prefix,
    int maxResults)
{
    QVector<QPair<sym_list::SymbolInfo, int>> result;
    if (!semanticIndex)
        return result;

    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex->getTypedCompletionSymbols(symbolType);

    result.reserve(qMin(symbols.size(), maxResults > 0 ? maxResults : symbols.size()));
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const int score =
            CompletionMatcher::calculateSymbolTypeScore(symbol.symbolName, prefix);
        if (score > 0)
            result.append(qMakePair(symbol, score));
    }

    std::sort(result.begin(), result.end(),
              [](const QPair<sym_list::SymbolInfo, int>& left,
                 const QPair<sym_list::SymbolInfo, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first.symbolName < right.first.symbolName;
              });

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionSymbolQuery::namesFromScored(
    const QVector<QPair<QString, int>>& scored,
    int maxResults)
{
    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored)
        result.append(match.first);

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionSymbolQuery::symbolNamesFromScored(
    const QVector<QPair<sym_list::SymbolInfo, int>>& scored,
    int maxResults)
{
    QStringList result;
    result.reserve(scored.size());
    for (const auto& match : scored) {
        if (!result.contains(match.first.symbolName))
            result.append(match.first.symbolName);
    }

    if (maxResults > 0 && result.size() > maxResults)
        result = result.mid(0, maxResults);

    return result;
}

QStringList CompletionSymbolQuery::namesFromSymbols(
    const QList<sym_list::SymbolInfo>& symbols)
{
    QStringList result;
    QSet<QString> seenNames;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const QString key = symbol.symbolName.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(symbol.symbolName);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool CompletionSymbolQuery::nameMatches(const QString& name, const QString& prefix)
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
        if (symbolType == sym_list::sym_module
            || symbolType == sym_list::sym_interface
            || symbolType == sym_list::sym_package
            || symbolType == sym_list::sym_packed_struct
            || symbolType == sym_list::sym_unpacked_struct) {
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

bool CompletionSymbolQuery::isModuleRangeSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_packed_struct_var
        || type == sym_list::sym_unpacked_struct_var;
}

bool CompletionSymbolQuery::isGlobalSymbolType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_task
        || type == sym_list::sym_function
        || type == sym_list::sym_interface
        || type == sym_list::sym_package
        || type == sym_list::sym_typedef
        || type == sym_list::sym_def_define
        || type == sym_list::sym_packed_struct
        || type == sym_list::sym_unpacked_struct
        || type == sym_list::sym_enum;
}
