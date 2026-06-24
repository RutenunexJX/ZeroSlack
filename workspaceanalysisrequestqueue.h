#ifndef WORKSPACEANALYSISREQUESTQUEUE_H
#define WORKSPACEANALYSISREQUESTQUEUE_H

#include "projectmodel.h"

#include <QElapsedTimer>

struct WorkspaceAnalysisRequestTelemetry {
    bool active = false;
    bool pending = false;
    qint64 activeAgeMs = -1;
    qint64 pendingAgeMs = -1;
    int pendingUpdateCount = 0;
    qint64 lastFinishedActiveAgeMs = -1;
    qint64 lastTakenPendingAgeMs = -1;
    int lastTakenPendingUpdateCount = 0;
};

class WorkspaceAnalysisRequestQueue
{
public:
    bool active() const { return activeRequest; }
    bool hasPending() const { return pendingRequest; }
    WorkspaceAnalysisRequestTelemetry telemetry() const;

    void start(const ProjectSnapshot& project);
    bool queueLatest(const ProjectSnapshot& project);
    bool finishAndTakePending(ProjectSnapshot* nextProject);
    void clear();

private:
    ProjectSnapshot currentProject;
    ProjectSnapshot pendingProject;
    bool activeRequest = false;
    bool pendingRequest = false;
    QElapsedTimer activeTimer;
    QElapsedTimer pendingTimer;
    bool activeTimerValid = false;
    bool pendingTimerValid = false;
    int pendingUpdateCount = 0;
    qint64 lastFinishedActiveAgeMs = -1;
    qint64 lastTakenPendingAgeMs = -1;
    int lastTakenPendingUpdateCount = 0;
};

#endif // WORKSPACEANALYSISREQUESTQUEUE_H
