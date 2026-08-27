#include "liveinsightsession.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include <array>
#include <exception>
#include <utility>

namespace {
constexpr int kKindCount = 5;

int kindIndex(LiveInsightKind kind)
{
    switch (kind) {
    case LiveInsightKind::Module:
        return 0;
    case LiveInsightKind::State:
        return 1;
    case LiveInsightKind::Hotspot:
        return 2;
    case LiveInsightKind::Wave:
        return 3;
    case LiveInsightKind::Kernel:
        return 4;
    }
    return 0;
}

LiveInsightKind kindAt(int index)
{
    switch (index) {
    case 1:
        return LiveInsightKind::State;
    case 2:
        return LiveInsightKind::Hotspot;
    case 3:
        return LiveInsightKind::Wave;
    case 4:
        return LiveInsightKind::Kernel;
    default:
        return LiveInsightKind::Module;
    }
}

QString malformedResultError()
{
    return QStringLiteral(
        "Live Insight worker returned a malformed or mismatched result.");
}
}

LiveInsightCancellationToken::LiveInsightCancellationToken()
    : cancellationFlag(std::make_shared<std::atomic_bool>(false))
{
}

bool LiveInsightCancellationToken::isCancellationRequested() const
{
    return cancellationFlag
        && cancellationFlag->load(std::memory_order_acquire);
}

void LiveInsightCancellationToken::requestCancellation() const
{
    if (cancellationFlag)
        cancellationFlag->store(true, std::memory_order_release);
}

class LiveInsightSessionPrivate
{
public:
    struct CompletionGate {
        QMutex mutex;
        LiveInsightSession* target = nullptr;
    };

    struct Channel {
        QTimer* debounceTimer = nullptr;
        LiveInsightSession::Builder builder;
        QSet<QObject*> visibleConsumers;
        LiveInsightSnapshot snapshot;
        LiveInsightBuildRequest pendingRequest;
        LiveInsightCancellationToken activeCancellation;
        quint64 generationCounter = 0;
        quint64 activeGeneration = 0;
        qint64 debounceWindowStart = -1;
        bool hasRequest = false;
    };

    explicit LiveInsightSessionPrivate(LiveInsightSession* owner)
        : q(owner)
        , completionGate(std::make_shared<CompletionGate>())
    {
        completionGate->target = owner;
        clock.start();
        executor = [](LiveInsightSession::Task task) {
            QThreadPool::globalInstance()->start(
                [task = std::move(task)]() mutable { task(); });
        };
        for (int index = 0; index < kKindCount; ++index) {
            Channel& current = channels.at(index);
            const LiveInsightKind kind = kindAt(index);
            current.snapshot.kind = kind;
            current.debounceTimer = new QTimer(q);
            current.debounceTimer->setSingleShot(true);
            QObject::connect(
                current.debounceTimer,
                &QTimer::timeout,
                q,
                [this, kind]() { dispatch(kind); });
        }
    }

    ~LiveInsightSessionPrivate()
    {
        {
            const QMutexLocker locker(&completionGate->mutex);
            completionGate->target = nullptr;
        }
        for (Channel& current : channels)
            current.activeCancellation.requestCancellation();
    }

    Channel& channel(LiveInsightKind kind)
    {
        return channels.at(kindIndex(kind));
    }

    const Channel& channel(LiveInsightKind kind) const
    {
        return channels.at(kindIndex(kind));
    }

    bool visible(const Channel& current) const
    {
        return !current.visibleConsumers.isEmpty();
    }

    void notify(Channel& current)
    {
        current.snapshot.visible = visible(current);
        emit q->snapshotChanged(current.snapshot.kind,
                                current.snapshot);
    }

    void cancelActive(Channel& current)
    {
        current.activeCancellation.requestCancellation();
        current.activeGeneration = 0;
    }

    void markHiddenDirty(Channel& current)
    {
        current.debounceTimer->stop();
        current.debounceWindowStart = -1;
        current.snapshot.phase = LiveInsightPhase::HiddenDirty;
        current.snapshot.dirty = true;
        current.snapshot.stale = current.snapshot.hasLastValid;
        current.snapshot.errorText.clear();
        notify(current);
    }

