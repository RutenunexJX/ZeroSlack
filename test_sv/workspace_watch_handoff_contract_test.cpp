#include <QtWidgets>
#include <QtTest>
#include <QtConcurrent>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include "applicationthememanager.h"
// Observe scheduling and wrap its real installed callback for deterministic
// retirement tests. Filesystem enumeration and native watches are not mocked.
#define private public
#include "workspacemanager.h"
#undef private

#include <cstdio>

namespace {
int checks=0, failures=0;
void check(bool ok,const QString& label)
{
    ++checks; failures+=!ok;
    std::printf("[%s] %s\n",ok?"PASS":"FAIL",qPrintable(label));std::fflush(stdout);
}
bool writeFile(const QString& path, const QByteArray& text="module unit; endmodule\n")
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(text)==text.size();
}
bool waitFor(const std::function<bool()>& ready,int timeout=5000)
{
    QElapsedTimer timer;timer.start();
    while (!ready() && timer.elapsed()<timeout) QTest::qWait(2);
    return ready();
}
QSet<QString> keys(const QStringList& paths)
{
    QSet<QString> result;
    for (const auto& path:paths) result.insert(QDir::cleanPath(path).toCaseFolded());
    return result;
}
bool matchesDisk(WorkspaceManager& manager)
{
    QStringList files, directories{manager.getWorkspacePath()};
    QDirIterator it(manager.getWorkspacePath(),QDir::AllEntries|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileInfo().isDir()) directories.append(it.filePath());
        else files.append(it.filePath());
    }
    auto* native=manager.findChild<QFileSystemWatcher*>();
    const bool matches=native && keys(manager.getAllFiles())==keys(files)
        && keys(native->files())==keys(files) && keys(native->directories())==keys(directories);
    if (!matches && native) {
        const auto show=[](const QSet<QString>& set) {return QStringList(set.cbegin(),set.cend()).join('|');};
        std::printf("mismatch: missing-published=%s extra-published=%s missing-files=%s extra-files=%s missing-dirs=%s extra-dirs=%s\n",
            qPrintable(show(keys(files)-keys(manager.getAllFiles()))),qPrintable(show(keys(manager.getAllFiles())-keys(files))),
            qPrintable(show(keys(files)-keys(native->files()))),qPrintable(show(keys(native->files())-keys(files))),
            qPrintable(show(keys(directories)-keys(native->directories()))),qPrintable(show(keys(native->directories())-keys(directories))));
        for (const auto& path : keys(directories)-keys(native->directories())) {
            std::printf("missing desired-dir=%d desired-file=%d additions=%s removals=%s pending=%d\n",
                manager.watcher.desiredDirectories.contains(path),manager.watcher.desiredFiles.contains(path),
                qPrintable(manager.watcher.additions.join('|')),qPrintable(manager.watcher.removals.join('|')),
                manager.isWorkspaceScanActive());
        }
    }
    return matches;
}
void quiet(WorkspaceManager& manager,const QString& label)
{
    QSignalSpy started(&manager,&WorkspaceManager::workspaceScanStarted);
    QTest::qWait(200);
    check(started.isEmpty() && !manager.isWorkspaceScanActive(),label+": quiet workspace stops scanning");
}

void registrationMutations()
{
    const QStringList cases={"add file","delete file","add empty directory","delete empty directory",
                             "add deep tree","replace same filename","file becomes directory"};
    for (const auto& name:cases) {
        QTemporaryDir fixture;
        const QString deep=fixture.filePath("rtl/deep");
        const QString base=deep+"/base.sv";
        check(writeFile(base) && QDir().mkpath(deep+"/empty"),name+": fixture created");
        WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
        bool injected=false,uncovered=false,mutationOk=false;
        QObject::connect(&manager,&WorkspaceManager::workspaceScanProgress,&manager,[&](const QString&,int count) {
            if (injected || count!=1) return;
            injected=true;
            auto* native=manager.findChild<QFileSystemWatcher*>();
            uncovered=native && !native->directories().contains(deep,Qt::CaseInsensitive);
            if (name=="add file") mutationOk=writeFile(deep+"/added.sv");
            else if (name=="delete file") mutationOk=QFile::remove(base);
            else if (name=="add empty directory") mutationOk=QDir().mkpath(deep+"/added");
            else if (name=="delete empty directory") mutationOk=QDir(deep).rmdir("empty");
            else if (name=="add deep tree") mutationOk=writeFile(deep+"/new/further/added.sv");
            else if (name=="replace same filename")
                mutationOk=QFile::remove(base) && writeFile(base,"module replacement; endmodule\n");
            else mutationOk=QFile::remove(base) && writeFile(base+"/child.sv");
        });
        check(manager.openWorkspace(fixture.path())
                  && waitFor([&]{return injected && !manager.isWorkspaceScanActive();}),name+": handoff settles");
        check(uncovered && mutationOk,name+": mutation occurred before deep native registration");
        check(matchesDisk(manager),name+": final published files and native coverage match disk");
        quiet(manager,name);
        if (name=="replace same filename") {
            QSignalSpy changed(&manager,&WorkspaceManager::fileChanged);
            check(writeFile(base,"module replacement_again; endmodule\n")
                      && waitFor([&]{return !changed.isEmpty();}),"same-name replacement retains the next real file event");
        }
    }
}

