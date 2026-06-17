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
    const sym_list::SymbolInfo moduleSymbol =
        resolveModule(query, &report.notFoundReason);
    if (moduleSymbol.symbolId < 0) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    report.found = true;
    report.notFoundReason = ModuleBriefNotFoundReason::None;
    report.moduleSymbol = moduleSymbol;
    report.moduleStableKey = symbolStableKeyForSymbol(moduleSymbol);
    const QList<sym_list::SymbolInfo> symbols = semanticIndex()->getSymbols();

    report.ports = symbolsInModule(
        moduleSymbol,
        symbols,
        SymbolTaxonomy::DeclarationGroup::Port);
    report.parameters = symbolsInModule(
        moduleSymbol,
        symbols,
        SymbolTaxonomy::DeclarationGroup::Parameter);
    report.instances = symbolsInModule(
        moduleSymbol,
        symbols,
        SymbolTaxonomy::DeclarationGroup::Instance);
    report.imports = importSymbols(moduleSymbol);
    report.diagnostics = diagnosticsForModule(moduleSymbol);
    report.portRows = symbolRows(report.ports, QStringLiteral("Port"));
    report.parameterRows = symbolRows(report.parameters, QStringLiteral("Parameter"));
    report.instanceRows = symbolRows(report.instances, QStringLiteral("Instance"));
    report.importRows = symbolRows(report.imports, QStringLiteral("Import"));
    report.diagnosticRows = diagnosticRows(report.diagnostics);
    report.contextRows = contextRows(report.imports,
                                     report.ports,
                                     report.instances,
                                     symbols);
    report.relationshipSummary = relationshipSummary(moduleSymbol);
    report.relationshipEvidenceRows = relationshipEvidenceRows(moduleSymbol);
    return report;
}

SemanticIndex* ModuleBriefService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

sym_list::SymbolInfo ModuleBriefService::resolveModule(
    const ModuleBriefQuery& query,
    ModuleBriefNotFoundReason* reason) const
{
    if (reason)
        *reason = ModuleBriefNotFoundReason::None;

    if (query.moduleStableKey.isValid()) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolByStableKey(query.moduleStableKey);
        if (symbol.symbolId < 0) {
            if (reason)
                *reason = ModuleBriefNotFoundReason::NoMatchingModule;
            return missingModuleBriefSymbol();
        }
        if (!SymbolTaxonomy::isModuleDeclaration(symbol)) {
            if (reason)
                *reason = ModuleBriefNotFoundReason::UnsupportedSymbolKind;
            return missingModuleBriefSymbol();
        }
        return symbol;
    }

    if (query.moduleSymbolId >= 0) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolById(query.moduleSymbolId);
        if (symbol.symbolId < 0) {
            if (reason)
                *reason = ModuleBriefNotFoundReason::NoMatchingModule;
            return missingModuleBriefSymbol();
        }
        if (!SymbolTaxonomy::isModuleDeclaration(symbol)) {
            if (reason)
                *reason = ModuleBriefNotFoundReason::UnsupportedSymbolKind;
            return missingModuleBriefSymbol();
        }
        return symbol;
    }
    if (query.moduleName.isEmpty()) {
        if (reason)
            *reason = ModuleBriefNotFoundReason::EmptyModuleName;
        return missingModuleBriefSymbol();
    }

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.moduleName;
    definitionQuery.fileName = query.fileName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    if (!definition.found) {
        if (reason)
            *reason = ModuleBriefNotFoundReason::NoMatchingModule;
        return missingModuleBriefSymbol();
    }
    if (!SymbolTaxonomy::isModuleDeclaration(definition.symbol)) {
        if (reason)
            *reason = ModuleBriefNotFoundReason::UnsupportedSymbolKind;
        return missingModuleBriefSymbol();
    }
    return definition.symbol;
}

QList<sym_list::SymbolInfo> ModuleBriefService::symbolsInModule(
    const sym_list::SymbolInfo& moduleSymbol,
    const QList<sym_list::SymbolInfo>& symbols,
    SymbolTaxonomy::DeclarationGroup group) const
{
    QList<sym_list::SymbolInfo> result;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (SymbolTaxonomy::declarationGroup(
                SymbolTaxonomy::semanticMetadata(symbol)) != group) {
            continue;
        }
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
        if (!SymbolTaxonomy::isPackageDeclaration(
                SymbolTaxonomy::semanticMetadata(relationship.toSymbol))) {
            continue;
        }
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
    summary.rows = relationshipRows(summary);
    return summary;
}

