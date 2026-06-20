#include "modulebriefservice.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<ModuleBriefService> ModuleBriefService::instance = nullptr;

namespace {
SemanticSymbolRecord missingModuleBriefRecord()
{
    return {};
}

SymbolTaxonomy::SemanticMetadata semanticMetadataForRecord(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
}

RtlInsightCodeLink codeLinkForRecord(const SemanticSymbolRecord& record)
{
    return RtlInsightLink::fromFileLine(record.location.fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
}

bool isInsideModule(const SemanticSymbolRecord& record,
                    const SemanticSymbolRecord& moduleRecord);
QSet<QString> interfaceNames(const QList<SemanticSymbolRecord>& records);
void sortRecords(QList<SemanticSymbolRecord>& records);

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
    const SemanticSymbolRecord moduleRecord =
        resolveModule(query, &report.notFoundReason);
    if (moduleRecord.localHandle < 0) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    report.found = true;
    report.notFoundReason = ModuleBriefNotFoundReason::None;
    report.moduleSymbolRecord = moduleRecord;
    report.moduleStableKey = report.moduleSymbolRecord.stableKey;
    report.moduleDisplayName = symbolDisplayName(report.moduleSymbolRecord);
    const QList<SemanticSymbolRecord> records =
        semanticIndex()->getSymbolRecords();

    const QList<SemanticSymbolRecord> ports = symbolsInModule(
        moduleRecord,
        records,
        SymbolTaxonomy::DeclarationGroup::Port);
    const QList<SemanticSymbolRecord> parameters = symbolsInModule(
        moduleRecord,
        records,
        SymbolTaxonomy::DeclarationGroup::Parameter);
    const QList<SemanticSymbolRecord> instances = symbolsInModule(
        moduleRecord,
        records,
        SymbolTaxonomy::DeclarationGroup::Instance);
    const QList<SemanticSymbolRecord> imports =
        importSymbols(report.moduleStableKey);
    report.diagnostics = diagnosticsForModule(moduleRecord);
    report.portRows = symbolRows(ports, QStringLiteral("Port"));
    report.parameterRows = symbolRows(parameters, QStringLiteral("Parameter"));
    report.instanceRows = symbolRows(instances, QStringLiteral("Instance"));
    report.importRows = symbolRows(imports, QStringLiteral("Import"));
    report.diagnosticRows = diagnosticRows(report.diagnostics);
    report.contextRows = contextRows(imports,
                                     ports,
                                     instances,
                                     records);
    report.relationshipSummary = relationshipSummary(report.moduleStableKey);
    report.relationshipEvidenceRows =
        relationshipEvidenceRows(report.moduleStableKey);
    return report;
}

SemanticIndex* ModuleBriefService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

SemanticSymbolRecord ModuleBriefService::resolveModule(
    const ModuleBriefQuery& query,
    ModuleBriefNotFoundReason* reason) const
{
    if (reason)
        *reason = ModuleBriefNotFoundReason::None;

    if (query.moduleStableKey.isValid()) {
        const SemanticSymbolRecord record =
            semanticIndex()->getSymbolRecordByStableKey(query.moduleStableKey);
        if (!record.isValid()) {
            if (reason)
                *reason = ModuleBriefNotFoundReason::NoMatchingModule;
            return missingModuleBriefRecord();
        }
        if (!SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForRecord(record))) {
            if (reason)
                *reason = ModuleBriefNotFoundReason::UnsupportedSymbolKind;
            return missingModuleBriefRecord();
        }
        return record;
    }

    if (query.moduleName.isEmpty()) {
        if (reason)
            *reason = ModuleBriefNotFoundReason::EmptyModuleName;
        return missingModuleBriefRecord();
    }

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.moduleName;
    definitionQuery.fileName = query.fileName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    if (!definition.found) {
        if (reason)
            *reason = ModuleBriefNotFoundReason::NoMatchingModule;
        return missingModuleBriefRecord();
    }
    if (!SymbolTaxonomy::isModuleDeclaration(
            semanticMetadataForRecord(definition.symbolRecord))) {
        if (reason)
            *reason = ModuleBriefNotFoundReason::UnsupportedSymbolKind;
        return missingModuleBriefRecord();
    }
    const SemanticSymbolRecord record = definition.symbolRecord;
    if (record.localHandle < 0)
        return missingModuleBriefRecord();
    return record;
}

