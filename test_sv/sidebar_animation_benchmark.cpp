#include "applicationthememanager.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "navigationwidget.h"
#include "navigationpanecoordinator.h"
#include "tabmanager.h"
#include "panelcompositor.h"
#include "panellayoutcontroller.h"
#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "liveinsightscontextprovider.h"
#include "ElaNavigationBar.h"
#include "ElaDrawerArea.h"
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cstdio>

struct EventCost {
    int count = 0;
    qint64 totalNs = 0;
    qint64 maximumNs = 0;
};

class MeasuredApplication final : public QApplication {
public:
    using QApplication::QApplication;
    bool measuring = false;
    bool nativeNavigation = false;
    QMap<QString, EventCost> costs;
    QVector<double> intervals;
    QPointer<QDockWidget> dock;
    QPointer<PanelCompositor> compositor;
    QPointer<ElaDrawerArea> drawer;
    QElapsedTimer frames;
    qint64 previousFrame = -1;

    bool notify(QObject* receiver, QEvent* event) override {
        const auto type = event->type();
        if (!measuring || (type != QEvent::Paint && type != QEvent::Resize
            && type != QEvent::LayoutRequest && type != QEvent::UpdateRequest))
            return QApplication::notify(receiver, event);
        const QString owner = receiver->parent() && receiver->parent()->inherits("MyCodeEditor")
            ? QString("Editor/%1").arg(receiver->objectName())
            : QString("%1/%2").arg(receiver->metaObject()->className(), receiver->objectName());
        const QString key = QString::number(type) + ":" + owner;
        if ((!nativeNavigation && compositor && receiver == compositor && type == QEvent::Paint)
            || (!nativeNavigation && drawer && type == QEvent::Paint && receiver->parent() == drawer
                && receiver->inherits("ElaDrawerContainer"))
            || (nativeNavigation && receiver == dock && type == QEvent::Resize)) {
            const auto now = frames.nsecsElapsed();
            if (previousFrame >= 0) intervals.append((now - previousFrame) / 1e6);
            previousFrame = now;
        }
        QElapsedTimer timer; timer.start();
        const bool handled = QApplication::notify(receiver, event);
        const auto elapsed = timer.nsecsElapsed();
        auto& cost = costs[key];
        ++cost.count; cost.totalNs += elapsed; cost.maximumNs = qMax(cost.maximumNs, elapsed);
        return handled;
    }
};

