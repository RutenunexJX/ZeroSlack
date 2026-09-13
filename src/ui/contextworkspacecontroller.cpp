#include "contextworkspacecontroller.h"

#include "applicationthememanager.h"
#include "contextcontentprovider.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextfloatingwindow.h"
#include "contextrail.h"
#include "insightvisualstyle.h"
#include "roundedicons.h"

#include <QDockWidget>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QWidget>
#include <QApplication>
#include <QAction>
#include <QMenu>

#include <array>

namespace {
QString normalizedRoot(const QString& root)
{
    if (root.trimmed().isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(root).absoluteFilePath()));
}

bool sameRoot(const QString& lhs, const QString& rhs)
{
#ifdef Q_OS_WIN
    return normalizedRoot(lhs).compare(
               normalizedRoot(rhs), Qt::CaseInsensitive)
        == 0;
#else
    return normalizedRoot(lhs) == normalizedRoot(rhs);
#endif
}

bool hasBuiltInProviderIcon(const QString& providerId)
{
    return providerId == QStringLiteral("workspaceHub")
        || providerId == QStringLiteral("temporaryEditor")
        || providerId == QStringLiteral("liveInsights")
        || providerId.startsWith(QStringLiteral("rtlInsight."))
        || providerId == QStringLiteral("pinloom");
}

QIcon contextProviderIcon(
    const QString& providerId,
    const QString& iconName,
    QStyle* fallbackStyle)
{
    if (!hasBuiltInProviderIcon(providerId)) {
        QIcon icon = QIcon::fromTheme(iconName);
        if (icon.isNull() && fallbackStyle) {
            icon = fallbackStyle->standardIcon(
                QStyle::SP_FileDialogContentsView);
        }
        return icon;
    }

    using namespace RoundedIcons;
    if (providerId == QStringLiteral("workspaceHub")) return icon(Grid);
    if (providerId == QStringLiteral("temporaryEditor")) return icon(File);
    if (providerId == QStringLiteral("rtlInsight.kernel")) return icon(Module);
    if (providerId == QStringLiteral("rtlInsight.block")) return icon(Hierarchy);
    if (providerId == QStringLiteral("rtlInsight.hotspot")) return icon(Signals);
    if (providerId == QStringLiteral("rtlInsight.state")) return icon(Context);
    if (providerId == QStringLiteral("rtlInsight.wave")) return icon(Wave);
    if (providerId == QStringLiteral("liveInsights")) return icon(Activity);
    return icon(Bookmark);
}
}

ContextWorkspaceController::ContextWorkspaceController(
    QMainWindow* mainWindow,
    QWidget* editorRegion,
    QObject* parent)
    : QObject(parent)
    , window(mainWindow)
    , editorRegionValue(editorRegion)
{
    Q_ASSERT(mainWindow);
    Q_ASSERT(editorRegion);

    railValue = new ContextRail(mainWindow);
    mainWindow->addToolBar(Qt::RightToolBarArea, railValue);

    peekHostValue = new ContextPeekHost(editorRegion);
    activeFloatingSurface = peekHostValue;
    availableFloatingWindow();
    QAction* footer = railValue->addSeparator();
    footer->setObjectName(QStringLiteral("contextFloatingFooter"));
    collapseFloatingAction = railValue->addAction(RoundedIcons::icon(RoundedIcons::Collapse), tr("Hide floating views"));
    collapseFloatingAction->setObjectName(QStringLiteral("contextFloatingCollapse"));
    collapseFloatingAction->setCheckable(true);
    connect(collapseFloatingAction, &QAction::triggered, this, &ContextWorkspaceController::setFloatingCollapsed);
    connect(railValue, &ContextRail::entryContextMenuRequested, this, [this](const QString& id, const QPoint& pos) {
        QMenu* menu = createRailContextMenu(id);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(pos);
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (!now) return;
        for (auto* surface : floatingSurfaces()) {
            auto* host = dynamic_cast<QWidget*>(surface);
            if (host == now || host->isAncestorOf(now)) {
                recordFocus(surface->resource().stableKey());
                return;
            }
        }
        if (dockHostValue && dockHostValue->isAncestorOf(now))
            recordFocus(dockHostValue->currentResource().stableKey());
        else if (editorRegionValue && (now == editorRegionValue || editorRegionValue->isAncestorOf(now)))
            focusedResourceKey.clear();
    });

    dockValue = new QDockWidget(tr("Context"), mainWindow);
    dockValue->setObjectName(QStringLiteral("contextWorkspaceDock"));
    dockValue->setAllowedAreas(Qt::RightDockWidgetArea
                               | Qt::LeftDockWidgetArea);
    dockValue->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    dockHostValue = new ContextDockHost(dockValue);
    dockValue->setWidget(dockHostValue);
    dockHostValue->setMinimumWidth(ContextWorkspaceState::kMinimumDockWidth);
    if (mainWindow->centralWidget())
        mainWindow->centralWidget()->setMinimumWidth(240);
    preferredDockWidthValue = qMax(
        ContextWorkspaceState::kMinimumDockWidth, mainWindow->width() * 3 / 10);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dockValue);
    dockValue->hide();

    connect(railValue,
            &ContextRail::entryActivated,
            this,
            &ContextWorkspaceController::activateRailProvider);
    connect(peekHostValue,
            &ContextPeekHost::pinRequested,
            this,
            [this]() { activeFloatingSurface = peekHostValue; pinPeek(); });
    connect(peekHostValue,
            &ContextPeekHost::closeRequested,
            this,
            [this]() { activeFloatingSurface = peekHostValue; closePeek(); });
    connect(peekHostValue,
            &ContextPeekHost::fullViewRequested,
            this,
            [this]() {
                if (peekHostValue && peekHostValue->hasResource())
                    emit fullViewRequested(peekHostValue->resource());
            });
    connect(peekHostValue,
            &ContextPeekHost::preferredSizeChanged,
            this,
            [this](const QSize&) {
                notifyWorkspaceStateChanged();
            });
    connect(peekHostValue,
            &ContextPeekHost::preferredSizeResetRequested,
            this,
            &ContextWorkspaceController::
                resetPeekToProviderPreferredSize);
    connect(dockHostValue, &ContextDockHost::sectionLayoutChanged, this,
            &ContextWorkspaceController::notifyWorkspaceStateChanged);
    connect(dockHostValue, &ContextDockHost::dragOutRequested, this, [this](const QString& key, const QPoint& position) {
        dragOutResource(key, position);
    });
    connect(dockHostValue, &ContextDockHost::floatingDropRequested, this, [this](const QString& key, int index) {
        if (pinFloatingResource(key)) dockHostValue->moveResource(key, index);
    });
    connect(dockHostValue,
            &ContextDockHost::closeResourceRequested,
            this,
            &ContextWorkspaceController::closePinnedResource);
    connect(dockHostValue,
            &ContextDockHost::unpinResourceRequested,
            this,
            [this](const QString& key) {
                unpinResource(key);
            });
    connect(dockHostValue,
            &ContextDockHost::fullViewResourceRequested,
            this,
            &ContextWorkspaceController::fullViewRequested);
    connect(dockHostValue,
            &ContextDockHost::currentResourceChanged,
            this,
            [this](const ContextResource& resource) {
                if (resource.isValid()) recordFocus(resource.stableKey());
                updateActiveRailEntry();
                emit activeResourceChanged(resource);
                notifyWorkspaceStateChanged();
            });
    connect(dockHostValue,
            &ContextDockHost::resourceOrderChanged,
            this,
            &ContextWorkspaceController::notifyWorkspaceStateChanged);
    connect(dockValue,
            &QDockWidget::visibilityChanged,
            this,
            [this](bool) {
                updateActiveRailEntry();
                notifyWorkspaceStateChanged();
            });
    connect(
        &ApplicationThemeManager::instance(),
        &ApplicationThemeManager::themeChanged,
        this,
        [this](ThemeMode) { refreshProviderIcons(); });
    dockValue->installEventFilter(this);
}

