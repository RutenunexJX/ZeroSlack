#ifndef SIGNALUSAGEHOTSPOTPANEL_H
#define SIGNALUSAGEHOTSPOTPANEL_H

#include "signalusagehotspotservice.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
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
    void renderReportForTest(const SignalUsageHotspotReport& report);
    int trackBlockCountForTest() const;
    int matrixNonEmptyCellCountForTest() const;
    bool triggerFirstUsageNavigationForTest();

private:
    QLabel* titleLabel = nullptr;
    QLineEdit* searchEdit = nullptr;
    QPushButton* trackModeButton = nullptr;
    QPushButton* matrixModeButton = nullptr;
    QList<QCheckBox*> roleChecks;
    QStackedWidget* modeStack = nullptr;
    QGraphicsScene* trackScene = nullptr;
    QGraphicsView* trackView = nullptr;
    QTableWidget* matrixTable = nullptr;
    QTreeWidget* matrixItemsTree = nullptr;
    QLabel* inspectorTitleLabel = nullptr;
    QLabel* inspectorDetailLabel = nullptr;

    SignalUsageHotspotQuery currentQuery;
    SignalUsageHotspotReport currentReport;
    QString searchText;
    SignalUsageHotspotRole activeMatrixRole = SignalUsageHotspotRole::Unknown;
    QString activeMatrixModuleName;
    QString activeMatrixFileName;
    int selectedItemIndex = -1;
    int lastTrackBlockCount = 0;
    int lastMatrixNonEmptyCellCount = 0;

    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void setMode(bool matrixMode);
    void rebuild();
    void renderUnavailable(const QString& message);
    void renderTrack();
    void renderMatrix();
    void renderMatrixItems();
    void showInspectorForItem(int itemIndex);
    void showInspectorMessage(const QString& title, const QString& message);
    void navigateItem(int itemIndex);
    bool itemPassesFilters(const SignalUsageHotspotItem& item) const;
    bool roleEnabled(SignalUsageHotspotRole role) const;
    QList<int> filteredItemIndexes() const;
    void showStatusMessage(const QString& message, int timeoutMs) const;
};

#endif // SIGNALUSAGEHOTSPOTPANEL_H
