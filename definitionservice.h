#ifndef DEFINITIONSERVICE_H
#define DEFINITIONSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QString>
#include <memory>

struct DefinitionQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    QString structTypeNameForMember;
};

struct DefinitionResult {
    bool found = false;
    bool localFile = false;
    sym_list::SymbolInfo symbol;
};

class DefinitionService
{
public:
    static DefinitionService* getInstance();

    explicit DefinitionService(SemanticIndex* semanticIndex = nullptr);
    ~DefinitionService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    DefinitionResult resolveDefinition(const DefinitionQuery& query) const;
    QList<sym_list::SymbolInfo> findDefinitions(const DefinitionQuery& query) const;
    bool canResolveDefinition(const DefinitionQuery& query) const;
    bool isDefinition(const sym_list::SymbolInfo& symbol, const QString& searchWord) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<DefinitionService> instance;

    SemanticIndex* semanticIndex() const;
    int definitionTypePriority(sym_list::sym_type_e type) const;
    bool inScope(const sym_list::SymbolInfo& symbol, const DefinitionQuery& query) const;
    bool shouldSkipForStructMemberType(const sym_list::SymbolInfo& symbol,
                                       const DefinitionQuery& query) const;
    DefinitionResult bestFromCandidates(const QList<sym_list::SymbolInfo>& candidates,
                                        const DefinitionQuery& query,
                                        bool localFile) const;
};

#endif // DEFINITIONSERVICE_H
