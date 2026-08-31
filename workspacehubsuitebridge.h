#ifndef WORKSPACEHUBSUITEBRIDGE_H
#define WORKSPACEHUBSUITEBRIDGE_H

#include "workspacehubtypes.h"
#include "zeroslackexport.h"

#include <QJsonObject>
#include <QSet>
#include <QString>

struct ZEROSLACK_API WorkspaceHubSuiteResult {
    bool ok = false;
    QJsonObject model;
    QString errorCode;
    QString errorMessage;
};

struct ZEROSLACK_API WorkspaceHubSuiteRegistry {
    bool ok = false;
    QSet<QString> providers;
    QString errorCode;
    QString errorMessage;

    bool contains(const QString& providerId) const
    {
        return ok && providers.contains(providerId);
    }
};

class ZEROSLACK_API WorkspaceHubSuiteBridge
{
public:
    static WorkspaceHubSuiteRegistry registry(
        int timeoutMs = 500);
    static WorkspaceHubSuiteResult resolve(
        const WorkspaceHubItem& item,
        int timeoutMs = 1200);
    static WorkspaceHubSuiteResult describeSurface(
        const WorkspaceHubItem& item,
        int timeoutMs = 1200);
    static WorkspaceHubSuiteResult open(
        const WorkspaceHubItem& item,
        int timeoutMs = 2000);
};

#endif // WORKSPACEHUBSUITEBRIDGE_H
