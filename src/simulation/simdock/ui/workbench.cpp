#include "workbench.h"
#include "simulationeventspanel.h"
#include "dialogs.h"
#include "uistyle.h"
#include "../core/workspace.h"
#include "applicationthememanager.h"
#include "roundedicons.h"
#include "uicontrols.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
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
        updateControls();
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
QSize Workbench::minimumSizeHint() const { return {112, 32}; }
WaveTheme Workbench::waveTheme() const
{ return waveThemeFromId(preference(QStringLiteral("waveform/theme"), QStringLiteral("default")).toString()); }
void Workbench::buildUi()
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0); row->setSpacing(6);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_run = Ui::button(QStringLiteral("Run"), this, true);
    m_run->setObjectName(QStringLiteral("startSimulation"));
    m_run->setMinimumWidth(76);
    m_run->setMaximumWidth(240);
    m_run->installEventFilter(this);
    row->addWidget(m_run);
    connect(m_run, &QPushButton::clicked, this, &Workbench::startSimulation);
    m_settings = UiControls::toolButton(this); m_settings->setObjectName(QStringLiteral("openSettings"));
    m_settings->setIcon(RoundedIcons::icon(RoundedIcons::SimulationSettings));
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
    if (watched == m_run && event->type() == QEvent::Resize) updateControls();
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
    const bool active = m_controller->busy();
    if (active && !m_wasBusy) m_runningTarget = m_controller->target();
    m_wasBusy = active;
    const auto target = active ? m_runningTarget : m_controller->target();
    const QString action = m_controller->preparing() ? QStringLiteral("Cancel") : active ? QStringLiteral("Stop") : QStringLiteral("Run");
    m_run->setEnabled(active || !workspace().isEmpty());
    const QString label = target.isEmpty() ? action : action + QStringLiteral(" · ") + target.top;
    m_run->setText(m_run->fontMetrics().elidedText(label, Qt::ElideRight, 208));
    m_run->setIcon(RoundedIcons::icon(m_controller->preparing() ? RoundedIcons::Cancel
        : active ? RoundedIcons::StopSimulation : RoundedIcons::RunSimulation));
    m_run->setToolTip(target.isEmpty() ? QStringLiteral("Choose a simulation top in Design")
        : target.top + QStringLiteral(" — ") + target.file + '\n' + m_controller->state());
    m_run->setAccessibleName(target.isEmpty() ? QStringLiteral("Choose a simulation target in Design") : label);
    m_run->setProperty("simulationTarget", target.top);
    m_run->setProperty("simulationAction", action);
}
void Workbench::reportIssue(const QString& issue)
{
    if (issue.isEmpty()) return;
    m_controller->appendLog(issue + '\n');
    emit issueRaised(issue);
    updateControls();
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
    if (m_controller->busy()) return;
    if (m_controller->session()->alive() && m_simulator != executable) m_controller->stop();
    m_simulator = executable; setPreference(QStringLiteral("simulator/path"), executable); updateControls();
}
void Workbench::openSettings()
{
    QPointer<Workbench> owner(this);
    QPointer<SettingsDialog> dialog = new SettingsDialog(m_simulator, this, m_controller->busy(),
        m_controller, m_configureWorkspace, waveTheme());
    const auto result = dialog->exec();
    if (!owner || !dialog) return;
    if (result == QDialog::Accepted) {
        setSimulator(dialog->executable());
        setPreference(QStringLiteral("waveform/theme"), wavePalette(dialog->waveTheme()).id);
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
    if (tops.size() != 1) return QStringLiteral("Select the exact module or program as simulation top in Design.");
    return m_controller->selectTarget({file, tops.first()}, {}, overrides);
}
void Workbench::startSimulation()
{
    if (m_controller->busy()) {
        m_controller->stop();
        updateControls();
        return;
    }
    if (workspace().isEmpty()) { reportIssue(QStringLiteral("Open a workspace before running simulation.")); return; }
    if (m_controller->target().isEmpty()) {
        if (m_standalone) reportIssue(QStringLiteral("Select a module in Design and set it as the simulation top."));
        else emit selectSimulationTargetRequested();
        return;
    }
    QPointer<Workbench> owner(this);
    if (!simulatorError(m_simulator).isEmpty()) openSettings();
    if (!owner) return;
    const auto issue = simulatorError(m_simulator); if (!issue.isEmpty()) { reportIssue(issue); return; }
    if (m_runHandler) { reportIssue(m_runHandler()); return; }
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
