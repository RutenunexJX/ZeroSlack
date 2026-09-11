#ifndef LIVEINSIGHTSESSION_H
#define LIVEINSIGHTSESSION_H

#include "liveinsighttypes.h"
#include "zeroslackexport.h"

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

class LiveInsightSessionPrivate;

class ZEROSLACK_API LiveInsightCancellationToken
{
public:
    LiveInsightCancellationToken();

    bool isCancellationRequested() const;

private:
    std::shared_ptr<std::atomic_bool> cancellationFlag;

    void requestCancellation() const;
    friend class LiveInsightSessionPrivate;
};

class ZEROSLACK_API LiveInsightSession final : public QObject
{
    Q_OBJECT

public:
    using Builder = std::function<LiveInsightBuildResult(
        const LiveInsightBuildRequest&,
        const LiveInsightCancellationToken&)>;
    using Task = std::function<void()>;
    using TaskExecutor = std::function<void(Task)>;

    static constexpr int kDebounceIntervalMs = 275;
    static constexpr int kMaximumDebounceLatencyMs = 300;

    explicit LiveInsightSession(QObject* parent = nullptr);
    ~LiveInsightSession() override;

    void setBuilder(LiveInsightKind kind, Builder builder);
    void setTaskExecutor(TaskExecutor executor);

    quint64 requestUpdate(const LiveInsightRequestKey& key,
                          const QVariantMap& input = {});
    void flushPending(LiveInsightKind kind);
    void cancel(LiveInsightKind kind);
    void clear(LiveInsightKind kind);

    void setConsumerVisible(QObject* consumer,
                            LiveInsightKind kind,
                            bool visible);
    bool isVisible(LiveInsightKind kind) const;
    bool hasPendingUpdate(LiveInsightKind kind) const;
    LiveInsightSnapshot snapshot(LiveInsightKind kind) const;

signals:
    void snapshotChanged(LiveInsightKind kind,
                         const LiveInsightSnapshot& snapshot);
    void buildDispatched(const LiveInsightBuildRequest& request);
    void resultRejected(LiveInsightKind kind,
                        quint64 generation,
                        const QString& reason);

private:
    friend class LiveInsightSessionPrivate;
    std::unique_ptr<LiveInsightSessionPrivate> d;
};

#endif // LIVEINSIGHTSESSION_H
