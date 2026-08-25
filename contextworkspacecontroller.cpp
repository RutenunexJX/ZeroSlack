#include "contextworkspacecontroller.h"

#include "contextcontentprovider.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextrail.h"

#include <QDockWidget>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QIcon>
#include <QMainWindow>
#include <QStyle>
#include <QWidget>

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

    dockValue = new QDockWidget(tr("Context"), mainWindow);
    dockValue->setObjectName(QStringLiteral("contextWorkspaceDock"));
    dockValue->setAllowedAreas(Qt::RightDockWidgetArea
                               | Qt::LeftDockWidgetArea);
    dockValue->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    dockHostValue = new ContextDockHost(dockValue);
    dockValue->setWidget(dockHostValue);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dockValue);
    dockValue->hide();

    connect(railValue,
            &ContextRail::entryActivated,
            this,
            [this](const QString& providerId) {
                if (peekHostValue
                    && peekHostValue->hasResource()
                    && peekHostValue->resource().providerId
                           == providerId) {
                    closePeek();
                    return;
                }
                if (activatePinnedProvider(providerId))
                    return;
                IContextContentProvider* provider =
                    providerForId(providerId);
                if (provider) {
                    const ContextResource resource =
                        provider->activationResource(
                            currentWorkspaceRoot);
                    if (resource.isValid()
                        && openResource(resource,
                                        ContextOpenMode::Peek)) {
                        return;
                    }
                }
                emit providerActivationRequested(providerId);
            });
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
    if (!iconName.isEmpty())
        entry.icon = QIcon::fromTheme(iconName);
    if (entry.icon.isNull() && window) {
        entry.icon = window->style()->standardIcon(
            QStyle::SP_FileDialogContentsView);
    }
    if (!railValue->addEntry(entry))
        return false;
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

    if (peekHostValue && peekHostValue->hasResource()
        && peekHostValue->resource().providerId == id) {
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

bool ContextWorkspaceController::openResource(
    const ContextResource& resource,
    ContextOpenMode mode,
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

    const ContextPresentation presentation =
        mode == ContextOpenMode::Peek
        ? ContextPresentation::Peek
        : ContextPresentation::Pinned;
    const ContextViewCapabilities capabilities =
        provider->capabilities(resource);
    if (!capabilities.supports(presentation)) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context provider does not support this presentation.");
        }
        return false;
    }

    const QString key = resource.stableKey();
    if (mode == ContextOpenMode::TransientDock
        && transientDockResourceKey == key
        && dockHostValue->containsResource(key)
        && dockValue->isVisible()) {
        closePinnedResource(key);
        return true;
    }
    if (mode == ContextOpenMode::TransientDock
        && !transientDockResourceKey.isEmpty()
        && transientDockResourceKey != key) {
        closePinnedResource(transientDockResourceKey);
    }
    if (dockHostValue->containsResource(key)) {
        QWidget* view = dockHostValue->viewForResource(key);
        if (!provider->activateView(view, resource)) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The context provider could not activate the resource.");
            }
            return false;
        }
        dockHostValue->updateResource(resource);
        dockHostValue->setFullViewAvailable(
            key,
            capabilities.supports(ContextPresentation::FullView));
        dockHostValue->activateResource(key);
        showDock(false);
        updateActiveRailEntry();
        if (mode == ContextOpenMode::Pinned)
            notifyWorkspaceStateChanged();
        return true;
    }
    if (peekHostValue->hasResource()
        && peekHostValue->resource().stableKey() == key) {
        if (mode == ContextOpenMode::Pinned)
            return pinPeek(failureReason);
        if (!provider->activateView(peekHostValue->view(), resource)) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The context provider could not activate the resource.");
            }
            return false;
        }
        peekHostValue->setActionsAvailable(
            capabilities.supports(ContextPresentation::Pinned),
            capabilities.supports(ContextPresentation::FullView));
        peekHostValue->updateResource(resource);
        peekHostValue->show();
        peekHostValue->raise();
        updateActiveRailEntry();
        return true;
    }

    QWidget* view = provider->createView(
        resource,
        mode == ContextOpenMode::Peek
            ? static_cast<QWidget*>(peekHostValue)
            : static_cast<QWidget*>(dockHostValue));
    if (!view) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context provider could not create its view.");
        }
        return false;
    }
    provider->observeViewResourceChanges(
        view,
        this,
        [this, guardedView = QPointer<QWidget>(view)](
            const ContextResource& updated) {
            if (guardedView)
                handleViewResourceChanged(guardedView, updated);
        });
    if (!provider->activateView(view, resource)) {
        view->deleteLater();
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context provider could not activate the resource.");
        }
        return false;
    }

    if (mode == ContextOpenMode::Peek) {
        closePeek();
        peekHostValue->setActionsAvailable(
            capabilities.supports(ContextPresentation::Pinned),
            capabilities.supports(ContextPresentation::FullView));
        peekHostValue->setView(resource, view);
    } else {
        const bool dockWasEmpty =
            dockHostValue->resourceCount() == 0;
        if (!dockHostValue->addResource(
                resource,
                view,
                capabilities.supports(
                    ContextPresentation::FullView))) {
            view->deleteLater();
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The context resource is already pinned.");
            }
            return false;
        }
        showDock(dockWasEmpty && !restoringState);
        if (mode == ContextOpenMode::TransientDock)
            transientDockResourceKey = key;
    }
    updateActiveRailEntry();
    emit resourceOpened(resource, mode);
    emit activeResourceChanged(resource);
    if (mode == ContextOpenMode::Pinned)
        notifyWorkspaceStateChanged();
    return true;
}

