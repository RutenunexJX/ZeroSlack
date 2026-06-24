#include "workspaceanalysisrequestqueue.h"

WorkspaceAnalysisRequestTelemetry WorkspaceAnalysisRequestQueue::telemetry() const
{
    WorkspaceAnalysisRequestTelemetry info;
    info.active = activeRequest;
    info.pending = pendingRequest;
    info.activeAgeMs =
        activeRequest && activeTimerValid ? activeTimer.elapsed() : -1;
    info.pendingAgeMs =
        pendingRequest && pendingTimerValid ? pendingTimer.elapsed() : -1;
    info.pendingUpdateCount =
        pendingRequest ? pendingUpdateCount : 0;
    info.lastFinishedActiveAgeMs = lastFinishedActiveAgeMs;
    info.lastTakenPendingAgeMs = lastTakenPendingAgeMs;
    info.lastTakenPendingUpdateCount = lastTakenPendingUpdateCount;
    return info;
}

void WorkspaceAnalysisRequestQueue::start(const ProjectSnapshot& project)
{
    currentProject = project;
    activeRequest = project.isOpen();
    activeTimerValid = activeRequest;
    if (activeTimerValid)
        activeTimer.restart();
}

bool WorkspaceAnalysisRequestQueue::queueLatest(const ProjectSnapshot& project)
{
    if (!activeRequest || !project.isOpen())
        return false;

    if (!pendingRequest) {
        pendingTimer.restart();
        pendingTimerValid = true;
    }
    pendingProject = project;
    pendingRequest = true;
    ++pendingUpdateCount;
    return true;
}

bool WorkspaceAnalysisRequestQueue::finishAndTakePending(
    ProjectSnapshot* nextProject)
{
    lastFinishedActiveAgeMs =
        activeTimerValid ? activeTimer.elapsed() : -1;
    activeTimerValid = false;
    activeRequest = false;
    currentProject = ProjectSnapshot();

    if (!pendingRequest) {
        lastTakenPendingAgeMs = -1;
        lastTakenPendingUpdateCount = 0;
        return false;
    }

    if (nextProject)
        *nextProject = pendingProject;
    lastTakenPendingAgeMs =
        pendingTimerValid ? pendingTimer.elapsed() : -1;
    lastTakenPendingUpdateCount = pendingUpdateCount;
    pendingProject = ProjectSnapshot();
    pendingRequest = false;
    pendingTimerValid = false;
    pendingUpdateCount = 0;
    return nextProject && nextProject->isOpen();
}

void WorkspaceAnalysisRequestQueue::clear()
{
    currentProject = ProjectSnapshot();
    pendingProject = ProjectSnapshot();
    activeRequest = false;
    pendingRequest = false;
    activeTimerValid = false;
    pendingTimerValid = false;
    pendingUpdateCount = 0;
    lastFinishedActiveAgeMs = -1;
    lastTakenPendingAgeMs = -1;
    lastTakenPendingUpdateCount = 0;
}
