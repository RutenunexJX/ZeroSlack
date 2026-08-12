#ifndef RTLINSIGHTSPANELVIEWSTATE_H
#define RTLINSIGHTSPANELVIEWSTATE_H

#include "moduleblockdiagramservice.h"
#include "rtlinsightlink.h"

#include <QRectF>
#include <QString>

#include <functional>

class InsightGraphView;
class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QGraphicsScene;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QTreeWidget;
class QWidget;
class SignalUsageHotspotPanel;

struct RtlInsightsPanelViewState
{
    QDockWidget* insightsDock = nullptr;
    QStackedWidget* insightsStack = nullptr;
    QTreeWidget* insightsTree = nullptr;
    QWidget* insightsGraphPanel = nullptr;
    QGraphicsScene* insightsGraphScene = nullptr;
    InsightGraphView* insightsGraphView = nullptr;
    QTreeWidget* graphInspector = nullptr;
    QTableWidget* graphTable = nullptr;
    SignalUsageHotspotPanel* signalUsageHotspotPanel =
        nullptr;
    QPushButton* moduleBriefButton = nullptr;
    QPushButton* signalJourneyButton = nullptr;
    QPushButton* signalUsageHotspotButton = nullptr;
    QPushButton* clockResetButton = nullptr;
    QPushButton* fsmGraphButton = nullptr;
    QPushButton* moduleBlockDiagramButton = nullptr;
    QPushButton* graphZoomOutButton = nullptr;
    QPushButton* graphFitButton = nullptr;
    QPushButton* graphZoomInButton = nullptr;
    QLineEdit* graphSearchEdit = nullptr;
    QComboBox* moduleBlockTopCombo = nullptr;
    QPushButton* moduleBlockSetSelectionButton =
        nullptr;
    QSpinBox* moduleBlockDepthSpin = nullptr;
    QCheckBox* moduleBlockCollapsePackagesCheck =
        nullptr;
    QCheckBox* moduleBlockShowUnresolvedCheck =
        nullptr;
    QComboBox* stateTransitionSignalCombo = nullptr;
    QComboBox* stateTransitionCurrentCombo = nullptr;
    QComboBox* stateTransitionNextCombo = nullptr;
    QCheckBox* stateTransitionResetCheck = nullptr;
    QCheckBox* stateTransitionErrorCheck = nullptr;
    QCheckBox* stateTransitionUnreachableCheck =
        nullptr;
    QComboBox* graphLayoutCombo = nullptr;
    QToolButton* graphMoreButton = nullptr;
    QToolButton* pinButton = nullptr;
    QAction* graphJumpAction = nullptr;
    QAction* graphFocusAction = nullptr;
    QAction* graphSetTopAction = nullptr;
    QAction* graphTemporaryEditorAction = nullptr;
    QAction* graphExportAction = nullptr;
    QPushButton* graphInspectorJumpButton = nullptr;
    QPushButton* graphInspectorFocusButton = nullptr;
    QPushButton* graphInspectorSetTopButton = nullptr;
    QPushButton* graphInspectorRevealButton = nullptr;

    QString currentFileName;
    QString currentModuleName;
    QString currentSignalName;
    RtlInsightSourceLocation currentSourceLocation;
    RtlInsightSourceLocation pendingSourceLocation;
    bool hasPendingSourceLocation = false;
    bool pinned = false;
    quint64 graphBuildGeneration = 0;
    quint64 graphDocumentRevision = 0;
    QString graphSearchText;
    QString currentGraphMode;
    QRectF lastGraphFitRect;
    ModuleBlockDiagramReport currentModuleBlockReport;
    int currentModuleBlockSelectedNodeId = -1;

    std::function<bool(const RtlInsightSourceLocation&)>
        sourceNavigationHandler;
    std::function<void(const QString&, int)>
        statusMessageHandler;
    std::function<void(const QString&, const QString&)>
        moduleBlockDiagramRequestHandler;
};

#endif // RTLINSIGHTSPANELVIEWSTATE_H
