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
class SemanticPanelRefreshCoordinator;
class TabManager;
class WorkspaceManager;
class QMainWindow;

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

private:
    QMainWindow* mainWindow = nullptr;
    TabManager* tabManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
    NavigationManager* navigationManager = nullptr;
    NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;
    bool configured = false;

    std::function<void(const QString&, int)> statusMessageHandler;
    std::unique_ptr<ProblemsPanelCoordinator> problemsPanel;
    std::unique_ptr<ReferencesPanelCoordinator> referencesPanel;
    std::unique_ptr<RelationshipsPanelCoordinator> relationshipsPanel;
    std::unique_ptr<SemanticPanelRefreshCoordinator> semanticPanelRefresh;
};

#endif // SEMANTICDOCKCOORDINATOR_H
