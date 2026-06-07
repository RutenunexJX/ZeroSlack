#ifndef RELATIONSHIPSERVICE_H
#define RELATIONSHIPSERVICE_H

#include "semanticindex.h"

#include <QList>
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
    bool typeMatches(SymbolRelationshipEngine::RelationType type,
                     const QList<SymbolRelationshipEngine::RelationType>& allowedTypes) const;
    RelationshipResult enrich(const SemanticRelationship& relationship) const;
};

#endif // RELATIONSHIPSERVICE_H
