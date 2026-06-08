#include "relationshipservice.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

std::unique_ptr<RelationshipService> RelationshipService::instance = nullptr;

namespace {
QString normalizedRelationshipFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool relationshipSymbolLess(const sym_list::SymbolInfo& lhs,
                            const sym_list::SymbolInfo& rhs)
{
    const int fileCompare = QString::compare(normalizedRelationshipFileName(lhs.fileName),
                                             normalizedRelationshipFileName(rhs.fileName),
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    if (lhs.startLine != rhs.startLine)
        return lhs.startLine < rhs.startLine;
    if (lhs.startColumn != rhs.startColumn)
        return lhs.startColumn < rhs.startColumn;
    return QString::compare(lhs.symbolName, rhs.symbolName, Qt::CaseInsensitive) < 0;
}

void sortRelationshipResults(QList<RelationshipResult>& relationships, bool outgoing)
{
    std::sort(relationships.begin(), relationships.end(),
              [outgoing](const RelationshipResult& lhs, const RelationshipResult& rhs) {
                  if (lhs.relationship.type != rhs.relationship.type) {
                      return static_cast<int>(lhs.relationship.type)
                          < static_cast<int>(rhs.relationship.type);
                  }

                  const sym_list::SymbolInfo& lhsSymbol = outgoing ? lhs.toSymbol : lhs.fromSymbol;
                  const sym_list::SymbolInfo& rhsSymbol = outgoing ? rhs.toSymbol : rhs.fromSymbol;
                  return relationshipSymbolLess(lhsSymbol, rhsSymbol);
              });
}
}

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
    sortRelationshipResults(result, query.outgoing);

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

RelationshipReport RelationshipService::findRelationshipReport(
    const RelationshipBrowseQuery& query) const
{
    RelationshipReport report;
    const int id = resolveSymbolId(query);
    report.subjectSymbolId = id;
    if (id < 0)
        return report;
    report.subjectSymbol = semanticIndex()->getSymbolById(id);

    QMap<DirectedRelationshipResult::Direction, int> directionGroupIndexes;
    QMap<DirectedRelationshipResult::Direction,
         QMap<SymbolRelationshipEngine::RelationType, int>> typeGroupIndexes;

    auto appendRelationships = [&](const QList<RelationshipResult>& relationships,
                                   DirectedRelationshipResult::Direction direction) {
        for (const RelationshipResult& relationship : relationships) {
            DirectedRelationshipResult directed;
            directed.relationship = relationship;
            directed.direction = direction;
            directed.peerSymbol = direction == DirectedRelationshipResult::Outgoing
                ? relationship.toSymbol
                : relationship.fromSymbol;
            if (directed.peerSymbol.symbolId < 0)
                continue;
            report.relationships.append(directed);
            report.typeCounts[relationship.relationship.type]++;
            report.directionCounts[direction]++;
            report.directionTypeCounts[direction][relationship.relationship.type]++;
            if (!directionGroupIndexes.contains(direction)) {
                RelationshipDirectionGroup directionGroup;
                directionGroup.direction = direction;
                directionGroupIndexes.insert(direction, report.directionGroups.size());
                report.directionGroups.append(directionGroup);
            }

            RelationshipDirectionGroup& directionGroup =
                report.directionGroups[directionGroupIndexes.value(direction)];
            directionGroup.count++;

            const SymbolRelationshipEngine::RelationType type =
                relationship.relationship.type;
            if (!typeGroupIndexes[direction].contains(type)) {
                RelationshipTypeGroup typeGroup;
                typeGroup.type = type;
                typeGroupIndexes[direction].insert(type,
                                                   directionGroup.typeGroups.size());
                directionGroup.typeGroups.append(typeGroup);
            }

            RelationshipTypeGroup& typeGroup =
                directionGroup.typeGroups[typeGroupIndexes[direction].value(type)];
            typeGroup.relationships.append(directed);
            typeGroup.count++;
            if (direction == DirectedRelationshipResult::Outgoing)
                report.outgoingCount++;
            else
                report.incomingCount++;
        }
    };

    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolId = id;
    relationshipQuery.symbolName = query.symbolName;
    relationshipQuery.fileName = query.fileName;
    relationshipQuery.moduleName = query.moduleName;
    relationshipQuery.types = query.types;

    if (query.includeOutgoing) {
        relationshipQuery.outgoing = true;
        appendRelationships(findRelationships(relationshipQuery),
                            DirectedRelationshipResult::Outgoing);
    }
    if (query.includeIncoming) {
        relationshipQuery.outgoing = false;
        appendRelationships(findRelationships(relationshipQuery),
                            DirectedRelationshipResult::Incoming);
    }

    report.totalCount = report.relationships.size();
    return report;
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

int RelationshipService::resolveSymbolId(const RelationshipBrowseQuery& query) const
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