QList<ModuleBriefRelationshipEvidenceRow> ModuleBriefService::relationshipEvidenceRows(
    const sym_list::SymbolInfo& moduleSymbol) const
{
    QList<ModuleBriefRelationshipEvidenceRow> rows;
    QSet<QString> seen;
    auto appendRows = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            semanticIndex()->getRelationshipResults(moduleSymbol.symbolId, outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            const sym_list::SymbolInfo peer =
                outgoing ? relationship.toSymbol : relationship.fromSymbol;
            if (peer.symbolId < 0)
                continue;
            const QString key = QStringLiteral("%1:%2:%3")
                                    .arg(relationship.relationship.fromId)
                                    .arg(relationship.relationship.toId)
                                    .arg(static_cast<int>(relationship.relationship.type));
            if (seen.contains(key))
                continue;
            seen.insert(key);

            ModuleBriefRelationshipEvidenceRow row;
            row.relationship = relationship;
            row.peerSymbol = peer;
            row.outgoing = outgoing;
            fillRelationshipEvidenceMetadata(row);
            rows.append(row);
        }
    };

    appendRows(true);
    appendRows(false);
    sortRelationshipEvidenceRows(rows);
    return rows;
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
        row.codeLink = RtlInsightLink::fromSymbol(symbol);
        row.sectionDisplayName = sectionDisplayName;
        row.typeDisplayName = symbolTypeDisplayName(symbol);
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
        row.codeLink = RtlInsightLink::fromDiagnostic(diagnostic);
        row.severityDisplayName =
            diagnosticSeverityDisplayName(diagnostic.severity);
        row.detailDisplayName = QStringLiteral("diagnostic");
        row.sourceRoleDisplayName =
            SymbolTaxonomy::sourceRoleDisplayName(
                SymbolTaxonomy::sourceRoleForFileName(diagnostic.fileName));
        rows.append(row);
    }
    return rows;
}

QList<ModuleBriefRelationshipRow> ModuleBriefService::relationshipRows(
    const ModuleBriefRelationshipSummary& summary)
{
    QList<ModuleBriefRelationshipRow> rows;
    auto appendRows = [&rows](bool outgoing,
                              const QMap<SymbolRelationshipEngine::RelationType, int>& counts) {
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            ModuleBriefRelationshipRow row;
            row.type = it.key();
            row.count = it.value();
            row.directionDisplayName = relationshipDirectionDisplayName(outgoing);
            row.typeDisplayName = relationshipTypeDisplayName(row.type);
            row.detailDisplayName = relationshipDetailDisplayName(row.count);
            rows.append(row);
        }
    };
    appendRows(true, summary.outgoingTypeCounts);
    appendRows(false, summary.incomingTypeCounts);
    return rows;
}

