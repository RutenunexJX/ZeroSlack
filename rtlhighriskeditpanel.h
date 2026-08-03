#ifndef RTLHIGHRISKEDITPANEL_H
#define RTLHIGHRISKEDITPANEL_H

#include "rtlconnectiontransformworkflow.h"
#include "rtlrenameworkflow.h"

#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

enum class RtlHighRiskEditKind {
    None,
    Rename,
    ConnectionTransform
};

enum class RtlHighRiskEditPanelState {
    Empty,
    Editing,
    Preparing,
    PreviewReady,
    DryRunPreviewReady,
    Confirming,
    Applied,
    DryRunComplete,
    Cancelled,
    Undone,
    NoChanges,
    Rejected,
    Conflict
};

struct RtlHighRiskEditDraft {
    std::uint64_t sessionId = 0;
    RtlHighRiskEditKind kind =
        RtlHighRiskEditKind::None;
    QString newName;
    bool convertOrderedToNamed = true;
    bool addMissingPorts = false;
    RtlMissingPortConnectionPolicy missingPortPolicy =
        RtlMissingPortConnectionPolicy::LeaveUnconnected;
    RtlExplicitCastPolicy castPolicy =
        RtlExplicitCastPolicy::PreserveExistingExpression;
};

struct RtlHighRiskEditPanelOutcome {
    RtlHighRiskEditPanelState panelState =
        RtlHighRiskEditPanelState::Empty;
    RtlHighRiskEditWorkflowState workflowState =
        RtlHighRiskEditWorkflowState::Idle;
    RtlHighRiskEditWorkflowFailure failure =
        RtlHighRiskEditWorkflowFailure::None;
    rtledit::TransactionStatus transactionStatus =
        rtledit::TransactionStatus::InvalidPreparation;
    QString actionId;
    QString message;
    QString conflictFile;
    QString confirmationFingerprint;
    bool dryRun = false;
    int fileCount = 0;
    int editCount = 0;
    bool hasStructuredPreview = false;
    bool canPreview = false;
    bool canConfirm = false;
    bool canCancel = false;
    bool canUndo = false;
    rtledit::WorkspaceEditPreview structuredPreview;
    rtledit::WorkspaceEditSourceDiff sourceDiff;
    QString renderedDiff;
};

struct RtlRenamePanelSession {
    RtlRenamePlanQuery baseQuery;
    QString subjectLabel;
    QString oldName;
    QString suggestedNewName;
};

struct RtlConnectionTransformPanelSession {
    RtlConnectionTransformRequest baseRequest;
    QString instanceLabel;
    bool dryRun = false;
};

Q_DECLARE_METATYPE(RtlHighRiskEditKind)
Q_DECLARE_METATYPE(RtlHighRiskEditPanelState)
Q_DECLARE_METATYPE(RtlHighRiskEditDraft)
Q_DECLARE_METATYPE(RtlHighRiskEditPanelOutcome)

// A non-applying input and High+Diff presentation surface. It never owns a
// planner, a prepared transaction, or a confirmation token.
class RtlHighRiskEditPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit RtlHighRiskEditPanel(
        QWidget* parent = nullptr);

    void beginRename(
        std::uint64_t sessionId,
        const QString& subjectLabel,
        const QString& oldName,
        const QString& suggestedNewName,
        bool dryRun);
    void beginConnectionTransform(
        std::uint64_t sessionId,
        const QString& instanceLabel,
        const RtlConnectionTransformRequest& defaults,
        bool dryRun);
    void presentOutcome(
        const RtlHighRiskEditPanelOutcome& outcome);
    void clearSession();

    RtlHighRiskEditDraft draft() const;
    RtlHighRiskEditKind editKind() const;
    RtlHighRiskEditPanelState state() const;
    std::uint64_t sessionId() const;
    QString statusText() const;
    QString confirmationFingerprint() const;
    int diffFileCount() const;
    int diffHunkCount() const;
    QString displayedDiffText() const;

    void setRenameNewName(const QString& name);
    void setConnectionOptions(
        bool convertOrderedToNamed,
        bool addMissingPorts,
        RtlMissingPortConnectionPolicy missingPortPolicy,
        RtlExplicitCastPolicy castPolicy);

signals:
    void draftChanged(std::uint64_t sessionId);
    void previewRequested(std::uint64_t sessionId);
    void confirmRequested(std::uint64_t sessionId);
    void cancelRequested(std::uint64_t sessionId);
    void undoRequested(std::uint64_t sessionId);

