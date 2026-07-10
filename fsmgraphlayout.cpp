#include "fsmgraphlayout.h"

#include <QHash>
#include <QLineF>
#include <QQueue>
#include <QSet>
#include <QSizeF>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

constexpr qreal kEpsilon = 0.001;

struct WorkEdge {
    int edgeId = -1;
    int transitionIndex = -1;
    int from = -1;
    int to = -1;
    int layoutFrom = -1;
    int layoutTo = -1;
    int layoutToVertexId = -1;
    bool selfLoop = false;
    bool reversed = false;
    bool usesAlias = false;
    bool fallbackAlias = false;
    QList<int> vertexPath;
};

struct WorkVertex {
    int id = -1;
    int stateIndex = -1;
    int canonicalStateIndex = -1;
    int rank = 0;
    qreal width = 0.0;
    qreal height = 0.0;
    qreal x = 0.0;
    qreal y = 0.0;
    bool dummy = false;
    bool alias = false;
};

struct EdgeSegment {
    int edgeId = -1;
    QPointF a;
    QPointF b;
};

QString conditionId(int transitionIndex)
{
    return QStringLiteral("C%1").arg(transitionIndex);
}

qreal itemWidth(const WorkVertex& vertex, const FsmLayoutOptions& options)
{
    if (vertex.width > 0.0)
        return vertex.width;
    return vertex.dummy ? options.dummyWidth : options.maxNodeWidth;
}

qreal itemHeight(const WorkVertex& vertex, const FsmLayoutOptions& options)
{
    if (vertex.height > 0.0)
        return vertex.height;
    if (vertex.dummy)
        return options.dummyWidth;
    return vertex.alias ? options.aliasNodeHeight : options.nodeHeight;
}

qreal textWidthEstimate(const QString& text, const FsmLayoutOptions& options)
{
    if (options.textWidth)
        return qMax<qreal>(0.0, options.textWidth(text));
    qreal width = 0.0;
    for (QChar ch : text) {
        width += ch.isSpace() ? 4.0 : 8.2;
    }
    return width;
}

qreal adaptiveNodeWidth(const QString& title,
                        const QString& detail,
                        bool showDetail,
                        const FsmLayoutOptions& options)
{
    const qreal titleWidth = textWidthEstimate(title, options);
    const qreal detailWidth = showDetail ? textWidthEstimate(detail, options)
                                         : 0.0;
    const qreal requested =
        qMax(titleWidth, detailWidth) + options.nodeHorizontalPadding;
    return qBound(options.minNodeWidth,
                  requested,
                  options.maxNodeWidth);
}

void assignVertexSizes(QList<WorkVertex>& vertices,
                       const QList<FsmStateRow>& stateRows,
                       const QString& stateRegisterDisplayName,
                       const FsmLayoutOptions& options)
{
    for (WorkVertex& vertex : vertices) {
        if (vertex.dummy) {
            vertex.width = options.dummyWidth;
            vertex.height = options.dummyWidth;
            continue;
        }
        const FsmStateRow& row = stateRows.at(vertex.stateIndex);
        const bool showDetail = !vertex.alias;
        const QString detail = stateRegisterDisplayName.isEmpty()
            ? row.detailDisplayName
            : stateRegisterDisplayName;
        vertex.width = adaptiveNodeWidth(row.stateDisplayName,
                                         detail,
                                         showDetail,
                                         options);
        vertex.height = showDetail ? options.nodeHeight
                                   : options.aliasNodeHeight;
    }
}

QRectF orientRect(const QPointF& center,
                  qreal width,
                  qreal height,
                  FsmLayoutDirection direction)
{
    Q_UNUSED(direction);
    return QRectF(center.x() - width / 2.0,
                  center.y() - height / 2.0,
                  width,
                  height);
}

qreal majorCoord(const QPointF& point, FsmLayoutDirection direction)
{
    return direction == FsmLayoutDirection::LeftRight ? point.x() : point.y();
}

qreal minorCoord(const QPointF& point, FsmLayoutDirection direction)
{
    return direction == FsmLayoutDirection::LeftRight ? point.y() : point.x();
}

QPointF pointFromMajorMinor(qreal major,
                            qreal minor,
                            FsmLayoutDirection direction)
{
    return direction == FsmLayoutDirection::LeftRight ? QPointF(major, minor)
                                                      : QPointF(minor, major);
}

QPointF sourcePort(const QRectF& rect,
                   qreal minorOffset,
                   FsmLayoutDirection direction,
                   bool forward)
{
    if (direction == FsmLayoutDirection::LeftRight) {
        return QPointF(forward ? rect.right() : rect.left(),
                       rect.center().y() + minorOffset);
    }
    return QPointF(rect.center().x() + minorOffset,
                   forward ? rect.bottom() : rect.top());
}

QPointF targetPort(const QRectF& rect,
                   qreal minorOffset,
                   FsmLayoutDirection direction,
                   bool forward)
{
    if (direction == FsmLayoutDirection::LeftRight) {
        return QPointF(forward ? rect.left() : rect.right(),
                       rect.center().y() + minorOffset);
    }
    return QPointF(rect.center().x() + minorOffset,
                   forward ? rect.top() : rect.bottom());
}

qreal angleFromLastSegment(const QList<QPointF>& points)
{
    for (int i = points.size() - 1; i > 0; --i) {
        const QPointF end = points.at(i);
        const QPointF prev = points.at(i - 1);
        if (QLineF(prev, end).length() > 0.5)
            return std::atan2(end.y() - prev.y(), end.x() - prev.x());
    }
    return 0.0;
}

void appendPoint(QList<QPointF>& points, const QPointF& point)
{
    if (!points.isEmpty()
        && QLineF(points.last(), point).length() < kEpsilon) {
        return;
    }
    points.append(point);
}

QRectF uniteRects(const QRectF& lhs, const QRectF& rhs)
{
    if (lhs.isNull() || !lhs.isValid())
        return rhs;
    if (rhs.isNull() || !rhs.isValid())
        return lhs;
    return lhs.united(rhs);
}

bool rectOverlapsAny(const QRectF& rect, const QList<QRectF>& others)
{
    for (const QRectF& other : others) {
        if (rect.intersects(other))
            return true;
    }
    return false;
}

qreal orientation(const QPointF& a, const QPointF& b, const QPointF& c)
{
    return (b.x() - a.x()) * (c.y() - a.y())
        - (b.y() - a.y()) * (c.x() - a.x());
}

bool pointOnSegment(const QPointF& point,
                    const QPointF& start,
                    const QPointF& end,
                    qreal tolerance = 0.1)
{
    return qAbs(orientation(start, end, point)) <= tolerance
        && point.x() >= qMin(start.x(), end.x()) - tolerance
        && point.x() <= qMax(start.x(), end.x()) + tolerance
        && point.y() >= qMin(start.y(), end.y()) - tolerance
        && point.y() <= qMax(start.y(), end.y()) + tolerance;
}

