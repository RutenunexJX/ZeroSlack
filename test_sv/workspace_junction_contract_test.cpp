#include <QtWidgets>
#include <QtTest>
#include <QtConcurrent>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include "applicationthememanager.h"
#include "editorfileidentity.h"
// Scheduling is observed, while directories and QFileSystemWatcher are real.
#define private public
#include "workspacemanager.h"
#undef private
#include <cstdio>

namespace {
int checks=0,failures=0;
void check(bool ok,const QString& label)
{
    ++checks;failures+=!ok;
    std::printf("[%s] %s\n",ok?"PASS":"FAIL",qPrintable(label));std::fflush(stdout);
}
bool waitFor(const std::function<bool()>& ready)
{
    QElapsedTimer timer;timer.start();
    while (!ready() && timer.elapsed()<10000) QTest::qWait(5);
    return ready();
}
bool write(const QString& path,const QByteArray& text="module unit; endmodule\n")
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(text)==text.size();
}
bool junction(const QString& alias,const QString& target)
{
    QProcess process;
    process.start("cmd.exe",{"/d","/s","/c","mklink","/J",QDir::toNativeSeparators(alias),QDir::toNativeSeparators(target)});
    return process.waitForFinished(10000) && process.exitCode()==0 && QFileInfo(alias).isJunction();
}
QSet<QString> keys(const QStringList& paths)
{
    QSet<QString> result;for (const auto& path:paths) result.insert(QDir::cleanPath(path).toCaseFolded());return result;
}
bool matches(WorkspaceManager& manager,const QStringList& files,const QStringList& directories)
{
    auto* native=manager.findChild<QFileSystemWatcher*>();
    return native && keys(manager.getAllFiles())==keys(files) && keys(native->files())==keys(files)
        && keys(native->directories())==keys(directories);
}
void quiet(WorkspaceManager& manager,const QString& name)
{
    QSignalSpy starts(&manager,&WorkspaceManager::workspaceScanStarted);QTest::qWait(300);
    check(starts.isEmpty() && !manager.isWorkspaceScanActive(),name+": no self-sustaining rescan");
}
void cycle(const QString& kind,const QString& gap=QString())
{
    // No recursive deletion: even failure leaves each named junction fixture intact.
    QTemporaryDir fixture;fixture.setAutoRemove(false);
    const QString root=fixture.filePath("a"),target=fixture.filePath("b"),alias=root+"/alias";
    check(write(root+"/unit.sv") && write(target+"/external.sv"),kind+": fixture");
    check(junction(alias,kind=="self"?root:target),kind+": real junction");
    if (kind=="indirect") check(junction(target+"/back",root),"indirect: return junction");
    if (gap=="delete") check(QDir().mkpath(root+"/empty"),"cyclic gap empty directory fixture");
    std::printf("retained=%s kind=%s physicalAlias=%s\n",qPrintable(fixture.path()),qPrintable(kind),qPrintable(EditorFileIdentity::lookupKey(alias)));
    WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    bool injected=false;
    if (!gap.isEmpty()) QObject::connect(&manager,&WorkspaceManager::workspaceScanProgress,&manager,[&](const QString&,int count) {
        if (injected || count!=1) return;
        injected=true;
        check(!manager.watcher.watcher->directories().contains(alias,Qt::CaseInsensitive),"cyclic mutation precedes pruned-node watch installation");
        check(gap=="add"?write(root+"/new/added.sv"):QDir(root).rmdir("empty"),"cyclic registration gap mutation");
    });
    QSignalSpy starts(&manager,&WorkspaceManager::workspaceScanStarted);
    QElapsedTimer elapsed;elapsed.start();
    check(manager.openWorkspace(root) && waitFor([&]{return !manager.isWorkspaceScanActive();}),kind+": quiet initial scan completes");
    QStringList files=kind=="self"?QStringList{root+"/unit.sv"}:QStringList{root+"/unit.sv",alias+"/external.sv"};
    QStringList dirs=kind=="self"?QStringList{root,alias}:QStringList{root,alias,alias+"/back"};
    if (gap=="add") { files.append(root+"/new/added.sv");dirs.append(root+"/new"); }
    if (!gap.isEmpty()) check(injected,"cyclic handoff actually exercises the registration gap");
    check(matches(manager,files,dirs),kind+": only finite logical files and expected native coverage");
    std::printf("measurement kind=%s elapsedMs=%lld scans=%lld files=%lld nativeFiles=%lld nativeDirs=%lld\n",
        qPrintable(kind),elapsed.elapsed(),starts.size(),manager.getAllFiles().size(),manager.watcher.watcher->files().size(),manager.watcher.watcher->directories().size());
    quiet(manager,kind+" initial");
    QSignalSpy changed(&manager,&WorkspaceManager::fileChanged);
    check(write(root+"/unit.sv","module edited; endmodule\n") && waitFor([&]{return !changed.isEmpty();}),kind+": later source content event survives pruning");
    check(write(root+"/later.sv") && waitFor([&]{return !manager.isWorkspaceScanActive() && manager.getAllFiles().contains(root+"/later.sv");}),kind+": later source membership discovered");
    auto more=files;more.append(root+"/later.sv");
    check(matches(manager,more,dirs),kind+": later publication does not grow recursive aliases");
    quiet(manager,kind+" later");
    check(QFile::remove(root+"/later.sv") && waitFor([&]{return !manager.isWorkspaceScanActive() && !manager.getAllFiles().contains(root+"/later.sv");}),kind+": deletion converges");
    check(matches(manager,files,dirs),kind+": deletion leaves correct watches");
    manager.closeWorkspace();
    check(waitFor([&]{return manager.watcher.watcher->files().isEmpty() && manager.watcher.watcher->directories().isEmpty();}),kind+": close retires all watches");
}
void aliasesAndHandoff()
{
    QTemporaryDir fixture;fixture.setAutoRemove(false);
    const QString root=fixture.filePath("a"),target=fixture.filePath("external"),x=root+"/x",y=root+"/y";
    check(write(root+"/ordinary/root.sv") && write(target+"/unit.sv"),"acyclic aliases fixture");
    check(junction(x,target) && junction(y,target),"two independent acyclic aliases to one target");
    std::printf("retained=%s kind=acyclic\n",qPrintable(fixture.path()));
    WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    bool injected=false,uncovered=false;
    QObject::connect(&manager,&WorkspaceManager::workspaceScanProgress,&manager,[&](const QString&,int count) {
        if (injected || count!=3) return;
        injected=true;uncovered=!manager.watcher.watcher->directories().contains(x,Qt::CaseInsensitive);
        check(write(target+"/deep/added.sv"),"gap mutation adds target subtree before alias registration");
    });
    check(manager.openWorkspace(root) && waitFor([&]{return injected && !manager.isWorkspaceScanActive();}),"acyclic gap scan completes");
    check(uncovered,"junction target change was in the registration gap");
    const QStringList files={root+"/ordinary/root.sv",x+"/unit.sv",y+"/unit.sv",x+"/deep/added.sv",y+"/deep/added.sv"};
    const QStringList dirs={root,root+"/ordinary",x,y,x+"/deep",y+"/deep"};
    check(matches(manager,files,dirs),"independent aliases stay visible with complete gap coverage");
    const auto physical=EditorFileIdentity::lookupKeys(manager.getSystemVerilogFiles());
    QSet<QString> unique;for (const auto& identity:physical) unique.insert(identity);
    check(unique.size()==3 && physical.value(x+"/unit.sv")==physical.value(y+"/unit.sv"),
          "logical aliases retain the existing unique physical document identity");
    quiet(manager,"acyclic handoff");
    const auto replacement=x+"/unit.sv";
    check(QFile::remove(replacement) && write(replacement+"/nested.sv")
        && waitFor([&]{return !manager.isWorkspaceScanActive() && manager.getAllFiles().contains(y+"/unit.sv/nested.sv");}),
        "source-to-directory replacement through junction reaches both aliases");
    check(!manager.getAllFiles().contains(replacement) && manager.watcher.watcher->directories().contains(replacement,Qt::CaseInsensitive),
        "kind replacement keeps correct native directory coverage");
    quiet(manager,"acyclic kind replacement");
    const QString other=fixture.filePath("ordinary");check(write(other+"/final.sv"),"rapid switch destination");
    for (int i=0;i<20;++i) manager.openWorkspace(i%2?other:root);
    check(waitFor([&]{return !manager.isWorkspaceScanActive();}) && manager.getWorkspacePath()==other
          && matches(manager,{other+"/final.sv"},{other}),"rapid switch publishes only the final ordinary workspace");
    check(manager.directoryThreadPool.maxThreadCount()==1 && !manager.pendingScanRequest.has_value(),"single worker drains the latest request");
    quiet(manager,"rapid switch");
}

