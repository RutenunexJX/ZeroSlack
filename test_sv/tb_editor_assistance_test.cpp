#include "tbinputassist.h"
#include "mycodeeditor.h"
#include "mainwindow.h"
#include "applicationthememanager.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "usertemplateservice.h"
#include "crashrecoveryservice.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "testuistyle.h"
#include "../src/simulation/simdock/core/analyzer.h"
#include "../src/simulation/simdock/core/dependencies.h"
#include "../src/simulation/simdock/core/testbench.h"
#include "../src/simulation/simdock/core/workspace.h"
#include "../src/simulation/simdock/core/questasession.h"
#include <QtTest>
#include <QAbstractButton>
#include <QComboBox>
#include <QClipboard>
#include <QFrame>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>

namespace {
bool put(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
QString report() { return qEnvironmentVariable("ZEROSLACK_TB_ASSIST_REPORT"); }
QString packages() { return QStringLiteral(
    "package drive_pkg;\n parameter int WIDTH=16;\n typedef enum logic [2:0] { IDLE=3'd3, RUN=3'd5, FAIL=3'd7 } mode_t;\nendpackage\n"
    "package other_pkg; typedef enum logic [2:0] { IDLE=3'd1, RUN=3'd2 } mode_t; endpackage\n"); }
QString dut() { return QStringLiteral(
    "module controller #(parameter int W=drive_pkg::WIDTH)(\n input logic clk, rst_n,\n input drive_pkg::mode_t mode,\n input logic [W-1:0] divider,\n output logic [W-1:0] observed, inout wire pin);\n"
    " assign observed = divider;\nendmodule\n"); }
std::shared_ptr<const SemanticIndexSnapshot> analyze(const QHash<QString, QString>& contents) {
    SlangManager slang;
    auto records = slang.extractOverlayWorkspaceSymbolRecords(contents);
    return std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, contents));
}
struct EnumScopeFixture {
    QString name, source, idle, run;
    bool currentExpressionKnown = true;
};
QList<EnumScopeFixture> enumScopeFixtures() {
    const QString members = "typedef enum logic [1:0] {IDLE=2'd1, RUN=2'd2} mode_t;";
    const QString classPackage = "package p; class C; " + members + " endclass endpackage\n";
    const auto tb = [](const QString& type, const QString& initializer, const QString& declarations = {}) {
        return "module controller(input " + type + " mode, output logic [1:0] observed);\n"
            " assign observed=mode;\nendmodule\nmodule tb;\n" + declarations + "\n " + type + " mode;\n"
            " logic [1:0] observed;\n controller dut(.mode(mode), .observed(observed));\n"
            " initial begin\n mode=" + initializer + ";\n #1; $finish;\n end\nendmodule\n";
    };
    const auto localTb = [](const QString& declarations, const QString& type, const QString& initializer) {
        return "module controller(input logic [1:0] mode); endmodule\nmodule tb;\n" + declarations
            + "\n " + type + " mode;\n controller dut(.mode(mode));\n initial begin\n mode="
            + initializer + ";\n #1; $finish;\n end\nendmodule\n";
    };
    return {
        {"class", classPackage + tb("p::C::mode_t", "p::C::IDLE"), "p::C::IDLE", "p::C::RUN"},
        {"collisions", "package p; typedef enum logic [1:0] {IDLE=2'd0, RUN=2'd3} other_t;\n"
            " class D; typedef enum logic [1:0] {IDLE=2'd3, RUN=2'd0} mode_t; endclass\n"
            " class C; " + members + " endclass endpackage\n" + tb("p::C::mode_t", "p::C::IDLE"),
            "p::C::IDLE", "p::C::RUN"},
        {"nested", "package p; class Outer; class Inner; " + members + " endclass endclass endpackage\n"
            + tb("p::Outer::Inner::mode_t", "p::Outer::Inner::IDLE"), "p::Outer::Inner::IDLE", "p::Outer::Inner::RUN"},
        {"unit-shadow", "class C; " + members + " endclass\n"
            + tb("$unit::C::mode_t", "$unit::C::IDLE",
                " class C; typedef enum logic [1:0] {IDLE=2'd3, RUN=2'd0} mode_t; endclass\n"),
            "$unit::C::IDLE", "$unit::C::RUN"},
        {"local", localTb(members, "mode_t", "IDLE"), "IDLE", "RUN"},
        {"local-class", localTb("class C; " + members + " endclass", "C::mode_t", "C::IDLE"), "C::IDLE", "C::RUN"},
        {"alias", "package p; class C; " + members + " endclass typedef C Alias; endpackage\n"
            + tb("p::Alias::mode_t", "p::Alias::IDLE"), "p::C::IDLE", "p::C::RUN", false},
        {"inherited", "package p; class C; " + members + " endclass class D extends C; endclass endpackage\n"
            + tb("p::D::mode_t", "p::C::IDLE"), "p::C::IDLE", "p::C::RUN"},
        {"package-import", "package p; " + members + " endpackage\n"
            + tb("p::mode_t", "IDLE", " import p::*;"), "p::IDLE", "p::RUN"},
        {"parameterized", "package p; class C #(parameter int W=2);\n"
            " typedef enum logic [W-1:0] {IDLE=1, RUN=2} mode_t;\n endclass endpackage\n"
            + tb("p::C#(2)::mode_t", "p::C#(2)::IDLE"), {}, {}, false},
        {"package-shadow", classPackage + localTb("import p::C;\n class p; class C;\n"
            " typedef enum logic [1:0] {IDLE=2'd3, RUN=2'd0} mode_t; endclass endclass\n",
            "C::mode_t", "C::IDLE"), {}, {}, false}
    };
}
template<class T> T* child(QObject* object, const char* name) {
    auto* p = object->findChild<T*>(QLatin1String(name)); if (!p) qFatal("Missing %s", name); return p;
}
void click(QObject* owner, const char* name) { auto* b = child<QAbstractButton>(owner, name); QTest::mouseClick(b, Qt::LeftButton); }
TbInputAssist scalar(QString expression = QStringLiteral("16'd9")) {
    TbInputAssist input; input.current = true; input.signal = QStringLiteral("divider"); input.module = QStringLiteral("tb");
    input.expression = expression; input.bitWidth = 16;
    input.rule = {QStringLiteral("reciprocal"), QStringLiteral("100000000"), QStringLiteral("1"), QStringLiteral("Hz"), QStringLiteral("Explicit fixture rule"), true, {}};
    return input;
}
struct Host {
    QTemporaryDir data{QDir(qEnvironmentVariable("SIMDOCK_TEST_WORKSPACE_PARENT", QDir::tempPath())).filePath("tb-editor-$-[demo]-XXXXXX")};
    std::unique_ptr<MainWindow> window;
    QString workspace, other;
    Host(const QString& existingTb = {}) {
        workspace = data.filePath("Clock $-[demo]"); other = data.filePath("other"); QDir().mkpath(other);
        if (existingTb.isEmpty()) {
            put(workspace + "/types.sv", packages().toUtf8()); put(workspace + "/controller.sv", dut().toUtf8());
        } else put(workspace + "/existing.sv", existingTb.toUtf8());
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", data.filePath("sessions.ini").toUtf8());
        qputenv("ZEROSLACK_EDITING_TIME_STORAGE_PATH", data.filePath("editing.json").toUtf8());
        window = std::make_unique<MainWindow>();
        window->tabManager->setCrashRecoveryService(std::make_unique<CrashRecoveryService>(data.filePath("recovery")));
        window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window->resize(1380, 900); window->show();
    }
    WorkspaceSessionCoordinator* sessions() { return window->findChild<WorkspaceSessionCoordinator*>(); }
    MyCodeEditor* editor() { return window->tabManager->getCurrentEditor(); }
};
}

