#include "workbench.h"
#include "simulationeventspanel.h"
#include "semanticindex.h"
#include "applicationthememanager.h"
#include "../core/dependencies.h"
#include "../core/analyzer.h"
#include "dialogs.h"
#include "../core/testbench.h"
#include "../core/workspace.h"
#include "uistyle.h"
#include "sourcedelegate.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaSpinBox.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToolButton.h>
#include "uicontrols.h"
#include "uitypography.h"
#include "roundedicons.h"
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QMenu>
#include <QScrollBar>
#include <QSet>
#include <QDialog>
#include <QCryptographicHash>
#include <ElaDialog.h>
#include <QPointer>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QSettings>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QStandardItemModel>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace simdock {
Workbench::Workbench(QWidget* parent, bool standalone)
    : QWidget(parent), m_standalone(standalone), m_session(this)
{
    Ui::initializeComponent();
    m_hostDarkTheme = isDarkTheme(ApplicationThemeManager::instance().mode());
    setObjectName(QStringLiteral("SimDockWorkbench"));
    setAutoFillBackground(true);
    setFont(Ui::font());
    setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    m_scanPool.setMaxThreadCount(1);
    buildUi();
    m_events = new SimulationEventsPanel(this); m_events->hide();
    connect(&m_session, &QuestaSession::eventRunStarted, m_events, &SimulationEventsPanel::startRun);
    connect(&m_session, &QuestaSession::eventsReady, m_events, &SimulationEventsPanel::append);
    connect(&m_session, &QuestaSession::eventRecordsRejected, m_events, &SimulationEventsPanel::reject);
    connect(&m_session, &QuestaSession::eventRunCleared, m_events, [this] { m_events->startRun({}); });
    connect(m_events, &SimulationEventsPanel::sourceRequested, this, &Workbench::openSourceRequested);
    m_simulator = preference(QStringLiteral("simulator/path")).toString();
    m_logTimer.setSingleShot(true);
    m_logTimer.setInterval(16);
    connect(&m_logTimer, &QTimer::timeout, this, &Workbench::flushLog);
    connect(&m_session, &QuestaSession::logText, this, &Workbench::appendLog);
    connect(&m_session, &QuestaSession::stateChanged, this, [this](const QString& state) {
        m_status->setText(state);
        updateControls();
    });
    connect(&m_session, &QuestaSession::finished, this, [this] { updateControls(); });
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged, this, [this] {
        setDarkTheme(isDarkTheme(ApplicationThemeManager::instance().mode()));
    });
    applyTheme();
    updateControls();
}
Workbench::~Workbench()
{
    shutdown();
    // Only this instance's tasks are drained; the host's global pool is untouched.
    m_scanPool.waitForDone();
    delete m_sources;
    delete m_logs;
    delete m_events;
}

QWidget* Workbench::eventsWidget() const { return m_events; }

QSize Workbench::minimumSizeHint() const { return {280, 240}; }