ContextWorkspaceController::~ContextWorkspaceController()
{
    restoringState = true;
    for (auto* surface : floatingSurfaces()) {
        activeFloatingSurface = surface;
        closePeek();
    }
    if (dockHostValue) {
        const QStringList keys = dockHostValue->resourceKeys();
        for (const QString& key : keys)
            closePinnedResource(key);
    }
}

ContextRail* ContextWorkspaceController::rail() const
{
    return railValue;
}

ContextPeekHost* ContextWorkspaceController::peekHost() const
{
    return peekHostValue;
}

ContextFloatingSurface* ContextWorkspaceController::floatingSurface() const { return floatingSurfaceFor(); }
QWidget* ContextWorkspaceController::floatingWidget() const { return dynamic_cast<QWidget*>(floatingSurfaceFor()); }

ContextDockHost* ContextWorkspaceController::dockHost() const
{
    return dockHostValue;
}

QDockWidget* ContextWorkspaceController::dockWidget() const
{
    return dockValue;
}

bool ContextWorkspaceController::dockVisible() const
{
    return dockValue && dockValue->isVisible();
}

bool ContextWorkspaceController::canShowDock() const
{
    if (!dockValue || !dockHostValue)
        return false;
    return dockHostValue->resourceCount() > 0
        || (railValue && !railValue->entryIds().isEmpty());
}

bool ContextWorkspaceController::setDockVisible(
    bool visible,
    QString* failureReason)
{
    const auto fail = [failureReason](const QString& reason) {
        if (failureReason)
            *failureReason = reason;
        return false;
    };
    if (!dockValue || !dockHostValue) {
        return fail(QStringLiteral(
            "The Context sidebar is unavailable."));
    }
    if (!visible) {
        if (dockValue->isVisible())
            dockValue->hide();
        return true;
    }
    // The rail is the sidebar's entry point, so a hidden rail is restored
    // together with the sidebar instead of leaving Context unreachable.
    if (railValue && !railValue->isVisible()
        && !railValue->entryIds().isEmpty()) {
        railValue->show();
        notifyWorkspaceStateChanged();
    }
    if (dockValue->isVisible())
        return true;
    if (dockHostValue->resourceCount() > 0) {
        showDock(false);
        return true;
    }
    const QStringList entries =
        railValue ? railValue->entryIds() : QStringList();
    if (entries.isEmpty()) {
        return fail(QStringLiteral(
            "No Context provider is registered."));
    }
    const QString providerId = entries.constFirst();
    IContextContentProvider* provider =
        providerForId(providerId);
    const ContextResource activation =
        provider
        ? provider->activationResource(currentWorkspaceRoot)
        : ContextResource{};
    QString openFailure;
    if (!activation.isValid()
        || !openResource(activation,
                         defaultPlacementFor(providerId),
                         &openFailure)
        || !dockValue->isVisible()) {
        return fail(openFailure.isEmpty()
                        ? QStringLiteral(
                              "The Context provider could not open "
                              "a sidebar view.")
                        : openFailure);
    }
    return true;
}