bool segmentsIntersect(const QPointF& a,
                       const QPointF& b,
                       const QPointF& c,
                       const QPointF& d)
{
    const QRectF abBounds(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                          QPointF(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
    const QRectF cdBounds(QPointF(qMin(c.x(), d.x()), qMin(c.y(), d.y())),
                          QPointF(qMax(c.x(), d.x()), qMax(c.y(), d.y())));
    if (!abBounds.adjusted(-0.1, -0.1, 0.1, 0.1).intersects(
            cdBounds.adjusted(-0.1, -0.1, 0.1, 0.1))) {
        return false;
    }

    const qreal o1 = orientation(a, b, c);
    const qreal o2 = orientation(a, b, d);
    const qreal o3 = orientation(c, d, a);
    const qreal o4 = orientation(c, d, b);
    if ((o1 > 0.0 && o2 < 0.0 || o1 < 0.0 && o2 > 0.0)
        && (o3 > 0.0 && o4 < 0.0 || o3 < 0.0 && o4 > 0.0)) {
        return true;
    }
    return pointOnSegment(c, a, b)
        || pointOnSegment(d, a, b)
        || pointOnSegment(a, c, d)
        || pointOnSegment(b, c, d);
}

bool segmentIntersectsRect(const QPointF& start,
                           const QPointF& end,
                           const QRectF& rect)
{
    if (rect.contains(start) || rect.contains(end))
        return true;
    if (rect.contains((start + end) / 2.0))
        return true;
    const QPointF topLeft = rect.topLeft();
    const QPointF topRight = rect.topRight();
    const QPointF bottomRight = rect.bottomRight();
    const QPointF bottomLeft = rect.bottomLeft();
    return segmentsIntersect(start, end, topLeft, topRight)
        || segmentsIntersect(start, end, topRight, bottomRight)
        || segmentsIntersect(start, end, bottomRight, bottomLeft)
        || segmentsIntersect(start, end, bottomLeft, topLeft);
}

bool rectIntersectsAnySegment(const QRectF& rect,
                              const QList<EdgeSegment>& segments,
                              qreal clearance,
                              int ignoredEdgeId = -1)
{
    const QRectF expanded = rect.adjusted(-clearance,
                                          -clearance,
                                          clearance,
                                          clearance);
    for (const EdgeSegment& segment : segments) {
        if (segment.edgeId == ignoredEdgeId)
            continue;
        if (QLineF(segment.a, segment.b).length() < 1.0)
            continue;
        if (segmentIntersectsRect(segment.a, segment.b, expanded))
            return true;
    }
    return false;
}

QPointF segmentNormal(const QPointF& a, const QPointF& b)
{
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    const qreal length = std::hypot(dx, dy);
    if (length < 0.1)
        return QPointF(0.0, -1.0);
    return QPointF(-dy / length, dx / length);
}

QPointF labelCandidateOnSegment(const QPointF& a,
                                const QPointF& b,
                                qreal t,
                                qreal offset)
{
    const QPointF base(a.x() + (b.x() - a.x()) * t,
                       a.y() + (b.y() - a.y()) * t);
    const QPointF normal = segmentNormal(a, b);
    return QPointF(base.x() + normal.x() * offset,
                   base.y() + normal.y() * offset);
}

QPointF chooseLabelAnchor(const QList<QPointF>& points,
                          const QSizeF& labelSize,
                          const QList<QRectF>& blockedRects,
                          QList<QRectF>& usedLabelRects,
                          const QList<EdgeSegment>& edgeSegments,
                          qreal clearance,
                          FsmLayoutDirection direction,
                          qreal labelEdgeDistance,
                          qreal labelSlideStep,
                          int ownerEdgeId)
{
    if (points.size() < 2)
        return points.isEmpty() ? QPointF() : points.first();

    struct Segment {
        QPointF a;
        QPointF b;
        qreal length = 0.0;
    };
    QList<Segment> segments;
    for (int i = 1; i < points.size(); ++i) {
        const qreal length = QLineF(points.at(i - 1), points.at(i)).length();
        const qreal majorDelta = qAbs(majorCoord(points.at(i), direction)
                                      - majorCoord(points.at(i - 1), direction));
        const qreal minorDelta = qAbs(minorCoord(points.at(i), direction)
                                      - minorCoord(points.at(i - 1), direction));
        if (length > 4.0 && majorDelta >= minorDelta)
            segments.append(Segment{points.at(i - 1), points.at(i), length});
    }
    std::sort(segments.begin(), segments.end(), [](const Segment& lhs,
                                                   const Segment& rhs) {
        return lhs.length > rhs.length;
    });

    if (segments.isEmpty()) {
        for (int i = 1; i < points.size(); ++i) {
            const qreal length =
                QLineF(points.at(i - 1), points.at(i)).length();
            if (length > 4.0)
                segments.append(Segment{points.at(i - 1), points.at(i), length});
        }
    }

    const QList<qreal> sideSigns{1.0, -1.0};
    const QList<qreal> extraDistances{0.0, 8.0, 16.0, 24.0, 32.0};
    QPointF selectedCenter;
    const auto tryCandidates = [&](bool avoidUsedLabels) {
        for (const Segment& segment : segments) {
            const qreal minMajor = qMin(majorCoord(segment.a, direction),
                                        majorCoord(segment.b, direction));
            const qreal maxMajor = qMax(majorCoord(segment.a, direction),
                                        majorCoord(segment.b, direction));
            const qreal baseMajor = (minMajor + maxMajor) / 2.0;
            const qreal segmentMinor =
                (minorCoord(segment.a, direction)
                 + minorCoord(segment.b, direction)) / 2.0;
            for (qreal extraDistance : extraDistances) {
                for (qreal side : sideSigns) {
                    const qreal searchStep =
                        qMax<qreal>(4.0, labelSlideStep / 3.0);
                    const int maximumSlides = qMax(
                        1,
                        static_cast<int>(
                            std::ceil((segment.length + 160.0)
                                      / searchStep)));
                    for (int slideIndex = 0;
                         slideIndex <= maximumSlides * 2;
                         ++slideIndex) {
                        const int slide = slideIndex == 0
                            ? 0
                            : ((slideIndex + 1) / 2)
                                * (slideIndex % 2 == 1 ? -1 : 1);
                        const qreal minorDistance =
                            labelEdgeDistance + extraDistance
                            + labelSize.width() / 2.0;
                        const qreal distanceBudget =
                            labelEdgeDistance + labelSize.width() + 12.0;
                        const qreal maximumMajorOvershoot = std::sqrt(
                            qMax<qreal>(0.0,
                                       distanceBudget * distanceBudget
                                           - minorDistance * minorDistance));
                        const qreal lowMajor =
                            minMajor - maximumMajorOvershoot;
                        const qreal highMajor =
                            maxMajor + maximumMajorOvershoot;
                        const qreal desiredMajor =
                            baseMajor + slide * searchStep;
                        const qreal major = lowMajor <= highMajor
                            ? qBound(lowMajor, desiredMajor, highMajor)
                            : baseMajor;
                        const qreal minor = segmentMinor
                            + side * minorDistance;
                        const QPointF center =
                            pointFromMajorMinor(major, minor, direction);
                        const QRectF rect(
                            center.x() - labelSize.width() / 2.0,
                            center.y() - labelSize.height() / 2.0,
                            labelSize.width(),
                            labelSize.height());
                        if (rectOverlapsAny(rect, blockedRects))
                            continue;
                        if (avoidUsedLabels
                            && rectOverlapsAny(rect, usedLabelRects)) {
                            continue;
                        }
                        if (rectIntersectsAnySegment(rect,
                                                     edgeSegments,
                                                     clearance,
                                                     ownerEdgeId)) {
                            continue;
                        }
                        usedLabelRects.append(rect);
                        selectedCenter = center;
                        return true;
                    }
                }
            }
        }
        return false;
    };
    if (tryCandidates(true))
        return selectedCenter;

    const Segment fallbackSegment = segments.isEmpty()
        ? Segment{points.first(), points.last(), QLineF(points.first(),
                                                        points.last()).length()}
        : segments.first();
    const qreal fallbackMajor =
        (majorCoord(fallbackSegment.a, direction)
         + majorCoord(fallbackSegment.b, direction)) / 2.0;
    const qreal fallbackMinor =
        (minorCoord(fallbackSegment.a, direction)
         + minorCoord(fallbackSegment.b, direction)) / 2.0
        + labelEdgeDistance + labelSize.width() / 2.0;
    const QPointF fallback =
        pointFromMajorMinor(fallbackMajor, fallbackMinor, direction);
    const QRectF rect(fallback.x() - labelSize.width() / 2.0,
                      fallback.y() - labelSize.height() / 2.0,
                      labelSize.width(),
                      labelSize.height());
    usedLabelRects.append(rect);
    return fallback;
}

QRectF labelRectAt(const QPointF& anchor, const QSizeF& size)
{
    return QRectF(anchor.x() - size.width() / 2.0,
                  anchor.y() - size.height() / 2.0,
                  size.width(),
                  size.height());
}

QList<QPointF> renderedEdgePolyline(const FsmLayoutEdge& edge);
QPointF quadraticPoint(const QPointF& start,
                       const QPointF& control,
                       const QPointF& end,
                       qreal t);

QPointF chooseSelfLoopLabelAnchor(
    const FsmLayoutEdge& edge,
    const QRectF& nodeRect,
    const QSizeF& labelSize,
    const QList<QRectF>& blockedRects,
    QList<QRectF>& usedLabelRects,
    const QList<EdgeSegment>& edgeSegments,
    const FsmLayoutOptions& options)
{
    const QList<QPointF> renderedPoints = renderedEdgePolyline(edge);
    QRectF pathBounds;
    for (const QPointF& point : renderedPoints)
        pathBounds = uniteRects(pathBounds, QRectF(point, QSizeF(1.0, 1.0)));

    const QList<int> lateralOffsets{0, -1, 1, -2, 2, -3, 3, -4, 4};
    for (int outward = 0; outward < 64; ++outward) {
        for (int lateral : lateralOffsets) {
            QPointF center;
            if (options.direction == FsmLayoutDirection::LeftRight) {
                center.setX(pathBounds.left()
                            - options.labelClearance
                            - labelSize.width() / 2.0
                            - outward
                                * (labelSize.width()
                                   + options.labelClearance));
                center.setY(nodeRect.center().y()
                            + lateral
                                * (labelSize.height()
                                   + options.labelClearance));
            } else {
                center.setX(nodeRect.center().x()
                            + lateral
                                * (labelSize.width()
                                   + options.labelClearance));
                center.setY(pathBounds.top()
                            - options.labelClearance
                            - labelSize.height() / 2.0
                            - outward
                                * (labelSize.height()
                                   + options.labelClearance));
            }
            const QRectF rect = labelRectAt(center, labelSize);
            if (rectOverlapsAny(rect, blockedRects)
                || rectOverlapsAny(rect, usedLabelRects)
                || rectIntersectsAnySegment(rect,
                                             edgeSegments,
                                             options.labelClearance,
                                             edge.edgeId)) {
                continue;
            }
            usedLabelRects.append(rect);
            return center;
        }
    }

    const QPointF fallback = options.direction
            == FsmLayoutDirection::LeftRight
        ? QPointF(pathBounds.left() - labelSize.width(),
                  nodeRect.center().y())
        : QPointF(nodeRect.center().x(),
                  pathBounds.top() - labelSize.height());
    usedLabelRects.append(labelRectAt(fallback, labelSize));
    return fallback;
}

QPointF chooseCurvedEdgeLabelAnchor(
    const FsmLayoutEdge& edge,
    const QSizeF& labelSize,
    const QList<QRectF>& blockedRects,
    QList<QRectF>& usedLabelRects,
    const QList<EdgeSegment>& edgeSegments,
    const FsmLayoutOptions& options)
{
    const QPointF start = edge.points.at(0);
    const QPointF control = edge.points.at(1);
    const QPointF end = edge.points.at(2);
    const QPointF chordMid = (start + end) / 2.0;
    const QPointF curveMid = quadraticPoint(start, control, end, 0.5);
    QPointF outward = curveMid - chordMid;
    const qreal outwardLength = std::hypot(outward.x(), outward.y());
    if (outwardLength > 0.1) {
        outward /= outwardLength;
    } else {
        outward = segmentNormal(start, end);
    }

    const QList<qreal> samples{0.50, 0.42, 0.58, 0.34, 0.66, 0.26, 0.74};
    const qreal initialOffset = qMax(labelSize.width(), labelSize.height())
            / 2.0
        + options.labelClearance;
    for (int outwardStep = 0; outwardStep < 64; ++outwardStep) {
        const qreal offset = initialOffset
            + outwardStep
                * qMax<qreal>(6.0, options.labelSlideStep / 2.0);
        for (qreal t : samples) {
            const QPointF center = quadraticPoint(start, control, end, t)
                + outward * offset;
            const QRectF rect = labelRectAt(center, labelSize);
            if (rectOverlapsAny(rect, blockedRects)
                || rectOverlapsAny(rect, usedLabelRects)
                || rectIntersectsAnySegment(rect,
                                             edgeSegments,
                                             options.labelClearance,
                                             edge.edgeId)) {
                continue;
            }
            usedLabelRects.append(rect);
            return center;
        }
    }

    const QPointF fallback = curveMid + outward * (initialOffset + 512.0);
    usedLabelRects.append(labelRectAt(fallback, labelSize));
    return fallback;
}

int stableStateIndex(const QList<FsmStateRow>& rows, const QString& stateName)
{
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).stateDisplayName == stateName)
            return i;
    }
    return -1;
}

int chooseInitialState(const QList<FsmStateRow>& rows,
                       const QList<WorkEdge>& edges,
                       int stateCount)
{
    if (rows.isEmpty())
        return -1;
    QList<int> incoming(stateCount, 0);
    for (const WorkEdge& edge : edges) {
        if (!edge.selfLoop && edge.to >= 0 && edge.to < incoming.size())
            ++incoming[edge.to];
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (incoming.at(i) == 0)
            return i;
    }
    return 0;
}

