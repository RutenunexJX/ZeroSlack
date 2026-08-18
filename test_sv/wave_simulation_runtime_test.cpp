#include "wavesimulationcoordinator.h"
#include "wavesimulationpreparationservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <iostream>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool writeFile(const QString& fileName, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(fileName).absolutePath());
    QSaveFile file(fileName);
    return file.open(QIODevice::WriteOnly)
        && file.write(content) == content.size()
        && file.commit();
}

QString optionValue(const QStringList& arguments,
                    const QString& prefix)
{
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix))
            return argument.mid(prefix.size());
    }
    return QString();
}

int runFakeTool(const QString& toolName,
                const QStringList& arguments)
{
    if (toolName == QStringLiteral("wave-bridge")) {
        if (arguments.size() < 4)
            return 2;
        if (arguments.at(1) == QStringLiteral("import-module"))
            return writeFile(arguments.at(3), QByteArrayLiteral("{}")) ? 0 : 3;
        if (arguments.at(1) == QStringLiteral("export-stimulus"))
            return writeFile(arguments.at(3), QByteArrayLiteral("{}")) ? 0 : 3;
        return 2;
    }
    if (toolName == QStringLiteral("wave-sim-runner")) {
        if (qEnvironmentVariableIntValue(
                "ZEROSLACK_FAKE_WAVE_RUNNER_FAILURE") == 1) {
            std::cerr << "forced runner failure\n";
            return 9;
        }
        const QString output = optionValue(
            arguments, QStringLiteral("--result-project="));
        const QString buildCache = optionValue(
            arguments, QStringLiteral("--build-cache="));
        return !output.isEmpty() && !buildCache.isEmpty()
                && writeFile(
                    QDir(buildCache).absoluteFilePath(
                        QStringLiteral("runner-cache.marker")),
                    QByteArrayLiteral("shared-cache"))
                && writeFile(output, QByteArrayLiteral("{}"))
            ? 0
            : 3;
    }
    if (toolName == QStringLiteral("wave-workbench")) {
        if (arguments.size() < 3
            || arguments.at(1) != QStringLiteral("--load-first-trace"))
            return 2;
        return writeFile(
                   QDir(QFileInfo(arguments.at(2)).absolutePath())
                       .absoluteFilePath(QStringLiteral("opened.marker")),
                   QByteArrayLiteral("opened"))
            ? 0
            : 3;
    }
    return -1;
}

QString toolFileName(const QString& baseName)
{
#ifdef Q_OS_WIN
    return baseName + QStringLiteral(".exe");
#else
    return baseName;
#endif
}

bool installFakeTool(const QString& directory,
                     const QString& baseName)
{
    const QString destination = QDir(directory).absoluteFilePath(
        toolFileName(baseName));
    QFile::remove(destination);
    return QFile::copy(
        QCoreApplication::applicationFilePath(), destination);
}

