#include "workbench.h"
#include <QMessageBox>
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
#include "sidebarsectionbutton.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
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
#include <QJsonArray>
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
    m_orderTimer.setSingleShot(true); m_orderTimer.setInterval(120);
    connect(&m_orderTimer, &QTimer::timeout, this, &Workbench::refreshCompilationOrder);
    connect(&m_inputWatcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleCompilationOrder(); });
    connect(&m_inputWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleCompilationOrder(); });
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
        m_status->setText(state==QStringLiteral("Failed")?tr("Simulation failed. See the Run log.")
            :state==QStringLiteral("Timed out")?tr("Simulation timed out. See the Run log."):state);
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
    outer->setSpacing(6);
    auto* heading = new QHBoxLayout;
    if(m_standalone)heading->addWidget(Ui::label(QStringLiteral("Simulation"), this, Ui::Role::PanelTitle), 1);
    else heading->addStretch();
    auto* auxiliary = UiControls::toolButton(this);
    auxiliary->setObjectName(QStringLiteral("simulationActions"));
    auxiliary->setIcon(RoundedIcons::icon(RoundedIcons::More));
    auxiliary->setAccessibleName(QStringLiteral("Simulation actions"));
    auxiliary->setToolTip(QStringLiteral("Simulation actions"));
    auto* menu = UiControls::menu(this);
    m_generate = menu->addAction(QStringLiteral("Create Demo TB"));
    m_generate->setObjectName(QStringLiteral("createTb"));
    m_closeSession=menu->addAction(QStringLiteral("Close session"));
    m_closeSession->setObjectName(QStringLiteral("closeSimulationSession"));
    m_legacyInfo=menu->addAction(QStringLiteral("Saved graphical inputs…"));
    m_legacyInfo->setObjectName(QStringLiteral("savedGraphicalInputs"));
    connect(m_legacyInfo,&QAction::triggered,this,[this]{
        QMessageBox::information(this,QStringLiteral("Saved graphical inputs"),
            QStringLiteral("Saved graphical inputs are preserved. Simulation uses the selected TB; graphical inputs are no longer regenerated."));
    });
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
    form->setSpacing(6);
    form->addWidget(Ui::label(QStringLiteral("Testbench"), this, Ui::Role::Section));
    m_existingInput = new QWidget(this);
    auto* existing = new QVBoxLayout(m_existingInput);
    existing->setContentsMargins(0, 0, 0, 0);
    m_tbFile = new ElaLineEdit(this);
    m_tbFile->setObjectName(QStringLiteral("tbFile"));
    m_tbFile->setReadOnly(true);
    m_tbFile->setPlaceholderText(QStringLiteral("Select a TB inside the workspace"));
    m_tbFile->setAccessibleName(QStringLiteral("Testbench file"));
    m_tbFile->setMinimumWidth(0);m_tbFile->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    auto* tbActions = new QHBoxLayout;
    tbActions->setSpacing(4);tbActions->addWidget(m_tbFile,1);
    m_openTb = UiControls::toolButton(this);
    m_openTb->setText(QStringLiteral("Open"));m_openTb->setAccessibleName(QStringLiteral("Open TB"));m_openTb->setToolTip(QStringLiteral("Open TB"));
    m_openTb->setObjectName(QStringLiteral("openTb"));
    m_chooseTb = UiControls::toolButton(this);
    m_chooseTb->setText(QStringLiteral("Choose…"));m_chooseTb->setAccessibleName(QStringLiteral("Choose TB"));m_chooseTb->setToolTip(QStringLiteral("Choose TB"));
    m_chooseTb->setObjectName(QStringLiteral("chooseTb"));
    tbActions->addWidget(m_openTb);
    tbActions->addWidget(m_chooseTb);
    existing->addLayout(tbActions);
    auto* topRow=new QHBoxLayout;
    topRow->addWidget(Ui::label(QStringLiteral("TB top"), this, Ui::Role::Section));
    m_tbTop = new ElaLineEdit(this);
    m_tbTop->setObjectName(QStringLiteral("tbTop"));
    m_tbTop->setPlaceholderText(QStringLiteral("tb_top"));
    m_tbTop->setAccessibleName(QStringLiteral("Simulation top module"));m_tbTop->setMinimumWidth(0);m_tbTop->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    topRow->addWidget(m_tbTop,1);existing->addLayout(topRow);
    form->addWidget(m_existingInput);
    m_inputNotice = Ui::label(QString(), this, Ui::Role::Metadata);
    m_inputNotice->setObjectName(QStringLiteral("simulationInputNotice"));
    m_inputNotice->setWordWrap(true);
    form->addWidget(m_inputNotice);
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
    auto* demoHeading=new SidebarSectionButton(QStringLiteral("Demo TB configuration"),m_projectPanel);
    demoHeading->setObjectName(QStringLiteral("demoTbConfiguration"));demoHeading->setChecked(false);form->addWidget(demoHeading);
    auto* demoBody=new QWidget(m_projectPanel);auto* demoLayout=new QVBoxLayout(demoBody);demoLayout->setContentsMargins(0,0,0,0);demoLayout->setSpacing(4);
    demoLayout->addWidget(Ui::label(QStringLiteral("DUT module"),demoBody,Ui::Role::Section));
    m_dut=new ElaComboBox(demoBody);m_dut->setObjectName(QStringLiteral("dutSelector"));
    m_dut->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_dut->setMinimumContentsLength(1);
    m_dut->setMinimumWidth(0);m_dut->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    m_dut->setPlaceholderText(QStringLiteral("Select build inputs first"));m_dut->setAccessibleName(QStringLiteral("DUT for Demo TB"));demoLayout->addWidget(m_dut);
    form->addWidget(demoBody);demoBody->hide();connect(demoHeading,&QPushButton::toggled,demoBody,&QWidget::setVisible);
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
    auto* projectsLabel = new SidebarSectionButton(QStringLiteral("Projects"), projects);
    m_projectsHeading = projectsLabel;
    projectsLabel->setObjectName(QStringLiteral("projectsDisclosure"));
    projectHeading->addWidget(projectsLabel,1);
    m_new = UiControls::pushButton(QString(), projects);
    m_new->setIcon(RoundedIcons::icon(RoundedIcons::ZoomIn));
    m_new->setFixedSize(28,28);
    m_new->setAccessibleName(tr("New project"));
    m_new->setToolTip(tr("New project"));
    m_new->setObjectName(QStringLiteral("newProject"));
    projectHeading->addWidget(m_new);
    m_rename = UiControls::pushButton(QString(), projects);
    m_rename->setObjectName(QStringLiteral("renameProject"));
    m_rename->setIcon(RoundedIcons::icon(RoundedIcons::Rename));
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
    m_projectsBody = m_projectList;
    projectsLabel->setChecked(QSettings().value("integrations/simdock/projectsExpanded", true).toBool());
    m_projectList->setVisible(projectsLabel->isChecked());
    connect(projectsLabel, &QPushButton::toggled, this, [this](bool expanded) {
        m_projectsBody->setVisible(expanded);
        QSettings().setValue("integrations/simdock/projectsExpanded", expanded);
        emit stateChanged();
    });

    m_projectList->setMinimumHeight(42);
    sourceLayout->addWidget(projects);
    auto* files = new QWidget(sources);
    files->setObjectName(QStringLiteral("sourcePanel"));
    auto* fileLayout = new QVBoxLayout(files);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(6);
    auto* fileHeading = new QHBoxLayout;
    auto* inputsLabel = new SidebarSectionButton(QStringLiteral("Build inputs"), files);
    m_inputsHeading = inputsLabel;
    inputsLabel->setObjectName(QStringLiteral("inputsDisclosure"));
    fileHeading->addWidget(inputsLabel, 1);
    m_refresh = UiControls::pushButton(QString(), files);
    m_refresh->setIcon(RoundedIcons::icon(RoundedIcons::Refresh));
    m_refresh->setFixedSize(28,28);
    m_refresh->setAccessibleName(tr("Rescan build inputs"));
    m_refresh->setToolTip(tr("Rescan build inputs"));
    m_refresh->setObjectName(QStringLiteral("refreshWorkspace"));
    fileHeading->addWidget(m_refresh);
    fileLayout->addLayout(fileHeading);
    m_inputsBody = new QWidget(files);
    auto* inputLayout = new QVBoxLayout(m_inputsBody);
    inputLayout->setContentsMargins(0, 0, 0, 0); inputLayout->setSpacing(4);
    fileLayout->addWidget(m_inputsBody, 1);
    m_sourceHint = UiControls::label(QString(), files);
    m_sourceHint->setObjectName(QStringLiteral("buildInputsStatus"));
    UiTypography::apply(m_sourceHint, UiTypography::Role::Metadata);
    m_sourceHint->setWordWrap(true);
    inputLayout->addWidget(m_sourceHint);
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
    inputLayout->addWidget(m_fileList, 1);
    m_fileList->setMinimumHeight(80);
    m_scanSummary=QStringLiteral("Open a workspace to add build inputs.");
    sourceLayout->addWidget(files, 1);
    sourceLayout->addStretch();
    inputsLabel->setChecked(QSettings().value("integrations/simdock/inputsExpanded", true).toBool());
    m_inputsBody->setVisible(inputsLabel->isChecked());
    const auto updateSections = [this, sourceLayout, files, projects, projectsLabel, inputsLabel] {
        const bool inputs = inputsLabel->isChecked(), projectsOpen = projectsLabel->isChecked();
        sourceLayout->setStretchFactor(files, inputs ? 1 : 0);
        sourceLayout->setStretchFactor(projects, projectsOpen && !inputs ? 1 : 0);
        sourceLayout->setStretch(sourceLayout->count() - 1, !inputs && !projectsOpen ? 1 : 0);
        updateControls();
    };
    connect(projectsLabel, &QPushButton::toggled, this, updateSections);
    connect(inputsLabel, &QPushButton::toggled, this, [this, updateSections](bool expanded) {
        m_inputsBody->setVisible(expanded);
        updateSections();
        QSettings().setValue("integrations/simdock/inputsExpanded", expanded);
        emit stateChanged();
    });
    QTimer::singleShot(0, this, updateSections);
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
    connect(m_openTb, &QToolButton::clicked, this, [this] {
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
        if (!p->sources.contains(identity[0])) p->sources << identity[0];
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
    connect(m_chooseTb, &QToolButton::clicked, this, &Workbench::chooseTb);
    connect(m_run, &QPushButton::clicked, this, &Workbench::startSimulation);
    connect(m_stop, &QPushButton::clicked, this, [this] {
        if (m_preparing) {
            cancelPreparation(); m_status->setText(QStringLiteral("Preparation cancelled"));
        } else {
            m_session.stop(); m_status->setText(QStringLiteral("Session closed"));
        }
        updateControls();
    });
    connect(m_closeSession,&QAction::triggered,m_stop,&QPushButton::click);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_logTimer.stop();
        m_pendingLog.clear();
        m_log->clear();
    });
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
    ++m_orderGeneration;
    if (m_orderCancelled) m_orderCancelled->store(true);
    m_orderTimer.stop();
    if (!m_inputWatcher.files().isEmpty()) m_inputWatcher.removePaths(m_inputWatcher.files());
    if (!m_inputWatcher.directories().isEmpty()) m_inputWatcher.removePaths(m_inputWatcher.directories());
    if (root.isEmpty()) {
        cancelPreparation();
        if (m_scanCancelled) m_scanCancelled->store(true);
        ++m_generation;
        m_scanning = false;
        m_root.clear(); m_scan = {}; m_projects.clear(); m_projectIndex = -1;
        m_loading = true; m_projectModel->clear(); m_loading = false;
        selectProject(-1);

        m_scanSummary=QStringLiteral("Open a workspace to add build inputs.");
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
    m_scan = {};
    m_loading = true;
    m_projectIndex = -1;
    QStringList errors;
    m_projects = loadProjects(m_root, &errors);
    if (QFileInfo::exists(QDir(m_root).filePath(QStringLiteral(".simdock/runs"))))
        errors << QStringLiteral("Legacy .simdock/runs files were preserved because ownership and active use cannot be verified. New runs use automatically cleaned session storage.");
    m_projectModel->clear();
    int selected = -1;
    const auto savedId = QSettings().value(workspacePreferenceKey() + "/activeProject").toString();
    for (int i = 0; i < m_projects.size(); ++i) {
        auto* item = new QStandardItem(m_projects[i].name);
        item->setSizeHint(QSize(180, 28));
        m_projectModel->appendRow(item);
        if (m_projects[i].id == (previousId.isEmpty() ? savedId : previousId)) selected = i;
    }
    m_loading = false;
    selectProject(selected);
    emit designProjectsChanged();
    for (const auto& error : errors) appendLog(error + QLatin1Char('\n'));
    m_scanning = true;
    m_scanSummary=QStringLiteral("Analyzing workspace…");
    updateControls();
    const auto generation = ++m_generation;
    auto* watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcher<Scan>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation) return;
        m_scan = result;
        m_scanning = false;
        int modules = 0;
        for (const auto& file : m_scan.files) modules += file.modules.size();
        m_scanSummary=QStringLiteral("%1 HDL files | %2 modules").arg(m_scan.files.size()).arg(modules);
        for (const auto& message : m_scan.messages) appendLog(message + QLatin1Char('\n'));
        refreshFiles();
        scheduleCompilationOrder();
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
QString Workbench::workspacePreferenceKey() const
{
    const auto root = QDir::fromNativeSeparators(m_root).toCaseFolded().toUtf8();
    return QStringLiteral("integrations/simdock/workspaces/")
        + QString::fromLatin1(QCryptographicHash::hash(root, QCryptographicHash::Sha256).toHex());
}
QList<DesignHierarchyRoot> Workbench::designRoots() const
{
    QList<DesignHierarchyRoot> roots;
    for (const auto& project : m_projects) {
        if (project.designTopModule.isEmpty()) continue;
        roots << DesignHierarchyRoot{project.id, project.name, project.designTopModule,
            project.designTopFile.isEmpty() ? QString() : QDir(m_root).filePath(project.designTopFile),
            QSettings().value(workspacePreferenceKey() + "/expanded/" + project.id, true).toBool()};
    }
    return roots;
}
void Workbench::setDesignExpanded(const QString& id, bool expanded)
{
    for (const auto& project : m_projects) if (project.id == id) {
        QSettings().setValue(workspacePreferenceKey() + "/expanded/" + id, expanded);
        return;
    }
}
bool Workbench::designMigrationCompleted() const
{ return QSettings().value(workspacePreferenceKey() + "/explicitTopMigrated", false).toBool(); }
void Workbench::markDesignMigrationCompleted()
{ QSettings().setValue(workspacePreferenceKey() + "/explicitTopMigrated", true); }

