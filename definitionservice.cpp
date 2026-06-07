#include "definitionservice.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

std::unique_ptr<DefinitionService> DefinitionService::instance = nullptr;

namespace {
QString normalizedDefinitionFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}
}

DefinitionService* DefinitionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<DefinitionService>();
    return instance.get();
}

DefinitionService::DefinitionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

DefinitionService::~DefinitionService() = default;

void DefinitionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

DefinitionResult DefinitionService::resolveDefinition(const DefinitionQuery& query) const
{
    DefinitionResult empty;
    if (query.symbolName.isEmpty())
        return empty;

    DefinitionResult local = bestFromCandidates(
        semanticIndex()->getSymbols(query.fileName),
        query,
        true);
    if (local.found)
        return local;

    QList<sym_list::SymbolInfo> globalCandidates = semanticIndex()->findDefinitions(query.symbolName);
    const QString queryFile = normalizedDefinitionFileName(query.fileName);
    globalCandidates.erase(
        std::remove_if(globalCandidates.begin(), globalCandidates.end(),
                       [&queryFile](const sym_list::SymbolInfo& symbol) {
                           return normalizedDefinitionFileName(symbol.fileName) == queryFile;
                       }),
        globalCandidates.end());
    return bestFromCandidates(globalCandidates, query, false);
}

QList<sym_list::SymbolInfo> DefinitionService::findDefinitions(const DefinitionQuery& query) const
{
    QList<sym_list::SymbolInfo> result;
    const DefinitionResult resolved = resolveDefinition(query);
    if (resolved.found)
        result.append(resolved.symbol);
    return result;
}

bool DefinitionService::canResolveDefinition(const DefinitionQuery& query) const
{
    return resolveDefinition(query).found;
}

bool DefinitionService::isDefinition(const sym_list::SymbolInfo& symbol,
                                     const QString& searchWord) const
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

SemanticIndex* DefinitionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

int DefinitionService::definitionTypePriority(sym_list::sym_type_e type) const
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

bool DefinitionService::inScope(const sym_list::SymbolInfo& symbol,
                                const DefinitionQuery& query) const
{
    if (query.moduleName.isEmpty())
        return true;
    return symbol.moduleScope == query.moduleName;
}

bool DefinitionService::shouldSkipForStructMemberType(const sym_list::SymbolInfo& symbol,
                                                      const DefinitionQuery& query) const
{
    if (query.structTypeNameForMember.isEmpty())
        return false;
    return symbol.symbolType == sym_list::sym_struct_member
        && symbol.moduleScope != query.structTypeNameForMember;
}

DefinitionResult DefinitionService::bestFromCandidates(
    const QList<sym_list::SymbolInfo>& candidates,
    const DefinitionQuery& query,
    bool localFile) const
{
    DefinitionResult best;
    int bestPriority = 999;

    for (const sym_list::SymbolInfo& symbol : candidates) {
        if (!isDefinition(symbol, query.symbolName))
            continue;
        if (shouldSkipForStructMemberType(symbol, query))
            continue;
        if (symbol.symbolType != sym_list::sym_struct_member
            && symbol.symbolType != sym_list::sym_enum_value
            && !inScope(symbol, query)) {
            continue;
        }

        int priority = definitionTypePriority(symbol.symbolType);
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
