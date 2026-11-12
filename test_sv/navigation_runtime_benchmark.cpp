#include "testuistyle.h"
#include "analysisscheduler.h"
#include "applicationthememanager.h"
#include "mainwindow.h"
#include "navigationwidget.h"
#include "navigationmanager.h"
#include "workspacemanager.h"
#include <QtWidgets>
#include <QtTest>
#include <functional>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#endif

// Native MainWindow probe. The endpoint includes the completed viewport paint
// and a DWM synchronization; it is not a physical monitor / photon measurement.
class MeasuredApplication : public QApplication {
public:
    using QApplication::QApplication;
    QWidget* viewport = nullptr;
    qint64 paints = 0;
    qint64 modelRevision = 0;
    qint64 paintedRevision = -1;
    qint64 longestEventNs = 0;
    bool notify(QObject* receiver, QEvent* event) override {
        QElapsedTimer elapsed; elapsed.start();
        const bool paint = receiver == viewport && event->type() == QEvent::Paint;
        const bool result = QApplication::notify(receiver, event);
        longestEventNs = qMax(longestEventNs, elapsed.nsecsElapsed());
        if (paint) { ++paints; paintedRevision = modelRevision; }
        return result;
    }
};

static int fileCount(QTreeWidget* tree) {
    int count = 0;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        count += (*it)->data(0, NavigationWidget::FileTreeKindRole).toInt() == NavigationWidget::FileItem;
    return count;
}

static bool waitUntil(const std::function<bool()>& ready, int timeout = 60000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) QTest::qWait(1);
    return ready();
}

static double cpuMs() {
#ifdef Q_OS_WIN
    FILETIME c, e, k, u;
    if (GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u))
        return ((quint64(k.dwHighDateTime) << 32) + k.dwLowDateTime
            + (quint64(u.dwHighDateTime) << 32) + u.dwLowDateTime) / 10000.0;
#endif
    return 0;
}

static QJsonObject memory() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX m{}; m.cb = sizeof(m);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m)))
        return {{"workingSetBytes", double(m.WorkingSetSize)}, {"privateBytes", double(m.PrivateUsage)}};
#endif
    return {};
}

static int materializedRows(QAbstractItemModel* model, const QModelIndex& parent = {})
{
    int result = model->rowCount(parent);
    for (int row = 0; row < model->rowCount(parent); ++row)
        result += materializedRows(model, model->index(row, 0, parent));
    return result;
}

