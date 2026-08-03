#include "semanticdockcoordinator.h"

#include "activitylogpanelcoordinator.h"
#include "instancepairconnectionfacade.h"
#include "instancepairconnectionpanel.h"
#include "instancepairconnectionworkflow.h"
#include "multisignalpropagationpanel.h"
#include "multisignalpropagationplanner.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "problemspanelcoordinator.h"
#include "rtlhighriskeditpanel.h"
#include "rtlinsightspanelcoordinator.h"
#include "scopedsearchpanel.h"
#include "semanticpanelrefreshcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "tabmanager.h"
#include "wavepreviewpanelcoordinator.h"
#include "workspaceeditdocumentmanager.h"
#include "workspaceedittransactionservice.h"
#include "workspacemanager.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QStackedWidget>

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
    scopedSearchPanel =
        std::make_unique<ScopedSearchPanelCoordinator>(
            dependencies.mainWindow);
    rtlActionDocuments =
        std::make_unique<WorkspaceEditDocumentManager>(
            dependencies.tabManager);
    rtlHighRiskEditPanel =
        std::make_unique<
            RtlHighRiskEditPanelCoordinator>(
            dependencies.mainWindow,
            SemanticIndex::getInstance(),
            rtlActionDocuments.get(),
            WorkspaceEditTransactionService::getInstance(),
            dependencies.mainWindow);

    instancePairDock = new QDockWidget(
        QStringLiteral("Instance Pair Connection"),
        dependencies.mainWindow);
    instancePairDock->setObjectName(
        QStringLiteral("InstancePairConnectionDock"));
    instancePairDock->setAllowedAreas(
        Qt::BottomDockWidgetArea);
    auto* instancePairStack =
        new QStackedWidget(instancePairDock);
    instancePairStack->setObjectName(
        QStringLiteral("instancePairConnectionStack"));
    instancePairDock->setWidget(instancePairStack);
    instancePairFacade =
        std::make_unique<InstancePairConnectionFacade>();
    instancePairCoordinator =
        std::make_unique<
            InstancePairConnectionCoordinator>(
            instancePairStack);
    instancePairWorkflow =
        std::make_unique<
            InstancePairConnectionWorkflow>(
            instancePairFacade.get(),
            instancePairCoordinator.get(),
            SemanticIndex::getInstance(),
            rtlActionDocuments.get(),
            WorkspaceEditTransactionService::getInstance());

    multiSignalDock = new QDockWidget(
        QStringLiteral("Multi-Signal Propagation"),
        dependencies.mainWindow);
    multiSignalDock->setObjectName(
        QStringLiteral("MultiSignalPropagationDock"));
    multiSignalDock->setAllowedAreas(
        Qt::BottomDockWidgetArea);
    multiSignalPlanner =
        std::make_unique<MultiSignalPropagationPlanner>();
    multiSignalPanel =
        new MultiSignalPropagationPanel(
            *multiSignalPlanner,
            *rtlActionDocuments,
            multiSignalDock);
    multiSignalDock->setWidget(multiSignalPanel);
    multiSignalWorkflow =
        std::make_unique<MultiSignalPropagationWorkflow>(
            multiSignalPanel,
            SemanticIndex::getInstance(),
            rtlActionDocuments.get(),
            WorkspaceEditTransactionService::getInstance());

    rtlInsightsPanel =
        std::make_unique<RtlInsightsPanelCoordinator>(dependencies.mainWindow);
    signalKernelGraphPanel =
        std::make_unique<SignalKernelGraphPanelCoordinator>(dependencies.mainWindow);
    wavePreviewPanel =
        std::make_unique<WavePreviewPanelCoordinator>(dependencies.mainWindow);

    dependencies.addBottomDock(problemsPanel->dock());
    dependencies.addBottomDock(activityLogPanel->dock());
    dependencies.addBottomDock(scopedSearchPanel->dock());
    dependencies.addBottomDock(
        rtlHighRiskEditPanel->dock());
    dependencies.addBottomDock(instancePairDock);
    dependencies.addBottomDock(multiSignalDock);
    dependencies.addBottomDock(rtlInsightsPanel->dock());
    dependencies.addBottomDock(signalKernelGraphPanel->dock());
    dependencies.addBottomDock(wavePreviewPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(), activityLogPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(),
                                  scopedSearchPanel->dock());
    dependencies.tabifyBottomDock(
        problemsPanel->dock(),
        rtlHighRiskEditPanel->dock());
    dependencies.tabifyBottomDock(
        problemsPanel->dock(), instancePairDock);
    dependencies.tabifyBottomDock(
        problemsPanel->dock(), multiSignalDock);
    dependencies.tabifyBottomDock(problemsPanel->dock(), rtlInsightsPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(),
                                  signalKernelGraphPanel->dock());
    dependencies.tabifyBottomDock(problemsPanel->dock(),
                                  wavePreviewPanel->dock());
    if (dependencies.workspaceManager) {
        QObject::connect(
            dependencies.workspaceManager,
            &WorkspaceManager::workspaceClosed,
            rtlHighRiskEditPanel.get(),
            [coordinator =
                 rtlHighRiskEditPanel.get()]() {
                coordinator->resetForWorkspaceClose();
            });
    }
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
    if (wavePreviewPanel)
        wavePreviewPanel->setStatusMessageHandler(statusMessageHandler);
    if (!rtlActionStatusConnected) {
        bool connected = false;
        if (instancePairWorkflow) {
            QObject::connect(
                instancePairWorkflow.get(),
                &InstancePairConnectionWorkflow::stateChanged,
                instancePairWorkflow.get(),
                [handler = statusMessageHandler](
                    const InstancePairConnectionWorkflowResult&
                        result) {
                    if (!result.message.isEmpty()
                        && handler) {
                        handler(
                            result.message, 5000);
                    }
                });
            connected = true;
        }
        if (multiSignalWorkflow) {
            QObject::connect(
                multiSignalWorkflow.get(),
                &MultiSignalPropagationWorkflow::stateChanged,
                multiSignalWorkflow.get(),
                [handler = statusMessageHandler](
                    const MultiSignalPropagationWorkflowResult&
                        result) {
                    if (!result.message.isEmpty()
                        && handler) {
                        handler(
                            result.message, 5000);
                    }
                });
            connected = true;
        }
        if (rtlHighRiskEditPanel) {
            QObject::connect(
                rtlHighRiskEditPanel.get(),
                &RtlHighRiskEditPanelCoordinator::
                    stateChanged,
                rtlHighRiskEditPanel.get(),
                [handler = statusMessageHandler](
                    const RtlHighRiskEditPanelOutcome&
                        outcome) {
                    if (!outcome.message.isEmpty()
                        && handler) {
                        handler(
                            outcome.message, 5000);
                    }
                });
            connected = true;
        }
        rtlActionStatusConnected = connected;
    }
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

