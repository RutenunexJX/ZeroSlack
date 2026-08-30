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
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

class ZEROSLACK_API RtlInsightWorkbench final : public QWidget
{
public:
    using BuildOverride = std::function<InsightViewBuildResult(
        const InsightViewContext&)>;

    explicit RtlInsightWorkbench(QWidget* parent = nullptr);
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

private:
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
    InsightGraphCore core;
    InsightCanvas* canvasValue = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QLineEdit* searchEdit = nullptr;
    QToolButton* panButton = nullptr;
    QToolButton* minimapButton = nullptr;
    QPushButton* detachButton = nullptr;
    InsightWorkbenchViewKind currentKind =
        InsightWorkbenchViewKind::Kernel;
    InsightViewContext currentContext;
    InsightGraphUpdate currentUpdate;
    std::function<bool(const QString&, int, int)> navigationHandler;

    IInsightViewPlugin* pluginForKind(
        InsightWorkbenchViewKind kind) const;
    void buildUi();
    void saveCurrentViewState();
    void restoreCurrentViewState();
    void updatePresentation(const InsightViewBuildResult& result);
};

#endif // RTLINSIGHTWORKBENCH_H
