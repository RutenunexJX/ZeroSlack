#ifndef SCOPEDREPLACEWORKFLOW_H
#define SCOPEDREPLACEWORKFLOW_H

#include "searchservice.h"

#include <rtledit/workspace_edit_transaction.h>

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class NotificationCenter;
class WorkspaceEditTransactionService;

enum class ScopedReplaceWorkflowState {
    Idle,
    PreviewReady,
    Cancelled,
    Applied,
    DryRunComplete,
    Undone,
    Failed,
};

enum class ScopedReplaceWorkflowFailure {
    None,
    MissingDependency,
    InvalidPreview,
    StaleSearchRevision,
    StaleDocumentRevision,
    ExternalModification,
    Conflict,
    ApplyFailed,
    AtomicRollbackFailed,
    NothingToUndo,
    UndoFailed,
};

struct ScopedReplaceWorkflowResult {
    ScopedReplaceWorkflowState state =
        ScopedReplaceWorkflowState::Idle;
    ScopedReplaceWorkflowFailure failure =
        ScopedReplaceWorkflowFailure::None;
    rtledit::TransactionStatus transactionStatus =
        rtledit::TransactionStatus::InvalidPreparation;
    QString message;
    QString renderedDiff;
    int fileCount = 0;
    int editCount = 0;

    bool succeeded() const;
};

Q_DECLARE_METATYPE(ScopedReplaceWorkflowState)
Q_DECLARE_METATYPE(ScopedReplaceWorkflowFailure)
Q_DECLARE_METATYPE(ScopedReplaceWorkflowResult)

// Coordinates one explicit replace preview at a time. Search revisions are
// validated before they are rebound to WorkspaceDocumentManager versions, and
// confirmation rechecks both the document snapshots and external file state.
class ScopedReplaceWorkflow final : public QObject
{
    Q_OBJECT

public:
    explicit ScopedReplaceWorkflow(
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions = nullptr,
        NotificationCenter* notifications = nullptr,
        QObject* parent = nullptr);
    ~ScopedReplaceWorkflow() override;

    void setNotificationCenter(NotificationCenter* notifications);

    ScopedReplaceWorkflowResult preparePreview(
        const ReplacePreviewPlan& preview,
        const QList<SearchDocumentSnapshot>& searchedDocuments,
        bool dryRun = false);
    ScopedReplaceWorkflowResult confirm(
        const QList<SearchDocumentSnapshot>& currentDocuments = {});
    ScopedReplaceWorkflowResult cancel();
    ScopedReplaceWorkflowResult undo();

    // Invalidates only a pending preview. Previously applied transaction
    // history remains available to the explicit Undo control.
    void discardPendingPreview();

    ScopedReplaceWorkflowState state() const;
    const ScopedReplaceWorkflowResult& lastResult() const;
    const rtledit::PreparedWorkspaceEditTransaction*
        preparedTransaction() const;
    bool hasPendingPreview() const;
    bool canUndoAppliedTransaction() const;

signals:
    void stateChanged(ScopedReplaceWorkflowResult result);
    void failureRaised(ScopedReplaceWorkflowResult result);

private:
    struct CapturedDocument {
        std::string filePath;
        rtledit::WorkspaceDocumentSnapshot snapshot;
        quint64 searchRevision = 0;
        QString externalStateToken;
    };

    rtledit::WorkspaceDocumentManager* documentManager = nullptr;
    std::unique_ptr<WorkspaceEditTransactionService>
        ownedTransactionService;
    WorkspaceEditTransactionService* transactionService = nullptr;
    QPointer<NotificationCenter> notificationCenter;

    ScopedReplaceWorkflowState currentState =
        ScopedReplaceWorkflowState::Idle;
    ScopedReplaceWorkflowResult currentResult;
    std::optional<rtledit::PreparedWorkspaceEditTransaction>
        pendingTransaction;
    std::vector<CapturedDocument> capturedDocuments;
    bool workflowUndoAvailable = false;

    ScopedReplaceWorkflowResult publish(
        ScopedReplaceWorkflowState state,
        ScopedReplaceWorkflowFailure failure,
        const QString& message,
        rtledit::TransactionStatus transactionStatus =
            rtledit::TransactionStatus::InvalidPreparation,
        const QString& renderedDiff = QString(),
        int fileCount = 0,
        int editCount = 0);
    ScopedReplaceWorkflowResult fail(
        ScopedReplaceWorkflowFailure failure,
        const QString& message,
        rtledit::TransactionStatus transactionStatus =
            rtledit::TransactionStatus::InvalidPreparation);
    void postFailureNotification(
        const ScopedReplaceWorkflowResult& result);
    void clearPending();

    static QString normalizedFileName(const std::string& filePath);
    static QString documentIdentity(const QString& fileName);
    static QString externalStateToken(const QString& fileName);
};

#endif // SCOPEDREPLACEWORKFLOW_H