bool ContextWorkspaceController::registerProvider(
    std::unique_ptr<IContextContentProvider> provider)
{
    if (!provider)
        return false;
    const QString id = provider->providerId().trimmed();
    if (id.isEmpty() || providers.contains(id))
        return false;

    ContextRailEntry entry;
    entry.id = id;
    entry.title = provider->displayName();
    entry.toolTip = provider->displayName();
    const QString iconName = provider->iconKey().trimmed();
    entry.icon = contextProviderIcon(
        id,
        iconName,
        window ? window->style() : nullptr);
    if (!railValue->addEntry(entry))
        return false;
    provider->setProviderStateChangedHandler([this]() {
        if (!restoringState)
            emit workspaceStateChanged();
    });
    providers.emplace(id, std::move(provider));
    return true;
}

bool ContextWorkspaceController::unregisterProvider(
    const QString& providerId)
{
    const QString id = providerId.trimmed();
    auto it = providers.find(id);
    if (it == providers.end())
        return false;

    for (auto* surface : floatingSurfaces()) {
        if (surface->resource().providerId == id)
            closeFloatingResource(surface->resource().stableKey());
    }
    if (dockHostValue) {
        const QStringList keys = dockHostValue->resourceKeys();
        for (const QString& key : keys) {
            const int separator = key.indexOf(QLatin1Char(':'));
            if (separator > 0 && key.left(separator) == id)
                closePinnedResource(key);
        }
    }
    railValue->removeEntry(id);
    providers.erase(it);
    return true;
}

QStringList ContextWorkspaceController::providerIds() const
{
    QStringList result;
    for (const auto& [id, provider] : providers) {
        Q_UNUSED(provider);
        result.append(id);
    }
    return result;
}

ContextPresentation ContextWorkspaceController::presentationFor(
    const ContextPlacement& placement)
{
    return placement.surface == ContextSurface::Floating
        ? ContextPresentation::Peek : ContextPresentation::Pinned;
}

bool ContextWorkspaceController::openResource(
    const ContextResource& resource,
    ContextPlacement placement,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!currentWorkspaceRoot.isEmpty()
        && !resource.workspaceId.trimmed().isEmpty()
        && !sameRoot(resource.workspaceId, currentWorkspaceRoot)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context resource belongs to another workspace.");
        }
        return false;
    }
    IContextContentProvider* provider = providerFor(resource);
    if (!provider) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "No registered context provider can open this resource.");
        }
        return false;
    }

    if (placement.binding == ContextBinding::DocumentBound
        && (placement.surface == ContextSurface::Docked || activeDocumentPath.isEmpty())) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "A document-bound view requires an active document and a floating surface: %1.")
                .arg(contextPlacementName(placement));
        }
        return false;
    }
    const ContextViewCapabilities capabilities =
        provider->capabilities(resource);
    if (!capabilities.supports(presentationFor(placement))) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context provider does not support this presentation.");
        }
        return false;
    }

    return placement.surface == ContextSurface::Floating
        ? openInFloatingSurface(resource, placement, *provider, capabilities, failureReason)
        : openInDockedSurface(resource, placement, *provider, capabilities, failureReason);
}

bool ContextWorkspaceController::activateDockedResource(
    const ContextResource& resource, IContextContentProvider& provider,
    const ContextViewCapabilities& capabilities, QString* failureReason)
{
    const QString key = resource.stableKey();
    if (!provider.activateView(dockHostValue->viewForResource(key), resource)) {
        if (failureReason)
            *failureReason = QStringLiteral("The context provider could not activate the resource.");
        return false;
    }
    dockHostValue->updateResource(resource);
    dockHostValue->setFullViewAvailable(key, capabilities.supports(ContextPresentation::FullView));
    dockHostValue->setSectionDetachable(key, capabilities.detachable);
    dockHostValue->activateResource(key);
    showDock(false);
    recordFocus(key);
    updateActiveRailEntry();
    return true;
}

bool ContextWorkspaceController::activateFloatingResource(
    const ContextResource& resource, IContextContentProvider& provider,
    const ContextViewCapabilities& capabilities, QString* failureReason)
{
    activeFloatingSurface = surfaceWithResource(resource.stableKey());
    if (!floatingEligible(resource.stableKey())) {
        if (failureReason) *failureReason = tr("Switch to the bound document before focusing this view.");
        return false;
    }
    if (!provider.activateView(floatingSurfaceFor()->view(), resource)) {
        if (failureReason)
            *failureReason = QStringLiteral("The context provider could not activate the resource.");
        return false;
    }
    floatingSurfaceFor()->setActionsAvailable(capabilities.supports(ContextPresentation::Pinned),
                                       capabilities.supports(ContextPresentation::FullView));
    floatingSurfaceFor()->updateResource(resource);
    focusResource(resource.stableKey());
    updateActiveRailEntry();
    return true;
}

QWidget* ContextWorkspaceController::createResourceView(
    const ContextResource& resource, IContextContentProvider& provider,
    QWidget* parent, QString* failureReason)
{
    QWidget* view = provider.createView(resource, parent);
    if (!view) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context provider could not create its view.");
        }
        return nullptr;
    }
    provider.observeViewResourceChanges(
        view,
        this,
        [this, guardedView = QPointer<QWidget>(view)](
            const ContextResource& updated) {
            if (guardedView)
                handleViewResourceChanged(guardedView, updated);
        });
    if (!provider.activateView(view, resource)) {
        view->deleteLater();
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context provider could not activate the resource.");
        }
        return nullptr;
    }
    return view;
}