QSet<int> findDfsBackEdges(const QList<WorkEdge>& edges,
                           int stateCount,
                           int initialState)
{
    QHash<int, QList<int>> outgoing;
    for (int i = 0; i < edges.size(); ++i) {
        if (!edges.at(i).selfLoop)
            outgoing[edges.at(i).from].append(i);
    }

    QList<int> color(stateCount, 0);
    QSet<int> reversed;
    std::function<void(int)> visit = [&](int state) {
        color[state] = 1;
        const QList<int> edgeIds = outgoing.value(state);
        for (int edgeId : edgeIds) {
            const int target = edges.at(edgeId).to;
            if (target < 0 || target >= color.size())
                continue;
            if (color.at(target) == 0) {
                visit(target);
            } else if (color.at(target) == 1) {
                reversed.insert(edgeId);
            }
        }
        color[state] = 2;
    };

    if (initialState >= 0 && initialState < stateCount)
        visit(initialState);
    for (int state = 0; state < stateCount; ++state) {
        if (color.at(state) == 0)
            visit(state);
    }
    return reversed;
}

QList<int> topologicalOrder(const QList<WorkEdge>& edges, int stateCount)
{
    QHash<int, QList<int>> outgoing;
    QList<int> indegree(stateCount, 0);
    for (const WorkEdge& edge : edges) {
        if (edge.selfLoop)
            continue;
        outgoing[edge.layoutFrom].append(edge.layoutTo);
        if (edge.layoutTo >= 0 && edge.layoutTo < indegree.size())
            ++indegree[edge.layoutTo];
    }

    QQueue<int> queue;
    for (int state = 0; state < stateCount; ++state) {
        if (indegree.at(state) == 0)
            queue.enqueue(state);
    }

    QList<int> order;
    while (!queue.isEmpty()) {
        const int state = queue.dequeue();
        order.append(state);
        const QList<int> targets = outgoing.value(state);
        for (int target : targets) {
            --indegree[target];
            if (indegree.at(target) == 0)
                queue.enqueue(target);
        }
    }

    if (order.size() != stateCount) {
        QSet<int> seen;
        for (int state : order)
            seen.insert(state);
        for (int state = 0; state < stateCount; ++state) {
            if (!seen.contains(state))
                order.append(state);
        }
    }
    return order;
}

QList<int> assignRanks(const QList<WorkEdge>& edges, int stateCount)
{
    QList<int> ranks(stateCount, 0);
    const QList<int> order = topologicalOrder(edges, stateCount);
    QHash<int, QList<int>> outgoingEdges;
    for (int i = 0; i < edges.size(); ++i) {
        if (!edges.at(i).selfLoop)
            outgoingEdges[edges.at(i).layoutFrom].append(i);
    }
    for (int state : order) {
        const QList<int> edgeIds = outgoingEdges.value(state);
        for (int edgeId : edgeIds) {
            const WorkEdge& edge = edges.at(edgeId);
            ranks[edge.layoutTo] =
                qMax(ranks.at(edge.layoutTo), ranks.at(edge.layoutFrom) + 1);
        }
    }
    return ranks;
}

void updateOrders(const QList<QList<int>>& layers, QHash<int, int>& orderByVertex)
{
    orderByVertex.clear();
    for (const QList<int>& layer : layers) {
        for (int i = 0; i < layer.size(); ++i)
            orderByVertex.insert(layer.at(i), i);
    }
}

void sortLayerByBarycenter(QList<int>& layer,
                           const QHash<int, QList<int>>& adjacent,
                           const QHash<int, int>& orderByVertex,
                           const QHash<int, QList<int>>& attractionByVertex)
{
    QHash<int, qreal> barycenter;
    for (int vertex : layer) {
        const QList<int> neighbors = adjacent.value(vertex);
        if (neighbors.isEmpty()) {
            barycenter.insert(vertex,
                              static_cast<qreal>(
                                  orderByVertex.value(vertex, 0)));
            continue;
        }
        qreal sum = 0.0;
        int count = 0;
        for (int neighbor : neighbors) {
            if (!orderByVertex.contains(neighbor))
                continue;
            sum += orderByVertex.value(neighbor);
            ++count;
        }
        for (int partner : attractionByVertex.value(vertex)) {
            if (!orderByVertex.contains(partner))
                continue;
            sum += 2.0 * orderByVertex.value(partner);
            count += 2;
        }
        barycenter.insert(vertex,
                          count > 0
                              ? sum / static_cast<qreal>(count)
                              : static_cast<qreal>(
                                    orderByVertex.value(vertex, 0)));
    }
    std::stable_sort(layer.begin(), layer.end(), [&](int lhs, int rhs) {
        const qreal left = barycenter.value(lhs);
        const qreal right = barycenter.value(rhs);
        if (!qFuzzyCompare(left + 1.0, right + 1.0))
            return left < right;
        return lhs < rhs;
    });
}

int countLayerPairCrossings(const QList<int>& upperLayer,
                            const QList<int>& lowerLayer,
                            const QHash<int, QList<int>>& successors)
{
    QHash<int, int> upperOrder;
    QHash<int, int> lowerOrder;
    for (int i = 0; i < upperLayer.size(); ++i)
        upperOrder.insert(upperLayer.at(i), i);
    for (int i = 0; i < lowerLayer.size(); ++i)
        lowerOrder.insert(lowerLayer.at(i), i);

    struct LayerEdge {
        int fromOrder = 0;
        int toOrder = 0;
        int from = -1;
        int to = -1;
    };
    QList<LayerEdge> edges;
    for (int from : upperLayer) {
        for (int to : successors.value(from)) {
            if (!lowerOrder.contains(to))
                continue;
            edges.append({upperOrder.value(from),
                          lowerOrder.value(to),
                          from,
                          to});
        }
    }

    int crossings = 0;
    for (int i = 0; i < edges.size(); ++i) {
        for (int j = i + 1; j < edges.size(); ++j) {
            const LayerEdge& lhs = edges.at(i);
            const LayerEdge& rhs = edges.at(j);
            if (lhs.from == rhs.from || lhs.to == rhs.to)
                continue;
            const int fromDelta = lhs.fromOrder - rhs.fromOrder;
            const int toDelta = lhs.toOrder - rhs.toOrder;
            if ((fromDelta < 0 && toDelta > 0)
                || (fromDelta > 0 && toDelta < 0)) {
                ++crossings;
            }
        }
    }
    return crossings;
}

int totalLayerCrossings(const QList<QList<int>>& layers,
                        const QHash<int, QList<int>>& successors)
{
    int crossings = 0;
    for (int rank = 0; rank + 1 < layers.size(); ++rank)
        crossings += countLayerPairCrossings(layers.at(rank),
                                             layers.at(rank + 1),
                                             successors);
    return crossings;
}

void transposeLayerOrder(QList<QList<int>>& layers,
                         const QHash<int, QList<int>>& successors)
{
    bool improved = true;
    while (improved) {
        improved = false;
        for (int rank = 0; rank < layers.size(); ++rank) {
            QList<int>& layer = layers[rank];
            for (int i = 0; i + 1 < layer.size(); ++i) {
                const int before = totalLayerCrossings(layers, successors);
                layer.swapItemsAt(i, i + 1);
                const int after = totalLayerCrossings(layers, successors);
                if (after < before) {
                    improved = true;
                } else {
                    layer.swapItemsAt(i, i + 1);
                }
            }
        }
    }
}

qint64 directedKey(int from, int to)
{
    return (static_cast<qint64>(from) << 32)
        ^ static_cast<quint32>(to);
}

bool shouldUseAliasForEdge(const WorkEdge& edge,
                           const QList<int>& ranks,
                           const QSet<qint64>& directedPairs,
                           const FsmLayoutOptions& options)
{
    if (edge.selfLoop)
        return false;
    const bool reciprocalPair =
        directedPairs.contains(directedKey(edge.to, edge.from));
    const int originalSpan = qAbs(ranks.at(edge.to) - ranks.at(edge.from));
    if (reciprocalPair && originalSpan <= 1)
        return false;
    if (edge.reversed)
        return originalSpan >= options.aliasBackEdgeRankThreshold;
    const int forwardSpan = ranks.at(edge.to) - ranks.at(edge.from);
    return forwardSpan >= options.aliasForwardEdgeRankThreshold;
}

qreal minimumGap(const WorkVertex& lhs,
                 const WorkVertex& rhs,
                 const FsmLayoutOptions& options)
{
    return itemWidth(lhs, options) / 2.0
        + options.nodeSpacing
        + itemWidth(rhs, options) / 2.0;
}

void assignLayerCoordinates(QList<WorkVertex>& vertices,
                            const QList<QList<int>>& layers,
                            const FsmLayoutOptions& options)
{
    for (const QList<int>& layer : layers) {
        qreal width = 0.0;
        for (int i = 0; i < layer.size(); ++i) {
            width += itemWidth(vertices.at(layer.at(i)), options);
            if (i + 1 < layer.size())
                width += options.nodeSpacing;
        }
        qreal cursor = -width / 2.0;
        for (int order = 0; order < layer.size(); ++order) {
            WorkVertex& vertex = vertices[layer.at(order)];
            const qreal widthForVertex = itemWidth(vertex, options);
            vertex.x = cursor + widthForVertex / 2.0;
            vertex.y = vertex.rank * (options.nodeHeight + options.layerSpacing);
            cursor += widthForVertex + options.nodeSpacing;
        }
    }
}

void enforceLayerSpacing(QList<WorkVertex>& vertices,
                         const QList<int>& layer,
                         const FsmLayoutOptions& options)
{
    if (layer.size() < 2)
        return;
    for (int i = 1; i < layer.size(); ++i) {
        const int previousId = layer.at(i - 1);
        const int currentId = layer.at(i);
        const qreal minX = vertices.at(previousId).x
            + minimumGap(vertices.at(previousId),
                         vertices.at(currentId),
                         options);
        if (vertices.at(currentId).x < minX)
            vertices[currentId].x = minX;
    }
    for (int i = layer.size() - 2; i >= 0; --i) {
        const int currentId = layer.at(i);
        const int nextId = layer.at(i + 1);
        const qreal maxX = vertices.at(nextId).x
            - minimumGap(vertices.at(currentId),
                         vertices.at(nextId),
                         options);
        if (vertices.at(currentId).x > maxX)
            vertices[currentId].x = maxX;
    }
}

void seedDummyChains(QList<WorkVertex>& vertices,
                     const QList<WorkEdge>& workEdges)
{
    for (const WorkEdge& edge : workEdges) {
        if (edge.selfLoop || edge.vertexPath.size() < 3)
            continue;
        const qreal firstX = vertices.at(edge.vertexPath.first()).x;
        const qreal lastX = vertices.at(edge.vertexPath.last()).x;
        const int span = edge.vertexPath.size() - 1;
        for (int i = 1; i + 1 < edge.vertexPath.size(); ++i) {
            WorkVertex& vertex = vertices[edge.vertexPath.at(i)];
            if (!vertex.dummy)
                continue;
            const qreal t = static_cast<qreal>(i)
                / static_cast<qreal>(span);
            vertex.x = firstX + (lastX - firstX) * t;
        }
    }
}

