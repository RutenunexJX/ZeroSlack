#include "modulebriefservice.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<ModuleBriefService> ModuleBriefService::instance = nullptr;

namespace {
sym_list::SymbolInfo missingModuleBriefSymbol()
{
    sym_list::SymbolInfo symbol;
    symbol.symbolId = -1;
    return symbol;
}
}

ModuleBriefService* ModuleBriefService::getInstance()
{
    if (!instance)
        instance = std::make_unique<ModuleBriefService>();
    return instance.get();
}

ModuleBriefService::ModuleBriefService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

ModuleBriefService::~ModuleBriefService() = default;

void ModuleBriefService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

ModuleBriefReport ModuleBriefService::buildModuleBrief(
    const ModuleBriefQuery& query) const
{
    ModuleBriefReport report;
    const sym_list::SymbolInfo moduleSymbol = resolveModule(query);
    if (moduleSymbol.symbolId < 0)
        return report;

    report.found = true;
    report.moduleSymbol = moduleSymbol;
    const QList<sym_list::SymbolInfo> symbols =
        semanticIndex()->getSymbols(moduleSymbol.fileName);

    report.ports = symbolsInModule(
        moduleSymbol,
        symbols,
        SymbolTaxonomy::isPortDeclaration);
    report.parameters = symbolsInModule(
        moduleSymbol,
        symbols,
        SymbolTaxonomy::isParameterDeclaration);
    report.instances = symbolsInModule(
        moduleSymbol,
        symbols,
        SymbolTaxonomy::isInstanceDeclaration);
    report.imports = importSymbols(moduleSymbol);
    report.diagnostics = diagnosticsForModule(moduleSymbol);
    report.portRows = symbolRows(report.ports, QStringLiteral("Port"));
    report.parameterRows = symbolRows(report.parameters, QStringLiteral("Parameter"));
    report.instanceRows = symbolRows(report.instances, QStringLiteral("Instance"));
    report.importRows = symbolRows(report.imports, QStringLiteral("Import"));
    report.diagnosticRows = diagnosticRows(report.diagnostics);
    report.relationshipSummary = relationshipSummary(moduleSymbol);
    return report;
}

SemanticIndex* ModuleBriefService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

sym_list::SymbolInfo ModuleBriefService::resolveModule(
    const ModuleBriefQuery& query) const
{
    if (query.moduleSymbolId >= 0) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolById(query.moduleSymbolId);
        return SymbolTaxonomy::isModuleDeclaration(symbol.symbolType)
            ? symbol
            : missingModuleBriefSymbol();
    }
    if (query.moduleName.isEmpty())
        return missingModuleBriefSymbol();

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.moduleName;
    definitionQuery.fileName = query.fileName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    return definition.found
            && SymbolTaxonomy::isModuleDeclaration(definition.symbol.symbolType)
        ? definition.symbol
        : missingModuleBriefSymbol();
}

QList<sym_list::SymbolInfo> ModuleBriefService::symbolsInModule(
    const sym_list::SymbolInfo& moduleSymbol,
    const QList<sym_list::SymbolInfo>& symbols,
    bool (*matchesType)(sym_list::sym_type_e)) const
{
    QList<sym_list::SymbolInfo> result;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (!matchesType(symbol.symbolType))
            continue;
        if (!isInsideModule(symbol, moduleSymbol))
            continue;
        result.append(symbol);
    }
    sortSymbols(result);
    return result;
}

QList<sym_list::SymbolInfo> ModuleBriefService::importSymbols(
    const sym_list::SymbolInfo& moduleSymbol) const
{
    QList<sym_list::SymbolInfo> result;
    QSet<int> seen;
    const QList<SemanticRelationshipResult> relationships =
        semanticIndex()->getRelationshipResults(moduleSymbol.symbolId, true);
    for (const SemanticRelationshipResult& relationship : relationships) {
        if (!SymbolTaxonomy::isPackageDeclaration(relationship.toSymbol.symbolType))
            continue;
        if (seen.contains(relationship.toSymbol.symbolId))
            continue;
        seen.insert(relationship.toSymbol.symbolId);
        result.append(relationship.toSymbol);
    }
    sortSymbols(result);
    return result;
}

