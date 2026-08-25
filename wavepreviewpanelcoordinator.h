#ifndef WAVEPREVIEWPANELCOORDINATOR_H
#define WAVEPREVIEWPANELCOORDINATOR_H

#include "documentchange.h"
#include "graphexportservice.h"
#include "wavepreviewservice.h"

#include <QDockWidget>
#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

class QLabel;
class QAction;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class WaveformPreviewLoader;

struct WavePreviewRefreshMetrics {
    int renderCount = 0;
    int documentChangeRenderCount = 0;
    int scopeDeltaUpdateCount = 0;
    int scopeRebuildCount = 0;
    qsizetype lastParsedCharacterCount = 0;
    std::uint64_t scopeCacheUpdateNanoseconds = 0;
    std::uint64_t waveServiceNanoseconds = 0;
    std::uint64_t modelSceneRebuildNanoseconds = 0;
    std::uint64_t canvasUpdateNanoseconds = 0;
    std::uint64_t canvasPaintNanoseconds = 0;
    int canvasPaintCount = 0;
};

class WavePreviewPanelCoordinator
{
public:
    explicit WavePreviewPanelCoordinator(QWidget* parent);
    ~WavePreviewPanelCoordinator();

    void setNavigationHandler(std::function<void(const QString&, int, int)> handler);
    void setStatusMessageHandler(
        std::function<void(const QString&, int)> handler);
    void setWaveformLibraryPath(const QString& libraryPath);
    void setWorkspaceRoot(const QString& rootPath);
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
    void applyDocumentChange(
        const QString& fileName,
        const DocumentChange& change,
        int latestDocumentLength,
        const std::function<QString(int, int)>& latestDocumentSlice,
        bool dirty,
        int scopeStartPosition,
        int scopeEndPosition,
        const QString& scopeLabel,
        int scopeStartLineZeroBased);
    void renderUnavailable(const QString& message);
    void focusFit();
    void focusZoomIn();
    void focusZoomOut();
    void setFocusSearchText(const QString& text);
    QString focusSearchText() const;
    void focusInspector();
    GraphExportResult exportPreview(
        const QString& outputPath,
        const GraphExportOptions& options = {}) const;
    QAction* previewExportAction() const { return exportAction; }
    qreal focusZoomFactorForTest() const {
        return focusZoomFactor;
    }

    QDockWidget* dock() const { return previewDock; }
    QTreeWidget* tree() const { return previewTree; }
    QWidget* canvas() const { return previewCanvas; }
    QWidget* waveformViewForTest() const { return waveformView.data(); }
    const WavePreviewReport& reportForTest() const { return currentReport; }
    WavePreviewRefreshMetrics refreshMetricsForTest() const;
    void resetRefreshMetricsForTest();

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
    QAction* exportAction = nullptr;
    QWidget* previewCanvas = nullptr;
    QPointer<QWidget> waveformView;
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
    bool refreshTimingEnabled = false;
    qreal focusZoomFactor = 1.0;
    qreal focusBaseTreePointSize = 0.0;
    std::unique_ptr<WaveformPreviewLoader> waveformPreviewLoader;
    QString waveformLibraryPath;
    QString workspaceRoot;
    QString waveformFailure;
    QMetaObject::Connection themeConnection;

    std::function<void(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

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
    void installWaveformView(const QString& libraryPath);
    void setWaveformPresentationState(const QString& state,
                                      const QString& message = QString());
    QString resolveNavigationFile(const QString& sourceFile) const;
    void navigateItem(QTreeWidgetItem* item) const;
    void applyFocusZoom();
};

#endif // WAVEPREVIEWPANELCOORDINATOR_H