void evolvingDirectories()
{
    QTemporaryDir fixture;
    const auto first=fixture.filePath("first/base.sv");
    check(writeFile(first),"evolving tree fixture created");
    WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    int injected=0;
    QObject::connect(&manager,&WorkspaceManager::workspaceScanProgress,&manager,[&](const QString&,int count) {
        if (injected>=3 || count!=injected+1) return;
        const QString folder=fixture.filePath(QString("first/g%1/deep").arg(injected));
        if (!manager.watcher.watcher->directories().contains(folder,Qt::CaseInsensitive)) {
            ++injected;
            check(writeFile(folder+"/added.sv"),"consecutive handoff mutation creates another uncovered directory");
        }
    });
    check(manager.openWorkspace(fixture.path()) && waitFor([&]{return injected==3 && !manager.isWorkspaceScanActive();}),
          "successive new directories converge after the final mutation");
    check(matchesDisk(manager),"successive handoffs publish all final files with coverage");
    quiet(manager,"successive handoffs");
    const auto newTree=fixture.filePath("ordinary/deep/later.sv");
    check(writeFile(newTree) && waitFor([&]{return !manager.isWorkspaceScanActive() && manager.getAllFiles().contains(newTree);}),
          "ordinary native notification discovers a later deep tree");
    check(matchesDisk(manager),"later tree is also registered and reconciled");
    // A membership probe must notice type changes even when the child path is unchanged.
    check(QFile::remove(first) && writeFile(first+"/nested.sv")
              && waitFor([&]{return !manager.isWorkspaceScanActive() && manager.getAllFiles().contains(first+"/nested.sv");}),
          "covered file-to-directory replacement is not mistaken for unchanged membership");
    check(matchesDisk(manager),"entry-kind replacement transfers native coverage");
    quiet(manager,"ordinary directory changes");
}

void restoreAndBackpressure()
{
    QTemporaryDir fixture;
    const auto a=fixture.filePath("a"), b=fixture.filePath("b");
    const auto af=a+"/rtl/deep/a.sv", bf=b+"/rtl/deep/b.sv";
    check(writeFile(af) && writeFile(bf),"cached workspaces created");
    WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    check(manager.openWorkspace(a) && waitFor([&]{return !manager.isWorkspaceScanActive();})
              && manager.openWorkspace(b) && waitFor([&]{return !manager.isWorkspaceScanActive();}),"both workspace caches populated");
    const auto added=a+"/rtl/deep/added.sv";
    bool injected=false;
    QObject::connect(&manager,&WorkspaceManager::workspaceScanProgress,&manager,[&](const QString& path,int count) {
        if (path==a && count==1 && !injected) { injected=true;check(writeFile(added),"cached activation receives a gap mutation"); }
    });
    check(manager.switchWorkspace(0) && manager.restoreSessionScanState({af},true)
              && waitFor([&]{return injected && !manager.isWorkspaceScanActive();}),"restored cached list reconciles through the handoff protocol");
    check(manager.getAllFiles().contains(added) && matchesDisk(manager),"cache/session restoration cannot hide the gap file");

    int peakWorkers=0,peakPending=0;
    QTimer heartbeat;
    QObject::connect(&heartbeat,&QTimer::timeout,&manager,[&] {
        peakWorkers=qMax(peakWorkers,manager.directoryThreadPool.activeThreadCount());
        peakPending=qMax(peakPending,int(manager.pendingScanRequest.has_value()));
    });
    heartbeat.start(1);
    for (int i=0;i<24;++i) manager.switchWorkspace(i%2);
    for (int i=0;i<24;++i) manager.refreshWorkspaceFiles();
    check(manager.pendingScanRequest.has_value(),"burst retains a latest pending request while an old task retires");
    peakPending=qMax(peakPending,int(manager.pendingScanRequest.has_value()));
    check(waitFor([&]{return !manager.isWorkspaceScanActive();}) && manager.getWorkspacePath()==b && matchesDisk(manager),
          "rapid switches and refresh requests publish only the final root");
    heartbeat.stop();
    check(peakWorkers<=1 && peakPending==1 && manager.directoryThreadPool.maxThreadCount()==1,
          "single worker and one latest pending request keep bounded backpressure");
    quiet(manager,"burst completion");
}

