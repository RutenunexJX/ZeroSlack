#ifndef ELA_FRAME_ANIMATION_H
#define ELA_FRAME_ANIMATION_H

#include <QEasingCurve>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

// Local clock for live sidebar widths. It does not replace Qt's animation driver.
class ElaFrameAnimation final : public QObject
{
    Q_OBJECT
public:
    explicit ElaFrameAnimation(QObject* parent = nullptr);
    void setStartValue(qreal value) { _start = value; }
    void setEndValue(qreal value) { _end = value; }
    void setDuration(int milliseconds) { _duration = qMax(0, milliseconds); }
    void setEasingCurve(const QEasingCurve& curve) { _curve = curve; }
    void start();
    void stop();
    bool isRunning() const { return _running; }

signals:
    void valueChanged(qreal value);
    void finished();

private:
    void advance();
    void schedule();
    QTimer _timer;
    QElapsedTimer _elapsed;
    QEasingCurve _curve;
    qreal _start = 0;
    qreal _end = 1;
    int _duration = 0;
    quint64 _serial = 0;
    bool _running = false;
};

#endif
