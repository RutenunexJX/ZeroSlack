#include "semanticindex.h"

#include "semanticindexsnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {
QString normalizedLookupFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool symbolSearchTypeMatches(sym_list::sym_type_e type,
                             const QList<sym_list::sym_type_e>& types)
{
    return types.isEmpty() || types.contains(type);
}

bool semanticDefinitionSymbolMatches(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord)
{
    if (symbol.symbolName != searchWord)
        return false;

    switch (symbol.symbolType) {
    case sym_list::sym_module:
    case sym_list::sym_interface:
    case sym_list::sym_package:
    case sym_list::sym_task:
    case sym_list::sym_function:
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport:
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_struct_member:
    case sym_list::sym_typedef:
    case sym_list::sym_enum_var:
    case sym_list::sym_enum_value:
        return true;
    default:
        return false;
    }
}

int semanticDefinitionTypePriority(sym_list::sym_type_e type)
{
    switch (type) {
    case sym_list::sym_module: return 0;
    case sym_list::sym_interface: return 1;
    case sym_list::sym_package: return 2;
    case sym_list::sym_port_input:
    case sym_list::sym_port_output:
    case sym_list::sym_port_inout:
    case sym_list::sym_port_ref:
    case sym_list::sym_port_interface:
    case sym_list::sym_port_interface_modport: return 3;
    case sym_list::sym_task:
    case sym_list::sym_function: return 4;
    case sym_list::sym_reg:
    case sym_list::sym_wire:
    case sym_list::sym_logic:
    case sym_list::sym_packed_struct_var:
    case sym_list::sym_unpacked_struct_var:
    case sym_list::sym_enum_var: return 5;
    case sym_list::sym_parameter:
    case sym_list::sym_localparam:
    case sym_list::sym_packed_struct:
    case sym_list::sym_unpacked_struct:
    case sym_list::sym_typedef: return 6;
    case sym_list::sym_struct_member:
    case sym_list::sym_enum_value: return 7;
    default: return 10;
    }
}

bool semanticDefinitionInScope(const sym_list::SymbolInfo& symbol,
                               const SemanticDefinitionQuery& query)
{
    if (query.moduleName.isEmpty())
        return true;
    return symbol.moduleScope == query.moduleName;
}

bool semanticDefinitionSkipForStructMemberType(
    const sym_list::SymbolInfo& symbol,
    const SemanticDefinitionQuery& query)
{
    if (query.structTypeNameForMember.isEmpty())
        return false;
    return symbol.symbolType == sym_list::sym_struct_member
        && symbol.moduleScope != query.structTypeNameForMember;
}

int symbolSearchMatchScore(const QString& symbolName,
                           const SemanticSymbolSearchQuery& query)
{
    if (symbolName.isEmpty())
        return 0;
    if (query.text.isEmpty())
        return 1;

    const Qt::CaseSensitivity sensitivity =
        query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (QString::compare(symbolName, query.text, sensitivity) == 0)
        return 100;
    if (query.exactMatch)
        return 0;
    if (symbolName.startsWith(query.text, sensitivity))
        return 75;
    if (symbolName.contains(query.text, sensitivity))
        return 50;
    return 0;
}

}

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

    for (const sym_list::SymbolInfo& symbol : candidates) {
        if (!semanticDefinitionSymbolMatches(symbol, query.symbolName))
            continue;
        if (semanticDefinitionSkipForStructMemberType(symbol, query))
            continue;
        if (symbol.symbolType != sym_list::sym_struct_member
            && symbol.symbolType != sym_list::sym_enum_value
            && !semanticDefinitionInScope(symbol, query)) {
            continue;
        }

        int priority = semanticDefinitionTypePriority(symbol.symbolType);
        if (!query.moduleName.isEmpty() && symbol.moduleScope == query.moduleName)
            priority -= 100;

        if (!best.found || priority < bestPriority) {
            best.found = true;
            best.localFile = localFile;
            best.symbol = symbol;
            bestPriority = priority;
        }
    }

    return best;
}
