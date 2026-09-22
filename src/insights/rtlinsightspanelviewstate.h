#ifndef RTLINSIGHTSPANELVIEWSTATE_H
#define RTLINSIGHTSPANELVIEWSTATE_H

#include "moduleblockdiagramservice.h"
#include "rtlinsightlink.h"

#include <QRectF>
#include <QString>
#include <QHash>
#include <QSet>
#include <QPointer>

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
class ModuleBlockDiagramToolbar;

struct ModuleBlockScope
{
    QString fileName;
    QString moduleName;
    QString label;
    QString instancePath;
};

struct RtlInsightsPanelViewState
{
    QPointer<QObject> graphCallbackContext;
    QDockWidget* insightsDock = nullptr;
    QStackedWidget* insightsStack = nullptr;
    QTreeWidget* insightsTree = nullptr;
    QWidget* insightsGraphPanel = nullptr;
    QWidget* graphToolbar = nullptr;
    ModuleBlockDiagramToolbar* moduleBlockToolbar = nullptr;
    QGraphicsScene* insightsGraphScene = nullptr;
    InsightGraphView* insightsGraphView = nullptr;
    QWidget* graphInspectorPanel = nullptr;
    QTreeWidget* graphInspector = nullptr;
    QTableWidget* graphTable = nullptr;
    SignalUsageHotspotPanel* signalUsageHotspotPanel =
        nullptr;
    QPushButton* signalUsageHotspotButton = nullptr;
    QPushButton* fsmGraphButton = nullptr;
    QPushButton* moduleBlockDiagramButton = nullptr;
    QPushButton* graphZoomOutButton = nullptr;
    QPushButton* graphFitButton = nullptr;
    QPushButton* graphZoomInButton = nullptr;
    QLineEdit* graphSearchEdit = nullptr;
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
    QAction* graphTemporaryEditorAction = nullptr;
    QAction* graphExportAction = nullptr;
    QPushButton* graphInspectorJumpButton = nullptr;
    QPushButton* graphInspectorFocusButton = nullptr;
    QPushButton* graphInspectorRevealButton = nullptr;

    QString currentFileName;
    QString currentModuleName;
    QString currentSignalName;
    RtlInsightSourceLocation currentSourceLocation;
    RtlInsightSourceLocation pendingSourceLocation;
    bool hasPendingSourceLocation = false;
    bool pinned = false;
    bool stateViewEnabled = true;
    quint64 graphBuildGeneration = 0;
    quint64 graphDocumentRevision = 0;
    QString graphSearchText;
    QString currentGraphMode;
    QRectF lastGraphFitRect;
    ModuleBlockDiagramReport currentModuleBlockReport;
    int currentModuleBlockSelectedNodeId = -1;
    QList<ModuleBlockScope> moduleBlockPath;
    QHash<int, QString> moduleBlockInstancePaths;
    QString moduleBlockTargetPath;
    QStringList moduleBlockHistory;
    int moduleBlockHistoryIndex = -1;
    bool moduleBlockAutoFit = true;
    QString moduleBlockRenderedScope;

    std::function<bool(const RtlInsightSourceLocation&)>
        sourceNavigationHandler;
    std::function<void(const QString&, int)>
        statusMessageHandler;
};

#endif // RTLINSIGHTSPANELVIEWSTATE_H
