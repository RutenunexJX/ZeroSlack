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
    const sym_list::SymbolInfo signal = resolveSignal(query);
    if (signal.symbolId < 0)
        return report;

    report.found = true;
    report.declaration = signal;
    fillDeclarationDisplayMetadata(report);
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
    const SignalJourneyQuery& query) const
{
    if (query.signalSymbolId >= 0) {
        const sym_list::SymbolInfo symbol =
            semanticIndex()->getSymbolById(query.signalSymbolId);
        return isJourneyDeclaration(symbol)
            ? symbol
            : missingSignalJourneySymbol();
    }
    if (query.signalName.isEmpty())
        return missingSignalJourneySymbol();

    SemanticDefinitionQuery definitionQuery;
    definitionQuery.symbolName = query.signalName;
    definitionQuery.fileName = query.fileName;
    definitionQuery.moduleName = query.moduleName;
    const SemanticDefinitionResult definition =
        semanticIndex()->resolveDefinition(definitionQuery);
    return definition.found && isJourneyDeclaration(definition.symbol)
        ? definition.symbol
        : missingSignalJourneySymbol();
}

QList<SignalJourneyItem> SignalJourneyService::relationshipItems(
    const sym_list::SymbolInfo& signal,
    bool outgoing,
    const QList<SymbolRelationshipEngine::RelationType>& types) const
{
    QList<SignalJourneyItem> items;
    const QList<SemanticRelationshipResult> relationships =
        semanticIndex()->getRelationshipResults(signal.symbolId, outgoing);
    for (const SemanticRelationshipResult& relationship : relationships) {
        if (!types.contains(relationship.relationship.type))
            continue;
        SignalJourneyItem item;
        item.relationship = relationship;
        item.outgoing = outgoing;
        item.peerSymbol = outgoing ? relationship.toSymbol : relationship.fromSymbol;
        if (item.peerSymbol.symbolId < 0)
            continue;
        fillDisplayMetadata(item);
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
            semanticIndex()->getRelationshipResults(signal.symbolId, outgoing);
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
            item.peerSymbol = peer;
            item.outgoing = outgoing;
            fillDisplayMetadata(item);
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
            semanticIndex()->getRelationshipResults(signal.symbolId, outgoing);
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
            item.peerSymbol = peer;
            item.outgoing = outgoing;
            fillDisplayMetadata(item);
            fillInterfaceDisplayMetadata(item);
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
            semanticIndex()->getRelationshipResults(signal.symbolId, outgoing);
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
            item.peerSymbol = peer;
            item.outgoing = outgoing;
            fillDisplayMetadata(item);
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
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Interface
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Modport
        || (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Port
            && metadata.interfaceLikeOwner)) {
        return true;
    }
    if (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Instance
        && metadata.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration) {
        const QString interfaceName = interfaceBaseName(symbol.dataType);
        return !interfaceName.isEmpty() && interfaces.contains(interfaceName);
    }
    return !symbol.moduleScope.isEmpty()
        && interfaces.contains(symbol.moduleScope)
        && SymbolTaxonomy::isDefinitionCandidate(metadata);
}

QSet<QString> SignalJourneyService::interfaceNames() const
{
    QSet<QString> names;
    for (const sym_list::SymbolInfo& symbol : semanticIndex()->getSymbols()) {
        if (SymbolTaxonomy::semanticMetadata(symbol).declarationKind
                == SymbolTaxonomy::DeclarationKind::Interface
            && !symbol.symbolName.isEmpty()) {
            names.insert(symbol.symbolName);
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

QString SignalJourneyService::interfaceBaseName(const QString& dataType)
{
    const int dot = dataType.indexOf(QLatin1Char('.'));
    return dot >= 0 ? dataType.left(dot) : dataType;
}

QString SignalJourneyService::interfaceConnectionKindDisplayName(
    const sym_list::SymbolInfo& symbol)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol);
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
        && !interfaceBaseName(symbol.dataType).isEmpty()) {
        return QStringLiteral("interface instance");
    }
    if (!symbol.moduleScope.isEmpty())
        return QStringLiteral("interface member");
    return QStringLiteral("interface connection");
}

QString SignalJourneyService::interfaceBaseDisplayName(
    const sym_list::SymbolInfo& symbol)
{
    const QString baseName = interfaceBaseName(symbol.dataType);
    if (!baseName.isEmpty())
        return baseName;
    return symbol.moduleScope;
}

