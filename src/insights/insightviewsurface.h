#pragma once
#include "rtlinsightviewplugins.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include <memory>
#include <QPushButton>
#include <QVBoxLayout>
#include "signalusagehotspotpanel.h"
#include "compactlayout.h"

// Owns a typed renderer. Reports and view-specific interaction stay inside the panel.
class InsightViewSurface final {
public:
    InsightViewSurface(InsightWorkbenchViewKind kind, QWidget* parent) : kindValue(kind) {
        if (kind == InsightWorkbenchViewKind::Kernel)
            kernelValue = std::make_unique<SignalKernelGraphPanelCoordinator>(parent);
        else rtlValue = std::make_unique<RtlInsightsPanelCoordinator>(parent);
        auto* dock = widget();
        dock->setObjectName(QStringLiteral("insightWorkbenchSurface.%1").arg(static_cast<int>(kind)));
        if (rtlValue) {
            for (const char* name : {"rtlModuleBriefButton", "rtlSignalJourneyButton", "rtlSignalUsageHotspotButton",
                    "rtlClockResetButton", "rtlFsmGraphButton", "rtlModuleBlockDiagramButton", "rtlInsightsTitle", "rtlInsightsPinButton"}) {
                if (auto* control = dock->findChild<QWidget*>(QString::fromLatin1(name))) control->hide();
            }
        }
        if (rtlValue) {
            if (auto* graph = dock->findChild<QWidget*>(QStringLiteral("rtlInsightsGraphPanel")))
                CompactFlowLayout::replaceRows(qobject_cast<QVBoxLayout*>(graph->layout()));
            CompactFlowLayout::replaceRows(qobject_cast<QVBoxLayout*>(rtlValue->signalUsageHotspotPanelForTest()->layout()));
        }
        dock->setMinimumWidth(0);
        dock->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        if (rtlValue) rtlValue->stackForTest()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        auto* title = new QWidget(dock);
        title->setFixedHeight(0);
        dock->setTitleBarWidget(title);
        dock->show();
    }
    ~InsightViewSurface() { auto* dock = widget(); kernelValue.reset(); rtlValue.reset(); delete dock; }
    QDockWidget* widget() const { return kernelValue ? kernelValue->dock() : rtlValue->dock(); }
    RtlInsightsPanelCoordinator* rtl() const { return rtlValue.get(); }
    SignalKernelGraphPanelCoordinator* kernel() const { return kernelValue.get(); }
    void fit() { if (kernelValue) kernelValue->focusFit(); else rtlValue->focusFit(); }
    void setNavigationHandler(std::function<bool(const QString&, int, int)> handler) {
        if (kernelValue) kernelValue->setNavigationHandler(handler);
        else rtlValue->setNavigationHandler(handler);
    }
    void setStatusHandler(std::function<void(const QString&, int)> handler) {
        if (kernelValue) kernelValue->setStatusMessageHandler(handler);
        else rtlValue->setStatusMessageHandler(handler);
    }
    void setContext(const InsightViewContext& c) {
        if (kernelValue) {
            kernelValue->showSignalKernelGraphForSymbol(c.signalName,c.fileName,c.moduleName,c.signalAccessPath);
            return;
        }
        rtlValue->updateModuleContext(c.fileName,c.moduleName,c.signalName);
        switch (kindValue) {
        case InsightWorkbenchViewKind::Block:
            rtlValue->showModuleBlockDiagramForModule(c.fileName,c.moduleName); break;
        case InsightWorkbenchViewKind::Hotspot:
            rtlValue->showSignalUsageHotspotForSignal(c.fileName,c.moduleName,c.signalName,c.signalAccessPath); break;
        case InsightWorkbenchViewKind::StateTransition:
            if (c.signalName.isEmpty()) rtlValue->showFsmGraph();
            else rtlValue->showStateTransitionGraphForSignal(c.fileName,c.moduleName,c.signalName);
            break;
        default: break;
        }
    }
    GraphExportResult exportGraph(const QString& path, const GraphExportOptions& options) const {
        return kernelValue ? kernelValue->exportGraph(path, options) : rtlValue->exportCurrentGraph(path, options);
    }
private:
    InsightWorkbenchViewKind kindValue;
    std::unique_ptr<RtlInsightsPanelCoordinator> rtlValue;
    std::unique_ptr<SignalKernelGraphPanelCoordinator> kernelValue;
};
