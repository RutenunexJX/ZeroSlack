#ifndef WAVEPREVIEWPANELCOORDINATOR_H
#define WAVEPREVIEWPANELCOORDINATOR_H

#include "documentchange.h"
#include "wavepreviewservice.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

struct WavePreviewRefreshMetrics {
    int renderCount = 0;
    int documentChangeRenderCount = 0;
    int scopeDeltaUpdateCount = 0;
    int scopeRebuildCount = 0;
    qsizetype lastParsedCharacterCount = 0;
};

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
                             const QString& scopeLabel = QString(),
                             int scopeStartLineZeroBased = 0);
    void applyDocumentChange(const QString& fileName,
                             const DocumentChange& change,
                             const QString& latestDocumentText,
                             bool dirty,
                             int scopeStartPosition,
                             int scopeEndPosition,
                             const QString& scopeLabel,
                             int scopeStartLineZeroBased);
    void renderUnavailable(const QString& message);

    QDockWidget* dock() const { return previewDock; }
    QTreeWidget* tree() const { return previewTree; }
    QWidget* canvas() const { return previewCanvas; }
    const WavePreviewReport& reportForTest() const { return currentReport; }
    WavePreviewRefreshMetrics refreshMetricsForTest() const
    {
        return refreshMetrics;
    }
    void resetRefreshMetricsForTest() { refreshMetrics = {}; }

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
    QString currentFileName;
    QString currentScopeText;
    QString currentScopeLabel;
    int currentScopeStartPosition = -1;
    int currentScopeEndPosition = -1;
    int currentScopeStartLineZeroBased = 0;
    bool currentSourceIsScoped = false;
    QString currentSummaryText;
    WavePreviewReport currentReport;
    bool currentDirty = false;
    QString laneFilterText;
    WavePreviewRefreshMetrics refreshMetrics;

    std::function<void(const QString&, int, int)> navigationHandler;

    void renderDocumentNow(const QString& fileName,
                           const QString& documentText,
                           bool dirty,
                           int scopeStartPosition,
                           int scopeEndPosition,
                           const QString& scopeLabel,
                           int sourcePositionOffset = 0,
                           int sourceLineOffset = 0);
    void renderReport(const WavePreviewReport& report,
                      const QString& fileName,
                      bool dirty);
    void navigateItem(QTreeWidgetItem* item) const;
};

#endif // WAVEPREVIEWPANELCOORDINATOR_H