void Workbench::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(10);
    auto* heading = new QHBoxLayout;
    heading->addWidget(Ui::label(QStringLiteral("Simulation"), this, Ui::Role::PanelTitle), 1);
    auto* auxiliary = UiControls::toolButton(this);
    auxiliary->setObjectName(QStringLiteral("simulationActions"));
    auxiliary->setIcon(RoundedIcons::icon(RoundedIcons::More));
    auxiliary->setAccessibleName(QStringLiteral("Simulation actions"));
    auxiliary->setToolTip(QStringLiteral("Simulation actions"));
    auto* menu = UiControls::menu(this);
    m_generate = menu->addAction(QStringLiteral("Create Demo TB"));
    m_generate->setObjectName(QStringLiteral("createTb"));
    auxiliary->setMenu(menu);
    auxiliary->setPopupMode(QToolButton::InstantPopup);
    heading->addWidget(auxiliary);
    m_settings = UiControls::toolButton(this);
    m_settings->setObjectName(QStringLiteral("openSettings"));
    m_settings->setIcon(RoundedIcons::icon(RoundedIcons::Settings));
    m_settings->setAccessibleName(QStringLiteral("Simulation settings"));
    m_settings->setToolTip(QStringLiteral("Simulation settings"));
    heading->addWidget(m_settings);
    outer->addLayout(heading);

    m_emptyState = Ui::label(QStringLiteral("Open a ZeroSlack workspace to configure simulation."), this, Ui::Role::Metadata);
    m_emptyState->setObjectName(QStringLiteral("simulationEmptyState"));
    m_emptyState->setWordWrap(true);
    outer->addWidget(m_emptyState);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("simulationFormScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    Ui::smoothScrolling(scroll);
    m_projectPanel = new QWidget(scroll);
    auto* form = new QVBoxLayout(m_projectPanel);
    form->setContentsMargins(0, 0, 4, 0);
    form->setSpacing(8);
    form->addWidget(Ui::label(QStringLiteral("DUT module"), this, Ui::Role::Section));
    m_dut = new ElaComboBox(this);
    m_dut->setObjectName(QStringLiteral("dutSelector"));
    m_dut->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_dut->setMinimumContentsLength(1);
    m_dut->setPlaceholderText(QStringLiteral("Select build inputs first"));
    form->addWidget(m_dut);
    form->addWidget(Ui::label(QStringLiteral("Simulation input"), this, Ui::Role::Section));
    m_existingInput = new QWidget(this);
    auto* existing = new QVBoxLayout(m_existingInput);
    existing->setContentsMargins(0, 0, 0, 0);
    m_tbFile = new ElaLineEdit(this);
    m_tbFile->setObjectName(QStringLiteral("tbFile"));
    m_tbFile->setReadOnly(true);
    m_tbFile->setPlaceholderText(QStringLiteral("Select a TB inside the workspace"));
    existing->addWidget(m_tbFile);
    auto* tbActions = new QHBoxLayout;
    m_openTb = Ui::button(QStringLiteral("Open TB"), this);
    m_openTb->setObjectName(QStringLiteral("openTb"));
    m_chooseTb = Ui::button(QStringLiteral("Choose TB"), this);
    m_chooseTb->setObjectName(QStringLiteral("chooseTb"));
    tbActions->addWidget(m_openTb);
    tbActions->addWidget(m_chooseTb);
    existing->addLayout(tbActions);
    existing->addWidget(Ui::label(QStringLiteral("TB top level"), this, Ui::Role::Section));
    m_tbTop = new ElaLineEdit(this);
    m_tbTop->setObjectName(QStringLiteral("tbTop"));
    m_tbTop->setPlaceholderText(QStringLiteral("tb_top"));
    existing->addWidget(m_tbTop);
    form->addWidget(m_existingInput);
    m_legacyNotice = Ui::label(QString(), this, Ui::Role::Metadata);
    m_legacyNotice->setObjectName(QStringLiteral("legacyInputNotice"));
    m_legacyNotice->setWordWrap(true);
    form->addWidget(m_legacyNotice);
    form->addWidget(Ui::label(QStringLiteral("Duration"), this, Ui::Role::Section));
    m_durationInput = new QWidget(this);
    auto* timing = new QHBoxLayout(m_durationInput);
    timing->setContentsMargins(0, 0, 0, 0);
    m_duration = Ui::spinBox(this);
    m_duration->setObjectName(QStringLiteral("duration"));
    m_duration->setRange(1, 3600000);
    m_duration->setValue(1000);
    m_units = new ElaComboBox(this);
    m_units->setObjectName(QStringLiteral("durationUnits"));
    m_units->addItem(QStringLiteral("ns"), 1);
    m_units->addItem(QStringLiteral("us"), 1000);
    m_units->addItem(QStringLiteral("ms"), 1000000);
    m_units->setFixedWidth(82);
    timing->addWidget(m_duration, 1);
    timing->addWidget(m_units);
    form->addWidget(m_durationInput);
    form->addStretch();
    scroll->setWidget(m_projectPanel);
    outer->addWidget(scroll, 1);
    m_status = Ui::label(QStringLiteral("Ready"), this, Ui::Role::Metadata);
    m_status->setObjectName(QStringLiteral("runStatus"));
    m_status->setWordWrap(true);
    outer->addWidget(m_status);
    auto* actions = new QHBoxLayout;
    m_stop = Ui::button(QStringLiteral("Close session"), this);
    m_stop->setObjectName(QStringLiteral("stopSimulation"));
    m_run = Ui::button(QStringLiteral("Start simulation"), this, true);
    m_run->setObjectName(QStringLiteral("startSimulation"));
    actions->addWidget(m_stop);
    actions->addWidget(m_run, 1);
    outer->addLayout(actions);

    auto* sources = new QWidget(this);
    m_sources = sources;
    sources->setObjectName(QStringLiteral("simulationSources"));
    auto* sourceLayout = new QVBoxLayout(sources);
    sourceLayout->setContentsMargins(2, 2, 2, 2);
    sourceLayout->setSpacing(8);
    auto* projects = new QWidget(sources);
    auto* projectLayout = new QVBoxLayout(projects);
    projectLayout->setContentsMargins(0, 0, 0, 0);
    projectLayout->setSpacing(4);
    auto* projectHeading = new QHBoxLayout;
    projectHeading->setSpacing(8);
    auto* projectsLabel = UiControls::label(QStringLiteral("Projects"), projects);
    UiTypography::apply(projectsLabel, UiTypography::Role::Section);
    projectHeading->addWidget(projectsLabel);
    projectHeading->addStretch();
    m_new = UiControls::pushButton(QStringLiteral("New"), projects);
    m_new->setObjectName(QStringLiteral("newProject"));
    projectHeading->addWidget(m_new);
    m_rename = UiControls::pushButton(QString(), projects);
    m_rename->setObjectName(QStringLiteral("renameProject"));
    m_rename->setIcon(RoundedIcons::icon(RoundedIcons::Change));
    m_rename->setFixedWidth(32);
    m_rename->setToolTip(QStringLiteral("Rename project"));
    m_rename->setAccessibleName(QStringLiteral("Rename project"));
    Ui::enableToolTip(m_rename);
    projectHeading->addWidget(m_rename);
    connect(m_rename, &QPushButton::clicked, this, &Workbench::renameProject);
    projectLayout->addLayout(projectHeading);
    m_projectList = UiControls::listView(projects);
    m_projectList->setObjectName(QStringLiteral("projectList"));
    m_projectList->setProperty("workspaceNavigationList", true);
    m_projectList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_projectList->setUniformItemSizes(true);
    m_projectModel = new QStandardItemModel(this);
    m_projectList->setModel(m_projectModel);
    projectLayout->addWidget(m_projectList, 1);

    m_projectList->setMinimumHeight(42);
    m_projectList->setMaximumHeight(140);
    sourceLayout->addWidget(projects);
    auto* files = new QWidget(sources);
    files->setObjectName(QStringLiteral("sourcePanel"));
    auto* fileLayout = new QVBoxLayout(files);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(6);
    auto* fileHeading = new QHBoxLayout;
    auto* inputsLabel = UiControls::label(QStringLiteral("Build inputs"), files);
    UiTypography::apply(inputsLabel, UiTypography::Role::Section);
    fileHeading->addWidget(inputsLabel, 1);
    m_refresh = UiControls::pushButton(QStringLiteral("Rescan"), files);
    m_refresh->setObjectName(QStringLiteral("refreshWorkspace"));
    fileHeading->addWidget(m_refresh);
    fileLayout->addLayout(fileHeading);
    m_sourceHint = UiControls::label(QStringLiteral("Select files and set their compilation order."), files);
    UiTypography::apply(m_sourceHint, UiTypography::Role::Metadata);
    m_sourceHint->setWordWrap(true);
    fileLayout->addWidget(m_sourceHint);
    m_fileList = new QListView(files);
    m_fileList->setObjectName(QStringLiteral("sourceList"));
    auto* fileStyle = ElaListView::createStyle(qApp, qMax(28, m_fileList->fontMetrics().height() + 10));
    static_cast<QProxyStyle*>(fileStyle)->setBaseStyle(new SourceMetricsStyle);
    m_fileList->setStyle(fileStyle);
    connect(m_fileList, &QObject::destroyed, fileStyle, &QObject::deleteLater);
    m_fileList->setProperty("workspaceNavigationList", true);
    m_fileList->setMouseTracking(true);
    m_fileList->setItemDelegate(new SourceDelegate(m_fileList));
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileList->setFont(UiTypography::font());
    m_fileList->setTextElideMode(Qt::ElideMiddle);
    m_fileModel = new SourceModel(this, [this](const QModelIndex& index, Qt::CheckState state) {
        if (m_scanning || m_session.busy() || m_requestingProject
            || (state != Qt::Checked && state != Qt::Unchecked)) return false;
        toggleSource(index);
        return current() != nullptr;
    });
    m_fileList->setModel(m_fileModel);
    m_fileList->setUniformItemSizes(true);
    UiControls::enableSmoothScrolling(m_fileList);
    fileLayout->addWidget(m_fileList, 1);
    auto* fileActions = new QHBoxLayout;
    fileActions->setSpacing(8);
    auto* up = UiControls::pushButton(QStringLiteral("Move up"), files);
    auto* down = UiControls::pushButton(QStringLiteral("Move down"), files);
    up->setObjectName(QStringLiteral("moveSourceUp"));
    down->setObjectName(QStringLiteral("moveSourceDown"));
    up->setToolTip(QStringLiteral("Compile the selected file earlier"));
    down->setToolTip(QStringLiteral("Compile the selected file later"));
    Ui::enableToolTip(up);
    Ui::enableToolTip(down);
    Ui::enableToolTip(m_fileList);
    fileActions->addWidget(up);
    fileActions->addWidget(down);
    fileLayout->addLayout(fileActions);
    m_fileList->setMinimumHeight(80);
    m_scanStatus = UiControls::label(QStringLiteral("Open a ZeroSlack workspace."), files);
    UiTypography::apply(m_scanStatus, UiTypography::Role::Metadata);
    m_scanStatus->setWordWrap(true);
    fileLayout->addWidget(m_scanStatus);
    sourceLayout->addWidget(files, 1);
    sources->hide();
    auto* logs = new QWidget(this);
    m_logs = logs;
    logs->setObjectName(QStringLiteral("simulationRunLog"));
    auto* logLayout = new QVBoxLayout(logs);
    logLayout->setContentsMargins(10, 8, 10, 8);
    auto* logHeading = new QHBoxLayout;
    logHeading->addStretch();
    auto* clear = Ui::button(QStringLiteral("Clear"), logs);
    clear->setObjectName(QStringLiteral("clearLog"));
    logHeading->addWidget(clear);
    logLayout->addLayout(logHeading);
    m_log = Ui::textView(logs);
    m_log->setObjectName(QStringLiteral("simulationLog"));
    m_log->setProperty("codeSurface", true);
    m_log->setNativeTextBehavior(true);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(10000);
    m_log->setFont(Ui::codeFont());
    m_log->setPlaceholderText(QStringLiteral("Compiler, loader, and simulation output will appear here."));
    logLayout->addWidget(m_log, 1);
    logs->hide();
    Ui::normalizeControls(this);
    connect(m_settings, &QToolButton::clicked, this, &Workbench::openSettings);
    connect(m_openTb, &QPushButton::clicked, this, [this] {
        if (const auto* p = current(); p && !p->tbFile.isEmpty()) emit openFileRequested(QDir(m_root).filePath(p->tbFile));
    });
    connect(m_new, &QPushButton::clicked, this, [this] {
        QPointer<Workbench> owner(this);
        const auto name = askProjectName(this);
        if (owner && !name.isEmpty()) createProject(name);
    });
    connect(m_refresh, &QPushButton::clicked, this, [this] { openWorkspace(m_root); });
    connect(m_projectList->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        if (!m_loading) selectProject(index.row());
    });
    connect(m_fileModel, &QStandardItemModel::itemChanged, this, &Workbench::sourceChanged);
    connect(m_dut, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !current()) return;
        auto* p = current();
        const auto identity = m_dut->currentData().toStringList();
        if (identity.size() != 2) return;
        if (p->dutFile != identity[0] || p->dutName != identity[1]) {
            p->dutFile = identity[0]; p->dutName = identity[1];
        }
        const auto selection = selectDependencies(m_dependencies, identity[0], p->sources);
        p->sources = selection.sources;
        for (const auto& message : selection.messages) appendLog(message + QLatin1Char('\n'));
        refreshFiles();
        saveCurrent(); updateControls();
    });
    connect(m_tbTop, &QLineEdit::editingFinished, this, [this] {
        if (auto* p = current(); p) { p->tbName = m_tbTop->text().trimmed(); saveCurrent(); updateControls(); }
    });
    auto durationChanged = [this] {
        if (m_loading || !current()) return;
        current()->durationNs = qint64(m_duration->value()) * m_units->currentData().toLongLong();
        saveCurrent(); updateControls();
    };
    connect(m_duration, &QSpinBox::valueChanged, this, durationChanged);
    connect(m_units, &QComboBox::currentIndexChanged, this, durationChanged);
    connect(m_generate, &QAction::triggered, this, &Workbench::createTb);
    connect(m_chooseTb, &QPushButton::clicked, this, &Workbench::chooseTb);
    connect(m_run, &QPushButton::clicked, this, &Workbench::startSimulation);
    connect(m_stop, &QPushButton::clicked, this, [this] {
        if (m_preparing) {
            cancelPreparation(); m_status->setText(QStringLiteral("Preparation cancelled"));
        } else {
            m_session.stop(); m_status->setText(QStringLiteral("Session closed"));
        }
        updateControls();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_logTimer.stop();
        m_pendingLog.clear();
        m_log->clear();
    });
    connect(up, &QPushButton::clicked, this, [this] { moveSource(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSource(1); });
}

