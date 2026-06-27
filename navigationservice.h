#ifndef NAVIGATIONSERVICE_H
#define NAVIGATIONSERVICE_H

#include "definitionnavigationservice.h"
#include "hierarchyservice.h"
#include "modulehierarchymodel.h"
#include "searchservice.h"
#include "symboloutlinemodel.h"

#include <QList>
#include <QSet>
#include <QString>
#include <cstdint>
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
    SymbolOutlineSymbolRow symbolRow;
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
    QString inferDesignTopModule() const;
    QString inferDesignTopModule(const QSet<QString>& fileScope) const;
    QStringList inferDesignTopModules() const;
    QStringList inferDesignTopModules(const QSet<QString>& fileScope) const;
    DesignHierarchyReport findDesignHierarchy(const QString& topModule) const;
    DesignHierarchyReport findDesignHierarchy(
        const QString& topModule,
        const QSet<QString>& fileScope) const;
    DesignHierarchyReport findDesignHierarchy(
        const QStringList& topModules,
        const QString& selectedTopModule = QString()) const;
    DesignHierarchyReport findDesignHierarchy(
        const QStringList& topModules,
        const QString& selectedTopModule,
        const QSet<QString>& fileScope) const;
    std::uint64_t semanticSnapshotRevision() const;
    QStringList modulesDefinedInFile(const QString& fileName) const;
    NavigationModuleTarget resolveModuleTarget(const QString& moduleName) const;

private:
    SemanticIndex* index = nullptr;
    DefinitionNavigationService definitionNavigationService;
    SearchService searchService;
    HierarchyService hierarchyService;
    static std::unique_ptr<NavigationService> instance;

    QList<SemanticSymbolRecord> moduleRecords() const;
    QList<ModuleHierarchyGroup> buildModuleFileGroups(
        const QList<SemanticSymbolRecord>& modules) const;
    QList<ModuleHierarchyGroup> buildModuleInstantiationHierarchy(
        const QList<SemanticSymbolRecord>& modules) const;
    QList<ModuleHierarchyGroup> filterModuleHierarchy(
        const QList<ModuleHierarchyGroup>& hierarchy,
        const QString& filter) const;
};

#endif // NAVIGATIONSERVICE_H
