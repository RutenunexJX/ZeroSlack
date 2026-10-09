#include "../../src/integrations/simdock/simdockcontextprovider.h"
#include "../../src/integrations/simdock/simdockcontextview.h"
#include "../../src/simulation/simdock/ui/workbench.h"
#include "../../src/simulation/simdock/core/questasession.h"
#include "../../src/simulation/simdock/ui/uistyle.h"
#include <ElaApplication.h>
#include "applicationthememanager.h"
#include <ElaListView.h>
#include <ElaPlainTextEdit.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtTest>
#include <algorithm>

class Events final : public QObject {
public:
    QWidget* root = nullptr;
    int paints = 0, layouts = 0;
    bool eventFilter(QObject* object, QEvent* event) override {
        const auto* widget = qobject_cast<QWidget*>(object);
        if (widget && root && (widget == root || root->isAncestorOf(widget))) {
            if (event->type() == QEvent::Paint) ++paints;
            if (event->type() == QEvent::LayoutRequest) ++layouts;
        }
        return false;
    }
    void reset() { paints = layouts = 0; }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    QTemporaryDir settings, workspace;
    QCoreApplication::setOrganizationName(QStringLiteral("SimDockTests"));
    QCoreApplication::setApplicationName(QStringLiteral("Performance"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Ela);
    ApplicationThemeManager::instance().applyToApplication();
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", settings.filePath("legacy.ini").toUtf8());
    simdock::Ui::initialize();
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    constexpr int count = 1500;
    for (int i = 0; i < count; ++i) {
        QFile file(workspace.filePath(QStringLiteral("source_%1.sv").arg(i, 4, 10, QLatin1Char('0'))));
        if (!file.open(QIODevice::WriteOnly)) return 3;
        file.write(QStringLiteral("module source_%1; endmodule\n").arg(i).toUtf8());
    }
    QWidget window;
    QVBoxLayout layout(&window);
    layout.setContentsMargins(0, 0, 0, 0);
    SimDockContextProvider provider;
    auto* host = qobject_cast<SimDockContextView*>(provider.createView(provider.activationResource({}), &window));
    if (!host || !host->isReady()) return 10;
    layout.addWidget(host);
    auto* panel = host->workbench();
    window.resize(1200, 780);
    window.show();
    QSignalSpy scanned(panel, &simdock::Workbench::scanFinished);
    QElapsedTimer timer;
    timer.start();
    if (!provider.activateView(host, provider.activationResource(workspace.path())) || !host->isReady()) return 11;
    if (scanned.isEmpty() && !scanned.wait(30000)) return 4;
    const double scanMs = timer.nsecsElapsed() / 1e6;
    if (!panel->createProject(QStringLiteral("First")) || !panel->createProject(QStringLiteral("Second"))) return 5;
    auto* projects = window.findChild<ElaListView*>(QStringLiteral("projectList"));
    auto* files = window.findChild<ElaListView*>(QStringLiteral("sourceList"));
    auto* log = window.findChild<ElaPlainTextEdit*>(QStringLiteral("simulationLog"));
    auto* session = window.findChild<simdock::QuestaSession*>();
    if (!projects || !files || !log || !session || files->model()->rowCount() != count) return 6;
    QTest::qWait(100);
    Events events;
    events.root = &window;
    app.installEventFilter(&events);
    QJsonObject report{{"platform", QGuiApplication::platformName()}, {"scale", window.devicePixelRatioF()},
        {"uiFont", QFontInfo(simdock::Ui::font()).family()}, {"codeFont", QFontInfo(simdock::Ui::codeFont()).family()},
        {"width", window.width()}, {"height", window.height()}, {"files", count},
        {"entry", "SimDockContextProvider, owned Workbench, isolated QWidget measurement shell"},
        {"scanMs", scanMs}, {"measurement", "CPU elapsed and Qt paint/layout events on the reported Qt platform, not display FPS"}};
    QList<double> timings;
    for (int i = 0; i < 12; ++i) {
        timer.restart();
        projects->setCurrentIndex(projects->model()->index(i % 2, 0));
        app.processEvents();
        timings << timer.nsecsElapsed() / 1e6;
    }
    std::sort(timings.begin(), timings.end());
    report.insert("projectSwitch", QJsonObject{{"medianMs", timings[6]}, {"p95Ms", timings[11]},
        {"paints", events.paints}, {"layouts", events.layouts}, {"iterations", 12}});
    events.reset();
    timings.clear();
    auto* source = qobject_cast<QStandardItemModel*>(files->model())->item(100);
    for (int i = 0; i < 12; ++i) {
        timer.restart();
        const auto state = i % 2 ? Qt::Unchecked : Qt::Checked;
        source->setCheckState(state);
        app.processEvents();
        if (source->checkState() != state) return 9;
        timings << timer.nsecsElapsed() / 1e6;
    }
    std::sort(timings.begin(), timings.end());
    report.insert("sourceToggle", QJsonObject{{"medianMs", timings[6]}, {"p95Ms", timings[11]},
        {"paints", events.paints}, {"layouts", events.layouts}, {"iterations", 12}});
    events.reset();
    log->clear();
    QString batch;
    for (int i = 0; i < 100; ++i) batch += QStringLiteral("Simulation transcript line %1: compile and load complete.\n").arg(i);
    timer.restart();
    for (int i = 0; i < 50; ++i)
        QMetaObject::invokeMethod(session, "logText", Qt::DirectConnection, Q_ARG(QString, batch));
    const double dispatchMs = timer.nsecsElapsed() / 1e6;
    while (log->document()->blockCount() < 5000 && timer.elapsed() < 5000) QTest::qWait(1);
    app.processEvents();
    report.insert("logBurst", QJsonObject{{"dispatchMs", dispatchMs}, {"settledMs", timer.nsecsElapsed() / 1e6},
        {"paints", events.paints}, {"layouts", events.layouts}, {"lines", log->document()->blockCount()}});
    QFile result(QString::fromLocal8Bit(argv[1]));
    if (!result.open(QIODevice::WriteOnly)) return 7;
    result.write(QJsonDocument(report).toJson());
    app.removeEventFilter(&events);
    window.close();
    return log->document()->blockCount() >= 5000 ? 0 : 8;
}
