#include "workbench.h"
#include "simulationeventspanel.h"
#include "dialogs.h"
#include "uistyle.h"
#include "../core/workspace.h"
#include "applicationthememanager.h"
#include "roundedicons.h"
#include "uicontrols.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include <ElaComboBox.h>
#include <ElaDialog.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace simdock {
Workbench::Workbench(QWidget* parent, bool standalone, WorkspaceSimulationController* controller, DesignProjectService* design)
    : QWidget(parent), m_controller(controller ? controller : new WorkspaceSimulationController(this)),
      m_design(design ? design : new DesignProjectService(this)), m_standalone(standalone)
{
    Ui::initializeComponent();
    setObjectName(QStringLiteral("SimDockWorkbench"));
    setFont(Ui::font());
    setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    buildUi();
    m_simulator = preference(QStringLiteral("simulator/path")).toString();
    m_log->setDocument(m_controller->logDocument());
    m_events = new SimulationEventsPanel(this, m_controller->eventModel()); m_events->hide();
    connect(m_events, &SimulationEventsPanel::sourceRequested, this, &Workbench::openSourceRequested);
    connect(m_design, &DesignProjectService::designProjectsChanged, this, &Workbench::designProjectsChanged);
    connect(m_controller, &WorkspaceSimulationController::targetChanged, this, [this] { updateControls(); emit stateChanged(); });
    connect(m_controller, &WorkspaceSimulationController::planReady, this, [this] { updateControls(); });
    connect(m_controller, &WorkspaceSimulationController::stateChanged, this, [this] {
        m_status->setText(m_controller->state()); m_status->setToolTip(m_controller->state()); updateControls();
    });
    connect(m_controller, &WorkspaceSimulationController::logAboutToAppend, this, [this] {
        if (!m_log) return;
        const auto* bar = m_log->verticalScrollBar();
        m_logPosition = bar->value(); m_logFollow = m_logPosition >= bar->maximum();
    });
    connect(m_controller, &WorkspaceSimulationController::logAppended, this, [this] {
        if (m_log) { auto* bar = m_log->verticalScrollBar(); bar->setValue(m_logFollow ? bar->maximum() : m_logPosition); }
    });
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged, this, [this] {
        setDarkTheme(isDarkTheme(ApplicationThemeManager::instance().mode()));
    });
    setDarkTheme(isDarkTheme(ApplicationThemeManager::instance().mode()));
    updateControls();
}
Workbench::~Workbench()
{
    disconnect(m_controller, nullptr, this, nullptr);
    disconnect(m_design, nullptr, this, nullptr);
    if (m_log) m_log->viewport()->removeEventFilter(this);
    delete m_logs; delete m_events;
    while (auto* child = findChild<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) delete child;
}
QWidget* Workbench::eventsWidget() const { return m_events; }
QSize Workbench::minimumSizeHint() const { return {280, 40}; }
WaveTheme Workbench::waveTheme() const
{ return waveThemeFromId(preference(QStringLiteral("waveform/theme"), QStringLiteral("default")).toString()); }
void Workbench::buildUi()
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 8, 4); row->setSpacing(6);
    m_target = UiControls::toolButton(this);
    m_target->setObjectName(QStringLiteral("simulationTarget"));
    m_target->setIcon(RoundedIcons::icon(RoundedIcons::Simulation));
    m_target->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_target->setMinimumWidth(110);
    m_target->setPopupMode(QToolButton::InstantPopup);
    auto* targets = UiControls::menu(this);
    auto* current = targets->addAction(QStringLiteral("Use current editor"));
    current->setObjectName(QStringLiteral("useCurrentTb"));
    connect(current, &QAction::triggered, this, [this] {
        reportIssue(m_targetHandler ? m_targetHandler({}) : QStringLiteral("Choose a TB file first."));
    });
    auto* browse = targets->addAction(QStringLiteral("Choose TB…"));
    browse->setObjectName(QStringLiteral("chooseTb"));
    connect(browse, &QAction::triggered, this, &Workbench::chooseTb);
    auto* open = targets->addAction(QStringLiteral("Open selected TB"));
    open->setObjectName(QStringLiteral("openTb"));
    connect(open, &QAction::triggered, this, [this] {
        if (!m_controller->target().isEmpty()) emit openFileRequested(QDir(workspace()).filePath(m_controller->target().file));
    });
    auto* clearTarget = targets->addAction(QStringLiteral("Clear target"));
    connect(clearTarget, &QAction::triggered, this, [this] { reportIssue(m_controller->clearTarget()); });
    targets->addSeparator();
    auto* inputs = targets->addAction(QStringLiteral("Last run inputs…"));
    inputs->setObjectName(QStringLiteral("showSimulationInputs"));
    connect(inputs, &QAction::triggered, this, [this] {
        const auto plan = m_controller->lastPlan(); if (!plan) return;
        QPointer<ElaDialog> dialog = new ElaDialog(this);
        dialog->setWindowTitle(QStringLiteral("Last run inputs")); dialog->setObjectName(QStringLiteral("simulationInputsDialog"));
        dialog->setIsDefaultClosed(true); dialog->setIsStayTop(false); dialog->setAppBarHeight(36); dialog->resize(640, 420);
        auto* layout = new QVBoxLayout(dialog); layout->setContentsMargins(20, 20, 20, 20);
        auto* listing = Ui::textView(dialog); listing->setObjectName(QStringLiteral("simulationInputsResult")); listing->setReadOnly(true);
        QStringList lines{plan->target.top + QStringLiteral(" — ") + plan->target.file, QString(), QStringLiteral("Compilation order")};
        const QDir directory(workspace());
        for (int i = 0; i < plan->compilation.files.size(); ++i)
            lines << QStringLiteral("%1. %2").arg(i + 1).arg(directory.relativeFilePath(plan->compilation.files[i]));
        if (!plan->compilation.headers.isEmpty()) {
            lines << QString() << QStringLiteral("Included headers");
            for (const auto& file : plan->compilation.headers) lines << directory.relativeFilePath(file);
        }
        listing->setPlainText(lines.join('\n')); layout->addWidget(listing);
        auto* close = Ui::button(QStringLiteral("Close"), dialog); layout->addWidget(close);
        connect(close, &QPushButton::clicked, dialog, &QDialog::accept);
        Ui::normalizeControls(dialog); Ui::constrainDialog(dialog);
        dialog->exec(); if (dialog) delete dialog;
    });
    m_target->setMenu(targets); row->addWidget(m_target, 1);
    m_status = UiControls::label(this); m_status->setObjectName(QStringLiteral("runStatus"));
    m_status->setTextFormat(Qt::PlainText); m_status->setMaximumWidth(240);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); row->addWidget(m_status, 1);
    m_run = Ui::button(QStringLiteral("Run"), this, true); m_run->setObjectName(QStringLiteral("startSimulation"));
    m_stop = Ui::button(QStringLiteral("Stop"), this); m_stop->setObjectName(QStringLiteral("stopSimulation"));
    row->addWidget(m_run); row->addWidget(m_stop);
    connect(m_run, &QPushButton::clicked, this, &Workbench::startSimulation);
    connect(m_stop, &QPushButton::clicked, m_controller, &WorkspaceSimulationController::stop);
    m_settings = UiControls::toolButton(this); m_settings->setObjectName(QStringLiteral("openSettings"));
    m_settings->setIcon(RoundedIcons::icon(RoundedIcons::Settings));
    m_settings->setAccessibleName(QStringLiteral("Simulation settings")); m_settings->setToolTip(QStringLiteral("Simulation settings"));
    connect(m_settings, &QToolButton::clicked, this, &Workbench::openSettings); row->addWidget(m_settings);
    auto* logs = new QWidget(this); m_logs = logs; logs->setObjectName(QStringLiteral("simulationRunLog"));
    auto* logLayout = new QVBoxLayout(logs); logLayout->setContentsMargins(10, 8, 10, 8);
    auto* heading = new QHBoxLayout; heading->addStretch();
    auto* clear = Ui::button(QStringLiteral("Clear"), logs); clear->setObjectName(QStringLiteral("clearLog"));
    heading->addWidget(clear); logLayout->addLayout(heading);
    connect(clear, &QPushButton::clicked, m_controller, &WorkspaceSimulationController::clearLog);
    m_log = Ui::textView(logs); m_log->setObjectName(QStringLiteral("simulationLog"));
    m_log->setProperty("codeSurface", true); m_log->setNativeTextBehavior(true); m_log->setReadOnly(true);
    m_log->setFont(Ui::codeFont()); m_log->setPlaceholderText(QStringLiteral("Compiler, loader, and simulation output will appear here."));
    m_log->setToolTip(QStringLiteral("Double-click a source location to open it."));
    m_log->viewport()->installEventFilter(this);
    logLayout->addWidget(m_log, 1); logs->hide();
    Ui::normalizeControls(this);
}
bool Workbench::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick && m_log && watched == m_log->viewport()) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const auto text = m_log->cursorForPosition(mouse->position().toPoint()).block().text();
        if (mouse->button() == Qt::LeftButton && text.size() <= 32768) {
            for (int dot = text.indexOf('.'); dot >= 0; dot = text.indexOf('.', dot + 1)) {
                for (const auto* extension : {".svh", ".sv", ".vh", ".v"}) {
                    const auto suffix = QLatin1String(extension);
                    const int end = dot + suffix.size();
                    if (text.mid(dot, suffix.size()).compare(suffix, Qt::CaseInsensitive) || end >= text.size()
                        || (text[end] != ':' && text[end] != '(')) continue;
                    int cursor = end + 1;
                    while (cursor < text.size() && text[cursor].isDigit()) ++cursor;
                    bool valid = false;
                    const int parsed = text.mid(end + 1, cursor - end - 1).toInt(&valid);
                    if (text[end] == '(' && (!valid || cursor >= text.size() || text[cursor] != ')')) continue;
                    for (int start = 0; start < dot; ++start) {
                        if (start && !text[start - 1].isSpace() && text[start - 1] != '"' && text[start - 1] != '\'') continue;
                        const auto path = QDir(workspace()).absoluteFilePath(text.mid(start, end - start).trimmed());
                        const QFileInfo file(path);
                        if (file.isFile() && insideWorkspace(workspace(), file.canonicalFilePath())) {
                            emit openSourceRequested(file.canonicalFilePath(), valid && parsed > 0 ? parsed : 1);
                            return true;
                        }
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
void Workbench::updateControls()
{
    const auto target = m_controller->target();
    const auto title = target.isEmpty() ? QStringLiteral("Current editor") : target.top + QStringLiteral(" · ") + QFileInfo(target.file).fileName();
    m_target->setText(title); m_target->setAccessibleName(QStringLiteral("Simulation target: ") + title);
    m_target->setToolTip(target.isEmpty() ? QStringLiteral("Run the current editor as the TB") : target.file + QStringLiteral(" — ") + target.top);
    m_target->setEnabled(!workspace().isEmpty() && !m_controller->busy());
    findChild<QAction*>(QStringLiteral("showSimulationInputs"))->setEnabled(bool(m_controller->lastPlan()));
    m_run->setEnabled(!workspace().isEmpty() && !m_controller->busy());
    m_stop->setEnabled(m_controller->busy() || m_controller->session()->alive());
    m_stop->setVisible(m_controller->busy() || m_controller->session()->alive());
    m_stop->setText(m_controller->preparing() ? QStringLiteral("Cancel") : QStringLiteral("Stop"));
    m_status->setVisible(!m_status->text().isEmpty() && m_status->text() != QStringLiteral("Ready"));
}
void Workbench::reportIssue(const QString& issue)
{
    if (issue.isEmpty()) return;
    m_controller->appendLog(issue + '\n'); m_status->setText(issue); m_status->setToolTip(issue); updateControls();
}
void Workbench::initialize()
{
    if (!m_standalone) return;
    if (!simulatorError(m_simulator).isEmpty()) openSettings();
    const auto root = preference(QStringLiteral("workspace/last")).toString();
    if (QFileInfo(root).isDir()) openWorkspace(root);
}
void Workbench::setSimulator(const QString& executable)
{
    if (m_controller->busy() || (m_controller->session()->alive() && m_simulator != executable)) return;
    m_simulator = executable; setPreference(QStringLiteral("simulator/path"), executable); updateControls();
}
void Workbench::openSettings()
{
    QPointer<Workbench> owner(this);
    QPointer<SettingsDialog> dialog = new SettingsDialog(m_simulator, this, m_controller->busy() || m_controller->session()->alive(),
        m_controller, m_configureWorkspace);
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    if (result == QDialog::Accepted) {
        setSimulator(dialog->executable());
    }
    delete dialog; updateControls();
}
void Workbench::openWorkspace(const QString& path) { reportIssue(setContext(path)); }
QString Workbench::setContext(const QString& path)
{
    const auto issue = contextError(); if (!issue.isEmpty()) return issue;
    const auto root = path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
    if (!path.isEmpty() && (root.isEmpty() || !QFileInfo(root).isDir())) return QStringLiteral("The workspace directory does not exist.");
    if (root == workspace()) return {};
    const auto running = m_controller->canChangeWorkspace(); if (!running.isEmpty()) return running;
    const auto previous = m_design->workspace();
    const auto design = m_design->setWorkspace(root); if (!design.isEmpty()) return design;
    const auto simulation = m_controller->setWorkspace(root);
    if (!simulation.isEmpty()) { m_design->setWorkspace(previous); return simulation; }
    if (m_standalone) setPreference(QStringLiteral("workspace/last"), root);
    updateControls(); emit scanFinished(); emit stateChanged(); return {};
}
bool Workbench::openSuiteTarget(const QString& root, const QString& id, QString* error)
{ *error = setContext(root); if (error->isEmpty() && !id.isEmpty()) *error = openProject(id); return error->isEmpty(); }
QString Workbench::openProject(const QString& id)
{
    if (id.isEmpty()) return {};
    const auto issue = contextError(); if (!issue.isEmpty()) return issue;
    QSet<QString> ambiguous;
    const auto projects = loadProjects(workspace(), nullptr, false, &ambiguous);
    if (ambiguous.contains(id)) return QStringLiteral("The legacy project identity is ambiguous.");
    for (const auto& project : projects) if (project.id == id) return m_controller->selectLegacyProject(project);
    return QStringLiteral("The saved simulation project no longer exists.");
}
QString Workbench::selectTarget(const QString& file, const QMap<QString, QByteArray>& overrides)
{
    if (m_controller->busy()) return QStringLiteral("Stop simulation before changing the TB.");
    const auto config = WorkspaceConfigurationService().loadWithResult(workspace());
    if (!config.usable()) return config.message;
    QStringList includes;
    for (const auto& path : config.configuration.includeDirs + m_controller->includeDirectories()) includes << QDir(workspace()).absoluteFilePath(path);
    QString error;
    const auto tops = declaredSimulationTops(workspace(), file, includes, config.configuration.defines, overrides, &error);
    if (!error.isEmpty()) return error;
    if (tops.isEmpty()) return QStringLiteral("The selected file declares no module or program top.");
    QString top = tops.first();
    if (tops.size() > 1) {
        QPointer<ElaDialog> dialog = new ElaDialog(this);
        dialog->setWindowTitle(QStringLiteral("Choose TB top")); dialog->setObjectName(QStringLiteral("simulationTopDialog"));
        dialog->setIsDefaultClosed(true); dialog->setIsStayTop(false); dialog->setAppBarHeight(36);
        dialog->resize(420, 200);
        auto* layout = new QVBoxLayout(dialog); layout->setContentsMargins(24, 44, 24, 20);
        auto* choices = new ElaComboBox(dialog); choices->setObjectName(QStringLiteral("simulationTopChoices")); choices->addItems(tops);
        layout->addWidget(choices);
        auto* select = Ui::button(QStringLiteral("Select"), dialog, true); layout->addWidget(select);
        connect(select, &QPushButton::clicked, dialog, &QDialog::accept);
        Ui::normalizeControls(dialog);
        Ui::constrainDialog(dialog);
        const auto result = dialog->exec();
        if (!dialog) return QStringLiteral("TB selection cancelled.");
        top = result == QDialog::Accepted ? choices->currentText() : QString(); delete dialog;
        if (top.isEmpty()) return QStringLiteral("TB selection cancelled.");
    }
    return m_controller->selectTarget({file, top}, {}, overrides);
}
void Workbench::chooseTb()
{
    QPointer<Workbench> owner(this);
    QPointer<QFileDialog> dialog = new QFileDialog(this, QStringLiteral("Choose TB"), workspace(), QStringLiteral("HDL files (*.sv *.v)"));
    dialog->setFileMode(QFileDialog::ExistingFile); dialog->setOption(QFileDialog::DontUseNativeDialog);
    const auto result = dialog->exec(); if (!owner || !dialog) return;
    const auto paths = dialog->selectedFiles(); delete dialog;
    if (result == QDialog::Accepted && !paths.isEmpty()) reportIssue(m_targetHandler ? m_targetHandler(paths.first()) : selectTarget(paths.first()));
}
void Workbench::startSimulation()
{
    if (m_controller->busy() || workspace().isEmpty()) return;
    QPointer<Workbench> owner(this);
    if (!simulatorError(m_simulator).isEmpty()) openSettings();
    if (!owner) return;
    const auto issue = simulatorError(m_simulator); if (!issue.isEmpty()) { reportIssue(issue); return; }
    if (m_runHandler) { reportIssue(m_runHandler()); return; }
    if (m_controller->target().isEmpty()) { reportIssue(QStringLiteral("Choose a TB file first.")); return; }
    Project project; project.tbFile = m_controller->target().file; project.tbName = m_controller->target().top;
    project.includeDirectories = m_controller->includeDirectories();
    m_controller->prepare(m_simulator, project, waveTheme());
}
QList<DesignHierarchyRoot> Workbench::designRoots() const { return m_design->designRoots(); }
QString Workbench::addDesignProject(const QString& file, const QString& module, bool activate) { return m_design->addDesignProject(file, module, activate); }
QString Workbench::removeDesignRoot(const QString& id) { return m_design->removeDesignRoot(id); }
void Workbench::setDesignExpanded(const QString& id, bool expanded) { reportIssue(m_design->setDesignExpanded(id, expanded)); }
bool Workbench::designMigrationCompleted() const { return m_design->designMigrationCompleted(); }
void Workbench::markDesignMigrationCompleted() { reportIssue(m_design->markDesignMigrationCompleted()); }
void Workbench::setDarkTheme(bool)
{ Ui::applyTheme(this); if (m_logs) Ui::applyTheme(m_logs); update(); }
QVariant Workbench::preference(const QString& key, const QVariant& fallback) const
{ return m_standalone ? QSettings().value(key, fallback) : m_preferences.value(key, fallback); }
void Workbench::setPreference(const QString& key, const QVariant& value)
{
    if (preference(key) == value) return;
    if (m_standalone) QSettings().setValue(key, value); else m_preferences.insert(key, value);
    emit preferencesChanged(); emit stateChanged();
}
QString Workbench::contextError() const
{
    for (const auto* dialog : findChildren<QDialog*>()) if (dialog->isVisible()) return QStringLiteral("Finish or cancel the open dialog first.");
    return {};
}
bool Workbench::canClose() const { return contextError().isEmpty(); }
QVariantMap Workbench::saveState() const
{
    QVariantMap preferences;
    for (const auto* key : {"simulator/path", "waveform/theme", "theme/dark"}) {
        const auto value = preference(QString::fromLatin1(key)); if (value.isValid()) preferences.insert(QString::fromLatin1(key), value);
    }
    return {{QStringLiteral("version"), 1}, {QStringLiteral("workspace"), workspace()},
        {QStringLiteral("projectId"), m_controller->legacyProjectId()}, {QStringLiteral("preferences"), preferences}};
}
QString Workbench::restoreState(const QVariantMap& state)
{
    if (state.value(QStringLiteral("version")).toInt() != 1) return QStringLiteral("Unsupported saved simulation state version.");
    const auto issue = setContext(state.value(QStringLiteral("workspace")).toString()); if (!issue.isEmpty()) return issue;
    // The workspace record is authoritative; old layouts cannot replace a newer target.
    if (m_controller->target().isEmpty() && !state.value(QStringLiteral("projectId")).toString().isEmpty()) {
        const auto legacy = openProject(state.value(QStringLiteral("projectId")).toString());
        if (!legacy.isEmpty()) reportIssue(legacy);
    }
    const auto preferences = state.value(QStringLiteral("preferences")).toMap();
    for (const auto* key : {"simulator/path", "waveform/theme"}) {
        const auto name = QString::fromLatin1(key); if (preferences.contains(name)) setPreference(name, preferences.value(name));
    }
    setSimulator(preference(QStringLiteral("simulator/path")).toString()); return {};
}
void Workbench::savePreferences() {}
void Workbench::shutdown() { m_controller->shutdown(); }
}
