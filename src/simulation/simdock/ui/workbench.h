#pragma once
#include "zeroslackexport.h"
#include "../core/model.h"
#include "../core/questasession.h"
#include <QWidget>
#include <QVariantMap>
#include <QThreadPool>
#include "../core/workspace.h"
#include "../core/dependencies.h"
#include <QTimer>
#include <QPointer>
#include <QFileSystemWatcher>
#include <zeroslack/semantic/hierarchyservice.h>
#include <atomic>
#include <memory>
#include <optional>
class ElaComboBox;
class ElaLineEdit;
class ElaListView;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaSpinBox;
class ElaText;
class QStandardItemModel;
class QStandardItem;
class QModelIndex;
class QAction;
class QToolButton;
class QLabel;
class QListView;
class QPushButton;

namespace simdock {
class SimulationEventsPanel;
class ZEROSLACK_API Workbench : public QWidget {
    Q_OBJECT
public:
    explicit Workbench(QWidget* parent = nullptr, bool standalone = false);
    ~Workbench() override;
    QWidget* sourceFilesWidget() const { return m_sources; }
    QWidget* runLogWidget() const { return m_logs; }
    QWidget* eventsWidget() const;
    void initialize();
    void openWorkspace(const QString& path);
    bool openSuiteTarget(const QString& root, const QString& projectId, QString* error);
    bool createProject(const QString& name);
    bool createDemo(const TbOptions& options, QString* error);
    void setEditorTbCreator(std::function<void(const QString&, const QString&)> creator) { m_editorTbCreator = std::move(creator); }
    void adoptCreatedTb(const QString& dutFile, const QString& dutName, const QString& tbFile, const QString& tbName);
    QString activeProjectId() const;
    QList<DesignHierarchyRoot> designRoots() const;
    QString addDesignProject(const QString& file, const QString& module, bool activate = true);
    QString removeDesignRoot(const QString& projectId);
    void setDesignExpanded(const QString& projectId, bool expanded);
    bool designMigrationCompleted() const;
    void markDesignMigrationCompleted();
    void refreshCompilationOrder();
    QStringList activeCompilationInputs() const;
    QString adoptHdlInputs(const QString& expectedProject, const QStringList& files, const QStringList& includeDirectories);
    QString automationProjectWriteIssue(const Project* baseline) const;
    void adoptAutomationProject(const Project& project);
    QJsonObject automationProjectState(const Project& disk) const;
    bool automationProjectEditing() const;
    QString workspace() const { return m_root; }
    void setSimulator(const QString& executable);
    Q_INVOKABLE QString setContext(const QString& workspace);
    Q_INVOKABLE QString openProject(const QString& projectId);
    Q_INVOKABLE QVariantMap saveState() const;
    Q_INVOKABLE QString restoreState(const QVariantMap& state);
    Q_INVOKABLE bool canClose() const;
    Q_INVOKABLE void setDarkTheme(bool dark);
    void shutdown();
    void savePreferences();
    QSize minimumSizeHint() const override;
signals:
    void openFileRequested(const QString& file);
    void openSourceRequested(const QString& file, int line);
    void stateChanged();
    void scanFinished();
    void designProjectsChanged();
    void preferencesChanged();
private:
    std::function<void(const QString&, const QString&)> m_editorTbCreator;
    QString contextError() const;
    QVariant preference(const QString& key, const QVariant& fallback = {}) const;
    void setPreference(const QString& key, const QVariant& value);
    void restoreLayout(const QVariantMap& state);
    QPointer<QWidget> m_sources, m_logs;
    SimulationEventsPanel* m_events = nullptr;
    QWidget *m_existingInput = nullptr, *m_durationInput = nullptr;
    ElaText *m_emptyState = nullptr, *m_inputNotice = nullptr;
    QToolButton* m_settings = nullptr;
    QAction *m_generate = nullptr, *m_closeSession = nullptr, *m_legacyInfo = nullptr;
    bool m_standalone = false;
    QVariantMap m_preferences;
    std::optional<bool> m_hostDarkTheme;
    QThreadPool m_scanPool;
    SourceCache m_sourceCache;
    void buildUi();
    void openSettings();
    void refreshFiles();
    void selectProject(int row);
    void toggleSource(const QModelIndex& index);
    void sourceChanged(QStandardItem* item);
    void updateSources();
    void updateDutChoices();
    void updateControls();
    bool saveCurrent();
    void renameProject();
    void chooseTb();
    void createTb();
    void startSimulation();
    void cancelPreparation();
    QString workspacePreferenceKey() const;
    void scheduleCompilationOrder();
    void appendLog(const QString& text);
    void flushLog();
    void applyTheme();
    Project* current();
    const Module* currentModule() const;
    QString m_root, m_simulator;
    Scan m_scan;
    std::shared_ptr<std::atomic_bool> m_preparationCancelled;
    quint64 m_preparationId = 0;
    bool m_preparing = false;
    QTimer m_orderTimer;
    QFileSystemWatcher m_inputWatcher;
    quint64 m_orderGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_orderCancelled;
    bool m_orderPending = false;
    QString m_orderIssue;
    QWidget *m_projectsBody = nullptr, *m_inputsBody = nullptr;
    QPushButton *m_projectsHeading = nullptr, *m_inputsHeading = nullptr;
    QList<Project> m_projects;
    int m_projectIndex = -1;
    quint64 m_generation = 0;
    bool m_loading = false, m_scanning = false, m_requestingProject = false;
    QuestaSession m_session;
    std::shared_ptr<std::atomic_bool> m_scanCancelled;
    QTimer m_logTimer;
    QString m_pendingLog;
    ElaText *m_status;
    QLabel* m_sourceHint;
    QString m_scanSummary;
    QListView *m_projectList, *m_fileList;
    QStandardItemModel *m_projectModel, *m_fileModel;
    ElaComboBox *m_dut, *m_units;
    ElaLineEdit *m_tbFile, *m_tbTop;
    ElaSpinBox* m_duration;
    ElaPlainTextEdit* m_log;
    QPushButton *m_new, *m_refresh, *m_rename;
    QToolButton *m_chooseTb, *m_openTb;
    ElaPushButton *m_run, *m_stop;
    QWidget* m_projectPanel;
};
}
