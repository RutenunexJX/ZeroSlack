// Headless completion-logic test. Populates semantic records from Slang, then
// drives CompletionService query methods. No GUI window is shown.
#include "slangmanager.h"
#include "analysisscheduler.h"
#include "completionmodel.h"
#include "completionservice.h"
#include "codetemplateservice.h"
#include "inlinecommandmode.h"
#include "commandlayercommandregistry.h"
#include "customabbreviationservice.h"
#include "diagnosticsrefreshcontroller.h"
#include "diagnosticnavigationservice.h"
#include "documentmodel.h"
#include "effectivevalueservice.h"
#include "editorsemanticcontextservice.h"
#include "editorinsertpaletteservice.h"
#include "foldblockshelfmodel.h"
#include "foldshelfpersistenceservice.h"
#include "foldshelfrestoreservice.h"
#include "formatterservice.h"
#include "structuredwhitespaceformatter.h"
#include "fsmgraphservice.h"
#include "ghostannotationservice.h"
#include "globalcontrolservice.h"
#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "packagetoolservice.h"
#include "relationshipservice.h"
#include "relationshipanalysiscontroller.h"
#include "rtlbatcheditservice.h"
#include "semanticdecorationservice.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "statetransitiontriggerservice.h"
#include "symbolanalyzer.h"
#include "symboltaxonomy.h"
#include "tsdocument.h"
#include "usertemplateservice.h"
#include "wavepreviewpanelcoordinator.h"
#include "wavepreviewservice.h"
#include "workspaceanalysisplanservice.h"
#include "workspaceanalysisrequestqueue.h"
#include "workspaceconfigurationservice.h"
#include "workspaceignoreservice.h"
#include "workspacesessionstateservice.h"
#include "workspacemanager.h"
#include "workspacesymbolanalysiscontroller.h"
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QKeyEvent>
#include <QLabel>
#include <QSettings>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <QTemporaryDir>
#include <QTextCursor>

static int g_checks = 0;
static int g_fails = 0;

static std::shared_ptr<SemanticIndexSnapshot> sharedSnapshotFromRecords(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships = {},
    const QList<SemanticDiagnostic>& diagnostics = {},
    const QHash<QString, QString>& fileContents = {})
{
    return std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records,
            relationships,
            diagnostics,
            fileContents));
}

static QStringList sorted(QStringList l) { l.sort(); return l; }

static void expectEq(const char* what, const QString& got, const QString& want) {
    ++g_checks;
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=\"%s\" want=\"%s\"\n", ok ? "PASS" : "FAIL", what,
           got.toLocal8Bit().constData(), want.toLocal8Bit().constData());
}

static void expectBool(const char* what, bool got, bool want) {
    ++g_checks;
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=%s want=%s\n", ok ? "PASS" : "FAIL", what,
           got ? "true" : "false", want ? "true" : "false");
}

static bool sendEditorKey(MyCodeEditor& editor,
                          int key,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                          const QString& text = QString())
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    QCoreApplication::sendEvent(&editor, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return event.isAccepted();
}

static void insertAtEditorCursor(MyCodeEditor& editor, const QString& text)
{
    QTextCursor cursor = editor.textCursor();
    cursor.insertText(text);
    editor.setTextCursor(cursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static bool writeTextFile(const QString& fileName, const QString& text)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QFile::Text))
        return false;
    QTextStream out(&file);
    out << text;
    return true;
}

static void expectList(const char* what, QStringList got, QStringList want) {
    ++g_checks;
    got = sorted(got);
    want = sorted(want);
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", what,
           got.join(",").toLocal8Bit().constData(),
           want.join(",").toLocal8Bit().constData());
}

template <typename Predicate>
static bool waitForEventPredicate(Predicate predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() <= timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate())
            return true;
    }
    return predicate();
}

static void expectGhostContains(const char* what,
                                const GhostAnnotationReport& report,
                                GhostAnnotationKind kind,
                                int line,
                                const QString& contains)
{
    ++g_checks;
    bool ok = false;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.kind == kind
            && annotation.line == line
            && annotation.text.contains(contains)) {
            ok = true;
            break;
        }
    }
    if (!ok)
        ++g_fails;
    QStringList seen;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.line == line)
            seen << annotation.text;
    }
    printf("[%s] %-34s want=\"%s\" line=%d seen=[%s]\n",
           ok ? "PASS" : "FAIL",
           what,
           contains.toLocal8Bit().constData(),
           line,
           seen.join(",").toLocal8Bit().constData());
}

static void expectGhostNotContains(const char* what,
                                   const GhostAnnotationReport& report,
                                   GhostAnnotationKind kind,
                                   int line,
                                   const QString& contains)
{
    ++g_checks;
    bool ok = true;
    QStringList seen;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.line == line)
            seen << annotation.text;
        if (annotation.kind == kind
            && annotation.line == line
            && annotation.text.contains(contains)) {
            ok = false;
        }
    }
    if (!ok)
        ++g_fails;
    printf("[%s] %-34s reject=\"%s\" line=%d seen=[%s]\n",
           ok ? "PASS" : "FAIL",
           what,
           contains.toLocal8Bit().constData(),
           line,
           seen.join(",").toLocal8Bit().constData());
}

static void expectGhostEquals(const char* what,
                              const GhostAnnotationReport& report,
                              int line,
                              const QString& expected)
{
    ++g_checks;
    QStringList seen;
    bool ok = false;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.line != line
            || annotation.kind != GhostAnnotationKind::FormalPort) {
            continue;
        }
        seen.append(annotation.text);
        if (annotation.text == expected)
            ok = true;
    }
    if (!ok)
        ++g_fails;
    printf("[%s] %-34s want=\"%s\" line=%d seen=[%s]\n",
           ok ? "PASS" : "FAIL",
           what,
           expected.toLocal8Bit().constData(),
           line,
           seen.join(",").toLocal8Bit().constData());
}

static void runEffectiveValueRegression()
{
    const QString fileName = QStringLiteral("instance_presentation_fixture.sv");
    const QString source = QStringLiteral(
        "module child #(\n"
        "  parameter logic [159:0] P = 160'h123456789abcdef00112233445566778899aabb,\n"
        "  parameter logic [7:0] MASK = 8'hx3,\n"
        "  parameter string S = \"default value\"\n"
        ") ();\n"
        "  localparam logic [159:0] L = P + 160'h1;\n"
        "  typedef enum logic signed [P[7:0] - 1:0] {\n"
        "    E = P[7:0],\n"
        "    E_CONCAT = {P[7:0], 8'ha5},\n"
        "    E_ARITH = E + 3\n"
        "  } e_t;\n"
        "endmodule\n"
        "module top;\n"
        "  child #(.P(160'h1000000000000000000000000000000000000010),\n"
        "          .MASK(8'b10xz_01z1), .S(\"alpha beta\")) u0();\n"
        "  child #(.P(160'h2000000000000000000000000000000000000020),\n"
        "          .MASK(8'o7x), .S(\"gamma\")) u1();\n"
        "endmodule\n");

    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source);
    auto findRecord = [&](const QString& name,
                          SymbolTaxonomy::CollectorKind kind) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name && record.collectorKind == kind)
                return record;
        }
        return SemanticSymbolRecord{};
    };

    const SemanticSymbolRecord parameter = findRecord(
        QStringLiteral("P"), SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord mask = findRecord(
        QStringLiteral("MASK"), SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord stringParameter = findRecord(
        QStringLiteral("S"), SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord localparam = findRecord(
        QStringLiteral("L"), SymbolTaxonomy::CollectorKind::Localparam);
    const SemanticSymbolRecord enumMember = findRecord(
        QStringLiteral("E"), SymbolTaxonomy::CollectorKind::EnumValue);
    const SemanticSymbolRecord enumConcat = findRecord(
        QStringLiteral("E_CONCAT"),
        SymbolTaxonomy::CollectorKind::EnumValue);
    const SemanticSymbolRecord enumArithmetic = findRecord(
        QStringLiteral("E_ARITH"),
        SymbolTaxonomy::CollectorKind::EnumValue);
    expectBool("effective value records found",
               parameter.isValid() && mask.isValid()
                   && stringParameter.isValid() && localparam.isValid()
                   && enumMember.isValid()
                   && enumConcat.isValid()
                   && enumArithmetic.isValid(),
               true);

    const auto contextFor = [](const QString& path) {
        HierarchyInstanceContext context;
        context.workspacePath = QStringLiteral("workspace");
        context.activeTopModule = QStringLiteral("top");
        context.instancePath = path;
        return context;
    };
    SemanticIndex valueIndex;
    valueIndex.setSnapshot(sharedSnapshotFromRecords(
        records, {}, {}, {{fileName, source}}));
    EffectiveValueService values(&valueIndex);
    const auto resolveValue = [&](const SemanticSymbolRecord& symbol,
                                  const HierarchyInstanceContext& context,
                                  const QString& documentText) {
        EffectiveValueQuery query;
        query.symbol = symbol;
        query.instanceContext = context;
        query.documentText = documentText;
        return values.resolve(query);
    };
    const EffectiveValueResult p0 = resolveValue(
        parameter, contextFor(QStringLiteral("top.u0")), source);
    const EffectiveValueResult p1 = resolveValue(
        parameter, contextFor(QStringLiteral("top.u1")), source);
    const EffectiveValueResult l0 = resolveValue(
        localparam, contextFor(QStringLiteral("top.u0")), source);
    const EffectiveValueResult l1 = resolveValue(
        localparam, contextFor(QStringLiteral("top.u1")), source);
    const EffectiveValueResult e0 = resolveValue(
        enumMember, contextFor(QStringLiteral("top.u0")), source);
    const EffectiveValueResult e1 = resolveValue(
        enumMember, contextFor(QStringLiteral("top.u1")), source);
    const EffectiveValueResult concat0 = resolveValue(
        enumConcat, contextFor(QStringLiteral("top.u0")), source);
    const EffectiveValueResult concat1 = resolveValue(
        enumConcat, contextFor(QStringLiteral("top.u1")), source);
    const EffectiveValueResult arithmetic0 = resolveValue(
        enumArithmetic, contextFor(QStringLiteral("top.u0")), source);
    const EffectiveValueResult arithmetic1 = resolveValue(
        enumArithmetic, contextFor(QStringLiteral("top.u1")), source);
    expectBool("two instances keep distinct parameter values",
               p0.available() && p1.available()
                   && p0.valueText != p1.valueText,
               true);
    expectBool("wide parameter is not truncated to 64 bits",
               p0.bitWidthText == QStringLiteral("160")
                   && p0.valueText.size() > 32,
               true);
    expectBool("localparam follows instance override",
               l0.available() && l1.available()
                   && l0.valueText != l1.valueText,
               true);
    expectBool("enum member follows instance override",
               e0.available() && e1.available()
                   && e0.valueText != e1.valueText,
               true);
    expectBool("enum underlying width follows instance override",
               e0.bitWidthText == QStringLiteral("16")
                   && e1.bitWidthText == QStringLiteral("32"),
               true);
    expectBool("enum concatenation keeps authoritative instance values",
               concat0.available() && concat1.available()
                   && concat0.valueText != concat1.valueText
                   && concat0.expressionText.contains(
                       QLatin1Char('{'))
                   && concat0.bitWidthText == QStringLiteral("16")
                   && concat1.bitWidthText == QStringLiteral("32"),
               true);
    expectBool("enum arithmetic keeps authoritative signed instance values",
               arithmetic0.available() && arithmetic1.available()
                   && arithmetic0.valueText != arithmetic1.valueText
                   && arithmetic0.expressionText.contains(
                       QLatin1Char('+'))
                   && arithmetic0.signednessText
                      == QStringLiteral("signed")
                   && arithmetic1.signednessText
                      == QStringLiteral("signed"),
               true);

    const EffectiveValueResult mask0 = resolveValue(
        mask, contextFor(QStringLiteral("top.u0")), source);
    const QString maskText = mask0.valueText.toLower();
    expectBool("four-state parameter preserves X and Z",
               mask0.available()
                   && maskText.contains(QLatin1Char('x'))
                   && maskText.contains(QLatin1Char('z')),
               true);
    const EffectiveValueResult string0 = resolveValue(
        stringParameter, contextFor(QStringLiteral("top.u0")), source);
    expectBool("string parameter keeps full contents",
               string0.available()
                   && string0.valueText.contains(
                       QStringLiteral("alpha beta")),
               true);
    expectBool("non-decimal override expression is retained",
               p0.expressionText.contains(
                   QStringLiteral("160'h"), Qt::CaseInsensitive),
               true);

    const EffectiveValueResult unbound = resolveValue(
        parameter, HierarchyInstanceContext{}, source);
    expectBool("unbound effective value uses default evaluation",
               unbound.defaultEvaluation && !unbound.instanceBound
                   && unbound.instancePath.contains(
                       QStringLiteral("\u672a\u7ed1\u5b9a\u5b9e\u4f8b"))
                   && unbound.available()
                   && unbound.valueText != p0.valueText,
               true);
    const EffectiveValueResult missing = resolveValue(
        parameter, contextFor(QStringLiteral("top.missing")), source);
    expectBool("missing exact instance never selects another instance",
               !missing.available()
                   && missing.failureReason.contains(
                       QStringLiteral("exact bound instance")),
               true);

    SemanticIndex hotEditIndex;
    hotEditIndex.setSnapshot(sharedSnapshotFromRecords(
        records, {}, {}, {{fileName, source}}));
    SemanticSymbolRecord singleFileParameter = parameter;
    singleFileParameter.presentation.instanceInfoByPath.clear();
    hotEditIndex.updateSymbolRecordsForFile(
        fileName, {singleFileParameter}, source);
    EffectiveValueService hotEditValues(&hotEditIndex);
    const auto resolveHotEdit = [&](const SemanticSymbolRecord& symbol,
                                    const QString& documentText) {
        EffectiveValueQuery query;
        query.symbol = symbol;
        query.instanceContext = contextFor(QStringLiteral("top.u0"));
        query.documentText = documentText;
        return hotEditValues.resolve(query);
    };
    const QList<SemanticSymbolRecord> unchangedRecords =
        hotEditIndex.getSymbolRecordsByName(QStringLiteral("P"));
    const EffectiveValueResult unchangedHotEdit =
        unchangedRecords.isEmpty()
            ? EffectiveValueResult()
            : resolveHotEdit(unchangedRecords.first(), source);
    expectBool("unchanged hot edit preserves workspace elaboration",
               unchangedHotEdit.available()
                   && unchangedHotEdit.valueText == p0.valueText,
               true);

    const QString changedSource = source + QStringLiteral("\n// changed\n");
    hotEditIndex.updateSymbolRecordsForFile(
        fileName, {singleFileParameter}, changedSource);
    const QList<SemanticSymbolRecord> changedRecords =
        hotEditIndex.getSymbolRecordsByName(QStringLiteral("P"));
    const EffectiveValueResult changedHotEdit =
        changedRecords.isEmpty()
            ? EffectiveValueResult()
            : resolveHotEdit(changedRecords.first(), changedSource);
    expectBool("changed hot edit reports stale workspace elaboration",
               !changedHotEdit.available()
                   && changedHotEdit.failureReason.contains(
                       QStringLiteral("source document changed")),
               true);
}

static void runFormalPortDeclarationRegression()
{
    const QString fileName = QStringLiteral("formal_port_fixture.sv");
    const QString source = QStringLiteral(
        "`define PORT_ATTR (* mark_debug = \"true\" *)\n"
        "interface axi_if;\n"
        "  modport master();\n"
        "endinterface\n"
        "typedef logic [7:0] test_t;\n"
        "module ports #(parameter int P_1 = 2) (\n"
        "  `PORT_ATTR input var logic signed [P_1 * 2 - 1:0] marked [0:P_1 - 1],\n"
        "`ifdef UNUSED_PORT_BRANCH\n"
        "  input logic unused_branch,\n"
        "`else\n"
        "  (* keep = \"true\" *) /* semantic comment */ input test_t conditioned [P_1 - 1:0],\n"
        "`endif\n"
        "  input test_t shared_a [P_1 - 1:0],\n"
        "               shared_b [P_1 - 1:0],\n"
        "  output\n"
        "    logic [P_1 - 1:0]\n"
        "    out_p,\n"
        "  inout wire io_p,\n"
        "  ref test_t ref_p [P_1 - 1:0],\n"
        "  axi_if.master bus_mp [P_1 - 1:0],\n"
        "  axi_if bus_plain\n"
        ");\n"
        "endmodule\n"
        "module legacy_ports #(parameter int W = 2) (legacy);\n"
        "  (* mark_debug = \"true\" *) input wire signed [W - 1:0] legacy [0:1];\n"
        "endmodule\n"
        "module explicit_ports (\n"
        "  (* mark_debug = \"true\" *) input .external(internal)\n"
        ");\n"
        "  logic internal;\n"
        "endmodule\n"
        "module top;\n"
        "  localparam int P_1 = 2;\n"
        "  logic signed [P_1 * 2 - 1:0] marked [0:P_1 - 1];\n"
        "  test_t conditioned [P_1 - 1:0];\n"
        "  test_t a [P_1 - 1:0], b [P_1 - 1:0];\n"
        "  logic [P_1 - 1:0] out_p;\n"
        "  wire io_p;\n"
        "  test_t ref_p [P_1 - 1:0];\n"
        "  axi_if bus_mp [P_1 - 1:0] ();\n"
        "  axi_if bus_plain ();\n"
        "  wire signed [P_1 - 1:0] legacy [0:1];\n"
        "  logic explicit_signal;\n"
        "  ports #(.P_1(P_1)) u_ports (\n"
        "    .marked(marked),\n"
        "    .conditioned(conditioned),\n"
        "    .shared_a(a),\n"
        "    .shared_b(b),\n"
        "    .out_p(out_p),\n"
        "    .io_p(io_p),\n"
        "    .ref_p(ref_p),\n"
        "    .bus_mp(bus_mp),\n"
        "    .bus_plain(bus_plain)\n"
        "  );\n"
        "  legacy_ports #(.W(P_1)) u_legacy (.legacy(legacy));\n"
        "  explicit_ports u_explicit (.external(explicit_signal));\n"
        "endmodule\n");

    const auto lineOf = [&](const QString& needle) {
        const int position = source.indexOf(needle);
        return position < 0
            ? 0
            : source.left(position).count(QLatin1Char('\n')) + 1;
    };

    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source);
    auto findPort = [&](const QString& name) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name
                && (record.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Port
                    || record.collectorKind
                        == SymbolTaxonomy::CollectorKind::PortInterface
                    || record.collectorKind
                        == SymbolTaxonomy::CollectorKind::PortInterfaceModport)) {
                return record;
            }
        }
        return SemanticSymbolRecord{};
    };
    HierarchyInstanceContext portContext;
    portContext.workspacePath = QStringLiteral("workspace");
    portContext.activeTopModule = QStringLiteral("top");
    portContext.instancePath = QStringLiteral("top.u_ports");
    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        records, {}, {}, {{fileName, source}}));
    EffectiveValueService values(&index);
    const auto resolvePort = [&](const QString& name) {
        EffectiveValueQuery query;
        query.symbol = findPort(name);
        query.instanceContext = portContext;
        query.documentText = source;
        return values.resolve(query);
    };
    const EffectiveValueResult typedArrayPort =
        resolvePort(QStringLiteral("shared_a"));
    expectBool("port effective value carries declaration and dimensions",
               findPort(QStringLiteral("shared_a"))
                           .presentation.declarationText
                       == QStringLiteral(
                           "input test_t shared_a [P_1 - 1:0]")
                   && findPort(QStringLiteral("shared_b"))
                              .presentation.declarationText
                       == QStringLiteral(
                           "input test_t shared_b [P_1 - 1:0]")
                   && typedArrayPort.available()
                   && typedArrayPort.declarationText
                          == QStringLiteral(
                              "input test_t shared_a [P_1 - 1:0]")
                   && typedArrayPort.packedDimensionsText
                          == QStringLiteral("[7:0]")
                   && typedArrayPort.unpackedDimensionsText
                          == QStringLiteral("[1:0]")
                   && !typedArrayPort.resolvedTypeText.isEmpty()
                   && typedArrayPort.bitWidthText
                          == QStringLiteral("16")
                   && typedArrayPort.signednessText
                          == QStringLiteral("unsigned"),
               true);
    const EffectiveValueResult markedPort =
        resolvePort(QStringLiteral("marked"));
    expectBool("attributed ANSI presentation excludes attributes",
               findPort(QStringLiteral("marked"))
                           .presentation.declarationText
                       == QStringLiteral(
                           "input var logic signed [P_1 * 2 - 1:0] marked [0:P_1 - 1]")
                   && markedPort.available()
                   && markedPort.declarationText
                          == QStringLiteral(
                              "input var logic signed [P_1 * 2 - 1:0] marked [0:P_1 - 1]"),
               true);
    expectBool("attributed non-ANSI presentation excludes attributes",
               findPort(QStringLiteral("legacy"))
                       .presentation.declarationText
                   == QStringLiteral(
                       "input wire signed [W - 1:0] legacy [0:1]"),
               true);
    expectBool("attributed explicit ANSI presentation excludes attributes",
               findPort(QStringLiteral("external"))
                       .presentation.declarationText
                   == QStringLiteral("input .external(internal)"),
               true);
    const EffectiveValueResult interfacePort =
        resolvePort(QStringLiteral("bus_mp"));
    const EffectiveValueResult plainInterfacePort =
        resolvePort(QStringLiteral("bus_plain"));
    expectBool("interface effective value carries modport and array",
               interfacePort.available()
                   && interfacePort.interfaceName
                          == QStringLiteral("axi_if")
                   && interfacePort.modportName
                          == QStringLiteral("master")
                   && interfacePort.declarationText.contains(
                       QStringLiteral("[P_1 - 1:0]"))
                   && interfacePort.unpackedDimensionsText
                          == QStringLiteral("[1:0]")
                   && plainInterfacePort.available()
                   && plainInterfacePort.modportName.isEmpty(),
               true);
    GhostAnnotationService service(&index);
    const GhostAnnotationReport report = service.annotationsForDocument(
        GhostAnnotationQuery{fileName, source});

    expectGhostEquals("formal attributed variable port",
                      report,
                      lineOf(QStringLiteral(".marked(marked)")),
                      QStringLiteral(
                          "input var logic signed [P_1 * 2 - 1:0] marked [0:P_1 - 1]"));
    expectGhostEquals("formal preprocessor wrapped port",
                      report,
                      lineOf(QStringLiteral(".conditioned(conditioned)")),
                      QStringLiteral(
                          "input test_t conditioned [P_1 - 1:0]"));
    expectGhostNotContains("formal port excludes attribute",
                           report,
                           GhostAnnotationKind::FormalPort,
                           lineOf(QStringLiteral(".marked(marked)")),
                           QStringLiteral("mark_debug"));
    expectGhostNotContains("formal port excludes conditional trivia",
                           report,
                           GhostAnnotationKind::FormalPort,
                           lineOf(QStringLiteral(".conditioned(conditioned)")),
                           QStringLiteral("`"));
    expectGhostNotContains("formal port excludes comments",
                           report,
                           GhostAnnotationKind::FormalPort,
                           lineOf(QStringLiteral(".conditioned(conditioned)")),
                           QStringLiteral("semantic comment"));
    expectGhostEquals("formal input typedef array",
                      report,
                      lineOf(QStringLiteral(".shared_a(a)")),
                      QStringLiteral(
                          "input test_t shared_a [P_1 - 1:0]"));
    expectGhostEquals("formal shared declarator",
                      report,
                      lineOf(QStringLiteral(".shared_b(b)")),
                      QStringLiteral(
                          "input test_t shared_b [P_1 - 1:0]"));
    expectGhostEquals("formal multiline output",
                      report,
                      lineOf(QStringLiteral(".out_p(out_p)")),
                      QStringLiteral(
                          "output logic [P_1 - 1:0] out_p"));
    expectGhostEquals("formal inout",
                      report,
                      lineOf(QStringLiteral(".io_p(io_p)")),
                      QStringLiteral("inout wire io_p"));
    expectGhostEquals("formal ref",
                      report,
                      lineOf(QStringLiteral(".ref_p(ref_p)")),
                      QStringLiteral(
                          "ref test_t ref_p [P_1 - 1:0]"));
    expectGhostEquals("formal interface modport array",
                      report,
                      lineOf(QStringLiteral(".bus_mp(bus_mp)")),
                      QStringLiteral(
                          "axi_if.master bus_mp [P_1 - 1:0]"));
    expectGhostEquals("formal interface",
                      report,
                      lineOf(QStringLiteral(".bus_plain(bus_plain)")),
                      QStringLiteral("axi_if bus_plain"));
    expectGhostEquals("formal attributed non-ANSI port",
                      report,
                      lineOf(QStringLiteral(".legacy(legacy)")),
                      QStringLiteral(
                          "input wire signed [W - 1:0] legacy [0:1]"));
    expectGhostEquals("formal attributed explicit ANSI port",
                      report,
                      lineOf(QStringLiteral(".external(explicit_signal)")),
                      QStringLiteral("input .external(internal)"));
}

static void expectGhostLineNotContains(const char* what,
                                       const GhostAnnotationReport& report,
                                       int line,
                                       const QString& contains)
{
    ++g_checks;
    bool ok = true;
    QStringList seen;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.line != line)
            continue;
        seen << annotation.text;
        if (annotation.text.contains(contains))
            ok = false;
    }
    if (!ok)
        ++g_fails;
    printf("[%s] %-34s reject=\"%s\" line=%d seen=[%s]\n",
           ok ? "PASS" : "FAIL",
           what,
           contains.toLocal8Bit().constData(),
           line,
           seen.join(",").toLocal8Bit().constData());
}

static QStringList recordNames(const QList<SemanticSymbolRecord>& records) {
    QStringList names;
    for (const auto& record : records)
        names << record.name;
    return names;
}

static const WavePreviewLane* waveLaneNamed(const WavePreviewReport& report,
                                            const QString& name)
{
    for (const WavePreviewLane& lane : report.lanes) {
        if (lane.signalName == name)
            return &lane;
    }
    return nullptr;
}

static const WavePreviewSignalContext* waveContextNamed(
    const WavePreviewReport& report,
    const QString& name)
{
    for (const WavePreviewSignalContext& context : report.signalContexts) {
        if (context.signalName == name)
            return &context;
    }
    return nullptr;
}

static const WavePreviewAssignment* firstWaveAssignment(
    const WavePreviewReport& report,
    const QString& name)
{
    const WavePreviewLane* lane = waveLaneNamed(report, name);
    if (!lane || lane->assignments.isEmpty())
        return nullptr;
    return &lane->assignments.first();
}

static const WavePreviewTraceSignal* waveTraceSignalNamed(
    const WavePreviewReport& report,
    const QString& name)
{
    for (const WavePreviewTraceSignal& signal : report.trace.traceSignals) {
        if (signal.signalName == name)
            return &signal;
    }
    return nullptr;
}

static QStringList waveEdgeLabels(const QList<WavePreviewEdgeSignal>& edges)
{
    QStringList labels;
    for (const WavePreviewEdgeSignal& signal : edges)
        labels.append(signal.label());
    return labels;
}

static SemanticSymbolRecord makeSemanticFixtureRecord(
    const QString& name,
    SymbolTaxonomy::DeclarationKind declarationKind,
    SymbolTaxonomy::CollectorKind collectorKind,
    const QString& ownerName,
    const QString& rawTypeText,
    int localHandle,
    const QString& fileName)
{
    SymbolTaxonomy::SymbolOwnerScope ownerScope =
        semanticFixtureOwnerScopeForDeclaration(declarationKind);
    bool interfaceLikeOwner = false;
    if (!ownerName.isEmpty()) {
        if (declarationKind == SymbolTaxonomy::DeclarationKind::StructMember) {
            ownerScope = SymbolTaxonomy::SymbolOwnerScope::Struct;
        } else if (declarationKind
                   == SymbolTaxonomy::DeclarationKind::Modport) {
            ownerScope = SymbolTaxonomy::SymbolOwnerScope::Interface;
            interfaceLikeOwner = true;
        } else {
            ownerScope = SymbolTaxonomy::SymbolOwnerScope::Module;
        }
    }

    return SemanticFixtureRecordBuilder(name, declarationKind)
        .withFile(fileName)
        .withLocalHandle(localHandle)
        .withLine(localHandle)
        .withCollectorKind(collectorKind)
        .withOwner(ownerScope, ownerName, {}, interfaceLikeOwner)
        .withType(rawTypeText)
        .record();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) {
        fprintf(stderr, "cannot open %s\n", path.toLocal8Bit().constData());
        return 2;
    }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        path,
        mgr.extractSymbolRecords(path, content),
        content);

    runEffectiveValueRegression();
    runFormalPortDeclarationRegression();

    ++g_checks;
    const bool serviceItemScoreOk =
        CompletionService::getInstance()->completionItemScore(QStringLiteral("save"),
                                                              QString())
        == 100;
    if (!serviceItemScoreOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           serviceItemScoreOk ? "PASS" : "FAIL",
           "CompletionService item score");
    GlobalControlService globalControlService;
    const QList<GlobalControlItem> globalRootItems =
        globalControlService.query(QString());
    bool globalRootHasWorkspaceDomain = false;
    bool globalRootHasFoldDomain = false;
    bool globalRootHasCommands = false;
    for (const GlobalControlItem& item : globalRootItems) {
        if (item.kind == GlobalControlItemKind::Domain
            && item.id == QStringLiteral("ow"))
            globalRootHasWorkspaceDomain = true;
        if (item.kind == GlobalControlItemKind::Domain
            && item.id == QStringLiteral("fd"))
            globalRootHasFoldDomain = true;
        if (item.kind == GlobalControlItemKind::Command)
            globalRootHasCommands = true;
    }
    expectBool("GlobalControl root shows only domains",
               globalRootHasWorkspaceDomain
                   && globalRootHasFoldDomain
                   && !globalRootHasCommands,
               true);
    const QList<GlobalControlItem> globalWorkspaceItems =
        globalControlService.query(QStringLiteral("ow"));
    bool globalWorkspaceShowsOpenOne = false;
    bool globalWorkspaceShowsOpenTwo = false;
    bool globalWorkspaceShowsRecent = false;
    bool globalWorkspaceShowsSession = false;
    bool globalWorkspaceShowsDeprecatedOw = false;
    for (const GlobalControlItem& item : globalWorkspaceItems) {
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow 1"))
            globalWorkspaceShowsOpenOne = true;
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow 2"))
            globalWorkspaceShowsOpenTwo = true;
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow r"))
            globalWorkspaceShowsRecent = true;
        if (item.kind == GlobalControlItemKind::Domain
            && item.id == QStringLiteral("ow s"))
            globalWorkspaceShowsSession = true;
        if (item.id == QStringLiteral("ow")
            && item.kind == GlobalControlItemKind::Command)
            globalWorkspaceShowsDeprecatedOw = true;
    }
    expectBool("GlobalControl ow domain shows ow r",
               globalWorkspaceShowsOpenOne
                   && globalWorkspaceShowsOpenTwo
                   && globalWorkspaceShowsRecent
                   && globalWorkspaceShowsSession
                   && !globalWorkspaceShowsDeprecatedOw,
               true);
    const QList<GlobalControlItem> globalRecentItems =
        globalControlService.query(QStringLiteral("ow r"));
    bool globalRecentCommandFound = false;
    bool globalRecentCountHintFound = false;
    for (const GlobalControlItem& item : globalRecentItems) {
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow r"))
            globalRecentCommandFound = true;
        if (item.kind == GlobalControlItemKind::Domain
            && item.title == QStringLiteral("ow <num>"))
            globalRecentCountHintFound = true;
    }
    expectBool("GlobalControl ow r query finds recent command",
               globalRecentCommandFound && !globalRecentCountHintFound,
               true);
    const QList<GlobalControlItem> globalSessionItems =
        globalControlService.query(QStringLiteral("ow s"));
    bool globalSessionSaveFound = false;
    bool globalSessionRestoreFound = false;
    bool globalSessionCleanFound = false;
    for (const GlobalControlItem& item : globalSessionItems) {
        globalSessionSaveFound =
            globalSessionSaveFound
            || (item.kind == GlobalControlItemKind::Command
                && item.id == QStringLiteral("ow s save"));
        globalSessionRestoreFound =
            globalSessionRestoreFound
            || (item.kind == GlobalControlItemKind::Command
                && item.id == QStringLiteral("ow s restore"));
        globalSessionCleanFound =
            globalSessionCleanFound
            || (item.kind == GlobalControlItemKind::Command
                && item.id == QStringLiteral("ow s clean"));
    }
    expectBool("GlobalControl ow s query shows session commands",
               globalSessionSaveFound
                   && globalSessionRestoreFound
                   && globalSessionCleanFound,
               true);
    expectBool("GlobalControl ow s w abbreviates save",
               !globalControlService.query(QStringLiteral("ow s w"))
                    .isEmpty()
                   && globalControlService.query(QStringLiteral("ow s w"))
                          .first()
                          .id == QStringLiteral("ow s save"),
               true);
    expectBool("GlobalControl ow s r abbreviates restore",
               !globalControlService.query(QStringLiteral("ow s r"))
                    .isEmpty()
                   && globalControlService.query(QStringLiteral("ow s r"))
                          .first()
                          .id == QStringLiteral("ow s restore"),
               true);
    expectBool("GlobalControl ow s c abbreviates clean",
               !globalControlService.query(QStringLiteral("ow s c"))
                    .isEmpty()
                   && globalControlService.query(QStringLiteral("ow s c"))
                          .first()
                          .id == QStringLiteral("ow s clean"),
               true);
    const QList<SemanticSymbolRecord> modelScoringRecords{
        SemanticFixtureRecordBuilder(
            QStringLiteral("logic"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9001)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("always_ff"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9002)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .record(),
    };
    CompletionModel modelScoring;
    modelScoring.updateSymbolRecordCompletions(modelScoringRecords,
                                               QStringLiteral("af"),
                                               CompletionCommandKind::Logic);
    ++g_checks;
    const QModelIndex modelScoringSymbolIndex =
        modelScoring.firstSelectableIndex();
    const QString firstScoredModelSymbol =
        modelScoring.getItem(modelScoringSymbolIndex).text;
    const bool modelScoringOk =
        firstScoredModelSymbol == QStringLiteral("always_ff");
    if (!modelScoringOk)
        ++g_fails;
    printf("[%s] %-34s got=\"%s\"\n",
           modelScoringOk ? "PASS" : "FAIL",
           "CompletionModel service scoring",
           firstScoredModelSymbol.toLocal8Bit().constData());
    expectEq("CompletionService symbol desc",
             CompletionService::getInstance()
                 ->commandSymbolPresentation(CompletionCommandKind::Logic)
                 .typeDescription,
             QStringLiteral("logic variables"));
    expectEq("CompletionModel symbol desc",
             modelScoring.getItem(modelScoringSymbolIndex).description,
             QStringLiteral("logic"));
    expectEq("CompletionModel symbol display",
             modelScoring.getItem(modelScoringSymbolIndex).displayText,
             QStringLiteral("always_ff (logic)"));
    expectBool("CompletionModel symbol stable key",
               modelScoring.getItem(modelScoringSymbolIndex).symbolStableKey
                   == modelScoringRecords.at(1).stableKey,
               true);
    expectBool("CompletionModel symbol record",
               modelScoring.getItem(modelScoringSymbolIndex).symbolRecord.stableKey
                   == modelScoring.getItem(modelScoringSymbolIndex).symbolStableKey
                   && modelScoring.getItem(modelScoringSymbolIndex)
                          .symbolRecord.localHandle == modelScoringRecords.at(1).localHandle,
               true);
    expectBool("CompletionModel symbol semantic metadata",
               modelScoring.getItem(modelScoringSymbolIndex).typeDisplayName
                   == QStringLiteral("logic")
                   && modelScoring.getItem(modelScoringSymbolIndex).ownerScopeName
                       == QStringLiteral("top")
                   && modelScoring.getItem(modelScoringSymbolIndex)
                          .sourceRoleDisplayName == QStringLiteral("design source")
                   && modelScoring.getItem(modelScoringSymbolIndex).declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal
                   && modelScoring.getItem(modelScoringSymbolIndex).ownerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Module
                   && modelScoring.getItem(modelScoringSymbolIndex).sourceRole
                       == SymbolTaxonomy::SourceRole::DesignSource
                   && modelScoring.getItem(modelScoringSymbolIndex)
                          .symbolRecord.collectorKind
                       == SymbolTaxonomy::CollectorKind::Logic,
               true);
    expectBool("CompletionModel header selectable",
               modelScoring.getItem(modelScoring.index(0, 0)).selectable,
               false);
    expectBool("CompletionModel real symbols precede default fallback",
               modelScoringSymbolIndex.row() == 1
                   && !modelScoring.getItem(modelScoringSymbolIndex)
                           .text
                           .startsWith(QStringLiteral("[DEFAULT]")),
               true);
    expectEq("CompletionModel display role",
             modelScoring.data(modelScoringSymbolIndex, Qt::DisplayRole).toString(),
             QStringLiteral("always_ff (logic)"));
    SemanticSymbolRecord metadataModelRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("metadata_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(QStringLiteral("snapshot_only.sv"))
            .withLocalHandle(9005)
            .withSourceRole(SymbolTaxonomy::SourceRole::Unknown)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    metadataModelRecord.analysisBand.label = QStringLiteral("current");
    metadataModelRecord.analysisBand.displayName = QStringLiteral("current");
    metadataModelRecord.analysisBand.priority = true;
    metadataModelRecord.analysisBand.publicationCheckpoint = 1;
    SemanticSymbolRecord backgroundMetadataRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("metadata_bg"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(QStringLiteral("background_only.sv"))
            .withLocalHandle(9006)
            .withSourceRole(SymbolTaxonomy::SourceRole::DesignSource)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    backgroundMetadataRecord.analysisBand.label =
        QStringLiteral("background");
    backgroundMetadataRecord.analysisBand.displayName =
        QStringLiteral("background");
    CompletionModel commandSymbolBandSummaryModel;
    commandSymbolBandSummaryModel.updateSymbolRecordCompletions(
        {metadataModelRecord, backgroundMetadataRecord},
        QString(),
        CompletionCommandKind::Module);
    expectBool("CompletionModel renders command symbol band summary header",
               commandSymbolBandSummaryModel.rowCount() == 4
                   && !commandSymbolBandSummaryModel.getItem(
                          commandSymbolBandSummaryModel.index(2, 0))
                          .selectable
                   && commandSymbolBandSummaryModel.data(
                          commandSymbolBandSummaryModel.index(2, 0),
                          Qt::DisplayRole).toString()
                          == QStringLiteral(
                              ":: COMMAND SYMBOL BANDS - bands current 1 item, background 1 item ::")
                   && commandSymbolBandSummaryModel.firstSelectableIndex()
                          .row() == 1,
               true);
    expectEq("CompletionService interface desc",
             CompletionService::getInstance()
                 ->commandSymbolPresentation(CompletionCommandKind::Interface)
                 .typeDescription,
             QStringLiteral("interfaces"));

    const QString moduleContextFile =
        QStringLiteral("semantic_module_context.sv");
    const QString moduleContextText =
        QStringLiteral("module first;\n"
                       "  logic a;\n"
                       "endmodule\n"
                       "module second;\n"
                       "  logic b;\n"
                       "endmodule\n");
    const int firstModuleNamePosition =
        moduleContextText.indexOf(QStringLiteral("first"));
    const int secondModuleNamePosition =
        moduleContextText.indexOf(QStringLiteral("second"));
    const QList<SemanticSymbolRecord> moduleContextRecords{
        SemanticFixtureRecordBuilder(
            QStringLiteral("first"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(moduleContextFile)
            .withRange(1, 8, 3, 10)
            .withTextSpan(firstModuleNamePosition,
                          QStringLiteral("first").size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("second"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(moduleContextFile)
            .withRange(4, 8, 6, 10)
            .withTextSpan(secondModuleNamePosition,
                          QStringLiteral("second").size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record(),
    };
    SemanticIndex moduleContextIndex;
    moduleContextIndex.setSnapshot(sharedSnapshotFromRecords(
        moduleContextRecords,
        {},
        {},
        {{moduleContextFile, moduleContextText}}));
    expectEq("SemanticIndex module on declaration line",
             moduleContextIndex.currentModuleAt(
                 moduleContextFile,
                 firstModuleNamePosition),
             QStringLiteral("first"));
    expectEq("SemanticIndex module range selects second",
             moduleContextIndex.currentModuleAt(
                 moduleContextFile,
                 moduleContextText.indexOf(QStringLiteral("logic b"))),
             QStringLiteral("second"));

    const QString decorationFile = QStringLiteral("decor_fixture.sv");
    const QString decorationText =
        QStringLiteral("module rtl_top(\n"
                       "    input clk_main, // 80m\n"
                       "    lite_if.s S_GENR_LITE_IF // tail\n"
                       ");\n");
    const QList<SemanticSymbolRecord> decorationRecords{
        SemanticFixtureRecordBuilder(
            QStringLiteral("rtl_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(decorationFile)
            .withLine(1, 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("clk_main"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(decorationFile)
            .withLine(2, 5)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("S_GENR_LITE_IF"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(decorationFile)
            .withLine(3, 5)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInterfaceModport)
            .record(),
    };
    SemanticIndex decorationIndex;
    decorationIndex.setSnapshot(sharedSnapshotFromRecords(decorationRecords));
    SemanticDecorationService decorationService(&decorationIndex);
    const SemanticDecorationReport decorationReport =
        decorationService.decorationsForDocument(
            SemanticDecorationQuery{decorationFile, decorationText});
    bool moduleNameDecorated = false;
    bool portNameDecorated = false;
    bool interfacePortNameDecorated = false;
    bool keywordDecorated = false;
    bool commentDecorated = false;
    auto decorationStartsInLineComment =
        [&decorationText](const SemanticDecoration& decoration) {
            const int lineStart =
                decorationText.lastIndexOf(QLatin1Char('\n'),
                                           decoration.startPosition) + 1;
            int lineEnd = decorationText.indexOf(QLatin1Char('\n'),
                                                 lineStart);
            if (lineEnd < 0)
                lineEnd = decorationText.size();
            const int lineComment =
                decorationText.indexOf(QStringLiteral("//"), lineStart);
            return lineComment >= lineStart
                && lineComment < lineEnd
                && decoration.startPosition >= lineComment;
        };
    for (const SemanticDecoration& decoration : decorationReport.decorations) {
        const QString span =
            decorationText.mid(decoration.startPosition, decoration.length);
        moduleNameDecorated |= span == QStringLiteral("rtl_top");
        portNameDecorated |= span == QStringLiteral("clk_main");
        interfacePortNameDecorated |= span == QStringLiteral("S_GENR_LITE_IF");
        keywordDecorated |= span == QStringLiteral("module")
            || span == QStringLiteral("input")
            || span == QStringLiteral("lite_if.s");
        commentDecorated |= decorationStartsInLineComment(decoration);
    }
    expectBool("SemanticDecoration names not keywords",
               moduleNameDecorated
                   && portNameDecorated
                   && interfacePortNameDecorated
                   && !keywordDecorated
                   && !commentDecorated,
               true);

    const QString ghostFile = QStringLiteral("ghost_fixture.sv");
    const QString ghostText =
        QStringLiteral("module child #(parameter int WIDTH = 8) (input logic [7:0] data, output logic ready);\n"
                       "endmodule\n"
                       "module other_child(output logic [3:0] data);\n"
                       "endmodule\n"
                       "module top;\n"
                       "  localparam DEPTH = 8;\n"
                       "  localparam PW = 32;\n"
                       "  logic [PW - 1:0] wide;\n"
                       "  logic [27:18] window;\n"
                       "  logic [7:0] data;\n"
                       "  logic [7:0] mem [16];\n"
                       "  child #(.WIDTH(16)) u_child (\n"
                       "    .data(data),\n"
                       "    .ready()\n"
                       "  );\n"
                       "  typedef enum logic [2:0] {IDLE, RUN = 3, DONE} state_t;\n"
                       "  assign slice = data[4 +: 3];\n"
                       "  assign literal = 16'hFF00;\n"
                       "  assign cat = {4{8'hAA}};\n"
                       "  for (genvar i = 0; i < 8; i++) begin : g\n"
                       "  end\n"
                       "  function automatic [31:0] function_add1(input [31:0] x);\n"
                       "    function_add1 = x + 1;\n"
                       "  endfunction\n"
                       "  localparam FROM_PW = PW;\n"
                       "  localparam SUM = 32'ha452_0000 + 32'454654;\n"
                       "  localparam STR = \"test\";\n"
                       "  localparam CLOG = $clog2(PW);\n"
                       "  localparam FADD = function_add1(PW);\n"
                       "endmodule\n");
    SlangManager ghostSlang;
    QList<EffectiveValueFact> ghostFacts;
    QList<SemanticSymbolRecord> ghostRecords =
        ghostSlang.extractSymbolRecords(ghostFile,
                                        ghostText,
                                        {},
                                        {},
                                        &ghostFacts);
    SemanticIndex ghostIndex;
    EffectiveValueService ghostValues(&ghostIndex);
    const std::uint64_t ghostComputationRevision =
        ghostValues.beginComputation({ghostFile});
    for (SemanticSymbolRecord& record : ghostRecords)
        record.presentation.computationRevision = ghostComputationRevision;
    ghostIndex.setSnapshot(
        sharedSnapshotFromRecords(
            ghostRecords,
            {},
            {},
            {{ghostFile, ghostText}}));
    ghostValues.publishDocumentFacts(ghostFile,
                                     ghostText,
                                     std::move(ghostFacts),
                                     ghostComputationRevision);
    GhostAnnotationService ghostService(&ghostIndex, &ghostValues);
    GhostAnnotationQuery ghostQuery;
    ghostQuery.fileName = ghostFile;
    ghostQuery.documentText = ghostText;
    const GhostAnnotationReport ghostReport =
        ghostService.annotationsForDocument(ghostQuery);
    expectGhostContains("Ghost port formal",
                        ghostReport,
                        GhostAnnotationKind::FormalPort,
                        13,
                        QStringLiteral("input logic [7:0] data"));
    expectGhostNotContains("Ghost literal parameter value hidden",
                           ghostReport,
                           GhostAnnotationKind::ParameterValue,
                           6,
                           QStringLiteral("="));
    expectGhostContains("Ghost parameter identifier value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        25,
                        QStringLiteral("= 32"));
    expectGhostContains("Ghost parameter expression value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        26,
                        QStringLiteral("= 32'd2757292030"));
    expectGhostNotContains("Ghost string parameter value hidden",
                           ghostReport,
                           GhostAnnotationKind::ParameterValue,
                           27,
                           QStringLiteral("="));
    expectGhostContains("Ghost parameter clog2 value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        28,
                        QStringLiteral("= 5"));
    expectGhostContains("Ghost parameter function value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        29,
                        QStringLiteral("= 32'd33"));
    expectGhostNotContains("Ghost literal parameter override hidden",
                           ghostReport,
                           GhostAnnotationKind::ParameterOverride,
                           12,
                           QStringLiteral("="));
    expectGhostContains("Ghost signal width",
                        ghostReport,
                        GhostAnnotationKind::SignalWidth,
                        8,
                        QStringLiteral("32 bits"));
    expectGhostContains("Ghost nonzero range width",
                        ghostReport,
                        GhostAnnotationKind::SignalWidth,
                        9,
                        QStringLiteral("[27:18] 10 bits"));
    expectGhostNotContains("Ghost obvious width hidden",
                           ghostReport,
                           GhostAnnotationKind::SignalWidth,
                           10,
                           QStringLiteral("8 bits"));
    expectGhostContains("Ghost array summary",
                        ghostReport,
                        GhostAnnotationKind::ArraySummary,
                        11,
                        QStringLiteral("16 entries"));
    expectGhostContains("Ghost enum idle",
                        ghostReport,
                        GhostAnnotationKind::EnumValue,
                        16,
                        QStringLiteral("= 0"));
    expectGhostContains("Ghost enum run",
                        ghostReport,
                        GhostAnnotationKind::EnumValue,
                        16,
                        QStringLiteral("= 3"));
    expectGhostContains("Ghost enum done",
                        ghostReport,
                        GhostAnnotationKind::EnumValue,
                        16,
                        QStringLiteral("= 4"));
    expectGhostContains("Ghost part select",
                        ghostReport,
                        GhostAnnotationKind::PartSelect,
                        17,
                        QStringLiteral("[4 +: 3] 3 bits"));
    expectGhostLineNotContains("Ghost literal overlay removed",
                               ghostReport,
                               18,
                               QStringLiteral("65280"));
    expectGhostContains("Ghost concat width",
                        ghostReport,
                        GhostAnnotationKind::ConcatenationWidth,
                        19,
                        QStringLiteral("32 bits"));
    expectGhostContains("Ghost generate loop",
                        ghostReport,
                        GhostAnnotationKind::GenerateLoop,
                        20,
                        QStringLiteral("instances=8"));

    const QString numericHoverText =
        QStringLiteral("assign a = 16'hFF00;\n"
                       "assign b = \"123\"; // 456\n"
                       "assign c = 'd4545;\n"
                       "assign d = 'hadda;\n"
                       "assign e = -8'sd1;\n"
                       "assign f = 4'h1f;\n"
                       "assign g = 8'b10xz_01z1;\n"
                       "localparam logic [7:0] H = {4'ha, 4'h5};\n"
                       "`include \"test.sv\"\n");
    const int literalHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("16'hFF00")))
        + 3;
    const int stringHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("123")));
    const int commentHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("456")));
    const int unsizedDecimalHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("'d4545"))) + 2;
    const int unsizedHexHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("'hadda"))) + 2;
    const int negativeHoverPosition =
        static_cast<int>(
            numericHoverText.indexOf(QStringLiteral("8'sd1"))) + 3;
    const int truncatedHoverPosition =
        static_cast<int>(
            numericHoverText.indexOf(QStringLiteral("4'h1f"))) + 3;
    const int unknownHoverPosition =
        static_cast<int>(
            numericHoverText.indexOf(QStringLiteral("8'b10xz_01z1"))) + 4;
    const int concatHoverPosition =
        static_cast<int>(
            numericHoverText.indexOf(QStringLiteral("4'ha"))) + 2;
    const int includeStringHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("test.sv")));
    auto numericLiteralAt = [&ghostService](const QString& documentText,
                                            int cursorPosition) {
        TSDocument syntaxDocument;
        syntaxDocument.setText(documentText);
        return ghostService.numericLiteralAt(
            GhostNumericLiteralQuery{&syntaxDocument, cursorPosition});
    };
    const GhostNumericLiteralReport literalHover =
        numericLiteralAt(numericHoverText, literalHoverPosition);
    expectBool("Ghost literal hover available",
               literalHover.available,
               true);
    expectBool("Ghost literal hover shows authoritative alternate bases",
               literalHover.valueText == QStringLiteral("16'd65280")
                   && literalHover.displayText.contains(
                       QStringLiteral(
                           "binary: 1111_1111_0000_0000"))
                   && literalHover.displayText.contains(
                       QStringLiteral("octal: 17_7400"))
                   && literalHover.displayText.contains(
                       QStringLiteral("decimal: 6_5280"))
                   && literalHover.displayText.contains(
                       QStringLiteral("hex: ff00")),
               true);
    const GhostNumericLiteralReport stringHover =
        numericLiteralAt(numericHoverText, stringHoverPosition);
    expectBool("Ghost numeric hover packs ASCII strings",
               stringHover.available
                   && stringHover.valueText
                          == QStringLiteral("characters: \"123\"")
                   && stringHover.radixRepresentations.contains(
                       QStringLiteral("hex: 31_3233")),
               true);

    const QList<GlobalControlItem> paletteCommands =
        globalControlService.query(
            GlobalControlCategory::Commands,
            QStringLiteral("duplicate"));
    expectBool("Ctrl+Space command category uses Action registry",
               std::any_of(
                   paletteCommands.cbegin(),
                   paletteCommands.cend(),
                   [](const GlobalControlItem& item) {
                       return item.actionId
                           == QString::fromLatin1(
                               ActionIds::EditDuplicateLines);
                   }),
               true);

    GlobalControlQueryContext paletteContext;
    paletteContext.editorAvailable = true;
    paletteContext.fileName = path;
    paletteContext.moduleName = QStringLiteral("top");
    paletteContext.documentText = content;
    paletteContext.cursorLine = 1;
    paletteContext.cursorPosition = content.indexOf(
        QStringLiteral("module top"));
    const EditorInsertPaletteService insertPalette;
    const QList<GlobalControlItem> paletteTemplates =
        insertPalette.query(GlobalControlCategory::Templates,
                            QStringLiteral("always_comb"),
                            paletteContext);
    expectBool("Ctrl+Space template category uses template catalog",
               std::any_of(
                   paletteTemplates.cbegin(),
                   paletteTemplates.cend(),
                   [](const GlobalControlItem& item) {
                       return item.kind
                                  == GlobalControlItemKind::Template
                           && item.id == QStringLiteral(";;ac")
                           && item.insertionText.contains(
                               QStringLiteral("always_comb"));
                   }),
               true);

    {
        const QString completionContextSource =
            QStringLiteral(
                "module palette_top;\n"
                "  packet_t packet;\n"
                "  state_t state;\n"
                "  always_comb state <= IDLE;\n"
                "  assign sink = packet.payload;\n"
                "  always_comb case (state) IDLE: sink = 1'b0; endcase\n"
                "endmodule\n");
        TSDocument completionSyntax;
        completionSyntax.setText(completionContextSource);
        const int memberPosition = completionContextSource.indexOf(
            QStringLiteral("payload")) + 3;
        const TSCompletionContextTarget memberContext =
            completionSyntax.completionContextAt(memberPosition);
        expectBool("Ctrl+Space syntax identifies member access path",
                   memberContext.memberAccess
                       && memberContext.memberPath
                              == QStringList{QStringLiteral("packet")},
                   true);
        const int assignmentValue = completionContextSource.indexOf(
            QStringLiteral("IDLE"));
        expectBool("Ctrl+Space syntax distinguishes nonblocking assignment",
                   completionSyntax.completionContextAt(assignmentValue)
                           .expectedTypeIdentifier
                       == QStringLiteral("state"),
                   true);
        const int caseValue = completionContextSource.lastIndexOf(
            QStringLiteral("IDLE"));
        expectBool("Ctrl+Space syntax derives case selector type anchor",
                   completionSyntax.completionContextAt(caseValue)
                           .expectedTypeIdentifier
                       == QStringLiteral("state"),
                   true);

        using DeclarationKind = SymbolTaxonomy::DeclarationKind;
        using CollectorKind = SymbolTaxonomy::CollectorKind;
        const QString palettePath = QStringLiteral("palette_context.sv");
        const QList<SemanticSymbolRecord> paletteRecords{
            makeSemanticFixtureRecord(
                QStringLiteral("palette_top"), DeclarationKind::Module,
                CollectorKind::User, QString(), QString(), 1, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("palette_child"), DeclarationKind::Module,
                CollectorKind::User, QString(), QString(), 1, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("wire_sig"), DeclarationKind::Signal,
                CollectorKind::Wire, QStringLiteral("palette_top"),
                QStringLiteral("wire"), 2, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("logic_sig"), DeclarationKind::Signal,
                CollectorKind::Logic, QStringLiteral("palette_top"),
                QStringLiteral("logic"), 3, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("u_signal"), DeclarationKind::Signal,
                CollectorKind::Logic, QStringLiteral("palette_top"),
                QStringLiteral("logic"), 3, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("sp_signal"), DeclarationKind::Signal,
                CollectorKind::Logic, QStringLiteral("palette_top"),
                QStringLiteral("logic"), 3, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("ne_signal"), DeclarationKind::Signal,
                CollectorKind::Logic, QStringLiteral("palette_top"),
                QStringLiteral("logic"), 3, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("nsp_signal"), DeclarationKind::Signal,
                CollectorKind::Logic, QStringLiteral("palette_top"),
                QStringLiteral("logic"), 3, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("ns_signal"), DeclarationKind::Signal,
                CollectorKind::Logic, QStringLiteral("palette_top"),
                QStringLiteral("logic"), 3, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("word_t"), DeclarationKind::Typedef,
                CollectorKind::Typedef, QStringLiteral("palette_top"),
                QStringLiteral("logic [7:0]"), 3, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("state_t"), DeclarationKind::Enum,
                CollectorKind::Enum, QStringLiteral("palette_top"),
                QStringLiteral("logic [1:0]"), 4, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("state"), DeclarationKind::Signal,
                CollectorKind::EnumVariable,
                QStringLiteral("palette_top"),
                QStringLiteral("state_t"), 5, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("IDLE"), DeclarationKind::Enum,
                CollectorKind::EnumValue, QStringLiteral("state_t"),
                QStringLiteral("state_t"), 6, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("RUN"), DeclarationKind::Enum,
                CollectorKind::EnumValue, QStringLiteral("state_t"),
                QStringLiteral("state_t"), 7, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("packet_t"), DeclarationKind::Struct,
                CollectorKind::PackedStruct,
                QStringLiteral("palette_top"), QString(), 8, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("packet"), DeclarationKind::StructVariable,
                CollectorKind::PackedStructVariable,
                QStringLiteral("palette_top"),
                QStringLiteral("packet_t"), 9, palettePath),
            makeSemanticFixtureRecord(
                QStringLiteral("payload"), DeclarationKind::StructMember,
                CollectorKind::StructMember, QStringLiteral("packet_t"),
                QStringLiteral("logic [7:0]"), 10, palettePath),
        };
        SemanticIndex paletteIndex;
        paletteIndex.setSnapshot(
            sharedSnapshotFromRecords(paletteRecords));
        CompletionService::getInstance()->setSemanticIndex(&paletteIndex);

        GlobalControlQueryContext typedContext;
        typedContext.editorAvailable = true;
        typedContext.fileName = palettePath;
        typedContext.moduleName = QStringLiteral("palette_top");
        typedContext.documentText = completionContextSource;
        typedContext.cursorLine = 4;
        typedContext.cursorPosition = memberPosition;
        typedContext.replacementStart = memberPosition - 3;
        typedContext.replacementLength = 7;
        typedContext.documentRevision = 12;

        const QList<GlobalControlItem> wireItems = insertPalette.query(
            GlobalControlCategory::Symbols,
            QStringLiteral("w "), typedContext);
        expectBool("Ctrl+Space selector filters wire only",
                   wireItems.size() == 1
                       && wireItems.first().title
                              == QStringLiteral("wire_sig")
                       && wireItems.first().replacementStart
                              == typedContext.replacementStart
                       && wireItems.first().sourceDocumentRevision == 12,
                    true);

        const QList<GlobalControlItem> moduleItems = insertPalette.query(
            GlobalControlCategory::Symbols,
            QStringLiteral("m palette_"), typedContext);
        expectBool("Ctrl+Space m selector filters modules only",
                   moduleItems.size() == 2
                       && std::all_of(
                           moduleItems.cbegin(),
                           moduleItems.cend(),
                           [](const GlobalControlItem& item) {
                               return item.title
                                      == QStringLiteral("palette_child")
                                   || item.title
                                      == QStringLiteral("palette_top");
                           }),
                   true);

        const auto itemTitles = [](const QList<GlobalControlItem>& items) {
            QSet<QString> titles;
            for (const GlobalControlItem& item : items)
                titles.insert(item.title);
            return titles;
        };
        const QSet<QString> typedefTitles = itemTitles(insertPalette.query(
            GlobalControlCategory::Symbols,
            QStringLiteral("td "), typedContext));
        expectBool("Ctrl+Space td selector includes all typedef families",
                   typedefTitles.contains(QStringLiteral("word_t"))
                       && typedefTitles.contains(QStringLiteral("state_t"))
                       && typedefTitles.contains(QStringLiteral("packet_t")),
                   true);
        expectBool("Ctrl+Space et selector filters enum types",
                   itemTitles(insertPalette.query(
                       GlobalControlCategory::Symbols,
                       QStringLiteral("et "), typedContext))
                           == QSet<QString>{QStringLiteral("state_t")},
                   true);
        expectBool("Ctrl+Space ev selector filters enum variables",
                   itemTitles(insertPalette.query(
                       GlobalControlCategory::Symbols,
                       QStringLiteral("ev "), typedContext))
                           == QSet<QString>{QStringLiteral("state")},
                   true);
        expectBool("Ctrl+Space st selector filters struct types",
                   itemTitles(insertPalette.query(
                       GlobalControlCategory::Symbols,
                       QStringLiteral("st "), typedContext))
                           == QSet<QString>{QStringLiteral("packet_t")},
                   true);
        const QSet<QString> structVariableTitles = itemTitles(
            insertPalette.query(GlobalControlCategory::Symbols,
                                QStringLiteral("sv "), typedContext));
        expectBool("Ctrl+Space sv selector includes struct variables",
                   structVariableTitles.contains(QStringLiteral("packet")),
                   true);
        expectBool("Ctrl+Space sv selector excludes other symbol families",
                   structVariableTitles
                       == QSet<QString>{QStringLiteral("packet")},
                   true);
        expectBool("Ctrl+Space removed selector aliases are name text",
                   itemTitles(insertPalette.query(
                       GlobalControlCategory::Symbols,
                       QStringLiteral("u "), typedContext))
                           .contains(QStringLiteral("u_signal"))
                       && itemTitles(insertPalette.query(
                           GlobalControlCategory::Symbols,
                           QStringLiteral("sp "), typedContext))
                              .contains(QStringLiteral("sp_signal"))
                       && itemTitles(insertPalette.query(
                           GlobalControlCategory::Symbols,
                           QStringLiteral("ne "), typedContext))
                              .contains(QStringLiteral("ne_signal"))
                       && itemTitles(insertPalette.query(
                           GlobalControlCategory::Symbols,
                           QStringLiteral("nsp "), typedContext))
                              .contains(QStringLiteral("nsp_signal"))
                       && itemTitles(insertPalette.query(
                           GlobalControlCategory::Symbols,
                           QStringLiteral("ns "), typedContext))
                              .contains(QStringLiteral("ns_signal")),
                   true);

        typedContext.memberAccess = true;
        typedContext.memberPath = {QStringLiteral("packet")};
        const QList<GlobalControlItem> memberItems = insertPalette.query(
            GlobalControlCategory::Symbols, QString(), typedContext);
        expectBool("Ctrl+Space member context suppresses unrelated symbols",
                   memberItems.size() == 1
                       && memberItems.first().title
                              == QStringLiteral("payload"),
                   true);
        expectBool("Ctrl+Space sm selector resolves receiver members",
                   itemTitles(insertPalette.query(
                       GlobalControlCategory::Symbols,
                       QStringLiteral("sm "), typedContext))
                           == QSet<QString>{QStringLiteral("payload")},
                   true);

        typedContext.memberAccess = false;
        typedContext.memberPath.clear();
        const QSet<QString> structFamilyTitles = itemTitles(
            insertPalette.query(GlobalControlCategory::Symbols,
                                QStringLiteral("s "), typedContext));
        expectBool("Ctrl+Space s selector includes visible struct family",
                   structFamilyTitles.contains(QStringLiteral("packet_t"))
                       && structFamilyTitles.contains(QStringLiteral("packet"))
                       && structFamilyTitles.contains(QStringLiteral("payload")),
                   true);
        expectBool("Ctrl+Space s selector excludes other symbol families",
                   structFamilyTitles.size() == 3,
                   true);
        expectBool("Ctrl+Space sm selector finds members of visible types",
                   itemTitles(insertPalette.query(
                       GlobalControlCategory::Symbols,
                       QStringLiteral("sm "), typedContext))
                           == QSet<QString>{QStringLiteral("payload")},
                   true);
        typedContext.expectedTypeIdentifier = QStringLiteral("state");
        const QList<GlobalControlItem> enumItems = insertPalette.query(
            GlobalControlCategory::Symbols, QString(), typedContext);
        expectBool("Ctrl+Space expected enum values rank first",
                   enumItems.size() >= 2
                       && enumItems.at(0).title == QStringLiteral("IDLE")
                       && enumItems.at(1).title == QStringLiteral("RUN"),
                   true);
        const QSet<QString> enumFamilyTitles = itemTitles(insertPalette.query(
            GlobalControlCategory::Symbols,
            QStringLiteral("e "), typedContext));
        expectBool("Ctrl+Space e selector includes complete enum family",
                   enumFamilyTitles.contains(QStringLiteral("state_t"))
                       && enumFamilyTitles.contains(QStringLiteral("state"))
                       && enumFamilyTitles.contains(QStringLiteral("IDLE"))
                       && enumFamilyTitles.contains(QStringLiteral("RUN")),
                   true);
        expectBool("Ctrl+Space ee selector filters enum values",
                   itemTitles(insertPalette.query(
                       GlobalControlCategory::Symbols,
                       QStringLiteral("ee "), typedContext))
                           == QSet<QString>{QStringLiteral("IDLE"),
                                            QStringLiteral("RUN")},
                   true);
        CompletionService::getInstance()->setSemanticIndex(
            SemanticIndex::getInstance());
    }
    const GhostNumericLiteralReport includeStringHover =
        numericLiteralAt(numericHoverText, includeStringHoverPosition);
    expectBool("Ghost literal hover skips include string",
               includeStringHover.available,
               false);
    const GhostNumericLiteralReport unsizedDecimalHover =
        numericLiteralAt(numericHoverText,
                         unsizedDecimalHoverPosition);
    expectBool("Ghost literal hover shows Slang unsized decimal in all bases",
               unsizedDecimalHover.available
                   && unsizedDecimalHover.radixRepresentations.size() == 4
                   && unsizedDecimalHover.displayText.contains(
                       QStringLiteral("binary:"))
                   && unsizedDecimalHover.displayText.contains(
                       QStringLiteral("octal:"))
                   && unsizedDecimalHover.displayText.contains(
                       QStringLiteral("decimal:"))
                   && unsizedDecimalHover.displayText.contains(
                       QStringLiteral("4545"))
                   && unsizedDecimalHover.displayText.contains(
                       QStringLiteral("hex:"))
                   && unsizedDecimalHover.displayText.contains(
                       QStringLiteral("11c1")),
               true);
    const GhostNumericLiteralReport unsizedHexHover =
        numericLiteralAt(numericHoverText,
                         unsizedHexHoverPosition);
    expectBool("Ghost literal hover shows Slang unsized hex value in other bases",
               unsizedHexHover.available
                   && unsizedHexHover.displayText.contains(
                       QStringLiteral("binary:"))
                   && unsizedHexHover.displayText.contains(
                       QStringLiteral("decimal:"))
                   && unsizedHexHover.displayText.contains(
                       QStringLiteral("hex:")),
               true);
    const GhostNumericLiteralReport negativeHover =
        numericLiteralAt(numericHoverText, negativeHoverPosition);
    expectBool("Ghost numeric hover preserves unary signed negative value",
               negativeHover.available
                   && negativeHover.displayText.contains(
                       QStringLiteral("binary: -1"))
                   && negativeHover.displayText.contains(
                       QStringLiteral("decimal: -1"))
                   && negativeHover.displayText.contains(
                       QStringLiteral("hex: -1")),
               true);
    const GhostNumericLiteralReport truncatedHover =
        numericLiteralAt(numericHoverText, truncatedHoverPosition);
    expectBool("Ghost numeric hover applies sized literal truncation",
               truncatedHover.available
                   && truncatedHover.displayText.contains(
                       QStringLiteral("binary: 1111"))
                   && truncatedHover.displayText.contains(
                       QStringLiteral("decimal: 15"))
                   && truncatedHover.displayText.contains(
                       QStringLiteral("hex: f")),
               true);
    const GhostNumericLiteralReport unknownHover =
        numericLiteralAt(numericHoverText, unknownHoverPosition);
    expectBool("Ghost numeric hover preserves X Z without guessed decimal",
               unknownHover.available
                   && unknownHover.radixRepresentations.size() == 3
                   && unknownHover.displayText.contains(
                       QStringLiteral("binary: 10xz_01z1"))
                   && unknownHover.displayText.contains(
                       QStringLiteral("hex:"))
                   && !unknownHover.displayText.contains(
                       QStringLiteral("decimal:")),
               true);
    const GhostNumericLiteralReport concatHover =
        numericLiteralAt(numericHoverText, concatHoverPosition);
    expectBool("Ghost numeric hover remains authoritative in concat parameter context",
               concatHover.available
                   && concatHover.displayText.contains(
                       QStringLiteral("binary: 1010"))
                   && concatHover.displayText.contains(
                       QStringLiteral("decimal: 10"))
                   && concatHover.displayText.contains(
                       QStringLiteral("hex: a")),
               true);
    const GhostNumericLiteralReport legacyStringHover =
        numericLiteralAt(QStringLiteral("`include \"123.sv\"\n"), 10);
    expectBool("Ghost literal hover skips include legacy string",
               legacyStringHover.available,
               false);
    const GhostNumericLiteralReport commentHover =
        numericLiteralAt(numericHoverText, commentHoverPosition);
    expectBool("Ghost literal hover skips comment",
               commentHover.available,
               false);

    const QString commentHighlightText =
        QStringLiteral("assign a = 1; // 80m\n");
    TSDocument commentTsDocument;
    commentTsDocument.setText(commentHighlightText);
    QTextDocument commentDocument(commentHighlightText);
    MyHighlighter commentHighlighter(&commentDocument, &commentTsDocument);
    commentHighlighter.rehighlight();
    const QTextBlock commentBlock = commentDocument.firstBlock();
    const int numberInComment =
        commentBlock.text().indexOf(QStringLiteral("80m"));
    bool commentNumberUsesCommentFormat = false;
    for (const QTextLayout::FormatRange& range :
         commentBlock.layout()->formats()) {
        if (numberInComment >= range.start
            && numberInComment < range.start + range.length) {
            commentNumberUsesCommentFormat =
                range.format.foreground().color() == QColor(QStringLiteral("#7F848E"))
                && range.format.fontItalic();
        }
    }
    expectBool("Highlighter line comment wins",
               commentNumberUsesCommentFormat,
               true);

    const QString moduleEndOriginal =
        QStringLiteral("module module_end_demo;\n"
                       "  logic a;\n"
                       "endmodule\n");
    MyCodeEditor moduleEndEditor;
    moduleEndEditor.setPlainText(moduleEndOriginal);
    QTextCursor moduleEndCursor = moduleEndEditor.textCursor();
    moduleEndCursor.setPosition(
        moduleEndOriginal.indexOf(QStringLiteral("logic")));
    moduleEndEditor.setTextCursor(moduleEndCursor);
    QString moduleEndMessage;
    const bool moduleEndOk =
        moduleEndEditor.goToFinalEndmodule(&moduleEndMessage);
    expectBool("go endmodule editor navigation succeeds",
               moduleEndOk,
               true);
    expectEq("go endmodule does not edit text",
             moduleEndEditor.toPlainText(),
             moduleEndOriginal);
    expectBool("go endmodule places caret before final endmodule",
               moduleEndEditor.textCursor().position()
                   == moduleEndOriginal.indexOf(
                       QStringLiteral("endmodule")),
               true);

    const QString noModuleEnd = QStringLiteral("logic a;\n");
    moduleEndEditor.setPlainText(noModuleEnd);
    moduleEndCursor = moduleEndEditor.textCursor();
    moduleEndCursor.setPosition(0);
    moduleEndEditor.setTextCursor(moduleEndCursor);
    moduleEndMessage.clear();
    expectBool("go endmodule reports no module",
               !moduleEndEditor.goToFinalEndmodule(&moduleEndMessage)
                   && moduleEndMessage == QStringLiteral("No current module")
                   && moduleEndEditor.toPlainText() == noModuleEnd,
               true);

    const QString formatterInput =
        QStringLiteral("module top;\n"
                       "logic a;\n"
                       "  // comment-only lines stay where the user put them\n"
                       "always_ff @(posedge clk) begin\n"
                       "if (rst) begin\n"
                       "a <= 1'b0;\n"
                       "end else begin\n"
                       "a <= ~a;\n"
                       "end\n"
                       "end\n"
                       "always_comb begin\n"
                       "case (sel)\n"
                       "2'b00: y = \"begin\";\n"
                       "default: y = a; // endcase\n"
                       "endcase\n"
                       "end\n"
                       "`ifdef KEEP_COLUMN\n"
                       "  assign macro_guarded = a;\n"
                       "`endif\n"
                       "endmodule\n");
    const FormatterReport formatterReport =
        FormatterService::getInstance()->formatDocument(formatterInput);
    expectBool("Formatter report changed",
               formatterReport.changed,
               true);
    expectEq("Formatter conservative indentation",
             formatterReport.formattedText,
             QStringLiteral("module top;\n"
                            "logic a;\n"
                            "// comment-only lines stay where the user put them\n"
                            "always_ff @(posedge clk) begin\n"
                            "    if (rst) begin\n"
                            "        a <= 1'b0;\n"
                            "    end else begin\n"
                            "        a <= ~a;\n"
                            "    end\n"
                            "end\n"
                            "always_comb begin\n"
                            "    case (sel)\n"
                            "        2'b00  : y = \"begin\";\n"
                            "        default: y = a;        // endcase\n"
                            "    endcase\n"
                            "end\n"
                            "`ifdef KEEP_COLUMN\n"
                            "assign macro_guarded = a;\n"
                            "`endif\n"
                            "endmodule\n"));
    const FormatterReport unchangedFormatterReport =
        FormatterService::getInstance()->formatDocument(
            formatterReport.formattedText);
    expectBool("Formatter idempotent",
               unchangedFormatterReport.changed,
               false);
    const QString formatterSingleStatementInput =
        QStringLiteral("module single_stmt_demo;\n"
                       "always_comb begin\n"
                       "if (en)\n"
                       "y = a;\n"
                       "else if (sel)\n"
                       "y = b;\n"
                       "else\n"
                       "y = c;\n"
                       "for (int i = 0; i < 2; i++)\n"
                       "data[i] = value;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterSingleStatementReport =
        FormatterService::getInstance()->formatDocument(
            formatterSingleStatementInput);
    expectBool("Formatter single statement body report changed",
               formatterSingleStatementReport.changed,
               true);
    expectEq("Formatter indents single statement bodies",
             formatterSingleStatementReport.formattedText,
             QStringLiteral("module single_stmt_demo;\n"
                            "always_comb begin\n"
                            "    if (en)\n"
                            "        y = a;\n"
                            "    else if (sel)\n"
                            "        y = b;\n"
                            "    else\n"
                            "        y = c;\n"
                            "    for (int i = 0; i < 2; i++)\n"
                            "        data[i] = value;\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedSingleStatementReport =
        FormatterService::getInstance()->formatDocument(
            formatterSingleStatementReport.formattedText);
    expectBool("Formatter single statement body idempotent",
               unchangedSingleStatementReport.changed,
               false);
    const FormatterReport indentOnlySingleStatementReport =
        FormatterService::getInstance()->formatDocument(
            formatterSingleStatementInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps single statement body indentation",
             indentOnlySingleStatementReport.formattedText,
             formatterSingleStatementReport.formattedText);

    const QString formatterDanglingElseInput =
        QStringLiteral("module dangling_else_demo;\n"
                       "always_comb begin\n"
                       "if (outer)\n"
                       "if (inner)\n"
                       "y = a;\n"
                       "else\n"
                       "y = b;\n"
                       "else\n"
                       "y = c;\n"
                       "end\n"
                       "endmodule\n");
    const QString formatterDanglingElseExpected =
        QStringLiteral("module dangling_else_demo;\n"
                       "always_comb begin\n"
                       "    if (outer)\n"
                       "        if (inner)\n"
                       "            y = a;\n"
                       "        else\n"
                       "            y = b;\n"
                       "    else\n"
                       "        y = c;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterDanglingElseReport =
        FormatterService::getInstance()->formatDocument(
            formatterDanglingElseInput);
    expectEq("Formatter preserves Tree-sitter dangling-else ownership",
             formatterDanglingElseReport.formattedText,
             formatterDanglingElseExpected);
    expectBool("Formatter dangling-else token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterDanglingElseInput,
                       formatterDanglingElseReport.formattedText),
               true);
    expectBool("Formatter dangling-else output idempotent",
               !FormatterService::getInstance()
                    ->formatDocument(
                        formatterDanglingElseReport.formattedText)
                    .changed,
               true);

    const QString formatterProceduralBodyInput =
        QStringLiteral("module procedural_stmt_demo;\n"
                       "always_ff @(posedge clk)\n"
                       "q <= d;\n"
                       "always_comb\n"
                       "y = a & b;\n"
                       "initial\n"
                       "ready = 1'b0;\n"
                       "final\n"
                       "$display(\"done\");\n"
                       "initial begin\n"
                       "forever\n"
                       "tick = ~tick;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterProceduralBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterProceduralBodyInput);
    expectBool("Formatter procedural body report changed",
               formatterProceduralBodyReport.changed,
               true);
    expectEq("Formatter indents procedural single statement bodies",
             formatterProceduralBodyReport.formattedText,
             QStringLiteral("module procedural_stmt_demo;\n"
                            "always_ff @(posedge clk)\n"
                            "    q <= d;\n"
                            "always_comb\n"
                            "    y = a & b;\n"
                            "initial\n"
                            "    ready = 1'b0;\n"
                            "final\n"
                            "    $display(\"done\");\n"
                            "initial begin\n"
                            "    forever\n"
                            "        tick = ~tick;\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedProceduralBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterProceduralBodyReport.formattedText);
    expectBool("Formatter procedural body idempotent",
               unchangedProceduralBodyReport.changed,
               false);
    const FormatterReport indentOnlyProceduralBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterProceduralBodyInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps procedural body indentation",
             indentOnlyProceduralBodyReport.formattedText,
             formatterProceduralBodyReport.formattedText);

    const QString formatterMultilineHeaderBodyInput =
        QStringLiteral("module multiline_header_demo;\n"
                       "always_ff @(posedge clk or\n"
                       "negedge rst_n)\n"
                       "q <= d;\n"
                       "always @(a or\n"
                       "b)\n"
                       "y = a & b;\n"
                       "always_comb begin\n"
                       "if (sel &&\n"
                       "ready)\n"
                       "z = a;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterMultilineHeaderBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterMultilineHeaderBodyInput);
    expectBool("Formatter multiline header body report changed",
               formatterMultilineHeaderBodyReport.changed,
               true);
    expectEq("Formatter indents multiline header single statement bodies",
             formatterMultilineHeaderBodyReport.formattedText,
             QStringLiteral("module multiline_header_demo;\n"
                            "always_ff @(posedge clk or\n"
                            "    negedge rst_n)\n"
                            "    q <= d;\n"
                            "always @(a or\n"
                            "    b)\n"
                            "    y = a & b;\n"
                            "always_comb begin\n"
                            "    if (sel &&\n"
                            "        ready)\n"
                            "        z = a;\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedMultilineHeaderBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterMultilineHeaderBodyReport.formattedText);
    expectBool("Formatter multiline header body idempotent",
               unchangedMultilineHeaderBodyReport.changed,
               false);
    const FormatterReport indentOnlyMultilineHeaderBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterMultilineHeaderBodyInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps multiline header body indentation",
             indentOnlyMultilineHeaderBodyReport.formattedText,
             formatterMultilineHeaderBodyReport.formattedText);

    const QString formatterSvBlockInput =
        QStringLiteral("program tb;\n"
                       "default clocking cb @(posedge clk);\n"
                       "input req;\n"
                       "output grant;\n"
                       "endclocking\n"
                       "property req_grant;\n"
                       "req |=> grant;\n"
                       "endproperty\n"
                       "sequence two_req;\n"
                       "req ##1 req;\n"
                       "endsequence\n"
                       "covergroup cg @(posedge clk);\n"
                       "coverpoint req;\n"
                       "endgroup\n"
                       "checker chk;\n"
                       "assert property (req_grant);\n"
                       "endchecker\n"
                       "endprogram\n");
    const FormatterReport formatterSvBlockReport =
        FormatterService::getInstance()->formatDocument(formatterSvBlockInput);
    expectBool("Formatter SystemVerilog block boundary report changed",
               formatterSvBlockReport.changed,
               true);
    expectEq("Formatter indents SystemVerilog block boundaries",
             formatterSvBlockReport.formattedText,
             QStringLiteral("program tb;\n"
                            "default clocking cb @(posedge clk);\n"
                            "    input  req  ;\n"
                            "    output grant;\n"
                            "endclocking\n"
                            "property req_grant;\n"
                            "    req |=> grant;\n"
                            "endproperty\n"
                            "sequence two_req;\n"
                            "    req ##1 req;\n"
                            "endsequence\n"
                            "covergroup cg @(posedge clk);\n"
                            "    coverpoint req;\n"
                            "endgroup\n"
                            "checker chk;\n"
                            "    assert property (req_grant);\n"
                            "endchecker\n"
                            "endprogram\n"));
    const FormatterReport unchangedSvBlockReport =
        FormatterService::getInstance()->formatDocument(
            formatterSvBlockReport.formattedText);
    expectBool("Formatter SystemVerilog block boundary idempotent",
               unchangedSvBlockReport.changed,
               false);
    const FormatterReport indentOnlySvBlockReport =
        FormatterService::getInstance()->formatDocument(
            formatterSvBlockInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps SystemVerilog block boundary indentation",
             indentOnlySvBlockReport.formattedText,
             QStringLiteral("program tb;\n"
                            "default clocking cb @(posedge clk);\n"
                            "    input req;\n"
                            "    output grant;\n"
                            "endclocking\n"
                            "property req_grant;\n"
                            "    req |=> grant;\n"
                            "endproperty\n"
                            "sequence two_req;\n"
                            "    req ##1 req;\n"
                            "endsequence\n"
                            "covergroup cg @(posedge clk);\n"
                            "    coverpoint req;\n"
                            "endgroup\n"
                            "checker chk;\n"
                            "    assert property (req_grant);\n"
                            "endchecker\n"
                            "endprogram\n"));
    const QString formatterForkInput =
        QStringLiteral("module fork_demo;\n"
                       "initial begin\n"
                       "fork\n"
                       "a = 1'b1;\n"
                       "b = 1'b0;\n"
                       "join_any\n"
                       "disable fork;\n"
                       "wait fork;\n"
                       "done = 1'b1;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterForkReport =
        FormatterService::getInstance()->formatDocument(formatterForkInput);
    expectBool("Formatter fork statement report changed",
               formatterForkReport.changed,
               true);
    expectEq("Formatter indents fork statements without disable/wait drift",
             formatterForkReport.formattedText,
             QStringLiteral("module fork_demo;\n"
                            "initial begin\n"
                            "    fork\n"
                            "        a = 1'b1;\n"
                            "        b = 1'b0;\n"
                            "    join_any\n"
                            "    disable fork;\n"
                            "    wait fork;\n"
                            "    done = 1'b1;\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedForkReport =
        FormatterService::getInstance()->formatDocument(
            formatterForkReport.formattedText);
    expectBool("Formatter fork statement idempotent",
               unchangedForkReport.changed,
               false);
    const FormatterReport indentOnlyForkReport =
        FormatterService::getInstance()->formatDocument(
            formatterForkInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps fork statement indentation",
             indentOnlyForkReport.formattedText,
             formatterForkReport.formattedText);

    const QString formatterAlignmentInput =
        QStringLiteral("module align_demo;\n"
                       "logic [7:0] data;\n"
                       "logic valid;\n"
                       "parameter int P = 8;\n"
                       "parameter int LONG_NAME = P + 1;\n"
                       "endmodule\n");
    const FormatterReport formatterAlignmentReport =
        FormatterService::getInstance()->formatDocument(formatterAlignmentInput);
    expectBool("Formatter alignment report changed",
               formatterAlignmentReport.changed,
               true);
    expectEq("Formatter aligns declaration blocks",
             formatterAlignmentReport.formattedText,
             QStringLiteral("module align_demo;\n"
                            "logic [7:0] data ;\n"
                            "logic       valid;\n"
                            "parameter int P         = 8    ;\n"
                            "parameter int LONG_NAME = P + 1;\n"
                            "endmodule\n"));
    const FormatterReport unchangedAlignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAlignmentReport.formattedText);
    expectBool("Formatter alignment idempotent",
               unchangedAlignmentReport.changed,
               false);
    const QString formatterArrayDeclInput =
        QStringLiteral("module array_decl_demo;\n"
                       "logic flag [3:0];\n"
                       "logic [7:0] data_bus [DEPTH-1:0];\n"
                       "parameter int LUT [4] = '{0, 1, 2, 3};\n"
                       "parameter int LONG_LUT [DEPTH] = DEFAULT_LUT;\n"
                       "endmodule\n");
    const FormatterReport formatterArrayDeclReport =
        FormatterService::getInstance()->formatDocument(
            formatterArrayDeclInput);
    expectBool("Formatter declaration array dimension report changed",
               formatterArrayDeclReport.changed,
               true);
    expectEq("Formatter aligns declaration array dimensions",
             formatterArrayDeclReport.formattedText,
             QStringLiteral("module array_decl_demo;\n"
                            "logic       flag     [3:0]      ;\n"
                            "logic [7:0] data_bus [DEPTH-1:0];\n"
                            "parameter int LUT      [4]     = '{0, 1, 2, 3};\n"
                            "parameter int LONG_LUT [DEPTH] = DEFAULT_LUT  ;\n"
                            "endmodule\n"));
    const FormatterReport unchangedArrayDeclReport =
        FormatterService::getInstance()->formatDocument(
            formatterArrayDeclReport.formattedText);
    expectBool("Formatter declaration array dimension idempotent",
               unchangedArrayDeclReport.changed,
               false);
    const QString formatterParameterPortInput =
        QStringLiteral("module param_port_demo #(\n"
                       "parameter int P = 8,\n"
                       "parameter int LONG_PARAM = P + 1,\n"
                       "localparam logic [7:0] MASK [2] = '{default: 1'b0}\n"
                       ")();\n"
                       "endmodule\n");
    const FormatterReport formatterParameterPortReport =
        FormatterService::getInstance()->formatDocument(
            formatterParameterPortInput);
    expectBool("Formatter parameter port list report changed",
               formatterParameterPortReport.changed,
               true);
    expectEq("Formatter aligns parameter port lists",
             formatterParameterPortReport.formattedText,
             QStringLiteral(
                 "module param_port_demo #(\n"
                 "    parameter  int         P              = 8               ,\n"
                 "    parameter  int         LONG_PARAM     = P + 1           ,\n"
                 "    localparam logic [7:0] MASK       [2] = '{default:1'b0}\n"
                 ")();\n"
                 "endmodule\n"));
    const FormatterReport unchangedParameterPortReport =
        FormatterService::getInstance()->formatDocument(
            formatterParameterPortReport.formattedText);
    expectBool("Formatter parameter port list idempotent",
               unchangedParameterPortReport.changed,
               false);
    const FormatterOptions indentOnlyOptions =
        FormatterService::optionsForProfile(FormatterProfile::IndentOnly);
    expectBool("Formatter indent-only profile disables alignments",
               !indentOnlyOptions.alignDeclarationBlocks
                   && !indentOnlyOptions.alignPortLists
                   && !indentOnlyOptions.alignInstanceMaps
                   && !indentOnlyOptions.alignCaseItems
                   && !indentOnlyOptions.alignEnumItems
                   && !indentOnlyOptions.alignAssignments
                   && !indentOnlyOptions.alignContinuationOperators
                   && !indentOnlyOptions.alignCallArgumentContinuations,
               true);
    expectEq("Formatter indent-only profile name",
             FormatterService::profileDisplayName(
                 FormatterProfile::IndentOnly),
             QStringLiteral("Indent Only"));
    const FormatterReport indentOnlyReport =
        FormatterService::getInstance()->formatDocument(
            formatterAlignmentInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only profile skips declaration alignment",
             indentOnlyReport.formattedText,
             QStringLiteral("module align_demo;\n"
                            "logic [7:0] data;\n"
                            "logic valid;\n"
                            "parameter int P = 8;\n"
                            "parameter int LONG_NAME = P + 1;\n"
                            "endmodule\n"));
    const FormatterReport indentOnlyArrayDeclReport =
        FormatterService::getInstance()->formatDocument(
            formatterArrayDeclInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips declaration array dimension alignment",
             indentOnlyArrayDeclReport.formattedText,
             QStringLiteral("module array_decl_demo;\n"
                            "logic flag [3:0];\n"
                            "logic [7:0] data_bus [DEPTH-1:0];\n"
                            "parameter int LUT [4] = '{0, 1, 2, 3};\n"
                            "parameter int LONG_LUT [DEPTH] = DEFAULT_LUT;\n"
                            "endmodule\n"));
    const FormatterReport indentOnlyParameterPortReport =
        FormatterService::getInstance()->formatDocument(
            formatterParameterPortInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips parameter port list alignment",
             indentOnlyParameterPortReport.formattedText,
             QStringLiteral("module param_port_demo #(\n"
                            "    parameter int P = 8,\n"
                            "    parameter int LONG_PARAM = P + 1,\n"
                            "    localparam logic [7:0] MASK [2] = '{default: 1'b0}\n"
                            ")();\n"
                            "endmodule\n"));

    const QString formatterPortListInput =
        QStringLiteral("module port_demo(\n"
                       "input logic clk,\n"
                       "input logic [7:0] data,\n"
                       "output logic ready,\n"
                       "inout wire pad\n"
                       ");\n"
                       "endmodule\n");
    const FormatterReport formatterPortListReport =
        FormatterService::getInstance()->formatDocument(formatterPortListInput);
    expectBool("Formatter port list report changed",
               formatterPortListReport.changed,
               true);
    expectEq("Formatter aligns port lists",
             formatterPortListReport.formattedText,
             QStringLiteral("module port_demo(\n"
                            "    input  logic       clk   ,\n"
                            "    input  logic [7:0] data  ,\n"
                            "    output logic       ready ,\n"
                            "    inout  wire        pad\n"
                            ");\n"
                            "endmodule\n"));
    const FormatterReport unchangedPortListReport =
        FormatterService::getInstance()->formatDocument(
            formatterPortListReport.formattedText);
    expectBool("Formatter port list idempotent",
               unchangedPortListReport.changed,
               false);

    const QString formatterInstanceMapInput =
        QStringLiteral("module inst_demo;\n"
                       "child #(\n"
                       ".PARAM(8),\n"
                       ".LONG_PARAM(WIDTH)\n"
                       ") u_child (\n"
                       ".clk(clk),\n"
                       ".rst_n(rst_n),\n"
                       ".data_in(data_bus),\n"
                       ".ready(ready)\n"
                       ");\n"
                       "endmodule\n");
    const FormatterReport formatterInstanceMapReport =
        FormatterService::getInstance()->formatDocument(
            formatterInstanceMapInput);
    expectBool("Formatter instance map report changed",
               formatterInstanceMapReport.changed,
               true);
    expectEq("Formatter aligns instance maps",
             formatterInstanceMapReport.formattedText,
             QStringLiteral("module inst_demo;\n"
                            "child #(\n"
                            "    .PARAM      ( 8     ),\n"
                            "    .LONG_PARAM ( WIDTH )\n"
                            ") u_child(\n"
                            "    .clk     ( clk      ),\n"
                            "    .rst_n   ( rst_n    ),\n"
                            "    .data_in ( data_bus ),\n"
                            "    .ready   ( ready    )\n"
                            ");\n"
                            "endmodule\n"));
    const FormatterReport unchangedInstanceMapReport =
        FormatterService::getInstance()->formatDocument(
            formatterInstanceMapReport.formattedText);
    expectBool("Formatter instance map idempotent",
               unchangedInstanceMapReport.changed,
               false);
    expectEq("Formatter instance map stable text",
             unchangedInstanceMapReport.formattedText,
             formatterInstanceMapReport.formattedText);

    const QString formatterAssociationSuffixInput =
        QStringLiteral("module association_suffix_demo;\n"
                       "child u_child (\n"
                       ".tx_driv_flag(tx_driv_flag[i]), // drive\n"
                       ".stop_bit(tx_stop_bit[i]),\n"
                       ".parity_check(tx_parity_check[i]),\n"
                       ".axi_wr_eff_len(tx_axi_wr_eff_len[i]),\n"
                       ".bypass(bypass) // direct\n"
                       ");\n"
                       "endmodule\n");
    const FormatterReport formatterAssociationSuffixReport =
        FormatterService::getInstance()->formatDocument(
            formatterAssociationSuffixInput);
    expectEq("Formatter aligns association suffix columns",
             formatterAssociationSuffixReport.formattedText,
             QStringLiteral("module association_suffix_demo;\n"
                            "child u_child(\n"
                            "    .tx_driv_flag   ( tx_driv_flag     [i] ), // drive\n"
                            "    .stop_bit       ( tx_stop_bit      [i] ),\n"
                            "    .parity_check   ( tx_parity_check  [i] ),\n"
                            "    .axi_wr_eff_len ( tx_axi_wr_eff_len[i] ),\n"
                            "    .bypass         ( bypass               )  // direct\n"
                            ");\n"
                            "endmodule\n"));
    expectBool("Formatter association suffix token stream invariant",
               StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
                   formatterAssociationSuffixInput,
                   formatterAssociationSuffixReport.formattedText),
               true);
    expectBool("Formatter association suffix idempotent",
               !FormatterService::getInstance()
                    ->formatDocument(
                        formatterAssociationSuffixReport.formattedText)
                    .changed,
               true);

    const QString formatterTrailingCommentInput =
        QStringLiteral("module comment_demo(\n"
                       "input logic clk, // clock\n"
                       "output logic ready // done\n"
                       ");\n"
                       "logic a; // flag\n"
                       "logic [7:0] data; // byte\n"
                       "child u_child (\n"
                       ".clk(clk), // clock\n"
                       ".data_in(data) // bus\n"
                       ");\n"
                       "endmodule\n");
    const FormatterReport formatterTrailingCommentReport =
        FormatterService::getInstance()->formatDocument(
            formatterTrailingCommentInput);
    expectBool("Formatter trailing comment report changed",
               formatterTrailingCommentReport.changed,
               true);
    expectEq("Formatter preserves aligned trailing comments",
             formatterTrailingCommentReport.formattedText,
             QStringLiteral("module comment_demo(\n"
                            "    input  logic clk   , // clock\n"
                            "    output logic ready   // done\n"
                            ");\n"
                            "logic       a   ;  // flag\n"
                            "logic [7:0] data;  // byte\n"
                            "child u_child(\n"
                            "    .clk     ( clk  ), // clock\n"
                            "    .data_in ( data )  // bus\n"
                            ");\n"
                            "endmodule\n"));
    const FormatterReport unchangedTrailingCommentReport =
        FormatterService::getInstance()->formatDocument(
            formatterTrailingCommentReport.formattedText);
    expectBool("Formatter trailing comment idempotent",
               unchangedTrailingCommentReport.changed,
               false);
    expectEq("Formatter trailing comment stable text",
             unchangedTrailingCommentReport.formattedText,
             formatterTrailingCommentReport.formattedText);

    const QString formatterStructuredModuleInput =
        QStringLiteral(
            "  module test#(\n"
            " parameter int P_TEST=1'd1,// 1\n"
            " parameter int P_TEST1=1'd0,// 2\n"
            " parameter logic P_TEST2=P_TEST*P_TEST1 // 3\n"
            " ) (\n"
            " input logic [P_TEST-1:0][2:0] port0 [1:0],// 0\n"
            " axi_if.master bus [P_TEST:P_TEST1], // bus\n"
            " output custom_t [((P_TEST*P_TEST1)-1):0] port333 // 3\n"
            " );\n"
            "endmodule\n");
    const FormatterReport formatterStructuredModuleReport =
        FormatterService::getInstance()->formatDocument(
            formatterStructuredModuleInput);
    const QStringList structuredModuleLines =
        formatterStructuredModuleReport.formattedText.split(
            QLatin1Char('\n'));
    expectBool("Formatter structured module line count",
               structuredModuleLines.size() >= 10,
               true);
    expectEq("Formatter module header spacing",
             structuredModuleLines.value(0),
             QStringLiteral("module test #("));
    expectEq("Formatter parameter to port close",
             structuredModuleLines.value(4),
             QStringLiteral(")("));
    expectEq("Formatter module closing delimiter",
             structuredModuleLines.value(8),
             QStringLiteral(");"));
    expectBool("Formatter canonical packed dimensions",
               structuredModuleLines.value(5).contains(
                   QStringLiteral("[P_TEST - 1:0][2:0]"))
                   && structuredModuleLines.value(7).contains(
                       QStringLiteral(
                           "[((P_TEST * P_TEST1) - 1):0]")),
               true);
    expectBool("Formatter parameter expressions use necessary spaces",
               structuredModuleLines.value(3).contains(
                   QStringLiteral("P_TEST * P_TEST1")),
               true);
    const int parameterEqualColumn =
        structuredModuleLines.value(1).indexOf(QLatin1Char('='));
    const int parameterCommaColumn =
        structuredModuleLines.value(1).indexOf(QLatin1Char(','));
    const int parameterCommentColumn =
        structuredModuleLines.value(1).indexOf(QStringLiteral("//"));
    expectBool("Formatter parameter columns align",
               parameterEqualColumn > 0
                   && structuredModuleLines.value(2).indexOf(
                          QLatin1Char('='))
                       == parameterEqualColumn
                   && structuredModuleLines.value(3).indexOf(
                          QLatin1Char('='))
                       == parameterEqualColumn
                   && structuredModuleLines.value(2).indexOf(
                          QLatin1Char(','))
                       == parameterCommaColumn
                   && structuredModuleLines.value(2).indexOf(
                          QStringLiteral("//"))
                       == parameterCommentColumn
                   && structuredModuleLines.value(3).indexOf(
                          QStringLiteral("//"))
                       == parameterCommentColumn,
               true);
    const int portNameColumn =
        structuredModuleLines.value(5).indexOf(
            QStringLiteral("port0"));
    const int portCommaColumn =
        structuredModuleLines.value(5).indexOf(QLatin1Char(','));
    const int portCommentColumn =
        structuredModuleLines.value(5).indexOf(QStringLiteral("//"));
    expectBool("Formatter ANSI/interface port columns align",
               structuredModuleLines.value(5).startsWith(
                   QStringLiteral("    input"))
                   && structuredModuleLines.value(6).startsWith(
                       QStringLiteral("    axi_if.master"))
                   && structuredModuleLines.value(7).startsWith(
                       QStringLiteral("    output"))
                   && structuredModuleLines.value(6).indexOf(
                          QStringLiteral("bus"))
                       == portNameColumn
                   && structuredModuleLines.value(7).indexOf(
                          QStringLiteral("port333"))
                       == portNameColumn
                   && structuredModuleLines.value(6).indexOf(
                          QLatin1Char(','))
                       == portCommaColumn
                   && structuredModuleLines.value(6).indexOf(
                          QStringLiteral("//"))
                       == portCommentColumn
                   && structuredModuleLines.value(7).indexOf(
                          QStringLiteral("//"))
                       == portCommentColumn,
               true);
    expectBool("Formatter module token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterStructuredModuleInput,
                       formatterStructuredModuleReport.formattedText),
               true);
    const FormatterReport formatterStructuredModuleStable =
        FormatterService::getInstance()->formatDocument(
            formatterStructuredModuleReport.formattedText);
    expectBool("Formatter structured module idempotent",
               !formatterStructuredModuleStable.changed
                   && formatterStructuredModuleStable.formattedText
                       == formatterStructuredModuleReport.formattedText,
               true);

    const QString formatterRecoverableHeaderInput =
        QStringLiteral(
            "\tmodule cpld_like#(\n"
            "\tparameter int WIDTH=8,// width\n"
            "\tparameter logic ENABLE=1'b1 // enable\n"
            "\t)(\n"
            "\tinput\tlogic\tclk,\n"
            "\tinput logic [WIDTH-1:0] data,\n"
            "\tbus_if.master bus,// fabric\n"
            "\toutput logic ready // ready\n"
            "\t);\n"
            "`ifdef BODY_ERROR\n"
            "\t`BODY_MACRO\n"
            "\tassign broken = ;\n"
            "`endif\n"
            "endmodule\n");
    const FormatterReport formatterRecoverableHeaderReport =
        FormatterService::getInstance()->formatDocument(
            formatterRecoverableHeaderInput);
    const QStringList recoverableHeaderLines =
        formatterRecoverableHeaderReport.formattedText.split(
            QLatin1Char('\n'));
    expectBool("Formatter formats reliable header despite body error",
               formatterRecoverableHeaderReport.changed
                   && recoverableHeaderLines.value(0)
                      == QStringLiteral("module cpld_like #(")
                   && recoverableHeaderLines.value(3)
                      == QStringLiteral(")(")
                   && recoverableHeaderLines.value(8)
                      == QStringLiteral(");"),
               true);
    const int recoverablePortNameColumn =
        recoverableHeaderLines.value(4).indexOf(
            QStringLiteral("clk"));
    const int recoverablePortCommaColumn =
        recoverableHeaderLines.value(4).indexOf(
            QLatin1Char(','));
    const int recoverablePortCommentColumn =
        recoverableHeaderLines.value(6).indexOf(
            QStringLiteral("//"));
    expectBool("Formatter recoverable header aligns interface ports",
               recoverablePortNameColumn > 0
                   && recoverableHeaderLines.value(5).indexOf(
                          QStringLiteral("data"))
                      == recoverablePortNameColumn
                   && recoverableHeaderLines.value(6).lastIndexOf(
                          QStringLiteral("bus"))
                      == recoverablePortNameColumn
                   && recoverableHeaderLines.value(7).indexOf(
                          QStringLiteral("ready"))
                      == recoverablePortNameColumn
                   && recoverableHeaderLines.value(5).indexOf(
                          QLatin1Char(','))
                      == recoverablePortCommaColumn
                   && recoverableHeaderLines.value(6).indexOf(
                          QLatin1Char(','))
                      == recoverablePortCommaColumn
                   && recoverableHeaderLines.value(7).indexOf(
                          QStringLiteral("//"))
                      == recoverablePortCommentColumn,
               true);
    bool recoverableHeaderWhitespaceClean = true;
    for (int line = 0; line <= 8; ++line) {
        const QString headerLine = recoverableHeaderLines.value(line);
        recoverableHeaderWhitespaceClean =
            recoverableHeaderWhitespaceClean
            && !headerLine.contains(QLatin1Char('\t'))
            && !headerLine.endsWith(QLatin1Char(' '));
    }
    expectBool("Formatter recoverable header has clean whitespace",
               recoverableHeaderWhitespaceClean,
               true);
    expectBool("Formatter recoverable header token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterRecoverableHeaderInput,
                       formatterRecoverableHeaderReport.formattedText),
               true);
    const FormatterReport formatterRecoverableHeaderStable =
        FormatterService::getInstance()->formatDocument(
            formatterRecoverableHeaderReport.formattedText);
    expectBool("Formatter recoverable header idempotent",
               !formatterRecoverableHeaderStable.changed
                   && formatterRecoverableHeaderStable.formattedText
                      == formatterRecoverableHeaderReport.formattedText,
               true);

    const QString formatterIncompleteHeaderInput =
        QStringLiteral(
            "  module incomplete #(\n"
            "\tparameter int WIDTH=8\n"
            ")(\n"
            "\tinput logic clk,\n"
            "\toutput logic ready\n"
            "  // missing closing delimiter and semicolon\n"
            "endmodule\n");
    const FormatterReport formatterIncompleteHeaderReport =
        FormatterService::getInstance()->formatDocument(
            formatterIncompleteHeaderInput);
    const QString formatterIncompleteHeaderExpected =
        QStringLiteral(
            "  module incomplete #(\n"
            "    parameter int WIDTH=8\n"
            ")(\n"
            "    input logic clk,\n"
            "    output logic ready\n"
            "  // missing closing delimiter and semicolon\n"
            "endmodule\n");
    expectEq("Formatter incomplete header is atomic",
             formatterIncompleteHeaderReport.formattedText,
             formatterIncompleteHeaderExpected);
    expectBool("Formatter incomplete header only normalizes safe whitespace",
               !formatterIncompleteHeaderReport.formattedText.contains(
                   QLatin1Char('\t'))
                   && StructuredWhitespaceFormatter::
                          hasIdenticalNonWhitespaceStream(
                              formatterIncompleteHeaderInput,
                              formatterIncompleteHeaderReport.formattedText),
               true);
    expectBool("Formatter incomplete header normalization is idempotent",
               !FormatterService::getInstance()
                    ->formatDocument(
                        formatterIncompleteHeaderReport.formattedText)
                    .changed,
               true);

    const QString sourceRoot =
        qEnvironmentVariable("ZEROSLACK_SOURCE_DIR");
    QDir formatterFixtureRoot(
        sourceRoot.isEmpty()
            ? QCoreApplication::applicationDirPath()
            : sourceRoot);
    QString cpldTopFixture;
    for (int depth = 0;
         depth < 8 && cpldTopFixture.isEmpty();
         ++depth) {
        const QString candidate =
            formatterFixtureRoot.absoluteFilePath(
                QStringLiteral(
                    "test_sv/new/elec_phy_import/phy/cpld_top.sv"));
        if (QFileInfo::exists(candidate))
            cpldTopFixture = candidate;
        else if (!formatterFixtureRoot.cdUp())
            break;
    }
    QFile cpldTopFile(cpldTopFixture);
    const bool cpldTopOpened =
        !cpldTopFixture.isEmpty()
        && cpldTopFile.open(
            QIODevice::ReadOnly | QFile::Text);
    expectBool("Formatter real cpld_top fixture opens",
               cpldTopOpened,
               true);
    const QString cpldTopInput =
        cpldTopOpened
            ? QString::fromUtf8(cpldTopFile.readAll())
            : QString();
    const FormatterReport cpldTopReport =
        FormatterService::getInstance()->formatDocument(
            cpldTopInput);
    const int cpldHeaderStart =
        cpldTopReport.formattedText.indexOf(
            QStringLiteral("module cpld_top("));
    const int cpldHeaderEnd =
        cpldTopReport.formattedText.indexOf(
            QStringLiteral(");"),
            cpldHeaderStart);
    const QString cpldHeader =
        cpldHeaderStart >= 0 && cpldHeaderEnd >= cpldHeaderStart
            ? cpldTopReport.formattedText.mid(
                  cpldHeaderStart,
                  cpldHeaderEnd - cpldHeaderStart + 2)
            : QString();
    const QStringList cpldHeaderLines =
        cpldHeader.split(QLatin1Char('\n'));
    bool cpldHeaderWhitespaceClean =
        !cpldHeader.isEmpty()
        && !cpldHeader.contains(QLatin1Char('\t'))
        && cpldHeaderLines.first()
               == QStringLiteral("module cpld_top(")
        && cpldHeaderLines.last()
               == QStringLiteral(");");
    for (const QString& line : cpldHeaderLines) {
        cpldHeaderWhitespaceClean =
            cpldHeaderWhitespaceClean
            && !line.endsWith(QLatin1Char(' '));
    }
    expectBool("Formatter real cpld_top header whitespace clean",
               cpldHeaderWhitespaceClean,
               true);
    const auto cpldHeaderLineContaining =
        [&cpldHeaderLines](const QString& text) {
        for (const QString& line : cpldHeaderLines) {
            if (line.contains(text))
                return line;
        }
        return QString();
    };
    const QString cpldClockLine =
        cpldHeaderLineContaining(
            QStringLiteral("clk_main"));
    const QString cpldTypedLine =
        cpldHeaderLineContaining(
            QStringLiteral("chl0_active_inj_layer"));
    const QString cpldInterfaceLine =
        cpldHeaderLineContaining(
            QStringLiteral("s0_oc_ctrl_if"));
    const int cpldNameColumn =
        cpldClockLine.lastIndexOf(
            QStringLiteral("clk_main"));
    expectBool("Formatter real cpld_top port columns align",
               cpldNameColumn > 0
                   && cpldTypedLine.lastIndexOf(
                          QStringLiteral(
                              "chl0_active_inj_layer"))
                      == cpldNameColumn
                   && cpldInterfaceLine.lastIndexOf(
                          QStringLiteral("s0_oc_ctrl_if"))
                      == cpldNameColumn
                   && cpldClockLine.indexOf(
                          QLatin1Char(','))
                      == cpldTypedLine.indexOf(
                          QLatin1Char(','))
                   && cpldClockLine.indexOf(
                          QStringLiteral("//"))
                      == cpldInterfaceLine.indexOf(
                          QStringLiteral("//")),
               true);
    expectBool("Formatter real cpld_top token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       cpldTopInput,
                       cpldTopReport.formattedText),
               true);
    const FormatterReport cpldTopStable =
        FormatterService::getInstance()->formatDocument(
            cpldTopReport.formattedText);
    expectBool("Formatter real cpld_top idempotent",
               !cpldTopStable.changed
                   && cpldTopStable.formattedText
                      == cpldTopReport.formattedText,
               true);

    const QString formatterStructuredInstanceInput =
        QStringLiteral(
            "test_module#(\n"
            ".P_TEST(LP_TEST),// 1\n"
            ".P_TEST1(LP_TEST1),// 2\n"
            ".P_TEST2(LP_TEST2),// 3\n"
            ".P_TEST3333(LP_TEST3333),// 4\n"
            ".P_TEST4()//\n"
            ")U_test_module (\n"
            ".port0(port0),// 0\n"
            ".port1(port1),// 1\n"
            ".port22(port22),\n"
            ".port333(port444),\n"
            ".port444(port444),\n"
            ".port5555(port5555),\n"
            ".port66666(port66666)// 6\n"
            ");\n");
    const QString formatterStructuredInstanceExpected =
        QStringLiteral(
            "test_module #(\n"
            "    .P_TEST     ( LP_TEST     ), // 1\n"
            "    .P_TEST1    ( LP_TEST1    ), // 2\n"
            "    .P_TEST2    ( LP_TEST2    ), // 3\n"
            "    .P_TEST3333 ( LP_TEST3333 ), // 4\n"
            "    .P_TEST4    (             )  //\n"
            ") U_test_module(\n"
            "    .port0     ( port0     ), // 0\n"
            "    .port1     ( port1     ), // 1\n"
            "    .port22    ( port22    ),\n"
            "    .port333   ( port444   ),\n"
            "    .port444   ( port444   ),\n"
            "    .port5555  ( port5555  ),\n"
            "    .port66666 ( port66666 )  // 6\n"
            ");\n");
    const FormatterReport formatterStructuredInstanceReport =
        FormatterService::getInstance()->formatDocument(
            formatterStructuredInstanceInput);
    expectEq("Formatter structured instance target",
             formatterStructuredInstanceReport.formattedText,
             formatterStructuredInstanceExpected);
    expectBool("Formatter instance token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterStructuredInstanceInput,
                       formatterStructuredInstanceReport.formattedText),
               true);
    const FormatterReport formatterStructuredInstanceStable =
        FormatterService::getInstance()->formatDocument(
            formatterStructuredInstanceReport.formattedText);
    expectBool("Formatter structured instance idempotent",
               !formatterStructuredInstanceStable.changed
                   && formatterStructuredInstanceStable.formattedText
                       == formatterStructuredInstanceReport.formattedText,
               true);

    const QString formatterConservativeInput =
        QStringLiteral(
            "module legacy(a,b);\n"
            "input logic a;\n"
            "`ifdef OPTIONAL\n"
            "output logic b;\n"
            "`endif\n"
            "child u_child(\n"
            "  .a(func(\n"
            "      a,\n"
            "      {b, 1'b0}\n"
            "  )),\n"
            "  .b(`OPTIONAL_CONN)\n"
            ");\n"
            "broken child_half(\n"
            "endmodule\n");
    const FormatterReport formatterConservativeReport =
        FormatterService::getInstance()->formatDocument(
            formatterConservativeInput);
    expectBool("Formatter conservative token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterConservativeInput,
                       formatterConservativeReport.formattedText),
               true);
    const FormatterReport formatterConservativeStable =
        FormatterService::getInstance()->formatDocument(
            formatterConservativeReport.formattedText);
    expectBool("Formatter conservative input idempotent",
               !formatterConservativeStable.changed
                   && formatterConservativeStable.formattedText
                       == formatterConservativeReport.formattedText,
               true);

    const QString formatterCaseItemInput =
        QStringLiteral("module case_demo;\n"
                       "always_comb begin\n"
                       "case (sel)\n"
                       "1'b0: y = a; // zero\n"
                       "STATE_LONG: y = b; // long\n"
                       "default: y = c;\n"
                       "endcase\n"
                       "unique casez (mode)\n"
                       "2'b0?: y = a;\n"
                       "default: y = b;\n"
                       "endcase\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterCaseItemReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseItemInput);
    expectBool("Formatter case item report changed",
               formatterCaseItemReport.changed,
               true);
    expectEq("Formatter aligns case items",
             formatterCaseItemReport.formattedText,
             QStringLiteral("module case_demo;\n"
                            "always_comb begin\n"
                            "    case (sel)\n"
                            "        1'b0      : y = a;  // zero\n"
                            "        STATE_LONG: y = b;  // long\n"
                            "        default   : y = c;\n"
                            "    endcase\n"
                            "    unique casez (mode)\n"
                            "        2'b0?  : y = a;\n"
                            "        default: y = b;\n"
                            "    endcase\n"
                            "end\n"
                            "endmodule\n"));
    expectBool("Formatter case label token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterCaseItemInput,
                       formatterCaseItemReport.formattedText),
               true);
    const FormatterReport unchangedCaseItemReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseItemReport.formattedText);
    expectBool("Formatter case item idempotent",
               unchangedCaseItemReport.changed,
               false);
    const QString formatterCaseBodyInput =
        QStringLiteral("module case_body_demo;\n"
                       "always_comb begin\n"
                       "case (state)\n"
                       "IDLE:\n"
                       "next = RUN;\n"
                       "LONG_STATE:\n"
                       "next = DONE;\n"
                       "default:\n"
                       "next = IDLE;\n"
                       "endcase\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterCaseBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseBodyInput);
    expectBool("Formatter case item body report changed",
               formatterCaseBodyReport.changed,
               true);
    expectEq("Formatter indents case item bodies",
             formatterCaseBodyReport.formattedText,
             QStringLiteral("module case_body_demo;\n"
                            "always_comb begin\n"
                            "    case (state)\n"
                            "        IDLE:\n"
                            "            next = RUN;\n"
                            "        LONG_STATE:\n"
                            "            next = DONE;\n"
                            "        default:\n"
                            "            next = IDLE;\n"
                            "    endcase\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedCaseBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseBodyReport.formattedText);
    expectBool("Formatter case item body idempotent",
               unchangedCaseBodyReport.changed,
               false);
    const FormatterReport indentOnlyCaseBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseBodyInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps case item body indentation",
             indentOnlyCaseBodyReport.formattedText,
             QStringLiteral("module case_body_demo;\n"
                            "always_comb begin\n"
                            "    case (state)\n"
                            "        IDLE:\n"
                            "            next = RUN;\n"
                            "        LONG_STATE:\n"
                            "            next = DONE;\n"
                            "        default:\n"
                            "            next = IDLE;\n"
                            "    endcase\n"
                            "end\n"
                            "endmodule\n"));

    const QString formatterNestedCaseBodyInput =
        QStringLiteral("module nested_case_body_demo;\n"
                       "always_comb begin\n"
                       "case (state)\n"
                       "IDLE:\n"
                       "if (enable)\n"
                       "if (ready)\n"
                       "next = RUN;\n"
                       "else\n"
                       "next = WAIT;\n"
                       "else\n"
                       "next = IDLE;\n"
                       "default:\n"
                       "next = IDLE;\n"
                       "endcase\n"
                       "end\n"
                       "endmodule\n");
    const QString formatterNestedCaseBodyExpected =
        QStringLiteral("module nested_case_body_demo;\n"
                       "always_comb begin\n"
                       "    case (state)\n"
                       "        IDLE:\n"
                       "            if (enable)\n"
                       "                if (ready)\n"
                       "                    next = RUN;\n"
                       "                else\n"
                       "                    next = WAIT;\n"
                       "            else\n"
                       "                next = IDLE;\n"
                       "        default:\n"
                       "            next = IDLE;\n"
                       "    endcase\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterNestedCaseBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterNestedCaseBodyInput);
    expectEq("Formatter indents first nested case statement structurally",
             formatterNestedCaseBodyReport.formattedText,
             formatterNestedCaseBodyExpected);
    expectBool("Formatter nested case token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterNestedCaseBodyInput,
                       formatterNestedCaseBodyReport.formattedText),
               true);
    expectBool("Formatter nested case output idempotent",
               !FormatterService::getInstance()
                    ->formatDocument(
                        formatterNestedCaseBodyReport.formattedText)
                    .changed,
               true);

    const QString formatterEnumInput =
        QStringLiteral("module enum_demo;\n"
                       "typedef enum logic [1:0] {\n"
                       "IDLE = 2'd0,\n"
                       "LONG_STATE = 2'd1, // active\n"
                       "DONE\n"
                       "} state_e;\n"
                       "endmodule\n");
    const FormatterReport formatterEnumReport =
        FormatterService::getInstance()->formatDocument(
            formatterEnumInput);
    expectBool("Formatter enum item report changed",
               formatterEnumReport.changed,
               true);
    expectEq("Formatter aligns enum items",
             formatterEnumReport.formattedText,
             QStringLiteral("module enum_demo;\n"
                            "typedef enum logic [1:0] {\n"
                            "    IDLE       = 2'd0,\n"
                            "    LONG_STATE = 2'd1,  // active\n"
                            "    DONE\n"
                            "} state_e;\n"
                            "endmodule\n"));
    const FormatterReport unchangedEnumReport =
        FormatterService::getInstance()->formatDocument(
            formatterEnumReport.formattedText);
    expectBool("Formatter enum item idempotent",
               unchangedEnumReport.changed,
               false);
    const FormatterReport indentOnlyEnumReport =
        FormatterService::getInstance()->formatDocument(
            formatterEnumInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips enum item alignment",
             indentOnlyEnumReport.formattedText,
             QStringLiteral("module enum_demo;\n"
                            "typedef enum logic [1:0] {\n"
                            "    IDLE = 2'd0,\n"
                            "    LONG_STATE = 2'd1, // active\n"
                            "    DONE\n"
                            "} state_e;\n"
                            "endmodule\n"));

    const QString formatterAssignmentInput =
        QStringLiteral("module assign_demo;\n"
                       "assign short = a;\n"
                       "assign very_long_name = b; // output\n"
                       "always_ff @(posedge clk) begin\n"
                       "q <= d;\n"
                       "wide_data[3:0] <= next_data[3:0]; // sample\n"
                       "end\n"
                       "always_comb begin\n"
                       "temp = a == b;\n"
                       "long_temp = temp ? c : d; // combo\n"
                       "if (temp) keep = d;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterAssignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAssignmentInput);
    expectBool("Formatter assignment report changed",
               formatterAssignmentReport.changed,
               true);
    expectEq("Formatter aligns assignment blocks",
             formatterAssignmentReport.formattedText,
             QStringLiteral("module assign_demo;\n"
                            "assign short          = a;\n"
                            "assign very_long_name = b;  // output\n"
                            "always_ff @(posedge clk) begin\n"
                            "    q              <= d             ;\n"
                            "    wide_data[3:0] <= next_data[3:0];  // sample\n"
                            "end\n"
                            "always_comb begin\n"
                            "    temp      = a == b      ;\n"
                            "    long_temp = temp ? c : d;  // combo\n"
                            "    if (temp) keep = d;\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedAssignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAssignmentReport.formattedText);
    expectBool("Formatter assignment idempotent",
               unchangedAssignmentReport.changed,
               false);

    const QString formatterIndexedTernaryInput =
        QStringLiteral("module indexed_ternary_demo;\n"
                       "always_comb begin\n"
                       "bar_reg[SHORT                 ] <= (!w_hs) ? bar_reg[A        ][31:0] : hold_short;\n"
                       "long_signal <= (!w_hs) ? bar_reg[LONG_NAME][31:0] : long_signal;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterIndexedTernaryReport =
        FormatterService::getInstance()->formatDocument(
            formatterIndexedTernaryInput);
    expectEq("Formatter aligns indexed ternary assignments",
             formatterIndexedTernaryReport.formattedText,
             QStringLiteral("module indexed_ternary_demo;\n"
                            "always_comb begin\n"
                            "    bar_reg    [SHORT] <= (!w_hs) ? bar_reg[A][31:0]         : hold_short ;\n"
                            "    long_signal        <= (!w_hs) ? bar_reg[LONG_NAME][31:0] : long_signal;\n"
                            "end\n"
                            "endmodule\n"));
    expectBool("Formatter indexed ternary token stream invariant",
               StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
                   formatterIndexedTernaryInput,
                   formatterIndexedTernaryReport.formattedText),
               true);
    expectBool("Formatter indexed ternary idempotent",
               !FormatterService::getInstance()
                    ->formatDocument(
                        formatterIndexedTernaryReport.formattedText)
                    .changed,
               true);

    const QString formatterSuffixColumnsInput =
        QStringLiteral("module suffix_columns_demo;\n"
                       "always_ff @(posedge clk) begin\n"
                       "rx_step_len[i] <= next_step[i];\n"
                       "rx_frac_step_len[i][j] <= next_frac[index][23:0]; // fraction\n"
                       "tx_stop_bit <= next_stop;\n"
                       "tx_axi_wr_eff_len[SHORT] <= next_eff[LONG_NAME];\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterSuffixColumnsReport =
        FormatterService::getInstance()->formatDocument(
            formatterSuffixColumnsInput);
    expectEq("Formatter aligns assignment suffix columns",
             formatterSuffixColumnsReport.formattedText,
             QStringLiteral("module suffix_columns_demo;\n"
                            "always_ff @(posedge clk) begin\n"
                            "    rx_step_len      [i]     <= next_step[i]          ;\n"
                            "    rx_frac_step_len [i][j]  <= next_frac[index][23:0];  // fraction\n"
                            "    tx_stop_bit              <= next_stop             ;\n"
                            "    tx_axi_wr_eff_len[SHORT] <= next_eff[LONG_NAME]   ;\n"
                            "end\n"
                            "endmodule\n"));
    expectBool("Formatter assignment suffix token stream invariant",
               StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
                   formatterSuffixColumnsInput,
                   formatterSuffixColumnsReport.formattedText),
               true);
    expectBool("Formatter assignment suffix idempotent",
               !FormatterService::getInstance()
                    ->formatDocument(
                        formatterSuffixColumnsReport.formattedText)
                    .changed,
               true);
    const QString formatterSuffixSelectionInput =
        QStringLiteral("    rx_step_len[i] <= next_step[i];\n"
                       "    rx_frac_step_len[i][j] <= next_frac[index][23:0];\n"
                       "    tx_stop_bit <= next_stop;\n"
                       "    tx_axi_wr_eff_len[SHORT] <= next_eff[LONG_NAME];\n");
    const FormatterReport formatterSuffixSelectionReport =
        FormatterService::getInstance()->formatSelection(
            formatterSuffixSelectionInput);
    expectEq("Formatter selection aligns assignment suffix columns",
             formatterSuffixSelectionReport.formattedText,
             QStringLiteral("    rx_step_len      [i]     <= next_step[i]          ;\n"
                            "    rx_frac_step_len [i][j]  <= next_frac[index][23:0];\n"
                            "    tx_stop_bit              <= next_stop             ;\n"
                            "    tx_axi_wr_eff_len[SHORT] <= next_eff[LONG_NAME]   ;\n"));
    expectBool("Formatter suffix selection token stream invariant",
               StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
                   formatterSuffixSelectionInput,
                   formatterSuffixSelectionReport.formattedText),
               true);
    expectBool("Formatter suffix selection idempotent",
               !FormatterService::getInstance()
                    ->formatSelection(
                        formatterSuffixSelectionReport.formattedText)
                    .changed,
               true);

    const QString formatterIncompleteSuffixInput =
        QStringLiteral("module incomplete_suffix_demo;\n"
                       "always_comb begin\n"
                       "data_a[i] = source[\n"
                       "data_long[index] = source_long[j];\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterIncompleteSuffixReport =
        FormatterService::getInstance()->formatDocument(
            formatterIncompleteSuffixInput);
    expectBool("Formatter incomplete suffix token stream invariant",
               StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
                   formatterIncompleteSuffixInput,
                   formatterIncompleteSuffixReport.formattedText),
               true);
    expectBool("Formatter incomplete suffix second pass is stable",
               !FormatterService::getInstance()
                    ->formatDocument(
                        formatterIncompleteSuffixReport.formattedText)
                    .changed,
               true);
    const FormatterReport indentOnlyAssignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAssignmentInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only profile skips assignment alignment",
             indentOnlyAssignmentReport.formattedText,
             QStringLiteral("module assign_demo;\n"
                            "assign short = a;\n"
                            "assign very_long_name = b; // output\n"
                            "always_ff @(posedge clk) begin\n"
                            "    q <= d;\n"
                            "    wide_data[3:0] <= next_data[3:0]; // sample\n"
                            "end\n"
                            "always_comb begin\n"
                            "    temp = a == b;\n"
                            "    long_temp = temp ? c : d; // combo\n"
                            "    if (temp) keep = d;\n"
                            "end\n"
                            "endmodule\n"));

    const QString formatterContinuationInput =
        QStringLiteral("module continuation_demo;\n"
                       "assign out = {\n"
                       "a,\n"
                       "b\n"
                       "};\n"
                       "always_comb begin\n"
                       "result = func(\n"
                       "a,\n"
                       "b\n"
                       ");\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterContinuationInput,
            FormatterProfile::IndentOnly);
    expectBool("Formatter continuation indent report changed",
               formatterContinuationReport.changed,
               true);
    expectEq("Formatter indents continuation lines",
             formatterContinuationReport.formattedText,
             QStringLiteral("module continuation_demo;\n"
                            "assign out = {\n"
                            "    a,\n"
                            "    b\n"
                            "};\n"
                            "always_comb begin\n"
                            "    result = func(\n"
                            "        a,\n"
                            "        b\n"
                            "    );\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterContinuationReport.formattedText,
            FormatterProfile::IndentOnly);
    expectBool("Formatter continuation indent idempotent",
               unchangedContinuationReport.changed,
               false);

    const QString formatterRhsContinuationInput =
        QStringLiteral("module rhs_demo;\n"
                       "always_comb begin\n"
                       "result =\n"
                       "lhs\n"
                       "+ rhs;\n"
                       "call_result =\n"
                       "func(\n"
                       "a,\n"
                       "b\n"
                       ");\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterRhsContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterRhsContinuationInput);
    expectBool("Formatter RHS continuation report changed",
               formatterRhsContinuationReport.changed,
               true);
    expectEq("Formatter indents assignment RHS continuations",
             formatterRhsContinuationReport.formattedText,
             QStringLiteral("module rhs_demo;\n"
                            "always_comb begin\n"
                            "    result =\n"
                            "        lhs\n"
                            "        + rhs;\n"
                            "    call_result =\n"
                            "        func(\n"
                            "             a,\n"
                            "             b\n"
                            "        );\n"
                            "end\n"
                            "endmodule\n"));
    const FormatterReport unchangedRhsContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterRhsContinuationReport.formattedText);
    expectBool("Formatter RHS continuation idempotent",
               unchangedRhsContinuationReport.changed,
               false);
    const FormatterReport indentOnlyRhsContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterRhsContinuationInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps RHS continuation indentation",
             indentOnlyRhsContinuationReport.formattedText,
             QStringLiteral("module rhs_demo;\n"
                            "always_comb begin\n"
                            "    result =\n"
                            "        lhs\n"
                            "        + rhs;\n"
                            "    call_result =\n"
                            "        func(\n"
                            "            a,\n"
                            "            b\n"
                            "        );\n"
                            "end\n"
                            "endmodule\n"));

    const QString formatterCallArgumentInput =
        QStringLiteral("module call_arg_demo;\n"
                       "always_comb begin\n"
                       "result = func(\n"
                       "a,\n"
                       "long_arg\n"
                       ");\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterCallArgumentReport =
        FormatterService::getInstance()->formatDocument(
            formatterCallArgumentInput);
    expectBool("Formatter call argument continuation report changed",
               formatterCallArgumentReport.changed,
               true);
    expectEq("Formatter aligns call argument continuations",
             formatterCallArgumentReport.formattedText,
             QStringLiteral("module call_arg_demo;\n"
                            "always_comb begin\n"
                            "    result = func(\n")
                 + QString(18, QLatin1Char(' '))
                 + QStringLiteral("a,\n")
                 + QString(18, QLatin1Char(' '))
                 + QStringLiteral("long_arg\n"
                                  "    );\n"
                                  "end\n"
                                  "endmodule\n"));
    const FormatterReport unchangedCallArgumentReport =
        FormatterService::getInstance()->formatDocument(
            formatterCallArgumentReport.formattedText);
    expectBool("Formatter call argument continuation idempotent",
               unchangedCallArgumentReport.changed,
               false);
    const FormatterReport indentOnlyCallArgumentReport =
        FormatterService::getInstance()->formatDocument(
            formatterCallArgumentInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips call argument continuation alignment",
             indentOnlyCallArgumentReport.formattedText,
             QStringLiteral("module call_arg_demo;\n"
                            "always_comb begin\n"
                            "    result = func(\n"
                            "        a,\n"
                            "        long_arg\n"
                            "    );\n"
                            "end\n"
                            "endmodule\n"));

    const QString formatterOperatorInput =
        QStringLiteral("module op_demo;\n"
                       "always_comb begin\n"
                       "result = lhs\n"
                       "+ short\n"
                       "- very_long_term\n"
                       "| mask;\n"
                       "assign out = lhs\n"
                       "^ rhs;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterOperatorReport =
        FormatterService::getInstance()->formatDocument(
            formatterOperatorInput);
    expectBool("Formatter continuation operator report changed",
               formatterOperatorReport.changed,
               true);
    expectEq("Formatter aligns continuation operators",
             formatterOperatorReport.formattedText,
             QStringLiteral("module op_demo;\n"
                            "always_comb begin\n"
                            "    result = lhs\n")
                 + QString(13, QLatin1Char(' '))
                 + QStringLiteral("+ short\n")
                 + QString(13, QLatin1Char(' '))
                 + QStringLiteral("- very_long_term\n")
                 + QString(13, QLatin1Char(' '))
                 + QStringLiteral("| mask;\n"
                                  "    assign out = lhs\n")
                 + QString(17, QLatin1Char(' '))
                 + QStringLiteral("^ rhs;\n"
                                  "end\n"
                                  "endmodule\n"));
    const FormatterReport unchangedOperatorReport =
        FormatterService::getInstance()->formatDocument(
            formatterOperatorReport.formattedText);
    expectBool("Formatter continuation operator idempotent",
               unchangedOperatorReport.changed,
               false);
    const FormatterReport indentOnlyOperatorReport =
        FormatterService::getInstance()->formatDocument(
            formatterOperatorInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips continuation operator alignment",
             indentOnlyOperatorReport.formattedText,
             QStringLiteral("module op_demo;\n"
                            "always_comb begin\n"
                            "    result = lhs\n"
                            "    + short\n"
                            "    - very_long_term\n"
                            "    | mask;\n"
                            "    assign out = lhs\n"
                            "    ^ rhs;\n"
                            "end\n"
                            "endmodule\n"));

    const QString formatterTernaryInput =
        QStringLiteral("module ternary_demo;\n"
                       "assign mux = sel\n"
                       "? data_a\n"
                       ": data_b;\n"
                       "always_comb begin\n"
                       "next = enable\n"
                       "? value_a\n"
                       ": value_b;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterTernaryReport =
        FormatterService::getInstance()->formatDocument(
            formatterTernaryInput);
    expectBool("Formatter ternary continuation report changed",
               formatterTernaryReport.changed,
               true);
    expectEq("Formatter aligns ternary continuations",
             formatterTernaryReport.formattedText,
             QStringLiteral("module ternary_demo;\n"
                            "assign mux = sel\n")
                 + QString(13, QLatin1Char(' '))
                 + QStringLiteral("? data_a\n")
                 + QString(13, QLatin1Char(' '))
                 + QStringLiteral(": data_b;\n"
                                  "always_comb begin\n"
                                  "    next = enable\n")
                 + QString(11, QLatin1Char(' '))
                 + QStringLiteral("? value_a\n")
                 + QString(11, QLatin1Char(' '))
                 + QStringLiteral(": value_b;\n"
                                  "end\n"
                                  "endmodule\n"));
    const FormatterReport unchangedTernaryReport =
        FormatterService::getInstance()->formatDocument(
            formatterTernaryReport.formattedText);
    expectBool("Formatter ternary continuation idempotent",
               unchangedTernaryReport.changed,
               false);
    const FormatterReport indentOnlyTernaryReport =
        FormatterService::getInstance()->formatDocument(
            formatterTernaryInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips ternary continuation alignment",
             indentOnlyTernaryReport.formattedText,
             QStringLiteral("module ternary_demo;\n"
                            "assign mux = sel\n"
                            "? data_a\n"
                            ": data_b;\n"
                            "always_comb begin\n"
                            "    next = enable\n"
                            "    ? value_a\n"
                            "    : value_b;\n"
                            "end\n"
                            "endmodule\n"));

    const QString formatterSelectionInput =
        QStringLiteral("    logic a; // flag\n"
                       "    logic [7:0] data; // byte\n");
    const FormatterReport formatterSelectionReport =
        FormatterService::getInstance()->formatSelection(
            formatterSelectionInput);
    expectBool("Formatter selection report changed",
               formatterSelectionReport.changed,
               true);
    expectEq("Formatter selection preserves base indentation",
             formatterSelectionReport.formattedText,
             QStringLiteral("    logic       a   ;  // flag\n"
                            "    logic [7:0] data;  // byte\n"));
    const FormatterReport unchangedSelectionReport =
        FormatterService::getInstance()->formatSelection(
            formatterSelectionReport.formattedText);
    expectBool("Formatter selection idempotent",
               unchangedSelectionReport.changed,
               false);

    const QString formatterDesignUnitInput =
        QStringLiteral(
            "    module unit_top #(\n"
            "            parameter int P=1\n"
            "        )(\n"
            "          input logic clk\n"
            "      );\n"
            "            logic q;\n"
            "            child u_child();\n"
            "        assign q = d;\n"
            "    always_ff @(posedge clk) begin\n"
            "            if (enable) begin\n"
            "                    case (sel)\n"
            "                            default: q <= d;\n"
            "                    endcase\n"
            "            end\n"
            "    end\n"
            "    endmodule\n"
            "    package unit_pkg;\n"
            "            parameter int WIDTH = 8;\n"
            "    typedef logic [WIDTH - 1:0] word_t;\n"
            "        function automatic logic pick(input logic value);\n"
            "                pick = value;\n"
            "        endfunction\n"
            "    endpackage\n"
            "    interface unit_if;\n"
            "            logic req;\n"
            "    modport master(output req);\n"
            "    endinterface\n"
            "    program unit_program;\n"
            "        initial begin\n"
            "                q = '0;\n"
            "        end\n"
            "    endprogram\n");
    const QString formatterDesignUnitExpected =
        QStringLiteral(
            "module unit_top #(\n"
            "    parameter int P = 1\n"
            ")(\n"
            "    input logic clk\n"
            ");\n"
            "logic q;\n"
            "child u_child();\n"
            "assign q = d;\n"
            "always_ff @(posedge clk) begin\n"
            "    if (enable) begin\n"
            "        case (sel)\n"
            "            default: q <= d;\n"
            "        endcase\n"
            "    end\n"
            "end\n"
            "endmodule\n"
            "package unit_pkg;\n"
            "parameter int WIDTH = 8;\n"
            "typedef logic [WIDTH - 1:0] word_t;\n"
            "function automatic logic pick(input logic value);\n"
            "    pick = value;\n"
            "endfunction\n"
            "endpackage\n"
            "interface unit_if;\n"
            "logic req;\n"
            "modport master(output req);\n"
            "endinterface\n"
            "program unit_program;\n"
            "initial begin\n"
            "    q = '0;\n"
            "end\n"
            "endprogram\n");
    const FormatterReport formatterDesignUnitReport =
        FormatterService::getInstance()->formatDocument(
            formatterDesignUnitInput);
    expectEq("Formatter design-unit members start at column zero",
             formatterDesignUnitReport.formattedText,
             formatterDesignUnitExpected);
    expectBool("Formatter design-unit token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterDesignUnitInput,
                       formatterDesignUnitReport.formattedText),
               true);
    const QStringList formatterDesignUnitLines =
        formatterDesignUnitReport.formattedText.split(
            QLatin1Char('\n'));
    auto designUnitLineColumn =
        [&](const QString& prefix) -> int {
            for (const QString& line : formatterDesignUnitLines) {
                if (line.trimmed().startsWith(prefix))
                    return static_cast<int>(
                        line.indexOf(prefix));
            }
            return -1;
        };
    expectBool(
        "Formatter direct members ignore arbitrary original indentation",
        designUnitLineColumn(QStringLiteral("logic q")) == 0
            && designUnitLineColumn(QStringLiteral("child u_child")) == 0
            && designUnitLineColumn(QStringLiteral("assign q")) == 0
            && designUnitLineColumn(QStringLiteral("always_ff")) == 0
            && designUnitLineColumn(QStringLiteral("function automatic")) == 0,
        true);
    expectBool(
        "Formatter parameter and port items keep four-space indentation",
        designUnitLineColumn(QStringLiteral("parameter int P")) == 4
            && designUnitLineColumn(QStringLiteral("input logic clk")) == 4,
        true);
    const FormatterReport formatterDesignUnitIndentOnlyReport =
        FormatterService::getInstance()->formatDocument(
            formatterDesignUnitInput,
            FormatterProfile::IndentOnly);
    const QStringList formatterDesignUnitIndentOnlyLines =
        formatterDesignUnitIndentOnlyReport.formattedText.split(
            QLatin1Char('\n'));
    auto indentOnlyDesignUnitLineColumn =
        [&](const QString& prefix) -> int {
            for (const QString& line :
                 formatterDesignUnitIndentOnlyLines) {
                if (line.trimmed().startsWith(prefix))
                    return static_cast<int>(
                        line.indexOf(prefix));
            }
            return -1;
        };
    expectBool(
        "Formatter indent-only direct members ignore arbitrary indentation",
        indentOnlyDesignUnitLineColumn(QStringLiteral("logic q")) == 0
            && indentOnlyDesignUnitLineColumn(
                   QStringLiteral("child u_child"))
                   == 0
            && indentOnlyDesignUnitLineColumn(
                   QStringLiteral("assign q"))
                   == 0
            && indentOnlyDesignUnitLineColumn(
                   QStringLiteral("always_ff"))
                   == 0
            && indentOnlyDesignUnitLineColumn(
                   QStringLiteral("function automatic"))
                   == 0,
        true);
    expectBool(
        "Formatter indent-only parameter and port indentation",
        indentOnlyDesignUnitLineColumn(
            QStringLiteral("parameter int P"))
                == 4
            && indentOnlyDesignUnitLineColumn(
                   QStringLiteral("input logic clk"))
                   == 4,
        true);
    expectBool("Formatter design-unit formatting idempotent",
               FormatterService::getInstance()
                   ->formatDocument(
                       formatterDesignUnitReport.formattedText)
                   .changed,
               false);

    const QString formatterLexicalTabInput =
        QStringLiteral(
            "\tmodule tab_demo;\t\n"
            "\t\tlogic\tdata;\t\n"
            "\t\tstring\tmessage\t=\t\"left\tright\";\t"
            "// keep\tcomment\n"
            "\tendmodule\t\n");
    const QString formatterLexicalTabIndentOnlySelectionExpected =
        QStringLiteral(
            "    module tab_demo;    \n"
            "    logic    data;    \n"
            "    string    message    =    \"left\tright\";    "
            "// keep\tcomment\n"
            "    endmodule    \n");
    const FormatterReport formatterLexicalTabIndentOnlySelection =
        FormatterService::getInstance()->formatSelection(
            formatterLexicalTabInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter selection expands lexical Tabs by four",
             formatterLexicalTabIndentOnlySelection.formattedText,
             formatterLexicalTabIndentOnlySelectionExpected);
    const QList<FormatterReport> formatterLexicalTabReports{
        FormatterService::getInstance()->formatDocument(
            formatterLexicalTabInput),
        FormatterService::getInstance()->formatDocument(
            formatterLexicalTabInput,
            FormatterProfile::IndentOnly),
        FormatterService::getInstance()->formatSelection(
            formatterLexicalTabInput),
        formatterLexicalTabIndentOnlySelection,
    };
    bool formatterLexicalTabBoundaryOk = true;
    bool formatterLexicalTabInvariantOk = true;
    for (const FormatterReport& report : formatterLexicalTabReports) {
        formatterLexicalTabBoundaryOk =
            formatterLexicalTabBoundaryOk
            && report.formattedText.count(QLatin1Char('\t')) == 2
            && report.formattedText.contains(
                QStringLiteral("\"left\tright\""))
            && report.formattedText.contains(
                QStringLiteral("// keep\tcomment"));
        formatterLexicalTabInvariantOk =
            formatterLexicalTabInvariantOk
            && StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterLexicalTabInput,
                       report.formattedText);
    }
    expectBool("Formatter preserves only string/comment Tabs",
               formatterLexicalTabBoundaryOk,
               true);
    expectBool("Formatter lexical Tab token stream invariant",
               formatterLexicalTabInvariantOk,
               true);
    expectBool("Formatter lexical Tab output idempotent",
               FormatterService::getInstance()
                   ->formatDocument(
                       formatterLexicalTabReports.first().formattedText)
                   .changed,
               false);

    const QString formatterMalformedTabInput =
        QStringLiteral(
            "\tmodule malformed_tabs;\n"
            "logic\tvisible;\n"
            "string message = \"left\tright\n"
            "endmodule\n");
    const QString formatterMalformedTabNormalized =
        StructuredWhitespaceFormatter::
            normalizeLexicalWhitespaceTabs(
                formatterMalformedTabInput, 4);
    expectBool(
        "Formatter malformed syntax converts confirmed lexical Tab",
        formatterMalformedTabNormalized.startsWith(
            QStringLiteral("    module malformed_tabs;")),
        true);
    expectBool("Formatter malformed syntax fallback keeps token stream",
               StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
                   formatterMalformedTabInput,
                   formatterMalformedTabNormalized),
               true);
    expectBool(
        "Formatter malformed string token Tab is conservative",
        formatterMalformedTabNormalized.contains(
            QStringLiteral("\"left\tright")),
        true);
    const QList<FormatterReport> formatterMalformedTabReports{
        FormatterService::getInstance()->formatDocument(
            formatterMalformedTabInput),
        FormatterService::getInstance()->formatDocument(
            formatterMalformedTabInput,
            FormatterProfile::IndentOnly),
        FormatterService::getInstance()->formatSelection(
            formatterMalformedTabInput),
        FormatterService::getInstance()->formatSelection(
            formatterMalformedTabInput,
            FormatterProfile::IndentOnly),
    };
    bool formatterMalformedTabPathsSafe = true;
    bool formatterMalformedTabDiagnosticsVisible = true;
    for (const FormatterReport& report : formatterMalformedTabReports) {
        formatterMalformedTabPathsSafe =
            formatterMalformedTabPathsSafe
            && report.formattedText.contains(
                QStringLiteral("\"left\tright"))
            && StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
                   formatterMalformedTabInput,
                   report.formattedText);
        formatterMalformedTabDiagnosticsVisible =
            formatterMalformedTabDiagnosticsVisible
            && report.outcome
                   == FormatterOutcome::ConservativeFallback
            && !report.diagnostic.isEmpty();
    }
    expectBool("Formatter malformed Tab safety covers document and selection",
               formatterMalformedTabPathsSafe,
               true);
    expectBool("Formatter conservative fallback exposes a deterministic reason",
               formatterMalformedTabDiagnosticsVisible,
               true);

    const QString formatterSemicolonInput =
        QStringLiteral(
            "module semicolon_demo;\n"
            "logic short_flag;\n"
            "logic [7:0] very_long_name = source;\n"
            "logic mid [1:0];\n"
            "assign short = a;\n"
            "assign much_longer = a & b;\n"
            "\n"
            "assign isolated_with_a_much_longer_rhs = a & b & c;\n"
            "// independent barrier\n"
            "assign after_comment = c;\n"
            "`ifdef FEATURE\n"
            "assign branch_short = a;\n"
            "`else\n"
            "assign branch_name_that_is_long = b;\n"
            "`endif\n"
            "always_comb begin\n"
            "    local_value = a;\n"
            "    nested_long_name = b & c;\n"
            "    for (int i = 0; i < 4; i++) begin\n"
            "        local_value = local_value + i;\n"
            "    end\n"
            "end\n"
            "assign multi =\n"
            "    a +\n"
            "    b;\n"
            "endmodule\n");
    const FormatterReport formatterSemicolonReport =
        FormatterService::getInstance()->formatDocument(
            formatterSemicolonInput);
    const QStringList formatterSemicolonLines =
        formatterSemicolonReport.formattedText.split(
            QLatin1Char('\n'));
    auto semicolonColumnForNeedle =
        [&](const QString& needle) -> int {
            for (const QString& line : formatterSemicolonLines) {
                if (line.contains(needle))
                    return static_cast<int>(
                        line.indexOf(QLatin1Char(';')));
            }
            return -1;
        };
    const int declarationSemicolonColumn =
        semicolonColumnForNeedle(QStringLiteral("short_flag"));
    expectBool(
        "Formatter aligns declaration semicolon column",
        declarationSemicolonColumn >= 0
            && declarationSemicolonColumn
                == semicolonColumnForNeedle(
                    QStringLiteral("very_long_name"))
            && declarationSemicolonColumn
                == semicolonColumnForNeedle(
                    QStringLiteral("mid")),
        true);
    const int assignSemicolonColumn =
        semicolonColumnForNeedle(QStringLiteral("assign short"));
    expectBool(
        "Formatter aligns assign semicolon column",
        assignSemicolonColumn >= 0
            && assignSemicolonColumn
                == semicolonColumnForNeedle(
                    QStringLiteral("assign much_longer")),
        true);
    expectBool(
        "Formatter does not align semicolons across barriers",
        assignSemicolonColumn
                < semicolonColumnForNeedle(
                    QStringLiteral(
                        "assign isolated_with_a_much_longer_rhs"))
            && semicolonColumnForNeedle(
                   QStringLiteral("assign after_comment"))
                != semicolonColumnForNeedle(
                    QStringLiteral(
                        "assign branch_name_that_is_long")),
        true);
    expectBool(
        "Formatter leaves for-header semicolons in place",
        formatterSemicolonReport.formattedText.contains(
            QStringLiteral(
                "for (int i = 0; i < 4; i++) begin")),
        true);
    expectBool("Formatter semicolon count preserved",
               formatterSemicolonReport.formattedText.count(
                   QLatin1Char(';'))
                   == formatterSemicolonInput.count(
                       QLatin1Char(';')),
               true);
    expectBool("Formatter semicolon token stream invariant",
               StructuredWhitespaceFormatter::
                   hasIdenticalNonWhitespaceStream(
                       formatterSemicolonInput,
                       formatterSemicolonReport.formattedText),
               true);
    const FormatterReport formatterSemicolonSecondReport =
        FormatterService::getInstance()->formatDocument(
            formatterSemicolonReport.formattedText);
    expectEq("Formatter semicolon formatting stable text",
             formatterSemicolonSecondReport.formattedText,
             formatterSemicolonReport.formattedText);
    expectBool("Formatter semicolon formatting idempotent",
               formatterSemicolonSecondReport.changed,
               false);

    MyCodeEditor ordinaryTabEditor;
    ordinaryTabEditor.setPlainText(QStringLiteral("logic ordinary;\n"));
    QTextCursor ordinaryTabCursor(
        ordinaryTabEditor.document()->findBlockByNumber(0));
    ordinaryTabCursor.setPosition(
        ordinaryTabEditor.document()
            ->findBlockByNumber(0)
            .position());
    ordinaryTabEditor.setTextCursor(ordinaryTabCursor);
    expectBool("Editor ordinary Tab is consumed",
               sendEditorKey(ordinaryTabEditor, Qt::Key_Tab),
               true);
    expectEq("Editor ordinary Tab inserts exactly four spaces",
             ordinaryTabEditor.toPlainText(),
             QStringLiteral("    logic ordinary;\n"));

    MyCodeEditor semanticCommandTabEditor;
    semanticCommandTabEditor.setDocumentFileName(path);
    semanticCommandTabEditor.setPlainText(
        QStringLiteral(
            "module top;\n"
            "assign lhs = ;l enable;\n"
            "endmodule\n"));
    QTextBlock semanticCommandBlock =
        semanticCommandTabEditor.document()->findBlockByNumber(1);
    QTextCursor semanticCommandCursor(semanticCommandBlock);
    semanticCommandCursor.setPosition(
        semanticCommandBlock.position()
        + semanticCommandBlock.text().indexOf(
              QStringLiteral("enable"))
        + QStringLiteral("enable").size());
    semanticCommandTabEditor.setTextCursor(semanticCommandCursor);
    expectBool("Editor legacy ;cmd Tab is ordinary indentation",
               sendEditorKey(
                   semanticCommandTabEditor,
                   Qt::Key_Tab),
               true);
    expectEq("Editor legacy ;cmd remains literal text",
             semanticCommandTabEditor.toPlainText(),
             QStringLiteral(
                 "module top;\n"
                 "assign lhs = ;l enable    ;\n"
                 "endmodule\n"));

    MyCodeEditor templateCommandTabEditor;
    templateCommandTabEditor.setPlainText(
        QStringLiteral(
            "module template_tab;\n"
            ";;l 8 tab_signal\n"
            "endmodule\n"));
    QTextBlock templateCommandBlock =
        templateCommandTabEditor.document()->findBlockByNumber(1);
    QTextCursor templateCommandCursor(templateCommandBlock);
    templateCommandCursor.movePosition(QTextCursor::EndOfBlock);
    templateCommandTabEditor.setTextCursor(templateCommandCursor);
    expectBool("Editor legacy ;;cmd Tab is ordinary indentation",
               sendEditorKey(
                   templateCommandTabEditor,
                   Qt::Key_Tab),
               true);
    expectBool("Editor legacy ;;cmd remains literal text",
               templateCommandTabEditor.toPlainText().contains(
                   QStringLiteral(";;l 8 tab_signal    \n")),
               true);

    MyCodeEditor paletteInsertionEditor;
    paletteInsertionEditor.setPlainText(
        QStringLiteral("module palette_insert;\n    \nendmodule\n"));
    QTextCursor paletteInsertionCursor(
        paletteInsertionEditor.document()->findBlockByNumber(1));
    paletteInsertionCursor.movePosition(QTextCursor::EndOfBlock);
    paletteInsertionEditor.setTextCursor(paletteInsertionCursor);
    const QString paletteSnippet =
        QStringLiteral("always_comb begin\n    next_value\nend");
    CodeTemplateSlotList paletteSlots;
    paletteSlots.append(
        {QStringLiteral("body"),
         static_cast<int>(paletteSnippet.indexOf(
             QStringLiteral("next_value"))),
         static_cast<int>(QStringLiteral("next_value").size()),
         0,
         false});
    QString paletteInsertionFailure;
    expectBool("Ctrl+Space insertion accepts template text",
               paletteInsertionEditor.insertCompletionText(
                   paletteSnippet,
                   -1,
                   0,
                   paletteSlots,
                   &paletteInsertionFailure),
               true);
    expectBool("Ctrl+Space insertion preserves current indentation",
               paletteInsertionEditor.toPlainText().contains(
                   QStringLiteral(
                       "    always_comb begin\n"
                       "        next_value\n"
                       "    end")),
               true);
    expectBool("Ctrl+Space insertion enters slot mode",
               paletteInsertionEditor.templateSlotModeActive()
                   && paletteInsertionEditor.textCursor().selectedText()
                          == QStringLiteral("next_value"),
               true);
    sendEditorKey(paletteInsertionEditor, Qt::Key_Escape);

    const QString wavePreviewInput =
        QStringLiteral("module wave_probe(\n"
                       "    input logic clk,\n"
                       "    input logic rst_n,\n"
                       "    input logic en,\n"
                       "    input logic [7:0] data,\n"
                       "    output logic [7:0] out\n"
                       ");\n"
                       "logic [7:0] q;\n"
                       "logic [7:0] next;\n"
                       "assign out = q + data;\n"
                       "// assign ghost = data;\n"
                       "initial $display(\"ghost <= value\");\n"
                       "always_ff @(posedge clk or negedge rst_n) begin\n"
                       "    if (!rst_n) q <= '0;\n"
                       "    else if (en) q <= data;\n"
                       "end\n"
                       "always_comb begin\n"
                       "    next = q + data;\n"
                       "end\n"
                       "always @(posedge clk) pulse <= en;\n"
                       "endmodule\n");
    const WavePreviewReport waveReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_probe.sv"), wavePreviewInput});
    expectBool("WavePreview report available",
               waveReport.available,
               true);
    expectBool("WavePreview block count",
               waveReport.blocks.size() == 3,
               true);
    expectBool("WavePreview assignment count",
               waveReport.assignmentCount == 5,
               true);
    expectBool("WavePreview no comment/string ghost lane",
               waveLaneNamed(waveReport, QStringLiteral("ghost")) == nullptr,
               true);
    TSDocument waveScopeDocument;
    waveScopeDocument.setText(wavePreviewInput);
    const int firstAlwaysPosition =
        wavePreviewInput.indexOf(QStringLiteral("always_ff"));
    const int combAlwaysPosition =
        wavePreviewInput.indexOf(QStringLiteral("always_comb"));
    const int pulseAlwaysPosition =
        wavePreviewInput.indexOf(QStringLiteral("always @(posedge"));
    const TSAlwaysScopeTarget firstAlwaysScope =
        waveScopeDocument.alwaysScopeTarget(
            wavePreviewInput.indexOf(QStringLiteral("q <= '0")));
    const TSAlwaysScopeTarget combSelectedScope =
        waveScopeDocument.alwaysScopeTarget(combAlwaysPosition,
                                            combAlwaysPosition,
                                            pulseAlwaysPosition);
    const TSAlwaysScopeTarget ambiguousAlwaysScope =
        waveScopeDocument.alwaysScopeTarget(combAlwaysPosition,
                                            combAlwaysPosition,
                                            pulseAlwaysPosition + 12);
    expectBool("TSDocument current always scope",
               firstAlwaysScope.ok()
                   && firstAlwaysScope.startChar == firstAlwaysPosition
                   && firstAlwaysScope.kindText == QStringLiteral("always_ff")
                   && firstAlwaysScope.label.contains(QStringLiteral("always_ff")),
               true);
    expectBool("TSDocument selected always scope trims whitespace",
               combSelectedScope.ok()
                   && combSelectedScope.startChar == combAlwaysPosition
                   && combSelectedScope.kindText == QStringLiteral("always_comb"),
               true);
    expectBool("TSDocument selected always rejects multiple blocks",
               !ambiguousAlwaysScope.ok()
                   && ambiguousAlwaysScope.status
                          == TSAlwaysScopeStatus::AmbiguousSelection,
               true);

    MyCodeEditor waveScopeEditor;
    waveScopeEditor.setPlainText(wavePreviewInput);
    QTextCursor waveScopeCursor(waveScopeEditor.document());
    waveScopeCursor.setPosition(
        wavePreviewInput.indexOf(QStringLiteral("next = q")));
    waveScopeEditor.setTextCursor(waveScopeCursor);
    const EditorAlwaysScopeTarget editorAlwaysScope =
        waveScopeEditor.currentAlwaysScopeTarget();
    WavePreviewQuery editorAlwaysQuery;
    editorAlwaysQuery.fileName = QStringLiteral("wave_probe.sv");
    editorAlwaysQuery.documentText = wavePreviewInput;
    editorAlwaysQuery.scopeStartPosition = editorAlwaysScope.startPosition;
    editorAlwaysQuery.scopeEndPosition = editorAlwaysScope.endPosition;
    editorAlwaysQuery.scopeLabel = editorAlwaysScope.label;
    const WavePreviewReport editorAlwaysReport =
        WavePreviewService::getInstance()->previewForDocument(
            editorAlwaysQuery);
    expectBool("Editor WavePreview always scope report",
               editorAlwaysScope.ok()
                   && editorAlwaysScope.label.contains(
                       QStringLiteral("always_comb"))
                   && editorAlwaysReport.scoped
                   && editorAlwaysReport.scopeLabel == editorAlwaysScope.label
                   && editorAlwaysReport.available
                   && editorAlwaysReport.blocks.size() == 1
                   && editorAlwaysReport.assignmentCount == 1
                   && waveLaneNamed(editorAlwaysReport,
                                    QStringLiteral("next"))
                   && !waveLaneNamed(editorAlwaysReport,
                                     QStringLiteral("q"))
                   && !waveLaneNamed(editorAlwaysReport,
                                     QStringLiteral("out"))
                   && !waveLaneNamed(editorAlwaysReport,
                                     QStringLiteral("pulse")),
               true);

    const QString moduleScopedWaveInput =
        QStringLiteral("module wave_a(\n"
                       "    input logic a,\n"
                       "    output logic y\n"
                       ");\n"
                       "always_comb begin\n"
                       "    y = a;\n"
                       "end\n"
                       "endmodule\n"
                       "\n"
                       "module wave_b(\n"
                       "    input logic b,\n"
                       "    output logic z\n"
                       ");\n"
                       "assign z = b;\n"
                       "endmodule\n");
    TSDocument moduleScopeDocument;
    moduleScopeDocument.setText(moduleScopedWaveInput);
    const int waveAModulePosition =
        moduleScopedWaveInput.indexOf(QStringLiteral("module wave_a"));
    const int waveBModulePosition =
        moduleScopedWaveInput.indexOf(QStringLiteral("module wave_b"));
    const int waveBAssignPosition =
        moduleScopedWaveInput.indexOf(QStringLiteral("assign z"));
    const TSModuleScopeTarget waveBModuleScope =
        moduleScopeDocument.moduleScopeTarget(waveBAssignPosition);
    const TSModuleScopeTarget selectedWaveAModuleScope =
        moduleScopeDocument.moduleScopeTarget(waveAModulePosition,
                                              waveAModulePosition,
                                              waveBModulePosition);
    const TSModuleScopeTarget ambiguousModuleScope =
        moduleScopeDocument.moduleScopeTarget(waveAModulePosition,
                                              waveAModulePosition,
                                              moduleScopedWaveInput.size());
    expectBool("TSDocument current module scope",
               waveBModuleScope.ok()
                   && waveBModuleScope.moduleName == QStringLiteral("wave_b")
                   && waveBModuleScope.label.contains(QStringLiteral("wave_b")),
               true);
    expectBool("TSDocument selected module scope trims whitespace",
               selectedWaveAModuleScope.ok()
                   && selectedWaveAModuleScope.moduleName
                          == QStringLiteral("wave_a")
                   && selectedWaveAModuleScope.startChar
                          == waveAModulePosition,
               true);
    expectBool("TSDocument selected module rejects multiple modules",
               !ambiguousModuleScope.ok()
                   && ambiguousModuleScope.status
                          == TSModuleScopeStatus::AmbiguousSelection,
               true);

    MyCodeEditor moduleScopeEditor;
    moduleScopeEditor.setPlainText(moduleScopedWaveInput);
    QTextCursor moduleScopeCursor(moduleScopeEditor.document());
    moduleScopeCursor.setPosition(waveBAssignPosition);
    moduleScopeEditor.setTextCursor(moduleScopeCursor);
    const EditorModuleScopeTarget editorModuleScope =
        moduleScopeEditor.currentModuleScopeTarget();
    WavePreviewQuery editorModuleQuery;
    editorModuleQuery.fileName = QStringLiteral("module_wave_probe.sv");
    editorModuleQuery.documentText = moduleScopedWaveInput;
    editorModuleQuery.scopeStartPosition = editorModuleScope.startPosition;
    editorModuleQuery.scopeEndPosition = editorModuleScope.endPosition;
    editorModuleQuery.scopeLabel = editorModuleScope.label;
    const WavePreviewReport editorModuleReport =
        WavePreviewService::getInstance()->previewForDocument(
            editorModuleQuery);
    expectBool("Editor WavePreview module scope report",
               editorModuleScope.ok()
                   && editorModuleScope.moduleName == QStringLiteral("wave_b")
                   && editorModuleReport.scoped
                   && editorModuleReport.scopeLabel == editorModuleScope.label
                   && editorModuleReport.available
                   && editorModuleReport.assignmentCount == 1
                   && waveLaneNamed(editorModuleReport,
                                    QStringLiteral("z"))
                   && !waveLaneNamed(editorModuleReport,
                                     QStringLiteral("y")),
               true);
    QWidget wavePreviewPanelHost;
    WavePreviewPanelCoordinator wavePreviewPanel(&wavePreviewPanelHost);
    wavePreviewPanel.refreshFromDocument(QStringLiteral("wave_probe.sv"),
                                         wavePreviewInput,
                                         false,
                                         editorAlwaysScope.startPosition,
                                         editorAlwaysScope.endPosition,
                                         editorAlwaysScope.label);
    QTreeWidget* wavePreviewTree = wavePreviewPanel.tree();
    QTreeWidgetItem* wavePreviewScopeItem =
        wavePreviewTree && wavePreviewTree->topLevelItemCount() > 0
            ? wavePreviewTree->topLevelItem(0)
            : nullptr;
    QTreeWidgetItem* wavePreviewLegendItem =
        wavePreviewTree && wavePreviewTree->topLevelItemCount() > 1
            ? wavePreviewTree->topLevelItem(1)
            : nullptr;
    const QString wavePreviewSketchType =
        wavePreviewScopeItem ? wavePreviewScopeItem->text(2) : QString();
    expectBool("WavePreview panel scope overview",
               wavePreviewScopeItem
                   && wavePreviewScopeItem->text(0) == QStringLiteral("Scope")
                   && wavePreviewScopeItem->text(1) == editorAlwaysScope.label
                   && (wavePreviewSketchType
                           == QStringLiteral("symbolic preview")
                       || wavePreviewSketchType
                           == QStringLiteral("code sketch"))
                   && wavePreviewScopeItem->text(3)
                          .startsWith(QStringLiteral("lines "))
                   && wavePreviewScopeItem->text(4)
                          == QStringLiteral("1 event")
                   && wavePreviewScopeItem->text(5)
                          == QStringLiteral("1 lane")
                   && wavePreviewScopeItem->text(6) == QStringLiteral("-"),
               true);
    expectBool("WavePreview panel legend overview",
               wavePreviewLegendItem
                   && wavePreviewLegendItem->text(0)
                          == QStringLiteral("Legend")
                   && !wavePreviewLegendItem->text(1).isEmpty()
                   && wavePreviewLegendItem->text(3)
                          == QStringLiteral("preview only / no testbench")
                   && wavePreviewLegendItem->text(5)
                          == QStringLiteral("readability"),
               true);
    expectBool("WavePreview canvas leaves legend room",
               wavePreviewPanel.canvas()
                   && wavePreviewPanel.canvas()->sizeHint().height() >= 144,
               true);

    const QString laneCapacityPrefix = QStringLiteral(
        "module lane_capacity;\n"
        "logic clk;\n"
        "logic a, b, c, d, e, f, g, h;\n"
        "always_ff @(posedge clk) begin\n"
        "    a <= b;\n");
    const QString laneCapacityExtra = QStringLiteral(
        "    c <= d;\n"
        "    e <= f;\n"
        "    g <= h;\n");
    const QString laneCapacitySuffix = QStringLiteral(
        "end\n"
        "endmodule\n");
    const QString laneCapacityExpandedText =
        laneCapacityPrefix + laneCapacityExtra + laneCapacitySuffix;
    const QString laneCapacityCompactText =
        laneCapacityPrefix + laneCapacitySuffix;
    QWidget laneCapacityHost;
    WavePreviewPanelCoordinator laneCapacityPanel(&laneCapacityHost);
    laneCapacityPanel.refreshFromDocument(
        QStringLiteral("lane_capacity.sv"),
        laneCapacityExpandedText,
        false,
        0,
        laneCapacityExpandedText.size(),
        QStringLiteral("module lane_capacity"),
        0);
    const int laneCapacityExpandedHeight =
        laneCapacityPanel.canvas()->sizeHint().height();

    DocumentChange laneCapacityShrink;
    laneCapacityShrink.position = laneCapacityPrefix.size();
    laneCapacityShrink.removedLength = laneCapacityExtra.size();
    laneCapacityShrink.removedText = laneCapacityExtra;
    laneCapacityShrink.oldLength = laneCapacityExpandedText.size();
    laneCapacityShrink.newLength = laneCapacityCompactText.size();
    laneCapacityShrink.startLine =
        laneCapacityPrefix.count(QLatin1Char('\n'));
    laneCapacityShrink.startColumn = 0;
    laneCapacityShrink.oldEndLine =
        laneCapacityShrink.startLine
        + laneCapacityExtra.count(QLatin1Char('\n'));
    laneCapacityShrink.newEndLine = laneCapacityShrink.startLine;
    laneCapacityShrink.lineDelta =
        laneCapacityShrink.newEndLine
        - laneCapacityShrink.oldEndLine;
    laneCapacityPanel.applyDocumentChange(
        QStringLiteral("lane_capacity.sv"),
        laneCapacityShrink,
        laneCapacityCompactText,
        true,
        0,
        laneCapacityCompactText.size(),
        QStringLiteral("module lane_capacity"),
        0);
    const int laneCapacityShrunkHeight =
        laneCapacityPanel.canvas()->sizeHint().height();
    const int laneCapacityShrunkAssignments =
        laneCapacityPanel.reportForTest().assignmentCount;
    QTreeWidget* const laneCapacityTree = laneCapacityPanel.tree();
    const bool laneCapacityCompactTreeUpdated =
        laneCapacityTree
        && !laneCapacityTree->findItems(
                QStringLiteral("a"),
                Qt::MatchExactly | Qt::MatchRecursive,
                0).isEmpty()
        && laneCapacityTree->findItems(
                QStringLiteral("c"),
                Qt::MatchExactly | Qt::MatchRecursive,
                0).isEmpty();

    DocumentChange laneCapacityExpand;
    laneCapacityExpand.position = laneCapacityPrefix.size();
    laneCapacityExpand.insertedText = laneCapacityExtra;
    laneCapacityExpand.oldLength = laneCapacityCompactText.size();
    laneCapacityExpand.newLength = laneCapacityExpandedText.size();
    laneCapacityExpand.startLine = laneCapacityShrink.startLine;
    laneCapacityExpand.startColumn = 0;
    laneCapacityExpand.oldEndLine = laneCapacityExpand.startLine;
    laneCapacityExpand.newEndLine =
        laneCapacityExpand.startLine
        + laneCapacityExtra.count(QLatin1Char('\n'));
    laneCapacityExpand.lineDelta =
        laneCapacityExpand.newEndLine
        - laneCapacityExpand.oldEndLine;
    laneCapacityPanel.applyDocumentChange(
        QStringLiteral("lane_capacity.sv"),
        laneCapacityExpand,
        laneCapacityExpandedText,
        true,
        0,
        laneCapacityExpandedText.size(),
        QStringLiteral("module lane_capacity"),
        0);
    const int laneCapacityRestoredHeight =
        laneCapacityPanel.canvas()->sizeHint().height();
    const int laneCapacityRestoredAssignments =
        laneCapacityPanel.reportForTest().assignmentCount;
    const bool laneCapacityExpandedTreeUpdated =
        laneCapacityTree
        && !laneCapacityTree->findItems(
                QStringLiteral("c"),
                Qt::MatchExactly | Qt::MatchRecursive,
                0).isEmpty();
    expectBool("WavePreview canvas/tree same-file edits update atomically",
               laneCapacityExpandedHeight > 144
                   && laneCapacityShrunkAssignments == 1
                   && laneCapacityRestoredAssignments == 4
                   && laneCapacityCompactTreeUpdated
                   && laneCapacityExpandedTreeUpdated
                   && laneCapacityShrunkHeight
                          == laneCapacityExpandedHeight
                   && laneCapacityRestoredHeight
                          == laneCapacityExpandedHeight,
               true);

    laneCapacityPanel.refreshFromDocument(
        QStringLiteral("lane_capacity_other.sv"),
        laneCapacityCompactText,
        false,
        0,
        laneCapacityCompactText.size(),
        QStringLiteral("module lane_capacity"),
        0);
    const int laneCapacityOtherFileHeight =
        laneCapacityPanel.canvas()->sizeHint().height();
    laneCapacityPanel.refreshFromDocument(
        QStringLiteral("lane_capacity.sv"),
        laneCapacityExpandedText,
        false,
        0,
        laneCapacityExpandedText.size(),
        QStringLiteral("module lane_capacity"),
        0);
    const int laneCapacityReloadedHeight =
        laneCapacityPanel.canvas()->sizeHint().height();
    laneCapacityPanel.renderUnavailable(
        QStringLiteral("lane capacity reset"));
    const int laneCapacityClearedHeight =
        laneCapacityPanel.canvas()->sizeHint().height();
    laneCapacityPanel.refreshFromDocument(
        QStringLiteral("lane_capacity.sv"),
        laneCapacityCompactText,
        false,
        0,
        laneCapacityCompactText.size(),
        QStringLiteral("module lane_capacity"),
        0);
    const int laneCapacityAfterClearHeight =
        laneCapacityPanel.canvas()->sizeHint().height();
    std::printf("wave.canvas_lane_capacity.expanded=%d shrunk=%d restored=%d other_file=%d cleared=%d after_clear=%d\n",
                laneCapacityExpandedHeight,
                laneCapacityShrunkHeight,
                laneCapacityRestoredHeight,
                laneCapacityOtherFileHeight,
                laneCapacityClearedHeight,
                laneCapacityAfterClearHeight);
    expectBool("WavePreview canvas lane capacity resets at session boundaries",
               laneCapacityOtherFileHeight < laneCapacityExpandedHeight
                   && laneCapacityReloadedHeight
                          == laneCapacityExpandedHeight
                   && laneCapacityClearedHeight
                          < laneCapacityExpandedHeight
                   && laneCapacityAfterClearHeight
                          == laneCapacityOtherFileHeight,
               true);

    const QString mappedScopeInput = QStringLiteral(
        "// leading line one\n"
        "// leading line two\n"
        "\n"
        "module mapped_wave;\n"
        "logic clk;\n"
        "logic q;\n"
        "always_ff @(posedge clk) begin\n"
        "    q <= 1'b1;\n"
        "end\n"
        "endmodule\n");
    TSDocument mappedScopeDocument;
    mappedScopeDocument.setText(mappedScopeInput);
    const TSModuleScopeTarget mappedModuleScope =
        mappedScopeDocument.moduleScopeTarget(
            mappedScopeInput.indexOf(QStringLiteral("q <=")));
    QWidget mappedWavePanelHost;
    WavePreviewPanelCoordinator mappedWavePanel(&mappedWavePanelHost);
    int mappedNavigationCount = 0;
    int mappedNavigationLine = -1;
    int mappedNavigationColumn = -1;
    QString mappedNavigationFile;
    mappedWavePanel.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            ++mappedNavigationCount;
            mappedNavigationFile = fileName;
            mappedNavigationLine = line;
            mappedNavigationColumn = column;
        });
    mappedWavePanel.refreshFromDocument(
        QStringLiteral("mapped_wave.sv"),
        mappedScopeInput,
        false,
        mappedModuleScope.startChar,
        mappedModuleScope.endChar,
        mappedModuleScope.label,
        mappedModuleScope.startLine);
    const WavePreviewReport mappedInitialReport =
        mappedWavePanel.reportForTest();
    const WavePreviewLane* mappedInitialLane =
        waveLaneNamed(mappedInitialReport, QStringLiteral("q"));
    const bool mappedInitialAssignmentLine = mappedInitialLane
        && mappedInitialLane->assignments.size() == 1
        && mappedInitialLane->assignments.first().line == 8;
    const bool mappedInitialBlockLines =
        mappedInitialReport.blocks.size() == 1
        && mappedInitialReport.blocks.first().startLine == 7
        && mappedInitialReport.blocks.first().endLine == 9;
    std::printf("wave.mapping.initial.scope_target_line=%d report_scope=%d assignment=%d block_start=%d block_end=%d\n",
                mappedModuleScope.startLine,
                mappedInitialReport.scopeStartLine,
                mappedInitialLane && !mappedInitialLane->assignments.isEmpty()
                    ? mappedInitialLane->assignments.first().line
                    : -1,
                !mappedInitialReport.blocks.isEmpty()
                    ? mappedInitialReport.blocks.first().startLine
                    : -1,
                !mappedInitialReport.blocks.isEmpty()
                    ? mappedInitialReport.blocks.first().endLine
                    : -1);
    expectBool("scoped WavePreview maps report lines after leading text",
               mappedModuleScope.ok()
                   && mappedModuleScope.startLine == 3
                   && mappedInitialReport.scopeStartLine == 4
                   && mappedInitialAssignmentLine
                   && mappedInitialBlockLines,
               true);

    const QString insertedLeadingLine = QStringLiteral("// inserted lead\n");
    const QString mappedEditedInput = insertedLeadingLine + mappedScopeInput;
    DocumentChange mappedLeadingChange;
    mappedLeadingChange.position = 0;
    mappedLeadingChange.insertedText = insertedLeadingLine;
    mappedLeadingChange.oldLength = mappedScopeInput.size();
    mappedLeadingChange.newLength = mappedEditedInput.size();
    mappedLeadingChange.startLine = 0;
    mappedLeadingChange.startColumn = 0;
    mappedLeadingChange.oldEndLine = 0;
    mappedLeadingChange.newEndLine = 1;
    mappedLeadingChange.lineDelta = 1;
    mappedWavePanel.resetRefreshMetricsForTest();
    mappedWavePanel.applyDocumentChange(
        QStringLiteral("mapped_wave.sv"),
        mappedLeadingChange,
        mappedEditedInput,
        true,
        mappedModuleScope.startChar + insertedLeadingLine.size(),
        mappedModuleScope.endChar + insertedLeadingLine.size(),
        mappedModuleScope.label,
        mappedModuleScope.startLine + 1);
    const WavePreviewReport mappedEditedReport =
        mappedWavePanel.reportForTest();
    const WavePreviewLane* mappedEditedLane =
        waveLaneNamed(mappedEditedReport, QStringLiteral("q"));
    const WavePreviewAssignment* mappedEditedAssignment =
        mappedEditedLane && mappedEditedLane->assignments.size() == 1
            ? &mappedEditedLane->assignments.first()
            : nullptr;
    const bool mappedEditedBlockLines =
        mappedEditedReport.blocks.size() == 1
        && mappedEditedReport.blocks.first().startLine == 8
        && mappedEditedReport.blocks.first().endLine == 10;
    std::printf("wave.mapping.edited.report_scope=%d assignment=%d block_start=%d block_end=%d delta_updates=%d rebuilds=%d\n",
                mappedEditedReport.scopeStartLine,
                mappedEditedAssignment ? mappedEditedAssignment->line : -1,
                !mappedEditedReport.blocks.isEmpty()
                    ? mappedEditedReport.blocks.first().startLine
                    : -1,
                !mappedEditedReport.blocks.isEmpty()
                    ? mappedEditedReport.blocks.first().endLine
                    : -1,
                mappedWavePanel.refreshMetricsForTest().scopeDeltaUpdateCount,
                mappedWavePanel.refreshMetricsForTest().scopeRebuildCount);
    expectBool("edit before scoped WavePreview remaps global lines exactly",
               mappedEditedReport.scopeStartLine == 5
                   && mappedEditedAssignment
                   && mappedEditedAssignment->line == 9
                   && mappedEditedBlockLines
                   && mappedWavePanel.refreshMetricsForTest()
                          .scopeDeltaUpdateCount == 1
                   && mappedWavePanel.refreshMetricsForTest()
                          .scopeRebuildCount == 0,
               true);
    QTreeWidgetItem* mappedEventItem = nullptr;
    if (QTreeWidget* mappedTree = mappedWavePanel.tree()) {
        for (int index = 0; index < mappedTree->topLevelItemCount(); ++index) {
            QTreeWidgetItem* item = mappedTree->topLevelItem(index);
            if (item && item->text(0) == QStringLiteral("q")
                && item->childCount() == 1) {
                mappedEventItem = item->child(0);
                break;
            }
        }
        if (mappedEventItem)
            mappedTree->itemDoubleClicked(mappedEventItem, 0);
    }
    expectBool("scoped WavePreview navigation uses global one-based line",
               mappedEditedAssignment
                   && mappedNavigationCount == 1
                   && mappedNavigationFile == QStringLiteral("mapped_wave.sv")
                   && mappedNavigationLine == 9
                   && mappedNavigationColumn
                          == mappedEditedAssignment->column,
               true);
    std::printf("wave.mapping.navigation.count=%d line=%d column=%d\n",
                mappedNavigationCount,
                mappedNavigationLine,
                mappedNavigationColumn);

    QWidget unavailableWavePreviewHost;
    WavePreviewPanelCoordinator unavailableWavePreviewPanel(
        &unavailableWavePreviewHost);
    unavailableWavePreviewPanel.renderUnavailable(
        QStringLiteral("Wave Preview unavailable: unsupported selected scope"));
    QLabel* unavailableWavePreviewSummary =
        unavailableWavePreviewPanel.dock()
            ? unavailableWavePreviewPanel.dock()->findChild<QLabel*>(
                  QStringLiteral("wavePreviewSummary"))
            : nullptr;
    expectBool("WavePreview unavailable reason is visible",
               unavailableWavePreviewSummary
                   && unavailableWavePreviewSummary->text().contains(
                       QStringLiteral("unsupported selected scope")),
               true);
    expectBool("WavePreview q lane has two events",
               waveLaneNamed(waveReport, QStringLiteral("q"))
                   && waveLaneNamed(waveReport, QStringLiteral("q"))->assignments.size() == 2,
               true);
    const WavePreviewLane* qSummaryLane =
        waveLaneNamed(waveReport, QStringLiteral("q"));
    expectBool("WavePreview q lane summary",
               qSummaryLane
                   && qSummaryLane->summary.eventCount == 2
                   && qSummaryLane->summary.sourceSignalCount == 1
                   && qSummaryLane->summary.blockCount == 1
                   && qSummaryLane->summary.maxCycleOffset == 1
                   && qSummaryLane->summary.continuousEventCount == 0
                   && qSummaryLane->summary.combinationalEventCount == 0
                   && qSummaryLane->summary.sequentialEventCount == 2
                   && qSummaryLane->summary.guardTexts
                       == QStringList{QStringLiteral("if !rst_n"),
                                      QStringLiteral("if en")}
                   && qSummaryLane->summary.hasSequentialEvent
                   && !qSummaryLane->summary.hasContinuousEvent,
               true);
    const WavePreviewLane* outSummaryLane =
        waveLaneNamed(waveReport, QStringLiteral("out"));
    expectBool("WavePreview continuous lane summary",
               outSummaryLane
                   && outSummaryLane->summary.eventCount == 1
                   && outSummaryLane->summary.sourceSignalCount == 2
                   && outSummaryLane->summary.blockCount == 0
                   && outSummaryLane->summary.maxCycleOffset == 0
                   && outSummaryLane->summary.continuousEventCount == 1
                   && outSummaryLane->summary.combinationalEventCount == 0
                   && outSummaryLane->summary.sequentialEventCount == 0
                   && outSummaryLane->summary.hasContinuousEvent,
               true);
    const WavePreviewSignalContext* dataContext =
        waveContextNamed(waveReport, QStringLiteral("data"));
    expectBool("WavePreview input declaration context",
               dataContext
                   && dataContext->direction == QStringLiteral("input")
                   && dataContext->typeText == QStringLiteral("logic [7:0]"),
               true);
    const WavePreviewSignalContext* outContext =
        waveContextNamed(waveReport, QStringLiteral("out"));
    expectBool("WavePreview output declaration context",
               outContext
                   && outContext->direction == QStringLiteral("output")
                   && outContext->typeText == QStringLiteral("logic [7:0]"),
               true);
    const WavePreviewLane* qLane = waveLaneNamed(waveReport, QStringLiteral("q"));
    expectBool("WavePreview lane carries internal context",
               qLane
                   && qLane->context.direction == QStringLiteral("internal")
                   && qLane->context.typeText == QStringLiteral("logic [7:0]"),
               true);
    const QString waveWarningInput =
        QStringLiteral("module wave_warning;\n"
                       "logic clk;\n"
                       "logic a;\n"
                       "logic q;\n"
                       "assign q = a;\n"
                       "always_ff @(posedge clk) q <= a;\n"
                       "always_comb q = a;\n"
                       "endmodule\n");
    const WavePreviewReport waveWarningReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_warning.sv"), waveWarningInput});
    expectBool("WavePreview lane warning count",
               waveWarningReport.warnings.size() == 2,
               true);
    expectBool("WavePreview warns mixed lane activity",
               waveWarningReport.warnings.contains(
                   QStringLiteral(
                       "signal q mixes assign/comb/seq activity; Wave Preview does not resolve writer priority")),
               true);
    expectBool("WavePreview warns multi-block lane activity",
               waveWarningReport.warnings.contains(
                   QStringLiteral(
                       "signal q is assigned from 2 procedural blocks; inspect block ownership before trusting lane timing")),
               true);
    const WavePreviewLane* warningQLane =
        waveLaneNamed(waveWarningReport, QStringLiteral("q"));
    expectBool("WavePreview lane summary carries warnings",
               warningQLane
                   && warningQLane->summary.warningTexts.size() == 2
                   && warningQLane->summary.warningTexts.contains(
                       QStringLiteral(
                           "signal q mixes assign/comb/seq activity; Wave Preview does not resolve writer priority"))
                   && warningQLane->summary.warningTexts.contains(
                       QStringLiteral(
                           "signal q is assigned from 2 procedural blocks; inspect block ownership before trusting lane timing")),
               true);
    auto scopedNoLaneWaveReport =
        [](const QString& fileName,
           const QString& text,
           const QString& marker) -> WavePreviewReport {
        WavePreviewQuery query;
        query.fileName = fileName;
        query.documentText = text;
        query.scopeStartPosition = text.indexOf(marker);
        query.scopeEndPosition = text.indexOf(QStringLiteral("endmodule"));
        query.scopeLabel = QStringLiteral("selected no-lane always");
        return WavePreviewService::getInstance()->previewForDocument(query);
    };
    auto waveWarningsContain =
        [](const WavePreviewReport& report, const QString& needle) -> bool {
        for (const QString& warning : report.warnings) {
            if (warning.contains(needle))
                return true;
        }
        return false;
    };
    const QString noLaneNeedle =
        QStringLiteral("no recognized assignment lanes");
    const QString waveNoLaneAlwaysFfInput =
        QStringLiteral("module wave_no_lane_ff(input logic clk);\n"
                       "always_ff @(posedge clk) begin\n"
                       "    observe_side_effect();\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveNoLaneAlwaysFfReport =
        scopedNoLaneWaveReport(QStringLiteral("wave_no_lane_ff.sv"),
                               waveNoLaneAlwaysFfInput,
                               QStringLiteral("always_ff"));
    expectBool("WavePreview always_ff no-lane warns",
               !waveNoLaneAlwaysFfReport.available
                   && waveNoLaneAlwaysFfReport.lanes.isEmpty()
                   && waveWarningsContain(waveNoLaneAlwaysFfReport,
                                          noLaneNeedle),
               true);

    const QString waveNoLaneLegacyInput =
        QStringLiteral("module wave_no_lane_legacy(input logic clk);\n"
                       "always @(posedge clk) begin\n"
                       "    $display(\"tick\");\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveNoLaneLegacyReport =
        scopedNoLaneWaveReport(QStringLiteral("wave_no_lane_legacy.sv"),
                               waveNoLaneLegacyInput,
                               QStringLiteral("always @"));
    expectBool("WavePreview legacy always no-lane warns",
               !waveNoLaneLegacyReport.available
                   && waveNoLaneLegacyReport.lanes.isEmpty()
                   && waveWarningsContain(waveNoLaneLegacyReport,
                                          noLaneNeedle),
               true);

    const QString waveNoLaneMacroInput =
        QStringLiteral("`define WAVE_DRIVE_Q q <= d\n"
                       "module wave_no_lane_macro(input logic clk);\n"
                       "always_ff @(posedge clk) begin\n"
                       "    `WAVE_DRIVE_Q\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveNoLaneMacroReport =
        scopedNoLaneWaveReport(QStringLiteral("wave_no_lane_macro.sv"),
                               waveNoLaneMacroInput,
                               QStringLiteral("always_ff"));
    expectBool("WavePreview macro no-lane warns",
               !waveNoLaneMacroReport.available
                   && waveNoLaneMacroReport.lanes.isEmpty()
                   && waveWarningsContain(waveNoLaneMacroReport,
                                          noLaneNeedle),
               true);

    const QString waveNoLaneEmptyInput =
        QStringLiteral("module wave_no_lane_empty;\n"
                       "always @(*) begin\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveNoLaneEmptyReport =
        scopedNoLaneWaveReport(QStringLiteral("wave_no_lane_empty.sv"),
                               waveNoLaneEmptyInput,
                               QStringLiteral("always @"));
    expectBool("WavePreview empty always no-lane warns",
               !waveNoLaneEmptyReport.available
                   && waveNoLaneEmptyReport.lanes.isEmpty()
                   && waveWarningsContain(waveNoLaneEmptyReport,
                                          noLaneNeedle),
               true);
    const QString emptyScopeNeedle =
        QStringLiteral("no recognized Wave Preview process body");
    const QString waveNoLaneCommentInput =
        QStringLiteral("module wave_no_lane_comment(input logic clk, input logic d, output logic q);\n"
                       "/*\n"
                       "always @(posedge clk) begin\n"
                       "    q <= d;\n"
                       "end\n"
                       "*/\n"
                       "endmodule\n");
    WavePreviewQuery waveNoLaneCommentQuery;
    waveNoLaneCommentQuery.fileName = QStringLiteral("wave_no_lane_comment.sv");
    waveNoLaneCommentQuery.documentText = waveNoLaneCommentInput;
    waveNoLaneCommentQuery.scopeStartPosition =
        waveNoLaneCommentInput.indexOf(QStringLiteral("always @"));
    waveNoLaneCommentQuery.scopeEndPosition =
        waveNoLaneCommentInput.indexOf(QStringLiteral("*/"));
    waveNoLaneCommentQuery.scopeLabel =
        QStringLiteral("stale commented always");
    const WavePreviewReport waveNoLaneCommentReport =
        WavePreviewService::getInstance()->previewForDocument(
            waveNoLaneCommentQuery);
    expectBool("WavePreview commented process scope warns",
               !waveNoLaneCommentReport.available
                   && waveNoLaneCommentReport.lanes.isEmpty()
                   && waveWarningsContain(waveNoLaneCommentReport,
                                          emptyScopeNeedle),
               true);

    const QString waveNoLaneMacroScopeInput =
        QStringLiteral("module wave_no_lane_macro_scope(input logic clk);\n"
                       "`SNPS_UNR_CONSTRAINT(\"unsupported constraint\", 1, clk, 1'b1)\n"
                       "endmodule\n");
    WavePreviewQuery waveNoLaneMacroScopeQuery;
    waveNoLaneMacroScopeQuery.fileName =
        QStringLiteral("wave_no_lane_macro_scope.sv");
    waveNoLaneMacroScopeQuery.documentText = waveNoLaneMacroScopeInput;
    waveNoLaneMacroScopeQuery.scopeStartPosition =
        waveNoLaneMacroScopeInput.indexOf(QStringLiteral("`SNPS_UNR_CONSTRAINT"));
    waveNoLaneMacroScopeQuery.scopeEndPosition =
        waveNoLaneMacroScopeInput.indexOf(QStringLiteral("endmodule"));
    waveNoLaneMacroScopeQuery.scopeLabel =
        QStringLiteral("macro process");
    const WavePreviewReport waveNoLaneMacroScopeReport =
        WavePreviewService::getInstance()->previewForDocument(
            waveNoLaneMacroScopeQuery);
    expectBool("WavePreview macro process scope warns",
               !waveNoLaneMacroScopeReport.available
                   && waveNoLaneMacroScopeReport.lanes.isEmpty()
                   && waveWarningsContain(waveNoLaneMacroScopeReport,
                                          emptyScopeNeedle),
               true);
    QList<SemanticSymbolRecord> waveSemanticRecords;
    waveSemanticRecords.append(
        makeSemanticFixtureRecord(QStringLiteral("remote_cfg"),
                                  SymbolTaxonomy::DeclarationKind::Port,
                                  SymbolTaxonomy::CollectorKind::PortInput,
                                  QStringLiteral("remote_top"),
                                  QStringLiteral("logic [3:0]"),
                                  7001,
                                  QStringLiteral("remote_wave.sv")));
    waveSemanticRecords.append(
        makeSemanticFixtureRecord(QStringLiteral("remote_state"),
                                  SymbolTaxonomy::DeclarationKind::Signal,
                                  SymbolTaxonomy::CollectorKind::Logic,
                                  QStringLiteral("remote_top"),
                                  QStringLiteral("logic [5:0]"),
                                  7003,
                                  QStringLiteral("remote_wave.sv")));
    waveSemanticRecords.append(
        makeSemanticFixtureRecord(QStringLiteral("local_cfg"),
                                  SymbolTaxonomy::DeclarationKind::Signal,
                                  SymbolTaxonomy::CollectorKind::Logic,
                                  QStringLiteral("semantic_wave"),
                                  QStringLiteral("logic [9:0]"),
                                  7002,
                                  QStringLiteral("semantic_wave.sv")));
    const auto waveSemanticSnapshot =
        sharedSnapshotFromRecords(waveSemanticRecords);
    const QString waveSemanticInput =
        QStringLiteral("module semantic_wave;\n"
                       "logic [1:0] local_cfg;\n"
                       "logic [3:0] out;\n"
                       "assign out = remote_cfg + remote_state + local_cfg;\n"
                       "endmodule\n");
    const WavePreviewReport waveSemanticReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("semantic_wave.sv"),
             waveSemanticInput,
             waveSemanticSnapshot});
    const WavePreviewSignalContext* remoteCfgContext =
        waveContextNamed(waveSemanticReport, QStringLiteral("remote_cfg"));
    expectBool("WavePreview semantic source context",
               remoteCfgContext
                   && remoteCfgContext->direction == QStringLiteral("input")
                   && remoteCfgContext->typeText == QStringLiteral("logic [3:0]")
                   && remoteCfgContext->line == 7001,
               true);
    const WavePreviewSignalContext* remoteStateContext =
        waveContextNamed(waveSemanticReport, QStringLiteral("remote_state"));
    expectBool("WavePreview semantic internal source context",
               remoteStateContext
                   && remoteStateContext->direction == QStringLiteral("internal")
                   && remoteStateContext->typeText == QStringLiteral("logic [5:0]")
                   && remoteStateContext->line == 7003,
               true);
    const WavePreviewSignalContext* localCfgContext =
        waveContextNamed(waveSemanticReport, QStringLiteral("local_cfg"));
    expectBool("WavePreview local context beats semantic snapshot",
               localCfgContext
                   && localCfgContext->direction == QStringLiteral("internal")
                   && localCfgContext->typeText == QStringLiteral("logic [1:0]"),
               true);
    expectBool("WavePreview reset guard label",
               qLane
                   && qLane->assignments.size() > 0
                   && qLane->assignments.first().guardText == QStringLiteral("if !rst_n"),
               true);
    expectBool("WavePreview enable guard label",
               qLane
                   && qLane->assignments.size() > 1
                   && qLane->assignments.at(1).guardText == QStringLiteral("if en"),
               true);
    expectBool("WavePreview always_ff block kind",
               !waveReport.blocks.isEmpty()
                   && waveReport.blocks.first().kind == WavePreviewBlockKind::AlwaysFf
                   && waveReport.blocks.first().assignmentCount == 2,
               true);
    expectList("WavePreview always_ff clock signals",
               !waveReport.blocks.isEmpty()
                   ? waveReport.blocks.first().clockSignals
                   : QStringList(),
               {QStringLiteral("clk")});
    expectList("WavePreview always_ff reset signals",
               !waveReport.blocks.isEmpty()
                   ? waveReport.blocks.first().resetSignals
                   : QStringList(),
               {QStringLiteral("rst_n")});
    expectList("WavePreview always_ff clock edges",
               !waveReport.blocks.isEmpty()
                   ? waveEdgeLabels(waveReport.blocks.first().clockEdgeSignals)
                   : QStringList(),
               {QStringLiteral("posedge clk")});
    expectList("WavePreview always_ff reset edges",
               !waveReport.blocks.isEmpty()
                   ? waveEdgeLabels(waveReport.blocks.first().resetEdgeSignals)
                   : QStringList(),
               {QStringLiteral("negedge rst_n")});
    expectBool("WavePreview always_comb block kind",
               waveReport.blocks.size() > 1
                   && waveReport.blocks.at(1).kind == WavePreviewBlockKind::AlwaysComb
                   && waveReport.blocks.at(1).assignmentCount == 1,
               true);
    expectBool("WavePreview clocked always block kind",
               waveReport.blocks.size() > 2
                   && waveReport.blocks.at(2).kind == WavePreviewBlockKind::AlwaysClocked
                   && waveReport.blocks.at(2).assignmentCount == 1,
               true);
    const WavePreviewAssignment* outAssign =
        firstWaveAssignment(waveReport, QStringLiteral("out"));
    expectBool("WavePreview continuous assignment kind",
               outAssign
                   && outAssign->kind == WavePreviewAssignmentKind::Continuous
                   && outAssign->cycleOffset == 0,
               true);
    expectList("WavePreview continuous sources",
               outAssign ? outAssign->sourceSignals : QStringList(),
               {QStringLiteral("q"), QStringLiteral("data")});
    const WavePreviewAssignment* qAssign =
        firstWaveAssignment(waveReport, QStringLiteral("q"));
    expectBool("WavePreview sequential cycle offset",
               qAssign
                   && qAssign->kind == WavePreviewAssignmentKind::NonBlocking
                   && qAssign->cycleOffset == 1
                   && qAssign->trigger.contains(QStringLiteral("posedge clk")),
               true);
    const WavePreviewAssignment* nextAssign =
        firstWaveAssignment(waveReport, QStringLiteral("next"));
    expectBool("WavePreview combinational cycle offset",
               nextAssign
                   && nextAssign->kind == WavePreviewAssignmentKind::Blocking
                   && nextAssign->cycleOffset == 0,
               true);
    expectList("WavePreview combinational sources",
               nextAssign ? nextAssign->sourceSignals : QStringList(),
               {QStringLiteral("q"), QStringLiteral("data")});
    const WavePreviewLane* nextSummaryLane =
        waveLaneNamed(waveReport, QStringLiteral("next"));
    expectBool("WavePreview blocking lane activity count",
               nextSummaryLane
                   && nextSummaryLane->summary.combinationalEventCount == 1
                   && nextSummaryLane->summary.sequentialEventCount == 0
                   && nextSummaryLane->summary.continuousEventCount == 0,
               true);
    const WavePreviewAssignment* pulseAssign =
        firstWaveAssignment(waveReport, QStringLiteral("pulse"));
    expectBool("WavePreview plain clocked always",
               pulseAssign
                   && pulseAssign->cycleOffset == 1
                   && pulseAssign->sourceSignals.contains(QStringLiteral("en")),
               true);
    expectBool("WavePreview clock reset groups",
               waveReport.clockResetGroups.size() == 2
                   && waveReport.clockResetGroups.first().clockSignals
                       == QStringList{QStringLiteral("clk")}
                   && waveReport.clockResetGroups.first().resetSignals
                       == QStringList{QStringLiteral("rst_n")}
                   && waveEdgeLabels(
                          waveReport.clockResetGroups.first().clockEdgeSignals)
                       == QStringList{QStringLiteral("posedge clk")}
                   && waveEdgeLabels(
                          waveReport.clockResetGroups.first().resetEdgeSignals)
                       == QStringList{QStringLiteral("negedge rst_n")}
                   && waveReport.clockResetGroups.first().assignmentCount == 2,
               true);

    const QString waveCasePreviewInput =
        QStringLiteral("module wave_case;\n"
                       "logic [1:0] sel;\n"
                       "logic a;\n"
                       "logic b;\n"
                       "logic y;\n"
                       "always_comb begin\n"
                       "    case (sel)\n"
                       "    2'b00: y = a;\n"
                       "    default: y = b;\n"
                       "    endcase\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveCaseReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_case.sv"), waveCasePreviewInput});
    const WavePreviewLane* yLane =
        waveLaneNamed(waveCaseReport, QStringLiteral("y"));
    expectBool("WavePreview case item guard label",
               yLane
                   && yLane->assignments.size() > 0
                   && yLane->assignments.first().guardText
                       == QStringLiteral("case sel: 2'b00"),
               true);
    expectBool("WavePreview default case guard label",
               yLane
                   && yLane->assignments.size() > 1
                   && yLane->assignments.at(1).guardText
                       == QStringLiteral("case sel: default"),
               true);
    expectBool("WavePreview case lane guard summary",
               yLane
                   && yLane->summary.guardTexts
                       == QStringList{QStringLiteral("case sel: 2'b00"),
                                      QStringLiteral("case sel: default")},
               true);

    const QString waveLoopPreviewInput =
        QStringLiteral("module wave_loop;\n"
                       "logic en;\n"
                       "logic [1:0] i;\n"
                       "logic [7:0] data [4];\n"
                       "logic [7:0] next;\n"
                       "logic [7:0] hold;\n"
                       "logic [7:0] q;\n"
                       "always_comb begin\n"
                       "    for (int j = 0; j < 4; j++) begin\n"
                       "        q = data[j];\n"
                       "    end\n"
                       "    foreach (data[i]) q = data[i];\n"
                       "    while (en) q = next;\n"
                       "    repeat (3) q = hold;\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveLoopReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_loop.sv"), waveLoopPreviewInput});
    const WavePreviewLane* loopLane =
        waveLaneNamed(waveLoopReport, QStringLiteral("q"));
    expectBool("WavePreview loop guard labels",
               loopLane
                   && loopLane->assignments.size() == 4
                   && loopLane->assignments.at(0).guardText
                       == QStringLiteral("for int j = 0; j < 4; j++")
                   && loopLane->assignments.at(1).guardText
                       == QStringLiteral("foreach data[i]")
                   && loopLane->assignments.at(2).guardText
                       == QStringLiteral("while en")
                   && loopLane->assignments.at(3).guardText
                       == QStringLiteral("repeat 3"),
               true);
    expectBool("WavePreview loop lane guard summary",
               loopLane
                   && loopLane->summary.guardTexts.size() == 4
                   && loopLane->summary.guardTexts.first()
                       == QStringLiteral("for int j = 0; j < 4; j++")
                   && loopLane->summary.guardTexts.last()
                       == QStringLiteral("repeat 3"),
               true);

    const QString waveTernaryPreviewInput =
        QStringLiteral("module wave_ternary;\n"
                       "logic en;\n"
                       "logic sel;\n"
                       "logic a;\n"
                       "logic b;\n"
                       "logic out;\n"
                       "logic next;\n"
                       "assign out = sel ? a : b;\n"
                       "always_comb begin\n"
                       "    if (en) next = sel ? a : b;\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveTernaryReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_ternary.sv"), waveTernaryPreviewInput});
    const WavePreviewAssignment* ternaryOutAssign =
        firstWaveAssignment(waveTernaryReport, QStringLiteral("out"));
    expectBool("WavePreview continuous ternary guard label",
               ternaryOutAssign
                   && ternaryOutAssign->guardText == QStringLiteral("?: sel"),
               true);
    const WavePreviewAssignment* ternaryNextAssign =
        firstWaveAssignment(waveTernaryReport, QStringLiteral("next"));
    expectBool("WavePreview combines if and ternary guards",
               ternaryNextAssign
                   && ternaryNextAssign->guardText
                       == QStringLiteral("if en && ?: sel"),
               true);
    const WavePreviewLane* ternaryNextLane =
        waveLaneNamed(waveTernaryReport, QStringLiteral("next"));
    expectBool("WavePreview ternary lane guard summary",
               ternaryNextLane
                   && ternaryNextLane->summary.guardTexts
                       == QStringList{QStringLiteral("if en && ?: sel")},
               true);

    const QString rstGenPath =
        QFileInfo(path).dir().absoluteFilePath(
            QStringLiteral("new/elec_phy_import/top/rst_gen.v"));
    QFile rstGenFile(rstGenPath);
    const bool rstGenOpened =
        rstGenFile.open(QIODevice::ReadOnly | QFile::Text);
    expectBool("WavePreview rst_gen fixture opens",
               rstGenOpened,
               true);
    QString rstGenInput;
    if (rstGenOpened) {
        rstGenInput = QTextStream(&rstGenFile).readAll();
        rstGenFile.close();
    }
    const WavePreviewReport rstGenReport =
        WavePreviewService::getInstance()->previewForDocument(
            {rstGenPath, rstGenInput});
    expectBool("WavePreview rst_gen full file shape",
               rstGenReport.available
                   && rstGenReport.blocks.size() == 2
                   && rstGenReport.assignmentCount == 6
                   && waveLaneNamed(rstGenReport, QStringLiteral("srst_o"))
                   && waveLaneNamed(rstGenReport, QStringLiteral("srst_n_o"))
                   && waveLaneNamed(rstGenReport, QStringLiteral("cnt"))
                   && waveLaneNamed(rstGenReport, QStringLiteral("srst")),
               true);
    const WavePreviewTraceSignal* rstCntTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("cnt"));
    const WavePreviewTraceSignal* rstSrstTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("srst"));
    const WavePreviewTraceSignal* rstSrstOTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("srst_o"));
    const WavePreviewTraceSignal* rstSrstNOTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("srst_n_o"));
    expectBool("WavePreview rst_gen local waveform trace",
               rstGenReport.trace.isValid()
                   && rstCntTrace
                   && rstCntTrace->values
                       == QStringList{QStringLiteral("1"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0")}
                   && rstSrstTrace
                   && rstSrstTrace->values
                       == QStringList{QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0")}
                   && rstSrstOTrace
                   && rstSrstOTrace->values == rstSrstTrace->values
                   && rstSrstNOTrace
                   && rstSrstNOTrace->values
                       == QStringList{QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1")},
               true);

    const int firstRstAlways =
        rstGenInput.indexOf(QStringLiteral("always@"));
    const int secondRstAlways =
        firstRstAlways >= 0
            ? rstGenInput.indexOf(QStringLiteral("always@"), firstRstAlways + 1)
            : -1;
    const int rstEndmodule =
        secondRstAlways >= 0
            ? rstGenInput.indexOf(QStringLiteral("endmodule"), secondRstAlways)
            : -1;
    WavePreviewQuery rstSecondAlwaysQuery;
    rstSecondAlwaysQuery.fileName = rstGenPath;
    rstSecondAlwaysQuery.documentText = rstGenInput;
    rstSecondAlwaysQuery.scopeStartPosition = secondRstAlways;
    rstSecondAlwaysQuery.scopeEndPosition = rstEndmodule;
    rstSecondAlwaysQuery.scopeLabel = QStringLiteral("selected rst always");
    const WavePreviewReport rstSecondAlwaysReport =
        WavePreviewService::getInstance()->previewForDocument(
            rstSecondAlwaysQuery);
    const WavePreviewLane* rstSelectedSrstLane =
        waveLaneNamed(rstSecondAlwaysReport, QStringLiteral("srst"));
    expectBool("WavePreview rst_gen selected always scope",
               rstSecondAlwaysReport.scoped
                   && rstSecondAlwaysReport.scopeLabel
                       == QStringLiteral("selected rst always")
                   && rstSecondAlwaysReport.available
                   && rstSecondAlwaysReport.blocks.size() == 1
                   && rstSecondAlwaysReport.assignmentCount == 2
                   && rstSelectedSrstLane
                   && rstSelectedSrstLane->assignments.size() == 2
                   && rstSelectedSrstLane->summary.sequentialEventCount == 2
                   && !waveLaneNamed(rstSecondAlwaysReport,
                                     QStringLiteral("cnt"))
                   && !waveLaneNamed(rstSecondAlwaysReport,
                                     QStringLiteral("srst_o"))
                   && !waveLaneNamed(rstSecondAlwaysReport,
                                     QStringLiteral("srst_n_o")),
               true);
    const WavePreviewTraceSignal* rstSelectedTrace =
        waveTraceSignalNamed(rstSecondAlwaysReport, QStringLiteral("srst"));
    expectBool("WavePreview rst_gen selected always waveform",
               rstSecondAlwaysReport.trace.isValid()
                   && rstSelectedTrace
                   && rstSelectedTrace->values
                       == QStringList{QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1")},
               true);

    const QString newProjectRoot =
        QFileInfo(path).dir().absoluteFilePath(QStringLiteral("new"));
    const QString cpldPreprocPath =
        QDir(newProjectRoot).absoluteFilePath(
            QStringLiteral("elec_phy_import/phy/cpld_preproc.sv"));
    QFile cpldPreprocFile(cpldPreprocPath);
    const bool cpldPreprocOpened =
        cpldPreprocFile.open(QIODevice::ReadOnly | QFile::Text);
    expectBool("WavePreview cpld_preproc fixture opens",
               cpldPreprocOpened,
               true);
    QString cpldPreprocInput;
    if (cpldPreprocOpened) {
        cpldPreprocInput = QTextStream(&cpldPreprocFile).readAll();
        cpldPreprocFile.close();
    }
    const WavePreviewReport cpldPreprocReport =
        WavePreviewService::getInstance()->previewForDocument(
            {cpldPreprocPath, cpldPreprocInput});
    expectBool("WavePreview cpld_preproc complex static preview",
               cpldPreprocReport.available
                   && cpldPreprocReport.blocks.size() >= 10
                   && cpldPreprocReport.assignmentCount >= 20
                   && cpldPreprocReport.lanes.size() >= 10,
               true);

    QDirIterator waveProjectFiles(
        newProjectRoot,
        QStringList{QStringLiteral("*.v"),
                    QStringLiteral("*.sv"),
                    QStringLiteral("*.svh")},
        QDir::Files,
        QDirIterator::Subdirectories);
    int waveProjectFileCount = 0;
    int waveProjectAvailableReports = 0;
    int waveProjectAssignments = 0;
    int waveProjectBlocks = 0;
    int waveProjectLanes = 0;
    QStringList waveProjectOpenFailures;
    while (waveProjectFiles.hasNext()) {
        const QString projectFilePath = waveProjectFiles.next();
        QFile projectFile(projectFilePath);
        if (!projectFile.open(QIODevice::ReadOnly | QFile::Text)) {
            waveProjectOpenFailures.append(projectFilePath);
            continue;
        }
        const QString projectText = QTextStream(&projectFile).readAll();
        projectFile.close();
        ++waveProjectFileCount;

        const WavePreviewReport projectReport =
            WavePreviewService::getInstance()->previewForDocument(
                {projectFilePath, projectText});
        if (projectReport.available)
            ++waveProjectAvailableReports;
        waveProjectAssignments += projectReport.assignmentCount;
        waveProjectBlocks += projectReport.blocks.size();
        waveProjectLanes += projectReport.lanes.size();
    }
    expectBool("WavePreview scans full test_sv/new project",
               waveProjectOpenFailures.isEmpty()
                   && waveProjectFileCount >= 25
                   && waveProjectAvailableReports >= 10
                   && waveProjectAssignments >= 50
                   && waveProjectBlocks >= 20
                   && waveProjectLanes >= 20,
               true);

    const QString planRoot =
        QDir::current().absoluteFilePath(QStringLiteral("test_sv/huge_plan"));
    const QString planA = QDir(planRoot).absoluteFilePath(QStringLiteral("a.sv"));
    const QString planB = QDir(planRoot).absoluteFilePath(QStringLiteral("b.sv"));
    const QString planC = QDir(planRoot).absoluteFilePath(QStringLiteral("c.sv"));
    const QString planD = QDir(planRoot).absoluteFilePath(QStringLiteral("d.sv"));
    ProjectSnapshot planProject;
    planProject.workspaceRoot = planRoot;
    planProject.systemVerilogFiles = {planA, planB, planC, planD};

    DocumentSnapshot dirtyOpen;
    dirtyOpen.fileName = planB;
    dirtyOpen.dirty = true;
    DocumentSnapshot cleanOpen;
    cleanOpen.fileName = planD;
    cleanOpen.dirty = false;

    WorkspaceAnalysisPlan priorityPlan =
        WorkspaceAnalysisPlanService::getInstance()->planForWorkspace(
            {planProject, QDir::toNativeSeparators(planC), {dirtyOpen, cleanOpen}});
    expectEq("Workspace plan prioritizes active/open files",
             priorityPlan.project.systemVerilogFiles.join(QStringLiteral("|")),
             QStringList{planC, planB, planD, planA}.join(QStringLiteral("|")));
    expectEq("Workspace plan classifies dirty open files",
             priorityPlan.dirtyOpenFiles.join(QStringLiteral("|")),
             QStringList{planB}.join(QStringLiteral("|")));
    expectBool("Workspace plan counts priority files",
               priorityPlan.priorityFileCount == 3
                   && priorityPlan.backgroundFileCount == 1
                   && priorityPlan.openFiles.size() == 2
                   && priorityPlan.dirtyOpenFiles.size() == 1
                   && priorityPlan.currentFileInWorkspace,
               true);
    expectEq("Workspace plan records priority bands",
             QStringList{
                 priorityPlan.currentFilePriorityFiles.join(QStringLiteral(",")),
                 priorityPlan.dirtyOpenPriorityFiles.join(QStringLiteral(",")),
                 priorityPlan.cleanOpenPriorityFiles.join(QStringLiteral(",")),
                 priorityPlan.backgroundFiles.join(QStringLiteral(","))
             }.join(QStringLiteral("|")),
             QStringList{planC, planB, planD, planA}.join(QStringLiteral("|")));
    expectEq("Workspace plan indexes file bands",
             QStringList{
                 priorityPlan.bandForFile(QDir::toNativeSeparators(planC)),
                 priorityPlan.bandForFile(planB),
                 priorityPlan.bandForFile(planD),
                 priorityPlan.bandForFile(planA)
             }.join(QStringLiteral("|")),
             QStringLiteral("current|dirty-open|open|background"));
    expectBool("Workspace plan band index covers workspace files",
               priorityPlan.fileBandsByNormalizedPath.size() == 4,
               true);
    QStringList bandSummaryText;
    for (const WorkspaceAnalysisBandSummary& summary :
         priorityPlan.bandSummaries) {
        bandSummaryText.append(
            QStringLiteral("%1:%2:%3:%4:%5")
                .arg(summary.label,
                     summary.displayName,
                     QString::number(summary.fileCount),
                     summary.priority ? QStringLiteral("priority")
                                      : QStringLiteral("background"),
                     QString::number(summary.publicationCheckpoint)));
    }
    expectEq("Workspace plan records band summaries",
             bandSummaryText.join(QStringLiteral("|")),
             QStringLiteral("current:current:1:priority:1|"
                            "dirty-open:dirty:1:priority:2|"
                            "open:open:1:priority:3|"
                            "background:background:1:background:0"));
    expectEq("Workspace plan formats band summary text",
             priorityPlan.bandSummaryText(),
             QStringLiteral("bands current 1, dirty 1, open 1, background 1"));
    QStringList bandMetadataText;
    for (const QString& fileName : {planC, planB, planD, planA}) {
        const WorkspaceAnalysisFileBandMetadata metadata =
            priorityPlan.bandMetadataForFile(fileName);
        bandMetadataText.append(
            QStringLiteral("%1:%2:%3:%4")
                .arg(metadata.label,
                     metadata.displayName,
                     metadata.priority ? QStringLiteral("priority")
                                       : QStringLiteral("background"),
                     QString::number(metadata.publicationCheckpoint)));
    }
    expectEq("Workspace plan exposes file band metadata",
             bandMetadataText.join(QStringLiteral("|")),
             QStringLiteral("current:current:priority:1|"
                            "dirty-open:dirty:priority:2|"
                            "open:open:priority:3|"
                            "background:background:background:0"));
    QHash<QString, SemanticAnalysisBandMetadata> semanticBandMetadata;
    for (auto it = priorityPlan.fileBandMetadataByNormalizedPath.constBegin();
         it != priorityPlan.fileBandMetadataByNormalizedPath.constEnd();
         ++it) {
        const WorkspaceAnalysisFileBandMetadata planMetadata = it.value();
        SemanticAnalysisBandMetadata metadata;
        metadata.label = planMetadata.label;
        metadata.displayName = planMetadata.displayName;
        metadata.priority = planMetadata.priority;
        metadata.publicationCheckpoint = planMetadata.publicationCheckpoint;
        semanticBandMetadata.insert(it.key(), metadata);
    }
    SemanticIndex tierIndex;
    tierIndex.setWorkspaceFileAnalysisBands(semanticBandMetadata);
    const SemanticSymbolRecord tierRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("tier_sig"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(planC)
            .withLine(1)
            .record();
    const SemanticSymbolRecord currentTierQueryRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("zz_tier_match"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(planC)
            .withLine(2)
            .record();
    const SemanticSymbolRecord currentTierDuplicateRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("shared_tier_symbol"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(planC)
            .withLine(3)
            .record();
    const SemanticSymbolRecord currentTierModuleRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("zz_tier_module"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(planC)
            .withLine(4)
            .record();
    tierIndex.updateSymbolRecordsForFile(
        planC,
        {tierRecord,
         currentTierQueryRecord,
         currentTierDuplicateRecord,
         currentTierModuleRecord},
        QStringLiteral("module tier_top; logic tier_sig; logic zz_tier_match; logic shared_tier_symbol; endmodule\nmodule zz_tier_module; endmodule\n"));
    const SemanticSymbolRecord backgroundTierQueryRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("aa_tier_match"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(planA)
            .withLine(1)
            .record();
    const SemanticSymbolRecord backgroundTierDuplicateRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("shared_tier_symbol"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(planA)
            .withLine(2)
            .record();
    const SemanticSymbolRecord backgroundTierModuleRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("aa_tier_module"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(planA)
            .withLine(3)
            .record();
    tierIndex.updateSymbolRecordsForFile(
        planA,
        {backgroundTierQueryRecord,
         backgroundTierDuplicateRecord,
         backgroundTierModuleRecord},
        QStringLiteral("module tier_bg; logic aa_tier_match; logic shared_tier_symbol; endmodule\nmodule aa_tier_module; endmodule\n"));
    const QList<SemanticSymbolRecord> tierRecords =
        tierIndex.getSymbolRecords(QDir::toNativeSeparators(planC));
    expectBool("SemanticIndex records carry analysis band metadata",
               !tierRecords.isEmpty()
                   && tierRecords.first().analysisBand.label
                       == QStringLiteral("current")
                   && tierRecords.first().analysisBand.priority
                   && tierRecords.first().analysisBand.publicationCheckpoint == 1,
               true);
    const std::shared_ptr<const SemanticIndexSnapshot> tierSnapshot =
        tierIndex.captureSnapshotPreservingDiagnostics();
    const QList<SemanticSymbolRecord> tierSnapshotRecords =
        tierSnapshot->getSymbolRecords(planC);
    expectBool("Semantic snapshot preserves analysis band metadata",
               !tierSnapshotRecords.isEmpty()
                   && tierSnapshotRecords.first().analysisBand.label
                       == QStringLiteral("current")
                   && tierSnapshotRecords.first().analysisBand.displayName
                       == QStringLiteral("current"),
               true);
    const SemanticAnalysisBandReport tierIndexBandReport =
        tierIndex.analysisBandReport();
    expectEq("SemanticIndex reports analysis band summary",
             tierIndexBandReport.summaryText(),
             QStringLiteral("bands current 4 symbols/1 file, background 3 symbols/1 file"));
    expectBool("SemanticIndex report exposes band provenance",
               tierIndexBandReport.totalSymbolCount == 7
                   && tierIndexBandReport.totalFileCount == 2
                   && tierIndexBandReport.bands.size() == 2
                   && tierIndexBandReport.bands.first().label
                       == QStringLiteral("current")
                   && tierIndexBandReport.bands.first().priority
                   && tierIndexBandReport.bands.first().publicationCheckpoint == 1
                   && tierIndexBandReport.bands.first().files.contains(planC),
               true);
    const SemanticAnalysisBandReport tierSnapshotBandReport =
        tierSnapshot->analysisBandReport();
    expectBool("Semantic snapshot reports analysis bands",
               tierSnapshotBandReport.summaryText()
                       == tierIndexBandReport.summaryText()
                   && tierSnapshotBandReport.bands.size()
                       == tierIndexBandReport.bands.size(),
               true);
    CompletionService tierCompletionService(&tierIndex);
    CommandCompletionQuery tierCompletionQuery;
    tierCompletionQuery.prefix = QStringLiteral("zz_tier");
    tierCompletionQuery.commandKind = CompletionCommandKind::Module;
    const QList<SemanticSymbolRecord> tierCompletionRecords =
        tierCompletionService.findCommandCompletionSymbolRecords(
            tierCompletionQuery);
    expectBool("Command completion exposes analysis band provenance",
               tierCompletionRecords.size() == 1
                    && tierCompletionRecords.first().name
                        == QStringLiteral("zz_tier_module")
                    && tierCompletionRecords.first().analysisBand.displayName
                        == QStringLiteral("current")
                    && tierCompletionRecords.first().analysisBand.label
                        == QStringLiteral("current"),
               true);
    const CommandSymbolCompletionItem tierCommandItem =
        tierCompletionService.commandSymbolCompletionItem(
            tierCompletionRecords.isEmpty()
                ? SemanticSymbolRecord()
                : tierCompletionRecords.first(),
            CompletionCommandKind::Module,
            QStringLiteral("zz"));
    expectBool("Command symbol item exposes analysis band provenance",
               tierCommandItem.analysisBandDisplayName
                       == QStringLiteral("current")
                   && tierCommandItem.analysisBand.label
                       == QStringLiteral("current"),
               true);
    const QList<SemanticSymbolSearchResult> tierSearchResults =
        tierIndex.searchSymbols({QStringLiteral("tier_match"),
                                 QString(),
                                 {SymbolTaxonomy::DeclarationKind::Signal},
                                 SymbolTaxonomy::SymbolSearchIntent::Any,
                                 false,
                                 false,
                                 -1});
    expectBool("Semantic search prefers current analysis band",
               tierSearchResults.size() >= 2
                   && tierSearchResults.first().symbolRecord.name
                       == QStringLiteral("zz_tier_match"),
               true);
    const QList<SemanticSymbolRecord> tierDefinitionRecords =
        tierIndex.findDefinitionRecords(QStringLiteral("shared_tier_symbol"));
    expectBool("Definition records prefer priority analysis bands",
               tierDefinitionRecords.size() >= 2
                   && tierDefinitionRecords.first().location.fileName == planC,
               true);

    WorkspaceAnalysisPlan externalCurrentPlan =
        WorkspaceAnalysisPlanService::getInstance()->planForWorkspace(
            {planProject,
             QDir(planRoot).absoluteFilePath(QStringLiteral("external.sv")),
             {dirtyOpen}});
    expectEq("Workspace plan ignores external current file",
             externalCurrentPlan.project.systemVerilogFiles.join(QStringLiteral("|")),
             QStringList{planB, planA, planC, planD}.join(QStringLiteral("|")));
    expectBool("Workspace plan reports external current file",
               !externalCurrentPlan.currentFileInWorkspace
                   && externalCurrentPlan.priorityFileCount == 1
                   && externalCurrentPlan.backgroundFileCount == 3,
               true);
    expectBool("Workspace plan bands omit external current file",
               externalCurrentPlan.currentFilePriorityFiles.isEmpty()
                   && externalCurrentPlan.dirtyOpenPriorityFiles
                       == QStringList{planB}
                   && externalCurrentPlan.cleanOpenPriorityFiles.isEmpty()
                   && externalCurrentPlan.backgroundFiles
                       == QStringList{planA, planC, planD},
               true);
    expectBool("Workspace plan band index omits external current file",
               externalCurrentPlan.bandForFile(
                   QDir(planRoot).absoluteFilePath(
                       QStringLiteral("external.sv"))).isEmpty()
                   && externalCurrentPlan.bandForFile(planB)
                       == QStringLiteral("dirty-open")
                   && externalCurrentPlan.bandForFile(planA)
                       == QStringLiteral("background"),
               true);
    expectEq("Workspace plan summarizes external current bands",
             externalCurrentPlan.bandSummaryText(),
             QStringLiteral("bands current 0, dirty 1, open 0, background 3"));

    QTemporaryDir foregroundWorkspace;
    expectBool("Workspace foreground temp dir valid",
               foregroundWorkspace.isValid(),
               true);
    const QString foregroundFile =
        QDir(foregroundWorkspace.path()).absoluteFilePath(
            QStringLiteral("foreground.sv"));
    QFile foregroundDisk(foregroundFile);
    expectBool("Workspace foreground disk file created",
               foregroundDisk.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (foregroundDisk.isOpen()) {
        QTextStream out(&foregroundDisk);
        out << "module disk_version; logic disk_sig; endmodule\n";
        foregroundDisk.close();
    }

    MyCodeEditor foregroundEditor;
    foregroundEditor.setPlainText(
        QStringLiteral("module open_tabs_first; logic live_sig; endmodule\n"));
    DocumentModel foregroundDocuments;
    foregroundDocuments.registerEditor(&foregroundEditor, foregroundFile);

    AnalysisScheduler foregroundScheduler;
    SymbolAnalyzer foregroundAnalyzer;
    foregroundScheduler.setDocumentModel(&foregroundDocuments);
    foregroundScheduler.setSymbolAnalyzer(&foregroundAnalyzer);
    foregroundScheduler.setWorkspaceOpenProvider([]() { return true; });
    foregroundScheduler.setCurrentFileProvider(
        [foregroundFile]() { return foregroundFile; });

    QStringList foregroundStartedOrder;
    QObject::connect(&foregroundAnalyzer,
                     &SymbolAnalyzer::analysisStarted,
                     &foregroundAnalyzer,
                     [&foregroundStartedOrder](const QString& fileName) {
                         foregroundStartedOrder.append(fileName);
                     });
    bool sawForegroundPlan = false;
    WorkspaceAnalysisPlan foregroundPlan;
    QObject::connect(&foregroundScheduler,
                     &AnalysisScheduler::workspaceAnalysisPlanPrepared,
                     &foregroundScheduler,
                     [&sawForegroundPlan, &foregroundPlan](
                         const WorkspaceAnalysisPlan& plan) {
                         sawForegroundPlan = true;
                         foregroundPlan = plan;
                     });

    ProjectSnapshot foregroundProject;
    foregroundProject.workspaceRoot = foregroundWorkspace.path();
    foregroundProject.systemVerilogFiles = {foregroundFile};
    foregroundProject.includeDirs = {foregroundWorkspace.path()};
    foregroundScheduler.requestWorkspaceAnalysis(foregroundProject);
    expectBool("Workspace foreground analysis avoids duplicate open-doc refresh",
               foregroundStartedOrder.size() == 1
                   && foregroundStartedOrder.first()
                       == foregroundProject.workspaceRoot,
               true);
    expectBool("Workspace plan signal reports foreground priority",
               sawForegroundPlan
                   && foregroundPlan.priorityFileCount == 1
                   && foregroundPlan.backgroundFileCount == 0
                   && foregroundPlan.currentFilePriorityFiles
                       == QStringList{foregroundFile}
                   && foregroundPlan.backgroundFiles.isEmpty()
                   && foregroundPlan.currentFileInWorkspace,
               true);
    foregroundAnalyzer.cancelWorkspaceAnalysisAndInvalidate();

    WorkspaceSymbolAnalysisController cachedWorkspaceController;
    SymbolAnalyzer cachedWorkspaceAnalyzer;
    cachedWorkspaceController.setSymbolAnalyzer(&cachedWorkspaceAnalyzer);
    int cachedWorkspaceStarts = 0;
    int cachedRelationshipCancels = 0;
    QObject::connect(&cachedWorkspaceController,
                     &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisStarted,
                     &cachedWorkspaceController,
                     [&cachedWorkspaceStarts](const ProjectSnapshot&, int) {
                         ++cachedWorkspaceStarts;
                     });
    QObject::connect(&cachedWorkspaceController,
                     &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisCancelRequested,
                     &cachedWorkspaceController,
                     [&cachedRelationshipCancels]() {
                         ++cachedRelationshipCancels;
                     });
    cachedWorkspaceController.requestWorkspaceAnalysis(foregroundProject);
    cachedWorkspaceAnalyzer.batchAnalysisCompleted(1, 1);
    cachedWorkspaceController.requestWorkspaceAnalysis(foregroundProject);
    expectBool("Workspace executor accepts only explicit scheduler requests",
               cachedWorkspaceStarts == 2,
               true);
    expectBool("Workspace executor does not launch legacy relationship pass",
               cachedRelationshipCancels == 0,
               true);
    cachedWorkspaceController.cancelWorkspaceAnalysis();
    cachedWorkspaceAnalyzer.cancelWorkspaceAnalysisAndInvalidate();

    SmartRelationshipBuilder foregroundRelationshipBuilder(nullptr, nullptr);
    foregroundScheduler.setRelationshipBuilder(&foregroundRelationshipBuilder);
    QStringList foregroundRelationshipOrder;
    QObject::connect(&foregroundAnalyzer,
                     &SymbolAnalyzer::analysisStarted,
                     &foregroundAnalyzer,
                     [&foregroundRelationshipOrder](const QString& fileName) {
                         if (fileName == QStringLiteral("open_tabs"))
                             foregroundRelationshipOrder.append(fileName);
                     });
    QObject::connect(&foregroundScheduler,
                     &AnalysisScheduler::workspaceRelationshipAnalysisStarted,
                     &foregroundScheduler,
                     [&foregroundRelationshipOrder](const ProjectSnapshot&, int) {
                         foregroundRelationshipOrder.append(
                             QStringLiteral("workspace_relationship"));
                     });
    foregroundScheduler.requestWorkspaceRelationshipAnalysis(foregroundProject);
    expectBool("Workspace relationship reuses published overlay snapshot",
               foregroundRelationshipOrder.size() == 1
                   && foregroundRelationshipOrder.first()
                       == QStringLiteral("workspace_relationship"),
               true);
    foregroundScheduler.cancelWorkspaceRelationshipAnalysis();

    RelationshipAnalysisController guardedBuilderController;
    {
        SmartRelationshipBuilder externallyOwnedBuilder(nullptr, nullptr);
        guardedBuilderController.setRelationshipBuilder(
            &externallyOwnedBuilder);
        expectBool("Relationship controller observes live external builder",
                   guardedBuilderController.hasRelationshipBuilder(),
                   true);
    }
    expectBool("Destroyed external builder clears controller handle",
               guardedBuilderController.hasRelationshipBuilder(),
               false);
    guardedBuilderController.requestCancelAllAnalyses();
    guardedBuilderController.waitForAllAnalyses();
    expectBool("Controller shutdown survives builder-first destruction",
               true,
               true);

    WorkspaceAnalysisRequestQueue requestQueue;
    ProjectSnapshot queueFirst = planProject;
    queueFirst.workspaceRoot = QDir(planRoot).absoluteFilePath(
        QStringLiteral("first"));
    ProjectSnapshot queueSecond = planProject;
    queueSecond.workspaceRoot = QDir(planRoot).absoluteFilePath(
        QStringLiteral("second"));
    ProjectSnapshot queueThird = planProject;
    queueThird.workspaceRoot = QDir(planRoot).absoluteFilePath(
        QStringLiteral("third"));
    requestQueue.start(queueFirst);
    expectBool("Workspace request queue starts active",
               requestQueue.active() && !requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry activeTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry tracks active age",
               activeTelemetry.active
                   && !activeTelemetry.pending
                   && activeTelemetry.activeAgeMs >= 0
                   && activeTelemetry.pendingAgeMs < 0,
               true);
    expectBool("Workspace request queue accepts pending",
               requestQueue.queueLatest(queueSecond)
                   && requestQueue.queueLatest(queueThird)
                   && requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry pendingTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry tracks pending pressure",
               pendingTelemetry.active
                   && pendingTelemetry.pending
                   && pendingTelemetry.activeAgeMs >= 0
                   && pendingTelemetry.pendingAgeMs >= 0
                   && pendingTelemetry.pendingUpdateCount == 2,
               true);
    ProjectSnapshot queuedNext;
    expectBool("Workspace request queue returns latest pending",
               requestQueue.finishAndTakePending(&queuedNext)
                   && queuedNext.workspaceRoot == queueThird.workspaceRoot
                   && !requestQueue.active()
                   && !requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry finishedTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry records completed pressure",
               !finishedTelemetry.active
                   && !finishedTelemetry.pending
                   && finishedTelemetry.lastFinishedActiveAgeMs >= 0
                   && finishedTelemetry.lastTakenPendingAgeMs >= 0
                   && finishedTelemetry.lastTakenPendingUpdateCount == 2,
               true);
    requestQueue.start(queuedNext);
    requestQueue.clear();
    expectBool("Workspace request queue clears state",
               !requestQueue.active() && !requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry clearedTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry clears state",
               !clearedTelemetry.active
                   && !clearedTelemetry.pending
                   && clearedTelemetry.activeAgeMs < 0
                   && clearedTelemetry.pendingAgeMs < 0
                   && clearedTelemetry.lastFinishedActiveAgeMs < 0
                   && clearedTelemetry.lastTakenPendingAgeMs < 0
                   && clearedTelemetry.lastTakenPendingUpdateCount == 0,
               true);
    requestQueue.start(queueFirst);
    requestQueue.queueLatest(queueSecond);
    requestQueue.queueLatest(queueThird);
    const WorkspaceAnalysisRequestTelemetry cancelTelemetry =
        requestQueue.cancel();
    expectBool("Workspace request queue cancels active and pending",
               !requestQueue.active()
                   && !requestQueue.hasPending()
                   && !cancelTelemetry.active
                   && !cancelTelemetry.pending,
               true);
    expectBool("Workspace request cancel keeps discarded telemetry",
               cancelTelemetry.lastFinishedActiveAgeMs >= 0
                   && cancelTelemetry.lastTakenPendingAgeMs >= 0
                   && cancelTelemetry.lastTakenPendingUpdateCount == 2,
               true);

    DiagnosticsRefreshController diagnosticsRefresh;
    QStringList emittedDiagnosticsRefreshes;
    QObject::connect(&diagnosticsRefresh,
                     &DiagnosticsRefreshController::diagnosticsRefreshRequested,
                     &diagnosticsRefresh,
                     [&emittedDiagnosticsRefreshes](const QString& fileName) {
                         emittedDiagnosticsRefreshes.append(
                             fileName.isEmpty()
                                 ? QStringLiteral("<all>")
                                 : fileName);
                     });
    diagnosticsRefresh.requestRefresh(QStringLiteral("first.sv"));
    diagnosticsRefresh.requestRefresh(QString());
    diagnosticsRefresh.requestRefresh(QStringLiteral("second.sv"));
    expectBool("Diagnostics refresh queue emits coalesced request",
               waitForEventPredicate(
                   [&emittedDiagnosticsRefreshes]() {
                       return !emittedDiagnosticsRefreshes.isEmpty();
                   },
                   1000),
               true);
    expectEq("Diagnostics refresh preserves full scope",
             emittedDiagnosticsRefreshes.join(QStringLiteral("|")),
             QStringLiteral("<all>"));
    emittedDiagnosticsRefreshes.clear();
    diagnosticsRefresh.requestRefresh(QStringLiteral("second.sv"));
    expectBool("Diagnostics refresh emits file request",
               waitForEventPredicate(
                   [&emittedDiagnosticsRefreshes]() {
                       return !emittedDiagnosticsRefreshes.isEmpty();
                   },
                   1000),
               true);
    expectEq("Diagnostics refresh keeps file scope",
             emittedDiagnosticsRefreshes.join(QStringLiteral("|")),
             QStringLiteral("second.sv"));
    emittedDiagnosticsRefreshes.clear();
    diagnosticsRefresh.requestRefresh(QStringLiteral("first.sv"));
    diagnosticsRefresh.requestRefresh(QStringLiteral("second.sv"));
    expectBool("Diagnostics refresh merges different files",
               waitForEventPredicate(
                   [&emittedDiagnosticsRefreshes]() {
                       return !emittedDiagnosticsRefreshes.isEmpty();
                   },
                   1000),
               true);
    expectEq("Diagnostics refresh promotes different files to full scope",
             emittedDiagnosticsRefreshes.join(QStringLiteral("|")),
             QStringLiteral("<all>"));

    expectEq("SymbolTaxonomy modport label",
             SymbolTaxonomy::symbolTypeLabel(
                 semanticFixtureMetadata(
                     SymbolTaxonomy::DeclarationKind::Modport,
                     SymbolTaxonomy::SymbolOwnerScope::Interface,
                     SymbolTaxonomy::CollectorKind::InterfaceModport)),
             QStringLiteral("modport"));
    expectBool("SymbolTaxonomy struct variable metadata",
               semanticFixtureMetadata(
                   SymbolTaxonomy::DeclarationKind::StructVariable)
                   .declarationKind
                   == SymbolTaxonomy::DeclarationKind::StructVariable,
               true);
    expectBool("SymbolTaxonomy direct context metadata",
               semanticFixtureMetadata(
                   SymbolTaxonomy::DeclarationKind::StructVariable)
                   .declarationKind
                   == SymbolTaxonomy::DeclarationKind::StructVariable,
               true);
    SymbolTaxonomy::SemanticMetadata enumTypedefMetadata;
    enumTypedefMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Typedef;
    expectBool("SymbolTaxonomy enum typedef semantic completion",
               SymbolTaxonomy::semanticCompletionKindMatches(
                   enumTypedefMetadata,
                   SymbolTaxonomy::SemanticCompletionKind::EnumType,
                   QStringLiteral("enum")),
               true);
    const SymbolTaxonomy::SemanticMetadata metadataSignal =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module);
    expectBool("SymbolTaxonomy metadata command type",
               SymbolTaxonomy::semanticCompletionKindMatches(
                   metadataSignal,
                   SymbolTaxonomy::SemanticCompletionKind::Logic),
               true);
    expectBool("SymbolTaxonomy metadata typed completion",
               SymbolTaxonomy::semanticCompletionKindMatches(
                   metadataSignal,
                   SymbolTaxonomy::SemanticCompletionKind::Logic),
               true);
    expectBool("SymbolTaxonomy metadata signal definition candidate",
               SymbolTaxonomy::isDefinitionCandidate(metadataSignal),
               true);
    expectBool("SymbolTaxonomy metadata signal definition priority",
               SymbolTaxonomy::definitionPriority(metadataSignal) == 5,
               true);
    const SymbolTaxonomy::SemanticMetadata moduleMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global,
                                SymbolTaxonomy::CollectorKind::Module);
    expectBool("SymbolTaxonomy metadata global completion",
               SymbolTaxonomy::isGlobalCompletionCandidate(moduleMetadata),
               true);
    const SymbolTaxonomy::SemanticMetadata logicMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::Logic);
    expectBool("SymbolTaxonomy metadata internal completion",
               SymbolTaxonomy::isInternalCompletionCandidate(logicMetadata),
               true);
    expectBool("SymbolTaxonomy metadata raw compatibility",
               logicMetadata.collectorKind
                   == SymbolTaxonomy::CollectorKind::Logic,
               true);
    expectBool("SymbolTaxonomy metadata global completion type",
               SymbolTaxonomy::isGlobalCompletionCandidate(metadataSignal),
               false);
    const SymbolTaxonomy::SemanticMetadata metadataGlobal =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global);
    expectBool("SymbolTaxonomy metadata global completion candidate",
               SymbolTaxonomy::isGlobalCompletionCandidate(metadataGlobal),
               true);
    expectBool("SymbolTaxonomy metadata command global type",
               SymbolTaxonomy::isCommandGlobalCompletionType(metadataGlobal),
               true);
    expectBool("SymbolTaxonomy metadata global info type",
               SymbolTaxonomy::isGlobalSemanticSymbolType(metadataGlobal),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticModuleMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global);
    expectBool("SymbolTaxonomy search intent uses semantic metadata",
               SymbolTaxonomy::matchesSearchIntent(
                   syntheticModuleMetadata,
                   SymbolTaxonomy::SymbolSearchIntent::ModuleDeclarations),
               true);
    expectBool("SymbolTaxonomy global definition uses semantic metadata",
               SymbolTaxonomy::isGlobalDefinition(syntheticModuleMetadata),
               true);
    expectBool("SymbolTaxonomy definition candidate uses metadata",
               SymbolTaxonomy::isDefinitionCandidate(syntheticModuleMetadata),
               true);
    expectBool("SymbolTaxonomy definition priority uses metadata",
               SymbolTaxonomy::definitionPriority(syntheticModuleMetadata) == 0,
               true);
    expectBool("SymbolTaxonomy outline uses semantic metadata",
               SymbolTaxonomy::isOutlineSymbol(syntheticModuleMetadata),
               true);
    expectBool("SymbolTaxonomy requested type uses metadata",
               SymbolTaxonomy::isModuleDeclaration(syntheticModuleMetadata),
               true);
    expectEq("SymbolTaxonomy metadata label",
             SymbolTaxonomy::symbolTypeLabel(syntheticModuleMetadata),
             QStringLiteral("module"));
    SymbolTaxonomy::SemanticMetadata syntheticInstanceMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Instance);
    expectBool("SymbolTaxonomy metadata instance declaration",
               SymbolTaxonomy::isInstanceDeclaration(syntheticInstanceMetadata),
               true);
    syntheticInstanceMetadata.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Reference;
    expectBool("SymbolTaxonomy metadata instance pin not declaration",
               SymbolTaxonomy::isInstanceDeclaration(syntheticInstanceMetadata),
               false);
    const SymbolTaxonomy::SemanticMetadata metadataInstanceSymbolMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Instance);
    expectBool("SymbolTaxonomy metadata instance declaration",
               SymbolTaxonomy::isInstanceDeclaration(
                   metadataInstanceSymbolMetadata),
               true);
    const SymbolStableKey metadataKey =
        semanticFixtureStableKey(QString(),
                                 QStringLiteral("metadata_top"),
                                 SymbolTaxonomy::DeclarationKind::Module);
    expectBool("stable key uses semantic declaration kind",
               metadataKey.declarationKind
                   == SymbolTaxonomy::DeclarationKind::Module,
               true);
    SymbolTaxonomy::SemanticMetadata syntheticPortMetadata;
    syntheticPortMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Port;
    syntheticPortMetadata.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Declaration;
    expectBool("SymbolTaxonomy metadata declaration group",
               SymbolTaxonomy::declarationGroup(syntheticPortMetadata)
                   == SymbolTaxonomy::DeclarationGroup::Port,
               true);
    expectBool("SymbolTaxonomy metadata port declaration",
               SymbolTaxonomy::isPortDeclaration(syntheticPortMetadata),
               true);
    expectBool("SymbolTaxonomy metadata port connection peer",
               SymbolTaxonomy::isPortConnectionPeer(syntheticPortMetadata),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticSignalMetadata;
    syntheticSignalMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    expectBool("SymbolTaxonomy metadata signal declaration",
               SymbolTaxonomy::isSignalDeclaration(syntheticSignalMetadata),
               true);
    expectBool("SymbolTaxonomy metadata logic declaration",
               SymbolTaxonomy::isLogicDeclaration(logicMetadata),
               true);
    const SymbolTaxonomy::SemanticMetadata stateRegisterMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::Logic);
    expectBool("SymbolTaxonomy metadata fsm state register",
               SymbolTaxonomy::isFsmStateRegisterDeclaration(
                   stateRegisterMetadata),
               true);
    const SymbolTaxonomy::SemanticMetadata stateValueMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Enum,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::EnumValue);
    expectBool("SymbolTaxonomy metadata fsm state value",
               SymbolTaxonomy::isFsmStateValueDeclaration(stateValueMetadata),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticTaskMetadata;
    syntheticTaskMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Task;
    expectBool("SymbolTaxonomy metadata subroutine declaration",
               SymbolTaxonomy::isSubroutineDeclaration(syntheticTaskMetadata),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticPackageParameterMetadata;
    syntheticPackageParameterMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Parameter;
    expectBool("SymbolTaxonomy metadata package visibility",
               SymbolTaxonomy::isPackageVisibleDefinition(
                   syntheticPackageParameterMetadata),
               true);
    QSet<QString> packageScopes;
    packageScopes.insert(QStringLiteral("pkg_scope"));
    const SymbolTaxonomy::SemanticMetadata packageMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Package,
                                SymbolTaxonomy::SymbolOwnerScope::Global,
                                SymbolTaxonomy::CollectorKind::Package);
    expectBool("SymbolTaxonomy package metadata declaration",
               SymbolTaxonomy::isPackageDeclaration(packageMetadata),
               true);
    expectBool("SymbolTaxonomy package scope names",
               packageScopes.contains(QStringLiteral("pkg_scope")),
               true);
    const SymbolTaxonomy::SemanticMetadata packageParameterMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Parameter,
                                SymbolTaxonomy::SymbolOwnerScope::Package);
    const QString packageParameterOwner = QStringLiteral("pkg_scope");
    expectBool("SymbolTaxonomy package definition hidden without import context",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   packageParameterMetadata,
                   packageParameterOwner,
                   QStringLiteral("top")),
               false);
    const SymbolTaxonomy::SemanticMetadata metadataPackageParameterMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Parameter,
                                SymbolTaxonomy::SymbolOwnerScope::Package);
    const QString metadataPackageParameterOwner = QStringLiteral("pkg_scope");
    expectBool("SymbolTaxonomy metadata package definition hidden without import context",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   metadataPackageParameterMetadata,
                   metadataPackageParameterOwner,
                   QStringLiteral("top")),
               false);
    const SymbolTaxonomy::SemanticMetadata metadataStructMemberMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::StructMember,
                                SymbolTaxonomy::SymbolOwnerScope::Struct);
    const QString metadataStructMemberOwner = QStringLiteral("pixel_t");
    expectBool("SymbolTaxonomy metadata member definition visible",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   metadataStructMemberMetadata,
                   metadataStructMemberOwner,
                   QStringLiteral("top")),
               true);
    const SymbolTaxonomy::SemanticMetadata metadataEnumValueMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Enum,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::EnumValue);
    const QString metadataEnumValueOwner = QStringLiteral("state_t");
    expectBool("SymbolTaxonomy metadata enum value definition visible",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   metadataEnumValueMetadata,
                   metadataEnumValueOwner,
                   QStringLiteral("top")),
               true);
    const SymbolTaxonomy::SemanticMetadata scopedLogicMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::Logic);
    const QString scopedLogicOwner = QStringLiteral("top");
    expectBool("SymbolTaxonomy scoped logic definition hidden",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   scopedLogicMetadata,
                   scopedLogicOwner,
                   QStringLiteral("other_top")),
               false);
    const SymbolTaxonomy::SemanticMetadata finalModuleMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global,
                                SymbolTaxonomy::CollectorKind::Module);
    expectBool("SymbolTaxonomy module metadata declaration",
               SymbolTaxonomy::isModuleDeclaration(finalModuleMetadata),
               true);

    CompletionModel defaultSelectionModel;
    defaultSelectionModel.updateSymbolRecordCompletions({},
                                                        QStringLiteral("missing"),
                                                        CompletionCommandKind::Logic);
    ++g_checks;
    const QModelIndex defaultSelectableIndex =
        defaultSelectionModel.firstSelectableIndex();
    const bool defaultSelectableOk = defaultSelectableIndex.isValid()
        && defaultSelectionModel.getItem(defaultSelectableIndex)
               .text
               .startsWith(QStringLiteral("[DEFAULT]"))
        && defaultSelectionModel.isSelectableIndex(defaultSelectableIndex);
    if (!defaultSelectableOk)
        ++g_fails;
    printf("[%s] %-34s row=%d\n",
           defaultSelectableOk ? "PASS" : "FAIL",
           "CompletionModel selectable default",
           defaultSelectableIndex.row());

    const QList<SemanticSymbolRecord> realLogicDefaultRecords{
        SemanticFixtureRecordBuilder(
            QStringLiteral("en_a"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9051)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("en_b"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9052)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .record(),
    };
    CompletionModel realLogicSelectionModel;
    realLogicSelectionModel.updateSymbolRecordCompletions(
        realLogicDefaultRecords,
        QStringLiteral("en"),
        CompletionCommandKind::Logic);
    bool defaultRowPresentWithRealLogic = false;
    for (int row = 0; row < realLogicSelectionModel.rowCount(); ++row) {
        defaultRowPresentWithRealLogic =
            defaultRowPresentWithRealLogic
            || realLogicSelectionModel
                   .getItem(realLogicSelectionModel.index(row, 0))
                   .text
                   .startsWith(QStringLiteral("[DEFAULT]"));
    }
    const QModelIndex realLogicSelectableIndex =
        realLogicSelectionModel.firstSelectableIndex();
    expectBool("CompletionModel real logic beats default",
               realLogicSelectableIndex.isValid()
                   && realLogicSelectionModel
                          .getItem(realLogicSelectableIndex)
                          .text
                          .startsWith(QStringLiteral("en_"))
                   && !defaultRowPresentWithRealLogic,
               true);

    CompletionModel inlineNoMatchModel;
    inlineNoMatchModel.updateSymbolRecordCompletions(
        {},
        QStringLiteral("missing"),
        CompletionCommandKind::Logic,
        false);
    bool inlineNoMatchHasDefault = false;
    bool inlineNoMatchHasMessage = false;
    for (int row = 0; row < inlineNoMatchModel.rowCount(); ++row) {
        const CompletionModel::CompletionItem item =
            inlineNoMatchModel.getItem(inlineNoMatchModel.index(row, 0));
        inlineNoMatchHasDefault = inlineNoMatchHasDefault
            || item.text.startsWith(QStringLiteral("[DEFAULT]"));
        inlineNoMatchHasMessage = inlineNoMatchHasMessage
            || item.text == QStringLiteral("No matching symbols");
    }
    expectBool("CompletionModel inline no-match is non-executable",
               inlineNoMatchHasMessage
                   && !inlineNoMatchHasDefault
                   && !inlineNoMatchModel.firstSelectableIndex().isValid(),
               true);

    const CommandSymbolPresentation logicPresentation =
        CompletionService::getInstance()->commandSymbolPresentation(
            CompletionCommandKind::Logic);
    expectEq("CompletionService command default",
             logicPresentation.defaultValue,
             QStringLiteral("logic"));
    expectEq("CompletionService command desc",
             logicPresentation.typeDescription,
             QStringLiteral("logic variables"));
    const CommandSymbolPresentation modulePresentation =
        CompletionService::getInstance()->commandSymbolPresentation(
            CompletionCommandKind::Module);
    expectEq("CompletionService module command default",
             modulePresentation.defaultValue,
             QStringLiteral("module_name u_module_name (\n);"));
    expectEq("CompletionService module command desc",
             modulePresentation.typeDescription,
             QStringLiteral("module instantiations"));

    const SemanticSymbolRecord modulePresentationRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("uart_core"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .withLocalHandle(9002)
            .record();
    const CommandSymbolCompletionItem modulePresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            modulePresentationRecord,
            CompletionCommandKind::Module);
    expectEq("CompletionService module instantiates",
             modulePresentationItem.defaultValue,
             QStringLiteral("uart_core u_uart_core (\n);"));

    const QString instantiationFixturePath =
        QStringLiteral("semantic_module_instantiation.sv");
    const SemanticSymbolRecord instTargetModule =
        SemanticFixtureRecordBuilder(QStringLiteral("target_module"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9100)
            .withLine(1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const QList<SemanticSymbolRecord> instTargetMembers{
        SemanticFixtureRecordBuilder(QStringLiteral("P_WIDTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9101)
            .withLine(2)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Parameter)
            .inModule(QStringLiteral("target_module"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("P_DEPTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9102)
            .withLine(3)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Parameter)
            .inModule(QStringLiteral("target_module"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9103)
            .withLine(5)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("target_module"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("rst_n"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9104)
            .withLine(6)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("target_module"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("data_i"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9105)
            .withLine(7)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("target_module"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("data_o"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9106)
            .withLine(8)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortOutput)
            .inModule(QStringLiteral("target_module"))
            .record(),
    };
    const SemanticSymbolRecord noParamModule =
        SemanticFixtureRecordBuilder(QStringLiteral("simple_child"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9110)
            .withLine(20)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const QList<SemanticSymbolRecord> noParamMembers{
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9111)
            .withLine(21)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("simple_child"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("rst_n"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9112)
            .withLine(22)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .inModule(QStringLiteral("simple_child"))
            .record(),
    };
    const SemanticSymbolRecord fallbackModule =
        SemanticFixtureRecordBuilder(QStringLiteral("opaque_child"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(instantiationFixturePath)
            .withLocalHandle(9120)
            .withLine(40)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    SemanticIndex instantiationIndex;
    QList<SemanticSymbolRecord> instantiationRecords{instTargetModule};
    instantiationRecords.append(instTargetMembers);
    instantiationRecords.append(noParamModule);
    instantiationRecords.append(noParamMembers);
    instantiationRecords.append(fallbackModule);
    instantiationIndex.setSnapshot(sharedSnapshotFromRecords(instantiationRecords));
    CompletionService instantiationCompletionService(&instantiationIndex);

    const CommandSymbolCompletionItem semanticInstantiation =
        instantiationCompletionService.commandSymbolCompletionItem(
            instTargetModule,
            CompletionCommandKind::Module);
    const QString expectedSemanticInstantiation =
        QStringLiteral("target_module #(\n"
                       "    .P_WIDTH(P_WIDTH),\n"
                       "    .P_DEPTH(P_DEPTH)\n"
                       ") u_target_module (\n"
                       "    .clk(clk),\n"
                       "    .rst_n(rst_n),\n"
                       "    .data_i(data_i),\n"
                       "    .data_o(data_o)\n"
                       ");");
    expectEq("CompletionService module semantic instantiation",
             semanticInstantiation.defaultValue,
             expectedSemanticInstantiation);
    QStringList semanticSlotNames;
    QStringList semanticSlotTexts;
    for (const CodeTemplateSlot& slot : semanticInstantiation.templateSlots) {
        semanticSlotNames.append(slot.name);
        semanticSlotTexts.append(
            semanticInstantiation.defaultValue.mid(slot.start, slot.length));
    }
    expectEq("CompletionService module slot order",
             semanticSlotNames.join(QStringLiteral("|")),
             QStringLiteral("instance|parameter:P_WIDTH|parameter:P_DEPTH|port:clk|port:rst_n|port:data_i|port:data_o"));
    expectEq("CompletionService module slot text",
             semanticSlotTexts.join(QStringLiteral("|")),
             QStringLiteral("u_target_module|P_WIDTH|P_DEPTH|clk|rst_n|data_i|data_o"));
    expectBool("CompletionService module activation selects instance",
               semanticInstantiation.selectionStart
                       == semanticInstantiation.templateSlots.first().start
                   && semanticInstantiation.selectionLength
                       == semanticInstantiation.templateSlots.first().length,
               true);

    MyCodeEditor moduleSlotEditor;
    moduleSlotEditor.setPlainText(semanticInstantiation.defaultValue);
    moduleSlotEditor.startTemplateSlotMode(
        0,
        semanticInstantiation.defaultValue.size(),
        semanticInstantiation.templateSlots);
    expectBool("Module instantiation Slot Mode starts on instance",
               moduleSlotEditor.templateSlotModeActive()
                   && moduleSlotEditor.templateSlotModeActiveIndex() == 0
                   && moduleSlotEditor.templateSlotModeSlotCount() == 7
                   && moduleSlotEditor.textCursor().selectedText()
                       == QStringLiteral("u_target_module"),
               true);
    insertAtEditorCursor(moduleSlotEditor, QStringLiteral("u_dut"));
    expectBool("Module instantiation Slot Mode advances to first parameter",
               sendEditorKey(moduleSlotEditor, Qt::Key_Tab)
                   && moduleSlotEditor.templateSlotModeActiveIndex() == 1
                   && moduleSlotEditor.textCursor().selectedText()
                       == QStringLiteral("P_WIDTH"),
               true);
    expectBool("Module instantiation Slot Mode preserves edited instance",
               moduleSlotEditor.toPlainText().contains(
                   QStringLiteral(") u_dut (\n")),
               true);

    const CommandSymbolCompletionItem noParamInstantiation =
        instantiationCompletionService.commandSymbolCompletionItem(
            noParamModule,
            CompletionCommandKind::Module);
    expectEq("CompletionService module no-param instantiation",
             noParamInstantiation.defaultValue,
             QStringLiteral("simple_child u_simple_child (\n"
                            "    .clk(clk),\n"
                            "    .rst_n(rst_n)\n"
                            ");"));
    expectBool("CompletionService module no-param omits parameter block",
               !noParamInstantiation.defaultValue.contains(QStringLiteral("#("))
                   && noParamInstantiation.templateSlots.size() == 3,
               true);

    const CommandSymbolCompletionItem fallbackInstantiation =
        instantiationCompletionService.commandSymbolCompletionItem(
            fallbackModule,
            CompletionCommandKind::Module);
    expectEq("CompletionService module instantiation fallback",
             fallbackInstantiation.defaultValue,
             QStringLiteral("opaque_child u_opaque_child (\n);"));
    expectBool("CompletionService module fallback has no slots",
               fallbackInstantiation.templateSlots.isEmpty(),
               true);

    const SemanticSymbolRecord structPresentationRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("pixel"),
            SymbolTaxonomy::DeclarationKind::StructVariable)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9003)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PackedStructVariable)
            .withType(QStringLiteral("pixel_t"))
            .record();
    const CommandSymbolCompletionItem structPresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            structPresentationRecord,
            CompletionCommandKind::PackedStructVariable);
    expectEq("CompletionService struct text",
             structPresentationItem.text,
             QStringLiteral("pixel"));
    expectEq("CompletionService struct description",
             structPresentationItem.description,
             QStringLiteral("pixel_t"));
    expectEq("CompletionService struct key",
             structPresentationItem.uniqueKey,
             QStringLiteral("pixel:pixel_t"));
    expectBool("CompletionService struct item record",
               structPresentationItem.symbolRecord.isValid()
                   && structPresentationItem.symbolRecord.localHandle == 9003
                   && structPresentationItem.symbolRecord.stableKey
                       == structPresentationItem.symbolStableKey
                   && structPresentationItem.symbolStableKey
                       == structPresentationRecord.stableKey
                   && structPresentationItem.symbolRecord.owner.name
                       == QStringLiteral("top")
                   && structPresentationItem.declarationKind
                       == SymbolTaxonomy::DeclarationKind::StructVariable
                   && structPresentationItem.ownerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Module,
               true);

    const SemanticSymbolRecord enumPresentationRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("IDLE"),
            SymbolTaxonomy::DeclarationKind::Enum)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9004)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .withType(QStringLiteral("state_t"))
            .record();
    const CommandSymbolCompletionItem enumPresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            enumPresentationRecord,
            CompletionCommandKind::EnumValue);
    expectEq("CompletionService enum desc",
             enumPresentationItem.description,
             enumPresentationItem.symbolRecord.type.rawTypeText);
    expectBool("CompletionService enum item record",
               enumPresentationItem.symbolRecord.isValid()
                   && enumPresentationItem.symbolRecord.localHandle == 9004
                   && enumPresentationItem.symbolRecord.stableKey
                       == enumPresentationItem.symbolStableKey
                   && enumPresentationItem.symbolRecord.name
                       == QStringLiteral("IDLE")
                   && enumPresentationItem.symbolRecord.type.rawTypeText
                       == QStringLiteral("state_t")
                   && enumPresentationItem.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Enum
                   && enumPresentationItem.ownerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Module,
               true);

    CompletionActivationQuery commandActivationQuery;
    commandActivationQuery.selectable = true;
    commandActivationQuery.itemText = QStringLiteral("[DEFAULT] logic");
    commandActivationQuery.defaultValue = QStringLiteral("logic");
    const CompletionActivationState commandActivationState =
        CompletionService::getInstance()->completionActivationState(
            commandActivationQuery);
    ++g_checks;
    const bool commandActivationOk =
        commandActivationState.action
            == CompletionActivationAction::ReplaceCommandInput
        && commandActivationState.text == QStringLiteral("logic")
        && commandActivationState.clearCommandMode
        && commandActivationState.hidePopup;
    if (!commandActivationOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           commandActivationOk ? "PASS" : "FAIL",
           "CompletionService activate command",
           commandActivationState.text.toLocal8Bit().constData());

    CompletionActivationQuery commandFallbackQuery;
    commandFallbackQuery.selectable = true;
    commandFallbackQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState commandFallbackState =
        CompletionService::getInstance()->completionActivationState(
            commandFallbackQuery);
    ++g_checks;
    const bool commandFallbackOk =
        commandFallbackState.action
            == CompletionActivationAction::ReplaceCommandInput
        && commandFallbackState.text == QStringLiteral("enable")
        && commandFallbackState.clearCommandMode
        && commandFallbackState.hidePopup;
    if (!commandFallbackOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           commandFallbackOk ? "PASS" : "FAIL",
           "CompletionService activate fallback",
           commandFallbackState.text.toLocal8Bit().constData());

    EditorCompletionActivationContext contextCommandActivation;
    contextCommandActivation.selectable = true;
    contextCommandActivation.itemText = QStringLiteral("clk");
    contextCommandActivation.defaultValue = QStringLiteral("logic clk");
    const CompletionActivationState contextCommandActivationState =
        EditorSemanticContextService::getInstance()
            ->completionActivationState(contextCommandActivation);
    expectBool("EditorContext command activation",
               contextCommandActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && contextCommandActivationState.text
                       == QStringLiteral("logic clk")
                   && contextCommandActivationState.clearCommandMode
                   && contextCommandActivationState.hidePopup,
               true);
    CompletionActivationQuery inactiveActivationQuery;
    inactiveActivationQuery.selectable = false;
    inactiveActivationQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState inactiveActivationState =
        CompletionService::getInstance()->completionActivationState(
            inactiveActivationQuery);
    ++g_checks;
    const bool inactiveActivationOk =
        inactiveActivationState.action == CompletionActivationAction::None
        && inactiveActivationState.text.isEmpty()
        && !inactiveActivationState.clearCommandMode
        && !inactiveActivationState.hidePopup;
    if (!inactiveActivationOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           inactiveActivationOk ? "PASS" : "FAIL",
           "CompletionService activate inactive");

    CompletionPopupKeyQuery popupQuery;
    popupQuery.key = Qt::Key_Down;
    expectBool("CompletionService popup forwards arrows",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action == CompletionPopupKeyAction::ForwardToPopup,
               true);
    popupQuery.key = Qt::Key_Return;
    popupQuery.hasRows = true;
    popupQuery.currentIndexValid = false;
    expectBool("CompletionService popup activates selectable",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable,
               true);
    popupQuery.key = Qt::Key_Backtab;
    popupQuery.modifiers = int(Qt::NoModifier);
    expectBool("CompletionService Backtab selects previous",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::SelectPreviousSelectable,
               true);
    popupQuery.modifiers = int(Qt::ShiftModifier);
    expectBool("CompletionService shifted Backtab selects previous",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::SelectPreviousSelectable,
               true);
    popupQuery.key = Qt::Key_Tab;
    expectBool("CompletionService Tab+Shift selects previous",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::SelectPreviousSelectable,
               true);
    popupQuery.modifiers = int(Qt::NoModifier);
    expectBool("CompletionService plain Tab activates selectable",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable,
               true);
    popupQuery.modifiers = int(Qt::ControlModifier);
    expectBool("CompletionService command-modified Tab is not activation",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::None,
               true);
    popupQuery.modifiers = int(Qt::NoModifier);
    popupQuery.key = Qt::Key_Escape;
    expectBool("CompletionService popup clears explicit command",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::HidePopupAndClearCommand,
               true);
    popupQuery.key = Qt::Key_Return;
    popupQuery.hasRows = true;
    popupQuery.currentIndexValid = false;
    expectBool("EditorContext popup activates selectable",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable,
               true);
    EditorCompletionPopupKeyContext contextCommandPopupQuery;
    contextCommandPopupQuery.key = Qt::Key_Tab;
    contextCommandPopupQuery.modifiers = int(Qt::ShiftModifier);
    contextCommandPopupQuery.hasRows = true;
    expectBool("EditorContext preserves Tab+Shift popup direction",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(contextCommandPopupQuery)
                       .action
                   == CompletionPopupKeyAction::SelectPreviousSelectable,
               true);
    contextCommandPopupQuery.key = Qt::Key_Escape;
    contextCommandPopupQuery.modifiers = int(Qt::NoModifier);
    expectBool("EditorContext command popup clears",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(contextCommandPopupQuery)
                       .action
                   == CompletionPopupKeyAction::HidePopupAndClearCommand,
               true);

    const CommandModeMatch oldCommandModeMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral("l ena"));
    expectBool("CompletionService old command reject",
               !oldCommandModeMatch.matched,
               true);
    expectBool("CompletionService old module command reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("m top"))
                    .matched,
               true);
    expectBool("CompletionService old reg command reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("r reset"))
                    .matched,
               true);

    const CommandModeMatch commandModeMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral(";l ena"));
    ++g_checks;
    const bool commandModeMatchOk = commandModeMatch.matched
        && commandModeMatch.prefixPosition == 0
        && commandModeMatch.input == QStringLiteral("ena")
        && commandModeMatch.command.kind == CompletionCommandKind::Logic;
    if (!commandModeMatchOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandModeMatchOk ? "PASS" : "FAIL",
           "CompletionService command match",
           commandModeMatch.input.toLocal8Bit().constData());
    const CommandModeMatch inlineCommandMatch =
        CompletionService::getInstance()->matchCommandMode(
            QStringLiteral("lhs = ;l sig"));
    expectBool("CompletionService inline command match",
               inlineCommandMatch.matched
                   && inlineCommandMatch.prefixPosition
                       == QStringLiteral("lhs = ").size()
                   && inlineCommandMatch.input == QStringLiteral("sig")
                   && inlineCommandMatch.command.kind
                       == CompletionCommandKind::Logic,
               true);
    const InlineCommandMatch assignmentAbbreviation =
        InlineCommandMode::matchAbbreviationBeforeCursor(
            QStringLiteral("lhs=;l sig"));
    expectBool("InlineCommandMode parses code-position command",
               assignmentAbbreviation.matched
                   && assignmentAbbreviation.prefixPosition
                       == QStringLiteral("lhs=").size()
                   && assignmentAbbreviation.input == QStringLiteral("sig")
                   && assignmentAbbreviation.descriptor.semanticKind
                       == CompletionCommandKind::Logic,
               true);
    const InlineCommandMatch portAbbreviation =
        InlineCommandMode::matchAbbreviationBeforeCursor(
            QStringLiteral(".clk(;l clk"));
    expectBool("InlineCommandMode parses port-position command",
               portAbbreviation.matched
                   && portAbbreviation.prefixPosition
                       == QStringLiteral(".clk(").size()
                   && portAbbreviation.input == QStringLiteral("clk"),
               true);
    const InlineCommandMatch doubleSemicolonAbbreviation =
        InlineCommandMode::matchAbbreviationBeforeCursor(
            QStringLiteral(";;l"));
    expectBool("InlineCommandMode prefers double semicolon",
               doubleSemicolonAbbreviation.matched
                   && doubleSemicolonAbbreviation.intent
                       == InlineCommandIntent::CodeTemplate
                   && doubleSemicolonAbbreviation.descriptor.label
                       == QStringLiteral(";;l"),
               true);
    const InlineCommandMatch nearestAbbreviation =
        InlineCommandMode::matchAbbreviationBeforeCursor(
            QStringLiteral("lhs=;l old + ;w net"));
    expectBool("InlineCommandMode chooses nearest suffix",
               nearestAbbreviation.matched
                   && nearestAbbreviation.prefixPosition
                       == QStringLiteral("lhs=;l old + ").size()
                   && nearestAbbreviation.descriptor.semanticKind
                       == CompletionCommandKind::Wire
                   && nearestAbbreviation.input == QStringLiteral("net"),
               true);
    expectBool("InlineCommandMode unknown command rejects",
               !InlineCommandMode::matchAbbreviationBeforeCursor(
                    QStringLiteral("lhs=;unknown sig"))
                    .matched,
               true);
    const QString crossLineComment =
        QStringLiteral("module top;\n"
                       "  /* block ;l sig\n"
                       "     still comment ;l sig");
    expectBool("InlineCommandMode detects cross-line block comment",
               InlineCommandMode::isPositionInCommentOrString(
                   crossLineComment,
                   crossLineComment.lastIndexOf(QStringLiteral(";l"))),
               true);
    const QString urlThenCommand =
        QStringLiteral("string url = \"http://host\"; assign x = ;l sig");
    const int urlCommandStart = urlThenCommand.lastIndexOf(QStringLiteral(";l"));
    expectBool("InlineCommandMode allows command after URL string",
               !InlineCommandMode::isPositionInCommentOrString(
                   urlThenCommand,
                   urlCommandStart)
                   && InlineCommandMode::matchAbbreviationBeforeCursor(
                          urlThenCommand)
                          .matched,
               true);
    const QString closedBlockCommentThenCommand =
        QStringLiteral("/* // */ assign x = ;l sig");
    const int closedBlockCommandStart =
        closedBlockCommentThenCommand.lastIndexOf(QStringLiteral(";l"));
    expectBool("InlineCommandMode allows command after closed block comment",
               !InlineCommandMode::isPositionInCommentOrString(
                   closedBlockCommentThenCommand,
                   closedBlockCommandStart)
                   && InlineCommandMode::matchAbbreviationBeforeCursor(
                          closedBlockCommentThenCommand)
                          .matched,
               true);
    const QString realLineComment =
        QStringLiteral("// ;l sig");
    expectBool("InlineCommandMode rejects real line comment",
               InlineCommandMode::isPositionInCommentOrString(
                   realLineComment,
                   realLineComment.indexOf(QStringLiteral(";l"))),
               true);
    const QString commandInsideString =
        QStringLiteral("string s = \";l sig\"");
    expectBool("InlineCommandMode rejects command inside string",
               InlineCommandMode::isPositionInCommentOrString(
                   commandInsideString,
                   commandInsideString.indexOf(QStringLiteral(";l"))),
               true);
    const CommandModeInputState inlineInputState =
        CompletionService::getInstance()->commandModeInputState(
            QStringLiteral("assign lhs = ;l sig"));
    const QString inlineSuffix = QStringLiteral(" + rhs;");
    const QString inlineReplacement =
        QStringLiteral("assign lhs = ;l sig").left(inlineInputState.prefixPosition)
        + QStringLiteral("sig")
        + inlineSuffix;
    expectBool("CompletionService inline replacement range",
               inlineInputState.matched
                   && inlineInputState.prefixPosition > 0
                   && inlineReplacement
                       == QStringLiteral("assign lhs = sig + rhs;"),
               true);
    const CommandModeMatch rightmostCommandMatch =
        CompletionService::getInstance()->matchCommandMode(
            QStringLiteral("lhs = ;l old + ;w net"));
    expectBool("CompletionService chooses rightmost command",
               rightmostCommandMatch.matched
                   && rightmostCommandMatch.prefixPosition
                       == QStringLiteral("lhs = ;l old + ").size()
                   && rightmostCommandMatch.command.kind
                       == CompletionCommandKind::Wire
                   && rightmostCommandMatch.input == QStringLiteral("net"),
               true);
    const CommandModeMatch inlineTemplateMatch =
        CompletionService::getInstance()->matchCommandMode(
            QStringLiteral("lhs = ;;l clk"));
    expectBool("CompletionService template beats suffix command",
               inlineTemplateMatch.matched
                   && inlineTemplateMatch.prefixPosition
                       == QStringLiteral("lhs = ").size()
                   && inlineTemplateMatch.intent
                       == InlineCommandIntent::CodeTemplate
                   && inlineTemplateMatch.descriptor.label
                       == QStringLiteral(";;l")
                   && inlineTemplateMatch.input == QStringLiteral("clk"),
               true);
    const CommandModeMatch separatedStatementCommand =
        CompletionService::getInstance()->matchCommandMode(
            QStringLiteral("code; ;l sig"));
    expectBool("CompletionService spaced command after semicolon",
               separatedStatementCommand.matched
                   && separatedStatementCommand.prefixPosition
                       == QStringLiteral("code; ").size()
                   && separatedStatementCommand.command.kind
                       == CompletionCommandKind::Logic,
               true);
    const CommandModeMatch unspacedStatementCommand =
        CompletionService::getInstance()->matchCommandMode(
            QStringLiteral("code;l sig"));
    expectBool("CompletionService unspaced command after code",
               unspacedStatementCommand.matched
                   && unspacedStatementCommand.prefixPosition
                       == QStringLiteral("code").size()
                   && unspacedStatementCommand.command.kind
                       == CompletionCommandKind::Logic,
               true);
    expectBool("CompletionService module command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";m top"))
                       .command.kind == CompletionCommandKind::Module,
               true);
    expectBool("CompletionService reg command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";r reset"))
                       .command.kind == CompletionCommandKind::Reg,
               true);
    expectBool("CompletionService wire command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";w net"))
                       .command.kind == CompletionCommandKind::Wire,
               true);
    const CommandModeMatch visibleSymbolMatch =
        CompletionService::getInstance()
            ->matchCommandMode(QStringLiteral(";v local"));
    expectBool("CompletionService visible-symbol command match",
               visibleSymbolMatch.matched
                   && visibleSymbolMatch.intent
                       == InlineCommandIntent::SemanticCompletion
                   && visibleSymbolMatch.command.kind
                       == CompletionCommandKind::VisibleSymbol
                   && visibleSymbolMatch.input
                       == QStringLiteral("local"),
               true);
    expectBool("CompletionService visible-symbol template absent",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral(";;v local"))
                    .matched,
               true);
    expectBool("CompletionService parameter command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";p WIDTH"))
                       .command.kind == CompletionCommandKind::Parameter,
               true);
    expectBool("CompletionService parameter command remains parameter",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";p WIDTH"))
                       .intent == InlineCommandIntent::SemanticCompletion,
               true);
    expectBool("CompletionService localparam command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";lp LOCAL"))
                       .command.kind == CompletionCommandKind::Localparam,
               true);
    const CommandModeMatch packageImportMatch =
        CompletionService::getInstance()
            ->matchCommandMode(QStringLiteral(";pk cfg"));
    expectBool("CompletionService package import command match",
               packageImportMatch.matched
                   && packageImportMatch.intent
                       == InlineCommandIntent::PackageImport
                   && packageImportMatch.command.kind
                       == CompletionCommandKind::Package
                   && packageImportMatch.input == QStringLiteral("cfg"),
               true);
    expectBool("CompletionService package import template absent",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral(";;pk cfg"))
                    .matched,
               true);
    const CommandModeMatch headerIncludeMatch =
        CompletionService::getInstance()
            ->matchCommandMode(QStringLiteral(";h defs"));
    expectBool("CompletionService header include command match",
               headerIncludeMatch.matched
                   && headerIncludeMatch.intent
                       == InlineCommandIntent::HeaderInclude
                   && headerIncludeMatch.input == QStringLiteral("defs"),
               true);
    expectBool("CompletionService header template absent",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral(";;h defs"))
                    .matched,
               true);

    const CommandModeInputState commandInputState =
        CompletionService::getInstance()->commandModeInputState(QStringLiteral(";l ena"));
    ++g_checks;
    const bool commandInputStateOk = commandInputState.matched
        && commandInputState.prefixPosition == 0
        && commandInputState.input == QStringLiteral("ena")
        && commandInputState.command.kind == CompletionCommandKind::Logic;
    if (!commandInputStateOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandInputStateOk ? "PASS" : "FAIL",
           "CompletionService command input",
           commandInputState.input.toLocal8Bit().constData());

    const CommandModeInputState commandExitState =
        CompletionService::getInstance()->commandModeInputState(QStringLiteral(";"));
    ++g_checks;
    const bool commandExitStateOk = !commandExitState.matched;
    if (!commandExitStateOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandExitStateOk ? "PASS" : "FAIL",
           "CompletionService command exit",
           commandExitState.input.toLocal8Bit().constData());

    ++g_checks;
    const bool commandModeRejectOk =
        !CompletionService::getInstance()
             ->matchCommandMode(QStringLiteral("assign l ena"))
             .matched;
    if (!commandModeRejectOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandModeRejectOk ? "PASS" : "FAIL",
           "CompletionService command reject");

    ++g_checks;
    const bool commandInputRejectOk =
        !CompletionService::getInstance()
             ->commandModeInputState(QStringLiteral("assign l ena"))
             .matched;
    if (!commandInputRejectOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandInputRejectOk ? "PASS" : "FAIL",
           "CompletionService command input reject");

    CommandModeCompletionQuery commandCompletionQuery;
    commandCompletionQuery.lineUpToCursor = QStringLiteral(";l en");
    commandCompletionQuery.fileName = path;
    commandCompletionQuery.moduleName = QStringLiteral("top");
    commandCompletionQuery.documentText = content;
    const CommandModeCompletionState commandCompletionState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionQuery);
    expectList("CompletionService command state",
               recordNames(commandCompletionState.symbolRecords),
               {"enable"});
    ++g_checks;
    const bool commandCompletionStateOk = commandCompletionState.matched
        && !commandCompletionState.hidePopup
        && commandCompletionState.showCompletions
        && commandCompletionState.completionPrefix == QStringLiteral("en")
        && commandCompletionState.command.kind == CompletionCommandKind::Logic
        && commandCompletionState.commandKind == CompletionCommandKind::Logic;
    if (!commandCompletionStateOk)
        ++g_fails;
    printf("[%s] %-34s prefix=\"%s\"\n",
           commandCompletionStateOk ? "PASS" : "FAIL",
           "CompletionService command state flags",
           commandCompletionState.completionPrefix.toLocal8Bit().constData());
    expectBool("CompletionService command state records",
               commandCompletionState.symbolRecords.size() == 1
                   && commandCompletionState.symbolStableKeys.size() == 1
                   && commandCompletionState.symbolRecords.first().isValid()
                   && commandCompletionState.symbolRecords.first().localHandle
                       >= 0
                   && commandCompletionState.symbolRecords.first().stableKey
                       == commandCompletionState.symbolStableKeys.first()
                   && commandCompletionState.symbolRecords.first().name
                       == QStringLiteral("enable")
                   && commandCompletionState.symbolRecords.first().owner.name
                       == QStringLiteral("top")
                   && commandCompletionState.symbolStableKeys.first()
                       == commandCompletionState.symbolRecords.first().stableKey,
               true);

    const CommandModeCompletionState headerIncludeState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";h defs")});
    expectBool("CompletionService header include state",
               headerIncludeState.matched
                   && headerIncludeState.intent
                       == InlineCommandIntent::HeaderInclude
                   && headerIncludeState.showCompletions
                   && headerIncludeState.completionPrefix
                       == QStringLiteral("defs")
                   && headerIncludeState.symbolRecords.isEmpty(),
               true);

    CommandModeCompletionQuery commandCompletionExitQuery;
    commandCompletionExitQuery.lineUpToCursor = QStringLiteral(";");
    const CommandModeCompletionState commandCompletionExitState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionExitQuery);
    ++g_checks;
    const bool commandCompletionExitOk = !commandCompletionExitState.matched
        && !commandCompletionExitState.showCompletions
        && commandCompletionExitState.symbolRecords.isEmpty()
        && commandCompletionExitState.symbolStableKeys.isEmpty();
    if (!commandCompletionExitOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandCompletionExitOk ? "PASS" : "FAIL",
           "CompletionService command state exit");

    CommandModeCompletionQuery commandCompletionHideQuery;
    commandCompletionHideQuery.lineUpToCursor = QStringLiteral(";sp pix");
    commandCompletionHideQuery.fileName = path;
    commandCompletionHideQuery.documentText = content;
    const CommandModeCompletionState commandCompletionHideState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionHideQuery);
    ++g_checks;
    const bool commandCompletionHideOk = commandCompletionHideState.matched
        && commandCompletionHideState.hidePopup
        && !commandCompletionHideState.showCompletions
        && commandCompletionHideState.command.kind
            == CompletionCommandKind::PackedStructVariable;
    if (!commandCompletionHideOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandCompletionHideOk ? "PASS" : "FAIL",
           "CompletionService command state hide");

    CommandModeCompletionQuery commandCompletionFilterQuery;
    commandCompletionFilterQuery.lineUpToCursor = QStringLiteral(";l clk");
    commandCompletionFilterQuery.fileName = path;
    commandCompletionFilterQuery.moduleName = QStringLiteral("top");
    commandCompletionFilterQuery.documentText = content;
    const CommandModeCompletionState commandCompletionFilterState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionFilterQuery);
    expectBool("CompletionService command filter prefix",
               commandCompletionFilterState.matched
                   && commandCompletionFilterState.completionPrefix
                       == QStringLiteral("clk")
                   && commandCompletionFilterState.command.kind
                       == CompletionCommandKind::Logic,
               true);

    const CommandModeCompletionState helpCompletionState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";?")});
    expectBool("CompletionService command help",
               helpCompletionState.matched
                   && helpCompletionState.helpRequested
                   && helpCompletionState.showCompletions
                   && helpCompletionState.helpCommands.size() >= 3,
               true);

    const CommandModeCompletionState templateCompletionState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;l clk")});
    expectBool("CompletionService template mode",
               templateCompletionState.matched
                   && templateCompletionState.intent
                       == InlineCommandIntent::CodeTemplate
                   && templateCompletionState.showCompletions
                   && !templateCompletionState.templateItems.isEmpty()
                   && templateCompletionState.templateItems.first().insertText
                       == QStringLiteral("logic clk;"),
               true);
    expectBool("CompletionService template command",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";;m uart"))
                       .intent == InlineCommandIntent::CodeTemplate,
               true);
    const CommandModeCompletionState moduleTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;m uart")});
    expectBool("CompletionService module template mode",
               moduleTemplateState.matched
                   && moduleTemplateState.intent
                       == InlineCommandIntent::CodeTemplate
                   && !moduleTemplateState.templateItems.isEmpty()
                   && moduleTemplateState.templateItems.first().insertText
                       == QStringLiteral("`timescale 1ns / 1ps\n"
                                         "module uart(\n"
                                         ");\n"
                                         "endmodule"),
               true);
    const CommandModeCompletionState actionHelpState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";:?")});
    expectBool("CompletionService reserved action help inactive",
               !actionHelpState.matched
                   && !actionHelpState.showCompletions,
               true);
    const CommandModeMatch foldActionMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral(";:fd"));
    expectBool("CompletionService reserved fold action unmatched",
               !foldActionMatch.matched,
               true);
    const CommandModeCompletionState foldActionState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";:fd")});
    expectBool("CompletionService reserved fold action has no completion",
               !foldActionState.matched
                   && !foldActionState.showCompletions
                   && foldActionState.templateItems.isEmpty(),
               true);
    CompletionActivationQuery foldActionActivation;
    foldActionActivation.selectable = true;
    foldActionActivation.itemText = QStringLiteral(";:fd");
    foldActionActivation.defaultValue = QStringLiteral(";:fd");
    const CompletionActivationState foldActionActivationState =
        CompletionService::getInstance()->completionActivationState(
            foldActionActivation);
    expectBool("CompletionService reserved fold action does not execute",
               foldActionActivationState.action
                       != CompletionActivationAction::ExecuteEditorAction
                   && foldActionActivationState.text == QStringLiteral(";:fd"),
               true);
    const CommandModeMatch foldShelfActionMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral(";:fds"));
    expectBool("CompletionService reserved fold shelf action unmatched",
               !foldShelfActionMatch.matched,
               true);
    const CommandModeCompletionState templateHelpState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;?")});
    expectBool("CompletionService template help",
               templateHelpState.matched
                   && templateHelpState.intent == InlineCommandIntent::CodeTemplate
                   && templateHelpState.helpRequested
                   && templateHelpState.showCompletions,
               true);
    expectBool("CompletionService bare double semicolon reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral(";;"))
                    .matched,
               true);
    const CommandModeCompletionState actionPrefixState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";:")});
    expectBool("CompletionService reserved action prefix inactive",
               !actionPrefixState.matched
                   && !actionPrefixState.showCompletions
                   && actionPrefixState.templateItems.isEmpty(),
               true);
    expectBool("CompletionService inline template command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral("assign lhs = ;;l clk"))
                       .intent == InlineCommandIntent::CodeTemplate,
               true);
    const CommandModeMatch unspacedTemplateCommand =
        CompletionService::getInstance()
            ->matchCommandMode(QStringLiteral("assign a = b;;l "));
    expectBool("CompletionService unspaced template command",
               unspacedTemplateCommand.matched
                   && unspacedTemplateCommand.prefixPosition
                       == QStringLiteral("assign a = b").size()
                   && unspacedTemplateCommand.intent
                       == InlineCommandIntent::CodeTemplate,
               true);
    expectBool("CompletionService template comment reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("// ;;l "))
                    .matched,
               true);
    expectBool("CompletionService template string reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("string s = \";;l "))
                    .matched,
               true);
    expectEq("CodeTemplateService seeds logic",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"), QStringLiteral("clk"))
                 .insertText,
             QStringLiteral("logic clk;"));
    expectEq("CodeTemplateService module template",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;m"),
                                      QStringLiteral("uart"))
                 .insertText,
             QStringLiteral("`timescale 1ns / 1ps\n"
                            "module uart(\n"
                            ");\n"
                            "endmodule"));
    const QString unsavedAlwaysFfDocument =
        QStringLiteral("module stale(input logic stale_clk,\n"
                       "             input logic stale_rst_n);\n"
                       "endmodule\n"
                       "module edited(input logic edit_clk,\n"
                       "              input logic edit_reset_n);\n"
                       "  ;;af \n"
                       "endmodule\n");
    CommandModeCompletionQuery unsavedAlwaysFfQuery;
    unsavedAlwaysFfQuery.lineUpToCursor = QStringLiteral("  ;;af ");
    unsavedAlwaysFfQuery.documentText = unsavedAlwaysFfDocument;
    unsavedAlwaysFfQuery.cursorPosition =
        unsavedAlwaysFfDocument.indexOf(QStringLiteral(";;af "))
        + QStringLiteral(";;af ").size();
    const CommandModeCompletionState unsavedAlwaysFfState =
        CompletionService::getInstance()->commandModeCompletionState(
            unsavedAlwaysFfQuery);
    bool unsavedAlwaysFfUsesLiveContext =
        unsavedAlwaysFfState.matched
        && unsavedAlwaysFfState.templateItems.size() == 4;
    QStringList unsavedAlwaysFfLabels;
    for (const CodeTemplateItem& item :
         unsavedAlwaysFfState.templateItems) {
        unsavedAlwaysFfLabels.append(item.label);
        unsavedAlwaysFfUsesLiveContext =
            unsavedAlwaysFfUsesLiveContext
            && item.insertText.contains(QStringLiteral("posedge edit_clk"))
            && !item.insertText.contains(QStringLiteral("stale_clk"));
    }
    expectBool("CompletionService templates use unsaved document context",
               unsavedAlwaysFfUsesLiveContext,
               true);
    expectEq("always_ff offers four explicit process styles",
             unsavedAlwaysFfLabels.join(QStringLiteral("|")),
             QStringLiteral(
                 "always_ff (no reset)|"
                 "always_ff (synchronous reset)|"
                 "always_ff (asynchronous active-low reset)|"
                 "always_ff (asynchronous active-high reset)"));
    bool everyAlwaysFfHasVisibleBody =
        !unsavedAlwaysFfState.templateItems.isEmpty();
    CodeTemplateItem asynchronousLowAlwaysFf;
    for (const CodeTemplateItem& item :
         unsavedAlwaysFfState.templateItems) {
        bool itemHasVisibleBody = false;
        for (const CodeTemplateSlot& slot : item.templateSlots) {
            if ((slot.name == QStringLiteral("body")
                 || slot.name == QStringLiteral("resetBody"))
                && slot.length == 0
                && slot.visibleWhenEmpty) {
                itemHasVisibleBody = true;
            }
        }
        everyAlwaysFfHasVisibleBody =
            everyAlwaysFfHasVisibleBody && itemHasVisibleBody;
        if (item.label.contains(
                QStringLiteral("asynchronous active-low"))) {
            asynchronousLowAlwaysFf = item;
        }
    }
    expectBool("always_ff variants expose visible empty body slots",
               everyAlwaysFfHasVisibleBody,
               true);
    QList<CodeTemplateSlot> asynchronousResetSlots;
    for (const CodeTemplateSlot& slot :
         asynchronousLowAlwaysFf.templateSlots) {
        if (slot.name == QStringLiteral("reset"))
            asynchronousResetSlots.append(slot);
    }
    expectBool("always_ff reset occurrences are linked",
               asynchronousResetSlots.size() == 2
                   && asynchronousResetSlots.at(0).tabStop >= 0
                   && asynchronousResetSlots.at(0).tabStop
                          == asynchronousResetSlots.at(1).tabStop,
               true);
    expectEq("always_ff reset style splits end and else",
             asynchronousLowAlwaysFf.insertText,
             QStringLiteral(
                 "always_ff @(posedge edit_clk or negedge edit_reset_n) begin\n"
                 "    if (!edit_reset_n) begin\n"
                 "        \n"
                 "    end\n"
                 "    else begin\n"
                 "        \n"
                 "    end\n"
                 "end"));

    const QString structuralAlwaysFfDocument =
        QStringLiteral("module edge_driven;\n"
                       "  logic phase, clear_b;\n"
                       "  always_ff @(posedge phase or negedge clear_b) begin\n"
                       "  end\n"
                       "  ;;af \n"
                       "endmodule\n");
    CommandModeCompletionQuery structuralAlwaysFfQuery;
    structuralAlwaysFfQuery.lineUpToCursor =
        QStringLiteral("  ;;af ");
    structuralAlwaysFfQuery.documentText =
        structuralAlwaysFfDocument;
    structuralAlwaysFfQuery.cursorPosition =
        structuralAlwaysFfDocument.indexOf(QStringLiteral(";;af "))
        + QStringLiteral(";;af ").size();
    const CommandModeCompletionState structuralAlwaysFfState =
        CompletionService::getInstance()->commandModeCompletionState(
            structuralAlwaysFfQuery);
    expectBool("always_ff infers unconventional event signal names structurally",
               structuralAlwaysFfState.templateItems.size() == 4
                   && structuralAlwaysFfState.templateItems.first()
                          .insertText.contains(
                              QStringLiteral("posedge phase"))
                   && structuralAlwaysFfState.templateItems.at(2)
                          .insertText.contains(
                              QStringLiteral("negedge clear_b")),
               true);

    QTemporaryDir userTemplateJsonDir;
    expectBool("UserTemplateService temp dir valid",
               userTemplateJsonDir.isValid(),
               true);
    const QString globalTemplateJson =
        QDir(userTemplateJsonDir.path()).absoluteFilePath(
            QStringLiteral("global_templates.json"));
    const QString workspaceTemplateJson =
        QDir(userTemplateJsonDir.path()).absoluteFilePath(
            QStringLiteral("workspace_templates.json"));
    expectBool("write global user template JSON",
               writeTextFile(globalTemplateJson,
                             QStringLiteral(
                                 "{\n"
                                 "  \"templates\": [\n"
                                 "    {\n"
                                 "      \"command\": \";;pipe\",\n"
                                 "      \"description\": \"global pipeline register\",\n"
                                 "      \"body\": \"logic [WIDTH-1:0] data_q;\\nalways_ff @(posedge clk) data_q <= data_d;\",\n"
                                 "      \"slots\": [\n"
                                 "        {\"name\": \"signal\", \"start\": 20, \"length\": 6}\n"
                                 "      ]\n"
                                 "    },\n"
                                 "    {\n"
                                 "      \"command\": \";;axi\",\n"
                                 "      \"description\": \"global AXI shell\",\n"
                                 "      \"insertText\": \"axi_if axi();\"\n"
                                 "    }\n"
                                 "  ]\n"
                                 "}\n")),
               true);
    expectBool("write workspace user template JSON",
               writeTextFile(workspaceTemplateJson,
                             QStringLiteral(
                                 "[\n"
                                 "  {\n"
                                 "    \"command\": \";;pipe\",\n"
                                 "    \"description\": \"workspace pipeline register\",\n"
                                 "    \"body\": \"logic ws_data;\",\n"
                                 "    \"slots\": [\n"
                                 "      {\"name\": \"ws_signal\", \"start\": 6, \"length\": 7}\n"
                                 "    ]\n"
                                 "  }\n"
                                 "]\n")),
               true);
    UserTemplateService reloadedUserTemplates(globalTemplateJson,
                                              workspaceTemplateJson);
    const UserTemplateLoadReport userTemplateLoadReport =
        reloadedUserTemplates.reload();
    expectBool("UserTemplateService loads global and workspace JSON",
               userTemplateLoadReport.valid
                   && userTemplateLoadReport.records.size() == 2,
               true);
    const QList<CodeTemplateItem> userTemplateCatalog =
        reloadedUserTemplates.catalog();
    bool catalogHasAxi = false;
    bool catalogHasWorkspacePipe = false;
    for (const CodeTemplateItem& item : userTemplateCatalog) {
        if (item.commandToken == QStringLiteral(";;axi")
            && item.insertText == QStringLiteral("axi_if axi();")) {
            catalogHasAxi = true;
        }
        if (item.commandToken == QStringLiteral(";;pipe")
            && item.description == QStringLiteral("workspace pipeline register")
            && item.insertText == QStringLiteral("logic ws_data;")) {
            catalogHasWorkspacePipe = true;
        }
    }
    expectBool("UserTemplateService catalog includes global template",
               catalogHasAxi,
               true);
    expectBool("UserTemplateService workspace overrides global",
               catalogHasWorkspacePipe,
               true);
    const QList<CodeTemplateItem> userTemplateMatches =
        reloadedUserTemplates.matchingTemplates(QStringLiteral(";;pipe"));
    expectBool("UserTemplateService query preserves workspace slots",
               userTemplateMatches.size() == 1
                   && userTemplateMatches.first().insertText
                       == QStringLiteral("logic ws_data;")
                   && userTemplateMatches.first().selectionStart == 6
                   && userTemplateMatches.first().selectionLength == 7
                   && userTemplateMatches.first().templateSlots.size() == 1
                   && userTemplateMatches.first().templateSlots.first().name
                       == QStringLiteral("ws_signal"),
               true);
    expectBool("UserTemplateService unknown command empty",
               reloadedUserTemplates
                   .matchingTemplates(QStringLiteral(";;missing"))
                   .isEmpty(),
               true);

    CompletionService userTemplateCompletionService;
    userTemplateCompletionService.setUserTemplateService(
        &reloadedUserTemplates);
    const CommandModeInputState userTemplateInputState =
        userTemplateCompletionService.commandModeInputState(
            QStringLiteral(";;pipe stage"));
    expectBool("CompletionService recognizes user template command",
               userTemplateInputState.matched
                   && userTemplateInputState.intent
                       == InlineCommandIntent::CodeTemplate
                   && userTemplateInputState.descriptor.label
                       == QStringLiteral(";;pipe")
                   && userTemplateInputState.input == QStringLiteral("stage"),
               true);
    const CommandModeInputState inlineUserTemplateInputState =
        userTemplateCompletionService.commandModeInputState(
            QStringLiteral("assign q = ;;pipe stage"));
    expectBool("CompletionService recognizes inline user template command",
               inlineUserTemplateInputState.matched
                   && inlineUserTemplateInputState.prefixPosition
                       == QStringLiteral("assign q = ").size()
                   && inlineUserTemplateInputState.intent
                       == InlineCommandIntent::CodeTemplate
                   && inlineUserTemplateInputState.descriptor.label
                       == QStringLiteral(";;pipe")
                   && inlineUserTemplateInputState.input
                       == QStringLiteral("stage"),
               true);
    const CommandModeInputState rightmostMixedTemplateState =
        userTemplateCompletionService.commandModeInputState(
            QStringLiteral(";l earlier ;;pipe stage"));
    expectBool("CompletionService chooses rightmost user template command",
               rightmostMixedTemplateState.matched
                   && rightmostMixedTemplateState.prefixPosition
                       == QStringLiteral(";l earlier ").size()
                   && rightmostMixedTemplateState.descriptor.label
                       == QStringLiteral(";;pipe"),
               true);
    const CommandModeCompletionState userTemplateCompletionState =
        userTemplateCompletionService.commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;pipe ")});
    expectBool("CompletionService returns user template item",
               userTemplateCompletionState.matched
                   && userTemplateCompletionState.intent
                       == InlineCommandIntent::CodeTemplate
                   && userTemplateCompletionState.showCompletions
                   && userTemplateCompletionState.templateItems.size() == 1
                   && userTemplateCompletionState.templateItems.first()
                          .commandToken == QStringLiteral(";;pipe")
                   && userTemplateCompletionState.templateItems.first()
                          .insertText == QStringLiteral("logic ws_data;")
                   && userTemplateCompletionState.templateItems.first()
                          .templateSlots.size() == 1,
               true);
    const CodeTemplateItem userTemplateCompletionItem =
        userTemplateCompletionState.templateItems.isEmpty()
            ? CodeTemplateItem()
            : userTemplateCompletionState.templateItems.first();
    CompletionActivationQuery userTemplateActivation;
    userTemplateActivation.selectable = true;
    userTemplateActivation.defaultValue =
        userTemplateCompletionItem.insertText;
    userTemplateActivation.selectionStart =
        userTemplateCompletionItem.selectionStart;
    userTemplateActivation.selectionLength =
        userTemplateCompletionItem.selectionLength;
    userTemplateActivation.templateSlots =
        userTemplateCompletionItem.templateSlots;
    const CompletionActivationState userTemplateActivationState =
        userTemplateCompletionService.completionActivationState(
            userTemplateActivation);
    expectBool("CompletionService user template activation carries slot",
               userTemplateActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && userTemplateActivationState.clearCommandMode
                   && userTemplateActivationState.hidePopup
                   && userTemplateActivationState.text
                       == QStringLiteral("logic ws_data;")
                   && userTemplateActivationState.templateSlots.size() == 1,
               true);
    MyCodeEditor userTemplateSlotEditor;
    userTemplateSlotEditor.setPlainText(userTemplateCompletionItem.insertText);
    userTemplateSlotEditor.startTemplateSlotMode(
        0,
        userTemplateCompletionItem.insertText.size(),
        userTemplateCompletionItem.templateSlots);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("User template Slot Mode starts on slot",
               userTemplateSlotEditor.templateSlotModeActive()
                   && userTemplateSlotEditor.templateSlotModeActiveIndex() == 0
                   && userTemplateSlotEditor.textCursor().selectedText()
                       == QStringLiteral("ws_data"),
               true);
    insertAtEditorCursor(userTemplateSlotEditor,
                         QStringLiteral("stage_data"));
    expectBool("User template Slot Mode edit keeps document",
               userTemplateSlotEditor.toPlainText()
                       == QStringLiteral("logic stage_data;")
                   && userTemplateSlotEditor.templateSlotModeActive(),
               true);
    expectBool("User template Slot Mode Tab cycles single slot",
               sendEditorKey(userTemplateSlotEditor, Qt::Key_Tab)
                   && userTemplateSlotEditor.templateSlotModeActive()
                   && userTemplateSlotEditor.templateSlotModeActiveIndex() == 0,
               true);
    expectBool("User template Slot Mode Esc exits",
               sendEditorKey(userTemplateSlotEditor, Qt::Key_Escape)
                   && !userTemplateSlotEditor.templateSlotModeActive(),
               true);

    expectBool("rewrite workspace user template JSON for reload",
               writeTextFile(workspaceTemplateJson,
                             QStringLiteral(
                                 "[\n"
                                 "  {\n"
                                 "    \"command\": \";;pipe\",\n"
                                 "    \"description\": \"workspace reload\",\n"
                                 "    \"body\": \"logic reload_data;\"\n"
                                 "  }\n"
                                 "]\n")),
               true);
    expectBool("UserTemplateService reload reflects file changes",
               reloadedUserTemplates.reload().valid
                   && reloadedUserTemplates
                          .matchingTemplates(QStringLiteral(";;pipe"))
                          .first()
                          .insertText == QStringLiteral("logic reload_data;"),
               true);

    const QString invalidTemplateJson =
        QDir(userTemplateJsonDir.path()).absoluteFilePath(
            QStringLiteral("invalid_templates.json"));
    expectBool("write invalid user template JSON",
               writeTextFile(invalidTemplateJson,
                             QStringLiteral("{ definitely not json")),
               true);
    UserTemplateService invalidJsonTemplates(invalidTemplateJson);
    const UserTemplateLoadReport invalidJsonReport =
        invalidJsonTemplates.reload();
    expectBool("UserTemplateService reports invalid JSON",
               !invalidJsonReport.valid
                   && !invalidJsonReport.failureReason.isEmpty()
                   && invalidJsonTemplates.catalog().isEmpty(),
               true);

    const QString invalidRecordJson =
        QDir(userTemplateJsonDir.path()).absoluteFilePath(
            QStringLiteral("invalid_record_templates.json"));
    expectBool("write invalid user template records",
               writeTextFile(invalidRecordJson,
                             QStringLiteral(
                                 "[\n"
                                 "  {\"command\": \";pipe\", \"body\": \"logic bad;\"},\n"
                                 "  {\"command\": \";;bad_slot\", \"body\": \"logic x;\", \"slots\": [{\"name\": \"x\", \"start\": 99, \"length\": 1}]},\n"
                                 "  {\"command\": \";;l\", \"body\": \"logic should_not_override;\"},\n"
                                 "  {\"command\": \";;h\", \"body\": \"reserved header;\"}\n"
                                 "]\n")),
               true);
    UserTemplateService invalidRecordTemplates(invalidRecordJson);
    const UserTemplateLoadReport invalidRecordReport =
        invalidRecordTemplates.reload();
    expectBool("UserTemplateService reports invalid records",
               !invalidRecordReport.valid
                   && invalidRecordReport.issues.size() >= 4
                   && invalidRecordTemplates.catalog().isEmpty(),
               true);

    UserTemplateRecord saveTemplate;
    saveTemplate.id = QStringLiteral("save_template");
    saveTemplate.commandToken = QStringLiteral(";;save");
    saveTemplate.description = QStringLiteral("saved JSON template");
    saveTemplate.insertText = QStringLiteral("logic saved;");
    UserTemplateService userTemplateSaveService(
        QDir(userTemplateJsonDir.path()).absoluteFilePath(
            QStringLiteral("saved_templates.json")));
    const UserTemplateSaveReport userTemplateSaveReport =
        userTemplateSaveService.setRecords({saveTemplate});
    expectBool("UserTemplateService saves valid JSON template",
               userTemplateSaveReport.valid
                   && userTemplateSaveService
                          .matchingTemplates(QStringLiteral(";;save"))
                          .size() == 1,
               true);
    UserTemplateRecord invalidTemplate = saveTemplate;
    invalidTemplate.commandToken = QStringLiteral(";save");
    const UserTemplateSaveReport invalidTokenReport =
        userTemplateSaveService.setRecords({invalidTemplate});
    expectBool("UserTemplateService rejects non-template token",
               !invalidTokenReport.valid
                   && !invalidTokenReport.failureReason.isEmpty()
                   && userTemplateSaveService.records().size() == 1,
               true);
    UserTemplateRecord duplicateTemplate = saveTemplate;
    duplicateTemplate.commandToken = QStringLiteral(";;other_save");
    const UserTemplateSaveReport duplicateReport =
        userTemplateSaveService.validateRecords(
            {saveTemplate, duplicateTemplate});
    expectBool("UserTemplateService rejects duplicate ids",
               !duplicateReport.valid
                   && duplicateReport.failureReason.contains(
                       QStringLiteral("unique"),
                       Qt::CaseInsensitive),
               true);
    expectBool("UserTemplateService rejects built-in override on save",
               !userTemplateSaveService
                    .validateRecords({UserTemplateRecord{
                        QString(),
                        QStringLiteral(";;l"),
                        QString(),
                        QStringLiteral("bad"),
                        QStringLiteral("logic bad;"),
                        -1,
                        0,
                        {}}})
                    .valid,
               true);
    expectBool("UserTemplateService leaves built-in templates separate",
               CodeTemplateService::getInstance()
                       ->templateForCommand(QStringLiteral(";;pipe"))
                       .insertText.isEmpty()
                   && CodeTemplateService::getInstance()
                          ->templateForCommand(QStringLiteral(";;l"),
                                               QStringLiteral("clk"))
                          .insertText
                       == QStringLiteral("logic clk;"),
               true);
    expectBool("User templates do not create ;cmd entry",
               !userTemplateCompletionService
                    .matchCommandMode(QStringLiteral(";pipe "))
                    .matched,
               true);
    expectBool("User templates do not alter Command Layer registry",
               findCommandLayerCommand(QStringLiteral("go package"))
                   != nullptr
                   && findCommandLayerCommand(QStringLiteral(";;pipe"))
                       == nullptr,
               true);
    const CommandLayerCommandMetadata* deleteLinesCommand =
        findCommandLayerCommand(QStringLiteral("delete lines"));
    const CommandLayerCommandMetadata* joinLinesCommand =
        findCommandLayerCommand(QStringLiteral("join lines"));
    expectBool("Command Layer maps line actions to canonical action ids",
               deleteLinesCommand
                   && deleteLinesCommand->actionId
                          == QStringLiteral("edit.deleteLines")
                   && deleteLinesCommand->executionRoute
                          == QStringLiteral("editor.lines.delete")
                   && joinLinesCommand
                   && joinLinesCommand->actionId
                          == QStringLiteral("edit.joinLines")
                   && joinLinesCommand->executionRoute
                          == QStringLiteral("editor.lines.join"),
               true);
    expectBool("User templates do not enter Global Control",
               GlobalControlService()
                   .query(QStringLiteral(";;pipe"))
                   .isEmpty(),
               true);

    QTemporaryDir customAbbreviationSettingsDir;
    expectBool("CustomAbbreviationService temp dir valid",
               customAbbreviationSettingsDir.isValid(),
               true);
    const QString customAbbreviationSettingsFile =
        QDir(customAbbreviationSettingsDir.path()).absoluteFilePath(
            QStringLiteral("custom_abbreviations.ini"));
    CustomAbbreviationService customAbbreviationService(
        customAbbreviationSettingsFile);
    CustomAbbreviationRecord logicAlias;
    logicAlias.id = QStringLiteral("logic_alias");
    logicAlias.abbreviation = QStringLiteral("lg");
    logicAlias.commandToken = QStringLiteral(";l");
    logicAlias.label = QStringLiteral("logic command");
    logicAlias.description = QStringLiteral("semantic logic shortcut");
    CustomAbbreviationRecord pipeAlias;
    pipeAlias.id = QStringLiteral("pipe_template_alias");
    pipeAlias.abbreviation = QStringLiteral("pt");
    pipeAlias.commandToken = QStringLiteral(";;pipe");
    pipeAlias.label = QStringLiteral("pipe template");
    pipeAlias.description = QStringLiteral("template shortcut");

    const CustomAbbreviationSaveReport abbreviationSaveReport =
        customAbbreviationService.setRecords({logicAlias, pipeAlias});
    expectBool("CustomAbbreviationService saves valid aliases",
               abbreviationSaveReport.valid
                   && abbreviationSaveReport.records.size() == 2,
               true);
    CustomAbbreviationService reloadedAbbreviations(
        customAbbreviationSettingsFile);
    const CustomAbbreviationResolution logicResolution =
        reloadedAbbreviations.resolveForIntent(
            QStringLiteral("LG"),
            InlineCommandIntent::SemanticCompletion);
    expectBool("CustomAbbreviationService resolves ;cmd alias",
               logicResolution.matched
                   && logicResolution.record.commandToken == QStringLiteral(";l")
                   && logicResolution.intent
                       == InlineCommandIntent::SemanticCompletion,
               true);
    const CustomAbbreviationResolution pipeResolution =
        reloadedAbbreviations.resolveForIntent(
            QStringLiteral("pt"),
            InlineCommandIntent::CodeTemplate);
    expectBool("CustomAbbreviationService resolves ;;cmd alias",
               pipeResolution.matched
                   && pipeResolution.record.commandToken
                       == QStringLiteral(";;pipe")
                   && pipeResolution.intent == InlineCommandIntent::CodeTemplate,
               true);
    expectBool("CustomAbbreviationService intent filter separates aliases",
               !reloadedAbbreviations
                    .resolveForIntent(QStringLiteral("pt"),
                                      InlineCommandIntent::SemanticCompletion)
                    .matched,
               true);
    const QList<CustomAbbreviationRecord> prefixMatches =
        reloadedAbbreviations.matchingRecords(QStringLiteral("p"));
    expectBool("CustomAbbreviationService prefix query",
               prefixMatches.size() == 1
                   && prefixMatches.first().commandToken
                       == QStringLiteral(";;pipe"),
               true);

    CustomAbbreviationRecord invalidActionAlias = logicAlias;
    invalidActionAlias.id = QStringLiteral("fold_action_alias");
    invalidActionAlias.abbreviation = QStringLiteral("fd");
    invalidActionAlias.commandToken = QStringLiteral(";:fd");
    const CustomAbbreviationSaveReport invalidActionReport =
        customAbbreviationService.setRecords({invalidActionAlias});
    expectBool("CustomAbbreviationService rejects ;: namespace",
               !invalidActionReport.valid
                   && !invalidActionReport.failureReason.isEmpty()
                   && reloadedAbbreviations.records().size() == 2,
               true);
    CustomAbbreviationRecord duplicateAlias = pipeAlias;
    duplicateAlias.id = QStringLiteral("other_pipe_template_alias");
    duplicateAlias.abbreviation = QStringLiteral("LG");
    duplicateAlias.commandToken = QStringLiteral(";;m");
    const CustomAbbreviationSaveReport duplicateAliasReport =
        customAbbreviationService.validateRecords({logicAlias, duplicateAlias});
    expectBool("CustomAbbreviationService rejects duplicate aliases",
               !duplicateAliasReport.valid
                   && duplicateAliasReport.failureReason.contains(
                       QStringLiteral("unique"),
                       Qt::CaseInsensitive),
               true);
    expectBool("CustomAbbreviationService leaves built-in ;cmd unchanged",
               CompletionService::getInstance()
                   ->matchCommandMode(QStringLiteral(";l "))
                   .matched,
               true);
    expectEq("CustomAbbreviationService leaves built-in ;;cmd unchanged",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("clk"))
                 .insertText,
             QStringLiteral("logic clk;"));

    QTemporaryDir foldShelfSettingsDir;
    expectBool("FoldShelfPersistence temp dir valid",
               foldShelfSettingsDir.isValid(),
               true);
    const QString foldShelfSettingsFile =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("fold_shelf.ini"));
    const QString foldShelfWorkspaceA =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("workspace_a"));
    const QString foldShelfWorkspaceB =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("workspace_b"));
    const QString foldShelfWorkspaceC =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("workspace_c"));
    FoldShelfPersistenceService foldShelfPersistence(
        foldShelfSettingsFile);
    FoldBlockShelfModel persistentFoldShelf;
    persistentFoldShelf.setPersistenceService(&foldShelfPersistence);
    persistentFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);

    FoldShelfItem persistentFoldItem;
    persistentFoldItem.alias = QStringLiteral("pipe block");
    persistentFoldItem.text =
        QStringLiteral("// fold pipe block\nlogic valid;\n// endfold\n");
    persistentFoldItem.sourceFile =
        QDir(foldShelfWorkspaceA).absoluteFilePath(
            QStringLiteral("rtl/top.sv"));
    persistentFoldItem.sourceModule = QStringLiteral("top");
    persistentFoldItem.sourceStartLine = 10;
    persistentFoldItem.sourceEndLine = 12;
    persistentFoldItem.originKind = FoldShelfOriginKind::Moved;
    const QString persistentFoldId =
        persistentFoldShelf.addItem(persistentFoldItem);
    expectBool("FoldShelfPersistence saves add",
               !persistentFoldId.isEmpty()
                   && persistentFoldShelf.items().size() == 1,
               true);

    FoldBlockShelfModel reloadedFoldShelf;
    reloadedFoldShelf.setPersistenceService(&foldShelfPersistence);
    reloadedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    const FoldShelfItem reloadedFoldItem =
        reloadedFoldShelf.item(persistentFoldId);
    expectBool("FoldShelfPersistence reloads workspace item",
               reloadedFoldShelf.items().size() == 1
                   && reloadedFoldItem.alias == QStringLiteral("pipe block")
                   && reloadedFoldItem.text == persistentFoldItem.text
                   && reloadedFoldItem.sourceModule == QStringLiteral("top")
                   && reloadedFoldItem.sourceFile.endsWith(
                       QStringLiteral("workspace_a/rtl/top.sv"))
                   && reloadedFoldItem.lineCount == 3,
               true);

    FoldBlockShelfModel isolatedFoldShelf;
    isolatedFoldShelf.setPersistenceService(&foldShelfPersistence);
    isolatedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    expectBool("FoldShelfPersistence scopes by workspace",
               isolatedFoldShelf.items().isEmpty(),
               true);

    expectBool("FoldShelfPersistence saves consume",
               reloadedFoldShelf.consumeItem(persistentFoldId),
               true);
    FoldBlockShelfModel consumedFoldShelf;
    consumedFoldShelf.setPersistenceService(&foldShelfPersistence);
    consumedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence reloads consumed item",
               consumedFoldShelf.item(persistentFoldId).consumed,
               true);

    expectBool("FoldShelfPersistence saves stale mark",
               consumedFoldShelf.markItemStale(persistentFoldId),
               true);
    FoldBlockShelfModel staleFoldShelf;
    staleFoldShelf.setPersistenceService(&foldShelfPersistence);
    staleFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence reloads stale item",
               staleFoldShelf.item(persistentFoldId).stale,
               true);

    expectBool("FoldShelfPersistence saves remove",
               staleFoldShelf.removeItem(persistentFoldId),
               true);
    FoldBlockShelfModel removedFoldShelf;
    removedFoldShelf.setPersistenceService(&foldShelfPersistence);
    removedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence reloads removed empty",
               removedFoldShelf.items().isEmpty(),
               true);

    const QString clearFoldId =
        removedFoldShelf.addItem(persistentFoldItem);
    expectBool("FoldShelfPersistence re-add before clear",
               !clearFoldId.isEmpty()
                   && !removedFoldShelf.items().isEmpty(),
               true);
    removedFoldShelf.clear();
    FoldBlockShelfModel clearedFoldShelf;
    clearedFoldShelf.setPersistenceService(&foldShelfPersistence);
    clearedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence saves clear",
               clearedFoldShelf.items().isEmpty(),
               true);

    FoldBlockShelfModel crossFileFoldShelf;
    crossFileFoldShelf.setPersistenceService(&foldShelfPersistence);
    crossFileFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    const QString crossFileFoldId =
        crossFileFoldShelf.addItem(persistentFoldItem);
    MyCodeEditor crossFileTargetEditor;
    crossFileTargetEditor.setDocumentFileName(
        QDir(foldShelfWorkspaceB).absoluteFilePath(
            QStringLiteral("rtl/target.sv")));
    crossFileTargetEditor.setPlainText(
        QStringLiteral("module target;\nendmodule\n"));
    const FoldShelfRestoreReport crossFileRestoreReport =
        FoldShelfRestoreService::restoreIntoEditor(
            &crossFileFoldShelf,
            &crossFileTargetEditor,
            crossFileFoldId,
            1,
            FoldShelfRestoreCompletion::ConsumeItem);
    expectBool("FoldShelfRestore cross-file consumes after insert",
               crossFileRestoreReport.success
                   && crossFileRestoreReport.inserted
                   && crossFileRestoreReport.itemConsumed
                   && crossFileFoldShelf.item(crossFileFoldId).consumed
                   && crossFileTargetEditor.toPlainText().contains(
                       QStringLiteral("// fold pipe block"))
                   && QDir::fromNativeSeparators(
                          crossFileTargetEditor.documentFileName())
                          .endsWith(QStringLiteral(
                              "workspace_b/rtl/target.sv")),
               true);
    FoldBlockShelfModel crossFileReloadedShelf;
    crossFileReloadedShelf.setPersistenceService(&foldShelfPersistence);
    crossFileReloadedShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfRestore persists cross-file consume",
               crossFileReloadedShelf.item(crossFileFoldId).consumed,
               true);

    FoldShelfItem staleCandidate = persistentFoldItem;
    staleCandidate.alias = QStringLiteral("stale candidate");
    FoldBlockShelfModel failedCrossFileShelf;
    failedCrossFileShelf.setPersistenceService(&foldShelfPersistence);
    failedCrossFileShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    const QString staleFoldId = failedCrossFileShelf.addItem(staleCandidate);
    const FoldShelfRestoreReport failedCrossFileReport =
        FoldShelfRestoreService::restoreIntoEditor(
            &failedCrossFileShelf,
            nullptr,
            staleFoldId,
            -1,
            FoldShelfRestoreCompletion::ConsumeItem);
    expectBool("FoldShelfRestore marks failed target stale",
               !failedCrossFileReport.success
                   && failedCrossFileReport.staleMarked
                   && failedCrossFileReport.failureReason.contains(
                       QStringLiteral("active editor"),
                       Qt::CaseInsensitive),
               true);
    FoldBlockShelfModel staleReloadedShelf;
    staleReloadedShelf.setPersistenceService(&foldShelfPersistence);
    staleReloadedShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    expectBool("FoldShelfRestore persists failed target stale",
               staleReloadedShelf.item(staleFoldId).stale,
               true);

    FoldBlockShelfModel removeRestoreShelf;
    removeRestoreShelf.setPersistenceService(&foldShelfPersistence);
    removeRestoreShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    const QString removeRestoreId =
        removeRestoreShelf.addItem(persistentFoldItem);
    MyCodeEditor removeRestoreEditor;
    removeRestoreEditor.setPlainText(
        QStringLiteral("module remove_target;\nendmodule\n"));
    const FoldShelfRestoreReport removeRestoreReport =
        FoldShelfRestoreService::restoreIntoEditor(
            &removeRestoreShelf,
            &removeRestoreEditor,
            removeRestoreId,
            1,
            FoldShelfRestoreCompletion::RemoveItem);
    expectBool("FoldShelfRestore removes after successful insert",
               removeRestoreReport.success
                   && removeRestoreReport.itemRemoved
                   && removeRestoreShelf.item(removeRestoreId).id.isEmpty()
                   && removeRestoreEditor.toPlainText().contains(
                       QStringLiteral("logic valid")),
               true);

    FoldBlockShelfModel managedFoldShelf;
    managedFoldShelf.setPersistenceService(&foldShelfPersistence);
    managedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceC);
    FoldShelfItem activeManagedItem = persistentFoldItem;
    activeManagedItem.alias = QStringLiteral("active pipe");
    activeManagedItem.sourceModule = QStringLiteral("alpha_top");
    activeManagedItem.consumed = false;
    activeManagedItem.stale = false;
    const QString activeManagedId =
        managedFoldShelf.addItem(activeManagedItem);
    FoldShelfItem consumedManagedItem = persistentFoldItem;
    consumedManagedItem.alias = QStringLiteral("done response");
    consumedManagedItem.sourceModule = QStringLiteral("beta_mod");
    const QString consumedManagedId =
        managedFoldShelf.addItem(consumedManagedItem);
    FoldShelfItem staleManagedItem = persistentFoldItem;
    staleManagedItem.alias = QStringLiteral("stale cache");
    staleManagedItem.sourceModule = QStringLiteral("gamma_mod");
    const QString staleManagedId =
        managedFoldShelf.addItem(staleManagedItem);
    expectBool("FoldShelfManagement marks setup states",
               managedFoldShelf.consumeItem(consumedManagedId)
                   && managedFoldShelf.markItemStale(staleManagedId),
               true);
    expectBool("FoldShelfManagement renames item",
               managedFoldShelf.renameItem(
                   activeManagedId,
                   QStringLiteral("  renamed pipe  "))
                   && managedFoldShelf.item(activeManagedId).alias
                          == QStringLiteral("renamed pipe"),
               true);
    expectBool("FoldShelfManagement rejects blank rename",
               !managedFoldShelf.renameItem(activeManagedId,
                                            QStringLiteral("   "))
                   && managedFoldShelf.item(activeManagedId).alias
                          == QStringLiteral("renamed pipe"),
               true);
    FoldBlockShelfModel renamedManagedReload;
    renamedManagedReload.setPersistenceService(&foldShelfPersistence);
    renamedManagedReload.setWorkspaceRoot(foldShelfWorkspaceC);
    expectBool("FoldShelfManagement persists rename",
               renamedManagedReload.item(activeManagedId).alias
                   == QStringLiteral("renamed pipe"),
               true);
    const QList<FoldShelfItem> alphaMatches =
        managedFoldShelf.itemsMatching(QStringLiteral("renamed alpha"));
    expectBool("FoldShelfManagement filters by terms",
               alphaMatches.size() == 1
                   && alphaMatches.first().id == activeManagedId,
               true);
    const QList<FoldShelfItem> staleMatches =
        managedFoldShelf.itemsMatching(QStringLiteral("stale gamma"));
    expectBool("FoldShelfManagement filters stale item",
               staleMatches.size() == 1
                   && staleMatches.first().id == staleManagedId,
               true);
    const int cleanedManagedCount =
        managedFoldShelf.removeConsumedOrStaleItems();
    expectBool("FoldShelfManagement cleans consumed and stale",
               cleanedManagedCount == 2
                   && managedFoldShelf.items().size() == 1
                   && managedFoldShelf.item(activeManagedId).id
                          == activeManagedId
                   && managedFoldShelf.item(consumedManagedId).id.isEmpty()
                   && managedFoldShelf.item(staleManagedId).id.isEmpty(),
               true);
    FoldBlockShelfModel cleanedManagedReload;
    cleanedManagedReload.setPersistenceService(&foldShelfPersistence);
    cleanedManagedReload.setWorkspaceRoot(foldShelfWorkspaceC);
    expectBool("FoldShelfManagement persists clean",
               cleanedManagedReload.items().size() == 1
                   && cleanedManagedReload.item(activeManagedId).id
                          == activeManagedId,
               true);
    expectBool("FoldShelfManagement clean no-op",
               managedFoldShelf.removeConsumedOrStaleItems() == 0,
               true);

    const CodeTemplateItem widthLogic =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;l"), QStringLiteral("8 sig"));
    expectEq("CodeTemplateService logic width before name",
             widthLogic.insertText,
             QStringLiteral("logic [7:0] sig;"));
    expectBool("CodeTemplateService logic selects name",
               widthLogic.selectionStart == widthLogic.insertText.indexOf(QStringLiteral("sig"))
                   && widthLogic.selectionLength == QStringLiteral("sig").size()
                   && widthLogic.templateSlots.size() == 1
                   && widthLogic.templateSlots.first().name == QStringLiteral("name")
                   && widthLogic.templateSlots.first().start == widthLogic.selectionStart
                   && widthLogic.templateSlots.first().length == widthLogic.selectionLength,
               true);
    expectEq("CodeTemplateService logic unpacked dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("8 sig 8"))
                 .insertText,
             QStringLiteral("logic [7:0] sig [7:0];"));
    expectEq("CodeTemplateService logic multi packed dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("8 8 sig"))
                 .insertText,
             QStringLiteral("logic [7:0][7:0] sig;"));
    expectEq("CodeTemplateService logic parameter dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral(":P_W sig 9"))
                 .insertText,
             QStringLiteral("logic [P_W - 1:0] sig [8:0];"));
    expectEq("CodeTemplateService logic exact range dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral(":PW+DW:0 sig"))
                 .insertText,
             QStringLiteral("logic [PW+DW:0] sig;"));
    expectEq("CodeTemplateService logic signed dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("-s :P_W sig"))
                 .insertText,
             QStringLiteral("logic signed [P_W - 1:0] sig;"));
    const CodeTemplateItem widthWire =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;w"), QStringLiteral("8 net_sig 4"));
    expectEq("CodeTemplateService wire dimensions",
             widthWire.insertText,
             QStringLiteral("wire [7:0] net_sig [3:0];"));
    expectBool("CodeTemplateService wire selects name",
               widthWire.selectionStart
                       == widthWire.insertText.indexOf(QStringLiteral("net_sig"))
                   && widthWire.selectionLength
                       == QStringLiteral("net_sig").size()
                   && widthWire.templateSlots.size() == 1
                   && widthWire.templateSlots.first().start
                       == widthWire.selectionStart,
               true);
    expectEq("CodeTemplateService reg signed dimensions",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;r"),
                                      QStringLiteral("-s :P_W state 8"))
                 .insertText,
             QStringLiteral("reg signed [P_W - 1:0] state [7:0];"));
    expectEq("CodeTemplateService reg exact range dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;r"),
                                      QStringLiteral(":PW+DW:0 state"))
                 .insertText,
             QStringLiteral("reg [PW+DW:0] state;"));
    const CodeTemplateItem regTemplate =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;r"), QStringLiteral(":PW+DW:0 state"));
    expectBool("CodeTemplateService reg carries name slot",
               regTemplate.templateSlots.size() == 1
                   && regTemplate.templateSlots.first().name
                       == QStringLiteral("name")
                   && regTemplate.templateSlots.first().start
                       == regTemplate.selectionStart
                   && regTemplate.templateSlots.first().length
                       == regTemplate.selectionLength,
               true);
    const CommandModeCompletionState widthTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;l 8 data")});
    expectBool("CompletionService template carries selection",
               widthTemplateState.matched
                   && !widthTemplateState.templateItems.isEmpty()
                   && widthTemplateState.templateItems.first().insertText
                       == QStringLiteral("logic [7:0] data;")
                   && widthTemplateState.templateItems.first().selectionStart
                       == QStringLiteral("logic [7:0] ").size()
                   && widthTemplateState.templateItems.first().selectionLength
                       == QStringLiteral("data").size()
                   && widthTemplateState.templateItems.first().templateSlots.size()
                       == 1,
               true);
    const CommandModeCompletionState wireTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;w 8 net_sig")});
    expectBool("CompletionService wire template carries selection",
               wireTemplateState.matched
                   && !wireTemplateState.templateItems.isEmpty()
                   && wireTemplateState.templateItems.first().insertText
                       == QStringLiteral("wire [7:0] net_sig;")
                   && wireTemplateState.templateItems.first().selectionStart
                       == QStringLiteral("wire [7:0] ").size()
                   && wireTemplateState.templateItems.first().selectionLength
                       == QStringLiteral("net_sig").size()
                   && wireTemplateState.templateItems.first().templateSlots.size()
                       == 1,
               true);
    CompletionActivationQuery signalTemplateActivation;
    signalTemplateActivation.selectable = true;
    signalTemplateActivation.defaultValue = widthLogic.insertText;
    signalTemplateActivation.selectionStart = widthLogic.selectionStart;
    signalTemplateActivation.selectionLength = widthLogic.selectionLength;
    signalTemplateActivation.templateSlots = widthLogic.templateSlots;
    const CompletionActivationState signalTemplateActivationState =
        CompletionService::getInstance()->completionActivationState(
            signalTemplateActivation);
    expectBool("CompletionService signal activation carries slot",
               signalTemplateActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && signalTemplateActivationState.clearCommandMode
                   && signalTemplateActivationState.hidePopup
                   && signalTemplateActivationState.templateSlots.size() == 1,
               true);
    MyCodeEditor signalSlotEditor;
    signalSlotEditor.setPlainText(widthLogic.insertText);
    signalSlotEditor.startTemplateSlotMode(0,
                                           widthLogic.insertText.size(),
                                           widthLogic.templateSlots);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("Signal Slot Mode starts on name",
               signalSlotEditor.templateSlotModeActive()
                   && signalSlotEditor.templateSlotModeActiveIndex() == 0
                   && signalSlotEditor.textCursor().selectedText()
                       == QStringLiteral("sig"),
               true);
    insertAtEditorCursor(signalSlotEditor, QStringLiteral("data_bus"));
    expectBool("Signal Slot Mode edit updates name",
               signalSlotEditor.toPlainText()
                       == QStringLiteral("logic [7:0] data_bus;")
                   && signalSlotEditor.templateSlotModeActive(),
               true);
    expectBool("Signal Slot Mode Tab cycles single slot",
               sendEditorKey(signalSlotEditor, Qt::Key_Tab)
                   && signalSlotEditor.templateSlotModeActive()
                   && signalSlotEditor.templateSlotModeActiveIndex() == 0
                   && signalSlotEditor.textCursor().selectedText()
                       == QStringLiteral("data_bus"),
               true);
    expectBool("Signal Slot Mode Esc exits",
               sendEditorKey(signalSlotEditor, Qt::Key_Escape)
                   && !signalSlotEditor.templateSlotModeActive(),
               true);
    const CodeTemplateItem parameterScalar =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;p"), QStringLiteral("WIDTH"));
    expectEq("CodeTemplateService parameter scalar",
             parameterScalar.insertText,
             QStringLiteral("parameter WIDTH = ;"));
    expectBool("CodeTemplateService parameter slots",
               parameterScalar.selectionStart
                       == parameterScalar.insertText.indexOf(QStringLiteral("WIDTH"))
                   && parameterScalar.selectionLength == QStringLiteral("WIDTH").size()
                   && parameterScalar.templateSlots.size() == 2
                   && parameterScalar.templateSlots.at(0).name == QStringLiteral("name")
                   && parameterScalar.templateSlots.at(0).start
                       == parameterScalar.insertText.indexOf(QStringLiteral("WIDTH"))
                   && parameterScalar.templateSlots.at(0).length
                       == QStringLiteral("WIDTH").size()
                   && parameterScalar.templateSlots.at(1).name == QStringLiteral("value")
                   && parameterScalar.templateSlots.at(1).start
                       == QStringLiteral("parameter WIDTH = ").size()
                   && parameterScalar.templateSlots.at(1).length == 0,
               true);
    expectBool("CodeTemplateService parameter primary slot selects name",
               parameterScalar.selectionStart
                       == parameterScalar.templateSlots.at(0).start
                   && parameterScalar.selectionLength
                       == parameterScalar.templateSlots.at(0).length,
               true);
    const CodeTemplateItem parameterArray =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;p"), QStringLiteral("8 test 8"));
    expectEq("CodeTemplateService parameter array",
             parameterArray.insertText,
             QStringLiteral("parameter [7:0] test [7:0] = '{};"));
    expectBool("CodeTemplateService parameter array slots",
               parameterArray.templateSlots.size() == 2
                   && parameterArray.templateSlots.at(0).start
                       == parameterArray.insertText.indexOf(QStringLiteral("test"))
                   && parameterArray.templateSlots.at(0).length
                       == QStringLiteral("test").size()
                   && parameterArray.templateSlots.at(1).start
                       == parameterArray.insertText.indexOf(QStringLiteral("{}")) + 1
                   && parameterArray.templateSlots.at(1).length == 0,
               true);
    expectEq("CodeTemplateService typed parameter array",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;p"),
                                      QStringLiteral("-logic 8 test 8"))
                 .insertText,
             QStringLiteral("parameter logic [7:0] test [7:0] = '{};"));
    expectEq("CodeTemplateService localparam typed scalar",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;lp"),
                                      QStringLiteral("-integer DEPTH"))
                 .insertText,
             QStringLiteral("localparam integer DEPTH = ;"));
    expectEq("CodeTemplateService localparam typed array",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;lp"),
                                      QStringLiteral("-bit :P_W LUT 16"))
                 .insertText,
             QStringLiteral("localparam bit [P_W - 1:0] LUT [15:0] = '{};"));
    const CommandModeCompletionState parameterTypeState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;p -")});
    expectBool("CompletionService parameter type suggestions",
               parameterTypeState.matched
                   && parameterTypeState.templateItems.size() >= 4
                   && parameterTypeState.templateItems.first().label
                       == QStringLiteral("int")
                   && parameterTypeState.templateItems.first().insertText
                       == QStringLiteral(";;p -int "),
               true);
    const CommandModeCompletionState fuzzyParameterTypeState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;p -lc")});
    expectBool("CompletionService parameter type fuzzy suggestions",
               fuzzyParameterTypeState.matched
                   && fuzzyParameterTypeState.templateItems.size() == 1
                   && fuzzyParameterTypeState.templateItems.first().label
                       == QStringLiteral("logic")
                   && fuzzyParameterTypeState.templateItems.first().insertText
                       == QStringLiteral(";;p -logic "),
               true);
    CompletionActivationQuery parameterTypeActivation;
    parameterTypeActivation.selectable = true;
    parameterTypeActivation.itemText = QStringLiteral("logic");
    parameterTypeActivation.defaultValue = QStringLiteral(";;p -logic ");
    const CompletionActivationState parameterTypeActivationState =
        CompletionService::getInstance()->completionActivationState(
            parameterTypeActivation);
    expectBool("CompletionService parameter type keeps command mode",
               parameterTypeActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && parameterTypeActivationState.text
                       == QStringLiteral(";;p -logic ")
                   && !parameterTypeActivationState.clearCommandMode
                   && !parameterTypeActivationState.hidePopup,
               true);

    const CommandModeCompletionState parameterTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;p WIDTH")});
    expectBool("CompletionService parameter template carries slots",
               parameterTemplateState.matched
                   && !parameterTemplateState.templateItems.isEmpty()
                   && parameterTemplateState.templateItems.first().templateSlots.size() == 2
                   && parameterTemplateState.templateItems.first().templateSlots.at(0).name
                       == QStringLiteral("name")
                   && parameterTemplateState.templateItems.first().templateSlots.at(1).name
                       == QStringLiteral("value"),
               true);
    CompletionActivationQuery parameterTemplateActivation;
    parameterTemplateActivation.selectable = true;
    parameterTemplateActivation.defaultValue = parameterScalar.insertText;
    parameterTemplateActivation.selectionStart = parameterScalar.selectionStart;
    parameterTemplateActivation.selectionLength = parameterScalar.selectionLength;
    parameterTemplateActivation.templateSlots = parameterScalar.templateSlots;
    const CompletionActivationState parameterTemplateActivationState =
        CompletionService::getInstance()->completionActivationState(
            parameterTemplateActivation);
    expectBool("CompletionService parameter activation carries slots",
               parameterTemplateActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && parameterTemplateActivationState.clearCommandMode
                   && parameterTemplateActivationState.hidePopup
                   && parameterTemplateActivationState.templateSlots.size() == 2,
               true);

    MyCodeEditor slotEditor;
    slotEditor.setPlainText(parameterScalar.insertText);
    slotEditor.startTemplateSlotMode(0,
                                     parameterScalar.insertText.size(),
                                     parameterScalar.templateSlots);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("Slot Mode starts on parameter name",
               slotEditor.templateSlotModeActive()
                   && slotEditor.templateSlotModeActiveIndex() == 0
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("WIDTH"),
               true);
    insertAtEditorCursor(slotEditor, QStringLiteral("DEPTH"));
    expectBool("Slot Mode edit updates active slot",
               slotEditor.toPlainText()
                       == QStringLiteral("parameter DEPTH = ;")
                   && slotEditor.templateSlotModeActive(),
               true);
    expectBool("Slot Mode Tab advances to value",
               sendEditorKey(slotEditor, Qt::Key_Tab)
                   && slotEditor.templateSlotModeActiveIndex() == 1
                   && !slotEditor.textCursor().hasSelection()
                   && slotEditor.textCursor().position()
                       == slotEditor.toPlainText().indexOf(QStringLiteral(";")),
               true);
    insertAtEditorCursor(slotEditor, QStringLiteral("8"));
    expectBool("Slot Mode Shift+Tab returns to name",
               sendEditorKey(slotEditor, Qt::Key_Tab, Qt::ShiftModifier)
                   && slotEditor.templateSlotModeActiveIndex() == 0
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("DEPTH"),
               true);
    expectBool("Slot Mode Tab returns to value",
               sendEditorKey(slotEditor, Qt::Key_Tab)
                   && slotEditor.templateSlotModeActiveIndex() == 1
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("8"),
               true);
    expectBool("Slot Mode final Tab cycles to name",
               sendEditorKey(slotEditor, Qt::Key_Tab)
                   && slotEditor.templateSlotModeActive()
                   && slotEditor.templateSlotModeActiveIndex() == 0
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("DEPTH"),
               true);
    const QString slotTextBeforeBacktab = slotEditor.toPlainText();
    expectBool("Slot Mode Shift+Tab cycles to final slot",
               sendEditorKey(slotEditor, Qt::Key_Backtab, Qt::ShiftModifier)
                   && slotEditor.templateSlotModeActive()
                   && slotEditor.templateSlotModeActiveIndex() == 1
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("8")
                   && slotEditor.toPlainText() == slotTextBeforeBacktab,
               true);
    expectBool("Slot Mode Backtab does not insert tab",
               !slotEditor.toPlainText().contains(QLatin1Char('\t')),
               true);
    QEventLoop slotBlinkLoop;
    QTimer::singleShot(600, &slotBlinkLoop, &QEventLoop::quit);
    slotBlinkLoop.exec();
    expectBool("Slot Mode blink timer toggles",
               slotEditor.templateSlotModeActive()
                   && !slotEditor.templateSlotModeBlinkOnForTest(),
               true);
    expectBool("Slot Mode Esc exits after cycling",
               sendEditorKey(slotEditor, Qt::Key_Escape)
                   && !slotEditor.templateSlotModeActive(),
               true);

    MyCodeEditor slotCancelEditor;
    slotCancelEditor.setPlainText(parameterScalar.insertText);
    slotCancelEditor.startTemplateSlotMode(0,
                                           parameterScalar.insertText.size(),
                                           parameterScalar.templateSlots);
    expectBool("Slot Mode Esc cancels without rollback",
               sendEditorKey(slotCancelEditor, Qt::Key_Escape)
                   && !slotCancelEditor.templateSlotModeActive()
                   && slotCancelEditor.toPlainText() == parameterScalar.insertText,
               true);

    const QString linkedSlotText =
        QStringLiteral("posedge rst_n; if (!rst_n) begin\n    \nend");
    CodeTemplateSlotList linkedSlots;
    CodeTemplateSlot firstReset;
    firstReset.name = QStringLiteral("reset");
    firstReset.start =
        linkedSlotText.indexOf(QStringLiteral("rst_n"));
    firstReset.length = QStringLiteral("rst_n").size();
    firstReset.tabStop = 1;
    linkedSlots.append(firstReset);
    CodeTemplateSlot secondReset = firstReset;
    secondReset.start =
        linkedSlotText.indexOf(QStringLiteral("rst_n"),
                               firstReset.start + firstReset.length);
    linkedSlots.append(secondReset);
    CodeTemplateSlot linkedBody;
    linkedBody.name = QStringLiteral("body");
    linkedBody.start =
        linkedSlotText.indexOf(QStringLiteral("\n    \n")) + 5;
    linkedBody.length = 0;
    linkedBody.tabStop = 2;
    linkedBody.visibleWhenEmpty = true;
    linkedSlots.append(linkedBody);

    MyCodeEditor linkedSlotEditor;
    linkedSlotEditor.setPlainText(linkedSlotText);
    linkedSlotEditor.startTemplateSlotMode(
        0, linkedSlotText.size(), linkedSlots);
    expectBool("linked slots share one logical tab stop",
               linkedSlotEditor.templateSlotModeActive()
                   && linkedSlotEditor.templateSlotModeSlotCount() == 2
                   && linkedSlotEditor.templateSlotModeActiveIndex() == 0
                   && linkedSlotEditor.textCursor().selectedText()
                       == QStringLiteral("rst_n"),
               true);
    insertAtEditorCursor(linkedSlotEditor,
                         QStringLiteral("reset_n"));
    expectBool("editing a linked slot updates every physical range",
               linkedSlotEditor.toPlainText()
                   == QStringLiteral(
                       "posedge reset_n; if (!reset_n) begin\n    \nend")
                   && linkedSlotEditor.templateSlotModeActive()
                   && linkedSlotEditor.templateSlotModeSlotCount() == 2,
               true);
    expectBool("linked slot Tab skips duplicate range",
               sendEditorKey(linkedSlotEditor, Qt::Key_Tab)
                   && linkedSlotEditor.templateSlotModeActiveIndex() == 1
                   && !linkedSlotEditor.textCursor().hasSelection(),
               true);
    expectBool("linked slot edit is one undo transaction",
               sendEditorKey(linkedSlotEditor,
                             Qt::Key_Z,
                             Qt::ControlModifier)
                   && linkedSlotEditor.toPlainText() == linkedSlotText,
               true);

    MyCodeEditor slotStaleEditor;
    slotStaleEditor.setPlainText(parameterScalar.insertText);
    slotStaleEditor.startTemplateSlotMode(0,
                                          parameterScalar.insertText.size(),
                                          parameterScalar.templateSlots);
    QTextCursor staleCursor(slotStaleEditor.document());
    staleCursor.setPosition(slotStaleEditor.document()->characterCount() - 1);
    slotStaleEditor.setTextCursor(staleCursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("Slot Mode cursor outside exits",
               !slotStaleEditor.templateSlotModeActive(),
               true);

    const RtlClearAssignmentRhsReport clearRhsReport =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{
                QStringLiteral("a <= xxx;\n"
                               "b = foo(bar);\n"
                               "assign c = d ? e : f;\n"),
                100});
    expectBool("RtlBatch clear RHS report ready",
               clearRhsReport.canApply()
                   && clearRhsReport.edits.size() == 3
                   && clearRhsReport.templateSlots.size() == 3,
               true);
    expectEq("RtlBatch clear RHS replacement",
             clearRhsReport.replacementText,
             QStringLiteral("a <= ;\n"
                            "b = ;\n"
                            "assign c = ;\n"));
    expectBool("RtlBatch clear RHS slots",
               clearRhsReport.templateSlots.at(0).name == QStringLiteral("rhs1")
                   && clearRhsReport.templateSlots.at(0).start
                       == clearRhsReport.replacementText
                              .indexOf(QStringLiteral(";"))
                   && clearRhsReport.edits.at(0).documentRhsStart
                       == clearRhsReport.edits.at(0).selectionRhsStart + 100,
               true);
    const RtlClearAssignmentRhsReport clearRhsDeclaration =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{QStringLiteral("logic a = b;"), 0});
    expectBool("RtlBatch clear RHS rejects declaration",
               clearRhsDeclaration.status
                   == RtlClearAssignmentRhsStatus::UnsupportedSelection,
               true);
    const RtlClearAssignmentRhsReport clearRhsIncomplete =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{QStringLiteral("a <= b"), 0});
    expectBool("RtlBatch clear RHS rejects incomplete",
               clearRhsIncomplete.status
                   == RtlClearAssignmentRhsStatus::UnsupportedSelection,
               true);
    const RtlClearAssignmentRhsReport clearRhsEmpty =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{QStringLiteral("  // comment\n"), 0});
    expectBool("RtlBatch clear RHS empty selection",
               clearRhsEmpty.status
                   == RtlClearAssignmentRhsStatus::EmptySelection,
               true);
    MyCodeEditor clearRhsEditor;
    const QString clearRhsOriginal =
        QStringLiteral("module batch_demo;\n"
                       "  a <= foo;\n"
                       "  b = bar;\n"
                       "endmodule\n");
    clearRhsEditor.setPlainText(clearRhsOriginal);
    const int clearRhsStart = clearRhsOriginal.indexOf(QStringLiteral("  a"));
    const int clearRhsEnd =
        clearRhsOriginal.indexOf(QStringLiteral("endmodule"));
    QTextCursor clearRhsCursor(clearRhsEditor.document());
    clearRhsCursor.setPosition(clearRhsStart);
    clearRhsCursor.setPosition(clearRhsEnd, QTextCursor::KeepAnchor);
    clearRhsEditor.setTextCursor(clearRhsCursor);
    expectBool("Editor clear RHS applies selection",
               clearRhsEditor.clearSelectedAssignmentRhs(),
               true);
    expectEq("Editor clear RHS text",
             clearRhsEditor.toPlainText(),
             QStringLiteral("module batch_demo;\n"
                            "  a <= ;\n"
                            "  b = ;\n"
                            "endmodule\n"));
    expectBool("Editor clear RHS starts slot mode",
               clearRhsEditor.templateSlotModeActive()
                   && clearRhsEditor.templateSlotModeActiveIndex() == 0
                   && clearRhsEditor.textCursor().position()
                       == clearRhsEditor.toPlainText().indexOf(
                              QStringLiteral("a <= ;"))
                              + QStringLiteral("a <= ").size(),
               true);
    clearRhsEditor.undo();
    expectEq("Editor clear RHS undo restores original",
             clearRhsEditor.toPlainText(),
             clearRhsOriginal);

    MyCodeEditor clearPartialRhsEditor;
    const QString clearPartialRhsOriginal =
        QStringLiteral("module batch_partial;\n"
                       "  a <= foo;\n"
                       "  b = bar;\n"
                       "  c <= keep;\n"
                       "endmodule\n");
    clearPartialRhsEditor.setPlainText(clearPartialRhsOriginal);
    QTextCursor clearPartialRhsCursor(clearPartialRhsEditor.document());
    clearPartialRhsCursor.setPosition(
        clearPartialRhsOriginal.indexOf(QStringLiteral("foo")) + 1);
    clearPartialRhsCursor.setPosition(
        clearPartialRhsOriginal.indexOf(QStringLiteral("bar")) + 2,
        QTextCursor::KeepAnchor);
    clearPartialRhsEditor.setTextCursor(clearPartialRhsCursor);
    expectBool("Editor clear RHS expands partial selection to lines",
               clearPartialRhsEditor.clearSelectedAssignmentRhs(),
               true);
    expectEq("Editor clear RHS partial-line text",
             clearPartialRhsEditor.toPlainText(),
             QStringLiteral("module batch_partial;\n"
                            "  a <= ;\n"
                            "  b = ;\n"
                            "  c <= keep;\n"
                            "endmodule\n"));
    expectBool("Editor clear RHS partial-line slots",
               clearPartialRhsEditor.templateSlotModeActive()
                   && clearPartialRhsEditor.templateSlotModeSlotCount() == 2
                   && clearPartialRhsEditor.templateSlotModeActiveIndex() == 0,
               true);
    clearPartialRhsEditor.undo();
    expectEq("Editor clear RHS partial-line undo restores original",
             clearPartialRhsEditor.toPlainText(),
             clearPartialRhsOriginal);

    MyCodeEditor clearCurrentRhsEditor;
    const QString clearCurrentRhsOriginal =
        QStringLiteral("module batch_current;\n"
                       "  assign c = rhs_value;\n"
                       "endmodule\n");
    clearCurrentRhsEditor.setPlainText(clearCurrentRhsOriginal);
    QTextCursor clearCurrentRhsCursor(clearCurrentRhsEditor.document());
    clearCurrentRhsCursor.setPosition(
        clearCurrentRhsOriginal.indexOf(QStringLiteral("rhs_value")) + 3);
    clearCurrentRhsEditor.setTextCursor(clearCurrentRhsCursor);
    QString clearCurrentRhsMessage;
    expectBool("Editor clear RHS applies current assignment",
               clearCurrentRhsEditor.clearSelectedAssignmentRhs(
                   &clearCurrentRhsMessage),
               true);
    expectEq("Editor clear RHS current assignment text",
             clearCurrentRhsEditor.toPlainText(),
             QStringLiteral("module batch_current;\n"
                            "  assign c = ;\n"
                            "endmodule\n"));
    expectBool("Editor clear RHS current assignment slot",
               clearCurrentRhsEditor.templateSlotModeActive()
                   && clearCurrentRhsEditor.templateSlotModeActiveIndex() == 0
                   && clearCurrentRhsEditor.textCursor().position()
                       == clearCurrentRhsEditor.toPlainText().indexOf(
                              QStringLiteral("assign c = ;"))
                              + QStringLiteral("assign c = ").size()
                   && clearCurrentRhsMessage
                          == QStringLiteral("Cleared RHS for 1 assignment"),
               true);
    expectBool("Editor clear RHS current assignment Tab cycles",
               sendEditorKey(clearCurrentRhsEditor, Qt::Key_Tab)
                   && clearCurrentRhsEditor.templateSlotModeActive()
                   && clearCurrentRhsEditor.templateSlotModeActiveIndex() == 0,
               true);
    expectBool("Editor clear RHS current assignment Esc exits",
               sendEditorKey(clearCurrentRhsEditor, Qt::Key_Escape)
                   && !clearCurrentRhsEditor.templateSlotModeActive(),
               true);
    clearCurrentRhsEditor.undo();
    expectEq("Editor clear RHS current assignment undo restores original",
             clearCurrentRhsEditor.toPlainText(),
             clearCurrentRhsOriginal);

    MyCodeEditor clearRhsSlotEditor;
    clearRhsSlotEditor.setPlainText(clearRhsOriginal);
    QTextCursor clearRhsSlotCursor(clearRhsSlotEditor.document());
    clearRhsSlotCursor.setPosition(clearRhsStart);
    clearRhsSlotCursor.setPosition(clearRhsEnd, QTextCursor::KeepAnchor);
    clearRhsSlotEditor.setTextCursor(clearRhsSlotCursor);
    clearRhsSlotEditor.clearSelectedAssignmentRhs();
    insertAtEditorCursor(clearRhsSlotEditor, QStringLiteral("foo_next"));
    expectBool("Editor clear RHS slot edit shifts next",
               clearRhsSlotEditor.templateSlotModeActive()
                   && clearRhsSlotEditor.toPlainText().contains(
                       QStringLiteral("a <= foo_next;")),
               true);
    expectBool("Editor clear RHS Tab advances",
               sendEditorKey(clearRhsSlotEditor, Qt::Key_Tab)
                   && clearRhsSlotEditor.templateSlotModeActiveIndex() == 1
                   && clearRhsSlotEditor.textCursor().position()
                       == clearRhsSlotEditor.toPlainText()
                              .lastIndexOf(QStringLiteral(";")),
               true);
    insertAtEditorCursor(clearRhsSlotEditor, QStringLiteral("bar_next"));
    expectBool("Editor clear RHS final Tab cycles to first slot",
               sendEditorKey(clearRhsSlotEditor, Qt::Key_Tab)
                   && clearRhsSlotEditor.templateSlotModeActive()
                   && clearRhsSlotEditor.templateSlotModeActiveIndex() == 0
                   && clearRhsSlotEditor.toPlainText().contains(
                       QStringLiteral("b = bar_next;")),
               true);
    expectBool("Editor clear RHS Esc exits slot mode",
               sendEditorKey(clearRhsSlotEditor, Qt::Key_Escape)
                   && !clearRhsSlotEditor.templateSlotModeActive(),
               true);

    MyCodeEditor clearRhsRejectEditor;
    clearRhsRejectEditor.setPlainText(QStringLiteral("logic a = b;\n"));
    QTextCursor clearRhsRejectCursor(clearRhsRejectEditor.document());
    clearRhsRejectCursor.select(QTextCursor::Document);
    clearRhsRejectEditor.setTextCursor(clearRhsRejectCursor);
    expectBool("Editor clear RHS rejects declaration",
               !clearRhsRejectEditor.clearSelectedAssignmentRhs()
                   && clearRhsRejectEditor.toPlainText()
                       == QStringLiteral("logic a = b;\n"),
               true);

    MyCodeEditor clearRhsNoAssignmentEditor;
    const QString clearRhsNoAssignmentOriginal =
        QStringLiteral("module no_rhs;\nendmodule\n");
    clearRhsNoAssignmentEditor.setPlainText(clearRhsNoAssignmentOriginal);
    QTextCursor clearRhsNoAssignmentCursor(
        clearRhsNoAssignmentEditor.document());
    clearRhsNoAssignmentCursor.setPosition(
        clearRhsNoAssignmentOriginal.indexOf(QStringLiteral("no_rhs")));
    clearRhsNoAssignmentEditor.setTextCursor(clearRhsNoAssignmentCursor);
    QString clearRhsNoAssignmentMessage;
    expectBool("Editor clear RHS no assignment fails without edit",
               !clearRhsNoAssignmentEditor.clearSelectedAssignmentRhs(
                   &clearRhsNoAssignmentMessage)
                   && clearRhsNoAssignmentEditor.toPlainText()
                       == clearRhsNoAssignmentOriginal
                   && clearRhsNoAssignmentMessage
                       == QStringLiteral("No assignment RHS found"),
               true);

    const QString packageToolText =
        QStringLiteral("package pkg_tools;\n"
                       "    parameter int EXISTING = 1;\n"
                       "\n"
                       "endpackage : pkg_tools\n"
                       "\n"
                       "module outside_pkg;\n"
                       "endmodule\n");
    TSDocument packageToolDocument;
    packageToolDocument.setText(packageToolText);
    const TSPackageToolInsertTarget packageParameterTarget =
        packageToolDocument.packageToolInsertTarget(
            packageToolText.indexOf(QStringLiteral("EXISTING")),
            PackageToolKind::Parameter);
    expectBool("Package Tools appends parameter near same kind",
               packageParameterTarget.ok()
                   && packageParameterTarget.insertAfterLine
                   && packageParameterTarget.lineIndent == QStringLiteral("    ")
                   && packageParameterTarget.insertChar
                       == packageToolText.indexOf(QStringLiteral("\n\n")),
               true);
    const TSPackageToolInsertTarget packageLocalparamTarget =
        packageToolDocument.packageToolInsertTarget(
            packageToolText.indexOf(QStringLiteral("EXISTING")),
            PackageToolKind::Localparam);
    expectBool("Package Tools defaults before endpackage",
               packageLocalparamTarget.ok()
                   && !packageLocalparamTarget.insertAfterLine
                   && packageLocalparamTarget.insertChar
                       == packageToolText.indexOf(QStringLiteral("endpackage")),
               true);
    const TSPackageToolInsertTarget packageModuleRejectTarget =
        packageToolDocument.packageToolInsertTarget(
            packageToolText.indexOf(QStringLiteral("outside_pkg")),
            PackageToolKind::Parameter);
    expectBool("Package Tools rejects module scope",
               !packageModuleRejectTarget.ok()
                   && packageModuleRejectTarget.status
                       == TSPackageToolInsertStatus::InsideRtlScope,
               true);

    const PackageToolService packageToolService;
    const auto packageToolOrder = PackageToolService::toolOrder();
    expectBool("Package Tools first phase tool count",
               packageToolOrder.size() == 6,
               true);
    bool allPackageTemplatesHaveTextAndSlots = true;
    bool packedStructTemplateContainsPacked = false;
    for (PackageToolKind kind : packageToolOrder) {
        const CodeTemplateItem item = packageToolService.templateForKind(kind);
        allPackageTemplatesHaveTextAndSlots =
            allPackageTemplatesHaveTextAndSlots
            && !item.insertText.isEmpty()
            && !item.templateSlots.isEmpty();
        if (kind == PackageToolKind::TypedefStructPacked) {
            packedStructTemplateContainsPacked =
                item.insertText.contains(
                    QStringLiteral("typedef struct packed"));
        }
    }
    expectBool("Package Tools six templates have slots",
               allPackageTemplatesHaveTextAndSlots,
               true);
    expectBool("Package Tools packed struct text",
               packedStructTemplateContainsPacked,
               true);
    const CodeTemplateItem packageStructTemplate =
        packageToolService.templateForKind(PackageToolKind::TypedefStruct);
    expectBool("Package Tools struct template slots",
               packageStructTemplate.templateSlots.size() == 3
                   && packageStructTemplate.templateSlots.at(0).name
                       == QStringLiteral("field_type")
                   && packageStructTemplate.templateSlots.at(1).name
                       == QStringLiteral("field_name")
                   && packageStructTemplate.templateSlots.at(2).name
                       == QStringLiteral("type_name"),
               true);
    const CodeTemplateItem packageEnumIndented =
        packageToolService.templateForInsertion(
            PackageToolKind::TypedefEnum,
            QStringLiteral("    "),
            false);
    expectBool("Package Tools indentation remaps multiline slot",
               packageEnumIndented.insertText.startsWith(
                   QStringLiteral("    typedef enum"))
                   && packageEnumIndented.templateSlots.size() == 2
                   && packageEnumIndented.templateSlots.first().length
                       > QStringLiteral("IDLE,\n    BUSY").size(),
               true);

    MyCodeEditor packageToolEditor;
    const QString packageToolOriginal =
        QStringLiteral("package edit_pkg;\n"
                       "\n"
                       "endpackage\n");
    packageToolEditor.setPlainText(packageToolOriginal);
    QTextCursor packageToolCursor(packageToolEditor.document());
    packageToolCursor.setPosition(
        packageToolOriginal.indexOf(QStringLiteral("edit_pkg")));
    packageToolEditor.setTextCursor(packageToolCursor);
    QString packageToolMessage;
    expectBool("Package Tools inserts parameter and starts Slot Mode",
               packageToolEditor.executePackageToolInsert(
                   PackageToolKind::Parameter,
                   &packageToolMessage)
                   && packageToolEditor.templateSlotModeActive()
                   && packageToolEditor.templateSlotModeSlotCount() == 3
                   && packageToolEditor.templateSlotModeActiveIndex() == 0
                   && packageToolEditor.textCursor().selectedText()
                       == QStringLiteral("int")
                   && packageToolEditor.toPlainText().contains(
                       QStringLiteral("    parameter int PARAM = 0;\n"
                                      "endpackage")),
               true);
    insertAtEditorCursor(packageToolEditor, QStringLiteral("logic [7:0]"));
    expectBool("Package Tools slot edit shifts later slots",
               sendEditorKey(packageToolEditor, Qt::Key_Tab)
                   && packageToolEditor.templateSlotModeActiveIndex() == 1
                   && packageToolEditor.textCursor().selectedText()
                       == QStringLiteral("PARAM")
                   && packageToolEditor.toPlainText().contains(
                       QStringLiteral("parameter logic [7:0] PARAM = 0;")),
               true);
    MyCodeEditor packageToolNoPackageEditor;
    packageToolNoPackageEditor.setPlainText(QStringLiteral("logic loose;\n"));
    expectBool("Package Tools failure reports package absence",
               !packageToolNoPackageEditor.executePackageToolInsert(
                   PackageToolKind::Function,
                   &packageToolMessage)
                   && packageToolMessage == QStringLiteral("No current package"),
               true);

    MyCodeEditor selectInsideEditor;
    const QString selectInsideText =
        QStringLiteral("module select_inside;\n"
                       "  always_comb begin\n"
                       "    a = foo;\n"
                       "    if (en) begin\n"
                       "      b <= bar;\n"
                       "    end\n"
                       "  end\n"
                       "endmodule\n");
    selectInsideEditor.setPlainText(selectInsideText);
    QTextCursor selectInsideCursor(selectInsideEditor.document());
    selectInsideCursor.setPosition(
        selectInsideText.indexOf(QStringLiteral("bar")));
    selectInsideEditor.setTextCursor(selectInsideCursor);
    QString selectInsideMessage;
    expectBool("select begin end selects nearest block interior",
               selectInsideEditor.selectInsideBeginEnd(&selectInsideMessage)
                   && selectInsideMessage
                          == QStringLiteral("Selected inside begin-end")
                   && selectInsideEditor.textCursor().selectedText()
                          == QStringLiteral("      b <= bar;"),
               true);
    expectBool("select begin end excludes delimiter lines",
               !selectInsideEditor.textCursor().selectedText().contains(
                   QStringLiteral("begin"))
                   && !selectInsideEditor.textCursor().selectedText().contains(
                       QStringLiteral("end")),
               true);
    MyCodeEditor selectInsideFailEditor;
    selectInsideFailEditor.setPlainText(
        QStringLiteral("module no_begin_end;\n  assign a = b;\nendmodule\n"));
    QString selectInsideFailMessage;
    expectBool("select begin end fails outside a begin-end block",
               !selectInsideFailEditor.selectInsideBeginEnd(
                   &selectInsideFailMessage)
                   && selectInsideFailMessage
                          == QStringLiteral("No begin-end block"),
               true);

    const CommandModeMatch statementCommand =
        CompletionService::getInstance()
            ->matchCommandMode(QStringLiteral("assign a = b;l "));
    expectBool("CompletionService command statement position",
               statementCommand.matched
                   && statementCommand.prefixPosition
                       == QStringLiteral("assign a = b").size(),
               true);
    expectBool("CompletionService command comment reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("// ;l "))
                    .matched,
               true);
    expectBool("CompletionService command string reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("string s = \";l "))
                    .matched,
               true);

    const CommandSymbolPresentation interfacePresentation =
        CompletionService::getInstance()->commandSymbolPresentation(
            CompletionCommandKind::Interface);
    expectEq("CompletionService interface default",
             interfacePresentation.defaultValue,
             QStringLiteral("interface"));

    EditorSemanticContext commandContext;
    commandContext.lineUpToCursor = QStringLiteral(";l ena ");
    commandContext.fileName = path;
    commandContext.moduleName = QStringLiteral("top");
    commandContext.documentText = content;
    const CommandModeCompletionState contextCommandState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionState(commandContext);
    expectList("EditorSemanticContext command state",
               recordNames(contextCommandState.symbolRecords),
               {"enable"});
    expectBool("EditorSemanticContext command state range",
               contextCommandState.matched
                   && contextCommandState.prefixPosition == 0,
               true);
    expectBool("EditorSemanticContext command state records",
               contextCommandState.symbolRecords.size() == 1
                   && contextCommandState.symbolStableKeys.size() == 1
                   && contextCommandState.symbolRecords.first().stableKey
                       == contextCommandState.symbolStableKeys.first()
                   && contextCommandState.symbolRecords.first().name
                       == QStringLiteral("enable")
                   && contextCommandState.symbolStableKeys.first()
                       == contextCommandState.symbolRecords.first().stableKey,
               true);
    const CommandModeInputState contextInputState =
        EditorSemanticContextService::getInstance()
            ->commandModeInputState(commandContext);
    expectBool("EditorSemanticContext command input",
               contextInputState.matched,
               true);
    const CommandModeMatch contextCommandMatch =
        EditorSemanticContextService::getInstance()
            ->commandModeMatch(commandContext);
    expectBool("EditorSemanticContext command match",
               contextCommandMatch.matched,
               true);

    EditorSemanticContext includeNavigationContext;
    includeNavigationContext.lineText = QStringLiteral("`include \"defs.svh\"");
    includeNavigationContext.column =
        includeNavigationContext.lineText.indexOf(QStringLiteral("defs"));
    const EditorSourceNavigationTarget includeNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(includeNavigationContext, 100);
    expectBool("EditorSemanticContext nav include target",
               includeNavigationTarget.matched
                   && includeNavigationTarget.includeTarget
                   && includeNavigationTarget.jumpable
                   && includeNavigationTarget.text == QStringLiteral("defs.svh")
                   && includeNavigationTarget.startPos > 100,
               true);
    const EditorSourceNavigationClickState includeClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(includeNavigationTarget);
    expectBool("EditorSemanticContext nav include click",
               includeClickState.action
                       == EditorSourceNavigationClickAction::OpenInclude
                   && includeClickState.text == QStringLiteral("defs.svh")
                   && includeClickState.acceptEvent,
               true);

    const QString navPath = QStringLiteral("navigation_target.sv");
    const SemanticSymbolRecord navSymbol =
        SemanticFixtureRecordBuilder(
            QStringLiteral("jump_sig"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(navPath)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(2100)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .withType(QStringLiteral("logic"))
            .record();
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        navPath,
        {navSymbol},
        QStringLiteral("module top; logic jump_sig; endmodule\n"));
    EditorSemanticContext identifierNavigationContext;
    identifierNavigationContext.fileName = navPath;
    identifierNavigationContext.moduleName = QStringLiteral("top");
    identifierNavigationContext.lineText = QStringLiteral("assign jump_sig = 1'b1;");
    identifierNavigationContext.column =
        identifierNavigationContext.lineText.indexOf(QStringLiteral("jump_sig"));
    const EditorSourceNavigationTarget identifierNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(identifierNavigationContext, 200);
    expectBool("EditorSemanticContext nav identifier target",
               identifierNavigationTarget.matched
                   && identifierNavigationTarget.identifierTarget
                   && identifierNavigationTarget.jumpable
                   && identifierNavigationTarget.text == QStringLiteral("jump_sig")
                   && identifierNavigationTarget.cursorPosition
                       == 200 + identifierNavigationContext.column,
               true);
    const EditorSourceNavigationClickState identifierClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(identifierNavigationTarget);
    expectBool("EditorSemanticContext nav identifier click",
               identifierClickState.action
                       == EditorSourceNavigationClickAction::NavigateToDefinition
                   && identifierClickState.contextCursorPosition
                       == identifierNavigationTarget.cursorPosition
                   && identifierClickState.acceptEvent,
               true);

    identifierNavigationContext.lineText =
        QStringLiteral("assign missing_sig = 1'b1;");
    identifierNavigationContext.column =
        identifierNavigationContext.lineText.indexOf(QStringLiteral("missing_sig"));
    const EditorSourceNavigationTarget unresolvedNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(identifierNavigationContext, 300);
    expectBool("EditorSemanticContext nav unresolved target",
               unresolvedNavigationTarget.matched
                   && unresolvedNavigationTarget.identifierTarget
                   && !unresolvedNavigationTarget.jumpable
                   && unresolvedNavigationTarget.text
                       == QStringLiteral("missing_sig"),
               true);
    const EditorSourceNavigationClickState emptyClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(EditorSourceNavigationTarget{});
    expectBool("EditorSemanticContext nav empty click",
               emptyClickState.action == EditorSourceNavigationClickAction::None
                   && !emptyClickState.acceptEvent,
               true);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        navPath,
        {},
        QString());
    SemanticIndex::getInstance()->clearSnapshot();

    EditorSourceSymbolShortcutContext sourceShortcutContext;
    sourceShortcutContext.key = Qt::Key_F12;
    sourceShortcutContext.modifiers = int(Qt::NoModifier);
    sourceShortcutContext.semanticContext = identifierNavigationContext;
    const EditorSourceSymbolShortcutState definitionShortcutState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext definition shortcut retained",
               definitionShortcutState.matched
                   && definitionShortcutState.acceptEvent
                   && definitionShortcutState.action
                       == SourceSymbolAction::GoToDefinition
                   && definitionShortcutState.semanticContext.lineText
                        == identifierNavigationContext.lineText,
                true);
    sourceShortcutContext.modifiers = int(Qt::ShiftModifier);
    const EditorSourceSymbolShortcutState removedReferencesShortcutState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext references shortcut removed",
               !removedReferencesShortcutState.matched
                   && !removedReferencesShortcutState.acceptEvent,
               true);
    sourceShortcutContext.key = Qt::Key_R;
    sourceShortcutContext.modifiers =
        int(Qt::ControlModifier | Qt::ShiftModifier);
    const EditorSourceSymbolShortcutState removedRelationshipsShortcutState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext relationships shortcut removed",
               !removedRelationshipsShortcutState.matched
                   && !removedRelationshipsShortcutState.acceptEvent,
               true);
    sourceShortcutContext.key = Qt::Key_R;
    sourceShortcutContext.modifiers = int(Qt::ControlModifier);
    const EditorSourceSymbolShortcutState plainControlRState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut plain ctrl-r",
               !plainControlRState.matched && !plainControlRState.acceptEvent,
               true);

    EditorSemanticContext sourceSymbolContext;
    sourceSymbolContext.fileName = path;
    sourceSymbolContext.moduleName = QStringLiteral("top");
    sourceSymbolContext.lineText = QStringLiteral("assign menu_sig = 1'b1;");
    sourceSymbolContext.column =
        sourceSymbolContext.lineText.indexOf(QStringLiteral("menu_sig"));
    const EditorSourceSymbolContextMenuState sourceMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(sourceSymbolContext);
    auto sourceMenuItemForAction =
        [](const EditorSourceSymbolContextMenuState& state,
           SourceSymbolAction action) -> const EditorSourceSymbolMenuItemState* {
            for (const EditorSourceSymbolMenuItemState& item : state.items) {
                if (item.action == action)
                    return &item;
            }
            return nullptr;
        };
    auto sourceMenuItemEnabled =
        [&](const EditorSourceSymbolContextMenuState& state,
            SourceSymbolAction action,
            bool enabled) {
            const EditorSourceSymbolMenuItemState* item =
                sourceMenuItemForAction(state, action);
            return item && item->enabled == enabled;
        };
    expectBool("EditorSemanticContext source menu enabled",
               sourceMenuState.items.size() == 4
                   && sourceMenuItemForAction(
                          sourceMenuState,
                          SourceSymbolAction::GoToDefinition) == nullptr
                   && sourceMenuState.items.at(0).enabled
                   && sourceMenuState.items.at(0).action
                       == SourceSymbolAction::ShowSignalKernelGraph
                   && sourceMenuState.items.at(1).enabled
                   && sourceMenuState.items.at(1).action
                       == SourceSymbolAction::ShowSignalUsageHotspot
                   && !sourceMenuState.items.at(2).enabled
                   && sourceMenuState.items.at(2).action
                       == SourceSymbolAction::ShowStateTransitionGraph
                   && !sourceMenuState.items.at(3).enabled
                   && sourceMenuState.items.at(3).action
                       == SourceSymbolAction::ShowModuleBlockDiagram,
               true);
    const EditorSourceSymbolActionRequestState sourceKernelGraphRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowSignalKernelGraph,
                sourceSymbolContext);
    expectBool("EditorSemanticContext source kernel graph request",
               sourceKernelGraphRequest.available
                   && sourceKernelGraphRequest.action
                       == SourceSymbolAction::ShowSignalKernelGraph
                   && sourceKernelGraphRequest.symbolName
                       == QStringLiteral("menu_sig")
                   && sourceKernelGraphRequest.fileName == path
                   && sourceKernelGraphRequest.moduleName
                       == QStringLiteral("top"),
               true);
    const EditorSourceSymbolActionRequestState sourceHotspotRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowSignalUsageHotspot,
                sourceSymbolContext);
    expectBool("EditorSemanticContext source hotspot request",
               sourceHotspotRequest.available
                   && sourceHotspotRequest.action
                       == SourceSymbolAction::ShowSignalUsageHotspot
                   && sourceHotspotRequest.symbolName
                       == QStringLiteral("menu_sig")
                   && sourceHotspotRequest.fileName == path
                   && sourceHotspotRequest.moduleName
                       == QStringLiteral("top"),
               true);
    const EditorSourceSymbolActionRequestState sourceModuleBlockSignalRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowModuleBlockDiagram,
                sourceSymbolContext);
    expectBool("EditorSemanticContext module block signal request",
               !sourceModuleBlockSignalRequest.available,
               true);
    const QString fsmPairPath = QStringLiteral("fsm_pair_menu.sv");
    const QString fsmPairContent = QStringLiteral(
        "module fsm_pair_top;\n"
        "  typedef enum logic [1:0] {S_IDLE, S_RUN, S_DONE} pair_state_t;\n"
        "  logic random_flag;\n"
        "  pair_state_t phy_pass_thrg_cfg_cs;\n"
        "  pair_state_t phy_pass_thrg_cfg_ns;\n"
        "  pair_state_t prot_cfg_cs;\n"
        "  pair_state_t prot_cfg_ns;\n"
        "  always_ff @(posedge clk or negedge rst_n) begin\n"
        "    if (!rst_n) begin\n"
        "      phy_pass_thrg_cfg_cs <= S_IDLE;\n"
        "      prot_cfg_cs <= S_IDLE;\n"
        "    end else begin\n"
        "      phy_pass_thrg_cfg_cs <= #TP phy_pass_thrg_cfg_ns;\n"
        "      prot_cfg_cs <= # TP prot_cfg_ns;\n"
        "    end\n"
        "  end\n"
        "  always_comb begin\n"
        "    case (phy_pass_thrg_cfg_cs)\n"
        "      S_IDLE: phy_pass_thrg_cfg_ns = S_RUN;\n"
        "      S_RUN: phy_pass_thrg_cfg_ns = S_DONE;\n"
        "      default: phy_pass_thrg_cfg_ns = S_IDLE;\n"
        "    endcase\n"
        "    case (prot_cfg_cs)\n"
        "      S_IDLE: prot_cfg_ns = S_DONE;\n"
        "      default: prot_cfg_ns = S_IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    const SemanticSymbolRecord fsmPairModule =
        SemanticFixtureRecordBuilder(QStringLiteral("fsm_pair_top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(fsmPairPath)
            .withRange(1, 1, 28, 10)
            .withLocalHandle(2300)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    auto makeFsmPairSignal =
        [&](const QString& name, int line, int handle) {
            return SemanticFixtureRecordBuilder(
                       name,
                       SymbolTaxonomy::DeclarationKind::Enum)
                .withFile(fsmPairPath)
                .withLine(line)
                .inModule(QStringLiteral("fsm_pair_top"))
                .withLocalHandle(handle)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumVariable)
                .withType(QStringLiteral("pair_state_t"))
                .record();
        };
    auto makeFsmPairState =
        [&](const QString& name, int handle) {
            return SemanticFixtureRecordBuilder(
                       name,
                       SymbolTaxonomy::DeclarationKind::Enum)
                .withFile(fsmPairPath)
                .withLine(2)
                .inModule(QStringLiteral("fsm_pair_top"))
                .withLocalHandle(handle)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
                .withType(QStringLiteral("pair_state_t"))
                .record();
        };
    const SemanticSymbolRecord ordinaryRegister =
        SemanticFixtureRecordBuilder(QStringLiteral("random_flag"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(fsmPairPath)
            .withLine(3)
            .inModule(QStringLiteral("fsm_pair_top"))
            .withLocalHandle(2301)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .withType(QStringLiteral("logic"))
            .record();
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        fsmPairPath,
        {fsmPairModule,
         ordinaryRegister,
         makeFsmPairSignal(QStringLiteral("phy_pass_thrg_cfg_cs"), 4, 2302),
         makeFsmPairSignal(QStringLiteral("phy_pass_thrg_cfg_ns"), 5, 2303),
         makeFsmPairSignal(QStringLiteral("prot_cfg_cs"), 6, 2304),
         makeFsmPairSignal(QStringLiteral("prot_cfg_ns"), 7, 2305),
         makeFsmPairState(QStringLiteral("S_IDLE"), 2306),
         makeFsmPairState(QStringLiteral("S_RUN"), 2307),
         makeFsmPairState(QStringLiteral("S_DONE"), 2308)},
        fsmPairContent);
    const FsmGraphReport fsmPairReport =
        FsmGraphService::getInstance()->buildFsmGraph(
            {{}, QStringLiteral("fsm_pair_top"), fsmPairPath});
    bool sawPhyPair = false;
    bool sawProtPair = false;
    bool sawOrdinaryRegister = false;
    for (const FsmGraph& graph : fsmPairReport.graphs) {
        sawPhyPair = sawPhyPair
            || (graph.stateRegisterDisplayName
                    == QStringLiteral("phy_pass_thrg_cfg_cs")
                && graph.nextStateSignalDisplayName
                    == QStringLiteral("phy_pass_thrg_cfg_ns"));
        sawProtPair = sawProtPair
            || (graph.stateRegisterDisplayName == QStringLiteral("prot_cfg_cs")
                && graph.nextStateSignalDisplayName
                    == QStringLiteral("prot_cfg_ns"));
        sawOrdinaryRegister = sawOrdinaryRegister
            || graph.stateRegisterDisplayName == QStringLiteral("random_flag");
    }
    expectBool("FsmGraph pairs suffixed next-state signals",
               fsmPairReport.found && fsmPairReport.graphs.size() == 2
                   && sawPhyPair && sawProtPair,
               true);
    expectBool("FsmGraph rejects ordinary register pairing",
               !sawOrdinaryRegister,
               true);
    const StateTransitionTriggerService stateTransitionTriggerService;
    expectBool("StateTransition trigger accepts next_state",
               stateTransitionTriggerService
                   .triggerForSymbol({QStringLiteral("next_state"), path,
                                      QStringLiteral("top")})
                   .available,
               true);
    expectBool("StateTransition trigger accepts suffixed ns",
               stateTransitionTriggerService
                   .triggerForSymbol({QStringLiteral("phy_pass_thrg_cfg_ns"),
                                      fsmPairPath,
                                      QStringLiteral("fsm_pair_top")})
                   .available,
               true);
    expectBool("StateTransition trigger rejects current_state",
               !stateTransitionTriggerService
                    .triggerForSymbol({QStringLiteral("current_state"), path,
                                       QStringLiteral("top")})
                    .available,
               true);
    expectBool("StateTransition trigger rejects suffixed cs",
               !stateTransitionTriggerService
                    .triggerForSymbol({QStringLiteral("phy_pass_thrg_cfg_cs"),
                                       fsmPairPath,
                                       QStringLiteral("fsm_pair_top")})
                    .available,
               true);
    auto sourceSymbolContextForName =
        [&](const QString& symbolName,
            const QString& fileName,
            const QString& moduleName) {
            EditorSemanticContext context;
            context.fileName = fileName;
            context.moduleName = moduleName;
            context.lineText = QStringLiteral("assign probe_%1 = %1;")
                                   .arg(symbolName);
            context.column = context.lineText.lastIndexOf(symbolName);
            return context;
        };
    const EditorSemanticContext nextStateContext =
        sourceSymbolContextForName(QStringLiteral("next_state"),
                                   path,
                                   QStringLiteral("top"));
    const EditorSourceSymbolContextMenuState nextStateMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(nextStateContext);
    const EditorSourceSymbolActionRequestState nextStateRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowStateTransitionGraph,
                nextStateContext);
    expectBool("EditorSemanticContext state transition next_state menu",
               nextStateMenuState.items.size() == 4
                   && sourceMenuItemEnabled(
                       nextStateMenuState,
                       SourceSymbolAction::ShowSignalUsageHotspot,
                       true)
                   && sourceMenuItemEnabled(
                       nextStateMenuState,
                       SourceSymbolAction::ShowStateTransitionGraph,
                       true)
                   && sourceMenuItemEnabled(
                       nextStateMenuState,
                       SourceSymbolAction::ShowModuleBlockDiagram,
                       false)
                   && nextStateRequest.available
                   && nextStateRequest.symbolName
                       == QStringLiteral("next_state"),
               true);
    const EditorSemanticContext suffixedNextStateContext =
        sourceSymbolContextForName(QStringLiteral("phy_pass_thrg_cfg_ns"),
                                   fsmPairPath,
                                   QStringLiteral("fsm_pair_top"));
    const EditorSourceSymbolContextMenuState suffixedNextStateMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(suffixedNextStateContext);
    const EditorSourceSymbolActionRequestState suffixedNextStateRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowStateTransitionGraph,
                suffixedNextStateContext);
    expectBool("EditorSemanticContext state transition suffixed ns menu",
               suffixedNextStateMenuState.items.size() == 4
                   && sourceMenuItemEnabled(
                       suffixedNextStateMenuState,
                       SourceSymbolAction::ShowSignalUsageHotspot,
                       true)
                   && sourceMenuItemEnabled(
                       suffixedNextStateMenuState,
                       SourceSymbolAction::ShowStateTransitionGraph,
                       true)
                   && sourceMenuItemEnabled(
                       suffixedNextStateMenuState,
                       SourceSymbolAction::ShowModuleBlockDiagram,
                       false)
                   && suffixedNextStateRequest.available
                   && suffixedNextStateRequest.symbolName
                       == QStringLiteral("phy_pass_thrg_cfg_ns"),
               true);
    const EditorSemanticContext currentStateContext =
        sourceSymbolContextForName(QStringLiteral("current_state"),
                                   path,
                                   QStringLiteral("top"));
    const EditorSourceSymbolContextMenuState currentStateMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(currentStateContext);
    const EditorSourceSymbolActionRequestState currentStateRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowStateTransitionGraph,
                currentStateContext);
    expectBool("EditorSemanticContext state transition rejects current_state",
               currentStateMenuState.items.size() == 4
                   && sourceMenuItemEnabled(
                       currentStateMenuState,
                       SourceSymbolAction::ShowSignalUsageHotspot,
                       true)
                   && sourceMenuItemEnabled(
                       currentStateMenuState,
                       SourceSymbolAction::ShowStateTransitionGraph,
                       false)
                   && sourceMenuItemEnabled(
                       currentStateMenuState,
                       SourceSymbolAction::ShowModuleBlockDiagram,
                       false)
                   && !currentStateRequest.available,
               true);
    const EditorSemanticContext suffixedCurrentStateContext =
        sourceSymbolContextForName(QStringLiteral("phy_pass_thrg_cfg_cs"),
                                   fsmPairPath,
                                   QStringLiteral("fsm_pair_top"));
    const EditorSourceSymbolContextMenuState suffixedCurrentStateMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(suffixedCurrentStateContext);
    const EditorSourceSymbolActionRequestState suffixedCurrentStateRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowStateTransitionGraph,
                suffixedCurrentStateContext);
    expectBool("EditorSemanticContext state transition rejects suffixed cs",
               suffixedCurrentStateMenuState.items.size() == 4
                   && sourceMenuItemEnabled(
                       suffixedCurrentStateMenuState,
                       SourceSymbolAction::ShowSignalUsageHotspot,
                       true)
                   && sourceMenuItemEnabled(
                       suffixedCurrentStateMenuState,
                       SourceSymbolAction::ShowStateTransitionGraph,
                       false)
                   && sourceMenuItemEnabled(
                       suffixedCurrentStateMenuState,
                       SourceSymbolAction::ShowModuleBlockDiagram,
                       false)
                   && !suffixedCurrentStateRequest.available,
               true);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        fsmPairPath,
        {},
        QString());
    SemanticIndex::getInstance()->clearSnapshot();
    const QString moduleBlockMenuPath =
        QStringLiteral("module_block_menu_target.sv");
    const SemanticSymbolRecord moduleBlockMenuRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("menu_mod"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(moduleBlockMenuPath)
            .withLocalHandle(2200)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        moduleBlockMenuPath,
        {moduleBlockMenuRecord},
        QStringLiteral("module menu_mod; endmodule\n"));
    EditorSemanticContext moduleBlockContext;
    moduleBlockContext.fileName = moduleBlockMenuPath;
    moduleBlockContext.moduleName = QStringLiteral("top");
    moduleBlockContext.lineText = QStringLiteral("menu_mod u_menu_mod ();");
    moduleBlockContext.column =
        moduleBlockContext.lineText.indexOf(QStringLiteral("menu_mod"));
    const EditorSourceSymbolContextMenuState moduleBlockMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(moduleBlockContext);
    const EditorSourceSymbolActionRequestState moduleBlockRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowModuleBlockDiagram,
                moduleBlockContext);
    expectBool("EditorSemanticContext module block module menu",
               moduleBlockMenuState.items.size() == 4
                   && sourceMenuItemEnabled(
                       moduleBlockMenuState,
                       SourceSymbolAction::ShowSignalUsageHotspot,
                       false)
                   && sourceMenuItemEnabled(
                       moduleBlockMenuState,
                       SourceSymbolAction::ShowModuleBlockDiagram,
                       true)
                   && moduleBlockRequest.available
                   && moduleBlockRequest.action
                       == SourceSymbolAction::ShowModuleBlockDiagram
                   && moduleBlockRequest.symbolName
                       == QStringLiteral("menu_mod")
                   && moduleBlockRequest.fileName == moduleBlockMenuPath
                   && moduleBlockRequest.moduleName == QStringLiteral("top"),
               true);
    const EditorSourceSymbolActionRequestState moduleBlockHotspotRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowSignalUsageHotspot,
                moduleBlockContext);
    expectBool("EditorSemanticContext module hotspot request unavailable",
               !moduleBlockHotspotRequest.available
                   && moduleBlockHotspotRequest.unavailableReason.contains(
                       QStringLiteral("Signal Usage Hotspot")),
               true);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        moduleBlockMenuPath,
        {},
        QString());
    SemanticIndex::getInstance()->clearSnapshot();
    EditorSemanticContext unavailableSourceSymbolContext;
    unavailableSourceSymbolContext.lineText = sourceSymbolContext.lineText;
    unavailableSourceSymbolContext.column = sourceSymbolContext.column;
    const EditorSourceSymbolContextMenuState disabledSourceMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(unavailableSourceSymbolContext);
    expectBool("EditorSemanticContext source menu disabled",
               disabledSourceMenuState.items.size() == 4
                   && sourceMenuItemForAction(
                          disabledSourceMenuState,
                          SourceSymbolAction::GoToDefinition) == nullptr
                   && sourceMenuItemEnabled(
                       disabledSourceMenuState,
                       SourceSymbolAction::ShowSignalKernelGraph,
                       false)
                   && sourceMenuItemEnabled(
                       disabledSourceMenuState,
                       SourceSymbolAction::ShowSignalUsageHotspot,
                       false)
                   && sourceMenuItemEnabled(
                       disabledSourceMenuState,
                       SourceSymbolAction::ShowStateTransitionGraph,
                       false)
                   && sourceMenuItemEnabled(
                       disabledSourceMenuState,
                       SourceSymbolAction::ShowModuleBlockDiagram,
                       false),
               true);
    const EditorSourceSymbolActionRequestState unavailableSourceRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowSignalKernelGraph,
                unavailableSourceSymbolContext);
    expectBool("EditorSemanticContext source request unavailable",
               !unavailableSourceRequest.available,
               true);

    QWidget signalKernelGraphHost;
    SignalKernelGraphPanelCoordinator signalKernelGraphPanel(
        &signalKernelGraphHost);
    SignalKernelGraphReport groupedSignalGraph;
    groupedSignalGraph.found = true;
    groupedSignalGraph.kernelModuleName = QStringLiteral("fanout_top");
    groupedSignalGraph.fanoutGroupingThreshold = 5;
    groupedSignalGraph.kernel.id = 1;
    groupedSignalGraph.kernel.role = SignalKernelGraphNodeRole::Kernel;
    groupedSignalGraph.kernel.displayName = QStringLiteral("fanout_sig");
    groupedSignalGraph.kernel.moduleDisplayName =
        QStringLiteral("fanout_top");
    groupedSignalGraph.kernel.typeDisplayName = QStringLiteral("logic");
    groupedSignalGraph.kernel.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("fanout_top.sv"), 10, 1);
    SignalKernelGraphFanoutGroup outputGroup;
    outputGroup.id = 1;
    outputGroup.role = SignalKernelGraphNodeRole::Output;
    outputGroup.groupKey = QStringLiteral("output:fanout_top");
    outputGroup.displayName = QStringLiteral("Outputs in fanout_top");
    outputGroup.moduleName = QStringLiteral("fanout_top");
    outputGroup.totalRoleNodeCount = 12;
    outputGroup.highFanout = true;
    for (int i = 0; i < 12; ++i) {
        SignalKernelGraphNode outputNode;
        outputNode.id = 10 + i;
        outputNode.role = SignalKernelGraphNodeRole::Output;
        outputNode.displayName = QStringLiteral("reader_%1").arg(i + 1);
        outputNode.moduleDisplayName = QStringLiteral("fanout_top");
        outputNode.typeDisplayName = QStringLiteral("logic");
        outputNode.navigateCodeLink =
            RtlInsightLink::fromFileLine(QStringLiteral("fanout_top.sv"),
                                         20 + i,
                                         1);
        groupedSignalGraph.outputs.append(outputNode);
        groupedSignalGraph.edges.append(
            {groupedSignalGraph.kernel.id, outputNode.id, QString()});
        outputGroup.nodeIds.append(outputNode.id);
    }
    outputGroup.nodeCount = outputGroup.nodeIds.size();
    groupedSignalGraph.outputFanoutGroups.append(outputGroup);
    signalKernelGraphPanel.renderReportForTest(groupedSignalGraph);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    const QRectF collapsedFanoutRect =
        signalKernelGraphPanel.lastRenderedFanoutGroupRectForTest();
    expectBool("SignalKernelGraphPanel defaults fanout collapsed",
               signalKernelGraphPanel.collapsedFanoutGroupCountForTest() == 1
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest() == 1
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    expectBool("SignalKernelGraphPanel collapsed fanout fixed card",
               collapsedFanoutRect.isValid()
                   && collapsedFanoutRect.width() <= 260.0
                   && collapsedFanoutRect.height() <= 70.0,
               true);
    expectBool("SignalKernelGraphPanel expands fanout group",
               signalKernelGraphPanel.toggleFanoutGroupForTest(
                   outputGroup.groupKey)
                   && signalKernelGraphPanel
                          .collapsedFanoutGroupCountForTest() == 0
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest()
                          == 13
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    expectBool("SignalKernelGraphPanel collapses fanout group again",
               signalKernelGraphPanel.toggleFanoutGroupForTest(
                   outputGroup.groupKey)
                   && signalKernelGraphPanel
                          .collapsedFanoutGroupCountForTest() == 1
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest()
                          == 1,
               true);

    SignalKernelGraphReport filteredSignalGraph;
    filteredSignalGraph.found = true;
    filteredSignalGraph.kernelModuleName = QStringLiteral("graph_top");
    filteredSignalGraph.kernel.id = 100;
    filteredSignalGraph.kernel.role = SignalKernelGraphNodeRole::Kernel;
    filteredSignalGraph.kernel.displayName = QStringLiteral("core_signal");
    filteredSignalGraph.kernel.moduleDisplayName =
        QStringLiteral("graph_top");
    filteredSignalGraph.kernel.typeDisplayName = QStringLiteral("logic");
    filteredSignalGraph.kernel.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 12, 1);

    SignalKernelGraphNode localInputNode;
    localInputNode.id = 101;
    localInputNode.role = SignalKernelGraphNodeRole::Input;
    localInputNode.inputLane = SignalKernelGraphInputLane::Control;
    localInputNode.displayName = QStringLiteral("start_pulse");
    localInputNode.moduleDisplayName = QStringLiteral("graph_top");
    localInputNode.typeDisplayName = QStringLiteral("logic");
    localInputNode.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 18, 1);
    filteredSignalGraph.inputs.append(localInputNode);
    filteredSignalGraph.edges.append(
        {localInputNode.id, filteredSignalGraph.kernel.id, QString()});

    SignalKernelGraphNode localOutputNode;
    localOutputNode.id = 102;
    localOutputNode.role = SignalKernelGraphNodeRole::Output;
    localOutputNode.displayName = QStringLiteral("done_local");
    localOutputNode.moduleDisplayName = QStringLiteral("graph_top");
    localOutputNode.typeDisplayName = QStringLiteral("logic");
    localOutputNode.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 22, 1);
    filteredSignalGraph.outputs.append(localOutputNode);
    filteredSignalGraph.edges.append(
        {filteredSignalGraph.kernel.id, localOutputNode.id, QString()});

    SignalKernelGraphFanoutGroup crossOutputGroup;
    crossOutputGroup.id = 2;
    crossOutputGroup.role = SignalKernelGraphNodeRole::Output;
    crossOutputGroup.groupKey = QStringLiteral("output:consumer_mod");
    crossOutputGroup.displayName =
        QStringLiteral("Outputs in consumer_mod");
    crossOutputGroup.moduleName = QStringLiteral("consumer_mod");
    crossOutputGroup.totalRoleNodeCount = 3;
    crossOutputGroup.highFanout = true;
    for (int i = 0; i < 2; ++i) {
        SignalKernelGraphNode remoteOutputNode;
        remoteOutputNode.id = 103 + i;
        remoteOutputNode.role = SignalKernelGraphNodeRole::Output;
        remoteOutputNode.displayName =
            i == 0 ? QStringLiteral("result_remote")
                   : QStringLiteral("status_remote");
        remoteOutputNode.moduleDisplayName = QStringLiteral("consumer_mod");
        remoteOutputNode.typeDisplayName = QStringLiteral("logic");
        remoteOutputNode.crossModule = true;
        remoteOutputNode.navigateCodeLink =
            RtlInsightLink::fromFileLine(
                QStringLiteral("consumer_mod.sv"),
                30 + i,
                1);
        filteredSignalGraph.outputs.append(remoteOutputNode);
        filteredSignalGraph.edges.append(
            {filteredSignalGraph.kernel.id, remoteOutputNode.id, QString()});
        crossOutputGroup.nodeIds.append(remoteOutputNode.id);
    }
    crossOutputGroup.nodeCount = crossOutputGroup.nodeIds.size();
    filteredSignalGraph.outputFanoutGroups.append(crossOutputGroup);

    signalKernelGraphPanel.renderReportForTest(filteredSignalGraph);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel filters inputs",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 3
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(true, false, false);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel hides outputs",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 2
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 0,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(false, true, false);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel hides inputs",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 2
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(true, true, true);
    signalKernelGraphPanel.setGraphSearchTextForTest(
        QStringLiteral("status_remote"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel searches collapsed cross-module group",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 1
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1
                   && signalKernelGraphPanel.searchMatchCountForTest() == 1
                   && signalKernelGraphPanel.focusedSearchNodeIdForTest()
                          == 104,
               true);
    expectBool("SignalKernelGraphPanel expands filtered search group",
               signalKernelGraphPanel.toggleFanoutGroupForTest(
                   crossOutputGroup.groupKey)
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest()
                          == 3
                   && signalKernelGraphPanel.searchMatchCountForTest() == 1
                   && signalKernelGraphPanel.focusedSearchNodeIdForTest()
                          == 104,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(true, true, false);
    signalKernelGraphPanel.setGraphSearchTextForTest(QString());

    CommandCompletionQuery commandQuery;
    commandQuery.fileName = path;
    commandQuery.moduleName = "top";
    commandQuery.commandKind = CompletionCommandKind::Logic;
    commandQuery.prefix = "en";
    expectList("CompletionService command logic",
               recordNames(CompletionService::getInstance()
                               ->findCommandCompletionSymbolRecords(commandQuery)),
               {"enable"});

    const QList<SemanticSymbolRecord> commandLogicSymbols =
        CompletionService::getInstance()->findCommandCompletionSymbolRecords(commandQuery);
    ++g_checks;
    const bool commandLogicOk = commandLogicSymbols.size() == 1
        && commandLogicSymbols.first().name == QStringLiteral("enable")
        && commandLogicSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::Logic
        && commandLogicSymbols.first().owner.name == QStringLiteral("top");
    if (!commandLogicOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           commandLogicOk ? "PASS" : "FAIL",
           "CompletionService command symbols",
           commandLogicSymbols.size());

    const QString slangVisibleFile =
        QStringLiteral("visible_package_scope.sv");
    const QString slangVisibleContent =
        QStringLiteral("package visible_pkg;\n"
                       "  int visible_var;\n"
                       "  function automatic int visible_fn();\n"
                       "    return visible_var;\n"
                       "  endfunction\n"
                       "endpackage\n"
                       "module visible_user;\n"
                       "  import visible_pkg::*;\n"
                       "endmodule\n");
    SlangManager slangVisibleManager;
    const QList<SemanticSymbolRecord> slangVisibleRecords =
        slangVisibleManager.extractSymbolRecords(
            slangVisibleFile,
            slangVisibleContent);
    bool slangPackageFunctionVisible = false;
    bool slangPackageVariableVisible = false;
    for (const SemanticSymbolRecord& record : slangVisibleRecords) {
        if (record.owner.name != QStringLiteral("visible_pkg")
            || record.visibility
                   != SymbolTaxonomy::SymbolVisibility::PackageVisible) {
            continue;
        }
        slangPackageFunctionVisible =
            slangPackageFunctionVisible
            || (record.name == QStringLiteral("visible_fn")
                && record.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Function);
        slangPackageVariableVisible =
            slangPackageVariableVisible
            || (record.name == QStringLiteral("visible_var")
                && record.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal);
    }
    expectBool("Slang package function is import-visible",
               slangPackageFunctionVisible,
               true);
    expectBool("Slang package variable is import-visible",
               slangPackageVariableVisible,
               true);
    SemanticIndex slangVisibleIndex;
    slangVisibleIndex.updateSymbolRecordsForFile(
        slangVisibleFile,
        slangVisibleRecords,
        slangVisibleContent);
    CompletionService slangVisibleService(&slangVisibleIndex);
    CommandCompletionQuery slangVisibleQuery;
    slangVisibleQuery.fileName = slangVisibleFile;
    slangVisibleQuery.moduleName = QStringLiteral("visible_user");
    slangVisibleQuery.prefix = QStringLiteral("visible_");
    slangVisibleQuery.cursorLine = 9;
    slangVisibleQuery.commandKind =
        CompletionCommandKind::VisibleSymbol;
    expectList(
        "visible-symbol query uses Slang package visibility",
        recordNames(slangVisibleService
                        .findCommandCompletionSymbolRecords(
                            slangVisibleQuery)),
        {"visible_fn", "visible_var"});

    const QString inlineScopeFile = QStringLiteral("inline_scope_test.sv");
    const QString inlineScopeContent =
        QStringLiteral("module mod_a;\n"
                       "  logic a_logic;\n"
                       "  wire a_wire;\n"
                       "  reg a_reg;\n"
                       "endmodule\n"
                       "module mod_b;\n"
                       "  logic b_logic;\n"
                       "  wire b_wire;\n"
                       "  reg b_reg;\n"
                       "endmodule\n");
    QList<SemanticSymbolRecord> inlineScopeRecords;
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("mod_a"),
        SymbolTaxonomy::DeclarationKind::Module,
        SymbolTaxonomy::CollectorKind::Module,
        QString(),
        QString(),
        1,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("a_logic"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Logic,
        QStringLiteral("mod_a"),
        QStringLiteral("logic"),
        2,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("a_wire"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Wire,
        QStringLiteral("mod_a"),
        QStringLiteral("wire"),
        3,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("a_reg"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Reg,
        QStringLiteral("mod_a"),
        QStringLiteral("reg"),
        4,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("a_port"),
        SymbolTaxonomy::DeclarationKind::Port,
        SymbolTaxonomy::CollectorKind::PortInput,
        QStringLiteral("mod_a"),
        QStringLiteral("logic"),
        5,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("A_WIDTH"),
        SymbolTaxonomy::DeclarationKind::Parameter,
        SymbolTaxonomy::CollectorKind::Parameter,
        QStringLiteral("mod_a"),
        QStringLiteral("int"),
        6,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("a_type_t"),
        SymbolTaxonomy::DeclarationKind::Typedef,
        SymbolTaxonomy::CollectorKind::Typedef,
        QStringLiteral("mod_a"),
        QStringLiteral("logic"),
        7,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("u_child"),
        SymbolTaxonomy::DeclarationKind::Instance,
        SymbolTaxonomy::CollectorKind::Inst,
        QStringLiteral("mod_a"),
        QStringLiteral("child"),
        8,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("a_fn"),
        SymbolTaxonomy::DeclarationKind::Function,
        SymbolTaxonomy::CollectorKind::Function,
        QStringLiteral("mod_a"),
        QStringLiteral("function"),
        9,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("mod_b"),
        SymbolTaxonomy::DeclarationKind::Module,
        SymbolTaxonomy::CollectorKind::Module,
        QString(),
        QString(),
        20,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("b_logic"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Logic,
        QStringLiteral("mod_b"),
        QStringLiteral("logic"),
        21,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("b_wire"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Wire,
        QStringLiteral("mod_b"),
        QStringLiteral("wire"),
        22,
        inlineScopeFile));
    inlineScopeRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("b_reg"),
        SymbolTaxonomy::DeclarationKind::Signal,
        SymbolTaxonomy::CollectorKind::Reg,
        QStringLiteral("mod_b"),
        QStringLiteral("reg"),
        23,
        inlineScopeFile));
    inlineScopeRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("shared_pkg"))
            .withFile(inlineScopeFile)
            .withLocalHandle(30)
            .withLine(1)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::PackageImport)
            .withUsageRole(
                SymbolTaxonomy::SymbolUsageRole::Reference)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("mod_a"))
            .withType(QStringLiteral("*"))
            .record());
    inlineScopeRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("SHARED_DEPTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(inlineScopeFile)
            .withLocalHandle(31)
            .withLine(30)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::Parameter)
            .inPackage(QStringLiteral("shared_pkg"))
            .withType(QStringLiteral("int"))
            .record());
    inlineScopeRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("shared_t"),
                                     SymbolTaxonomy::DeclarationKind::Typedef)
            .withFile(inlineScopeFile)
            .withLocalHandle(32)
            .withLine(31)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::Typedef)
            .inPackage(QStringLiteral("shared_pkg"))
            .withType(QStringLiteral("logic"))
            .record());
    inlineScopeRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("HIDDEN_DEPTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(inlineScopeFile)
            .withLocalHandle(33)
            .withLine(32)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::Parameter)
            .inPackage(QStringLiteral("hidden_pkg"))
            .withType(QStringLiteral("int"))
            .record());
    for (int packageOffset = 0; packageOffset < 2; ++packageOffset) {
        const QString packageName = packageOffset == 0
            ? QStringLiteral("duplicate_pkg_a")
            : QStringLiteral("duplicate_pkg_b");
        inlineScopeRecords.append(
            SemanticFixtureRecordBuilder(packageName)
                .withFile(inlineScopeFile)
                .withLocalHandle(34 + packageOffset)
                .withLine(1)
                .withCollectorKind(
                    SymbolTaxonomy::CollectorKind::PackageImport)
                .withUsageRole(
                    SymbolTaxonomy::SymbolUsageRole::Reference)
                .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                           QStringLiteral("mod_a"))
                .withType(QStringLiteral("*"))
                .record());
        inlineScopeRecords.append(
            SemanticFixtureRecordBuilder(
                QStringLiteral("DUPLICATE"),
                SymbolTaxonomy::DeclarationKind::Parameter)
                .withFile(inlineScopeFile)
                .withLocalHandle(36 + packageOffset)
                .withLine(33 + packageOffset)
                .withCollectorKind(
                    SymbolTaxonomy::CollectorKind::Parameter)
                .inPackage(packageName)
                .withType(QStringLiteral("int"))
                .record());
    }
    inlineScopeRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("PKG_LOCAL"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(inlineScopeFile)
            .withLocalHandle(38)
            .withLine(35)
            .withCollectorKind(
                SymbolTaxonomy::CollectorKind::Localparam)
            .inPackage(QStringLiteral("local_pkg"))
            .withType(QStringLiteral("int"))
            .record());
    SemanticIndex inlineScopeIndex;
    inlineScopeIndex.updateSymbolRecordsForFile(
        inlineScopeFile,
        inlineScopeRecords,
        inlineScopeContent);
    CompletionService inlineScopeService(&inlineScopeIndex);
    auto inlineScopeNames = [&](const QString& moduleName,
                                const QString& lineUpToCursor,
                                int cursorLine) {
        CommandModeCompletionQuery query;
        query.lineUpToCursor = lineUpToCursor;
        query.fileName = inlineScopeFile;
        query.moduleName = moduleName;
        query.documentText = inlineScopeContent;
        query.cursorLine = cursorLine;
        query.cursorPosition = lineUpToCursor.size();
        return recordNames(
            inlineScopeService.commandModeCompletionState(query).symbolRecords);
    };
    expectList("inline command scope mod_a logic",
               inlineScopeNames(QStringLiteral("mod_a"),
                                QStringLiteral("assign lhs = ;l "),
                                2),
               {"a_logic"});
    expectList("inline command scope mod_b logic",
               inlineScopeNames(QStringLiteral("mod_b"),
                                QStringLiteral("assign lhs = ;l "),
                                7),
               {"b_logic"});
    expectList("inline command scope mod_a wire",
               inlineScopeNames(QStringLiteral("mod_a"),
                                QStringLiteral("assign lhs = ;w "),
                                3),
               {"a_wire"});
    expectList("inline command scope mod_b wire",
               inlineScopeNames(QStringLiteral("mod_b"),
                                QStringLiteral("assign lhs = ;w "),
                                8),
               {"b_wire"});
    expectList("inline command scope mod_a reg",
               inlineScopeNames(QStringLiteral("mod_a"),
                                QStringLiteral("assign lhs = ;r "),
                                4),
               {"a_reg"});
    expectList("inline command scope mod_b reg",
               inlineScopeNames(QStringLiteral("mod_b"),
                                QStringLiteral("assign lhs = ;r "),
                                9),
               {"b_reg"});
    expectList("visible-symbol command respects module and imports",
               inlineScopeNames(QStringLiteral("mod_a"),
                                QStringLiteral("assign lhs = ;v "),
                                10),
               {"A_WIDTH",
                "a_fn",
                "a_logic",
                "a_port",
                "a_reg",
                "a_type_t",
                "a_wire",
                "SHARED_DEPTH",
                "shared_t",
                "u_child"});
    expectList("visible-symbol command excludes foreign module internals",
               inlineScopeNames(QStringLiteral("mod_b"),
                                QStringLiteral("assign lhs = ;v "),
                                24),
               {"b_logic", "b_reg", "b_wire"});

    CommandModeCompletionQuery packageVisibleQuery;
    packageVisibleQuery.lineUpToCursor = QStringLiteral(";v ");
    packageVisibleQuery.fileName = inlineScopeFile;
    packageVisibleQuery.packageName = QStringLiteral("local_pkg");
    packageVisibleQuery.documentText = inlineScopeContent;
    packageVisibleQuery.cursorLine = 35;
    packageVisibleQuery.cursorPosition = inlineScopeContent.size();
    expectList(
        "visible-symbol command supports package scope",
        recordNames(inlineScopeService
                        .commandModeCompletionState(packageVisibleQuery)
                        .symbolRecords),
        {"PKG_LOCAL"});

    CommandModeCompletionQuery anchoredInlineFilterQuery;
    anchoredInlineFilterQuery.lineUpToCursor =
        QStringLiteral("assign lhs = ;l");
    anchoredInlineFilterQuery.fileName = inlineScopeFile;
    anchoredInlineFilterQuery.moduleName = QStringLiteral("mod_a");
    anchoredInlineFilterQuery.documentText = inlineScopeContent;
    anchoredInlineFilterQuery.cursorLine = 2;
    anchoredInlineFilterQuery.cursorPosition =
        inlineScopeContent.indexOf(QStringLiteral("a_logic"));
    anchoredInlineFilterQuery.hasExplicitMatch = true;
    anchoredInlineFilterQuery.explicitMatch =
        InlineCommandMode::matchAbbreviationBeforeCursor(
            anchoredInlineFilterQuery.lineUpToCursor);
    anchoredInlineFilterQuery.explicitMatch.input = QStringLiteral("a_");
    expectList(
        "inline filtering keeps anchor scope",
        recordNames(inlineScopeService
                        .commandModeCompletionState(anchoredInlineFilterQuery)
                        .symbolRecords),
        {"a_logic"});
    anchoredInlineFilterQuery.explicitMatch.input = QStringLiteral("b_");
    expectList(
        "inline filtering excludes foreign module",
        recordNames(inlineScopeService
                        .commandModeCompletionState(anchoredInlineFilterQuery)
                        .symbolRecords),
        {});

    CommandModeCompletionQuery visibleFilterQuery;
    visibleFilterQuery.lineUpToCursor =
        QStringLiteral("assign lhs = ;v");
    visibleFilterQuery.fileName = inlineScopeFile;
    visibleFilterQuery.moduleName = QStringLiteral("mod_a");
    visibleFilterQuery.documentText = inlineScopeContent;
    visibleFilterQuery.cursorLine = 10;
    visibleFilterQuery.cursorPosition =
        inlineScopeContent.indexOf(QStringLiteral("endmodule"));
    visibleFilterQuery.hasExplicitMatch = true;
    visibleFilterQuery.explicitMatch =
        InlineCommandMode::matchAbbreviationBeforeCursor(
            visibleFilterQuery.lineUpToCursor);
    visibleFilterQuery.explicitMatch.input = QStringLiteral("a_por");
    expectList(
        "visible-symbol typing filters the explicit session",
        recordNames(inlineScopeService
                        .commandModeCompletionState(visibleFilterQuery)
                        .symbolRecords),
        {"a_port"});
    visibleFilterQuery.explicitMatch.input = QStringLiteral("a_p");
    expectList(
        "visible-symbol Backspace broadens the explicit session",
        recordNames(inlineScopeService
                        .commandModeCompletionState(visibleFilterQuery)
                        .symbolRecords),
        {"SHARED_DEPTH", "a_port", "a_type_t"});

    commandQuery.commandKind = CompletionCommandKind::Wire;
    commandQuery.prefix = "net";
    expectList("CompletionService command wire",
               recordNames(CompletionService::getInstance()
                               ->findCommandCompletionSymbolRecords(commandQuery)),
               {"net_sig"});

    commandQuery.commandKind = CompletionCommandKind::Reg;
    commandQuery.prefix = "cou";
    expectList("CompletionService command reg",
               recordNames(CompletionService::getInstance()
                               ->findCommandCompletionSymbolRecords(commandQuery)),
               {"counter"});

    commandQuery.commandKind = CompletionCommandKind::Parameter;
    commandQuery.prefix = "DAT";
    expectList("CompletionService command parameter",
               recordNames(CompletionService::getInstance()
                               ->findCommandCompletionSymbolRecords(commandQuery)),
               {"DATA_WIDTH"});

    commandQuery.commandKind = CompletionCommandKind::Localparam;
    commandQuery.prefix = "LOC";
    expectList("CompletionService command localparam",
               recordNames(CompletionService::getInstance()
                               ->findCommandCompletionSymbolRecords(commandQuery)),
               {"LOCAL_MAX"});

    commandQuery.commandKind = CompletionCommandKind::PackedStructVariable;
    commandQuery.prefix = "pix";
    commandQuery.documentText = content;
    const QList<SemanticSymbolRecord> packedStructVars =
        CompletionService::getInstance()->findCommandCompletionSymbolRecords(commandQuery);
    ++g_checks;
    const bool packedStructOk = packedStructVars.size() == 1
        && packedStructVars.first().name == QStringLiteral("pixel")
        && packedStructVars.first().collectorKind
            == SymbolTaxonomy::CollectorKind::PackedStructVariable
        && packedStructVars.first().owner.name == QStringLiteral("top");
    if (!packedStructOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           packedStructOk ? "PASS" : "FAIL",
           "CompletionService command struct",
           packedStructVars.size());

    commandQuery.moduleName.clear();
    ++g_checks;
    const bool structGlobalHidden =
        CompletionService::getInstance()
            ->findCommandCompletionSymbolRecords(commandQuery)
            .isEmpty();
    if (!structGlobalHidden)
        ++g_fails;
    printf("[%s] %-34s\n",
           structGlobalHidden ? "PASS" : "FAIL",
           "CompletionService command struct hidden");

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SourceRole = SymbolTaxonomy::SourceRole;
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    const QString snapshotOnlyFile = QStringLiteral("snapshot_only.sv");
    QList<SemanticSymbolRecord> snapshotRecords;
    const SemanticSymbolRecord snapshotTop = makeSemanticFixtureRecord(
        QStringLiteral("snap_top"),
        DeclarationKind::Module,
        CollectorKind::Module,
        QString(),
        QString(),
        4000,
        snapshotOnlyFile);
    snapshotRecords.append(snapshotTop);
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_child"),
        DeclarationKind::Module,
        CollectorKind::Module,
        QString(),
        QString(),
        7000,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_if"),
        DeclarationKind::Interface,
        CollectorKind::Interface,
        QString(),
        QString(),
        4002,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pkg"),
        DeclarationKind::Package,
        CollectorKind::Package,
        QString(),
        QString(),
        4004,
        snapshotOnlyFile));
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_pkg_param"))
            .withFile(snapshotOnlyFile)
            .withLocalHandle(4005)
            .withLine(4005)
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Parameter,
                                                  SymbolOwnerScope::Package,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Package, QStringLiteral("snap_pkg"))
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("snap_pkg"))
            .withFile(snapshotOnlyFile)
            .withLocalHandle(4006)
            .withLine(1)
            .withCollectorKind(CollectorKind::PackageImport)
            .withUsageRole(SymbolTaxonomy::SymbolUsageRole::Reference)
            .withType(QStringLiteral("*"))
            .record());
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("SNAP_FEATURE"),
        DeclarationKind::Macro,
        CollectorKind::DefDefine,
        QString(),
        QString(),
        4003,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pixel"),
        DeclarationKind::StructVariable,
        CollectorKind::PackedStructVariable,
        QString(),
        QStringLiteral("global_pixel_t"),
        5001,
        QStringLiteral("other_snapshot.sv")));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pixel"),
        DeclarationKind::StructVariable,
        CollectorKind::PackedStructVariable,
        QStringLiteral("snap_top"),
        QStringLiteral("snap_pixel_t"),
        5002,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pair"),
        DeclarationKind::StructVariable,
        CollectorKind::UnpackedStructVariable,
        QStringLiteral("snap_top"),
        QStringLiteral("snap_pair_t"),
        5003,
        snapshotOnlyFile));
    const SemanticSymbolRecord snapshotEnable = makeSemanticFixtureRecord(
        QStringLiteral("snap_enable"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("snap_top"),
        QString(),
        5008,
        snapshotOnlyFile);
    snapshotRecords.append(snapshotEnable);
    const SemanticSymbolRecord duplicateSnapshotEnable =
        makeSemanticFixtureRecord(QStringLiteral("snap_enable"),
                                  DeclarationKind::Signal,
                                  CollectorKind::Logic,
                                  QStringLiteral("snap_top"),
                                  QString(),
                                  5017,
                                  snapshotOnlyFile);
    snapshotRecords.append(duplicateSnapshotEnable);
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_top_signal"))
            .withFile(snapshotOnlyFile)
            .withLocalHandle(5016)
            .withLine(5016)
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Signal,
                                                  SymbolOwnerScope::Module,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Module, QStringLiteral("snap_top"))
            .record());
    const SemanticSymbolRecord snapshotOtherEnable = makeSemanticFixtureRecord(
        QStringLiteral("snap_other_enable"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("other_top"),
        QString(),
        5009,
        snapshotOnlyFile);
    snapshotRecords.append(snapshotOtherEnable);
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_task"),
        DeclarationKind::Task,
        CollectorKind::Task,
        QString(),
        QString(),
        5010,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_state_t"),
        DeclarationKind::Typedef,
        CollectorKind::Typedef,
        QString(),
        QStringLiteral("enum"),
        5011,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_local_state_t"),
        DeclarationKind::Typedef,
        CollectorKind::Typedef,
        QStringLiteral("snap_top"),
        QStringLiteral("enum"),
        5012,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_state"),
        DeclarationKind::Enum,
        CollectorKind::EnumVariable,
        QStringLiteral("snap_top"),
        QString(),
        5013,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("SNAP_IDLE"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        QStringLiteral("snap_top"),
        QString(),
        5014,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("SNAP_RUN"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        QStringLiteral("snap_top"),
        QString(),
        5015,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("red"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("other_t"),
        QString(),
        5004,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("red"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("snap_pixel_t"),
        QString(),
        5005,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("green"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("snap_pixel_t"),
        QString(),
        5006,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("blue"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("snap_pixel_t"),
        QString(),
        5007,
        snapshotOnlyFile));
    const QString snapshotScopeFile = QStringLiteral("snapshot_scope.sv");
    const QString snapshotScopeContent =
        QStringLiteral("module snap_scope;\n"
                       "  logic snap_signal;\n"
                       "endmodule\n"
                       "module other_scope;\n"
                       "endmodule\n");
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("snap_scope"),
                                     DeclarationKind::Module)
            .withFile(snapshotScopeFile)
            .withLocalHandle(6000)
            .withRange(1, 1, 3, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("module snap_scope")),
                          QStringLiteral("snap_scope").size())
            .withCollectorKind(CollectorKind::Module)
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("snap_signal"),
                                     DeclarationKind::Signal)
            .withFile(snapshotScopeFile)
            .withLocalHandle(6001)
            .withRange(2, 1, 2, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("snap_signal")),
                          QStringLiteral("snap_signal").size())
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("snap_scope"))
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("meta_signal"))
            .withFile(snapshotScopeFile)
            .withLocalHandle(6003)
            .withRange(2, 1, 2, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("snap_signal")),
                          QStringLiteral("meta_signal").size())
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Signal,
                                                  SymbolOwnerScope::Module,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Module, QStringLiteral("snap_scope"))
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("other_scope"),
                                     DeclarationKind::Module)
            .withFile(snapshotScopeFile)
            .withLocalHandle(6002)
            .withRange(4, 1, 5, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("module other_scope")),
                          QStringLiteral("other_scope").size())
            .withCollectorKind(CollectorKind::Module)
            .record());
    const SemanticSymbolRecord snapshotClock = makeSemanticFixtureRecord(
        QStringLiteral("snap_clk"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("snap_top"),
        QString(),
        6003,
        snapshotScopeFile);
    snapshotRecords.append(snapshotClock);
    const SemanticSymbolRecord snapshotReset = makeSemanticFixtureRecord(
        QStringLiteral("snap_rst_n"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("snap_top"),
        QString(),
        6004,
        snapshotScopeFile);
    snapshotRecords.append(snapshotReset);
    const QString semanticModuleScopeFile =
        QStringLiteral("semantic_module_scope.sv");
    const QString semanticModuleScopeContent =
        QStringLiteral("module semantic_scope;\n"
                       "  logic semantic_signal;\n"
                       "endmodule\n");
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_scope"))
            .withFile(semanticModuleScopeFile)
            .withLocalHandle(6010)
            .withRange(1, 1, 3, 1)
            .withTextSpan(semanticModuleScopeContent.indexOf(
                              QStringLiteral("module semantic_scope")),
                          QStringLiteral("semantic_scope").size())
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Module,
                                                  SymbolOwnerScope::Global,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Global)
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_signal"),
                                     DeclarationKind::Signal)
            .withFile(semanticModuleScopeFile)
            .withLocalHandle(6011)
            .withRange(2, 1, 2, 1)
            .withTextSpan(semanticModuleScopeContent.indexOf(
                              QStringLiteral("semantic_signal")),
                          QStringLiteral("semantic_signal").size())
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("semantic_scope"))
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(
            QStringLiteral("semantic_metadata_signal"))
            .withFile(semanticModuleScopeFile)
            .withLocalHandle(6012)
            .withRange(2, 1, 2, 1)
            .withTextSpan(semanticModuleScopeContent.indexOf(
                              QStringLiteral("semantic_signal")),
                          QStringLiteral("semantic_metadata_signal").size())
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Signal,
                                                  SymbolOwnerScope::Module,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Module,
                       QStringLiteral("semantic_scope"))
            .record());
    QList<SemanticRelationship> snapshotRelationships;
    snapshotRelationships.append(
        semanticFixtureRelationship(snapshotTop,
                                    snapshotEnable,
                                    SymbolRelationshipEngine::CONTAINS));
    snapshotRelationships.append(semanticFixtureRelationship(
        snapshotTop,
        duplicateSnapshotEnable,
        SymbolRelationshipEngine::CONTAINS));
    snapshotRelationships.append(semanticFixtureRelationship(
        snapshotOtherEnable,
        snapshotEnable,
        SymbolRelationshipEngine::REFERENCES));
    snapshotRelationships.append(
        semanticFixtureRelationship(snapshotClock,
                                    snapshotTop,
                                    SymbolRelationshipEngine::CLOCKS));
    snapshotRelationships.append(
        semanticFixtureRelationship(snapshotReset,
                                    snapshotTop,
                                    SymbolRelationshipEngine::RESETS));
    QHash<QString, QString> snapshotFileContents;
    snapshotFileContents.insert(snapshotOnlyFile,
                                QStringLiteral("import snap_pkg::*;\n"
                                               "module snap_top;\n"
                                               "endmodule\n"));
    snapshotFileContents.insert(snapshotScopeFile, snapshotScopeContent);
    snapshotFileContents.insert(semanticModuleScopeFile, semanticModuleScopeContent);
    SemanticIndex snapshotIndex;
    snapshotIndex.setSnapshot(
        sharedSnapshotFromRecords(
            snapshotRecords,
            snapshotRelationships,
            QList<SemanticDiagnostic>{},
            snapshotFileContents));
    CompletionService snapshotCompletionService(&snapshotIndex);
    RelationshipService snapshotRelationshipService(&snapshotIndex);
    expectBool("RelationshipService named contains",
               snapshotRelationshipService.hasNamedRelationship(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap_enable"),
                   SymbolRelationshipEngine::CONTAINS),
               true);
    expectBool("RelationshipService named rejects missing",
               snapshotRelationshipService.hasNamedRelationship(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_top"),
                   SymbolRelationshipEngine::CONTAINS),
               false);
    const QList<SemanticSymbolRecord> snapshotStructRecords =
        snapshotIndex.getSymbolRecords();
    bool snapshotStructRecordOk = false;
    for (const SemanticSymbolRecord& record : snapshotStructRecords) {
        if (record.collectorKind
            != SymbolTaxonomy::CollectorKind::PackedStructVariable)
            continue;
        if (record.name == QStringLiteral("snap_pixel")
            && record.owner.name == QStringLiteral("snap_top")
            && record.type.rawTypeText == QStringLiteral("snap_pixel_t")) {
            snapshotStructRecordOk = true;
            break;
        }
    }
    expectBool("snapshot struct variable semantic record",
               snapshotStructRecordOk,
               true);
    CommandCompletionQuery snapshotLogicCommandQuery;
    snapshotLogicCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotLogicCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotLogicCommandQuery.commandKind = CompletionCommandKind::Logic;
    snapshotLogicCommandQuery.prefix = QStringLiteral("snap_e");
    expectList("snapshot command logic names",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotLogicCommandQuery)),
               {"snap_enable"});
    const QList<SemanticSymbolRecord> snapshotLogicCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotLogicCommandQuery);
    ++g_checks;
    const bool snapshotLogicCommandOk = snapshotLogicCommandSymbols.size() == 1
        && snapshotLogicCommandSymbols.first().name == QStringLiteral("snap_enable")
        && snapshotLogicCommandSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::Logic
        && snapshotLogicCommandSymbols.first().owner.name == QStringLiteral("snap_top");
    if (!snapshotLogicCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotLogicCommandOk ? "PASS" : "FAIL",
           "snapshot command logic symbols",
           snapshotLogicCommandSymbols.size());
    CommandCompletionQuery snapshotMetadataModuleCommandQuery;
    snapshotMetadataModuleCommandQuery.commandKind =
        CompletionCommandKind::Module;
    snapshotMetadataModuleCommandQuery.prefix = QStringLiteral("semantic");
    expectList("snapshot metadata command module",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotMetadataModuleCommandQuery)),
               {"semantic_scope"});
    CommandCompletionQuery snapshotTaskCommandQuery;
    snapshotTaskCommandQuery.commandKind = CompletionCommandKind::Task;
    snapshotTaskCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command task names",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotTaskCommandQuery)),
               {"snap_task"});
    const QList<SemanticSymbolRecord> snapshotTaskCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotTaskCommandQuery);
    ++g_checks;
    const bool snapshotTaskCommandOk = snapshotTaskCommandSymbols.size() == 1
        && snapshotTaskCommandSymbols.first().name == QStringLiteral("snap_task")
        && snapshotTaskCommandSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::Task
        && snapshotTaskCommandSymbols.first().owner.name.isEmpty();
    if (!snapshotTaskCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotTaskCommandOk ? "PASS" : "FAIL",
           "snapshot command task symbols",
           snapshotTaskCommandSymbols.size());
    CommandCompletionQuery snapshotModuleCommandQuery;
    snapshotModuleCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotModuleCommandQuery.commandKind = CompletionCommandKind::Module;
    snapshotModuleCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command module in scope",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotModuleCommandQuery)),
               {"snap_child", "snap_scope", "snap_top"});
    const QList<SemanticSymbolRecord> snapshotModuleCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotModuleCommandQuery);
    ++g_checks;
    const bool snapshotModuleCommandOk = snapshotModuleCommandSymbols.size() == 3
        && snapshotModuleCommandSymbols.first().localHandle == 7000
        && snapshotModuleCommandSymbols.at(1).localHandle == 6000
        && snapshotModuleCommandSymbols.last().localHandle == 4000;
    if (!snapshotModuleCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotModuleCommandOk ? "PASS" : "FAIL",
           "snapshot command module symbols",
           snapshotModuleCommandSymbols.size());
    CommandCompletionQuery snapshotInterfaceCommandQuery;
    snapshotInterfaceCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotInterfaceCommandQuery.commandKind =
        CompletionCommandKind::Interface;
    snapshotInterfaceCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command interface in scope",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotInterfaceCommandQuery)),
               {"snap_if"});
    const QList<SemanticSymbolRecord> snapshotInterfaceCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotInterfaceCommandQuery);
    ++g_checks;
    const bool snapshotInterfaceCommandOk = snapshotInterfaceCommandSymbols.size() == 1
        && snapshotInterfaceCommandSymbols.first().localHandle == 4002;
    if (!snapshotInterfaceCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotInterfaceCommandOk ? "PASS" : "FAIL",
           "snapshot command interface symbols",
           snapshotInterfaceCommandSymbols.size());
    CommandCompletionQuery snapshotPackageCommandQuery;
    snapshotPackageCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotPackageCommandQuery.commandKind = CompletionCommandKind::Package;
    snapshotPackageCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command package in scope",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotPackageCommandQuery)),
               {"snap_pkg"});
    const QList<SemanticSymbolRecord> snapshotPackageCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotPackageCommandQuery);
    ++g_checks;
    const bool snapshotPackageCommandOk = snapshotPackageCommandSymbols.size() == 1
        && snapshotPackageCommandSymbols.first().localHandle == 4004;
    if (!snapshotPackageCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotPackageCommandOk ? "PASS" : "FAIL",
           "snapshot command package symbols",
           snapshotPackageCommandSymbols.size());
    CommandModeCompletionQuery snapshotPackageImportStateQuery;
    snapshotPackageImportStateQuery.lineUpToCursor =
        QStringLiteral(";pk snap");
    snapshotPackageImportStateQuery.moduleName = QStringLiteral("snap_top");
    const CommandModeCompletionState snapshotPackageImportState =
        snapshotCompletionService.commandModeCompletionState(
            snapshotPackageImportStateQuery);
    expectBool("snapshot package import command state",
               snapshotPackageImportState.matched
                   && snapshotPackageImportState.intent
                       == InlineCommandIntent::PackageImport
                   && snapshotPackageImportState.showCompletions
                   && snapshotPackageImportState.commandKind
                       == CompletionCommandKind::Package
                   && recordNames(snapshotPackageImportState.symbolRecords)
                          == QStringList{QStringLiteral("snap_pkg")},
               true);
    if (!snapshotPackageCommandSymbols.isEmpty()) {
        const CommandSymbolCompletionItem packageImportItem =
            snapshotCompletionService.commandSymbolCompletionItem(
                snapshotPackageCommandSymbols.first(),
                CompletionCommandKind::Package,
                QStringLiteral("snap"));
        expectBool("snapshot package import item",
                   packageImportItem.text == QStringLiteral("snap_pkg")
                       && packageImportItem.defaultValue
                           == QStringLiteral("import snap_pkg::*;")
                       && packageImportItem.description
                           == QStringLiteral("package import"),
                   true);
        CompletionActivationQuery packageImportActivationQuery;
        packageImportActivationQuery.selectable = true;
        packageImportActivationQuery.itemText = packageImportItem.text;
        packageImportActivationQuery.defaultValue =
            packageImportItem.defaultValue;
        const CompletionActivationState packageImportActivation =
            snapshotCompletionService.completionActivationState(
                packageImportActivationQuery);
        expectBool("snapshot package import activation",
                   packageImportActivation.action
                           == CompletionActivationAction::ReplaceCommandInput
                       && packageImportActivation.text
                           == QStringLiteral("import snap_pkg::*;")
                       && packageImportActivation.clearCommandMode
                       && packageImportActivation.hidePopup,
                   true);
    }
    CommandCompletionQuery snapshotSemanticPackageParamQuery;
    snapshotSemanticPackageParamQuery.moduleName = QStringLiteral("snap_top");
    snapshotSemanticPackageParamQuery.commandKind =
        CompletionCommandKind::Parameter;
    snapshotSemanticPackageParamQuery.prefix = QStringLiteral("semantic");
    expectList("snapshot semantic package parameter hidden before import context",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotSemanticPackageParamQuery)),
               {});
    snapshotSemanticPackageParamQuery.fileName = snapshotOnlyFile;
    snapshotSemanticPackageParamQuery.cursorLine = 2;
    expectList("snapshot semantic package parameter command",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotSemanticPackageParamQuery)),
               {"semantic_pkg_param"});
    CommandCompletionQuery snapshotDefineCommandQuery;
    snapshotDefineCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotDefineCommandQuery.commandKind = CompletionCommandKind::Macro;
    snapshotDefineCommandQuery.prefix = QStringLiteral("SNAP");
    expectList("snapshot command define in scope",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotDefineCommandQuery)),
               {"SNAP_FEATURE"});
    const QList<SemanticSymbolRecord> snapshotDefineCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotDefineCommandQuery);
    ++g_checks;
    const bool snapshotDefineCommandOk = snapshotDefineCommandSymbols.size() == 1
        && snapshotDefineCommandSymbols.first().localHandle == 4003;
    if (!snapshotDefineCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotDefineCommandOk ? "PASS" : "FAIL",
           "snapshot command define symbols",
           snapshotDefineCommandSymbols.size());
    CommandCompletionQuery snapshotEnumCommandQuery;
    snapshotEnumCommandQuery.commandKind = CompletionCommandKind::EnumType;
    snapshotEnumCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command enum typedef",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotEnumCommandQuery)),
               {"snap_state_t"});
    snapshotEnumCommandQuery.moduleName = QStringLiteral("snap_top");
    expectList("snapshot command local enum typedef",
               recordNames(snapshotCompletionService
                               .findCommandCompletionSymbolRecords(
                                   snapshotEnumCommandQuery)),
               {"snap_local_state_t"});
    expectList("snapshot semantic metadata scope names",
               snapshotIndex.getScopeSymbolNames(semanticModuleScopeFile, 2),
               {"semantic_scope", "semantic_signal", "semantic_metadata_signal"});
    const QList<SemanticSymbolRecord> snapshotEnumRecords =
        snapshotIndex.getSymbolRecords();
    bool snapshotEnumRecordOk = false;
    for (const SemanticSymbolRecord& record : snapshotEnumRecords) {
        if (record.collectorKind != SymbolTaxonomy::CollectorKind::EnumValue)
            continue;
        if (record.name == QStringLiteral("SNAP_IDLE")
            && record.owner.name == QStringLiteral("snap_top")) {
            snapshotEnumRecordOk = true;
            break;
        }
    }
    expectBool("snapshot enum value semantic record",
               snapshotEnumRecordOk,
               true);
    CommandCompletionQuery snapshotCommandQuery;
    snapshotCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotCommandQuery.documentText = QStringLiteral("module snap_top;\nendmodule\n");
    snapshotCommandQuery.commandKind =
        CompletionCommandKind::PackedStructVariable;
    snapshotCommandQuery.prefix = QStringLiteral("snap");
    const QList<SemanticSymbolRecord> snapshotCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(snapshotCommandQuery);
    ++g_checks;
    const bool snapshotCommandOk = snapshotCommandSymbols.size() == 1
        && snapshotCommandSymbols.first().name == QStringLiteral("snap_pixel")
        && snapshotCommandSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::PackedStructVariable
        && snapshotCommandSymbols.first().owner.name == QStringLiteral("snap_top")
        && snapshotCommandSymbols.first().type.rawTypeText
            == QStringLiteral("snap_pixel_t");
    if (!snapshotCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotCommandOk ? "PASS" : "FAIL",
           "snapshot command struct symbols",
           snapshotCommandSymbols.size());

    QTemporaryDir atomicPublicationWorkspace;
    expectBool("Workspace atomic publication temp dir valid",
               atomicPublicationWorkspace.isValid(),
               true);
    const QString atomicCurrentFile =
        QDir(atomicPublicationWorkspace.path()).absoluteFilePath(
            QStringLiteral("atomic_current.sv"));
    const QString atomicOpenFile =
        QDir(atomicPublicationWorkspace.path()).absoluteFilePath(
            QStringLiteral("atomic_open.sv"));
    const QString atomicBackgroundFile =
        QDir(atomicPublicationWorkspace.path()).absoluteFilePath(
            QStringLiteral("atomic_background.sv"));
    const QString atomicCurrentModule =
        QStringLiteral("atomic_current_pub_module");
    const QString atomicOpenModule =
        QStringLiteral("atomic_open_pub_module");
    const QString atomicBackgroundModule =
        QStringLiteral("atomic_background_pub_module");

    auto writeTextFile = [](const QString& fileName, const QString& text) {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream out(&file);
        out << text;
        return true;
    };

    {
        QTemporaryDir ignoreSettingsDir;
        expectBool("Workspace ignore settings temp dir valid",
                   ignoreSettingsDir.isValid(),
                   true);
        const QSettings::Format previousSettingsFormat =
            QSettings::defaultFormat();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,
                           QSettings::UserScope,
                           ignoreSettingsDir.path());

        QTemporaryDir ignoreWorkspaceA;
        QTemporaryDir ignoreWorkspaceB;
        expectBool("Workspace ignore temp dir A valid",
                   ignoreWorkspaceA.isValid(),
                   true);
        expectBool("Workspace ignore temp dir B valid",
                   ignoreWorkspaceB.isValid(),
                   true);

        const QString rtlDir =
            QDir(ignoreWorkspaceA.path()).absoluteFilePath(
                QStringLiteral("rtl"));
        const QString generatedDir =
            QDir(ignoreWorkspaceA.path()).absoluteFilePath(
                QStringLiteral("generated"));
        expectBool("Workspace ignore child dirs created",
                   QDir().mkpath(rtlDir) && QDir().mkpath(generatedDir),
                   true);

        const QString keepFile =
            QDir(rtlDir).absoluteFilePath(QStringLiteral("keep_top.sv"));
        const QString ignoredFile =
            QDir(generatedDir).absoluteFilePath(
                QStringLiteral("ignored_top.sv"));
        const QString workspaceBFile =
            QDir(ignoreWorkspaceB.path()).absoluteFilePath(
                QStringLiteral("b_top.sv"));
        expectBool("Workspace ignore keep file created",
                   writeTextFile(
                       keepFile,
                       QStringLiteral("module keep_top; endmodule\n")),
                   true);
        expectBool("Workspace ignore generated file created",
                   writeTextFile(
                       ignoredFile,
                       QStringLiteral("module ignored_top; endmodule\n")),
                   true);
        expectBool("Workspace ignore B file created",
                   writeTextFile(
                       workspaceBFile,
                       QStringLiteral("module b_top; endmodule\n")),
                   true);

        auto normalizeTestPath = [](const QString& path) {
            return QDir::cleanPath(
                QDir::fromNativeSeparators(
                    QFileInfo(path).absoluteFilePath()));
        };
        const QString normalizedGeneratedDir =
            normalizeTestPath(generatedDir);
        const QString normalizedKeepFile = normalizeTestPath(keepFile);
        const QString normalizedIgnoredFile = normalizeTestPath(ignoredFile);
        const QString normalizedWorkspaceBFile =
            normalizeTestPath(workspaceBFile);

        const WorkspaceIgnoreReport relativeIgnoreReport =
            WorkspaceIgnoreService::getInstance()
                ->normalizeIgnoredDirectories(
                    WorkspaceIgnoreQuery{
                        ignoreWorkspaceA.path(),
                        {QStringLiteral("generated"), generatedDir}});
        expectBool("Workspace ignore service normalizes and dedupes",
                   relativeIgnoreReport.valid
                       && relativeIgnoreReport.ignoredDirectories
                              == QStringList{normalizedGeneratedDir},
                   true);

        const WorkspaceIgnoreReport outsideIgnoreReport =
            WorkspaceIgnoreService::getInstance()
                ->normalizeIgnoredDirectories(
                    WorkspaceIgnoreQuery{
                        ignoreWorkspaceA.path(),
                        {QDir::tempPath()}});
        expectBool("Workspace ignore service rejects outside path",
                   !outsideIgnoreReport.valid
                       && !outsideIgnoreReport.failureReason.isEmpty(),
                   true);

        const WorkspaceIgnoreReport rootIgnoreReport =
            WorkspaceIgnoreService::getInstance()
                ->normalizeIgnoredDirectories(
                    WorkspaceIgnoreQuery{
                        ignoreWorkspaceA.path(),
                        {ignoreWorkspaceA.path()}});
        expectBool("Workspace ignore service rejects workspace root",
                   !rootIgnoreReport.valid
                       && !rootIgnoreReport.failureReason.isEmpty(),
                   true);

        WorkspaceManager ignoreWorkspaceManager;
        expectBool("Workspace ignore open workspace A",
                   ignoreWorkspaceManager.openWorkspace(
                       ignoreWorkspaceA.path()),
                   true);
        expectBool("Workspace ignore scan workspace A",
                   waitForEventPredicate(
                       [&]() {
                           const QStringList svFiles =
                               ignoreWorkspaceManager.getSystemVerilogFiles();
                           return ignoreWorkspaceManager.workspaceEntries().size()
                                      == 1
                               && ignoreWorkspaceManager.workspaceEntries()
                                      .first()
                                      .scanComplete
                               && svFiles.contains(normalizedKeepFile)
                               && svFiles.contains(normalizedIgnoredFile);
                       },
                       3000),
                   true);

        QString ignoreError;
        expectBool("Workspace ignore manager applies generated dir",
                   ignoreWorkspaceManager.setIgnoredDirectories(
                       {generatedDir},
                       &ignoreError),
                   true);
        expectList("Workspace ignore manager stores normalized dir",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   {normalizedGeneratedDir});
        expectBool("Workspace ignore manager hides generated file",
                   ignoreWorkspaceManager.getSystemVerilogFiles().contains(
                       normalizedKeepFile)
                       && !ignoreWorkspaceManager.getSystemVerilogFiles()
                               .contains(normalizedIgnoredFile),
                   true);

        const QStringList rejectedBaseline =
            ignoreWorkspaceManager.ignoredDirectories();
        expectBool("Workspace ignore manager rejects outside dir",
                   !ignoreWorkspaceManager.setIgnoredDirectories(
                       {QDir::tempPath()},
                       &ignoreError)
                       && !ignoreError.isEmpty(),
                   true);
        expectList("Workspace ignore reject preserves dirs",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   rejectedBaseline);

        expectBool("Workspace ignore open workspace B",
                   ignoreWorkspaceManager.openWorkspace(
                       ignoreWorkspaceB.path()),
                   true);
        expectBool("Workspace ignore scan workspace B",
                   waitForEventPredicate(
                       [&]() {
                           return ignoreWorkspaceManager.activeWorkspaceIndex()
                                      == 1
                               && ignoreWorkspaceManager.workspaceEntries().size()
                                      == 2
                               && ignoreWorkspaceManager.workspaceEntries()
                                      .at(1)
                                      .scanComplete
                               && ignoreWorkspaceManager.getSystemVerilogFiles()
                                      .contains(normalizedWorkspaceBFile);
                       },
                       3000),
                   true);
        expectBool("Workspace ignore switch back to A",
                   ignoreWorkspaceManager.switchWorkspace(0),
                   true);
        expectList("Workspace ignore cached switch restores dirs",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   {normalizedGeneratedDir});
        expectBool("Workspace ignore cached switch filters files",
                   ignoreWorkspaceManager.getSystemVerilogFiles().contains(
                       normalizedKeepFile)
                       && !ignoreWorkspaceManager.getSystemVerilogFiles()
                               .contains(normalizedIgnoredFile),
                   true);

        expectBool("Workspace ignore clear succeeds",
                   ignoreWorkspaceManager.setIgnoredDirectories(
                       QStringList(),
                       &ignoreError),
                   true);
        expectList("Workspace ignore clear resets dirs",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   QStringList());
        expectBool("Workspace ignore clear restores hidden file",
                   ignoreWorkspaceManager.getSystemVerilogFiles().contains(
                       normalizedKeepFile)
                       && ignoreWorkspaceManager.getSystemVerilogFiles()
                              .contains(normalizedIgnoredFile),
                   true);

        WorkspaceConfigurationService explicitConfigStore(
            QDir(ignoreSettingsDir.path()).absoluteFilePath(
                QStringLiteral("workspace-config.ini")));
        WorkspaceConfiguration storedConfig =
            explicitConfigStore.defaultConfiguration(ignoreWorkspaceA.path());
        storedConfig.includeDirs = {rtlDir, ignoreWorkspaceA.path()};
        storedConfig.defines.insert(QStringLiteral("WIDTH"),
                                    QStringLiteral("32"));
        storedConfig.defines.insert(QStringLiteral("FOO"), QString());
        storedConfig.fileExtensions = {QStringLiteral("svx"),
                                       QStringLiteral(".sv")};
        storedConfig.ignoredDirs = {generatedDir};
        storedConfig.topModule = QStringLiteral("keep_top");
        expectBool("Workspace configuration service saves",
                   explicitConfigStore.save(storedConfig),
                   true);
        const WorkspaceConfiguration loadedConfig =
            explicitConfigStore.load(ignoreWorkspaceA.path());
        expectBool("Workspace configuration service persists fields",
                   loadedConfig.includeDirs.contains(normalizedGeneratedDir)
                       == false
                       && loadedConfig.includeDirs.contains(
                              normalizeTestPath(rtlDir))
                       && loadedConfig.defines.value(QStringLiteral("WIDTH"))
                              == QStringLiteral("32")
                       && loadedConfig.defines.contains(QStringLiteral("FOO"))
                       && loadedConfig.fileExtensions
                              == QStringList{QStringLiteral(".svx"),
                                             QStringLiteral(".sv")}
                       && loadedConfig.ignoredDirs
                              == QStringList{normalizedGeneratedDir}
                       && loadedConfig.topModule
                              == QStringLiteral("keep_top"),
                   true);

        QTemporaryDir configWorkspace;
        expectBool("Workspace configuration temp workspace valid",
                   configWorkspace.isValid(),
                   true);
        const QString configRtlDir =
            QDir(configWorkspace.path()).absoluteFilePath(
                QStringLiteral("rtl"));
        const QString configGeneratedDir =
            QDir(configWorkspace.path()).absoluteFilePath(
                QStringLiteral("generated"));
        const QString configIncludeDir =
            QDir(configWorkspace.path()).absoluteFilePath(
                QStringLiteral("include"));
        expectBool("Workspace configuration dirs created",
                   QDir().mkpath(configRtlDir)
                       && QDir().mkpath(configGeneratedDir)
                       && QDir().mkpath(configIncludeDir),
                   true);
        const QString configSvFile =
            QDir(configRtlDir).absoluteFilePath(
                QStringLiteral("config_default.sv"));
        const QString configSvxFile =
            QDir(configRtlDir).absoluteFilePath(
                QStringLiteral("config_extra.svx"));
        const QString configIgnoredFile =
            QDir(configGeneratedDir).absoluteFilePath(
                QStringLiteral("config_generated.svx"));
        expectBool("Workspace configuration sv file created",
                   writeTextFile(configSvFile,
                                 QStringLiteral("module config_default; endmodule\n")),
                   true);
        expectBool("Workspace configuration svx file created",
                   writeTextFile(configSvxFile,
                                 QStringLiteral("module config_extra; endmodule\n")),
                   true);
        expectBool("Workspace configuration ignored svx file created",
                   writeTextFile(configIgnoredFile,
                                 QStringLiteral("module config_generated; endmodule\n")),
                   true);

        WorkspaceManager configWorkspaceManager;
        expectBool("Workspace configuration manager opens workspace",
                   configWorkspaceManager.openWorkspace(configWorkspace.path()),
                   true);
        expectBool("Workspace configuration manager scans workspace",
                   waitForEventPredicate(
                       [&]() {
                           return configWorkspaceManager.workspaceEntries().size()
                                      == 1
                               && configWorkspaceManager.workspaceEntries()
                                      .first()
                                      .scanComplete;
                       },
                       3000),
                   true);
        WorkspaceConfiguration managerConfig =
            configWorkspaceManager.workspaceConfiguration();
        managerConfig.includeDirs = {configIncludeDir};
        managerConfig.defines.insert(QStringLiteral("WIDTH"),
                                     QStringLiteral("64"));
        managerConfig.defines.insert(QStringLiteral("USE_FEATURE"),
                                     QString());
        managerConfig.fileExtensions = {QStringLiteral(".svx")};
        managerConfig.ignoredDirs = {configGeneratedDir};
        managerConfig.topModule = QStringLiteral("config_extra");
        QString workspaceConfigError;
        const bool managerConfigurationApplied =
            configWorkspaceManager.setWorkspaceConfiguration(
                managerConfig,
                &workspaceConfigError);
        if (!managerConfigurationApplied) {
            std::fprintf(
                stderr,
                "Workspace configuration diagnostic: error=\"%s\" root=\"%s\" ignored=\"%s\"\n",
                qPrintable(workspaceConfigError),
                qPrintable(managerConfig.workspaceRoot),
                qPrintable(configGeneratedDir));
        }
        expectBool("Workspace configuration manager applies config",
                   managerConfigurationApplied,
                   true);
        const QString normalizedConfigSvx =
            normalizeTestPath(configSvxFile);
        const QString normalizedConfigSv =
            normalizeTestPath(configSvFile);
        const QString normalizedConfigIgnored =
            normalizeTestPath(configIgnoredFile);
        const QStringList configuredSvFiles =
            configWorkspaceManager.getSystemVerilogFiles();
        expectBool("Workspace configuration file extensions filter",
                   configuredSvFiles.contains(normalizedConfigSvx)
                       && !configuredSvFiles.contains(normalizedConfigSv)
                       && !configuredSvFiles.contains(normalizedConfigIgnored),
                   true);
        const WorkspaceConfiguration activeConfig =
            configWorkspaceManager.workspaceConfiguration();
        expectBool("Workspace configuration active values round trip",
                   activeConfig.includeDirs
                           == QStringList{normalizeTestPath(configIncludeDir)}
                       && activeConfig.defines.value(
                              QStringLiteral("WIDTH")) == QStringLiteral("64")
                       && activeConfig.defines.contains(
                              QStringLiteral("USE_FEATURE"))
                       && activeConfig.fileExtensions
                              == QStringList{QStringLiteral(".svx")}
                       && activeConfig.ignoredDirs
                              == QStringList{
                                  normalizeTestPath(configGeneratedDir)}
                       && activeConfig.topModule
                              == QStringLiteral("config_extra"),
                   true);

        WorkspaceManager persistedConfigWorkspaceManager;
        expectBool("Workspace configuration persisted workspace opens",
                   persistedConfigWorkspaceManager.openWorkspace(
                       configWorkspace.path()),
                   true);
        expectBool("Workspace configuration persisted scan",
                   waitForEventPredicate(
                       [&]() {
                           return persistedConfigWorkspaceManager
                                      .workspaceEntries()
                                      .size()
                                      == 1
                               && persistedConfigWorkspaceManager
                                      .workspaceEntries()
                                      .first()
                                      .scanComplete;
                       },
                       3000),
                   true);
        const WorkspaceConfiguration persistedActiveConfig =
            persistedConfigWorkspaceManager.workspaceConfiguration();
        expectBool("Workspace configuration persists via manager",
                   persistedActiveConfig.fileExtensions
                           == QStringList{QStringLiteral(".svx")}
                       && persistedActiveConfig.topModule
                              == QStringLiteral("config_extra")
                       && persistedConfigWorkspaceManager
                              .getSystemVerilogFiles()
                              .contains(normalizedConfigSvx)
                       && !persistedConfigWorkspaceManager
                               .getSystemVerilogFiles()
                               .contains(normalizedConfigSv),
                   true);

        QTemporaryDir sessionWorkspaceA;
        QTemporaryDir sessionWorkspaceB;
        QTemporaryDir sessionExternalDir;
        expectBool("Workspace session temp dirs valid",
                   sessionWorkspaceA.isValid()
                       && sessionWorkspaceB.isValid()
                       && sessionExternalDir.isValid(),
                   true);
        const QString sessionARtlDir =
            QDir(sessionWorkspaceA.path()).absoluteFilePath(
                QStringLiteral("rtl"));
        const QString sessionBRtlDir =
            QDir(sessionWorkspaceB.path()).absoluteFilePath(
                QStringLiteral("rtl"));
        const QString sessionAIncludeDir =
            QDir(sessionWorkspaceA.path()).absoluteFilePath(
                QStringLiteral("include"));
        const QString sessionBIncludeDir =
            QDir(sessionWorkspaceB.path()).absoluteFilePath(
                QStringLiteral("include"));
        const QString sessionAIgnoredDir =
            QDir(sessionWorkspaceA.path()).absoluteFilePath(
                QStringLiteral("generated"));
        const QString sessionBIgnoredDir =
            QDir(sessionWorkspaceB.path()).absoluteFilePath(
                QStringLiteral("generated"));
        const QString sessionExternalIncludeDir =
            QDir(sessionExternalDir.path()).absoluteFilePath(
                QStringLiteral("external_inc"));
        expectBool("Workspace session dirs created",
                   QDir().mkpath(sessionARtlDir)
                       && QDir().mkpath(sessionBRtlDir)
                       && QDir().mkpath(sessionAIncludeDir)
                       && QDir().mkpath(sessionBIncludeDir)
                       && QDir().mkpath(sessionAIgnoredDir)
                       && QDir().mkpath(sessionBIgnoredDir)
                       && QDir().mkpath(sessionExternalIncludeDir),
                   true);
        const QString sessionATopFile =
            QDir(sessionARtlDir).absoluteFilePath(
                QStringLiteral("top.sv"));
        const QString sessionBTopFile =
            QDir(sessionBRtlDir).absoluteFilePath(
                QStringLiteral("top.sv"));
        const QString sessionAHelperFile =
            QDir(sessionARtlDir).absoluteFilePath(
                QStringLiteral("helper.svh"));
        const QString sessionBHelperFile =
            QDir(sessionBRtlDir).absoluteFilePath(
                QStringLiteral("helper.svh"));
        const QString sessionAMissingFile =
            QDir(sessionARtlDir).absoluteFilePath(
                QStringLiteral("missing.sv"));
        expectBool("Workspace session files created",
                   writeTextFile(sessionATopFile,
                                 QStringLiteral("module session_top; endmodule\n"))
                       && writeTextFile(sessionBTopFile,
                                        QStringLiteral("module session_top; endmodule\n"))
                       && writeTextFile(sessionAHelperFile,
                                        QStringLiteral("`define SESSION_OK\n"))
                       && writeTextFile(sessionBHelperFile,
                                        QStringLiteral("`define SESSION_OK\n")),
                   true);

        WorkspaceSessionState sessionState;
        sessionState.workspaceRoot = sessionWorkspaceA.path();
        sessionState.tabs = {
            WorkspaceSessionTabState{sessionATopFile, 2, 5, 12, 0, false},
            WorkspaceSessionTabState{sessionAHelperFile, 1, 3, 4, 0, true},
            WorkspaceSessionTabState{sessionAMissingFile, 1, 1, 0, 0, false},
        };
        sessionState.ui.mainWindowGeometry =
            QByteArrayLiteral("geometry-bytes");
        sessionState.ui.mainWindowState = QByteArrayLiteral("state-bytes");
        sessionState.ui.navigationFilesQuery =
            QStringLiteral("helper");
        sessionState.ui.navigationDesignQuery =
            QStringLiteral("u_stage");
        sessionState.scannedFiles = {
            sessionATopFile,
            sessionAHelperFile,
            sessionAMissingFile,
            QDir(sessionExternalIncludeDir).absoluteFilePath(
                QStringLiteral("outside.sv")),
        };
        sessionState.scanComplete = true;

        const QString localSessionStore =
            QDir(sessionExternalDir.path()).absoluteFilePath(
                QStringLiteral("local-workspace-sessions.ini"));
        WorkspaceSessionStateService sessionService(localSessionStore);
        const WorkspaceSessionSaveResult sessionSave =
            sessionService.save(sessionState);
        expectBool("Workspace session save writes only local AppData",
                   sessionSave.saved
                       && QFileInfo(sessionSave.storagePath).isFile()
                       && !QFileInfo(
                               QDir(sessionWorkspaceA.path())
                                   .absoluteFilePath(
                                       QStringLiteral(".zs")))
                               .exists()
                       && !QFileInfo(
                               QDir(sessionWorkspaceB.path())
                                   .absoluteFilePath(
                                       QStringLiteral(".zs")))
                               .exists(),
                   true);
        expectBool("Workspace local session does not follow a moved root",
                   !sessionService.load(sessionWorkspaceB.path()).loaded,
                   true);

        const WorkspaceSessionRestoreResult restoredSession =
            sessionService.load(sessionWorkspaceA.path());
        const QString normalizedSessionATop =
            normalizeTestPath(sessionATopFile);
        const QString normalizedSessionAHelper =
            normalizeTestPath(sessionAHelperFile);
        expectBool("Workspace local session load succeeds",
                   restoredSession.loaded
                       && restoredSession.state.workspaceRoot
                              == normalizeTestPath(sessionWorkspaceA.path())
                       && !restoredSession.state.workspaceId.isEmpty(),
                   true);
        bool restoredTopTab = false;
        bool restoredActiveHelperTab = false;
        for (const WorkspaceSessionTabState& tab :
             restoredSession.state.tabs) {
            restoredTopTab =
                restoredTopTab
                || (tab.filePath == normalizedSessionATop
                    && tab.cursorLine == 2
                    && tab.cursorColumn == 5
                    && tab.verticalScrollValue == 12
                    && !tab.active);
            restoredActiveHelperTab =
                restoredActiveHelperTab
                || (tab.filePath == normalizedSessionAHelper
                    && tab.cursorLine == 1
                    && tab.cursorColumn == 3
                    && tab.verticalScrollValue == 4
                    && tab.active);
        }
        expectBool("Workspace session restores tabs and skips missing",
                   restoredSession.state.tabs.size() == 2
                       && restoredTopTab
                       && restoredActiveHelperTab
                       && restoredSession.skippedTabs.size() == 1,
                   true);
        expectBool("Workspace session restores ui bytes",
                   restoredSession.state.ui.mainWindowGeometry
                           == QByteArrayLiteral("geometry-bytes")
                       && restoredSession.state.ui.mainWindowState
                              == QByteArrayLiteral("state-bytes")
                       && restoredSession.state.ui.navigationFilesQuery
                              == QStringLiteral("helper")
                       && restoredSession.state.ui.navigationDesignQuery
                              == QStringLiteral("u_stage"),
                   true);
        expectList("Workspace session restores scanned files",
                   restoredSession.state.scannedFiles,
                   QStringList{normalizedSessionAHelper,
                               normalizedSessionATop});
        expectBool("Workspace session filters missing scanned files",
                   restoredSession.state.scanComplete
                       && restoredSession.skippedScannedFiles.size() == 1,
                   true);
        WorkspaceManager sessionScanManager;
        expectBool("Workspace session scan manager opens moved root",
                   sessionScanManager.openWorkspace(
                       sessionWorkspaceB.path()),
                   true);
        expectBool("Workspace session scan manager restores list",
                   sessionScanManager.restoreSessionScanState(
                       QStringList{normalizeTestPath(sessionBTopFile),
                                   normalizeTestPath(sessionBHelperFile),
                                   QDir(sessionBRtlDir).absoluteFilePath(
                                       QStringLiteral("missing.sv"))},
                       true),
                   true);
        expectBool("Workspace session scan manager filters stale files",
                   sessionScanManager.getAllFiles().size() == 2
                       && sessionScanManager.getAllFiles().contains(
                              normalizeTestPath(sessionBTopFile))
                       && sessionScanManager.getAllFiles().contains(
                              normalizeTestPath(sessionBHelperFile))
                       && sessionScanManager.workspaceEntries().size() == 1
                       && sessionScanManager.workspaceEntries()
                              .first()
                              .scanComplete,
                   true);

        QTemporaryDir slangConfigWorkspace;
        expectBool("Workspace configuration slang temp dir valid",
                   slangConfigWorkspace.isValid(),
                   true);
        const QString slangIncludeDir =
            QDir(slangConfigWorkspace.path()).absoluteFilePath(
                QStringLiteral("inc"));
        expectBool("Workspace configuration slang include dir created",
                   QDir().mkpath(slangIncludeDir),
                   true);
        const QString slangHeaderFile =
            QDir(slangIncludeDir).absoluteFilePath(
                QStringLiteral("defs.svh"));
        const QString slangSourceFile =
            QDir(slangConfigWorkspace.path()).absoluteFilePath(
                QStringLiteral("uses_include.sv"));
        expectBool("Workspace configuration slang header created",
                   writeTextFile(slangHeaderFile,
                                 QStringLiteral("`define INCLUDED_OK\n")),
                   true);
        expectBool("Workspace configuration slang source created",
                   writeTextFile(
                       slangSourceFile,
                       QStringLiteral("`include \"defs.svh\"\nmodule uses_include; endmodule\n")),
                   true);
        const QString slangSourceText =
            QStringLiteral("`include \"defs.svh\"\nmodule uses_include; endmodule\n");
        SlangManager configSlang;
        const QList<SemanticDiagnostic> missingIncludeDiagnostics =
            configSlang.extractDiagnostics(slangSourceFile,
                                           slangSourceText,
                                           QStringList{});
        const QList<SemanticDiagnostic> resolvedIncludeDiagnostics =
            configSlang.extractDiagnostics(slangSourceFile,
                                           slangSourceText,
                                           QStringList{slangIncludeDir});
        expectBool("Workspace configuration include dirs affect Slang diagnostics",
                   !missingIncludeDiagnostics.isEmpty()
                       && resolvedIncludeDiagnostics.isEmpty(),
                   true);
        const QString defineDiagnosticFile =
            QDir(slangConfigWorkspace.path()).absoluteFilePath(
                QStringLiteral("uses_define.sv"));
        expectBool("Workspace configuration define source created",
                   writeTextFile(
                       defineDiagnosticFile,
                       QStringLiteral(
                           "`ifndef ENABLE_OK\n"
                           "module define_bad(input logic a\n"
                           "endmodule\n"
                           "`else\n"
                           "module define_ok; endmodule\n"
                           "`endif\n")),
                   true);
        const QList<SemanticDiagnostic> missingDefineDiagnostics =
            configSlang.extractWorkspaceDiagnostics(
                QStringList{defineDiagnosticFile},
                QStringList{slangConfigWorkspace.path()});
        QHash<QString, QString> enabledDefines;
        enabledDefines.insert(QStringLiteral("ENABLE_OK"), QStringLiteral("1"));
        const QList<SemanticDiagnostic> resolvedDefineDiagnostics =
            configSlang.extractWorkspaceDiagnostics(
                QStringList{defineDiagnosticFile},
                QStringList{slangConfigWorkspace.path()},
                enabledDefines);
        expectBool("Workspace configuration defines affect Slang diagnostics",
                   !missingDefineDiagnostics.isEmpty()
                       && resolvedDefineDiagnostics.isEmpty(),
                   true);

        QSettings::setDefaultFormat(previousSettingsFormat);
    }

    SemanticIndex::getInstance()->clearSnapshot();
    QTemporaryDir diagnosticNavigationWorkspace;
    expectBool("Diagnostic navigation temp dir valid",
               diagnosticNavigationWorkspace.isValid(),
               true);
    const QString diagnosticNavFileA =
        QDir(diagnosticNavigationWorkspace.path()).absoluteFilePath(
            QStringLiteral("nav_a.sv"));
    const QString diagnosticNavFileB =
        QDir(diagnosticNavigationWorkspace.path()).absoluteFilePath(
            QStringLiteral("nav_b.sv"));
    SemanticDiagnostic diagnosticNavErrorA;
    diagnosticNavErrorA.fileName = diagnosticNavFileA;
    diagnosticNavErrorA.line = 2;
    diagnosticNavErrorA.column = 4;
    diagnosticNavErrorA.message = QStringLiteral("first error");
    diagnosticNavErrorA.severity = SemanticDiagnostic::Error;
    diagnosticNavErrorA.owner = SemanticDiagnostic::SlangCompiler;
    SemanticDiagnostic diagnosticNavWarningA;
    diagnosticNavWarningA.fileName = diagnosticNavFileA;
    diagnosticNavWarningA.line = 5;
    diagnosticNavWarningA.column = 2;
    diagnosticNavWarningA.message = QStringLiteral("later warning");
    diagnosticNavWarningA.severity = SemanticDiagnostic::Warning;
    diagnosticNavWarningA.owner = SemanticDiagnostic::SlangCompiler;
    SemanticDiagnostic diagnosticNavErrorB;
    diagnosticNavErrorB.fileName = diagnosticNavFileB;
    diagnosticNavErrorB.line = 1;
    diagnosticNavErrorB.column = 1;
    diagnosticNavErrorB.message = QStringLiteral("workspace error");
    diagnosticNavErrorB.severity = SemanticDiagnostic::Error;
    diagnosticNavErrorB.owner = SemanticDiagnostic::SemanticIndexOwner;
    SemanticIndex::getInstance()->setSnapshot(sharedSnapshotFromRecords(
        {},
        {},
        {diagnosticNavErrorB, diagnosticNavWarningA, diagnosticNavErrorA}));
    DiagnosticNavigationService diagnosticNavigation;
    DiagnosticNavigationQuery navQuery;
    navQuery.currentFileName = diagnosticNavFileA;
    navQuery.currentLine = 3;
    navQuery.currentColumn = 1;
    navQuery.scope = DiagnosticPanelScope::CurrentFile;
    navQuery.severity = DiagnosticSeverityFilter::All;
    DiagnosticNavigationResult navResult =
        diagnosticNavigation.navigate(navQuery);
    expectBool("Diagnostic navigation current file next",
               navResult.found
                   && navResult.diagnostic.diagnostic.fileName
                          == diagnosticNavFileA
                   && navResult.diagnostic.diagnostic.line == 5,
               true);
    navQuery.previous = true;
    navResult = diagnosticNavigation.navigate(navQuery);
    expectBool("Diagnostic navigation current file previous",
               navResult.found
                   && navResult.diagnostic.diagnostic.fileName
                          == diagnosticNavFileA
                   && navResult.diagnostic.diagnostic.line == 2,
               true);
    navQuery.previous = false;
    navQuery.currentLine = 1;
    navQuery.severity = DiagnosticSeverityFilter::Errors;
    navResult = diagnosticNavigation.navigate(navQuery);
    expectBool("Diagnostic navigation severity filter",
               navResult.found
                   && navResult.diagnostic.diagnostic.fileName
                          == diagnosticNavFileA
                   && navResult.diagnostic.diagnostic.line == 2,
               true);
    navQuery.scope = DiagnosticPanelScope::WorkspaceFiles;
    navQuery.severity = DiagnosticSeverityFilter::All;
    navQuery.workspaceFiles = {diagnosticNavFileA, diagnosticNavFileB};
    navQuery.currentFileName = diagnosticNavFileA;
    navQuery.currentLine = 6;
    navResult = diagnosticNavigation.navigate(navQuery);
    expectBool("Diagnostic navigation workspace wraps by location",
               navResult.found
                   && navResult.diagnostic.diagnostic.fileName
                          == diagnosticNavFileB
                   && navResult.diagnostic.diagnostic.line == 1,
               true);
    DiagnosticPanelQueryOptions ownerOptions;
    ownerOptions.scope = DiagnosticPanelScope::AllFiles;
    const DiagnosticReport ownerReport =
        DiagnosticService::getInstance()->findDiagnosticReport(
            DiagnosticService::getInstance()->queryForPanel(ownerOptions));
    bool sawSlangOwner = false;
    bool sawSemanticOwner = false;
    for (const DiagnosticResult& result : ownerReport.diagnostics) {
        sawSlangOwner = sawSlangOwner
            || result.ownerDisplayName == QStringLiteral("Slang");
        sawSemanticOwner = sawSemanticOwner
            || result.ownerDisplayName == QStringLiteral("Semantic index");
    }
    expectBool("Diagnostic owner display names",
               sawSlangOwner && sawSemanticOwner,
               true);
    DiagnosticNavigationQuery noDiagnosticQuery;
    noDiagnosticQuery.currentFileName = diagnosticNavFileA;
    noDiagnosticQuery.scope = DiagnosticPanelScope::WorkspaceFiles;
    noDiagnosticQuery.workspaceFiles = {diagnosticNavFileA};
    noDiagnosticQuery.severity = DiagnosticSeverityFilter::Info;
    const DiagnosticNavigationResult noDiagnosticResult =
        diagnosticNavigation.navigate(noDiagnosticQuery);
    expectBool("Diagnostic navigation reports empty result",
               !noDiagnosticResult.found
                   && noDiagnosticResult.failureReason
                          == QStringLiteral("No diagnostics"),
               true);
    SemanticIndex::getInstance()->clearSnapshot();

    expectBool("Workspace atomic current file created",
               writeTextFile(
                   atomicCurrentFile,
                   QStringLiteral("module atomic_current_pub_module; logic a; endmodule\n")),
               true);
    expectBool("Workspace atomic open file created",
               writeTextFile(
                   atomicOpenFile,
                   QStringLiteral("module atomic_open_pub_module; logic o; endmodule\n")),
               true);
    expectBool("Workspace atomic background file created",
               writeTextFile(
                   atomicBackgroundFile,
                   QStringLiteral("module atomic_background_pub_module; logic b; endmodule\n")),
               true);

    auto snapshotContainsModule =
        [](std::shared_ptr<const SemanticIndexSnapshot> snapshot,
           const QString& moduleName) {
            if (!snapshot)
                return false;
            for (const SemanticSymbolRecord& record
                 : snapshot->getSymbolRecords()) {
                if (record.name == moduleName
                    && record.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Module) {
                    return true;
                }
            }
            return false;
        };

    ProjectSnapshot atomicProject;
    atomicProject.workspaceRoot = atomicPublicationWorkspace.path();
    atomicProject.systemVerilogFiles = {
        atomicCurrentFile,
        atomicOpenFile,
        atomicBackgroundFile
    };
    atomicProject.includeDirs = {atomicPublicationWorkspace.path()};
    SymbolAnalyzer atomicAnalyzer;
    bool firstProgressSeesAtomicSnapshot = false;
    bool secondProgressSeesAtomicSnapshot = false;
    QObject::connect(&atomicAnalyzer,
                     &SymbolAnalyzer::batchProgress,
                     &atomicAnalyzer,
                     [&](int filesDone,
                         int totalFiles,
                         const QString& currentFileName) {
                         if (filesDone != 1
                             && filesDone != 2) {
                             return;
                         }
                         if (totalFiles != 3)
                             return;
                         const auto snapshot =
                             SemanticIndex::getInstance()->snapshot();
                         if (filesDone == 1
                             && currentFileName == atomicCurrentFile) {
                             firstProgressSeesAtomicSnapshot =
                                 snapshotContainsModule(snapshot,
                                                        atomicCurrentModule)
                                 && snapshotContainsModule(snapshot,
                                                           atomicOpenModule)
                                 && snapshotContainsModule(
                                     snapshot,
                                     atomicBackgroundModule);
                         }
                         if (filesDone == 2
                             && currentFileName == atomicOpenFile) {
                             secondProgressSeesAtomicSnapshot =
                                 snapshotContainsModule(snapshot,
                                                        atomicCurrentModule)
                                 && snapshotContainsModule(snapshot,
                                                           atomicOpenModule)
                                 && snapshotContainsModule(
                                     snapshot,
                                     atomicBackgroundModule);
                         }
                     });
    atomicAnalyzer.analyzeProject(atomicProject);
    expectBool("Workspace first progress observes atomic snapshot",
               firstProgressSeesAtomicSnapshot,
               true);
    expectBool("Workspace later progress preserves atomic snapshot",
               secondProgressSeesAtomicSnapshot,
               true);
    const auto finalAtomicSnapshot = SemanticIndex::getInstance()->snapshot();
    expectBool("Workspace final publication includes background symbols",
               snapshotContainsModule(finalAtomicSnapshot,
                                      atomicCurrentModule)
                   && snapshotContainsModule(finalAtomicSnapshot,
                                             atomicOpenModule)
                   && snapshotContainsModule(finalAtomicSnapshot,
                                             atomicBackgroundModule),
               true);

    SemanticIndex::getInstance()->clearSnapshot();
    QTemporaryDir cancelWorkspace;
    expectBool("Workspace cancel temp dir valid",
               cancelWorkspace.isValid(),
               true);
    const QString cancelFirstFile =
        QDir(cancelWorkspace.path()).absoluteFilePath(
            QStringLiteral("cancel_first.sv"));
    const QString cancelSecondFile =
        QDir(cancelWorkspace.path()).absoluteFilePath(
            QStringLiteral("cancel_second.sv"));
    expectBool("Workspace cancel first file created",
               writeTextFile(
                   cancelFirstFile,
                   QStringLiteral("module cancel_first_pub_module; logic a; endmodule\n")),
               true);
    expectBool("Workspace cancel second file created",
               writeTextFile(
                   cancelSecondFile,
                   QStringLiteral("module cancel_second_pub_module; logic b; endmodule\n")),
               true);

    ProjectSnapshot cancelProject;
    cancelProject.workspaceRoot = cancelWorkspace.path();
    cancelProject.systemVerilogFiles = {
        cancelFirstFile,
        cancelSecondFile
    };
    cancelProject.includeDirs = {cancelWorkspace.path()};
    SymbolAnalyzer cancelAnalyzer;
    bool cancelExpired = false;
    int cancelCompletionSymbols = -1;
    QObject::connect(&cancelAnalyzer,
                     &SymbolAnalyzer::workspaceAnalysisExpired,
                     &cancelAnalyzer,
                     [&cancelExpired]() {
                         cancelExpired = true;
                     });
    QObject::connect(&cancelAnalyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     &cancelAnalyzer,
                     [&cancelCompletionSymbols](
                         const QString&,
                         int symbolsFound) {
                         cancelCompletionSymbols = symbolsFound;
                     });
    int cancelChecks = 0;
    cancelAnalyzer.analyzeProject(cancelProject, [&cancelChecks]() {
        ++cancelChecks;
        return cancelChecks >= 3;
    });
    expectBool("Workspace cancellation expires before publication",
               cancelExpired
                   && cancelCompletionSymbols == 0
                   && !SemanticIndex::getInstance()->snapshot(),
               true);
    expectBool("Workspace cancellation provider reached Slang boundary",
               cancelChecks >= 3,
               true);
    int slangSymbolCancelChecks = 0;
    SlangManager cancelSlangSymbols;
    const QList<SemanticSymbolRecord> cancelledWorkspaceRecords =
        cancelSlangSymbols.extractWorkspaceSymbolRecords(
            cancelProject.systemVerilogFiles,
            cancelProject.includeDirs,
            QHash<QString, QString>{},
            [&slangSymbolCancelChecks]() {
                ++slangSymbolCancelChecks;
                return slangSymbolCancelChecks >= 8;
            });
    expectBool("Workspace Slang symbols honor cancel boundary",
               cancelledWorkspaceRecords.isEmpty()
                   && slangSymbolCancelChecks >= 8,
               true);

    const QString cancelDiagnosticFile =
        QDir(cancelWorkspace.path()).absoluteFilePath(
            QStringLiteral("cancel_diagnostic.sv"));
    expectBool("Workspace cancel diagnostic file created",
               writeTextFile(
                   cancelDiagnosticFile,
                   QStringLiteral("module cancel_bad(input logic a\nendmodule\n")),
               true);
    SlangManager diagnosticsProbe;
    const QList<SemanticDiagnostic> uncancelledDiagnostics =
        diagnosticsProbe.extractWorkspaceDiagnostics(
            QStringList{cancelDiagnosticFile},
            cancelProject.includeDirs);
    expectBool("Workspace diagnostic fixture produces diagnostics",
               !uncancelledDiagnostics.isEmpty(),
               true);
    int slangDiagnosticCancelChecks = 0;
    SlangManager cancelSlangDiagnostics;
    const QList<SemanticDiagnostic> cancelledWorkspaceDiagnostics =
        cancelSlangDiagnostics.extractWorkspaceDiagnostics(
            QStringList{cancelDiagnosticFile},
            cancelProject.includeDirs,
            QHash<QString, QString>{},
            [&slangDiagnosticCancelChecks]() {
                ++slangDiagnosticCancelChecks;
                return slangDiagnosticCancelChecks >= 5;
            });
    expectBool("Workspace Slang diagnostics honor cancel boundary",
               cancelledWorkspaceDiagnostics.isEmpty()
                   && slangDiagnosticCancelChecks >= 5,
               true);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
