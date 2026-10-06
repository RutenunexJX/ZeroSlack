#include <QtWidgets>
#include <QtTest>
#include <memory>
#include <functional>
#define private public
#include "editorsourcenavigation.h"
#include "signalkernelgraphpanelcoordinator.h"
#undef private
#include "mycodeeditor.h"
#include "tsdocument.h"
#include "tabmanager.h"
#include "definitionpreviewservice.h"
#include "codepreviewservice.h"
#include "previewsource.h"
#include "slangmanager.h"
#include "semanticindexsnapshot.h"
#include "testuistyle.h"

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks; failures += !ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
}
bool write(const QString& path, const QString& text) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size();
}
std::shared_ptr<const SemanticIndexSnapshot> snapshot(const QString& path, const QString& text) {
    SlangManager slang;
    return std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
        slang.extractSymbolRecords(path,text), {}, {}, {{path,text}}));
}
bool directoryAlias(const QString& target, const QString& alias) {
#ifdef Q_OS_WIN
    QProcess process;
    process.start("cmd.exe", {"/d", "/s", "/c", "mklink", "/J",
        QDir::toNativeSeparators(alias), QDir::toNativeSeparators(target)});
    return process.waitForFinished(10000) && process.exitCode() == 0 && QFileInfo(alias).isDir();
#else
    return QFile::link(target,alias);
#endif
}
void sourceIdentity() {
    QTemporaryDir fixture;
    const auto realDirectory = fixture.filePath("real");
    const auto aliasDirectory = fixture.filePath("alias");
    const auto secondDirectory = fixture.filePath("second-alias");
    const auto otherDirectory = fixture.filePath("other");
    QDir().mkpath(realDirectory); QDir().mkpath(otherDirectory);
    const auto target = QDir(realDirectory).filePath("unit.sv");
    const auto empty = QDir(realDirectory).filePath("empty.sv");
    const auto alias = QDir(aliasDirectory).filePath("unit.sv");
    const auto secondAlias = QDir(secondDirectory).filePath("unit.sv");
    const auto other = QDir(otherDirectory).filePath("unit.sv");
    const QString original = "module alias_unit;\nlogic ready;\nendmodule\n";
    check(write(target,original) && write(empty,{}) && write(other,original+"// other\n"),
        "source identity fixtures exist on disk, including an empty file");
    auto direct = std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords({}, {}, {}, {{target,original},{empty,{}}}));
    const auto emptySource = direct->cachedFileSource(empty);
    check(emptySource.exists && emptySource.text.isEmpty() && !emptySource.fileKey.isEmpty()
        && !direct->cachedFileSource(fixture.filePath("absent.sv")).exists,
        "snapshot source lookup distinguishes present empty text from absence");
    const auto emptyPreview = PreviewSource::capture(empty,nullptr,direct);
    check(emptyPreview.exists && emptyPreview.hasLocationWitness && emptyPreview.text.isEmpty()
        && !emptyPreview.excerpt(1,1,0,0,1,1).available,
        "empty snapshot source remains a present witness without fabricated preview lines");
    QString normalizedAlias = QDir(realDirectory).filePath("nested/../unit.sv");
#ifdef Q_OS_WIN
    normalizedAlias = QDir::toNativeSeparators(normalizedAlias).toUpper();
