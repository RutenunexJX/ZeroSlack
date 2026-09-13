#include "contextworkspacecontroller.h"
#include "contextcontentprovider.h"
#include "contextdockhost.h"
#include "contextfloatingwindow.h"
#include "contextpeekhost.h"
#include "contextrail.h"
#include "roundedicons.h"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QScreen>
#include <QGuiApplication>

ContextFloatingSurface* ContextWorkspaceController::floatingSurfaceFor() const
{
    return activeFloatingSurface;
}

ContextFloatingSurface* ContextWorkspaceController::floatingSurfaceFor(const ContextViewCapabilities& capabilities)
{
    if (!capabilities.detachable)
        return peekHostValue;
    auto* host = availableFloatingWindow();
    host->setInitialSize(capabilities.preferredSize());
    ContextWorkspaceState geometry = lastFloatingGeometry;
    if (geometry.floatingGeometryValid && !restoringState) {
        const QRect available = window->screen()->availableGeometry();
        const int offset = 24;
        const QSize size(qMin(geometry.floatingWidth, qMax(ContextWorkspaceState::kMinimumPeekWidth, available.width() - offset * 2)),
                         qMin(geometry.floatingHeight, qMax(ContextWorkspaceState::kMinimumPeekHeight, available.height() - offset * 2)));
        int x = geometry.floatingX + offset;
        int y = geometry.floatingY + offset;
        if (x + size.width() > available.x() + available.width()) x = available.x() + offset;
        if (y + size.height() > available.y() + available.height()) y = available.y() + offset;
        const QRect cascade = ContextWorkspaceState::resolvedFloatingGeometry(
            QRect(x, y, size.width(), size.height()),
            {}, {}, available, {});
        geometry.floatingX = cascade.x();
        geometry.floatingY = cascade.y();
        geometry.floatingWidth = cascade.width();
        geometry.floatingHeight = cascade.height();
        geometry.floatingScreenName = window->screen()->name();
    }
    host->restoreGeometry(geometry);
    return host;
}

ContextFloatingWindow* ContextWorkspaceController::availableFloatingWindow()
{
    for (const auto& host : floatingWindowValues) {
        if (host && !host->hasResource()) return host;
    }
    auto* host = new ContextFloatingWindow(window, editorRegionValue);
    floatingWindowValues.append(host);
    host->setIdleOpacity(floatingOpacity);
    host->installEventFilter(this);
    connect(host, &ContextFloatingWindow::pinRequested, this, [this, host] {
        pinFloatingResource(host->resource().stableKey());
    });
    connect(host, &ContextFloatingWindow::closeRequested, this, [this, host] {
        closeFloatingResource(host->resource().stableKey());
    });
    connect(host, &ContextFloatingWindow::fullViewRequested, this, [this, host] {
        if (host->hasResource()) emit fullViewRequested(host->resource());
    });
    connect(host, &ContextFloatingWindow::geometryChanged, this, [this, host] {
        captureLastFloatingGeometry(host);
        notifyWorkspaceStateChanged();
    });
    connect(host, &ContextFloatingWindow::sidebarDragStarted, this, [this, host] {
        host->setProperty("contextDockWasVisible", dockValue->isVisible());
        showDock(false);
    });
    connect(host, &ContextFloatingWindow::sidebarDragFinished, this, [this, host](bool accepted) {
        if (!accepted && !host->property("contextDockWasVisible").toBool()) dockValue->hide();
    });
    return host;
}

QList<ContextFloatingWindow*> ContextWorkspaceController::floatingWindows() const
{
    QList<ContextFloatingWindow*> result;
    for (const auto& host : floatingWindowValues)
        if (host && host->hasResource()) result.append(host);
    return result;
}

ContextFloatingWindow* ContextWorkspaceController::floatingWindow() const
{
    if (auto* host = dynamic_cast<ContextFloatingWindow*>(activeFloatingSurface)) return host;
    return floatingWindowValues.isEmpty() ? nullptr : floatingWindowValues.last().data();
}

