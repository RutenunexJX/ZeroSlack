#ifndef INSTANCEPAIRCONNECTIONPANEL_H
#define INSTANCEPAIRCONNECTIONPANEL_H

#include "zeroslackexport.h"

#include "instancepairconnectionfacade.h"

#include <QHash>
#include <QMetaType>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>

class QEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;

// This request is the only value emitted by the drag surface. It preserves
// semantic identity and revision context instead of asking a UI consumer to
// recover either from visible labels.
struct InstancePairConnectionPlanRequest {
    SymbolStableKey leftSignalStableKey;
    QString leftSignalName;
    QString leftInstancePath;
    QString leftModuleName;
    QString rightInstancePath;
    QString rightModuleName;
    QString connectionName;
    std::uint64_t documentRevision = 0;
    std::uint64_t semanticGeneration = 0;
    QHash<QString, std::uint64_t> documentRevisions;

    bool isValid() const;
    bool sameContext(
        const InstancePairConnectionPlanRequest& other) const;
};

Q_DECLARE_METATYPE(InstancePairConnectionPlanRequest)

// The drag is deliberately process-local and typed. The drop target never
// deserializes a display string or accepts arbitrary textual MIME payloads.
class ZEROSLACK_API InstancePairConnectionDragMimeData final : public QMimeData
{
public:
    explicit InstancePairConnectionDragMimeData(
        InstancePairConnectionPlanRequest request);

    static QString mimeType();
    const InstancePairConnectionPlanRequest& request() const;

private:
    InstancePairConnectionPlanRequest typedRequest;
};

class ZEROSLACK_API InstancePairConnectionPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit InstancePairConnectionPanel(QWidget* parent = nullptr);

    void setAnalysis(
        const InstancePairConnectionAnalysis& analysis);
    bool setProposal(
        const InstancePairConnectionProposal& proposal);
    void clearProposal();
    void cancelDragState();
    void setWorkflowOutcome(
        const QString& message,
        bool applied,
        bool undoAvailable);

    const InstancePairConnectionAnalysis& analysis() const;
    const InstancePairConnectionProposal* proposalForConfirmation() const;
    InstancePairConnectionPlanRequest currentPlanRequest() const;
    bool hasDisplayableProposal() const;
    bool dragActive() const;
    int diffFileCount() const;

signals:
    // A legal signal-to-instance drop asks the owner to build a plan.
    void planRequested(InstancePairConnectionPlanRequest request);
    // The button is an explicit non-mutating preview request.
    void previewRequested(InstancePairConnectionPlanRequest request);
    // The owner may retrieve proposalForConfirmation(); this panel never
    // confirms or applies a WorkspaceEdit transaction itself.
    void confirmRequested(InstancePairConnectionPlanRequest request);
    void undoRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    InstancePairConnectionAnalysis currentAnalysis;
    std::optional<InstancePairConnectionProposal> currentProposal;
    InstancePairConnectionPlanRequest requestContext;

    QLabel* stateLabel = nullptr;
    QLabel* leftInstanceLabel = nullptr;
    QLabel* leftModuleLabel = nullptr;
    QLabel* rightInstanceLabel = nullptr;
    QLabel* rightModuleLabel = nullptr;
    QTreeWidget* leftItems = nullptr;
    QTreeWidget* rightItems = nullptr;
    QWidget* dropTarget = nullptr;
    QLabel* transactionSummary = nullptr;
    QTabWidget* diffTabs = nullptr;
    QPushButton* previewButton = nullptr;
    QPushButton* confirmButton = nullptr;
    QPushButton* undoButton = nullptr;
    bool activeDrag = false;

    void rebuildBlocks();
    void rebuildRequestContext();
    void renderProposal(
        const InstancePairConnectionProposal& proposal);
    void renderRejectedProposal(
        const InstancePairConnectionProposal& proposal);
    void clearDiffTabs();
    void setDragActive(bool active);
    bool acceptsDropRequest(
        const InstancePairConnectionPlanRequest& request) const;
    bool proposalMatchesCurrentContext(
        const InstancePairConnectionProposal& proposal,
        QString* reason) const;
    void rejectStalePayload(const QString& reason);
};

// The coordinator attaches exactly one reusable panel page to a stack. Data
// updates do not select the page, show/raise a window, resize a dock, or move
// keyboard focus.
class ZEROSLACK_API InstancePairConnectionCoordinator final : public QObject
{
    Q_OBJECT

public:
    explicit InstancePairConnectionCoordinator(
        QStackedWidget* host,
        QObject* parent = nullptr);

    static QString panelId();
    InstancePairConnectionPanel* panel() const;
    void presentAnalysis(
        const InstancePairConnectionAnalysis& analysis);
    bool presentProposal(
        const InstancePairConnectionProposal& proposal);
    void clearProposal();

signals:
    void planRequested(InstancePairConnectionPlanRequest request);
    void previewRequested(InstancePairConnectionPlanRequest request);
    void confirmRequested(InstancePairConnectionPlanRequest request);
    void undoRequested();

private:
    QPointer<QStackedWidget> hostStack;
    QPointer<InstancePairConnectionPanel> panelWidget;

    InstancePairConnectionPanel* ensurePanel();
    void wirePanel(InstancePairConnectionPanel* panel);
};

#endif // INSTANCEPAIRCONNECTIONPANEL_H
