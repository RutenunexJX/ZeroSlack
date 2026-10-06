#include <QtWidgets>
#include <QtTest>
#include "actionregistry.h"
#include "editoractioncontextservice.h"
#include "editorfileidentity.h"
#include "hierarchyservice.h"
#include "instancepairconnectionpanel.h"
#include "instancepairconnectionworkflow.h"
#include "multisignalpropagationpanel.h"
#include "mycodeeditor.h"
#include "panellayoutcontroller.h"
#include "rtlactioncoordinator.h"
#include "semanticdockcoordinator.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "workspacemanager.h"
#include "workspaceeditdocumentmanager.h"
#include "workspaceedittransactionservice.h"
#include "testuistyle.h"

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks; failures += !ok; std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
}
bool write(const QString& path, const QString& text) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size();
}
bool directoryAlias(const QString& target, const QString& alias) {
#ifdef Q_OS_WIN
    QProcess process;
    process.start("cmd.exe", {"/d", "/s", "/c", "mklink", "/J", QDir::toNativeSeparators(alias), QDir::toNativeSeparators(target)});
    return process.waitForFinished(10000) && process.exitCode() == 0 && QFileInfo(alias).isDir();
#else
    return QFile::link(target, alias);
#endif
}
std::shared_ptr<const SemanticIndexSnapshot> publication(const QHash<QString, QString>& contents, const QStringList& order) {
    SlangManager slang;
    const auto parsed = slang.analyzeOverlayWorkspace(contents, order, {}, {}, {}, {true,true,true});
    check(parsed.error.isEmpty() && parsed.diagnostics.isEmpty(), "workflow fixture is actual zero-diagnostic Slang input");
    auto snapshot = SemanticIndexSnapshot::fromSymbolRecords(parsed.symbols, {}, {}, contents);
    SmartRelationshipBuilder builder(nullptr, &slang);
    QList<SemanticRelationship> relationships;
    for (auto it=parsed.relationships.cbegin(); it!=parsed.relationships.cend(); ++it) {
        const auto source = snapshot.cachedFileSource(it.key());
        const auto edges = builder.computeRelationships(it.key(), source.text, snapshot.getSymbolRecords(it.key()), &snapshot, {}, {}, &it.value());
        for (const auto& edge : edges) {
            SemanticRelationship relation; relation.fromId=edge.fromId; relation.toId=edge.toId; relation.type=edge.type;
            relation.evidenceText=edge.context; relation.evidenceRange=edge.evidenceRange; relation.confidence=edge.confidence;
            relation.fromAccessPath=edge.fromAccessPath; relation.toAccessPath=edge.toAccessPath;
            relation.exactValueForward=edge.exactValueForward; relationships.append(relation);
        }
    }
    return std::make_shared<SemanticIndexSnapshot>(snapshot.withAdditionalRelationships(relationships));
}
void workflowCases(const QString& root) {
    const QString real=QDir(root).filePath(QString::fromUtf8("MixedCase-源码")); QDir().mkpath(real);
    const QString source=QDir(real).filePath("SourceLeaf.sv"), sink=QDir(real).filePath("SinkLeaf.sv");
    const QString top=QDir(real).filePath("Top.sv"), empty=QDir(real).filePath("Empty.sv");
    const QString sourceText="module source_leaf(\n    input logic clk\n);\n    logic [7:0] payload;\n    logic [3:0] flags;\nendmodule\n";
    const QString sinkText="module sink_leaf(\n    input logic clk\n);\nendmodule\n";
    const QString topText="module top(\n    input logic clk\n);\n    source_leaf u_source(\n        .clk(clk)\n    );\n    sink_leaf u_sink(\n        .clk(clk)\n    );\nendmodule\n";
    const QHash<QString,QString> contents{{source,sourceText},{sink,sinkText},{top,topText},{empty,{}}};
    for (auto it=contents.cbegin(); it!=contents.cend(); ++it) check(write(it.key(),it.value()), "saved workflow source written");
    const QString aliasRoot=QDir(root).filePath("DirectoryAlias");
    const bool aliasCreated=directoryAlias(real,aliasRoot);
    check(aliasCreated, "supported directory alias is created");
    const QString alias=QDir(aliasRoot).filePath("SourceLeaf.sv");
    QMainWindow host;
    auto* tabsWidget=new QTabWidget(&host); host.setCentralWidget(tabsWidget);
    TabManager tabs(tabsWidget);
    WorkspaceManager workspace; workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
    check(workspace.openWorkspace(real), "workflow product workspace opens");
    QElapsedTimer timer; timer.start();
    while (workspace.isWorkspaceScanActive() && timer.elapsed()<10000) QTest::qWait(5);
    tabs.setWorkspaceScope({real},real);
    const QStringList order{source,sink,top,empty};
    QStringList inputFiles=order;
#ifdef Q_OS_WIN
    inputFiles += QStringList{source.toUpper(),QDir::toNativeSeparators(source)};
#endif
    if (aliasCreated) inputFiles.append(alias);
    check(workspace.restoreSessionScanState(inputFiles,true), "workspace publishes lexical aliases of the same physical file");
    QString caseAlias=source;
#ifdef Q_OS_WIN
    caseAlias=source.toUpper();
#endif
    check(tabs.openFileInTab(source) && tabs.openFileInTab(caseAlias), "mixed-case alias opens the same real Qt document");
    check(tabsWidget->count()==1, "physical aliases do not create duplicate primary tabs");
    auto* editor=tabs.getCurrentEditor(); if (!editor) return;
    auto* index=SemanticIndex::getInstance(); index->clearSemanticState();
    index->setSnapshot(publication(contents,order));
    const auto snapshot=index->snapshot();
    for (const auto& name : inputFiles) {
        const auto resolved=snapshot->cachedFileSource(name);
        check(resolved.exists && resolved.text==contents.value(EditorFileIdentity::physicalPath(name)),
              "shared source query accepts display case, slash, Unicode and directory aliases");
    }
    check(snapshot->cachedFileSource(empty).exists && snapshot->cachedFileSource(empty).text.isEmpty()
          && !snapshot->cachedFileSource(QDir(real).filePath("Absent.sv")).exists, "empty source is distinct from missing source");
    auto* hierarchy=HierarchyService::getInstance();
    const QSet<QString> originalFiles(order.cbegin(),order.cend());
    const auto design=hierarchy->getDesignHierarchyReport({"top"},"top",originalFiles);
    QString sourcePath,sinkPath;
    for (const auto& node : design.nodes) {
        if (node.instanceName=="u_source") sourcePath=node.instancePath;
        if (node.instanceName=="u_sink") sinkPath=node.instancePath;
    }
    check(!sourcePath.isEmpty() && !sinkPath.isEmpty(), "real semantic publication resolves both hierarchy instances");
    editor->setHierarchyInstanceContext({real,"top",sourcePath});
    QTextCursor cursor(editor->document()); cursor.setPosition(sourceText.indexOf("payload")+2); editor->setTextCursor(cursor);
    SemanticDockCoordinator docks(&host,&tabs,&workspace,nullptr,nullptr); docks.setup();
    PanelLayoutController layout(&host); layout.registerBottomPanel("connections",docks.connectionsDock()); layout.finalize();
    EditorActionContextService contextService(index,hierarchy);
    contextService.updateWorkspaceContext(workspace.projectSnapshot());
    RtlActionCoordinatorCallbacks callbacks;
    callbacks.resolveContext=[&](const EditorSemanticContext& context) {
        EditorActionContextQuery query; query.editorContext=context;
        const auto captured=index->snapshot()->cachedFileSource(context.fileName);
        query.semanticStatus.state=captured.exists && captured.text==context.documentText
            ? DocumentSemanticState::Current : DocumentSemanticState::Stale;
        return contextService.resolve(query);
    };
    RtlActionCoordinator coordinator(&tabs,&workspace,&docks,&layout,&host,callbacks);
    WorkspaceEditTransactionService::getInstance()->clearHistory();
    ActionInvocation invocation; invocation.parameters.insert("signalNames",QStringList{"payload","flags"});
    const auto* multiAction=findActionById(QString::fromLatin1(ActionIds::RtlPropagateMultipleSignals));
    const auto multiResult=coordinator.execute(*multiAction,invocation);
    if (!multiResult.succeeded) std::printf("Multi action: %s\n",qPrintable(multiResult.failureReason));
    check(multiResult.succeeded, "real RTL action accepts saved multi-signal sources");
    auto* multi=docks.multiSignalPropagationPanel();
    bool saved=!multi->input().capturedDocuments.isEmpty();
    QSet<QString> identities;
    for (const auto& doc:multi->input().capturedDocuments) { saved &= !doc.unsaved; identities.insert(EditorFileIdentity::lookupKey(doc.fileName)); }
    check(saved && multi->input().capturedDocuments.size()==contents.size() && identities.size()==contents.size(),
          "capture preserves saved status and deduplicates physical aliases including empty source");
    const bool multiPreview=multi->requestPreview() && multi->hasHighDiffPreview();
    if (!multiPreview) std::printf("Multi preview: %s\n",qPrintable(multi->statusText()));
    check(multiPreview, "multi-signal actual Qt workflow produces High+Diff preview");
    const auto originalMultiQuery=multi->lastQuery()
        ? std::optional<MultiSignalPropagationQuery>(*multi->lastQuery()) : std::nullopt;
    const auto confirmed=docks.multiSignalPropagationWorkflow()->confirm();
    if (!confirmed.succeeded()) std::printf("Multi confirm: %s\n",qPrintable(confirmed.message));
    check(confirmed.state==MultiSignalPropagationWorkflowState::Applied && editor->toPlainText()!=sourceText,
          "multi-signal confirmation applies to the real shared QTextDocument");
    const auto undone=docks.multiSignalPropagationWorkflow()->undo();
    check(undone.state==MultiSignalPropagationWorkflowState::Undone && editor->toPlainText()==sourceText,
          "one real multi-signal undo restores the source document");
    auto* documents=docks.rtlActionDocumentManager();
    const auto topRestored=documents->snapshot(top.toUtf8().toStdString());
    check(topRestored && QString::fromUtf8(topRestored->text)==topText, "same undo restores the companion workspace file");
    index->setSnapshot(publication(contents,order));
    tabsWidget->setCurrentWidget(editor);
    cursor=editor->textCursor(); cursor.setPosition(sourceText.indexOf("payload")+2); editor->setTextCursor(cursor);
    editor->setHierarchyInstanceContext({real,"top",sourcePath});
    ActionInvocation pairInvocation;
    pairInvocation.parameters={{"leftInstancePath",sourcePath},{"rightInstancePath",sinkPath},{"connectionName","link"}};
    const auto* pairAction=findActionById(QString::fromLatin1(ActionIds::RtlConnectInstancePair));
    const auto pairResult=coordinator.execute(*pairAction,pairInvocation);
    if (!pairResult.succeeded) std::printf("Pair action: %s\n",qPrintable(pairResult.failureReason));
    check(pairResult.succeeded, "real RTL instance-pair action accepts saved sources");
    auto* pair=docks.instancePairConnectionCoordinator()->panel();
    check(pair->currentPlanRequest().isValid(), "instance-pair revision context matches aliased captured documents");
    const auto preview=docks.instancePairConnectionWorkflow()->requestPreview(pair->currentPlanRequest());
    if (!preview.succeeded()) std::printf("Pair preview: %s\n",qPrintable(preview.message));
    check(preview.succeeded() && pair->hasDisplayableProposal(), "instance-pair actual workflow produces High+Diff preview");
    const auto pairConfirm=docks.instancePairConnectionWorkflow()->confirm(pair->currentPlanRequest());
    if (!pairConfirm.succeeded()) std::printf("Pair confirm: %s\n",qPrintable(pairConfirm.message));
    check(pairConfirm.state==InstancePairConnectionWorkflowState::Applied && editor->toPlainText()!=sourceText,
          "instance-pair confirmation updates actual Qt document and workspace");
    const auto pairUndo=docks.instancePairConnectionWorkflow()->undo();
    bool restored=true;
    for (auto it=contents.cbegin();it!=contents.cend();++it) {
        const auto live=documents->snapshot(it.key().toUtf8().toStdString());
        restored &= live && QString::fromUtf8(live->text)==it.value();
    }
    check(pairUndo.state==InstancePairConnectionWorkflowState::Undone && restored,
          "one actual instance-pair undo restores every file");

    if (originalMultiQuery) {
        auto captureMulti=[&] {
            auto query=*originalMultiQuery; query.semanticToken=index->snapshotToken();
            for (auto& doc : query.documents) {
                const auto live=documents->snapshot(doc.fileName.toUtf8().toStdString());
                if (!live) continue;
                doc.revision=live->version.value; doc.text=QString::fromUtf8(live->text);
                auto syntax=std::make_shared<TSDocument>(); syntax->setText(doc.text); doc.syntax=syntax;
                const auto semantic=query.semanticToken.snapshot->cachedFileSource(doc.fileName);
                doc.unsaved=!semantic.exists || semantic.text!=doc.text;
            }
            for (auto& member : query.members) {
                const auto& doc=query.documents[EditorFileIdentity::lookupKey(member.context.fileName)];
                member.context.documentText=doc.text; member.context.documentRevision=doc.revision;
            }
            return query;
        };
        MultiSignalPropagationPlanner planner(index,hierarchy);
        const auto savedQuery=captureMulti();
        const auto savedPlan=planner.plan(savedQuery,*documents);
        check(savedPlan.ready(), "fresh real document revisions can be planned again after both undos");
        if (aliasCreated) {
            auto aliased=savedQuery;
            auto duplicate=aliased.documents.value(EditorFileIdentity::lookupKey(source)); duplicate.fileName=alias;
            aliased.documents.insert(alias,duplicate);
            const auto aliasedPlan=planner.plan(aliased,*documents);
            check(aliasedPlan.ready() && aliasedPlan.workspaceEdit.edits.size()==savedPlan.workspaceEdit.edits.size(),
                  "duplicate physical document input does not duplicate planned edits");
            ++aliased.documents[alias].revision;
            check(planner.plan(aliased,*documents).failure==MultiSignalPropagationFailure::InvalidTreeSnapshot,
                  "conflicting aliases cannot replace the captured revision");
        }
        QTextCursor changed(editor->document()); changed.movePosition(QTextCursor::End); changed.insertText("// unsaved local comment\n");
        check(planner.plan(savedQuery,*documents).failure==MultiSignalPropagationFailure::StaleDocumentRevision,
              "real Qt edit invalidates a previously captured document revision");
        const auto unsavedQuery=captureMulti();
        const auto unsavedPlan=planner.plan(unsavedQuery,*documents);
        if (!unsavedPlan.ready()) std::printf("Unsaved plan: %s\n",qPrintable(unsavedPlan.message));
        check(unsavedQuery.documents.value(EditorFileIdentity::lookupKey(source)).unsaved && unsavedPlan.ready(),
              "multi-signal retains its supported unsaved buffer contract with real Qt text");
        auto falseSaved=unsavedQuery; falseSaved.documents[EditorFileIdentity::lookupKey(source)].unsaved=false;
        check(planner.plan(falseSaved,*documents).failure==MultiSignalPropagationFailure::StaleSemanticSource,
              "changed real text cannot bypass the saved-source guard");
        auto wrongSyntax=unsavedQuery;
        auto oldSyntax=std::make_shared<TSDocument>(); oldSyntax->setText(sourceText);
        wrongSyntax.documents[EditorFileIdentity::lookupKey(source)].syntax=oldSyntax;
        check(planner.plan(wrongSyntax,*documents).failure==MultiSignalPropagationFailure::InvalidTreeSnapshot,
              "captured syntax must still match the real document text");
        auto unsavedPair=pair->analysis().query;
        for (const auto& doc : unsavedQuery.documents)
            unsavedPair.documents[EditorFileIdentity::lookupKey(doc.fileName)]={doc.fileName,doc.revision,doc.text,doc.syntax,doc.unsaved};
        InstancePairConnectionFacade facade(index,hierarchy);
        check(facade.analyze(unsavedPair,*documents).failure==InstancePairConnectionFailure::StaleSemanticSource,
              "instance-pair continues to reject real unsaved buffers");
        index->setSnapshot(index->snapshot());
        check(planner.plan(unsavedQuery,*documents).failure==MultiSignalPropagationFailure::StaleSemanticGeneration,
              "semantic publication change invalidates an otherwise captured Qt query");
        editor->setPlainText(sourceText);
    }
    for (auto* openEditor : tabs.openEditors()) openEditor->document()->setModified(false);
    check(tabs.closeAllTabs(), "test-owned workflow documents close after text restoration checks");
    tabs.clearCrashRecoveryAfterNormalClose();
    WorkspaceEditTransactionService::getInstance()->clearHistory(); index->clearSemanticState();
    if (aliasCreated) check(QDir().rmdir(aliasRoot), "directory alias removed without touching its target");
}
}
int main(int argc,char** argv) {
    setbuf(stdout,nullptr); QTemporaryDir settings;
    QCoreApplication::setOrganizationName("ZeroSlackRtlSourceIdentityContract"); QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",settings.filePath("sessions.ini").toUtf8());
    QApplication app(argc,argv); QStandardPaths::setTestModeEnabled(true);
    if (!initializeUiStyleForTest()) return 2;
    QTemporaryDir root; workflowCases(root.path()); QThreadPool::globalInstance()->waitForDone();
    std::printf("RTL source identity contract: %d checks, %d failures\n",checks,failures); return failures ? 1 : 0;
}
