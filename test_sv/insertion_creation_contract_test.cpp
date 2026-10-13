#include <QtWidgets>
#include <QtTest>
#include <memory>
#include <functional>
#include <optional>
#include <atomic>
#define private public
#include "mainwindow.h"
#include "globalcontrolcoordinator.h"
#include "editorcoordinator.h"
#undef private
#include "analysisscheduler.h"
#include "completionservice.h"
#include "editorinsertpaletteservice.h"
#include "usertemplateservice.h"
#include "filecreation.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "workspacefileoperationservice.h"
#include "semanticindexsnapshot.h"
#include "semantic_fixture_records.h"
#include "testuistyle.h"

namespace {
int checks=0, failures=0;
void check(bool ok,const char* label) {
    ++checks; failures+=!ok; std::printf("[%s] %s\n",ok?"PASS":"FAIL",label);
}
bool write(const QString& path,const QByteArray& content) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(content)==content.size();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray(); }
void exercise(const QString& root) {
    MainWindow window;
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    window.analysisScheduler->shutdown();
    const auto sourcePath=QDir(root).filePath("source.sv");
    const QByteArray source="module source;\n\nendmodule\n";
    const auto headerPath=QDir(root).filePath("existing.svh");
    check(write(sourcePath,source) && write(headerPath,"// existing\n"),"insertion fixtures written");
    check(window.workspaceManager->openWorkspace(root),"real workspace opened");
    QElapsedTimer wait; wait.start();
    while(window.workspaceManager->isWorkspaceScanActive() && wait.elapsed()<10000) QTest::qWait(5);
    check(!window.workspaceManager->isWorkspaceScanActive(),"workspace scan settled");
    check(window.tabManager->openFileInTab(sourcePath),"source tab opened");
    auto* editor=window.tabManager->getCurrentEditor();
    window.show(); window.activateWindow();
    const auto reset=[&] {
        window.tabManager->activateOpenFile(sourcePath); editor->setReadOnly(false);
        editor->setPlainText(QString::fromUtf8(source));
        QTextCursor c(editor->document()); c.setPosition(15); editor->setTextCursor(c);
        editor->setFocus(); QApplication::processEvents();
    };
    reset();
    auto* coordinator=window.globalControlCoordinator.get();
    const auto context=coordinator->contextProvider();
    for(const auto& filter:QStringList{"import p", "include existing.svh", "new header readonly.svh", "instantiate"})
        check(coordinator->itemProvider(GlobalControlCategory::Templates,filter,context).isEmpty(),
            "Templates exposes no legacy structured creation route");
    check(!QFileInfo::exists(QDir(root).filePath("readonly.svh")),"removed palette route creates no header");

    WorkspaceFileOperationService files;
    // Every fault enters the same callers as the product. The probe is reached
    // only after their final preflight and runs an actual independent writer.
    for(int entry=0;entry<3;++entry) for(int scenario=0;scenario<5;++scenario) {
        reset();
        const QString name=QString("entry_%1_case_%2.%3").arg(entry).arg(scenario).arg(entry==2?"json":"svh");
        const auto path=QDir(root).filePath(name); const QByteArray competitor="OTHER WRITER MUST SURVIVE";
        if(scenario==1) check(write(path,competitor),"preexisting creator target prepared");
        int boundaryHits=0;
        auto old=FileCreation::exchangeProbeForTesting([&](FileCreation::Stage stage,const QString& target) {
            if(QDir::cleanPath(target)!=QDir::cleanPath(path)) return true;
            if(scenario==2 && stage==FileCreation::Stage::BeforePublish) {
                ++boundaryHits; check(write(path,competitor),"competing writer creates target after final preflight");
            }
            if((scenario==3 || scenario==4) && stage==FileCreation::Stage::BeforeFlush) {
                ++boundaryHits;
                if(scenario==4) check(write(path,competitor),"competing target exists when staged write fails");
                return false;
            }
            return true;
        });
        const int tabs=window.tabManager->getDocumentModel()->openDocuments().size();
        bool success=false; QString reason;
        if(entry==0) { const auto result=files.apply(files.planCreateFile(root,root,name)); success=result.succeeded; reason=result.failureReason; }
        if(entry==1) {
            IncludeNewHeaderRequest request; request.fileStem=QFileInfo(name).completeBaseName();
            request.extension="svh"; request.currentFileName=sourcePath; request.templateName="empty";
            request.templateBody="// header\n__CURSOR__"; request.cursorToken="__CURSOR__";
            const auto result=window.editorCoordinator->createIncludeNewHeader(request);
            success=result.success; reason=result.errorMessage;
        }
        if(entry==2) success=window.ensureUserTemplateJsonFile(path,&reason);
        FileCreation::exchangeProbeForTesting(std::move(old));
        const bool expected=scenario==0 || (entry==2 && scenario==1);
        check(success==expected && (success || !reason.isEmpty()),"creator reports first success preexisting policy and boundary failures");
        if(scenario==0) {
            check(QFileInfo::exists(path) && (entry==0?read(path).isEmpty():!read(path).isEmpty()),"creator publishes complete expected content");
        } else {
            check(scenario==3?!QFileInfo::exists(path):read(path)==competitor,"failure cleanup preserves another writer's target");
            check(editor->toPlainText()==QString::fromUtf8(source)
                && editor->document()->availableUndoSteps()==0
                && window.tabManager->getDocumentModel()->openDocuments().size()==tabs,
                "failed create has no source insertion undo step or new tab");
        }
        check(QDir(root).entryList({name+".creating.*"},QDir::Files).isEmpty(),"creator removes only its owned staging file");
        if(scenario>=2) check(boundaryHits==1,"fault injection reaches actual post-preflight I/O boundary exactly once");
    }
    for(const auto& doc:window.tabManager->getDocumentModel()->openDocuments())
        if(auto* e=window.tabManager->getDocumentModel()->editorForFile(doc.fileName)) e->document()->setModified(false);
    window.tabManager->clearCrashRecoveryAfterNormalClose();
    SemanticIndex::getInstance()->clearSemanticState();
}
}
int main(int argc,char** argv) {
    setbuf(stdout,nullptr); QTemporaryDir settings;
    QCoreApplication::setOrganizationName("ZeroSlackCreationContractTest"); QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",settings.filePath("sessions.ini").toUtf8());
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QStandardPaths::setTestModeEnabled(true);
    if(!initializeUiStyleForTest()) return 2;
    QTemporaryDir workspace; exercise(workspace.path());
    std::printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
}
