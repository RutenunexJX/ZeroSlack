#include "../../src/simulation/simdock/core/workspace.h"
#include "../../src/simulation/simdock/core/ownedpaths.h"
#include "../../src/simulation/simdock/core/sessionfiles.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace simdock;
namespace {
bool put(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QString manifest(const QString& root, const Project& p) { return QDir(projectDirectory(root)).filePath(p.storageName + "/project.json"); }
}
class StorageTest final : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { qunsetenv("ZEROSLACK_SIMULATION_CACHE_ROOT"); }
    void readableNamesAndStableIdentity() {
        QTemporaryDir root;
        QVERIFY(put(root.filePath("rtl.sv"), "module rtl; endmodule\n"));
        QVERIFY(put(root.filePath("manual.sv"), "module manual; endmodule\n"));
        auto a = newProject("Counter demo"), b = newProject("Counter demo"); QString error;
        a.sources = {"rtl.sv"}; a.tbFile = "manual.sv"; a.tbName = "manual";
        QVERIFY2(saveProject(root.path(), a, &error), qPrintable(error));
        QVERIFY2(saveProject(root.path(), b, &error), qPrintable(error));
        QCOMPARE(a.storageName, QString("Counter demo")); QCOMPARE(b.name, QString("Counter demo (2)"));
        QVERIFY(QFileInfo::exists(manifest(root.path(), a)));
        QCOMPARE(projectTbPath(a, "tb_counter"), QString("sim/Counter demo/tb_counter.sv"));
        const auto old = manifest(root.path(), a), id = a.id;
        a.name = "Renamed project";
        QVERIFY2(saveProject(root.path(), a, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(old)); QCOMPARE(a.id, id);
        const auto loaded = loadProjects(root.path());
        const auto found = std::find_if(loaded.begin(), loaded.end(), [&](const auto& p) { return p.id == id; });
        QVERIFY(found != loaded.end()); QCOMPARE(found->name, a.name); QCOMPARE(found->tbFile, QString("manual.sv"));
        QCOMPARE(read(root.filePath("manual.sv")), QByteArray("module manual; endmodule\n"));
        const auto before = read(manifest(root.path(), a));
        for (const auto& name : {"CON", "nul.txt", "../escape", "A/B", "A\\B", "bad*name", "trailing.", " space "}) {
            auto invalid = a; invalid.name = QString::fromLatin1(name);
            QVERIFY2(!saveProject(root.path(), invalid, &error), name);
            QCOMPARE(read(manifest(root.path(), a)), before);
        }
    }
    void migrationRetainsInputsAndCanRetry() {
        QTemporaryDir root; QString error;
        auto old = newProject("Legacy drawing"); old.inputMode = InputMode::Graphical;
        old.tbFile = "sim/old-generated.sv"; old.tbName = "old_tb";
        old.stimulus = {{"wave", QJsonObject{{"value", "0bX10Z"}}}, {"scoreboard", QJsonObject{{"kind", "values"}}}};
        old.alternateInput = {"manual.sv", "manual", 5000, {}};
        const auto original = projectJson(old);
        const auto bytes = QJsonDocument(original).toJson();
        const auto legacy = QDir(projectDirectory(root.path())).filePath(old.id + ".json");
        const QByteArray editedTb = "// User-modified generated TB\nmodule old_tb; endmodule\n";
        QVERIFY(put(legacy, bytes)); QVERIFY(put(root.filePath(old.tbFile), editedTb));
        QLockFile busy(QDir(projectDirectory(root.path())).filePath(".storage.lock")); QVERIFY(busy.tryLock());
        QStringList notices;
        auto projects = loadProjects(root.path(), &notices);
        QCOMPARE(projects.size(), 1); QVERIFY(!notices.isEmpty()); QCOMPARE(read(legacy), bytes);
        busy.unlock(); notices.clear(); projects = loadProjects(root.path(), &notices);
        QCOMPARE(projects.size(), 1); auto p = projects.first(); QCOMPARE(p.id, old.id);
        QCOMPARE(p.preservedInputs.value("stimulus").toObject(), old.stimulus);
        QCOMPARE(p.preservedInputs.value("alternateInput"), original.value("alternateInput"));
        QCOMPARE(p.tbFile, old.tbFile); QCOMPARE(p.inputMode, InputMode::ExistingTb);
        QVERIFY(!QFileInfo::exists(legacy)); QVERIFY(QFileInfo::exists(manifest(root.path(), p)));
        p.name = "Readable legacy"; QVERIFY2(saveProject(root.path(), p, &error), qPrintable(error));
        auto restored = loadProjects(root.path()).first(); QCOMPARE(restored.preservedInputs, p.preservedInputs);
        QCOMPARE(read(root.filePath(old.tbFile)), editedTb);
        QFile::remove(root.filePath(old.tbFile));
        QVERIFY(!validateInputs(root.path(), restored).isEmpty());
        QCOMPARE(loadProjects(root.path()).first().preservedInputs, p.preservedInputs);
    }
    void concurrentChangesAndRenameFailurePreserveData() {
        QTemporaryDir root; auto p = newProject("Original"); QString error;
        QVERIFY(saveProject(root.path(), p, &error));
        const auto path = manifest(root.path(), p);
        const auto original = read(path);
        QVERIFY(put(path, original + "\n")); auto renamed = p; renamed.name = "Updated";
        QVERIFY(!saveProject(root.path(), renamed, &error)); QCOMPARE(read(path), original + "\n");
        p = loadProjects(root.path()).first();
#ifdef Q_OS_WIN
        const auto native = QDir::toNativeSeparators(path);
        HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(native.utf16()), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        renamed = p; renamed.name = "Cannot rename while held";
        const bool saved = saveProject(root.path(), renamed, &error);
        CloseHandle(held);
        QVERIFY(!saved); QCOMPARE(read(path), original + "\n");
        QCOMPARE(loadProjects(root.path()).first().name, QString("Original"));
#endif
    }
    void ownedResultsRespectReferencesAndLiveInstances() {
        QTemporaryDir root; qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT", root.filePath("runtime").toUtf8());
        SessionFiles a, b; QString error; QStringList notices;
        QVERIFY2(a.create(&error, &notices), qPrintable(error)); QVERIFY(b.create(&error, &notices));
        const auto first = QDir(a.path()).filePath("run-1"), second = QDir(a.path()).filePath("run-2");
        const auto library = QDir(a.path()).filePath("library-1");
        QVERIFY(put(first + "/result.wlf", "wave 1")); QVERIFY(put(second + "/result.wlf", "wave 2"));
        QVERIFY(put(library + "/_info", "compiled library"));
        QVERIFY(a.prune({second, library}, &error)); QVERIFY(!QFileInfo::exists(first));
        QVERIFY(QFileInfo::exists(second + "/result.wlf")); QVERIFY(QFileInfo::exists(library + "/_info"));
        SessionFiles::recover(SessionFiles::cacheRoot()); QVERIFY(QFileInfo::exists(a.path())); QVERIFY(QFileInfo::exists(b.path()));
        QVERIFY(a.setChild(QCoreApplication::applicationPid(), &error)); QVERIFY(!a.release(&error));
        QVERIFY(a.setChild(0, &error)); const auto directory = a.path();
        QVERIFY2(a.release(&error), qPrintable(error)); QVERIFY(!QFileInfo::exists(directory));
        QVERIFY(QFileInfo::exists(b.path())); QVERIFY(b.release(&error));
    }
    void crashedOwnerRecoveredUnknownDataPreserved() {
        QTemporaryDir root;
        const auto cache = root.filePath("cache");
        QProcess orphan;
#ifdef Q_OS_WIN
        orphan.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
#endif
        orphan.start(QCoreApplication::applicationFilePath(), {"--leave-orphan", cache});
        QVERIFY(orphan.waitForFinished(10000)); QCOMPARE(orphan.exitCode(), 0);
        const auto directory = QString::fromUtf8(orphan.readAllStandardOutput()).trimmed();
        QVERIFY(plainPathWithin(cache, directory)); QVERIFY(QFileInfo::exists(directory));
        const auto unknown = QDir(cache).filePath("session-user-data");
        QVERIFY(put(unknown + "/input.sv", "user source"));
        const auto notices = SessionFiles::recover(cache);
        QVERIFY(!QFileInfo::exists(directory)); QCOMPARE(read(unknown + "/input.sv"), QByteArray("user source"));
        QVERIFY(!notices.isEmpty());
        QString error; QVERIFY(!removeOwnedTree(cache, root.path(), &error)); QVERIFY(QFileInfo::exists(unknown));
    }
    void linkedStoragePreservesUserFiles() {
#ifndef Q_OS_WIN
        QSKIP("Windows junction semantics are required.");
#else
        QTemporaryDir root; QString error; QStringList notices;
        const auto userFile = root.filePath("user/source.sv");
        QVERIFY(put(userFile, "user source"));
        qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT", root.filePath("cache").toUtf8());
        SessionFiles storage; QVERIFY(storage.create(&error, &notices));
        const auto link = QDir(storage.path()).filePath("run-linked");
        auto quoted = [](QString value) { return "'" + value.replace("'", "''") + "'"; };
        QProcess junction;
        junction.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
        junction.start("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe",
            {"-NoProfile", "-NonInteractive", "-Command", "New-Item -ItemType Junction -Path " + quoted(link) + " -Target " + quoted(root.filePath("user")) + " -ErrorAction Stop | Out-Null"});
        QVERIFY(junction.waitForFinished(10000)); QCOMPARE(junction.exitCode(), 0);
        QVERIFY(QFileInfo(link).isJunction());
        QVERIFY(!storage.prune({}, &error)); QVERIFY(!storage.release(&error));
        QCOMPARE(read(userFile), QByteArray("user source"));
        QVERIFY(QDir().rmdir(link)); // Removes the junction itself, never its target.
        QVERIFY(storage.release(&error)); QCOMPARE(read(userFile), QByteArray("user source"));
#endif
    }
    void occupiedLibraryDoesNotPreventRunCleanup() {
#ifndef Q_OS_WIN
        QSKIP("Windows delete sharing is required.");
#else
        QTemporaryDir root; QString error; QStringList notices;
        qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT", root.filePath("cache").toUtf8());
        SessionFiles storage; QVERIFY(storage.create(&error, &notices));
        const auto library = storage.path() + "/library-held/state";
        const auto oldRun = storage.path() + "/run-old";
        const auto currentRun = storage.path() + "/run-current";
        QVERIFY(put(library, "held library")); QVERIFY(put(oldRun + "/result", "old")); QVERIFY(put(currentRun + "/result", "current"));
        const auto native = QDir::toNativeSeparators(library);
        HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(native.utf16()), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        const auto closeHeld = qScopeGuard([&] { CloseHandle(held); });
        QVERIFY(!storage.prune({currentRun}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo::exists(library)); QVERIFY(QFileInfo::exists(currentRun));
        QVERIFY(!QFileInfo::exists(oldRun));
#endif
    }
    void occupiedFileKeepsOwnershipForRecovery() {
#ifndef Q_OS_WIN
        QSKIP("Windows delete sharing is required.");
#else
        QTemporaryDir root;
        QProcess orphan;
        orphan.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
        orphan.start(QCoreApplication::applicationFilePath(), {"--leave-partial", root.path()});
        QVERIFY(orphan.waitForFinished(10000)); QCOMPARE(orphan.exitCode(), 0);
        const auto directory = QString::fromUtf8(orphan.readAllStandardOutput()).trimmed();
        QVERIFY(plainPathWithin(root.path(), directory));
        QVERIFY(QFileInfo::exists(directory + "/owner.json"));
        QVERIFY(QFileInfo::exists(directory + "/run-locked/state"));
        QCOMPARE(SessionFiles::recover(root.path()), QStringList());
        QVERIFY(!QFileInfo::exists(directory));
#endif
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && (app.arguments()[1] == "--leave-orphan" || app.arguments()[1] == "--leave-partial")) {
        qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT", app.arguments()[2].toUtf8());
        SessionFiles storage; QString error; QStringList notices;
        if (!storage.create(&error, &notices)) return 2;
#ifdef Q_OS_WIN
        if (app.arguments()[1] == "--leave-partial") {
            const auto occupied = QDir(storage.path()).filePath("run-locked/state");
            if (!put(occupied, "held state")) return 3;
            const auto native = QDir::toNativeSeparators(occupied);
            HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(native.utf16()), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (held == INVALID_HANDLE_VALUE) return 4;
            if (storage.release(&error) || error.isEmpty() || !QFileInfo::exists(storage.path() + "/owner.json")) return 5;
            // Exit without destructors; Windows releases the held handle.
        }
#endif
        const auto path = storage.path().toUtf8(); std::fwrite(path.constData(), 1, size_t(path.size()), stdout); std::fflush(stdout);
        std::_Exit(0);
    }
    StorageTest test; return QTest::qExec(&test, argc, argv);
}
#include "storage_test.moc"