void Workbench::initialize()
{
    if (!m_standalone) return;
    if (m_simulator.isEmpty() || !simulatorError(m_simulator).isEmpty()) openSettings();
    const QString root = preference(QStringLiteral("workspace/last")).toString();
    if (QFileInfo(root).isDir()) openWorkspace(root);
}
void Workbench::setSimulator(const QString& executable)
{
    if (m_simulator != executable && m_preparing) cancelPreparation();
    m_simulator = executable;
    setPreference(QStringLiteral("simulator/type"), QStringLiteral("questa"));
    setPreference(QStringLiteral("simulator/path"), executable);
    updateControls();
}
void Workbench::openSettings()
{
    QPointer<Workbench> owner(this);
    QPointer<SettingsDialog> dialog = new SettingsDialog(m_simulator, this, m_session.alive(), preference(QStringLiteral("waveform/theme"), QStringLiteral("default")).toString(), m_hostDarkTheme.has_value());
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    if (result == QDialog::Accepted) {
        if (!m_session.alive()) setSimulator(dialog->executable());
        setPreference(QStringLiteral("waveform/theme"), wavePalette(dialog->waveTheme()).id);
    }
    delete dialog;
    updateControls();
}

void Workbench::openWorkspace(const QString& path)
{
    const auto error = setContext(path);
    if (!error.isEmpty()) appendLog(error + QLatin1Char('\n'));
}

