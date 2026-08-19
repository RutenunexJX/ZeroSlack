#include "waveembeddedworkspaceloader.h"
#include "tabmanager.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <iostream>

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "fixture library path is required\n";
        return 2;
    }

    QTemporaryDir temporary;
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
        QString::fromLocal8Bit(argv[1]),
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
    const QStringList capabilities = workspace->property(
        "wavewidgets.capabilities").toStringList();
    auto* asyncTiming = workspace->findChild<QAction*>(
        QStringLiteral("AsyncTimingAction"));
    auto* clockDomains = workspace->findChild<QToolButton*>(
        QStringLiteral("SimulationClockDomainsButton"));
    auto* compare = workspace->findChild<QAction*>(
        QStringLiteral("RunSimulationCompareAction"));
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
            || !workspace->findChild<QWidget*>(
                QStringLiteral("SimulationComparisonPanel"))
            || !workspace->findChild<QTableWidget*>(
                QStringLiteral("CompareResultTable"))
            || !asyncTiming || !clockDomains || !clockDomains->menu()
            || !compare)) {
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