#endif
    const auto normalized = direct->cachedFileSource(normalizedAlias);
    check(normalized.exists && normalized.text == original
        && normalized.fileKey == direct->cachedFileSource(target).fileKey,
        "lexical normalization, native separators and platform case rules share the content key");
    const bool aliasesCreated = directoryAlias(realDirectory,aliasDirectory)
        && directoryAlias(realDirectory,secondDirectory);
    check(aliasesCreated,"real directory aliases are available for snapshot identity contracts");
    if (!aliasesCreated) return;
    check(direct->cachedFileSource(alias).exists && direct->cachedFileSource(alias).text == original,
        "unregistered physical alias resolves to a directly indexed canonical source");
    auto aliased = snapshot(alias,original);
    const auto reverse = aliased->cachedFileSource(target);
    check(reverse.exists && reverse.text == original
        && reverse.fileKey == aliased->cachedFileSource(alias).fileKey,
        "canonical request finds a source published under an alias");
    check(aliased->cachedFileSource(secondAlias).text == original,
        "another physical alias resolves through the immutable publication identity");
    CodePreviewService codes;
    CodePreviewQuery query;
    query.codeLink=RtlInsightLink::fromFileLine(target,1,8); query.locationSnapshot=aliased;
    check(codes.previewForCodeLink(query).available,
        "real code preview accepts a canonical link witnessed by an aliased source");
    SemanticIndex index; index.setSnapshot(aliased);
    DefinitionPreviewService definitions(&index);
    EditorSemanticContext context;
    context.fileName=fixture.filePath("caller.sv"); context.moduleName="caller";
    context.lineText="alias_unit instance();"; context.column=2; context.cursorLine=1;
    check(definitions.previewForContext(context).available,
        "real Slang definition through an alias keeps its source witness");
    {
        QTabWidget widget; TabManager tabs(&widget);
        tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
        check(tabs.openFileInTab(target),"canonical source opens for alias/live-buffer precedence");
        codes.setDocumentModel(tabs.getDocumentModel());
        tabs.getCurrentEditor()->clear();
        query.codeLink.fileName=alias;
        const auto cleared = codes.previewForCodeLink(query);
        check(!cleared.available && cleared.stale && cleared.sourceDescription=="open document",
            "empty open canonical buffer wins over an aliased analysis witness");
        tabs.getCurrentEditor()->document()->setModified(false);
        codes.setDocumentModel(nullptr);
    }
    SemanticFileSymbolUpdate update; update.fileName=alias; update.content=original+"// replacement\n";
    const auto replaced = aliased->withReplacedFiles({update},{},{});
    check(replaced.cachedFileSource(target).text==update.content
        && aliased->cachedFileSource(target).text==original,
        "single-file replacement updates alias text without changing the old publication");
    update.removed=true;
    check(!replaced.withReplacedFiles({update},{},{}).cachedFileSource(target).exists
        && replaced.cachedFileSource(target).exists,
        "source removal also removes its alias index only in the new publication");
    auto ambiguous=SemanticIndexSnapshot::fromSymbolRecords({}, {}, {},
        {{alias,original},{secondAlias,original+"// independent override\n"}});
    check(!ambiguous.cachedFileSource(target).exists && ambiguous.cachedFileSource(alias).text==original,
        "ambiguous physical aliases require an exact key instead of selecting arbitrary text");
    check(ambiguous.withReplacedFiles({update},{},{}).cachedFileSource(target).text.contains("independent override"),
        "removing one alias restores an unambiguous physical lookup");
    check(QDir().rmdir(aliasDirectory) && directoryAlias(otherDirectory,aliasDirectory),
        "real alias can be rebound for publication lifecycle verification");
    update.removed=false; update.content=original+"// rebound\n";
    const auto rebound = aliased->withReplacedFiles({update},{},{});
    check(!rebound.cachedFileSource(target).exists && rebound.cachedFileSource(other).text==update.content
        && aliased->cachedFileSource(target).text==original,
        "alias identity refreshes only for replaced input and older witness identity remains frozen");
    SemanticFileSymbolUpdate unrelated; unrelated.fileName=empty; unrelated.content={};
    const auto preserved=aliased->withReplacedFiles({unrelated},{},{});
    check(preserved.cachedFileSource(target).text==original && !preserved.cachedFileSource(other).exists,
        "a batch binds only its written inputs, preserving untouched alias identity after disk retargeting");
    QDir().rmdir(aliasDirectory); QDir().rmdir(secondDirectory);
}
void batchPublication() {
    QTemporaryDir fixture;
    const auto first=fixture.filePath("first.sv");
    const auto empty=fixture.filePath("empty.sv");
    const auto untouched=fixture.filePath("untouched.sv");
    const QString code="module batch_first;\nlogic ready;\nendmodule\n";
    check(write(first,code) && write(empty,{}) && write(untouched,"// keep\n"),
        "batch publication fixtures exist, including empty source");
    SlangManager slang;
    SemanticFileSymbolUpdate a; a.fileName=first; a.content=code;
    a.symbolRecords=slang.extractSymbolRecords(first,code);
    SemanticFileSymbolUpdate b; b.fileName=empty; b.content={};
    SemanticFileSymbolUpdate c; c.fileName=untouched; c.content="// keep\n";
    SemanticIndexSnapshot initial;
    const auto full=initial.withReplacedFiles({a,b,c},{},{});
    const auto factory=SemanticIndexSnapshot::fromSymbolRecords(a.symbolRecords,{}, {},
        {{first,code},{empty,{}},{untouched,c.content}});
    check(full.fileContentsView()==factory.fileContentsView()
        && full.symbolRecordCount()==factory.symbolRecordCount()
        && full.cachedFileSource(empty).exists && full.cachedFileSource(empty).text.isEmpty(),
        "production empty-snapshot full batch preserves factory text, empty inputs and Slang records");
    a.content=code+"// changed\n"; b.content="// populated\n";
    const auto multiple=full.withReplacedFiles({a,b},{},{});
    check(multiple.cachedFileSource(first).text==a.content && multiple.cachedFileSource(empty).text==b.content
        && multiple.cachedFileSource(untouched).text==c.content && full.cachedFileSource(first).text==code
        && full.cachedFileSource(empty).text.isEmpty(),
        "multi-file replacement updates only requested content and keeps the previous publication immutable");
    SemanticFileSymbolUpdate erase=a; erase.removed=true;
    SemanticFileSymbolUpdate final=a; final.fileName=fixture.path()+"/./first.sv";
    final.content=code+"// last update\n";
    const auto repeated=multiple.withReplacedFiles({a,erase,final},{},{});
    check(repeated.cachedFileSource(first).exists && repeated.cachedFileSource(first).text==final.content
        && repeated.symbolRecordCount(first)==a.symbolRecords.size(),
        "duplicate normalized input keys retain write-remove-write order");
    const auto deleted=multiple.withReplacedFiles({final,erase},{},{});
    check(!deleted.cachedFileSource(first).exists && deleted.symbolRecordCount(first)==0
        && deleted.cachedFileSource(empty).text==b.content && multiple.cachedFileSource(first).exists,
        "last removal wins without changing other inputs or the old snapshot");
    b.content={};
    const auto singleton=multiple.withReplacedFiles({b},{},{});
    check(singleton.cachedFileSource(empty).exists && singleton.cachedFileSource(empty).text.isEmpty()
        && singleton.cachedFileSource(first).text==a.content,
        "single-file delta keeps empty text present and unrelated files intact");
    erase.fileName=fixture.filePath("not-present.sv");
    const auto removedOnly=full.withReplacedFiles({erase},{},{});
    check(removedOnly.fileContentsView()==full.fileContentsView()
        && full.withReplacedFiles({}, {}, {}).fileContentsView()==full.fileContentsView(),
        "remove-only and empty write batches preserve source presence without rebinding existing files");
}
void navigation() {
    const QString text = QStringLiteral(
        "package p; parameter N=1; endpackage\n"
        "module nav;\nimport p::*;\nlogic ready$next;\nlogic \\escaped.name ;\n"
        "assign ready$next = \\escaped.name ;\n"
        "string s = \"import p::*;\";\n// import p::*;\n/*\nimport p::*;\n*/ assign ready$next = 1'b0;\nendmodule\n"
        "`include \"nav.svh\"\n");
    QTemporaryDir fixture;
    const auto path = fixture.filePath("nav.sv");
    auto* index = SemanticIndex::getInstance();
    index->setSnapshot(snapshot(path,text.left(text.indexOf("`include"))));
    MyCodeEditor editor; editor.setDocumentFileName(path); editor.setPlainText(text);
    editor.resize(1000,600); editor.show(); QApplication::processEvents();
    EditorSourceNavigationUi ui;
    auto* service = EditorSemanticContextService::getInstance();
    const auto contextAt = [&](int position, bool) {
        QTextCursor c(editor.document()); c.setPosition(position);
        EditorSemanticContext context;
        context.fileName = path; context.moduleName = "nav";
        context.lineText = c.block().text(); context.column = c.positionInBlock();
        context.cursorPosition = position; context.cursorLine = c.blockNumber()+1;
        context.lineUpToCursor = context.lineText.left(context.column);
        return context;
    };
    EditorSourceNavigationTarget emitted; int count = 0;
    QObject::connect(&editor,&MyCodeEditor::sourceNavigationRequested,&editor,
        [&](const EditorSourceNavigationTarget& target,const EditorSemanticContext&) { emitted=target; ++count; });
    const auto click = [&](int position) {
        QTextCursor c(editor.document()); c.setPosition(position); editor.setTextCursor(c);
        editor.ensureCursorVisible(); QApplication::processEvents();
        return ui.requestNavigationAtPosition(&editor,editor.cursorRect(c).center(),service,contextAt);
    };
    const auto verify = [&](const QString& token,int position,bool include,bool jumpable,const char* label) {
        const int before=count;
        const bool accepted=click(position);
        check(accepted && count==before+1 && emitted.text==token
            && text.mid(emitted.startPos,emitted.endPos-emitted.startPos)==token
            && emitted.includeTarget==include && emitted.jumpable==jumpable,label);
    };
    verify("ready$next",text.indexOf("assign ready$next")+10,false,true,"real editor click resolves complete dollar identifier");
    verify("\\escaped.name",text.indexOf("= \\escaped.name")+6,false,true,"real editor click resolves complete escaped identifier");
    verify("p",text.indexOf("import p::*;")+7,false,true,"real import click keeps package navigation");
    verify("nav.svh",text.indexOf("nav.svh")+3,true,true,"real include click keeps path navigation");
    verify("ready$next",text.indexOf("*/ assign ready$next")+14,false,true,
        "real source click retains code after a multiline comment closes");
    for (const int position : {text.indexOf("string s")+20,text.indexOf("// import")+11,text.indexOf("/*\n")+10}) {
        const int before=count;
        check(!click(position) && count==before,"string and comment import lookalikes cannot dispatch navigation");
    }
    const auto* source = SourceNavigationService::getInstance();
    check(source->packageImportAtColumn("import p::*;",7).matched
        && !source->packageImportAtColumn("string s = \"import p::*;\";",19).matched
        && !source->packageImportAtColumn("// import p::*;",11).matched,
        "pure text queries use the same syntax rules");
    const int renamedStart = text.indexOf("*/ assign ready$next") + 10;
    QTextCursor rename(editor.document());
    rename.setPosition(renamedStart + QStringLiteral("ready$next").size());
    rename.insertText("2");
    rename.setPosition(renamedStart + 3);
    editor.setTextCursor(rename); editor.ensureCursorVisible();
    const auto* syntax = editor.syntaxDocument();
    check(syntax->hasPendingEdits() && syntax->lastEditPreservedStructure()
        && !syntax->hasDeferredSyntaxEdits(),
        "identifier edit reaches the real editor's structure-preserving pending state");
    const int before = count;
    const bool accepted = ui.requestNavigationAtPosition(
        &editor,editor.cursorRect(rename).center(),service,contextAt);
    check(accepted && count == before + 1 && emitted.text == "ready$next2"
        && emitted.startPos == renamedStart && emitted.endPos == renamedStart + 11,
        "immediate editor navigation uses the updated token before a safe reparse completes");
    TSDocument deferred;
    const QString deferredText = "module pending; logic ready; endmodule";
    deferred.setText(deferredText);
    DocumentChange change;
    change.position = deferredText.indexOf("logic");
    change.startColumn = change.position;
    change.insertedText = "/*";
    change.oldLength = deferredText.size(); change.newLength = change.oldLength + 2;
    deferred.applyEdit(change,true);
    check(deferred.hasDeferredSyntaxEdits()
        && !source->targetAtPosition(deferred,deferredText.indexOf("ready")+2).matched,
        "structural deferred edit cannot expose an old identifier as current code");
    index->clearSemanticState();
}
void preview() {
    QTemporaryDir fixture;
    const auto target = fixture.filePath("child.sv");
    const QString original = "module preview_child;\nlogic ready;\nendmodule\n";
    check(write(target,original),"preview cross-file fixture written");
    SemanticIndex index;
    index.setSnapshot(snapshot(target,original));
    QTabWidget widget; TabManager tabs(&widget);
    tabs.setCrashRecoveryService(std::make_unique<CrashRecoveryService>(fixture.filePath("recovery")));
    DefinitionPreviewService definitions(&index); definitions.setDocumentModel(tabs.getDocumentModel());
    CodePreviewService codes; codes.setDocumentModel(tabs.getDocumentModel());
    EditorSemanticContext context;
    context.fileName=fixture.filePath("top.sv"); context.moduleName="top";
    context.lineText="preview_child inst();"; context.column=3; context.cursorLine=2;
    CodePreviewQuery query;
    query.codeLink=RtlInsightLink::fromFileLine(target,1,8);
    query.sourceRange.fileName=target; query.sourceRange.line=1; query.sourceRange.column=8;
    query.sourceRange.endLine=1; query.sourceRange.endColumn=21;
    query.locationSnapshot=index.snapshot();
    const auto unopened=definitions.previewForContext(context);
    check(unopened.available && !unopened.stale && unopened.targetFile==target
        && unopened.sourceDescription=="semantic snapshot" && unopened.codeLines.contains("module preview_child;"),
        "unopened cross-file definition previews its witnessed snapshot");
    check(codes.previewForCodeLink(query).available && codes.previewForCodeLink(query).preciseRange,
        "unopened shared code preview uses the same witnessed coordinates");
    check(tabs.openFileInTab(target),"preview target opens through real TabManager");
    auto* editor=tabs.getCurrentEditor();
    const auto opened=definitions.previewForContext(context);
    check(opened.available && opened.sourceDescription=="open document" && !opened.documentId.isEmpty(),
        "open preview identifies its live document");
    {
        QMainWindow graphWindow;
        SignalKernelGraphPanelCoordinator graph(&graphWindow);
        graphWindow.addDockWidget(Qt::RightDockWidgetArea,graph.dock()); graphWindow.show();
        graph.setDocumentModel(tabs.getDocumentModel());
        SignalKernelGraphService graphService(&index);
        SignalKernelGraphQuery graphQuery; graphQuery.signalName="ready";
        graphQuery.fileName=target; graphQuery.moduleName="preview_child";
        const auto graphReport=graphService.buildSignalKernelGraph(graphQuery);
        check(graphReport.found && graphReport.locationSnapshot==index.snapshot(),
            "real graph service carries its coordinate publication to preview");
        graph.renderReportForTest(graphReport);
        graph.dock()->show(); QApplication::processEvents();
        graph.showNodePreview(graphReport.kernel,QRectF(0,0,100,80));
        if (!graph.hoverPopup->contentModel().navigationTarget.isValid())
            for(const auto& row:graph.hoverPopup->contentModel().rows) std::printf("Graph preview: %s\n",row.text.toUtf8().constData());
        check(graph.hoverPopup->isVisible() && graph.hoverPopup->contentModel().navigationTarget.isValid(),
            "actual graph caller displays a current code preview");
        const auto builds=graph.graphBuildRequestCountForTest();
        editor->setPlainText("// inserted before graph node\n"+original);
        check(!graph.hoverPopup->isVisible() && graph.graphBuildRequestCountForTest()==builds,
            "document edit expires displayed graph preview without rebuilding analysis");
        graph.showNodePreview(graphReport.kernel,QRectF(0,0,100,80));
        bool staleMessage=false;
        for(const auto& row:graph.hoverPopup->contentModel().rows) staleMessage |= row.text.contains("Refresh analysis");
        check(staleMessage && !graph.hoverPopup->contentModel().navigationTarget.isValid(),
            "actual graph caller displays stale-source reason on repeated hover");
        graph.setDocumentModel(nullptr);
        check(!graph.hoverPopup->isVisible(),"rebinding preview document model clears presentation and connections");
    }
    editor->setPlainText(QString(original).replace("module preview_child;","module preview_child; // dirty preview marker"));
    const auto dirty=definitions.previewForContext(context);
    const auto dirtyCode=codes.previewForCodeLink(query);
    check(dirty.available && !dirty.stale && dirty.codeLines.join('\n').contains("dirty preview marker")
        && dirty.documentRevision!=opened.documentRevision && dirty.highlightedLine==1,
        "unsaved same-line append preserves verified definition anchor");
    check(dirtyCode.available && dirtyCode.preciseRange && dirtyCode.codeLines.join('\n').contains("dirty preview marker"),
        "shared code preview retains correct same-line dirty text");
    editor->clear();
    const auto empty=definitions.previewForContext(context);
    const auto emptyCode=codes.previewForCodeLink(query);
    check(!empty.available && empty.stale && empty.codeLines.isEmpty() && empty.sourceDescription=="open document"
        && empty.unavailableReason.contains("empty"),"cleared open definition buffer never falls back to old text");
    check(!emptyCode.available && emptyCode.stale && !emptyCode.preciseRange && emptyCode.codeLines.isEmpty(),
        "cleared shared code preview has no old range or text");
    editor->setPlainText("// added before definition\n"+original);
    const auto shifted=definitions.previewForContext(context);
    const auto shiftedCode=codes.previewForCodeLink(query);
    check(!shifted.available && shifted.stale && !shifted.unavailableReason.isEmpty(),
        "insertion before a definition rejects stale semantic coordinates");
    check(!shiftedCode.available && shiftedCode.stale && !shiftedCode.preciseRange,
        "shared code preview rejects shifted coordinates");
    EditorSourceNavigationUi ui; auto* popup=ui.beginExternalPeek(editor,true);
    popup->showPreview(unopened,QPoint(10,10),editor->font());
    bool sourceShown=false;
    for(const auto& row:popup->contentModel().rows) sourceShown |= row.text.contains("last analyzed");
    check(sourceShown,"snapshot preview visibly identifies its analyzed-text source");
    popup->showPreview(shifted,QPoint(10,10),editor->font());
    bool staleShown=false;
    for(const auto& row:popup->contentModel().rows) staleShown |= row.text==shifted.unavailableReason;
    check(popup->contentModel().navigationTarget.fileName.isEmpty() && staleShown,
        "unavailable definition popup cannot navigate stale coordinates");
    popup->showCodePreview(shiftedCode,QPoint(10,10),editor->font());
    check(popup->contentModel().navigationTarget.fileName.isEmpty(),
        "unavailable code popup cannot open stale temporary editor");
    index.setSnapshot(snapshot(target,editor->toPlainText()));
    const auto refreshed=definitions.previewForContext(context);
    check(refreshed.available && !refreshed.stale && refreshed.targetLine==2
        && refreshed.codeLines.join('\n').contains("added before"),"analysis refresh restores cross-file definition at the new line");
    check(!codes.previewForCodeLink(query).available,"old code-link witness stays stale after index refresh");
    query.locationSnapshot=index.snapshot(); query.codeLink.line=2;
    query.sourceRange.line=2; query.sourceRange.endLine=2;
    check(codes.previewForCodeLink(query).available && codes.previewForCodeLink(query).preciseRange,
        "refreshed shared code link restores precise preview");
    query.sourceRange.endColumn=9999;
    check(!codes.previewForCodeLink(query).available,"invalid precise range is rejected against the witness");
    query.sourceRange.fileName=fixture.filePath("other.sv");
    const auto otherRange=codes.previewForCodeLink(query);
    check(otherRange.available && !otherRange.preciseRange && otherRange.targetEndColumn==0,
        "range from another source cannot masquerade as precise code evidence");
    editor->document()->setModified(false);
    tabs.closeTab(widget.indexOf(editor));
    check(definitions.previewForContext(context).available
        && definitions.previewForContext(context).sourceDescription=="semantic snapshot",
        "closing a document preserves explicitly identified analysis-source preview");
    check(tabs.openFileInTab(target),"preview target reopens from disk");
    check(!definitions.previewForContext(context).available,"reopened older disk content rejects newer semantic coordinates");
    index.setSnapshot(snapshot(target,original));
    check(definitions.previewForContext(context).available,"refresh after reopening restores preview");
}
}
int main(int argc,char** argv) {
    setbuf(stdout,nullptr); QApplication app(argc,argv); QStandardPaths::setTestModeEnabled(true);
    if (!initializeUiStyleForTest()) return 2;
    sourceIdentity(); batchPublication(); navigation(); preview();
    std::printf("%d checks, %d failures\n",checks,failures); return failures ? 1 : 0;
}
