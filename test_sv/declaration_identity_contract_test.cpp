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
#include "definitionservice.h"
#include "editorinsertpaletteservice.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
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
QStringList names(const QList<SemanticSymbolRecord>& records) {
    QStringList result; for (const auto& r : records) result.append(r.name); result.sort(); return result;
}
QStringList titles(const QList<GlobalControlItem>& items) {
    QStringList result; for (const auto& i : items) result.append(i.title); result.sort(); return result;
}
QString source() {
    return "module top_a;\n"
        "typedef struct packed { logic alpha; logic ALPHA; } payload_t;\n"
        "typedef payload_t alias_t;\nalias_t item;\n"
        "typedef struct packed { payload_t inner; } outer_t;\nouter_t outer;\n"
        "typedef enum logic { A_IDLE, A_RUN } state_t;\nstate_t state;\n"
        "struct packed { logic anon; } anonymous_item;\n"
        "enum logic { ANON_IDLE, ANON_RUN } anonymous_state;\n"
        "always_comb begin\nitem.alpha = 1'b0;\nstate = A_IDLE;\nend\nendmodule\n"
        "module top_b;\n"
        "typedef struct packed { logic beta; } payload_t;\npayload_t item;\n"
        "typedef enum logic { B_IDLE, B_RUN } state_t;\nstate_t state;\nendmodule\n";
}
void semanticCases(const QString& root) {
    SlangManager slang; SemanticIndex index; CompletionService service(&index);
    const QString path = QDir(root).filePath("types.sv"), text = source();
    auto records = slang.extractSymbolRecords(path, text);
    auto snapshot = SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{path,text}});
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(snapshot));
    SemanticQueryContext context; context.fileName = path; context.moduleName = "top_a"; context.cursorLine = 12;
    const auto members = service.findStructMemberCompletionRecords({"item"}, "top_a", {}, context);
    check(names(members) == QStringList({"ALPHA","alpha"}), "same-file modules and alias keep exact case-distinct type members");
    check(names(service.findStructMemberCompletionRecords({"outer","inner"}, "top_a", {}, context)) == names(members),
          "nested aggregate path follows field type identities");
    check(names(service.findExpectedEnumValueRecords("state", "top_a", {}, {}, context)) == QStringList({"A_IDLE","A_RUN"}),
          "same-name enum values remain scoped to bound variable");
    check(names(service.findStructMemberCompletionRecords({"anonymous_item"}, "top_a", {}, context)) == QStringList({"anon"}),
          "anonymous struct has bound member owner");
    check(names(service.findExpectedEnumValueRecords("anonymous_state", "top_a", {}, {}, context)) == QStringList({"ANON_IDLE","ANON_RUN"}),
          "anonymous enum has bound value owner");
    for (const auto& member : members)
        check(member.owner.stableKey.isValid() && index.getSymbolRecordByStableKey(member.owner.stableKey).isValid(),
              "real Slang member identity resolves within immutable snapshot");
    CommandCompletionQuery query; query.fileName=path; query.moduleName="top_a"; query.cursorLine=12;
    check(!names(service.findVisibleStructMemberRecords(query)).contains("beta"), "visible struct candidates exclude other module");
    check(!names(service.findVisibleEnumValueRecords(query)).contains("B_IDLE"), "visible enum candidates exclude other module");
    DefinitionService definitions(&index); DefinitionQuery definition;
    definition.fileName=path; definition.moduleName="top_a"; definition.cursorLine=12;
    definition.linePrefixBeforeCursor="item.alpha"; definition.symbolName="alpha";
    const auto found=definitions.resolveDefinition(definition);
    check(found.found && found.symbolRecord.owner.stableKey == members.value(0).owner.stableKey,
          "definition navigation consumes bound type identity");
    definition.linePrefixBeforeCursor.clear(); definition.structTypeNameForMember="payload_t"; definition.symbolName="beta";
    check(!definitions.resolveDefinition(definition).found,"explicit member-type navigation cannot borrow another scope's member");
    check(index.getStructMemberRecords("payload_t").isEmpty(), "context-free ambiguous legacy query is conservative");
    auto legacy=records;
    for(auto& record:legacy) {record.owner.stableKey={}; record.type.stableKey={};}
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(legacy,{},{},{{path,text}})));
    context.moduleName="top_b"; context.cursorLine=18;
    check(service.findStructMemberCompletionRecords({"item"},"top_b",{},context).isEmpty(),
          "legacy records lacking identities do not merge ambiguous type declarations");
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(snapshot)); context.moduleName="top_a"; context.cursorLine=12;
    const QString changed=QString(text).replace("logic alpha", "logic gamma").replace("item.alpha", "item.gamma");
    index.updateSymbolRecordsForFile(path, slang.extractSymbolRecords(path,changed), changed);
    check(names(service.findStructMemberCompletionRecords({"item"},"top_a",{},context)) == QStringList({"ALPHA","gamma"}),
          "incremental replacement excludes stale member and includes current member");
    check(names(snapshot.getSymbolRecordsByOwner("payload_t")).contains("alpha"), "old immutable snapshot retains original records");
    SemanticFileSymbolUpdate removal; removal.fileName=path; removal.removed=true;
    index.updateSymbolRecordsForFiles({removal});
    check(service.findStructMemberCompletionRecords({"item"},"top_a",{},context).isEmpty(), "removed scope has no stale members");

    const QString pkgPath=QDir(root).filePath("packages.sv");
    const QString packages="package p; typedef struct packed { logic p_value; } payload_t; "
        "typedef enum { P_IDLE, P_RUN } state_t; payload_t item; state_t state; endpackage\n"
        "package q; typedef struct packed { logic q_value; } payload_t; "
        "typedef enum { Q_IDLE, Q_RUN } state_t; payload_t item; state_t state; endpackage\n"
        "module use_p; import p::*; payload_t item; state_t state; endmodule\n";
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
        slang.extractSymbolRecords(pkgPath,packages),{},{},{{pkgPath,packages}})));
    context={}; context.fileName=pkgPath; context.packageName="p"; context.cursorLine=1;
    check(names(service.findStructMemberCompletionRecords({"item"},{},{},context)) == QStringList({"p_value"}),
          "same-file packages isolate same-name type and variable");
    check(names(service.findExpectedEnumValueRecords("state",{},"p",{},context)) == QStringList({"P_IDLE","P_RUN"}),
          "package enum identity remains isolated");
    context.packageName.clear(); context.moduleName="use_p"; context.cursorLine=3;
    check(names(service.findStructMemberCompletionRecords({"item"},"use_p",{},context)) == QStringList({"p_value"}),
          "legal package import preserves type binding");

    const QString ownerPath=QDir(root).filePath("owner.sv"), userPath=QDir(root).filePath("user.sv");
    const QString ownerText="package owned; typedef struct packed { logic old_member; } payload_t; endpackage\n";
    const QString userText="module user_scope; import owned::*; payload_t item; endmodule\n";
    const auto workspace=slang.analyzeOverlayWorkspace({{ownerPath,ownerText},{userPath,userText}},
        {ownerPath,userPath},{},{},{},{true,false,false});
    check(workspace.error.isEmpty(),"real multi-file workspace semantic capture completed");
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
        workspace.symbols,{},{},{{ownerPath,ownerText},{userPath,userText}})));
    context={}; context.fileName=userPath; context.moduleName="user_scope"; context.cursorLine=1;
    check(names(service.findStructMemberCompletionRecords({"item"},"user_scope",{},context))==QStringList({"old_member"}),
          "multi-file imported variable points to package declaration");
    const QString moved="\n\n"+QString(ownerText).replace("old_member","new_member");
    index.updateSymbolRecordsForFile(ownerPath,slang.extractSymbolRecords(ownerPath,moved),moved);
    check(names(service.findStructMemberCompletionRecords({"item"},"user_scope",{},context))==QStringList({"new_member"}),
          "incremental owner relocation rebinds unchanged consumer reference");
    removal.fileName=ownerPath; index.updateSymbolRecordsForFiles({removal});
    check(service.findStructMemberCompletionRecords({"item"},"user_scope",{},context).isEmpty(),
          "removed cross-file type keeps unresolved identity without name fallback");

    const QString includePath=QDir(root).filePath("ports.svh"), modulePath=QDir(root).filePath("included.sv");
    check(write(includePath,"input logic included_port;\n"),"include fixture written");
    const QString module="module included(included_port);\n`include \"ports.svh\"\nendmodule\n";
    auto included=slang.extractSymbolRecords(modulePath,module);
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(included,{},{},{{modulePath,module}})));
    for (const auto& r:included) if(r.declarationKind==SymbolTaxonomy::DeclarationKind::Module) {
        const auto item=service.commandSymbolCompletionItem(r,CompletionCommandKind::Module);
        check(item.defaultValue.contains(".included_port(included_port)"), "included port belongs to actual module without same-file restriction");
    }
    check(write(includePath,"parameter INC=2,"),"included parameter declaration written");
    const QString parameterModule="module included #(parameter BEFORE=1,\n`include \"ports.svh\"\nparameter AFTER=3)(input logic a); endmodule\n";
    included=slang.extractSymbolRecords(modulePath,parameterModule);
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(included,{},{},{{modulePath,parameterModule}})));
    bool includedParameter=false;
    for(const auto& r:included) {
        if(r.name=="INC") includedParameter=r.location.fileName!=modulePath
            && r.owner.stableKey.fileName.compare(modulePath,Qt::CaseInsensitive)==0;
        if(r.declarationKind==SymbolTaxonomy::DeclarationKind::Module) {
            const auto generated=service.commandSymbolCompletionItem(r,CompletionCommandKind::Module).defaultValue;
            check(generated.contains(".INC(INC)"),
                  "included parameter participates in selected module template");
            check(generated.indexOf(".BEFORE")<generated.indexOf(".INC") && generated.indexOf(".INC")<generated.indexOf(".AFTER"),
                  "included and direct parameters preserve compilation source order");
        }
    }
    check(includedParameter,"real included member retains distinct source and module owner paths");
    check(write(includePath,"typedef struct packed { logic shared_field; } payload_t;\npayload_t item;\n"),
          "shared included type fixture written");
    const QString twice="module first_scope;\n`include \"ports.svh\"\nendmodule\n"
        "module second_scope;\n`include \"ports.svh\"\nendmodule\n";
    included=slang.extractSymbolRecords(modulePath,twice);
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(included,{},{},{{modulePath,twice}})));
    context={}; context.fileName=modulePath; context.moduleName="first_scope"; context.cursorLine=2;
    const auto firstFields=service.findStructMemberCompletionRecords({"item"},"first_scope",{},context);
    context.moduleName="second_scope"; context.cursorLine=5;
    const auto secondFields=service.findStructMemberCompletionRecords({"item"},"second_scope",{},context);
    check(firstFields.size()==1 && secondFields.size()==1
          && !(firstFields.first().stableKey==secondFields.first().stableKey),
          "one included field declared in two scopes has distinct exact identities");
    const QString aPath=QDir(root).filePath("a.sv"), bPath=QDir(root).filePath("b.sv");
    const QString a="module same_child; endmodule", b="module same_child(input logic foreign_port); endmodule";
    auto aRecords=slang.extractSymbolRecords(aPath,a); auto both=aRecords; both.append(slang.extractSymbolRecords(bPath,b));
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(both,{},{},{{aPath,a},{bPath,b}})));
    for(const auto& r:aRecords) if(r.declarationKind==SymbolTaxonomy::DeclarationKind::Module)
        check(!service.commandSymbolCompletionItem(r,CompletionCommandKind::Module).defaultValue.contains("foreign_port"),
              "empty selected module does not borrow another declaration's ports");
    const QString noPorts="module no_ports #(parameter WIDTH=1, parameter width=2); endmodule\n";
    auto parameterRecords=slang.extractSymbolRecords(aPath,noPorts);
    parameterRecords.append(parameterRecords); // Duplicate capture rows are not new declarations.
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(parameterRecords,{},{},{{aPath,noPorts}})));
    for(const auto& r:parameterRecords) if(r.declarationKind==SymbolTaxonomy::DeclarationKind::Module) {
        const auto item=service.commandSymbolCompletionItem(r,CompletionCommandKind::Module);
        check(item.defaultValue.count(".WIDTH(WIDTH)")==1 && item.defaultValue.count(".width(width)")==1
              && item.templateSlots.size()==3,"parameter-only module retains case-distinct mappings and deduplicates actual rows");
        break;
    }
}
void productCases(const QString& root) {
    MainWindow window;
    window.workspaceSessionCoordinator->setRestoreOnActivation(false);
    window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    window.analysisScheduler->shutdown();
    const auto path=QDir(root).filePath("product.sv");
    const QString text=source()+"module case_child #(parameter WIDTH=1, parameter width=2)(input logic a, input logic A); endmodule\n";
    check(write(path,text) && window.workspaceManager->openWorkspace(root),"MainWindow workspace opened");
    QElapsedTimer timer; timer.start();
    while(window.workspaceManager->isWorkspaceScanActive() && timer.elapsed()<10000) QTest::qWait(5);
    check(window.tabManager->openFileInTab(path),"product source opened");
    auto* editor=window.tabManager->getCurrentEditor(); window.show(); window.activateWindow(); editor->setFocus();
    SlangManager slang;
    auto* index=SemanticIndex::getInstance();
    index->setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
        slang.extractSymbolRecords(path,text),{},{},{{path,text}})));
    auto* palette=window.globalControlCoordinator.get();
    const auto position=[&](int pos) { QTextCursor c(editor->document()); c.setPosition(pos); editor->setTextCursor(c); QApplication::processEvents(); };
    position(text.indexOf("item.alpha =")+5);
    auto context=palette->contextProvider();
    check(context.memberAccess && context.moduleName=="top_a", "real editor context captures module and member access");
    auto items=palette->itemProvider(GlobalControlCategory::Symbols,{},context);
    check(titles(items)==QStringList({"ALPHA","alpha"}), "actual palette provider isolates same-name member declarations");
    const auto alpha=std::find_if(items.cbegin(),items.cend(),[](const auto& i){return i.title=="alpha";});
    check(alpha!=items.cend(),"correct member selectable");
    if(alpha!=items.cend()) {
        const auto before=editor->toPlainText(); palette->dispatch(*alpha); const auto after=editor->toPlainText();
        editor->undo(); check(editor->toPlainText()==before,"palette member insertion undo restores exact source");
        editor->redo(); check(editor->toPlainText()==after,"palette member insertion redo restores exact result");
        editor->setPlainText(text);
    }
    position(text.indexOf("state = A_IDLE;")+8); context=palette->contextProvider();
    items=palette->itemProvider(GlobalControlCategory::Symbols,{},context);
    check(context.expectedTypeIdentifier=="state" && titles(items).contains("A_IDLE")
          && titles(items).contains("A_RUN") && !titles(items).contains("B_IDLE") && !titles(items).contains("B_RUN"),
          "actual MainWindow expected enum candidates use bound type");
    position(text.indexOf("endmodule")); context=palette->contextProvider();
    items=palette->itemProvider(GlobalControlCategory::Symbols,"m case_child",context);
    check(items.size()==1,"real module palette returns selected declaration");
    if(items.size()==1) {
        const auto& item=items.first();
        check(item.insertionText.contains(".WIDTH(WIDTH)") && item.insertionText.contains(".width(width)")
              && item.insertionText.contains(".a(a)") && item.insertionText.contains(".A(A)"), "case-distinct parameters and ports all generated");
        check(item.templateSlots.size()==5,"all generated slots retained");
        for(const auto& slot:item.templateSlots)
            check(slot.start>=0 && slot.length>0 && slot.start+slot.length<=item.insertionText.size(),"slot range is valid in generated source");
        const auto before=editor->toPlainText(); palette->dispatch(item); const auto after=editor->toPlainText();
        check(editor->templateSlotModeActive() && editor->textCursor().selectedText()=="u_case_child", "palette insertion activates actual instance slot");
        editor->undo(); check(editor->toPlainText()==before,"module template insertion has one undo");
        editor->redo(); check(editor->toPlainText()==after,"module template insertion has one redo");
        position(after.indexOf("u_case_child")+2);
        QString message; check(editor->editInstanceSlotsAt(editor->textCursor().position(),&message), "real Edit Instance Slots entry still works");
        QTest::keyClick(editor,Qt::Key_Tab); check(editor->templateSlotModeActiveIndex()==1,"instance slot Tab navigation");
        QTest::keyClick(editor,Qt::Key_Backtab); check(editor->templateSlotModeActiveIndex()==0,"instance slot ShiftTab navigation");
        QTest::keyClick(editor,Qt::Key_Escape); check(!editor->templateSlotModeActive(),"instance slots Escape exits mode");
    }
    // The built-in asynchronous always_ff template has two linked reset slots.
    editor->setPlainText("module shared;\n\nendmodule\n"); position(15);
    items=palette->itemProvider(GlobalControlCategory::Templates,"always_ff",palette->contextProvider());
    const auto templateItem=std::find_if(items.cbegin(),items.cend(),[](const auto& item){
        return item.insertionText.contains("negedge rst_n") && item.insertionText.contains("if (!rst_n)");
    });
    check(templateItem!=items.cend(),"real template palette returns asynchronous reset template");
    if(templateItem!=items.cend()) {
        palette->dispatch(*templateItem);
        QTest::keyClick(editor,Qt::Key_Tab);
        check(editor->templateSlotModeActive() && editor->textCursor().selectedText()=="rst_n",
              "MainWindow built-in template activates linked reset slots");
        QKeyEvent key(QEvent::KeyPress,Qt::Key_R,Qt::NoModifier,"reset_n"); QApplication::sendEvent(editor,&key);
        check(editor->toPlainText().count("reset_n")==2,"real template typing mirrors exactly once");
        const auto after=editor->toPlainText();
        window.executeRegisteredUiAction("edit.undo",{});
        check(editor->toPlainText().count("rst_n")==2,"registered menu undo restores both linked slots");
        window.executeRegisteredUiAction("edit.redo",{});
        check(editor->toPlainText()==after,"registered menu redo restores one linked edit");
    }
    CodeTemplateSlot first; first.name="reset"; first.length=5; first.tabStop=1;
    auto second=first;
    const QString original="module shared;\nlogic rst_n;\nassign out = rst_n;\nendmodule\n";
    first.start=original.indexOf("rst_n"); second.start=original.lastIndexOf("rst_n");
    const CodeTemplateSlotList slotMetadata{first,second};
    auto* auxiliary=window.tabManager->createAuxiliaryView({},path,&window);
    check(auxiliary && auxiliary->document()==editor->document(),"actual TabManager auxiliary view shares text document");
    if(auxiliary) {
        auxiliary->resize(400,200); auxiliary->show();
        const auto focus=[&](MyCodeEditor* view){view->setFocus(); QApplication::processEvents();};
        const auto select=[&](MyCodeEditor* view){QTextCursor c(view->document());c.setPosition(first.start);c.setPosition(first.start+5,QTextCursor::KeepAnchor);view->setTextCursor(c);};
        const auto type=[&](MyCodeEditor* view){QKeyEvent event(QEvent::KeyPress,Qt::Key_R,Qt::NoModifier,"reset_n");QApplication::sendEvent(view,&event);};
        for(bool reverse:{false,true}) {
            auto* owner=reverse?auxiliary:editor; auto* foreign=reverse?editor:auxiliary;
            editor->setPlainText(original); focus(owner); owner->startTemplateSlotMode(0,original.size(),slotMetadata);
            focus(foreign); select(foreign); type(foreign);
            QString expected=original; expected.replace(first.start,5,"reset_n");
            check(editor->toPlainText()==expected,"foreign view ordinary input changes selected slot only in both directions");
            foreign->undo(); check(editor->toPlainText()==original,"foreign view public undo restores exactly one edit");
            foreign->redo(); check(editor->toPlainText()==expected,"foreign view public redo remains unmirrored");
        }
        editor->setPlainText(original); focus(editor); editor->startTemplateSlotMode(0,original.size(),slotMetadata);
        focus(auxiliary); auxiliary->startTemplateSlotMode(0,original.size(),slotMetadata); type(auxiliary);
        const auto both=QString(original).replace("rst_n","reset_n");
        check(editor->toPlainText()==both,"two sequential slot controllers cannot recursively double linked text");
        QTest::keyClick(auxiliary,Qt::Key_Z,Qt::ControlModifier);
        check(editor->toPlainText()==original,"keyboard undo restores complete linked edit");
        QTest::keyClick(auxiliary,Qt::Key_Y,Qt::ControlModifier);
        check(editor->toPlainText()==both,"keyboard redo restores complete linked edit");
        editor->setPlainText(original); focus(editor); editor->startTemplateSlotMode(0,original.size(),slotMetadata);
        QFocusEvent popup(QEvent::FocusOut,Qt::PopupFocusReason); QApplication::sendEvent(editor,&popup);
        check(editor->templateSlotModeActive(),"temporary popup focus preserves active slot semantics");
        {
            QMenu menu(editor); auto* action=menu.addAction("Edit selected reset");
            QObject::connect(action,&QAction::triggered,editor,[&]{editor->insertPlainText("menu_reset");});
            menu.popup(editor->mapToGlobal(QPoint(20,20))); QApplication::processEvents();
            check(editor->templateSlotModeActive(),"actual context popup retains template session");
            action->trigger(); menu.close();
            check(editor->toPlainText().count("menu_reset")==2,"popup action edits through one owning transaction");
            editor->undo(); focus(editor); editor->startTemplateSlotMode(0,original.size(),slotMetadata);
        }
        select(auxiliary);
        {auto edit=auxiliary->beginSynchronousEditTransaction(); auto cursor=auxiliary->textCursor(); cursor.insertText("external");}
        check(editor->toPlainText().count("rst_n")==1 && !editor->templateSlotModeActive(),
              "foreign transaction revokes stale controller and cannot expand its slots");
        editor->setPlainText(original); focus(editor); editor->startTemplateSlotMode(0,original.size(),slotMetadata);
        auto cursor=editor->textCursor(); cursor.insertText("raw");
        check(editor->toPlainText().count("rst_n")==1 && !editor->templateSlotModeActive(),
              "unowned QTextCursor edit does not acquire mirroring authority");
        editor->setPlainText(original); editor->startTemplateSlotMode(0,original.size(),slotMetadata);
        editor->formatDocument();
        check(editor->toPlainText().count("rst_n")==2,"format transaction cannot duplicate linked values");
        editor->setReadOnly(true); editor->startTemplateSlotMode(0,editor->toPlainText().size(),slotMetadata);
        check(!editor->templateSlotModeActive(),"read-only view cannot enter writable linked session"); editor->setReadOnly(false);
        const auto other=QDir(root).filePath("other.sv"); check(write(other,"module other; endmodule\n"),"lifecycle target created");
        editor->setPlainText(original); focus(editor); editor->startTemplateSlotMode(0,original.size(),slotMetadata);
        check(window.tabManager->openFileInTab(other),"ordinary tab switch executed");
        QApplication::processEvents(); check(!editor->templateSlotModeActive(),"tab switch revokes old slot interaction");
        focus(auxiliary); auxiliary->startTemplateSlotMode(0,original.size(),slotMetadata);
        check(window.tabManager->rebindAuxiliaryView(auxiliary,{},other) && !auxiliary->templateSlotModeActive(),
              "auxiliary rebind revokes old document slot session");
        check(window.tabManager->closeAuxiliaryView(auxiliary),"active auxiliary can close without residual writer");
        window.tabManager->activateOpenFile(path); editor->setPlainText(original);
        auto* survivor=window.tabManager->createAuxiliaryView({},path,&window);
        check(survivor!=nullptr,"auxiliary survives primary-view lifecycle case");
        focus(editor); editor->startTemplateSlotMode(0,original.size(),slotMetadata); editor->document()->setModified(false);
        QPointer<MyCodeEditor> oldPrimary(editor);
        int primaryIndex=-1;
        for(int i=0;i<window.tabManager->getDocumentModel()->openDocuments().size();++i)
            if(window.tabManager->getEditorAt(i)==editor) primaryIndex=i;
        check(primaryIndex>=0,"primary tab located for real close");
        if(primaryIndex>=0) window.tabManager->closeTab(primaryIndex);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        check(!oldPrimary || !oldPrimary->templateSlotModeActive(),"closing primary revokes its linked writer");
        if(survivor) {
            survivor->show(); focus(survivor); select(survivor); type(survivor);
            check(survivor->toPlainText().count("reset_n")==1,"surviving auxiliary edit cannot be mirrored by closed primary");
            survivor->document()->setModified(false); window.tabManager->closeAuxiliaryView(survivor);
        }
        auto* remaining=window.tabManager->getCurrentEditor();
        if(remaining) {
            remaining->setPlainText(original); focus(remaining); remaining->startTemplateSlotMode(0,original.size(),slotMetadata);
            remaining->document()->setModified(false); QPointer<MyCodeEditor> oldView(remaining);
            const auto nextRoot=QDir(root).filePath("next_workspace"); QDir().mkpath(nextRoot);
            check(write(QDir(nextRoot).filePath("next.sv"),"module next; endmodule\n")
                  && window.workspaceManager->openWorkspace(nextRoot),"workspace switch executes through real manager");
            QApplication::processEvents();
            check(!oldView || !oldView->templateSlotModeActive(),"workspace switch revokes prior editor interaction");
        } else check(false,"workspace switch has a live prior view");
    }
    for(const auto& doc:window.tabManager->getDocumentModel()->openDocuments())
        if(auto* e=window.tabManager->getDocumentModel()->editorForFile(doc.fileName)) e->document()->setModified(false);
    window.tabManager->clearCrashRecoveryAfterNormalClose(); index->clearSemanticState();
}
}
int main(int argc,char** argv) {
    setbuf(stdout,nullptr); QTemporaryDir settings;
    QCoreApplication::setOrganizationName("ZeroSlackDeclarationIdentityTest"); QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH",settings.filePath("sessions.ini").toUtf8());
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QStandardPaths::setTestModeEnabled(true);
    if(!initializeUiStyleForTest()) return 2;
    QTemporaryDir workspace; semanticCases(workspace.path()); productCases(workspace.path());
    QThreadPool::globalInstance()->waitForDone();
    std::printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
}
