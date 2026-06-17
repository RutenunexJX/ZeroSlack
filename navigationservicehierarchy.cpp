#include "navigationservice.h"

#include "symboltaxonomy.h"

#include <QFileInfo>
#include <QSet>
#include <algorithm>

QList<sym_list::SymbolInfo> NavigationService::moduleSymbols() const
{
    SearchQuery moduleQuery;
    moduleQuery.intent = SymbolTaxonomy::SymbolSearchIntent::ModuleDeclarations;

    QList<sym_list::SymbolInfo> modules;
    const QList<SearchResult> moduleResults = searchService.findSymbols(moduleQuery);
    modules.reserve(moduleResults.size());
    for (const SearchResult& result : moduleResults)
        modules.append(result.symbol);
    return modules;
}

QList<ModuleHierarchyGroup> NavigationService::buildModuleFileGroups(
    const QList<sym_list::SymbolInfo>& modules) const
{
    QHash<QString, QStringList> groups;
    for (const sym_list::SymbolInfo& module : modules) {
        if (!module.fileName.isEmpty() && !module.symbolName.isEmpty())
            groups[module.fileName].append(module.symbolName);
    }

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        it.value().removeDuplicates();
        it.value().sort(Qt::CaseInsensitive);
    }

    QList<ModuleHierarchyGroup> result;
    result.reserve(groups.size());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        ModuleHierarchyGroup group;
        group.rootKind = ModuleHierarchyRootKind::FileGroup;
        group.rootName = it.key();
        group.rootDisplayName = QFileInfo(it.key()).fileName();
        group.rootToolTip = it.key();
        group.childModules = it.value();
        result.append(group);
    }

    std::sort(result.begin(), result.end(), [](const ModuleHierarchyGroup& a,
                                               const ModuleHierarchyGroup& b) {
        return QString::compare(a.rootDisplayName, b.rootDisplayName, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<ModuleHierarchyGroup> NavigationService::buildModuleInstantiationHierarchy(
    const QList<sym_list::SymbolInfo>& modules) const
{
    QList<ModuleHierarchyGroup> hierarchy;
    QSet<int> childModuleIds;
    QHash<int, QStringList> childrenByParentId;

    for (const sym_list::SymbolInfo& module : modules) {
        if (module.symbolId < 0 || module.symbolName.isEmpty())
            continue;

        QStringList children;
        const QList<HierarchyNode> childNodes =
            hierarchyService.moduleInstantiationChildren(symbolStableKeyForSymbol(module));
        for (const HierarchyNode& node : childNodes) {
            if (!SymbolTaxonomy::isModuleDeclaration(node.symbol)
                || node.symbol.symbolName.isEmpty()) {
                continue;
            }
            children.append(node.symbol.symbolName);
            childModuleIds.insert(node.symbol.symbolId);
        }

        if (!children.isEmpty()) {
            children.removeDuplicates();
            children.sort(Qt::CaseInsensitive);
            childrenByParentId.insert(module.symbolId, children);
        }
    }

    for (const sym_list::SymbolInfo& module : modules) {
        if (!childrenByParentId.contains(module.symbolId))
            continue;
        if (!childModuleIds.contains(module.symbolId)) {
            ModuleHierarchyGroup group;
            group.rootKind = ModuleHierarchyRootKind::ModuleRoot;
            group.rootName = module.symbolName;
            group.rootDisplayName = module.symbolName;
            group.rootToolTip = QStringLiteral("Module: %1").arg(module.symbolName);
            group.childModules = childrenByParentId.value(module.symbolId);
            hierarchy.append(group);
        }
    }

    if (hierarchy.isEmpty()) {
        for (const sym_list::SymbolInfo& module : modules) {
            if (childrenByParentId.contains(module.symbolId)) {
                ModuleHierarchyGroup group;
                group.rootKind = ModuleHierarchyRootKind::ModuleRoot;
                group.rootName = module.symbolName;
                group.rootDisplayName = module.symbolName;
                group.rootToolTip = QStringLiteral("Module: %1").arg(module.symbolName);
                group.childModules = childrenByParentId.value(module.symbolId);
                hierarchy.append(group);
            }
        }
    }

    std::sort(hierarchy.begin(), hierarchy.end(), [](const ModuleHierarchyGroup& a,
                                                     const ModuleHierarchyGroup& b) {
        return QString::compare(a.rootDisplayName, b.rootDisplayName, Qt::CaseInsensitive) < 0;
    });
    return hierarchy;
}

QList<ModuleHierarchyGroup> NavigationService::filterModuleHierarchy(
    const QList<ModuleHierarchyGroup>& hierarchy,
    const QString& filter) const
{
    if (filter.isEmpty())
        return hierarchy;

    QList<ModuleHierarchyGroup> filtered;
    for (const ModuleHierarchyGroup& group : hierarchy) {
        const bool rootMatches = group.rootDisplayName.contains(filter, Qt::CaseInsensitive)
            || group.rootName.contains(filter, Qt::CaseInsensitive);
        QStringList children;
        for (const QString& moduleName : group.childModules) {
            if (rootMatches || moduleName.contains(filter, Qt::CaseInsensitive))
                children.append(moduleName);
        }

        if (rootMatches || !children.isEmpty()) {
            ModuleHierarchyGroup filteredGroup = group;
            filteredGroup.childModules = children;
            filtered.append(filteredGroup);
        }
    }
    return filtered;
}
