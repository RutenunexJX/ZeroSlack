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
    report.assignments = relationshipItems(
        signal,
        false,
        {SymbolRelationshipEngine::ASSIGNS_TO});
    report.reads = relationshipItems(
        signal,
        false,
        {SymbolRelationshipEngine::READS_FROM});
    report.portConnections = portConnectionItems(signal);
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
        return SymbolTaxonomy::isSignalDeclaration(symbol.symbolType)
            || SymbolTaxonomy::isPortDeclaration(symbol.symbolType)
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
    return definition.found
            && (SymbolTaxonomy::isSignalDeclaration(definition.symbol.symbolType)
                || SymbolTaxonomy::isPortDeclaration(definition.symbol.symbolType))
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
            if (!SymbolTaxonomy::isPortConnectionPeer(peer.symbolType))
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
            items.append(item);
        }
    };
    appendDirection(false);
    appendDirection(true);
    sortItems(items);
    return items;
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
