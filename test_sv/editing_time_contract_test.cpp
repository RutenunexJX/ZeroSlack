#include "actionregistry.h"
#include "activitylogservice.h"
#include "analysisscheduler.h"
#include "editingtimeservice.h"
#include "editingtimestore.h"
#include "editorsplitcontroller.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "temporaryeditorcontextview.h"
#include "testuistyle.h"
#include "workspacemanager.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLockFile>
#include <QPlainTextDocumentLayout>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>
#include <thread>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
constexpr qint64 ms = 1000000;
void key(MyCodeEditor* editor, QEvent::Type type, int code, const QString& text = {},
         Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool repeat = false)
{
    QKeyEvent event(type, code, modifiers, text, repeat);
    QApplication::sendEvent(editor, &event);
}
void focus(MyCodeEditor* editor)
{
    editor->window()->show();
    editor->show();
    editor->window()->activateWindow();
    editor->setFocus();
    QApplication::processEvents();
}
QByteArray get(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool put(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

int writerProcess(const QStringList& arguments)
{
    if (arguments.size() != 6) return 10;
    EditingTimeStore store(arguments.at(2));
    EditingTimeSaveResult initial;
    for (int attempt = 0; attempt < 200 && !initial.ok; ++attempt) {
        initial = store.synchronize({}, {});
        if (!initial.ok) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!initial.ok) return 11;
    const qint64 start = EditingTimeService::monotonicNow();
    if (!put(arguments.at(3), QByteArray::number(start))) return 12;
    for (int attempt = 0; attempt < 2000 && !QFileInfo::exists(arguments.at(4)); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!QFileInfo::exists(arguments.at(4))) return 13;
    EditingTimeSaveResult result;
    for (int attempt = 0; attempt < 200 && !result.ok; ++attempt) {
        result = store.synchronize(initial.snapshot.generation,
            {{start, start + arguments.at(5).toLongLong()}});
        if (!result.ok) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return result.ok ? 0 : 14;
}
struct EditorFixture {
    QTemporaryDir directory;
    qint64 now = 1000 * ms;
    EditingTimeService timer{directory.filePath("time.json"), nullptr, [this] { return now; }};
    MyCodeEditor editor;
    EditorFixture()
    {
        editor.resize(700, 360);
        timer.attachEditor(&editor);
        timer.synchronize();
        focus(&editor);
    }
    void press(int code = Qt::Key_A, const QString& text = QStringLiteral("a"),
               Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool repeat = false)
    { key(&editor, QEvent::KeyPress, code, text, modifiers, repeat); }
    void release(int code = Qt::Key_A, bool repeat = false)
    { key(&editor, QEvent::KeyRelease, code, {}, Qt::NoModifier, repeat); }
};

struct WindowFixture {
    QTemporaryDir directory;
    std::unique_ptr<MainWindow> window;
    WindowFixture()
    {
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", directory.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", directory.filePath("time.json").toUtf8());
        QSettings().clear();
        window = std::make_unique<MainWindow>();
        window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window->tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(directory.filePath("recovery")));
        window->tabManager->unsavedDocumentManagerForTesting()->setDecisionProvider(
            [](const auto&, QWidget*) { return UnsavedDocumentBatchDecision::DiscardAll; });
        window->resize(1000, 700);
        window->show();
        QApplication::processEvents();
    }
    ~WindowFixture() { window->close(); }
    EditingTimeService* timer() const { return window->findChild<EditingTimeService*>(); }
    MyCodeEditor* editor()
    {
        window->tabManager->createNewTab();
        auto* editor = window->tabManager->getCurrentEditor();
        focus(editor);
        return editor;
    }
};
}

class EditingTimeContractTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(initializeUiStyleForTest());
        qInfo().noquote() << "Executable:" << QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
        for (const wchar_t* name : {L"libzeroslack_core.dll", L"libzeroslack_documents.dll",
                                   L"libzeroslack_semantic.dll", L"ElaWidgetTools.dll"}) {
            wchar_t path[32768]{};
            const auto length = GetModuleFileNameW(GetModuleHandleW(name), path, 32768);
            QVERIFY(length > 0);
            qInfo().noquote() << "Loaded module:" << QString::fromWCharArray(path, length);
        }