QList<ModuleBriefContextRow> ModuleBriefService::contextRows(
    const QList<sym_list::SymbolInfo>& imports,
    const QList<sym_list::SymbolInfo>& ports,
    const QList<sym_list::SymbolInfo>& instances,
    const QList<sym_list::SymbolInfo>& allSymbols)
{
    QList<ModuleBriefContextRow> rows;
    QSet<QString> seen;
    const QSet<QString> interfaces = interfaceNames(allSymbols);

    auto appendRow = [&](const QString& section,
                         const QString& kind,
                         const sym_list::SymbolInfo& symbol) {
        const QString key = QStringLiteral("%1:%2:%3:%4:%5")
                                .arg(section)
                                .arg(symbol.symbolId)
                                .arg(symbol.moduleScope,
                                     symbol.symbolName,
                                     symbol.dataType);
        if (seen.contains(key))
            return;
        seen.insert(key);

        ModuleBriefContextRow row;
        row.symbol = symbol;
        row.codeLink = RtlInsightLink::fromSymbol(symbol);
        row.sectionDisplayName = section;
        row.symbolDisplayName = symbolDisplayName(symbol);
        row.contextKindDisplayName = kind;
        row.symbolTypeDisplayName = symbolTypeDisplayName(symbol);
        row.detailDisplayName = contextDetailDisplayName(kind, symbol);
        row.sourceRoleDisplayName =
            SymbolTaxonomy::sourceRoleDisplayName(
                SymbolTaxonomy::semanticMetadata(symbol).sourceRole);
        rows.append(row);
    };

    for (const sym_list::SymbolInfo& packageSymbol : imports) {
        appendRow(QStringLiteral("Package"), QStringLiteral("package import"), packageSymbol);
        QList<sym_list::SymbolInfo> packageMembers;
        for (const sym_list::SymbolInfo& symbol : allSymbols) {
            if (symbol.moduleScope != packageSymbol.symbolName)
                continue;
            if (!SymbolTaxonomy::isPackageVisibleDefinition(
                    SymbolTaxonomy::semanticMetadata(symbol))) {
                continue;
            }
            packageMembers.append(symbol);
        }
        sortSymbols(packageMembers);
        for (const sym_list::SymbolInfo& symbol : packageMembers) {
            appendRow(QStringLiteral("Package Member"),
                      QStringLiteral("package %1")
                          .arg(symbolTypeDisplayName(symbol)),
                      symbol);
        }
    }

    for (const sym_list::SymbolInfo& port : ports) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            SymbolTaxonomy::semanticMetadata(port);
        if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Port
            && metadata.interfaceLikeOwner) {
            appendRow(QStringLiteral("Interface"), QStringLiteral("interface port"), port);
        }
    }

    for (const sym_list::SymbolInfo& instance : instances) {
        const QString interfaceName = SymbolTaxonomy::interfaceTypeName(instance);
        if (!interfaceName.isEmpty() && interfaces.contains(interfaceName)) {
            appendRow(QStringLiteral("Interface"),
                      QStringLiteral("interface instance"),
                      instance);
        }
    }

    return rows;
}

QString ModuleBriefService::symbolTypeDisplayName(
    const sym_list::SymbolInfo& symbol)
{
    return SymbolTaxonomy::symbolTypeLabel(
        SymbolTaxonomy::semanticMetadata(symbol));
}

