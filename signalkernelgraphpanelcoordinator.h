#ifndef SIGNALKERNELGRAPHPANELCOORDINATOR_H
#define SIGNALKERNELGRAPHPANELCOORDINATOR_H

#include "signalkernelgraphservice.h"

#include <QDockWidget>
#include <QRectF>
#include <QSet>
#include <QString>

#include <functional>

class DocumentModel;
class EditorHoverPopup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QGraphicsScene;
class QGraphicsView;
class QTimer;
class QRect;

class SignalKernelGraphPanelCoordinator
{
public:
    explicit SignalKernelGraphPanelCoordinator(QWidget* parent);
    ~SignalKernelGraphPanelCoordinator();

    void setNavigationHandler(std::function<bool(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setDocumentModel(DocumentModel* documentModel);

    void showSignalKernelGraphForSymbol(const QString& symbolName,
                                        const QString& fileName,
                                        const QString& moduleName,
                                        const QString& signalAccessPath = {});
    void refresh();

    QDockWidget* dock() const { return graphDock; }
    QGraphicsView* view() const { return graphView; }
    void renderReportForTest(const SignalKernelGraphReport& report);
    int collapsedFanoutGroupCountForTest() const;
    int visibleGraphNodeCountForTest() const;
    int renderedFanoutGroupItemCountForTest() const;
    QRectF lastRenderedFanoutGroupRectForTest() const;
    bool toggleFanoutGroupForTest(const QString& groupKey);
    void setGraphSearchTextForTest(const QString& text);
    void setGraphFilterForTest(bool showInputs,
                               bool showOutputs,
                               bool crossModuleOnly);
    int searchMatchCountForTest() const;
    int focusedSearchNodeIdForTest() const;

private:
    QDockWidget* graphDock = nullptr;
    QLabel* titleLabel = nullptr;
    QLineEdit* graphSearchEdit = nullptr;
    QCheckBox* showInputsCheck = nullptr;
    QCheckBox* showOutputsCheck = nullptr;
    QCheckBox* crossModuleOnlyCheck = nullptr;
    QGraphicsView* graphView = nullptr;
    QGraphicsScene* graphScene = nullptr;
    EditorHoverPopup* hoverPopup = nullptr;
    QTimer* hoverCloseTimer = nullptr;
    QString currentHoverNodeKey;
    SignalKernelGraphQuery currentQuery;
    SignalKernelGraphReport currentReport;
    QSet<QString> knownFanoutGroupKeys;
    QSet<QString> collapsedFanoutGroupKeys;
    QString graphSearchText;
    bool graphShowInputs = true;
    bool graphShowOutputs = true;
    bool graphCrossModuleOnly = false;
    int lastVisibleGraphNodeCount = 0;
    int lastRenderedFanoutGroupItemCount = 0;
    QRectF lastRenderedFanoutGroupRect;
    int lastSearchMatchCount = 0;
    int lastFocusedSearchNodeId = -1;

    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void renderReport(const SignalKernelGraphReport& report);
    void renderUnavailable(const QString& message);
    bool nodePassesGraphFilter(const SignalKernelGraphNode& node) const;
    bool nodeMatchesGraphSearch(const SignalKernelGraphNode& node) const;
    QString fanoutGroupUiKey(const SignalKernelGraphFanoutGroup& group) const;
    void initializeFanoutCollapseState(const SignalKernelGraphReport& report);
    bool isFanoutGroupCollapsed(
        const SignalKernelGraphFanoutGroup& group) const;
    void toggleFanoutGroup(const QString& groupKey);
    void showNodePreview(const SignalKernelGraphNode& node,
                         const QRectF& nodeSceneRect);
    void closeNodePreviewNow();
    void placeHoverPopupAvoidingNode(const QRect& nodeGlobalRect) const;
    void navigateNode(const SignalKernelGraphNode& node) const;
    void rebaseToNode(const SignalKernelGraphNode& node);
    void showDock();
    void showStatusMessage(const QString& message, int timeoutMs) const;
};

#endif // SIGNALKERNELGRAPHPANELCOORDINATOR_H
