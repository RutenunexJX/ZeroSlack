#include "ElaFrameAnimation.h"

#include <QPointer>

ElaFrameAnimation::ElaFrameAnimation(QObject* parent)
    : QObject(parent), _timer(this)
{
    _timer.setObjectName("elaSidebarFrameTimer");
    _timer.setTimerType(Qt::PreciseTimer);
    _timer.setSingleShot(true);
    connect(&_timer, &QTimer::timeout, this, &ElaFrameAnimation::advance);
}

void ElaFrameAnimation::start()
{
    stop();
    _running = true;
    _elapsed.start();
    advance();
}

void ElaFrameAnimation::stop()
{
    ++_serial;
    _running = false;
    _timer.stop();
}

void ElaFrameAnimation::advance()
{
    if (!_running) return;
    const auto serial = _serial;
    const qreal progress = _duration ? qMin(qreal(1), _elapsed.nsecsElapsed() / (_duration * 1000000.0)) : 1;
    const bool complete = progress >= 1;
    if (complete) _running = false;
    const qreal value = complete ? _end : _start + (_end - _start) * _curve.valueForProgress(progress);
    QPointer<ElaFrameAnimation> guard(this);
    Q_EMIT valueChanged(value);
    if (!guard || serial != _serial) return;
    if (complete) Q_EMIT finished();
    else schedule();
}

void ElaFrameAnimation::schedule()
{
    // Round each next 120 Hz deadline up to timer milliseconds. Missed frames
    // are skipped using elapsed wall time, rather than extending the motion.
    const qint64 now = _elapsed.nsecsElapsed();
    const qint64 nextFrame = (now * 120 / 1000000000 + 1) * 1000000000 / 120;
    const qint64 deadline = qMin(qint64(_duration) * 1000000, nextFrame);
    _timer.start(int(qMax(qint64(1), (deadline - now + 999999) / 1000000)));
}