void ContextWorkspaceController::announceResourceOpened(
    const ContextResource& resource, ContextPlacement placement)
{
    recordFocus(resource.stableKey());
    updateActiveRailEntry();
    emit resourceOpened(resource, placement);
    emit activeResourceChanged(resource);
}

bool ContextWorkspaceController::openInFloatingSurface(
    const ContextResource& resource, ContextPlacement placement, IContextContentProvider& provider,
    const ContextViewCapabilities& capabilities, QString* failureReason)
{
    const QString key = resource.stableKey();
    ContextFloatingSurface* target = nullptr;
    if (placement.binding == ContextBinding::DocumentBound || placement.persistence == ContextPersistence::Kept) {
        target = floatingSurfaceFor(capabilities);
        if (target == peekHostValue) {
            if (failureReason) *failureReason = tr("The editor overlay only supports Floating/Transient/Global: %1.")
                .arg(contextPlacementName(placement));
            return false;
        }
    }
    if (dockHostValue->containsResource(key)) {
        if (placement.binding == ContextBinding::DocumentBound) {
            if (failureReason) *failureReason = tr("Unpin the sidebar resource before binding it to a document.");
            return false;
        }
        return activateDockedResource(resource, provider, capabilities, failureReason);
    }
    if (surfaceWithResource(key)) {
        if (placement.binding == ContextBinding::DocumentBound || !boundDocument(key).isEmpty())
            setResourceBinding(key, placement.binding);
        if (placement.persistence == ContextPersistence::Kept) keptFloatingKeys.insert(key);
        return activateFloatingResource(resource, provider, capabilities, failureReason);
    }
    if (!target) target = floatingSurfaceFor(capabilities);
    QWidget* view = createResourceView(resource, provider, dynamic_cast<QWidget*>(target), failureReason);
    if (!view)
        return false;
    if (target->hasResource()) closeFloatingResource(target->resource().stableKey());
    activeFloatingSurface = target;
    if (placement.binding == ContextBinding::DocumentBound) documentBindings.insert(key, activeDocumentPath);
    if (placement.persistence == ContextPersistence::Kept) keptFloatingKeys.insert(key);
    floatingSurfaceFor()->setActionsAvailable(capabilities.supports(ContextPresentation::Pinned),
                                       capabilities.supports(ContextPresentation::FullView));
    floatingSurfaceFor()->setView(resource, view);
    if (auto* host = dynamic_cast<ContextFloatingWindow*>(target)) captureLastFloatingGeometry(host);
    hiddenFloatingKeys.remove(key);
    applyFloatingVisibility();
    if (placement.binding == ContextBinding::DocumentBound) {
        rememberDocumentLayout(activeDocumentPath);
        touchDocumentLayout(activeDocumentPath);
    }
    announceResourceOpened(resource, placement);
    notifyWorkspaceStateChanged();
    return true;
}

bool ContextWorkspaceController::addDockedResource(
    const ContextResource& resource, IContextContentProvider& provider,
    const ContextViewCapabilities& capabilities, QString* failureReason)
{
    QWidget* view = createResourceView(resource, provider, dockHostValue, failureReason);
    if (!view)
        return false;
    const bool dockWasEmpty = dockHostValue->resourceCount() == 0;
    if (!dockHostValue->addResource(resource, view, capabilities.supports(ContextPresentation::FullView))) {
        view->deleteLater();
        if (failureReason)
            *failureReason = QStringLiteral("The context resource is already pinned.");
        return false;
    }
    dockHostValue->setSectionDetachable(resource.stableKey(), capabilities.detachable);
    showDock(dockWasEmpty && !restoringState);
    return true;
}

bool ContextWorkspaceController::openInDockedSurface(
    const ContextResource& resource, ContextPlacement placement,
    IContextContentProvider& provider, const ContextViewCapabilities& capabilities,
    QString* failureReason)
{
    const QString key = resource.stableKey();
    switch (placement.persistence) {
    case ContextPersistence::Transient:
        if (transientDockResourceKey == key && dockHostValue->containsResource(key)
            && dockValue->isVisible()) {
            closePinnedResource(key);
            return true;
        }
        if (!transientDockResourceKey.isEmpty() && transientDockResourceKey != key)
            closePinnedResource(transientDockResourceKey);
        if (dockHostValue->containsResource(key))
            return activateDockedResource(resource, provider, capabilities, failureReason);
        // Existing previews retain their surface, just as existing dock tabs do.
        if (surfaceWithResource(key))
            return activateFloatingResource(resource, provider, capabilities, failureReason);
        if (!addDockedResource(resource, provider, capabilities, failureReason))
            return false;
        transientDockResourceKey = key;
        announceResourceOpened(resource, placement);
        return true;
    case ContextPersistence::Kept:
        if (dockHostValue->containsResource(key)) {
            if (!activateDockedResource(resource, provider, capabilities, failureReason))
                return false;
            notifyWorkspaceStateChanged();
            return true;
        }
        if (auto* surface = surfaceWithResource(key)) {
            activeFloatingSurface = surface;
            return pinPeek(failureReason);
        }
        if (!addDockedResource(resource, provider, capabilities, failureReason))
            return false;
        announceResourceOpened(resource, placement);
        notifyWorkspaceStateChanged();
        return true;
    }
    return false;
}

