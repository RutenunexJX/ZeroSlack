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
#include <ElaDrawerArea.h>
#include <ElaToolButton.h>
#include <ElaStatusBar.h>
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
#include <QFontDatabase>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QSignalBlocker>
#include <QSplitter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QResizeEvent>
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
    QVariantMap layout;
    for (const auto& key : {"layout/columns", "layout/workbench", "layout/simulationExpanded", "layout/logExpanded", "layout/compactPage"}) {
        const auto value = preference(QString::fromLatin1(key));
        if (value.isValid()) layout.insert(QString::fromLatin1(key), value);
    }
    restoreLayout(layout);
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
}

QSize Workbench::minimumSizeHint() const { return {280, 240}; }

void Workbench::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePresentation();
}

void Workbench::updatePresentation()
{
    if (!m_compactBody || m_arranging) return;
    // Short floating windows need the same scrollable pages as narrow docks.
    setCompact(width() < 900 || height() < 600);
}

void Workbench::arrangeSimulationFields(bool compact)
{
    m_simulationHeading->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_tbActions->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_runActions->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_waveActions->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    QWidget* fields[] = {m_tbCaption, m_durationCaption, m_tbTop, m_duration, m_units};
    for (auto* field : fields)
        m_simulationFields->removeWidget(field);
    if (compact) {
        m_simulationFields->addWidget(m_tbCaption, 0, 0, 1, 2);
        m_simulationFields->addWidget(m_tbTop, 1, 0, 1, 2);
        m_simulationFields->addWidget(m_durationCaption, 2, 0, 1, 2);
        m_simulationFields->addWidget(m_duration, 3, 0);
        m_simulationFields->addWidget(m_units, 3, 1);
    } else {
        m_simulationFields->addWidget(m_tbCaption, 0, 0);
        m_simulationFields->addWidget(m_durationCaption, 0, 1, 1, 2);
        m_simulationFields->addWidget(m_tbTop, 1, 0);
        m_simulationFields->addWidget(m_duration, 1, 1);
        m_simulationFields->addWidget(m_units, 1, 2);
    }
}

void Workbench::setCompact(bool compact)
{
    if (compact == m_compact) return;
    QScopedValueRollback<bool> arranging(m_arranging, true);
    m_configDrawer->finishDrawerAnimation();
    m_logDrawer->finishDrawerAnimation();
    if (compact && m_wideBody->isVisible()) {
        m_wideColumnsState = m_top->saveState();
        m_wideWorkbenchState = m_logToggle->isChecked() ? m_split->saveState() : m_expandedLogState;
    }
    m_compact = compact;
    setProperty("compactLayout", compact);
    arrangeSimulationFields(compact);
    m_configToggle->setVisible(!compact);
    m_logToggle->setVisible(!compact);
    m_analysisStatus->setVisible(!compact);
    m_logs->setMaximumHeight(QWIDGETSIZE_MAX);

    if (compact) {
        m_sidebar->setMinimumWidth(0);
        m_sidebar->setMaximumWidth(QWIDGETSIZE_MAX);
        m_files->setMinimumWidth(0);
        m_config->setMinimumWidth(0);
        m_columns->removeWidget(m_sidebar);
        // Transfer whole existing panels only at the breakpoint. Views, models,
        // editor fields, selections and the session retain their identities.
        m_pages[0]->setWidget(m_sidebar);
        m_pages[1]->setWidget(m_files);
        m_pages[2]->setWidget(m_config);
        m_pages[3]->setWidget(m_logs);
        m_configDrawer->setExpanded(true, false);
        m_logDrawer->show();
        m_logDrawer->setExpanded(true, false);
        m_wideBody->hide();
        m_compactBody->show();
    } else {
        for (auto* page : m_pages) page->takeWidget();
        m_sidebar->setFixedWidth(220);
        m_files->setMinimumWidth(260);
        m_config->setMinimumWidth(320);
        m_columns->insertWidget(0, m_sidebar);
        m_top->addWidget(m_files);
        m_top->addWidget(m_config);
        m_split->addWidget(m_logs);
        for (auto* panel : {m_sidebar, m_files, m_config, m_logs}) panel->show();
        m_compactBody->hide();
        m_wideBody->show();
        layout()->activate();
        m_wideBody->layout()->activate();
        if (m_wideColumnsState.isEmpty()) m_top->setSizes({420, 360});
        else m_top->restoreState(m_wideColumnsState);
        if (m_wideWorkbenchState.isEmpty()) m_split->setSizes({420, 200});
        else m_split->restoreState(m_wideWorkbenchState);
        m_configDrawer->setExpanded(m_configToggle->isChecked(), false);
        m_logDrawer->setExpanded(m_logToggle->isChecked(), false);
        if (!m_logToggle->isChecked()) collapseLogLayout();
    }
    updateGeometry();
}

