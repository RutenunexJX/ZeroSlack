#pragma once

#include "zeroslackexport.h"
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

namespace LinkedConnectorPath {

enum class Side { Left, Right };
enum class Reason {
    None, EmptyId, DuplicateId, NotPositioned, EndpointInvisible,
    InvalidGeometry, OutsideOverlay, WrongSide, ZeroGap
};

struct Input {
    QString id;
    Side side = Side::Left;
    qreal codeY = 0;
    QPointF card;
    QRectF central, overlay;
    bool positioned = false;
    bool codeVisible = false, cardVisible = false;
    bool highlighted = false;
};

struct Route {
    QString id;
    QPolygonF points;
    bool draw = false, highlighted = false;
    Reason reason = Reason::InvalidGeometry;
};

// Logical coordinates only. Boundary points are allowed; raster clipping is
// the overlay's responsibility, including the pen's antialiased footprint.
ZEROSLACK_API Route build(const Input& input);
ZEROSLACK_API QList<Route> buildAll(const QList<Input>& inputs);
ZEROSLACK_API bool staysOutside(const QPolygonF& points, const QRectF& central);

} // namespace LinkedConnectorPath
