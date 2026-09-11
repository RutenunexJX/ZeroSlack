#ifndef WORKSPACEIGNORESERVICE_H
#define WORKSPACEIGNORESERVICE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

struct WorkspaceIgnoreQuery {
    QString workspaceRoot;
    QStringList directoryPaths;
};

struct WorkspaceIgnoreIssue {
    QString path;
    QString reason;
};

struct WorkspaceIgnoreReport {
    bool valid = false;
    QStringList ignoredDirectories;
    QList<WorkspaceIgnoreIssue> issues;
    QString failureReason;
};

class WorkspaceIgnoreService
{
public:
    static WorkspaceIgnoreService* getInstance();

    WorkspaceIgnoreReport normalizeIgnoredDirectories(
        const WorkspaceIgnoreQuery& query) const;

private:
    static std::unique_ptr<WorkspaceIgnoreService> instance;
};

#endif // WORKSPACEIGNORESERVICE_H
