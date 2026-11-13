#include "applicationthememanager.h"
#include "analysisscheduler.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"
#include "mainwindow.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "editoractioncontextservice.h"
#include "testuistyle.h"
#include "navigationpanecoordinator.h"
#include "navigationwidget.h"
#include "navigationrowstyle.h"
#include "designhierarchymodel.h"
#include "liveinsightscontextprovider.h"
#include "signaltracepanel.h"
#include "panellayoutcontroller.h"
#include "navigationmanager.h"
#include "insightvisualstyle.h"
#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "contextrail.h"
#include "filecommandcoordinator.h"
#include "globalcontrolcoordinator.h"
#include "version.h"
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QStackedWidget>
#include <QTreeWidgetItemIterator>
#include <QTreeView>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QElapsedTimer>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QTabWidget>
#include <functional>

namespace {
bool waitForContextIdle(MainWindow& window, ContextWorkspaceController& controller,
                        const QDeadlineTimer& deadline)
{
    if (controller.canCloseResources()) return true;
    if (deadline.hasExpired()) return false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QSet<QFutureWatcherBase*> observed;
    std::function<void()> observe = [&] {
        if (controller.canCloseResources()) { loop.quit(); return; }
        for (auto* watcher : window.findChildren<QFutureWatcherBase*>()) {
            if (observed.contains(watcher)) continue;
            observed.insert(watcher);
            QObject::connect(watcher, &QFutureWatcherBase::finished, &loop, observe, Qt::QueuedConnection);
            QObject::connect(watcher, &QObject::destroyed, &loop, [&, watcher] { observed.remove(watcher); });
        }
    };
    // The frozen xIPs component clears its busy state in watcher completion handlers.
    // Observe after those handlers; a follow-on detail load shares this deadline.
    observe();
    timeout.start(int(deadline.remainingTime()));
    loop.exec();
    return controller.canCloseResources();
}
class StyleChangeObserver final : public QObject {
public:
    int changes = 0;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::StyleChange) ++changes;
        return QObject::eventFilter(watched, event);
    }
};

// Release/change a real read lease only after the OS has rejected the atomic
// replacement. This synchronizes the race without a timer or a test retry.
class SaveFailureObserver {
public:
    SaveFailureObserver(QString target, std::function<void()> onFirstFailure)
        : target(std::move(target)), action(std::move(onFirstFailure)) {
        active = this;
        previous = qInstallMessageHandler(&message);
    }
    ~SaveFailureObserver() { qInstallMessageHandler(previous); active = nullptr; }
    int failures = 0;
    bool renameFailure = true;
private:
    static void message(QtMsgType type, const QMessageLogContext& context, const QString& text) {
        auto* self = active;
        if (self && text.startsWith("atomic save failure") && text.contains(self->target)) {
            ++self->failures;
            self->renameFailure &= text.contains("stage commit error 10");
            if (self->failures == 1 && self->action) self->action();
        }
        if (self && self->previous) self->previous(type, context, text);
    }
    inline static SaveFailureObserver* active = nullptr;
    QtMessageHandler previous = nullptr;
    QString target;
    std::function<void()> action;
};

bool write(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
int workspaceTab(QTabBar* bar, const QString& path) {
    for (int i = 0; i < bar->count(); ++i) if (bar->tabData(i).toString() == path) return i;
    return -1;
}
bool closeWorkspace(QTabBar* bar, const QString& path) {
    const int index = workspaceTab(bar, path);
    if (index < 0) return false;
    const QPoint point = bar->tabRect(index).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, bar->mapToGlobal(point));
    QApplication::sendEvent(bar, &event);
    const bool shown=QTest::qWaitFor([] { return qobject_cast<QMenu*>(QApplication::activePopupWidget()) != nullptr; }, 1000);
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QAction* action=nullptr;
    if (menu) for (auto* candidate:menu->actions())
        if (candidate->objectName()=="closeWorkspaceAction") action=candidate;
    if (!shown || !action) {
        qWarning()<<"workspace close menu"<<index<<point<<bar->geometry()<<bar->tabAt(point)
            <<bar->contextMenuPolicy()<<QApplication::activePopupWidget();
        return false;
    }
    action->trigger();
    menu->close();
    return true;
}
}

class WorkspaceSwitcherTest final : public QObject {
    Q_OBJECT
private slots:
    void legacyIgnoredDirectoriesUsesProtectedConfigurationCommit() {
        QTemporaryDir fixture;
        QVERIFY(write(fixture.filePath("top.sv"), "module top; endmodule\n"));
        WorkspaceManager manager;
        manager.setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(manager.openWorkspace(fixture.path()));
        manager.setIgnoredDirectories({"generated"});
        const auto before = manager.ignoredDirectories();
        const auto path = WorkspaceConfigurationService::projectFilePath(fixture.path());
        QVERIFY(write(path, "{broken"));
        manager.setIgnoredDirectories({"replacement"});
        QCOMPARE(manager.ignoredDirectories(), before);
        QFile preserved(path); QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("{broken"));
    }
    void invalidConfigurationPreservesActiveWorkspace()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString a = fixture.filePath("a"), b = fixture.filePath("b");
        QVERIFY(write(a + "/a.sv", "module a; endmodule\n"));
        QVERIFY(write(b + "/b.sv", "module b; endmodule\n"));
        WorkspaceManager manager;
        manager.setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(manager.openWorkspace(a));
        QSignalSpy errors(&manager, &WorkspaceManager::workspaceActivationFailed);
        const auto path = WorkspaceConfigurationService::projectFilePath(b);
        QVERIFY(write(path, "{broken"));
        QVERIFY(!manager.openWorkspace(b));
        QCOMPARE(manager.getWorkspacePath(), a);
        QCOMPARE(manager.workspaceEntries().size(), 1);
        QCOMPARE(errors.size(), 1);
        QVERIFY(QFile::remove(path));
        QVERIFY(manager.openWorkspace(b));
        QVERIFY(manager.switchWorkspace(0));
        QVERIFY(write(path, "{broken"));
        QVERIFY(!manager.switchWorkspace(1));
        QCOMPARE(manager.getWorkspacePath(), a);
        QVERIFY(!manager.closeWorkspace(0));
        QCOMPARE(manager.getWorkspacePath(), a);
        QCOMPARE(manager.workspaceEntries().size(), 2);
        QVERIFY(QFile::remove(path));
        QVERIFY(manager.switchWorkspace(1));
        QCOMPARE(manager.getWorkspacePath(), b);
    }

    void atomicSaveReadLease_data() {
        QTest::addColumn<QString>("outcome");
        for (const auto* outcome : {"released", "persistent", "changed"})
            QTest::newRow(outcome) << QString::fromLatin1(outcome);
    }

    void atomicSaveReadLease() {
#ifndef Q_OS_WIN
        QSKIP("Windows read handles can exclude atomic replacement");
#else
        QFETCH(QString, outcome);
        QTemporaryDir files; QVERIFY(files.isValid());
        const QString path = files.filePath("scratch.sv");
        const QByteArray original("module original; endmodule\n");
        const QByteArray external("module changed_externally; endmodule\n");
        QVERIFY(write(path, original));
        QTabWidget widget;
        TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(files.filePath("recovery")));
        QVERIFY(tabs.openFileInTab(path));
        auto* editor = tabs.getCurrentEditor();
        editor->insertPlainText("// local edit\n");
        const auto localText = editor->toPlainText();
        auto* document = tabs.sharedDocumentForEditor(editor);
        QFile reader(path); QVERIFY(reader.open(QIODevice::ReadOnly));
        bool replacementWritten = false;
        SaveFailureObserver observed(path, [&] {
            if (outcome != "persistent") reader.close();
            if (outcome == "changed") replacementWritten = write(path, external);
        });
        QSignalSpy failed(&tabs, &TabManager::fileSaveFailed);
        QSignalSpy saved(&tabs, &TabManager::fileSaved);
        QElapsedTimer elapsed; elapsed.start();
        const bool succeeded = tabs.saveCurrentTab();
        QVERIFY(observed.failures > 0);
        QVERIFY(observed.failures <= 4);
        QVERIFY(observed.renameFailure);
        QVERIFY2(elapsed.elapsed() < 2000, "A persistent lease must not hang saving");
        reader.close();
        QCOMPARE(editor->toPlainText(), localText);
        QFile onDisk(path); QVERIFY(onDisk.open(QIODevice::ReadOnly));
        const auto bytes = onDisk.readAll(); onDisk.close();
        tabs.flushCrashRecovery();
        const auto recovery = tabs.listCrashRecoveryCandidates(tabs.temporaryRecoveryWorkspace());
        if (outcome == "released") {
            QVERIFY(succeeded);
            QCOMPARE(failed.count(), 0); QCOMPARE(saved.count(), 1);
            QVERIFY(!document->dirty());
            QCOMPARE(bytes, localText.toUtf8()); // Preserve the original LF encoding contract.
            QVERIFY(recovery.candidates.isEmpty());
        } else {
            QVERIFY(!succeeded);
            QCOMPARE(failed.count(), 1); QCOMPARE(saved.count(), 0);
            QVERIFY(document->dirty());
            QCOMPARE(recovery.candidates.size(), 1);
            QCOMPARE(bytes, outcome == "changed" ? external : original);
            if (outcome == "changed") {
                QVERIFY(replacementWritten);
                QCOMPARE(document->externalState(), SharedDocumentExternalState::Conflict);
            }
        }
        QCOMPARE(QDir(files.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"scratch.sv"});
