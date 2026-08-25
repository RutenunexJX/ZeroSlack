#ifndef FSMGRAPHLAYOUT_H
#define FSMGRAPHLAYOUT_H

#include "fsmgraphservice.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <functional>

enum class FsmLayoutDirection {
    TopDown,
    LeftRight
};

struct FsmLayoutOptions {
    FsmLayoutDirection direction = FsmLayoutDirection::TopDown;
    qreal minNodeWidth = 144.0;
    qreal maxNodeWidth = 252.0;
    qreal nodeHorizontalPadding = 28.0;
    qreal nodeHeight = 58.0;
    qreal aliasNodeHeight = 32.0;
    qreal dummyWidth = 26.0;
    qreal layerSpacing = 96.0;
    qreal nodeSpacing = 56.0;
    qreal parallelEdgeSpacing = 26.0;
    qreal gridStep = 32.0;
    qreal orthogonalStraightThreshold = 14.0;
    qreal labelEdgeDistance = 24.0;
    qreal labelSlideStep = 14.0;
    qreal labelWidth = 42.0;
    qreal labelHeight = 22.0;
    qreal labelClearance = 8.0;
    qreal sceneMargin = 40.0;
    qreal titleLineHeight = 20.0;
    qreal detailLineHeight = 16.0;
    qreal selfLoopHeight = 32.0;
    int aliasBackEdgeRankThreshold = 2;
    int aliasForwardEdgeRankThreshold = 3;
    int fallbackAliasPercentLimit = 20;
    int crossingSortIterations = 12;
    std::function<qreal(const QString&)> textWidth;
};

struct FsmLayoutNode {
    int nodeId = -1;
    QString stateName;
    QString displayName;
    QString detail;
    QRectF rect;
    QRectF titleTextRect;
    QRectF detailTextRect;
    int rank = 0;
    int order = 0;
    bool alias = false;
    bool initialState = false;
    bool implicitState = false;
    bool showDetail = true;
    int canonicalNodeId = -1;
    bool deadEndState = false;
    RtlInsightCodeLink codeLink;
};

struct FsmLayoutEdge {
    int edgeId = -1;
    int transitionIndex = -1;
    int fromNodeId = -1;
    int toNodeId = -1;
    QString fromState;
    QString toState;
    QString condition;
    QString label;
    QList<QPointF> points;
    QPointF arrowTip;
    qreal arrowAngle = 0.0;
    QPointF labelAnchor;
    QRectF labelRect;
    int layoutFromRank = 0;
    int layoutToRank = 0;
    bool selfLoop = false;
    bool reversedForLayout = false;
    bool curved = false;
    RtlInsightCodeLink codeLink;
};

struct FsmGraphLayout {
    QList<FsmLayoutNode> nodes;
    QList<FsmLayoutEdge> edges;
    QRectF nodeBounds;
    QRectF sceneBounds;
    QStringList warnings;
    int renderedCrossingCount = 0;
    int fallbackAliasCount = 0;
};

FsmGraphLayout layoutFsmGraph(const FsmGraph& graph,
                              const FsmLayoutOptions& options = {});

#endif // FSMGRAPHLAYOUT_H
