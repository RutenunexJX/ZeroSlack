#include "applicationthememanager.h"
#include "duttemplateplanner.h"
#include "editorinsertpaletteservice.h"
#include "mycodeeditor.h"
#include "testuistyle.h"
#include "usertemplateservice.h"
#include "slangmanager.h"
#include "templatecapabilities.h"
#include "duttemplatesession.h"
#include <QtTest>
#include <QtWidgets>
#define private public
#include "mainwindow.h"
#include "globalcontrolpanel.h"
#undef private
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"

namespace {
void put(const QString &path, const QByteArray &bytes) { QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write(bytes); }
void evidence(const QString &name, const QByteArray &data) {
    const auto dir = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
    if (!dir.isEmpty()) put(QDir(dir).filePath(name), data);
}
const QByteArray dut = R"(module core #(parameter int W=8) (
 input logic clk, input logic [W-1:0] data_in, output logic [W-1:0] data_out,
 inout wire pad, input logic [3:0] words [0:1]);
 timeunit 1ns; timeprecision 1ps;
 assign data_out = data_in;
endmodule
)";
const QByteArray timingDut = R"(module core #(parameter int W=8) (
 input logic a, b, input logic [W-1:0] data_in, output logic [W-1:0] data_out);
 timeunit 1ns; timeprecision 1ps;
 always_ff @(posedge a or negedge b)
   if (!b) data_out <= '0; else data_out <= data_in;