bool ContextWorkspaceController::pinPeek(QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!peekHostValue || !floatingSurfaceFor()->hasResource()) {
        if (failureReason)
            *failureReason = QStringLiteral("No context preview is open.");
        return false;
    }

    const ContextResource resource = floatingSurfaceFor()->resource();
    IContextContentProvider* provider = providerFor(resource);
    if (!provider
        || !provider->capabilities(resource)
                .supports(ContextPresentation::Pinned)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "This context preview cannot be pinned.");
        }
        return false;
    }

    const bool dockWasEmpty =
        dockHostValue->resourceCount() == 0;
    QWidget* view = floatingSurfaceFor()->takeView();
    if (!view || !dockHostValue->addResource(
                     resource,
                     view,
                     provider->capabilities(resource).supports(
                         ContextPresentation::FullView))) {
        if (view)
            floatingSurfaceFor()->setView(resource, view);
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context preview could not be pinned.");
        }
        return false;
    }
    dockHostValue->setSectionDetachable(resource.stableKey(), provider->capabilities(resource).detachable);
    showDock(dockWasEmpty && !restoringState);
    documentBindings.remove(resource.stableKey());
    keptFloatingKeys.remove(resource.stableKey());
    forgetStoredResource(resource.stableKey());
    updateActiveRailEntry();
    emit resourceOpened(resource, ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global});
    emit activeResourceChanged(resource);
    notifyWorkspaceStateChanged();
    return true;
}

bool ContextWorkspaceController::unpinResource(
    const QString& resourceKey,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    const ContextResource resource =
        dockHostValue->containsResource(resourceKey)
        ? [&]() {
              for (int index = 0;
                   index < dockHostValue->resourceCount();
                   ++index) {
                  const ContextResource candidate =
                      dockHostValue->resourceAt(index);
                  if (candidate.stableKey() == resourceKey)
                      return candidate;
              }
              return ContextResource{};
          }()
        : ContextResource{};
    IContextContentProvider* provider = providerFor(resource);
    if (!provider
        || !provider->capabilities(resource)
                .supports(ContextPresentation::Peek)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "This pinned context cannot be previewed.");
        }
        return false;
    }

    QWidget* view = dockHostValue->takeResource(resourceKey);
    if (!view) {
        if (failureReason)
            *failureReason = QStringLiteral("Pinned context was not found.");
        return false;
    }
    if (transientDockResourceKey == resourceKey)
        transientDockResourceKey.clear();
    const ContextViewCapabilities capabilities = provider->capabilities(resource);
    ContextFloatingSurface* target = floatingSurfaceFor(capabilities);
    if (target->hasResource()) closeFloatingResource(target->resource().stableKey());
    activeFloatingSurface = target;
    floatingSurfaceFor()->setActionsAvailable(
        provider->capabilities(resource).supports(
            ContextPresentation::Pinned),
        provider->capabilities(resource).supports(
            ContextPresentation::FullView));
    floatingSurfaceFor()->setView(resource, view);
    if (auto* host = dynamic_cast<ContextFloatingWindow*>(target)) captureLastFloatingGeometry(host);
    hiddenFloatingKeys.remove(resourceKey);
    applyFloatingVisibility();
    recordFocus(resourceKey);
    if (dockHostValue->resourceCount() == 0)
        dockValue->hide();
    updateActiveRailEntry();
    emit activeResourceChanged(resource);
    notifyWorkspaceStateChanged();
    return true;
}

bool ContextWorkspaceController::closePinnedResource(
    const QString& resourceKey)
{
    ContextResource resource;
    for (int index = 0;
         dockHostValue && index < dockHostValue->resourceCount();
         ++index) {
        const ContextResource candidate = dockHostValue->resourceAt(index);
        if (candidate.stableKey() == resourceKey) {
            resource = candidate;
            break;
        }
    }
    if (!resource.isValid())
        return false;
    const bool transient = transientDockResourceKey == resourceKey;
    QWidget* view = dockHostValue->takeResource(resourceKey);
    if (!view)
        return false;
    if (transient)
        transientDockResourceKey.clear();
    disposeView(resource, view);
    if (dockHostValue->resourceCount() == 0)
        dockValue->hide();
    updateActiveRailEntry();
    emit resourceClosed(resource);
    if (!transient)
        notifyWorkspaceStateChanged();
    return true;
}

void ContextWorkspaceController::closePeek()
{
    if (!peekHostValue || !floatingSurfaceFor()->hasResource())
        return;
    const ContextResource resource = floatingSurfaceFor()->resource();
    QWidget* view = floatingSurfaceFor()->takeView();
    disposeView(resource, view);
    if (!preservingDocumentLayout) forgetStoredResource(resource.stableKey());
    documentBindings.remove(resource.stableKey());
    keptFloatingKeys.remove(resource.stableKey());
    hiddenFloatingKeys.remove(resource.stableKey());
    focusOrder.removeAll(resource.stableKey());
    if (focusedResourceKey == resource.stableKey()) focusedResourceKey.clear();
    updateActiveRailEntry();
    emit resourceClosed(resource);
    notifyWorkspaceStateChanged();
}

QString ContextWorkspaceController::workspaceRoot() const
{
    return currentWorkspaceRoot;
}

void ContextWorkspaceController::setWorkspaceRoot(const QString& root)
{
    const QString normalized = normalizedRoot(root);
    if (sameRoot(currentWorkspaceRoot, normalized))
        return;
    clearResources();
    currentWorkspaceRoot = normalized;
    activeDocumentPath.clear();
    lastFloatingGeometry = {};
    setFloatingCollapsed(false);
}

