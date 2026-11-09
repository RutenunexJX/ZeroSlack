#pragma once
#include "zeroslackexport.h"
#include "../core/workspacesimulationcontroller.h"
#include <QObject>
#include <QPointer>
class TabManager;
class WorkspaceManager;
namespace simdock {
class Workbench;
class ZEROSLACK_API SimulationRunCoordinator final : public QObject {
    Q_OBJECT
public:
    SimulationRunCoordinator(TabManager* tabs, WorkspaceManager* workspaces, Workbench* view, QObject* parent);
    QString selectTarget(const QString& file = {});
    QString run();
    SimulationBuildRequest capture(const SimulationTarget& target, QString* error, bool allowPending = false) const;
    WorkspaceSimulationController::SaveInputs saveInputs();
    WorkspaceSimulationController::CheckCurrent checkCurrent();
    WorkspaceSimulationController::AwaitInputs awaitInputs();
private:
    QString check(const SimulationBuildRequest& request, bool checkCatalog = true) const;
    QString saveParticipating(const SimulationBuildPlan& plan, SimulationBuildRequest& request);
    QPointer<TabManager> m_tabs;
    QPointer<WorkspaceManager> m_workspaces;
    QPointer<Workbench> m_view;
    QPointer<WorkspaceSimulationController> m_controller;
};
}
