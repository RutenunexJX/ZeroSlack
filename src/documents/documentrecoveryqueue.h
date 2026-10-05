#ifndef DOCUMENTRECOVERYQUEUE_H
#define DOCUMENTRECOVERYQUEUE_H

#include "crashrecoveryservice.h"
#include <QObject>
#include <QVariantMap>
#include <functional>
#include <memory>

// Transient scheduling owned by TabManager. Documents and recovery records
// remain authoritative; workers receive only immutable snapshot requests.
class DocumentRecoveryQueue final : public QObject {
public:
    using Completion = std::function<void(QObject*, const CrashRecoverySnapshotRequest&, const CrashRecoveryWriteResult&)>;
    DocumentRecoveryQueue(const CrashRecoveryService& service, Completion completion, QObject* parent);
    ~DocumentRecoveryQueue() override;
    void enqueue(QObject* document, CrashRecoverySnapshotRequest request);
    QList<CrashRecoveryDocumentKey> cancel(QObject* document,
        const std::function<bool(const CrashRecoveryDocumentKey&)>& matches = {});
    void cancelAll();
    void flush();
    QVariantMap metrics() const;
private:
    struct State;
    std::unique_ptr<State> state;
};

#endif
