#include "semanticindex.h"

#include "semanticindexlookuphelpers.h"
#include "semanticindexsnapshot.h"

#include <QSet>
#include <algorithm>

using namespace semantic_index_lookup;

namespace {

bool packageVisibleDefinitionType(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_typedef:
    case sym_list::sym_enum:
    case sym_list::sym_enum_value:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
        return true;
    default:
        return false;
    }
}

bool globalDefinitionType(sym_list::sym_type_e type)
{
    return type == sym_list::sym_module
        || type == sym_list::sym_interface
        || type == sym_list::sym_package;
}

QSet<QString> packageScopeNames(const QList<sym_list::SymbolInfo>& symbols)
{
    QSet<QString> names;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (symbol.symbolType == sym_list::sym_package
            && !symbol.symbolName.isEmpty()) {
            names.insert(symbol.symbolName);
        }
    }
    return names;
}

bool packageScopeVisible(const sym_list::SymbolInfo& symbol,
                         const QSet<QString>& packages)
{
    return packageVisibleDefinitionType(symbol.symbolType)
        && !symbol.moduleScope.isEmpty()
        && packages.contains(symbol.moduleScope);
}

} // namespace

QList<SemanticSymbolSearchResult> SemanticIndex::searchSymbols(
    const SemanticSymbolSearchQuery& query) const
{
    QList<SemanticSymbolSearchResult> result;
    const QList<sym_list::SymbolInfo> symbols = getSymbols(query.fileName);
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!symbolSearchTypeMatches(symbol.symbolType, query.types))
            continue;

        const int score = symbolSearchMatchScore(symbol.symbolName, query);
        if (score <= 0)
            continue;

        SemanticSymbolSearchResult item;
        item.symbol = symbol;
        item.score = score;
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolSearchResult& a,
                        const SemanticSymbolSearchResult& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.symbol.fileName != b.symbol.fileName)
            return a.symbol.fileName < b.symbol.fileName;
        if (a.symbol.startLine != b.symbol.startLine)
            return a.symbol.startLine < b.symbol.startLine;
        return a.symbol.symbolName < b.symbol.symbolName;
    });

    if (query.maxResults >= 0 && result.size() > query.maxResults)
        result = result.mid(0, query.maxResults);
    return result;
}

sym_list::SymbolInfo SemanticIndex::getSymbolById(int symbolId) const
{
    if (m_snapshot)
        return m_snapshot->getSymbolById(symbolId);

    if (symbolId < 0) {
        sym_list::SymbolInfo missing;
        missing.symbolId = -1;
        return missing;
    }

    sym_list::SymbolInfo symbol = symbolDatabase()->getSymbolById(symbolId);
    if (symbol.symbolId != -1)
        return symbol;

    const QList<sym_list::SymbolInfo> allSymbols = getSymbols();
    for (const sym_list::SymbolInfo& candidate : allSymbols) {
        if (candidate.symbolId == symbolId)
            return candidate;
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
}

SemanticDefinitionResult SemanticIndex::resolveDefinition(
    const SemanticDefinitionQuery& query) const
{
    SemanticDefinitionResult empty;
    if (query.symbolName.isEmpty())
        return empty;

    SemanticDefinitionResult local = bestDefinitionFromCandidates(
        getSymbols(query.fileName),
        query,
        true);
    if (local.found)
        return local;

    QList<sym_list::SymbolInfo> globalCandidates = findDefinitions(query.symbolName);
    const QString queryFile = normalizedLookupFileName(query.fileName);
    globalCandidates.erase(
        std::remove_if(globalCandidates.begin(), globalCandidates.end(),
                       [&queryFile](const sym_list::SymbolInfo& symbol) {
                           return normalizedLookupFileName(symbol.fileName) == queryFile;
                       }),
        globalCandidates.end());
    return bestDefinitionFromCandidates(globalCandidates, query, false);
}

QList<sym_list::SymbolInfo> SemanticIndex::findDefinitionSymbols(
    const SemanticDefinitionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    const SemanticDefinitionResult resolved = resolveDefinition(query);
    if (resolved.found)
        result.append(resolved.symbol);
    return result;
}

int SemanticIndex::findSymbolId(const QString& name,
                                const SemanticQueryContext& context) const
{
    if (m_snapshot)
        return m_snapshot->findSymbolId(name, context);

    const QList<sym_list::SymbolInfo> symbols = findDefinitions(name, context);
    if (symbols.isEmpty())
        return -1;
    return symbols.first().symbolId;
}

QList<sym_list::SymbolInfo> SemanticIndex::findDefinitions(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    if (m_snapshot)
        return m_snapshot->findDefinitions(name, context);

    QList<sym_list::SymbolInfo> symbols = symbolDatabase()->findSymbolsByName(name);
    if (symbols.isEmpty())
        return symbols;

    return sortedDefinitions(symbols, context);
}

QList<sym_list::SymbolInfo> SemanticIndex::sortedDefinitions(
    const QList<sym_list::SymbolInfo>& symbols,
    const SemanticQueryContext& context) const
{
    QList<sym_list::SymbolInfo> sorted = symbols;
    const QString normalizedContextFile = normalizedLookupFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const sym_list::SymbolInfo& a,
                                                        const sym_list::SymbolInfo& b) {
        auto score = [&context, &normalizedContextFile](const sym_list::SymbolInfo& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedLookupFileName(s.fileName) == normalizedContextFile)
                value += 100;
            if (!context.moduleName.isEmpty() && s.moduleScope == context.moduleName)
                value += 50;
            if (s.symbolType == sym_list::sym_module
                || s.symbolType == sym_list::sym_interface
                || s.symbolType == sym_list::sym_package)
                value += 10;
            return value;
        };

        const int aScore = score(a);
        const int bScore = score(b);
        if (aScore != bScore)
            return aScore > bScore;
        if (a.fileName != b.fileName)
            return a.fileName < b.fileName;
        if (a.startLine != b.startLine)
            return a.startLine < b.startLine;
        return a.symbolId < b.symbolId;
    });
    return sorted;
}

SemanticDefinitionResult SemanticIndex::bestDefinitionFromCandidates(
    const QList<sym_list::SymbolInfo>& candidates,
    const SemanticDefinitionQuery& query,
    bool localFile) const
{
    SemanticDefinitionResult best;
    int bestPriority = 999;
    const QSet<QString> packages = packageScopeNames(getSymbols());

    for (const sym_list::SymbolInfo& symbol : candidates) {
        if (!semanticDefinitionSymbolMatches(symbol, query.symbolName))
            continue;
        if (semanticDefinitionSkipForStructMemberType(symbol, query))
            continue;
        if (symbol.symbolType != sym_list::sym_struct_member
            && symbol.symbolType != sym_list::sym_interface_modport
            && symbol.symbolType != sym_list::sym_enum_value
            && !globalDefinitionType(symbol.symbolType)
            && !semanticDefinitionInScope(symbol, query)
            && !packageScopeVisible(symbol, packages)) {
            continue;
        }

        int priority = semanticDefinitionTypePriority(symbol.symbolType);
        if (!query.moduleName.isEmpty() && symbol.moduleScope == query.moduleName)
            priority -= 100;
        else if (packageScopeVisible(symbol, packages))
            priority -= 20;

        if (!best.found || priority < bestPriority) {
            best.found = true;
            best.localFile = localFile;
            best.symbol = symbol;
            bestPriority = priority;
        }
    }

    return best;
}
