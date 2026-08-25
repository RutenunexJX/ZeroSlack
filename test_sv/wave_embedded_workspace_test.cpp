#include "waveformpreviewloader.h"
#include "wavepreviewpayloadadapter.h"
#if !defined(ZEROSLACK_WAVEFORM_PREVIEW_STANDALONE)
#include "waveembeddedworkspaceloader.h"
#include "tabmanager.h"
#endif

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <iostream>

class WaveSourceNavigationProbe final
{
public:
    void record(const QString& file,
                int line,
                int column,
                const QString& semantic,
                const QString& lane)
    {
        sourceFile = file;
        sourceLine = line;
        sourceColumn = column;
        semanticId = semantic;
        laneId = lane;
    }

    QString sourceFile;
    int sourceLine = 0;
    int sourceColumn = 0;
    QString semanticId;
    QString laneId;
};

namespace {
bool exerciseWaveformPreviewAbi(const QString& libraryPath,
                                const QString& temporaryRoot,
                                QWidget* owner)
{
    const auto reject = [](const char* message) {
        std::cerr << message << '\n';
        return false;
    };

    WaveformPreviewLoader loader;
    QString failure;
    QWidget* view = loader.createView(
        libraryPath, owner, &failure);
    if (!view) {
        std::cerr << failure.toStdString() << '\n';
        return false;
    }
    const QStringList capabilities =
        view->property("wavewidgets.capabilities").toStringList();
    if (view->property("wavewidgets.abiVersion").toInt() != 1
        || view->property("wavewidgets.contract").toString()
            != QStringLiteral("wave-workbench.waveform-view/v1")
        || view->property("wavewidgets.previewContract").toString()
            != QStringLiteral("wave-preview/v1")
        || !capabilities.contains(QStringLiteral("wave-preview/v1"))
        || !capabilities.contains(QStringLiteral("generation-replace/v1"))
        || !capabilities.contains(QStringLiteral("waveform-theme/v1"))
        || !capabilities.contains(QStringLiteral("compact-density/v1"))
        || !capabilities.contains(QStringLiteral("source-navigation/v1"))) {
        delete view;
        return reject("waveform ABI, contract, or capabilities are missing");
    }

    const QString sourceDirectory =
        QDir(temporaryRoot).filePath(QStringLiteral("rtl"));
    if (!QDir().mkpath(sourceDirectory)) {
        delete view;
        return reject("cannot create waveform source fixture directory");
    }
    const QString sourcePath =
        QDir(sourceDirectory).filePath(QStringLiteral("test.sv"));
    QFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly)
        || source.write("module test; endmodule\n") <= 0) {
        delete view;
        return reject("cannot create waveform source fixture");
    }
    source.close();

    WavePreviewReport report;
    report.available = true;
    report.scopeLabel = QStringLiteral("test");
    report.trace.available = true;
    report.trace.cycleCount = 4;
    report.trace.traceSignals = {
        WavePreviewTraceSignal{
            QStringLiteral("clk"),
            1,
            {QStringLiteral("0"), QStringLiteral("1"),
             QStringLiteral("0"), QStringLiteral("1"),
             QStringLiteral("0")},
            true},
        WavePreviewTraceSignal{
            QStringLiteral("data"),
            8,
            {QStringLiteral("8'h00"), QStringLiteral("8'h11"),
             QStringLiteral("8'h11"), QStringLiteral("X"),
             QStringLiteral("X")},
            false}};
    report.signalContexts = {
        WavePreviewSignalContext{
            QStringLiteral("clk"), QStringLiteral("input"),
            QStringLiteral("logic"), QStringLiteral("input logic clk"),
            7, 3},
        WavePreviewSignalContext{
            QStringLiteral("data"), QStringLiteral("output"),
            QStringLiteral("logic [7:0]"),
            QStringLiteral("output logic [7:0] data"), 12, 5}};

    const WavePreviewPayloadBuildResult first =
        WavePreviewPayloadAdapter::buildSymbolic(
            report, sourcePath, temporaryRoot);
    if (!first.ok()) {
        std::cerr << first.error.toStdString() << '\n';
        delete view;
        return false;
    }
    const QJsonObject firstRoot =
        QJsonDocument::fromJson(first.payload).object();
    const QJsonArray lanes = firstRoot.value(QStringLiteral("lanes")).toArray();
    if (firstRoot.value(QStringLiteral("mode")).toString()
            != QStringLiteral("symbolic")
        || lanes.size() != 2) {
        delete view;
        return reject("adapter did not publish a strict symbolic payload");
    }
    QString sourceLaneId;
    for (const QJsonValue& laneValue : lanes) {
        const QJsonObject lane = laneValue.toObject();
        if (lane.value(QStringLiteral("provenance")).toString()
                != QStringLiteral("zeroslack-symbolic")
            || !lane.value(QStringLiteral("source")).isObject()
            || lane.value(QStringLiteral("source")).toObject()
                   .value(QStringLiteral("file")).toString()
                != QStringLiteral("rtl/test.sv")) {
            delete view;
            return reject("symbolic provenance or portable source is missing");
        }
        if (sourceLaneId.isEmpty())
            sourceLaneId = lane.value(QStringLiteral("id")).toString();
    }

    if (!loader.replacePreview(view, first.payload, &failure)
        || view->property("previewGeneration").toULongLong()
            != first.generation
        || view->property("previewMode").toString()
            != QStringLiteral("symbolic")
        || view->property("presentationState").toString()
            != QStringLiteral("ready")) {
        std::cerr << failure.toStdString() << '\n';
        delete view;
        return false;
    }

    if (!loader.setTheme(view, QStringLiteral("dark"), &failure)
        || view->property("themeName").toString()
            != QStringLiteral("dark")
        || !loader.setCompact(view, true, &failure)
        || !view->property("compact").toBool()
        || !loader.setPresentationState(
            view,
            QStringLiteral("loading"),
            QStringLiteral("Updating Symbolic Preview"),
            &failure)
        || view->property("presentationState").toString()
            != QStringLiteral("loading")
        || !loader.setPresentationState(
            view, QStringLiteral("ready"), QString(), &failure)) {
        std::cerr << failure.toStdString() << '\n';
        delete view;
        return false;
    }

    WaveSourceNavigationProbe navigation;
    loader.setSourceNavigationHandler(
        [&navigation](const QString& file,
                      int line,
                      int column,
                      const QString& semantic,
                      const QString& lane) {
            navigation.record(file, line, column, semantic, lane);
        });
    bool selected = false;
    if (!QMetaObject::invokeMethod(
            view,
            "selectLane",
            Qt::DirectConnection,
            Q_RETURN_ARG(bool, selected),
            Q_ARG(QString, sourceLaneId))
        || !selected) {
        delete view;
        return reject("waveform source lane could not be selected");
    }
    QKeyEvent navigateEvent(
        QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(view, &navigateEvent);
    if (navigation.sourceFile != QStringLiteral("rtl/test.sv")
        || navigation.sourceLine != 7 || navigation.sourceColumn != 3
        || navigation.laneId != sourceLaneId) {
        delete view;
        return reject("waveform source-navigation round trip failed");
    }

    report.trace.traceSignals[1].values[0] = QStringLiteral("8'hA5");
    const WavePreviewPayloadBuildResult newer =
        WavePreviewPayloadAdapter::buildSymbolic(
            report,
            sourcePath,
            temporaryRoot,
            first.generation + 1);
    if (!newer.ok()
        || !loader.replacePreview(view, newer.payload, &failure)
        || view->property("previewGeneration").toULongLong()
            != newer.generation) {
        std::cerr << failure.toStdString() << '\n';
        delete view;
        return false;
    }
    const quint64 followingGeneration =
        WavePreviewPayloadAdapter::nextGeneration();
    if (followingGeneration <= newer.generation) {
        delete view;
        return reject("waveform generations are not globally monotonic");
    }
    if (loader.replacePreview(view, first.payload, &failure)
        || view->property("previewGeneration").toULongLong()
            != newer.generation
        || !failure.contains(QStringLiteral("stale"),
                             Qt::CaseInsensitive)) {
        delete view;
        return reject("stale payload replaced the last valid waveform");
    }

    QJsonObject malformedRoot =
        QJsonDocument::fromJson(newer.payload).object();
    malformedRoot.insert(
        QStringLiteral("generation"),
        static_cast<double>(newer.generation + 1));
    malformedRoot.insert(QStringLiteral("unknownRootField"), true);
    const QByteArray malformed =
        QJsonDocument(malformedRoot).toJson(QJsonDocument::Compact);
    if (loader.replacePreview(view, malformed, &failure)
        || view->property("previewGeneration").toULongLong()
            != newer.generation) {
        delete view;
        return reject("malformed payload replaced the last valid waveform");
    }

    QPointer<QWidget> primaryGuard(view);
    delete view;
    if (primaryGuard)
        return reject("waveform view did not support direct host destruction");

    auto* lifecycleOwner = new QWidget;
    QPointer<QWidget> lifecycleView(loader.createView(
        libraryPath, lifecycleOwner, &failure));
    if (!lifecycleView) {
        std::cerr << failure.toStdString() << '\n';
        delete lifecycleOwner;
        return false;
    }
    delete lifecycleOwner;
    if (lifecycleView)
        return reject("waveform child survived its host destruction");

    return true;
}
}