QString Workbench::setContext(const QString& path)
{
    const auto issue = contextError();
    if (!issue.isEmpty()) return issue;
    const QString root = path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
    if (!path.isEmpty() && (root.isEmpty() || !QFileInfo(root).isDir()))
        return QStringLiteral("The workspace directory does not exist.");
    m_session.stop();
    if (root.isEmpty()) {
        cancelPreparation();
        if (m_scanCancelled) m_scanCancelled->store(true);
        ++m_generation;
        m_scanning = false;
        m_root.clear(); m_scan = {}; m_dependencies = DependencyIndex(); m_projects.clear(); m_projectIndex = -1;
        m_loading = true; m_projectModel->clear(); m_loading = false;
        selectProject(-1);

        m_scanStatus->setText(QStringLiteral("Open a ZeroSlack workspace."));
        m_status->setText(QStringLiteral("Ready"));
        return {};
    }
    cancelPreparation();
    const QString previousId = current() && root == m_root ? current()->id : QString();
    if (m_scanCancelled) m_scanCancelled->store(true);
    m_scanCancelled = std::make_shared<std::atomic_bool>(false);
    m_root = root;
    m_status->setText(QStringLiteral("Ready"));
    setPreference(QStringLiteral("workspace/last"), m_root);
    m_scan = {}; m_dependencies = DependencyIndex();
    m_loading = true;
    m_projectIndex = -1;
    QStringList errors;
    m_projects = loadProjects(m_root, &errors);
    if (QFileInfo::exists(QDir(m_root).filePath(QStringLiteral(".simdock/runs"))))
        errors << QStringLiteral("Legacy .simdock/runs files were preserved because ownership and active use cannot be verified. New runs use automatically cleaned session storage.");
    m_projectModel->clear();
    int selected = m_projects.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_projects.size(); ++i) {
        auto* item = new QStandardItem(m_projects[i].name);
        item->setSizeHint(QSize(180, 38));
        m_projectModel->appendRow(item);
        if (m_projects[i].id == previousId) selected = i;
    }
    m_loading = false;
    selectProject(selected);
    for (const auto& error : errors) appendLog(error + QLatin1Char('\n'));
    m_scanning = true;
    m_scanStatus->setText(QStringLiteral("Analyzing workspace..."));
    updateControls();
    const auto generation = ++m_generation;
    auto* watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcher<Scan>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation) return;
        m_scan = result;
        m_dependencies = DependencyIndex(m_scan);
        m_scanning = false;
        int modules = 0;
        for (const auto& file : m_scan.files) modules += file.modules.size();
        m_scanStatus->setText(QStringLiteral("%1 HDL files | %2 modules").arg(m_scan.files.size()).arg(modules));
        for (const auto& message : m_scan.messages) appendLog(message + QLatin1Char('\n'));
        refreshFiles();
        updateControls();
        emit scanFinished();
    });
    watcher->setFuture(QtConcurrent::run(&m_scanPool, [root, cancelled = m_scanCancelled, cache = &m_sourceCache] {
        if (cancelled->load()) return Scan{};
        return scanWorkspace(root, cancelled.get(), nullptr, cache);
    }));
    return {};
}

bool Workbench::openSuiteTarget(const QString& root, const QString& projectId, QString* error)
{
    if (m_session.busy()) { *error = QStringLiteral("Wait for the current simulation to finish."); return false; }
    if (!QFileInfo(root).isDir()) { *error = QStringLiteral("The workspace directory does not exist."); return false; }
    if (!projectId.isEmpty()) {
        const auto projects = loadProjects(root);
        bool found = false;
        for (const auto& project : projects) if (project.id == projectId) found = true;
        if (!found) { *error = QStringLiteral("The simulation project no longer exists."); return false; }
    }
    *error = setContext(root);
    if (!error->isEmpty()) return false;
    if (!projectId.isEmpty()) {
        for (int i = 0; i < m_projects.size(); ++i) if (m_projects[i].id == projectId) { selectProject(i); break; }
    }
    return true;
}

