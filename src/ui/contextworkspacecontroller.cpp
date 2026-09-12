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
    floatingWindowValue = new ContextFloatingWindow(mainWindow, editorRegion);
    activeFloatingSurface = peekHostValue;
    connect(floatingWindowValue, &ContextFloatingWindow::pinRequested, this, [this] { pinPeek(); });
    connect(floatingWindowValue, &ContextFloatingWindow::closeRequested, this, &ContextWorkspaceController::closePeek);
    connect(floatingWindowValue, &ContextFloatingWindow::fullViewRequested, this, [this] {
        if (floatingSurfaceFor()->hasResource())
            emit fullViewRequested(floatingSurfaceFor()->resource());
    });
    connect(floatingWindowValue, &ContextFloatingWindow::geometryChanged,
            this, &ContextWorkspaceController::notifyWorkspaceStateChanged);

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
            [this]() { pinPeek(); });
    connect(peekHostValue,
            &ContextPeekHost::closeRequested,
            this,
            &ContextWorkspaceController::closePeek);
    connect(peekHostValue,
            &ContextPeekHost::fullViewRequested,
            this,
            [this]() {
                if (peekHostValue && floatingSurfaceFor()->hasResource())
                    emit fullViewRequested(floatingSurfaceFor()->resource());
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
    closePeek();
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

ContextFloatingSurface* ContextWorkspaceController::floatingSurfaceFor(
    const ContextViewCapabilities* capabilities) const
{
    if (!capabilities)
        return activeFloatingSurface;
    floatingWindowValue->setInitialSize(capabilities->preferredSize());
    return capabilities->detachable
        ? static_cast<ContextFloatingSurface*>(floatingWindowValue.data())
        : static_cast<ContextFloatingSurface*>(peekHostValue.data());
}

ContextFloatingSurface* ContextWorkspaceController::floatingSurface() const { return floatingSurfaceFor(); }
ContextFloatingWindow* ContextWorkspaceController::floatingWindow() const { return floatingWindowValue; }
QWidget* ContextWorkspaceController::floatingWidget() const { return dynamic_cast<QWidget*>(floatingSurfaceFor()); }
void ContextWorkspaceController::setFloatingOpacity(int percentage) { floatingWindowValue->setIdleOpacity(percentage); }

ContextDockHost* ContextWorkspaceController::dockHost() const
{
    return dockHostValue;
}

QDockWidget* ContextWorkspaceController::dockWidget() const
{
    return dockValue;
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

    if (peekHostValue && floatingSurfaceFor()->hasResource()
        && floatingSurfaceFor()->resource().providerId == id) {
        closePeek();
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

    if (placement.binding != ContextBinding::Global
        || (placement.surface == ContextSurface::Floating
            && placement.persistence == ContextPersistence::Kept)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Document-bound or floating kept placements are not available yet: %1.")
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
        ? openInFloatingSurface(resource, *provider, capabilities, failureReason)
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
    dockHostValue->activateResource(key);
    showDock(false);
    updateActiveRailEntry();
    return true;
}

bool ContextWorkspaceController::activateFloatingResource(
    const ContextResource& resource, IContextContentProvider& provider,
    const ContextViewCapabilities& capabilities, QString* failureReason)
{
    if (!provider.activateView(floatingSurfaceFor()->view(), resource)) {
        if (failureReason)
            *failureReason = QStringLiteral("The context provider could not activate the resource.");
        return false;
    }
    floatingSurfaceFor()->setActionsAvailable(capabilities.supports(ContextPresentation::Pinned),
                                       capabilities.supports(ContextPresentation::FullView));
    floatingSurfaceFor()->updateResource(resource);
    floatingWidget()->show();
    floatingWidget()->raise();
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
    updateActiveRailEntry();
    emit resourceOpened(resource, placement);
    emit activeResourceChanged(resource);
}

bool ContextWorkspaceController::openInFloatingSurface(
    const ContextResource& resource, IContextContentProvider& provider,
    const ContextViewCapabilities& capabilities, QString* failureReason)
{
    const QString key = resource.stableKey();
    if (dockHostValue->containsResource(key))
        return activateDockedResource(resource, provider, capabilities, failureReason);
    if (floatingSurfaceFor()->hasResource() && floatingSurfaceFor()->resource().stableKey() == key)
        return activateFloatingResource(resource, provider, capabilities, failureReason);
    ContextFloatingSurface* target = floatingSurfaceFor(&capabilities);
    QWidget* view = createResourceView(resource, provider, dynamic_cast<QWidget*>(target), failureReason);
    if (!view)
        return false;
    closePeek();
    activeFloatingSurface = target;
    floatingSurfaceFor()->setActionsAvailable(capabilities.supports(ContextPresentation::Pinned),
                                       capabilities.supports(ContextPresentation::FullView));
    floatingSurfaceFor()->setView(resource, view);
    announceResourceOpened(resource, {ContextSurface::Floating, ContextPersistence::Transient,
                                      ContextBinding::Global});
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
        if (floatingSurfaceFor()->hasResource() && floatingSurfaceFor()->resource().stableKey() == key)
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
        if (floatingSurfaceFor()->hasResource() && floatingSurfaceFor()->resource().stableKey() == key)
            return pinPeek(failureReason);
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
    showDock(dockWasEmpty && !restoringState);
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
    closePeek();
    const ContextViewCapabilities capabilities = provider->capabilities(resource);
    activeFloatingSurface = floatingSurfaceFor(&capabilities);
    floatingSurfaceFor()->setActionsAvailable(
        provider->capabilities(resource).supports(
            ContextPresentation::Pinned),
        provider->capabilities(resource).supports(
            ContextPresentation::FullView));
    floatingSurfaceFor()->setView(resource, view);
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
    updateActiveRailEntry();
    emit resourceClosed(resource);
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
}

void ContextWorkspaceController::clearResources()
{
    const bool previousRestoring = restoringState;
    restoringState = true;
    closePeek();
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
    floatingWindowValue->captureGeometry(state);
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
    floatingWindowValue->restoreGeometry(state.valid ? state : ContextWorkspaceState{});
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

    if (dockHostValue
        && !state.activePinnedResourceKey.isEmpty()) {
        dockHostValue->activateResource(
            restoredResourceKeys.value(
                state.activePinnedResourceKey,
                state.activePinnedResourceKey));
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
    if (peekHostValue && floatingSurfaceFor()->view() == view)
        updated = floatingSurfaceFor()->updateResource(resource);
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
    if (peekHostValue && floatingSurfaceFor()->hasResource()) {
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
    if (floatingSurfaceFor()->hasResource() && floatingSurfaceFor()->resource().providerId == providerId) {
        if (floatingWidget()->isVisible()) {
            closePeek();
        } else {
            floatingWidget()->show();
            floatingWidget()->raise();
            updateActiveRailEntry();
        }
        return;
    }
    ContextResource opened = dockHostValue->currentResource();
    if (opened.providerId != providerId) {
        opened = {};
        for (int index = 0; index < dockHostValue->resourceCount(); ++index) {
            const ContextResource candidate = dockHostValue->resourceAt(index);
            if (candidate.providerId == providerId) {
                opened = candidate;
                break;
            }
        }
    }
    if (opened.isValid()) {
        if (dockValue->isVisible() && dockHostValue->currentResource() == opened) {
            dockValue->hide();
        } else {
            dockHostValue->activateResource(opened.stableKey());
            showDock(false);
        }
        updateActiveRailEntry();
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
    if (!peekHostValue || !floatingSurfaceFor()->hasResource())
        return;
    IContextContentProvider* provider =
        providerFor(floatingSurfaceFor()->resource());
    if (!provider)
        return;
    const QSize previous = peekHostValue->preferredSize();
    peekHostValue->setPreferredSize(
        provider->capabilities(floatingSurfaceFor()->resource())
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