QString Workbench::addDesignProject(const QString& file, const QString& module, bool activate)
{
    const auto issue = contextError();
    if (!issue.isEmpty()) return issue;
    if (m_root.isEmpty() || module.isEmpty()) return tr("Open a workspace and choose a module first.");
    if (m_session.busy() || m_preparing) return tr("Stop the simulation before changing design projects.");
    if (!file.isEmpty() && !insideWorkspace(m_root, file)) return tr("The design module must belong to this workspace.");
    const auto relative = file.isEmpty() ? QString() : QDir(m_root).relativeFilePath(file);
    for (auto& project : m_projects) if (project.designTopModule == module && project.designTopFile.isEmpty() && !relative.isEmpty()) {
        auto updated = project; updated.designTopFile = relative;
        if (!updated.sources.contains(relative)) updated.sources << relative;
        QString error;
        if (!saveProject(m_root, updated, &error)) return error;
        project = updated; emit designProjectsChanged(); emit stateChanged();
        return activate ? openProject(project.id) : QString();
    }
    for (const auto& project : m_projects)
        if (project.designTopModule == module && project.designTopFile == relative)
            return activate ? openProject(project.id) : QString();
    auto project = newProject(module);
    // An explicit design root is separate from the DUT and simulation TB.
    project.designTopFile = relative; project.designTopModule = module;
    if (!relative.isEmpty()) project.sources << relative;
    QString error;
    if (!saveProject(m_root, project, &error)) return error;
    adoptAutomationProject(project);
    if (activate) return openProject(project.id);
    return {};
}
QString Workbench::removeDesignRoot(const QString& id)
{
    if (m_session.busy() || m_preparing) return tr("Stop the simulation before changing design projects.");
    for (auto& project : m_projects) if (project.id == id) {
        auto updated = project; updated.designTopFile.clear(); updated.designTopModule.clear();
        QString error;
        if (!saveProject(m_root, updated, &error)) return error;
        project = updated;
        emit designProjectsChanged(); emit stateChanged();
        return {};
    }
    return tr("The project no longer exists.");
}
void Workbench::scheduleCompilationOrder()
{
    ++m_orderGeneration;
    if (m_orderCancelled) m_orderCancelled->store(true);
    m_orderPending = current() && !m_root.isEmpty();
    m_orderIssue.clear();
    if (m_orderPending) m_orderTimer.start();
    else m_orderTimer.stop();
}
void Workbench::refreshCompilationOrder()
{
    if (!current() || m_root.isEmpty() || m_scanning) return;
    const auto project = *current();
    const auto root = m_root;
    const auto generation = ++m_orderGeneration;
    if (m_orderCancelled) m_orderCancelled->store(true);
    m_orderCancelled = std::make_shared<std::atomic_bool>(false);
    m_orderPending = true; updateControls();
    auto* watcher = new QFutureWatcher<BuildInputPlan>(this);
    connect(watcher, &QFutureWatcher<BuildInputPlan>::finished, this, [this, watcher, root, project, generation] {
        const auto plan = watcher->result(); watcher->deleteLater();
        if (generation != m_orderGeneration || root != m_root || !current()) return;
        if (projectJson(*current()) != projectJson(project)) { scheduleCompilationOrder(); updateControls(); return; }
        m_orderPending = false;
        m_orderIssue = plan.error();
        const auto oldFiles = m_inputWatcher.files(), oldDirectories = m_inputWatcher.directories();
        if (!oldFiles.isEmpty()) m_inputWatcher.removePaths(oldFiles);
        if (!oldDirectories.isEmpty()) m_inputWatcher.removePaths(oldDirectories);
        QStringList files, directories;
        if (plan.order.input) {
            files = plan.order.input->watchFiles;
            directories = plan.order.input->watchDirectories;
        }
        const auto config = WorkspaceConfigurationService::projectFilePath(root);
        if (QFileInfo(config).isFile()) files << config;
        directories << root << QFileInfo(config).absolutePath();
        files.removeDuplicates(); directories.removeDuplicates();
        files.removeIf([](const auto& path) { return !QFileInfo(path).isFile(); });
        directories.removeIf([](const auto& path) { return !QFileInfo(path).isDir(); });
        if (!files.isEmpty()) m_inputWatcher.addPaths(files);
        if (!directories.isEmpty()) m_inputWatcher.addPaths(directories);
        if (plan.order.valid()) {
            auto updated = project; plan.apply(updated, root);
            if (updated.sources != project.sources) {
                QString error;
                if (saveProject(root, updated, &error)) { *current() = updated; emit stateChanged(); }
                else m_orderIssue = error;
            } else *current() = updated;
        }
        refreshFiles(); updateControls();
    });
    watcher->setFuture(QtConcurrent::run(&m_scanPool, [root, project, cancelled = m_orderCancelled] {
        return planBuildInputs(root, project, cancelled.get());
    }));
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
    scheduleCompilationOrder();
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
    item->setSizeHint(QSize(180, 28));
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
    if (row < 0) {
        row = m_projects.size(); m_projects << project;
        auto* item = new QStandardItem(project.name);
        item->setSizeHint(QSize(180, 28));
        m_projectModel->appendRow(item);
    }
    else { m_projects[row] = project; m_projectModel->item(row)->setText(project.name); }
    if (m_projectIndex == row) selectProject(row);
    emit designProjectsChanged();
    emit stateChanged();
}
QJsonObject Workbench::automationProjectState(const Project& disk) const
{
    for (const auto& project : m_projects) if (project.id == disk.id) {
        return {{"present", true}, {"project", projectJson(project)},
            {"matchesDisk", projectJson(project) == projectJson(disk) && project.storageRevisions == disk.storageRevisions},
            {"selected", activeProjectId() == disk.id}, {"editing", automationProjectEditing()},
            {"compilationPending", activeProjectId() == disk.id && m_orderPending},
            {"compilationDiagnostics", activeProjectId() == disk.id ? m_orderIssue : QString()},
            {"compilationOrder", QJsonArray::fromStringList(project.compilationUnits)},
            {"busy", m_preparing || m_scanning || m_session.busy()}};
    }
    return {{"present", false}, {"matchesDisk", false}};
}

