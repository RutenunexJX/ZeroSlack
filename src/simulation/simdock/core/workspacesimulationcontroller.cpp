#include "workspacesimulationcontroller.h"
#include <QElapsedTimer>
#include "simulationeventmodel.h"
#include "workspace.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QPlainTextDocumentLayout>
#include <QSettings>
#include <QTextCursor>
#include <QTextDocument>
#include <QtConcurrent>

namespace simdock {
namespace {
QJsonObject targetRecord(const SimulationTarget& target, const QString& legacyId, const QStringList& includes,
    const QMap<QString, QString>& mappings = {})
{
    QJsonObject sources;
    for (auto it = mappings.cbegin(); it != mappings.cend(); ++it) sources.insert(it.key(), it.value());
    return {{"target", QJsonObject{{"file", target.file}, {"top", target.top}}}, {"legacyProjectId", legacyId},
        {"includeDirectories", QJsonArray::fromStringList(includes)}, {"definitionSources", sources}, {"legacyImported", true}};
}
QString targetIssue(const QString& root, const SimulationTarget& target, const QStringList& extraIncludes,
    const QMap<QString, QByteArray>& overrides = {})
{
    const auto loaded = WorkspaceConfigurationService().loadWithResult(root);
    if (!loaded.usable()) return loaded.message;
    const auto& config = loaded.configuration;
    auto includes = config.includeDirs;
    for (auto& path : includes) path = QDir(root).absoluteFilePath(path);
    for (const auto& path : extraIncludes) includes << QDir(root).absoluteFilePath(path);
    return validateSimulationTarget(root, target, includes, config.defines, overrides);
}
}
WorkspaceSimulationController::WorkspaceSimulationController(QObject* parent)
    : QObject(parent), m_session(this), m_log(new QTextDocument(this)), m_events(new SimulationEventModel(this))
{
    setObjectName(QStringLiteral("workspaceSimulationController"));
    m_pool.setMaxThreadCount(1);
    m_log->setDocumentLayout(new QPlainTextDocumentLayout(m_log));
    m_log->setMaximumBlockCount(10000);
    m_logTimer.setSingleShot(true); m_logTimer.setInterval(16);
    connect(&m_logTimer, &QTimer::timeout, this, &WorkspaceSimulationController::flushLog);
    connect(&m_session, &QuestaSession::logText, this, &WorkspaceSimulationController::appendLog);
    connect(&m_session, &QuestaSession::stateChanged, this, [this](const QString& state) { m_state = state; emit stateChanged(); });
    connect(&m_session, &QuestaSession::finished, this, [this](bool completed, const QString& message) {
        flushLog(); emit finished(completed, message); emit stateChanged();
    });
    connect(&m_session, &QuestaSession::eventRunStarted, m_events, &SimulationEventModel::startRun);
    connect(&m_session, &QuestaSession::eventsReady, m_events, &SimulationEventModel::append);
    connect(&m_session, &QuestaSession::eventRecordsRejected, m_events, &SimulationEventModel::reject);
    connect(&m_session, &QuestaSession::eventRunCleared, m_events, [this] { m_events->startRun({}); });
}
WorkspaceSimulationController::~WorkspaceSimulationController()
{ shutdown(); m_pool.waitForDone(); }
QString WorkspaceSimulationController::canChangeWorkspace() const
{ return busy() ? tr("Stop the running simulation or cancel preparation before closing or changing workspace.") : QString(); }
QString WorkspaceSimulationController::setWorkspace(const QString& path)
{
    const auto root = path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
    if (!path.isEmpty() && (root.isEmpty() || !QFileInfo(root).isDir())) return tr("The workspace does not exist.");
    if (root == m_root) return {};
    const auto issue = canChangeWorkspace();
    if (!issue.isEmpty()) return issue;
    WorkspaceStateFile store;
    SimulationTarget target;
    QString legacyId, error;
    QStringList includes;
    QMap<QString, QString> mappings;
    if (!root.isEmpty()) {
        if (!store.open(root, QStringLiteral("simulation"), QStringLiteral("zeroslack.simulation/v1"), &error)) return error;
        if (store.exists()) {
            const auto state = store.value(), saved = state.value("target").toObject();
            if (!state.value("target").isObject() || !saved.value("file").isString() || !saved.value("top").isString()
                || !state.value("includeDirectories").isArray()) return tr("Invalid simulation state; the record was preserved.");
            target = {saved.value("file").toString(), saved.value("top").toString()};
            legacyId = state.value("legacyProjectId").toString();
            for (const auto& value : state.value("includeDirectories").toArray()) {
                if (!value.isString() || value.toString().trimmed().isEmpty() || QDir::isAbsolutePath(value.toString())
                    || !insideWorkspace(root, QDir(root).filePath(value.toString())))
                    return tr("Invalid simulation include path; the record was preserved.");
                includes << value.toString();
            }
            if (state.contains("definitionSources") && !state.value("definitionSources").isObject())
                return tr("Invalid simulation source mappings; the record was preserved.");
            const auto sources = state.value("definitionSources").toObject();
            for (auto it = sources.begin(); it != sources.end(); ++it) {
                if (it.key().trimmed().isEmpty() || !it.value().isString() || it.value().toString().trimmed().isEmpty() || QDir::isAbsolutePath(it.value().toString())
                    || !insideWorkspace(root, QDir(root).filePath(it.value().toString())))
                    return tr("Invalid simulation source mapping; the record was preserved.");
                mappings.insert(it.key(), it.value().toString());
            }
            if ((!target.file.isEmpty() && (QDir::isAbsolutePath(target.file) || !insideWorkspace(root, QDir(root).filePath(target.file))))
                || target.file.isEmpty() != target.top.isEmpty()) return tr("Invalid saved TB identity; the record was preserved.");
        } else {
            const auto key = QStringLiteral("integrations/simdock/workspaces/") + QString::fromLatin1(QCryptographicHash::hash(
                QDir::fromNativeSeparators(root).toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
            const auto savedId = QSettings().value(key + "/activeProject").toString();
            QSet<QString> ambiguousIds;
            const auto projects = loadProjects(root, nullptr, false, &ambiguousIds);
            for (const auto& project : projects) if (!savedId.isEmpty() && project.id == savedId && !ambiguousIds.contains(savedId)) {
                const SimulationTarget candidate{project.tbFile, project.tbName};
                if (targetIssue(root, candidate, project.includeDirectories).isEmpty()) {
                    target = candidate; legacyId = project.id; includes = project.includeDirectories;
                }
            }
            if (!store.save(targetRecord(target, legacyId, includes), &error)) return error;
        }
    }
    stop();
    m_root = root; m_target = target; m_legacyProjectId = legacyId; m_includeDirectories = includes; m_store = std::move(store);
    m_definitionSources = mappings;
    m_lastPlan.reset();
    clearLog(); m_events->startRun({}); m_state = QStringLiteral("Ready");
    emit targetChanged(); emit stateChanged();
    return {};
}
QString WorkspaceSimulationController::saveTarget(const SimulationTarget& target, const QString& legacyId, const QStringList& includes)
{
    if (target == m_target && legacyId == m_legacyProjectId && includes == m_includeDirectories) return {};
    if (busy()) return tr("Stop simulation or cancel preparation before selecting another TB.");
    QString error;
    if (!m_store.save(targetRecord(target, legacyId, includes, m_definitionSources), &error)) return error;
    m_target = target; m_legacyProjectId = legacyId; m_includeDirectories = includes;
    emit targetChanged(); return {};
}
QString WorkspaceSimulationController::selectTarget(const SimulationTarget& target, const QString& legacyId,
    const QMap<QString, QByteArray>& overrides)
{
    const auto issue = targetIssue(m_root, target, m_includeDirectories, overrides);
    const SimulationTarget relative{QDir(m_root).relativeFilePath(QDir(m_root).absoluteFilePath(target.file)), target.top};
    return issue.isEmpty() ? saveTarget(relative, legacyId, m_includeDirectories) : issue;
}
QString WorkspaceSimulationController::selectLegacyProject(const Project& project)
{
    const SimulationTarget target{project.tbFile, project.tbName};
    if (target.isEmpty()) return saveTarget({}, project.id, project.includeDirectories);
    const auto issue = targetIssue(m_root, target, project.includeDirectories);
    return issue.isEmpty() ? saveTarget(target, project.id, project.includeDirectories) : issue;
}
QString WorkspaceSimulationController::clearTarget() { return saveTarget({}, {}, m_includeDirectories); }
QString WorkspaceSimulationController::setSettings(const QStringList& includes, const QMap<QString, QString>& sources)
{
    if (m_root.isEmpty() || busy()) return tr("Open an idle workspace before changing simulation settings.");
    QStringList relativeIncludes;
    QMap<QString, QString> relativeSources;
    for (const auto& path : includes) {
        const auto absolute = QDir(m_root).absoluteFilePath(path);
        if (!insideWorkspace(m_root, absolute) || !QFileInfo(absolute).isDir())
            return tr("Include directory is unavailable or outside the workspace: %1").arg(path);
        relativeIncludes << QDir(m_root).relativeFilePath(absolute);
    }
    relativeIncludes.removeDuplicates();
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        const auto absolute = QDir(m_root).absoluteFilePath(it.value());
        if (it.key().trimmed().isEmpty() || !insideWorkspace(m_root, absolute) || !QFileInfo(absolute).isFile())
            return tr("Definition source is unavailable or outside the workspace: %1").arg(it.value());
        relativeSources.insert(it.key(), QDir(m_root).relativeFilePath(absolute));
    }
    if (relativeIncludes == m_includeDirectories && relativeSources == m_definitionSources) return {};
    QString error;
    if (!m_store.save(targetRecord(m_target, m_legacyProjectId, relativeIncludes, relativeSources), &error)) return error;
    m_includeDirectories = relativeIncludes; m_definitionSources = relativeSources;
    emit targetChanged(); return {};
}
void WorkspaceSimulationController::preparationFailed(const QString& error)
{
    m_preparing = false; m_state = error; appendLog(error + '\n');
    emit finished(false, error); emit stateChanged();
}
void WorkspaceSimulationController::prepare(const QString& executable, SimulationBuildRequest request, WaveTheme theme,
    SaveInputs saveInputs, CheckCurrent checkCurrent, AwaitInputs ready)
{
    if (busy() || m_root.isEmpty()) return;
    if (QFileInfo(request.project.workspaceRoot).canonicalFilePath() != m_root) {
        preparationFailed(tr("The simulation request belongs to a different workspace.")); return;
    }
    clearLog();
    const auto generation = ++m_request;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    m_preparing = true; m_state = tr("Planning TB inputs…"); emit stateChanged();
    awaitInputs(std::move(request), generation, ready,
        [this, executable, theme, generation, saveInputs, checkCurrent, ready](SimulationBuildRequest captured) {
            planAndSave(executable, std::move(captured), theme, generation, saveInputs, checkCurrent, ready);
        });
}
void WorkspaceSimulationController::awaitInputs(SimulationBuildRequest request, quint64 generation, AwaitInputs ready,
    std::function<void(SimulationBuildRequest)> next)
{
    if (generation != m_request) return;
    QString error;
    if (!ready || ready(request, error)) {
        if (error.isEmpty()) next(std::move(request));
        else preparationFailed(error);
        return;
    }
    if (!error.isEmpty()) { preparationFailed(error); return; }
    m_state = tr("Waiting for workspace inputs…"); emit stateChanged();
    auto* timer = new QTimer(this);
    QElapsedTimer elapsed; elapsed.start();
    connect(timer, &QTimer::timeout, this,
        [this, timer, generation, elapsed, request = std::move(request), ready = std::move(ready), next = std::move(next)]() mutable {
            if (generation != m_request) { timer->stop(); timer->deleteLater(); return; }
            QString issue;
            const bool available = ready(request, issue);
            if (!available && issue.isEmpty() && elapsed.elapsed() < 120000) return;
            timer->stop(); timer->deleteLater();
            if (!issue.isEmpty()) preparationFailed(issue);
            else if (!available) preparationFailed(tr("Workspace input discovery timed out during preparation."));
            else next(std::move(request));
        });
    timer->start(50);
}
void WorkspaceSimulationController::planAndSave(const QString& executable, SimulationBuildRequest request, WaveTheme theme,
    quint64 generation, SaveInputs saveInputs, CheckCurrent checkCurrent, AwaitInputs ready)
{
    if (generation != m_request) return;
    if (!saveInputs) { prepareCaptured(executable, request, theme, generation, std::move(checkCurrent)); return; }
    m_state = tr("Planning TB inputs…"); emit stateChanged();
    auto* watcher = new QFutureWatcher<std::shared_ptr<const SimulationBuildPlan>>(this);
    connect(watcher, &QFutureWatcher<std::shared_ptr<const SimulationBuildPlan>>::finished, this,
        [this, watcher, generation, executable, request, theme, saveInputs = std::move(saveInputs), checkCurrent = std::move(checkCurrent), ready = std::move(ready)]() mutable {
            const auto plan = watcher->result(); watcher->deleteLater();
            if (generation != m_request) return;
            if (!plan->valid()) { preparationFailed(plan->error()); return; }
            const auto issue = saveInputs(*plan, request);
            if (generation != m_request) return;
            if (!issue.isEmpty()) { preparationFailed(issue); return; }
            awaitInputs(std::move(request), generation, std::move(ready),
                [this, executable, theme, generation, checkCurrent](SimulationBuildRequest captured) {
                    prepareCaptured(executable, captured, theme, generation, checkCurrent);
                });
        });
    watcher->setFuture(QtConcurrent::run(&m_pool, [request, cancelled = m_cancelled] {
        return SimulationBuildPlanner::plan(request, [cancelled] { return cancelled->load(); });
    }));
}
void WorkspaceSimulationController::prepareCaptured(const QString& executable, const SimulationBuildRequest& request,
    WaveTheme theme, quint64 generation, CheckCurrent checkCurrent)
{
    m_state = tr("Preparing simulation…"); emit stateChanged();
    auto* watcher = new QFutureWatcher<PreparedRun>(this);
    connect(watcher, &QFutureWatcher<PreparedRun>::finished, this,
        [this, watcher, generation, request, theme, checkCurrent = std::move(checkCurrent)] {
            const auto result = watcher->result(); watcher->deleteLater();
            if (generation != m_request) return;
            QString error = result.error;
            if (error.isEmpty() && checkCurrent) error = checkCurrent(request);
            if (!error.isEmpty()) { preparationFailed(error); return; }
            m_lastPlan = result.plan;
            emit planReady(result.plan);
            if (generation != m_request) return;
            m_preparing = false;
            if (!m_session.start(result, &error, theme)) preparationFailed(error);
            else emit stateChanged();
        });
    watcher->setFuture(QtConcurrent::run(&m_pool, [executable, request, cancelled = m_cancelled] {
        return prepareRun(executable, request, cancelled.get());
    }));
}
void WorkspaceSimulationController::prepare(const QString& executable, const Project& project, WaveTheme theme,
    const QMap<QString, QByteArray>& overrides)
{
    if (busy() || m_root.isEmpty()) return;
    clearLog();
    const auto root = m_root;
    const auto request = ++m_request;
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    m_preparing = true; m_state = QStringLiteral("Preparing simulation…"); emit stateChanged();
    auto* watcher = new QFutureWatcher<PreparedRun>(this);
    connect(watcher, &QFutureWatcher<PreparedRun>::finished, this, [this, watcher, request, theme] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (request != m_request) return;
        QString error = result.error;
        if (error.isEmpty()) {
            m_lastPlan = result.plan;
            emit planReady(result.plan);
            if (request != m_request) return;
        }
        m_preparing = false;
        if (!error.isEmpty() || !m_session.start(result, &error, theme)) {
            appendLog(error + QLatin1Char('\n')); m_state = error; emit finished(false, error);
        }
        emit stateChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [root, executable, project, overrides, cancelled = m_cancelled] {
        return prepareRun(executable, root, project, cancelled.get(), overrides);
    }));
}
void WorkspaceSimulationController::cancelPreparation()
{
    if (m_cancelled) m_cancelled->store(true);
    ++m_request;
    if (m_preparing) {
        m_preparing = false; m_state = QStringLiteral("Preparation cancelled");
        emit finished(false, m_state); emit stateChanged();
    }
}
void WorkspaceSimulationController::stop()
{ cancelPreparation(); m_session.stop(); }
void WorkspaceSimulationController::shutdown()
{ stop(); flushLog(); }
void WorkspaceSimulationController::appendLog(const QString& text)
{
    m_pendingLog += text;
    if (m_pendingLog.size() >= 1024 * 1024) flushLog();
    else if (!m_logTimer.isActive()) m_logTimer.start();
}
void WorkspaceSimulationController::clearLog()
{ m_logTimer.stop(); m_pendingLog.clear(); m_log->clear(); }
void WorkspaceSimulationController::flushLog()
{
    m_logTimer.stop();
    if (m_pendingLog.isEmpty()) return;
    emit logAboutToAppend();
    QTextCursor cursor(m_log);
    cursor.movePosition(QTextCursor::End); cursor.insertText(m_pendingLog); m_pendingLog.clear();
    constexpr int limit = 8 * 1024 * 1024;
    if (m_log->characterCount() > limit) {
        cursor.setPosition(0); cursor.setPosition(m_log->characterCount() - limit, QTextCursor::KeepAnchor); cursor.removeSelectedText();
    }
    emit logAppended();
}
}
