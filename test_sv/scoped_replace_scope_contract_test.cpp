#include <QtWidgets>
#include <QtTest>
#include "mainwindow.h"
#include "analysisscheduler.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "tabmanager.h"
#include "shareddocument.h"
#include "scopedsearchpanel.h"
#include "applicationthememanager.h"
#include <cstdio>
#include <functional>

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
    while (!ready() && timer.elapsed()<5000) QTest::qWait(5);
    return ready();
}
bool write(const QString& path,const QString& text)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)
        && file.write(text.toUtf8())==text.toUtf8().size();
}
struct State {
    QPointer<QTextDocument> document;
    QString text;
    int revision,undo,redo;
    bool modified;
};
QList<State> capture(TabManager& tabs)
{
    QList<State> result;
    for (auto* editor:tabs.openEditors()) {
        auto* document=editor->document();
        result.append({document,document->toPlainText(),document->revision(),document->availableUndoSteps(),
                       document->availableRedoSteps(),document->isModified()});
    }
    return result;
}
bool unchanged(const QList<State>& before,TabManager& tabs)
{
    if (tabs.openEditors().size()!=before.size()) return false;
    for (const auto& state:before) {
        auto* doc=state.document.data();
        if (!doc || doc->toPlainText()!=state.text || doc->revision()!=state.revision
            || doc->availableUndoSteps()!=state.undo || doc->availableRedoSteps()!=state.redo
            || doc->isModified()!=state.modified) return false;
    }
    return true;
}
void scenario(const QString& action,bool initiallyOpen=true,
              ScopedSearchScope scope=ScopedSearchScope::Workspace)
{
    QTemporaryDir fixture;
    const QString root=fixture.filePath("a"),other=fixture.filePath("b"),file=root+"/rtl/unit.sv";
    const QString original="module unit;\n initial begin\n  logic target;\n end\nendmodule\n";
    check(write(file,original) && QDir().mkpath(other),action+": fixture");
    MainWindow window;
    window.analysisScheduler->shutdown();
    auto& manager=*window.workspaceManager;
    auto& tabs=*window.tabManager;
    manager.setRecentWorkspacePersistenceEnabledForTesting(false);
    check(manager.openWorkspace(root) && waitFor([&]{return !manager.isWorkspaceScanActive();}),action+": workspace ready");
    if (initiallyOpen) check(tabs.openFileInTab(file),action+": actual source editor opened");
    if (auto* editor=tabs.getCurrentEditor()) {
        QTextCursor cursor=editor->textCursor();cursor.setPosition(original.indexOf("target"));editor->setTextCursor(cursor);
    }
    auto* panel=window.findChild<ScopedSearchPanelCoordinator*>()->panel();
    panel->setScope(scope);panel->setQueryText("target");panel->setReplacementText("renamed");
    if (action=="dry-run") panel->dryRunControl()->setChecked(true);
    check(waitFor([&]{panel->refresh();return panel->displayedResultCount()==1;}),action+": real scoped search finds source");
    check(panel->buildReplacePreview().ready() && panel->applyButton()->isEnabled(),action+": preview prepared");
    if (action=="switch empty" || action=="switch other") {
        if (action=="switch other") check(write(other+"/other.sv","module other; endmodule\n"),"other workspace fixture");
        check(manager.openWorkspace(other) && waitFor([&]{return !manager.isWorkspaceScanActive();}),action+": switched");
    } else if (action=="close workspace") manager.closeWorkspace();
    else if (action=="exclude source") {
        check(manager.setIgnoredDirectories({root+"/rtl"}) && waitFor([&]{return !manager.isWorkspaceScanActive()
            && !manager.getSystemVerilogFiles().contains(file);}),action+": file leaves project scope");
    } else if (action=="delete source") {
        check(QFile::remove(file) && waitFor([&]{return !manager.isWorkspaceScanActive()
            && !manager.getAllFiles().contains(file);}),action+": file leaves disk and scope");
    } else if (action=="close active") check(tabs.closeAllTabs(),action+": active scope emptied");
    else if (action=="readonly") tabs.sharedDocumentForEditor(tabs.getCurrentEditor())->setReadOnly(true);
    else if (action=="stale") tabs.getCurrentEditor()->insertPlainText("changed");
    else if (action=="external") check(write(file,original+"// external\n"),action+": disk changed after preview");
    const auto before=capture(tabs);
    if (action=="cancel") panel->cancelReplace();
    else panel->confirmReplace();
    const bool applied=action=="normal";
    if (applied) {
        auto* editor=tabs.getDocumentModel()->editorForFile(file);
        check(editor && editor->toPlainText().contains("renamed") && panel->undoButton()->isEnabled(),"normal confirmation applies through actual transaction adapter");
        panel->undoReplace();
        check(editor && editor->toPlainText()==original,"actual workflow undo restores source");
    } else {
        check(unchanged(before,tabs),action+": confirmation preserves every buffer, revision and undo/redo state");
        check(!panel->replaceStatus()->text().startsWith("Applied"),action+": invalid scope never reports Applied");
        if (!initiallyOpen) check(!tabs.getDocumentModel()->editorForFile(file),action+": confirmation does not open the old source");
    }
    if (action!="delete source") {
        QFile disk(file);check(disk.open(QIODevice::ReadOnly) && disk.readAll()
            ==(action=="external"?original+"// external\n":original).toUtf8(),action+": disk source preserved");
    }
    if (action=="switch empty") {
        check(manager.openWorkspace(root) && waitFor([&]{return !manager.isWorkspaceScanActive();}),"failed scope can return to original workspace");
        panel->refresh();
        check(panel->buildReplacePreview().ready(),"new search can rebuild preview after rejected confirmation");
        panel->confirmReplace();
        auto* live=tabs.getDocumentModel()->editorForFile(file);
        check(live && live->toPlainText().contains("renamed"),"rebuilt authoritative preview applies");
        panel->undoReplace();
        check(live && live->toPlainText()==original,"retry retains normal undo");
    }
    for (auto* view:tabs.openEditors()) view->document()->setModified(false);
    tabs.clearCrashRecoveryAfterNormalClose();
}
}
int main(int argc,char** argv)
{
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",settings.filePath("sessions.ini").toUtf8());
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QStandardPaths::setTestModeEnabled(true);
    auto& theme=ApplicationThemeManager::instance();
    if (!theme.selectBackend(UiStyleBackend::Ela)) return 2;
    theme.applyToApplication();
    for (const QString action:{"switch empty","close workspace","exclude source","delete source"}) {
        scenario(action,true);scenario(action,false);
    }
    scenario("switch other");
    for (const auto scope:{ScopedSearchScope::File,ScopedSearchScope::Module,ScopedSearchScope::SyntaxBlock})
        scenario("close active",true,scope);
    for (const QString action:{"normal","readonly","stale","external","cancel","dry-run"}) scenario(action);
    QThreadPool::globalInstance()->waitForDone();
    std::printf("checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
