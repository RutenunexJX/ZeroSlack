#include "analysisscheduler.h"
#include "applicationthememanager.h"
#include "mainwindow.h"
#include "navigationwidget.h"
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
    bool notify(QObject* receiver, QEvent* event) override {
        const bool paint = receiver == viewport && event->type() == QEvent::Paint;
        const bool result = QApplication::notify(receiver, event);
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

int main(int argc, char** argv) {
    MeasuredApplication app(argc, argv);
    if (argc != 5) return 2;
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
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 4;
    theme.setMode(ThemeMode::Light); theme.applyToApplication();
    QElapsedTimer init; init.start();
    MainWindow host;
    const double constructMs = init.nsecsElapsed() / 1e6;
    host.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    host.resize(1280, 800); host.show();
    auto* navigation = host.findChild<NavigationWidget*>();
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