qreal medianAdjacentX(int vertexId,
                      const QList<WorkVertex>& vertices,
                      const QHash<int, QList<int>>& predecessors,
                      const QHash<int, QList<int>>& successors,
                      const QHash<int, QList<int>>& attractionByVertex)
{
    QList<qreal> values;
    for (int predecessor : predecessors.value(vertexId))
        values.append(vertices.at(predecessor).x);
    for (int successor : successors.value(vertexId))
        values.append(vertices.at(successor).x);
    for (int partner : attractionByVertex.value(vertexId)) {
        values.append(vertices.at(partner).x);
        values.append(vertices.at(partner).x);
    }
    if (values.isEmpty())
        return vertices.at(vertexId).x;
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    if (values.size() % 2 == 1)
        return values.at(mid);
    return (values.at(mid - 1) + values.at(mid)) / 2.0;
}

void straightenCoordinates(QList<WorkVertex>& vertices,
                           const QList<QList<int>>& layers,
                           const QList<WorkEdge>& workEdges,
                           const QHash<int, QList<int>>& predecessors,
                           const QHash<int, QList<int>>& successors,
                           const QHash<int, QList<int>>& attractionByVertex,
                           const FsmLayoutOptions& options)
{
    seedDummyChains(vertices, workEdges);
    for (const QList<int>& layer : layers)
        enforceLayerSpacing(vertices, layer, options);

    for (int iteration = 0; iteration < 10; ++iteration) {
        QList<qreal> desired(vertices.size(), 0.0);
        for (const WorkVertex& vertex : vertices)
            desired[vertex.id] = medianAdjacentX(vertex.id,
                                                 vertices,
                                                 predecessors,
                                                 successors,
                                                 attractionByVertex);
        for (const QList<int>& layer : layers) {
            for (int vertexId : layer) {
                WorkVertex& vertex = vertices[vertexId];
                const qreal weight = vertex.dummy ? 0.90 : 0.35;
                vertex.x = vertex.x * (1.0 - weight)
                    + desired.at(vertexId) * weight;
            }
            enforceLayerSpacing(vertices, layer, options);
        }
        seedDummyChains(vertices, workEdges);
        for (const QList<int>& layer : layers)
            enforceLayerSpacing(vertices, layer, options);
    }
}

void snapCoordinatesToGrid(QList<WorkVertex>& vertices,
                           const QList<QList<int>>& layers,
                           const FsmLayoutOptions& options)
{
    const qreal grid = qMax<qreal>(1.0, options.gridStep);
    for (const QList<int>& layer : layers) {
        for (int vertexId : layer) {
            WorkVertex& vertex = vertices[vertexId];
            vertex.x = std::round(vertex.x / grid) * grid;
            vertex.y = vertex.rank * (options.nodeHeight
                                      + options.layerSpacing);
        }
        enforceLayerSpacing(vertices, layer, options);
        if (layer.size() == 1 && !vertices.at(layer.first()).dummy)
            vertices[layer.first()].x = 0.0;
    }
}

void separateWeakComponents(QList<WorkVertex>& vertices,
                            const QList<WorkEdge>& workEdges,
                            int initialVertexId,
                            const FsmLayoutOptions& options)
{
    QHash<int, QList<int>> adjacent;
    for (const WorkEdge& edge : workEdges) {
        for (int i = 1; i < edge.vertexPath.size(); ++i) {
            const int lhs = edge.vertexPath.at(i - 1);
            const int rhs = edge.vertexPath.at(i);
            adjacent[lhs].append(rhs);
            adjacent[rhs].append(lhs);
        }
    }

    QList<QList<int>> components;
    QSet<int> visited;
    for (const WorkVertex& seed : vertices) {
        if (visited.contains(seed.id))
            continue;
        QList<int> component;
        QList<int> pending{seed.id};
        visited.insert(seed.id);
        while (!pending.isEmpty()) {
            const int vertexId = pending.takeLast();
            component.append(vertexId);
            for (int neighbor : adjacent.value(vertexId)) {
                if (visited.contains(neighbor))
                    continue;
                visited.insert(neighbor);
                pending.append(neighbor);
            }
        }
        components.append(component);
    }
    if (components.size() <= 1)
        return;

    std::stable_sort(components.begin(),
                     components.end(),
                     [initialVertexId](const QList<int>& lhs,
                                       const QList<int>& rhs) {
        const bool lhsInitial = lhs.contains(initialVertexId);
        const bool rhsInitial = rhs.contains(initialVertexId);
        if (lhsInitial != rhsInitial)
            return lhsInitial;
        return *std::min_element(lhs.begin(), lhs.end())
            < *std::min_element(rhs.begin(), rhs.end());
    });

    const qreal grid = qMax<qreal>(1.0, options.gridStep);
    const qreal componentSpacing = qMax(options.nodeSpacing * 2.0,
                                        options.maxNodeWidth / 2.0);
    qreal cursor = 0.0;
    for (const QList<int>& component : components) {
        qreal minimum = std::numeric_limits<qreal>::max();
        qreal maximum = -std::numeric_limits<qreal>::max();
        for (int vertexId : component) {
            const WorkVertex& vertex = vertices.at(vertexId);
            const qreal halfWidth = itemWidth(vertex, options) / 2.0;
            minimum = qMin(minimum, vertex.x - halfWidth);
            maximum = qMax(maximum, vertex.x + halfWidth);
        }
        const qreal shift = std::round((cursor - minimum) / grid) * grid;
        for (int vertexId : component)
            vertices[vertexId].x += shift;
        cursor += maximum - minimum + componentSpacing;
    }

    qreal globalMinimum = std::numeric_limits<qreal>::max();
    qreal globalMaximum = -std::numeric_limits<qreal>::max();
    for (const WorkVertex& vertex : vertices) {
        const qreal halfWidth = itemWidth(vertex, options) / 2.0;
        globalMinimum = qMin(globalMinimum, vertex.x - halfWidth);
        globalMaximum = qMax(globalMaximum, vertex.x + halfWidth);
    }
    const qreal centerShift = std::round(
        ((globalMinimum + globalMaximum) / 2.0) / grid) * grid;
    for (WorkVertex& vertex : vertices)
        vertex.x -= centerShift;
}

qreal boundedPortOffset(const QRectF& rect,
                        qreal desiredMinor,
                        qreal laneOffset,
                        FsmLayoutDirection direction)
{
    const qreal rectMinorCenter = minorCoord(rect.center(), direction);
    const qreal halfSpan = (direction == FsmLayoutDirection::LeftRight
            ? rect.height()
            : rect.width()) / 2.0
        - 18.0;
    return qBound(-halfSpan,
                  desiredMinor - rectMinorCenter + laneOffset,
                  halfSpan);
}

QPointF quadraticControlPoint(const QPointF& start,
                              const QPointF& end,
                              qreal bend)
{
    const QPointF mid = (start + end) / 2.0;
    const QPointF normal = segmentNormal(start, end);
    return QPointF(mid.x() + normal.x() * bend,
                   mid.y() + normal.y() * bend);
}

QPointF quadraticPoint(const QPointF& start,
                       const QPointF& control,
                       const QPointF& end,
                       qreal t)
{
    const qreal u = 1.0 - t;
    return start * (u * u)
        + control * (2.0 * u * t)
        + end * (t * t);
}

bool quadraticRouteCrossesNode(
    const QPointF& start,
    const QPointF& control,
    const QPointF& end,
    int fromNodeId,
    int toNodeId,
    const QHash<int, QRectF>& rectByVertexId)
{
    constexpr int kSamples = 64;
    QPointF previous = start;
    for (int i = 1; i <= kSamples; ++i) {
        const QPointF point = quadraticPoint(
            start,
            control,
            end,
            static_cast<qreal>(i) / kSamples);
        for (auto it = rectByVertexId.cbegin();
             it != rectByVertexId.cend();
             ++it) {
            if (it.key() == fromNodeId || it.key() == toNodeId)
                continue;
            if (segmentIntersectsRect(previous,
                                      point,
                                      it.value().adjusted(-3.0,
                                                          -3.0,
                                                          3.0,
                                                          3.0))) {
                return true;
            }
        }
        previous = point;
    }
    return false;
}

bool quadraticRouteCrossesIndependentEdge(
    const QPointF& start,
    const QPointF& control,
    const QPointF& end,
    int fromNodeId,
    int toNodeId,
    const QList<FsmLayoutEdge>& existingEdges)
{
    constexpr int kSamples = 64;
    QList<QPointF> candidatePoints;
    candidatePoints.reserve(kSamples + 1);
    for (int i = 0; i <= kSamples; ++i) {
        candidatePoints.append(quadraticPoint(
            start,
            control,
            end,
            static_cast<qreal>(i) / kSamples));
    }
    for (const FsmLayoutEdge& edge : existingEdges) {
        if (edge.selfLoop
            || edge.fromNodeId == fromNodeId
            || edge.fromNodeId == toNodeId
            || edge.toNodeId == fromNodeId
            || edge.toNodeId == toNodeId) {
            continue;
        }
        const QList<QPointF> edgePoints = renderedEdgePolyline(edge);
        for (int i = 1; i < candidatePoints.size(); ++i) {
            for (int j = 1; j < edgePoints.size(); ++j) {
                const QPointF lhsStart = candidatePoints.at(i - 1);
                const QPointF lhsEnd = candidatePoints.at(i);
                const QPointF rhsStart = edgePoints.at(j - 1);
                const QPointF rhsEnd = edgePoints.at(j);
                const bool sharedSamplePoint =
                    QLineF(lhsStart, rhsStart).length() < 2.0
                    || QLineF(lhsStart, rhsEnd).length() < 2.0
                    || QLineF(lhsEnd, rhsStart).length() < 2.0
                    || QLineF(lhsEnd, rhsEnd).length() < 2.0;
                if (!sharedSamplePoint
                    && segmentsIntersect(lhsStart,
                                         lhsEnd,
                                         rhsStart,
                                         rhsEnd)) {
                    return true;
                }
            }
        }
    }
    return false;
}

QList<QPointF> renderedEdgePolyline(const FsmLayoutEdge& edge)
{
    constexpr int kSamples = 48;
    if (edge.selfLoop && edge.points.size() == 4) {
        QList<QPointF> sampled;
        const QPointF p0 = edge.points.at(0);
        const QPointF p1 = edge.points.at(1);
        const QPointF p2 = edge.points.at(2);
        const QPointF p3 = edge.points.at(3);
        for (int i = 0; i <= kSamples; ++i) {
            const qreal t = static_cast<qreal>(i) / kSamples;
            const qreal u = 1.0 - t;
            sampled.append(p0 * (u * u * u)
                           + p1 * (3.0 * u * u * t)
                           + p2 * (3.0 * u * t * t)
                           + p3 * (t * t * t));
        }
        return sampled;
    }
    if (edge.curved && edge.points.size() == 3) {
        QList<QPointF> sampled;
        for (int i = 0; i <= kSamples; ++i) {
            sampled.append(quadraticPoint(
                edge.points.at(0),
                edge.points.at(1),
                edge.points.at(2),
                static_cast<qreal>(i) / kSamples));
        }
        return sampled;
    }
    return edge.points;
}

