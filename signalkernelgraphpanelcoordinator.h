#ifndef SIGNALKERNELGRAPHPANELCOORDINATOR_H
#define SIGNALKERNELGRAPHPANELCOORDINATOR_H

#include "signalkernelgraphservice.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class DocumentModel;
class EditorHoverPopup;
class QLabel;
class QGraphicsScene;
class QGraphicsView;

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
                                        const QString& moduleName);
    void showSignalKernelGraphForStableKey(const SymbolStableKey& stableKey);
    void refresh();

    QDockWidget* dock() const { return graphDock; }
    QGraphicsView* view() const { return graphView; }

private:
    QDockWidget* graphDock = nullptr;
    QLabel* titleLabel = nullptr;
    QGraphicsView* graphView = nullptr;
    QGraphicsScene* graphScene = nullptr;
    EditorHoverPopup* hoverPopup = nullptr;
    SignalKernelGraphQuery currentQuery;

    std::function<void(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void renderReport(const SignalKernelGraphReport& report);
    void renderUnavailable(const QString& message);
    void showNodePreview(const SignalKernelGraphNode& node,
                         const QPoint& globalPosition);
    void navigateNode(const SignalKernelGraphNode& node) const;
    void rebaseToNode(const SignalKernelGraphNode& node);
    void showDock();
    void showStatusMessage(const QString& message, int timeoutMs) const;
};

#endif // SIGNALKERNELGRAPHPANELCOORDINATOR_H
