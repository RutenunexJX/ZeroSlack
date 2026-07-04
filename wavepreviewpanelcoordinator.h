#ifndef WAVEPREVIEWPANELCOORDINATOR_H
#define WAVEPREVIEWPANELCOORDINATOR_H

#include "wavepreviewservice.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

class WavePreviewPanelCoordinator
{
public:
    explicit WavePreviewPanelCoordinator(QWidget* parent);
    ~WavePreviewPanelCoordinator();

    void setNavigationHandler(std::function<void(const QString&, int, int)> handler);
    void refreshFromDocument(const QString& fileName,
                             const QString& documentText,
                             bool dirty,
                             int scopeStartPosition = -1,
                             int scopeEndPosition = -1,
                             const QString& scopeLabel = QString());
    void renderUnavailable(const QString& message);

    QDockWidget* dock() const { return previewDock; }
    QTreeWidget* tree() const { return previewTree; }
    QWidget* canvas() const { return previewCanvas; }

private:
    QDockWidget* previewDock = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* summaryLabel = nullptr;
    QLabel* scopeLabel = nullptr;
    QComboBox* clockCombo = nullptr;
    QComboBox* resetCombo = nullptr;
    QLineEdit* laneFilterEdit = nullptr;
    QCheckBox* assignsCheck = nullptr;
    QCheckBox* conditionsCheck = nullptr;
    QCheckBox* stateLabelsCheck = nullptr;
    QCheckBox* sourceLinesCheck = nullptr;
    QWidget* previewCanvas = nullptr;
    QTreeWidget* previewTree = nullptr;
    QTimer* refreshTimer = nullptr;
    QString currentFileName;
    QString pendingFileName;
    QString pendingDocumentText;
    QString pendingScopeLabel;
    int pendingScopeStartPosition = -1;
    int pendingScopeEndPosition = -1;
    bool pendingDirty = false;
    bool pendingRefresh = false;
    QString currentSummaryText;
    WavePreviewReport currentReport;
    bool currentDirty = false;
    QString laneFilterText;

    std::function<void(const QString&, int, int)> navigationHandler;

    void queueRefresh(const QString& fileName,
                      const QString& documentText,
                      bool dirty,
                      int scopeStartPosition,
                      int scopeEndPosition,
                      const QString& scopeLabel);
    void flushQueuedRefresh();
    void clearQueuedRefresh();
    void renderDocumentNow(const QString& fileName,
                           const QString& documentText,
                           bool dirty,
                           int scopeStartPosition,
                           int scopeEndPosition,
                           const QString& scopeLabel);
    void renderReport(const WavePreviewReport& report,
                      const QString& fileName,
                      bool dirty);
    void navigateItem(QTreeWidgetItem* item) const;
};

#endif // WAVEPREVIEWPANELCOORDINATOR_H