QList<SemanticDiagnostic> ModuleBriefService::diagnosticsForModule(
    const sym_list::SymbolInfo& moduleSymbol) const
{
    QList<SemanticDiagnostic> result;
    const QList<SemanticDiagnostic> diagnostics =
        semanticIndex()->getDiagnostics(moduleSymbol.fileName);
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.line <= 0) {
            result.append(diagnostic);
            continue;
        }
        const bool afterStart = diagnostic.line >= moduleSymbol.startLine;
        const bool beforeEnd = moduleSymbol.endLine <= 0
            || diagnostic.line <= moduleSymbol.endLine;
        if (afterStart && beforeEnd)
            result.append(diagnostic);
    }
    std::sort(result.begin(), result.end(),
              [](const SemanticDiagnostic& lhs, const SemanticDiagnostic& rhs) {
                  if (lhs.line != rhs.line)
                      return lhs.line < rhs.line;
                  if (lhs.column != rhs.column)
                      return lhs.column < rhs.column;
                  return lhs.message < rhs.message;
              });
    return result;
}

ModuleBriefRelationshipSummary ModuleBriefService::relationshipSummary(
    const sym_list::SymbolInfo& moduleSymbol) const
{
    ModuleBriefRelationshipSummary summary;
    const QList<SemanticRelationshipResult> outgoing =
        semanticIndex()->getRelationshipResults(moduleSymbol.symbolId, true);
    const QList<SemanticRelationshipResult> incoming =
        semanticIndex()->getRelationshipResults(moduleSymbol.symbolId, false);

    summary.outgoingCount = outgoing.size();
    summary.incomingCount = incoming.size();
    summary.totalCount = summary.outgoingCount + summary.incomingCount;
    for (const SemanticRelationshipResult& relationship : outgoing)
        summary.outgoingTypeCounts[relationship.relationship.type]++;
    for (const SemanticRelationshipResult& relationship : incoming)
        summary.incomingTypeCounts[relationship.relationship.type]++;
    return summary;
}

bool ModuleBriefService::isInsideModule(
    const sym_list::SymbolInfo& symbol,
    const sym_list::SymbolInfo& moduleSymbol)
{
    if (symbol.symbolId == moduleSymbol.symbolId)
        return false;
    if (!moduleSymbol.symbolName.isEmpty()
        && symbol.moduleScope == moduleSymbol.symbolName) {
        return true;
    }
    if (symbol.fileName != moduleSymbol.fileName)
        return false;
    if (moduleSymbol.startLine <= 0 || symbol.startLine <= 0)
        return false;
    if (symbol.startLine < moduleSymbol.startLine)
        return false;
    return moduleSymbol.endLine <= 0 || symbol.startLine <= moduleSymbol.endLine;
}

QList<ModuleBriefSymbolRow> ModuleBriefService::symbolRows(
    const QList<sym_list::SymbolInfo>& symbols,
    const QString& sectionDisplayName)
{
    QList<ModuleBriefSymbolRow> rows;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        ModuleBriefSymbolRow row;
        row.symbol = symbol;
        row.sectionDisplayName = sectionDisplayName;
        row.typeDisplayName = symbolTypeDisplayName(symbol.symbolType);
        row.detailDisplayName = symbolDetailDisplayName(symbol);
        rows.append(row);
    }
    return rows;
}

QList<ModuleBriefDiagnosticRow> ModuleBriefService::diagnosticRows(
    const QList<SemanticDiagnostic>& diagnostics)
{
    QList<ModuleBriefDiagnosticRow> rows;
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        ModuleBriefDiagnosticRow row;
        row.diagnostic = diagnostic;
        row.severityDisplayName =
            diagnosticSeverityDisplayName(diagnostic.severity);
        row.detailDisplayName = QStringLiteral("diagnostic");
        rows.append(row);
    }
    return rows;
}

QString ModuleBriefService::symbolTypeDisplayName(sym_list::sym_type_e type)
{
    return SymbolTaxonomy::symbolTypeLabel(type);
}

QString ModuleBriefService::symbolDetailDisplayName(
    const sym_list::SymbolInfo& symbol)
{
    const QString type = symbolTypeDisplayName(symbol.symbolType);
    return symbol.dataType.isEmpty()
        ? type
        : QStringLiteral("%1 %2").arg(type, symbol.dataType);
}

QString ModuleBriefService::diagnosticSeverityDisplayName(
    SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

void ModuleBriefService::sortSymbols(QList<sym_list::SymbolInfo>& symbols)
{
    std::sort(symbols.begin(), symbols.end(),
              [](const sym_list::SymbolInfo& lhs,
                 const sym_list::SymbolInfo& rhs) {
                  if (lhs.fileName != rhs.fileName)
                      return lhs.fileName < rhs.fileName;
                  if (lhs.startLine != rhs.startLine)
                      return lhs.startLine < rhs.startLine;
                  if (lhs.startColumn != rhs.startColumn)
                      return lhs.startColumn < rhs.startColumn;
                  if (lhs.symbolName != rhs.symbolName)
                      return lhs.symbolName < rhs.symbolName;
                  return lhs.symbolId < rhs.symbolId;
              });
}
