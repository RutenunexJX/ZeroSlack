#ifndef WORKSPACEHUBSESSION_H
#define WORKSPACEHUBSESSION_H

#include "workspacehubtypes.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

class ZEROSLACK_API WorkspaceHubSession final : public QObject
{
    Q_OBJECT

public:
    using CancellationFlag = std::shared_ptr<std::atomic_bool>;
    using Builder = std::function<WorkspaceHubSnapshot(
        const WorkspaceHubRequest&,
        quint64,
        const CancellationFlag&)>;
    using SemanticCapture = std::function<void(WorkspaceHubRequest*)>;

    explicit WorkspaceHubSession(QObject* parent = nullptr);
    ~WorkspaceHubSession() override;

    WorkspaceHubSnapshot snapshot() const;
    quint64 requestedGeneration() const;
    QString requestedKey() const;

    void requestUpdate(const WorkspaceHubRequest& request);
    void clear();
    void setBuilder(Builder builder);
    void setSemanticCapture(SemanticCapture capture);
    void setDebounceIntervalForTesting(int milliseconds);

    static WorkspaceHubSnapshot buildDefaultSnapshot(
        const WorkspaceHubRequest& request,
        quint64 generation,
        const CancellationFlag& cancellation);

signals:
    void snapshotChanged(const WorkspaceHubSnapshot& snapshot);

private:
    QTimer debounceTimer;
    Builder builderValue;
    SemanticCapture semanticCapture;
    WorkspaceHubRequest pendingRequest;
    WorkspaceHubSnapshot currentSnapshot;
    quint64 generationValue = 0;
    QString requestKeyValue;
    QString workspaceRootKeyValue;
    CancellationFlag cancellation;

    void startPendingRequest();
    void publish(const WorkspaceHubSnapshot& snapshot);
};

#endif // WORKSPACEHUBSESSION_H
