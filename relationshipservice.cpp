#include "relationshipservice.h"

std::unique_ptr<RelationshipService> RelationshipService::instance = nullptr;

RelationshipService* RelationshipService::getInstance()
{
    if (!instance)
        instance = std::make_unique<RelationshipService>();
    return instance.get();
}

RelationshipService::RelationshipService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

RelationshipService::~RelationshipService() = default;

void RelationshipService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QList<RelationshipResult> RelationshipService::findRelationships(const RelationshipQuery& query) const
{
    const int id = resolveSymbolId(query);
    if (id < 0)
        return {};

    QList<RelationshipResult> result;
    const QList<SemanticRelationship> relationships =
        semanticIndex()->getRelationships(id, query.outgoing);

    for (const SemanticRelationship& rel : relationships) {
        if (!typeMatches(rel.type, query.types))
            continue;
        result.append(enrich(rel));
    }

    return result;
}

QList<RelationshipResult> RelationshipService::findOutgoingRelationships(
    const RelationshipQuery& query) const
{
    RelationshipQuery outgoingQuery = query;
    outgoingQuery.outgoing = true;
    return findRelationships(outgoingQuery);
}

QList<RelationshipResult> RelationshipService::findIncomingRelationships(
    const RelationshipQuery& query) const
{
    RelationshipQuery incomingQuery = query;
    incomingQuery.outgoing = false;
    return findRelationships(incomingQuery);
}

QList<int> RelationshipService::findRelatedSymbolIds(const RelationshipQuery& query) const
{
    QList<int> result;
    const QList<RelationshipResult> relationships = findRelationships(query);
    for (const RelationshipResult& relationship : relationships) {
        result.append(query.outgoing
                          ? relationship.relationship.toId
                          : relationship.relationship.fromId);
    }
    return result;
}

bool RelationshipService::hasRelationship(
    int fromSymbolId,
    int toSymbolId,
    SymbolRelationshipEngine::RelationType type) const
{
    if (fromSymbolId < 0 || toSymbolId < 0)
        return false;

    RelationshipQuery query;
    query.symbolId = fromSymbolId;
    query.outgoing = true;
    query.types = {type};

    const QList<RelationshipResult> relationships = findRelationships(query);
    for (const RelationshipResult& relationship : relationships) {
        if (relationship.relationship.fromId == fromSymbolId
            && relationship.relationship.toId == toSymbolId
            && relationship.relationship.type == type) {
            return true;
        }
    }
    return false;
}

bool RelationshipService::hasRelationships(const RelationshipQuery& query) const
{
    return !findRelationships(query).isEmpty();
}

SemanticIndex* RelationshipService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

int RelationshipService::resolveSymbolId(const RelationshipQuery& query) const
{
    if (query.symbolId >= 0)
        return query.symbolId;
    if (query.symbolName.isEmpty())
        return -1;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;

    return semanticIndex()->findSymbolId(query.symbolName, context);
}

bool RelationshipService::typeMatches(
    SymbolRelationshipEngine::RelationType type,
    const QList<SymbolRelationshipEngine::RelationType>& allowedTypes) const
{
    return allowedTypes.isEmpty() || allowedTypes.contains(type);
}

RelationshipResult RelationshipService::enrich(const SemanticRelationship& relationship) const
{
    RelationshipResult result;
    result.relationship = relationship;

    result.fromSymbol = semanticIndex()->getSymbolById(relationship.fromId);
    result.toSymbol = semanticIndex()->getSymbolById(relationship.toId);

    return result;
}
