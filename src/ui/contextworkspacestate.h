#ifndef CONTEXTWORKSPACESTATE_H
#define CONTEXTWORKSPACESTATE_H

#include <QList>
#include <QMap>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QVariantMap>

struct ContextWorkspaceState {
    static constexpr int kLegacyVersion = 1;
    static constexpr int kResizableVersion = 2;
    static constexpr int kProviderStateVersion = 3;
    static constexpr int kFloatingGeometryVersion = 4;
    static constexpr int kFloatingInstancesVersion = 5;
    static constexpr int kDockSectionsVersion = 6;
    static constexpr int kVersion = 6;

    static constexpr int kMinimumPeekWidth = 280;
    static constexpr int kMaximumPeekWidth = 920;
    static constexpr int kDefaultPeekWidth = 520;
    static constexpr int kMinimumPeekHeight = 220;
    static constexpr int kMaximumStoredPeekHeight = 8192;
    static constexpr int kDefaultPeekHeight = 440;
    static constexpr int kMinimumDockWidth = 280;
    static constexpr int kMaximumStoredDockWidth = 8192;
    static constexpr int kDefaultDockWidth = 520;
    static constexpr int kFloatingTitleHeight = 32;

    static QRect resolvedFloatingGeometry(
        const QRect& saved, const QString& savedScreenName,
        const QList<QRect>& availableScreenGeometries,
        const QRect& primaryScreenGeometry, const QList<QString>& availableScreenNames)
    {
        const int width = boundedPeekWidth(saved.width());
        const int height = boundedPeekHeight(saved.height());
        const int index = availableScreenNames.indexOf(savedScreenName);
        if (!savedScreenName.isEmpty() && index >= 0 && index < availableScreenGeometries.size()) {
            const QRect screen = availableScreenGeometries.at(index);
            const qint64 visibleWidth = qMin(qint64(saved.x()) + width, qint64(screen.x()) + screen.width())
                - qMax(qint64(saved.x()), qint64(screen.x()));
            const qint64 visibleTitle = qMin(qint64(saved.y()) + kFloatingTitleHeight,
                                             qint64(screen.y()) + screen.height())
                - qMax(qint64(saved.y()), qint64(screen.y()));
            if (screen.isValid() && visibleWidth >= kMinimumPeekWidth / 2
                && visibleTitle >= kFloatingTitleHeight) {
                return QRect(saved.topLeft(), QSize(width, height));
            }
        }
        const QRect primary = primaryScreenGeometry.isValid() ? primaryScreenGeometry
            : QRect(0, 0, kDefaultPeekWidth, kDefaultPeekHeight);
        const int fittedWidth = qMin(width, primary.width());
        const int fittedHeight = qMin(height, primary.height());
        return QRect(qBound(primary.x(), saved.x(), primary.x() + primary.width() - fittedWidth),
                     qBound(primary.y(), saved.y(), primary.y() + primary.height() - fittedHeight),
                     fittedWidth, fittedHeight);
    }

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
    QVariantMap providerStates;
    QString activePinnedResourceKey;
    int peekWidth = kDefaultPeekWidth;
    int peekHeight = kDefaultPeekHeight;
    int dockWidth = kDefaultDockWidth;
    bool dockVisible = false;
    bool railVisible = true;
    bool valid = false;
    int floatingX = 0;
    int floatingY = 0;
    int floatingWidth = kDefaultPeekWidth;
    int floatingHeight = kDefaultPeekHeight;
    QString floatingScreenName;
    bool floatingGeometryValid = false;

    struct FloatingInstance {
        QVariantMap resource;
        int x = 0;
        int y = 0;
        int width = kDefaultPeekWidth;
        int height = kDefaultPeekHeight;
        QString screenName;
        bool geometryValid = false;
        bool kept = false;
        bool operator==(const FloatingInstance& other) const {
            return resource == other.resource && x == other.x && y == other.y
                && width == other.width && height == other.height
                && screenName == other.screenName && geometryValid == other.geometryValid && kept == other.kept;
        }
    };
    QList<FloatingInstance> floatingInstances;
    bool floatingCollapsed = false;
    QMap<QString, QList<FloatingInstance>> documentFloatingLayouts;
    QStringList documentFloatingOrder;
    struct DockSection {
        QString resourceKey;
        bool collapsed = false;
        int height = 0;
        bool operator==(const DockSection& other) const {
            return resourceKey == other.resourceKey && collapsed == other.collapsed && height == other.height;
        }
    };
    QList<DockSection> dockSections;
};

using ContextFloatingInstanceState = ContextWorkspaceState::FloatingInstance;
using ContextDockSectionState = ContextWorkspaceState::DockSection;

struct ContextWorkspaceRestoreResult {
    int restoredResources = 0;
    int skippedResources = 0;
    QStringList warnings;
};

#endif // CONTEXTWORKSPACESTATE_H
