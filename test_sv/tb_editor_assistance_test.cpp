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
int inputIndex(MyCodeEditor* editor, const QString& name) {
    auto items = editor->tbInputAssists();
    for (int i = 0; i < items.size(); ++i) if (items[i].signal == name && items[i].current) return i;
    return -1;
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
    bool openAssist(MyCodeEditor* editor, const QString& name) {
        if (!QTest::qWaitFor([&] { return inputIndex(editor, name) >= 0; }, 15000)) {
            auto* index = SemanticIndex::getInstance();
            qWarning() << "Missing input" << name << "editor revision" << editor->semanticDocumentRevision()
                << "cached semantic text matches" << (index->getCachedFileContent(editor->documentFileName()) == editor->toPlainText())
                << "published inputs" << editor->tbInputAssists().size();
            for (const auto& i : editor->tbInputAssists()) qWarning() << "Published input" << i.signal << i.current << i.status;
            SemanticIndex local; local.setSnapshot(index->snapshot());
            EffectiveValueService localValues(&local, EffectiveValueService::getInstance()->snapshotForDocument(editor->documentFileName()));
            for (const auto& i : TbInputAssistService::analyze(editor->documentFileName(), editor->toPlainText(), local, {}, &localValues, editor->semanticDocumentRevision()))
                qWarning() << "Captured input" << i.signal << i.current << i.status;
            for (const auto& i : TbInputAssistService::analyze(editor->documentFileName(), editor->toPlainText(), *index))
                qWarning() << "Direct input" << i.signal << i.current << i.status << i.bitWidth << i.enumValues.size();
            for (const auto& r : index->getSymbolRecords(editor->documentFileName())) if (r.name == name) {
                EffectiveValueQuery q; q.symbol = r; q.documentText = editor->toPlainText(); q.documentRevision = editor->semanticDocumentRevision();
                const auto value = EffectiveValueService::getInstance()->resolve(q);
                qWarning() << "Value status" << int(value.status) << value.failureReason << value.requestedDocumentRevision << value.computedDocumentRevision << value.computationRevision;
            }
            if (!report().isEmpty()) editor->window()->grab().save(QDir(report()).filePath("missing-inlay.png"));
            return false;
        }
        const auto i = inputIndex(editor, name);
        auto input = editor->tbInputAssists()[i]; QTextCursor c(editor->document()); c.setPosition(input.start);
        editor->setTextCursor(c); editor->centerCursor(); editor->viewport()->repaint();
        if (!QTest::qWaitFor([&] { return editor->tbAssistHitRegions().contains(QStringLiteral("tb-input:%1").arg(i)); }, 5000)) {
            qWarning() << "Missing painted input" << name << editor->viewport()->size(); return false;
        }
        QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier, editor->tbAssistHitRegions().value(QStringLiteral("tb-input:%1").arg(i)).center());
        const bool opened = QTest::qWaitFor([&] { return editor->findChild<QFrame*>("tbInputAssistPopup"); }, 3000);
        if (!opened) qWarning() << "Input click missed" << name << editor->tbAssistHitRegions();
        return opened;
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
        QVERIFY(generated.contains("W = drive_pkg::WIDTH")); QVERIFY(!generated.contains("observed =")); QVERIFY(generated.contains("assign pin = 'z"));
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
        QVERIFY2(simdock::completeTestbenchTypes(module, index, file, classSource, &error), qPrintable(error));
        simdock::generateTestbench(module, simdock::suggestedTbOptions(module), 1000, &error);
        QVERIFY(!error.isEmpty()); // Class-typed new TB generation remains outside the supported contract.
    }
    void enumScopeWindow() {
        {
            const auto source = enumScopeFixtures().at(1).source;
            Host f(source); QVERIFY(f.data.isValid()); QVERIFY(f.sessions()->openWorkspace(f.workspace));
            const auto file = f.workspace + "/existing.sv";
            QVERIFY(f.window->tabManager->openFileInTab(file)); auto* editor = f.editor();
            QVERIFY(openAssist(editor, "mode")); auto* popup = child<QFrame>(editor, "tbInputAssistPopup");
            QVERIFY(editor->tbInputAssists()[inputIndex(editor, "mode")].status.isEmpty());
            auto* button = child<QPushButton>(popup, "tbEnum.RUN");
            QVERIFY(button->isEnabled()); QVERIFY(button->text().contains("p::C::RUN"));
            capture(f.window.get(), "enum-class-choices"); click(popup, "tbEnum.RUN");
            auto expected = source; expected.replace("mode=p::C::IDLE", "mode=p::C::RUN");
            QCOMPARE(editor->toPlainText(), expected); QVERIFY(editor->document()->isModified());
            editor->undo(); QCOMPARE(editor->toPlainText(), source);
            editor->redo(); QCOMPARE(editor->toPlainText(), expected);
            QVERIFY(openAssist(editor, "mode")); popup = child<QFrame>(editor, "tbInputAssistPopup");
            QPointer<QPushButton> staleButton = child<QPushButton>(popup, "tbEnum.IDLE");
            QTextCursor cursor(editor->document()); cursor.setPosition(0); cursor.insertText("// later edit\n");
            QVERIFY(!popup->isVisible()); const auto edited = editor->toPlainText();
            if (staleButton) staleButton->click(); QCOMPARE(editor->toPlainText(), edited);
            editor->undo(); QCOMPARE(editor->toPlainText(), expected);
            QVERIFY(f.window->tabManager->saveCurrentTab()); QCOMPARE(read(file), expected.toUtf8());
            QVERIFY(put(QDir(report()).filePath("editor-class.original.sv"), source.toUtf8()));
            QVERIFY(put(QDir(report()).filePath("editor-class.assisted.sv"), read(file)));
            capture(f.window.get(), "enum-class-applied"); QVERIFY(f.window->close());
        }
        {
            const auto fixtures = enumScopeFixtures();
            const auto fixture = std::find_if(fixtures.cbegin(), fixtures.cend(), [](const auto& f) { return f.name == "parameterized"; });
            QVERIFY(fixture != fixtures.cend()); Host f(fixture->source);
            QVERIFY(f.sessions()->openWorkspace(f.workspace));
            QVERIFY(f.window->tabManager->openFileInTab(f.workspace + "/existing.sv")); auto* editor = f.editor();
            QVERIFY(openAssist(editor, "mode")); auto* popup = child<QFrame>(editor, "tbInputAssistPopup");
            auto* button = child<QPushButton>(popup, "tbEnum.RUN");
            QVERIFY(!button->isEnabled()); QVERIFY(button->toolTip().contains("Parameterized class"));
            button->click(); QCOMPARE(editor->toPlainText(), fixture->source); QVERIFY(!editor->document()->isModified());
            capture(f.window.get(), "enum-unsupported"); QVERIFY(f.window->close());
        }
    }
    void mainWindowWorkflow() {
        auto* originalClipboard = new QMimeData;
        const auto* clipboard = QApplication::clipboard()->mimeData();
        if (clipboard) for (const auto& format : clipboard->formats()) originalClipboard->setData(format, clipboard->data(format));
        const auto restoreClipboard = qScopeGuard([&] { QApplication::clipboard()->setMimeData(originalClipboard); });
        Host f; QVERIFY(f.data.isValid()); QVERIFY(f.sessions()->openWorkspace(f.workspace));
        const auto dutFile = f.workspace + "/controller.sv";
        QVERIFY(f.window->tabManager->openFileInTab(dutFile));
        QTRY_VERIFY_WITH_TIMEOUT(SemanticIndex::getInstance()->getCachedFileContent(dutFile) == dut(), 30000);
        click(f.window.get(), "editorNewTb");
        QTRY_VERIFY(f.window->findChild<QFrame*>("newTbEditorPopup"));
        auto* creator = child<QFrame>(f.window.get(), "newTbEditorPopup");
        QVERIFY2(child<QPushButton>(creator, "tbCreateAndOpen")->isEnabled(), qPrintable(child<QLabel>(creator, "tbNewStatus")->text()));
        QVERIFY(child<QPlainTextEdit>(creator, "tbNewPreview")->toPlainText().contains("mode = drive_pkg::IDLE"));
        capture(f.window.get(), "01-create-framework");
        click(creator, "tbCreateAndOpen");
        const auto tbFile = f.workspace + "/sim/tb_controller.sv";
        QTRY_COMPARE(f.editor()->documentFileName(), tbFile);
        auto* editor = f.editor(); const auto generated = editor->toPlainText();
        QVERIFY(put(QDir(report()).filePath("generated.sv"), generated.toUtf8()));
        QVERIFY(openAssist(editor, "mode")); capture(f.window.get(), "02-enum-choice");
        click(child<QFrame>(editor, "tbInputAssistPopup"), "tbEnum.RUN");
        const auto selected = editor->toPlainText(); auto expected = generated; expected.replace("mode = drive_pkg::IDLE", "mode = drive_pkg::RUN");
        QCOMPARE(selected, expected); QVERIFY(editor->document()->isModified());
        editor->undo(); QCOMPARE(editor->toPlainText(), generated); editor->redo(); QCOMPARE(editor->toPlainText(), selected);
        QVERIFY(openAssist(editor, "divider"));
        auto* popup = child<QFrame>(editor, "tbInputAssistPopup");
        child<QLineEdit>(popup, "tbRuleScale")->setText("100000000");
        child<QLineEdit>(popup, "tbRuleOffset")->setText("1");
        child<QLineEdit>(popup, "tbRuleUnit")->setText("Hz");
        child<QLineEdit>(popup, "tbRuleSource")->setText("Acceptance fixture: reference clock 100 MHz");
        capture(f.window.get(), "03-explicit-binding");
        click(popup, "tbRuleApply");
        QVERIFY(editor->toPlainText().contains("@zeroslack-engineering"));
        const auto bound = editor->toPlainText();
        QVERIFY(openAssist(editor, "divider")); popup = child<QFrame>(editor, "tbInputAssistPopup");
        child<QLineEdit>(popup, "tbEngineeringTarget")->setText("5000000");
        QVERIFY(child<QLabel>(popup, "tbEngineeringPreview")->text().contains("16'd19"));
        capture(f.window.get(), "04-exact-preview"); click(popup, "tbRuleApply");
        QVERIFY(editor->toPlainText().contains("divider = 16'd19"));
        const auto exact = editor->toPlainText();
        QVERIFY(openAssist(editor, "divider")); popup = child<QFrame>(editor, "tbInputAssistPopup");
        child<QLineEdit>(popup, "tbEngineeringTarget")->setText("3000000");
        QVERIFY(child<QLabel>(popup, "tbEngineeringPreview")->text().contains("Quantized"));
        capture(f.window.get(), "05-quantized-preview"); click(popup, "tbRuleApply");
        const auto quantized = editor->toPlainText(); QVERIFY(quantized.contains("divider = 16'd32"));
        editor->undo(); QCOMPARE(editor->toPlainText(), exact);
        editor->undo(); QCOMPARE(editor->toPlainText(), bound);
        editor->undo(); QCOMPARE(editor->toPlainText(), selected);
        editor->redo(); editor->redo(); editor->redo(); QCOMPARE(editor->toPlainText(), quantized);
        QVERIFY(openAssist(editor, "divider")); popup = child<QFrame>(editor, "tbInputAssistPopup");
        child<QLineEdit>(popup, "tbEngineeringTarget")->setText("0");
        QVERIFY(!child<QPushButton>(popup, "tbRuleApply")->isEnabled()); capture(f.window.get(), "06-invalid-input");
        child<QLineEdit>(popup, "tbEngineeringTarget")->setText("5000000");
        QVERIFY(child<QPushButton>(popup, "tbRuleApply")->isEnabled());
        // Foreign edits invalidate an already visible popup, including its old controls.
        QPointer<QPushButton> oldApply = child<QPushButton>(popup, "tbRuleApply");
        QTextCursor c(editor->document()); c.setPosition(0); c.insertText("// user edit\n");
        QVERIFY(!popup->isVisible()); const auto edited = editor->toPlainText();
        if (oldApply) oldApply->click(); QCOMPARE(editor->toPlainText(), edited);
        editor->undo(); QCOMPARE(editor->toPlainText(), quantized);
        QVERIFY(f.window->tabManager->saveCurrentTab()); QVERIFY(!editor->document()->isModified());
        QVERIFY(put(QDir(report()).filePath("assisted.sv"), editor->toPlainText().toUtf8()));
        capture(f.window.get(), "07-source-with-inlays");
        editor->selectAll(); editor->copy(); QCOMPARE(QApplication::clipboard()->text(), quantized);
        // A changed package in another open document invalidates an old apply control.
        const auto typesFile = f.workspace + "/types.sv";
        QVERIFY(f.window->tabManager->openFileInTab(typesFile)); auto* typesEditor = f.editor();
        QVERIFY(f.window->tabManager->openFileInTab(tbFile)); QVERIFY(openAssist(editor, "divider"));
        popup = child<QFrame>(editor, "tbInputAssistPopup");
        child<QLineEdit>(popup, "tbEngineeringTarget")->setText("5000000");
        oldApply = child<QPushButton>(popup, "tbRuleApply");
        c = QTextCursor(typesEditor->document()); c.setPosition(typesEditor->toPlainText().indexOf("WIDTH=16") + 6);
        c.setPosition(c.position() + 2, QTextCursor::KeepAnchor); c.insertText("8");
        QVERIFY(!editor->tbAssistDependenciesCurrent()); if (oldApply) oldApply->click(); QCOMPARE(editor->toPlainText(), quantized);
        typesEditor->undo(); QVERIFY(editor->tbAssistDependenciesCurrent());
        // Existing file creation cannot overwrite saved or unsaved documents.
        QVERIFY(f.window->tabManager->openFileInTab(dutFile)); click(f.window.get(), "editorNewTb");
        creator = child<QFrame>(f.window.get(), "newTbEditorPopup");
        QVERIFY(!child<QPushButton>(creator, "tbCreateAndOpen")->isEnabled());
        QCOMPARE(read(tbFile), quantized.toUtf8()); click(creator, "tbCreateCancel");
        QVERIFY(f.window->tabManager->openFileInTab(tbFile));
        QVERIFY(openAssist(editor, "mode"));
        QVERIFY(f.window->tabManager->openFileInTab(dutFile));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!editor->findChild<QFrame*>("tbInputAssistPopup"));
        QVERIFY(f.window->tabManager->openFileInTab(tbFile));
        // Hand-written literals remain the authority and update the derived value.
        const int at = editor->toPlainText().indexOf("16'd32"); QVERIFY(at >= 0);
        c = QTextCursor(editor->document()); c.setPosition(at); c.setPosition(at + 6, QTextCursor::KeepAnchor); c.insertText("16'h0009");
        QVERIFY(openAssist(editor, "divider")); popup = child<QFrame>(editor, "tbInputAssistPopup");
        QCOMPARE(child<QLineEdit>(popup, "tbEngineeringTarget")->text(), QStringLiteral("10000000"));
        ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
        QVERIFY(openAssist(editor, "divider"));
        capture(f.window.get(), "08-dark-formula");
        f.window->resize(860, 780); QVERIFY(openAssist(editor, "divider")); capture(f.window.get(), "09-narrow-dark");
        // Close/reopen restores rule metadata and derives the value from disk.
        QVERIFY(f.window->tabManager->saveCurrentTab());
        const auto saved = editor->toPlainText();
        QVERIFY(f.window->tabManager->closeAllTabs());
        QVERIFY(f.window->tabManager->openFileInTab(tbFile)); editor = f.editor();
        QCOMPARE(editor->toPlainText(), saved); QVERIFY(openAssist(editor, "divider"));
        QCOMPARE(child<QLineEdit>(child<QFrame>(editor, "tbInputAssistPopup"), "tbEngineeringTarget")->text(), QStringLiteral("10000000"));
        QVERIFY(f.sessions()->openWorkspace(f.other));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!f.window->findChild<QFrame*>("tbInputAssistPopup"));
        QVERIFY(f.window->close());
    }
    void liveQuestaGeneratedAndAssisted() {
        const auto executable = qEnvironmentVariable("SIMDOCK_TEST_QUESTA");
        if (executable.isEmpty()) QSKIP("Requires the explicitly configured real Questa executable.");
        QVERIFY(!report().isEmpty());
        const auto generated = read(QDir(report()).filePath("generated.sv"));
        const auto assisted = read(QDir(report()).filePath("assisted.sv"));
        QVERIFY(!generated.isEmpty()); QVERIFY(!assisted.isEmpty());
        QTemporaryDir data(QDir(qEnvironmentVariable("SIMDOCK_TEST_WORKSPACE_PARENT", QDir::tempPath())).filePath("tb-questa-$-[demo]-XXXXXX"));
        QVERIFY(data.isValid()); const auto root = data.path();
        QVERIFY(put(root + "/types.sv", packages().toUtf8())); QVERIFY(put(root + "/controller.sv", dut().toUtf8()));
        auto project = simdock::newProject("Editor $-[demo]");
        const auto dependencies = simdock::selectDependencies(simdock::scanWorkspace(root), "controller.sv", {});
        QVERIFY2(dependencies.messages.isEmpty(), qPrintable(dependencies.messages.join('\n')));
        QCOMPARE(dependencies.sources, QStringList({"types.sv", "controller.sv"}));
        project.sources = dependencies.sources; project.dutFile = "controller.sv"; project.dutName = "controller";
        QString error; QVERIFY2(simdock::saveProject(root, project, &error), qPrintable(error));
        project.tbName = "tb_controller"; project.tbFile = simdock::projectTbPath(project, project.tbName); project.durationNs = 500;
        simdock::QuestaSession session; QString log;
        connect(&session, &simdock::QuestaSession::logText, this, [&](const QString& chunk) { log += chunk; });
        QSignalSpy finished(&session, &simdock::QuestaSession::finished);
        const auto cleanup = qScopeGuard([&] { session.stop(); });
        QString previousRun; qint64 pid = 0;
        for (const auto& phase : {QStringLiteral("generated"), QStringLiteral("assisted"), QStringLiteral("repeat")}) {
            QVERIFY(put(QDir(root).filePath(project.tbFile), phase == "generated" ? generated : assisted));
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