QList<ContextFloatingSurface*> ContextWorkspaceController::floatingSurfaces() const
{
    QList<ContextFloatingSurface*> result;
    if (peekHostValue && peekHostValue->hasResource()) result.append(peekHostValue);
    for (auto* host : floatingWindows()) result.append(host);
    return result;
}

ContextFloatingSurface* ContextWorkspaceController::surfaceWithResource(const QString& key) const
{
    for (auto* host : floatingSurfaces())
        if (host->resource().stableKey() == key) return host;
    return nullptr;
}

void ContextWorkspaceController::recordFocus(QString key)
{
    if (key.isEmpty()) return;
    focusedResourceKey = key;
    focusOrder.removeAll(key);
    focusOrder.prepend(key);
    if (auto* surface = surfaceWithResource(key)) activeFloatingSurface = surface;
    updateActiveRailEntry();
}

bool ContextWorkspaceController::focusResource(const QString& key)
{
    if (auto* surface = surfaceWithResource(key)) {
        if (!floatingEligible(key)) return false;
        activeFloatingSurface = surface;
        hiddenFloatingKeys.remove(key);
        if (floatingCollapsedValue) setFloatingCollapsed(false);
        auto* widget = dynamic_cast<QWidget*>(surface);
        widget->show();
        widget->raise();
        if (widget->isWindow()) widget->activateWindow();
    } else if (dockHostValue->containsResource(key)) {
        dockHostValue->activateResource(key);
        showDock(false);
    } else {
        return false;
    }
    recordFocus(key);
    return true;
}

bool ContextWorkspaceController::closeFloatingResource(const QString& key)
{
    auto* surface = surfaceWithResource(key);
    if (!surface) return false;
    activeFloatingSurface = surface;
    closePeek();
    return true;
}

bool ContextWorkspaceController::pinFloatingResource(const QString& key)
{
    auto* surface = surfaceWithResource(key);
    if (!surface) return false;
    activeFloatingSurface = surface;
    const bool pinned = pinPeek();
    if (pinned) hiddenFloatingKeys.remove(key);
    return pinned;
}

bool ContextWorkspaceController::dragOutResource(const QString& key, const QPoint& globalPosition, QString* failureReason)
{
    if (failureReason) failureReason->clear();
    ContextResource resource;
    for (int i = 0; i < dockHostValue->resourceCount(); ++i)
        if (dockHostValue->resourceAt(i).stableKey() == key) resource = dockHostValue->resourceAt(i);
    auto* provider = providerFor(resource);
    if (!provider || !provider->capabilities(resource).detachable) {
        if (failureReason) *failureReason = tr("This resource does not support native floating windows.");
        return false;
    }
    if (!unpinResource(key, failureReason)) return false;
    auto* host = dynamic_cast<ContextFloatingWindow*>(surfaceWithResource(key));
    if (!host) return false;
    QScreen* screen = QGuiApplication::screenAt(globalPosition);
    if (!screen) screen = window->screen();
    const QRect available = screen->availableGeometry();
    const QRect rect = ContextWorkspaceState::resolvedFloatingGeometry(
        QRect(globalPosition, host->frameGeometry().size()), {}, {}, available, {});
    ContextWorkspaceState geometry;
    geometry.floatingGeometryValid = true;
    geometry.floatingX = rect.x(); geometry.floatingY = rect.y();
    geometry.floatingWidth = rect.width(); geometry.floatingHeight = rect.height();
    geometry.floatingScreenName = screen->name();
    host->restoreGeometry(geometry);
    captureLastFloatingGeometry(host);
    notifyWorkspaceStateChanged();
    return true;
}

void ContextWorkspaceController::setFloatingOpacity(int percentage)
{
    floatingOpacity = qBound(60, percentage, 100);
    for (const auto& host : floatingWindowValues)
        if (host) host->setIdleOpacity(floatingOpacity);
}

bool ContextWorkspaceController::floatingCollapsed() const { return floatingCollapsedValue; }