qreal portHalfSpan(const QRectF& rect, FsmLayoutDirection direction)
{
    return qMax<qreal>(0.0,
                       (direction == FsmLayoutDirection::LeftRight
                            ? rect.height()
                            : rect.width()) / 2.0 - 18.0);
}

QHash<int, qreal> assignPortOffsets(
    const QHash<int, QList<int>>& edgeIdsByVertex,
    const QHash<int, QRectF>& rectByVertexId,
    const QHash<int, int>& oppositeVertexByEdgeId,
    FsmLayoutDirection direction,
    qreal preferredSpacing)
{
    QHash<int, qreal> result;
    for (auto it = edgeIdsByVertex.cbegin(); it != edgeIdsByVertex.cend(); ++it) {
        const int vertexId = it.key();
        QList<int> edgeIds = it.value();
        std::stable_sort(edgeIds.begin(), edgeIds.end(), [&](int lhs, int rhs) {
            const int lhsOpposite = oppositeVertexByEdgeId.value(lhs, -1);
            const int rhsOpposite = oppositeVertexByEdgeId.value(rhs, -1);
            const qreal lhsMinor =
                rectByVertexId.contains(lhsOpposite)
                    ? minorCoord(rectByVertexId.value(lhsOpposite).center(),
                                 direction)
                    : 0.0;
            const qreal rhsMinor =
                rectByVertexId.contains(rhsOpposite)
                    ? minorCoord(rectByVertexId.value(rhsOpposite).center(),
                                 direction)
                    : 0.0;
            if (!qFuzzyCompare(lhsMinor + 1.0, rhsMinor + 1.0))
                return lhsMinor < rhsMinor;
            return lhs < rhs;
        });
        const qreal halfSpan =
            portHalfSpan(rectByVertexId.value(vertexId), direction);
        const qreal spacing = edgeIds.size() > 1
            ? qMin(preferredSpacing,
                   (halfSpan * 2.0) / static_cast<qreal>(edgeIds.size() - 1))
            : 0.0;
        for (int i = 0; i < edgeIds.size(); ++i) {
            const qreal centered =
                (static_cast<qreal>(i)
                 - static_cast<qreal>(edgeIds.size() - 1) / 2.0)
                * spacing;
            result.insert(edgeIds.at(i),
                          qBound(-halfSpan, centered, halfSpan));
        }
    }
    return result;
}

void appendOrthogonalSegment(QList<QPointF>& points,
                             const QPointF& start,
                             const QPointF& end,
                             const FsmLayoutOptions& options,
                             qreal majorLaneOffset)
{
    const qreal startMajor = majorCoord(start, options.direction);
    const qreal endMajor = majorCoord(end, options.direction);
    const qreal startMinor = minorCoord(start, options.direction);
    const qreal endMinor = minorCoord(end, options.direction);
    appendPoint(points, start);
    if (qAbs(startMinor - endMinor) <= options.orthogonalStraightThreshold) {
        appendPoint(points, end);
        return;
    }
    const qreal midMajor = (startMajor + endMajor) / 2.0 + majorLaneOffset;
    appendPoint(points,
                pointFromMajorMinor(midMajor, startMinor, options.direction));
    appendPoint(points,
                pointFromMajorMinor(midMajor, endMinor, options.direction));
    appendPoint(points, end);
}

struct ChannelTrack {
    qreal major = 0.0;
    qreal minorStart = 0.0;
    qreal minorEnd = 0.0;
};

void separateChannelTracks(QList<QPointF>& points,
                           QList<ChannelTrack>& occupiedTracks,
                           const FsmLayoutOptions& options)
{
    constexpr qreal kTrackTolerance = 1.0;
    const qreal step = qMax<qreal>(6.0, options.labelSlideStep / 2.0);
    for (int i = 1; i < points.size(); ++i) {
        const QPointF start = points.at(i - 1);
        const QPointF end = points.at(i);
        const qreal startMajor = majorCoord(start, options.direction);
        const qreal endMajor = majorCoord(end, options.direction);
        const qreal startMinor = minorCoord(start, options.direction);
        const qreal endMinor = minorCoord(end, options.direction);
        if (qAbs(startMajor - endMajor) > kTrackTolerance
            || qAbs(startMinor - endMinor) < 2.0) {
            continue;
        }

        qreal lowMajor = -std::numeric_limits<qreal>::max();
        qreal highMajor = std::numeric_limits<qreal>::max();
        if (i >= 2 && i + 1 < points.size()) {
            const qreal previousMajor =
                majorCoord(points.at(i - 2), options.direction);
            const qreal nextMajor =
                majorCoord(points.at(i + 1), options.direction);
            lowMajor = qMin(previousMajor, nextMajor) + 4.0;
            highMajor = qMax(previousMajor, nextMajor) - 4.0;
        }
        if (lowMajor > highMajor) {
            lowMajor = qMin(startMajor, endMajor);
            highMajor = qMax(startMajor, endMajor);
        }

        const qreal intervalStart = qMin(startMinor, endMinor);
        const qreal intervalEnd = qMax(startMinor, endMinor);
        qreal selectedMajor = startMajor;
        const QList<int> offsets{0, -1, 1, -2, 2, -3, 3, -4, 4,
                                 -5, 5, -6, 6, -7, 7, -8, 8};
        for (int offset : offsets) {
            const qreal candidate = qBound(lowMajor,
                                           startMajor + offset * step,
                                           highMajor);
            bool conflict = false;
            for (const ChannelTrack& track : std::as_const(occupiedTracks)) {
                if (qAbs(track.major - candidate) > kTrackTolerance)
                    continue;
                if (qMin(intervalEnd, track.minorEnd)
                        - qMax(intervalStart, track.minorStart)
                    > 1.0) {
                    conflict = true;
                    break;
                }
            }
            if (!conflict) {
                selectedMajor = candidate;
                break;
            }
        }

        points[i - 1] = pointFromMajorMinor(selectedMajor,
                                             startMinor,
                                             options.direction);
        points[i] = pointFromMajorMinor(selectedMajor,
                                         endMinor,
                                         options.direction);
        occupiedTracks.append(ChannelTrack{selectedMajor,
                                           intervalStart,
                                           intervalEnd});
    }
}

bool segmentCrossesNonEndpointNode(const QPointF& start,
                                   const QPointF& end,
                                   int fromNodeId,
                                   int toNodeId,
                                   const QHash<int, QRectF>& rectByStateIndex)
{
    for (auto it = rectByStateIndex.cbegin(); it != rectByStateIndex.cend(); ++it) {
        if (it.key() == fromNodeId || it.key() == toNodeId)
            continue;
        const QRectF expanded = it.value().adjusted(-3.0,
                                                    -3.0,
                                                    3.0,
                                                    3.0);
        if (segmentIntersectsRect(start, end, expanded))
            return true;
    }
    return false;
}

bool pointListCrossesNonEndpointNode(const QList<QPointF>& points,
                                     int fromNodeId,
                                     int toNodeId,
                                     const QHash<int, QRectF>& rectByStateIndex)
{
    for (int i = 1; i < points.size(); ++i) {
        if (segmentCrossesNonEndpointNode(points.at(i - 1),
                                          points.at(i),
                                          fromNodeId,
                                          toNodeId,
                                          rectByStateIndex)) {
            return true;
        }
    }
    return false;
}

int independentPolylineCrossingCount(const QList<FsmLayoutEdge>& edges,
                                     QHash<int, int>* crossingsByTransition)
{
    int crossingCount = 0;
    if (crossingsByTransition)
        crossingsByTransition->clear();
    for (int i = 0; i < edges.size(); ++i) {
        const FsmLayoutEdge& lhs = edges.at(i);
        if (lhs.selfLoop || lhs.curved)
            continue;
        for (int j = i + 1; j < edges.size(); ++j) {
            const FsmLayoutEdge& rhs = edges.at(j);
            if (rhs.selfLoop || rhs.curved)
                continue;
            if (lhs.fromNodeId == rhs.fromNodeId
                || lhs.toNodeId == rhs.toNodeId) {
                continue;
            }
            for (int a = 1; a < lhs.points.size(); ++a) {
                for (int b = 1; b < rhs.points.size(); ++b) {
                    const QPointF lhsStart = lhs.points.at(a - 1);
                    const QPointF lhsEnd = lhs.points.at(a);
                    const QPointF rhsStart = rhs.points.at(b - 1);
                    const QPointF rhsEnd = rhs.points.at(b);
                    const bool sharedEndpoint =
                        QLineF(lhsStart, rhsStart).length() < 4.0
                        || QLineF(lhsStart, rhsEnd).length() < 4.0
                        || QLineF(lhsEnd, rhsStart).length() < 4.0
                        || QLineF(lhsEnd, rhsEnd).length() < 4.0;
                    if (sharedEndpoint)
                        continue;
                    if (!segmentsIntersect(lhsStart,
                                           lhsEnd,
                                           rhsStart,
                                           rhsEnd)) {
                        continue;
                    }
                    ++crossingCount;
                    if (crossingsByTransition) {
                        (*crossingsByTransition)[lhs.transitionIndex] =
                            crossingsByTransition->value(lhs.transitionIndex)
                            + 1;
                        (*crossingsByTransition)[rhs.transitionIndex] =
                            crossingsByTransition->value(rhs.transitionIndex)
                            + 1;
                    }
                }
            }
        }
    }
    return crossingCount;
}