endmodule
)";
UserTemplateRecord rootRecord() {
    UserTemplateService service(QString::fromUtf8(TB_TEMPLATE_FILE), QString());
    for (const auto &record : service.records()) if (record.commandToken == "@tb") return record;
    return {};
}
CodeTemplateSlot region(const TemplateInstance &item, const QString &name) {
    for (const auto &slot : item.inputs) if (slot.name == name) return slot;
    return {};
}
void place(MyCodeEditor &editor, int position) { QTextCursor cursor(editor.document()); cursor.setPosition(position); editor.setTextCursor(cursor); }
QString installRoot(MyCodeEditor &editor) {
    const auto record = rootRecord();
    TemplateInsertionSpec spec; spec.label = record.label; spec.snapshot = record.command;
    spec.fragments = {{0, int(record.insertText.size()), false}};
    QString error;
    if (!editor.insertSimulationTemplate(record.insertText, record.templateSlots, spec, {}, {}, &error)) qFatal("%s", qPrintable(error));
    return editor.selectedTemplateId();
}
SimulationCommandRequest requestFor(MyCodeEditor &editor, const QString &rootId, const QString &dir) {
    SimulationCommandRequest request;
    request.context.fileName = QDir(dir).filePath("tb.sv");
    request.context.documentText = editor.toPlainText(); request.context.moduleName = "tb";
    request.item.title = "@dut"; request.item.parameters = {{"kind", "dut"}};
    request.item.templateParentId = rootId; request.item.templateParentSlot = "instances"; request.item.templateContent = "instances";
    const auto root = *editor.templateModel()->instance(rootId);
    request.item.replacementStart = region(root, "instances").start + region(root, "instances").length;
    request.context.cursorPosition = request.item.replacementStart;
    request.orderedFiles = {QDir(dir).filePath("dut.sv"), request.context.fileName};
    request.cancelled = std::make_shared<std::atomic_bool>(false);
    return request;
}
void keys(const QString &value) { for (const auto ch : value) { QTest::keyClicks(QApplication::focusWidget(), QString(ch)); QCoreApplication::processEvents(); } }
void enter() { QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return); QCoreApplication::processEvents(); }
}
class TbDutTemplateTest : public QObject {
    Q_OBJECT
private slots:
    void automaticSignalRoles() {
        QTemporaryDir dir;
        const auto file = dir.filePath("roles.sv");
        QString source = R"(`timescale 1ns/1ps
module leaf(input logic a,b,d, output logic q);
 always_ff @(posedge a or negedge b) if (!b) q <= 1'b1; else q <= d;
endmodule
module highleaf(input logic a,b,d, output logic q);
 always_ff @(posedge a or posedge b) if (b) q <= 1'b0; else q <= d;
endmodule
module wrapper #(parameter bit SEL=0)(input logic x,y,z,d, output logic q);
 wire alias_x; assign alias_x = x;
 if (SEL) begin: selected
   leaf u(.a(y),.b(!z),.d(d),.q(q));
 end else begin: selected
   leaf u(.a(alias_x),.b(!z),.d(d),.q(q));
 end
endmodule
module synchronous(input logic a,b,d, output logic [3:0] q,r);
 always_ff @(posedge a) if (b) begin q <= 4'hf; r <= 4'h3; end
 else begin q <= {3'b0,d}; r <= q; end
endmodule
module ambiguous(input logic a,b,d, output logic q);
 always_ff @(posedge a) if (b) q <= 0; else q <= d;
endmodule
module derived(input logic a,d, output logic q);
 logic divider;
 always_ff @(posedge a) divider <= ~divider;
 always_ff @(posedge divider) q <= d;
endmodule
module tb;
 logic a,other,z,d,sync_c,sync_r,amb_c,amb_r,gate_a,gate_en,gate_r,conflict,derived_a;
 logic clk_fake,rst_fake;
 wire q0,q1,q2,q3,q4,q5,q6; wire [3:0] sq,sr;
 wrapper #(.SEL(0)) first(.x(a),.y(other),.z(z),.d(d),.q(q0));
 wrapper #(.SEL(0)) second(.x(a),.y(other),.z(z),.d(d),.q(q1));
 synchronous s(.a(sync_c),.b(sync_r),.d(d),.q(sq),.r(sr));
 ambiguous control(.a(amb_c),.b(amb_r),.d(d),.q(q2));
 leaf gated(.a(gate_a & gate_en),.b(gate_r),.d(d),.q(q3));
 leaf low(.a(a),.b(conflict),.d(d),.q(q4));
 highleaf high(.a(a),.b(conflict),.d(d),.q(q5));
 derived internal_clock(.a(derived_a),.d(d),.q(q6));
endmodule
)";
        auto analyze = [&](const QString &text) {
            SemanticAnalysisRequest request; request.sourceOverrides[file] = text;
            SemanticInputCapture capture(request); SlangManager slang;
            return slang.analyzeCapturedWorkspace(capture, {file}, {}, {}, "tb", {}, {true, true, false});
        };
        auto analysis = analyze(source);
        QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error));
        for (const auto &diagnostic : analysis.diagnostics) QVERIFY2(diagnostic.severity != SemanticDiagnostic::Error, qPrintable(diagnostic.message));
        auto info = [&](const QString &path) {
            for (const auto &record : analysis.symbols) if (record.presentation.objectInfoByPath.contains(path)) return record.presentation.objectInfoByPath.value(path);
            return SemanticElaboratedSymbolInfo{};
        };
        auto has = [&](const QString &path, const QString &role) {
            for (const auto &proof : info(path).signalRoles) if (proof.role == role) return true;
            return false;
        };
        QVERIFY(has("tb.a", "Clock")); QVERIFY(!has("tb.other", "Clock"));
        QVERIFY(has("tb.z", "Reset")); QVERIFY(!has("tb.z", "Clock"));
        QCOMPARE(info("tb.z").signalRoles.first().activeLevel, 1);
        QVERIFY(info("tb.z").signalRoles.first().mappingChain.size() >= 3);
        QSet<QString> instances;
        for (const auto &proof : info("tb.z").signalRoles) {
            instances.insert(proof.sourceInstance); QVERIFY(proof.sourceLine > 0);
            QCOMPARE(proof.sourceFile, file); QVERIFY(!proof.parameters.isEmpty()); QVERIFY(!proof.analysisIdentity.isEmpty());
        }
        QCOMPARE(instances.size(), 2);
        QVERIFY(has("tb.sync_r", "Reset")); QCOMPARE(info("tb.sync_r").signalRoles.first().activeLevel, 1);
        QVERIFY(has("tb.amb_c", "Clock")); QVERIFY(!has("tb.amb_r", "Reset"));
        QVERIFY(!has("tb.gate_a", "Clock")); QVERIFY(!has("tb.gate_en", "Clock"));
        QVERIFY(!info("tb.gate_a").signalRoleIssues.isEmpty());
        QVERIFY(!has("tb.clk_fake", "Clock")); QVERIFY(!has("tb.rst_fake", "Reset"));
        QVERIFY(info("tb.conflict").signalRoleConflict);
        QVERIFY(has("tb.internal_clock.divider", "Clock")); QVERIFY(has("tb.derived_a", "Clock"));
        auto snapshot = std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(analysis.symbols, {}, {}, {{file,source}}));
        TemplateInputContext context; context.snapshot = snapshot; context.location.fileName = file;
        context.location.moduleName = "tb"; context.location.cursorPosition = source.lastIndexOf("endmodule");
        context.location.cursorLine = source.left(context.location.cursorPosition).count('\n') + 1;
        QVariantMap input{{"kind","Signal"},{"scope","visible"},{"access","write"},{"integral",true},
            {"minWidth",1},{"maxWidth",1},{"unpackedRank",0},{"moduleVariable",true},{"variableOnly",true},{"closedChoice",true},{"role","Clock"}};
        auto choices = TemplateInputService::candidates(input, context, {});
        QVERIFY2(choices.values.contains("a"), qPrintable(choices.reason)); QVERIFY(!choices.values.contains("divider"));
        QVERIFY(!choices.values.contains("gate_a")); QVERIFY(!choices.evidence.value("a").isEmpty());
        input["role"] = "Reset"; choices = TemplateInputService::candidates(input, context, {});
        QVERIFY2(choices.values.contains("z"), qPrintable(choices.reason)); QVERIFY(choices.values.contains("sync_r"));
        QVERIFY(!choices.values.contains("amb_r")); QVERIFY(!choices.values.contains("conflict"));
        for (const auto &record : analysis.symbols) if (record.presentation.objectInfoByPath.contains("tb.z"))
            context.roles[TemplateInputService::roleKey(record, "tb.z")] = "Clock";
        QVERIFY(!TemplateInputService::candidates(input, context, {}).values.contains("z"));
        context.roles.clear();
        auto staleSource = source; staleSource.replace(".z(z)", ".z(d)");
        context.snapshot = std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(analysis.symbols, {}, {}, {{file,staleSource}}));
        QVERIFY(TemplateInputService::candidates(input, context, {}).values.isEmpty()); context.snapshot = snapshot;
        const auto oldIdentity = info("tb.a").signalRoles.first().analysisIdentity;
        QJsonArray proofs;
        for (const auto &record : analysis.symbols) for (auto it = record.presentation.objectInfoByPath.cbegin(); it != record.presentation.objectInfoByPath.cend(); ++it)
            for (const auto &proof : it->signalRoles) {
                QJsonObject parameters; for (auto p = proof.parameters.cbegin(); p != proof.parameters.cend(); ++p) parameters[p.key()] = p.value();
                proofs.append(QJsonObject{{"object",it.key()},{"role",proof.role},{"edge",proof.edge},{"activeLevel",proof.activeLevel},
                    {"rule",proof.rule},{"source",proof.sourceFile},{"line",proof.sourceLine},{"instance",proof.sourceInstance},
                    {"mapping",QJsonArray::fromStringList(proof.mappingChain)},{"parameters",parameters},{"analysis",proof.analysisIdentity},{"conflict",it->signalRoleConflict}});
            }
        evidence("automatic-role-proofs.json", QJsonDocument(proofs).toJson()); evidence("automatic-roles.sv", source.toUtf8());
        source.replace("wrapper #(.SEL(0))", "wrapper #(.SEL(1))"); analysis = analyze(source);
        QVERIFY(has("tb.other", "Clock")); QVERIFY(info("tb.other").signalRoles.first().analysisIdentity != oldIdentity);
        context.current = [] { return false; }; QVERIFY(TemplateInputService::candidates(input, context, {}).values.isEmpty());
        analysis = analyze("module tb; logic a,z; missing_encrypted_ip opaque(.a(a),.b(z)); endmodule\n");
        QVERIFY(!has("tb.a", "Clock")); QVERIFY(!has("tb.z", "Reset"));
        QVERIFY(info("tb.a").signalRoleIssues.join(" ").contains("black-box"));
    }
    void semanticTransactions() {
        QTemporaryDir dir; put(dir.filePath("dut.sv"), dut);
        MyCodeEditor editor; editor.setDocumentFileName(dir.filePath("tb.sv"));
        const auto rootId = installRoot(editor); editor.setTemplateAuthoringLocked(true);
        const auto baseline = editor.toPlainText();
        auto request = requestFor(editor, rootId, dir.path());
        DutTemplateOptions options; options.module = "core"; options.parameters["W"] = "12";
        auto analysis = DutTemplatePlanner::analyze(request, editor.templateModel()->instances(), options);
        QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error));
        QCOMPARE(analysis.ports.size(), 5);
        options.instance = "unit";
        const auto plan = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QString error;
        QVERIFY2(editor.applyTemplateEdits(plan.edits, &error), qPrintable(error));
        const auto firstId = editor.selectedTemplateId();
        const auto first = editor.toPlainText();
        evidence("first-tb.sv", first.toUtf8());
        QVERIFY(first.contains("[11:0]")); QVERIFY(first.contains(QRegularExpression("wire [^;\\n]*unit_pad;")));
        QVERIFY(first.contains("unit_words [0:1]"));
        QVERIFY(first.indexOf("unit_clk;") < first.indexOf("// Instances"));
        QVERIFY(first.indexOf(".clk(unit_clk)") > first.indexOf("// Instances"));
        QVERIFY(editor.templateModel()->issue().isEmpty());
        MyCodeEditor second; second.setDocument(editor.document()); second.setTemplateAuthoringLocked(true);
        QCOMPARE(second.templateModel(), editor.templateModel());
        editor.undo(); QCOMPARE(editor.toPlainText(), baseline); QCOMPARE(editor.templateModel()->instances().size(), 1);
        editor.redo(); QCOMPARE(second.toPlainText(), first); QVERIFY(editor.templateModel()->instance(firstId));
        auto stored = editor.templateModel()->serialize(dir.filePath("tb.sv"));
        QTextDocument restored; restored.setPlainText(first);
        QVERIFY(DocumentTemplateModel::get(&restored)->restore(stored, dir.filePath("tb.sv")));
        QCOMPARE(DocumentTemplateModel::get(&restored)->instances().size(), 3);
        request = requestFor(editor, rootId, dir.path());
        options.reuse.insert("clk"); options.bindings["clk"] = "unit_clk";
        auto next = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        QVERIFY2(next.error.isEmpty(), qPrintable(next.error));
        QVERIFY2(editor.applyTemplateEdits(next.edits, &error), qPrintable(error));
        const auto secondId = editor.selectedTemplateId();
        QVERIFY(editor.toPlainText().contains("unit_2 ("));
        QCOMPARE(editor.toPlainText().count(".clk(unit_clk)"), 2);
        QCOMPARE(editor.toPlainText().count("unit_clk;"), 1);
        auto instance = *editor.templateModel()->instance(firstId);
        request = requestFor(editor, rootId, dir.path());
        request.item.replacementStart = instance.fragments.first().start; request.item.replacementLength = instance.fragments.first().length;
        options.replaceId = firstId; options.parameters["W"] = "16"; options.instance = "renamed";
        next = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        QVERIFY2(next.error.isEmpty(), qPrintable(next.error));
        QVERIFY(editor.applyTemplateEdits(next.edits, &error));
        QVERIFY(editor.toPlainText().contains("[15:0]")); QVERIFY(editor.toPlainText().contains("[11:0]"));
        QVERIFY(editor.templateModel()->instance(secondId)); QCOMPARE(editor.toPlainText().count(".clk(unit_clk)"), 2);
        instance = *editor.templateModel()->instance(firstId);
        TemplateDocumentEdit remove;
        remove.start = instance.fragments.first().start; remove.length = instance.fragments.first().length;
        remove.parentId = rootId; remove.parentSlot = "instances"; remove.replaceId = firstId;
        QVERIFY(editor.applyTemplateEdits({remove}, &error));
        QVERIFY(editor.toPlainText().contains("unit_clk;")); QVERIFY(editor.templateModel()->instance(secondId));
        editor.undo(); QVERIFY(editor.templateModel()->instance(firstId));
        stored = editor.templateModel()->serialize(dir.filePath("tb.sv"));
        restored.setPlainText(editor.toPlainText());
        QVERIFY(DocumentTemplateModel::get(&restored)->restore(stored, dir.filePath("tb.sv")));
        evidence("generated-tb.sv", editor.toPlainText().toUtf8()); evidence("dut.sv", dut);
        evidence("generated-metadata.json", QJsonDocument(stored).toJson());
    }
    void rejectionAndRegions() {
        QTemporaryDir dir; put(dir.filePath("dut.sv"), dut);
        MyCodeEditor editor; const auto rootId = installRoot(editor);
        auto request = requestFor(editor, rootId, dir.path()); const auto baseline = editor.toPlainText();
        DutTemplateOptions options; options.module = "core"; options.parameters["W"] = "unknown";
        QVERIFY(!DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options).error.isEmpty());
        options.parameters["W"] = "8"; options.reuse.insert("clk"); options.bindings["clk"] = "missing";
        QVERIFY(!DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options).error.isEmpty());
        QCOMPARE(editor.toPlainText(), baseline);
        request.cancelled->store(true);
        QVERIFY(!DutTemplatePlanner::analyze(request, editor.templateModel()->instances(), options).error.isEmpty());
        const auto root = *editor.templateModel()->instance(rootId);
        for (const auto &name : {"definitions", "instances", "behaviors"}) {
            const auto slot = region(root, name); auto context = request.context;
            context.templateParentId = rootId; context.templateParentSlot = name;
            context.templateContent = slot.input.value("content").toString();
            context.replacementStart = context.cursorPosition = slot.start + 2;
            QCOMPARE(SimulationCommandPlanner::placement(context, {{"kind", "dut"}}, context.templateContent).status == SimulationPlacementStatus::Applicable, QString(name) == "instances");
        }
        editor.setTemplateAuthoringLocked(true);
        const auto definitions = region(root, "definitions");
        QVERIFY(editor.editTemplateInstance(rootId, "definitions", definitions.start + 2));
        QVERIFY(editor.replaceTemplateInput(editor.textCursor(), "logic user_clock;\n"));
        QVERIFY(editor.toPlainText().contains("logic user_clock;"));
        place(editor, 0); QTest::keyClick(&editor, Qt::Key_Delete); QVERIFY(editor.toPlainText().startsWith("module tb;"));
        auto readOnly = requestFor(editor, rootId, dir.path()); options.reuse.clear();
        const auto plan = DutTemplatePlanner::prepare(readOnly, editor.templateModel()->instances(), options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error)); editor.setReadOnly(true);
        QString error; QVERIFY(!editor.applyTemplateEdits(plan.edits, &error));
    }
    void typeAndValueBoundaries() {
        QTemporaryDir dir; const auto file = dir.filePath("dut.sv");
        put(file, R"(package types;
 typedef enum logic [2:0] {IDLE=3'b001, BUSY=3'b101} state_t;
 typedef struct packed {logic [2:0] tag; logic [4:0] value;} payload_t;
endpackage
module core(input types::state_t mode, input types::payload_t data, output logic [7:0] q);
 timeunit 1ns; timeprecision 1ps;
 assign q = data;
endmodule
)");
        MyCodeEditor editor; editor.setDocumentFileName(dir.filePath("tb.sv"));
        const auto root = installRoot(editor); auto request = requestFor(editor, root, dir.path());
        DutTemplateOptions options; options.module = "core";
        auto plan = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QString error; QVERIFY(editor.applyTemplateEdits(plan.edits, &error));
        QVERIFY(editor.toPlainText().contains("types::state_t")); QVERIFY(editor.toPlainText().contains("types::payload_t"));
        auto reuse = options;
        reuse.bindings = {{"mode","u_core_mode"},{"data","u_core_data"}};
        reuse.reuse = {"mode","data"};
        const auto typedAnalysis = DutTemplatePlanner::analyze(requestFor(editor, root, dir.path()), editor.templateModel()->instances(), reuse);
        QJsonArray typeProofs;
        for (const auto& record : typedAnalysis.records) if (record.name.startsWith("u_core_")) {
            QJsonObject shapes;
            for (auto it = record.presentation.declaredTypeFactsByPath.cbegin(); it != record.presentation.declaredTypeFactsByPath.cend(); ++it)
                shapes[it.key()] = QJsonObject{{"base",it->externalDeclarationBaseText},{"complete",it->complete},{"failure",it->failureReason}};
            typeProofs.append(QJsonObject{{"name",record.name},{"owner",record.owner.name},
                {"kind",int(record.declarationKind)},{"file",record.location.fileName},{"position",record.location.position},
                {"defaultBase",record.presentation.defaultDeclaredTypeFacts.externalDeclarationBaseText},{"paths",shapes}});
        }
        for (const auto& port : typedAnalysis.ports) typeProofs.append(QJsonObject{{"port",port.name},{"type",port.type},{"reuse",QJsonArray::fromStringList(port.compatibleSignals)}});
        evidence("typed-port-reuse.json", QJsonDocument(typeProofs).toJson());
        const auto sharedTypes = DutTemplatePlanner::prepare(requestFor(editor, root, dir.path()), editor.templateModel()->instances(), reuse);
        QVERIFY2(sharedTypes.error.isEmpty(), qPrintable(sharedTypes.error));
        QVERIFY(sharedTypes.edits.first().text.contains(".data(u_core_data)"));
        QVERIFY(sharedTypes.edits.first().text.contains(".mode(u_core_mode)"));
        editor.undo();
        put(file, "interface link; logic a; endinterface\nmodule core(link p); endmodule\n");
        request = requestFor(editor, root, dir.path());
        QVERIFY(!DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options).error.isEmpty());
        put(file, "module core #(parameter type T=logic)(input T a); endmodule\n");
        QVERIFY(!DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options).error.isEmpty());
        QVariantMap frequency{{"kind","Value"},{"finite",true},{"minimum","0"},{"exclusiveMinimum",true}};
        QVERIFY(TemplateInputService::validate(frequency,"156.25",{},{}).isEmpty());
        for (const auto &value : {"0", "-1", "1e309", "NaN"}) QVERIFY(!TemplateInputService::validate(frequency,value,{},{}).isEmpty());
        frequency.remove("exclusiveMinimum");
        QVERIFY(TemplateInputService::validate(frequency,"12.5",{},{}).isEmpty());
        QVERIFY(TemplateInputService::validate(frequency,"0",{},{}).isEmpty());
        const QVariantMap polarity{{"kind","Choice"},{"choices",QStringList{"0","1"}}};
        QVERIFY(!TemplateInputService::validate(polarity,"2",{},{}).isEmpty());
        auto invalid = *editor.templateModel()->instance(root);
        for (auto &slot : invalid.inputs) if (slot.name == "definitions") slot.start = region(invalid,"behaviors").start;
        QVERIFY(!DutTemplatePlanner::rootError(invalid).isEmpty());
    }
    void capabilitiesAndStalePreparation() {
        UserTemplateService catalog(QString::fromUtf8(TB_TEMPLATE_FILE), {});
        UserTemplateRecord clock;
        for (const auto& record : catalog.records()) {
            const auto rendered = TemplateCapabilities::render(record.command, record.insertText, record.templateSlots);
            QVERIFY2(rendered.error.isEmpty(), qPrintable(rendered.error)); QVERIFY(!rendered.text.contains("{{="));
            if (record.commandToken == "@clk_init") clock = record;
        }
        QVERIFY(!clock.insertText.isEmpty());
        auto bad = clock.command; bad["capability"] = "executeProcess";
        QVERIFY(!TemplateCapabilities::validate(bad).isEmpty());
        bad = clock.command; bad["capabilityVersion"] = "1";
        QVERIFY(!TemplateCapabilities::validate(bad).isEmpty());
        for (const auto& version : {QVariant(true), QVariant(1.5), QVariant(2)}) {
            bad = clock.command; bad["capabilityVersion"] = version;
            QVERIFY(!TemplateCapabilities::validate(bad).isEmpty());
        }
        bad = clock.command; bad["render"] = QVariantMap{{"expressions", true}};
        QVERIFY(!TemplateCapabilities::validate(bad).isEmpty());
        bad = clock.command;
        bad["render"] = QVariantMap{{"expressions",QVariantMap{{"a",QVariantMap{{"expression","b"}}},{"b",QVariantMap{{"expression","a"}}}}}};
        QVERIFY(!TemplateCapabilities::validate(bad).isEmpty());
        auto inputs = clock.templateSlots;
        for (auto& slot : inputs) if (slot.input.value("role") == "Clock") slot.input["closedChoice"] = false;
        QVERIFY(!TemplateCapabilities::render(clock.command, clock.insertText, inputs).error.isEmpty());
        QTemporaryDir dir; put(dir.filePath("dut.sv"), dut);
        MyCodeEditor editor; editor.setDocumentFileName(dir.filePath("tb.sv"));
        const auto root = installRoot(editor); auto request = requestFor(editor, root, dir.path());
        const auto before = editor.toPlainText(); int checks = 0; QString feedback;
        auto* session = new DutTemplateSession(&editor, request,
            [&] { return ++checks > 1 ? QString("Source identity changed") : QString(); },
            [&](const QString& text) { feedback = text; }, &editor);
        session->start(); QTRY_VERIFY_WITH_TIMEOUT(feedback.contains("Source identity changed"), 10000);
        QCOMPARE(editor.toPlainText(), before); QCOMPARE(editor.templateModel()->instances().size(), 1);
    }
    void mainWindowKeyboard() {
        QTemporaryDir dir;
        put(dir.filePath("dut.sv"), timingDut); put(dir.filePath("tb.sv"), {});
        auto *service = UserTemplateService::getInstance(); const auto original = service->globalTemplateLocation();
        const auto catalog = qEnvironmentVariable("TB_TEST_CATALOG", QString::fromUtf8(TB_TEMPLATE_FILE));
        QVERIFY(QFile::copy(catalog, dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        QString rootCommand, moduleCommand, clockCommand, resetCommand;
        for (const auto& record : service->records()) {
            if (TemplateCapabilities::root(record.command)) rootCommand = TemplateCapabilities::activationQuery(record.command);
            else if (TemplateCapabilities::module(record.command)) moduleCommand = record.commandToken;
            else for (const auto& slot : record.templateSlots) {
                if (slot.input.value("role") == "Clock") clockCommand = record.commandToken;
                if (slot.input.value("role") == "Reset") resetCommand = record.commandToken;
            }
        }
        QVERIFY(!rootCommand.isEmpty() && !moduleCommand.isEmpty() && !clockCommand.isEmpty() && !resetCommand.isEmpty());
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path()); window.tabManager->openFileInTab(dir.filePath("tb.sv"));
        auto *editor = window.tabManager->getCurrentEditor(); editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        QTest::keyPress(editor, Qt::Key_F24); QTest::keyClick(editor, Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
        keys(rootCommand);
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 1, 20000);
        QVERIFY(editor->templateInputEditing());
        keys("tb_example"); QCOMPARE(editor->templateModel()->instances().size(), 1);
        QTest::keyClick(editor, Qt::Key_Tab); QTest::keyClick(editor, Qt::Key_Tab);
        QCOMPARE(editor->templateContainerContent(), "instances");
        keys(moduleCommand); enter();
        QPointer<QWidget> panel;
        QTRY_VERIFY_WITH_TIMEOUT((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->isVisible(), 20000);
        QVERIFY(panel->accessibleName().contains("DUT module")); keys("CRE"); enter();
        QTRY_VERIFY_WITH_TIMEOUT(panel && panel->isVisible() && panel->accessibleName().contains("instance name"), 20000);
        enter(); QVERIFY(panel->accessibleName().contains("Parameter W")); keys("12"); enter();
        QTRY_VERIFY_WITH_TIMEOUT(panel && panel->isVisible() && panel->accessibleName().startsWith("a "), 20000);
        for (int i=0; i<4; ++i) enter();
        QVERIFY(panel->accessibleName().contains("Enter applies")); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 3, 20000);
        QVERIFY(editor->toPlainText().contains("[11:0]"));
        const auto text = editor->toPlainText(); const auto selected = editor->selectedTemplateId();
        enter();
        QTRY_VERIFY((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->isVisible());
        enter(); QTRY_VERIFY_WITH_TIMEOUT(panel && panel->isVisible() && panel->accessibleName().contains("DUT module"), 20000);
        enter(); QTRY_VERIFY_WITH_TIMEOUT(panel && panel->isVisible() && panel->accessibleName().contains("instance name"), 20000);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape); QCoreApplication::processEvents();
        QCOMPARE(editor->toPlainText(), text); QCOMPARE(editor->selectedTemplateId(), selected);
        editor->undo(); QCOMPARE(editor->templateModel()->instances().size(), 1); editor->redo(); QCOMPARE(editor->toPlainText(), text);
        TemplateInstance root;
        for (const auto &item : editor->templateModel()->instances()) if (TemplateCapabilities::root(item.snapshot)) root = item;
        const auto behaviors = TemplateCapabilities::region(root.snapshot, "behaviors");
        QTest::keyClick(editor, Qt::Key_Down); QCOMPARE(editor->selectedTemplateId(), root.id);
        enter(); QTest::keyClick(editor, Qt::Key_Backtab);
        QCOMPARE(editor->templateContainerContent(), "behaviors");
        QCOMPARE(editor->textCursor().position(), region(root,behaviors).start + region(root,behaviors).length);
        keys(clockCommand); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 4, 20000);
        const auto clockId = editor->selectedTemplateId();
        const auto closed = editor->toPlainText();
        keys("invalid_signal"); QCOMPARE(editor->toPlainText(), closed);
        QApplication::clipboard()->setText("invalid_signal"); QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), closed);
        editor->QPlainTextEdit::paste(); QCOMPARE(editor->toPlainText(), closed);
        QInputMethodEvent ime; ime.setCommitString("invalid_signal"); QApplication::sendEvent(editor, &ime);
        QCOMPARE(editor->toPlainText(), closed);
        QTest::keyClick(editor, Qt::Key_Down); QTest::keyClick(editor, Qt::Key_Tab); keys("0");
        QTest::keyClick(editor, Qt::Key_Backtab); QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(editor->templateInputEditing()); QCOMPARE(editor->selectedTemplateId(), clockId);
        QTest::keyClick(editor, Qt::Key_Tab); keys("156.25");
        QVERIFY(editor->toPlainText().contains("frequency_mhz = 156.25;"));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor->templateContainerContent(), "behaviors");
        keys(resetCommand); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 5, 20000);
        QTest::keyClick(editor, Qt::Key_Down); QTest::keyClick(editor, Qt::Key_Tab); keys("12.5");
        QTest::keyClick(editor, Qt::Key_Tab); keys("0");
        QVERIFY(editor->toPlainText().contains("hold_ns = 12.5;"));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor->templateContainerContent(), "behaviors");
        QVERIFY(window.tabManager->saveCurrentTab());
        const auto saved = editor->toPlainText();
        QFile catalogFile(dir.filePath("templates.json")); QVERIFY(catalogFile.open(QIODevice::ReadOnly));
        auto changedCatalog = QJsonDocument::fromJson(catalogFile.readAll()).object(); catalogFile.close();
        auto rows = changedCatalog["templates"].toArray();
        for (int i=0; i<rows.size(); ++i) { auto row = rows[i].toObject(); if (row["command"] == clockCommand) {
            row["body"] = row["body"].toString() + "// New catalog definition\n"; rows[i] = row;
        }}
        changedCatalog["templates"] = rows; put(dir.filePath("templates.json"), QJsonDocument(changedCatalog).toJson());
        service->reload(); QCOMPARE(editor->toPlainText(), saved);
        QVERIFY(window.tabManager->closeTabsInWorkspace(dir.path()));
        QVERIFY(window.tabManager->openFileInTab(dir.filePath("tb.sv")));
        editor = window.tabManager->getCurrentEditor(); QCOMPARE(editor->toPlainText(), saved);
        QCOMPARE(editor->templateModel()->instances().size(), 5); QVERIFY(editor->templateModel()->instance(clockId));
        QVERIFY(editor->editTemplateInstance(clockId, "freq_mhz"));
        QApplication::clipboard()->setText("100.0"); QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(editor->toPlainText().contains("frequency_mhz = 100.0;"));
        editor->undo(); QCOMPARE(editor->toPlainText(), saved); editor->redo(); QVERIFY(editor->toPlainText().contains("frequency_mhz = 100.0;"));
        editor->undo(); QCOMPARE(editor->toPlainText(), saved);
        evidence("main-window-tb.sv", saved.toUtf8()); evidence("main-window-dut.sv", timingDut);
        evidence("main-window-metadata.json", QJsonDocument(editor->templateModel()->serialize(dir.filePath("tb.sv"))).toJson());
        QFile inputCatalog(catalog); QVERIFY(inputCatalog.open(QIODevice::ReadOnly)); evidence("used-user-templates.json", inputCatalog.readAll());
        const auto artifacts = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR"); if (!artifacts.isEmpty()) window.grab().save(QDir(artifacts).filePath("main-window.png"));
        for (auto *view : window.tabManager->openEditors()) view->document()->setModified(false);
        service->setGlobalTemplateFilePath(original);
    }
};
int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QStandardPaths::setTestModeEnabled(true);
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  QCoreApplication::setOrganizationName("ZeroSlackTbDutTemplateTest");
  QCoreApplication::setApplicationName("Isolated");
  QTemporaryDir profile;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     profile.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                     profile.path());
  qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH",
          profile.filePath("legacy.ini").toUtf8());
  UserTemplateService::getInstance()->setGlobalTemplateFilePath(
      profile.filePath("templates.json"));
  if (!initializeUiStyleForTest())
    return 2;
  ApplicationThemeManager::instance().setAnimationsEnabled(false);
  TbDutTemplateTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "tb_dut_template_test.moc"
