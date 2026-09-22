#include "uicontrols.h"
#include "semanticdockcoordinator.h"
#include "deferredpanel.h"

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
#include "scopedsearchpanel.h"
#include "semanticpanelrefreshcoordinator.h"
#include "tabmanager.h"
#include "workspaceeditdocumentmanager.h"
#include "workspaceedittransactionservice.h"
#include "workspacemanager.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTabWidget>

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

SemanticDockCoordinator::PanelBundle::~PanelBundle()
{
    if (connectionsContent)
        connectionsContent->cancelCreation();
}

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

    connectionsDock = new QDockWidget(
        QStringLiteral("Connections"),
        dependencies.mainWindow);
    connectionsDock->setObjectName(
        QStringLiteral("ConnectionsDock"));
    connectionsDock->setAllowedAreas(
        Qt::BottomDockWidgetArea);
    connectionsContent = new DeferredPanel(connectionsDock, nullptr,
        [this](QWidget* parent) -> QWidget* {
            connectionsTabs = UiControls::tabWidget(parent);
            connectionsTabs->setObjectName(
                QStringLiteral("connectionsWorkflowTabs"));
            connectionsTabs->setAccessibleName(
                QStringLiteral("Connection workflows"));
            auto* instancePairStack =
                new QStackedWidget(connectionsTabs);
            instancePairStack->setObjectName(
                QStringLiteral("instancePairConnectionStack"));
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

            multiSignalPlanner =
                std::make_unique<MultiSignalPropagationPlanner>();
            multiSignalPanel =
                new MultiSignalPropagationPanel(
                    *multiSignalPlanner,
                    *rtlActionDocuments,
                    connectionsTabs);
            multiSignalWorkflow =
                std::make_unique<MultiSignalPropagationWorkflow>(
                    multiSignalPanel,
                    SemanticIndex::getInstance(),
                    rtlActionDocuments.get(),
                    WorkspaceEditTransactionService::getInstance());

            connectionsTabs->addTab(
                instancePairStack,
                QStringLiteral("Instance Pair"));
            connectionsTabs->addTab(
                multiSignalPanel,
                QStringLiteral("Multi-Signal Propagation"));
            connectionsTabs->setTabToolTip(
                0, QStringLiteral("Connect signals between two hierarchy instances"));
            connectionsTabs->setTabToolTip(
                1, QStringLiteral("Propagate multiple signals through hierarchy boundaries"));
            QObject::connect(instancePairWorkflow.get(), &InstancePairConnectionWorkflow::stateChanged,
                instancePairWorkflow.get(), [this](const InstancePairConnectionWorkflowResult& result) {
                    if (connectionStatusHandler && !result.message.isEmpty())
                        connectionStatusHandler(result.message, 5000);
                });
            QObject::connect(multiSignalWorkflow.get(), &MultiSignalPropagationWorkflow::stateChanged,
                multiSignalWorkflow.get(), [this](const MultiSignalPropagationWorkflowResult& result) {
                    if (connectionStatusHandler && !result.message.isEmpty())
                        connectionStatusHandler(result.message, 5000);
                });
            return connectionsTabs;
        });
    connectionsContent->setObjectName(QStringLiteral("deferredConnectionsPanel"));
    connectionsDock->setWidget(connectionsContent);

    dependencies.addBottomDock(problemsPanel->dock());
    dependencies.addBottomDock(activityLogPanel->dock());
    dependencies.addBottomDock(scopedSearchPanel->dock());
    dependencies.addBottomDock(
        rtlHighRiskEditPanel->dock());
    dependencies.addBottomDock(connectionsDock);
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
        nullptr,
        nullptr);
    setStatusMessageHandler(statusMessageHandler);
    semanticPanelRefresh->configurePanels();
    if (dependencies.workspaceManager
        && problemsPanel
        && problemsPanel->dock()) {
        QObject::connect(
            dependencies.workspaceManager,
            &WorkspaceManager::workspaceClosed,
            problemsPanel->dock(),
            [refresh = semanticPanelRefresh.get()]() {
                refresh->updateProblemsPanel();
            });
    }
}

void SemanticDockCoordinator::PanelBundle::setStatusMessageHandler(
    const std::function<void(const QString&, int)>& statusMessageHandler)
{
    if (semanticPanelRefresh)
        semanticPanelRefresh->setStatusMessageHandler(statusMessageHandler);
    connectionStatusHandler = statusMessageHandler;
    if (!rtlActionStatusConnected) {
        bool connected = false;
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
    if (panels.connectionsContent)
        panels.connectionsContent->ensureCreated();
    return panels.instancePairCoordinator.get();
}

InstancePairConnectionWorkflow*
SemanticDockCoordinator::
instancePairConnectionWorkflow() const
{
    if (panels.connectionsContent)
        panels.connectionsContent->ensureCreated();
    return panels.instancePairWorkflow.get();
}

MultiSignalPropagationPanel*
SemanticDockCoordinator::
multiSignalPropagationPanel() const
{
    if (panels.connectionsContent)
        panels.connectionsContent->ensureCreated();
    return panels.multiSignalPanel;
}

MultiSignalPropagationWorkflow*
SemanticDockCoordinator::
multiSignalPropagationWorkflow() const
{
    if (panels.connectionsContent)
        panels.connectionsContent->ensureCreated();
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
    return panels.connectionsDock;
}

QDockWidget*
SemanticDockCoordinator::multiSignalPropagationDock() const
{
    return panels.connectionsDock;
}

QDockWidget* SemanticDockCoordinator::connectionsDock() const
{
    return panels.connectionsDock;
}

QTabWidget* SemanticDockCoordinator::connectionsTabs() const
{
    if (panels.connectionsContent)
        panels.connectionsContent->ensureCreated();
    return panels.connectionsTabs;
}

bool SemanticDockCoordinator::showConnectionPage(
    const QString& panelId)
{
    if (panels.connectionsContent)
        panels.connectionsContent->ensureCreated();
    if (!panels.connectionsTabs)
        return false;
    if (panelId == InstancePairConnectionCoordinator::panelId()) {
        panels.connectionsTabs->setCurrentIndex(0);
        return true;
    }
    if (panelId == MultiSignalPropagationPanel::panelId()) {
        panels.connectionsTabs->setCurrentIndex(1);
        return true;
    }
    return panelId == QStringLiteral("connections");
}

RtlInsightsPanelCoordinator* SemanticDockCoordinator::rtlInsightsPanelCoordinator() const
{
    return nullptr;
}

SignalKernelGraphPanelCoordinator*
SemanticDockCoordinator::signalKernelGraphPanelCoordinator() const
{
    return nullptr;
}
