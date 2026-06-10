#ifndef NAVIGATIONSERVICE_H
#define NAVIGATIONSERVICE_H

#include "definitionnavigationservice.h"
#include "hierarchyservice.h"
#include "modulehierarchymodel.h"
#include "searchservice.h"
#include "symboloutlinemodel.h"

#include <QList>
#include <QString>
#include <memory>

struct NavigationModuleQuery {
    QString filter;
};

struct NavigationSymbolOutlineQuery {
    QString fileName;
    QString filter;
};

struct NavigationModuleTarget {
    bool found = false;
    sym_list::SymbolInfo symbol;
};

class NavigationService
{
public:
    static NavigationService* getInstance();

    explicit NavigationService(SemanticIndex* semanticIndex = nullptr);
    ~NavigationService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<ModuleHierarchyGroup> findModuleHierarchy(
        const NavigationModuleQuery& query = {}) const;
    QList<SymbolOutlineGroup> findSymbolOutline(
        const NavigationSymbolOutlineQuery& query) const;
    NavigationModuleTarget resolveModuleTarget(const QString& moduleName) const;

private:
    SemanticIndex* index = nullptr;
    DefinitionNavigationService definitionNavigationService;
    SearchService searchService;
    HierarchyService hierarchyService;
    static std::unique_ptr<NavigationService> instance;

    QList<sym_list::SymbolInfo> moduleSymbols() const;
    QList<ModuleHierarchyGroup> buildModuleFileGroups(
        const QList<sym_list::SymbolInfo>& modules) const;
    QList<ModuleHierarchyGroup> buildModuleInstantiationHierarchy(
        const QList<sym_list::SymbolInfo>& modules) const;
    QList<ModuleHierarchyGroup> filterModuleHierarchy(
        const QList<ModuleHierarchyGroup>& hierarchy,
        const QString& filter) const;
    QList<sym_list::sym_type_e> outlineSymbolTypes() const;
};

#endif // NAVIGATIONSERVICE_H