static int measureDesign(MeasuredApplication& app, MainWindow& host, NavigationWidget* navigation,
                         const QString& output, const QString& fixtureRoot, int count,
                         double constructMs)
{
    auto* tree = host.findChild<QTreeView*>("navigationDesignTree");
    auto* search = host.findChild<QLineEdit*>("navigationDesignSearchLineEdit");
    if (!tree || !search) return 20;
    app.viewport = tree->viewport();
    QJsonArray telemetry, actions;
    int applied = 0;
    const auto connection = QObject::connect(host.navigationManager.get(), &NavigationManager::navigationTelemetry, tree,
        [&](const SemanticAnalysisTelemetry& value) {
            telemetry.append(QJsonObject{{"generation", double(value.generation)},
                {"workerMs", double(value.workerMs)}, {"applyMs", double(value.uiRefreshMs)},
                {"detail", value.detail}});
            ++applied;
        });
    const auto cleanup = qScopeGuard([&] { QObject::disconnect(connection); app.viewport = nullptr; });
    QSignalSpy resets(tree->model(), &QAbstractItemModel::modelReset);
    navigation->setActiveTab(NavigationWidget::DesignTab);
    QElapsedTimer init; init.start();
    app.longestEventNs = 0;
    std::fprintf(stderr, "design: opening workspace\n"); std::fflush(stderr);
    if (!host.workspaceManager->openWorkspace(fixtureRoot)) return 21;
    if (!waitUntil([&] { return !host.workspaceManager->isWorkspaceScanActive()
            && !host.analysisScheduler->isSemanticAnalysisActive()
            && !host.workspaceManager->getSystemVerilogFiles().isEmpty(); }, 180000)) return 22;
    const double semanticReadyMs = init.nsecsElapsed() / 1e6;
    std::fprintf(stderr, "design: semantic ready %.2f\n", semanticReadyMs); std::fflush(stderr);
    // The pre-migration UI requires this explicit browse entry. The new UI uses
    // the same operation only to reveal a module, without choosing Simulation top.
    const auto issue = host.navigationManager->setDesignTop("top", QDir(fixtureRoot).filePath("design.sv"));
    std::fprintf(stderr, "design: selected top %s\n", qPrintable(issue)); std::fflush(stderr);
    if (!issue.isEmpty()) { std::fprintf(stderr, "%s\n", qPrintable(issue)); return 23; }
    if (!waitUntil([&] { return tree->isVisible() && tree->isEnabled()
            && tree->model()->rowCount() > 0 && applied > 0; }, 180000)) return 24;
    tree->viewport()->update();
    const auto firstPaint = app.paints;
    if (!waitUntil([&] { return app.paints > firstPaint; })) return 25;
    const double firstScreenMs = init.nsecsElapsed() / 1e6;
    const double firstLongestMs = app.longestEventNs / 1e6;
    const int firstRows = materializedRows(tree->model());
    std::fprintf(stderr, "design: first screen rows=%d\n", firstRows); std::fflush(stderr);
    const auto measure = [&](const QString& name, const std::function<void()>& action,
                             const std::function<bool()>& condition = [] { return true; }) {
        const auto paints = app.paints;
        const auto resetCount = resets.size();
        app.longestEventNs = 0;
        QElapsedTimer elapsed; elapsed.start();
        const double cpuBefore = cpuMs();
        action();
        const double dispatch = elapsed.nsecsElapsed() / 1e6;
        tree->viewport()->update();
        const bool ready = waitUntil([&] {
            const auto pending = tree->property("designSearchPending");
            return condition() && (!pending.isValid() || !pending.toBool()) && app.paints > paints;
        }, 180000);
        const double paintMs = elapsed.nsecsElapsed() / 1e6;
#ifdef Q_OS_WIN
        const long sync = DwmFlush();
#else
        const long sync = -1;
#endif
        actions.append(QJsonObject{{"action", name}, {"ready", ready}, {"dispatchMs", dispatch},
            {"paintReadyMs", paintMs}, {"presentSyncMs", elapsed.nsecsElapsed() / 1e6},
            {"dwmResult", double(sync)}, {"threadCpuMs", cpuMs() - cpuBefore},
            {"maximumEventMs", app.longestEventNs / 1e6},
            {"modelResets", resets.size() - resetCount}, {"materializedRows", materializedRows(tree->model())}});
        std::fprintf(stderr, "design: %s ready=%d %.2fms\n", qPrintable(name), ready, paintMs); std::fflush(stderr);
        QTest::qWait(80);
        return ready;
    };
    bool valid = true;
    const auto designRoot = [&] {
        for (int row=0;row<tree->model()->rowCount();++row) {
            const auto index=tree->model()->index(row,0);
            if (index.data(Qt::UserRole+43).toString()==QStringLiteral("top")
                || index.data().toString()==QStringLiteral("top")) return index;
        }
        return tree->model()->index(0,0);
    };
    for (int i = 0; i < 6; ++i) {
        valid &= measure("collapse", [&] { tree->collapse(designRoot()); });
        valid &= measure("cached-expand", [&] { tree->expand(designRoot()); });
        valid &= measure("cached-locate", [&] { navigation->selectDesignInstance("top.u_0", true); });
        valid &= measure("search-instance", [&] { search->setText("u_0"); });
        valid &= measure("clear-search", [&] { search->clear(); });
    }
    const int beforeEdit = applied;
    const QString source = QDir(fixtureRoot).filePath("design.sv");
    QFile input(source); if (!input.open(QIODevice::ReadOnly)) return 26;
    const auto original = input.readAll(); input.close();
    valid &= measure("anchor-edit-refresh", [&] {
        QSaveFile file(source);
        if (file.open(QIODevice::WriteOnly)) { file.write("// benchmark anchor edit\n" + original); file.commit(); }
        host.analysisScheduler->handleExternalFileChanged(source, 0);
    }, [&] { return applied > beforeEdit && !host.analysisScheduler->isSemanticAnalysisActive(); });
    host.grab().save(output + ".png");
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly)) return 27;
    file.write(QJsonDocument(QJsonObject{{"mode", "design"}, {"platform", QApplication::platformName()},
        {"dpr", host.devicePixelRatioF()}, {"fixtureInstanceCount", count},
        {"mainWindowConstructMs", constructMs}, {"semanticReadyMs", semanticReadyMs},
        {"firstScreenMs", firstScreenMs}, {"firstMaximumEventMs", firstLongestMs},
        {"firstMaterializedRows", firstRows}, {"modelResets", resets.size()},
        {"actions", actions}, {"telemetry", telemetry}, {"memory", memory()}, {"valid", valid}}).toJson());
    app.viewport = nullptr;
    host.close();
    return valid ? 0 : 28;
}

