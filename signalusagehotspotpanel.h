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

class QCheckBox;
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
struct SignalUsageHotspotThemePresentationSnapshot;

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

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QLabel* titleLabel = nullptr;
    QLineEdit* searchEdit = nullptr;
    QPushButton* trackModeButton = nullptr;
    QPushButton* matrixModeButton = nullptr;
    QPushButton* zoomOutButton = nullptr;
    QPushButton* zoomInButton = nullptr;
    QPushButton* fitButton = nullptr;
    QPushButton* centerCurrentButton = nullptr;
    QPushButton* resetLayoutButton = nullptr;
    QAction* fitViewAction = nullptr;
    QAction* zoomInViewAction = nullptr;
    QAction* zoomOutViewAction = nullptr;
    QAction* centerCurrentViewAction = nullptr;
    QAction* resetLayoutViewAction = nullptr;
    QAction* exportTrackAction = nullptr;
    QAction* exportMatrixAction = nullptr;
    QList<QCheckBox*> roleChecks;
    QSplitter* contentSplitter = nullptr;
    QStackedWidget* modeStack = nullptr;
    QGraphicsScene* trackScene = nullptr;
    InsightGraphView* trackView = nullptr;
    QGraphicsScene* matrixScene = nullptr;
    InsightGraphView* matrixView = nullptr;
    QTreeWidget* matrixItemsTree = nullptr;
    QLabel* inspectorTitleLabel = nullptr;
    QLabel* inspectorDetailLabel = nullptr;

    SignalUsageHotspotQuery currentQuery;
    SignalUsageHotspotReport currentReport;
    QString searchText;
    SignalUsageHotspotRole activeMatrixRole = SignalUsageHotspotRole::Unknown;
    QString activeMatrixModuleName;
    QString activeMatrixFileName;
    bool activeMatrixCellValid = false;
    QSet<int> focusedItemIndexes;
    QString currentEditorFileName;
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
    int presentationThemeMode = -1;
    std::uint64_t themePresentationGeneration = 0;
    std::shared_ptr<const SignalUsageHotspotThemePresentationSnapshot>
        pendingThemePresentation;

    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void setMode(bool matrixMode);
    void rebuild();
    void renderUnavailable(const QString& message);
    void renderTrack();
    void renderMatrix();
    void renderMatrixItems();
    void activateMatrixCell(SignalUsageHotspotRole role,
                            const QString& moduleName,
                            const QString& fileName,
                            bool focusTrack);
    void activateMatrixCellForItem(int itemIndex, bool focusTrack);
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
    void showInspectorForItem(int itemIndex);
    void showInspectorMessage(const QString& title, const QString& message);
    void showMatrixCellDetails();
    void navigateItem(int itemIndex);
    bool itemPassesFilters(const SignalUsageHotspotItem& item) const;
    bool roleEnabled(SignalUsageHotspotRole role) const;
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
