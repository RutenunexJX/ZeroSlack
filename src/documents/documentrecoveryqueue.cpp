#include "documentrecoveryqueue.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrent>

struct DocumentRecoveryQueue::State {
    struct Gate {
        QMutex mutex;
        bool invalid = false;
        void cancel() { QMutexLocker lock(&mutex); invalid = true; }
        bool cancelled() { QMutexLocker lock(&mutex); return invalid; }
        bool commit(const std::function<bool()>& action) {
            QMutexLocker lock(&mutex);
            return !invalid && action();
        }
    };
    struct Job {
        QObject* key;
        QPointer<QObject> document;
        CrashRecoverySnapshotRequest request;
        std::shared_ptr<Gate> gate = std::make_shared<Gate>();
        int attempt = 0;
    };
    struct Result { CrashRecoveryWriteResult write; qint64 workerNs; };
    DocumentRecoveryQueue* owner;
    CrashRecoveryService service;
    Completion completion;
    QThreadPool pool;
    QHash<QObject*, std::shared_ptr<Job>> pending;
    std::shared_ptr<Job> active;
    QFutureWatcher<Result>* watcher = nullptr;
    quint64 submitted = 0, replaced = 0, started = 0, committed = 0, cancelled = 0, retries = 0;
    qint64 workerNs = 0;
    qsizetype maximumPending = 0;

    State(DocumentRecoveryQueue* owner, const CrashRecoveryService& service, Completion completion)
        : owner(owner), service(service), completion(std::move(completion)) {
        pool.setMaxThreadCount(1);
        pool.setThreadPriority(QThread::LowPriority);
    }
    void start() {
        if (active || pending.isEmpty()) return;
        auto it = pending.begin();
        active = it.value();
        pending.erase(it);
        auto* observed = new QFutureWatcher<Result>(owner);
        watcher = observed;
        QObject::connect(observed, &QFutureWatcher<Result>::finished, owner, [this, observed] {
            if (watcher == observed) finish(true);
        });
        ++started;
        observed->setFuture(QtConcurrent::run(&pool,
            [request = active->request, gate = active->gate, service = service] {
                QElapsedTimer timer;
                timer.start();
                auto result = service.writeSnapshot(request, [gate](const std::function<bool()>& commit) {
                    return gate->commit(commit);
                }, [gate] { return gate->cancelled(); });
                return Result{std::move(result), timer.nsecsElapsed()};
            }));
    }
    void finish(bool launchNext) {
        auto job = std::move(active);
        auto* observed = watcher;
        watcher = nullptr;
        QObject::disconnect(observed, nullptr, owner, nullptr);
        auto result = observed->future().takeResult();
        observed->deleteLater();
        workerNs += result.workerNs;
        if (job->gate->cancelled() || !job->document) {
            ++cancelled;
        } else {
            if (result.write.status == CrashRecoveryStatus::Success) ++committed;
            // One retry, with the same immutable input. A newer pending
            // revision supersedes this retry. Further attempts need a new
            // checkpoint, so storage failure never spins indefinitely.
            const bool retryable = result.write.status == CrashRecoveryStatus::IoError
                || result.write.status == CrashRecoveryStatus::StorageUnavailable;
            if (retryable && job->attempt == 0 && !pending.contains(job->key)) {
                ++job->attempt;
                ++retries;
                pending.insert(job->key, job);
            }
            const QPointer<DocumentRecoveryQueue> alive(owner);
            if (completion) completion(job->document, job->request, result.write);
            if (!alive) return;
        }
        if (launchNext) start();
    }
};

DocumentRecoveryQueue::DocumentRecoveryQueue(const CrashRecoveryService& service, Completion completion, QObject* parent)
    : QObject(parent), state(std::make_unique<State>(this, service, std::move(completion))) {}

DocumentRecoveryQueue::~DocumentRecoveryQueue()
{
    cancelAll();
    if (state->watcher) disconnect(state->watcher, nullptr, this, nullptr);
    state->pool.waitForDone();
}

void DocumentRecoveryQueue::enqueue(QObject* document, CrashRecoverySnapshotRequest request)
{
    ++state->submitted;
    if (state->pending.contains(document)) ++state->replaced;
    state->pending.insert(document, std::make_shared<State::Job>(State::Job{document, document, std::move(request)}));
    state->maximumPending = qMax(state->maximumPending, state->pending.size());
    state->start();
}

QList<CrashRecoveryDocumentKey> DocumentRecoveryQueue::cancel(QObject* document,
    const std::function<bool(const CrashRecoveryDocumentKey&)>& matches)
{
    QList<CrashRecoveryDocumentKey> keys;
    if (state->active && state->active->key == document
        && (!matches || matches(state->active->request.document))) {
        state->active->gate->cancel();
        keys.append(state->active->request.document);
    }
    auto pending = state->pending.value(document);
    if (pending && (!matches || matches(pending->request.document))) {
        state->pending.remove(document);
        pending->gate->cancel(); keys.append(pending->request.document);
    }
    return keys;
}

void DocumentRecoveryQueue::cancelAll()
{
    if (state->active) state->active->gate->cancel();
    state->pending.clear();
}

void DocumentRecoveryQueue::flush()
{
    while (state->active || !state->pending.isEmpty()) {
        state->start();
        state->watcher->waitForFinished();
        state->finish(false);
    }
}

QVariantMap DocumentRecoveryQueue::metrics() const
{
    return {{"submitted", qulonglong(state->submitted)}, {"replaced", qulonglong(state->replaced)},
        {"started", qulonglong(state->started)}, {"committed", qulonglong(state->committed)},
        {"cancelled", qulonglong(state->cancelled)}, {"retries", qulonglong(state->retries)},
        {"workerNs", state->workerNs}, {"maximumPending", qlonglong(state->maximumPending)},
        {"pending", state->pending.size()}, {"active", bool(state->active)}};
}