int main(int argc, char** argv) {
    MeasuredApplication app(argc, argv);
    if (argc != 5 && argc != 6) return 2;
    const QString output = QString::fromLocal8Bit(argv[1]);
    const QString fixtureRoot = QString::fromLocal8Bit(argv[2]);
    const int count = QByteArray(argv[3]).toInt();
    const bool grouped = QByteArray(argv[4]) == "grouped";
    QTemporaryDir profile(QFileInfo(output).absolutePath() + "/profile-XXXXXX");
    if (!profile.isValid() || count < 1) return 3;
    QCoreApplication::setApplicationName("ZeroSlack-Navigation-Runtime-Benchmark");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.filePath("system"));
    qputenv("ZEROSLACK_PROFILE_ROOT", profile.path().toUtf8());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", profile.filePath("sessions.ini").toUtf8());
    qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", profile.filePath("editing-time.json").toUtf8());
    qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT", profile.filePath("simulation").toUtf8());
    qputenv("XIPS_LIBRARY", profile.filePath("library").toUtf8());
    auto& theme = ApplicationThemeManager::instance();
    if (!initializeUiStyleForTest()) return 4;
    theme.setMode(ThemeMode::Light); theme.applyToApplication();
    QElapsedTimer init; init.start();
    MainWindow host;
    const double constructMs = init.nsecsElapsed() / 1e6;
    host.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    host.resize(1280, 800); host.show();
    auto* navigation = host.findChild<NavigationWidget*>();
    if (argc == 6 && QByteArray(argv[5]) == "design")
        return measureDesign(app, host, navigation, output, fixtureRoot, count, constructMs);
    auto* tree = host.findChild<QTreeWidget*>("navigationFileTree");
    auto* search = host.findChild<QLineEdit*>("navigationSearchLineEdit");
    if (!navigation || !tree || !search) return 5;
    app.viewport = tree->viewport();
    QObject::connect(tree->model(), &QAbstractItemModel::modelReset, &app, [&] { ++app.modelRevision; });
    QObject::connect(tree->model(), &QAbstractItemModel::rowsInserted, &app, [&] { ++app.modelRevision; });
    QObject::connect(tree->model(), &QAbstractItemModel::rowsRemoved, &app, [&] { ++app.modelRevision; });
    QObject::connect(tree->model(), &QAbstractItemModel::dataChanged, &app, [&] { ++app.modelRevision; });
    QTest::qWait(300);
    init.restart();
    if (!host.workspaceManager->openWorkspace(fixtureRoot)) return 6;
    const auto filesReady = [&] { return !host.workspaceManager->isWorkspaceScanActive()
        && host.workspaceManager->getSystemVerilogFiles().size() == count
        && fileCount(tree) == host.workspaceManager->getAllFiles().size(); };
    if (!waitUntil(filesReady)) {
        std::fprintf(stderr, "workspace setup timeout: tree=%d workspace=%d scan=%d semantic=%d\n",
            fileCount(tree), int(host.workspaceManager->getAllFiles().size()),
            host.workspaceManager->isWorkspaceScanActive(), host.analysisScheduler->isSemanticAnalysisActive());
        host.grab().save(output + ".failure.png");
        return 7;
    }
    const double workspaceReadyMs = init.nsecsElapsed() / 1e6;
    if (!waitUntil([&] { return !host.analysisScheduler->isSemanticAnalysisActive(); })) return 8;
    if (grouped) {
        WorkspaceVirtualSourceGroup group;
        group.name = "RTL group";
        for (int i = 0; i < count; i += 2)
            group.files.append(QDir(fixtureRoot).filePath(QString("rtl/group_%1/module_%2.sv").arg(i / 100).arg(i)));
        QString error;
        if (!host.workspaceManager->setVirtualSourceGroups({group}, &error)) return 9;
        if (!waitUntil([&] { return filesReady() && !host.analysisScheduler->isSemanticAnalysisActive(); })) {
            std::fprintf(stderr, "group setup timeout: tree=%d workspace=%d scan=%d semantic=%d\n",
                fileCount(tree), int(host.workspaceManager->getAllFiles().size()),
                host.workspaceManager->isWorkspaceScanActive(), host.analysisScheduler->isSemanticAnalysisActive());
            host.grab().save(output + ".failure.png");
            return 10;
        }
    }
    QTest::qWait(600);
    navigation->setActiveTab(NavigationWidget::FileTab);
    search->setFocus();
    if (!tree->isVisible()) return 11;
    const int totalFiles = host.workspaceManager->getAllFiles().size();
    QJsonArray actions;
    bool valid = true;
    const auto measure = [&](const QString& name, int expected, const std::function<void()>& action) {
        const auto paintBefore = app.paints;
        QElapsedTimer timer; timer.start();
        const double cpuBefore = cpuMs();
        QSignalSpy resets(tree->model(), &QAbstractItemModel::modelReset);
        action();
        const double dispatchMs = timer.nsecsElapsed() / 1e6;
        const double dispatchCpuMs = cpuMs() - cpuBefore;
        const bool ready = waitUntil([&] { return fileCount(tree) == expected && app.paints > paintBefore
            && app.paintedRevision == app.modelRevision; });
        const double paintReadyMs = timer.nsecsElapsed() / 1e6;
        long syncResult = -1;
#ifdef Q_OS_WIN
        syncResult = DwmFlush();
#endif
        const double presentSyncMs = timer.nsecsElapsed() / 1e6;
        valid &= ready;
        actions.append(QJsonObject{{"action", name}, {"dispatchMs", dispatchMs},
            {"dispatchThreadCpuMs", dispatchCpuMs}, {"paintReadyMs", paintReadyMs},
            {"presentSyncMs", presentSyncMs}, {"dwmResult", double(syncResult)},
            {"ready", ready}, {"modelResets", resets.size()}, {"files", fileCount(tree)}});
        QTest::qWait(80);
    };
    for (int i = 0; i < 6; ++i) {
        measure("filter-empty", 0, [&] { QTest::keyClick(search, Qt::Key_Z); });
        measure("clear-filter", totalFiles, [&] { QTest::keyClick(search, Qt::Key_Backspace); });
    }
    for (int i = 0; i < 6; ++i) {
        navigation->setActiveTab(NavigationWidget::DesignTab); QTest::qWait(80);
        measure("return-files", totalFiles, [&] { navigation->setActiveTab(NavigationWidget::FileTab); });
    }
    host.grab().save(output + ".png");
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly)) return 12;
    file.write(QJsonDocument(QJsonObject{{"platform", QApplication::platformName()},
        {"dpr", host.devicePixelRatioF()}, {"count", count}, {"totalFiles", totalFiles}, {"grouped", grouped},
        {"mainWindowConstructMs", constructMs}, {"workspaceTreeReadyMs", workspaceReadyMs},
        {"memory", memory()}, {"actions", actions}, {"valid", valid}}).toJson());
    app.viewport = nullptr;
    host.close();
    return valid ? 0 : 13;
}
