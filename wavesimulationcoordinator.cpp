#include "wavesimulationcoordinator.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QtConcurrent>

WaveSimulationCoordinator::WaveSimulationCoordinator(QObject* parent)
    : QObject(parent)
{
}

WaveSimulationCoordinator::~WaveSimulationCoordinator()
{
    cancel();
}

bool WaveSimulationCoordinator::start(
    const WaveSimulationRunRequest& request,
    QString* failureReason)
{
    if (active) {
        if (failureReason)
            *failureReason = QStringLiteral("A Wave Simulation run is already active.");
        return false;
    }
    if (!request.tools.isValid()) {
        const QString message = QStringLiteral(
            "WaveWorkbench tools are unavailable: %1")
                                    .arg(request.tools.missingTools().join(
                                        QStringLiteral(", ")));
        if (failureReason)
            *failureReason = message;
        return false;
    }

    currentRequest = request;
    prepared = {};
    standardOutput.clear();
    standardError.clear();
    cancellation = std::make_shared<std::atomic_bool>(false);
    active = true;
    processTerminalHandled = false;
    setStage(WaveSimulationStage::Preparing,
             QStringLiteral("Preparing the selected module for Wave Simulation..."));

    watcher = new QFutureWatcher<WaveSimulationPreparationResult>(this);
    connect(watcher,
            &QFutureWatcher<WaveSimulationPreparationResult>::finished,
            this,
            &WaveSimulationCoordinator::handlePreparationFinished);
    const WaveSimulationPreparationRequest preparation =
        request.preparation;
    const std::shared_ptr<std::atomic_bool> cancelFlag = cancellation;
    watcher->setFuture(QtConcurrent::run(
        [preparation, cancelFlag]() {
            return WaveSimulationPreparationService::prepare(
                preparation,
                [cancelFlag]() {
                    return cancelFlag->load(std::memory_order_relaxed);
                });
        }));
    return true;
}

void WaveSimulationCoordinator::cancel()
{
    if (!active)
        return;
    if (cancellation)
        cancellation->store(true, std::memory_order_relaxed);
    if (process) {
        processTerminalHandled = true;
        process->kill();
        clearProcess();
        finishCancelled();
    }
}

bool WaveSimulationCoordinator::isRunning() const
{
    return active;
}

WaveSimulationStage WaveSimulationCoordinator::stage() const
{
    return currentStage;
}

void WaveSimulationCoordinator::setStage(
    WaveSimulationStage next,
    const QString& message)
{
    currentStage = next;
    emit stageChanged(next, message);
}

void WaveSimulationCoordinator::handlePreparationFinished()
{
    if (!watcher)
        return;
    prepared = watcher->result();
    watcher->deleteLater();
    watcher = nullptr;
    if (!active)
        return;
    if (cancellation
        && cancellation->load(std::memory_order_relaxed)) {
        finishCancelled();
        return;
    }
    if (!prepared.succeeded()) {
        fail(prepared.message.isEmpty()
                 ? QStringLiteral("Wave Simulation preparation failed (%1).")
                       .arg(WaveSimulationPreparationService::statusCode(
                           prepared.status))
                 : prepared.message);
        return;
    }
    startNextStage();
}

void WaveSimulationCoordinator::startProcess(
    WaveSimulationStage nextStage,
    const QString& program,
    const QStringList& arguments)
{
    clearProcess();
    standardOutput.clear();
    standardError.clear();
    processTerminalHandled = false;
    QProcess* next = new QProcess(this);
    process = next;
    next->setProgram(program);
    next->setArguments(arguments);
    next->setWorkingDirectory(prepared.resultRoot);
    connect(next, &QProcess::readyReadStandardOutput,
            this, [this, next]() {
                appendBounded(&standardOutput,
                              next->readAllStandardOutput());
            });
    connect(next, &QProcess::readyReadStandardError,
            this, [this, next]() {
                appendBounded(&standardError,
                              next->readAllStandardError());
            });
    connect(next,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
                handleProcessFinished(exitCode, static_cast<int>(status));
            });
    connect(next, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                    handleProcessError();
            });
    const QString message = nextStage == WaveSimulationStage::ImportingTarget
        ? QStringLiteral("Creating the WaveWorkbench stimulus project...")
        : nextStage == WaveSimulationStage::ExportingStimulus
            ? QStringLiteral("Exporting the default graphical stimulus...")
            : QStringLiteral("Running Verilator simulation...");
    setStage(nextStage, message);
    next->start();
}

