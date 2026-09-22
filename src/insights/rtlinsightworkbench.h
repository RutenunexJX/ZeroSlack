#ifndef RTLINSIGHTWORKBENCH_H
#define RTLINSIGHTWORKBENCH_H

#include "graphexportservice.h"
#include "insightgraphcore.h"
#include "rtlinsightviewplugins.h"
#include "zeroslackexport.h"

#include <QHash>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <map>
#include <memory>

class InsightCanvas;
class InsightViewSurface;
class RtlInsightsPanelCoordinator;
class SignalKernelGraphPanelCoordinator;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

class ZEROSLACK_API RtlInsightWorkbench final : public QWidget
{
public:
    using BuildOverride = std::function<InsightViewBuildResult(
        const InsightViewContext&)>;

    explicit RtlInsightWorkbench(QWidget* parent = nullptr, bool specialized = false);
    ~RtlInsightWorkbench() override;

    bool registerPlugin(std::unique_ptr<IInsightViewPlugin> plugin);
    QStringList pluginIds() const;
    int pluginCount() const;
    bool hasPlugin(InsightWorkbenchViewKind kind) const;

    bool setViewKind(InsightWorkbenchViewKind kind);
    InsightWorkbenchViewKind viewKind() const;
    QString currentPluginId() const;
    void setContext(const InsightViewContext& context);
    InsightViewContext context() const;
    bool refresh();
    void setNavigationHandler(
        std::function<bool(const QString&, int, int)> handler);

    void setStatusHandler(std::function<void(const QString&, int)> handler);
    RtlInsightsPanelCoordinator* rtlSurfaceForTest() const;
    SignalKernelGraphPanelCoordinator* kernelSurfaceForTest() const;
    InsightGraphCore* graphCore();
    InsightCanvas* canvas() const;
    InsightGraphUpdate lastUpdate() const;
    QString statusText() const;

    GraphExportResult exportCurrentGraph(
        const QString& outputPath,
        const GraphExportOptions& options = {}) const;

    void setBuildOverrideForTest(
        InsightWorkbenchViewKind kind,
        BuildOverride builder);
    QPushButton* detachButtonForTest() const;
    // Hides the chrome a host already provides around the workbench (its own
    // title line and detach entry), for embedding in a titled container.
    void setCompactChrome(bool compact);
    void fitGraph();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void fitNewSurface();
    bool surfaceNeedsFit = true;
    struct ViewState {
        QString searchText;
        QStringList selectedNodeIds;
        qreal zoom = 1.0;
        bool minimapVisible = true;
        bool panMode = false;
    };

    std::map<int, std::unique_ptr<IInsightViewPlugin>> plugins;
    QHash<int, BuildOverride> buildOverrides;
    QHash<int, ViewState> viewStates;
    bool specializedViews = false;
    std::unique_ptr<InsightViewSurface> surface;
    InsightGraphCore core;
    InsightCanvas* canvasValue = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QLineEdit* searchEdit = nullptr;
    QToolButton* panButton = nullptr;
    QToolButton* minimapButton = nullptr;
    QPushButton* detachButton = nullptr;
    bool compactChrome = false;
    InsightWorkbenchViewKind currentKind =
        InsightWorkbenchViewKind::Kernel;
    InsightViewContext currentContext;
    InsightGraphUpdate currentUpdate;
    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusHandler;

    IInsightViewPlugin* pluginForKind(
        InsightWorkbenchViewKind kind) const;
    void buildUi();
    void saveCurrentViewState();
    void restoreCurrentViewState();
    void updatePresentation(const InsightViewBuildResult& result);
};

#endif // RTLINSIGHTWORKBENCH_H