Project* Workbench::current()
{
    return m_projectIndex >= 0 && m_projectIndex < m_projects.size() ? &m_projects[m_projectIndex] : nullptr;
}
QString Workbench::activeProjectId() const
{
    return m_projectIndex >= 0 && m_projectIndex < m_projects.size() ? m_projects[m_projectIndex].id : QString();
}
QStringList Workbench::activeCompilationInputs() const
{
    if (m_projectIndex < 0 || m_projectIndex >= m_projects.size()) return {};
    const auto& project = m_projects[m_projectIndex];
    QStringList inputs;
    for (const auto& file : project.sources) inputs << QDir(m_root).filePath(file);
    if (!project.tbFile.isEmpty()) inputs << QDir(m_root).filePath(project.tbFile);
    inputs.removeDuplicates();
    return inputs;
}
QString Workbench::adoptHdlInputs(const QString& expectedProject, const QStringList& files, const QStringList& includeDirectories)
{
    if (!current() || activeProjectId() != expectedProject)
        return QStringLiteral("Select the same project in Build inputs before using this command.");
    if (m_session.busy() || m_preparing) return QStringLiteral("Stop the simulation before changing Build inputs.");
    auto updated = *current();
    QStringList ordered;
    for (const auto& file : files) {
        if (!insideWorkspace(m_root, file) || !QFileInfo(file).isFile())
            return QStringLiteral("The exported compilation input is missing or outside the workspace: %1").arg(file);
        const auto relative = QDir(m_root).relativeFilePath(file);
        if (!ordered.contains(relative)) ordered << relative;
    }
    for (const auto& file : updated.sources) if (!ordered.contains(file)) ordered << file;
    updated.sources = ordered;
    for (const auto& dir : includeDirectories) {
        if (!insideWorkspace(m_root, dir) || !QFileInfo(dir).isDir())
            return QStringLiteral("The exported include directory is unavailable: %1").arg(dir);
        const auto relative = QDir(m_root).relativeFilePath(dir);
        if (!updated.includeDirectories.contains(relative)) updated.includeDirectories << relative;
    }
    QString error;
    if (!saveProject(m_root, updated, &error)) return error;
    *current() = updated;
    refreshFiles();
    emit stateChanged();
    return {};
}
const Module* Workbench::currentModule() const
{
    const auto identity = m_dut->currentData().toStringList();
    if (identity.size() != 2) return nullptr;
    for (const auto& file : m_scan.files) if (file.path == identity[0])
        for (const auto& module : file.modules) if (module.name == identity[1]) return &module;
    return nullptr;
}
bool Workbench::createProject(const QString& name)
{
    if (m_root.isEmpty() || m_session.busy()) return false;
    auto project = newProject(name);
    QString error;
    if (!saveProject(m_root, project, &error)) { appendLog(error + QLatin1Char('\n')); return false; }
    m_projects << project;
    auto* item = new QStandardItem(project.name);
    item->setSizeHint(QSize(180, 38));
    m_projectModel->appendRow(item);
    selectProject(m_projects.size() - 1);
    return true;
}
QString Workbench::automationProjectWriteIssue(const Project* baseline) const
{
    const auto contextIssue = contextError();
    if (!contextIssue.isEmpty()) return contextIssue;
    if (m_preparing || m_scanning || m_session.busy()) return QStringLiteral("Simulation workbench is busy.");
    if (automationProjectEditing()) return QStringLiteral("Finish editing simulation inputs before applying automation changes.");
    if (baseline) for (const auto& project : m_projects) if (project.id == baseline->id
        && (projectJson(project) != projectJson(*baseline) || project.storageRevisions != baseline->storageRevisions))
        return QStringLiteral("The workbench and disk project revisions differ; existing workbench content was retained.");
    return {};
}
bool Workbench::automationProjectEditing() const
{
    const auto* focus = QApplication::focusWidget();
    for (const auto* control : {static_cast<QWidget*>(m_tbTop), static_cast<QWidget*>(m_duration), static_cast<QWidget*>(m_units)})
        if (control->hasFocus() || (focus && control->isAncestorOf(focus))) return true;
    return false;
}
void Workbench::adoptAutomationProject(const Project& project)
{
    int row = -1;
    for (int i = 0; i < m_projects.size(); ++i) if (m_projects[i].id == project.id) { row = i; break; }
    if (row < 0) { row = m_projects.size(); m_projects << project; m_projectModel->appendRow(new QStandardItem(project.name)); }
    else { m_projects[row] = project; m_projectModel->item(row)->setText(project.name); }
    if (m_projectIndex == row) selectProject(row);
    emit stateChanged();
}
QJsonObject Workbench::automationProjectState(const Project& disk) const
{
    for (const auto& project : m_projects) if (project.id == disk.id) {
        return {{"present", true}, {"project", projectJson(project)},
            {"matchesDisk", projectJson(project) == projectJson(disk) && project.storageRevisions == disk.storageRevisions},
            {"selected", activeProjectId() == disk.id}, {"editing", automationProjectEditing()},
            {"busy", m_preparing || m_scanning || m_session.busy()}};
    }
    return {{"present", false}, {"matchesDisk", false}};
}