void overlappingNativeKinds()
{
    QTemporaryDir fixture;
    const auto node=fixture.filePath("role.sv");
    check(write(node),"overlapping native kinds fixture");
    WorkspaceManager manager;manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    check(manager.openWorkspace(fixture.path()) && waitFor([&]{return !manager.isWorkspaceScanActive();}),"native kind fixture scanned");
    auto* native=manager.watcher.watcher.get();
    const auto drainRegistration=[&] {
        // This component fixture controls reconciliation before queued native
        // events. One advance may exhaust its 4 ms budget before additions.
        for (int step=0;step<8 && (!manager.watcher.removals.isEmpty() || !manager.watcher.additions.isEmpty());++step)
            manager.watcher.advance();
        check(manager.watcher.removals.isEmpty() && manager.watcher.additions.isEmpty(),"bounded native registration batches complete");
    };
    // Reproduce the native state a file rewatch can create before enumeration
    // observes its new directory kind. No queued removal event is delivered yet.
    check(QFile::remove(node) && QDir().mkpath(node) && native->addPath(node)
          && native->files().contains(node) && native->directories().contains(node),
          "real Qt backend exposes both registrations for one replaced path");
    manager.watcher.apply({{}, {fixture.path(),node}});
    drainRegistration();
    check(native->directories().contains(node) && !native->files().contains(node),"reconciliation retires old file ownership");
    check(QDir(fixture.path()).rmdir("role.sv") && write(node),"overlapping path returns to a file");
    manager.watcher.apply({{node}, {fixture.path()}});
    drainRegistration();
    check(waitFor([&]{return native->files().contains(node) && !native->directories().contains(node)
                      && !manager.isWorkspaceScanActive();}),
          "hidden former file registration cannot suppress the current native file watch");
    quiet(manager,"overlapping native kinds");
    QSignalSpy changed(&manager,&WorkspaceManager::fileChanged);
    check(write(node,"module later; endmodule\n") && waitFor([&]{return !changed.isEmpty();}),
          "next real file event survives both kind transitions");
}
}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);
    auto& theme=ApplicationThemeManager::instance();if (!theme.selectBackend(UiStyleBackend::Ela)) return 2;theme.applyToApplication();
#ifdef Q_OS_WIN
    cycle("self");cycle("indirect");cycle("self","add");cycle("self","delete");aliasesAndHandoff();overlappingNativeKinds();
#else
    std::printf("Windows junction fixture unavailable on this platform\n");return 77;
#endif
    std::printf("checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