    void schedule(Channel& current, bool preserveWindow)
    {
        if (!current.hasRequest || !current.snapshot.dirty)
            return;
        if (!visible(current)) {
            markHiddenDirty(current);
            return;
        }

        const qint64 now = clock.elapsed();
        if (!preserveWindow || current.debounceWindowStart < 0)
            current.debounceWindowStart = now;
        const qint64 elapsed = qMax<qint64>(
            0, now - current.debounceWindowStart);
        const int remaining = qMax(
            0,
            LiveInsightSession::kMaximumDebounceLatencyMs
                - static_cast<int>(elapsed));
        const int delay = qMin(
            LiveInsightSession::kDebounceIntervalMs,
            remaining);
        current.snapshot.phase = LiveInsightPhase::Debouncing;
        current.snapshot.stale = current.snapshot.hasLastValid;
        current.snapshot.errorText.clear();
        current.debounceTimer->start(delay);
        notify(current);
    }

    void dispatch(LiveInsightKind kind)
    {
        Channel& current = channel(kind);
        current.debounceTimer->stop();
        current.debounceWindowStart = -1;
        if (!current.hasRequest || !current.snapshot.dirty)
            return;
        if (!visible(current)) {
            markHiddenDirty(current);
            return;
        }

        cancelActive(current);
        current.activeCancellation = LiveInsightCancellationToken();
        current.activeGeneration = current.pendingRequest.generation;
        current.snapshot.phase = LiveInsightPhase::Building;
        current.snapshot.stale = current.snapshot.hasLastValid;
        current.snapshot.errorText.clear();
        notify(current);

        const LiveInsightBuildRequest request = current.pendingRequest;
        const LiveInsightCancellationToken cancellation =
            current.activeCancellation;
        const LiveInsightSession::Builder builder = current.builder;
        const std::shared_ptr<CompletionGate> gate = completionGate;
        emit q->buildDispatched(request);

        LiveInsightSession::Task task =
            [gate, request, cancellation, builder]() mutable {
                if (cancellation.isCancellationRequested())
                    return;

                LiveInsightBuildResult result;
                try {
                    result = builder
                        ? builder(request, cancellation)
                        : LiveInsightBuildResult::failure(
                              request,
                              QStringLiteral(
                                  "No Live Insight builder is registered."));
                } catch (const std::exception& error) {
                    result = LiveInsightBuildResult::failure(
                        request,
                        QString::fromUtf8(error.what()));
                } catch (...) {
                    result = LiveInsightBuildResult::failure(
                        request,
                        QStringLiteral(
                            "Live Insight builder raised an unknown exception."));
                }

                if (cancellation.isCancellationRequested())
                    result = LiveInsightBuildResult::cancellation(request);

                const QMutexLocker locker(&gate->mutex);
                LiveInsightSession* target = gate->target;
                if (!target)
                    return;
                QMetaObject::invokeMethod(
                    target,
                    [gate, request, result, cancellation]() {
                        LiveInsightSession* callbackTarget = nullptr;
                        {
                            const QMutexLocker callbackLocker(
                                &gate->mutex);
                            callbackTarget = gate->target;
                        }
                        if (callbackTarget) {
                            callbackTarget->d->complete(
                                request, result, cancellation);
                        }
                    },
                    Qt::QueuedConnection);
            };

        try {
            executor(std::move(task));
        } catch (const std::exception& error) {
            complete(
                request,
                LiveInsightBuildResult::failure(
                    request, QString::fromUtf8(error.what())),
                cancellation);
        } catch (...) {
            complete(
                request,
                LiveInsightBuildResult::failure(
                    request,
                    QStringLiteral(
                        "Live Insight task executor rejected the build.")),
                cancellation);
        }
    }