int independentRenderedCrossingCount(const QList<FsmLayoutEdge>& edges)
{
    int crossingCount = 0;
    for (int i = 0; i < edges.size(); ++i) {
        const FsmLayoutEdge& lhs = edges.at(i);
        if (lhs.selfLoop)
            continue;
        const QList<QPointF> lhsPoints = renderedEdgePolyline(lhs);
        for (int j = i + 1; j < edges.size(); ++j) {
            const FsmLayoutEdge& rhs = edges.at(j);
            if (rhs.selfLoop)
                continue;
            if (lhs.fromNodeId == rhs.fromNodeId
                || lhs.fromNodeId == rhs.toNodeId
                || lhs.toNodeId == rhs.fromNodeId
                || lhs.toNodeId == rhs.toNodeId) {
                continue;
            }
            const QList<QPointF> rhsPoints = renderedEdgePolyline(rhs);
            bool pairCrosses = false;
            for (int a = 1; a < lhsPoints.size() && !pairCrosses; ++a) {
                for (int b = 1; b < rhsPoints.size(); ++b) {
                    const QPointF lhsStart = lhsPoints.at(a - 1);
                    const QPointF lhsEnd = lhsPoints.at(a);
                    const QPointF rhsStart = rhsPoints.at(b - 1);
                    const QPointF rhsEnd = rhsPoints.at(b);
                    const bool sharedSamplePoint =
                        QLineF(lhsStart, rhsStart).length() < 2.0
                        || QLineF(lhsStart, rhsEnd).length() < 2.0
                        || QLineF(lhsEnd, rhsStart).length() < 2.0
                        || QLineF(lhsEnd, rhsEnd).length() < 2.0;
                    if (!sharedSamplePoint
                        && segmentsIntersect(lhsStart,
                                             lhsEnd,
                                             rhsStart,
                                             rhsEnd)) {
                        pairCrosses = true;
                        break;
                    }
                }
            }
            crossingCount += pairCrosses ? 1 : 0;
        }
    }
    return crossingCount;
}

QList<QPointF> removeRedundantPolylinePoints(const QList<QPointF>& points)
{
    QList<QPointF> result;
    for (const QPointF& point : points)
        appendPoint(result, point);
    bool changed = true;
    while (changed && result.size() > 2) {
        changed = false;
        for (int i = 1; i + 1 < result.size(); ++i) {
            const QPointF a = result.at(i - 1);
            const QPointF b = result.at(i);
            const QPointF c = result.at(i + 1);
            const qreal direct = QLineF(a, c).length();
            const qreal via = QLineF(a, b).length() + QLineF(b, c).length();
            if (qAbs(via - direct) < 1.0
                || qAbs(orientation(a, b, c)) < 1.0) {
                result.removeAt(i);
                changed = true;
                break;
            }
        }
    }
    return result;
}

QList<QPointF> simplifyLongEdgePoints(
    const QList<QPointF>& original,
    int fromNodeId,
    int toNodeId,
    const QHash<int, QRectF>& rectByStateIndex)
{
    QList<QPointF> points = removeRedundantPolylinePoints(original);
    if (points.size() <= 3)
        return points;

    const QList<QPointF> direct{points.first(), points.last()};
    if (!pointListCrossesNonEndpointNode(direct,
                                         fromNodeId,
                                         toNodeId,
                                         rectByStateIndex)) {
        return direct;
    }

    int bestPivot = -1;
    qreal bestLength = std::numeric_limits<qreal>::max();
    for (int i = 1; i + 1 < points.size(); ++i) {
        const QList<QPointF> candidate{points.first(),
                                       points.at(i),
                                       points.last()};
        if (pointListCrossesNonEndpointNode(candidate,
                                            fromNodeId,
                                            toNodeId,
                                            rectByStateIndex)) {
            continue;
        }
        const qreal length = QLineF(candidate.at(0), candidate.at(1)).length()
            + QLineF(candidate.at(1), candidate.at(2)).length();
        if (length < bestLength) {
            bestLength = length;
            bestPivot = i;
        }
    }
    if (bestPivot >= 0) {
        return {points.first(),
                points.at(bestPivot),
                points.last()};
    }
    return points;
}

} // namespace