int main(int argc, char** argv) {
    MeasuredApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (argc > 2) return 2;
    const QString outputPath = argc == 2 ? QString::fromLocal8Bit(argv[1])
        : QCoreApplication::applicationDirPath() + "/panel-native-benchmark.json";
    QTemporaryDir profile; if (!profile.isValid()) return 3;
    QCoreApplication::setApplicationName("ZeroSlack-Panel-Benchmark");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    auto& theme = ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 4;
    theme.applyToApplication();
    QString source("module sidebar_benchmark;\n");
    for (int i = 0; i < 5000; ++i)
        source += QString("  logic [31:0] signal_%1; // a representative source line for viewport resize and syntax painting\n").arg(i);
    source += "endmodule\n";
    const auto path = profile.filePath("sidebar_benchmark.sv");
    QFile fixture(path); if (!fixture.open(QIODevice::WriteOnly)) return 5;
    fixture.write(source.toUtf8()); fixture.close();
    MainWindow host;
    if (!host.tabManager->openFileInTab(path)) return 6;
    auto* editor = host.tabManager->getCurrentEditor();
    auto* bar = host.findChild<ElaNavigationBar*>("navigationElaBar");
    auto* navigationPane = host.findChild<NavigationPaneCoordinator*>();
    app.dock = host.findChild<QDockWidget*>("navigationDock");
    app.compositor = host.findChild<PanelCompositor*>();
    if (!editor || !bar || !navigationPane || !app.dock) return 7;
    auto* context = host.findChild<ContextWorkspaceController*>();
    PanelLayoutController* drawer = nullptr;
    for (auto* child : host.children())
        if (auto* candidate = dynamic_cast<PanelLayoutController*>(child)) drawer = candidate;
    if (!context || !drawer) return 11;
    const auto resource = LiveInsightsContextProvider::resourceForKind(LiveInsightKind::Module, profile.path());
    if (!context->openResource(resource, {ContextSurface::Docked, ContextPersistence::Kept, ContextBinding::Global})) return 12;
    auto* navigation = host.findChild<NavigationWidget*>();
    navigation->setWorkspaceRoot(profile.path()); navigation->updateFileHierarchy({path});
    host.show(); QTest::qWait(1500);
    QJsonArray scenarios;
    const bool nativeRun = QApplication::platformName() == "windows";
    const QList<QSize> sizes = nativeRun
        ? QList<QSize>{QSize(1000, 700), host.screen()->availableGeometry().size()}
        : QList<QSize>{QSize(1000, 700), QSize(2560, 1392), QSize(3840, 2160)};
    for (QSize size : sizes) {
        if (nativeRun && size == sizes.last()) host.showMaximized();
        else host.resize(size);
        QTest::qWait(100);
        for (const auto& scene : {QString("left"), QString("right"), QString("bottom"), QString("section")}) {
        if (app.compositor) app.compositor->settle();
        for (auto* item : host.findChildren<ElaDrawerArea*>()) item->finishDrawerAnimation();
        navigationPane->setExpanded(true, false);
        drawer->setBottomCollapsed(true);
        context->setDockVisible(scene == "section");
        if (app.compositor) app.compositor->settle();
        for (auto* item : host.findChildren<ElaDrawerArea*>()) item->finishDrawerAnimation();
        context->dockHost()->setSectionCollapsed(resource.stableKey(), false, false);
        QTest::qWait(100);
        app.costs.clear(); app.intervals.clear(); editor->resetHotPathMetricsForTest();
        app.nativeNavigation = scene == "left";
        app.drawer = scene == "right" ? host.findChild<ElaDrawerArea*>("contextSidebarDrawer")
            : scene == "bottom" ? host.findChild<ElaDrawerArea*>("bottomPanelDrawer")
            : scene == "section" ? context->dockHost()->sectionWidget(resource.stableKey())->findChild<ElaDrawerArea*>("contextSectionDrawer") : nullptr;
        QJsonArray durations;
        QJsonArray dispatchTimes;
        QJsonArray renderers;
        QJsonArray snapshotBytes;
        QJsonArray preparationMs;
        for (int i = 0; i < 12; ++i) {
            app.previousFrame = -1; app.frames.start(); app.measuring = true;
            QElapsedTimer duration; duration.start();
            QEventLoop motion;
            QTimer deadline;
            deadline.setSingleShot(true);
            QObject::connect(&deadline, &QTimer::timeout, &motion, &QEventLoop::quit);
            const auto finished = app.compositor ? QObject::connect(app.compositor, &PanelCompositor::finished,
                &motion, &QEventLoop::quit) : QMetaObject::Connection();
            const auto navigationFinished = QObject::connect(bar, &ElaNavigationBar::displayModeTransitionFinished,
                &motion, &QEventLoop::quit);
            const auto drawerFinished = app.drawer ? QObject::connect(app.drawer, &ElaDrawerArea::drawerAnimationFinished,
                &motion, &QEventLoop::quit) : QMetaObject::Connection();
            if (scene == "left") navigationPane->setExpanded(i % 2 != 0);
            else if (scene == "right") context->setDockVisible(i % 2 == 0);
            else if (scene == "bottom") drawer->setBottomCollapsed(i % 2 != 0);
            else context->dockHost()->setSectionCollapsed(resource.stableKey(), i % 2 == 0);
            dispatchTimes.append(duration.nsecsElapsed() / 1e6);
            renderers.append(bar->isDisplayModeAnimating() ? "ElaNavigationBar"
                : app.drawer && app.drawer->isDrawerAnimating() ? "ElaDrawerArea"
                : app.compositor && app.compositor->isActive() ? app.compositor->renderer() : "immediate");
            snapshotBytes.append(app.drawer ? app.drawer->drawerSnapshotBytes() : app.compositor ? app.compositor->snapshotBytes() : 0);
            preparationMs.append(app.drawer ? app.drawer->drawerPreparationMs() : 0);
            if ((app.compositor && app.compositor->isActive()) || bar->isDisplayModeAnimating()
                || (app.drawer && app.drawer->isDrawerAnimating())) {
                deadline.start(3000);
                motion.exec();
            }
            QObject::disconnect(finished);
            QObject::disconnect(navigationFinished);
            QObject::disconnect(drawerFinished);
            QTest::qWait(20);
            app.measuring = false;
            durations.append(duration.nsecsElapsed() / 1e6);
            if ((app.compositor && app.compositor->isActive()) || bar->isDisplayModeAnimating()
                || (app.drawer && app.drawer->isDrawerAnimating())) return 8;
        }
        QJsonObject events;
        for (auto it = app.costs.cbegin(); it != app.costs.cend(); ++it)
            events[it.key()] = QJsonObject{{"count", it->count}, {"totalMs", it->totalNs / 1e6},
                                           {"maxMs", it->maximumNs / 1e6}};
        auto sorted = app.intervals; std::sort(sorted.begin(), sorted.end());
        const auto quantile = [&](double fraction) {
            return sorted.isEmpty() ? 0.0 : sorted[qMin(sorted.size() - 1, qsizetype(fraction * sorted.size()))];
        };
        const auto metrics = editor->hotPathMetricsForTest();
        scenarios.append(QJsonObject{{"scene", scene}, {"width", host.width()}, {"height", host.height()},
            {"editorWidth", editor->width()}, {"editorHeight", editor->height()},
            {"durationsMs", durations}, {"frameIntervals", sorted.size()},
            {"dispatchMs", dispatchTimes}, {"renderers", renderers},
            {"snapshotBytes", snapshotBytes}, {"preparationMs", preparationMs},
            {"renderer", app.nativeNavigation ? "ElaNavigationBar/live-layout"
                : app.drawer ? "ElaDrawerArea" : app.compositor ? app.compositor->renderer() : "live-layout"},
            {"compositionRefreshRate", !app.nativeNavigation && app.compositor ? app.compositor->compositionRefreshRate() : 0},
            {"intervalMedianMs", quantile(.5)}, {"intervalP95Ms", quantile(.95)},
            {"intervalMaxMs", quantile(1)}, {"visiblePresentationRefreshes", qint64(metrics.visiblePresentationRefreshes)},
            {"events", events}});
        }
    }
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) return 9;
    output.write(QJsonDocument(QJsonObject{{"platform", QApplication::platformName()},
        {"devicePixelRatio", host.devicePixelRatioF()}, {"transitionsPerSize", 12},
        {"note", "CPU event timings, not display presentation times. Event costs are inclusive and can nest."},
        {"scenarios", scenarios}}).toJson());
    std::puts("Panel animation benchmark complete.");
    if (nativeRun && argc == 1) {
        app.setQuitOnLastWindowClosed(true);
        return app.exec();
    }
    return editor->document()->isModified() ? 10 : 0;
}
