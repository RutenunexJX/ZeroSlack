#pragma once
#include "zeroslackexport.h"
#include "../core/model.h"
#include "../core/questasession.h"
#include <QWidget>
#include <QVariantMap>
#include <QThreadPool>
#include "../core/workspace.h"
#include "../core/dependencies.h"
#include "../core/preparation.h"
#include <QTimer>
#include <QPointer>
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

namespace simdock {
class ZEROSLACK_API Workbench : public QWidget {
    Q_OBJECT
public:
    explicit Workbench(QWidget* parent = nullptr, bool standalone = false);
    ~Workbench() override;
    QWidget* sourceFilesWidget() const { return m_sources; }
    QWidget* runLogWidget() const { return m_logs; }
    void initialize();
    void openWorkspace(const QString& path);
    bool openSuiteTarget(const QString& root, const QString& projectId, QString* error);
    bool createProject(const QString& name);
    bool createDemo(const TbOptions& options, QString* error);
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
    void stateChanged();
    void scanFinished();
    void preferencesChanged();
private:
    QString contextError() const;
    QVariant preference(const QString& key, const QVariant& fallback = {}) const;
    void setPreference(const QString& key, const QVariant& value);
    void restoreLayout(const QVariantMap& state);
    QPointer<QWidget> m_sources, m_logs;
    QWidget *m_graphicalInput = nullptr, *m_existingInput = nullptr, *m_durationInput = nullptr;
    ElaText *m_emptyState = nullptr, *m_graphicalDuration = nullptr;
    ElaComboBox* m_inputMode = nullptr;
    ElaLineEdit* m_generatedPath = nullptr;
    QToolButton* m_settings = nullptr;
    QAction* m_generate = nullptr;
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
    void saveCurrent();
    void chooseTb();
    void createTb();
    void editStimulus();
    void startSimulation();
    void chooseWaveSignals();
    void cancelPreparation();
    void moveSource(int offset);
    void appendLog(const QString& text);
    void flushLog();
    void applyTheme();
    Project* current();
    const Module* currentModule() const;
    QString m_root, m_simulator;
    Scan m_scan;
    DependencyIndex m_dependencies;
    std::shared_ptr<const PreparedStimulus> m_preparedStimulus, m_generatedDesign;
    QJsonObject m_generatedDrawing;
    QString m_generatedTb;
    std::shared_ptr<std::atomic_bool> m_preparationCancelled;
    quint64 m_preparationId = 0;
    bool m_preparing = false;
    QList<Project> m_projects;
    int m_projectIndex = -1;
    quint64 m_generation = 0;
    bool m_loading = false, m_scanning = false, m_requestingProject = false;
    QuestaSession m_session;
    std::shared_ptr<std::atomic_bool> m_scanCancelled;
    QTimer m_logTimer;
    QString m_pendingLog;
    ElaText *m_scanStatus, *m_status, *m_sourceHint;
    ElaListView *m_projectList, *m_fileList;
    QStandardItemModel *m_projectModel, *m_fileModel;
    ElaComboBox *m_dut, *m_units;
    ElaComboBox* m_waveScope;
    ElaPushButton* m_waveSignals;
    ElaLineEdit *m_tbFile, *m_tbTop;
    ElaSpinBox* m_duration;
    ElaPlainTextEdit* m_log;
    ElaPushButton *m_new, *m_refresh, *m_stimulus, *m_chooseTb, *m_openTb, *m_run, *m_stop;
    QWidget* m_projectPanel;
};
}