void Workbench::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    // The host chooses the width. Child panel minima must not enlarge a dock.
    outer->setSizeConstraint(QLayout::SetNoConstraint);

    m_wideBody = new QWidget(this);
    m_wideBody->setObjectName(QStringLiteral("wideWorkbench"));
    auto* columns = new QHBoxLayout(m_wideBody);
    m_columns = columns;
    columns->setContentsMargins(12, 12, 12, 12);
    columns->setSpacing(8);
    auto* sidebar = m_sidebar = new QFrame(m_wideBody);
    sidebar->setObjectName(QStringLiteral("workspaceSidebar"));
    sidebar->setProperty("surface", "panel");
    sidebar->setFixedWidth(220);
    auto* navigation = new QVBoxLayout(sidebar);
    navigation->setContentsMargins(12, 16, 12, 12);
    navigation->setSpacing(12);
    navigation->addWidget(Ui::label(QStringLiteral("Workspace"), sidebar, Ui::Role::Section));
    m_workspaceTitle = Ui::label(QStringLiteral("No workspace"), sidebar, Ui::Role::PanelTitle);
    m_workspaceTitle->setWordWrap(true);
    auto wrapping = m_workspaceTitle->sizePolicy();
    wrapping.setHorizontalPolicy(QSizePolicy::Ignored);
    m_workspaceTitle->setSizePolicy(wrapping);
    navigation->addWidget(m_workspaceTitle);
    m_workspacePath = Ui::label(QStringLiteral("Select a folder containing HDL source files"), sidebar, Ui::Role::Metadata);
    m_workspacePath->setSizePolicy(wrapping);
    m_workspacePath->setIsWrapAnywhere(true);
    m_workspacePath->setMaximumHeight(60);
    Ui::enableToolTip(m_workspacePath);
    navigation->addWidget(m_workspacePath);
    m_open = Ui::button(QStringLiteral("Open workspace"), sidebar);
    m_open->setObjectName(QStringLiteral("openWorkspace"));
    navigation->addWidget(m_open);
    navigation->addSpacing(8);
    auto* projectHeading = new QHBoxLayout;
    projectHeading->setSpacing(8);
    projectHeading->addWidget(Ui::label(QStringLiteral("Projects"), sidebar, Ui::Role::Section));
    projectHeading->addStretch();
    m_new = Ui::button(QStringLiteral("New"), sidebar);
    m_new->setObjectName(QStringLiteral("newProject"));
    m_new->setFixedWidth(56);
    projectHeading->addWidget(m_new);
    navigation->addLayout(projectHeading);
    m_projectList = new ElaListView(sidebar);
    m_projectList->setObjectName(QStringLiteral("projectList"));
    auto* projectStyle = ElaListView::createStyle(m_projectList, 32);
    static_cast<QProxyStyle*>(projectStyle)->setBaseStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    m_projectList->setStyle(projectStyle);
    m_projectList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_projectList->setFont(Ui::font());
    m_projectList->setUniformItemSizes(true);
    Ui::smoothScrolling(m_projectList);
    m_projectModel = new QStandardItemModel(this);
    m_projectList->setModel(m_projectModel);
    navigation->addWidget(m_projectList, 1);
    m_scanStatus = Ui::label(QStringLiteral("Only files inside the workspace are analyzed"), sidebar, Ui::Role::Metadata);
    m_scanStatus->setWordWrap(true);
    navigation->addWidget(m_scanStatus);
    m_settings = Ui::button(QStringLiteral("Settings"), sidebar);
    m_settings->setObjectName(QStringLiteral("openSettings"));
    navigation->addWidget(m_settings);
    columns->addWidget(sidebar);

    auto* split = m_split = new QSplitter(Qt::Vertical, m_wideBody);
    split->setObjectName(QStringLiteral("workbenchSplitter"));
    split->setHandleWidth(8);
    auto* top = m_top = new QSplitter(Qt::Horizontal, split);
    top->setObjectName(QStringLiteral("columnSplitter"));
    top->setHandleWidth(8);
    auto* files = m_files = new QFrame(top);
    files->setObjectName(QStringLiteral("sourcePanel"));
    files->setProperty("surface", "panel");
    files->setMinimumWidth(260);
    auto* fileLayout = new QVBoxLayout(files);
    fileLayout->setContentsMargins(12, 12, 12, 12);
    fileLayout->setSpacing(8);
    auto* fileHeading = new QHBoxLayout;
    fileHeading->setSpacing(8);
    fileHeading->addWidget(Ui::label(QStringLiteral("Source files"), this, Ui::Role::PanelTitle));
    fileHeading->addStretch();
    m_refresh = Ui::button(QStringLiteral("Rescan"), this);
    m_refresh->setObjectName(QStringLiteral("refreshWorkspace"));
    fileHeading->addWidget(m_refresh);
    fileLayout->addLayout(fileHeading);
    m_sourceHint = Ui::label(QStringLiteral("Select source files. Compilation follows the list order."), this, Ui::Role::Metadata);
    m_sourceHint->setWordWrap(true);
    fileLayout->addWidget(m_sourceHint);
    m_fileList = new ElaListView(this);
    m_fileList->setObjectName(QStringLiteral("sourceList"));
    auto* fileStyle = ElaListView::createStyle(m_fileList, 32);
    static_cast<QProxyStyle*>(fileStyle)->setBaseStyle(new SourceMetricsStyle);
    m_fileList->setStyle(fileStyle);
    m_fileList->setItemDelegate(new SourceDelegate(m_fileList));
    m_fileList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileList->setFont(Ui::font());
    m_fileList->setTextElideMode(Qt::ElideMiddle);
    m_fileModel = new SourceModel(this, [this](const QModelIndex& index, Qt::CheckState state) {
        if (m_scanning || m_session.busy() || m_requestingProject
            || (state != Qt::Checked && state != Qt::Unchecked)) return false;
        toggleSource(index);
        return current() != nullptr;
    });
    m_fileList->setModel(m_fileModel);
    m_fileList->setUniformItemSizes(true);
    Ui::smoothScrolling(m_fileList);
    fileLayout->addWidget(m_fileList, 1);
    auto* fileActions = new QHBoxLayout;
    fileActions->setSpacing(8);
    auto* up = Ui::button(QStringLiteral("Move up"), this);
    auto* down = Ui::button(QStringLiteral("Move down"), this);
    up->setObjectName(QStringLiteral("moveSourceUp"));
    down->setObjectName(QStringLiteral("moveSourceDown"));
    up->setToolTip(QStringLiteral("Compile the selected file earlier"));
    down->setToolTip(QStringLiteral("Compile the selected file later"));
    Ui::enableToolTip(up);
    Ui::enableToolTip(down);
    Ui::enableToolTip(m_fileList);
    fileActions->addWidget(up);
    fileActions->addWidget(down);
    fileActions->addStretch();
    fileLayout->addLayout(fileActions);

    auto* config = m_config = new QFrame(top);
    config->setObjectName(QStringLiteral("simulationPanel"));
    config->setProperty("surface", "panel");
    config->setMinimumWidth(320);
    auto* configLayout = new QVBoxLayout(config);
    configLayout->setContentsMargins(16, 16, 16, 16);
    configLayout->setSpacing(12);
    m_configToggle = Ui::disclosure(QStringLiteral("Simulation"), config);
    m_configToggle->setObjectName(QStringLiteral("simulationToggle"));
    auto* simulationHeading = new QHBoxLayout;
    m_simulationHeading = simulationHeading;
    simulationHeading->addWidget(m_configToggle, 1);
    m_stimulus = Ui::button(QStringLiteral("Draw stimulus"), config);
    m_stimulus->setObjectName(QStringLiteral("editStimulus"));
    simulationHeading->addWidget(m_stimulus);
    configLayout->addLayout(simulationHeading);
    m_configDrawer = new ElaDrawerArea(config);
    m_configDrawer->setObjectName(QStringLiteral("simulationDrawer"));
    m_configDrawer->setDrawerHeaderVisible(false);
    m_configDrawer->setBorderRadius(4);
    m_projectPanel = new QWidget(config);
    auto* form = new QVBoxLayout(m_projectPanel);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    form->addWidget(Ui::label(QStringLiteral("DUT module"), this, Ui::Role::Section));
    m_dut = new ElaComboBox(this);
    m_dut->setObjectName(QStringLiteral("dutSelector"));
    m_dut->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_dut->setMinimumContentsLength(12);
    m_dut->setPlaceholderText(QStringLiteral("Select source files first"));
    form->addWidget(m_dut);
    form->addSpacing(8);
    form->addWidget(Ui::label(QStringLiteral("Testbench"), this, Ui::Role::Section));
    m_tbFile = new ElaLineEdit(this);
    m_tbFile->setReadOnly(true);
    m_tbFile->setPlaceholderText(QStringLiteral("Create or select a TB file"));
    m_tbFile->setObjectName(QStringLiteral("tbFile"));
    form->addWidget(m_tbFile);
    auto* tbActions = new QHBoxLayout;
    m_tbActions = tbActions;
    tbActions->setSpacing(8);
    m_generate = Ui::button(QStringLiteral("Create Demo TB"), this);
    m_generate->setObjectName(QStringLiteral("createTb"));
    m_chooseTb = Ui::button(QStringLiteral("Select existing TB"), this);
    m_chooseTb->setObjectName(QStringLiteral("chooseTb"));
    tbActions->addWidget(m_generate, 1);
    tbActions->addWidget(m_chooseTb, 1);
    form->addLayout(tbActions);
    form->addSpacing(8);
    auto* fields = new QGridLayout;
    m_simulationFields = fields;
    fields->setHorizontalSpacing(12);
    fields->setVerticalSpacing(8);
    m_tbCaption = Ui::label(QStringLiteral("TB top level"), this, Ui::Role::Section);
    m_durationCaption = Ui::label(QStringLiteral("Duration"), this, Ui::Role::Section);
    fields->addWidget(m_tbCaption, 0, 0);
    fields->addWidget(m_durationCaption, 0, 1, 1, 2);
    m_tbTop = new ElaLineEdit(this);
    m_tbTop->setObjectName(QStringLiteral("tbTop"));
    m_tbTop->setPlaceholderText(QStringLiteral("tb_top"));
    fields->addWidget(m_tbTop, 1, 0);
    m_duration = Ui::spinBox(this);
    m_duration->setObjectName(QStringLiteral("duration"));
    m_duration->setRange(1, 3600000);
    m_duration->setValue(1000);
    fields->addWidget(m_duration, 1, 1);
    m_units = new ElaComboBox(this);
    m_units->addItem(QStringLiteral("ns"), 1);
    m_units->addItem(QStringLiteral("us"), 1000);
    m_units->addItem(QStringLiteral("ms"), 1000000);
    m_units->setFixedWidth(82);
    fields->addWidget(m_units, 1, 2);
    fields->setColumnStretch(0, 1);
    form->addLayout(fields);
    form->addWidget(Ui::label(QStringLiteral("Waveform scope"), this, Ui::Role::Section));
    auto* waves = new QHBoxLayout;
    m_waveActions = waves;
    m_waveScope = new ElaComboBox(this);
    m_waveScope->setObjectName(QStringLiteral("waveScope"));
    m_waveScope->addItem(QStringLiteral("Interface (TB top)"), QStringLiteral("interface"));
    m_waveScope->addItem(QStringLiteral("Selected signals"), QStringLiteral("selected"));
    m_waveScope->addItem(QStringLiteral("All signals"), QStringLiteral("all"));
    m_waveSignals = Ui::button(QStringLiteral("Choose signals"), this);
    m_waveSignals->setObjectName(QStringLiteral("chooseWaveSignals"));
    waves->addWidget(m_waveScope, 1); waves->addWidget(m_waveSignals);
    form->addLayout(waves);
    connect(m_waveScope, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || !current()) return;
        current()->waveScope = m_waveScope->currentData().toString();
        saveCurrent(); updateControls();
    });
    connect(m_waveSignals, &QPushButton::clicked, this, &Workbench::chooseWaveSignals);
    m_configDrawer->addDrawer(m_projectPanel);
    m_configDrawer->setExpanded(true, false);
    configLayout->addWidget(m_configDrawer);
    connect(m_configToggle, &QToolButton::toggled, m_configDrawer, [this](bool expanded) {
        m_configDrawer->setExpanded(m_compact || expanded, !m_compact);
    });
    configLayout->addStretch();
    auto* note = Ui::label(QStringLiteral("Compile and load the TB. View waveforms in Questa."), this, Ui::Role::Metadata);
    note->setWordWrap(true);
    configLayout->addWidget(note);
    auto* actions = new QHBoxLayout;
    m_runActions = actions;
    actions->setSpacing(8);
    m_stop = Ui::button(QStringLiteral("Close session"), this);
    m_stop->setObjectName(QStringLiteral("stopSimulation"));
    m_run = Ui::button(QStringLiteral("Start simulation"), this, true);
    m_run->setObjectName(QStringLiteral("startSimulation"));
    actions->addWidget(m_stop);
    actions->addWidget(m_run, 1);
    configLayout->addLayout(actions);

    auto* logs = m_logs = new QFrame(split);
    logs->setObjectName(QStringLiteral("logPanel"));
    logs->setProperty("surface", "panel");
    auto* logLayout = new QVBoxLayout(logs);
    logLayout->setContentsMargins(12, 8, 12, 8);
    logLayout->setSpacing(4);
    auto* logHeading = new QHBoxLayout;
    m_logToggle = Ui::disclosure(QStringLiteral("Run log"), logs);
    m_logToggle->setObjectName(QStringLiteral("logToggle"));
    logHeading->addWidget(m_logToggle);
    logHeading->addStretch();
    auto* clear = Ui::button(QStringLiteral("Clear"), this);
    clear->setObjectName(QStringLiteral("clearLog"));
    logHeading->addWidget(clear);
    logLayout->addLayout(logHeading);
    m_log = Ui::textView(this);
    m_log->setObjectName(QStringLiteral("simulationLog"));
    m_log->setProperty("codeSurface", true);
    m_log->setNativeTextBehavior(true);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(10000);
    m_log->setFont(Ui::codeFont());
    m_log->setPlaceholderText(QStringLiteral("Compiler, loader, and simulation output will appear here."));
    m_logDrawer = new ElaDrawerArea(logs);
    m_logDrawer->setObjectName(QStringLiteral("logDrawer"));
    m_logDrawer->setDrawerHeaderVisible(false);
    m_logDrawer->setDrawerEdge(Qt::BottomEdge);
    m_logDrawer->addDrawer(m_log);
    m_logDrawer->setExpanded(true, false);
    logLayout->addWidget(m_logDrawer, 1);
    connect(m_logToggle, &QToolButton::toggled, this, &Workbench::setLogExpanded);
    connect(m_logDrawer, &ElaDrawerArea::drawerAnimationFinished, this, [this](bool expanded) {
        if (!expanded) collapseLogLayout();
    });
    top->setChildrenCollapsible(false);
    top->setSizes({420, 360});
    top->setStretchFactor(0, 1);
    top->setStretchFactor(1, 0);
    split->setChildrenCollapsible(false);
    split->setSizes({420, 200});
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    columns->addWidget(split, 1);
    outer->addWidget(m_wideBody, 1);

    m_compactBody = new QWidget(this);
    m_compactBody->setObjectName(QStringLiteral("compactWorkbench"));
    auto* compactLayout = new QVBoxLayout(m_compactBody);
    compactLayout->setContentsMargins(8, 8, 8, 8);
    compactLayout->setSpacing(8);
    m_section = new ElaComboBox(m_compactBody);
    m_section->setObjectName(QStringLiteral("workbenchSection"));
    m_section->setAccessibleName(QStringLiteral("Workbench section"));
    m_section->addItem(QStringLiteral("Workspace and projects"), QStringLiteral("workspace"));
    m_section->addItem(QStringLiteral("Source files and order"), QStringLiteral("sources"));
    m_section->addItem(QStringLiteral("Simulation and stimulus"), QStringLiteral("simulation"));
    m_section->addItem(QStringLiteral("Run log"), QStringLiteral("log"));
    m_section->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_section->setMinimumContentsLength(1);
    compactLayout->addWidget(m_section);
    m_sections = new QStackedWidget(m_compactBody);
    m_sections->setObjectName(QStringLiteral("workbenchSections"));
    for (int i = 0; i < 4; ++i) {
        auto* page = m_pages[i] = new QScrollArea(m_sections);
        page->setObjectName(QStringLiteral("workbenchPage%1").arg(i));
        page->setWidgetResizable(true);
        page->setFrameShape(QFrame::NoFrame);
        page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        Ui::smoothScrolling(page);
        m_sections->addWidget(page);
    }
    connect(m_section, &QComboBox::currentIndexChanged, m_sections, &QStackedWidget::setCurrentIndex);
    compactLayout->addWidget(m_sections, 1);
    outer->addWidget(m_compactBody, 1);
    m_compactBody->hide();

    auto* statusBar = new ElaStatusBar(this);
    statusBar->setSizeGripEnabled(false);
    statusBar->addWidget(Ui::label(QStringLiteral("QuestaSim"), this, Ui::Role::Metadata));
    m_analysisStatus = Ui::label(QStringLiteral("Workspace analysis"), this, Ui::Role::Metadata);
    statusBar->addWidget(m_analysisStatus, 1);
    m_status = Ui::label(QStringLiteral("Ready"), this, Ui::Role::Metadata);
    m_status->setObjectName(QStringLiteral("runStatus"));
    statusBar->addPermanentWidget(m_status);
    outer->addWidget(statusBar);
    Ui::normalizeControls(this);

    connect(m_settings, &QPushButton::clicked, this, &Workbench::openSettings);
    connect(m_open, &QPushButton::clicked, this, [this] {
        QPointer<Workbench> owner(this);
        QPointer<QFileDialog> dialog = new QFileDialog(this, QStringLiteral("Open workspace"), m_root);
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOptions(QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
        const auto result = dialog->exec();
        if (!owner || !dialog) return;
        const auto paths = dialog->selectedFiles();
        delete dialog;
        if (result == QDialog::Accepted && !paths.isEmpty()) openWorkspace(paths.first());
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
            p->tbFile.clear(); p->tbName.clear(); p->stimulus = {};
            m_tbFile->clear(); m_tbTop->clear();
        }
        const auto selection = selectDependencies(m_dependencies, identity[0], p->sources);
        p->sources = selection.sources;
        for (const auto& message : selection.messages) appendLog(message + QLatin1Char('\n'));
        refreshFiles();
        saveCurrent(); updateControls();
    });
    connect(m_tbTop, &QLineEdit::editingFinished, this, [this] {
        if (auto* p = current()) { p->tbName = m_tbTop->text().trimmed(); saveCurrent(); updateControls(); }
    });
    auto durationChanged = [this] {
        if (m_loading || !current()) return;
        current()->durationNs = qint64(m_duration->value()) * m_units->currentData().toLongLong();
        saveCurrent();
    };
    connect(m_duration, &QSpinBox::valueChanged, this, durationChanged);
    connect(m_units, &QComboBox::currentIndexChanged, this, durationChanged);
    connect(m_stimulus, &QPushButton::clicked, this, &Workbench::editStimulus);
    connect(m_generate, &QPushButton::clicked, this, &Workbench::createTb);
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
        m_workspaceTitle->setText(QStringLiteral("No workspace"));
        m_workspacePath->setText(QStringLiteral("Select a folder containing HDL source files"));
        m_workspacePath->setToolTip({});
        m_scanStatus->setText(QStringLiteral("Only files inside the workspace are analyzed"));
        return {};
    }
    cancelPreparation();
    const QString previousId = current() && root == m_root ? current()->id : QString();
    if (m_scanCancelled) m_scanCancelled->store(true);
    m_scanCancelled = std::make_shared<std::atomic_bool>(false);
    m_configDrawer->finishDrawerAnimation();
    m_logDrawer->finishDrawerAnimation();
    m_root = root;
    setPreference(QStringLiteral("workspace/last"), m_root);
    m_workspaceTitle->setText(QFileInfo(m_root).fileName());
    m_workspacePath->setText(QDir::toNativeSeparators(m_root));
    m_workspacePath->setToolTip(QDir::toNativeSeparators(m_root));
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
    m_tbFile->setText(p ? p->tbFile : QString());
    m_tbTop->setText(p ? p->tbName : QString());
    m_waveScope->setCurrentIndex(qMax(0, m_waveScope->findData(p ? p->waveScope : QStringLiteral("interface"))));
    const qint64 duration = p ? p->durationNs : 1000;
    const int unit = duration % 1000000 == 0 ? 2 : (duration % 1000 == 0 ? 1 : 0);
    m_units->setCurrentIndex(unit);
    m_duration->setValue(int(duration / m_units->currentData().toLongLong()));
    m_loading = false;
    refreshFiles();
    updateControls();
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
            p->tbFile.clear(); p->tbName.clear(); p->stimulus = {}; m_tbFile->clear(); m_tbTop->clear();
        }
        p->dutFile = identity[0]; p->dutName = identity[1];
    }
}
void Workbench::saveCurrent()
{
    if (m_loading || !current()) return;
    QString error;
    if (!saveProject(m_root, *current(), &error)) appendLog(QStringLiteral("Settings could not be saved: %1\n").arg(error));
}
void Workbench::updateControls()
{
    const bool busy = m_session.busy() || m_preparing;
    m_settings->setEnabled(!m_preparing);
    const auto* p = current();
    m_open->setEnabled(!busy);
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
    m_sourceHint->setText(p ? QStringLiteral("Select source files. Compilation follows the list order.")
        : QStringLiteral("Select a file to create a simulation project."));
    m_projectPanel->setEnabled(p && !busy && !m_scanning);
    m_stimulus->setEnabled(p && currentModule() && !busy && !m_scanning);
    const bool graphical = p && !p->stimulus.isEmpty();
    m_stimulus->setText(graphical ? QStringLiteral("Edit stimulus") : QStringLiteral("Draw stimulus"));
    m_tbTop->setReadOnly(graphical);
    m_duration->setEnabled(!graphical); m_units->setEnabled(!graphical);
    m_duration->setToolTip(graphical ? QStringLiteral("Change duration in Edit stimulus > Timing") : QString());
    m_generate->setEnabled(p && currentModule() && !busy && !m_scanning);
    m_run->setEnabled(p && !p->sources.isEmpty() && !p->tbFile.isEmpty() && !busy && !m_scanning);
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
    p->stimulus = {};
    p->tbFile = source.path;
    p->tbName = source.modules.size() == 1 ? source.modules.first().name : QFileInfo(path).completeBaseName();
    m_tbFile->setText(p->tbFile); m_tbTop->setText(p->tbName);
    saveCurrent(); updateControls();
}
bool Workbench::createDemo(const TbOptions& options, QString* error)
{
    auto* p = current();
    const auto* module = currentModule();
    if (!p || !module) { *error = QStringLiteral("Select a DUT first."); return false; }
    const QString text = generateTestbench(*module, options, p->durationNs, error);
    if (!error->isEmpty()) return false;
    const QString path = projectTbPath(*p, options.name);
    if (!writeNewTb(m_root, path, text, error)) return false;
    p->stimulus = {};
    p->tbFile = path; p->tbName = options.name;
    m_tbFile->setText(path); m_tbTop->setText(options.name);
    saveCurrent();
    appendLog(QStringLiteral("Created %1\n").arg(path));
    updateControls();
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
void Workbench::setLogExpanded(bool expanded)
{
    if (m_compact) {
        m_logs->setMaximumHeight(QWIDGETSIZE_MAX);
        m_logDrawer->show();
        m_logDrawer->setExpanded(true, false);
        return;
    }
    if (expanded) {
        m_logs->setMaximumHeight(QWIDGETSIZE_MAX);
        m_logDrawer->show();
        if (!m_expandedLogState.isEmpty()) m_split->restoreState(m_expandedLogState);
    } else if (m_logDrawer->getIsExpand()) {
        // QSplitter remembers requested sizes even when a panel's minimum
        // height clamps them. Persist the actual visible layout before hiding
        // the log, so a later collapsed settings panel cannot resurrect an
        // older, larger requested log height.
        m_split->setSizes(m_split->sizes());
        m_expandedLogState = m_split->saveState();
    }
    m_logDrawer->setExpanded(expanded);
    if (!expanded && !m_logDrawer->isDrawerAnimating()) collapseLogLayout();
}
void Workbench::collapseLogLayout()
{
    if (m_compact) return;
    m_logDrawer->hide();
    const int height = m_logs->minimumSizeHint().height();
    m_logs->setMaximumHeight(height);
    const auto sizes = m_split->sizes();
    m_split->setSizes({sizes.first() + sizes.last() - height, height});
}
void Workbench::applyTheme()
{
    Ui::applyTheme(this);
    m_configDrawer->finishDrawerAnimation();
    m_logDrawer->finishDrawerAnimation();
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
        {QStringLiteral("layout/columns"), m_compact ? m_wideColumnsState : m_top->saveState()},
        {QStringLiteral("layout/workbench"), m_compact ? m_wideWorkbenchState : (m_logToggle->isChecked() ? m_split->saveState() : m_expandedLogState)},
        {QStringLiteral("layout/compactPage"), m_section->currentData()},
        {QStringLiteral("layout/simulationExpanded"), m_configToggle->isChecked()},
        {QStringLiteral("layout/logExpanded"), m_logToggle->isChecked()}};
}
void Workbench::restoreLayout(const QVariantMap& state)
{
    m_wideColumnsState = state.value(QStringLiteral("layout/columns")).toByteArray();
    m_expandedLogState = state.value(QStringLiteral("layout/workbench")).toByteArray();
    m_wideWorkbenchState = m_expandedLogState;
    if (!m_compact) {
        m_top->restoreState(m_wideColumnsState);
        m_split->restoreState(m_wideWorkbenchState);
    }
    m_section->setCurrentIndex(qMax(0, m_section->findData(state.value(QStringLiteral("layout/compactPage"), QStringLiteral("workspace")))));
    m_configToggle->setChecked(state.value(QStringLiteral("layout/simulationExpanded"), true).toBool());
    m_logToggle->setChecked(state.value(QStringLiteral("layout/logExpanded"), true).toBool());
    // Collapsing the log captures the current splitter sizes. During initial
    // restoration those may still be clamped by the expanded settings panel.
    // Retain the persisted expanded sizes until the user opens the log.
    if (!m_wideWorkbenchState.isEmpty()) m_expandedLogState = m_wideWorkbenchState;
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
    for (const auto& key : {"layout/columns", "layout/workbench", "layout/simulationExpanded", "layout/logExpanded", "layout/compactPage"}) {
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