QList<SemanticSymbolRecord> ModuleBriefService::symbolsInModule(
    const SemanticSymbolRecord& moduleRecord,
    const QList<SemanticSymbolRecord>& records,
    SymbolTaxonomy::DeclarationGroup group) const
{
    QList<SemanticSymbolRecord> result;
    for (const SemanticSymbolRecord& record : records) {
        if (SymbolTaxonomy::declarationGroup(
                semanticMetadataForRecord(record)) != group) {
            continue;
        }
        if (!isInsideModule(record, moduleRecord))
            continue;
        result.append(record);
    }
    sortRecords(result);
    return result;
}

QList<SemanticSymbolRecord> ModuleBriefService::importSymbols(
    const SymbolStableKey& moduleStableKey) const
{
    QList<SemanticSymbolRecord> result;
    QSet<int> seen;
    const QList<SemanticRelationshipResult> relationships =
        moduleStableKey.isValid()
            ? semanticIndex()->getRelationshipResults(moduleStableKey, true)
            : QList<SemanticRelationshipResult>();
    for (const SemanticRelationshipResult& relationship : relationships) {
        const SemanticSymbolRecord packageRecord = relationship.toSymbolRecord;
        if (!SymbolTaxonomy::isPackageDeclaration(
                semanticMetadataForRecord(packageRecord))) {
            continue;
        }
        const int localHandle = packageRecord.localHandle;
        if (localHandle < 0)
            continue;
        if (seen.contains(localHandle))
            continue;
        seen.insert(localHandle);
        result.append(packageRecord);
    }
    sortRecords(result);
    return result;
}

QList<SemanticDiagnostic> ModuleBriefService::diagnosticsForModule(
    const SemanticSymbolRecord& moduleRecord) const
{
    QList<SemanticDiagnostic> result;
    const QList<SemanticDiagnostic> diagnostics =
        semanticIndex()->getDiagnostics(moduleRecord.location.fileName);
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.line <= 0) {
            result.append(diagnostic);
            continue;
        }
        const bool afterStart =
            diagnostic.line >= moduleRecord.location.startLine;
        const bool beforeEnd = moduleRecord.location.endLine <= 0
            || diagnostic.line <= moduleRecord.location.endLine;
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
    const SymbolStableKey& moduleStableKey) const
{
    ModuleBriefRelationshipSummary summary;
    const QList<SemanticRelationshipResult> outgoing =
        moduleStableKey.isValid()
            ? semanticIndex()->getRelationshipResults(moduleStableKey, true)
            : QList<SemanticRelationshipResult>();
    const QList<SemanticRelationshipResult> incoming =
        moduleStableKey.isValid()
            ? semanticIndex()->getRelationshipResults(moduleStableKey, false)
            : QList<SemanticRelationshipResult>();

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
    const SymbolStableKey& moduleStableKey) const
{
    QList<ModuleBriefRelationshipEvidenceRow> rows;
    QSet<QString> seen;
    auto appendRows = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            moduleStableKey.isValid()
                ? semanticIndex()->getRelationshipResults(moduleStableKey, outgoing)
                : QList<SemanticRelationshipResult>();
        for (const SemanticRelationshipResult& relationship : relationships) {
            const SemanticSymbolRecord peerRecord = outgoing
                ? relationship.toSymbolRecord
                : relationship.fromSymbolRecord;
            if (!peerRecord.isValid())
                continue;
            QString key = semanticRelationshipStableKeyText(relationship.relationship);
            if (key.isEmpty()) {
                key = QStringLiteral("%1:%2:%3")
                          .arg(relationship.relationship.fromId)
                          .arg(relationship.relationship.toId)
                          .arg(static_cast<int>(relationship.relationship.type));
            }
            if (seen.contains(key))
                continue;
            seen.insert(key);

            ModuleBriefRelationshipEvidenceRow row;
            row.outgoing = outgoing;
            row.peerSymbolRecord = peerRecord;
            fillRelationshipEvidenceMetadata(row, relationship);
            rows.append(row);
        }
    };

    appendRows(true);
    appendRows(false);
    sortRelationshipEvidenceRows(rows);
    return rows;
}

namespace {

bool isInsideModule(
    const SemanticSymbolRecord& record,
    const SemanticSymbolRecord& moduleRecord)
{
    if (record.localHandle == moduleRecord.localHandle) {
        return false;
    }

    if (!moduleRecord.name.isEmpty()
        && record.owner.name == moduleRecord.name) {
        return true;
    }
    if (record.location.fileName != moduleRecord.location.fileName)
        return false;
    if (moduleRecord.location.startLine <= 0
        || record.location.startLine <= 0)
        return false;
    if (record.location.startLine < moduleRecord.location.startLine)
        return false;
    return moduleRecord.location.endLine <= 0
        || record.location.startLine <= moduleRecord.location.endLine;
}

}

