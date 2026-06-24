#ifndef WAVEPREVIEWPANELCOORDINATOR_H
#define WAVEPREVIEWPANELCOORDINATOR_H

#include "wavepreviewservice.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class QLabel;
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
                             bool dirty);
    void renderUnavailable(const QString& message);

    QDockWidget* dock() const { return previewDock; }
    QTreeWidget* tree() const { return previewTree; }
    QWidget* canvas() const { return previewCanvas; }

private:
    QDockWidget* previewDock = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* summaryLabel = nullptr;
    QWidget* previewCanvas = nullptr;
    QTreeWidget* previewTree = nullptr;
    QTimer* refreshTimer = nullptr;
    QString currentFileName;
    QString pendingFileName;
    QString pendingDocumentText;
    bool pendingDirty = false;
    bool pendingRefresh = false;
    QString currentSummaryText;

    std::function<void(const QString&, int, int)> navigationHandler;

    void queueRefresh(const QString& fileName,
                      const QString& documentText,
                      bool dirty);
    void flushQueuedRefresh();
    void clearQueuedRefresh();
    void renderDocumentNow(const QString& fileName,
                           const QString& documentText,
                           bool dirty);
    void renderReport(const WavePreviewReport& report,
                      const QString& fileName,
                      bool dirty);
    void navigateItem(QTreeWidgetItem* item) const;
};

#endif // WAVEPREVIEWPANELCOORDINATOR_H
