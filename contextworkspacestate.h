#ifndef CONTEXTWORKSPACESTATE_H
#define CONTEXTWORKSPACESTATE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QVariantMap>

struct ContextWorkspaceState {
    static constexpr int kLegacyVersion = 1;
    static constexpr int kVersion = 2;

    static constexpr int kMinimumPeekWidth = 280;
    static constexpr int kMaximumPeekWidth = 920;
    static constexpr int kDefaultPeekWidth = 520;
    static constexpr int kMinimumPeekHeight = 220;
    static constexpr int kMaximumStoredPeekHeight = 8192;
    static constexpr int kDefaultPeekHeight = 440;
    static constexpr int kMinimumDockWidth = 280;
    static constexpr int kMaximumStoredDockWidth = 8192;
    static constexpr int kDefaultDockWidth = 520;

    static int boundedPeekWidth(int width)
    {
        return qBound(kMinimumPeekWidth,
                      width,
                      kMaximumPeekWidth);
    }

    static int boundedPeekHeight(int height)
    {
        return qBound(kMinimumPeekHeight,
                      height,
                      kMaximumStoredPeekHeight);
    }

    static int boundedDockWidth(int width)
    {
        return qBound(kMinimumDockWidth,
                      width,
                      kMaximumStoredDockWidth);
    }

    QList<QVariantMap> pinnedResources;
    QString activePinnedResourceKey;
    int peekWidth = kDefaultPeekWidth;
    int peekHeight = kDefaultPeekHeight;
    int dockWidth = kDefaultDockWidth;
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