QList<ModuleBriefSymbolRow> ModuleBriefService::symbolRows(
    const QList<SemanticSymbolRecord>& records,
    const QString& sectionDisplayName)
{
    QList<ModuleBriefSymbolRow> rows;
    for (const SemanticSymbolRecord& record : records) {
        ModuleBriefSymbolRow row;
        row.symbolRecord = record;
        row.symbolStableKey = record.stableKey;
        row.codeLink = codeLinkForRecord(record);
        row.sectionDisplayName = sectionDisplayName;
        row.symbolDisplayName = symbolDisplayName(record);
        row.typeDisplayName = symbolTypeDisplayName(record);
        row.detailDisplayName = symbolDetailDisplayName(record);
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
    const QList<SemanticSymbolRecord>& imports,
    const QList<SemanticSymbolRecord>& ports,
    const QList<SemanticSymbolRecord>& instances,
    const QList<SemanticSymbolRecord>& allRecords)
{
    QList<ModuleBriefContextRow> rows;
    QSet<QString> seen;
    const QSet<QString> interfaces = interfaceNames(allRecords);

    auto appendRow = [&](const QString& section,
                         const QString& kind,
                         const SemanticSymbolRecord& record) {
        const QString key = QStringLiteral("%1:%2:%3:%4:%5")
                                .arg(section)
                                .arg(record.localHandle)
                                .arg(record.owner.name,
                                     record.name,
                                     record.type.rawTypeText);
        if (seen.contains(key))
            return;
        seen.insert(key);

        ModuleBriefContextRow row;
        row.symbolRecord = record;
        row.symbolStableKey = record.stableKey;
        row.codeLink = codeLinkForRecord(record);
        row.sectionDisplayName = section;
        row.symbolDisplayName = symbolDisplayName(record);
        row.contextKindDisplayName = kind;
        row.symbolTypeDisplayName = symbolTypeDisplayName(record);
        row.detailDisplayName = contextDetailDisplayName(kind, record);
        row.sourceRoleDisplayName =
            SymbolTaxonomy::sourceRoleDisplayName(
                record.sourceRole);
        rows.append(row);
    };

    for (const SemanticSymbolRecord& packageRecord : imports) {
        appendRow(QStringLiteral("Package"),
                  QStringLiteral("package import"),
                  packageRecord);
        QList<SemanticSymbolRecord> packageMembers;
        for (const SemanticSymbolRecord& record : allRecords) {
            if (record.owner.name != packageRecord.name)
                continue;
            if (!SymbolTaxonomy::isPackageVisibleDefinition(
                    semanticMetadataForRecord(record))) {
                continue;
            }
            packageMembers.append(record);
        }
        sortRecords(packageMembers);
        for (const SemanticSymbolRecord& record : packageMembers) {
            appendRow(QStringLiteral("Package Member"),
                      QStringLiteral("package %1")
                          .arg(symbolTypeDisplayName(record)),
                      record);
        }
    }

    for (const SemanticSymbolRecord& port : ports) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForRecord(port);
        if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Port
            && metadata.interfaceLikeOwner) {
            appendRow(QStringLiteral("Interface"), QStringLiteral("interface port"), port);
        }
    }

    for (const SemanticSymbolRecord& instance : instances) {
        const QString interfaceName =
            SymbolTaxonomy::interfaceTypeName(instance.type.rawTypeText);
        if (!interfaceName.isEmpty() && interfaces.contains(interfaceName)) {
            appendRow(QStringLiteral("Interface"),
                      QStringLiteral("interface instance"),
                      instance);
        }
    }

    return rows;
}

QString ModuleBriefService::symbolTypeDisplayName(
    const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::symbolTypeLabel(
        semanticMetadataForRecord(record));
}