private:
    RtlHighRiskEditKind currentKind =
        RtlHighRiskEditKind::None;
    RtlHighRiskEditPanelState currentState =
        RtlHighRiskEditPanelState::Empty;
    std::uint64_t currentSessionId = 0;
    bool rebuilding = false;
    rtledit::WorkspaceEditSourceDiff currentSourceDiff;

    QStackedWidget* inputStack = nullptr;
    QWidget* emptyInputPage = nullptr;
    QWidget* renameInputPage = nullptr;
    QWidget* connectionInputPage = nullptr;
    QLabel* renameSubjectLabel = nullptr;
    QLabel* renameOldNameLabel = nullptr;
    QLineEdit* renameNewNameEdit = nullptr;
    QLabel* connectionInstanceLabel = nullptr;
    QCheckBox* convertOrderedCheck = nullptr;
    QCheckBox* addMissingPortsCheck = nullptr;
    QComboBox* missingPortPolicyCombo = nullptr;
    QComboBox* castPolicyCombo = nullptr;
    QLabel* stateLabel = nullptr;
    QLabel* transactionSummary = nullptr;
    QLabel* conflictLabel = nullptr;
    QLabel* tokenFingerprintLabel = nullptr;
    QTreeWidget* diffTree = nullptr;
    QPlainTextEdit* diffView = nullptr;
    QPushButton* previewButton = nullptr;
    QPushButton* confirmButton = nullptr;
    QPushButton* cancelButton = nullptr;
    QPushButton* undoButton = nullptr;

    void setupUi();
    void emitDraftChanged();
    void clearDiff();
    void rebuildDiff(
        const rtledit::WorkspaceEditSourceDiff& diff);
    void renderDiffSelection(QTreeWidgetItem* item);
    void updateInputAvailability(
        const RtlHighRiskEditPanelOutcome& outcome);
    static QString renderHunk(
        const rtledit::SourceDiffFile& file,
        int hunkIndex);
};

// Owns the only confirmation token and the only active session for both RTL
// workflows. The contained workflows remain the sole transaction protocol.
class RtlHighRiskEditPanelCoordinator final : public QObject
{
    Q_OBJECT

public:
    RtlHighRiskEditPanelCoordinator(
        QWidget* dockParent,
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions,
        QObject* parent = nullptr);
    RtlHighRiskEditPanelCoordinator(
        QWidget* dockParent,
        std::unique_ptr<RtlRenameWorkflow> renameWorkflow,
        std::unique_ptr<RtlConnectionTransformWorkflow>
            connectionWorkflow,
        QObject* parent = nullptr);
    ~RtlHighRiskEditPanelCoordinator() override;

    static QString panelId();

    QDockWidget* dock() const;
    RtlHighRiskEditPanel* panel() const;
    std::uint64_t activeSessionId() const;
    RtlHighRiskEditKind activeKind() const;
    const RtlHighRiskEditPanelOutcome& lastOutcome() const;
    bool hasPendingPreview() const;
    bool hasProtectedUndoPosition() const;
    bool hasPendingConfirmationToken() const;

    bool beginRename(
        RtlRenamePanelSession session,
        QString* failureReason = nullptr);
    bool beginConnectionTransform(
        RtlConnectionTransformPanelSession session,
        QString* failureReason = nullptr);

    RtlHighRiskEditPanelOutcome requestPreview();
    RtlHighRiskEditPanelOutcome requestPreview(
        std::uint64_t sessionId);
    RtlHighRiskEditPanelOutcome confirm();
    RtlHighRiskEditPanelOutcome confirm(
        std::uint64_t sessionId);
    RtlHighRiskEditPanelOutcome cancel();
    RtlHighRiskEditPanelOutcome cancel(
        std::uint64_t sessionId);
    RtlHighRiskEditPanelOutcome undo();
    RtlHighRiskEditPanelOutcome undo(
        std::uint64_t sessionId);
    void resetForWorkspaceClose();

signals:
    void stateChanged(
        RtlHighRiskEditPanelOutcome outcome);
    void acceptedParameters(
        QString actionId,
        QVariantMap parameters,
        bool dryRun);

private:
    QPointer<QDockWidget> dockWidget;
    QPointer<RtlHighRiskEditPanel> panelWidget;
    std::unique_ptr<WorkspaceEditTransactionService>
        ownedTransactionService;
    std::unique_ptr<RtlRenameWorkflow> rename;
    std::unique_ptr<RtlConnectionTransformWorkflow>
        connection;
    std::optional<RtlRenamePanelSession> renameSession;
    std::optional<RtlConnectionTransformPanelSession>
        connectionSession;
    RtlHighRiskEditKind currentKind =
        RtlHighRiskEditKind::None;
    RtlHighRiskEditKind appliedOwner =
        RtlHighRiskEditKind::None;
    std::uint64_t currentSessionId = 0;
    std::uint64_t nextSessionId = 1;
    QString pendingConfirmationToken;
    RtlHighRiskEditPanelOutcome currentOutcome;

    void setupUi(QWidget* dockParent);
    void wirePanel();
    void draftChanged(std::uint64_t sessionId);
    void cancelActivePendingPreview();
    void publishEditing(const QString& message);
    RtlHighRiskEditPanelOutcome publish(
        const RtlHighRiskEditWorkflowResult& result);
    RtlHighRiskEditPanelOutcome ignoredRequest(
        const QString& message) const;
    bool sessionMatches(std::uint64_t sessionId) const;
    bool currentWorkflowCanUndo() const;
    RtlHighRiskEditWorkflow* currentTransactionWorkflow();
    const RtlHighRiskEditWorkflow*
    currentTransactionWorkflow() const;
    static bool isConflict(
        RtlHighRiskEditWorkflowFailure failure);
    static QString confirmationFingerprintFor(
        const QString& token);
};

#endif // RTLHIGHRISKEDITPANEL_H
