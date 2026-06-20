#include "signaljourneyservice.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<SignalJourneyService> SignalJourneyService::instance = nullptr;

namespace {
SemanticSymbolRecord missingSignalJourneyRecord()
{
    return {};
}

QString relationshipTypeDisplayName(SymbolRelationshipEngine::RelationType type)
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

RtlInsightCodeLink codeLinkForRecord(const SemanticSymbolRecord& record)
{
    return RtlInsightLink::fromFileLine(record.location.fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
}

QString symbolDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.name.isEmpty())
        return record.name;
    return QStringLiteral("<unknown>");
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
}

QString typeDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::symbolTypeLabel(metadataForRecord(record));
}

QString sourceRoleDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::sourceRoleDisplayName(
        metadataForRecord(record).sourceRole);
}

QString interfaceBaseDisplayNameForRecord(const SemanticSymbolRecord& record)
{
    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;
    return SymbolTaxonomy::interfaceTypeName(record.type.rawTypeText);
}

SymbolStableKey signalJourneyStableKeyForRecord(
    const SemanticSymbolRecord& record)
{
    return record.stableKey;
}

QList<SemanticRelationshipResult> signalJourneyRelationshipResultsForRecord(
    SemanticIndex* index,
    const SemanticSymbolRecord& record,
    bool outgoing)
{
    if (!index)
        return {};
    const SymbolStableKey stableKey = signalJourneyStableKeyForRecord(record);
    return stableKey.isValid()
        ? index->getRelationshipResults(stableKey, outgoing)
        : QList<SemanticRelationshipResult>();
}

bool isInterfaceConnectionPeer(const SemanticSymbolRecord& record,
                               const QSet<QString>& interfaceNames)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(record);
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Modport
        || (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Port
            && metadata.interfaceLikeOwner)) {
        return true;
    }
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Instance
        && metadata.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration) {
        const QString interfaceName =
            interfaceBaseDisplayNameForRecord(record);
        return !interfaceName.isEmpty() && interfaceNames.contains(interfaceName);
    }
    return !record.owner.name.isEmpty()
        && interfaceNames.contains(record.owner.name)
        && SymbolTaxonomy::isDefinitionCandidate(metadata);
}

bool isJourneyDeclaration(const SemanticSymbolRecord& record,
                          const QSet<QString>& interfaceNames)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(record);
    if (SymbolTaxonomy::isSignalDeclaration(metadata)
        || SymbolTaxonomy::isPortDeclaration(metadata)) {
        return true;
    }
    return isInterfaceConnectionPeer(record, interfaceNames);
}
}

SignalJourneyService* SignalJourneyService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SignalJourneyService>();
    return instance.get();
}

SignalJourneyService::SignalJourneyService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

SignalJourneyService::~SignalJourneyService() = default;

void SignalJourneyService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

SignalJourneyReport SignalJourneyService::buildSignalJourney(
    const SignalJourneyQuery& query) const
{
    SignalJourneyReport report;
    const SemanticSymbolRecord signal =
        resolveSignal(query, &report.notFoundReason);
    if (signal.localHandle < 0) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    report.found = true;
    report.notFoundReason = SignalJourneyNotFoundReason::None;
    report.declarationSymbolRecord = signal;
    fillDeclarationDisplayMetadata(report, signal);
    report.assignments = relationshipItems(
        signal,
        false,
        {SymbolRelationshipEngine::ASSIGNS_TO});
    report.reads = relationshipItems(
        signal,
        false,
        {SymbolRelationshipEngine::READS_FROM});
    report.portConnections = portConnectionItems(signal);
    report.interfaceConnections = interfaceConnectionItems(signal);
    report.timingConnections = timingConnectionItems(signal);
    return report;
}

SemanticIndex* SignalJourneyService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

SemanticSymbolRecord SignalJourneyService::resolveSignal(
    const SignalJourneyQuery& query,
    SignalJourneyNotFoundReason* reason) const
{
    if (reason)
        *reason = SignalJourneyNotFoundReason::None;

    if (query.signalStableKey.isValid()) {
        const SemanticSymbolRecord record =
            semanticIndex()->getSymbolRecordByStableKey(query.signalStableKey);
        if (!record.isValid()) {
            if (reason)
                *reason = SignalJourneyNotFoundReason::NoMatchingSignal;
            return missingSignalJourneyRecord();
        }
        if (!isJourneyDeclaration(record, interfaceNames())) {
            if (reason)
                *reason = SignalJourneyNotFoundReason::UnsupportedSymbolKind;
            return missingSignalJourneyRecord();
        }
        return record;
    }

    if (query.signalName.isEmpty()) {
        if (reason)
            *reason = SignalJourneyNotFoundReason::EmptySignalName;
        return missingSignalJourneyRecord();
    }

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.signalName;
    definitionQuery.fileName = query.fileName;
    definitionQuery.moduleName = query.moduleName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    if (!definition.found) {
        if (reason)
            *reason = SignalJourneyNotFoundReason::NoMatchingSignal;
        return missingSignalJourneyRecord();
    }
    if (!isJourneyDeclaration(
            definition.symbolRecord,
            interfaceNames())) {
        if (reason)
            *reason = SignalJourneyNotFoundReason::UnsupportedSymbolKind;
        return missingSignalJourneyRecord();
    }
    if (definition.symbolRecord.localHandle < 0)
        return missingSignalJourneyRecord();
    return definition.symbolRecord;
}