QString SignalJourneyService::sourceRoleDisplayName(SymbolTaxonomy::SourceRole role)
{
    switch (role) {
    case SymbolTaxonomy::SourceRole::DesignSource:
        return QStringLiteral("design source");
    case SymbolTaxonomy::SourceRole::Header:
        return QStringLiteral("header");
    case SymbolTaxonomy::SourceRole::Unknown:
    default:
        return QStringLiteral("source");
    }
}

void SignalJourneyService::fillDeclarationDisplayMetadata(
    SignalJourneyReport& report)
{
    report.declarationCodeLink = RtlInsightLink::fromSymbol(report.declaration);
    report.declarationDisplayName = symbolDisplayName(report.declaration);
    report.declarationTypeDisplayName =
        SymbolTaxonomy::symbolTypeLabel(
            SymbolTaxonomy::semanticMetadata(report.declaration));
    report.declarationFileDisplayName =
        report.declarationCodeLink.fileDisplayName;
    report.declarationLineDisplayName =
        report.declarationCodeLink.lineDisplayName;
    report.declarationSourceRoleDisplayName =
        sourceRoleDisplayName(
            SymbolTaxonomy::semanticMetadata(report.declaration).sourceRole);
}

void SignalJourneyService::fillDisplayMetadata(SignalJourneyItem& item)
{
    item.fromSymbol = item.relationship.fromSymbol;
    item.toSymbol = item.relationship.toSymbol;
    item.peerCodeLink = RtlInsightLink::fromSymbol(item.peerSymbol);
    item.fromCodeLink = RtlInsightLink::fromSymbol(item.fromSymbol);
    item.toCodeLink = RtlInsightLink::fromSymbol(item.toSymbol);
    item.directionDisplayName = directionDisplayName(item.outgoing);
    item.relationshipTypeDisplayName =
        relationshipTypeDisplayName(item.relationship.relationship.type);
    item.peerSymbolDisplayName = symbolDisplayName(item.peerSymbol);
    item.fromSymbolDisplayName = symbolDisplayName(item.fromSymbol);
    item.toSymbolDisplayName = symbolDisplayName(item.toSymbol);
    item.fromTypeDisplayName =
        SymbolTaxonomy::symbolTypeLabel(
            SymbolTaxonomy::semanticMetadata(item.fromSymbol));
    item.toTypeDisplayName =
        SymbolTaxonomy::symbolTypeLabel(
            SymbolTaxonomy::semanticMetadata(item.toSymbol));
    item.fromSourceRoleDisplayName =
        sourceRoleDisplayName(
            SymbolTaxonomy::semanticMetadata(item.fromSymbol).sourceRole);
    item.toSourceRoleDisplayName =
        sourceRoleDisplayName(
            SymbolTaxonomy::semanticMetadata(item.toSymbol).sourceRole);
    item.connectionKindDisplayName = QStringLiteral("relationship");
    item.peerTypeDisplayName =
        SymbolTaxonomy::symbolTypeLabel(
            SymbolTaxonomy::semanticMetadata(item.peerSymbol));
    item.peerSourceRoleDisplayName =
        sourceRoleDisplayName(
            SymbolTaxonomy::semanticMetadata(item.peerSymbol).sourceRole);
    item.interfaceBaseDisplayName = interfaceBaseDisplayName(item.peerSymbol);
    item.peerFileDisplayName = item.peerCodeLink.fileDisplayName;
    item.peerLineDisplayName = item.peerCodeLink.lineDisplayName;
    item.detailDisplayName = QStringLiteral("%1 %2")
                                 .arg(item.directionDisplayName,
                                      item.relationshipTypeDisplayName);
}

void SignalJourneyService::fillInterfaceDisplayMetadata(SignalJourneyItem& item)
{
    item.connectionKindDisplayName =
        interfaceConnectionKindDisplayName(item.peerSymbol);
    item.interfaceBaseDisplayName = interfaceBaseDisplayName(item.peerSymbol);
}

void SignalJourneyService::sortItems(QList<SignalJourneyItem>& items)
{
    std::sort(items.begin(), items.end(),
              [](const SignalJourneyItem& lhs,
                 const SignalJourneyItem& rhs) {
                  const sym_list::SymbolInfo& left = lhs.peerSymbol;
                  const sym_list::SymbolInfo& right = rhs.peerSymbol;
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