ScopedSearchPanelCoordinator*
SemanticDockCoordinator::scopedSearchPanelCoordinator() const
{
    return panels.scopedSearchPanel.get();
}

InstancePairConnectionCoordinator*
SemanticDockCoordinator::
instancePairConnectionCoordinator() const
{
    return panels.instancePairCoordinator.get();
}

InstancePairConnectionWorkflow*
SemanticDockCoordinator::
instancePairConnectionWorkflow() const
{
    return panels.instancePairWorkflow.get();
}

MultiSignalPropagationPanel*
SemanticDockCoordinator::
multiSignalPropagationPanel() const
{
    return panels.multiSignalPanel;
}

MultiSignalPropagationWorkflow*
SemanticDockCoordinator::
multiSignalPropagationWorkflow() const
{
    return panels.multiSignalWorkflow.get();
}

RtlHighRiskEditPanelCoordinator*
SemanticDockCoordinator::
rtlHighRiskEditPanelCoordinator() const
{
    return panels.rtlHighRiskEditPanel.get();
}

WorkspaceEditDocumentManager*
SemanticDockCoordinator::
rtlActionDocumentManager() const
{
    return panels.rtlActionDocuments.get();
}

QDockWidget*
SemanticDockCoordinator::instancePairConnectionDock() const
{
    return panels.instancePairDock;
}

QDockWidget*
SemanticDockCoordinator::multiSignalPropagationDock() const
{
    return panels.multiSignalDock;
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

WavePreviewPanelCoordinator*
SemanticDockCoordinator::wavePreviewPanelCoordinator() const
{
    return panels.wavePreviewPanel.get();
}