#endif
    }

    void immediatePausePrecisionAndUnion()
    {
        EditorFixture f;
        QVERIFY(f.editor.hasFocus());
        f.press();
        QVERIFY(f.timer.isActive());
        f.now += 17 * ms + 123;
        f.release();
        QCOMPARE(f.timer.totalNanoseconds(), 17 * ms + 123);
        QVERIFY(!f.timer.isActive());
        f.now += 30000 * ms;
        QCOMPARE(f.timer.totalNanoseconds(), 17 * ms + 123);
        f.press();
        f.now += 10 * ms;
        f.press(Qt::Key_B, "b");
        f.now += 15 * ms;
        f.release();
        QVERIFY(f.timer.isActive());
        f.now += 5 * ms;
        f.release(Qt::Key_B);
        QCOMPARE(f.timer.totalNanoseconds(), 47 * ms + 123);
        f.now += 5000 * ms;
        f.press();
        f.now += 4 * ms;
        f.release(Qt::Key_A, true);
        f.press(Qt::Key_A, "a", Qt::NoModifier, true);
        f.now += 6 * ms;
        f.release();
        QCOMPARE(f.timer.totalNanoseconds(), 57 * ms + 123);
        QVERIFY(!QFileInfo::exists(f.directory.filePath("time.json")));
        QVERIFY(f.timer.synchronize());
        f.now += 86400000 * ms;
        EditingTimeService reopened(f.directory.filePath("time.json"), nullptr, [&] { return f.now; });
        QVERIFY(reopened.synchronize());
        QCOMPARE(reopened.totalNanoseconds(), 57 * ms + 123);
        QVERIFY(!reopened.isActive());
    }

    void readOnlyNoOpsAndProgrammaticChanges()
    {
        EditorFixture f;
        f.editor.setPlainText("alpha\nbeta");
        f.editor.insertPlainText("programmatic");
        QTextCursor cursor(f.editor.document());
        cursor.insertText("foreign");
        f.editor.undo();
        f.editor.redo();
        f.editor.clear();
        for (int code : {Qt::Key_Control, Qt::Key_Shift, Qt::Key_Alt,
                         Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Backspace, Qt::Key_Delete}) {
            f.press(code, {});
            f.now += 100 * ms;
            f.release(code);
        }
        f.editor.setReadOnly(true);
        f.press(); f.now += 50 * ms; f.release();
        f.editor.setReadOnly(false);
        f.editor.setPlainText("a");
        f.editor.selectAll();
        f.press(); f.now += 50 * ms; f.release(); // identical replacement
        QCOMPARE(f.timer.totalNanoseconds(), qint64(0));
        QVERIFY(!f.timer.isActive());
        QVERIFY(!QFileInfo::exists(f.directory.filePath("time.json")));
    }

    void imePreeditCommitAndCancellation()
    {
        EditorFixture f;
        const auto connection = connect(&f.editor, &MyCodeEditor::documentChangeApplied,
            &f.editor, [&](const DocumentChange&) { f.now += 200000; });
        QInputMethodEvent preedit(QStringLiteral("zhong"), {});
        QApplication::sendEvent(&f.editor, &preedit);
        f.now += 30000 * ms;
        QVERIFY(!f.timer.isActive());
        QCOMPARE(f.timer.totalNanoseconds(), qint64(0));
        QInputMethodEvent commit;
        commit.setCommitString(QString::fromUtf8("中"));
        QApplication::sendEvent(&f.editor, &commit);
        QCOMPARE(f.editor.toPlainText(), QString::fromUtf8("中"));
        QCOMPARE(f.timer.totalNanoseconds(), qint64(200000));
        QVERIFY(!f.timer.isActive());
        f.now += 30000 * ms;
        QApplication::sendEvent(&f.editor, &preedit);
        QInputMethodEvent cancel;
        QApplication::sendEvent(&f.editor, &cancel);
        QCOMPARE(f.timer.totalNanoseconds(), qint64(200000));
        QInputMethodEvent deletion;
        deletion.setCommitString({}, -1, 1);
        QApplication::sendEvent(&f.editor, &deletion);
        QVERIFY(f.editor.toPlainText().isEmpty());
        QCOMPARE(f.timer.totalNanoseconds(), qint64(400000));
        f.editor.setReadOnly(true);
        QApplication::sendEvent(&f.editor, &commit);
        QCOMPARE(f.timer.totalNanoseconds(), qint64(400000));
        disconnect(connection);
    }

    void clipboardUndoRedoKeyIntervals()
    {
        EditorFixture f;
        f.editor.setPlainText("alpha");
        f.editor.selectAll();
        for (int code : {Qt::Key_X, Qt::Key_V, Qt::Key_Z, Qt::Key_Y}) {
            const auto before = f.editor.toPlainText();
            f.press(code, {}, Qt::ControlModifier);
            QVERIFY(f.editor.toPlainText() != before);
            QVERIFY(f.timer.isActive());
            f.now += 31 * ms;
            f.release(code);
        }
        QCOMPARE(f.timer.totalNanoseconds(), 124 * ms);
        QApplication::clipboard()->clear();
        f.press(Qt::Key_V, {}, Qt::ControlModifier);
        f.now += 50 * ms; f.release(Qt::Key_V);
        QCOMPARE(f.timer.totalNanoseconds(), 124 * ms);
    }

    void lifecycleEndsActivity_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("focus-out") << 0;
        QTest::newRow("hide") << 1;
        QTest::newRow("close") << 2;
        QTest::newRow("application-inactive") << 3;
        QTest::newRow("rebind") << 4;
        QTest::newRow("read-only") << 5;
        QTest::newRow("window-inactive") << 6;
        QTest::newRow("application-state-suspended") << 7;
    }
    void lifecycleEndsActivity()
    {
        QFETCH(int, boundary);
        EditorFixture f;
        f.press(); f.now += 9 * ms;
        if (boundary == 0) {
            QFocusEvent out(QEvent::FocusOut); QApplication::sendEvent(&f.editor, &out);
        } else if (boundary == 1) f.editor.hide();
        else if (boundary == 2) f.editor.close();
        else if (boundary == 3) {
            QEvent out(QEvent::ApplicationDeactivate); QApplication::sendEvent(qApp, &out);
        } else if (boundary == 4) {
            auto* document = new QTextDocument(&f.editor);
            document->setDocumentLayout(new QPlainTextDocumentLayout(document));
            f.editor.attachSharedDocument(document);
        } else if (boundary == 5) f.editor.setReadOnly(true);
        else if (boundary == 6) {
            QEvent out(QEvent::WindowDeactivate); QApplication::sendEvent(&f.editor, &out);
        } else qApp->applicationStateChanged(Qt::ApplicationSuspended);
        QVERIFY(!f.timer.isActive());
        QCOMPARE(f.timer.totalNanoseconds(), 9 * ms);
        f.now += 60000 * ms;
        QCOMPARE(f.timer.totalNanoseconds(), 9 * ms);
    }

    void destructionClosesMissingRelease()
    {
        QTemporaryDir dir;
        qint64 now = 1000 * ms;
        EditingTimeService timer(dir.filePath("time.json"), nullptr, [&] { return now; });
        auto editor = std::make_unique<MyCodeEditor>();
        timer.attachEditor(editor.get()); focus(editor.get());
        key(editor.get(), QEvent::KeyPress, Qt::Key_A, "a");
        now += 27 * ms; editor.reset();
        QVERIFY(!timer.isActive());
        QCOMPARE(timer.totalNanoseconds(), 27 * ms);
        now += 40000 * ms;
        QCOMPARE(timer.totalNanoseconds(), 27 * ms);
    }

    void interruptedInputCannotClaimLaterDocumentNotifications()
    {
        EditorFixture f;
        {
            auto command = f.editor.beginUserEdit();
            f.now += 2 * ms;
            QFocusEvent out(QEvent::FocusOut);
            QApplication::sendEvent(&f.editor, &out);
            f.now += 30000 * ms;
            f.editor.insertPlainText("later foreign edit");
        }
        QCOMPARE(f.timer.totalNanoseconds(), qint64(0));
        {
            auto command = f.editor.beginUserEdit();
            f.editor.insertPlainText("synchronous user edit");
            f.now += 3 * ms;
            QFocusEvent out(QEvent::FocusOut);
            QApplication::sendEvent(&f.editor, &out);
            f.now += 30000 * ms;
        }
        QCOMPARE(f.timer.totalNanoseconds(), 3 * ms);
        QVERIFY(!f.timer.isActive());
        {
            auto command = f.editor.beginUserEdit();
            f.editor.insertPlainText("edit before remote reset");
            f.now += 2 * ms;
            QFocusEvent out(QEvent::FocusOut);
            QApplication::sendEvent(&f.editor, &out);
            f.now += 10 * ms;
            EditingTimeStore remote(f.directory.filePath("time.json"));
            QVERIFY(remote.reset(f.now).ok);
            QVERIFY(f.timer.synchronize());
            f.now += 30000 * ms;
        }
        QCOMPARE(f.timer.totalNanoseconds(), qint64(0));
    }

    void persistenceFailureRetryAndReset()
    {
        EditorFixture f;
        f.press(); f.now += 17 * ms; f.release();
        QVERIFY(f.timer.synchronize());
        const auto path = f.directory.filePath("time.json");
        const QByteArray saved = get(path);
        QSignalSpy failures(&f.timer, &EditingTimeService::persistenceFailed);
        QLockFile lock(path + ".lock");
        QVERIFY(lock.tryLock());
        f.press(); f.now += 13 * ms; f.release();
        QVERIFY(!f.timer.synchronize());
        QVERIFY(!f.timer.synchronize());
        QCOMPARE(failures.count(), 1);
        QVERIFY(!f.timer.reset());
        QCOMPARE(f.timer.totalNanoseconds(), 30 * ms);
        QCOMPARE(get(path), saved);
        lock.unlock();
        QVERIFY(f.timer.synchronize());
        QVERIFY(f.timer.persistenceError().isEmpty());
        QCOMPARE(f.timer.totalNanoseconds(), 30 * ms);
        EditingTimeService reopened(path);
        QVERIFY(reopened.synchronize());
        QCOMPARE(reopened.totalNanoseconds(), 30 * ms);
        QVERIFY(f.timer.reset());
        QCOMPARE(f.timer.totalNanoseconds(), qint64(0));
        QVERIFY(reopened.synchronize());
        QCOMPARE(reopened.totalNanoseconds(), qint64(0));
    }

    void atomicCommitFailureRetainsIncrement()
    {
#ifdef Q_OS_WIN
        EditorFixture f;
        f.press(); f.now += 5 * ms; f.release();
        QVERIFY(f.timer.synchronize());
        const QString path = f.directory.filePath("time.json");
        const QByteArray before = get(path);
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        f.press(); f.now += 7 * ms; f.release();
        const bool saveFailed = !f.timer.synchronize();
        const bool resetFailed = !f.timer.reset();
        CloseHandle(handle);
        QVERIFY(saveFailed);
        QVERIFY(resetFailed);
        QCOMPARE(get(path), before);
        QCOMPARE(f.timer.totalNanoseconds(), 12 * ms);
        QVERIFY(f.timer.synchronize());
        QCOMPARE(f.timer.totalNanoseconds(), 12 * ms);
        EditingTimeStore store(path);
        QCOMPARE(store.synchronize({}, {}).snapshot.total, 12 * ms);
#endif
    }

    void multiInstanceMergeAndResetBoundary()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("time.json");
        EditingTimeStore a(path), b(path);
        const auto original = a.synchronize({}, {}).snapshot.generation;
        QVERIFY(a.synchronize(original, {{100 * ms, 120 * ms}}).ok);
        QCOMPARE(b.synchronize(original, {{150 * ms, 180 * ms}}).snapshot.total, 50 * ms);
        const auto reset = a.reset(200 * ms);
        QVERIFY(reset.ok);
        // Old instance's input wholly before reset cannot revive the old total.
        QCOMPARE(b.synchronize(original, {{180 * ms, 190 * ms}}).snapshot.total, qint64(0));
        // A held key across reset contributes only the post-reset part.
        QCOMPARE(b.synchronize(original, {{190 * ms, 215 * ms}}).snapshot.total, 15 * ms);
        QCOMPARE(a.synchronize(reset.snapshot.generation, {{220 * ms, 225 * ms}}).snapshot.total, 20 * ms);
        QCOMPARE(b.synchronize(original, {}).snapshot.total, 20 * ms);
    }

    void invalidStorePreservedAndNanosecondsExact()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("time.json");
        const QByteArray corrupt("{invalid-existing-statistics");
        QVERIFY(put(path, corrupt));
        EditingTimeStore store(path);
        QVERIFY(!store.synchronize({}, {{1, 2}}).ok);
        QVERIFY(!store.reset(100).ok);
        QCOMPARE(get(path), corrupt);
        QVERIFY(QFile::remove(path));
        constexpr qint64 exact = 9007199254740993LL;
        QVERIFY(store.synchronize(QStringLiteral("initial"), {{0, exact}}).ok);
        QCOMPARE(store.synchronize({}, {}).snapshot.total, exact);
        QCOMPARE(QJsonDocument::fromJson(get(path)).object().value("totalNs").toString(), QString::number(exact));
    }

    void processConcurrencyAndOldProcessAfterReset()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("time.json"), gate = dir.filePath("go");
        QProcess a, b;
        const auto start = [&](QProcess& process, const QString& ready, qint64 amount) {
            process.start(QCoreApplication::applicationFilePath(),
                {"--editing-time-writer", path, ready, gate, QString::number(amount)});
        };
        start(a, dir.filePath("a.ready"), 11000123);
        start(b, dir.filePath("b.ready"), 17000456);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("a.ready")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("b.ready")), 10000);
        QVERIFY(put(gate, "go"));
        QVERIFY(a.waitForFinished(10000)); QVERIFY(b.waitForFinished(10000));
        QCOMPARE(a.exitCode(), 0); QCOMPARE(b.exitCode(), 0);
        EditingTimeStore observer(path);
        QCOMPARE(observer.synchronize({}, {}).snapshot.total, qint64(28000579));
        QVERIFY(QFile::remove(gate));
        QProcess old;
        start(old, dir.filePath("old.ready"), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("old.ready")), 10000);
        const qint64 childStart = get(dir.filePath("old.ready")).toLongLong();
        const qint64 resetAt = EditingTimeService::monotonicNow();
        QVERIFY(resetAt > childStart + 1000);
        QVERIFY(resetAt - childStart < 10000 * ms); // same monotonic domain across processes
        QVERIFY(observer.reset(resetAt).ok);
        QVERIFY(put(gate, "go"));
        QVERIFY(old.waitForFinished(10000)); QCOMPARE(old.exitCode(), 0);
        QCOMPARE(observer.synchronize({}, {}).snapshot.total, qint64(0));
    }

    void sharedSplitAndEditableTemporaryViews()
    {
        QTemporaryDir dir;
        qint64 now = 1000 * ms;
        EditingTimeService timer(dir.filePath("time.json"), nullptr, [&] { return now; });
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* tabs = new QTabWidget(&host);
        layout->addWidget(tabs);
        TabManager manager(tabs);
        manager.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(dir.filePath("recovery")));
        manager.enableSplitLayout(&host);
        timer.attachTabManager(&manager);
        manager.createNewTab();
        auto* first = manager.getCurrentEditor();
        first->setPlainText("alpha\nbeta");
        const auto documentId = manager.sharedDocumentForEditor(first)->documentId();
        QVERIFY(manager.splitCurrentView(EditorSplitDirection::Right));
        auto* second = manager.getCurrentEditor();
        QVERIFY(first != second);
        QCOMPARE(first->document(), second->document());
        host.resize(900, 400); focus(first);
        key(first, QEvent::KeyPress, Qt::Key_A, "a"); now += 11 * ms;
        key(first, QEvent::KeyRelease, Qt::Key_A);
        QCOMPARE(timer.totalNanoseconds(), 11 * ms); // two observers, one input
        focus(second);
        key(second, QEvent::KeyPress, Qt::Key_B, "b"); now += 13 * ms;
        focus(first); // no matching release, view switch ends it
        QCOMPARE(timer.totalNanoseconds(), 24 * ms);
        QVERIFY(!timer.isActive());

        TemporaryEditorContextView temporary(&manager);
        EditorLocation location; location.documentId = documentId;
        QVERIFY(temporary.openLocation(location));
        temporary.resize(600, 360); temporary.show(); focus(temporary.editor());
        QVERIFY(manager.isAuxiliaryView(temporary.editor()));
        QCOMPARE(first->document(), temporary.editor()->document());
        key(temporary.editor(), QEvent::KeyPress, Qt::Key_C, "c"); now += 19 * ms;
        key(temporary.editor(), QEvent::KeyRelease, Qt::Key_C);
        QCOMPARE(timer.totalNanoseconds(), 43 * ms);
        first->insertPlainText("programmatic shared update");
        QCOMPARE(timer.totalNanoseconds(), 43 * ms);
        key(temporary.editor(), QEvent::KeyPress, Qt::Key_D, "d"); now += 7 * ms;
        temporary.hide(); now += 30000 * ms;
        QCOMPARE(timer.totalNanoseconds(), 50 * ms);
        QVERIFY(!timer.isActive());
        const auto file = dir.filePath("loaded.sv");
        QVERIFY(put(file, "module loaded; endmodule\n"));
        QVERIFY(manager.openFileInTab(file));
        QCOMPARE(timer.totalNanoseconds(), 50 * ms);
        for (auto* editor : manager.openEditors()) editor->document()->setModified(false);
    }

    void actualWindowCommandsAndVisibleReset()
    {
        WindowFixture f;
        auto* timer = f.timer(); QVERIFY(timer);
        auto* label = f.window->findChild<QLabel*>("editingTimeLabel");
        auto* reset = f.window->findChild<QToolButton*>("resetEditingTimeButton");
        QVERIFY(label && reset);
        QVERIFY(label->isVisible()); QVERIFY(reset->isVisible());
        auto* title = f.window->findChild<QWidget*>("workspaceTitleBar");
        QVERIFY(title && title->isAncestorOf(label) && title->isAncestorOf(reset));
        QVERIFY(f.window->findChildren<QStatusBar*>().isEmpty());
        auto* editor = f.editor();
        editor->setPlainText("module timer_demo;\nendmodule\n");
        QCOMPARE(timer->totalNanoseconds(), qint64(0));
        key(editor, QEvent::KeyPress, Qt::Key_A, "a");
        QTest::qWait(140);
        QVERIFY(timer->isActive());
        QVERIFY(timer->totalNanoseconds() >= 100 * ms);
        key(editor, QEvent::KeyRelease, Qt::Key_A);
        const qint64 paused = timer->totalNanoseconds();
        QTest::qWait(150);
        QCOMPARE(timer->totalNanoseconds(), paused);
        QVERIFY(label->text() != QStringLiteral("Editing 00:00:00.0"));

        const QString evidence = qEnvironmentVariable("ZEROSLACK_TIMER_EVIDENCE_DIR");
        if (!evidence.isEmpty())
            QVERIFY(f.window->grab().save(evidence + "/mainwindow-timer.png"));

        QApplication::clipboard()->setText("pasted_text");
        auto invoke = [&](const QString& id) {
            bool handled = false;
            editor->registeredActionRequested(id,
                {{"editorViewId", editor->property("editorViewId")}}, &handled);
            return handled;
        };
        QVERIFY(invoke("edit.paste"));
        QVERIFY(editor->toPlainText().contains("pasted_text"));
        QVERIFY(!timer->isActive());
        QVERIFY(timer->totalNanoseconds() > paused);
        QVERIFY(invoke("edit.undo"));
        QVERIFY(invoke("edit.redo"));
        editor->selectAll();
        QVERIFY(invoke("edit.cut"));
        QVERIFY(editor->toPlainText().isEmpty());
        QVERIFY(invoke("edit.paste"));
        focus(editor);
        const qint64 beforeShortcut = timer->totalNanoseconds();
        key(editor, QEvent::KeyPress, Qt::Key_V, {}, Qt::ControlModifier);
        QTest::qWait(45);
        key(editor, QEvent::KeyRelease, Qt::Key_V);
        QVERIFY(timer->totalNanoseconds() >= beforeShortcut + 35 * ms);
        const auto text = editor->toPlainText();
        const int undoSteps = editor->document()->availableUndoSteps();
        const int redoSteps = editor->document()->availableRedoSteps();
        const int revision = editor->document()->revision();
        const bool dirty = editor->document()->isModified();
        const QString workspace = f.window->workspaceManager->getWorkspacePath();
        QTest::mouseClick(reset, Qt::LeftButton);
        QCOMPARE(timer->totalNanoseconds(), qint64(0));
        QCOMPARE(label->text(), QStringLiteral("Editing 00:00:00.0"));
        QCOMPARE(editor->toPlainText(), text);
        QCOMPARE(editor->document()->availableUndoSteps(), undoSteps);
        QCOMPARE(editor->document()->availableRedoSteps(), redoSteps);
        QCOMPARE(editor->document()->revision(), revision);
        QCOMPARE(editor->document()->isModified(), dirty);
        QCOMPARE(f.window->workspaceManager->getWorkspacePath(), workspace);
        EditingTimeStore persisted(f.directory.filePath("time.json"));
        QCOMPARE(persisted.synchronize({}, {}).snapshot.total, qint64(0));
        f.window->resize(760, 520);
        QApplication::processEvents();
        QVERIFY(label->isVisible() && reset->isVisible());
        QVERIFY(f.window->rect().contains(QRect(label->mapTo(f.window.get(), QPoint()), label->size())));
        QVERIFY(f.window->rect().contains(QRect(reset->mapTo(f.window.get(), QPoint()), reset->size())));
        const auto* minimize = f.window->findChild<QToolButton*>("windowMinimizeButton");
        QVERIFY(minimize);
        const QRect timerRect(label->mapTo(title, QPoint()), label->size());
        const QRect resetRect(reset->mapTo(title, QPoint()), reset->size());
        const QRect windowButtons(minimize->mapTo(title, QPoint()), minimize->size());
        QVERIFY(title->rect().contains(timerRect) && title->rect().contains(resetRect));
        QVERIFY(!timerRect.intersects(resetRect) && !resetRect.intersects(windowButtons));
        if (!evidence.isEmpty())
            QVERIFY(f.window->grab().save(evidence + "/mainwindow-timer-compact.png"));
    }

    void realWindowCloseReopenAndFailedSave()
    {
        WindowFixture f;
        auto* editor = f.editor();
        auto* timer = f.timer();
        key(editor, QEvent::KeyPress, Qt::Key_A, "a"); QTest::qWait(70);
        const auto path = f.directory.filePath("time.json");
        QLockFile lock(path + ".lock");
        QVERIFY(lock.tryLock());
        const int oldEvents = ActivityLogService::getInstance()->events().size();
        QVERIFY(!f.window->close());
        QVERIFY(f.window->isVisible());
        QVERIFY(!timer->isActive());
        QVERIFY(timer->totalNanoseconds() >= 50 * ms);
        QVERIFY(!timer->persistenceError().isEmpty());
        QVERIFY(ActivityLogService::getInstance()->events().size() > oldEvents);
        QVERIFY(f.window->findChild<QLabel*>("editingTimeLabel")->text().contains("Save failed"));
        const auto total = timer->totalNanoseconds();
        lock.unlock();
        QVERIFY(f.window->close());
        f.window.reset();
        QTest::qWait(100);
        f.window = std::make_unique<MainWindow>();
        f.window->show(); QApplication::processEvents();
        QCOMPARE(f.timer()->totalNanoseconds(), total);
        QVERIFY(!f.timer()->isActive());
        QVERIFY(f.timer()->reset());
        QVERIFY(f.window->close());
        f.window.reset();
        f.window = std::make_unique<MainWindow>();
        QCOMPARE(f.timer()->totalNanoseconds(), qint64(0));
        QVERIFY(!f.timer()->isActive());
    }

    void closedWindowWithoutPendingTimeNeedsNoWrite()
    {
        WindowFixture f;
        QVERIFY(!f.timer()->hasPendingTime());
        QLockFile lock(f.directory.filePath("time.json.lock"));
        QVERIFY(lock.tryLock());
        QVERIFY(f.window->close());
        QVERIFY(!f.window->isVisible());
    }
};

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    if (app.arguments().value(1) == QStringLiteral("--editing-time-writer"))
        return writerProcess(app.arguments());
    QCoreApplication::setOrganizationName("ZeroSlack");
    QCoreApplication::setApplicationName("ZeroSlack-EditingTime-Test");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", settings.filePath("time.json").toUtf8());
    EditingTimeContractTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "editing_time_contract_test.moc"
