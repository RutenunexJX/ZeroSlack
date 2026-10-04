#include "navigationmanager.h"

#include "navigationservice.h"
#include "navigationwidget.h"
#include "semanticindexsnapshot.h"
#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QPointer>
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

namespace {
// Row identities belong to the existing tree. A source-only update can replace
// all navigation anchors while preserving expansion, selection and row objects.
bool reuseDesignRowIds(DesignHierarchyReport& report, const DesignHierarchyReport& previous)
{
    auto key = [](const DesignHierarchyNode& node) {
        return QString::number(node.rootModule.size()) + ':' + node.rootModule
            + QString::number(node.instancePath.size()) + ':' + node.instancePath
            + QString::number(node.moduleType.size()) + ':' + node.moduleType;
    };
    if (report.nodes.size() != previous.nodes.size()) return false;
    QHash<QString, QString> oldIds, replacements;
    for (const auto& node : previous.nodes) {
        const auto identity = key(node);
        if (oldIds.contains(identity)) return false;
        oldIds.insert(identity, node.id);
    }
    QSet<QString> used;
    for (const auto& node : report.nodes) {
        const auto identity = key(node);
        if (!oldIds.contains(identity) || used.contains(identity)) return false;
        used.insert(identity);
        replacements.insert(node.id, oldIds.value(identity));
    }
    for (auto& node : report.nodes) {
        node.id = replacements.value(node.id);
        node.parentId = replacements.value(node.parentId);
        node.rootId = replacements.value(node.rootId);
    }
    return true;
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
    if (designShuttingDown || !navigationService || !navigationWidget || !navigationWidget->isVisible())
        return false;
    const auto token = navigationService->semanticSnapshotToken();
    const auto files = normalizedDesignScopeFiles(getSystemVerilogFiles());
    const auto top = caches.designTopInferred ? QString() : caches.designTopModule;
    if (!force && caches.designHierarchyValid && caches.designSnapshotGeneration == token.revision
        && caches.designFileScope == files && caches.designHierarchy.selectedTopModule == top)
        return false;
    auto sameRequest = [&](const std::optional<DesignWorkRequest>& pending) {
        return pending && pending->token.snapshot == token.snapshot && pending->token.revision == token.revision
            && pending->files == files && pending->selectedTop == top
            && pending->workspace == context.currentWorkspacePath
            && pending->generation == designRequestGeneration;
    };
    if (!force && (sameRequest(pendingDesignRequest) || sameRequest(activeDesignRequest)))
        return false;
    DesignWorkRequest request;
    request.token = token;
    request.files = files;
    request.selectedTop = top;
    request.workspace = context.currentWorkspacePath;
    request.telemetry = semanticAnalysisContext;
    request.generation = ++designRequestGeneration;
    request.force = force;
    if (caches.designFileScope == files && caches.designHierarchy.selectedTopModule == top) {
        request.previousHierarchy = caches.designHierarchy;
        request.previousFingerprint = caches.designStructureFingerprint;
        request.previousAnchorFingerprint = caches.designAnchorFingerprint;
    }
    caches.designHierarchyValid = false;
    if (caches.designFileScope != files || caches.designHierarchy.selectedTopModule != top) {
        navigationWidget->clearDesignHierarchy();
        designHierarchyWidgetValid = false;
    }
    // Keep the same-scope presentation inert while its immutable input is
    // checked; a presentation-only publication can reuse its existing rows.
    navigationWidget->setDesignHierarchyPending(true);
    pendingDesignRequest = std::move(request);
    launchPendingDesignWork();
    return false;
}

void NavigationManager::launchPendingDesignWork()
{
    if (designShuttingDown || designWatcher || !pendingDesignRequest
        || !navigationWidget || !navigationWidget->isVisible())
        return;
    const auto request = *pendingDesignRequest;
    pendingDesignRequest.reset();
    activeDesignRequest = request;
    auto* watcher = new QFutureWatcher<DesignWorkResult>(this);
    designWatcher = watcher;
    connect(watcher, &QFutureWatcher<DesignWorkResult>::finished, this, [this, watcher, request] {
        const auto result = watcher->future().takeResult();
        designWatcher = nullptr;
        activeDesignRequest.reset();
        watcher->deleteLater();
        const auto currentToken = navigationService ? navigationService->semanticSnapshotToken() : SemanticSnapshotToken{};
        const QString currentTop = caches.designTopInferred ? QString() : caches.designTopModule;
        if (!designShuttingDown && request.generation == designRequestGeneration
            && navigationWidget && navigationWidget->isVisible()
            && context.currentWorkspacePath == request.workspace && currentTop == request.selectedTop
            && currentToken.snapshot == request.token.snapshot && currentToken.revision == request.token.revision
            && normalizedDesignScopeFiles(getSystemVerilogFiles()) == request.files) {
            QElapsedTimer applyTimer;
            applyTimer.start();
            const bool keepTree = designHierarchyWidgetValid && result.reusedStructure && !request.force
                && caches.designFileScope == request.files
                && caches.designStructureFingerprint == result.fingerprint
                && caches.designHierarchy.selectedTopModule == request.selectedTop;
            caches.designHierarchy = result.hierarchy;
            caches.designRootModules = result.roots;
            if (caches.designTopInferred)
                caches.designTopModule = result.roots.value(0);
            caches.designFileScope = request.files;
            caches.designStructureFingerprint = result.fingerprint;
            caches.designAnchorFingerprint = result.anchorFingerprint;
            caches.designSnapshotGeneration = request.token.revision;
            caches.designHierarchyValid = true;
            saveDesignHierarchyCache();
            QPointer<NavigationManager> self(this);
            if (currentView == DesignHierarchyView) {
                if (keepTree)
                    navigationWidget->updateDesignSummary(caches.designHierarchy, false);
                else
                    navigationWidget->updateDesignHierarchy(caches.designHierarchy);
                if (!self)
                    return;
                designHierarchyWidgetValid = true;
            } else {
                navigationWidget->updateDesignSummary(caches.designHierarchy, !keepTree);
                if (!self)
                    return;
                designHierarchyWidgetValid = keepTree;
            }
            auto telemetry = request.telemetry;
            telemetry.stage = SemanticAnalysisStage::Navigation;
            telemetry.uiRefreshMs = applyTimer.elapsed();
            telemetry.detail = QStringLiteral("Design view applied snapshot=%1 hierarchyRebuild=%2").arg(request.token.revision).arg(keepTree ? 0 : 1);
            emit navigationTelemetry(telemetry);
            if (!self)
                return;
            emit dataRefreshed(DesignHierarchyView);
            if (!self)
                return;
        }
        launchPendingDesignWork();
    });
    watcher->setFuture(QtConcurrent::run(&designThreadPool, [request] {
        DesignWorkResult result;
        result.hierarchy.snapshotGeneration = request.token.revision;
        result.hierarchy.selectedTopModule = request.selectedTop;
        if (!request.token.snapshot || request.files.isEmpty())
            return result;
        SemanticIndex readIndex(request.token);
        NavigationService reader(&readIndex);
        const auto scope = designScopeSet(request.files);
        result.fingerprint = reader.designStructureFingerprint(scope, request.selectedTop);
        result.anchorFingerprint = reader.designStructureFingerprint(scope, request.selectedTop, true);
        const bool sameStructure = !request.force && !request.previousFingerprint.isEmpty()
            && result.fingerprint == request.previousFingerprint;
        if (sameStructure && result.anchorFingerprint == request.previousAnchorFingerprint) {
            result.reusedStructure = true;
            result.hierarchy = request.previousHierarchy;
            result.hierarchy.snapshotGeneration = request.token.revision;
            result.roots = result.hierarchy.rootModules;
            return result;
        }
        result.roots = reader.inferDesignTopModules(scope);
        if (!request.selectedTop.isEmpty() && !result.roots.contains(request.selectedTop))
            result.roots.prepend(request.selectedTop);
        if (!result.roots.isEmpty())
            result.hierarchy = reader.findDesignHierarchy(result.roots, request.selectedTop, scope);
        result.hierarchy.snapshotGeneration = request.token.revision;
        result.hierarchy.selectedTopModule = request.selectedTop;
        result.reusedStructure = sameStructure && reuseDesignRowIds(result.hierarchy, request.previousHierarchy);
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