    void complete(const LiveInsightBuildRequest& request,
                  const LiveInsightBuildResult& result,
                  const LiveInsightCancellationToken& cancellation)
    {
        Channel& current = channel(request.key.kind);
        const bool requestStillCurrent = current.hasRequest
            && request.generation == current.snapshot.requestedGeneration
            && request.generation == current.pendingRequest.generation
            && request.key == current.snapshot.requestedKey
            && request.key == current.pendingRequest.key;
        if (!requestStillCurrent) {
            emit q->resultRejected(
                request.key.kind,
                request.generation,
                QStringLiteral("Superseded Live Insight generation."));
            return;
        }
        if (cancellation.isCancellationRequested()
            || current.activeGeneration != request.generation) {
            emit q->resultRejected(
                request.key.kind,
                request.generation,
                QStringLiteral("Cancelled Live Insight generation."));
            return;
        }
        current.activeGeneration = 0;

        if (!visible(current)) {
            current.snapshot.dirty = true;
            markHiddenDirty(current);
            return;
        }
        if (!result.isWellFormedFor(request)) {
            current.snapshot.phase = LiveInsightPhase::Error;
            current.snapshot.dirty = false;
            current.snapshot.stale = current.snapshot.hasLastValid;
            current.snapshot.errorText = malformedResultError();
            emit q->resultRejected(
                request.key.kind,
                request.generation,
                current.snapshot.errorText);
            notify(current);
            return;
        }
        if (result.cancelled) {
            current.snapshot.phase = current.snapshot.hasLastValid
                ? LiveInsightPhase::Ready
                : LiveInsightPhase::Empty;
            current.snapshot.dirty = false;
            current.snapshot.stale = current.snapshot.hasLastValid
                && current.snapshot.publishedKey != request.key;
            current.snapshot.errorText.clear();
            notify(current);
            return;
        }
        if (!result.succeeded) {
            current.snapshot.phase = LiveInsightPhase::Error;
            current.snapshot.dirty = false;
            current.snapshot.stale = current.snapshot.hasLastValid;
            current.snapshot.errorText = result.errorText;
            notify(current);
            return;
        }

        current.snapshot.phase = LiveInsightPhase::Ready;
        current.snapshot.publishedKey = request.key;
        current.snapshot.publishedGeneration = request.generation;
        current.snapshot.payload = result.payload;
        current.snapshot.errorText.clear();
        current.snapshot.hasLastValid = true;
        current.snapshot.stale = false;
        current.snapshot.dirty = false;
        notify(current);
    }

    void removeConsumer(QObject* consumer)
    {
        observedConsumers.remove(consumer);
        for (Channel& current : channels) {
            if (!current.visibleConsumers.remove(consumer))
                continue;
            if (!visible(current)
                && (current.snapshot.phase
                        == LiveInsightPhase::Building
                    || current.snapshot.phase
                        == LiveInsightPhase::Debouncing)) {
                if (current.snapshot.phase
                    == LiveInsightPhase::Building) {
                    cancelActive(current);
                    ++current.generationCounter;
                    current.pendingRequest.generation =
                        current.generationCounter;
                    current.snapshot.requestedGeneration =
                        current.generationCounter;
                }
                current.snapshot.dirty = true;
                markHiddenDirty(current);
            } else {
                notify(current);
            }
        }
    }

    LiveInsightSession* q = nullptr;
    std::array<Channel, kKindCount> channels;
    QElapsedTimer clock;
    LiveInsightSession::TaskExecutor executor;
    QSet<QObject*> observedConsumers;
    std::shared_ptr<CompletionGate> completionGate;
};

LiveInsightSession::LiveInsightSession(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<LiveInsightSessionPrivate>(this))
{
    qRegisterMetaType<LiveInsightKind>();
    qRegisterMetaType<LiveInsightPhase>();
    qRegisterMetaType<LiveInsightRequestKey>();
    qRegisterMetaType<LiveInsightBuildRequest>();
    qRegisterMetaType<LiveInsightBuildResult>();
    qRegisterMetaType<LiveInsightSnapshot>();
}

LiveInsightSession::~LiveInsightSession() = default;

void LiveInsightSession::setBuilder(LiveInsightKind kind,
                                    Builder builder)
{
    d->channel(kind).builder = std::move(builder);
}

void LiveInsightSession::setTaskExecutor(TaskExecutor executor)
{
    if (executor) {
        d->executor = std::move(executor);
        return;
    }
    d->executor = [](Task task) {
        QThreadPool::globalInstance()->start(
            [task = std::move(task)]() mutable { task(); });
    };
}