QList<SignalJourneyItem> SignalJourneyService::relationshipItems(
    const SemanticSymbolRecord& signal,
    bool outgoing,
    const QList<SymbolRelationshipEngine::RelationType>& types) const
{
    QList<SignalJourneyItem> items;
    const QList<SemanticRelationshipResult> relationships =
        signalJourneyRelationshipResultsForRecord(semanticIndex(),
                                                  signal,
                                                  outgoing);
    for (const SemanticRelationshipResult& relationship : relationships) {
        if (!types.contains(relationship.relationship.type))
            continue;
        SignalJourneyItem item;
        item.outgoing = outgoing;
        const SemanticSymbolRecord peerRecord =
            outgoing ? relationship.toSymbolRecord : relationship.fromSymbolRecord;
        if (!peerRecord.isValid())
            continue;
        fillDisplayMetadata(item, relationship, peerRecord);
        items.append(item);
    }
    sortItems(items);
    return items;
}

QList<SignalJourneyItem> SignalJourneyService::portConnectionItems(
    const SemanticSymbolRecord& signal) const
{
    QList<SignalJourneyItem> items;
    QSet<QString> seen;
    auto appendDirection = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            signalJourneyRelationshipResultsForRecord(semanticIndex(),
                                                      signal,
                                                      outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            const SemanticSymbolRecord peerRecord =
                outgoing ? relationship.toSymbolRecord : relationship.fromSymbolRecord;
            if (!SymbolTaxonomy::isPortConnectionPeer(
                    metadataForRecord(peerRecord))) {
                continue;
            }
            const QString key = QStringLiteral("%1:%2:%3")
                                    .arg(relationship.relationship.fromId)
                                    .arg(relationship.relationship.toId)
                                    .arg(static_cast<int>(relationship.relationship.type));
            if (seen.contains(key))
                continue;
            seen.insert(key);

            SignalJourneyItem item;
            item.outgoing = outgoing;
            fillDisplayMetadata(item, relationship, peerRecord);
            items.append(item);
        }
    };
    appendDirection(false);
    appendDirection(true);
    sortItems(items);
    return items;
}

QList<SignalJourneyItem> SignalJourneyService::interfaceConnectionItems(
    const SemanticSymbolRecord& signal) const
{
    QList<SignalJourneyItem> items;
    QSet<QString> seen;
    const QSet<QString> interfaces = interfaceNames();
    auto appendDirection = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            signalJourneyRelationshipResultsForRecord(semanticIndex(),
                                                      signal,
                                                      outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            const SemanticSymbolRecord peerRecord =
                outgoing ? relationship.toSymbolRecord : relationship.fromSymbolRecord;
            if (!isInterfaceConnectionPeer(peerRecord, interfaces))
                continue;
            const QString key = QStringLiteral("%1:%2:%3")
                                    .arg(relationship.relationship.fromId)
                                    .arg(relationship.relationship.toId)
                                    .arg(static_cast<int>(relationship.relationship.type));
            if (seen.contains(key))
                continue;
            seen.insert(key);

            SignalJourneyItem item;
            item.outgoing = outgoing;
            fillDisplayMetadata(item, relationship, peerRecord);
            fillInterfaceDisplayMetadata(item, peerRecord);
            item.detailDisplayName = QStringLiteral("interface %1")
                                         .arg(item.detailDisplayName);
            items.append(item);
        }
    };
    appendDirection(false);
    appendDirection(true);
    sortItems(items);
    return items;
}