void WaveSimulationCoordinator::handleProcessFinished(
    int exitCode,
    int exitStatus)
{
    if (!active || processTerminalHandled)
        return;
    processTerminalHandled = true;
    if (process) {
        appendBounded(&standardOutput,
                      process->readAllStandardOutput());
        appendBounded(&standardError,
                      process->readAllStandardError());
    }
    if (cancellation
        && cancellation->load(std::memory_order_relaxed)) {
        clearProcess();
        finishCancelled();
        return;
    }
    if (exitStatus != static_cast<int>(QProcess::NormalExit)
        || exitCode != 0) {
        QString detail = QString::fromUtf8(standardError).trimmed();
        if (detail.isEmpty())
            detail = QString::fromUtf8(standardOutput).trimmed();
        clearProcess();
        fail(QStringLiteral("Wave Simulation stage failed%1")
                 .arg(detail.isEmpty()
                          ? QStringLiteral(".")
                          : QStringLiteral(": %1").arg(detail)));
        return;
    }
    clearProcess();
    startNextStage();
}

void WaveSimulationCoordinator::handleProcessError()
{
    if (!active || processTerminalHandled)
        return;
    processTerminalHandled = true;
    const QString detail = process
        ? process->errorString()
        : QStringLiteral("unknown process error");
    clearProcess();
    fail(QStringLiteral("Cannot start WaveWorkbench tool: %1")
             .arg(detail));
}

void WaveSimulationCoordinator::startNextStage()
{
    switch (currentStage) {
    case WaveSimulationStage::Preparing:
        startProcess(
            WaveSimulationStage::ImportingTarget,
            currentRequest.tools.bridge,
            {QStringLiteral("import-module"),
             prepared.manifestPath,
             prepared.stimulusProjectPath});
        return;
    case WaveSimulationStage::ImportingTarget:
        startProcess(
            WaveSimulationStage::ExportingStimulus,
            currentRequest.tools.bridge,
            {QStringLiteral("export-stimulus"),
             prepared.stimulusProjectPath,
             prepared.stimulusPath});
        return;
    case WaveSimulationStage::ExportingStimulus:
        startProcess(
            WaveSimulationStage::Running,
            currentRequest.tools.runner,
            {QStringLiteral("run-module"),
             QStringLiteral("--manifest=") + prepared.manifestPath,
             QStringLiteral("--stimulus=") + prepared.stimulusPath,
             QStringLiteral("--workspace=") + prepared.mirrorWorkspaceRoot,
             QStringLiteral("--artifacts=") + prepared.resultRoot,
             QStringLiteral("--result-project=") + prepared.resultProjectPath});
        return;
    case WaveSimulationStage::Running:
        if (!QFileInfo::exists(prepared.resultProjectPath)) {
            fail(QStringLiteral(
                "The simulation completed without producing a WaveWorkbench result project."));
            return;
        }
        openResultWindow();
        return;
    default:
        return;
    }
}

void WaveSimulationCoordinator::openResultWindow()
{
    setStage(WaveSimulationStage::OpeningResult,
             QStringLiteral("Opening the simulation result in WaveWorkbench..."));
    qint64 processId = 0;
    if (!QProcess::startDetached(
            currentRequest.tools.application,
            {QStringLiteral("--load-first-trace"),
             prepared.resultProjectPath},
            prepared.resultRoot,
            &processId)) {
        fail(QStringLiteral("Cannot open the WaveWorkbench result window."));
        return;
    }
    active = false;
    cancellation.reset();
    setStage(WaveSimulationStage::Complete,
             QStringLiteral("Wave Simulation result opened in WaveWorkbench."));
    emit finished(true,
                  prepared.resultProjectPath,
                  QStringLiteral("Wave Simulation completed."));
}

void WaveSimulationCoordinator::fail(const QString& message)
{
    active = false;
    cancellation.reset();
    setStage(WaveSimulationStage::Failed, message);
    emit finished(false, QString(), message);
}

void WaveSimulationCoordinator::finishCancelled()
{
    active = false;
    cancellation.reset();
    setStage(WaveSimulationStage::Cancelled,
             QStringLiteral("Wave Simulation was cancelled."));
    emit finished(false,
                  QString(),
                  QStringLiteral("Wave Simulation was cancelled."));
}

void WaveSimulationCoordinator::clearProcess()
{
    if (!process)
        return;
    process->disconnect(this);
    process->deleteLater();
    process = nullptr;
}

void WaveSimulationCoordinator::appendBounded(
    QByteArray* destination,
    const QByteArray& content)
{
    if (!destination || content.isEmpty())
        return;
    destination->append(content);
    if (destination->size() > kMaximumProcessOutput) {
        *destination = destination->right(kMaximumProcessOutput);
    }
}