void ContextWorkspaceController::clearResources()
{
    const bool previousRestoring = restoringState;
    restoringState = true;
    for (auto* surface : floatingSurfaces()) {
        activeFloatingSurface = surface;
        closePeek();
    }
    hiddenFloatingKeys.clear();
    focusOrder.clear();
    focusedResourceKey.clear();
    documentBindings.clear();
    keptFloatingKeys.clear();
    documentLayouts.clear();
    documentLayoutOrder.clear();
    if (dockHostValue) {
        const QStringList keys = dockHostValue->resourceKeys();
        for (const QString& key : keys)
            closePinnedResource(key);
    }
    if (dockValue)
        dockValue->hide();
    transientDockResourceKey.clear();
    restoringState = previousRestoring;
    updateActiveRailEntry();
    notifyWorkspaceStateChanged();
}

ContextWorkspaceState ContextWorkspaceController::captureState() const
{
    ContextWorkspaceState state;
    state.valid = true;
    // Remember the last native geometry even while the overlay is active.
    state.floatingX = lastFloatingGeometry.floatingX;
    state.floatingY = lastFloatingGeometry.floatingY;
    state.floatingWidth = lastFloatingGeometry.floatingWidth;
    state.floatingHeight = lastFloatingGeometry.floatingHeight;
    state.floatingScreenName = lastFloatingGeometry.floatingScreenName;
    state.floatingGeometryValid = lastFloatingGeometry.floatingGeometryValid;
    state.floatingCollapsed = floatingCollapsedValue;
    state.documentFloatingLayouts = documentLayouts;
    state.documentFloatingOrder = documentLayoutOrder;
    QSet<QString> livePaths;
    for (auto* host : floatingWindows()) {
        const auto instance = captureFloatingInstance(host);
        if (instance.resource.isEmpty()) continue;
        const QString path = boundDocument(host->resource().stableKey());
        if (path.isEmpty()) state.floatingInstances.append(instance);
        else if (state.documentFloatingOrder.contains(path)) {
            if (!livePaths.contains(path)) state.documentFloatingLayouts[path].clear();
            livePaths.insert(path);
            state.documentFloatingLayouts[path].append(instance);
        }
    }
    if (peekHostValue) {
        const QSize peekSize = peekHostValue->preferredSize();
        state.peekWidth =
            ContextWorkspaceState::boundedPeekWidth(
                peekSize.width());
        state.peekHeight =
            ContextWorkspaceState::boundedPeekHeight(
                peekSize.height());
    }
    state.dockWidth = ContextWorkspaceState::boundedDockWidth(
        dockValue && dockValue->isVisible()
            ? dockValue->width()
            : preferredDockWidthValue);
    state.dockVisible = dockValue && dockValue->isVisible();
    state.railVisible = railValue && railValue->isVisible();
    for (const auto& [id, provider] : providers) {
        if (!provider)
            continue;
        const QVariantMap providerState = provider->saveProviderState();
        if (!providerState.isEmpty())
            state.providerStates.insert(id, providerState);
    }
    if (!dockHostValue)
        return state;

    const QString activeKey =
        dockHostValue->currentResource().stableKey();
    if (activeKey != transientDockResourceKey)
        state.activePinnedResourceKey = activeKey;
    for (int index = 0;
         index < dockHostValue->resourceCount();
         ++index) {
        const ContextResource resource =
            dockHostValue->resourceAt(index);
        if (resource.stableKey() == transientDockResourceKey)
            continue;
        if (state.activePinnedResourceKey.isEmpty())
            state.activePinnedResourceKey = resource.stableKey();
        IContextContentProvider* provider = providerFor(resource);
        QWidget* view = dockHostValue->viewForResource(
            resource.stableKey());
        if (!provider || !view)
            continue;
        ContextResource persisted =
            provider->resourceForPersistence(
                resource, view, currentWorkspaceRoot);
        if (!persisted.isValid())
            continue;
        persisted.workspaceId.clear();
        state.pinnedResources.append(persisted.toVariantMap());
        state.dockSections.append({persisted.stableKey(), dockHostValue->isSectionCollapsed(resource.stableKey()),
                                   dockHostValue->sectionHeight(resource.stableKey())});
    }
    return state;
}

