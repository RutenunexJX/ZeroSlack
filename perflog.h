#ifndef PERFLOG_H
#define PERFLOG_H

// Lightweight main-thread profiling: a scoped timer that appends to %TEMP%/zeroslack_perf.log when
// the scope takes >= threshold ms. Used to find UI-thread hot spots (the GUI is a WIN32 app, so
// qDebug isn't visible). Remove the PERF_SCOPE calls once profiling is done.
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QDir>

class PerfScope {
public:
    explicit PerfScope(const char* label, qint64 thresholdMs = 3)
        : m_label(label), m_threshold(thresholdMs) { m_timer.start(); }
    ~PerfScope() {
        const qint64 ms = m_timer.elapsed();
        if (ms >= m_threshold) {
            QFile f(QDir::tempPath() + QStringLiteral("/zeroslack_perf.log"));
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream(&f) << m_label << "  " << ms << " ms\n";
            }
        }
    }
private:
    const char* m_label;
    qint64 m_threshold;
    QElapsedTimer m_timer;
};

#define PERF_SCOPE(label) PerfScope _perfScope_(label)

#endif // PERFLOG_H
