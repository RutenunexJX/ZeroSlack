#include "signaljourneyservice.h"

#include <QSet>
#include <algorithm>

std::unique_ptr<SignalJourneyService> SignalJourneyService::instance = nullptr;

namespace {
sym_list::SymbolInfo missingSignalJourneySymbol()
{
    sym_list::SymbolInfo symbol;
    symbol.symbolId = -1;
    return symbol;
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

RtlInsightCodeLink codeLinkForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (record.isValid()) {
        const QString fileName = fallback.fileName.isEmpty()
            ? record.location.fileName
            : fallback.fileName;
        return RtlInsightLink::fromFileLine(fileName,
                                            record.location.startLine,
                                            record.location.startColumn);
    }
    return RtlInsightLink::fromSymbol(fallback);
}

QString symbolDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.name.isEmpty())
        return record.name;
    if (!fallback.symbolName.isEmpty())
        return fallback.symbolName;
    return QStringLiteral("<unknown>");
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(fallback);
    if (!record.isValid())
        return metadata;

    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

QString typeDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return SymbolTaxonomy::symbolTypeLabel(
        metadataForRecord(record, fallback));
}

QString sourceRoleDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    return SymbolTaxonomy::sourceRoleDisplayName(
        metadataForRecord(record, fallback).sourceRole);
}

QString interfaceBaseDisplayNameForRecord(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;
    return SymbolTaxonomy::interfaceTypeName(fallback);
}

SymbolStableKey signalJourneyStableKeyForSymbol(
    const sym_list::SymbolInfo& symbol)
{
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    return record.stableKey.isValid()
        ? record.stableKey
        : symbolStableKeyForSymbol(symbol);
}

