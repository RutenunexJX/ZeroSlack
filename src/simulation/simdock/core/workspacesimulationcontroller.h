#pragma once
#include "zeroslackexport.h"
#include "questasession.h"
#include "simulationtarget.h"
#include "workspacestatefile.h"
#include <QThreadPool>
class QTextDocument;

namespace simdock {
class SimulationEventModel;
class ZEROSLACK_API WorkspaceSimulationController final : public QObject {
    Q_OBJECT
public:
    explicit WorkspaceSimulationController(QObject* parent = nullptr);
    ~WorkspaceSimulationController() override;
    QString setWorkspace(const QString& root);
    QString workspace() const { return m_root; }
    SimulationTarget target() const { return m_target; }
    std::shared_ptr<const SimulationBuildPlan> lastPlan() const { return m_lastPlan; }
    QString legacyProjectId() const { return m_legacyProjectId; }
    QString selectTarget(const SimulationTarget& target, const QString& legacyId = {},
        const QMap<QString, QByteArray>& overrides = {});
    QString selectLegacyProject(const Project& project);
    QString clearTarget();
    QStringList includeDirectories() const { return m_includeDirectories; }
    QMap<QString, QString> definitionSources() const { return m_definitionSources; }
    QString setSettings(const QStringList& includeDirectories, const QMap<QString, QString>& definitionSources);
    QString canChangeWorkspace() const;
    QuestaSession* session() { return &m_session; }
    bool preparing() const { return m_preparing; }
    bool busy() const { return m_preparing || m_session.busy(); }
    QString state() const { return m_state; }
    QTextDocument* logDocument() const { return m_log; }
    SimulationEventModel* eventModel() const { return m_events; }
    void appendLog(const QString& text);
    void clearLog();
    void prepare(const QString& executable, const Project& project, WaveTheme theme,
        const QMap<QString, QByteArray>& overrides = {});
    using SaveInputs = std::function<QString(const SimulationBuildPlan&, SimulationBuildRequest&)>;
    using CheckCurrent = std::function<QString(const SimulationBuildRequest&)>;
    using AwaitInputs = std::function<bool(SimulationBuildRequest&, QString&)>;
    void prepare(const QString& executable, SimulationBuildRequest request, WaveTheme theme,
        SaveInputs saveInputs = {}, CheckCurrent checkCurrent = {}, AwaitInputs awaitInputs = {});
    void cancelPreparation();
    void stop();
    void shutdown();
signals:
    void stateChanged();
    void targetChanged();
    void finished(bool completed, const QString& message);
    void planReady(std::shared_ptr<const simdock::SimulationBuildPlan> plan);
    void logAboutToAppend();
    void logAppended();
private:
    QString saveTarget(const SimulationTarget& target, const QString& legacyId, const QStringList& includes);
    void flushLog();
    void prepareCaptured(const QString& executable, const SimulationBuildRequest& request, WaveTheme theme,
        quint64 generation, CheckCurrent checkCurrent);
    void awaitInputs(SimulationBuildRequest request, quint64 generation, AwaitInputs ready,
        std::function<void(SimulationBuildRequest)> next);
    void planAndSave(const QString& executable, SimulationBuildRequest request, WaveTheme theme,
        quint64 generation, SaveInputs saveInputs, CheckCurrent checkCurrent, AwaitInputs ready);
    void preparationFailed(const QString& error);
    QString m_root, m_legacyProjectId, m_state = QStringLiteral("Ready"), m_pendingLog;
    QStringList m_includeDirectories;
    QMap<QString, QString> m_definitionSources;
    SimulationTarget m_target;
    std::shared_ptr<const SimulationBuildPlan> m_lastPlan;
    WorkspaceStateFile m_store;
    QuestaSession m_session;
    QThreadPool m_pool;
    QTimer m_logTimer;
    QTextDocument* m_log;
    SimulationEventModel* m_events;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    quint64 m_request = 0;
    bool m_preparing = false;
};
}
