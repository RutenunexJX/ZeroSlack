#include "semanticdockcoordinator.h"

#include "activitylogpanelcoordinator.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
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
{
    dependencies.set(mainWindow,
                     tabManager,
                     workspaceManager,
                     navigationManager,
                     navigationCommandCoordinator);
}

SemanticDockCoordinator::~SemanticDockCoordinator() = default;

SemanticDockCoordinator::PanelBundle::~PanelBundle() = default;

void SemanticDockCoordinator::DockDependencies::set(
    QMainWindow* newMainWindow,
    TabManager* newTabManager,
    WorkspaceManager* newWorkspaceManager,
    NavigationManager* newNavigationManager,
    NavigationCommandCoordinator* newNavigationCommandCoordinator)
{
    mainWindow = newMainWindow;
    tabManager = newTabManager;
    workspaceManager = newWorkspaceManager;
    navigationManager = newNavigationManager;
    navigationCommandCoordinator = newNavigationCommandCoordinator;
}

bool SemanticDockCoordinator::DockDependencies::hasMainWindow() const
{
    return mainWindow != nullptr;
}

void SemanticDockCoordinator::DockDependencies::addBottomDock(
    QDockWidget* dock) const
{
    if (mainWindow && dock)
        mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
}

void SemanticDockCoordinator::DockDependencies::tabifyBottomDock(
    QDockWidget* first,
    QDockWidget* second) const
{
    if (mainWindow && first && second)
        mainWindow->tabifyDockWidget(first, second);
}

void SemanticDockCoordinator::PanelBundle::createPanels(
    const DockDependencies& dependencies)
{
    problemsPanel =
        std::make_unique<ProblemsPanelCoordinator>(dependencies.mainWindow);
    activityLogPanel =
        std::make_unique<ActivityLogPanelCoordinator>(dependencies.mainWindow);
    referencesPanel =
        std::make_unique<ReferencesPanelCoordinator>(dependencies.mainWindow);
    relationshipsPanel =
        std::make_unique<RelationshipsPanelCoordinator>(dependencies.mainWindow);
    rtlInsightsPanel =
        std::make_unique<RtlInsightsPanelCoordinator>(dependencies.mainWindow);
    signalKernelGraphPanel =
        std::make_unique<SignalKernelGraphPanelCoordinator>(dependencies.mainWindow);

    dependencies.addBottomDock(problemsPanel->dock());
    dependencies.addBottomDock(activityLogPanel->dock());
    dependencies.addBottomDock(referencesPanel->dock());
    dependencies.addBottomDock(relationshipsPanel->dock());
    dependencies.addBottomDock(rtlInsightsPanel->dock());
    dependencies.addBottomDock(signalKernelGraphPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(), activityLogPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(), referencesPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(), relationshipsPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(), rtlInsightsPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(),
                                  signalKernelGraphPanel->dock());
}

void SemanticDockCoordinator::PanelBundle::createRefreshCoordinator(
    const DockDependencies& dependencies,
    const std::function<void(const QString&, int)>& statusMessageHandler)
{
    semanticPanelRefresh = std::make_unique<SemanticPanelRefreshCoordinator>(
        dependencies.tabManager,
        dependencies.workspaceManager,
        dependencies.navigationManager,
        dependencies.navigationCommandCoordinator,
        problemsPanel.get(),
        referencesPanel.get(),
        relationshipsPanel.get(),
        rtlInsightsPanel.get(),
        signalKernelGraphPanel.get());
    setStatusMessageHandler(statusMessageHandler);
    semanticPanelRefresh->configurePanels();
}

void SemanticDockCoordinator::PanelBundle::setStatusMessageHandler(
    const std::function<void(const QString&, int)>& statusMessageHandler)
{
    if (semanticPanelRefresh)
        semanticPanelRefresh->setStatusMessageHandler(statusMessageHandler);
}

void SemanticDockCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
    panels.setStatusMessageHandler(statusMessageHandler);
}

void SemanticDockCoordinator::setup()
{
    if (configured || !dependencies.hasMainWindow())
        return;

    panels.createPanels(dependencies);
    panels.createRefreshCoordinator(dependencies, statusMessageHandler);

    configured = true;
}

SemanticPanelRefreshCoordinator* SemanticDockCoordinator::refreshCoordinator() const
{
    return panels.semanticPanelRefresh.get();
}

ActivityLogPanelCoordinator*
SemanticDockCoordinator::activityLogPanelCoordinator() const
{
    return panels.activityLogPanel.get();
}

ProblemsPanelCoordinator* SemanticDockCoordinator::problemsPanelCoordinator() const
{
    return panels.problemsPanel.get();
}

ReferencesPanelCoordinator* SemanticDockCoordinator::referencesPanelCoordinator() const
{
    return panels.referencesPanel.get();
}

RelationshipsPanelCoordinator* SemanticDockCoordinator::relationshipsPanelCoordinator() const
{
    return panels.relationshipsPanel.get();
}

RtlInsightsPanelCoordinator* SemanticDockCoordinator::rtlInsightsPanelCoordinator() const
{
    return panels.rtlInsightsPanel.get();
}

SignalKernelGraphPanelCoordinator*
SemanticDockCoordinator::signalKernelGraphPanelCoordinator() const
{
    return panels.signalKernelGraphPanel.get();
}
