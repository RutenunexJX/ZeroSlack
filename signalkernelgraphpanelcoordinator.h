#ifndef SIGNALKERNELGRAPHPANELCOORDINATOR_H
#define SIGNALKERNELGRAPHPANELCOORDINATOR_H

#include "signalkernelgraphservice.h"

#include <QDockWidget>
#include <QSet>
#include <QString>

#include <functional>

class DocumentModel;
class EditorHoverPopup;
class QLabel;
class QGraphicsScene;
class QGraphicsView;
class QTimer;
class QRect;
class QRectF;

class SignalKernelGraphPanelCoordinator
{
public:
    explicit SignalKernelGraphPanelCoordinator(QWidget* parent);
    ~SignalKernelGraphPanelCoordinator();

    void setNavigationHandler(std::function<void(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setDocumentModel(DocumentModel* documentModel);

    void showSignalKernelGraphForSymbol(const QString& symbolName,
                                        const QString& fileName,
                                        const QString& moduleName,
                                        const QString& signalAccessPath = {});
    void showSignalKernelGraphForStableKey(const SymbolStableKey& stableKey);
    void refresh();

    QDockWidget* dock() const { return graphDock; }
    QGraphicsView* view() const { return graphView; }
    void renderReportForTest(const SignalKernelGraphReport& report);
    int collapsedFanoutGroupCountForTest() const;
    int visibleGraphNodeCountForTest() const;
    int renderedFanoutGroupItemCountForTest() const;
    bool toggleFanoutGroupForTest(const QString& groupKey);

private:
    QDockWidget* graphDock = nullptr;
    QLabel* titleLabel = nullptr;
    QGraphicsView* graphView = nullptr;
    QGraphicsScene* graphScene = nullptr;
    EditorHoverPopup* hoverPopup = nullptr;
    QTimer* hoverCloseTimer = nullptr;
    QString currentHoverNodeKey;
    SignalKernelGraphQuery currentQuery;
    SignalKernelGraphReport currentReport;
    QSet<QString> knownFanoutGroupKeys;
    QSet<QString> collapsedFanoutGroupKeys;
    int lastVisibleGraphNodeCount = 0;
    int lastRenderedFanoutGroupItemCount = 0;

    std::function<void(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void renderReport(const SignalKernelGraphReport& report);
    void renderUnavailable(const QString& message);
    QString fanoutGroupUiKey(const SignalKernelGraphFanoutGroup& group) const;
    void initializeFanoutCollapseState(const SignalKernelGraphReport& report);
    bool isFanoutGroupCollapsed(
        const SignalKernelGraphFanoutGroup& group) const;
    void toggleFanoutGroup(const QString& groupKey);
    void showNodePreview(const SignalKernelGraphNode& node,
                         const QRectF& nodeSceneRect);
    void closeNodePreviewDelayed();
    void closeNodePreviewNow();
    void placeHoverPopupAvoidingNode(const QRect& nodeGlobalRect) const;
    void navigateNode(const SignalKernelGraphNode& node) const;
    void rebaseToNode(const SignalKernelGraphNode& node);
    void showDock();
    void showStatusMessage(const QString& message, int timeoutMs) const;
};

#endif // SIGNALKERNELGRAPHPANELCOORDINATOR_H
