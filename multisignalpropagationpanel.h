#ifndef MULTISIGNALPROPAGATIONPANEL_H
#define MULTISIGNALPROPAGATIONPANEL_H

#include "multisignalpropagationplanner.h"

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QWidget>

#include <functional>
#include <cstdint>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class SemanticIndex;
class WorkspaceEditTransactionService;

struct MultiSignalPropagationSignalChoice {
    QString label;
    MultiSignalPropagationMemberRequest member;
    bool selected = false;
};

struct MultiSignalPropagationAncestorChoice {
    QString label;
    QString instancePath;
};

struct MultiSignalPropagationPanelInput {
    QList<MultiSignalPropagationSignalChoice> signalChoices;
    QList<MultiSignalPropagationAncestorChoice> ancestors;
    int selectedAncestorIndex = 0;
    MultiSignalPropagationMode mode =
        MultiSignalPropagationMode::IndependentPorts;
    QString groupName;
    QSet<QString> workspaceFiles;
    SemanticSnapshotToken semanticToken;
    QHash<QString, MultiSignalPropagationDocumentSnapshot>
        capturedDocuments;
    // Planning is always side-effect free. This flag controls whether the
    // explicitly confirmed prepared transaction may mutate the workspace.
    bool dryRun = true;
};

// Compact, non-applying High+Diff preview surface. Planning is synchronous and
// occurs only after an explicit requestPreview() or Preview button activation.
class MultiSignalPropagationPanel final : public QWidget
{
    Q_OBJECT

public:
    using PlanCallback = std::function<
        MultiSignalPropagationProposal(
            const MultiSignalPropagationQuery&,
            rtledit::WorkspaceDocumentManager&)>;

    MultiSignalPropagationPanel(
        MultiSignalPropagationPlanner& planner,
        rtledit::WorkspaceDocumentManager& documents,
        QWidget* parent = nullptr);
    MultiSignalPropagationPanel(
        PlanCallback planner,
        rtledit::WorkspaceDocumentManager& documents,
        QWidget* parent = nullptr);

    static QString panelId();
    void setInput(
        const MultiSignalPropagationPanelInput& input);
    const MultiSignalPropagationPanelInput& input() const;

    void setSignalSelected(int index, bool selected);
    void setExportedPortName(
        int index, const QString& name);
    void setGroupMemberName(
        int index, const QString& name);
    void setSelectedAncestorIndex(int index);
    void setMode(MultiSignalPropagationMode mode);
    void setGroupName(const QString& name);

    int selectedSignalCount() const;
    bool requestPreview();
    void clearPreview();

    const MultiSignalPropagationQuery* lastQuery() const;
    const MultiSignalPropagationProposal* proposal() const;
    bool hasHighDiffPreview() const;
    QString statusText() const;
    QString renderedDiff() const;
    void setTransactionOutcome(
        const QString& message,
        bool applied,
        bool undoAvailable);

signals:
    void confirmRequested();
    void undoRequested();

private:
    PlanCallback planCallback;
    rtledit::WorkspaceDocumentManager* documentManager =
        nullptr;
    MultiSignalPropagationPanelInput currentInput;
    std::optional<MultiSignalPropagationQuery>
        lastBuiltQuery;
    std::optional<MultiSignalPropagationProposal>
        currentProposal;
    bool highDiffPreview = false;
    bool rebuilding = false;

    QTableWidget* signalTable = nullptr;
    QComboBox* ancestorCombo = nullptr;
    QComboBox* modeCombo = nullptr;
    QLineEdit* groupNameEdit = nullptr;
    QPushButton* previewButton = nullptr;
    QPushButton* confirmButton = nullptr;
    QPushButton* undoButton = nullptr;
    QLabel* stateLabel = nullptr;
    QLabel* transactionSummary = nullptr;
    QListWidget* blockersList = nullptr;
    QPlainTextEdit* diffView = nullptr;

    void setupUi();
    void rebuildInputControls();
    void updateModeControls();
    void updatePreviewEnabled();
    void inputChanged();
    MultiSignalPropagationQuery buildQuery() const;
    void renderProposal(
        const MultiSignalPropagationProposal& proposal);
    void renderFailure(
        const MultiSignalPropagationProposal& proposal);
};

enum class MultiSignalPropagationWorkflowState {
    Idle,
    PreviewReady,
    DryRunComplete,
    Applied,
    Undone,
    Failed
};

struct MultiSignalPropagationWorkflowResult {
    MultiSignalPropagationWorkflowState state =
        MultiSignalPropagationWorkflowState::Idle;
    rtledit::TransactionStatus transactionStatus =
        rtledit::TransactionStatus::InvalidPreparation;
    QString message;
    bool successful = false;

    bool succeeded() const;
};

Q_DECLARE_METATYPE(MultiSignalPropagationWorkflowState)
Q_DECLARE_METATYPE(MultiSignalPropagationWorkflowResult)

// Owns confirmation-time conflict checking and the single unified workspace
// transaction used by the panel. The panel remains a non-applying preview
// surface and only emits explicit confirmation/undo requests.
class MultiSignalPropagationWorkflow final : public QObject
{
    Q_OBJECT

public:
    MultiSignalPropagationWorkflow(
        MultiSignalPropagationPanel* panel,
        SemanticIndex* semanticIndex,
        rtledit::WorkspaceDocumentManager* documents,
        WorkspaceEditTransactionService* transactions,
        QObject* parent = nullptr);

    MultiSignalPropagationWorkflowResult confirm();
    MultiSignalPropagationWorkflowResult undo();
    const MultiSignalPropagationWorkflowResult&
    lastResult() const;
    bool canUndoAppliedTransaction() const;

signals:
    void stateChanged(
        MultiSignalPropagationWorkflowResult result);

private:
    MultiSignalPropagationPanel* panelWidget = nullptr;
    SemanticIndex* index = nullptr;
    rtledit::WorkspaceDocumentManager* documentManager = nullptr;
    WorkspaceEditTransactionService* transactionService = nullptr;
    MultiSignalPropagationWorkflowResult currentResult;
    std::uint64_t appliedTransactionGeneration = 0;
    bool workflowUndoAvailable = false;

    MultiSignalPropagationWorkflowResult publish(
        MultiSignalPropagationWorkflowState state,
        rtledit::TransactionStatus status,
        const QString& message,
        bool succeeded);
};

#endif // MULTISIGNALPROPAGATIONPANEL_H