quint64 LiveInsightSession::requestUpdate(
    const LiveInsightRequestKey& key,
    const QVariantMap& input)
{
    LiveInsightSessionPrivate::Channel& current = d->channel(key.kind);
    const bool preserveDebounceWindow =
        current.debounceTimer->isActive()
        && current.snapshot.phase == LiveInsightPhase::Debouncing;
    current.debounceTimer->stop();
    d->cancelActive(current);

    ++current.generationCounter;
    current.pendingRequest = {};
    current.pendingRequest.schema = liveInsightRequestSchema();
    current.pendingRequest.key = key;
    current.pendingRequest.generation = current.generationCounter;
    current.pendingRequest.input = input;
    current.hasRequest = true;
    current.snapshot.requestedKey = key;
    current.snapshot.requestedGeneration = current.generationCounter;
    current.snapshot.dirty = true;
    current.snapshot.stale = current.snapshot.hasLastValid;
    current.snapshot.errorText.clear();

    if (!key.isValid()) {
        current.snapshot.phase = LiveInsightPhase::Error;
        current.snapshot.dirty = false;
        current.snapshot.errorText = QStringLiteral(
            "Live Insight request requires workspace, document, and context identity.");
        d->notify(current);
        return current.generationCounter;
    }

    d->schedule(current, preserveDebounceWindow);
    return current.generationCounter;
}

void LiveInsightSession::flushPending(LiveInsightKind kind)
{
    LiveInsightSessionPrivate::Channel& current = d->channel(kind);
    current.debounceTimer->stop();
    current.debounceWindowStart = -1;
    d->dispatch(kind);
}

void LiveInsightSession::cancel(LiveInsightKind kind)
{
    LiveInsightSessionPrivate::Channel& current = d->channel(kind);
    current.debounceTimer->stop();
    current.debounceWindowStart = -1;
    d->cancelActive(current);
    ++current.generationCounter;
    current.hasRequest = false;
    current.snapshot.requestedGeneration = current.generationCounter;
    current.snapshot.phase = current.snapshot.hasLastValid
        ? LiveInsightPhase::Ready
        : LiveInsightPhase::Empty;
    current.snapshot.dirty = false;
    current.snapshot.stale = current.snapshot.hasLastValid
        && current.snapshot.publishedKey
            != current.snapshot.requestedKey;
    current.snapshot.errorText.clear();
    d->notify(current);
}

void LiveInsightSession::clear(LiveInsightKind kind)
{
    LiveInsightSessionPrivate::Channel& current = d->channel(kind);
    current.debounceTimer->stop();
    current.debounceWindowStart = -1;
    d->cancelActive(current);
    ++current.generationCounter;
    current.hasRequest = false;
    const bool channelVisible = d->visible(current);
    current.snapshot = {};
    current.snapshot.kind = kind;
    current.snapshot.visible = channelVisible;
    d->notify(current);
}

void LiveInsightSession::setConsumerVisible(
    QObject* consumer,
    LiveInsightKind kind,
    bool visible)
{
    if (!consumer)
        return;
    LiveInsightSessionPrivate::Channel& current = d->channel(kind);
    if (!d->observedConsumers.contains(consumer)) {
        d->observedConsumers.insert(consumer);
        connect(
            consumer,
            &QObject::destroyed,
            this,
            [this](QObject* object) { d->removeConsumer(object); });
    }

    const bool wasVisible = d->visible(current);
    if (visible)
        current.visibleConsumers.insert(consumer);
    else
        current.visibleConsumers.remove(consumer);
    const bool isNowVisible = d->visible(current);
    if (wasVisible == isNowVisible)
        return;

    if (!isNowVisible) {
        if (current.snapshot.phase == LiveInsightPhase::Building) {
            d->cancelActive(current);
            ++current.generationCounter;
            current.pendingRequest.generation = current.generationCounter;
            current.snapshot.requestedGeneration =
                current.generationCounter;
            current.snapshot.dirty = current.hasRequest;
        }
        if (current.snapshot.phase == LiveInsightPhase::Debouncing)
            current.snapshot.dirty = current.hasRequest;
        if (current.snapshot.dirty) {
            d->markHiddenDirty(current);
            return;
        }
        d->notify(current);
        return;
    }

    if (current.snapshot.dirty && current.hasRequest) {
        d->schedule(current, false);
        return;
    }
    d->notify(current);
}

bool LiveInsightSession::isVisible(LiveInsightKind kind) const
{
    return d->visible(d->channel(kind));
}

bool LiveInsightSession::hasPendingUpdate(LiveInsightKind kind) const
{
    const LiveInsightSessionPrivate::Channel& current = d->channel(kind);
    return current.snapshot.dirty
        || current.snapshot.phase == LiveInsightPhase::Debouncing
        || current.snapshot.phase == LiveInsightPhase::Building;
}

LiveInsightSnapshot LiveInsightSession::snapshot(
    LiveInsightKind kind) const
{
    return d->channel(kind).snapshot;
}