WaveSimulationPreparationRequest fixtureRequest(
    const QString& root,
    const QString& sourcePath,
    const QString& source)
{
    WaveSimulationPreparationRequest request;
    request.project.revision = 1;
    request.project.workspaceRoot = root;
    request.project.allFiles = {sourcePath};
    request.project.systemVerilogFiles = {sourcePath};
    request.project.includeDirs = {root};
    request.project.fileExtensions = {
        QStringLiteral("sv"), QStringLiteral("svh")};
    request.project.topModule = QStringLiteral("top");
    request.target.fileName = sourcePath;
    request.target.moduleName = QStringLiteral("top");
    request.sourceOverrides = {{sourcePath, source, 7}};
    const QString cache = QDir(root).absoluteFilePath(
        QStringLiteral(".wave-cache"));
    request.cachePaths.root = cache;
    request.cachePaths.sourceMirrors = QDir(cache).absoluteFilePath(
        QStringLiteral("source-mirrors"));
    request.cachePaths.buildCache = QDir(cache).absoluteFilePath(
        QStringLiteral("build-cache"));
    request.cachePaths.results = QDir(cache).absoluteFilePath(
        QStringLiteral("results"));
    return request;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const QString toolName = QFileInfo(
        QCoreApplication::applicationFilePath()).baseName();
    const int fakeResult = runFakeTool(
        toolName, QCoreApplication::arguments());
    if (fakeResult >= 0)
        return fakeResult;

    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary runtime fixture is valid");
    if (!temporary.isValid())
        return 1;

    const QString workspace = temporary.filePath(
        QStringLiteral("workspace"));
    const QString sourcePath = QDir(workspace).absoluteFilePath(
        QStringLiteral("rtl/top.sv"));
    const QString diskSource = QStringLiteral(
        "module top(input logic clk, output logic q); assign q = clk; endmodule\n");
    const QString unsavedSource = QStringLiteral(
        "// unsaved-snapshot\n"
        "module top #(parameter int W = 4) (\n"
        "    input logic clk,\n"
        "    input logic rst_n,\n"
        "    input logic [W-1:0] d,\n"
        "    output logic [W-1:0] q\n"
        ");\n"
        "always_ff @(posedge clk) begin\n"
        "    if (!rst_n) q <= '0; else q <= d;\n"
        "end\n"
        "endmodule\n");
    check(writeFile(sourcePath, diskSource.toUtf8()),
          "disk source fixture can be written");

    const WaveSimulationPreparationRequest preparation =
        fixtureRequest(workspace, sourcePath, unsavedSource);
    const QString toolsDirectory = temporary.filePath(
        QStringLiteral("tools"));
    QDir().mkpath(toolsDirectory);
    check(installFakeTool(toolsDirectory, QStringLiteral("wave-bridge"))
              && installFakeTool(toolsDirectory, QStringLiteral("wave-sim-runner"))
              && installFakeTool(toolsDirectory, QStringLiteral("wave-workbench")),
          "independent process fixtures are installed");
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH",
            QFileInfo(QCoreApplication::applicationFilePath())
                    .absolutePath()
                    .toLocal8Bit()
                + QByteArrayLiteral(";") + oldPath);

    WaveSimulationRunRequest run;
    run.preparation = preparation;
    run.tools.bridge = QDir(toolsDirectory).absoluteFilePath(
        toolFileName(QStringLiteral("wave-bridge")));
    run.tools.runner = QDir(toolsDirectory).absoluteFilePath(
        toolFileName(QStringLiteral("wave-sim-runner")));
    run.tools.application = QDir(toolsDirectory).absoluteFilePath(
        toolFileName(QStringLiteral("wave-workbench")));

    WaveSimulationCoordinator coordinator;
    QEventLoop loop;
    bool completed = false;
    bool succeeded = false;
    QString resultProject;
    QString lastStageMessage;
    WaveSimulationStage lastStage = WaveSimulationStage::Idle;
    QObject::connect(
        &coordinator,
        &WaveSimulationCoordinator::stageChanged,
        &loop,
        [&](WaveSimulationStage stage, const QString& message) {
            lastStage = stage;
            lastStageMessage = message;
        });
    QObject::connect(
        &coordinator,
        &WaveSimulationCoordinator::finished,
        &loop,
        [&](bool success, const QString& projectPath, const QString&) {
            completed = true;
            succeeded = success;
            resultProject = projectPath;
            loop.quit();
        });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout,
                     &loop, &QEventLoop::quit);
    QString startFailure;
    const bool started = coordinator.start(run, &startFailure);
    check(started,
          "asynchronous selected-module run starts");
    watchdog.start(60000);
    loop.exec();
    if (!completed) {
        std::cerr << "runtime watchdog at stage "
                  << static_cast<int>(lastStage) << "/"
                  << static_cast<int>(coordinator.stage()) << ": "
                  << lastStageMessage.toStdString() << '\n';
    }
    check(completed && succeeded
              && QFileInfo::exists(resultProject),
          "process chain produces a result project without blocking the event loop");
    check(QFileInfo::exists(
              QDir(preparation.cachePaths.buildCache).absoluteFilePath(
                  QStringLiteral("runner-cache.marker"))),
          "runner did not receive the shared build-cache directory");

    const QString runId = QFileInfo(resultProject).dir().dirName();
    QFile manifest(QDir(QFileInfo(resultProject).absolutePath())
                       .absoluteFilePath(QStringLiteral("module-manifest.json")));
    const bool manifestOpened = manifest.open(QIODevice::ReadOnly);
    const QByteArray manifestText = manifestOpened
        ? manifest.readAll()
        : QByteArray();
    check(manifestOpened
              && manifestText.contains("\"module\": \"top\"")
              && manifestText.contains("\"name\": \"W\""),
          "manifest is derived from the unsaved Slang snapshot");
    QFile mirrored(QDir(preparation.cachePaths.sourceMirrors)
                       .absoluteFilePath(
                           runId + QStringLiteral("/workspace/rtl/top.sv")));
    const bool mirrorOpened = mirrored.open(QIODevice::ReadOnly);
    const QByteArray mirroredText = mirrorOpened
        ? mirrored.readAll()
        : QByteArray();
    check(mirrorOpened
              && mirroredText.contains("unsaved-snapshot")
              && !mirroredText.contains("assign q = clk"),
          "source mirror uses unsaved editor text instead of stale disk text");

    const QString marker = QDir(QFileInfo(resultProject).absolutePath())
        .absoluteFilePath(QStringLiteral("opened.marker"));
    QElapsedTimer markerWait;
    markerWait.start();
    while (!QFileInfo::exists(marker) && markerWait.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    check(QFileInfo::exists(marker),
          "the completed result opens in an independent WaveWorkbench process");

    const QString sourceRoot = qEnvironmentVariable(
        "ZEROSLACK_SOURCE_DIR");
    const QString realWorkspace = QDir(sourceRoot).absoluteFilePath(
        QStringLiteral("test_sv/new"));
    const QString realSource = QDir(realWorkspace).absoluteFilePath(
        QStringLiteral("elec_phy_import/top/rst_gen.v"));
    WaveSimulationPreparationRequest realRequest;
    realRequest.project.revision = 2;
    realRequest.project.workspaceRoot = realWorkspace;
    realRequest.project.allFiles = {realSource};
    realRequest.project.systemVerilogFiles = {realSource};
    realRequest.project.includeDirs = {realWorkspace};
    realRequest.project.fileExtensions = {
        QStringLiteral("v"), QStringLiteral("sv")};
    realRequest.project.topModule = QStringLiteral("rst_gen");
    realRequest.target.fileName = realSource;
    realRequest.target.moduleName = QStringLiteral("rst_gen");
    const QString realCache = temporary.filePath(
        QStringLiteral("real-module-cache"));
    realRequest.cachePaths.root = realCache;
    realRequest.cachePaths.sourceMirrors = QDir(realCache).absoluteFilePath(
        QStringLiteral("source-mirrors"));
    realRequest.cachePaths.buildCache = QDir(realCache).absoluteFilePath(
        QStringLiteral("build-cache"));
    realRequest.cachePaths.results = QDir(realCache).absoluteFilePath(
        QStringLiteral("results"));
    const WaveSimulationPreparationResult realPrepared =
        WaveSimulationPreparationService::prepare(realRequest);
    check(realPrepared.succeeded()
              && realPrepared.manifest.target.module
                     == QStringLiteral("rst_gen")
              && QFileInfo::exists(realPrepared.manifestPath)
              && QFileInfo::exists(
                  QDir(realPrepared.mirrorWorkspaceRoot).absoluteFilePath(
                      QStringLiteral("elec_phy_import/top/rst_gen.v"))),
          "a real test_sv module resolves and materializes without a handwritten testbench");

    qputenv("ZEROSLACK_FAKE_WAVE_RUNNER_FAILURE", QByteArrayLiteral("1"));
    WaveSimulationCoordinator failingCoordinator;
    QEventLoop failureLoop;
    bool failureCompleted = false;
    bool failureSucceeded = true;
    QString failureMessage;
    QObject::connect(
        &failingCoordinator,
        &WaveSimulationCoordinator::finished,
        &failureLoop,
        [&](bool success, const QString&, const QString& message) {
            failureCompleted = true;
            failureSucceeded = success;
            failureMessage = message;
            failureLoop.quit();
        });
    QTimer failureWatchdog;
    failureWatchdog.setSingleShot(true);
    QObject::connect(&failureWatchdog, &QTimer::timeout,
                     &failureLoop, &QEventLoop::quit);
    QString failureStartReason;
    check(failingCoordinator.start(run, &failureStartReason),
          "failure-path run starts asynchronously");
    failureWatchdog.start(60000);
    failureLoop.exec();
    qunsetenv("ZEROSLACK_FAKE_WAVE_RUNNER_FAILURE");
    check(failureCompleted && !failureSucceeded
              && failingCoordinator.stage()
                     == WaveSimulationStage::Failed
              && failureMessage.contains(
                  QStringLiteral("forced runner failure")),
          "runner failure reaches an explicit terminal state with diagnostics");

    std::cout << "wave simulation runtime checks: "
              << checks << ", failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
