#include "navigationmanager.h"

#include "navigationservice.h"
#include "navigationwidget.h"
#include "semanticindexsnapshot.h"
#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QPointer>
#include "tabmanager.h"
#include "workspacemanager.h"
#include "projectmodel.h"
#include "editorfileidentity.h"
#include "../workspace/designprojectservice.h"

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

    const QStringList files = getFileHierarchyFiles();
    const bool changed = !caches.fileListValid || caches.fileList != files;
    caches.fileList = files;
    caches.fileListValid = true;
    if (changed)
        caches.fileHierarchyValid = false;
    return changed;
}

bool NavigationManager::updateDesignHierarchyData(bool force)
{
    if (designShuttingDown || !navigationService || !navigationWidget || !navigationWidget->isVisible()) return false;
    const auto token = navigationService->semanticSnapshotToken();
    const auto files = normalizedDesignScopeFiles(getSystemVerilogFiles());
    navigationWidget->setDesignCandidateContext({}, true,
        connectedWorkspaceManager && connectedWorkspaceManager->isWorkspaceScanActive());
    if (!force && caches.designHierarchyValid && caches.designSnapshotGeneration == token.revision
        && caches.designFileScope == files) return false;
    const auto same = [&](const std::optional<DesignWorkRequest>& request) {
        return request && request->token.snapshot == token.snapshot && request->token.revision == token.revision
            && request->workspace == context.currentWorkspacePath && request->files == files
            && !request->cancelled->load();
    };
    if (!force && (same(pendingDesignRequest) || same(activeDesignRequest))) return false;
    if (activeDesignRequest) activeDesignRequest->cancelled->store(true);
    if (pendingDesignRequest) pendingDesignRequest->cancelled->store(true);
    DesignWorkRequest request;
    request.token = token; request.files = files; request.workspace = context.currentWorkspacePath;
    request.telemetry = semanticAnalysisContext; request.generation = ++designRequestGeneration;
    request.cancelled = std::make_shared<std::atomic_bool>(false);
    request.previousFingerprint = caches.designStructureFingerprint;
    request.force = force;
    caches.designHierarchyValid = false;
    navigationWidget->setDesignHierarchyPending(true);
    pendingDesignRequest = std::move(request); launchPendingDesignWork();
    return false;
}
void NavigationManager::launchPendingDesignWork()
{
    if (designShuttingDown || designWatcher || !pendingDesignRequest || !navigationWidget) return;
    const auto request = *pendingDesignRequest;
    pendingDesignRequest.reset(); activeDesignRequest = request;
    auto* watcher = new QFutureWatcher<DesignWorkResult>(this); designWatcher = watcher;
    connect(watcher, &QFutureWatcher<DesignWorkResult>::finished, this, [this, watcher, request] {
        QPointer<NavigationManager> guard(this);
        const auto result = watcher->result();
        designWatcher = nullptr; activeDesignRequest.reset(); watcher->deleteLater();
        const auto token = navigationService ? navigationService->semanticSnapshotToken() : SemanticSnapshotToken();
        if (!designShuttingDown && !result.cancelled && !request.cancelled->load()
            && request.generation == designRequestGeneration && navigationWidget
            && request.workspace == context.currentWorkspacePath && token.snapshot == request.token.snapshot
            && token.revision == request.token.revision && normalizedDesignScopeFiles(getSystemVerilogFiles()) == request.files) {
            QElapsedTimer apply; apply.start();
            caches.designHierarchy = result.hierarchy; caches.designFileScope = request.files;
            caches.designSnapshotGeneration = request.token.revision;
            caches.designStructureFingerprint = result.fingerprint; caches.designHierarchyValid = true;
            caches.designRoots.clear();
            for (const auto& node : result.hierarchy.nodes) if (node.isTop)
                caches.designRoots.append({node.rootId, node.moduleType, node.moduleType, node.definitionFile, false});
            saveDesignHierarchyCache();
            const bool visible = navigationWidget->isVisible() && currentView == DesignHierarchyView;
            if (visible) {
                navigationWidget->updateDesignHierarchy(result.hierarchy);
                designHierarchyWidgetValid = true;
            } else designHierarchyWidgetValid = false;
            auto telemetry = request.telemetry;
            telemetry.stage = SemanticAnalysisStage::Navigation; telemetry.workerMs = result.workerMs;
            telemetry.uiRefreshMs = apply.elapsed();
            telemetry.detail = QStringLiteral("Design snapshot=%1 nodes=%2 structureChanged=%3 deferred=%4")
                .arg(request.token.revision).arg(result.hierarchy.nodes.size()).arg(!result.reusedStructure).arg(!visible);
            emit navigationTelemetry(telemetry);
            if (!guard) return;
            emit dataRefreshed(DesignHierarchyView);
            if (!guard) return;
            if (!pendingNavigationNode.isEmpty()) {
                const int index = caches.designHierarchy.nodeById.value(pendingNavigationNode, -1);
                pendingNavigationNode.clear();
                if (index >= 0) {
                    const auto node = caches.designHierarchy.nodes[index];
                    navigateToDesignNodeFile(pendingNavigationInstantiation ? node.instanceFile : node.definitionFile,
                        pendingNavigationInstantiation ? node.instanceLine : node.definitionLine, node);
                }
            }
        }
        if (guard) launchPendingDesignWork();
    });
    watcher->setFuture(QtConcurrent::run(&designThreadPool, [request] {
        QElapsedTimer timer; timer.start();
        DesignWorkResult result;
        SemanticIndex index(request.token);
        result.hierarchy = HierarchyService(&index).automaticDesignHierarchy(designScopeSet(request.files),
            [cancelled = request.cancelled] { return cancelled->load(); });
        result.cancelled = request.cancelled->load();
        result.fingerprint = result.hierarchy.structureFingerprint;
        result.reusedStructure = result.fingerprint == request.previousFingerprint;
        result.workerMs = timer.elapsed();
        return result;
    }));
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

QStringList NavigationManager::getFileHierarchyFiles() const
{
    if (connectedWorkspaceManager
        && connectedWorkspaceManager->isWorkspaceOpen()) {
        return connectedWorkspaceManager->getAllFiles();
    }
    if (connectedTabManager)
        return connectedTabManager->getAllOpenFileNames();
    return {};
}