QString ModuleBriefService::symbolDetailDisplayName(
    const SemanticSymbolRecord& record)
{
    const QString type = symbolTypeDisplayName(record);
    return record.type.rawTypeText.isEmpty()
        ? type
        : QStringLiteral("%1 %2").arg(type, record.type.rawTypeText);
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

QString ModuleBriefService::symbolDisplayName(
    const SemanticSymbolRecord& record)
{
    return record.name.isEmpty()
        ? QStringLiteral("<unnamed>")
        : record.name;
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
    const SemanticSymbolRecord& record)
{
    return record.type.rawTypeText.isEmpty()
        ? kind
        : QStringLiteral("%1 %2").arg(kind, record.type.rawTypeText);
}

namespace {

QSet<QString> interfaceNames(const QList<SemanticSymbolRecord>& records)
{
    QSet<QString> names;
    for (const SemanticSymbolRecord& record : records) {
        if (semanticMetadataForSymbolRecord(record).declarationKind
                == SymbolTaxonomy::DeclarationKind::Interface
            && !record.name.isEmpty()) {
            names.insert(record.name);
        }
    }
    return names;
}

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
    ModuleBriefRelationshipEvidenceRow& row,
    const SemanticRelationshipResult& relationship)
{
    row.type = relationship.relationship.type;
    row.fromSymbolRecord = relationship.fromSymbolRecord;
    row.toSymbolRecord = relationship.toSymbolRecord;
    row.fromStableKey = row.fromSymbolRecord.stableKey.isValid()
        ? row.fromSymbolRecord.stableKey
        : relationship.fromStableKey;
    row.toStableKey = row.toSymbolRecord.stableKey.isValid()
        ? row.toSymbolRecord.stableKey
        : relationship.toStableKey;
    row.peerStableKey = row.outgoing
        ? row.toStableKey
        : row.fromStableKey;
    if (!row.peerSymbolRecord.stableKey.isValid()) {
        row.peerSymbolRecord = row.outgoing
            ? row.toSymbolRecord
            : row.fromSymbolRecord;
    }
    row.peerCodeLink = codeLinkForRecord(row.peerSymbolRecord);
    row.fromCodeLink = codeLinkForRecord(row.fromSymbolRecord);
    row.toCodeLink = codeLinkForRecord(row.toSymbolRecord);
    row.provenance = relationship.provenance;
    row.confidence = relationship.confidence;
    row.evidenceText = relationship.evidenceText;
    row.directionDisplayName = relationshipDirectionDisplayName(row.outgoing);
    row.typeDisplayName = relationshipTypeDisplayName(row.type);
    row.provenanceDisplayName = provenanceDisplayName(row.provenance);
    row.confidenceDisplayName = confidenceDisplayName(row.confidence);
    row.evidenceDisplayName = evidenceDisplayName(row.evidenceText);
    row.peerDisplayName = symbolDisplayName(row.peerSymbolRecord);
    row.fromSymbolDisplayName = symbolDisplayName(row.fromSymbolRecord);
    row.toSymbolDisplayName = symbolDisplayName(row.toSymbolRecord);
    row.detailDisplayName = relationshipEvidenceDetailDisplayName(row);
}

void ModuleBriefService::sortRelationshipEvidenceRows(
    QList<ModuleBriefRelationshipEvidenceRow>& rows)
{
    std::sort(rows.begin(),
              rows.end(),
              [](const ModuleBriefRelationshipEvidenceRow& lhs,
                 const ModuleBriefRelationshipEvidenceRow& rhs) {
                  if (lhs.outgoing != rhs.outgoing)
                      return lhs.outgoing && !rhs.outgoing;
                  if (lhs.type != rhs.type) {
                      return lhs.type < rhs.type;
                  }
                  if (lhs.peerSymbolRecord.location.fileName
                      != rhs.peerSymbolRecord.location.fileName) {
                      return lhs.peerSymbolRecord.location.fileName
                          < rhs.peerSymbolRecord.location.fileName;
                  }
                  if (lhs.peerSymbolRecord.location.startLine
                      != rhs.peerSymbolRecord.location.startLine) {
                      return lhs.peerSymbolRecord.location.startLine
                          < rhs.peerSymbolRecord.location.startLine;
                  }
                  if (lhs.peerSymbolRecord.location.startColumn
                      != rhs.peerSymbolRecord.location.startColumn) {
                      return lhs.peerSymbolRecord.location.startColumn
                          < rhs.peerSymbolRecord.location.startColumn;
                  }
                  if (lhs.peerSymbolRecord.name != rhs.peerSymbolRecord.name)
                      return lhs.peerSymbolRecord.name < rhs.peerSymbolRecord.name;
                  return lhs.peerSymbolRecord.localHandle
                      < rhs.peerSymbolRecord.localHandle;
              });
}

namespace {

void sortRecords(QList<SemanticSymbolRecord>& records)
{
    std::sort(records.begin(), records.end(),
              [](const SemanticSymbolRecord& lhs,
                 const SemanticSymbolRecord& rhs) {
                  if (lhs.location.fileName != rhs.location.fileName)
                      return lhs.location.fileName < rhs.location.fileName;
                  if (lhs.location.startLine != rhs.location.startLine)
                      return lhs.location.startLine < rhs.location.startLine;
                  if (lhs.location.startColumn != rhs.location.startColumn)
                      return lhs.location.startColumn < rhs.location.startColumn;
                  if (lhs.name != rhs.name)
                      return lhs.name < rhs.name;
                  return lhs.localHandle < rhs.localHandle;
              });
}

}