class TbEditorAssistanceTest : public QObject {
    Q_OBJECT
    void capture(MainWindow* window, const QString& name) {
        if (report().isEmpty()) return;
        QTest::qWait(80); QDir().mkpath(report());
        QVERIFY(window->grab().save(QDir(report()).filePath(name + ".png")));
        const QJsonObject geometry{{"devicePixelRatio", window->devicePixelRatioF()}, {"width", window->width()}, {"height", window->height()}};
        QVERIFY(put(QDir(report()).filePath(name + ".json"), QJsonDocument(geometry).toJson()));
    }
private slots:
    void numericRules() {
        auto input = scalar(); QString error;
        QCOMPARE(TbInputAssistService::engineeringValue(input, &error), QStringLiteral("10000000")); QVERIFY(error.isEmpty());
        auto p = TbInputAssistService::preview(input, "5000000"); QVERIFY2(p.valid, qPrintable(p.error));
        QCOMPARE(p.expression, QStringLiteral("16'd19")); QVERIFY(!p.quantized); QCOMPARE(p.actual, QStringLiteral("5000000"));
        p = TbInputAssistService::preview(input, "3000000"); QVERIFY(p.valid); QVERIFY(p.quantized); QCOMPARE(p.expression, QStringLiteral("16'd32"));
        for (const auto& bad : {"0", "-2", "nan", "inf", "1e309", "100000001", "1", "system(1)"}) QVERIFY(!TbInputAssistService::preview(input, QLatin1String(bad)).valid);
        for (const auto& bad : {"16'bx", "16'bz", "16'd65536", "BASE+1", "16'(f())"}) { input.expression = QLatin1String(bad); QVERIFY(!TbInputAssistService::preview(input, "5000000").valid); }
        input = scalar("16'h00_09"); p = TbInputAssistService::preview(input, "5000000"); QVERIFY(p.valid); QCOMPARE(p.expression, QStringLiteral("16'h00_13"));
        input = scalar(); input.rule.kind = "linear"; input.rule.scale = "2"; input.rule.offset = "-3";
        p = TbInputAssistService::preview(input, "37"); QVERIFY(p.valid); QCOMPARE(p.expression, QStringLiteral("16'd20"));
        input.signedIntegral = true; input.expression = "-16'sd7";
        p = TbInputAssistService::preview(input, "-23"); QVERIFY2(p.valid, qPrintable(p.error)); QCOMPARE(p.expression, QStringLiteral("-16'sd10"));
        input.rule.scale = "0"; QVERIFY(!TbInputAssistService::preview(input, "3").valid);
        input = scalar(); input.rule.offset = "-9"; QVERIFY(TbInputAssistService::engineeringValue(input, &error).isEmpty()); QVERIFY(error.contains("zero"));
        input = scalar(); input.bitWidth = 64; QVERIFY(!TbInputAssistService::preview(input, "5000000").valid);
        input = scalar(); input.current = false; QVERIFY(!TbInputAssistService::preview(input, "5000000").valid);
        input = scalar(); input.rule.kind = "script"; QVERIFY(!TbInputAssistService::preview(input, "5000000").valid);
        input = scalar("53'd9007199254740991"); input.bitWidth = 53;
        input.rule.kind = "linear"; input.rule.scale = "1"; input.rule.offset = "0";
        QCOMPARE(TbInputAssistService::engineeringValue(input, &error), QStringLiteral("9007199254740991"));
        p = TbInputAssistService::preview(input, "9007199254740991"); QVERIFY(p.valid); QCOMPARE(p.expression, input.expression);
        QVERIFY(!TbInputAssistService::preview(input, "9007199254740992").valid);
    }
    void semanticInitializers() {
        QTemporaryDir tmp; const auto file = tmp.filePath("design.sv");
        const QString tb = QStringLiteral(
            "module tb;\n// 中文 UTF-16 anchor\n drive_pkg::mode_t mode;\n logic clk, rst_n; logic [15:0] divider, observed; wire pin;\n"
            " controller dut(.clk(clk),.rst_n(rst_n),.mode(mode),.divider(divider),.observed(observed),.pin(pin));\n"
            " initial begin\n mode = drive_pkg::IDLE;\n divider = 16'd9;\n clk = '0;\n rst_n = '0;\n #10;\n divider = 12;\n end\nendmodule\n");
        const QString source = packages() + dut() + tb;
        SemanticIndex index; index.setSnapshot(analyze({{file, source}}));
        auto items = TbInputAssistService::analyze(file, source, index);
        if (items.size() != 4) qWarning().noquote() << simdock::syntaxTree(source.toUtf8());
        QCOMPARE(items.size(), 4);
        auto mode = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.signal == "mode"; });
        if (mode != items.end() && !mode->current) for (const auto& record : index.getSymbolRecords(file)) if (record.name == "mode")
            qWarning() << record.owner.name << int(record.declarationKind) << int(record.usageRole) << int(record.collectorKind)
                << record.location.position << record.presentation.defaultInfo.available << record.presentation.defaultInfo.fixedSize
                << record.presentation.defaultInfo.integral << record.presentation.defaultInfo.bitWidth << record.presentation.defaultInfo.enumValues.size();
        QVERIFY(mode != items.end()); QVERIFY2(mode->current, qPrintable(mode->status)); QCOMPARE(mode->enumValues.size(), 3);
        QCOMPARE(mode->enumValues[0].expressionText, QStringLiteral("drive_pkg::IDLE"));
        QCOMPARE(mode->enumValues[0].displayValueText, QStringLiteral("3"));
        QCOMPARE(source.mid(mode->start, mode->length), QStringLiteral("drive_pkg::IDLE"));
        const auto trivia = TbInputAssistService::analyze(file, source + "\n", index);
        QCOMPARE(trivia.size(), 4); for (const auto& i : trivia) QVERIFY(i.current);
        QString typeEdit = source; typeEdit.replace("logic [15:0] divider", "logic [7:0] divider");
        const auto stale = TbInputAssistService::analyze(file, typeEdit, index);
        QVERIFY(!stale.isEmpty()); for (const auto& i : stale) QVERIFY(!i.current);
        QString changed = source; changed.replace("mode = drive_pkg::IDLE", "mode = drive_pkg::RUN");
        // Reuse proven type facts for RHS-only changes without recompiling the workspace.
        items = TbInputAssistService::analyze(file, changed, index);
        QVERIFY(std::any_of(items.begin(), items.end(), [](const auto& i) { return i.signal == "mode" && i.current && i.status.isEmpty() && i.expression == "drive_pkg::RUN"; }));
        QString unknown = source; unknown.replace("mode = drive_pkg::IDLE", "mode = drive_pkg::MISSING");
        items = TbInputAssistService::analyze(file, unknown, index);
        QVERIFY(std::any_of(items.begin(), items.end(), [](const auto& i) { return i.signal == "mode" && !i.status.isEmpty(); }));
        index.setSnapshot(analyze({{file, changed}})); items = TbInputAssistService::analyze(file, changed, index);
        QVERIFY(std::any_of(items.begin(), items.end(), [](const auto& i) { return i.signal == "mode" && i.current && i.status.isEmpty() && i.expression == "drive_pkg::RUN"; }));
        QString otherType = source; otherType.replace("drive_pkg::mode_t", "other_pkg::mode_t"); otherType.replace("mode = drive_pkg::IDLE", "mode = other_pkg::IDLE");
        index.setSnapshot(analyze({{file, otherType}})); items = TbInputAssistService::analyze(file, otherType, index);
        mode = std::find_if(items.begin(), items.end(), [](const auto& i) { return i.signal == "mode"; });
        QVERIFY(mode != items.end()); QCOMPARE(mode->enumValues.size(), 2);
        QCOMPARE(mode->enumValues[0].expressionText, QStringLiteral("other_pkg::IDLE"));
        QCOMPARE(mode->enumValues[0].displayValueText, QStringLiteral("1"));
        QString declaration = source; declaration.replace(" mode = drive_pkg::IDLE;\n", ""); declaration.replace("drive_pkg::mode_t mode;", "drive_pkg::mode_t mode = drive_pkg::IDLE;");
        index.setSnapshot(analyze({{file, declaration}})); items = TbInputAssistService::analyze(file, declaration, index);
        QCOMPARE(items.size(), 4); for (const auto& i : items) QVERIFY(i.current);
        QString duplicate = source; duplicate.replace(" #10;", " divider = 16'd2;\n #10;");
        index.setSnapshot(analyze({{file, duplicate}})); items = TbInputAssistService::analyze(file, duplicate, index);
        QCOMPARE(items.size(), 5);
        for (const auto& i : items) if (i.signal == "divider") { QVERIFY(!i.current); QVERIFY(i.status.contains("Multiple")); }
        QVERIFY(TbInputAssistService::analyze(file, duplicate, index, [] { return true; }).isEmpty());
        auto module = simdock::analyzeSource((packages() + dut()).toUtf8(), "design.sv").modules.first();
        const auto design = packages() + dut(); index.setSnapshot(analyze({{file, design}})); QString error;
        QVERIFY2(simdock::completeTestbenchTypes(module, index, file, design, &error), qPrintable(error));
        const auto generated = simdock::generateTestbench(module, simdock::suggestedTbOptions(module), 1000, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(generated.contains("mode = drive_pkg::IDLE"));
        QVERIFY(generated.contains("W = 16")); QVERIFY(!generated.contains("observed =")); QVERIFY(generated.contains("assign pin = 'z"));
    }
    void semanticEnumScopes_data() {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("idle");
        QTest::addColumn<QString>("run");
        QTest::addColumn<bool>("currentExpressionKnown");
        for (const auto& fixture : enumScopeFixtures()) QTest::newRow(qPrintable(fixture.name))
            << fixture.source << fixture.idle << fixture.run << fixture.currentExpressionKnown;
    }
    void semanticEnumScopes() {
        QFETCH(QString, source); QFETCH(QString, idle); QFETCH(QString, run); QFETCH(bool, currentExpressionKnown);
        QTemporaryDir tmp; const auto file = tmp.filePath("scope.sv");
        SemanticIndex index; index.setSnapshot(analyze({{file, source}}));
        const auto items = TbInputAssistService::analyze(file, source, index);
        QCOMPARE(items.size(), 1); const auto& input = items.first();
        QVERIFY2(input.current, qPrintable(input.status)); QCOMPARE(input.enumValues.size(), 2);
        QCOMPARE(input.enumValues[0].expressionText, idle); QCOMPARE(input.enumValues[1].expressionText, run);
        QVERIFY(input.enumValues[0].available); QCOMPARE(input.enumValues[0].displayValueText, QStringLiteral("1"));
        QVERIFY(input.enumValues[1].available); QCOMPARE(input.enumValues[1].displayValueText, QStringLiteral("2"));
        QCOMPARE(input.status.isEmpty(), currentExpressionKnown);
        QVERIFY(!input.status.contains("Unknown enum"));
        if (idle.isEmpty()) for (const auto& member : input.enumValues) QVERIFY(!member.expressionFailureReason.isEmpty());
        if (!report().isEmpty()) {
            const auto base = QDir(report()).filePath(QStringLiteral("scope-%1").arg(QLatin1String(QTest::currentDataTag())));
            QVERIFY(put(base + ".original.sv", source.toUtf8()));
            if (!run.isEmpty()) {
                auto modified = source; modified.replace(input.start, input.length, run);
                QVERIFY(put(base + ".assisted.sv", modified.toUtf8()));
            }
        }
        // A same-named value in another scope is not the current enum member.
        if (QLatin1String(QTest::currentDataTag()) == "collisions") {
            QString changed = source; changed.replace("mode=p::C::IDLE", "mode=IDLE");
            const auto unverified = TbInputAssistService::analyze(file, changed, index);
            QCOMPARE(unverified.size(), 1); QVERIFY(!unverified.first().status.isEmpty());
            QVERIFY(!unverified.first().enumValues.first().unqualifiedReferenceAvailable);
        }
    }
    void sharedEnumScopes() {
        QTemporaryDir tmp; const auto file = tmp.filePath("structured.sv");
        const QString source = "package p; class C; typedef enum logic [1:0] {IDLE=1, RUN=2} mode_t; endclass\n"
            " typedef struct packed { C::mode_t mode; logic enable; } input_t; endpackage\n"
            " module controller(input p::input_t bus); endmodule\n";
        SemanticIndex index; index.setSnapshot(analyze({{file, source}}));
        bool found = false;
        for (const auto& record : index.getSymbolRecords(file)) {
            if (record.name != "bus" || record.declarationKind != SymbolTaxonomy::DeclarationKind::Port) continue;
            const auto& facts = record.presentation.defaultInfo;
            QVERIFY2(facts.structuredLeavesAvailable, qPrintable(facts.structuredFailureReason));
            QCOMPARE(facts.structuredLeaves.size(), 2);
            for (const auto& leaf : facts.structuredLeaves) if (leaf.relativePath == ".mode") {
                found = true; QCOMPARE(leaf.enumValues.size(), 2);
                QCOMPARE(leaf.enumValues[0].expressionText, QStringLiteral("p::C::IDLE"));
                QCOMPARE(leaf.enumValues[1].expressionText, QStringLiteral("p::C::RUN"));
                QVERIFY(leaf.enumValues[0].available); QCOMPARE(leaf.enumValues[0].displayValueText, QStringLiteral("1"));
            }
        }
        QVERIFY(found);
        const auto classSource = enumScopeFixtures().first().source;
        index.setSnapshot(analyze({{file, classSource}}));
        auto module = simdock::analyzeSource(classSource.toUtf8(), "class.sv").modules.first();
        QString error;
        QVERIFY(!simdock::completeTestbenchTypes(module, index, file, classSource, &error));
        QVERIFY(!error.isEmpty()); // Class-typed new TB generation remains outside the supported contract.
    }
    void enumScopeWindow() {
        QVERIFY(!TbInputAssistService::interactiveEnabled());
        for (const auto& fixture : enumScopeFixtures()) {
            if (fixture.name != "collisions" && fixture.name != "parameterized") continue;
            Host f(fixture.source); QVERIFY(f.data.isValid());
            QVERIFY(f.sessions()->openWorkspace(f.workspace));
            const auto file = f.workspace + "/existing.sv";
            QVERIFY(f.window->tabManager->openFileInTab(file)); auto* editor = f.editor();
            QTRY_COMPARE_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(file), fixture.source, 30000);
            const auto facts = TbInputAssistService::analyze(file, fixture.source, *SemanticIndex::getInstance());
            QCOMPARE(facts.size(), 1); QVERIFY(facts.first().current);
            QCOMPARE(facts.first().enumValues.size(), 2);
            QVERIFY(editor->tbInputAssists().isEmpty());
            editor->viewport()->repaint();
            QVERIFY(editor->tbAssistHitRegions().isEmpty());
            QVERIFY(!editor->findChild<QFrame*>("tbInputAssistPopup"));
            QCOMPARE(editor->toPlainText(), fixture.source);
            QVERIFY(!editor->document()->isModified());
            QCOMPARE(read(file), fixture.source.toUtf8());
            capture(f.window.get(), "paused-enum-" + fixture.name);
            QVERIFY(f.window->close());
        }
    }
    void mainWindowWorkflow() {
        QVERIFY(!TbInputAssistService::interactiveEnabled());
        const auto originalTheme=ApplicationThemeManager::instance().mode();
        const auto restoreTheme=qScopeGuard([&]{ApplicationThemeManager::instance().setMode(originalTheme);});
        Host f; QVERIFY(f.data.isValid()); QVERIFY(f.sessions()->openWorkspace(f.workspace));
        const auto dutFile = f.workspace + "/controller.sv";
        QVERIFY(f.window->tabManager->openFileInTab(dutFile));
        QTRY_COMPARE_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(dutFile), dut(), 30000);
        click(f.window.get(), "editorNewTb");
        QTRY_VERIFY(f.window->findChild<QFrame*>("newTbEditorPopup"));
        auto* creator = child<QFrame>(f.window.get(), "newTbEditorPopup");
        QVERIFY2(child<QPushButton>(creator, "tbCreateAndOpen")->isEnabled(), qPrintable(child<QLabel>(creator, "tbNewStatus")->text()));
        QVERIFY(child<QPlainTextEdit>(creator, "tbNewPreview")->toPlainText().contains("mode = drive_pkg::IDLE"));
        const auto tbFile = f.workspace + "/sim/tb_controller.sv";
        click(creator, "tbCreateCancel"); QVERIFY(!QFileInfo::exists(tbFile));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        click(f.window.get(), "editorNewTb");
        creator=child<QFrame>(f.window.get(), "newTbEditorPopup");
        capture(f.window.get(), "01-create-framework");
        click(creator, "tbCreateAndOpen");
        QTRY_COMPARE(f.editor()->documentFileName(), tbFile);
        auto* editor=f.editor(); const auto generated=editor->toPlainText();
        QVERIFY(put(QDir(report()).filePath("generated.sv"),generated.toUtf8()));
        QTRY_COMPARE_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(tbFile),generated,30000);
        const auto facts=TbInputAssistService::analyze(tbFile,generated,*SemanticIndex::getInstance());
        const auto mode=std::find_if(facts.cbegin(),facts.cend(),[](const auto& input){return input.signal=="mode";});
        QVERIFY(mode!=facts.cend()); QVERIFY(mode->current); QCOMPARE(mode->enumValues.size(),3);
        QVERIFY(editor->tbInputAssists().isEmpty());
        QVERIFY(!editor->findChild<QFrame*>("tbInputAssistPopup"));
        QTextCursor transaction(editor->document()); transaction.beginEditBlock();
        auto value=editor->document()->find("mode = drive_pkg::IDLE"); QVERIFY(!value.isNull());
        value.insertText("mode = drive_pkg::RUN");
        value=editor->document()->find("divider = '0"); QVERIFY(!value.isNull());
        value.insertText("divider = 16'd32"); transaction.endEditBlock();
        const auto edited=editor->toPlainText();
        QVERIFY(edited.contains("mode = drive_pkg::RUN")); QVERIFY(edited.contains("divider = 16'd32"));
        QVERIFY(editor->document()->isModified());
        editor->undo(); QCOMPARE(editor->toPlainText(),generated);
        editor->redo(); QCOMPARE(editor->toPlainText(),edited);
        QVERIFY(f.window->tabManager->saveCurrentTab()); QCOMPARE(read(tbFile),edited.toUtf8());
        QVERIFY(put(QDir(report()).filePath("edited.sv"),edited.toUtf8()));
        QVERIFY(f.window->tabManager->openFileInTab(dutFile)); click(f.window.get(), "editorNewTb");
        creator=child<QFrame>(f.window.get(), "newTbEditorPopup");
        QVERIFY(!child<QPushButton>(creator, "tbCreateAndOpen")->isEnabled());
        click(creator, "tbCreateCancel"); QCOMPARE(read(tbFile),edited.toUtf8());
        QVERIFY(f.window->tabManager->closeAllTabs());
        QVERIFY(f.window->tabManager->openFileInTab(tbFile)); editor=f.editor();
        QCOMPARE(editor->toPlainText(),edited);
        QTRY_COMPARE_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(tbFile),edited,30000);
        QVERIFY(editor->tbInputAssists().isEmpty()); QVERIFY(editor->tbAssistHitRegions().isEmpty());
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        f.window->resize(860,780); capture(f.window.get(),"02-manual-edit-reopened");
        QCOMPARE(editor->toPlainText(),edited);
        QVERIFY(f.sessions()->openWorkspace(f.other));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(!f.window->findChild<QFrame*>("tbInputAssistPopup"));
        QVERIFY(f.window->close());
    }
    void liveQuestaGeneratedAndEdited() {
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Requires the explicitly configured real Questa executable.");
        QVERIFY(!report().isEmpty());
        const auto generated = read(QDir(report()).filePath("generated.sv"));
        const auto edited = read(QDir(report()).filePath("edited.sv"));
        QVERIFY(!generated.isEmpty()); QVERIFY(!edited.isEmpty());
        QTemporaryDir data(QDir(qEnvironmentVariable("SIMDOCK_TEST_WORKSPACE_PARENT", QDir::tempPath())).filePath("tb-questa-$-[demo]-XXXXXX"));
        QVERIFY(data.isValid()); const auto root = data.path();
        QVERIFY(put(root + "/types.sv", packages().toUtf8())); QVERIFY(put(root + "/controller.sv", dut().toUtf8()));
        auto project = simdock::newProject("Editor $-[demo]");
        project.sources = {"controller.sv", "types.sv"};
        project.dutFile = "controller.sv"; project.dutName = "controller";
        const auto dependencies = simdock::planBuildInputs(root, project);
        QVERIFY2(dependencies.order.valid(), qPrintable(dependencies.error()));
        dependencies.apply(project, root);
        QCOMPARE(project.sources, QStringList({"types.sv", "controller.sv"}));
        QCOMPARE(project.compilationUnits, project.sources);
        QString error; QVERIFY2(simdock::saveProject(root, project, &error), qPrintable(error));
        project.tbName = "tb_controller"; project.tbFile = simdock::projectTbPath(project, project.tbName); project.durationNs = 500;
        simdock::QuestaSession session; QString log;
        connect(&session, &simdock::QuestaSession::logText, this, [&](const QString& chunk) { log += chunk; });
        QSignalSpy finished(&session, &simdock::QuestaSession::finished);
        const auto cleanup = qScopeGuard([&] { session.stop(); });
        QString previousRun; qint64 pid = 0;
        for (const auto& phase : {QStringLiteral("generated"), QStringLiteral("edited"), QStringLiteral("repeat")}) {
            QVERIFY(put(QDir(root).filePath(project.tbFile), phase == "generated" ? generated : edited));
            auto checkedDut = QStringLiteral("`timescale 1ns/1ps\n") + dut();
            checkedDut.replace(" assign observed", QStringLiteral(
                " initial begin #1; if (mode !== drive_pkg::%1 || divider !== 16'd%2) $fatal(1, \"Input verification failed\");\n"
                " $display(\"TB_ASSIST_INPUTS_PASS\"); end\n assign observed")
                .arg(phase == "generated" ? QStringLiteral("IDLE") : QStringLiteral("RUN"))
                .arg(phase == "generated" ? 0 : 32));
            QVERIFY(put(root + "/controller.sv", checkedDut.toUtf8()));
            const auto prepared = simdock::prepareRun(executable, root, project);
            QVERIFY2(prepared.error.isEmpty(), qPrintable(prepared.error));
            QVERIFY2(session.start(prepared, &error), qPrintable(error));
            QTRY_VERIFY2_WITH_TIMEOUT(!finished.isEmpty(), qPrintable(log), 130000);
            QVERIFY2(finished.takeFirst()[0].toBool(), qPrintable(log));
            if (pid) QCOMPARE(session.processId(), pid); else pid = session.processId();
            const auto path = QDir(report()).filePath("questa-" + phase);
            QVERIFY(put(path + "/session.log", log.toUtf8()));
            QVERIFY(put(path + "/run.do", read(session.lastRunDirectory() + "/run.do")));
            QVERIFY2(log.contains("TB_ASSIST_INPUTS_PASS"), qPrintable(log));
            // Questa 2024.1's source viewer can report vsim-21 after $finish
            // on literal '$' paths; compiler/run success and HDL assertions
            // are verified independently, and the full transcript is retained.
            if (!previousRun.isEmpty()) QTRY_VERIFY2_WITH_TIMEOUT(!QFileInfo::exists(previousRun), qPrintable(log), 5000);
            previousRun = session.lastRunDirectory();
            log.clear();
        }
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.alive(), 15000);
        QVERIFY(!QFileInfo::exists(previousRun));
    }
};

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true); QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("ZeroSlackTbEditorTest"); QCoreApplication::setApplicationName("ZeroSlackTbEditorTest");
    QTemporaryDir profile; QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH", profile.filePath("legacy.ini").toUtf8());
    UserTemplateService::getInstance()->setGlobalTemplateFilePath(profile.filePath("templates.json"));
    if (!initializeUiStyleForTest()) return 2;
    ApplicationThemeManager::instance().setAnimationsEnabled(false); ApplicationThemeManager::instance().applyToApplication();
    TbEditorAssistanceTest test; return QTest::qExec(&test, argc, argv);
}
#include "tb_editor_assistance_test.moc"