#endif
    }

    void atomicSaveRequiresConflictGuard() {
#ifndef Q_OS_WIN
        QSKIP("Windows rename recovery policy");
#else
        QTemporaryDir files; QVERIFY(files.isValid());
        const QString path = files.filePath("scratch.sv");
        const QByteArray original("module untouched; endmodule\n");
        QVERIFY(write(path, original));
        QFile reader(path); QVERIFY(reader.open(QIODevice::ReadOnly));
        SaveFailureObserver observed(path, {});
        QString reason;
        QByteArray raw("stale"), logical("stale");
        QVERIFY(!TabFileIo().writeTextFile(nullptr, path, "replacement", &reason, &raw, &logical));
        QCOMPARE(observed.failures, 1);
        QVERIFY(observed.renameFailure && !reason.isEmpty());
        QVERIFY(raw.isEmpty() && logical.isEmpty());
        QCOMPARE(reader.readAll(), original);
        reader.close();
        const QString directory = files.filePath("directory"); QVERIFY(QDir().mkpath(directory));
        int guards = 0;
        QVERIFY(!TabFileIo().writeTextFile(nullptr, directory, "replacement", &reason, &raw, &logical,
            [&](QString*) { ++guards; return true; }));
        QCOMPARE(guards, 1); // The initial identity check runs; opening errors never retry.
#endif
    }

    void workspaceIconsAreVisibleAndClickable_data() {
        QTest::addColumn<int>("count");
        QTest::addColumn<bool>("dark");
        for (int count : {2, 3})
            for (bool dark : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(count).arg(dark ? "dark" : "light"))) << count << dark;
    }

    void workspaceIconsAreVisibleAndClickable() {
        QFETCH(int, count); QFETCH(bool, dark);
        QTemporaryDir files; QVERIFY(files.isValid());
        MainWindow window; window.resize(1050, 660); window.show();
        auto* sessions = window.findChild<WorkspaceSessionCoordinator*>(); QVERIFY(sessions);
        auto* bar = window.findChild<QTabBar*>("workspaceIconTabs"); QVERIFY(bar);
        auto* strip = window.findChild<QWidget*>("workspaceIconStrip"); QVERIFY(strip);
        auto* title = window.findChild<QWidget*>("workspaceTitleBar"); QVERIFY(title);
        auto* brand = window.findChild<QLabel*>("workspaceFilePath"); QVERIFY(brand);
        auto* open = window.findChild<QToolButton*>("openWorkspaceButton"); QVERIFY(open);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        QStringList paths;
        for (int i = 0; i < count; ++i) {
            const auto path = files.filePath(QString("workspace-%1").arg(i));
            QVERIFY(QDir().mkpath(path)); QVERIFY(sessions->openWorkspace(path)); paths.append(path);
        }
        QCOMPARE(bar->count(), count);
        window.setWindowTitle(QStringLiteral("ZeroSlack — %1").arg(QLatin1String(APP_VERSION)));
        for (int width : {1050, 760, 600}) {
            const auto theme = dark ? ThemeMode::Dark : ThemeMode::Light;
            // Construction and workspace activation can restore theme settings.
            ApplicationThemeManager::instance().setMode(theme);
            window.resize(width, 660); QTest::qWait(80);
            QCOMPARE(ApplicationThemeManager::instance().mode(), theme);
            QTRY_COMPARE(title->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
            QCOMPARE(title->palette().color(QPalette::Window).lightness() < 128, dark);
            QVERIFY(brand->isVisible());
            QCOMPARE(brand->text(), QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
            QVERIFY(brand->width() >= brand->sizeHint().width());
            QVERIFY(brand->visibleRegion().contains(brand->rect()));
            const QRect brandRect(brand->mapTo(&window, QPoint()), brand->size());
            const QRect stripRect(strip->mapTo(&window, QPoint()), strip->size());
            QVERIFY(title->geometry().contains(brandRect));
            QVERIFY(brandRect.right() < stripRect.left());
            const QString evidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
            if (!evidence.isEmpty()) {
                QDir().mkpath(evidence);
                QVERIFY(window.grab().save(evidence + QString("/workspaces-%1-%2-%3.png")
                    .arg(count).arg(dark ? "dark" : "light").arg(width)));
            }
            for (auto* scroll : bar->findChildren<QToolButton*>())
                QVERIFY2(!scroll->isVisible(), "2/3 workspace icons must fit without overflow arrows");
            for (int i = 0; i < count; ++i) {
                const QRect rect = bar->tabRect(i);
                QVERIFY2(bar->rect().contains(rect), "The entire tab must be inside the visible tab bar");
                QVERIFY2(bar->visibleRegion().contains(rect.adjusted(1, 1, -1, -1)), "The tab must not be clipped");
                const QPoint point = bar->mapTo(&window, rect.center());
                QCOMPARE(window.childAt(point), static_cast<QWidget*>(bar));
                QTest::mouseClick(bar, Qt::LeftButton, {}, rect.center());
                QTRY_COMPARE(window.workspaceManager->getWorkspacePath(), paths.at(i));
                QCOMPARE(brand->text(), QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
            }
            int controlsLeft = window.width();
            for (const auto* name : {"windowMinimizeButton", "windowMaximizeButton", "windowCloseButton"}) {
                auto* button = window.findChild<QAbstractButton*>(name); QVERIFY(button && button->isVisible());
                const QRect rect(button->mapTo(&window, QPoint()), button->size());
                QVERIFY(window.rect().contains(rect));
                QVERIFY(!rect.intersects(brandRect));
                QVERIFY(!rect.intersects(stripRect));
                QCOMPARE(window.childAt(rect.center()), static_cast<QWidget*>(button));
                controlsLeft = qMin(controlsLeft, rect.left());
            }
            const int stripRight = strip->mapTo(&window, strip->rect().topRight()).x();
            QVERIFY2(controlsLeft - stripRight >= 80, "Keep a usable blank title area for dragging the window");
            int blankRun = 0, longestBlank = 0;
            // The fixed brand label is draggable too. The right-hand region
            // now hosts Run, Settings and editing time, not only blank space.
            auto* run = title->findChild<QAbstractButton*>("startSimulation"); QVERIFY(run);
            auto* editingTime = title->findChild<QWidget*>("editingTimeWidget"); QVERIFY(editingTime);
            auto* accessory = editingTime->parentWidget();
            QVERIFY(accessory && title->isAncestorOf(accessory));
            QVERIFY(accessory->isAncestorOf(run));
            for (int x = title->geometry().left(); x < controlsLeft; ++x) {
                auto* hit = window.childAt(QPoint(x,title->mapTo(&window,title->rect().center()).y()));
                bool draggable = hit == title || (hit && title->isAncestorOf(hit));
                for (auto* widget = hit; widget && widget != title; widget = widget->parentWidget())
                    if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QLineEdit*>(widget)
                        || widget == strip || widget == accessory) draggable = false;
                blankRun = draggable ? blankRun + 1 : 0;
                longestBlank = qMax(longestBlank,blankRun);
            }
            QVERIFY2(longestBlank >= 80,"Keep 80 contiguous pixels of draggable title area outside controls");
            QVERIFY(open->isVisible());
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void explicitOpenExistingWorkspaceExpandsNavigation() {
        QTemporaryDir files; QVERIFY(files.isValid());
        MainWindow window; window.resize(1050, 660); window.show();
        auto* sessions = window.findChild<WorkspaceSessionCoordinator*>();
        auto* navigation = window.findChild<NavigationPaneCoordinator*>();
        auto* commands = window.findChild<FileCommandCoordinator*>();
        GlobalControlCoordinator* control = nullptr;
        for (auto* child : window.children())
            if (auto* candidate = dynamic_cast<GlobalControlCoordinator*>(child)) control = candidate;
        QVERIFY(sessions && navigation && commands && control);
        auto* search = window.findChild<QLineEdit*>("globalControlSearchEdit"); QVERIFY(search);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        QStringList paths;
        for (int i = 0; i < 2; ++i) {
            auto path = files.filePath(QString::number(i)); QVERIFY(QDir().mkpath(path));
            QVERIFY(sessions->openWorkspace(path)); navigation->setExpanded(false, false); paths.append(path);
        }
        QString selection;
        int selected = 0;
        commands->setWorkspaceDirectorySelector([&](QWidget*) { ++selected; return selection; });
        auto runOpenCommand = [&] {
            control->open(); search->setText("ow 1"); QTest::keyClick(search, Qt::Key_Return);
        };
        // Both the currently active root and an inactive existing root use the explicit command path.
        for (const auto& path : {paths.last(), paths.first()}) {
            navigation->setExpanded(false, false); selection = path;
            const int before = selected; runOpenCommand(); QCOMPARE(selected, before + 1);
            QTRY_COMPARE(window.workspaceManager->getWorkspacePath(), path);
            QTRY_VERIFY(navigation->isExpanded() && navigation->dock()->isVisible());
            QCOMPARE(window.workspaceManager->workspaceEntries().size(), 2);
        }
        navigation->setExpanded(false, false); selection.clear();
        runOpenCommand(); QVERIFY(!navigation->isExpanded());
        auto* bar = window.findChild<QTabBar*>("workspaceIconTabs"); QVERIFY(bar);
        QVERIFY(sessions->switchWorkspace(1)); navigation->setExpanded(false, false);
        QVERIFY(sessions->switchWorkspace(0)); navigation->setExpanded(false, false);
        QTest::qWait(60);
        const QRect target = bar->tabRect(workspaceTab(bar, paths.last()));
        QVERIFY(bar->rect().contains(target));
        QTest::mouseClick(bar, Qt::LeftButton, {}, target.center());
        QTRY_COMPARE(window.workspaceManager->getWorkspacePath(), paths.last());
        QVERIFY(!navigation->isExpanded());
    }

    void workspaceSwitchPreservesUnchangedApplicationStyle() {
        QTemporaryDir files; QVERIFY(files.isValid());
        const QString first = files.filePath("first"), second = files.filePath("second");
        QVERIFY(QDir().mkpath(first)); QVERIFY(QDir().mkpath(second));
        MainWindow window;
        window.resize(780, 600); window.show();
        auto* sessions = window.findChild<WorkspaceSessionCoordinator*>();
        auto* strip = window.findChild<QTabBar*>("workspaceIconTabs");
        QVERIFY(sessions && strip);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(sessions->openWorkspace(first));
        QVERIFY(sessions->openWorkspace(second));
        QTRY_COMPARE(window.workspaceManager->getWorkspacePath(), second);

        QWidget styleProbe;
        styleProbe.ensurePolished();
        StyleChangeObserver observer;
        styleProbe.installEventFilter(&observer);
        auto& themes = ApplicationThemeManager::instance();
        const ThemeMode original = themes.mode();
        const QPalette originalPalette = QApplication::palette();
        const QString originalStyleSheet = qApp->styleSheet();
        const ThemeMode changed = isDarkTheme(original) ? ThemeMode::Light : ThemeMode::Dark;
        themes.setMode(changed);
        QVERIFY(observer.changes > 0);
        QCOMPARE(QApplication::palette().color(QPalette::Window),
                 InsightVisualStyle::applicationPalette(changed).color(QPalette::Window));
        themes.setMode(original);
        QCOMPARE(QApplication::palette(), originalPalette);
        QCOMPARE(qApp->styleSheet(), originalStyleSheet);
        observer.changes = 0;

        for (const QString& path : {first, second}) {
            const int index = workspaceTab(strip, path); QVERIFY(index >= 0);
            QTest::mouseClick(strip, Qt::LeftButton, {}, strip->tabRect(index).center());
            QTRY_COMPARE(window.workspaceManager->getWorkspacePath(), path);
            QCOMPARE(themes.mode(), original);
            QCOMPARE(QApplication::palette(), originalPalette);
            QCOMPARE(qApp->styleSheet(), originalStyleSheet);
            QCOMPARE(observer.changes, 0);
        }
    }

    void workspaceIconsOverflowAndOpenFolder() {
        QTemporaryDir files; QVERIFY(files.isValid());
        MainWindow window;
        window.resize(780, 600); window.show();
        auto* sessions = window.findChild<WorkspaceSessionCoordinator*>();
        auto* navigation = window.findChild<NavigationPaneCoordinator*>();
        auto* strip = window.findChild<QTabBar*>("workspaceIconTabs");
        QVERIFY(sessions && navigation && strip);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        QStringList paths;
        for (int i = 0; i < 18; ++i) {
            const QString path = files.filePath(QString::number(i) + "/same-name");
            QVERIFY(QDir().mkpath(path));
            navigation->setExpanded(false, false);
            QVERIFY(sessions->openWorkspace(path));
            QTRY_VERIFY(navigation->dock()->isVisible());
            QVERIFY(navigation->isExpanded());
            paths.append(path);
        }
        QCOMPARE(strip->count(), paths.size());
        QCOMPARE(strip->currentIndex(), 17);
        for (int i = 0; i < paths.size(); ++i) {
            QVERIFY(strip->tabText(i).isEmpty());
            QVERIFY(!strip->tabIcon(i).isNull());
            QVERIFY(strip->tabToolTip(i).contains(QDir::toNativeSeparators(paths.at(i))));
            QVERIFY(strip->accessibleTabName(i).contains("same-name"));
        }
        auto* pages = window.findChild<QStackedWidget*>("centralContentStack");
        QVERIFY(pages);
        auto* brand = window.findChild<QLabel*>("workspaceFilePath"); QVERIFY(brand);
        for (auto theme : {ThemeMode::Light, ThemeMode::Dark}) {
            ApplicationThemeManager::instance().setMode(theme);
            QTRY_COMPARE(pages->currentWidget()->palette().color(QPalette::Window),
                         InsightVisualStyle::theme().tab.tabBackgroundSelected);
            for (int width : {600, 1050}) {
                window.resize(width, 600);
                QTest::qWait(40);
                QVERIFY(brand->isVisible() && brand->width() >= brand->sizeHint().width());
                QCOMPARE(brand->text(), QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
                const QRect brandRect(brand->mapTo(&window, QPoint()), brand->size());
                const QRect tabsRect(strip->mapTo(&window, QPoint()), strip->size());
                QVERIFY(brandRect.right() < tabsRect.left());
                auto* close = window.findChild<QAbstractButton*>("windowCloseButton");
                QVERIFY(close && close->isVisible());
                QVERIFY(!brandRect.intersects(QRect(close->mapTo(&window, QPoint()), close->size())));
                QVERIFY(strip->width() < window.width() - close->width());
                strip->setFocus();
                QTest::keyClick(strip, Qt::Key_Home);
                // Arrow navigation remains available even when most icons overflow.
                for (int n = strip->currentIndex(); n > 0; --n) QTest::keyClick(strip, Qt::Key_Left);
                QTRY_COMPARE(window.workspaceManager->getWorkspacePath(), paths.first());
                for (int n = 1; n < paths.size(); ++n) QTest::keyClick(strip, Qt::Key_Right);
                QTRY_COMPARE(window.workspaceManager->getWorkspacePath(), paths.last());
            }
        }
        QVERIFY(closeWorkspace(strip, paths.last()));
        QTRY_COMPARE(strip->count(), 17);
        QCOMPARE(strip->tabData(strip->currentIndex()).toString(), window.workspaceManager->getWorkspacePath());
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void designTopMarkersSurviveSelectionAndWorkspaceChanges() {
        NavigationWidget navigation;
        navigation.resize(320, 520); navigation.show();
        const QString top = "D:/synthetic/top.sv", other = "D:/synthetic/child.sv";
        navigation.setWorkspaceRoot("D:/synthetic");
        navigation.updateFileHierarchy({top, other});
        DesignHierarchyReport report;
        report.topModule = "top"; report.selectedTopModule = "top";
        report.rootModules = {"top", "child"};
        DesignHierarchyNode root; root.id = "top"; root.instanceName = "top";
        root.projectId = "top-project"; root.moduleType = "top"; root.definitionFile = top; root.isTop = true; root.inSelectedTop = true;
        DesignHierarchyNode child = root; child.id = "child"; child.instanceName = "child";
        child.projectId = "child-project"; child.moduleType = "child"; child.definitionFile = other; child.inSelectedTop = false;
        report.nodes = {root, child};
        QPair<QString,QString> simulationTarget{top,"top"};
        navigation.setSimulationTargetProvider([&] { return simulationTarget; });
        navigation.updateDesignHierarchy(report);
        auto* tree = navigation.findChild<QTreeView*>("navigationDesignTree"); QVERIFY(tree);
        auto* model = qobject_cast<DesignHierarchyModel*>(tree->model()); QVERIFY(model);
        auto find = [model](const QString& path) {
            return model->indexForId(path);
        };
        QTRY_VERIFY(find("top").isValid() && find("child").isValid());
        auto topItem = find("top"), otherItem = find("child");
        for (auto theme : {ThemeMode::Light, ThemeMode::Dark}) {
            ApplicationThemeManager::instance().setMode(theme);
            navigation.highlightFile(other);
            QVERIFY(topItem.data(NavigationRowStyle::SimulationRole).toBool());
            QVERIFY(topItem.data(Qt::AccessibleDescriptionRole).toString().contains("Simulation top"));
            QVERIFY(model->node(topItem).isTop);
            QVERIFY(!otherItem.data(NavigationRowStyle::SimulationRole).toBool());
        }
        report.selectedTopModule = "child";
        report.nodes[0].inSelectedTop = false; report.nodes[1].inSelectedTop = true;
        simulationTarget = {other,"child"};
        navigation.updateDesignHierarchy(report);
        navigation.refreshSimulationTop();
        QTRY_VERIFY(find("top").isValid() && find("child").isValid());
        topItem = find("top"); otherItem = find("child");
        QVERIFY(!topItem.data(Qt::AccessibleDescriptionRole).toString().contains("Simulation top"));
        QVERIFY(!topItem.data(NavigationRowStyle::SimulationRole).toBool());
        QVERIFY(otherItem.data(Qt::AccessibleDescriptionRole).toString().contains("Simulation top")
            && otherItem.data(NavigationRowStyle::SimulationRole).toBool()
            && model->node(otherItem).isTop);
        navigation.setWorkspaceRoot("D:/another-project");
        navigation.updateDesignHierarchy({});
        navigation.updateFileHierarchy({top, other});
        QTRY_VERIFY(!find("child").isValid() && !find("top").isValid());
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void toolboxActionsPinningAndRestoration() {
        QTemporaryDir files; QVERIFY(files.isValid());
        const QString root = files.filePath("project");
        const QString source = root + "/top.sv";
        QVERIFY(write(source, "module leaf(input logic a, output logic b); assign b=a; endmodule\n"
                              "module top(input logic a, output logic b); leaf u0(a,b); endmodule\n"));
        QString hidden;
        {
            MainWindow window; window.resize(1180, 760); window.show();
            auto* sessions = window.findChild<WorkspaceSessionCoordinator*>(); QVERIFY(sessions);
            window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
            QVERIFY(sessions->openWorkspace(root)); QVERIFY(window.tabManager->openFileInTab(source));
            QTRY_VERIFY_WITH_TIMEOUT(!SemanticIndex::getInstance()->getSymbolRecords(source).isEmpty(), 15000);
            window.navigationManager->setDesignTop("top");
            auto* navigation = window.findChild<NavigationWidget*>(); QVERIFY(navigation);
            auto* designTree = window.findChild<QTreeView*>("navigationDesignTree"); QVERIFY(designTree);
            QSignalSpy refreshed(window.navigationManager.get(), &NavigationManager::dataRefreshed);
            bool refreshAvailable = false;
            QTimer::singleShot(20, &window, [&] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                auto* refresh = menu ? menu->findChild<QAction*>("refreshDesignAction") : nullptr;
                refreshAvailable = refresh && refresh->isEnabled();
                if (refreshAvailable) { menu->setActiveAction(refresh); QTest::keyClick(menu, Qt::Key_Return); }
                else if (menu) menu->close();
            });
            QMetaObject::invokeMethod(navigation, "onDesignTreeContextMenuRequested", Q_ARG(QPoint, QPoint(4, designTree->viewport()->height() - 4)));
            QVERIFY(refreshAvailable); QVERIFY(!refreshed.isEmpty());
            auto* controller = window.findChild<ContextWorkspaceController*>(); QVERIFY(controller);
            auto* titleBar = window.findChild<QWidget*>("workspaceTitleBar"); QVERIFY(titleBar);
            auto* titlePath = window.findChild<QLabel*>("workspaceFilePath");
            QVERIFY(titlePath && titlePath->isVisible());
            QCOMPARE(titlePath->text(), QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
            auto* more = controller->rail()->findChild<QAction*>("contextRail.toolbox"); QVERIFY(more);
            auto ids = controller->rail()->entryIds();
            ids.removeAll(QStringLiteral("toolbox"));
            QCOMPARE(ids.size(), 7);
            QVERIFY(!ids.contains(QStringLiteral("simdock")));
            QVERIFY(window.simulationWorkbench()->isVisible());
            const auto retainedProvider=LiveInsightsContextProvider::providerIdForKind(LiveInsightKind::State);
            QCOMPARE(ids.size(), controller->providerIds().size());
            auto* pane = window.findChild<NavigationPaneCoordinator*>(); QVERIFY(pane);
            PanelLayoutController* panels = nullptr;
            for (auto* child : window.children())
                if (auto* candidate = dynamic_cast<PanelLayoutController*>(child)) panels = candidate;
            QVERIFY(panels);
            auto* trace = panels->drawerContent()->findChild<SignalTracePanel*>(QStringLiteral("signalTracePanel")); QVERIFY(trace);
            const auto external = [](const QString& id) {
                return id == "rtlInsight.kernel" || id == "rtlInsight.hotspot";
            };
            const auto visible = [&](const QString& id) {
                if (id == "rtlInsight.kernel" || id == "rtlInsight.hotspot")
                    return panels->isPanelOpen("hotspot") && panels->activeBottomPanelId() == "hotspot"
                        && trace->isVisible()
                        && trace->saveViewState().value("mode").toInt() == (id == "rtlInsight.hotspot" ? 1 : 0);
                return controller->dockVisible() && controller->dockHost()->currentResource().providerId == id;
            };
            const auto destination = [&](const QString& id) -> QWidget* {
                if (id == "rtlInsight.kernel" || id == "rtlInsight.hotspot") return trace;
                return controller->dockHost()->viewForResource(controller->dockHost()->currentResource().stableKey());
            };
            const auto closeExternal = [&](const QString& id) {
                panels->closePanel("hotspot");
            };
            for (const auto& id : ids) {
                auto* entry = controller->rail()->findChild<QAction*>("contextRail." + id); QVERIFY(entry);
                QVERIFY(controller->setProviderPinned(id, true));
                const QDeadlineTimer activationDeadline(5000);
                QVERIFY2(waitForContextIdle(window, *controller, activationDeadline), "Previous component must finish before replacement");
                if (external(id)) {
                    QVERIFY(controller->openTool(retainedProvider));
                    QCOMPARE(controller->rail()->activeEntryId(), retainedProvider);
                    closeExternal(id);
                }
                QVERIFY(controller->setDockVisible(false));
                entry->trigger();
                if (id == retainedProvider)
                    qInfo() << "diag.simdock-root" << controller->workspaceRoot()
                        << window.simulationWorkbench()->workspace()
                        << QFileInfo(controller->workspaceRoot()).canonicalFilePath();
                if (controller->toolboxVisible())
                    for (auto* label : window.findChild<QWidget*>("contextToolbox")->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
                        qInfo() << "diag.toolbox-status" << id << label->text();
                QTRY_VERIFY2_WITH_TIMEOUT(visible(id), qPrintable(QStringLiteral("%1: dock=%2 resource=%3 rail=%4")
                    .arg(id).arg(controller->dockVisible()).arg(controller->dockHost()->currentResource().providerId)
                    .arg(controller->rail()->activeEntryId())), int(activationDeadline.remainingTime()));
                if (external(id)) {
                    QTRY_COMPARE(controller->dockHost()->currentResource().providerId, retainedProvider);
                    QCOMPARE(controller->rail()->activeEntryId(), QString());
                } else {
                    QTRY_COMPARE(controller->dockHost()->currentResource().providerId, id);
                    QCOMPARE(controller->rail()->activeEntryId(), id);
                }
                QWidget* retained = destination(id); QVERIFY(retained);
                entry->trigger();
                if (external(id)) {
                    QTRY_VERIFY2(visible(id), qPrintable(QStringLiteral("%1: dock=%2 resource=%3 rail=%4")
                    .arg(id).arg(controller->dockVisible()).arg(controller->dockHost()->currentResource().providerId)
                    .arg(controller->rail()->activeEntryId())));
                    QCOMPARE(destination(id), retained);
                    closeExternal(id);
                    QTRY_VERIFY(!visible(id));
                } else QTRY_VERIFY(!controller->dockVisible());
                QVERIFY(controller->rail()->activeEntryId().isEmpty());
                entry->trigger();
                if (id == retainedProvider)
                    qInfo() << "diag.simdock-root" << controller->workspaceRoot()
                        << window.simulationWorkbench()->workspace()
                        << QFileInfo(controller->workspaceRoot()).canonicalFilePath();
                if (controller->toolboxVisible())
                    for (auto* label : window.findChild<QWidget*>("contextToolbox")->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
                        qInfo() << "diag.toolbox-status" << id << label->text();
                QTRY_VERIFY2(visible(id), qPrintable(QStringLiteral("%1: dock=%2 resource=%3 rail=%4")
                    .arg(id).arg(controller->dockVisible()).arg(controller->dockHost()->currentResource().providerId)
                    .arg(controller->rail()->activeEntryId())));
                QCOMPARE(destination(id), retained);
                more->trigger(); QTRY_VERIFY(controller->toolboxVisible());
                QCOMPARE(controller->rail()->activeEntryId(), QString("toolbox"));
                more->trigger(); QTRY_VERIFY(!controller->dockVisible());
                more->trigger(); QTRY_VERIFY(controller->toolboxVisible());
                auto* run = window.findChild<QToolButton*>("contextToolRun." + id); QTRY_VERIFY(run && run->isVisible());
                QCOMPARE(run->toolButtonStyle(), Qt::ToolButtonIconOnly);
                QVERIFY(run->text().isEmpty() && !run->toolTip().isEmpty());
                run->click();
                QTRY_VERIFY(!controller->toolboxVisible());
                QTRY_VERIFY2(visible(id), qPrintable(QStringLiteral("%1: dock=%2 resource=%3 rail=%4")
                    .arg(id).arg(controller->dockVisible()).arg(controller->dockHost()->currentResource().providerId)
                    .arg(controller->rail()->activeEntryId())));
                QCOMPARE(controller->dockHost()->currentResource().providerId,
                    external(id) ? retainedProvider : id);
                QCOMPARE(controller->rail()->activeEntryId(), external(id) ? retainedProvider : id);
            }
            // Each route keeps its owning surface open while switching tools.
            for (const auto& id : ids) {
                const QDeadlineTimer activationDeadline(5000);
                QVERIFY2(waitForContextIdle(window, *controller, activationDeadline), "Previous component must finish before replacement");
                controller->rail()->findChild<QAction*>("contextRail." + id)->trigger();
                QTRY_VERIFY2_WITH_TIMEOUT(visible(id), qPrintable(QStringLiteral("%1: dock=%2 resource=%3 rail=%4")
                    .arg(id).arg(controller->dockVisible()).arg(controller->dockHost()->currentResource().providerId)
                    .arg(controller->rail()->activeEntryId())), int(activationDeadline.remainingTime()));
                if (external(id)) QCOMPARE(destination(id), static_cast<QWidget*>(trace));
                else QCOMPARE(controller->dockHost()->currentResource().providerId, id);
            }
            pane->setExpanded(false,false);
            QVERIFY(!pane->isExpanded());
            more->trigger(); QTRY_VERIFY(controller->toolboxVisible());
            auto* firstCard = window.findChild<QToolButton*>("contextToolRun." + ids.at(0))->parentWidget();
            auto* secondCard = window.findChild<QToolButton*>("contextToolRun." + ids.at(1))->parentWidget();
            const auto gridRows = [&] {
                QSet<int> rows;
                for (const auto& id : ids)
                    rows.insert(window.findChild<QToolButton*>("contextToolRun." + id)->parentWidget()->y());
                return rows.size();
            };
            window.resizeDocks({controller->dockWidget()}, {280}, Qt::Horizontal);
            QTRY_COMPARE(firstCard->y(), secondCard->y());
            QVERIFY(firstCard->x() != secondCard->x());
            hidden = retainedProvider;
            auto* pin = window.findChild<QCheckBox*>("contextToolPin." + hidden); QVERIFY(pin && pin->isChecked());
            QTRY_VERIFY(pin->isVisible());
            pin->click();
            QVERIFY(!controller->isProviderPinned(hidden));
            QVERIFY(!controller->rail()->findChild<QAction*>("contextRail." + hidden)->isVisible());
            QVERIFY(window.findChild<QToolButton*>("contextToolRun." + hidden)->isVisible());
            QVERIFY(waitForContextIdle(window,*controller,QDeadlineTimer(5000)));
            window.findChild<QToolButton*>("contextToolRun." + hidden)->click();
            QTRY_VERIFY(controller->dockVisible() && !controller->toolboxVisible());
            QCOMPARE(controller->dockHost()->currentResource().providerId, hidden);
            QVERIFY(controller->rail()->activeEntryId().isEmpty());
            more->trigger(); QTRY_VERIFY(controller->toolboxVisible());
            for (const auto& id : ids) QVERIFY(controller->setProviderPinned(id, false));
            QVERIFY(more->isVisible());
            for (auto theme : {ThemeMode::Light, ThemeMode::Dark}) {
                ApplicationThemeManager::instance().setMode(theme);
                QTRY_COMPARE(titleBar->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
                for (int width : {760, 1180}) {
                    window.resize(width, 760);
                    window.resizeDocks({controller->dockWidget()}, {280}, Qt::Horizontal);
                    QTest::qWait(60);
                    QVERIFY(controller->toolboxVisible());
                    QTRY_VERIFY(window.findChild<QToolButton*>("contextToolRun." + hidden)->isVisible());
                }
                const int narrowRows = gridRows();
                qInfo() << "diag.toolbox-narrow" << narrowRows << controller->dockWidget()->width()
                    << controller->dockWidget()->minimumWidth() << controller->dockWidget()->maximumWidth()
                    << firstCard->minimumSizeHint();
                window.resizeDocks({controller->dockWidget()}, {520}, Qt::Horizontal);
                QTest::qWait(60);
                qInfo() << "diag.toolbox-wide" << gridRows() << controller->dockWidget()->width();
                QTRY_VERIFY(gridRows() < narrowRows);
                window.resizeDocks({controller->dockWidget()}, {280}, Qt::Horizontal);
                QTest::qWait(60);
                QCOMPARE(firstCard->y(), secondCard->y());
                const QString evidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
                if (!evidence.isEmpty()) {
                    QDir().mkpath(evidence);
                    window.grab().save(evidence + (theme == ThemeMode::Light ? "/toolbox-light.png" : "/toolbox-dark.png"));
                }
            }
        }
        {
            MainWindow restored; restored.resize(1000, 680); restored.show();
            auto* controller = restored.findChild<ContextWorkspaceController*>(); QVERIFY(controller);
            for (const auto& id : controller->providerIds()) {
                QVERIFY(!controller->isProviderPinned(id));
                QVERIFY(!controller->rail()->findChild<QAction*>("contextRail." + id)->isVisible());
            }
            auto* more = controller->rail()->findChild<QAction*>("contextRail.toolbox");
            QVERIFY(more && more->isVisible()); more->trigger();
            QTRY_VERIFY(controller->toolboxVisible());
            auto* pin = restored.findChild<QCheckBox*>("contextToolPin." + hidden); QVERIFY(pin);
            QTRY_VERIFY(pin->isVisible());
            pin->click(); QVERIFY(controller->isProviderPinned(hidden));
            QVERIFY(controller->rail()->findChild<QAction*>("contextRail." + hidden)->isVisible());
            for (const auto& id : controller->providerIds()) QVERIFY(controller->setProviderPinned(id, true));
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void standaloneAnalysisDoesNotRecompileWorkspace() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString root = files.filePath("project");
        const QString projectFile = root + "/top.sv";
        const QString temporary = files.filePath("outside/scratch.sv");
        const QByteArray projectSource =
            "module child #(parameter WIDTH = 4); logic [WIDTH-1:0] data; endmodule\n"
            "module top; child #(.WIDTH(8)) u0(); endmodule\n";
        const QByteArray temporarySource =
            "`include \"config.svh\"\n"
            "`ifdef WORKSPACE_ONLY\nmodule leaked_workspace_macro;\n"
            "`else\nmodule scratch;\n`endif\n"
            "`ifdef LOCAL_HEADER\nlogic local_header_used;\n"
            "`else\nlogic leaked_workspace_include;\n`endif\nendmodule\n";
        QVERIFY(write(projectFile, projectSource));
        QVERIFY(write(root + "/include/config.svh", "`define WORKSPACE_HEADER\n"));
        QVERIFY(write(files.filePath("outside/config.svh"), "`define LOCAL_HEADER\n"));
        QVERIFY(write(temporary, temporarySource));
        auto* index = SemanticIndex::getInstance();
        index->clearSemanticState();
        ProjectModel project;
        DocumentModel documents;
        SymbolAnalyzer analyzer;
        AnalysisScheduler scheduler;
        scheduler.setDocumentModel(&documents);
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setProjectModel(&project);
        QSignalSpy workspaceFinished(&scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
        QSignalSpy workspaceStarted(&scheduler, &AnalysisScheduler::workspaceSymbolAnalysisStarted);
        project.setWorkspaceConfiguration({root + "/include"}, {{"WORKSPACE_ONLY", "1"}},
                                          {"sv", "svh", "v"}, "top", {});
        project.setWorkspaceState(root, {projectFile});
        QTRY_VERIFY_WITH_TIMEOUT(!workspaceFinished.isEmpty(), 15000);
        QVERIFY(!index->getSymbolRecords(projectFile).isEmpty());
        const auto workspaceRecords = index->getSymbolRecords(projectFile);
        const int startedCount = workspaceStarted.count();

        MyCodeEditor editor;
        editor.setProperty("standaloneDocument", true);
        editor.setPlainText(QString::fromUtf8(temporarySource));
        documents.registerEditor(&editor, temporary);
        QTRY_COMPARE_WITH_TIMEOUT(scheduler.semanticStatus(temporary).state,
                                  DocumentSemanticState::Current, 15000);
        QCOMPARE(workspaceStarted.count(), startedCount);
        QCOMPARE(project.systemVerilogFiles(), QStringList{projectFile});
        QCOMPARE(index->getCachedFileContent(projectFile), QString::fromUtf8(projectSource));
        QCOMPARE(index->getSymbolRecords(projectFile).size(), workspaceRecords.size());
        for (const auto& record : workspaceRecords) {
            const auto after = index->getSymbolRecordByStableKey(record.stableKey);
            QCOMPARE(after.presentation.instanceInfoByPath.keys(), record.presentation.instanceInfoByPath.keys());
            QCOMPARE(after.presentation.computationRevision, record.presentation.computationRevision);
        }
        QStringList names;
        for (const auto& record : index->getSymbolRecords(temporary)) names.append(record.name);
        QVERIFY(names.contains("scratch"));
        QVERIFY(names.contains("local_header_used"));
        QVERIFY(!names.contains("leaked_workspace_macro"));
        QVERIFY(!names.contains("leaked_workspace_include"));
        const QString otherRoot = files.filePath("other-project");
        const QString otherFile = otherRoot + "/other.sv";
        QVERIFY(write(otherFile, "module other; endmodule\n"));
        project.setWorkspaceState(otherRoot, {otherFile});
        QTRY_VERIFY_WITH_TIMEOUT(workspaceFinished.count() >= 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(index->getCachedFileContent(temporary),
                                  QString::fromUtf8(temporarySource), 15000);
        QCOMPARE(scheduler.semanticStatus(temporary).state, DocumentSemanticState::Current);
        project.closeProject();
        QTRY_COMPARE_WITH_TIMEOUT(index->getCachedFileContent(temporary),
                                  QString::fromUtf8(temporarySource), 15000);
        QCOMPARE(scheduler.semanticStatus(temporary).state, DocumentSemanticState::Current);
        scheduler.shutdown();
    }

    void workspaceAndTemporaryLifetimes() {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString a = files.filePath("workspace-a");
        const QString b = files.filePath("workspace-b");
        const QString nested = a + "/nested";
        const QString fileA = a + "/rtl/counter.sv";
        const QString fileA2 = a + "/rtl/top.v";
        const QString fileB = b + "/rtl/top.sv";
        const QString fileNested = nested + "/nested.sv";
        const QString temporary = files.filePath("outside/scratch.sv");
        const QByteArray source = "module counter;\n" + QByteArray(100, '\n') + "endmodule\n";
        QVERIFY(write(fileA, source)); QVERIFY(write(fileA2, source));
        QVERIFY(write(fileB, source)); QVERIFY(write(fileNested, source));
        QVERIFY(write(temporary, source));

        MainWindow window;
        window.resize(1050, 700); window.show();
        auto* tabs = window.tabManager.get();
        auto* workspaces = window.workspaceManager.get();
        auto* sessions = window.findChild<WorkspaceSessionCoordinator*>();
        QVERIFY(sessions);
        workspaces->setRecentWorkspacePersistenceEnabledForTesting(false);
        tabs->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(files.filePath("recovery")));
        QVERIFY(sessions->openWorkspace(a));
        QVERIFY(tabs->openFileInTab(fileA));
        auto* editorA = tabs->getCurrentEditor();
        QVERIFY(tabs->openFileInTab(fileA2));
        auto* editorA2 = tabs->getCurrentEditor();
        editorA2->moveCursor(QTextCursor::End);
        editorA2->insertPlainText("// unsaved A\n");
        QTextCursor cursor = editorA2->textCursor(); cursor.setPosition(30); editorA2->setTextCursor(cursor);
        editorA2->verticalScrollBar()->setValue(12);
        const int savedScroll = editorA2->verticalScrollBar()->value();
        const int savedCursor = editorA2->textCursor().position();
        QVERIFY(tabs->workspaceHasUnsavedChanges(a));
        QVERIFY(tabs->openFileInTab(temporary));
        auto* external = tabs->getCurrentEditor();
        external->insertPlainText("// temporary edit\n");
        QVERIFY(tabs->isTemporaryEditor(external));
        QVERIFY(external->hierarchyInstanceContext().workspacePath.isEmpty());
        QVERIFY(external->editorSemanticContextForPosition(0).standaloneDocument);
        EditorActionContextService actionContext;
        actionContext.updateWorkspaceContext(workspaces->projectSnapshot());
        EditorActionContextQuery query;
        query.editorContext = external->editorSemanticContextForPosition(0);
        const auto resolved = actionContext.resolve(query);
        QVERIFY(resolved.workspacePath.isEmpty()); QVERIFY(!resolved.hierarchyBound());
        auto* externalGroup = tabs->editorSplitController()->groupForPage(external);
        QVERIFY(externalGroup->tabText(externalGroup->indexOf(external)).contains("TEMP"));
        QVERIFY(externalGroup->tabBar()->property("temporaryTabBoundary").toInt() >= 0);
        externalGroup->tabBar()->moveTab(externalGroup->indexOf(external), 0);
        QTRY_COMPARE(externalGroup->indexOf(external), externalGroup->count() - 1);
        QCOMPARE(externalGroup->tabBar()->property("temporaryTabBoundary").toInt(),
                 externalGroup->indexOf(external));
        QCOMPARE(tabs->workspaceSessionTabs(a).size(), 2);
        QVERIFY(tabs->workspaceSessionTabs(a).at(1).active);
        tabs->checkpointCrashRecovery();
        const auto aRecovery = tabs->listCrashRecoveryCandidates(a);
        for (const auto& candidate : aRecovery.candidates) QVERIFY(candidate.originalFilePath != temporary);
        const auto tempRecovery = tabs->listCrashRecoveryCandidates(tabs->temporaryRecoveryWorkspace());
        QVERIFY(tempRecovery.succeeded()); QCOMPARE(tempRecovery.candidates.size(), 1);
        QCOMPARE(tempRecovery.candidates.first().originalFilePath, temporary);

        QVERIFY(sessions->openWorkspace(b)); QVERIFY(tabs->openFileInTab(fileB));
        auto* editorB = tabs->getCurrentEditor();
        auto* strip = window.findChild<QTabBar*>("workspaceIconTabs");
        QVERIFY(strip);
        QCOMPARE(strip->count(), 2);
        QVERIFY(!window.findChild<QToolButton*>("sidebarWorkspaceSwitcher"));
        QVERIFY(!window.findChild<QToolButton*>("titleWorkspaceSwitcher"));
        QVERIFY(!window.findChild<QMenu*>("workspaceSwitcherPopup"));
        QVERIFY(!window.findChild<QMenu*>("openWorkspacesMenu"));
        auto* brand = window.findChild<QLabel*>("workspaceFilePath");
        QVERIFY(brand && brand->isVisible());
        QCOMPARE(brand->text(), QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
        auto* expand = window.findChild<QToolButton*>("expandProjectSidebarButton");
        QTRY_VERIFY(strip->isVisible());
        const auto iconsVisible = [&] {
            for (int i = 0; i < strip->count(); ++i)
                if (!strip->rect().contains(strip->tabRect(i))) return false;
            for (auto* scroll : strip->findChildren<QToolButton*>())
                if (scroll->isVisible()) return false;
            return true;
        };
        // Opening a workspace posts a layout request. Capture and click only once
        // both icons have their final visible geometry in the multi-editor window.
        QTRY_VERIFY(iconsVisible());
        for (int i = 0; i < strip->count(); ++i) {
            const auto rect = strip->tabRect(i);
            QVERIFY(strip->visibleRegion().contains(rect.adjusted(1, 1, -1, -1)));
            QCOMPARE(window.childAt(strip->mapTo(&window, rect.center())), static_cast<QWidget*>(strip));
        }
        const QString lifetimeEvidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
        auto* titleBar = window.findChild<QWidget*>("workspaceTitleBar"); QVERIFY(titleBar);
        for (const auto theme : {ThemeMode::Dark, ThemeMode::Light}) {
            ApplicationThemeManager::instance().setMode(theme);
            QTRY_VERIFY(iconsVisible());
            QCOMPARE(ApplicationThemeManager::instance().mode(), theme);
            QTRY_COMPARE(titleBar->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
            if (!lifetimeEvidence.isEmpty()) {
                QDir().mkpath(lifetimeEvidence);
                QVERIFY(window.grab().save(lifetimeEvidence + (theme == ThemeMode::Dark
                    ? "/workspace-window-dark.png" : "/workspace-window-light.png")));
            }
        }
        const int count = tabs->editorCount();
        for (int i = 0; i < 3; ++i) {
            const int toA = workspaceTab(strip, a); QVERIFY(toA >= 0);
            QVERIFY(strip->tabText(toA).isEmpty());
            QVERIFY(strip->tabToolTip(toA).contains("Unsaved changes"));
            const QString evidence = qEnvironmentVariable("ZEROSLACK_UI_EVIDENCE_DIR");
            if (i == 0 && !evidence.isEmpty()) {
                QDir().mkpath(evidence);
                window.grab().save(evidence + "/workspace-window.png");
            }
            if (i == 0) { strip->setFocus(); QTest::keyClick(strip, Qt::Key_Left); }
            else QTest::mouseClick(strip, Qt::LeftButton, {}, strip->tabRect(toA).center());
            QTRY_COMPARE(workspaces->getWorkspacePath(), a);
            QCOMPARE(strip->currentIndex(), toA);
            QCOMPARE(tabs->getCurrentEditor(), editorA2);
            QCOMPARE(editorA2->textCursor().position(), savedCursor);
            QCOMPARE(editorA2->verticalScrollBar()->value(), savedScroll);
            QVERIFY(editorA2->toPlainText().contains("unsaved A"));
            QVERIFY(tabs->activateOpenFile(temporary));
            QCOMPARE(workspaces->getWorkspacePath(), a);
            QTest::mouseClick(strip, Qt::LeftButton, {}, strip->tabRect(workspaceTab(strip, b)).center());
            QTRY_COMPARE(workspaces->getWorkspacePath(), b);
            QCOMPARE(tabs->getCurrentEditor(), editorB);
            QCOMPARE(tabs->editorCount(), count);
            QVERIFY(externalGroup->isTabVisible(externalGroup->indexOf(external)));
        }
        QVERIFY(sessions->switchWorkspace(0));
        auto* collapse = window.findChild<QToolButton*>("collapseProjectSidebarButton");
        QVERIFY(collapse); collapse->click();
        QTRY_VERIFY(expand->isVisible());
        QVERIFY(strip->isVisible());
        auto* project = window.findChild<QToolButton*>("projectRailButton");
        auto* settings = window.findChild<QToolButton*>("settingsRailButton");
        QVERIFY(project && settings); QTRY_VERIFY(!project->isVisible()); QVERIFY(!settings->isVisible());
        expand->click(); QTRY_VERIFY(project->isVisible()); QVERIFY(strip->isVisible());

        int reviews = 0;
        bool reviewMatchesWorkspace = true;
        tabs->setTabLocked(editorA2, true);
        tabs->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [&](const QList<PendingDocumentChange>& changes, QWidget*) {
                ++reviews;
                reviewMatchesWorkspace &= changes.size() == 1 && changes.first().fileName == fileA2;
                return reviews == 1 ? UnsavedDocumentBatchDecision::Cancel : UnsavedDocumentBatchDecision::DiscardAll;
            });
        QVERIFY(closeWorkspace(strip, a));
        QTRY_COMPARE(reviews, 1);
        QCOMPARE(workspaces->workspaceEntries().size(), 2);
        QVERIFY(tabs->isTabLocked(editorA2));
        QVERIFY(closeWorkspace(strip, a));
        QTRY_COMPARE(workspaces->workspaceEntries().size(), 1);
        QCOMPARE(workspaces->getWorkspacePath(), b);
        QCOMPARE(strip->count(), 1);
        QCOMPARE(strip->tabData(strip->currentIndex()).toString(), b);
        QVERIFY(tabs->activateOpenFile(temporary)); QVERIFY(external->document()->isModified());
        QCOMPARE(reviews, 2);
        QVERIFY(reviewMatchesWorkspace);
        QCOMPARE(tabs->getCurrentEditor(), external);
        QCOMPARE(tabs->sharedDocumentForEditor(external)->fileName(), temporary);
        QSignalSpy saveFailures(tabs, &TabManager::fileSaveFailed);
        const bool saveSucceeded = tabs->saveCurrentTab();
        if (!saveSucceeded) {
            files.setAutoRemove(false);
            const auto* document = tabs->sharedDocumentForEditor(external);
            qWarning() << "temporary save diagnostic" << saveFailures << "exists" << QFileInfo::exists(temporary)
                       << "writable" << QFileInfo(temporary).isWritable()
                       << "readOnly" << (document && document->readOnly())
                       << "state" << (document ? int(document->externalState()) : -1)
                       << "preserved fixture" << files.path();
        }
        QVERIFY(saveSucceeded);
        QFile saved(temporary); QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(saved.readAll().contains("temporary edit"));
        QVERIFY(tabs->listCrashRecoveryCandidates(tabs->temporaryRecoveryWorkspace()).candidates.isEmpty());
        QVERIFY(!QFileInfo::exists(files.filePath("outside/.zeroslack")));

        // The deepest open root owns a file; closing its parent leaves it open.
        QVERIFY(sessions->openWorkspace(a)); QVERIFY(sessions->openWorkspace(nested));
        QVERIFY(tabs->openFileInTab(fileNested));
        auto* nestedEditor = tabs->getCurrentEditor();
        QCOMPARE(tabs->workspaceForFile(fileNested), nested);
        QVERIFY(sessions->closeWorkspace(1));
        QVERIFY(tabs->openEditors().contains(nestedEditor));
        Q_UNUSED(editorA);
    }
};

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QCoreApplication::setApplicationName("ZeroSlack-Workspace-Switcher-Test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile.path() + "/sessions.ini").toUtf8());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    WorkspaceSwitcherTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "workspace_switcher_test.moc"
