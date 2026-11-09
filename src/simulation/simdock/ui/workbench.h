#pragma once
#include "zeroslackexport.h"
#include "../core/workspacesimulationcontroller.h"
#include "../../../workspace/designprojectservice.h"
#include <QWidget>
#include <QPointer>
#include <QVariantMap>
#include <functional>
class ElaPlainTextEdit;
class QLabel;
class QToolButton;
class QPushButton;
namespace simdock {
class SimulationEventsPanel;
class ZEROSLACK_API Workbench : public QWidget {
    Q_OBJECT
public:
    explicit Workbench(QWidget* parent = nullptr, bool standalone = false,
        WorkspaceSimulationController* controller = nullptr, DesignProjectService* design = nullptr);
    ~Workbench() override;
    WorkspaceSimulationController* controller() const { return m_controller; }
    DesignProjectService* designService() const { return m_design; }
    QWidget* runLogWidget() const { return m_logs; }
    QWidget* eventsWidget() const;
    QString workspace() const { return m_controller->workspace(); }
    QString simulator() const { return m_simulator; }
    WaveTheme waveTheme() const;
    void initialize();
    void openWorkspace(const QString& path);
    bool openSuiteTarget(const QString& root, const QString& projectId, QString* error);
    QString selectTarget(const QString& file, const QMap<QString, QByteArray>& overrides = {});
    void setSimulator(const QString& executable);
    void setRunHandler(std::function<QString()> handler) { m_runHandler = std::move(handler); }
    void setTargetHandler(std::function<QString(const QString&)> handler) { m_targetHandler = std::move(handler); }
    void setWorkspaceConfigurationHandler(std::function<void()> handler) { m_configureWorkspace = std::move(handler); }
    QList<DesignHierarchyRoot> designRoots() const;
    QString addDesignProject(const QString& file, const QString& module, bool activate = true);
    QString removeDesignRoot(const QString& projectId);
    void setDesignExpanded(const QString& projectId, bool expanded);
    bool designMigrationCompleted() const;
    void markDesignMigrationCompleted();
    Q_INVOKABLE QString setContext(const QString& workspace);
    Q_INVOKABLE QString openProject(const QString& projectId);
    Q_INVOKABLE QVariantMap saveState() const;
    Q_INVOKABLE QString restoreState(const QVariantMap& state);
    Q_INVOKABLE bool canClose() const;
    Q_INVOKABLE void setDarkTheme(bool dark);
    void shutdown();
    void savePreferences();
    QSize minimumSizeHint() const override;
public slots:
    void startSimulation();
signals:
    void openFileRequested(const QString& file);
    void openSourceRequested(const QString& file, int line);
    void stateChanged();
    void scanFinished();
    void designProjectsChanged();
    void preferencesChanged();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QVariant preference(const QString& key, const QVariant& fallback = {}) const;
    void setPreference(const QString& key, const QVariant& value);
    void buildUi();
    void openSettings();
    void chooseTb();
    void updateControls();
    void reportIssue(const QString& issue);
    QString contextError() const;
    QPointer<QWidget> m_logs;
    QPointer<ElaPlainTextEdit> m_log;
    QPointer<SimulationEventsPanel> m_events;
    QToolButton *m_target = nullptr, *m_settings = nullptr;
    QPushButton *m_run = nullptr, *m_stop = nullptr;
    QLabel* m_status = nullptr;
    WorkspaceSimulationController* m_controller;
    DesignProjectService* m_design;
    QString m_simulator;
    QVariantMap m_preferences;
    bool m_standalone = false, m_logFollow = true;
    int m_logPosition = 0;
    std::function<QString()> m_runHandler;
    std::function<QString(const QString&)> m_targetHandler;
    std::function<void()> m_configureWorkspace;
};
}