ContextWorkspaceRestoreResult ContextWorkspaceController::restoreState(
    const ContextWorkspaceState& state,
    bool preserveRestoredDockGeometry)
{
    ContextWorkspaceRestoreResult result;
    const int restoredQtDockWidth =
        dockValue && dockValue->width() > 0
        ? boundedDockWidthForWindow(dockValue->width())
        : ContextWorkspaceState::kDefaultDockWidth;
    const bool previousRestoring = restoringState;
    restoringState = true;
    clearResources();
    lastFloatingGeometry = state.valid ? state : ContextWorkspaceState{};
    setFloatingCollapsed(state.valid && state.floatingCollapsed);
    if (state.valid) {
        QStringList order = state.documentFloatingOrder;
        QStringList missingOrder;
        for (const QString& path : state.documentFloatingLayouts.keys())
            if (!order.contains(path)) missingOrder.append(path);
        // Missing recency metadata is older than every recorded use; resolve ties explicitly.
        missingOrder.sort(Qt::CaseSensitive);
        order = missingOrder + order;
        for (const QString& path : order) {
            const QString normalized = normalizedDocumentPath(path);
            if (normalized.isEmpty() || state.documentFloatingLayouts.value(path).isEmpty()) continue;
            documentLayouts.insert(normalized, state.documentFloatingLayouts.value(path));
            touchDocumentLayout(normalized);
        }
    }
    for (const auto& [id, provider] : providers) {
        if (!provider)
            continue;
        provider->restoreProviderState(
            state.valid
                ? state.providerStates.value(id).toMap()
                : QVariantMap{});
    }
    if (!state.valid) {
        if (peekHostValue) {
            peekHostValue->setPreferredSize(
                QSize(ContextWorkspaceState::kDefaultPeekWidth,
                      ContextWorkspaceState::kDefaultPeekHeight));
        }
        preferredDockWidthValue = qMax(
            ContextWorkspaceState::kMinimumDockWidth,
            window ? window->width() * 3 / 10
                   : ContextWorkspaceState::kDefaultDockWidth);
        restoringState = previousRestoring;
        return result;
    }

    if (peekHostValue) {
        peekHostValue->setPreferredSize(
            QSize(
                ContextWorkspaceState::boundedPeekWidth(
                    state.peekWidth),
                ContextWorkspaceState::boundedPeekHeight(
                    state.peekHeight)));
    }
    preferredDockWidthValue = preserveRestoredDockGeometry
        ? restoredQtDockWidth
        : ContextWorkspaceState::boundedDockWidth(
              state.dockWidth);

    QHash<QString, QString> restoredResourceKeys;
    for (const QVariantMap& encoded : state.pinnedResources) {
        QString failureReason;
        ContextResource persisted =
            ContextResource::fromVariantMap(encoded, &failureReason);
        if (!persisted.isValid()) {
            ++result.skippedResources;
            result.warnings.append(
                failureReason.isEmpty()
                    ? QStringLiteral("Context resource is invalid.")
                    : failureReason);
            continue;
        }
        const QString persistedKey = persisted.stableKey();
        persisted.workspaceId = currentWorkspaceRoot;
        IContextContentProvider* provider =
            providerForId(persisted.providerId);
        ContextResource restored;
        if (provider) {
            restored = provider->resourceFromPersistence(
                persisted, currentWorkspaceRoot);
        } else {
            for (const auto& [id, candidate] : providers) {
                Q_UNUSED(id);
                const ContextResource migrated =
                    candidate->resourceFromPersistence(
                        persisted, currentWorkspaceRoot);
                if (!migrated.isValid())
                    continue;
                provider = candidate.get();
                restored = migrated;
                break;
            }
        }
        if (!restored.isValid()
            || !openResource(restored,
                             ContextPlacement{ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global},
                             &failureReason)) {
            ++result.skippedResources;
            result.warnings.append(
                failureReason.isEmpty()
                    ? QStringLiteral("Context resource is unavailable: %1")
                          .arg(persisted.title)
                    : failureReason);
            continue;
        }
        restoredResourceKeys.insert(
            persistedKey, restored.stableKey());
        ++result.restoredResources;
    }

    restoreFloatingInstances(state.floatingInstances, result);
    if (!activeDocumentPath.isEmpty()) restoreDocumentLayout(activeDocumentPath, result);
    if (dockHostValue
        && !state.activePinnedResourceKey.isEmpty()) {
        dockHostValue->activateResource(
            restoredResourceKeys.value(
                state.activePinnedResourceKey,
                state.activePinnedResourceKey));
    }
    for (const auto& section : state.dockSections) {
        const QString key = restoredResourceKeys.value(section.resourceKey, section.resourceKey);
        dockHostValue->setSectionHeight(key, section.height);
        dockHostValue->setSectionCollapsed(key, section.collapsed);
    }
    if (dockValue && dockHostValue) {
        const bool previousApplying = applyingDockWidth;
        applyingDockWidth = true;
        dockValue->setVisible(
            state.dockVisible
            && dockHostValue->resourceCount() > 0);
        if (!preserveRestoredDockGeometry
            && state.dockWidth > 0
            && !dockValue->isFloating()
            && window
            && dockHostValue->resourceCount() > 0) {
            window->resizeDocks(
                {dockValue},
                {boundedDockWidthForWindow(
                    preferredDockWidthValue)},
                Qt::Horizontal);
        }
        applyingDockWidth = previousApplying;
    }
    if (railValue)
        railValue->setVisible(state.railVisible);
    restoringState = previousRestoring;
    updateActiveRailEntry();
    return result;
}

bool ContextWorkspaceController::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if (event && event->type() == QEvent::WindowActivate) {
        if (auto* host = qobject_cast<ContextFloatingWindow*>(watched); host && host->hasResource())
            recordFocus(host->resource().stableKey());
    }
    if (watched == dockValue
        && event
        && event->type() == QEvent::Resize) {
        if (!restoringState
            && !applyingDockWidth
            && dockValue
            && dockValue->isVisible()) {
            preferredDockWidthValue =
                ContextWorkspaceState::boundedDockWidth(
                    dockValue->width());
            notifyWorkspaceStateChanged();
        }
    }
    return QObject::eventFilter(watched, event);
}

IContextContentProvider* ContextWorkspaceController::providerFor(
    const ContextResource& resource) const
{
    IContextContentProvider* provider = providerForId(resource.providerId);
    return provider && provider->canOpen(resource)
        ? provider
        : nullptr;
}

