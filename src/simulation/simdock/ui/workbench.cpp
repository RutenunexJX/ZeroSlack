#include "../core/stimulus.h"
#include "stimulusdialog.h"
#include "workbench.h"
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
}

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
    m_inputMode = new ElaComboBox(this);
    m_inputMode->setObjectName(QStringLiteral("simulationInputMode"));
    m_inputMode->addItem(QStringLiteral("Graphical stimulus"), int(InputMode::Graphical));
    m_inputMode->addItem(QStringLiteral("Existing testbench"), int(InputMode::ExistingTb));
    form->addWidget(m_inputMode);
    m_graphicalInput = new QWidget(this);
    auto* graphical = new QVBoxLayout(m_graphicalInput);
    graphical->setContentsMargins(0, 0, 0, 0);
    m_stimulus = Ui::button(QStringLiteral("Draw stimulus"), this);
    m_stimulus->setObjectName(QStringLiteral("editStimulus"));
    graphical->addWidget(m_stimulus);
    auto* details = Ui::disclosure(QStringLiteral("Generated TB details"), this);
    details->setObjectName(QStringLiteral("generatedTbDetailsToggle"));
    details->setChecked(false);
    m_generatedPath = new ElaLineEdit(this);
    m_generatedPath->setObjectName(QStringLiteral("generatedTbPath"));
    m_generatedPath->setReadOnly(true);
    m_generatedPath->setPlaceholderText(QStringLiteral("Generated when stimulus is saved"));
    graphical->addWidget(details);
    graphical->addWidget(m_generatedPath);
    m_generatedPath->hide();
    connect(details, &QToolButton::toggled, m_generatedPath, &QWidget::setVisible);
    form->addWidget(m_graphicalInput);
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
    form->addWidget(Ui::label(QStringLiteral("Duration"), this, Ui::Role::Section));
    m_graphicalDuration = Ui::label(QString(), this, Ui::Role::Body);
    m_graphicalDuration->setObjectName(QStringLiteral("graphicalDuration"));
    m_graphicalDuration->setToolTip(QStringLiteral("Change duration in Edit stimulus > Timing"));
    form->addWidget(m_graphicalDuration);
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
    auto* waveToggle = Ui::disclosure(QStringLiteral("Waveform options"), this);
    waveToggle->setObjectName(QStringLiteral("waveOptionsToggle"));
    waveToggle->setChecked(false);
    form->addWidget(waveToggle);
    auto* waveOptions = new QWidget(this);
    auto* waves = new QVBoxLayout(waveOptions);
    waves->setContentsMargins(0, 0, 0, 0);
    waves->addWidget(Ui::label(QStringLiteral("Waveform scope"), this, Ui::Role::Section));
    m_waveScope = new ElaComboBox(this);
    m_waveScope->setObjectName(QStringLiteral("waveScope"));
    m_waveScope->addItem(QStringLiteral("Interface (TB top)"), QStringLiteral("interface"));
    m_waveScope->addItem(QStringLiteral("Selected signals"), QStringLiteral("selected"));
    m_waveScope->addItem(QStringLiteral("All signals"), QStringLiteral("all"));
    m_waveSignals = Ui::button(QStringLiteral("Choose signals"), this);
    m_waveSignals->setObjectName(QStringLiteral("chooseWaveSignals"));
    waves->addWidget(m_waveScope);
    waves->addWidget(m_waveSignals);
    form->addWidget(waveOptions);
    waveOptions->hide();
    connect(waveToggle, &QToolButton::toggled, waveOptions, &QWidget::setVisible);
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
    connect(m_inputMode, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !current()) return;
        setInputMode(*current(), InputMode(m_inputMode->currentData().toInt()));
        saveCurrent();
        selectProject(m_projectIndex);
    });
    connect(m_waveScope, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !current()) return;
        current()->waveScope = m_waveScope->currentData().toString();
        saveCurrent(); updateControls();
    });
    connect(m_waveSignals, &QPushButton::clicked, this, &Workbench::chooseWaveSignals);
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
            p->tbFile.clear(); p->tbName.clear(); p->stimulus = {}; p->alternateInput = {};
            m_tbFile->clear(); m_tbTop->clear();
        }
        const auto selection = selectDependencies(m_dependencies, identity[0], p->sources);
        p->sources = selection.sources;
        for (const auto& message : selection.messages) appendLog(message + QLatin1Char('\n'));
        refreshFiles();
        saveCurrent(); updateControls();
    });
    connect(m_tbTop, &QLineEdit::editingFinished, this, [this] {
        if (auto* p = current(); p && p->inputMode == InputMode::ExistingTb) { p->tbName = m_tbTop->text().trimmed(); saveCurrent(); updateControls(); }
    });
    auto durationChanged = [this] {
        if (m_loading || !current() || current()->inputMode != InputMode::ExistingTb) return;
        current()->durationNs = qint64(m_duration->value()) * m_units->currentData().toLongLong();
        saveCurrent(); updateControls();
    };
    connect(m_duration, &QSpinBox::valueChanged, this, durationChanged);
    connect(m_units, &QComboBox::currentIndexChanged, this, durationChanged);
    connect(m_stimulus, &QPushButton::clicked, this, &Workbench::editStimulus);
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
void Workbench::selectProject(int row)
{
    if (m_session.busy()) return;
    cancelPreparation();
    m_projectIndex = row;
    m_loading = true;
    m_projectList->setCurrentIndex(m_projectModel->index(row, 0));
    const auto* p = current();
    m_inputMode->setCurrentIndex(m_inputMode->findData(int(p ? p->inputMode : InputMode::Graphical)));
    m_tbTop->setText(p ? p->tbName : QString());
    m_waveScope->setCurrentIndex(qMax(0, m_waveScope->findData(p ? p->waveScope : QStringLiteral("interface"))));
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
        if (!p->dutName.isEmpty() && (p->dutName != identity[1] || p->dutFile != identity[0])) {
            p->tbFile.clear(); p->tbName.clear(); p->stimulus = {}; p->alternateInput = {}; m_tbFile->clear(); m_tbTop->clear();
        }
        p->dutFile = identity[0]; p->dutName = identity[1];
    }
}
void Workbench::saveCurrent()
{
    if (m_loading || !current()) return;
    QString error;
    if (!saveProject(m_root, *current(), &error)) appendLog(QStringLiteral("Settings could not be saved: %1\n").arg(error));
    emit stateChanged();
}
void Workbench::updateControls()
{
    m_projectList->setFixedHeight(qBound(42, m_projectModel->rowCount() * 38 + 4, 140));
    const bool busy = m_session.busy() || m_preparing;
    m_settings->setEnabled(!m_preparing);
    const auto* p = current();
    const auto tbPath = p ? p->tbFile : QString();
    const auto fullPath = tbPath.isEmpty() ? QString() : QDir::toNativeSeparators(QDir(m_root).filePath(tbPath));
    for (auto* field : {m_tbFile, m_generatedPath}) {
        field->setText(tbPath);
        field->setToolTip(fullPath);
    }
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
    m_stimulus->setEnabled(p && currentModule() && !busy && !m_scanning);
    const bool graphical = !p || p->inputMode == InputMode::Graphical;
    m_graphicalInput->setVisible(graphical);
    m_existingInput->setVisible(!graphical);
    m_durationInput->setVisible(!graphical);
    m_graphicalDuration->setVisible(graphical);
    m_graphicalDuration->setText(QStringLiteral("%1 ns").arg(p ? p->durationNs : 1000));
    m_emptyState->setVisible(!p);
    m_emptyState->setText(m_root.isEmpty() ? QStringLiteral("Open a ZeroSlack workspace to configure simulation.")
        : QStringLiteral("Create or select a project in Build inputs."));
    m_openTb->setEnabled(p && !p->tbFile.isEmpty());
    m_stimulus->setText(p && !p->stimulus.isEmpty() ? QStringLiteral("Edit stimulus") : QStringLiteral("Draw stimulus"));
    m_tbTop->setReadOnly(graphical);
    m_duration->setEnabled(!graphical); m_units->setEnabled(!graphical);
    m_duration->setToolTip(graphical ? QStringLiteral("Change duration in Edit stimulus > Timing") : QString());
    m_generate->setEnabled(p && currentModule() && !busy && !m_scanning);
    m_run->setEnabled(p && !p->sources.isEmpty() && !p->tbFile.isEmpty() && !busy && !m_scanning);
    m_waveSignals->setVisible(p && p->waveScope == QStringLiteral("selected"));
    m_waveSignals->setEnabled(p && p->waveScope == QStringLiteral("selected") && !busy);
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
    setInputMode(*p, InputMode::ExistingTb);
    p->stimulus = {};
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
    const QString text = generateTestbench(*module, options, duration, error);
    if (!error->isEmpty()) return false;
    const QString path = projectTbPath(*p, options.name);
    if (!writeNewTb(m_root, path, text, error)) return false;
    setInputMode(*p, InputMode::ExistingTb);
    p->stimulus = {};
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
    QPointer<Workbench> owner(this);
    QPointer<TestbenchDialog> dialog = new TestbenchDialog(*currentModule(), current()->durationNs, this);
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    const auto options = dialog->options();
    delete dialog;
    if (result != QDialog::Accepted) return;
    QString error;
    if (!createDemo(options, &error)) appendLog(error + QLatin1Char('\n'));
}
void Workbench::cancelPreparation()
{
    if (m_preparationCancelled) m_preparationCancelled->store(true);
    ++m_preparationId;
    m_preparing = false;
}
void Workbench::editStimulus()
{
    const auto* p = current();
    if (!p || !currentModule() || m_session.busy() || m_preparing || m_scanning) return;
    const auto project = *p;
    const auto root = m_root;
    const auto generation = m_generation, request = ++m_preparationId;
    m_preparationCancelled = std::make_shared<std::atomic_bool>(false);
    m_preparing = true;
    m_status->setText(QStringLiteral("Preparing stimulus…")); updateControls();
    using Result = std::pair<std::shared_ptr<const PreparedStimulus>, QString>;
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, project, generation, request] {
        const auto [prepared, error] = watcher->result(); watcher->deleteLater();
        if (generation != m_generation || request != m_preparationId) return;
        m_preparing = false; updateControls();
        if (!current() || projectJson(*current()) != projectJson(project)) return;
        if (!error.isEmpty() || !prepared) {
            appendLog(error + QLatin1Char('\n')); m_status->setText(QStringLiteral("Preparation failed")); return;
        }
        m_preparedStimulus = prepared;
        m_status->setText(QStringLiteral("Editing stimulus"));
        QPointer<Workbench> owner(this);
        QPointer<StimulusDialog> dialog = new StimulusDialog(prepared->module, prepared->semantics,
            project.stimulus, project.durationNs,
            [this, prepared, project, generation](const QJsonObject& drawing, const QString& content, QString* issue) {
                if (m_generation != generation || !current() || current()->id != project.id) {
                    *issue = QStringLiteral("The simulation project changed. Reopen the drawing."); return false;
                }
                if (!saveGeneratedStimulus(m_root, *current(), drawing, content, issue)) return false;
                m_generatedDesign = m_preparedStimulus; m_generatedDrawing = drawing; m_generatedTb = content;
                return true;
            }, this);
        auto saveCancelled = m_preparationCancelled;
        connect(dialog, &QDialog::finished, this, [saveCancelled] { saveCancelled->store(true); });
        dialog->setSavePreparer([this, prepared, project, saveCancelled](const QJsonObject& drawing, StimulusDialog::SaveCompletion done) {
            struct Generated { std::shared_ptr<const PreparedStimulus> design; QString content, error; };
            auto* generated = new QFutureWatcher<Generated>(this);
            connect(generated, &QFutureWatcher<Generated>::finished, this, [this, generated, saveCancelled, done = std::move(done)] {
                const auto result = generated->result(); generated->deleteLater();
                if (saveCancelled->load()) return;
                if (result.design) m_preparedStimulus = result.design;
                done(result.content, result.error);
            });
            generated->setFuture(QtConcurrent::run(&m_scanPool, [prepared, drawing, project, saveCancelled, cache = &m_sourceCache] {
                Generated result;
                result.design = prepareStimulus(prepared->scan.root, project, prepared, cache, saveCancelled.get(), &result.error);
                if (result.design)
                    result.content = stimulusTestbench(result.design->module, result.design->semantics, drawing, &result.error);
                return result;
            }));
        });
        const auto result = dialog->exec();
        if (!owner || !dialog) return;
        const bool run = dialog->runRequested(); delete dialog;
        m_status->setText(QStringLiteral("Ready"));
        if (result != QDialog::Accepted) return;
        selectProject(m_projectIndex);
        appendLog(QStringLiteral("Saved input waveforms and generated %1\n").arg(current()->tbFile));
        if (run) startSimulation();
    });
    watcher->setFuture(QtConcurrent::run(&m_scanPool,
        [root, project, previous = m_preparedStimulus, cancelled = m_preparationCancelled, cache = &m_sourceCache] {
            QString error;
            auto prepared = prepareStimulus(root, project, previous, cache, cancelled.get(), &error);
            return Result{prepared, error};
        }));
}
void Workbench::startSimulation()
{
    if (!current() || m_preparing || m_scanning || m_session.busy()) return;
    QPointer<Workbench> owner(this);
    if (!simulatorError(m_simulator).isEmpty()) openSettings();
    if (!owner || !current()) return;
    const auto issue = simulatorError(m_simulator);
    if (!issue.isEmpty()) { appendLog(issue + QLatin1Char('\n')); return; }
    if (current()->stimulus.isEmpty()) { current()->tbName = m_tbTop->text().trimmed(); saveCurrent(); }
    const auto project = *current();
    const auto root = m_root, executable = m_simulator;
    const auto generation = m_generation, request = ++m_preparationId;
    m_preparationCancelled = std::make_shared<std::atomic_bool>(false);
    m_preparing = true; m_status->setText(QStringLiteral("Preparing simulation…")); updateControls();
    struct Result { PreparedRun run; std::shared_ptr<const PreparedStimulus> design; QString content; };
    auto* watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, project, generation, request] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != m_generation || request != m_preparationId) return;
        m_preparing = false;
        if (!current() || projectJson(*current()) != projectJson(project)) { updateControls(); return; }
        QString error = result.run.error;
        if (error.isEmpty() && !project.stimulus.isEmpty()) {
            if (saveGeneratedStimulus(m_root, *current(), project.stimulus, result.content, &error)) {
                m_preparedStimulus = result.design; m_generatedDesign = result.design;
                m_generatedDrawing = project.stimulus; m_generatedTb = result.content;
                m_tbFile->setText(current()->tbFile);
            }
        }
        const auto theme = waveThemeFromId(preference(QStringLiteral("waveform/theme"), QStringLiteral("default")).toString());
        if (!error.isEmpty() || !m_session.start(result.run, &error, theme)) {
            appendLog(error + QLatin1Char('\n')); m_status->setText(QStringLiteral("Not started"));
        }
        updateControls();
    });
    watcher->setFuture(QtConcurrent::run(&m_scanPool,
        [root, executable, project, previous = m_preparedStimulus, generated = m_generatedDesign,
         drawing = m_generatedDrawing, content = m_generatedTb, cancelled = m_preparationCancelled, cache = &m_sourceCache] {
            Result result;
            auto updated = project;
            QMap<QString, QByteArray> overrides;
            if (!project.stimulus.isEmpty()) {
                result.design = prepareStimulus(root, project, previous, cache, cancelled.get(), &result.run.error);
                if (!result.design) return result;
                result.content = result.design == generated && drawing == project.stimulus ? content
                    : stimulusTestbench(result.design->module, result.design->semantics, project.stimulus, &result.run.error);
                if (!result.run.error.isEmpty()) return result;
                updated = generatedStimulusProject(project, project.stimulus, result.content);
                overrides.insert(updated.tbFile, result.content.toUtf8());
            }
            if (cancelled->load()) { result.run.error = QStringLiteral("Preparation cancelled."); return result; }
            result.run = prepareRun(executable, root, updated, cancelled.get(), overrides);
            if (result.run.error.isEmpty() && result.design) {
                for (const auto& source : result.design->scan.files) {
                    const auto path = QDir::cleanPath(QDir(root).filePath(source.path));
                    if (result.run.inputFingerprints.value(path) != QCryptographicHash::hash(source.content, QCryptographicHash::Sha256)) {
                        result.run.error = QStringLiteral("Sources changed during preparation. Try again."); break;
                    }
                }
                if (!stimulusIncludesCurrent(result.design->semantics, [cancelled] { return cancelled->load(); }))
                    result.run.error = QStringLiteral("Sources changed during preparation. Try again.");
            }
            return result;
        }));
}
void Workbench::chooseWaveSignals()
{
    if (!current() || m_preparing || m_session.busy()) return;
    QPointer<Workbench> owner(this);
    QPointer<ElaDialog> dialog = new ElaDialog(this);
    dialog->setWindowTitle(QStringLiteral("Choose waveform signals"));
    dialog->setObjectName(QStringLiteral("waveSignalsDialog"));
    dialog->resize(420, 500);
    auto* layout = new QVBoxLayout(dialog);
    auto* list = new ElaListView(dialog);
    auto* model = new QStandardItemModel(list);
    list->setModel(model);
    QStringList names = current()->waveSignals;
    if (const auto* module = currentModule()) for (const auto& port : module->ports)
        if (!names.contains(port.name)) names << port.name;
    for (const auto& name : names) {
        auto* item = new QStandardItem(name);
        item->setCheckable(true); item->setEditable(false);
        item->setCheckState(current()->waveSignals.contains(name) ? Qt::Checked : Qt::Unchecked);
        model->appendRow(item);
    }
    layout->addWidget(list);
    auto* buttons = new QHBoxLayout;
    auto* cancel = Ui::button(QStringLiteral("Cancel"), dialog);
    auto* save = Ui::button(QStringLiteral("Apply"), dialog, true);
    buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(save); layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(save, &QPushButton::clicked, dialog, &QDialog::accept);
    Ui::constrainDialog(dialog);
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    if (result == QDialog::Accepted && current()) {
        QStringList selected;
        for (int i = 0; i < model->rowCount(); ++i)
            if (model->item(i)->checkState() == Qt::Checked) selected << model->item(i)->text();
        current()->waveSignals = selected; saveCurrent();
    }
    delete dialog;
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
    if (m_session.alive()) return QStringLiteral("Close the active Questa session before changing workspace or project.");
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
        {QStringLiteral("preferences"), preferences},
        {QStringLiteral("layout/waveOptionsExpanded"), findChild<QToolButton*>(QStringLiteral("waveOptionsToggle"))->isChecked()}};
}
void Workbench::restoreLayout(const QVariantMap& state)
{
    findChild<QToolButton*>(QStringLiteral("waveOptionsToggle"))->setChecked(state.value(QStringLiteral("layout/waveOptionsExpanded"), false).toBool());
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
    const auto state = saveState();
    for (const auto& key : {"layout/waveOptionsExpanded"}) {
        const auto name = QString::fromLatin1(key);
        setPreference(name, state.value(name));
    }
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
