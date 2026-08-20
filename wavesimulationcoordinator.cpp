#include "wavesimulationcoordinator.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    next->setProcessEnvironment(
        currentRequest.tools.processEnvironment());
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
        QString detail;
        if (currentStage == WaveSimulationStage::Running)
            detail = publishRunnerDiagnostics();
        if (detail.isEmpty())
            detail = QString::fromUtf8(standardError).trimmed();
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
        if (QFileInfo::exists(prepared.defaultScenarioPath)) {
            startProcess(
                WaveSimulationStage::ImportingTarget,
                currentRequest.tools.bridge,
                {QStringLiteral("import-stimulus"),
                 prepared.manifestPath,
                 prepared.defaultScenarioPath,
                 prepared.stimulusProjectPath});
        } else {
            startProcess(
                WaveSimulationStage::ImportingTarget,
                currentRequest.tools.bridge,
                {QStringLiteral("import-module"),
                 prepared.manifestPath,
                 prepared.stimulusProjectPath});
        }
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
    {
        QStringList arguments{
            QStringLiteral("run-module"),
            QStringLiteral("--manifest=") + prepared.manifestPath,
            QStringLiteral("--stimulus=") + prepared.stimulusPath,
            QStringLiteral("--workspace=") + prepared.mirrorWorkspaceRoot,
            QStringLiteral("--artifacts=") + prepared.resultRoot,
            QStringLiteral("--build-cache=")
                + currentRequest.preparation.cachePaths.buildCache,
            QStringLiteral("--scenario-directory=")
                + prepared.scenarioDirectory,
            QStringLiteral("--result-project=")
                + prepared.resultProjectPath,
        };
        if (!currentRequest.tools.verilator.trimmed().isEmpty()) {
            arguments.append(
                QStringLiteral("--verilator=")
                + currentRequest.tools.verilator);
        }
        if (!currentRequest.tools.cxxCompiler.trimmed().isEmpty()) {
            arguments.append(
                QStringLiteral("--cxx=")
                + currentRequest.tools.cxxCompiler);
        }
        startProcess(
            WaveSimulationStage::Running,
            currentRequest.tools.runner,
            arguments);
        return;
    }
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
             QStringLiteral("Opening the simulation result in a Wave tab..."));
    emit resultReady(prepared.resultProjectPath,
                     currentRequest.tools.widgetLibrary,
                     currentRequest.tools.application);
    active = false;
    cancellation.reset();
    setStage(WaveSimulationStage::Complete,
             QStringLiteral("Wave Simulation result is ready."));
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

QString WaveSimulationCoordinator::publishRunnerDiagnostics()
{
    const QByteArray trimmed = standardOutput.trimmed();
    QJsonDocument document = QJsonDocument::fromJson(trimmed);
    if (!document.isObject()) {
        const QList<QByteArray> lines = trimmed.split('\n');
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            document = QJsonDocument::fromJson(it->trimmed());
            if (document.isObject())
                break;
        }
    }
    if (!document.isObject())
        return {};

    const QJsonObject report = document.object();
    const QString workspaceRoot = QFileInfo(
        currentRequest.preparation.project.workspaceRoot)
                                      .absoluteFilePath();
    const QDir workspace(workspaceRoot);
    const QJsonArray diagnostics = report.value(
        QStringLiteral("diagnostics")).toArray();
    for (const QJsonValue& value : diagnostics) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString portableSource = QDir::cleanPath(
            QDir::fromNativeSeparators(
                object.value(QStringLiteral("sourceFile")).toString()));
        if (portableSource.isEmpty()
            || QDir::isAbsolutePath(portableSource)
            || portableSource == QStringLiteral("..")
            || portableSource.startsWith(QStringLiteral("../"))) {
            continue;
        }
        WaveSimulationDiagnostic diagnostic;
        diagnostic.sourceFile = QFileInfo(
            workspace.filePath(portableSource)).absoluteFilePath();
        diagnostic.line = object.value(QStringLiteral("line")).toInt();
        diagnostic.column = object.value(QStringLiteral("column")).toInt();
        diagnostic.severity = object.value(
            QStringLiteral("severity")).toString();
        diagnostic.stage = object.value(
            QStringLiteral("stage")).toString();
        diagnostic.code = object.value(
            QStringLiteral("code")).toString();
        diagnostic.message = object.value(
            QStringLiteral("message")).toString();
        if (diagnostic.isValid())
            emit diagnosticAvailable(diagnostic);
    }
    const QJsonObject tools = report.value(QStringLiteral("toolchain"))
                                  .toObject()
                                  .value(QStringLiteral("tools"))
                                  .toObject();
    QStringList toolchainFailures;
    const auto appendToolFailure =
        [&tools, &toolchainFailures](const QString& key,
                                     const QString& label) {
            const QJsonObject tool = tools.value(key).toObject();
            if (tool.isEmpty()
                || tool.value(QStringLiteral("status")).toString()
                       == QStringLiteral("ready")) {
                return;
            }
            const QString diagnostic = tool.value(
                QStringLiteral("diagnostic")).toString().trimmed();
            if (!diagnostic.isEmpty()) {
                toolchainFailures.append(
                    QStringLiteral("%1: %2").arg(label, diagnostic));
            }
        };
    appendToolFailure(QStringLiteral("verilator"),
                      QStringLiteral("Verilator"));
    appendToolFailure(QStringLiteral("cxx"),
                      QStringLiteral("C++ compiler"));
    if (!toolchainFailures.isEmpty()) {
        toolchainFailures.prepend(
            QStringLiteral("Simulation toolchain is unavailable."));
        toolchainFailures.append(
            QStringLiteral(
                "Configure executable paths in Settings > Simulation, or "
                "install them on PATH."));
        return toolchainFailures.join(QLatin1Char('\n'));
    }

    return report.value(QStringLiteral("diagnostic")).toString().trimmed();
}
