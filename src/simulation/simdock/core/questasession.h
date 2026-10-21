#pragma once
#include "zeroslackexport.h"
#include "model.h"
#include "wavepalette.h"
#include "sessionfiles.h"
#include "simulationevents.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <memory>
#include <atomic>

namespace simdock {
QString tclWord(const QString& text);
QString runScript(const QString& root, const Project& project, const QString& runDir, const QString& iniPath,
                  WaveTheme waveTheme = WaveTheme::QuestaDefault, bool reuseDesign = false, const QString& workDirectory = {}, bool interactive = true);
QString simulatorError(const QString& executable);
QString scoreboardResultScript(const QString& tbName);
struct PreparedRun {
    QString executable, root, error;
    Project project;
    QByteArray designFingerprint;
    QMap<QString, QByteArray> inputFingerprints;
};
PreparedRun prepareRun(const QString& executable, const QString& root, const Project&,
                       const std::atomic_bool* cancelled = nullptr,
                       const QMap<QString, QByteArray>& overrides = {});

class ZEROSLACK_API QuestaSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 processId READ processId)
public:
    explicit QuestaSession(QObject* parent = nullptr);
    ~QuestaSession() override;
    bool start(const QString& executable, const QString& root, const Project& project, QString* error, WaveTheme waveTheme = WaveTheme::QuestaDefault);
    bool start(const PreparedRun&, QString* error, WaveTheme waveTheme = WaveTheme::QuestaDefault);
    void stop();
    bool busy() const { return m_busy; }
    bool alive() const { return m_process.state() != QProcess::NotRunning; }
    qint64 processId() const { return m_process.processId(); }
    QString lastRunDirectory() const { return m_runDir; }
    void setWallTimeout(int ms) { m_wallTimeout = ms; }
    void setInteractive(bool enabled) { if (!alive()) m_interactive = enabled; }
signals:
    void logText(const QString& text);
    void stateChanged(const QString& state);
    void finished(bool completed, const QString& message);
    void eventRunStarted(const QString& runId);
    void eventsReady(const QList<simdock::SimulationEvent>& events);
    void eventRecordsRejected(quint64 count);
    void eventRunCleared();
private:
    bool launch(const QString& executable, QString* error);
    void poll();
    void finish(bool success, const QString& message);
    bool sendPending(QString* error);
    QProcess m_process;
    QTimer m_poll, m_deadline;
    void releaseFiles();
    std::unique_ptr<SessionFiles> m_sessionDir;
    QString m_executable, m_pending, m_runId, m_runDir, m_lastState;
    qint64 m_logOffset = 0;
    bool m_busy = false, m_ready = false, m_stopping = false;
    bool m_interactive = true;
    int m_wallTimeout = 120000;
    void* m_job = nullptr;
    QByteArray m_cachedDesign, m_pendingDesign;
    QString m_cachedWork, m_pendingWork;
    QString m_activeWork;
    bool m_oldRunsReleased = false;
    SimulationEventDecoder m_events;
};
}
