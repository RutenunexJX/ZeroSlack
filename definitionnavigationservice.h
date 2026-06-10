#ifndef DEFINITIONNAVIGATIONSERVICE_H
#define DEFINITIONNAVIGATIONSERVICE_H

#include "semanticindex.h"

#include <QString>
#include <memory>

class DefinitionService;
struct DefinitionQuery;
struct DefinitionResult;

struct DefinitionNavigationQuery {
    QString symbolName;
    QString fileName;
    QString moduleName;
    QString linePrefixBeforeCursor;
};

struct DefinitionNavigationTarget {
    bool found = false;
    bool localFile = false;
    QString symbolName;
    QString fileName;
    int line = 0;
    int column = 0;
    sym_list::sym_type_e symbolType = sym_list::sym_module;
    QString symbolTypeText;
};

class DefinitionNavigationService
{
public:
    static DefinitionNavigationService* getInstance();

    explicit DefinitionNavigationService(SemanticIndex* semanticIndex = nullptr);
    ~DefinitionNavigationService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    DefinitionNavigationTarget resolveTarget(
        const DefinitionNavigationQuery& query) const;
    bool canResolveTarget(const DefinitionNavigationQuery& query) const;
    QString tooltipText(const DefinitionNavigationQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    std::unique_ptr<DefinitionService> definitionService;
    static std::unique_ptr<DefinitionNavigationService> instance;

    static DefinitionQuery toDefinitionQuery(const DefinitionNavigationQuery& query);
    static DefinitionNavigationTarget toNavigationTarget(
        const DefinitionResult& result);
    static QString symbolTypeText(sym_list::sym_type_e symbolType);
};

#endif // DEFINITIONNAVIGATIONSERVICE_H