void retireAtInstalledBoundary()
{
    for (const QString operation:{"switch","close","destroy"}) {
        QTemporaryDir fixture;
        const QString a=fixture.filePath("a"),b=fixture.filePath("b");
        check(writeFile(a+"/deep/a.sv") && writeFile(b+"/b.sv"),operation+": lifecycle fixtures created");
        QPointer<WorkspaceManager> manager=new WorkspaceManager;
        manager->setRecentWorkspacePersistenceEnabledForTesting(false);
        bool armed=false,retired=false,observedValidation=false;
        QStringList publishedRoots;
        QObject::connect(manager,&WorkspaceManager::workspaceScanFinished,qApp,[&](const QString& path,int,int){publishedRoots.append(path);});
        QObject::connect(manager,&WorkspaceManager::workspaceScanProgress,manager,[&,manager](const QString& path,int count) {
            if (armed || path!=a || count!=1) return;
            armed=true;
            QTimer::singleShot(0,manager,[&,manager] {
                auto completion=std::move(manager->watcher.installed);
                manager->watcher.installed=[&,manager,completion=std::move(completion)] {
                    if (completion) completion();
                    observedValidation=manager && manager->isWorkspaceScanActive() && manager->scanWatcher
                        && manager->watcher.watcher->directories().contains(a+"/deep",Qt::CaseInsensitive);
                    if (operation=="switch") manager->openWorkspace(b);
                    else if (operation=="close") manager->closeWorkspace();
                    else delete manager.data();
                    retired=true;
                };
            });
        });
        check(manager->openWorkspace(a) && waitFor([&]{return retired;}),operation+": retirement runs after native installation");
        check(observedValidation,operation+": coverage-validation worker was queued before retirement");
        check(waitFor([&]{return !manager || !manager->isWorkspaceScanActive();}) && !publishedRoots.contains(a),
              operation+": old validation cannot publish the retired root");
        if (operation=="switch") check(manager && manager->getWorkspacePath()==b && matchesDisk(*manager),"new root owns list and native coverage after old validation cancellation");
        else if (operation=="close") {
            check(waitFor([&]{return manager->watcher.watcher->files().isEmpty() && manager->watcher.watcher->directories().isEmpty();})
                      && manager->getAllFiles().isEmpty(),"closing removes native coverage and published files");
        } else check(manager.isNull(),"destroying manager cancels and joins its worker safely");
        if (manager) delete manager.data();
    }
}

void semanticCoverage()
{
    QTemporaryDir fixture;
    const auto root=fixture.filePath("workspace"),external=fixture.filePath("external/deep");
    const auto include=external+"/config.svh";
    check(writeFile(root+"/rtl/deep/base.sv") && writeFile(include,"`define VALUE 1\n"),"semantic include fixtures created");
    WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    bool injected=false,installedAtNotification=false;
    QSignalSpy semantic(&manager,&WorkspaceManager::semanticInputsChanged);
    QObject::connect(&manager,&WorkspaceManager::semanticInputsChanged,&manager,[&] {
        installedAtNotification=manager.watcher.watcher->files().contains(include,Qt::CaseInsensitive);
    });
    QObject::connect(&manager,&WorkspaceManager::workspaceScanProgress,&manager,[&](const QString&,int count) {
        if (injected || count!=1) return;
        injected=true;
        manager.applySemanticWatchPaths(root,{include},{external});
        check(writeFile(include,"`define VALUE 2\n"),"external include changes before its watch installation");
    });
    check(manager.openWorkspace(root) && waitFor([&]{return injected && !manager.isWorkspaceScanActive() && !semantic.isEmpty();}),
          "directory handoff does not overwrite pending semantic include revalidation");
    check(installedAtNotification && semantic.first().first().toString()==root,
          "semantic revalidation runs after native coverage for the current root");
    semantic.clear();
    manager.applySemanticWatchPaths(root,{include},{external});
    QTest::qWait(200);
    check(semantic.isEmpty() && !manager.isWorkspaceScanActive(),"unchanged semantic coverage does not self-trigger a scan loop");
    check(writeFile(external+"/added.svh","`define ADDED 1\n") && waitFor([&]{return !semantic.isEmpty();}),
          "real external include directory changes still invalidate semantics");
    check(manager.getAllFiles()==QStringList{root+"/rtl/deep/base.sv"},"external semantic directories do not enter workspace file membership");
    quiet(manager,"external semantic notification");
}
}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    auto& theme=ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 2;
    theme.applyToApplication();
    registrationMutations();evolvingDirectories();restoreAndBackpressure();retireAtInstalledBoundary();semanticCoverage();
    std::printf("checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
