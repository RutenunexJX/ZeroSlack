#ifndef RELATIONSHIPSERVICE_H
#define RELATIONSHIPSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QMap>
#include <QString>
#include <memory>

struct RelationshipQuery {
    int symbolId = -1;
    QString symbolName;
    QString fileName;
    QString moduleName;
    bool outgoing = true;
    QList<SymbolRelationshipEngine::RelationType> types;
};

struct RelationshipResult {
    SemanticRelationship relationship;
    sym_list::SymbolInfo fromSymbol;
    sym_list::SymbolInfo toSymbol;
};

struct RelationshipBrowseQuery {
    int symbolId = -1;
    QString symbolName;
    QString fileName;
    QString moduleName;
    bool includeOutgoing = true;
    bool includeIncoming = true;
    QList<SymbolRelationshipEngine::RelationType> types;
};

struct DirectedRelationshipResult {
    enum Direction {
        Outgoing,
        Incoming
    };

    RelationshipResult relationship;
    Direction direction = Outgoing;
    sym_list::SymbolInfo peerSymbol;
};

struct RelationshipTypeGroup {
    SymbolRelationshipEngine::RelationType type = SymbolRelationshipEngine::REFERENCES;
    QList<DirectedRelationshipResult> relationships;
    int count = 0;
};

struct RelationshipDirectionGroup {
    DirectedRelationshipResult::Direction direction = DirectedRelationshipResult::Outgoing;
    QList<RelationshipTypeGroup> typeGroups;
    int count = 0;
};

struct RelationshipReport {
    QList<DirectedRelationshipResult> relationships;
    QList<RelationshipDirectionGroup> directionGroups;
    int totalCount = 0;
    int outgoingCount = 0;
    int incomingCount = 0;
    QMap<DirectedRelationshipResult::Direction, int> directionCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> typeCounts;
    QMap<DirectedRelationshipResult::Direction,
         QMap<SymbolRelationshipEngine::RelationType, int>> directionTypeCounts;
};

class RelationshipService
{
public:
    static RelationshipService* getInstance();

    explicit RelationshipService(SemanticIndex* semanticIndex = nullptr);
    ~RelationshipService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<RelationshipResult> findRelationships(const RelationshipQuery& query) const;
    QList<RelationshipResult> findOutgoingRelationships(const RelationshipQuery& query) const;
    QList<RelationshipResult> findIncomingRelationships(const RelationshipQuery& query) const;
    RelationshipReport findRelationshipReport(const RelationshipBrowseQuery& query) const;
    QList<int> findRelatedSymbolIds(const RelationshipQuery& query) const;
    bool hasRelationship(int fromSymbolId,
                         int toSymbolId,
                         SymbolRelationshipEngine::RelationType type) const;
    bool hasRelationships(const RelationshipQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<RelationshipService> instance;

    SemanticIndex* semanticIndex() const;
    int resolveSymbolId(const RelationshipQuery& query) const;
    int resolveSymbolId(const RelationshipBrowseQuery& query) const;
    bool typeMatches(SymbolRelationshipEngine::RelationType type,
                     const QList<SymbolRelationshipEngine::RelationType>& allowedTypes) const;
    RelationshipResult enrich(const SemanticRelationship& relationship) const;
};

#endif // RELATIONSHIPSERVICE_H
