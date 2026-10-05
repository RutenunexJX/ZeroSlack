#include "applicationthememanager.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "navigationwidget.h"
#include "navigationpanecoordinator.h"
#include "tabmanager.h"
#include "panellayoutcontroller.h"
#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "liveinsightscontextprovider.h"
#include "ElaNavigationBar.h"
#include "ElaDrawerArea.h"
#include <QApplication>
#include <QAbstractEventDispatcher>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cstdio>
#include <QTextBlock>
#include <QTextLayout>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static double threadCpuMs() {
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return ((quint64(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime
            + (quint64(user.dwHighDateTime) << 32) + user.dwLowDateTime) / 10000.0;
#endif
    return 0;
}

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
    QPointer<ElaDrawerArea> drawer;
    QElapsedTimer frames;
    qint64 previousFrame = -1;
    QPointer<QWidget> editorViewport;
    QJsonArray viewportEvents;
    bool tail = false;
    QString traceMode = qEnvironmentVariable("ZEROSLACK_SIDEBAR_TRACE_MODE", "minimal");
    struct ViewSample { int type; qint64 start, duration; int width, height; bool tail; };
    struct DispatchSample { quintptr receiver; int type, depth; qint64 start, duration, children; bool tail; };
    QVector<ViewSample> viewSamples;
    QVector<DispatchSample> dispatchSamples;
    QHash<quintptr, QString> owners;
    qint64 childTimes[256]{};
    int dispatchDepth = 0;
    qint64 rootDispatchNs = 0;
    qint64 sleepingSince = -1;
    qint64 sleepingNs = 0;

    void resetSamples() {
        viewSamples.clear(); viewSamples.reserve(1024);
        dispatchSamples.clear(); dispatchSamples.reserve(32768);
        dispatchDepth = 0; rootDispatchNs = 0; sleepingNs = 0; sleepingSince = -1;
        owners.clear();
        const auto identify = [this](QObject* object) {
            owners[quintptr(object)] = QString("%1/%2@%3 parent=%4/%5")
                .arg(object->metaObject()->className(), object->objectName()).arg(quintptr(object), 0, 16)
                .arg(object->parent() ? object->parent()->metaObject()->className() : "none",
                     object->parent() ? object->parent()->objectName() : QString());
        };
        for (auto* widget : QApplication::allWidgets()) identify(widget);
        for (auto* window : QGuiApplication::allWindows()) identify(window);
        for (auto* object : findChildren<QObject*>()) identify(object);
        if (editorViewport && editorViewport->window())
            for (auto* object : editorViewport->window()->findChildren<QObject*>()) identify(object);
    }

    void finishSamples() {
        if (traceMode == "legacy") return;
        for (const auto& sample : viewSamples)
            viewportEvents.append(QJsonObject{{"event", sample.type == QEvent::Paint ? "paint" : "resize"},
                {"ms", sample.start / 1e6}, {"handlerMs", sample.duration / 1e6}, {"tail", sample.tail},
                {"width", sample.width}, {"height", sample.height}});
    }

    QJsonArray dispatchJson() const {
        QJsonArray output;
        for (const auto& sample : dispatchSamples)
            output.append(QJsonObject{{"owner", owners.value(sample.receiver, "unregistered")},
                {"type", sample.type}, {"depth", sample.depth}, {"ms", sample.start / 1e6},
                {"handlerMs", sample.duration / 1e6}, {"exclusiveMs", (sample.duration-sample.children)/1e6},
                {"tail", sample.tail}});
        return output;
    }

    bool notify(QObject* receiver, QEvent* event) override {
        const auto type = event->type();
        if (measuring && traceMode != "legacy") {
            if (traceMode == "off") return QApplication::notify(receiver, event);
            const bool viewportEvent = receiver == editorViewport && (type == QEvent::Resize || type == QEvent::Paint);
            const bool profile = traceMode == "profile";
            if (!viewportEvent && !profile) return QApplication::notify(receiver, event);
            const qint64 start = frames.nsecsElapsed();
            const int depth = dispatchDepth++;
            Q_ASSERT(depth < 256);
            childTimes[depth] = 0;
            const bool handled = QApplication::notify(receiver, event);
            const qint64 elapsed = frames.nsecsElapsed() - start;
            --dispatchDepth;
            if (depth) childTimes[depth - 1] += elapsed;
            else if (!tail) rootDispatchNs += elapsed;
            if (profile) dispatchSamples.append({quintptr(receiver), int(type), depth, start, elapsed, childTimes[depth], tail});
            if (viewportEvent && editorViewport)
                viewSamples.append({int(type), start, elapsed, editorViewport->width(), editorViewport->height(), tail});
            return handled;
        }
        if (!measuring || (type != QEvent::Paint && type != QEvent::Resize
            && type != QEvent::LayoutRequest && type != QEvent::UpdateRequest))
            return QApplication::notify(receiver, event);
        const QString owner = receiver->parent() && receiver->parent()->inherits("MyCodeEditor")
            ? QString("Editor/%1").arg(receiver->objectName())
            : QString("%1/%2").arg(receiver->metaObject()->className(), receiver->objectName());
        const QString key = QString::number(type) + ":" + owner;
        const bool viewportEvent = receiver == editorViewport && (type == QEvent::Resize || type == QEvent::Paint);
        const double timestampMs = frames.nsecsElapsed() / 1e6;
        if ((!nativeNavigation && drawer && type == QEvent::Paint && receiver->parent() == drawer
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
        if (viewportEvent && editorViewport)
            viewportEvents.append(QJsonObject{{"event", type == QEvent::Resize ? "resize" : "paint"},
                {"ms", timestampMs}, {"handlerMs", elapsed / 1e6}, {"tail", tail},
                {"width", editorViewport->width()}, {"height", editorViewport->height()}});
        return handled;
    }
};

int main(int argc, char** argv) {
    MeasuredApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QObject::connect(app.eventDispatcher(), &QAbstractEventDispatcher::aboutToBlock, &app, [&] {
        if (app.measuring && !app.tail) app.sleepingSince = app.frames.nsecsElapsed();
    });
    QObject::connect(app.eventDispatcher(), &QAbstractEventDispatcher::awake, &app, [&] {
        if (app.measuring && !app.tail && app.sleepingSince >= 0) app.sleepingNs += app.frames.nsecsElapsed() - app.sleepingSince;
        app.sleepingSince = -1;
    });
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
    theme.setMode(ThemeMode::Light);
    theme.setAnimationsEnabled(true);
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
    if (!editor || !bar || !navigationPane || !app.dock) return 7;
    app.editorViewport = editor->viewport();
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
    const bool sidebarFps = qEnvironmentVariableIsSet("ZEROSLACK_SIDEBAR_FPS_BENCHMARK");
    const bool maximized = sidebarFps && nativeRun && qEnvironmentVariableIsSet("ZEROSLACK_SIDEBAR_FPS_MAXIMIZED");
    const QList<QSize> sizes = maximized ? QList<QSize>{host.screen()->availableGeometry().size()}
        : sidebarFps ? QList<QSize>{QSize(1280, 800)} : nativeRun
        ? QList<QSize>{QSize(1000, 700), host.screen()->availableGeometry().size()}
        : QList<QSize>{QSize(1000, 700), QSize(2560, 1392), QSize(3840, 2160)};
    for (QSize size : sizes) {
        if (maximized || (!sidebarFps && nativeRun && size == sizes.last())) host.showMaximized();
        else host.resize(size);
        QTest::qWait(100);
        for (const auto& scene : {QString("left"), QString("right"), QString("bottom"), QString("section")}) {
        if (sidebarFps && (qEnvironmentVariableIsSet("ZEROSLACK_BOTTOM_FPS_BENCHMARK")
                ? scene != "bottom" : scene != "left" && scene != "right")) continue;
        for (auto* item : host.findChildren<ElaDrawerArea*>()) item->finishDrawerAnimation();
        navigationPane->setExpanded(true, false);
        drawer->setBottomCollapsed(true);
        context->setDockVisible(scene == "section");
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
        QJsonArray actions;
        for (int i = 0; i < 12; ++i) {
            app.previousFrame = -1; app.viewportEvents = {}; app.tail = false;
            app.resetSamples();
            app.frames.start(); app.measuring = true;
            const double cpuStart = threadCpuMs();
            QElapsedTimer duration; duration.start();
            QEventLoop motion;
            QTimer deadline;
            deadline.setSingleShot(true);
            QObject::connect(&deadline, &QTimer::timeout, &motion, &QEventLoop::quit);
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
                : "immediate");
            snapshotBytes.append(app.drawer ? app.drawer->drawerSnapshotBytes() : 0);
            preparationMs.append(app.drawer ? app.drawer->drawerPreparationMs() : 0);
            if (bar->isDisplayModeAnimating()
                || (app.drawer && app.drawer->isDrawerAnimating())) {
                deadline.start(3000);
                motion.exec();
            }
            QObject::disconnect(navigationFinished);
            QObject::disconnect(drawerFinished);
            const double actionMs = duration.nsecsElapsed() / 1e6;
            const double cpuMs = threadCpuMs() - cpuStart;
            app.tail = true;
            QTest::qWait(20);
            app.measuring = false;
            app.finishSamples();
            durations.append(actionMs);
            actions.append(QJsonObject{{"index", i},
                {"opening", scene == "left" ? i % 2 != 0 : i % 2 == 0},
                {"durationMs", actionMs}, {"tailMs", duration.nsecsElapsed() / 1e6 - actionMs},
                {"uiThreadCpuMs", cpuMs}, {"viewportEvents", app.viewportEvents},
                {"rootDispatchMs", app.rootDispatchNs / 1e6}, {"dispatcherBlockMs", app.sleepingNs / 1e6},
                {"dispatchEvents", app.dispatchJson()}});
            if (bar->isDisplayModeAnimating()
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
        int visibleCharacters = 0, visibleFormats = 0, visibleBlocks = 0;
        const int lastVisible = editor->cursorForPosition(editor->viewport()->rect().bottomRight()).blockNumber();
        for (auto block = editor->cursorForPosition(QPoint(0, 0)).block();
             block.isValid() && block.blockNumber() <= lastVisible; block = block.next()) {
            ++visibleBlocks;
            visibleCharacters += block.text().size();
            visibleFormats += block.layout()->formats().size();
        }
        scenarios.append(QJsonObject{{"scene", scene}, {"width", host.width()}, {"height", host.height()},
            {"editorWidth", editor->width()}, {"editorHeight", editor->height()},
            {"visibleCharacters", visibleCharacters}, {"visibleFormats", visibleFormats},
            {"visibleBlocks", visibleBlocks}, {"extraSelections", editor->extraSelections().size()},
            {"actions", actions},
            {"durationsMs", durations}, {"frameIntervals", sorted.size()},
            {"dispatchMs", dispatchTimes}, {"renderers", renderers},
            {"snapshotBytes", snapshotBytes}, {"preparationMs", preparationMs},
            {"renderer", app.nativeNavigation ? "ElaNavigationBar/live-layout"
                : app.drawer ? "ElaDrawerArea" : "live-layout"},
            {"intervalMedianMs", quantile(.5)}, {"intervalP95Ms", quantile(.95)},
            {"intervalMaxMs", quantile(1)}, {"visiblePresentationRefreshes", qint64(metrics.visiblePresentationRefreshes)},
            {"events", events}});
        }
    }
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) return 9;
    QJsonArray nativeWidgets;
    for (auto* widget : host.findChildren<QWidget*>())
        if (widget->internalWinId()) nativeWidgets.append(QJsonObject{{"class", widget->metaObject()->className()},
            {"name", widget->objectName()}, {"nativeAttribute", widget->testAttribute(Qt::WA_NativeWindow)},
            {"width", widget->width()}, {"height", widget->height()}, {"isWindow", widget->isWindow()}});
    output.write(QJsonDocument(QJsonObject{{"platform", QApplication::platformName()},
        {"traceMode", app.traceMode}, {"nativeWidgets", nativeWidgets},
        {"devicePixelRatio", host.devicePixelRatioF()}, {"transitionsPerSize", 12},
        {"qtVersion", qVersion()}, {"screenRefreshHz", host.screen()->refreshRate()},
        {"dontCreateNativeWidgetSiblings", QApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings)},
        {"windowMode", maximized ? "maximized" : "window"},
        {"sourceLines", 5002}, {"sourceBytes", source.toUtf8().size()},
        {"theme", "Light"}, {"viewportBaseColor", editor->viewport()->palette().color(QPalette::Base).name()},
        {"viewportOpaque", editor->viewport()->testAttribute(Qt::WA_OpaquePaintEvent)},
        {"viewportAutoFill", editor->viewport()->autoFillBackground()},
        {"note", "Actual editor viewport Resize/Paint dispatches, not presented display frames. Action duration excludes the separately recorded 20 ms tail. handlerMs is inclusive dispatch wall time; uiThreadCpuMs is Windows thread CPU time, with OS accounting granularity. Legacy aggregate event costs can nest."},
        {"scenarios", scenarios}}).toJson());
    std::puts("Panel animation benchmark complete.");
    if (nativeRun && argc == 1) {
        app.setQuitOnLastWindowClosed(true);
        return app.exec();
    }
    return editor->document()->isModified() ? 10 : 0;
}
