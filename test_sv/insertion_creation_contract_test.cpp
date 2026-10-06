#include <QtWidgets>
#include <QtTest>
#include <memory>
#include <functional>
#include <optional>
#include <atomic>
#define private public
#include "mainwindow.h"
#include "globalcontrolcoordinator.h"
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
    const QString package="package p; parameter N=1; endpackage\n";
    const auto packagePath=QDir(root).filePath("p.sv");
    const auto record=SemanticFixtureRecordBuilder("p",SymbolTaxonomy::DeclarationKind::Package)
        .withFile(packagePath).withLocalHandle(1).withLine(1,9).record();
    const auto packageSnapshot=std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords({record},{},{},{{packagePath,package}}));
    auto* coordinator=window.globalControlCoordinator.get();
    const auto capture=[&](GlobalControlItemOperation operation,const QString& filter) {
        SemanticIndex::getInstance()->setSnapshot(packageSnapshot);
        auto context=coordinator->contextProvider();
        const auto items=coordinator->itemProvider(GlobalControlCategory::Templates,filter,context);
        for(const auto& item:items) if(item.operation==operation) return item;
        const auto site=PackageToolService::analyzePackageImportSite(context.documentText,context.cursorPosition,context.cursorPosition);
        std::printf("Missing operation=%d available=%d pos=%d module=%s site=%d reason=%s items=%d\n",int(operation),context.editorAvailable,
            context.cursorPosition,context.moduleName.toUtf8().constData(),site.valid,site.failureMessage.toUtf8().constData(),int(items.size()));
        CommandCompletionQuery query; query.fileName=context.fileName; query.commandKind=CompletionCommandKind::Package;
        std::printf("Package records=%d\n",int(CompletionService::getInstance()->findCommandCompletionSymbolRecords(query).size()));
        check(false,"production provider supplies requested structured operation"); return GlobalControlItem{};
    };
    const QList<GlobalControlItemOperation> operations={GlobalControlItemOperation::InsertPackageImport,
        GlobalControlItemOperation::InsertHeaderInclude,GlobalControlItemOperation::CreateHeader};
    const QStringList filters={"import p","include existing.svh","new header readonly.svh"};
    for(int i=0;i<3;++i) {
        reset(); auto item=capture(operations[i],filters[i]); editor->setReadOnly(true);
        const auto before=editor->toPlainText(); const int undo=editor->document()->availableUndoSteps();
        const auto tabs=window.tabManager->getDocumentModel()->openDocuments().size();
        QString reason; bool success=false;
        if(i==0) success=editor->insertPackageImport("p",&reason);
        if(i==1) success=editor->insertHeaderInclude("existing.svh",&reason);
        if(i==2) success=editor->createAndInsertHeader("readonly.svh",&reason);
        check(!success && reason.contains("read-only"),"public structured insertion rejects editor read-only state");
        coordinator->dispatch(item);
        check(editor->toPlainText()==before && editor->document()->availableUndoSteps()==undo
            && window.tabManager->getDocumentModel()->openDocuments().size()==tabs
            && !QFileInfo::exists(QDir(root).filePath("readonly.svh")),
            "real palette and public readonly attempts leave text undo files and tabs unchanged");
    }
    reset(); auto stale=capture(GlobalControlItemOperation::CreateHeader,"new header stale.svh");
    editor->insertPlainText(" "); const auto changed=editor->toPlainText(); coordinator->dispatch(stale);
    check(editor->toPlainText()==changed && !QFileInfo::exists(QDir(root).filePath("stale.svh")),
        "structured palette rejects changed source revision before file creation");
    reset(); stale=capture(GlobalControlItemOperation::CreateHeader,"new header stale.svh");
    check(window.tabManager->openFileInTab(headerPath),"different palette target opened");
    coordinator->dispatch(stale);
    check(!QFileInfo::exists(QDir(root).filePath("stale.svh")) && editor->toPlainText()==QString::fromUtf8(source),
        "structured palette rejects another document instance");
    for(int i=0;i<3;++i) {
        reset(); auto item=capture(operations[i],i==2?"new header writable.svh":filters[i]);
        coordinator->dispatch(item);
        check(editor->toPlainText()!=QString::fromUtf8(source),"fresh writable structured palette insertion succeeds");
        editor->undo(); check(editor->toPlainText()==QString::fromUtf8(source),"structured insertion is one reversible text operation");
        editor->redo(); check(editor->toPlainText()!=QString::fromUtf8(source),"structured insertion redo restores text");
    }

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
        if(entry==1) success=editor->createAndInsertHeader(name,&reason);
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