FsmGraphLayout layoutFsmGraphPass(const FsmGraph& graph,
                                  const FsmLayoutOptions& options,
                                  const QSet<int>& forcedAliasTransitionIds)
{
    FsmGraphLayout layout;
    QList<FsmStateRow> stateRows = graph.stateRows;
    const int reportStateCount = stateRows.size();
    if (stateRows.isEmpty())
        return layout;

    QHash<QString, int> stateIndexByName;
    for (int i = 0; i < stateRows.size(); ++i)
        stateIndexByName.insert(stateRows.at(i).stateDisplayName, i);
    const auto ensureStateIndex = [&](const QString& stateName,
                                      const FsmTransitionRow& transition)
        -> int {
        if (stateIndexByName.contains(stateName))
            return stateIndexByName.value(stateName);
        FsmStateRow row;
        row.stateDisplayName = stateName;
        row.detailDisplayName = QStringLiteral("implicit transition endpoint");
        row.codeLink = transition.codeLink;
        const int index = stateRows.size();
        stateRows.append(row);
        stateIndexByName.insert(stateName, index);
        return index;
    };

    QList<WorkEdge> workEdges;
    workEdges.reserve(graph.transitionRows.size());
    for (int i = 0; i < graph.transitionRows.size(); ++i) {
        const FsmTransitionRow& transition = graph.transitionRows.at(i);
        const int from = ensureStateIndex(transition.fromStateDisplayName,
                                          transition);
        const int to = ensureStateIndex(transition.toStateDisplayName,
                                        transition);
        WorkEdge edge;
        edge.edgeId = workEdges.size();
        edge.transitionIndex = i;
        edge.from = from;
        edge.to = to;
        edge.layoutFrom = from;
        edge.layoutTo = to;
        edge.selfLoop = from == to;
        workEdges.append(edge);
    }
    const int stateCount = stateRows.size();
    QSet<qint64> directedPairs;
    for (const WorkEdge& edge : workEdges) {
        if (!edge.selfLoop)
            directedPairs.insert(directedKey(edge.from, edge.to));
    }

    const int initialState = chooseInitialState(stateRows,
                                               workEdges,
                                               stateCount);
    const QSet<int> reversedEdgeIds =
        findDfsBackEdges(workEdges, stateCount, initialState);
    for (int i = 0; i < workEdges.size(); ++i) {
        WorkEdge& edge = workEdges[i];
        if (edge.selfLoop || !reversedEdgeIds.contains(i))
            continue;
        edge.reversed = true;
        edge.layoutFrom = edge.to;
        edge.layoutTo = edge.from;
    }

    const QList<int> ranks = assignRanks(workEdges, stateCount);
    int maxRank = 0;
    for (int rank : ranks)
        maxRank = qMax(maxRank, rank);

    QList<WorkVertex> vertices;
    vertices.reserve(stateCount + graph.transitionRows.size());
    QList<int> stateVertexIds(stateCount, -1);
    for (int i = 0; i < stateCount; ++i) {
        WorkVertex vertex;
        vertex.id = vertices.size();
        vertex.stateIndex = i;
        vertex.canonicalStateIndex = i < reportStateCount
            ? i
            : qBound(0, initialState, qMax(0, reportStateCount - 1));
        vertex.rank = ranks.at(i);
        vertex.dummy = false;
        vertex.alias = i >= reportStateCount;
        stateVertexIds[i] = vertex.id;
        vertices.append(vertex);
    }

    QHash<qint64, int> aliasVertexByPair;
    for (WorkEdge& edge : workEdges) {
        const bool regularAlias =
            shouldUseAliasForEdge(edge, ranks, directedPairs, options);
        const int originalSpan = qAbs(ranks.at(edge.to) - ranks.at(edge.from));
        const bool reciprocalAdjacent =
            directedPairs.contains(directedKey(edge.to, edge.from))
            && originalSpan <= 1;
        const bool forcedAlias =
            forcedAliasTransitionIds.contains(edge.transitionIndex)
            && !edge.selfLoop
            && !reciprocalAdjacent;
        if (!regularAlias && !forcedAlias)
            continue;
        const qint64 aliasKey = directedKey(edge.from, edge.to);
        int aliasVertexId = aliasVertexByPair.value(aliasKey, -1);
        if (aliasVertexId < 0) {
            WorkVertex alias;
            alias.id = vertices.size();
            alias.stateIndex = edge.to;
            alias.canonicalStateIndex = edge.to;
            alias.rank = ranks.at(edge.from) + 1;
            alias.alias = true;
            vertices.append(alias);
            aliasVertexId = alias.id;
            aliasVertexByPair.insert(aliasKey, aliasVertexId);
            maxRank = qMax(maxRank, alias.rank);
        }
        edge.usesAlias = true;
        edge.fallbackAlias = forcedAlias && !regularAlias;
        edge.layoutFrom = edge.from;
        edge.layoutTo = edge.to;
        edge.layoutToVertexId = aliasVertexId;
    }

    for (WorkEdge& edge : workEdges) {
        if (edge.selfLoop) {
            edge.vertexPath = {stateVertexIds.at(edge.from),
                               stateVertexIds.at(edge.to)};
            continue;
        }
        if (edge.usesAlias) {
            edge.vertexPath = {stateVertexIds.at(edge.from),
                               edge.layoutToVertexId};
            continue;
        }
        edge.vertexPath.clear();
        edge.vertexPath.append(stateVertexIds.at(edge.layoutFrom));
        const int fromRank = ranks.at(edge.layoutFrom);
        const int toRank = ranks.at(edge.layoutTo);
        for (int rank = fromRank + 1; rank < toRank; ++rank) {
            WorkVertex dummy;
            dummy.id = vertices.size();
            dummy.rank = rank;
            dummy.dummy = true;
            vertices.append(dummy);
            edge.vertexPath.append(dummy.id);
            maxRank = qMax(maxRank, rank);
        }
        edge.vertexPath.append(stateVertexIds.at(edge.layoutTo));
    }

    assignVertexSizes(vertices,
                      stateRows,
                      graph.stateRegisterDisplayName,
                      options);

    QList<QList<int>> layers(maxRank + 1);
    for (const WorkVertex& vertex : vertices) {
        if (vertex.rank >= 0 && vertex.rank < layers.size())
            layers[vertex.rank].append(vertex.id);
    }

    QHash<int, QList<int>> predecessors;
    QHash<int, QList<int>> successors;
    for (const WorkEdge& edge : workEdges) {
        if (edge.selfLoop)
            continue;
        for (int i = 1; i < edge.vertexPath.size(); ++i) {
            const int from = edge.vertexPath.at(i - 1);
            const int to = edge.vertexPath.at(i);
            successors[from].append(to);
            predecessors[to].append(from);
        }
    }

    QHash<int, QList<int>> attractionByVertex;
    for (const WorkEdge& edge : workEdges) {
        if (edge.selfLoop
            || !directedPairs.contains(directedKey(edge.to, edge.from))) {
            continue;
        }
        const int fromVertex = stateVertexIds.at(edge.from);
        const int toVertex = stateVertexIds.at(edge.to);
        if (!attractionByVertex[fromVertex].contains(toVertex))
            attractionByVertex[fromVertex].append(toVertex);
        if (!attractionByVertex[toVertex].contains(fromVertex))
            attractionByVertex[toVertex].append(fromVertex);
    }

    QHash<int, int> orderByVertex;
    updateOrders(layers, orderByVertex);
    for (int iteration = 0; iteration < options.crossingSortIterations; ++iteration) {
        for (int rank = 1; rank < layers.size(); ++rank) {
            sortLayerByBarycenter(layers[rank],
                                  predecessors,
                                  orderByVertex,
                                  attractionByVertex);
            updateOrders(layers, orderByVertex);
        }
        for (int rank = layers.size() - 2; rank >= 0; --rank) {
            sortLayerByBarycenter(layers[rank],
                                  successors,
                                  orderByVertex,
                                  attractionByVertex);
            updateOrders(layers, orderByVertex);
        }
    }
    transposeLayerOrder(layers, successors);
    updateOrders(layers, orderByVertex);
    assignLayerCoordinates(vertices, layers, options);
    straightenCoordinates(vertices,
                          layers,
                          workEdges,
                          predecessors,
                          successors,
                          attractionByVertex,
                          options);
    snapCoordinatesToGrid(vertices, layers, options);
    separateWeakComponents(vertices,
                           workEdges,
                           stateVertexIds.value(initialState, -1),
                           options);

    QHash<int, QRectF> rectByVertexId;
    layout.nodes.reserve(vertices.size());
    for (const WorkVertex& vertex : vertices) {
        if (vertex.dummy)
            continue;
        const QPointF center =
            pointFromMajorMinor(vertex.y, vertex.x, options.direction);
        const QRectF rect =
            orientRect(center,
                       itemWidth(vertex, options),
                       itemHeight(vertex, options),
                       options.direction);
        rectByVertexId.insert(vertex.id, rect);

        const FsmStateRow& row = stateRows.at(vertex.stateIndex);
        FsmLayoutNode node;
        node.nodeId = vertex.id;
        node.stateName = row.stateDisplayName;
        node.displayName = row.stateDisplayName;
        node.detail = row.detailDisplayName;
        node.rect = rect;
        node.titleTextRect = QRectF(rect.left() + 10.0,
                                    rect.top() + 8.0,
                                    qMax<qreal>(0.0, rect.width() - 20.0),
                                    options.titleLineHeight);
        node.showDetail = !vertex.alias;
        if (node.showDetail) {
            node.detailTextRect =
                QRectF(rect.left() + 10.0,
                       rect.top() + 32.0,
                       qMax<qreal>(0.0, rect.width() - 20.0),
                       options.detailLineHeight);
        }
        node.rank = vertex.rank;
        node.order = orderByVertex.value(vertex.id);
        node.alias = vertex.alias;
        node.canonicalNodeId = stateVertexIds.at(vertex.canonicalStateIndex);
        node.deadEndState = row.deadEndState;
        node.codeLink = row.codeLink;
        layout.nodeBounds = uniteRects(layout.nodeBounds, rect);
        layout.sceneBounds = uniteRects(layout.sceneBounds, rect);
        layout.nodes.append(node);
    }

    QHash<qint64, int> parallelIndex;
    QHash<qint64, int> parallelCount;
    for (const WorkEdge& edge : workEdges) {
        if (edge.selfLoop)
            continue;
        const qint64 key = directedKey(edge.from, edge.to);
        parallelCount[key] = parallelCount.value(key) + 1;
    }
    QHash<int, int> sourceVertexByEdgeId;
    QHash<int, int> targetVertexByEdgeId;
    QHash<int, QList<int>> outgoingEdgesByVertex;
    QHash<int, QList<int>> incomingEdgesByVertex;
    QHash<int, int> targetVertexByOutgoingEdgeId;
    QHash<int, int> sourceVertexByIncomingEdgeId;
    for (const WorkEdge& edge : workEdges) {
        if (edge.selfLoop)
            continue;
        const int sourceVertexId = stateVertexIds.at(edge.from);
        const int targetVertexId = edge.usesAlias
            ? edge.layoutToVertexId
            : stateVertexIds.at(edge.to);
        sourceVertexByEdgeId.insert(edge.edgeId, sourceVertexId);
        targetVertexByEdgeId.insert(edge.edgeId, targetVertexId);
        outgoingEdgesByVertex[sourceVertexId].append(edge.edgeId);
        incomingEdgesByVertex[targetVertexId].append(edge.edgeId);
        targetVertexByOutgoingEdgeId.insert(edge.edgeId, targetVertexId);
        sourceVertexByIncomingEdgeId.insert(edge.edgeId, sourceVertexId);
    }
    const QHash<int, qreal> sourcePortOffsetByEdgeId =
        assignPortOffsets(outgoingEdgesByVertex,
                          rectByVertexId,
                          targetVertexByOutgoingEdgeId,
                          options.direction,
                          options.parallelEdgeSpacing);
    const QHash<int, qreal> targetPortOffsetByEdgeId =
        assignPortOffsets(incomingEdgesByVertex,
                          rectByVertexId,
                          sourceVertexByIncomingEdgeId,
                          options.direction,
                          options.parallelEdgeSpacing);
    QHash<qint64, QList<int>> edgeIdsByBand;
    for (const WorkEdge& edge : workEdges) {
        if (edge.selfLoop)
            continue;
        const int sourceRank =
            vertices.at(sourceVertexByEdgeId.value(edge.edgeId)).rank;
        const int targetRank =
            vertices.at(targetVertexByEdgeId.value(edge.edgeId)).rank;
        const qint64 key =
            (static_cast<qint64>(sourceRank) << 32)
            ^ static_cast<quint32>(targetRank);
        edgeIdsByBand[key].append(edge.edgeId);
    }
    QHash<int, qreal> majorLaneOffsetByEdgeId;
    for (auto it = edgeIdsByBand.cbegin(); it != edgeIdsByBand.cend(); ++it) {
        QList<int> edgeIds = it.value();
        std::stable_sort(edgeIds.begin(), edgeIds.end(), [&](int lhs, int rhs) {
            const QRectF lhsSource =
                rectByVertexId.value(sourceVertexByEdgeId.value(lhs));
            const QRectF lhsTarget =
                rectByVertexId.value(targetVertexByEdgeId.value(lhs));
            const QRectF rhsSource =
                rectByVertexId.value(sourceVertexByEdgeId.value(rhs));
            const QRectF rhsTarget =
                rectByVertexId.value(targetVertexByEdgeId.value(rhs));
            const qreal lhsSourceMinor =
                minorCoord(lhsSource.center(), options.direction)
                + sourcePortOffsetByEdgeId.value(lhs);
            const qreal lhsTargetMinor =
                minorCoord(lhsTarget.center(), options.direction)
                + targetPortOffsetByEdgeId.value(lhs);
            const qreal rhsSourceMinor =
                minorCoord(rhsSource.center(), options.direction)
                + sourcePortOffsetByEdgeId.value(rhs);
            const qreal rhsTargetMinor =
                minorCoord(rhsTarget.center(), options.direction)
                + targetPortOffsetByEdgeId.value(rhs);
            const qreal lhsDelta = lhsTargetMinor - lhsSourceMinor;
            const qreal rhsDelta = rhsTargetMinor - rhsSourceMinor;
            const int lhsDirection = lhsDelta < -options.orthogonalStraightThreshold
                ? -1
                : (lhsDelta > options.orthogonalStraightThreshold ? 1 : 0);
            const int rhsDirection = rhsDelta < -options.orthogonalStraightThreshold
                ? -1
                : (rhsDelta > options.orthogonalStraightThreshold ? 1 : 0);
            if (lhsDirection != rhsDirection)
                return lhsDirection < rhsDirection;
            const qreal lhsSpan = qAbs(lhsDelta);
            const qreal rhsSpan = qAbs(rhsDelta);
            if (!qFuzzyCompare(lhsSpan + 1.0, rhsSpan + 1.0))
                return lhsSpan > rhsSpan;
            const qreal lhsMin = qMin(lhsSourceMinor, lhsTargetMinor);
            const qreal rhsMin = qMin(rhsSourceMinor, rhsTargetMinor);
            if (!qFuzzyCompare(lhsMin + 1.0, rhsMin + 1.0))
                return lhsMin < rhsMin;
            return lhs < rhs;
        });
        const int count = qMax(1, edgeIds.size());
        const qreal spacing = qMax<qreal>(16.0, options.labelSlideStep);
        for (int index = 0; index < edgeIds.size(); ++index) {
            majorLaneOffsetByEdgeId.insert(
                edgeIds.at(index),
                (static_cast<qreal>(index)
                 - static_cast<qreal>(count - 1) / 2.0)
                    * spacing);
        }
    }

    QList<QRectF> blockedLabelRects;
    for (const FsmLayoutNode& node : layout.nodes) {
        blockedLabelRects.append(
            node.rect.adjusted(-options.labelClearance,
                               -options.labelClearance,
                               options.labelClearance,
                               options.labelClearance));
    }
    QList<QRectF> usedLabelRects;
    QList<ChannelTrack> occupiedTracks;

    QList<int> routingOrder;
    routingOrder.reserve(workEdges.size());
    for (int i = 0; i < workEdges.size(); ++i) {
        const WorkEdge& edge = workEdges.at(i);
        const bool reciprocal = !edge.selfLoop
            && directedPairs.contains(directedKey(edge.to, edge.from));
        if (!reciprocal)
            routingOrder.append(i);
    }
    for (int i = 0; i < workEdges.size(); ++i) {
        const WorkEdge& edge = workEdges.at(i);
        const bool reciprocal = !edge.selfLoop
            && directedPairs.contains(directedKey(edge.to, edge.from));
        if (reciprocal)
            routingOrder.append(i);
    }

    for (int workEdgeIndex : routingOrder) {
        const WorkEdge& workEdge = workEdges.at(workEdgeIndex);
        const FsmTransitionRow& transition =
            graph.transitionRows.at(workEdge.transitionIndex);
        const int sourceVertexId =
            sourceVertexByEdgeId.value(workEdge.edgeId,
                                       stateVertexIds.at(workEdge.from));
        const int targetVertexId =
            targetVertexByEdgeId.value(workEdge.edgeId,
                                       stateVertexIds.at(workEdge.to));
        FsmLayoutEdge edge;
        edge.edgeId = layout.edges.size();
        edge.transitionIndex = workEdge.transitionIndex;
        edge.fromNodeId = sourceVertexId;
        edge.toNodeId = targetVertexId;
        edge.fromState = transition.fromStateDisplayName;
        edge.toState = transition.toStateDisplayName;
        edge.condition = transition.conditionDisplayName;
        edge.label = conditionId(workEdge.transitionIndex);
        edge.codeLink = transition.codeLink;
        edge.selfLoop = workEdge.selfLoop;
        edge.reversedForLayout = workEdge.reversed;
        edge.layoutFromRank = vertices.at(sourceVertexId).rank;
        edge.layoutToRank = vertices.at(targetVertexId).rank;

        if (workEdge.selfLoop) {
            const QRectF rect = rectByVertexId.value(
                stateVertexIds.at(workEdge.from));
            if (options.direction == FsmLayoutDirection::LeftRight) {
                const QPointF start(rect.left(), rect.center().y() - 18.0);
                const QPointF c1(rect.left() - options.selfLoopHeight,
                                 rect.top() - 14.0);
                const QPointF c2(rect.left() - options.selfLoopHeight,
                                 rect.bottom() + 14.0);
                const QPointF end(rect.left(), rect.center().y() + 18.0);
                edge.points = {start, c1, c2, end};
            } else {
                const QPointF start(rect.center().x() - 22.0, rect.top());
                const QPointF c1(rect.center().x() - 32.0,
                                 rect.top() - options.selfLoopHeight);
                const QPointF c2(rect.center().x() + 32.0,
                                 rect.top() - options.selfLoopHeight);
                const QPointF end(rect.center().x() + 22.0, rect.top());
                edge.points = {start, c1, c2, end};
            }
            edge.arrowTip = edge.points.last();
            edge.arrowAngle = angleFromLastSegment(edge.points);
            for (const QPointF& point : edge.points) {
                layout.sceneBounds = uniteRects(layout.sceneBounds,
                                                QRectF(point, QSizeF(1, 1)));
            }
            layout.edges.append(edge);
            continue;
        }

        const qint64 parallelKey = directedKey(workEdge.from, workEdge.to);
        const int currentParallel = parallelIndex.value(parallelKey, 0);
        parallelIndex[parallelKey] = currentParallel + 1;
        const int totalParallel = qMax(1, parallelCount.value(parallelKey, 1));
        const qreal laneOffset =
            (static_cast<qreal>(currentParallel)
             - static_cast<qreal>(totalParallel - 1) / 2.0)
            * options.parallelEdgeSpacing;
        const bool reciprocalPair =
            directedPairs.contains(directedKey(workEdge.to, workEdge.from));

        QList<QPointF> points;
        if (reciprocalPair) {
            const QRectF fromRect = rectByVertexId.value(sourceVertexId);
            const QRectF toRect = rectByVertexId.value(targetVertexId);
            QPointF start;
            QPointF end;
            QPointF control;
            const qreal bendSign = workEdge.from < workEdge.to ? -1.0 : 1.0;
            const qreal minimumBend = 72.0 + qAbs(laneOffset) * 1.35;
            const qreal minorExtent = options.direction
                    == FsmLayoutDirection::LeftRight
                ? layout.nodeBounds.height()
                : layout.nodeBounds.width();
            const qreal maximumBend = qMax<qreal>(
                420.0,
                minorExtent * 3.0 + options.maxNodeWidth);
            const qreal sideSign = workEdge.from < workEdge.to ? -1.0 : 1.0;
            const QList<qreal> portMagnitudes{52.0, 36.0, 20.0, 0.0,
                                              -20.0, -36.0};
            bool routeFound = false;
            for (qreal portMagnitude : portMagnitudes) {
                const qreal pairSide = sideSign * portMagnitude;
                const qreal sourceOffset =
                    sourcePortOffsetByEdgeId.value(workEdge.edgeId)
                    + pairSide + laneOffset * 0.25;
                const qreal targetOffset =
                    targetPortOffsetByEdgeId.value(workEdge.edgeId)
                    + pairSide + laneOffset * 0.25;
                start = sourcePort(fromRect,
                                   sourceOffset,
                                   options.direction,
                                   true);
                end = targetPort(toRect,
                                 targetOffset,
                                 options.direction,
                                 true);
                for (qreal magnitude = minimumBend;
                     magnitude <= maximumBend;
                     magnitude += 24.0) {
                    const QPointF candidate = quadraticControlPoint(
                        start,
                        end,
                        bendSign * magnitude);
                    control = candidate;
                    if (quadraticRouteCrossesNode(start,
                                                  candidate,
                                                  end,
                                                  sourceVertexId,
                                                  targetVertexId,
                                                  rectByVertexId)
                        || quadraticRouteCrossesIndependentEdge(
                            start,
                            candidate,
                            end,
                            sourceVertexId,
                            targetVertexId,
                            layout.edges)) {
                        continue;
                    }
                    routeFound = true;
                    break;
                }
                if (routeFound)
                    break;
            }
            appendPoint(points, start);
            appendPoint(points, control);
            appendPoint(points, end);
            edge.curved = true;
        } else {
            QList<int> pathVertices = workEdge.vertexPath;
            if (workEdge.reversed && !workEdge.usesAlias)
                std::reverse(pathVertices.begin(), pathVertices.end());
            if (!pathVertices.isEmpty())
                pathVertices.first() = sourceVertexId;
            if (!pathVertices.isEmpty())
                pathVertices.last() = targetVertexId;

            for (int i = 1; i < pathVertices.size(); ++i) {
                const WorkVertex& fromVertex = vertices.at(pathVertices.at(i - 1));
                const WorkVertex& toVertex = vertices.at(pathVertices.at(i));
                QPointF start;
                QPointF end;
                if (i == 1) {
                    start = sourcePort(
                        rectByVertexId.value(sourceVertexId),
                        sourcePortOffsetByEdgeId.value(workEdge.edgeId),
                        options.direction,
                        true);
                } else {
                    start = pointFromMajorMinor(fromVertex.y,
                                                fromVertex.x,
                                                options.direction);
                }
                if (i + 1 == pathVertices.size()) {
                    end = targetPort(
                        rectByVertexId.value(targetVertexId),
                        targetPortOffsetByEdgeId.value(workEdge.edgeId),
                        options.direction,
                        true);
                } else {
                    end = pointFromMajorMinor(toVertex.y,
                                              toVertex.x,
                                              options.direction);
                }
                appendOrthogonalSegment(
                    points,
                    start,
                    end,
                    options,
                    majorLaneOffsetByEdgeId.value(workEdge.edgeId));
            }
            separateChannelTracks(points, occupiedTracks, options);
        }
        edge.points = points;
        edge.arrowTip = edge.points.isEmpty() ? QPointF() : edge.points.last();
        edge.arrowAngle = angleFromLastSegment(edge.points);

        for (const QPointF& point : edge.points)
            layout.sceneBounds = uniteRects(layout.sceneBounds,
                                            QRectF(point, QSizeF(1, 1)));
        layout.edges.append(edge);
    }
    layout.renderedCrossingCount =
        independentRenderedCrossingCount(layout.edges);

    QList<EdgeSegment> edgeSegments;
    for (const FsmLayoutEdge& edge : layout.edges) {
        const QList<QPointF> renderedPoints = renderedEdgePolyline(edge);
        for (int i = 1; i < renderedPoints.size(); ++i) {
            edgeSegments.append(EdgeSegment{edge.edgeId,
                                            renderedPoints.at(i - 1),
                                            renderedPoints.at(i)});
        }
    }
    const QSizeF labelSize(options.labelWidth, options.labelHeight);
    for (FsmLayoutEdge& edge : layout.edges) {
        const QList<QPointF> renderedPoints = renderedEdgePolyline(edge);
        if (edge.selfLoop) {
            edge.labelAnchor = chooseSelfLoopLabelAnchor(
                edge,
                rectByVertexId.value(edge.fromNodeId),
                labelSize,
                blockedLabelRects,
                usedLabelRects,
                edgeSegments,
                options);
        } else if (edge.curved && edge.points.size() == 3) {
            edge.labelAnchor = chooseCurvedEdgeLabelAnchor(
                edge,
                labelSize,
                blockedLabelRects,
                usedLabelRects,
                edgeSegments,
                options);
        } else {
            edge.labelAnchor =
                chooseLabelAnchor(renderedPoints,
                                  labelSize,
                                  blockedLabelRects,
                                  usedLabelRects,
                                  edgeSegments,
                                  options.labelClearance,
                                  options.direction,
                                  options.labelEdgeDistance,
                                  options.labelSlideStep,
                                  edge.edgeId);
        }
        edge.labelRect = labelRectAt(edge.labelAnchor, labelSize);
        layout.sceneBounds = uniteRects(layout.sceneBounds, edge.labelRect);
    }

    layout.nodeBounds =
        layout.nodeBounds.adjusted(-options.sceneMargin,
                                   -options.sceneMargin,
                                   options.sceneMargin,
                                   options.sceneMargin);
    layout.sceneBounds =
        layout.sceneBounds.adjusted(-options.sceneMargin,
                                    -options.sceneMargin,
                                    options.sceneMargin,
                                    options.sceneMargin);
    if (layout.sceneBounds.isNull())
        layout.sceneBounds = layout.nodeBounds;
    for (const WorkEdge& edge : workEdges) {
        if (edge.fallbackAlias)
            ++layout.fallbackAliasCount;
    }
    return layout;
}

