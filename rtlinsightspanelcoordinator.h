#ifndef RTLINSIGHTSPANELCOORDINATOR_H
#define RTLINSIGHTSPANELCOORDINATOR_H

#include <QDockWidget>
#include <QString>
#include <QTreeWidget>

#include <functional>
#include <memory>

class SemanticIndexSnapshot;
class QPushButton;

class RtlInsightsPanelCoordinator
{
public:
    explicit RtlInsightsPanelCoordinator(QWidget* parent);

    void setNavigationHandler(std::function<void(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);

    void updateModuleContext(const QString& fileName,
                             const QString& moduleName,
                             const QString& signalName = QString());
    void showModuleInsights(const QString& fileName,
                            const QString& moduleName,
                            const QString& signalName = QString());
    void showStateTransitionGraphForSignal(const QString& fileName,
                                           const QString& moduleName,
                                           const QString& signalName);
    void showModuleBlockDiagramForModule(const QString& fileName,
                                         const QString& moduleName);
    void showSemanticDiff(std::shared_ptr<const SemanticIndexSnapshot> beforeSnapshot,
                          std::shared_ptr<const SemanticIndexSnapshot> afterSnapshot,
                          const QString& moduleName = QString(),
                          const QString& beforeFileName = QString(),
                          const QString& afterFileName = QString());
    void refresh();

    QDockWidget* dock() const { return insightsDock; }
    QTreeWidget* tree() const { return insightsTree; }

private:
    QDockWidget* insightsDock = nullptr;
    QTreeWidget* insightsTree = nullptr;
    QPushButton* moduleBriefButton = nullptr;
    QPushButton* signalJourneyButton = nullptr;
    QPushButton* clockResetButton = nullptr;
    QPushButton* fsmGraphButton = nullptr;
    QPushButton* moduleBlockDiagramButton = nullptr;
    QString currentFileName;
    QString currentModuleName;
    QString currentSignalName;

    std::function<void(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;

    void renderActionList();
    void renderNoContext();
    void showModuleBrief();
    void showSignalJourney();
    void showClockResetDomainMap();
    void showFsmGraph();
    void showModuleBlockDiagram();
    void updateActionState();
    void logReportStart(const QString& reportName) const;
    void logReportDone(const QString& reportName, int durationMs) const;
    void logReportError(const QString& reportName, const QString& message) const;
};

#endif // RTLINSIGHTSPANELCOORDINATOR_H
