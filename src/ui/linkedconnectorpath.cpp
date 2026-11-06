#include "linkedconnectorpath.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace LinkedConnectorPath {
namespace {
bool finite(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool valid(const QRectF& rect)
{
    return finite(rect.topLeft()) && finite(rect.bottomRight())
        && std::isfinite(rect.width()) && std::isfinite(rect.height())
        && rect.width() > 0 && rect.height() > 0;
}

bool entersInterior(const QPointF& from, const QPointF& to, const QRectF& rect)
{
    long double begin = 0, end = 1;
    const auto axis = [&begin, &end](qreal first, qreal last, qreal low, qreal high) {
        const long double delta = static_cast<long double>(last) - first;
        if (delta == 0) return first > low && first < high;
        auto a = (static_cast<long double>(low) - first) / delta;
        auto z = (static_cast<long double>(high) - first) / delta;
        if (a > z) std::swap(a, z);
        begin = std::max(begin, a);
        end = std::min(end, z);
        return begin < end;
    };
    return axis(from.x(), to.x(), rect.left(), rect.right())
        && axis(from.y(), to.y(), rect.top(), rect.bottom()) && begin < end;
}
} // namespace

bool staysOutside(const QPolygonF& points, const QRectF& central)
{
    if (!valid(central) || points.size() < 2) return false;
    for (const auto& point : points) if (!finite(point)) return false;
    for (qsizetype i = 1; i < points.size(); ++i)
        if (entersInterior(points[i - 1], points[i], central)) return false;
    return true;
}

Route build(const Input& input)
{
    Route result{input.id, {}, false, input.highlighted, Reason::InvalidGeometry};
    const auto reject = [&result](Reason reason) { result.reason = reason; return result; };
    if (input.id.isEmpty()) return reject(Reason::EmptyId);
    if (!input.positioned) return reject(Reason::NotPositioned);
    if (!input.codeVisible || !input.cardVisible) return reject(Reason::EndpointInvisible);
    if (!valid(input.central) || !valid(input.overlay) || !std::isfinite(input.codeY)
        || !finite(input.card) || (input.side != Side::Left && input.side != Side::Right))
        return reject(Reason::InvalidGeometry);
    const bool left = input.side == Side::Left;
    const QPointF code(left ? input.central.left() : input.central.right(), input.codeY);
    if (!input.overlay.contains(input.central) || !input.overlay.contains(code)
        || !input.overlay.contains(input.card) || input.codeY < input.central.top()
        || input.codeY > input.central.bottom()) return reject(Reason::OutsideOverlay);
    if (input.card.x() == code.x()) return reject(Reason::ZeroGap);
    if (left ? input.card.x() > code.x() : input.card.x() < code.x())
        return reject(Reason::WrongSide);
    const qreal middle = code.x() / 2 + input.card.x() / 2;
    const QPolygonF points{code, QPointF(middle, code.y()),
                          QPointF(middle, input.card.y()), input.card};
    if (!staysOutside(points, input.central)) return reject(Reason::InvalidGeometry);
    result.points = points;
    result.draw = true;
    result.reason = Reason::None;
    return result;
}

QList<Route> buildAll(const QList<Input>& inputs)
{
    QList<Route> result;
    result.reserve(inputs.size());
    QSet<QString> seen;
    for (const auto& input : inputs) {
        if (!input.id.isEmpty() && seen.contains(input.id)) {
            result.append({input.id, {}, false, input.highlighted, Reason::DuplicateId});
            continue;
        }
        seen.insert(input.id);
        result.append(build(input));
    }
    return result;
}
} // namespace LinkedConnectorPath
