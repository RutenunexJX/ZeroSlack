#include "navigationmanager.h"

#include "navigationservice.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {
QString normalizedDesignScopeFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()))
        .toCaseFolded();
}

QStringList normalizedDesignScopeFiles(const QStringList& files)
{
    QSet<QString> seen;
    QStringList result;
    result.reserve(files.size());
    for (const QString& file : files) {
        const QString normalized = normalizedDesignScopeFileName(file);
        if (normalized.isEmpty() || seen.contains(normalized))
            continue;
        seen.insert(normalized);
        result.append(normalized);
    }
    std::sort(result.begin(), result.end(), [](const QString& lhs, const QString& rhs) {
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QSet<QString> designScopeSet(const QStringList& files)
{
    QSet<QString> result;
    result.reserve(files.size());
    for (const QString& file : files)
        result.insert(file);
    return result;
}
}

bool NavigationManager::updateFileHierarchyData()
{
    if (!shouldRefreshCache())
        return false;

    const QStringList files = getSystemVerilogFiles();
    const bool changed = !caches.fileListValid || caches.fileList != files;
    caches.fileList = files;
    caches.fileListValid = true;
    if (changed)
        caches.fileHierarchyValid = false;
    return changed;
}

bool NavigationManager::updateModuleHierarchyData()
{
    if (!navigationService)
        return false;

    if (caches.moduleHierarchyValid
        && caches.moduleHierarchyFilter == context.searchFilter) {
        return false;
    }

    NavigationModuleQuery query;
    query.filter = context.searchFilter;
    caches.moduleHierarchy = navigationService->findModuleHierarchy(query);
    caches.moduleHierarchyFilter = context.searchFilter;
    caches.moduleHierarchyValid = true;
    return true;
}

bool NavigationManager::updateSymbolHierarchyData()
{
    if (!navigationService)
        return false;

    if (caches.symbolOutlineValid
        && caches.symbolOutlineFileName == context.currentFileName
        && caches.symbolOutlineFilter == context.searchFilter) {
        return false;
    }

    NavigationSymbolOutlineQuery query;
    query.fileName = context.currentFileName;
    query.filter = context.searchFilter;
    caches.symbolOutline = navigationService->findSymbolOutline(query);
    caches.symbolOutlineFileName = context.currentFileName;
    caches.symbolOutlineFilter = context.searchFilter;
    caches.symbolOutlineValid = true;
    return true;
}

bool NavigationManager::updateDesignHierarchyData(bool force)
{
    if (!navigationService)
        return false;

    const std::uint64_t snapshotRevision =
        navigationService->semanticSnapshotRevision();
    const QStringList fileScope = normalizedDesignScopeFiles(getSystemVerilogFiles());
    const QString selectedTop =
        caches.designTopInferred ? QString() : caches.designTopModule;

    if (fileScope.isEmpty()) {
        const bool changed =
            force
            || !caches.designHierarchyValid
            || caches.designSnapshotGeneration != snapshotRevision
            || caches.designFileScope != fileScope
            || !caches.designHierarchy.topModule.isEmpty()
            || !caches.designHierarchy.nodes.isEmpty();
        caches.designHierarchy = {};
        caches.designHierarchy.snapshotGeneration = snapshotRevision;
        caches.designRootModules.clear();
        caches.designFileScope = fileScope;
        caches.designSnapshotGeneration = snapshotRevision;
        caches.designHierarchyValid = true;
        saveDesignHierarchyCache();
        return changed;
    }

    if (!force && caches.designHierarchyValid
        && caches.designSnapshotGeneration == snapshotRevision
        && caches.designFileScope == fileScope
        && caches.designHierarchy.selectedTopModule == selectedTop
        && caches.designHierarchy.rootModules == caches.designRootModules) {
        return false;
    }

    const QSet<QString> fileScopeSet = designScopeSet(fileScope);
    QStringList rootModules = navigationService->inferDesignTopModules(fileScopeSet);
    if (!caches.designTopInferred && !caches.designTopModule.isEmpty()
        && !rootModules.contains(caches.designTopModule)) {
        rootModules.prepend(caches.designTopModule);
    }
    if (caches.designTopInferred)
        caches.designTopModule = rootModules.isEmpty() ? QString() : rootModules.first();
    caches.designRootModules = rootModules;

    if (rootModules.isEmpty()) {
        const bool changed =
            force
            || !caches.designHierarchyValid
            || caches.designSnapshotGeneration != snapshotRevision
            || !caches.designHierarchy.topModule.isEmpty();
        caches.designHierarchy = {};
        caches.designHierarchy.snapshotGeneration = snapshotRevision;
        caches.designSnapshotGeneration = snapshotRevision;
        caches.designFileScope = fileScope;
        caches.designHierarchyValid = true;
        saveDesignHierarchyCache();
        return changed;
    }

    caches.designHierarchy =
        navigationService->findDesignHierarchy(rootModules, selectedTop, fileScopeSet);
    caches.designSnapshotGeneration = snapshotRevision;
    caches.designFileScope = fileScope;
    if (caches.designHierarchy.snapshotGeneration == 0)
        caches.designHierarchy.snapshotGeneration = snapshotRevision;
    caches.designHierarchyValid = true;
    saveDesignHierarchyCache();
    return true;
}

bool NavigationManager::shouldRefreshCache() const
{
    // Workspace mode owns the file list.
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return !caches.fileListValid;
    }

    // Without a workspace, derive the file list from open tabs.
    if (connectedTabManager) {
        QStringList openFiles = connectedTabManager->getOpenSystemVerilogFiles();
        return !caches.fileListValid || caches.fileList != openFiles;
    }

    return !caches.fileListValid || !caches.fileList.isEmpty();
}

QStringList NavigationManager::getSystemVerilogFiles() const
{
    // Prefer workspace files.
    if (connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceOpen()) {
        return connectedWorkspaceManager->getSystemVerilogFiles();
    }

    // Otherwise use open tab files.
    if (connectedTabManager) {
        return connectedTabManager->getOpenSystemVerilogFiles();
    }

    return QStringList();
}

QStringList NavigationManager::filterFiles(const QStringList& files, const QString& filter) const
{
    if (filter.isEmpty()) return files;

    QStringList filteredFiles;
    filteredFiles.reserve(files.size());

    for (const QString& file : files) {
        if (file.contains(filter, Qt::CaseInsensitive)) {
            filteredFiles.append(file);
        }
    }

    return filteredFiles;
}