QString ModuleBriefService::symbolDetailDisplayName(
    const sym_list::SymbolInfo& symbol)
{
    const QString type = symbolTypeDisplayName(symbol);
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

QString ModuleBriefService::symbolDisplayName(const sym_list::SymbolInfo& symbol)
{
    return symbol.symbolName.isEmpty()
        ? QStringLiteral("<unnamed>")
        : symbol.symbolName;
}

QString ModuleBriefService::notFoundReasonDisplayName(
    ModuleBriefNotFoundReason reason)
{
    switch (reason) {
    case ModuleBriefNotFoundReason::None:
        return QString();
    case ModuleBriefNotFoundReason::EmptyModuleName:
        return QStringLiteral("empty module name");
    case ModuleBriefNotFoundReason::NoMatchingModule:
        return QStringLiteral("no matching module");
    case ModuleBriefNotFoundReason::UnsupportedSymbolKind:
        return QStringLiteral("unsupported symbol kind");
    }
    return QStringLiteral("module brief unavailable");
}

QString ModuleBriefService::provenanceDisplayName(
    RelationshipProvenance provenance)
{
    switch (provenance) {
    case RelationshipProvenance::SlangExtracted:
        return QStringLiteral("slang extracted");
    case RelationshipProvenance::Inferred:
        return QStringLiteral("inferred");
    case RelationshipProvenance::LexicalFallback:
        return QStringLiteral("lexical fallback");
    case RelationshipProvenance::OpenDocument:
        return QStringLiteral("open document");
    case RelationshipProvenance::Workspace:
        return QStringLiteral("workspace");
    case RelationshipProvenance::FeatureGenerated:
        return QStringLiteral("feature generated");
    case RelationshipProvenance::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

QString ModuleBriefService::confidenceDisplayName(int confidence)
{
    return confidence > 0
        ? QStringLiteral("%1%").arg(confidence)
        : QStringLiteral("unknown");
}

QString ModuleBriefService::evidenceDisplayName(const QString& evidenceText)
{
    return evidenceText.isEmpty()
        ? QStringLiteral("no evidence detail")
        : evidenceText;
}

QString ModuleBriefService::contextDetailDisplayName(
    const QString& kind,
    const sym_list::SymbolInfo& symbol)
{
    return symbol.dataType.isEmpty()
        ? kind
        : QStringLiteral("%1 %2").arg(kind, symbol.dataType);
}

QSet<QString> ModuleBriefService::interfaceNames(
    const QList<sym_list::SymbolInfo>& symbols)
{
    QSet<QString> names;
    for (const sym_list::SymbolInfo& symbol : symbols) {
        if (SymbolTaxonomy::semanticMetadata(symbol).declarationKind
                == SymbolTaxonomy::DeclarationKind::Interface
            && !symbol.symbolName.isEmpty()) {
            names.insert(symbol.symbolName);
        }
    }
    return names;
}

QString ModuleBriefService::relationshipDirectionDisplayName(bool outgoing)
{
    return outgoing ? QStringLiteral("Outgoing") : QStringLiteral("Incoming");
}

QString ModuleBriefService::relationshipTypeDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("Contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("References");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("Instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("Calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("Inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("Implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("Assigns To");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("Reads From");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("Clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("Resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("Generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("Constrains");
    }
    return QStringLiteral("Relationship");
}

QString ModuleBriefService::relationshipDetailDisplayName(int count)
{
    return QStringLiteral("%1 relationships").arg(count);
}

QString ModuleBriefService::relationshipEvidenceDetailDisplayName(
    const ModuleBriefRelationshipEvidenceRow& row)
{
    return QStringLiteral("%1 %2")
        .arg(row.directionDisplayName, row.typeDisplayName);
}

void ModuleBriefService::fillRelationshipEvidenceMetadata(
    ModuleBriefRelationshipEvidenceRow& row)
{
    row.fromSymbol = row.relationship.fromSymbol;
    row.toSymbol = row.relationship.toSymbol;
    row.fromStableKey = row.relationship.fromStableKey;
    row.toStableKey = row.relationship.toStableKey;
    row.peerStableKey = row.outgoing
        ? row.relationship.toStableKey
        : row.relationship.fromStableKey;
    row.peerCodeLink = RtlInsightLink::fromSymbol(row.peerSymbol);
    row.fromCodeLink = RtlInsightLink::fromSymbol(row.fromSymbol);
    row.toCodeLink = RtlInsightLink::fromSymbol(row.toSymbol);
    row.provenance = row.relationship.provenance;
    row.confidence = row.relationship.confidence;
    row.evidenceText = row.relationship.evidenceText;
    row.directionDisplayName = relationshipDirectionDisplayName(row.outgoing);
    row.typeDisplayName = relationshipTypeDisplayName(row.relationship.relationship.type);
    row.provenanceDisplayName = provenanceDisplayName(row.provenance);
    row.confidenceDisplayName = confidenceDisplayName(row.confidence);
    row.evidenceDisplayName = evidenceDisplayName(row.evidenceText);
    row.peerDisplayName = symbolDisplayName(row.peerSymbol);
    row.fromSymbolDisplayName = symbolDisplayName(row.fromSymbol);
    row.toSymbolDisplayName = symbolDisplayName(row.toSymbol);
    row.detailDisplayName = relationshipEvidenceDetailDisplayName(row);
}

void ModuleBriefService::sortRelationshipEvidenceRows(
    QList<ModuleBriefRelationshipEvidenceRow>& rows)
{
    std::sort(rows.begin(),
              rows.end(),
              [](const ModuleBriefRelationshipEvidenceRow& lhs,
                 const ModuleBriefRelationshipEvidenceRow& rhs) {
                  const sym_list::SymbolInfo& left = lhs.peerSymbol;
                  const sym_list::SymbolInfo& right = rhs.peerSymbol;
                  if (lhs.outgoing != rhs.outgoing)
                      return lhs.outgoing && !rhs.outgoing;
                  if (lhs.relationship.relationship.type
                      != rhs.relationship.relationship.type) {
                      return lhs.relationship.relationship.type
                          < rhs.relationship.relationship.type;
                  }
                  if (left.fileName != right.fileName)
                      return left.fileName < right.fileName;
                  if (left.startLine != right.startLine)
                      return left.startLine < right.startLine;
                  if (left.startColumn != right.startColumn)
                      return left.startColumn < right.startColumn;
                  if (left.symbolName != right.symbolName)
                      return left.symbolName < right.symbolName;
                  return left.symbolId < right.symbolId;
              });
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
