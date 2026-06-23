#include "workspaceanalysisrequestqueue.h"

void WorkspaceAnalysisRequestQueue::start(const ProjectSnapshot& project)
{
    currentProject = project;
    activeRequest = project.isOpen();
}

bool WorkspaceAnalysisRequestQueue::queueLatest(const ProjectSnapshot& project)
{
    if (!activeRequest || !project.isOpen())
        return false;

    pendingProject = project;
    pendingRequest = true;
    return true;
}

bool WorkspaceAnalysisRequestQueue::finishAndTakePending(
    ProjectSnapshot* nextProject)
{
    activeRequest = false;
    currentProject = ProjectSnapshot();

    if (!pendingRequest)
        return false;

    if (nextProject)
        *nextProject = pendingProject;
    pendingProject = ProjectSnapshot();
    pendingRequest = false;
    return nextProject && nextProject->isOpen();
}

void WorkspaceAnalysisRequestQueue::clear()
{
    currentProject = ProjectSnapshot();
    pendingProject = ProjectSnapshot();
    activeRequest = false;
    pendingRequest = false;
}