QList<SignalJourneyItem> SignalJourneyService::timingConnectionItems(
    const SemanticSymbolRecord& signal) const
{
    QList<SignalJourneyItem> items;
    QSet<QString> seen;
    auto appendDirection = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            signalJourneyRelationshipResultsForRecord(semanticIndex(),
                                                      signal,
                                                      outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            if (relationship.relationship.type != SymbolRelationshipEngine::CLOCKS
                && relationship.relationship.type != SymbolRelationshipEngine::RESETS) {
                continue;
            }
            const SemanticSymbolRecord peerRecord =
                outgoing ? relationship.toSymbolRecord : relationship.fromSymbolRecord;
            if (!peerRecord.isValid())
                continue;
            const QString key = QStringLiteral("%1:%2:%3")
                                    .arg(relationship.relationship.fromId)
                                    .arg(relationship.relationship.toId)
                                    .arg(static_cast<int>(relationship.relationship.type));
            if (seen.contains(key))
                continue;
            seen.insert(key);

            SignalJourneyItem item;
            item.outgoing = outgoing;
            fillDisplayMetadata(item, relationship, peerRecord);
            item.detailDisplayName = QStringLiteral("timing %1")
                                         .arg(item.detailDisplayName);
            items.append(item);
        }
    };
    appendDirection(false);
    appendDirection(true);
    sortItems(items);
    return items;
}

QSet<QString> SignalJourneyService::interfaceNames() const
{
    QSet<QString> names;
    for (const SemanticSymbolRecord& record : semanticIndex()->getSymbolRecords()) {
        if (record.declarationKind
                == SymbolTaxonomy::DeclarationKind::Interface
            && !record.name.isEmpty()) {
            names.insert(record.name);
        }
    }
    return names;
}

QString SignalJourneyService::directionDisplayName(bool outgoing)
{
    return outgoing ? QStringLiteral("outgoing") : QStringLiteral("incoming");
}

QString SignalJourneyService::relationshipTypeDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    return ::relationshipTypeDisplayName(type);
}

QString SignalJourneyService::notFoundReasonDisplayName(
    SignalJourneyNotFoundReason reason)
{
    switch (reason) {
    case SignalJourneyNotFoundReason::None:
        return QString();
    case SignalJourneyNotFoundReason::EmptySignalName:
        return QStringLiteral("empty signal name");
    case SignalJourneyNotFoundReason::NoMatchingSignal:
        return QStringLiteral("no matching signal");
    case SignalJourneyNotFoundReason::UnsupportedSymbolKind:
        return QStringLiteral("unsupported symbol kind");
    }
    return QStringLiteral("signal journey unavailable");
}

QString SignalJourneyService::provenanceDisplayName(
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

QString SignalJourneyService::confidenceDisplayName(int confidence)
{
    return confidence > 0
        ? QStringLiteral("%1%").arg(confidence)
        : QStringLiteral("unknown");
}

QString SignalJourneyService::evidenceDisplayName(const QString& evidenceText)
{
    return evidenceText.isEmpty()
        ? QStringLiteral("no evidence detail")
        : evidenceText;
}

QString SignalJourneyService::interfaceConnectionKindDisplayName(
    const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(record);
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Port
        && metadata.interfaceLikeOwner) {
        return QStringLiteral("interface port");
    }
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Modport)
        return QStringLiteral("interface modport");
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Interface)
        return QStringLiteral("interface declaration");
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Instance
        && metadata.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration
        && !interfaceBaseDisplayNameForRecord(record).isEmpty()) {
        return QStringLiteral("interface instance");
    }
    if (!record.owner.name.isEmpty())
        return QStringLiteral("interface member");
    return QStringLiteral("interface connection");
}

QString SignalJourneyService::interfaceBaseDisplayName(
    const SemanticSymbolRecord& record)
{
    const QString baseName = interfaceBaseDisplayNameForRecord(record);
    if (!baseName.isEmpty())
        return baseName;
    return record.owner.name;
}

void SignalJourneyService::fillDeclarationDisplayMetadata(
    SignalJourneyReport& report,
    const SemanticSymbolRecord& declaration)
{
    if (!report.declarationSymbolRecord.isValid())
        report.declarationSymbolRecord = declaration;
    report.declarationStableKey =
        report.declarationSymbolRecord.stableKey;
    report.declarationCodeLink =
        codeLinkForRecord(report.declarationSymbolRecord);
    report.declarationDisplayName =
        symbolDisplayNameForRecord(report.declarationSymbolRecord);
    report.declarationTypeDisplayName =
        typeDisplayNameForRecord(report.declarationSymbolRecord);
    report.declarationFileDisplayName =
        report.declarationCodeLink.fileDisplayName;
    report.declarationLineDisplayName =
        report.declarationCodeLink.lineDisplayName;
    report.declarationSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(report.declarationSymbolRecord);
}