bool ContextWorkspaceController::pinPeek(QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!peekHostValue || !peekHostValue->hasResource()) {
        if (failureReason)
            *failureReason = QStringLiteral("No context preview is open.");
        return false;
    }

    const ContextResource resource = peekHostValue->resource();
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
    QWidget* view = peekHostValue->takeView();
    if (!view || !dockHostValue->addResource(
                     resource,
                     view,
                     provider->capabilities(resource).supports(
                         ContextPresentation::FullView))) {
        if (view)
            peekHostValue->setView(resource, view);
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The context preview could not be pinned.");
        }
        return false;
    }
    showDock(dockWasEmpty && !restoringState);
    updateActiveRailEntry();
    emit resourceOpened(resource, ContextOpenMode::Pinned);
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
    peekHostValue->setActionsAvailable(
        provider->capabilities(resource).supports(
            ContextPresentation::Pinned),
        provider->capabilities(resource).supports(
            ContextPresentation::FullView));
    peekHostValue->setView(resource, view);
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
    if (!peekHostValue || !peekHostValue->hasResource())
        return;
    const ContextResource resource = peekHostValue->resource();
    QWidget* view = peekHostValue->takeView();
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
    if (!state.valid) {
        if (peekHostValue) {
            peekHostValue->setPreferredSize(
                QSize(ContextWorkspaceState::kDefaultPeekWidth,
                      ContextWorkspaceState::kDefaultPeekHeight));
        }
        preferredDockWidthValue =
            ContextWorkspaceState::kDefaultDockWidth;
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

    for (const QVariantMap& encoded : state.pinnedResources) {
        QString failureReason;
        ContextResource persisted =
            ContextResource::fromVariantMap(encoded, &failureReason);
        IContextContentProvider* provider =
            providerForId(persisted.providerId);
        if (!persisted.isValid() || !provider) {
            ++result.skippedResources;
            result.warnings.append(
                failureReason.isEmpty()
                    ? QStringLiteral("Context provider is unavailable: %1")
                          .arg(persisted.providerId)
                    : failureReason);
            continue;
        }
        persisted.workspaceId = currentWorkspaceRoot;
        const ContextResource restored =
            provider->resourceFromPersistence(
                persisted, currentWorkspaceRoot);
        if (!restored.isValid()
            || !openResource(restored,
                             ContextOpenMode::Pinned,
                             &failureReason)) {
            ++result.skippedResources;
            result.warnings.append(
                failureReason.isEmpty()
                    ? QStringLiteral("Context resource is unavailable: %1")
                          .arg(persisted.title)
                    : failureReason);
            continue;
        }
        ++result.restoredResources;
    }

    if (dockHostValue
        && !state.activePinnedResourceKey.isEmpty()) {
        dockHostValue->activateResource(
            state.activePinnedResourceKey);
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
    if (peekHostValue && peekHostValue->view() == view)
        updated = peekHostValue->updateResource(resource);
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
    if (peekHostValue && peekHostValue->hasResource()) {
        railValue->setActiveEntryId(
            peekHostValue->resource().providerId);
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

bool ContextWorkspaceController::activatePinnedProvider(
    const QString& providerId)
{
    if (!dockHostValue)
        return false;
    for (int index = 0;
         index < dockHostValue->resourceCount();
         ++index) {
        const ContextResource resource = dockHostValue->resourceAt(index);
        if (resource.providerId != providerId)
            continue;
        dockHostValue->activateResource(resource.stableKey());
        showDock(false);
        updateActiveRailEntry();
        return true;
    }
    return false;
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
