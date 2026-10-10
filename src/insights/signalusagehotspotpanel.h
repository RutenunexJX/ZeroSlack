#ifndef SIGNALUSAGEHOTSPOTPANEL_H
#define SIGNALUSAGEHOTSPOTPANEL_H

#include "actionregistry.h"
#include "graphexportservice.h"
#include "signalusagehotspotservice.h"
#include "zeroslackexport.h"

#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <QSet>
#include <QHash>
#include <QPointer>
#include <QVariantMap>

class QAction;
class QGraphicsScene;
class InsightGraphView;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSplitter;
class QStackedWidget;
class QTreeWidget;
class QToolButton;
class QGraphicsLineItem;

enum class SignalUsageHotspotExportSurface {
    Track,
    Matrix
};

class ZEROSLACK_API SignalUsageHotspotPanel : public QWidget,
                                public ActionExecutionHost
{
public:
    using ReportBuilder = std::function<SignalUsageHotspotReport(
        const SignalUsageHotspotQuery&,
        std::shared_ptr<const SemanticIndexSnapshot>)>;

    explicit SignalUsageHotspotPanel(QWidget* parent = nullptr);

    void setNavigationHandler(
        std::function<bool(const QString&, int, int)> handler);
    void setStatusMessageHandler(
        std::function<void(const QString&, int)> handler);

    void showHotspotForSymbol(const QString& symbolName,
                              const QString& fileName,
                              const QString& moduleName,
                              const QString& signalAccessPath = {});
    void refreshReport();
    void setWorkspaceRoot(const QString& root);
    void refreshSemanticSnapshot();
    void clearTarget();
    QVariantMap saveViewState() const;
    void restoreViewState(const QVariantMap& state);
    void setMainAreaHandler(std::function<void(bool)> handler);
    void setInMainArea(bool expanded);
    bool isInMainArea() const;
    void setCurrentEditorLocation(const QString& fileName, int line);
    void focusFit();
    void focusZoomIn();
    void focusZoomOut();
    void setFocusSearchText(const QString& text);
    QString focusSearchText() const;
    void focusInspector();
    void refreshThemePresentation();
    GraphExportResult exportGraph(
        SignalUsageHotspotExportSurface surface,
        const QString& outputPath,
        const GraphExportOptions& options = {}) const;
    QAction* graphExportAction(
        SignalUsageHotspotExportSurface surface) const;
    QAction* graphViewActionForTest(
        const QString& actionId) const;
    QList<QAction*> graphViewActionsForTest() const;
    ActionExecutionResult triggerGraphViewActionForTest(
        const QString& actionId);
    qreal trackZoomFactorForTest() const;
    void renderReportForTest(const SignalUsageHotspotReport& report);
    int trackBlockCountForTest() const;
    int trackLaneCountForTest() const;
    int matrixNonEmptyCellCountForTest() const;
    int matrixItemCountForTest() const;
    QList<qreal> trackBlockCenterXsForTest() const;
    qreal firstTrackRailWidthForTest() const;
    qreal trackSceneWidthForTest() const;
    int firstTrackLaneStartLineForTest() const;
    int firstTrackLaneEndLineForTest() const;
    bool selectMatrixCellForTest(SignalUsageHotspotRole role,
                                 const QString& moduleName,
                                 const QString& fileName);
    bool selectUsageForTest(int itemIndex);
    bool triggerFirstUsageNavigationForTest();
    void setReportBuilderForTest(ReportBuilder builder);
    bool reportBuildInFlightForTest() const;
    quint64 reportBuildRequestCountForTest() const;
    int selectedItemIndexForTest() const;
    QString currentDeclarationDisplayNameForTest() const;
    void setMatrixModeForTest(bool matrixMode);
    bool matrixModeForTest() const;
    QList<QList<int>> trackClustersForTest() const;
    qreal trackRailLeftForTest() const;
    qreal rowHeightForTest() const;
    bool selectTrackClusterForTest(int clusterIndex);
    QTreeWidget* inlineItemsForTest() const;
    void collapseDetails();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QLabel* titleLabel = nullptr;
    QLineEdit* searchEdit = nullptr;
    QPushButton* trackModeButton = nullptr;
    QPushButton* matrixModeButton = nullptr;
    QAction* fitViewAction = nullptr;
    QAction* zoomInViewAction = nullptr;
    QAction* zoomOutViewAction = nullptr;
    QAction* centerCurrentViewAction = nullptr;
    QAction* resetLayoutViewAction = nullptr;
    QAction* exportTrackAction = nullptr;
    QAction* exportMatrixAction = nullptr;
    QStackedWidget* modeStack = nullptr;
    QGraphicsScene* trackScene = nullptr;
    InsightGraphView* trackView = nullptr;
    QGraphicsScene* matrixScene = nullptr;
    InsightGraphView* matrixView = nullptr;
    QPointer<QTreeWidget> inlineItems;
    QToolButton* mainAreaButton = nullptr;
    QList<QToolButton*> roleButtons;
    QSet<int> enabledRoles;
    QString workspaceRoot;
    QString expandedLane;
    QList<int> detailIndexes;
    bool mainArea = false;
    bool renderQueued = false;
    bool rebuilding = false;
    std::function<void(bool)> mainAreaHandler;
    struct VisibleLane {
        int laneIndex = -1;
        QList<int> indexes;
        QHash<int, QList<int>> roles;
    };
    QList<VisibleLane> visibleLanes;
    QHash<QString, QString> reportFileKeys;
    QHash<QString, qreal> rowPositions[2];
    struct ReadingPosition {
        QString lane;
        qreal offset = 0;
        int horizontal = 0;
    };
    ReadingPosition pendingReadingPosition;
    bool pendingReadingPositionValid = false;
    QList<QList<int>> trackClusters;
    QList<QRectF> trackClusterRects;
    struct EditorMarker {
        int laneIndex = -1;
        qreal y = 0;
        QGraphicsLineItem* item = nullptr;
    };
    QList<EditorMarker> editorMarkers;
    qreal railLeft = 180;
    qreal rowHeight = 40;
    quint64 requestedSnapshotRevision = 0;
    QString pendingSelectedIdentity;

    SignalUsageHotspotQuery currentQuery;
    SignalUsageHotspotReport currentReport;
    QString searchText;
    SignalUsageHotspotRole activeMatrixRole = SignalUsageHotspotRole::Unknown;
    QString activeMatrixModuleName;
    QString activeMatrixFileName;
    bool activeMatrixCellValid = false;
    QSet<int> focusedItemIndexes;
    QString currentEditorFileName;
    QString currentEditorFileKey;
    int currentEditorLine = 0;
    int selectedItemIndex = -1;
    int lastTrackBlockCount = 0;
    int lastTrackLaneCount = 0;
    int lastMatrixNonEmptyCellCount = 0;
    qreal lastTrackRailWidth = 0.0;
    qreal lastTrackSceneWidth = 0.0;
    double trackZoomFactor = 1.0;
    ReportBuilder reportBuilder;
    std::uint64_t reportGeneration = 0;
    std::uint64_t reportBuildRequestCount = 0;
    int activeReportBuilds = 0;
    bool currentReportPending = false;

    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void setMode(bool matrixMode);
    void rebuild();
    void renderUnavailable(const QString& message);
    void renderTrack();
    void renderMatrix();
    void buildProjection();
    void queueRebuild();
    void updateEditorMarkers();
    ReadingPosition captureReadingPosition() const;
    void restoreReadingPosition(const ReadingPosition& position);
    qreal addInlineDetails(QGraphicsScene* scene, qreal y, qreal width);
    void selectIndexes(const QList<int>& indexes, const QString& lane);
    QString itemIdentity(int index) const;
    QString fileKey(const QString& file) const;
    QString laneKey(const QString& module, const QString& file) const;
    void cacheReportFileKeys();
    void activateMatrixCell(SignalUsageHotspotRole role,
                            const QString& moduleName,
                            const QString& fileName,
                            bool focusTrack);
    void clearMatrixFocus();
    QRectF trackRectForItem(int itemIndex) const;
    qreal targetTrackRailWidth() const;
    bool setTrackZoom(double zoomFactor);
    bool zoomTrack(double factor);
    bool fitTrackToView();
    bool centerCurrentUsage();
    bool resetLayout();
    void restoreLayout();
    void saveLayout() const;
    void navigateItem(int itemIndex);
    bool itemPassesFilters(const SignalUsageHotspotItem& item) const;
    QList<int> filteredItemIndexes(bool includeMatrixFocus = true) const;
    void showStatusMessage(const QString& message, int timeoutMs) const;
    QAction* createGraphViewAction(
        const QString& actionId);
    void bindGraphViewButton(
        QPushButton* button,
        QAction* action);
    QAction* graphViewAction(
        const QString& actionId) const;
    QList<QAction*> graphViewActions() const;
    bool hasGraphViewContent() const;
    void refreshGraphViewActionAvailability() const;
    ActionExecutionResult requestGraphViewAction(
        const QString& actionId);
    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override;
};

#endif // SIGNALUSAGEHOTSPOTPANEL_H