void SignalJourneyService::fillDisplayMetadata(
    SignalJourneyItem& item,
    const SemanticRelationshipResult& relationship,
    const SemanticSymbolRecord& peerRecord)
{
    item.fromSymbolRecord = relationship.fromSymbolRecord;
    item.toSymbolRecord = relationship.toSymbolRecord;
    item.peerSymbolRecord =
        item.outgoing ? item.toSymbolRecord : item.fromSymbolRecord;
    if (!item.peerSymbolRecord.isValid())
        item.peerSymbolRecord = peerRecord;
    item.fromStableKey = item.fromSymbolRecord.stableKey.isValid()
        ? item.fromSymbolRecord.stableKey
        : relationship.fromStableKey;
    item.toStableKey = item.toSymbolRecord.stableKey.isValid()
        ? item.toSymbolRecord.stableKey
        : relationship.toStableKey;
    item.peerStableKey = item.peerSymbolRecord.stableKey.isValid()
        ? item.peerSymbolRecord.stableKey
        : (item.outgoing ? item.toStableKey : item.fromStableKey);
    item.peerCodeLink = codeLinkForRecord(item.peerSymbolRecord);
    item.fromCodeLink = codeLinkForRecord(item.fromSymbolRecord);
    item.toCodeLink = codeLinkForRecord(item.toSymbolRecord);
    item.provenance = relationship.provenance;
    item.confidence = relationship.confidence;
    item.evidenceText = relationship.evidenceText;
    item.directionDisplayName = directionDisplayName(item.outgoing);
    item.relationshipTypeDisplayName =
        relationshipTypeDisplayName(relationship.relationship.type);
    item.provenanceDisplayName = provenanceDisplayName(item.provenance);
    item.confidenceDisplayName = confidenceDisplayName(item.confidence);
    item.evidenceDisplayName = evidenceDisplayName(item.evidenceText);
    item.peerSymbolDisplayName =
        symbolDisplayNameForRecord(item.peerSymbolRecord);
    item.fromSymbolDisplayName =
        symbolDisplayNameForRecord(item.fromSymbolRecord);
    item.toSymbolDisplayName =
        symbolDisplayNameForRecord(item.toSymbolRecord);
    item.fromTypeDisplayName =
        typeDisplayNameForRecord(item.fromSymbolRecord);
    item.toTypeDisplayName =
        typeDisplayNameForRecord(item.toSymbolRecord);
    item.fromSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(item.fromSymbolRecord);
    item.toSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(item.toSymbolRecord);
    item.connectionKindDisplayName = QStringLiteral("relationship");
    item.peerTypeDisplayName =
        typeDisplayNameForRecord(item.peerSymbolRecord);
    item.peerSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(item.peerSymbolRecord);
    item.interfaceBaseDisplayName =
        interfaceBaseDisplayNameForRecord(item.peerSymbolRecord);
    item.peerFileDisplayName = item.peerCodeLink.fileDisplayName;
    item.peerLineDisplayName = item.peerCodeLink.lineDisplayName;
    item.detailDisplayName = QStringLiteral("%1 %2")
                                 .arg(item.directionDisplayName,
                                      item.relationshipTypeDisplayName);
}

void SignalJourneyService::fillInterfaceDisplayMetadata(
    SignalJourneyItem& item,
    const SemanticSymbolRecord& peerRecord)
{
    item.connectionKindDisplayName =
        interfaceConnectionKindDisplayName(item.peerSymbolRecord);
    item.interfaceBaseDisplayName =
        interfaceBaseDisplayName(peerRecord);
}

void SignalJourneyService::sortItems(QList<SignalJourneyItem>& items)
{
    std::sort(items.begin(), items.end(),
              [](const SignalJourneyItem& lhs,
                 const SignalJourneyItem& rhs) {
                  const QString leftFileName =
                      lhs.peerSymbolRecord.location.fileName;
                  const QString rightFileName =
                      rhs.peerSymbolRecord.location.fileName;
                  const int leftLine =
                      lhs.peerSymbolRecord.location.startLine;
                  const int rightLine =
                      rhs.peerSymbolRecord.location.startLine;
                  const int leftColumn =
                      lhs.peerSymbolRecord.location.startColumn;
                  const int rightColumn =
                      rhs.peerSymbolRecord.location.startColumn;
                  const QString leftName = lhs.peerSymbolRecord.name;
                  const QString rightName = rhs.peerSymbolRecord.name;
                  if (leftFileName != rightFileName)
                      return leftFileName < rightFileName;
                  if (leftLine != rightLine)
                      return leftLine < rightLine;
                  if (leftColumn != rightColumn)
                      return leftColumn < rightColumn;
                  if (leftName != rightName)
                      return leftName < rightName;
                  return lhs.peerStableKey.toString()
                      < rhs.peerStableKey.toString();
              });
}
