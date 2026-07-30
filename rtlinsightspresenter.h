#ifndef RTLINSIGHTSPRESENTER_H
#define RTLINSIGHTSPRESENTER_H

#include <QString>

#include <memory>

class SemanticIndexSnapshot;
struct RtlInsightsPanelViewState;
class RtlInsightsGraphController;

class RtlInsightsPresenter
{
public:
    RtlInsightsPresenter(
        RtlInsightsPanelViewState& state,
        RtlInsightsGraphController& graphController);

    void updateModuleContext(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName = QString());
    void showModuleInsights(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName = QString());
    void showStateTransitionGraphForSignal(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName);
    void showSignalUsageHotspotForSignal(
        const QString& fileName,
        const QString& moduleName,
        const QString& signalName,
        const QString& signalAccessPath = {});
    void showModuleBlockDiagramForModule(
        const QString& fileName,
        const QString& moduleName);
    void showSemanticDiff(
        std::shared_ptr<const SemanticIndexSnapshot>
            beforeSnapshot,
        std::shared_ptr<const SemanticIndexSnapshot>
            afterSnapshot,
        const QString& moduleName = QString(),
        const QString& beforeFileName = QString(),
        const QString& afterFileName = QString());

    void refresh();
    void renderNoContext();
    void renderActionList();
    void showModuleBrief();
    void showSignalJourney();
    void showSignalUsageHotspot();
    void showClockResetDomainMap();
    void showFsmGraph();
    void showModuleBlockDiagram();
    void updateActionState();

private:
    RtlInsightsPanelViewState& state;
    RtlInsightsGraphController& graphController;

    void logReportStart(const QString& reportName) const;
    void logReportDone(const QString& reportName,
                       int durationMs) const;
    void logReportError(const QString& reportName,
                        const QString& message) const;
};

#endif // RTLINSIGHTSPRESENTER_H
