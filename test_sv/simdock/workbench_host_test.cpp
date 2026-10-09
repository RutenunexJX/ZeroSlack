#include "../../src/simulation/simdock/ui/workbench.h"
#include "applicationthememanager.h"
#include <QtTest>
#include <QtConcurrent>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QSemaphore>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSplitter>
#include <QLineEdit>
#include <QToolButton>
#include <cmath>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Migrated host contract: the same owned workbench is embedded directly.
// The former DLL loader checks are superseded by the integrated dependency audit.
class WorkbenchHostTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    static QWidget* create(QWidget* parent, QObject*) {
        return parent ? new simdock::Workbench(parent) : nullptr;
    }
    static bool setHostTheme(QWidget* widget, bool dark) {
        ApplicationThemeManager::instance().setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        return QMetaObject::invokeMethod(widget, "setDarkTheme", Q_ARG(bool, dark));
    }
    static constexpr auto id = "1456b0f8-d164-4e78-bb8f-448a3edb9740";
    QVariantMap state(QWidget* widget) {
        QVariantMap value;
        if (!QMetaObject::invokeMethod(widget, "saveState", Q_RETURN_ARG(QVariantMap, value))) qFatal("saveState unavailable");
        return value;
    }
    QString call(QWidget* widget, const char* method, const QString& arg) {
        QString error;
        if (!QMetaObject::invokeMethod(widget, method, Q_RETURN_ARG(QString, error), Q_ARG(QString, arg))) qFatal("QString API unavailable");
        return error;
    }
    QString restore(QWidget* widget, const QVariantMap& value) {
        QString error;
        if (!QMetaObject::invokeMethod(widget, "restoreState", Q_RETURN_ARG(QString, error), Q_ARG(QVariantMap, value))) qFatal("restoreState unavailable");
        return error;
    }
    bool closable(QWidget* widget) {
        bool value = false;
        if (!QMetaObject::invokeMethod(widget, "canClose", Q_RETURN_ARG(bool, value))) qFatal("canClose unavailable");
        return value;
    }
    static void write(const QString& path, const QByteArray& data) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) qFatal("fixture write failed");
    }
    static void fixture(const QTemporaryDir& root) {
        write(root.filePath("dut.sv"), "module checked(input logic clk,rst,byte_valid,out_ready, input logic [7:0] byte_data, "
              "output logic byte_ready,uart_tx,out_valid,output logic [7:0] out_data); "
              "assign byte_ready=1'b1; assign uart_tx=1'b1; assign out_valid=byte_valid; assign out_data=byte_data; endmodule");
        QDir().mkpath(root.filePath(".simdock/projects"));
        const QJsonObject project{{"schema", "simdock.project/v1"}, {"id", id}, {"name", "Embedded test"},
            {"sources", QJsonArray{"dut.sv"}}, {"dutFile", "dut.sv"}, {"dutName", "checked"}, {"durationNs", 10000}};
        write(root.filePath(QString(".simdock/projects/%1.json").arg(id)), QJsonDocument(project).toJson());
    }
    static void section(QWidget* widget, const QString& name) {
        if (widget->property("compactLayout").toBool()) {
            auto* selector = widget->findChild<QComboBox*>("workbenchSection");
            QVERIFY(selector && selector->isVisible());
            selector->setCurrentIndex(selector->findData(name));
            QCoreApplication::processEvents();
        }
    }
    static bool reachable(QWidget* control, QWidget* boundary) {
        if (!control || !control->isVisible()) return false;
        for (auto* parent = control->parentWidget(); parent && parent != boundary; parent = parent->parentWidget()) {
            if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
                scroll->ensureWidgetVisible(control, 0, 0);
                QCoreApplication::processEvents();
                if (!scroll->viewport()->rect().contains(QRect(control->mapTo(scroll->viewport(), QPoint()), control->size())))
                    return false;
            }
        }
        return boundary->rect().contains(QRect(control->mapTo(boundary, QPoint()), control->size()));
    }
    static void capture(QWidget* window, const QString& name) {
        const auto report = qEnvironmentVariable("SIMDOCK_EMBED_REPORT");
        if (report.isEmpty()) return;
        QTest::qWait(80);
        QVERIFY(window->grab().save(QDir(report).filePath(name + ".png")));
        const auto bounds = window->screen()->availableGeometry();
        const QJsonObject info{{"platform", QGuiApplication::platformName()}, {"dpr", window->devicePixelRatioF()},
            {"width", window->width()}, {"height", window->height()}, {"x", window->frameGeometry().x()},
            {"y", window->frameGeometry().y()}, {"availableWidth", bounds.width()}, {"availableHeight", bounds.height()}};
        write(QDir(report).filePath(name + ".json"), QJsonDocument(info).toJson());
        if (QGuiApplication::platformName() == "windows") {
            const auto native = window->screen()->grabWindow(window->winId());
            QVERIFY(!native.isNull());
            QVERIFY(native.save(QDir(report).filePath(name + "-native.png")));
        }
    }
    static void dialogFits(QDialog* dialog, const QString& name) {
        QVERIFY(dialog);
        QTest::qWait(50);
        QVERIFY2(dialog->screen()->availableGeometry().contains(dialog->frameGeometry()), qPrintable(name));
        // A native dialog is a window and does not automatically inherit its
        // embedded parent's palette. Its new scroll surface must match it.
        if (dialog->parentWidget()) QCOMPARE(dialog->palette().color(QPalette::Window), dialog->parentWidget()->palette().color(QPalette::Window));
        for (auto* area : dialog->findChildren<QScrollArea*>())
            if (area->widget() && area->property("simdockDialogSurface").toBool())
                QCOMPARE(area->widget()->palette().color(QPalette::Window), dialog->palette().color(QPalette::Window));
        capture(dialog, name);
    }
    static double contrast(QColor a, QColor b) {
        const auto luminance = [](QColor c) {
            const auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
            return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
        };
        const auto x = luminance(a), y = luminance(b);
        return (qMax(x, y) + .05) / (qMin(x, y) + .05);
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("UnrelatedHost");
        QCoreApplication::setApplicationName("HostEmbeddingTest");
        QCoreApplication::setApplicationVersion("host-7");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QSettings().setValue("workspace/last", settings.path());
        QSettings().setValue("host/sentinel", "untouched");
        qApp->setStyleSheet("QLabel#hostLabel { color: #113355; }");
        QLocale::setDefault(QLocale(QLocale::German));
        QVERIFY(ApplicationThemeManager::instance().selectBackend(UiStyleBackend::Ela));
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        ApplicationThemeManager::instance().applyToApplication();
        QVERIFY(!create(nullptr, nullptr));
        if (qEnvironmentVariableIsSet("SIMDOCK_REQUIRE_NATIVE_DPR2")) {
            QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
            QCOMPARE(qApp->primaryScreen()->devicePixelRatio(), 2.0);
        }
    }
    void hostIsolationAndState() {
        const auto identity = qApp->applicationName(); const auto organization = qApp->organizationName();
        const auto version = qApp->applicationVersion(); const auto palette = qApp->palette();
        const auto font = qApp->font(); const auto sheet = qApp->styleSheet(); auto* style = qApp->style();
        const auto locale = QLocale(); const auto paths = qApp->libraryPaths();
        const auto settingsKeys = QSettings().allKeys();
        const bool quitLast = qApp->quitOnLastWindowClosed();
        QWidget host; host.resize(1250, 760); QVBoxLayout layout(&host);
        auto* widget = create(&host, this); QVERIFY(widget); layout.addWidget(widget); host.show();
        QVERIFY(!widget->isWindow()); QCOMPARE(widget->parentWidget(), &host);
        QCOMPARE(QString::fromLatin1(widget->metaObject()->className()), QStringLiteral("simdock::Workbench"));
        QVERIFY(state(widget)["workspace"].toString().isEmpty()); QVERIFY(closable(widget));
        QTemporaryDir root; fixture(root); QSignalSpy scanned(widget, SIGNAL(scanFinished()));
        QVERIFY(call(widget, "setContext", root.path()).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        QVERIFY(call(widget, "openProject", id).isEmpty());
        const auto saved = state(widget);
        QVERIFY(!call(widget, "setContext", root.filePath("missing")).isEmpty());
        QCOMPARE(state(widget), saved);
        QVERIFY(!call(widget, "openProject", "missing").isEmpty());
        QVERIFY(call(widget, "setContext", {}).isEmpty());
        QVERIFY(state(widget)["workspace"].toString().isEmpty());
        QVERIFY(restore(widget, saved).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 10000);
        QCOMPARE(state(widget)["projectId"].toString(), QString::fromLatin1(id));
        auto invalid = saved; invalid["version"] = 2; QVERIFY(!restore(widget, invalid).isEmpty());
        invalid = saved; invalid["projectId"] = "missing"; QVERIFY(!restore(widget, invalid).isEmpty());
        auto themed = saved; themed["preferences"] = QVariantMap{{"theme/dark", true}, {"waveform/theme", "nord"}};
        QVERIFY(restore(widget, themed).isEmpty());
        QTest::qWait(80);
        QCOMPARE(scanned.size(), 2);
        auto* second = create(&host, nullptr); QVERIFY(second);
        QVERIFY(state(second)["preferences"].toMap().isEmpty()); delete second;
        QTest::qWait(60);
        const auto report = qEnvironmentVariable("SIMDOCK_EMBED_REPORT");
        if (!report.isEmpty()) QVERIFY(host.grab().save(QDir(report).filePath("embedded-workbench.png")));
        delete widget;
        QCOMPARE(qApp->applicationName(), identity); QCOMPARE(qApp->organizationName(), organization);
        QCOMPARE(qApp->applicationVersion(), version); QCOMPARE(qApp->font(), font);
        QCOMPARE(qApp->palette(), palette); QCOMPARE(qApp->styleSheet(), sheet); QCOMPARE(qApp->style(), style);
        QCOMPARE(QLocale(), locale); QCOMPARE(qApp->libraryPaths(), paths);
        QCOMPARE(qApp->quitOnLastWindowClosed(), quitLast); QCOMPARE(QSettings().allKeys(), settingsKeys);
        QCOMPARE(QSettings().value("host/sentinel").toString(), QStringLiteral("untouched"));
    }
    void responsiveGeometry_data() {
        QTest::addColumn<int>("width"); QTest::addColumn<bool>("bounded");
        for (int width : {280, 520, 960, 1250}) for (bool bounded : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(width).arg(bounded ? "bounded" : "resizable"))) << width << bounded;
    }
    void hostThemeAuthority() {
        const auto palette = qApp->palette(); const auto font = qApp->font(); const auto sheet = qApp->styleSheet();
        const auto identity = qApp->applicationName(); const auto org = qApp->organizationName();
        const auto version = qApp->applicationVersion(); auto* style = qApp->style();
        const auto keys = QSettings().allKeys();
        const auto paths = qApp->libraryPaths(); const auto locale = QLocale(); const bool quit = qApp->quitOnLastWindowClosed();
        QWidget host; QVBoxLayout layout(&host); layout.setContentsMargins(0, 0, 0, 0);
        auto* widget = create(&host, nullptr); layout.addWidget(widget); host.resize(960, 760); host.show();
        QVERIFY(widget->metaObject()->indexOfMethod("setDarkTheme(bool)") >= 0);
        QTemporaryDir root; fixture(root); QSignalSpy scanned(widget, SIGNAL(scanFinished()));
        QVERIFY(call(widget, "setContext", root.path()).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        auto* sources = widget->findChild<QListView*>("sourceList"); auto* model = sources->model();
        sources->setCurrentIndex(model->index(0, 0)); const QPersistentModelIndex selected(sources->currentIndex());
        for (bool dark : {true, false, true}) {
            QVERIFY(setHostTheme(widget, dark));
            QTest::qWait(40);
            auto stale = state(widget); stale["preferences"] = QVariantMap{{"theme/dark", !dark}, {"waveform/theme", "nord"}};
            QVERIFY(restore(widget, stale).isEmpty());
            QCOMPARE(widget->palette().color(QPalette::Window).lightness() < 128, dark);
            for (auto* list : {sources, widget->findChild<QListView*>("projectList")})
                for (auto group : {QPalette::Active, QPalette::Inactive})
                    QVERIFY(contrast(list->palette().color(group, QPalette::HighlightedText), list->palette().color(group, QPalette::Highlight)) >= 4.5);
            const auto theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
            host.resize(960, 760); QCoreApplication::processEvents(); capture(&host, "host-theme-" + theme + "-wide");
            host.resize(280, 760); QCoreApplication::processEvents(); section(widget, "workspace");
            capture(&host, "host-theme-" + theme + "-280");
            auto* settings = widget->findChild<QPushButton*>("openSettings"); QVERIFY(reachable(settings, widget));
            QTimer::singleShot(0, widget, [&] {
                auto* dialog = widget->findChild<QDialog*>("settingsDialog"); QVERIFY(dialog);
                QTimer::singleShot(5000, dialog, &QDialog::reject);
                dialogFits(dialog, "host-settings-" + theme);
                auto* appearance = dialog->findChild<QComboBox*>("themeSelector"); QVERIFY(appearance && !appearance->isEnabled());
                QCOMPARE(appearance->currentIndex(), dark ? 1 : 0);
                QVERIFY(setHostTheme(widget, !dark));
                QCOMPARE(appearance->currentIndex(), dark ? 0 : 1);
                QVERIFY(setHostTheme(widget, dark));
                auto* wave = dialog->findChild<QComboBox*>("waveThemeSelector"); QVERIFY(wave && wave->isEnabled());
                wave->setCurrentIndex(wave->findData("nord"));
                // Even a stale/forced field value cannot override host authority on Save.
                appearance->setCurrentIndex(dark ? 0 : 1);
                dialog->findChild<QLineEdit*>("simulatorPath")->setText(qEnvironmentVariable("SIMDOCK_TEST_QUESTA"));
                if (qEnvironmentVariable("SIMDOCK_TEST_QUESTA").isEmpty()) { dialog->reject(); return; }
                auto* save = dialog->findChild<QPushButton*>("saveSettings"); QVERIFY(reachable(save, dialog)); QTest::mouseClick(save, Qt::LeftButton);
            });
            QTest::mouseClick(settings, Qt::LeftButton);
            QCOMPARE(widget->palette().color(QPalette::Window).lightness() < 128, dark);
            QCOMPARE(sources->model(), model); QVERIFY(selected.isValid()); QCOMPARE(sources->currentIndex(), QModelIndex(selected));
            QCOMPARE(state(widget)["preferences"].toMap()["waveform/theme"].toString(), QStringLiteral("nord"));
        }
        QTest::qWait(100); QCOMPARE(scanned.size(), 1);
        // Restoring an old state into a newly opened panel after the host hint.
        auto* reopened = create(&host, nullptr); QVERIFY(setHostTheme(reopened, true));
        auto stale = state(widget); stale["preferences"] = QVariantMap{{"theme/dark", false}};
        QVERIFY(restore(reopened, stale).isEmpty()); QCOMPARE(reopened->palette().color(QPalette::Window).lightness() < 128, true);
        delete reopened;
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
        QCOMPARE(qApp->palette(), palette); QCOMPARE(qApp->font(), font); QCOMPARE(qApp->styleSheet(), sheet); QCOMPARE(qApp->style(), style);
        QCOMPARE(qApp->applicationName(), identity); QCOMPARE(qApp->organizationName(), org); QCOMPARE(qApp->applicationVersion(), version);
        QCOMPARE(QSettings().allKeys(), keys); QCOMPARE(QSettings().value("host/sentinel").toString(), QStringLiteral("untouched"));
        QCOMPARE(qApp->libraryPaths(), paths); QCOMPARE(QLocale(), locale); QCOMPARE(qApp->quitOnLastWindowClosed(), quit);
    }
    void responsiveGeometry() {
        QFETCH(int, width); QFETCH(bool, bounded);
        QTemporaryDir root; fixture(root); QWidget host; QVBoxLayout layout(&host); layout.setContentsMargins(0, 0, 0, 0);
        auto* widget = create(&host, nullptr); QVERIFY(widget); layout.addWidget(widget);
        const int height = qMin(760, host.screen()->availableGeometry().height() - 80);
        if (bounded) host.setFixedSize(width, height); else host.resize(width, height);
        host.show(); QVERIFY(QTest::qWaitForWindowExposed(&host));
        QSignalSpy scanned(widget, SIGNAL(scanFinished()));
        QVERIFY(call(widget, "setContext", root.path()).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        QCOMPARE(host.width(), width); QCOMPARE(widget->width(), width); QVERIFY(widget->minimumSizeHint().width() <= 280);
        QCOMPARE(widget->property("compactLayout").toBool(), width < 900 || height < 600);
        const QList<QPair<QString, QStringList>> controls{
            {"workspace", {"openWorkspace", "newProject", "projectList", "openSettings"}},
            {"sources", {"sourceList", "refreshWorkspace", "moveSourceUp", "moveSourceDown"}},
            {"simulation", {"editStimulus", "dutSelector", "tbFile", "createTb", "chooseTb", "tbTop", "duration", "startSimulation", "stopSimulation"}},
            {"log", {"simulationLog", "clearLog"}}};
        for (const auto& [page, names] : controls) {
            section(widget, page);
            for (const auto& name : names) {
                auto* control = widget->findChild<QWidget*>(name);
                QVERIFY2(reachable(control, widget), qPrintable(QString("%1 at %2: %3x%4").arg(name).arg(width)
                    .arg(control ? control->width() : 0).arg(control ? control->height() : 0)));
            }
            if (width < 900) for (auto* scroll : widget->findChildren<QScrollArea*>())
                if (scroll->objectName().startsWith("workbenchPage") && scroll->isVisible()) QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            if (width < 900 && bounded) capture(&host, QString("host-%1-%2").arg(width).arg(page));
        }
        if (width >= 900 && bounded) capture(&host, QString("host-%1-wide").arg(width));
    }
    void resizePreservesState() {
        QTemporaryDir root; fixture(root); write(root.filePath("extra.sv"), "module extra; endmodule");
        QWidget host; QVBoxLayout layout(&host); layout.setContentsMargins(0, 0, 0, 0);
        auto* widget = create(&host, nullptr); layout.addWidget(widget); host.resize(1250, 700); host.show();
        QSignalSpy scanned(widget, SIGNAL(scanFinished()));
        QVERIFY(call(widget, "setContext", root.path()).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        auto* list = widget->findChild<QListView*>("sourceList"); auto* model = list->model();
        QVERIFY(model->setData(model->index(1, 0), Qt::Checked, Qt::CheckStateRole));
        list->setCurrentIndex(model->index(1, 0));
        widget->findChild<QPushButton*>("moveSourceUp")->click();
        const QPersistentModelIndex selected(list->currentIndex());
        const auto path = selected.data(Qt::UserRole);
        auto* columns = widget->findChild<QSplitter*>("columnSplitter"); columns->setSizes({450, 550});
        const auto wideState = state(widget);
        auto* tb = widget->findChild<QLineEdit*>("tbTop"); tb->setText("tb_keep");
        for (int width : {520, 280, 960, 1250, 280, 520, 1250}) {
            host.resize(width, 700); QCoreApplication::processEvents(); section(widget, "sources");
            const auto saved = state(widget); QVERIFY(restore(widget, saved).isEmpty());
            QCOMPARE(list, widget->findChild<QListView*>("sourceList")); QCOMPARE(list->model(), model);
            QVERIFY(selected.isValid()); QCOMPARE(list->currentIndex(), QModelIndex(selected));
            QCOMPARE(selected.data(Qt::UserRole), path); QCOMPARE(selected.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
            QCOMPARE(tb->text(), QStringLiteral("tb_keep")); QCOMPARE(state(widget)["projectId"].toString(), QString::fromLatin1(id));
            QCOMPARE(state(widget)["layout/compactPage"].toString(), QStringLiteral("sources"));
        }
        QTest::qWait(120); QCOMPARE(scanned.size(), 1);
        // A compact save keeps the last wide splitter proportions, not an empty splitter.
        host.resize(520, 700); QCoreApplication::processEvents();
        auto saved = state(widget); saved["layout/columns"] = wideState["layout/columns"];
        QVERIFY(restore(widget, saved).isEmpty()); host.resize(1250, 700); QCoreApplication::processEvents();
        QCOMPARE(state(widget)["layout/columns"], wideState["layout/columns"]);
        QVERIFY(reachable(widget->findChild<QPushButton*>("startSimulation"), widget));
    }
    void narrowWorkspaceActions_data() {
        QTest::addColumn<int>("width"); QTest::newRow("280") << 280; QTest::newRow("520") << 520;
    }
    void narrowWorkspaceActions() {
        QFETCH(int, width);
        QTemporaryDir root; fixture(root); write(root.filePath("extra.sv"), "module extra; endmodule");
        QWidget host; QVBoxLayout layout(&host); layout.setContentsMargins(0, 0, 0, 0);
        auto* widget = create(&host, nullptr); layout.addWidget(widget); host.resize(width, 700); host.show();
        QSignalSpy scanned(widget, SIGNAL(scanFinished())); section(widget, "workspace");
        auto* open = widget->findChild<QPushButton*>("openWorkspace"); QVERIFY(reachable(open, widget));
        QTimer::singleShot(0, widget, [&] {
            auto* dialog = widget->findChild<QFileDialog*>(); QVERIFY(dialog);
            QTimer::singleShot(5000, dialog, &QDialog::reject);
            dialog->setDirectory(root.path()); QMetaObject::invokeMethod(dialog, "accept");
        });
        QTest::mouseClick(open, Qt::LeftButton); QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        auto* add = widget->findChild<QPushButton*>("newProject"); QVERIFY(reachable(add, widget));
        QTimer::singleShot(0, widget, [&] {
            auto* dialog = widget->findChild<QDialog*>("newProjectDialog"); QVERIFY(dialog);
            QTimer::singleShot(5000, dialog, &QDialog::reject);
            dialogFits(dialog, QString("new-project-%1").arg(width));
            dialog->findChild<QLineEdit*>("projectNameInput")->setText("Narrow test");
            auto* create = dialog->findChild<QPushButton*>("confirmCreateProject");
            QVERIFY(reachable(create, dialog)); QTest::mouseClick(create, Qt::LeftButton);
        });
        QTest::mouseClick(add, Qt::LeftButton);
        auto* projects = widget->findChild<QListView*>("projectList"); QCOMPARE(projects->model()->rowCount(), 2);
        QCOMPARE(projects->currentIndex().data().toString(), QStringLiteral("Narrow test"));
        section(widget, "sources"); auto* sources = widget->findChild<QListView*>("sourceList");
        QVERIFY(reachable(sources, widget)); auto* model = sources->model(); QCOMPARE(model->rowCount(), 2);
        for (int row = 0; row < 2; ++row) {
            sources->setCurrentIndex(model->index(row, 0)); sources->setFocus();
            QTest::keyClick(sources, Qt::Key_Space);
            QCOMPARE(model->index(row, 0).data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
        }
        const auto moved = sources->currentIndex().data(Qt::UserRole);
        auto* up = widget->findChild<QPushButton*>("moveSourceUp"); QVERIFY(reachable(up, widget)); QTest::mouseClick(up, Qt::LeftButton);
        QCOMPARE(model->index(0, 0).data(Qt::UserRole), moved);
        auto* down = widget->findChild<QPushButton*>("moveSourceDown"); QVERIFY(reachable(down, widget)); QTest::mouseClick(down, Qt::LeftButton);
        QCOMPARE(model->index(1, 0).data(Qt::UserRole), moved);
        auto* rescan = widget->findChild<QPushButton*>("refreshWorkspace"); QVERIFY(reachable(rescan, widget));
        QTest::mouseClick(rescan, Qt::LeftButton); QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 2, 10000);
        section(widget, "workspace"); QVERIFY(reachable(projects, widget));
        const auto original = projects->model()->match(projects->model()->index(0, 0), Qt::DisplayRole, QStringLiteral("Embedded test"), 1, Qt::MatchExactly);
        QVERIFY(!original.isEmpty()); projects->scrollTo(original.first());
        QTest::mouseClick(projects->viewport(), Qt::LeftButton, Qt::NoModifier, projects->visualRect(original.first()).center());
        QCOMPARE(state(widget)["projectId"].toString(), QString::fromLatin1(id));
        auto* settings = widget->findChild<QPushButton*>("openSettings"); QVERIFY(reachable(settings, widget));
        QTimer::singleShot(0, widget, [&] {
            auto* dialog = widget->findChild<QDialog*>("settingsDialog"); QVERIFY(dialog);
            QTimer::singleShot(5000, dialog, &QDialog::reject);
            dialogFits(dialog, QString("settings-%1").arg(width));
            const auto simulator = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
            if (simulator.isEmpty()) { dialog->reject(); return; }
            dialog->findChild<QLineEdit*>("simulatorPath")->setText(simulator);
            auto* wave = dialog->findChild<QComboBox*>("waveThemeSelector"); wave->setCurrentIndex(wave->findData("nord"));
            auto* save = dialog->findChild<QPushButton*>("saveSettings"); QVERIFY(reachable(save, dialog)); QTest::mouseClick(save, Qt::LeftButton);
        });
        QTest::mouseClick(settings, Qt::LeftButton);
        if (!qEnvironmentVariable("SIMDOCK_TEST_QUESTA").isEmpty())
            QCOMPARE(state(widget)["preferences"].toMap()["waveform/theme"].toString(), QStringLiteral("nord"));
    }
    void rapidContextsAndOwnedWorker() {
        QTemporaryDir first, second; fixture(first); fixture(second);
        for (int i = 0; i < 100; ++i) write(first.filePath(QString("extra%1.sv").arg(i)), "module extra; endmodule");
        // A saturated host/global pool must not starve the embedded scanner.
        QSemaphore release; auto* pool = QThreadPool::globalInstance();
        const int previousLimit = pool->maxThreadCount(); pool->setMaxThreadCount(1);
        auto blocked = QtConcurrent::run([&] { release.acquire(); });
        struct Cleanup { QSemaphore& semaphore; QFuture<void>& job; QThreadPool* pool; int limit;
            ~Cleanup() { semaphore.release(); job.waitForFinished(); pool->setMaxThreadCount(limit); }
        } cleanup{release, blocked, pool, previousLimit};
        QWidget host; QPointer<QWidget> widget(create(&host, this)); QVERIFY(widget);
        QSignalSpy scanned(widget, SIGNAL(scanFinished()));
        for (int i = 0; i < 20; ++i) QVERIFY(call(widget, "setContext", i % 2 ? second.path() : first.path()).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        auto* list = widget->findChild<QListView*>("sourceList"); QVERIFY(list);
        QCOMPARE(list->model()->rowCount(), 1);
        QVERIFY(call(widget, "setContext", first.path()).isEmpty());
        QVERIFY(closable(widget));
        delete widget; QVERIFY(widget.isNull());
        QTest::qWait(30); // No late callback may touch a destroyed widget.
        QVERIFY(!blocked.isFinished());
    }
    void embeddedStimulusChecksWorkflow_data() {
        QTest::addColumn<int>("width");
        for (int width : {280, 520, 1250}) QTest::newRow(qPrintable(QString::number(width))) << width;
    }
    void embeddedStimulusChecksWorkflow() {
        QFETCH(int, width);
        QTemporaryDir root; fixture(root); QWidget host; QVBoxLayout layout(&host);
        layout.setContentsMargins(0, 0, 0, 0);
        auto* widget = create(&host, nullptr); QVERIFY(widget); layout.addWidget(widget);
        host.resize(width, 700); host.show(); QSignalSpy scanned(widget, SIGNAL(scanFinished()));
        QVERIFY(call(widget, "setContext", root.path()).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        section(widget, "simulation");
        auto* edit = widget->findChild<QPushButton*>("editStimulus"); QVERIFY(edit && edit->isEnabled());
        QVERIFY(reachable(edit, widget));
        QTimer editorPoll;
        editorPoll.setInterval(10);
        connect(&editorPoll, &QTimer::timeout, widget, [&] {
            auto* dialog = widget->findChild<QDialog*>("stimulusDialog");
            if (!dialog || !dialog->isVisible()) return;
            editorPoll.stop();
            QTimer::singleShot(5000, dialog, &QDialog::reject);
            QVERIFY(!closable(widget)); QVERIFY(!call(widget, "setContext", {}).isEmpty());
            QVERIFY(!call(widget, "openProject", {}).isEmpty());
            auto* editor = dialog->findChild<QWidget*>("WaveStimulusEditor"); QVERIFY(editor && editor->autoFillBackground());
            dialogFits(dialog, QString("stimulus-%1").arg(width));
            auto* timing = dialog->findChild<QToolButton*>("stimulusTiming"); QVERIFY(reachable(timing, dialog));
            QTest::mouseClick(timing, Qt::LeftButton); QTest::qWait(200);
            for (const auto* name : {"stimulusClock", "stimulusReset", "stimulusPeriod", "stimulusDuration", "applyStimulusTiming"})
                QVERIFY2(reachable(dialog->findChild<QWidget*>(name), dialog), name);
            dialogFits(dialog, QString("stimulus-timing-%1").arg(width));
            auto* checksButton = dialog->findChild<QPushButton*>("stimulusChecks");
            QVERIFY(reachable(checksButton, dialog));
            QTimer::singleShot(0, dialog, [&] {
                auto* checks = dialog->findChild<QDialog*>("scoreboardDialog"); QVERIFY(checks);
                QTimer::singleShot(3000, checks, &QDialog::reject);
                auto* kind = checks->findChild<QComboBox*>("scoreboardKind"); QVERIFY(kind);
                kind->setCurrentIndex(kind->findData("uart_tx"));
                checks->findChild<QSpinBox*>("scoreboard_baud")->setValue(10000000);
                dialogFits(checks, QString("checks-%1").arg(width));
                auto* apply = checks->findChild<QPushButton*>("applyScoreboard");
                QVERIFY(reachable(apply, checks)); QTest::mouseClick(apply, Qt::LeftButton);
            });
            QTest::mouseClick(checksButton, Qt::LeftButton);
            auto* save = dialog->findChild<QPushButton*>("saveStimulus");
            QVERIFY(reachable(save, dialog)); QTest::mouseClick(save, Qt::LeftButton);
        });
        editorPoll.start(); QTest::mouseClick(edit, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!editorPoll.isActive(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!widget->findChild<QDialog*>("stimulusDialog"), 10000);
        QVERIFY(closable(widget));
        QFile project(root.filePath(QString(".simdock/projects/%1.json").arg(id)));
        QVERIFY(project.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(project.readAll()).object();
        QCOMPARE(saved["stimulus"].toObject()["scoreboard"].toObject()["kind"].toString(), QStringLiteral("uart_tx"));
        QVERIFY(QFileInfo::exists(root.filePath(saved["tbFile"].toString())));
        QVERIFY(widget->findChild<QPushButton*>("startSimulation")->isEnabled());
    }
    void destroyWithOpenDialog_data() { embeddedStimulusChecksWorkflow_data(); }
    void destroyWithOpenDialog() {
        QFETCH(int, width);
        for (const auto* action : {"openSettings", "newProject", "createTb", "editStimulus"}) {
            QTemporaryDir root; fixture(root); QWidget host; QVBoxLayout layout(&host);
            layout.setContentsMargins(0, 0, 0, 0); host.resize(width, 700);
            QPointer<QWidget> widget(create(&host, nullptr)); QVERIFY(widget); layout.addWidget(widget);
            host.show(); QSignalSpy scanned(widget, SIGNAL(scanFinished()));
            QVERIFY(call(widget, "setContext", root.path()).isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
            section(widget, QByteArray(action) == "openSettings" || QByteArray(action) == "newProject" ? "workspace" : "simulation");
            auto* button = widget->findChild<QPushButton*>(QString::fromLatin1(action)); QVERIFY(button && button->isEnabled());
            QVERIFY(reachable(button, widget));
            QTimer destroyPoll;
            destroyPoll.setInterval(10);
            connect(&destroyPoll, &QTimer::timeout, &host, [&] {
                if (!widget) return;
                bool visible = false;
                for (auto* dialog : widget->findChildren<QDialog*>()) visible |= dialog->isVisible();
                if (!visible) return;
                QVERIFY(!closable(widget)); destroyPoll.stop(); delete widget;
            });
            destroyPoll.start(); QTest::mouseClick(button, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(widget.isNull(), 10000);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    void simulationLifecycle_data() { embeddedStimulusChecksWorkflow_data(); }
    void simulationLifecycle() {
        QFETCH(int, width);
        const auto simulator = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (simulator.isEmpty()) QSKIP("Set SIMDOCK_TEST_QUESTA for actual process lifecycle validation.");
        QTemporaryDir root; fixture(root); QWidget host; QVBoxLayout layout(&host);
        layout.setContentsMargins(0, 0, 0, 0); host.resize(width, 700);
        QPointer<QWidget> widget(create(&host, nullptr)); QVERIFY(widget); layout.addWidget(widget); host.show();
        QSignalSpy scanned(widget, SIGNAL(scanFinished()));
        auto saved = state(widget); saved["workspace"] = root.path(); saved["projectId"] = id;
        saved["preferences"] = QVariantMap{{"simulator/path", simulator}};
        QVERIFY(restore(widget, saved).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, 10000);
        section(widget, "simulation");
        auto* generate = widget->findChild<QPushButton*>("createTb"); QVERIFY(generate && generate->isEnabled());
        QVERIFY(reachable(generate, widget));
        QTimer::singleShot(0, widget, [&] {
            auto* dialog = widget->findChild<QDialog*>(); QVERIFY(dialog);
            QTimer::singleShot(3000, dialog, &QDialog::reject);
            dialogFits(dialog, QString("demo-tb-%1").arg(width));
            for (auto* button : dialog->findChildren<QPushButton*>())
                if (button->text() == "Create file") { QVERIFY(reachable(button, dialog)); QTest::mouseClick(button, Qt::LeftButton); return; }
        });
        QTest::mouseClick(generate, Qt::LeftButton);
        auto* choose = widget->findChild<QPushButton*>("chooseTb"); QVERIFY(reachable(choose, widget));
        const auto tbFile = widget->findChild<QLineEdit*>("tbFile")->text(); QVERIFY(!tbFile.isEmpty());
        QTimer::singleShot(0, widget, [&] {
            auto* dialog = widget->findChild<QFileDialog*>(); QVERIFY(dialog);
            QTimer::singleShot(5000, dialog, &QDialog::reject);
            dialog->selectFile(root.filePath(tbFile)); QMetaObject::invokeMethod(dialog, "accept");
        });
        QTest::mouseClick(choose, Qt::LeftButton);
        auto* run = widget->findChild<QPushButton*>("startSimulation");
        auto* stop = widget->findChild<QPushButton*>("stopSimulation"); QVERIFY(run && run->isEnabled() && stop);
        QObject* session = nullptr;
        for (auto* child : widget->findChildren<QObject*>())
            if (QByteArray(child->metaObject()->className()) == "simdock::QuestaSession") session = child;
        QVERIFY(session);
        QVERIFY(reachable(run, widget)); QTest::mouseClick(run, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(session->property("processId").toLongLong() != 0, 15000);
        QVERIFY(!closable(widget)); QVERIFY(!call(widget, "setContext", {}).isEmpty());
        auto* log = widget->findChild<QPlainTextEdit*>("simulationLog"); QVERIFY(log);
        section(widget, "log"); QVERIFY(reachable(log, widget));
        // A cold Questa GUI can take most of the session's 120 s deadline on
        // this host. Wait through that deadline and include its log on failure.
        QTRY_VERIFY2_WITH_TIMEOUT(log->toPlainText().contains("Run completed"),
                                 qPrintable(log->toPlainText()), 130000);
        QVERIFY(!closable(widget));
        capture(&host, QString("run-completed-%1").arg(width));
        section(widget, "simulation"); QVERIFY(reachable(stop, widget)); QTest::mouseClick(stop, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(session->property("processId").toLongLong(), 0, 10000);
        QVERIFY(closable(widget));
        section(widget, "log"); auto* clear = widget->findChild<QPushButton*>("clearLog");
        QVERIFY(reachable(clear, widget)); QTest::mouseClick(clear, Qt::LeftButton); QCOMPARE(log->toPlainText(), QString());
        section(widget, "simulation"); QVERIFY(reachable(run, widget)); QTest::mouseClick(run, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(session->property("processId").toLongLong() != 0, 15000);
        const auto pid = session->property("processId").toLongLong();
#ifdef Q_OS_WIN
        const auto process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid)); QVERIFY(process);
#endif
        const auto report = qEnvironmentVariable("SIMDOCK_EMBED_REPORT");
        if (!report.isEmpty()) write(QDir(report).filePath(QString("destroyed-session-%1-pid.txt").arg(width)), QByteArray::number(pid));
        delete widget; QVERIFY(widget.isNull()); // Destructor kills its own process tree.
#ifdef Q_OS_WIN
        QCOMPARE(WaitForSingleObject(process, 3000), DWORD(WAIT_OBJECT_0)); CloseHandle(process);
#endif
    }
};
QTEST_MAIN(WorkbenchHostTest)
#include "workbench_host_test.moc"
