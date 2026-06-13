#ifndef SEMANTICDOCKCOORDINATOR_H
#define SEMANTICDOCKCOORDINATOR_H

#include <QString>

#include <functional>
#include <memory>

class NavigationCommandCoordinator;
class NavigationManager;
class ProblemsPanelCoordinator;
class ReferencesPanelCoordinator;
class RelationshipsPanelCoordinator;
class RtlInsightsPanelCoordinator;
class SemanticPanelRefreshCoordinator;
class TabManager;
class WorkspaceManager;
class QMainWindow;
class QDockWidget;

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
    ProblemsPanelCoordinator* problemsPanelCoordinator() const;
    ReferencesPanelCoordinator* referencesPanelCoordinator() const;
    RelationshipsPanelCoordinator* relationshipsPanelCoordinator() const;
    RtlInsightsPanelCoordinator* rtlInsightsPanelCoordinator() const;

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
        std::unique_ptr<ReferencesPanelCoordinator> referencesPanel;
        std::unique_ptr<RelationshipsPanelCoordinator> relationshipsPanel;
        std::unique_ptr<RtlInsightsPanelCoordinator> rtlInsightsPanel;
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
