#ifndef REFERENCESERVICE_H
#define REFERENCESERVICE_H

#include "relationshipservice.h"

#include <QList>
#include <QString>
#include <memory>

struct ReferenceQuery {
    int symbolId = -1;
    QString symbolName;
    QString fileName;
    QString moduleName;
    QList<SymbolRelationshipEngine::RelationType> types;
};

struct ReferenceResult {
    RelationshipResult relationship;
    sym_list::SymbolInfo referencingSymbol;
    sym_list::SymbolInfo referencedSymbol;
};

class ReferenceService
{
public:
    static ReferenceService* getInstance();

    explicit ReferenceService(SemanticIndex* semanticIndex = nullptr);
    ~ReferenceService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<ReferenceResult> findReferences(const ReferenceQuery& query) const;
    bool hasReferences(const ReferenceQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    RelationshipService relationshipService;
    static std::unique_ptr<ReferenceService> instance;

    SemanticIndex* semanticIndex() const;
    int resolveSymbolId(const ReferenceQuery& query) const;
    QList<SymbolRelationshipEngine::RelationType> effectiveTypes(
        const ReferenceQuery& query) const;
    ReferenceResult toReferenceResult(const RelationshipResult& relationship) const;
};

#endif // REFERENCESERVICE_H
