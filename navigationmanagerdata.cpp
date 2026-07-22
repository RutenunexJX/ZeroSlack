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

bool NavigationManager::updateDesignHierarchyData(bool force)
{
    if (!navigationService)
        return false;

    const std::uint64_t snapshotRevision =
        navigationService->semanticSnapshotRevision();
    const QStringList fileScope = normalizedDesignScopeFiles(getSystemVerilogFiles());
    const QString selectedTop =
        caches.designTopInferred ? QString() : caches.designTopModule;
    const QSet<QString> fileScopeSet = designScopeSet(fileScope);

    if (!force && caches.designHierarchyValid
        && caches.designSnapshotGeneration == snapshotRevision
        && caches.designFileScope == fileScope
        && caches.designHierarchy.selectedTopModule == selectedTop
        && caches.designHierarchy.rootModules == caches.designRootModules) {
        return false;
    }

    const QByteArray structureFingerprint =
        navigationService->designStructureFingerprint(fileScopeSet,
                                                      selectedTop);

    if (!force
        && !caches.designStructureFingerprint.isEmpty()
        && caches.designStructureFingerprint == structureFingerprint
        && caches.designFileScope == fileScope
        && caches.designHierarchy.selectedTopModule == selectedTop) {
        caches.designSnapshotGeneration = snapshotRevision;
        caches.designHierarchy.snapshotGeneration = snapshotRevision;
        caches.designHierarchyValid = true;
        saveDesignHierarchyCache();
        return false;
    }

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
        caches.designStructureFingerprint = structureFingerprint;
        caches.designSnapshotGeneration = snapshotRevision;
        caches.designHierarchyValid = true;
        saveDesignHierarchyCache();
        return changed;
    }

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
        caches.designStructureFingerprint = structureFingerprint;
        caches.designHierarchyValid = true;
        saveDesignHierarchyCache();
        return changed;
    }

    caches.designHierarchy =
        navigationService->findDesignHierarchy(rootModules, selectedTop, fileScopeSet);
    caches.designSnapshotGeneration = snapshotRevision;
    caches.designFileScope = fileScope;
    caches.designStructureFingerprint = structureFingerprint;
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