void Workbench::selectProject(int row)
{
    cancelPreparation();
    if (m_projectIndex != row) m_session.stop();
    m_projectIndex = row;
    if (!m_root.isEmpty()) QSettings().setValue(workspacePreferenceKey() + "/activeProject", activeProjectId());
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
    scheduleCompilationOrder();
    updateControls();
    emit designProjectsChanged();
    emit stateChanged();
}
void Workbench::refreshFiles()
{
    m_loading = true;
    const auto* p = current();
    QStringList selectedPaths = p ? p->sources : QStringList();
    if (p && !p->tbFile.isEmpty() && !selectedPaths.contains(p->tbFile)) selectedPaths << p->tbFile;
    const QSet<QString> selected(selectedPaths.begin(), selectedPaths.end());
    QStringList paths;
    if (p) for (const auto& unit : p->compilationUnits)
        if (selected.contains(unit) && !paths.contains(unit)) paths << unit;
    for (const auto& path : selectedPaths) if (!paths.contains(path)) paths << path;
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
            item->setToolTip(p && path == p->tbFile ? path + tr("\nThe simulation entry is always included. Choose another TB to replace it.") : path);
            const auto suffix = QFileInfo(path).suffix().toLower();
            if (suffix == "vh" || suffix == "svh")
                item->setToolTip(path + tr("\nInclude only; not compiled as a standalone source."));
            item->setSizeHint(QSize(200, 28));
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
        if (item->data(Qt::UserRole).toString() == current()->tbFile) return;
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
    const bool explicitTb = current()->sources.contains(current()->tbFile);
    current()->sources.clear();
    for (int i = 0; i < m_fileModel->rowCount(); ++i) {
        const auto* item = m_fileModel->item(i);
        const auto file = item->data(Qt::UserRole).toString();
        if (item->checkState() == Qt::Checked && (file != current()->tbFile || explicitTb)) current()->sources << file;
    }
    updateDutChoices();
    saveCurrent();
    updateControls();
}
void Workbench::sourceChanged(QStandardItem* item)
{
    if (m_loading || !current() || m_session.busy()) return;
    if (item->data(Qt::UserRole).toString() == current()->tbFile && item->checkState() != Qt::Checked) {
        const QSignalBlocker blocker(m_fileModel); item->setCheckState(Qt::Checked); return;
    }
    if (item->checkState() != Qt::Checked) { updateSources(); return; }
    auto* p = current();
    const QString path = item->data(Qt::UserRole).toString();
    if (!p->sources.contains(path)) p->sources << path;
    if (p->dutName.isEmpty()) {
        for (const auto& file : m_scan.files) {
            if (file.path != path || file.modules.isEmpty()) continue;
            p->dutFile = path;
            if (file.modules.size() != 1) break;
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
    // Keep an invalid explicit DUT unresolved; never substitute another module.
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
    scheduleCompilationOrder();
    emit designProjectsChanged();
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
    scheduleCompilationOrder();
    updateControls(); emit designProjectsChanged(); emit stateChanged();
}
void Workbench::updateControls()
{
    const int rowHeight = qMax(28, m_projectList->sizeHintForRow(0));
    const int compactHeight = qBound(36, m_projectModel->rowCount() * (rowHeight + 2 * m_projectList->spacing())
        + 2 * m_projectList->frameWidth() + 4, 140);
    m_projectList->setMinimumHeight(m_inputsHeading->isChecked() ? compactHeight : 36);
    m_projectList->setMaximumHeight(m_inputsHeading->isChecked() ? compactHeight : QWIDGETSIZE_MAX);
    const bool busy = m_session.busy() || m_preparing;
    m_settings->setEnabled(!m_preparing);
    const auto* p = current();
    const auto tbPath = p ? p->tbFile : QString();
    const auto fullPath = tbPath.isEmpty() ? QString() : QDir::toNativeSeparators(QDir(m_root).filePath(tbPath));
    m_tbFile->setText(tbPath);
    m_tbFile->setCursorPosition(0);
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
    m_sourceHint->setText(m_root.isEmpty()?tr("Open a workspace to add build inputs.")
        :m_scanning?tr("Analyzing workspace…")
        :!m_orderIssue.isEmpty()?m_orderIssue:m_orderPending?tr("Checking compilation dependencies…")
        :m_scan.files.isEmpty()?tr("Add HDL files, then rescan build inputs.")
        :!p?tr("Select a file to create a simulation project.")
        :p->sources.isEmpty()?tr("Select build inputs for this project."):QString());
    m_sourceHint->setVisible(!m_sourceHint->text().isEmpty());
    m_inputsHeading->setToolTip(m_scanSummary);
    m_projectPanel->setEnabled(p && !busy && !m_scanning);
    m_projectPanel->setVisible(p!=nullptr);
    m_emptyState->setVisible(!p);
    m_emptyState->setText(m_root.isEmpty() ? QStringLiteral("Open a ZeroSlack workspace to configure simulation.")
        : QStringLiteral("Create or select a project in Build inputs."));
    const bool tbAvailable = p && QFileInfo(QDir(m_root).filePath(p->tbFile)).isFile();
    m_openTb->setEnabled(tbAvailable);
    m_legacyInfo->setVisible(p&&!p->preservedInputs.isEmpty());
    const auto toolIssue=simulatorError(m_simulator);
    m_inputNotice->setText(!p?QString():!tbAvailable?tr("Choose a TB file to run.")
        :p->tbName.trimmed().isEmpty()?tr("Enter the TB top module to run.")
        :m_scanning?tr("Analyzing workspace…")
        :m_orderPending?tr("Checking compilation dependencies…")
        :!m_orderIssue.isEmpty()?tr("Resolve the error shown in Build inputs.")
        :p->sources.isEmpty()?tr("Select files in Build inputs.")
        :!toolIssue.isEmpty()?tr("Choose a valid Questa installation in Simulation settings."):QString());
    m_inputNotice->setVisible(!busy&&!m_inputNotice->text().isEmpty());
    m_rename->setEnabled(p && !busy && !m_scanning);
    m_generate->setEnabled(p && currentModule() && !busy && !m_scanning);
    m_run->setEnabled(p && !p->sources.isEmpty() && tbAvailable && !p->tbName.trimmed().isEmpty() && !busy && !m_scanning && !m_orderPending && m_orderIssue.isEmpty());
    m_stop->setEnabled(m_preparing || m_session.alive());
    m_stop->setVisible(busy);
    m_stop->setText(m_preparing ? QStringLiteral("Cancel preparation") : busy ? QStringLiteral("Stop simulation") : QStringLiteral("Close session"));
    m_closeSession->setVisible(m_session.alive()&&!busy);
    m_status->setVisible(!m_status->text().isEmpty()&&m_status->text()!=QStringLiteral("Ready"));
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
            appendLog(error + QLatin1Char('\n')); m_status->setText(error);
        }
        updateControls();
    });
    watcher->setFuture(QtConcurrent::run(&m_scanPool, [root, executable, project, cancelled = m_preparationCancelled] {
        return prepareRun(executable, root, project, cancelled.get());
    }));
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
    if (m_session.busy() || m_preparing) return tr("Stop the simulation before activating another project.");
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
    m_orderTimer.stop(); ++m_orderGeneration;
    if (m_orderCancelled) m_orderCancelled->store(true);
    cancelPreparation();
    if (m_scanCancelled) m_scanCancelled->store(true);
    ++m_generation;
    m_logTimer.stop();
    m_session.stop();
}
}