FsmGraphLayout layoutFsmGraph(const FsmGraph& graph,
                              const FsmLayoutOptions& options)
{
    const QSet<int> noForcedAliases;
    FsmGraphLayout initial =
        layoutFsmGraphPass(graph, options, noForcedAliases);
    QHash<int, int> crossingsByTransition;
    independentPolylineCrossingCount(initial.edges, &crossingsByTransition);

    QList<int> candidates;
    for (auto it = crossingsByTransition.cbegin();
         it != crossingsByTransition.cend();
         ++it) {
        if (it.value() >= 2)
            candidates.append(it.key());
    }
    if (candidates.isEmpty())
        return initial;

    std::stable_sort(candidates.begin(), candidates.end(), [&](int lhs, int rhs) {
        const int lhsCount = crossingsByTransition.value(lhs);
        const int rhsCount = crossingsByTransition.value(rhs);
        if (lhsCount != rhsCount)
            return lhsCount > rhsCount;
        return lhs < rhs;
    });

    const int limit = qMax(
        0,
        static_cast<int>(
            std::ceil(static_cast<qreal>(graph.transitionRows.size())
                      * qMax(0, options.fallbackAliasPercentLimit)
                      / 100.0)));
    QSet<int> forcedAliases;
    for (int transitionIndex : candidates) {
        if (forcedAliases.size() >= limit)
            break;
        forcedAliases.insert(transitionIndex);
    }
    if (forcedAliases.isEmpty()) {
        initial.warnings.append(
            QStringLiteral("fallback alias skipped: limit is zero"));
        return initial;
    }

    FsmGraphLayout fallback =
        layoutFsmGraphPass(graph, options, forcedAliases);
    fallback.warnings.prepend(
        QStringLiteral("fallback alias requested %1 transition(s)")
            .arg(forcedAliases.size()));
    if (candidates.size() > forcedAliases.size()) {
        fallback.warnings.append(
            QStringLiteral("fallback alias limit reached: %1/%2 candidates")
                .arg(forcedAliases.size())
                .arg(candidates.size()));
    }
    return fallback;
}
