#ifndef CONTEXTWORKSPACESTATE_H
#define CONTEXTWORKSPACESTATE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct ContextWorkspaceState {
    static constexpr int kVersion = 1;

    QList<QVariantMap> pinnedResources;
    QString activePinnedResourceKey;
    int peekWidth = 520;
    int dockWidth = 520;
    bool dockVisible = false;
    bool railVisible = true;
    bool valid = false;
};

struct ContextWorkspaceRestoreResult {
    int restoredResources = 0;
    int skippedResources = 0;
    QStringList warnings;
};

#endif // CONTEXTWORKSPACESTATE_H
