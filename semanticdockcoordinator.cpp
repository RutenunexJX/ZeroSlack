#include "semanticdockcoordinator.h"

#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QMainWindow>

#include <utility>

SemanticDockCoordinator::SemanticDockCoordinator(
    QMainWindow* mainWindow,
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    NavigationManager* navigationManager,
    NavigationCommandCoordinator* navigationCommandCoordinator)
    : mainWindow(mainWindow)
    , tabManager(tabManager)
    , workspaceManager(workspaceManager)
    , navigationManager(navigationManager)
    , navigationCommandCoordinator(navigationCommandCoordinator)
{
}

SemanticDockCoordinator::~SemanticDockCoordinator() = default;

void SemanticDockCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
    if (semanticPanelRefresh)
        semanticPanelRefresh->setStatusMessageHandler(statusMessageHandler);
}

void SemanticDockCoordinator::setup()
{
    if (configured || !mainWindow)
        return;

    problemsPanel = std::make_unique<ProblemsPanelCoordinator>(mainWindow);
    referencesPanel = std::make_unique<ReferencesPanelCoordinator>(mainWindow);
    relationshipsPanel = std::make_unique<RelationshipsPanelCoordinator>(mainWindow);

    mainWindow->addDockWidget(Qt::BottomDockWidgetArea, problemsPanel->dock());
    mainWindow->addDockWidget(Qt::BottomDockWidgetArea, referencesPanel->dock());
    mainWindow->addDockWidget(Qt::BottomDockWidgetArea, relationshipsPanel->dock());

    semanticPanelRefresh = std::make_unique<SemanticPanelRefreshCoordinator>(
        tabManager,
        workspaceManager,
        navigationManager,
        navigationCommandCoordinator,
        problemsPanel.get(),
        referencesPanel.get(),
        relationshipsPanel.get());
    semanticPanelRefresh->setStatusMessageHandler(statusMessageHandler);
    semanticPanelRefresh->configurePanels();

    configured = true;
}

SemanticPanelRefreshCoordinator* SemanticDockCoordinator::refreshCoordinator() const
{
    return semanticPanelRefresh.get();
}

ProblemsPanelCoordinator* SemanticDockCoordinator::problemsPanelCoordinator() const
{
    return problemsPanel.get();
}

ReferencesPanelCoordinator* SemanticDockCoordinator::referencesPanelCoordinator() const
{
    return referencesPanel.get();
}

RelationshipsPanelCoordinator* SemanticDockCoordinator::relationshipsPanelCoordinator() const
{
    return relationshipsPanel.get();
}