void Workbench::selectProject(int row)
{
    cancelPreparation();
    if (m_projectIndex != row) m_session.stop();
    m_projectIndex = row;
    m_loading = true;
    m_projectList->setCurrentIndex(m_projectModel->index(row, 0));
    const auto* p = current();
    m_tbTop->setText(p ? p->tbName : QString());
    const qint64 duration = p ? p->durationNs : 1000;
    const int unit = duration % 1000000 == 0 ? 2 : (duration % 1000 == 0 ? 1 : 0);
    m_units->setCurrentIndex(unit);
    m_duration->setValue(int(duration / m_units->currentData().toLongLong()));
    m_loading = false;
    refreshFiles();
    updateControls();
    emit stateChanged();
}
void Workbench::refreshFiles()
{
    m_loading = true;
    const auto* p = current();
    QStringList paths = p ? p->sources : QStringList();
    const QSet<QString> selected(paths.begin(), paths.end());
    QSet<QString> available, seen = selected;
    for (const auto& source : m_scan.files) {
        available.insert(source.path);
        if (!seen.contains(source.path)) { paths << source.path; seen.insert(source.path); }
    }
    bool sameOrder = paths.size() == m_fileModel->rowCount();
    if (sameOrder) for (int row = 0; row < paths.size(); ++row)
        if (m_fileModel->item(row)->data(Qt::UserRole).toString() != paths[row]) { sameOrder = false; break; }
    const QString focused = m_fileList->currentIndex().data(Qt::UserRole).toString();
    const int scroll = m_fileList->verticalScrollBar()->value();
    if (!sameOrder) {
        m_fileList->setUpdatesEnabled(false);
        m_fileModel->clear();
        QList<QStandardItem*> rows;
        for (const auto& path : paths) {
            auto* item = new QStandardItem(path);
            item->setData(path, Qt::UserRole);
            item->setCheckable(true);
            item->setCheckState(selected.contains(path) ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(path);
            item->setSizeHint(QSize(200, 32));
            if (!available.contains(path) && !QFileInfo(QDir(m_root).filePath(path)).isFile()) {
                item->setText(path + QStringLiteral("  | File missing"));
                item->setForeground(QColor(QStringLiteral("#D14B4B")));
            }
            rows << item;
        }
        m_fileModel->invisibleRootItem()->appendRows(rows);
        const int focusedRow = paths.indexOf(focused);
        if (focusedRow >= 0) m_fileList->setCurrentIndex(m_fileModel->index(focusedRow, 0));
        m_fileList->verticalScrollBar()->setValue(scroll);
        m_fileList->setUpdatesEnabled(true);
    } else for (int row = 0; row < paths.size(); ++row) {
        auto* item = m_fileModel->item(row);
        const auto check = selected.contains(paths[row]) ? Qt::Checked : Qt::Unchecked;
        if (item->checkState() != check) item->setCheckState(check);
        const bool missing = !available.contains(paths[row]) && !QFileInfo(QDir(m_root).filePath(paths[row])).isFile();
        const QString caption = paths[row] + (missing ? QStringLiteral("  | File missing") : QString());
        if (item->text() != caption) item->setText(caption);
        const QBrush foreground = missing ? QBrush(QColor(QStringLiteral("#D14B4B"))) : QBrush();
        if (item->foreground() != foreground) item->setForeground(foreground);
    }
    m_loading = false;
    updateDutChoices();
}
void Workbench::toggleSource(const QModelIndex& index)
{
    if (!index.isValid() || m_scanning || m_session.busy() || m_requestingProject) return;
    if (current()) {
        auto* item = m_fileModel->itemFromIndex(index);
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        return;
    }
    const QString path = index.data(Qt::UserRole).toString();
    const QString root = m_root;
    m_requestingProject = true;
    // Create the project after the delegate has finished handling the mouse event.
    QTimer::singleShot(0, this, [this, path, root] {
        QPointer<Workbench> owner(this);
        const QString name = askProjectName(this);
        if (!owner) return;
        m_requestingProject = false;
        if (name.isEmpty() || root != m_root || !createProject(name)) return;
        for (int row = 0; row < m_fileModel->rowCount(); ++row) {
            auto* item = m_fileModel->item(row);
            if (item->data(Qt::UserRole).toString() != path) continue;
            item->setCheckState(Qt::Checked);
            break;
        }
    });
}
void Workbench::updateSources()
{
    if (!current() || m_session.busy()) return;
    current()->sources.clear();
    for (int i = 0; i < m_fileModel->rowCount(); ++i) {
        const auto* item = m_fileModel->item(i);
        if (item->checkState() == Qt::Checked) current()->sources << item->data(Qt::UserRole).toString();
    }
    updateDutChoices();
    saveCurrent();
    updateControls();
}
void Workbench::sourceChanged(QStandardItem* item)
{
    if (m_loading || !current() || m_session.busy()) return;
    if (item->checkState() != Qt::Checked) { updateSources(); return; }
    auto* p = current();
    const QString path = item->data(Qt::UserRole).toString();
    const auto selection = selectDependencies(m_dependencies, path, p->sources);
    p->sources = selection.sources;
    if (p->dutName.isEmpty()) {
        for (const auto& file : m_scan.files) {
            if (file.path != path || file.modules.isEmpty()) continue;
            p->dutFile = path;
            p->dutName = file.modules.first().name;
            break;
        }
    }
    m_loading = true;
    const QSet<QString> selected(p->sources.begin(), p->sources.end());
    QHash<QString, QStandardItem*> items;
    for (int row = 0; row < m_fileModel->rowCount(); ++row) {
        auto* source = m_fileModel->item(row);
        const auto file = source->data(Qt::UserRole).toString();
        items.insert(file, source);
        const auto check = selected.contains(file) ? Qt::Checked : Qt::Unchecked;
        if (source->checkState() != check) source->setCheckState(check);
    }
    for (int row = 0; row < p->sources.size(); ++row) {
        auto* source = items.value(p->sources[row]);
        if (source && source->row() != row) m_fileModel->insertRow(row, m_fileModel->takeRow(source->row()));
    }
    m_loading = false;
    m_fileList->setCurrentIndex(item->index());
    m_fileList->scrollTo(item->index());
    updateDutChoices();
    saveCurrent();
    updateControls();
    for (const auto& message : selection.messages) appendLog(message + QLatin1Char('\n'));
}
void Workbench::updateDutChoices()
{
    const QSignalBlocker blocker(m_dut);
    m_dut->clear();
    auto* p = current();
    if (!p) return;
    int choice = -1;
    const QSet<QString> selected(p->sources.begin(), p->sources.end());
    for (const auto& file : m_scan.files) if (selected.contains(file.path)) {
        for (const auto& module : file.modules) {
            if (file.path == p->tbFile) continue;
            m_dut->addItem(module.name + QStringLiteral("  |  ") + file.path, QStringList{file.path, module.name});
            if (p->dutFile == file.path && p->dutName == module.name) choice = m_dut->count() - 1;
        }
    }
    if (choice < 0 && m_dut->count() > 0) choice = 0;
    m_dut->setCurrentIndex(choice);
    if (choice >= 0) {
        const auto identity = m_dut->itemData(choice).toStringList();
        p->dutFile = identity[0]; p->dutName = identity[1];
    }
}
bool Workbench::saveCurrent()
{
    if (m_loading || !current()) return false;
    QString error;
    if (!saveProject(m_root, *current(), &error)) {
        appendLog(QStringLiteral("Settings could not be saved: %1\n").arg(error)); return false;
    }
    m_projectModel->item(m_projectIndex)->setText(current()->name);
    emit stateChanged();
    return true;
}
void Workbench::renameProject()
{
    if (!current() || m_session.busy() || m_preparing) return;
    QPointer<Workbench> owner(this);
    const auto id = current()->id;
    const auto name = askProjectName(this, current()->name);
    if (!owner || name.isEmpty() || !current() || current()->id != id) return;
    auto updated = *current(); updated.name = name;
    QString error;
    if (!saveProject(m_root, updated, &error)) { appendLog(error + QLatin1Char('\n')); return; }
    *current() = updated;
    m_projectModel->item(m_projectIndex)->setText(updated.name);
    updateControls(); emit stateChanged();
}
void Workbench::updateControls()
{
    m_projectList->setFixedHeight(qBound(42, m_projectModel->rowCount() * 38 + 4, 140));
    const bool busy = m_session.busy() || m_preparing;
    m_settings->setEnabled(!m_preparing);
    const auto* p = current();
    const auto tbPath = p ? p->tbFile : QString();
    const auto fullPath = tbPath.isEmpty() ? QString() : QDir::toNativeSeparators(QDir(m_root).filePath(tbPath));
    m_tbFile->setText(tbPath);
    m_tbFile->setToolTip(fullPath);
    m_new->setEnabled(!m_root.isEmpty() && !busy);
    m_refresh->setEnabled(!m_root.isEmpty() && !busy && !m_scanning);
    m_projectList->setEnabled(!busy);
    {
        // Keep browsing available while project source selection is locked.
        const QSignalBlocker blocker(m_fileModel);
        const bool checkable = !m_root.isEmpty() && !busy && !m_scanning;
        for (int row = 0; row < m_fileModel->rowCount(); ++row)
            if (m_fileModel->item(row)->isCheckable() != checkable) m_fileModel->item(row)->setCheckable(checkable);
    }
    m_fileList->viewport()->update();
    m_sourceHint->setText(p ? QStringLiteral("Select files and set their compilation order.")
        : QStringLiteral("Select a file to create a simulation project."));
    m_projectPanel->setEnabled(p && !busy && !m_scanning);
    m_emptyState->setVisible(!p);
    m_emptyState->setText(m_root.isEmpty() ? QStringLiteral("Open a ZeroSlack workspace to configure simulation.")
        : QStringLiteral("Create or select a project in Build inputs."));
    const bool tbAvailable = p && QFileInfo(QDir(m_root).filePath(p->tbFile)).isFile();
    m_openTb->setEnabled(tbAvailable);
    m_legacyNotice->setVisible(p && (!p->preservedInputs.isEmpty() || !tbAvailable));
    m_legacyNotice->setText(tbAvailable ? QStringLiteral("Saved graphical inputs are preserved. This project runs the existing TB.")
        : p && !p->preservedInputs.isEmpty()
            ? QStringLiteral("Choose an existing TB to run. Saved graphical inputs are retained; they are no longer regenerated.")
            : QStringLiteral("Choose an existing TB to run."));
    m_rename->setEnabled(p && !busy && !m_scanning);
    m_generate->setEnabled(p && currentModule() && !busy && !m_scanning);
    m_run->setEnabled(p && !p->sources.isEmpty() && tbAvailable && !busy && !m_scanning);
    m_stop->setEnabled(m_preparing || m_session.alive());
    m_stop->setText(m_preparing ? QStringLiteral("Cancel preparation") : busy ? QStringLiteral("Stop simulation") : QStringLiteral("Close session"));
}
void Workbench::chooseTb()
{
    auto* p = current();
    if (!p) return;
    QPointer<Workbench> owner(this);
    QPointer<QFileDialog> dialog = new QFileDialog(this, QStringLiteral("Select a TB inside the workspace"), m_root,
        QStringLiteral("HDL files (*.sv *.v)"));
    dialog->setFileMode(QFileDialog::ExistingFile);
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    const auto paths = dialog->selectedFiles();
    delete dialog;
    if (result != QDialog::Accepted || paths.isEmpty()) return;
    const auto path = paths.first();
    if (!insideWorkspace(m_root, path)) { appendLog(QStringLiteral("The TB must be inside the workspace.\n")); return; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) { appendLog(QStringLiteral("Could not read the TB file.\n")); return; }
    const auto source = analyzeSource(file.readAll(), QDir(m_root).relativeFilePath(path));
    p->tbFile = source.path;
    p->tbName = source.modules.size() == 1 ? source.modules.first().name : QFileInfo(path).completeBaseName();
    m_tbFile->setText(p->tbFile); m_tbTop->setText(p->tbName);
    saveCurrent(); selectProject(m_projectIndex);
}
bool Workbench::createDemo(const TbOptions& options, QString* error)
{
    auto* p = current();
    const auto* module = currentModule();
    if (!p || !module) { *error = QStringLiteral("Select a DUT first."); return false; }
    const auto duration = p->durationNs;
    auto semanticModule = *module;
    auto* semantic = SemanticIndex::getInstance();
    const auto file = QDir(m_root).filePath(module->file);
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly)) { *error = source.errorString(); return false; }
    if (!completeTestbenchTypes(semanticModule, *semantic, file, QString::fromUtf8(source.readAll()), error)) return false;
    const QString text = generateTestbench(semanticModule, options, duration, error);
    if (!error->isEmpty()) return false;
    const QString path = projectTbPath(*p, options.name);
    if (!writeNewTb(m_root, path, text, error)) return false;
    p->tbFile = path; p->tbName = options.name;
    p->durationNs = duration;
    m_tbFile->setText(path); m_tbTop->setText(options.name);
    saveCurrent();
    appendLog(QStringLiteral("Created %1\n").arg(path));
    selectProject(m_projectIndex);
    return true;
}
void Workbench::createTb()
{
    if (!current() || !currentModule()) return;
    if (m_editorTbCreator) {
        m_editorTbCreator(QDir(m_root).filePath(currentModule()->file), currentModule()->name);
        return;
    }
    auto module = *currentModule();
    auto* semantic = SemanticIndex::getInstance();
    const auto file = QDir(m_root).filePath(module.file);
    QString semanticError;
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly)) { appendLog(source.errorString()); return; }
    if (!completeTestbenchTypes(module, *semantic, file, QString::fromUtf8(source.readAll()), &semanticError)) {
        appendLog(semanticError + QLatin1Char('\n')); return;
    }
    QPointer<Workbench> owner(this);
    QPointer<TestbenchDialog> dialog = new TestbenchDialog(module, current()->durationNs, this);
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    const auto options = dialog->options();
    delete dialog;
    if (result != QDialog::Accepted) return;
    QString error;
    if (!createDemo(options, &error)) appendLog(error + QLatin1Char('\n'));
}
void Workbench::adoptCreatedTb(const QString& dutFile, const QString& dutName, const QString& tbFile, const QString& tbName)
{
    auto* p = current();
    if (!p || p->dutName != dutName || resolvedPath(QDir(m_root).filePath(p->dutFile)) != resolvedPath(dutFile)
        || !insideWorkspace(m_root, tbFile)) return;
    p->tbFile = QDir(m_root).relativeFilePath(tbFile); p->tbName = tbName;
    m_tbFile->setText(p->tbFile); m_tbTop->setText(tbName);
    saveCurrent(); selectProject(m_projectIndex);
}
void Workbench::cancelPreparation()
{
    if (m_preparationCancelled) m_preparationCancelled->store(true);
    ++m_preparationId;
    m_preparing = false;
}
void Workbench::startSimulation()
{
    if (!current() || m_preparing || m_scanning || m_session.busy()) return;
    QPointer<Workbench> owner(this);
    if (!simulatorError(m_simulator).isEmpty()) openSettings();
    if (!owner || !current()) return;
    const auto issue = simulatorError(m_simulator);
    if (!issue.isEmpty()) { appendLog(issue + QLatin1Char('\n')); return; }
    current()->tbName = m_tbTop->text().trimmed();
    if (!saveCurrent()) return;
    m_logTimer.stop(); m_pendingLog.clear(); m_log->clear();
    const auto project = *current();
    const auto root = m_root, executable = m_simulator;
    const auto generation = m_generation, request = ++m_preparationId;
    m_preparationCancelled = std::make_shared<std::atomic_bool>(false);
    m_preparing = true; m_status->setText(QStringLiteral("Preparing simulation…")); updateControls();
    auto* watcher = new QFutureWatcher<PreparedRun>(this);
    connect(watcher, &QFutureWatcher<PreparedRun>::finished, this, [this, watcher, project, generation, request] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != m_generation || request != m_preparationId) return;
        m_preparing = false;
        if (!current() || projectJson(*current()) != projectJson(project)) { updateControls(); return; }
        QString error = result.error;
        const auto theme = waveThemeFromId(preference(QStringLiteral("waveform/theme"), QStringLiteral("default")).toString());
        if (!error.isEmpty() || !m_session.start(result, &error, theme)) {
            appendLog(error + QLatin1Char('\n')); m_status->setText(QStringLiteral("Not started"));
        }
        updateControls();
    });
    watcher->setFuture(QtConcurrent::run(&m_scanPool, [root, executable, project, cancelled = m_preparationCancelled] {
        return prepareRun(executable, root, project, cancelled.get());
    }));
}
void Workbench::moveSource(int offset)
{
    if (m_session.busy() || m_scanning || !current()) return;
    const int row = m_fileList->currentIndex().row(), target = row + offset;
    if (row < 0 || target < 0 || target >= m_fileModel->rowCount()) return;
    m_loading = true;
    auto items = m_fileModel->takeRow(row);
    m_fileModel->insertRow(target, items);
    m_fileList->setCurrentIndex(m_fileModel->index(target, 0));
    m_loading = false;
    updateSources();
}
void Workbench::appendLog(const QString& text)
{
    m_pendingLog += text;
    if (m_pendingLog.size() >= 1024 * 1024) flushLog();
    else if (!m_logTimer.isActive()) m_logTimer.start();
}
void Workbench::flushLog()
{
    m_logTimer.stop();
    if (m_pendingLog.isEmpty()) return;
    auto* bar = m_log->verticalScrollBar();
    const int position = bar->value();
    const bool follow = position >= bar->maximum();
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    cursor.insertText(m_pendingLog);
    cursor.endEditBlock();
    m_pendingLog.clear();
    bar->setValue(follow ? bar->maximum() : position);
}
void Workbench::applyTheme()
{
    Ui::applyTheme(this);
    if (m_logs) Ui::applyTheme(m_logs);
    update();
}
void Workbench::setDarkTheme(bool dark)
{
    m_hostDarkTheme = dark;
    applyTheme();
    // A host can change theme while Settings is already open.
    for (auto* dialog : findChildren<QDialog*>()) {
        if (dialog->objectName() != QStringLiteral("settingsDialog")) continue;
        if (auto* selector = dialog->findChild<QComboBox*>(QStringLiteral("themeSelector"))) {
            selector->setCurrentIndex(dark ? 1 : 0);
            selector->setEnabled(false);
            selector->setToolTip(QStringLiteral("Theme follows the host application."));
        }
    }
}
QVariant Workbench::preference(const QString& key, const QVariant& fallback) const
{
    return m_standalone ? QSettings().value(key, fallback) : m_preferences.value(key, fallback);
}
void Workbench::setPreference(const QString& key, const QVariant& value)
{
    if (preference(key) == value) return;
    if (m_standalone) QSettings().setValue(key, value);
    else m_preferences.insert(key, value);
    emit preferencesChanged();
}
QString Workbench::contextError() const
{
    if (m_requestingProject) return QStringLiteral("Finish or cancel the project dialog first.");
    for (const auto* dialog : findChildren<QDialog*>())
        if (dialog->isVisible()) return QStringLiteral("Finish or cancel the open dialog first.");
    return {};
}
bool Workbench::canClose() const { return contextError().isEmpty(); }
QString Workbench::openProject(const QString& id)
{
    const auto issue = contextError();
    if (!issue.isEmpty()) return issue;
    if ((current() && current()->id == id) || (!current() && id.isEmpty())) return {};
    if (id.isEmpty()) { selectProject(-1); return {}; }
    for (int i = 0; i < m_projects.size(); ++i)
        if (m_projects[i].id == id) { selectProject(i); return {}; }
    return QStringLiteral("The simulation project no longer exists in this workspace.");
}
QVariantMap Workbench::saveState() const
{
    QVariantMap preferences;
    for (const auto& key : {"simulator/path", "waveform/theme", "theme/dark"}) {
        const auto value = preference(QString::fromLatin1(key));
        if (value.isValid()) preferences.insert(QString::fromLatin1(key), value);
    }
    return {{QStringLiteral("version"), 1}, {QStringLiteral("workspace"), m_root},
        {QStringLiteral("projectId"), m_projectIndex >= 0 && m_projectIndex < m_projects.size() ? m_projects[m_projectIndex].id : QString()},
        {QStringLiteral("preferences"), preferences}};
}
void Workbench::restoreLayout(const QVariantMap&)
{
    // Old waveform disclosure state has no corresponding control.
}
QString Workbench::restoreState(const QVariantMap& state)
{
    if (state.value(QStringLiteral("version")).toInt() != 1) return QStringLiteral("Unsupported SimDock workbench state version.");
    const auto issue = contextError();
    if (!issue.isEmpty()) return issue;
    const auto root = state.value(QStringLiteral("workspace")).toString();
    const auto id = state.value(QStringLiteral("projectId")).toString();
    if (!id.isEmpty()) {
        bool found = false;
        if (!root.isEmpty()) for (const auto& p : loadProjects(root)) if (p.id == id) found = true;
        if (!found) return QStringLiteral("The saved simulation project no longer exists.");
    }
    // Restoring presentation on the same workspace must not restart analysis
    // or reset source view selection. Explicit Rescan still uses setContext.
    const auto canonical = root.isEmpty() ? QString() : QFileInfo(root).canonicalFilePath();
    if (canonical != m_root || (!root.isEmpty() && canonical.isEmpty())) {
        const auto error = setContext(root);
        if (!error.isEmpty()) return error;
    }
    openProject(id);
    const auto preferences = state.value(QStringLiteral("preferences")).toMap();
    for (const auto& key : {"simulator/path", "waveform/theme", "theme/dark"}) {
        const auto name = QString::fromLatin1(key);
        if (preferences.contains(name) && !(m_hostDarkTheme.has_value() && name == QStringLiteral("theme/dark")))
            setPreference(name, preferences.value(name));
    }
    setSimulator(preference(QStringLiteral("simulator/path")).toString());
    restoreLayout(state);
    return {};
}
void Workbench::savePreferences()
{
    // Preferences are persisted by their individual setters.
}
void Workbench::shutdown()
{
    cancelPreparation();
    if (m_scanCancelled) m_scanCancelled->store(true);
    ++m_generation;
    m_logTimer.stop();
    m_session.stop();
}
}