QList<SemanticRelationshipResult> signalJourneyRelationshipResultsForSymbol(
    SemanticIndex* index,
    const sym_list::SymbolInfo& symbol,
    bool outgoing)
{
    if (!index)
        return {};
    const SymbolStableKey stableKey = signalJourneyStableKeyForSymbol(symbol);
    return stableKey.isValid()
        ? index->getRelationshipResults(stableKey, outgoing)
        : QList<SemanticRelationshipResult>();
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
    const sym_list::SymbolInfo signal =
        resolveSignal(query, &report.notFoundReason);
    if (signal.symbolId < 0) {
        report.notFoundReasonDisplayName =
            notFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    report.found = true;
    report.notFoundReason = SignalJourneyNotFoundReason::None;
    report.declarationSymbolRecord = semanticSymbolRecordForSymbol(signal);
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

sym_list::SymbolInfo SignalJourneyService::resolveSignal(
    const SignalJourneyQuery& query,
    SignalJourneyNotFoundReason* reason) const
{
    if (reason)
        *reason = SignalJourneyNotFoundReason::None;

    if (query.signalStableKey.isValid()) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolByStableKey(query.signalStableKey);
        if (symbol.symbolId < 0) {
            if (reason)
                *reason = SignalJourneyNotFoundReason::NoMatchingSignal;
            return missingSignalJourneySymbol();
        }
        if (!isJourneyDeclaration(symbol)) {
            if (reason)
                *reason = SignalJourneyNotFoundReason::UnsupportedSymbolKind;
            return missingSignalJourneySymbol();
        }
        return symbol;
    }

    if (query.signalName.isEmpty()) {
        if (reason)
            *reason = SignalJourneyNotFoundReason::EmptySignalName;
        return missingSignalJourneySymbol();
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
        return missingSignalJourneySymbol();
    }
    if (!isJourneyDeclaration(definition.symbol)) {
        if (reason)
            *reason = SignalJourneyNotFoundReason::UnsupportedSymbolKind;
        return missingSignalJourneySymbol();
    }
    return definition.symbol;
}

QList<SignalJourneyItem> SignalJourneyService::relationshipItems(
    const sym_list::SymbolInfo& signal,
    bool outgoing,
    const QList<SymbolRelationshipEngine::RelationType>& types) const
{
    QList<SignalJourneyItem> items;
    const QList<SemanticRelationshipResult> relationships =
        signalJourneyRelationshipResultsForSymbol(semanticIndex(),
                                                  signal,
                                                  outgoing);
    for (const SemanticRelationshipResult& relationship : relationships) {
        if (!types.contains(relationship.relationship.type))
            continue;
        SignalJourneyItem item;
        item.relationship = relationship;
        item.outgoing = outgoing;
        const sym_list::SymbolInfo peer =
            outgoing ? relationship.toSymbol : relationship.fromSymbol;
        if (peer.symbolId < 0)
            continue;
        fillDisplayMetadata(item, peer);
        items.append(item);
    }
    sortItems(items);
    return items;
}

QList<SignalJourneyItem> SignalJourneyService::portConnectionItems(
    const sym_list::SymbolInfo& signal) const
{
    QList<SignalJourneyItem> items;
    QSet<QString> seen;
    auto appendDirection = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            signalJourneyRelationshipResultsForSymbol(semanticIndex(),
                                                      signal,
                                                      outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            const sym_list::SymbolInfo peer =
                outgoing ? relationship.toSymbol : relationship.fromSymbol;
            if (!SymbolTaxonomy::isPortConnectionPeer(
                    SymbolTaxonomy::semanticMetadata(peer))) {
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
            item.relationship = relationship;
            item.outgoing = outgoing;
            fillDisplayMetadata(item, peer);
            items.append(item);
        }
    };
    appendDirection(false);
    appendDirection(true);
    sortItems(items);
    return items;
}

QList<SignalJourneyItem> SignalJourneyService::interfaceConnectionItems(
    const sym_list::SymbolInfo& signal) const
{
    QList<SignalJourneyItem> items;
    QSet<QString> seen;
    auto appendDirection = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            signalJourneyRelationshipResultsForSymbol(semanticIndex(),
                                                      signal,
                                                      outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            const sym_list::SymbolInfo peer =
                outgoing ? relationship.toSymbol : relationship.fromSymbol;
            if (!isInterfaceConnectionPeer(peer))
                continue;
            const QString key = QStringLiteral("%1:%2:%3")
                                    .arg(relationship.relationship.fromId)
                                    .arg(relationship.relationship.toId)
                                    .arg(static_cast<int>(relationship.relationship.type));
            if (seen.contains(key))
                continue;
            seen.insert(key);

            SignalJourneyItem item;
            item.relationship = relationship;
            item.outgoing = outgoing;
            fillDisplayMetadata(item, peer);
            fillInterfaceDisplayMetadata(item, peer);
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
    const sym_list::SymbolInfo& signal) const
{
    QList<SignalJourneyItem> items;
    QSet<QString> seen;
    auto appendDirection = [&](bool outgoing) {
        const QList<SemanticRelationshipResult> relationships =
            signalJourneyRelationshipResultsForSymbol(semanticIndex(),
                                                      signal,
                                                      outgoing);
        for (const SemanticRelationshipResult& relationship : relationships) {
            if (relationship.relationship.type != SymbolRelationshipEngine::CLOCKS
                && relationship.relationship.type != SymbolRelationshipEngine::RESETS) {
                continue;
            }
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

            SignalJourneyItem item;
            item.relationship = relationship;
            item.outgoing = outgoing;
            fillDisplayMetadata(item, peer);
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

bool SignalJourneyService::isJourneyDeclaration(
    const sym_list::SymbolInfo& symbol) const
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
    if (SymbolTaxonomy::isSignalDeclaration(metadata)
        || SymbolTaxonomy::isPortDeclaration(metadata)) {
        return true;
    }
    return isInterfaceConnectionPeer(symbol);
}

bool SignalJourneyService::isInterfaceConnectionPeer(
    const sym_list::SymbolInfo& symbol) const
{
    const QSet<QString> interfaces = interfaceNames();
    const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(record, symbol);
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Modport
        || (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Port
            && metadata.interfaceLikeOwner)) {
        return true;
    }
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Instance
        && metadata.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration) {
        const QString interfaceName =
            interfaceBaseDisplayNameForRecord(record, symbol);
        return !interfaceName.isEmpty() && interfaces.contains(interfaceName);
    }
    return !record.owner.name.isEmpty()
        && interfaces.contains(record.owner.name)
        && SymbolTaxonomy::isDefinitionCandidate(metadata);
}

QSet<QString> SignalJourneyService::interfaceNames() const
{
    QSet<QString> names;
    for (const sym_list::SymbolInfo& symbol : semanticIndex()->getSymbols()) {
        const SemanticSymbolRecord record = semanticSymbolRecordForSymbol(symbol);
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

QString SignalJourneyService::symbolDisplayName(const sym_list::SymbolInfo& symbol)
{
    return symbol.symbolName;
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
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        metadataForRecord(record, fallback);
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
        && !interfaceBaseDisplayNameForRecord(record, fallback).isEmpty()) {
        return QStringLiteral("interface instance");
    }
    if (!record.owner.name.isEmpty())
        return QStringLiteral("interface member");
    return QStringLiteral("interface connection");
}

QString SignalJourneyService::interfaceBaseDisplayName(
    const SemanticSymbolRecord& record,
    const sym_list::SymbolInfo& fallback)
{
    const QString baseName = interfaceBaseDisplayNameForRecord(record, fallback);
    if (!baseName.isEmpty())
        return baseName;
    return record.owner.name;
}

void SignalJourneyService::fillDeclarationDisplayMetadata(
    SignalJourneyReport& report,
    const sym_list::SymbolInfo& declaration)
{
    if (!report.declarationSymbolRecord.isValid())
        report.declarationSymbolRecord =
            semanticSymbolRecordForSymbol(declaration);
    report.declarationStableKey =
        report.declarationSymbolRecord.stableKey.isValid()
            ? report.declarationSymbolRecord.stableKey
            : symbolStableKeyForSymbol(declaration);
    report.declarationCodeLink =
        codeLinkForRecord(report.declarationSymbolRecord, declaration);
    report.declarationDisplayName =
        symbolDisplayNameForRecord(report.declarationSymbolRecord,
                                   declaration);
    report.declarationTypeDisplayName =
        typeDisplayNameForRecord(report.declarationSymbolRecord,
                                 declaration);
    report.declarationFileDisplayName =
        report.declarationCodeLink.fileDisplayName;
    report.declarationLineDisplayName =
        report.declarationCodeLink.lineDisplayName;
    report.declarationSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(report.declarationSymbolRecord,
                                       declaration);
}

void SignalJourneyService::fillDisplayMetadata(
    SignalJourneyItem& item,
    const sym_list::SymbolInfo& peerSymbol)
{
    const sym_list::SymbolInfo fromSymbol = item.relationship.fromSymbol;
    const sym_list::SymbolInfo toSymbol = item.relationship.toSymbol;
    item.fromSymbolRecord = item.relationship.fromSymbolRecord.isValid()
        ? item.relationship.fromSymbolRecord
        : semanticSymbolRecordForSymbol(fromSymbol);
    item.toSymbolRecord = item.relationship.toSymbolRecord.isValid()
        ? item.relationship.toSymbolRecord
        : semanticSymbolRecordForSymbol(toSymbol);
    item.peerSymbolRecord =
        item.outgoing ? item.toSymbolRecord : item.fromSymbolRecord;
    if (!item.peerSymbolRecord.isValid())
        item.peerSymbolRecord = semanticSymbolRecordForSymbol(peerSymbol);
    item.fromStableKey = item.fromSymbolRecord.stableKey.isValid()
        ? item.fromSymbolRecord.stableKey
        : item.relationship.fromStableKey;
    item.toStableKey = item.toSymbolRecord.stableKey.isValid()
        ? item.toSymbolRecord.stableKey
        : item.relationship.toStableKey;
    item.peerStableKey = item.peerSymbolRecord.stableKey.isValid()
        ? item.peerSymbolRecord.stableKey
        : (item.outgoing ? item.toStableKey : item.fromStableKey);
    item.peerCodeLink = codeLinkForRecord(item.peerSymbolRecord,
                                          peerSymbol);
    item.fromCodeLink = codeLinkForRecord(item.fromSymbolRecord,
                                          fromSymbol);
    item.toCodeLink = codeLinkForRecord(item.toSymbolRecord,
                                        toSymbol);
    item.provenance = item.relationship.provenance;
    item.confidence = item.relationship.confidence;
    item.evidenceText = item.relationship.evidenceText;
    item.directionDisplayName = directionDisplayName(item.outgoing);
    item.relationshipTypeDisplayName =
        relationshipTypeDisplayName(item.relationship.relationship.type);
    item.provenanceDisplayName = provenanceDisplayName(item.provenance);
    item.confidenceDisplayName = confidenceDisplayName(item.confidence);
    item.evidenceDisplayName = evidenceDisplayName(item.evidenceText);
    item.peerSymbolDisplayName =
        symbolDisplayNameForRecord(item.peerSymbolRecord, peerSymbol);
    item.fromSymbolDisplayName =
        symbolDisplayNameForRecord(item.fromSymbolRecord, fromSymbol);
    item.toSymbolDisplayName =
        symbolDisplayNameForRecord(item.toSymbolRecord, toSymbol);
    item.fromTypeDisplayName =
        typeDisplayNameForRecord(item.fromSymbolRecord, fromSymbol);
    item.toTypeDisplayName =
        typeDisplayNameForRecord(item.toSymbolRecord, toSymbol);
    item.fromSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(item.fromSymbolRecord,
                                       fromSymbol);
    item.toSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(item.toSymbolRecord,
                                       toSymbol);
    item.connectionKindDisplayName = QStringLiteral("relationship");
    item.peerTypeDisplayName =
        typeDisplayNameForRecord(item.peerSymbolRecord, peerSymbol);
    item.peerSourceRoleDisplayName =
        sourceRoleDisplayNameForRecord(item.peerSymbolRecord,
                                       peerSymbol);
    item.interfaceBaseDisplayName =
        interfaceBaseDisplayNameForRecord(item.peerSymbolRecord,
                                          peerSymbol);
    item.peerFileDisplayName = item.peerCodeLink.fileDisplayName;
    item.peerLineDisplayName = item.peerCodeLink.lineDisplayName;
    item.detailDisplayName = QStringLiteral("%1 %2")
                                 .arg(item.directionDisplayName,
                                      item.relationshipTypeDisplayName);
}

void SignalJourneyService::fillInterfaceDisplayMetadata(
    SignalJourneyItem& item,
    const sym_list::SymbolInfo& peerSymbol)
{
    item.connectionKindDisplayName =
        interfaceConnectionKindDisplayName(item.peerSymbolRecord,
                                           peerSymbol);
    item.interfaceBaseDisplayName =
        interfaceBaseDisplayName(item.peerSymbolRecord, peerSymbol);
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
