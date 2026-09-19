#ifndef SEMANTICDOCKCOORDINATOR_H
#define SEMANTICDOCKCOORDINATOR_H

#include <QString>
#include <QPointer>

#include <functional>
#include <memory>

class NavigationCommandCoordinator;
class ActivityLogPanelCoordinator;
class InstancePairConnectionCoordinator;
class InstancePairConnectionFacade;
class InstancePairConnectionWorkflow;
class MultiSignalPropagationPanel;
class MultiSignalPropagationPlanner;
class MultiSignalPropagationWorkflow;
class NavigationManager;
class ProblemsPanelCoordinator;
class RtlHighRiskEditPanelCoordinator;
class RtlInsightsPanelCoordinator;
class ScopedSearchPanelCoordinator;
class SemanticPanelRefreshCoordinator;
class SignalKernelGraphPanelCoordinator;
class TabManager;
class WorkspaceManager;
class WorkspaceEditDocumentManager;
class QMainWindow;
class QDockWidget;
class QTabWidget;
class DeferredPanel;

class SemanticDockCoordinator
{
public:
    SemanticDockCoordinator(QMainWindow* mainWindow,
                            TabManager* tabManager,
                            WorkspaceManager* workspaceManager,
                            NavigationManager* navigationManager,
                            NavigationCommandCoordinator* navigationCommandCoordinator);
    ~SemanticDockCoordinator();

    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setup();

    SemanticPanelRefreshCoordinator* refreshCoordinator() const;
    ActivityLogPanelCoordinator* activityLogPanelCoordinator() const;
    ProblemsPanelCoordinator* problemsPanelCoordinator() const;
    ScopedSearchPanelCoordinator* scopedSearchPanelCoordinator() const;
    InstancePairConnectionCoordinator*
    instancePairConnectionCoordinator() const;
    InstancePairConnectionWorkflow*
    instancePairConnectionWorkflow() const;
    MultiSignalPropagationPanel*
    multiSignalPropagationPanel() const;
    MultiSignalPropagationWorkflow*
    multiSignalPropagationWorkflow() const;
    RtlHighRiskEditPanelCoordinator*
    rtlHighRiskEditPanelCoordinator() const;
    WorkspaceEditDocumentManager*
    rtlActionDocumentManager() const;
    QDockWidget* instancePairConnectionDock() const;
    QDockWidget* multiSignalPropagationDock() const;
    QDockWidget* connectionsDock() const;
    QTabWidget* connectionsTabs() const;
    bool showConnectionPage(const QString& panelId);
    RtlInsightsPanelCoordinator* rtlInsightsPanelCoordinator() const;
    SignalKernelGraphPanelCoordinator* signalKernelGraphPanelCoordinator() const;

private:
    struct DockDependencies {
        QMainWindow* mainWindow = nullptr;
        TabManager* tabManager = nullptr;
        WorkspaceManager* workspaceManager = nullptr;
        NavigationManager* navigationManager = nullptr;
        NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;

        void set(QMainWindow* mainWindow,
                 TabManager* tabManager,
                 WorkspaceManager* workspaceManager,
                 NavigationManager* navigationManager,
                 NavigationCommandCoordinator* navigationCommandCoordinator);
        bool hasMainWindow() const;
        void addBottomDock(QDockWidget* dock) const;
    };

    struct PanelBundle {
        ~PanelBundle();

        std::unique_ptr<ProblemsPanelCoordinator> problemsPanel;
        std::unique_ptr<ActivityLogPanelCoordinator> activityLogPanel;
        std::unique_ptr<ScopedSearchPanelCoordinator> scopedSearchPanel;
        std::unique_ptr<WorkspaceEditDocumentManager>
            rtlActionDocuments;
        std::unique_ptr<RtlHighRiskEditPanelCoordinator>
            rtlHighRiskEditPanel;
        std::unique_ptr<InstancePairConnectionFacade>
            instancePairFacade;
        std::unique_ptr<InstancePairConnectionCoordinator>
            instancePairCoordinator;
        std::unique_ptr<InstancePairConnectionWorkflow>
            instancePairWorkflow;
        std::unique_ptr<MultiSignalPropagationPlanner>
            multiSignalPlanner;
        std::unique_ptr<MultiSignalPropagationWorkflow>
            multiSignalWorkflow;
        QDockWidget* connectionsDock = nullptr;
        QPointer<DeferredPanel> connectionsContent;
        QTabWidget* connectionsTabs = nullptr;
        MultiSignalPropagationPanel* multiSignalPanel = nullptr;
        bool rtlActionStatusConnected = false;
        std::function<void(const QString&, int)> connectionStatusHandler;
        std::unique_ptr<SemanticPanelRefreshCoordinator> semanticPanelRefresh;

        void createPanels(const DockDependencies& dependencies);
        void createRefreshCoordinator(
            const DockDependencies& dependencies,
            const std::function<void(const QString&, int)>& statusMessageHandler);
        void setStatusMessageHandler(
            const std::function<void(const QString&, int)>& statusMessageHandler);
    };

    DockDependencies dependencies;
    PanelBundle panels;
    bool configured = false;

    std::function<void(const QString&, int)> statusMessageHandler;
};

#endif // SEMANTICDOCKCOORDINATOR_H
