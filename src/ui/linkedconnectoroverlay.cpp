#include "linkedconnectoroverlay.h"
#include <QEvent>
#include <QPainter>
#include <QPainterPath>

LinkedConnectorOverlay::LinkedConnectorOverlay(QWidget* host) : QWidget(host)
{
    setObjectName("linkedConnectorOverlay");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    host->installEventFilter(this);
    setGeometry(host->rect());
}

void LinkedConnectorOverlay::setScene(const Scene& scene)
{
    m_scene = scene;
    QList<LinkedConnectorPath::Input> inputs;
    if (scene.valid) for (const auto& stroke : scene.strokes) inputs.append(stroke.input);
    m_routes = LinkedConnectorPath::buildAll(inputs);
    ++m_geometrySerial;
    update();
    emit geometryApplied(m_geometrySerial);
}

bool LinkedConnectorOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
            setGeometry(parentWidget()->rect());
            raise();
            emit presentationRequested();
            break;
        case QEvent::Hide:
            setScene({});
            break;
        default: break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LinkedConnectorOverlay::paintEvent(QPaintEvent*)
{
    Completion completed;
    completed.paintSerial = m_completion.paintSerial + 1;
    completed.geometrySerial = m_geometrySerial;
    completed.identity = m_scene.identity;
    completed.valid = m_scene.valid && m_scene.identity.document
        && m_scene.identity.document->revision() == m_scene.identity.documentRevision;
    if (completed.valid) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath allowed;
        allowed.setFillRule(Qt::OddEvenFill);
        allowed.addRect(QRectF(rect()));
        allowed.addRect(m_scene.central);
        painter.setClipPath(allowed);
        for (const auto& rect : m_scene.occlusions) {
            QPainterPath outside;
            outside.setFillRule(Qt::OddEvenFill);
            outside.addRect(QRectF(this->rect()));outside.addRect(rect);
            painter.setClipPath(outside, Qt::IntersectClip);
        }
        for (qsizetype i = 0; i < m_routes.size(); ++i) {
            const auto& route = m_routes[i];
            if (!route.draw) continue;
            painter.save();
            painter.setClipRect(m_scene.strokes[i].clip, Qt::IntersectClip);
            painter.setPen(QPen(QColor(52, 120, 212, route.highlighted ? 230 : 145), 1.5,
                Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            painter.drawPolyline(route.points);
            painter.restore();
            ++completed.paths;
        }
    }
    // Publish the identity captured for this paint, never a later request's identity.
    m_completion = completed;
    emit paintCompleted(completed.paintSerial, completed.geometrySerial);
}
