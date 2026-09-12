#include "contextworkspacecontroller.h"
#include "contextfloatingwindow.h"
#include "contextdockhost.h"

#include <QDir>

QString ContextWorkspaceController::normalizedDocumentPath(const QString& path) const
{
    if (path.trimmed().isEmpty() || QDir::isAbsolutePath(path)) return {};
    QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (normalized == QStringLiteral(".")) return {};
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

QString ContextWorkspaceController::boundDocument(const QString& key) const
{
    return documentBindings.value(key);
}

bool ContextWorkspaceController::floatingEligible(const QString& key) const
{
    const QString path = documentBindings.value(key);
    return path.isEmpty() || path == activeDocumentPath;
}

void ContextWorkspaceController::touchDocumentLayout(const QString& path)
{
    if (path.isEmpty() || !documentLayouts.contains(path)) return;
    documentLayoutOrder.removeAll(path);
    documentLayoutOrder.append(path);
    while (documentLayoutOrder.size() > 32)
        documentLayouts.remove(documentLayoutOrder.takeFirst());
}

void ContextWorkspaceController::rememberDocumentLayout(const QString& path)
{
    if (path.isEmpty()) return;
    QList<ContextFloatingInstanceState> instances;
    for (auto* host : floatingWindows()) {
        if (boundDocument(host->resource().stableKey()) == path) {
            const auto instance = captureFloatingInstance(host);
            if (!instance.resource.isEmpty()) instances.append(instance);
        }
    }
    if (instances.isEmpty()) return;
    documentLayouts.insert(path, instances);
    if (!documentLayoutOrder.contains(path)) touchDocumentLayout(path);
}

void ContextWorkspaceController::forgetStoredResource(const QString& key)
{
    for (auto it = documentLayouts.begin(); it != documentLayouts.end();) {
        auto& instances = it.value();
        for (auto entry = instances.begin(); entry != instances.end();) {
            if (ContextResource::fromVariantMap(entry->resource).stableKey() == key)
                entry = instances.erase(entry);
            else ++entry;
        }
        if (instances.isEmpty()) {
            documentLayoutOrder.removeAll(it.key());
            it = documentLayouts.erase(it);
        } else ++it;
    }
}

void ContextWorkspaceController::restoreDocumentLayout(const QString& path, ContextWorkspaceRestoreResult& result)
{
    QList<ContextFloatingInstanceState> missing;
    for (const auto& instance : documentLayouts.value(path)) {
        const QString key = ContextResource::fromVariantMap(instance.resource).stableKey();
        if (!surfaceWithResource(key) && !dockHostValue->containsResource(key)) missing.append(instance);
    }
    const bool previous = restoringState;
    restoringState = true;
    restoreFloatingInstances(missing, result, path);
    restoringState = previous;
}

void ContextWorkspaceController::setActiveDocument(const QString& filePath)
{
    const QString path = normalizedDocumentPath(filePath);
    if (path == activeDocumentPath) return;
    rememberDocumentLayout(activeDocumentPath);
    activeDocumentPath = path;
    touchDocumentLayout(path);
    ContextWorkspaceRestoreResult result;
    if (!path.isEmpty()) restoreDocumentLayout(path, result);
    applyFloatingVisibility();
    updateActiveRailEntry();
    notifyWorkspaceStateChanged();
}

void ContextWorkspaceController::documentClosed(const QString& filePath)
{
    const QString path = normalizedDocumentPath(filePath);
    if (path.isEmpty()) return;
    rememberDocumentLayout(path);
    const bool previous = preservingDocumentLayout;
    const bool previousRestoring = restoringState;
    restoringState = true;
    preservingDocumentLayout = true;
    for (auto* host : floatingWindows()) {
        const QString key = host->resource().stableKey();
        if (boundDocument(key) == path) closeFloatingResource(key);
    }
    preservingDocumentLayout = previous;
    restoringState = previousRestoring;
    if (activeDocumentPath == path) activeDocumentPath.clear();
    applyFloatingVisibility();
    notifyWorkspaceStateChanged();
}

bool ContextWorkspaceController::setResourceBinding(const QString& key, ContextBinding binding, QString* failureReason)
{
    if (failureReason) failureReason->clear();
    auto* host = dynamic_cast<ContextFloatingWindow*>(surfaceWithResource(key));
    if (!host || (binding == ContextBinding::DocumentBound && activeDocumentPath.isEmpty())) {
        if (failureReason) *failureReason = host ? tr("There is no active document to bind this view to.")
                                                : tr("Only native floating views can be bound to a document.");
        return false;
    }
    forgetStoredResource(key);
    if (binding == ContextBinding::Global) documentBindings.remove(key);
    else {
        documentBindings.insert(key, activeDocumentPath);
        rememberDocumentLayout(activeDocumentPath);
        touchDocumentLayout(activeDocumentPath);
    }
    applyFloatingVisibility();
    notifyWorkspaceStateChanged();
    return true;
}
