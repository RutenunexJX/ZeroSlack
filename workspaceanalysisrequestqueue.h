#ifndef WORKSPACEANALYSISREQUESTQUEUE_H
#define WORKSPACEANALYSISREQUESTQUEUE_H

#include "projectmodel.h"

class WorkspaceAnalysisRequestQueue
{
public:
    bool active() const { return activeRequest; }
    bool hasPending() const { return pendingRequest; }

    void start(const ProjectSnapshot& project);
    bool queueLatest(const ProjectSnapshot& project);
    bool finishAndTakePending(ProjectSnapshot* nextProject);
    void clear();

private:
    ProjectSnapshot currentProject;
    ProjectSnapshot pendingProject;
    bool activeRequest = false;
    bool pendingRequest = false;
};

#endif // WORKSPACEANALYSISREQUESTQUEUE_H
