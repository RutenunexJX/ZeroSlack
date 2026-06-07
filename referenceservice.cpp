#include "referenceservice.h"

std::unique_ptr<ReferenceService> ReferenceService::instance = nullptr;

ReferenceService* ReferenceService::getInstance()
{
    if (!instance)
        instance = std::make_unique<ReferenceService>();
    return instance.get();
}

ReferenceService::ReferenceService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      relationshipService(index)
{
}

ReferenceService::~ReferenceService() = default;

void ReferenceService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    relationshipService.setSemanticIndex(index);
}

QList<ReferenceResult> ReferenceService::findReferences(const ReferenceQuery& query) const
{
    const int id = resolveSymbolId(query);
    if (id < 0)
        return {};

    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolId = id;
    relationshipQuery.outgoing = false;
    relationshipQuery.types = effectiveTypes(query);

    QList<ReferenceResult> result;
    const QList<RelationshipResult> relationships =
        relationshipService.findRelationships(relationshipQuery);
    for (const RelationshipResult& rel : relationships)
        result.append(toReferenceResult(rel));
    return result;
}

bool ReferenceService::hasReferences(const ReferenceQuery& query) const
{
    return !findReferences(query).isEmpty();
}

SemanticIndex* ReferenceService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

int ReferenceService::resolveSymbolId(const ReferenceQuery& query) const
{
    if (query.symbolId >= 0)
        return query.symbolId;
    if (query.symbolName.isEmpty())
        return -1;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    const QList<sym_list::SymbolInfo> defs =
        semanticIndex()->findDefinitions(query.symbolName, context);
    return defs.isEmpty() ? -1 : defs.first().symbolId;
}

QList<SymbolRelationshipEngine::RelationType> ReferenceService::effectiveTypes(
    const ReferenceQuery& query) const
{
    if (!query.types.isEmpty())
        return query.types;
    return {
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
    };
}

ReferenceResult ReferenceService::toReferenceResult(
    const RelationshipResult& relationship) const
{
    ReferenceResult result;
    result.relationship = relationship;
    result.referencingSymbol = relationship.fromSymbol;
    result.referencedSymbol = relationship.toSymbol;
    return result;
}