IContextContentProvider* ContextWorkspaceController::providerForId(
    const QString& providerId) const
{
    const auto it = providers.find(providerId.trimmed());
    return it == providers.end() ? nullptr : it->second.get();
}

void ContextWorkspaceController::disposeView(
    const ContextResource& resource,
    QWidget* view)
{
    if (!view)
        return;
    if (IContextContentProvider* provider = providerFor(resource))
        provider->saveViewState(view);
    view->deleteLater();
}

void ContextWorkspaceController::handleViewResourceChanged(
    QWidget* view,
    const ContextResource& resource)
{
    if (!view || !resource.isValid())
        return;
    bool updated = false;
    for (auto* surface : floatingSurfaces()) {
        if (surface->view() == view) {
            updated = surface->updateResource(resource);
            break;
        }
    }
    if (!updated && dockHostValue
        && dockHostValue->viewForResource(resource.stableKey()) == view) {
        updated = dockHostValue->updateResource(resource);
    }
    if (!updated)
        return;
    updateActiveRailEntry();
    emit activeResourceChanged(resource);
    if (dockHostValue
        && dockHostValue->viewForResource(resource.stableKey()) == view) {
        notifyWorkspaceStateChanged();
    }
}

void ContextWorkspaceController::updateActiveRailEntry()
{
    if (!railValue)
        return;
    if (peekHostValue && floatingSurfaceFor()->hasResource() && floatingWidget()->isVisible()) {
        railValue->setActiveEntryId(
            floatingSurfaceFor()->resource().providerId);
        return;
    }
    if (dockValue && dockValue->isVisible()
        && dockHostValue) {
        railValue->setActiveEntryId(
            dockHostValue->currentResource().providerId);
        return;
    }
    railValue->setActiveEntryId({});
}

ContextPlacement ContextWorkspaceController::defaultPlacementFor(const QString& providerId)
{
    Q_UNUSED(providerId);
    // A later phase will resolve each provider's remembered placement here.
    return {ContextSurface::Docked, ContextPersistence::Transient, ContextBinding::Global};
}

void ContextWorkspaceController::activateRailProvider(
    const QString& providerId)
{
    for (const QString key : focusOrder) {
        auto* surface = surfaceWithResource(key);
        QWidget* widget = surface ? dynamic_cast<QWidget*>(surface) : dockValue.data();
        ContextResource candidate = surface ? surface->resource() : ContextResource{};
        if (!surface) {
            for (int i = 0; i < dockHostValue->resourceCount(); ++i)
                if (dockHostValue->resourceAt(i).stableKey() == key) candidate = dockHostValue->resourceAt(i);
        }
        if (candidate.providerId != providerId || (surface && !floatingEligible(key))) continue;
        if (focusedResourceKey == key && widget->isVisible()
            && (surface || !dockHostValue->isSectionCollapsed(key))) {
            if (surface == peekHostValue) {
                closeFloatingResource(key);
            } else if (!surface) {
                dockHostValue->setSectionCollapsed(key, true);
            } else {
                widget->hide();
                if (surface) hiddenFloatingKeys.insert(key);
            }
            updateActiveRailEntry();
        } else {
            focusResource(key);
        }
        return;
    }
    if (IContextContentProvider* provider = providerForId(providerId)) {
        const ContextResource resource = provider->activationResource(currentWorkspaceRoot);
        if (resource.isValid() && openResource(resource, defaultPlacementFor(providerId)))
            return;
    }
    emit providerActivationRequested(providerId);
}

void ContextWorkspaceController::resetPeekToProviderPreferredSize()
{
    if (!peekHostValue || !peekHostValue->hasResource())
        return;
    IContextContentProvider* provider =
        providerFor(peekHostValue->resource());
    if (!provider)
        return;
    const QSize previous = peekHostValue->preferredSize();
    peekHostValue->setPreferredSize(
        provider->capabilities(peekHostValue->resource())
            .preferredSize());
    if (peekHostValue->preferredSize() != previous)
        notifyWorkspaceStateChanged();
}

void ContextWorkspaceController::refreshProviderIcons()
{
    if (!railValue)
        return;
    for (const auto& [id, provider] : providers) {
        if (!provider)
            continue;
        railValue->setEntryIcon(
            id,
            contextProviderIcon(
                id,
                provider->iconKey().trimmed(),
                window ? window->style() : nullptr));
    }
}

int ContextWorkspaceController::boundedDockWidthForWindow(
    int width) const
{
    const int stored =
        ContextWorkspaceState::boundedDockWidth(width);
    if (!window)
        return stored;
    const int available = qMax(1, window->width());
    const int minimum = qMin(
        ContextWorkspaceState::kMinimumDockWidth,
        available);
    const int reservedCenter = qMin(
        240,
        qMax(0, available - minimum));
    const int maximum = qMax(
        minimum,
        available - reservedCenter);
    return qBound(minimum, stored, maximum);
}

void ContextWorkspaceController::showDock(bool applyPreferredWidth)
{
    if (!dockValue)
        return;
    const bool previousApplying = applyingDockWidth;
    applyingDockWidth = true;
    dockValue->show();
    dockValue->raise();
    if (applyPreferredWidth
        && !dockValue->isFloating()
        && window) {
        window->resizeDocks(
            {dockValue},
            {boundedDockWidthForWindow(
                preferredDockWidthValue)},
            Qt::Horizontal);
    }
    applyingDockWidth = previousApplying;
}

void ContextWorkspaceController::notifyWorkspaceStateChanged()
{
    if (!restoringState)
        emit workspaceStateChanged();
}
