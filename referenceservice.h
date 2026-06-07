#ifndef REFERENCESERVICE_H
#define REFERENCESERVICE_H

#include "relationshipservice.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <memory>

struct ReferenceQuery {
    int symbolId = -1;
    QString symbolName;
    QString fileName;
    QString moduleName;
    bool workspaceFilesOnly = false;
    bool currentFileOnly = false;
    QStringList workspaceFiles;
    QList<SymbolRelationshipEngine::RelationType> types;
};

struct ReferenceResult {
    RelationshipResult relationship;
    sym_list::SymbolInfo referencingSymbol;
    sym_list::SymbolInfo referencedSymbol;
};

struct ReferenceReport {
    QList<ReferenceResult> references;
    int totalCount = 0;
    QMap<QString, int> fileCounts;
    QMap<SymbolRelationshipEngine::RelationType, int> typeCounts;
};

class ReferenceService
{
public:
    static ReferenceService* getInstance();

    explicit ReferenceService(SemanticIndex* semanticIndex = nullptr);
    ~ReferenceService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<ReferenceResult> findReferences(const ReferenceQuery& query) const;
    ReferenceReport findReferenceReport(const ReferenceQuery& query) const;
    bool hasReferences(const ReferenceQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    RelationshipService relationshipService;
    static std::unique_ptr<ReferenceService> instance;

    SemanticIndex* semanticIndex() const;
    int resolveSymbolId(const ReferenceQuery& query) const;
    QList<SymbolRelationshipEngine::RelationType> effectiveTypes(
        const ReferenceQuery& query) const;
    bool scopeMatches(const ReferenceQuery& query, const sym_list::SymbolInfo& symbol) const;
    ReferenceResult toReferenceResult(const RelationshipResult& relationship) const;
};

#endif // REFERENCESERVICE_H