#if defined(ZEROSLACK_WAVEFORM_PREVIEW_STANDALONE)
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "wavewidgets library path is required\n";
        return 2;
    }
    QTemporaryDir temporary;
    QWidget owner;
    if (!temporary.isValid()
        || !exerciseWaveformPreviewAbi(
            QString::fromLocal8Bit(argv[1]),
            temporary.path(),
            &owner)) {
        return 1;
    }
    std::cout << "waveform-view/v1 standalone round trip passed\n";
    return 0;
}
#else
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "fixture library path is required\n";
        return 2;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "cannot create waveform test directory\n";
        return 2;
    }
    const QString libraryPath = QString::fromLocal8Bit(argv[1]);
    if (argc >= 3
        && QString::fromLocal8Bit(argv[2])
            == QStringLiteral("--waveform-only")) {
        QWidget waveformOwner;
        if (!exerciseWaveformPreviewAbi(
                libraryPath, temporary.path(), &waveformOwner)) {
            return 1;
        }
        std::cout << "waveform-view/v1 round trip passed\n";
        return 0;
    }
    const bool realWorkspace = argc >= 3;
    const QString projectPath = realWorkspace
        ? QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath()
        : temporary.filePath(QStringLiteral("result.wave.json"));
    if (!realWorkspace) {
        QFile project(projectPath);
        if (!temporary.isValid() || !project.open(QIODevice::WriteOnly)
            || project.write("{}") != 2) {
            std::cerr << "cannot create result fixture\n";
            return 2;
        }
        project.close();
    }

    QWidget owner;
    WaveEmbeddedWorkspaceLoader loader;
    QString failure;
    QWidget* workspace = loader.createWorkspace(
        libraryPath,
        projectPath,
        &owner,
        &failure);
    if (!workspace
        || (!realWorkspace
            && workspace->objectName()
                != QStringLiteral("FixtureWaveWorkspace"))
        || workspace->parentWidget() != &owner
        || loader.loadedLibraryPath().isEmpty()) {
        std::cerr << failure.toStdString() << '\n';
        return 1;
    }
    if (!exerciseWaveformPreviewAbi(
            libraryPath,
            temporary.path(),
            &owner)) {
        return 1;
    }
    const QStringList capabilities = workspace->property(
        "wavewidgets.capabilities").toStringList();
    if (!capabilities.contains(QStringLiteral("result-source-navigation/v1"))
        || workspace->metaObject()->indexOfMethod(
               "canRevealSourceObject(QString,QString,int,int,QString,QString)") < 0
        || workspace->metaObject()->indexOfMethod(
               "revealSourceObject(QString,QString,int,int,QString,QString)") < 0
        || workspace->metaObject()->indexOfSignal(
               "sourceNavigationRequested(QString,int,int,QString,QString)") < 0) {
        std::cerr << "source navigation workspace contract is missing\n";
        return 1;
    }
    auto* asyncTiming = workspace->findChild<QAction*>(
        QStringLiteral("AsyncTimingAction"));
    auto* clockDomains = workspace->findChild<QToolButton*>(
        QStringLiteral("SimulationClockDomainsButton"));
    auto* stubDependencies = workspace->findChild<QToolButton*>(
        QStringLiteral("SimulationStubDependenciesButton"));
    auto* compare = workspace->findChild<QAction*>(
        QStringLiteral("RunSimulationCompareAction"));
    auto* runChecks = workspace->findChild<QAction*>(
        QStringLiteral("RunSimulationChecksAction"));
    auto* runAllScenarios = workspace->findChild<QAction*>(
        QStringLiteral("RunAllSimulationScenariosAction"));
    auto* batchResults = workspace->findChild<QTableWidget*>(
        QStringLiteral("SimulationBatchResultTable"));
    if (realWorkspace
        && (!workspace->findChild<QWidget*>(
                QStringLiteral("StimulusCanvas"))
            || !workspace->findChild<QWidget*>(
                QStringLiteral("ActualTraceCanvas"))
            || !workspace->findChild<QWidget*>(
                QStringLiteral("TraceSignalBrowser"))
            || !capabilities.contains(QStringLiteral(
                "internal-signal-hierarchy/v1"))
            || !capabilities.contains(QStringLiteral(
                "multi-clock-async-events/v1"))
            || !capabilities.contains(QStringLiteral(
                "expected-actual-compare/v1"))
            || !capabilities.contains(QStringLiteral(
                "lightweight-trace-checks/v1"))
            || !capabilities.contains(QStringLiteral(
                "result-source-navigation/v1"))
            || !capabilities.contains(QStringLiteral(
                "explicit-unresolved-module-stubs/v1"))
            || !capabilities.contains(QStringLiteral(
                "multi-scenario-batch-run/v1"))
            || !capabilities.contains(QStringLiteral(
                "on-demand-fst-trace/v1"))
            || !workspace->findChild<QWidget*>(
                QStringLiteral("SimulationComparisonPanel"))
            || !workspace->findChild<QTableWidget*>(
                QStringLiteral("CompareResultTable"))
            || !workspace->findChild<QTableWidget*>(
                QStringLiteral("SimulationCheckResultTable"))
            || !workspace->findChild<QAction*>(
                QStringLiteral("SimulationSourceNavigationAction"))
            || !workspace->findChild<QToolButton*>(
                QStringLiteral("SimulationDriverNavigationButton"))
            || !asyncTiming || !clockDomains || !clockDomains->menu()
            || !stubDependencies || !stubDependencies->menu()
            || !compare || !runChecks || !runAllScenarios
            || !batchResults)) {
        std::cerr << "real shared wave workspace capabilities are missing\n";
        return 1;
    }

    QTabWidget tabs;
    TabManager tabManager(&tabs);
    bool toolPageClosed = false;
    QObject::connect(
        &tabManager,
        &TabManager::toolPageClosed,
        [&toolPageClosed](const QString& stableId) {
            toolPageClosed = stableId == QStringLiteral("wave:test");
        });
    QWidget* opened = tabManager.openToolPage(
        workspace,
        QStringLiteral("wave:test"),
        QStringLiteral("Wave Test"));
    if (opened != workspace
        || tabManager.toolPage(QStringLiteral("wave:test")) != workspace
        || tabManager.getCurrentEditor() != nullptr
        || !tabManager.activateToolPage(QStringLiteral("wave:test"))) {
        std::cerr << "tool page was not hosted as a first-class non-editor tab\n";
        return 1;
    }
    if (realWorkspace && argc >= 4) {
        tabs.resize(1440, 900);
        tabs.show();
        QEventLoop traceLoad;
        QObject::connect(
            workspace,
            SIGNAL(initialTraceReferenceLoaded(bool,QString)),
            &traceLoad,
            SLOT(quit()));
        QTimer::singleShot(5000, &traceLoad, &QEventLoop::quit);
        traceLoad.exec();
        application.processEvents();
        if (!tabs.grab().save(QString::fromLocal8Bit(argv[3]))) {
            std::cerr << "embedded workspace screenshot could not be saved\n";
            return 1;
        }
    }
    tabManager.closeTab(tabs.indexOf(workspace));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!toolPageClosed
        || tabManager.toolPage(QStringLiteral("wave:test"))) {
        std::cerr << "tool page close lifecycle failed\n";
        return 1;
    }

    QString missingFailure;
    if (loader.createWorkspace(
            QString::fromLocal8Bit(argv[1]),
            temporary.filePath(QStringLiteral("missing.wave.json")),
            &owner,
            &missingFailure)
        || !missingFailure.contains(QStringLiteral("missing"),
                                    Qt::CaseInsensitive)) {
        std::cerr << "missing result project was not rejected\n";
        return 1;
    }

    std::cout << "embedded Wave workspace loader passed\n";
    return 0;
}
#endif