void ContextWorkspaceController::applyFloatingVisibility()
{
    for (auto* surface : floatingSurfaces()) {
        auto* widget = dynamic_cast<QWidget*>(surface);
        const QString key = surface->resource().stableKey();
        widget->setVisible(!floatingCollapsedValue && !hiddenFloatingKeys.contains(key) && floatingEligible(key));
    }
}

void ContextWorkspaceController::setFloatingCollapsed(bool collapsed)
{
    floatingCollapsedValue = collapsed;
    collapseFloatingAction->setChecked(collapsed);
    collapseFloatingAction->setText(collapsed ? tr("Restore floating views") : tr("Hide floating views"));
    collapseFloatingAction->setIcon(RoundedIcons::icon(collapsed ? RoundedIcons::Expand : RoundedIcons::Collapse));
    applyFloatingVisibility();
    updateActiveRailEntry();
    notifyWorkspaceStateChanged();
}

void ContextWorkspaceController::captureLastFloatingGeometry(ContextFloatingWindow* host)
{
    host->captureGeometry(lastFloatingGeometry);
}

ContextFloatingInstanceState ContextWorkspaceController::captureFloatingInstance(ContextFloatingWindow* host) const
{
    ContextFloatingInstanceState instance;
    const auto resource = host->resource();
    auto* provider = providerFor(resource);
    if (!provider) return instance;
    auto persisted = provider->resourceForPersistence(resource, host->view(), currentWorkspaceRoot);
    if (!persisted.isValid()) return instance;
    persisted.workspaceId.clear();
    instance.resource = persisted.toVariantMap();
    ContextWorkspaceState geometry;
    host->captureGeometry(geometry);
    instance.x = geometry.floatingX;
    instance.y = geometry.floatingY;
    instance.width = geometry.floatingWidth;
    instance.height = geometry.floatingHeight;
    instance.screenName = geometry.floatingScreenName;
    instance.geometryValid = geometry.floatingGeometryValid;
    instance.kept = keptFloatingKeys.contains(resource.stableKey());
    return instance;
}

void ContextWorkspaceController::restoreFloatingInstances(const QList<ContextFloatingInstanceState>& instances,
                                                        ContextWorkspaceRestoreResult& result, const QString& documentPath)
{
    const ContextWorkspaceState savedLast = lastFloatingGeometry;
    int restoredCount = floatingWindows().size();
    for (const auto& instance : instances) {
        QString reason;
        auto persisted = ContextResource::fromVariantMap(instance.resource, &reason);
        auto* provider = providerForId(persisted.providerId);
        auto restored = provider ? provider->resourceFromPersistence(persisted, currentWorkspaceRoot) : ContextResource{};
        restored.workspaceId = currentWorkspaceRoot;
        if (restoredCount >= 16 || !restored.isValid() || surfaceWithResource(restored.stableKey())
            || dockHostValue->containsResource(restored.stableKey())) {
            ++result.skippedResources;
            result.warnings.append(restoredCount >= 16 ? tr("Floating restore limit is 16; remaining resources were skipped.")
                                                       : tr("Floating resource is unavailable or duplicated."));
            continue;
        }
        if (floatingSurfaceFor(provider->capabilities(restored)) == peekHostValue) {
            ++result.skippedResources;
            result.warnings.append(tr("A native floating resource cannot be restored into the editor overlay."));
            continue;
        }
        lastFloatingGeometry.floatingX = instance.x;
        lastFloatingGeometry.floatingY = instance.y;
        lastFloatingGeometry.floatingWidth = instance.width;
        lastFloatingGeometry.floatingHeight = instance.height;
        lastFloatingGeometry.floatingScreenName = instance.screenName;
        lastFloatingGeometry.floatingGeometryValid = instance.geometryValid;
        const ContextPlacement placement{ContextSurface::Floating,
            instance.kept ? ContextPersistence::Kept : ContextPersistence::Transient,
            documentPath.isEmpty() ? ContextBinding::Global : ContextBinding::DocumentBound};
        if (!openResource(restored, placement, &reason)) {
            ++result.skippedResources;
            result.warnings.append(reason);
            continue;
        }
        ++restoredCount;
        ++result.restoredResources;
    }
    lastFloatingGeometry = savedLast;
    applyFloatingVisibility();
}

