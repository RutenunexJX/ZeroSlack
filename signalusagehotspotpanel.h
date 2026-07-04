#ifndef SIGNALUSAGEHOTSPOTPANEL_H
#define SIGNALUSAGEHOTSPOTPANEL_H

#include "signalusagehotspotservice.h"

#include <QWidget>

#include <functional>
#include <QSet>

class QCheckBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSplitter;
class QStackedWidget;
class QTreeWidget;

class SignalUsageHotspotPanel : public QWidget
{
public:
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
    void setMatrixModeForTest(bool matrixMode);
    bool triggerFirstUsageNavigationForTest();

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
    QList<QCheckBox*> roleChecks;
    QSplitter* contentSplitter = nullptr;
    QStackedWidget* modeStack = nullptr;
    QGraphicsScene* trackScene = nullptr;
    QGraphicsView* trackView = nullptr;
    QGraphicsScene* matrixScene = nullptr;
    QGraphicsView* matrixView = nullptr;
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
    void setTrackZoom(double zoomFactor);
    void zoomTrack(double factor);
    void fitTrackToView();
    void centerCurrentUsage();
    void resetLayout();
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
};

#endif // SIGNALUSAGEHOTSPOTPANEL_H