QMenu* ContextWorkspaceController::createRailContextMenu(const QString& providerId)
{
    auto* menu = new QMenu(railValue);
    menu->setToolTipsVisible(true);
    menu->setObjectName(QStringLiteral("contextRailMenu"));
    auto* provider = providerForId(providerId);
    const ContextResource activation = provider ? provider->activationResource(currentWorkspaceRoot) : ContextResource{};
    QAction* create = menu->addAction(tr("New global floating view"));
    create->setObjectName(QStringLiteral("contextNewFloating"));
    create->setEnabled(activation.isValid());
    connect(create, &QAction::triggered, this, [this, activation] {
        openResource(activation, {ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global});
    });
    QAction* createBound = menu->addAction(tr("New floating view bound to current document"));
    createBound->setObjectName(QStringLiteral("contextNewDocumentFloating"));
    createBound->setEnabled(!activeDocumentPath.isEmpty() && activation.isValid()
        && floatingSurfaceFor(provider->capabilities(activation)) != peekHostValue);
    createBound->setToolTip(activeDocumentPath.isEmpty() ? tr("Open a source document first.")
                                                       : tr("Bind to %1").arg(activeDocumentPath));
    connect(createBound, &QAction::triggered, this, [this, activation] {
        openResource(activation, {ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::DocumentBound});
    });
    QStringList keys;
    for (auto* surface : floatingSurfaces())
        if (surface->resource().providerId == providerId) keys.append(surface->resource().stableKey());
    for (int i = 0; i < dockHostValue->resourceCount(); ++i)
        if (dockHostValue->resourceAt(i).providerId == providerId) keys.append(dockHostValue->resourceAt(i).stableKey());
    if (!keys.isEmpty()) menu->addSeparator();
    for (const auto& key : keys) {
        auto* surface = surfaceWithResource(key);
        QString title = surface ? surface->resource().title : QString();
        if (!surface) {
            for (int i = 0; i < dockHostValue->resourceCount(); ++i)
                if (dockHostValue->resourceAt(i).stableKey() == key) title = dockHostValue->resourceAt(i).title;
        }
        QAction* item = menu->addAction(title);
        item->setData(key);
        item->setEnabled(!surface || floatingEligible(key));
        if (!floatingEligible(key)) item->setToolTip(tr("Switch to %1 to show this view.").arg(boundDocument(key)));
        connect(item, &QAction::triggered, this, [this, key] { focusResource(key); });
        if (dynamic_cast<ContextFloatingWindow*>(surface)) {
            QMenu* binding = menu->addMenu(tr("Binding: %1").arg(title));
            QAction* global = binding->addAction(tr("Switch to global"));
            global->setObjectName(QStringLiteral("contextBindingGlobal"));
            global->setEnabled(!boundDocument(key).isEmpty());
            connect(global, &QAction::triggered, this, [this, key] { setResourceBinding(key, ContextBinding::Global); });
            QAction* document = binding->addAction(tr("Bind to current document"));
            document->setObjectName(QStringLiteral("contextBindingDocument"));
            document->setEnabled(!activeDocumentPath.isEmpty());
            document->setToolTip(activeDocumentPath.isEmpty() ? tr("Open a source document first.") : activeDocumentPath);
            connect(document, &QAction::triggered, this, [this, key] { setResourceBinding(key, ContextBinding::DocumentBound); });
        }
    }
    QAction* collect = menu->addAction(tr("Move all floating views to sidebar"));
    collect->setObjectName(QStringLiteral("contextCollectFloating"));
    collect->setEnabled(false);
    for (auto* surface : floatingSurfaces())
        if (surface->resource().providerId == providerId) collect->setEnabled(true);
    connect(collect, &QAction::triggered, this, [this, providerId] {
        for (auto* surface : floatingSurfaces())
            if (surface->resource().providerId == providerId) pinFloatingResource(surface->resource().stableKey());
    });
    return menu;
}
