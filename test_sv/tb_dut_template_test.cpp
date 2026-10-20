#include "applicationthememanager.h"
#include "duttemplateplanner.h"
#include "editorinsertpaletteservice.h"
#include "mycodeeditor.h"
#include "testuistyle.h"
#include "usertemplateservice.h"
#include "slangmanager.h"
#include "templatecapabilities.h"
#include "duttemplatesession.h"
#include "editorfileidentity.h"
#include "workspacesessionstateservice.h"
#include "formatterservice.h"
#include "formattercursoranchor.h"
#include "shareddocument.h"
#include "annotationlayer.h"
#include <QtTest>
#include <QtWidgets>
#define private public
#include "mainwindow.h"
#include "globalcontrolcoordinator.h"
#include "globalcontrolpanel.h"
#undef private
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "simulationcommandservice.h"
#include "../src/integrations/simdock/simdockcontextview.h"
#include "../src/simulation/simdock/ui/workbench.h"

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
    void formatKeepsLiveDutCandidates_data() {
        QTest::addColumn<QString>("token");
        QTest::addColumn<bool>("numeric");
        for (const auto& token : {"clk_init", "reset"})
            for (bool numeric : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(token, numeric ? "numeric" : "signal")))
                    << QString(token) << numeric;
    }
    void formatKeepsLiveDutCandidates() {
        QFETCH(QString, token); QFETCH(bool, numeric);
        QTemporaryDir dir; const auto path = dir.filePath("tb.sv"); put(path, {});
        auto source = timingDut;
        source.replace("input logic a, b,", "input logic a, b, c, d,");
        source.replace("output logic [W-1:0] data_out", "output logic [W-1:0] data_out, other_out");
        source.replace("endmodule", "always_ff @(posedge c or negedge d)\n"
            "  if (!d) other_out <= '0; else other_out <= data_in;\nendmodule");
        put(dir.filePath("dut.sv"), source);
        auto* catalog = UserTemplateService::getInstance(); const auto original = catalog->globalTemplateLocation();
        const auto restore = qScopeGuard([&] { catalog->setGlobalTemplateFilePath(original); });
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        catalog->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] { for (auto* view : window.tabManager->openEditors()) view->document()->setModified(false); });
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        QTest::keyPress(editor, Qt::Key_F24); QTest::keyClick(editor, Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
        keys("tb"); enter(); QTRY_COMPARE(editor->templateModel()->instances().size(), 1);
        keys("tb_format");
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_instances"), Qt::LeftButton);
        keys("@dut"); enter();
        QPointer<QFrame> dutPanel;
        QTRY_VERIFY_WITH_TIMEOUT((dutPanel = window.findChild<QFrame*>("dutTemplateInputs"))
            && dutPanel->accessibleName().contains("DUT module"), 15000);
        keys("core"); enter(); QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 3, 15000);
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_behaviors"), Qt::LeftButton);
        keys("@" + token); enter(); QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 4, 15000);
        const auto child = editor->selectedTemplateId();
        const QString signalName = token == "clk_init" ? "clk_name" : "reset_name";
        const QString numberName = token == "clk_init" ? "freq_mhz" : "time_ns";
        const QStringList expected = token == "clk_init" ? QStringList{"u_core_a", "u_core_c"} : QStringList{"u_core_b", "u_core_d"};
        const auto fresh = [&] {
            const auto snapshot = SemanticIndex::getInstance()->snapshot();
            return snapshot && snapshot->getCachedFileContent(path) == editor->toPlainText();
        };
        const auto candidates = [&] {
            const auto* item = editor->templateModel()->instance(child);
            const auto slot = region(*item, signalName);
            QHash<QString, QString> bindings;
            for (const auto& input : item->inputs)
                bindings[input.name] = editor->toPlainText().mid(input.start, input.length);
            return TemplateInputService::candidates(slot.input, *slot.inputContext, bindings);
        };
        QJsonArray trace; MyCodeEditor* secondary = nullptr;
        const auto record = [&](const QString& phase) {
            const auto slot = region(*editor->templateModel()->instance(child), signalName);
            const auto choices = candidates();
            trace << QJsonObject{{"phase",phase},{"module",slot.inputContext->location.moduleName},
                {"line",slot.inputContext->location.cursorLine},{"position",slot.inputContext->location.cursorPosition},
                {"cursor",editor->textCursor().position()},{"fresh",fresh()},
                {"editing",editor->templateInputEditing()},{"otherEditing",secondary && secondary->templateInputEditing()},
                {"undoSteps",editor->document()->availableUndoSteps()},
                {"values",QJsonArray::fromStringList(choices.values)},{"reason",choices.reason}};
            evidence(QString("live-format-%1.json").arg(QTest::currentDataTag()), QJsonDocument(trace).toJson());
        };
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("before");
        QCOMPARE(candidates().values, expected);
        QTest::keyClick(editor, Qt::Key_Down);
        QTest::keyClick(editor, Qt::Key_Tab); QTest::keyClicks(editor, token == "clk_init" ? "156.25" : "12.5");
        if (!numeric) QTest::keyClick(editor, Qt::Key_Backtab);
        auto* shared = qobject_cast<SharedDocument*>(editor->document()->parent()); QVERIFY(shared);
        MyCodeEditor other;
        secondary = &other;
        shared->attachView(&other); other.setTemplateAuthoringLocked(true);
        auto detach = qScopeGuard([&] { shared->detachView(&other); });
        QVERIFY(other.editTemplateInstance(child, numeric ? signalName : numberName));
        const auto oldText = editor->toPlainText();
        const auto oldMetadata = editor->templateModel()->serialize(path);
        const auto selection = editor->textCursor().selectedText(), otherSelection = other.textCursor().selectedText();
        const auto report = editor->formatDocument();
        QVERIFY2(report.accepted(), qPrintable(report.diagnostic)); QVERIFY(report.changed);
        const auto formatted = editor->toPlainText();
        const auto formattedMetadata = editor->templateModel()->serialize(path);
        QVERIFY(editor->templateAuthoringLocked() && editor->templateInputEditing());
        QVERIFY(other.templateAuthoringLocked() && other.templateInputEditing());
        QCOMPARE(editor->selectedTemplateId(), child); QCOMPARE(other.selectedTemplateId(), child);
        QCOMPARE(editor->templateSlotModeActiveIndex(), numeric ? 1 : 0);
        QCOMPARE(other.templateSlotModeActiveIndex(), numeric ? 0 : 1);
        QCOMPARE(editor->textCursor().selectedText(), selection); QCOMPARE(other.textCursor().selectedText(), otherSelection);
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("formatted-current-analysis");
        QCOMPARE(candidates().values, expected);
        // Each view retains its own slot controller as well as the shared model context.
        if (numeric) QTest::keyClick(editor, Qt::Key_Backtab);
        const auto selectedSignal = editor->textCursor().selectedText();
        QTest::keyClick(editor, Qt::Key_Down);
        QVERIFY(editor->textCursor().selectedText() != selectedSignal);
        QVERIFY(expected.contains(editor->textCursor().selectedText()));
        record("candidate-selected");
        QCOMPARE(candidates().values, expected);
        QTest::keyClick(editor, Qt::Key_Up);
        QCOMPARE(editor->toPlainText(), formatted);
        // Remove the two candidate-selection edits before undoing the formatter.
        editor->undo(); record("undo-selection-1"); editor->undo(); record("undo-selection-2");
        other.undo(); record("undo-format"); QCOMPARE(editor->toPlainText(), oldText);
        QCOMPARE(editor->templateModel()->serialize(path), oldMetadata);
        QVERIFY(editor->templateInputEditing() && other.templateInputEditing());
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("undo-current-analysis"); QCOMPARE(candidates().values, expected);
        editor->redo(); QCOMPARE(editor->toPlainText(), formatted);
        QCOMPARE(editor->templateModel()->serialize(path), formattedMetadata);
        QVERIFY(editor->templateInputEditing() && other.templateInputEditing());
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("redo-current-analysis"); QCOMPARE(candidates().values, expected);
        QVERIFY(!editor->formatDocument().changed);
        QCOMPARE(candidates().values, expected);
        auto* signalView = numeric ? &other : editor;
        const auto otherSignal = signalView->textCursor().selectedText();
        QTest::keyClick(signalView, Qt::Key_Down);
        QVERIFY(signalView->textCursor().selectedText() != otherSignal);
        auto* numberView = numeric ? editor : &other;
        QTest::keyClick(numberView, Qt::Key_Backtab); QTest::keyClick(numberView, Qt::Key_Tab);
        QTest::keyClicks(numberView, token == "clk_init" ? "125.5" : "20.5");
        const auto number = region(*editor->templateModel()->instance(child), numberName);
        QCOMPARE(editor->toPlainText().mid(number.start, number.length).trimmed(), token == "clk_init" ? "125.5" : "20.5");
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("changed-inputs"); QCOMPARE(candidates().values, expected);
        shared->detachView(&other); detach.dismiss();
        const auto savedText = editor->toPlainText();
        QVERIFY(window.tabManager->closeTabsInWorkspace(dir.path()));
        QVERIFY(window.tabManager->openFileInTab(path)); editor = window.tabManager->getCurrentEditor();
        QCOMPARE(editor->toPlainText(), savedText);
        editor->setTemplateAuthoringLocked(true);
        QVERIFY(editor->editTemplateInstance(child, signalName));
        QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("reopened"); QCOMPARE(candidates().values, expected);
        QTest::keyClick(editor, Qt::Key_Down);
        QVERIFY(expected.contains(editor->textCursor().selectedText()));
        // A pending edit in a real DUT must invalidate proof until fresh analysis.
        QVERIFY(window.tabManager->saveCurrentTab());
        QVERIFY(window.tabManager->openFileInTab(dir.filePath("dut.sv")));
        const auto changedDut = source.replace("posedge a", "posedge b").replace("negedge b", "negedge a")
            .replace("if (!b)", "if (!a)").replace("posedge c", "posedge d").replace("negedge d", "negedge c").replace("if (!d)", "if (!c)");
        window.tabManager->getCurrentEditor()->setPlainText(QString::fromUtf8(changedDut));
        record("dut-source-changed");
        QVERIFY(candidates().values.isEmpty());
        const auto retained = editor->toPlainText();
        QTest::keyClick(editor, Qt::Key_Down); QCOMPARE(editor->toPlainText(), retained);
        QVERIFY(window.tabManager->saveCurrentTab());
        QTRY_VERIFY_WITH_TIMEOUT(fresh() && SemanticIndex::getInstance()->snapshot()->getCachedFileContent(dir.filePath("dut.sv"))
            == QString::fromUtf8(changedDut), 15000);
        record("dut-reanalyzed");
        QCOMPARE(candidates().values, token == "clk_init" ? QStringList({"u_core_b","u_core_d"}) : QStringList({"u_core_a","u_core_c"}));
        QVERIFY(!editor->templateModel()->validationIssue(child).isEmpty());
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->templateInputEditing()); QVERIFY(editor->templateAuthoringLocked());
    }
    void incompleteInputNavigation_data() {
        QTest::addColumn<QString>("token");
        QTest::newRow("clock-invalid-value") << QString("clk_init");
        QTest::newRow("reset-invalid-value") << QString("reset");
    }
    void incompleteInputNavigation() {
        QFETCH(QString, token);
        QTemporaryDir dir; const auto path = dir.filePath("tb.sv"); put(path, {});
        put(dir.filePath("dut.sv"), timingDut);
        auto* catalog = UserTemplateService::getInstance(); const auto original = catalog->globalTemplateLocation();
        const auto restore = qScopeGuard([&] { catalog->setGlobalTemplateFilePath(original); });
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        catalog->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] { for (auto* view : window.tabManager->openEditors()) view->document()->setModified(false); });
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        QTest::keyPress(editor, Qt::Key_F24); QTest::keyClick(editor, Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
        keys("tb"); enter(); QTRY_COMPARE(editor->templateModel()->instances().size(), 1);
        keys("tb_incomplete");
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_instances"), Qt::LeftButton);
        keys("@dut"); enter();
        QPointer<QFrame> dutPanel;
        QTRY_VERIFY_WITH_TIMEOUT((dutPanel = window.findChild<QFrame*>("dutTemplateInputs")) && dutPanel->accessibleName().contains("DUT module"), 15000);
        keys("core"); enter(); QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 3, 15000);
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_behaviors"), Qt::LeftButton);
        QStringList messages;
        connect(editor, &MyCodeEditor::editorStatusMessageRequested, &window, [&](const QString& text) { messages << text; });
        keys("@" + token); enter(); QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 4, 15000);
        const auto child = editor->selectedTemplateId();
        const auto activeVisuals = [&] {
            AnnotationLayerQuery query; query.firstVisibleLine = 0; query.lastVisibleLine = editor->blockCount();
            query.maxAnnotationsPerLine = 64; query.maxLanes = 16;
            QList<EditorAnnotation> result;
            for (const auto& entry : editor->annotationLayerReportForTest(query).annotations)
                if (entry.annotation.kind == EditorAnnotationKind::TemplateSlot && entry.annotation.active) result << entry.annotation;
            return result;
        };
        QTRY_COMPARE(activeVisuals().size(), 1);
        const auto firstPhase = activeVisuals().first().phaseVisible;
        QTRY_VERIFY_WITH_TIMEOUT(activeVisuals().first().phaseVisible != firstPhase, 1600);
        const auto timers = [&] { QList<QTimer*> result; for (auto* timer : editor->findChildren<QTimer*>())
            if (timer->isActive() && timer->interval() == 500) result << timer; return result; };
        const auto initialTimers = timers(); QVERIFY(!initialTimers.isEmpty());
        QTest::keyClick(editor, Qt::Key_Tab); QCOMPARE(editor->templateSlotModeActiveIndex(), 1);
        keys("-1");
        QCOMPARE(timers(), initialTimers);
        QVERIFY(editor->findChild<QLabel*>("templateLocation")->property("fullText").toString().contains(token == "clk_init" ? "Freq mhz" : "Time ns"));
        const auto initial = editor->toPlainText();
        QTest::keyClick(editor, Qt::Key_Tab);
        const int afterTab = editor->templateSlotModeActiveIndex();
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        const bool afterReturn = editor->templateInputEditing();
        evidence("incomplete-" + token + ".json", QJsonDocument(QJsonObject{
            {"afterTab",afterTab},{"editingAfterReturn",afterReturn},{"messages",QJsonArray::fromStringList(messages)},
            {"source",editor->toPlainText()},{"metadata",editor->templateModel()->serialize(path)}}).toJson());
        QCOMPARE(afterTab, token == "clk_init" ? 0 : 2); QVERIFY(!afterReturn);
        QVERIFY(editor->templateAuthoringLocked()); QCOMPARE(editor->toPlainText(), initial);
        QCOMPARE(editor->selectedTemplateId(), child);
        QVERIFY(activeVisuals().isEmpty());
        QVERIFY(editor->findChild<QLabel*>("templateInputFeedback")->property("fullText").toString().contains("Unfinished"));
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(editor->templateInputEditing());
        QTest::keyClick(editor, Qt::Key_Tab); keys(token == "clk_init" ? "156.25" : "12.5");
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->templateInputEditing());
        for (const auto state : {"pending", "no-candidates", "stale-context"}) {
            QVERIFY(editor->editTemplateInstance(child));
            auto context = std::make_shared<TemplateInputContext>(*editor->templateModel()->instance(child)->inputs.first().inputContext);
            context->current = [state] { return QString(state) != "stale-context"; };
            context->liveSnapshot = [state]() -> std::shared_ptr<const SemanticIndexSnapshot> {
                return QString(state) == "pending" ? nullptr : std::make_shared<const SemanticIndexSnapshot>();
            };
            editor->templateModel()->setInputContext(child, context);
            editor->refreshTemplateSlotRanges();
            const auto retained = editor->toPlainText();
            QTest::keyClick(editor, Qt::Key_Down); QCOMPARE(editor->toPlainText(), retained);
            QTest::keyClick(editor, Qt::Key_Tab); QCOMPARE(editor->templateSlotModeActiveIndex(), 1);
            QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
            QVERIFY(!editor->templateInputEditing()); QVERIFY(editor->templateAuthoringLocked());
            QCOMPARE(editor->toPlainText(), retained);
            QVERIFY(editor->findChild<QLabel*>("templateInputFeedback")->property("fullText").toString().contains("Unfinished"));
        }
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor->selectedTemplateRegion(), "behaviors");
        QVERIFY(editor->templateModel()->instance(child));
        QVERIFY(window.tabManager->saveCurrentTab());
    }
    void formatPreservesTemplateIdentity_data() {
        QTest::addColumn<bool>("locked"); QTest::addColumn<bool>("editing");
        QTest::newRow("unlocked") << false << false;
        QTest::newRow("locked-browse") << true << false;
        QTest::newRow("locked-edit") << true << true;
    }
    void formatPreservesTemplateIdentity() {
        QFETCH(bool, locked); QFETCH(bool, editing);
        MyCodeEditor editor; editor.show(); editor.setFocus();
        const auto root = installRoot(editor);
        if (locked) editor.setTemplateAuthoringLocked(true);
        if (editing) QVERIFY(editor.editTemplateInstance(root, "name"));
        else if (editor.templateInputEditing()) QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        const auto before = editor.toPlainText();
        const auto report = editor.formatDocument();
        evidence(QString("format-%1.json").arg(QTest::currentDataTag()), QJsonDocument(QJsonObject{
            {"accepted",report.accepted()},{"changed",report.changed},{"diagnostic",report.diagnostic},
            {"before",before},{"after",editor.toPlainText()},{"instances",editor.templateModel()->instances().size()}}).toJson());
        QVERIFY2(report.accepted(), qPrintable(report.diagnostic)); QVERIFY(report.changed);
        QVERIFY(editor.templateModel()->instance(root));
        QCOMPARE(editor.templateAuthoringLocked(), locked); QCOMPARE(editor.templateInputEditing(), editing);
        const auto formatted = editor.toPlainText();
        editor.undo(); QCOMPARE(editor.toPlainText(), before); QVERIFY(editor.templateModel()->instance(root));
        QCOMPARE(editor.templateInputEditing(), editing);
        editor.redo(); QCOMPARE(editor.toPlainText(), formatted); QVERIFY(editor.templateModel()->instance(root));
        QCOMPARE(editor.templateInputEditing(), editing);
        QVERIFY(!editor.formatDocument().changed);
        if (locked) {
            place(editor, 0); const auto fixed = editor.toPlainText(); QTest::keyClicks(&editor, "forbidden");
            QCOMPARE(editor.toPlainText(), fixed);
        }
    }
    void formatReadOnlyAndAtomicRejection() {
        MyCodeEditor editor; const auto root = installRoot(editor);
        editor.setReadOnly(true); editor.setTemplateAuthoringLocked(true);
        const auto before = editor.toPlainText(); const auto metadata = editor.templateModel()->serialize("tb.sv");
        QVERIFY(!editor.formatDocument().accepted()); QCOMPARE(editor.toPlainText(), before);
        editor.setReadOnly(false);
        const auto changed = before + "module unexpected; endmodule\n";
        FormatterTriviaPositionMapper mapper(before, changed); QString error;
        QVERIFY(!editor.templateModel()->applyFormatting(changed, mapper, &error));
        QCOMPARE(editor.toPlainText(), before); QCOMPARE(editor.templateModel()->serialize("tb.sv"), metadata);
        QVERIFY(editor.templateModel()->instance(root));
    }
    void formatOldMetadataWithSharedViews() {
        const auto fixture = qEnvironmentVariable("TB_TEST_CANCEL_FIXTURE");
        if (fixture.isEmpty()) QSKIP("Supply the isolated 0.32.6 source and metadata fixture.");
        QFile source(QDir(fixture).filePath("tb.sv")); QVERIFY(source.open(QIODevice::ReadOnly));
        QFile saved(QDir(fixture).filePath("metadata.json")); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto metadata = QJsonDocument::fromJson(saved.readAll()).object();
        SharedDocument shared("format-old", metadata["file"].toString(), QString::fromUtf8(source.readAll()));
        MyCodeEditor editor, other;
        shared.attachView(&editor); shared.attachView(&other);
        const auto detach = qScopeGuard([&] { shared.detachView(&other); shared.detachView(&editor); });
        QVERIFY(editor.templateModel()->restore(metadata, metadata["file"].toString()));
        QString root;
        for (const auto& item : editor.templateModel()->instances()) if (item.parentId.isEmpty()) root = item.id;
        QVERIFY(!root.isEmpty()); QCOMPARE(editor.templateModel()->instances().size(), 3);
        editor.setTemplateAuthoringLocked(true); other.setTemplateAuthoringLocked(true);
        QVERIFY(editor.editTemplateInstance(root, "behaviors"));
        const auto regionBefore = region(*editor.templateModel()->instance(root), "behaviors");
        place(editor, regionBefore.start);
        const QString nestedText = "initial begin\nrepeat (2) begin\n  \nend\nend\n";
        TemplateInsertionSpec nested; nested.label = "@nested"; nested.snapshot = {{"kind","snippet"}};
        nested.fragments = {{0, int(nestedText.size()), false}};
        CodeTemplateSlotList inputs{{"count", int(nestedText.indexOf('2')), 1, 1, false, {{"kind","Value"}}},
            {"body", int(nestedText.indexOf("  \n")), 3, 2, true, {{"kind","Code"},{"content","statements"}}}};
        QString error;
        QVERIFY2(editor.insertSimulationTemplate(nestedText, inputs, nested, root, "behaviors", &error), qPrintable(error));
        const auto child = editor.selectedTemplateId();
        QVERIFY(editor.editTemplateInstance(child, "body"));
        QVERIFY(editor.replaceTemplateInput(editor.textCursor(), "  // handwritten\n  $display(\"keep\");\n"));
        QVERIFY(other.editTemplateInstance(root, "name"));
        const auto oldSource = editor.toPlainText(); const auto oldMetadata = editor.templateModel()->serialize(metadata["file"].toString());
        const int oldPosition = editor.textCursor().position(), otherAnchor = other.textCursor().anchor();
        const auto otherSelection = other.textCursor().selectedText();
        const auto report = editor.formatDocument();
        QVERIFY2(report.accepted(), qPrintable(report.diagnostic)); QVERIFY(report.changed);
        QCOMPARE(editor.templateModel()->instances().size(), 4);
        QCOMPARE(editor.selectedTemplateId(), child); QCOMPARE(editor.selectedTemplateRegion(), "body");
        QVERIFY(editor.templateInputEditing()); QVERIFY(other.templateInputEditing());
        QCOMPARE(other.textCursor().selectedText(), otherSelection);
        const auto formatted = editor.toPlainText(); const auto formattedMetadata = editor.templateModel()->serialize(metadata["file"].toString());
        for (const auto& oldItem : oldMetadata["instances"].toArray()) {
            const auto item = oldItem.toObject(); const auto* now = editor.templateModel()->instance(item["id"].toString());
            QVERIFY(now); QCOMPARE(now->parentId, item["parent"].toString()); QCOMPARE(now->parentSlot, item["parentSlot"].toString());
            QCOMPARE(now->fragments.size(), item["fragments"].toArray().size());
        }
        other.undo(); QCOMPARE(editor.toPlainText(), oldSource); QCOMPARE(editor.templateModel()->serialize(metadata["file"].toString()), oldMetadata);
        QVERIFY(editor.templateInputEditing()); QVERIFY(other.templateInputEditing());
        QCOMPARE(editor.textCursor().position(), oldPosition); QCOMPARE(other.textCursor().anchor(), otherAnchor);
        editor.redo(); QCOMPARE(editor.toPlainText(), formatted); QCOMPARE(editor.templateModel()->serialize(metadata["file"].toString()), formattedMetadata);
        QVERIFY(editor.templateInputEditing()); QVERIFY(other.templateInputEditing());
        MyCodeEditor reopened; reopened.setPlainText(formatted);
        QVERIFY(reopened.templateModel()->restore(formattedMetadata, metadata["file"].toString()));
        QCOMPARE(reopened.templateModel()->instances().size(), 4);
        QVERIFY(!editor.formatDocument().changed);
        evidence("formatted-old-shared.sv", formatted.toUtf8()); evidence("formatted-old-shared.json", QJsonDocument(formattedMetadata).toJson());
    }
    void searchCancellationPreservesCodeInput_data() {
        QTest::addColumn<QString>("area");
        QTest::addColumn<QString>("scenario");
        for (const auto& name : {"definitions", "instances", "behaviors"})
            QTest::newRow(qPrintable(QString("empty-%1").arg(name))) << QString(name) << QString("empty");
        for (const auto& name : {"nonempty", "selection", "reverse-selection", "nested-empty",
                 "nested-selection", "repeat", "native-event", "changed-source", "changed-cursor", "acceptance-copy"})
            QTest::newRow(name) << QString("behaviors") << QString(name);
    }
    void searchCancellationPreservesCodeInput() {
        QFETCH(QString, area); QFETCH(QString, scenario);
        QTemporaryDir dir;
        const auto path = dir.filePath("tb.sv");
        if (scenario == "acceptance-copy") {
            const auto fixture = qEnvironmentVariable("TB_TEST_CANCEL_FIXTURE");
            if (fixture.isEmpty()) QSKIP("Supply a copy of the acceptance source and template metadata.");
            QVERIFY(QFile::copy(QDir(fixture).filePath("tb.sv"), path));
            QFile file(QDir(fixture).filePath("metadata.json")); QVERIFY(file.open(QIODevice::ReadOnly));
            auto metadata = QJsonDocument::fromJson(file.readAll()).object();
            metadata["file"] = EditorFileIdentity::lookupKey(path);
            QString error;
            QVERIFY2(WorkspaceSessionStateService().saveDocumentTemplates(path, metadata, &error), qPrintable(error));
        } else put(path, {});
        auto* service = UserTemplateService::getInstance();
        const auto original = service->globalTemplateLocation();
        const auto restoreCatalog = qScopeGuard([&] { service->setGlobalTemplateFilePath(original); });
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] {
            for (auto* view : window.tabManager->openEditors()) view->document()->setModified(false);
        });
        QString owner;
        if (scenario == "acceptance-copy") {
            QCOMPARE(editor->templateModel()->instances().size(), 3);
            for (const auto& item : editor->templateModel()->instances()) if (item.parentId.isEmpty()) owner = item.id;
        } else owner = installRoot(*editor);
        QVERIFY(!owner.isEmpty());
        editor->setTemplateAuthoringLocked(true);
        if (scenario.startsWith("nested-")) {
            const auto slot = region(*editor->templateModel()->instance(owner), area);
            QVERIFY(editor->editTemplateInstance(owner, area, slot.start));
            const QString source = "initial begin\n  \nend\n";
            TemplateInsertionSpec spec; spec.label = "@process"; spec.snapshot = {{"kind", "snippet"}};
            spec.fragments = {{0, int(source.size()), false}};
            CodeTemplateSlotList inputs{{"body", int(source.indexOf("  \n")), 3, 1, true,
                {{"kind", "Code"}, {"content", "statements"}}}};
            QString error;
            QVERIFY2(editor->insertSimulationTemplate(source, inputs, spec, owner, area, &error), qPrintable(error));
            owner = editor->selectedTemplateId(); area = "body";
        }
        auto* regionButton = editor->findChild<QToolButton*>("templateRegion_" + area);
        QVERIFY(regionButton && regionButton->isVisible());
        QTest::mouseClick(regionButton, Qt::LeftButton);
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->templateInputEditing());
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(editor->templateInputEditing()); QCOMPARE(editor->selectedTemplateRegion(), area);
        const bool selection = scenario.contains("selection");
        if (scenario == "nonempty" || selection || scenario == "changed-cursor") {
            QVERIFY(editor->replaceTemplateInput(editor->textCursor(), "preserved = 1;\n  "));
            const int start = editor->toPlainText().indexOf("preserved = 1;");
            QTextCursor cursor(editor->document());
            cursor.setPosition(start + (scenario == "reverse-selection" ? 14 : 0));
            if (selection) cursor.setPosition(start + (scenario == "reverse-selection" ? 0 : 14), QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
        }
        auto* panel = window.globalControlCoordinator->panel.get();
        QJsonArray trace;
        const auto observe = [&](const QString& step) {
            trace << QJsonObject{{"step",step},{"position",editor->textCursor().position()},
                {"anchor",editor->textCursor().anchor()},{"slot",editor->templateSlotModeActiveIndex()},
                {"owner",editor->selectedTemplateId()},{"region",editor->selectedTemplateRegion()},
                {"locked",editor->templateAuthoringLocked()},{"search",panel->isVisible()},
                {"text",editor->toPlainText()}};
        };
        const auto saveTrace = qScopeGuard([&] {
            evidence(QString("cancel-%1.json").arg(QTest::currentDataTag()), QJsonDocument(trace).toJson());
        });
        auto expected = editor->toPlainText();
        auto expectedCursor = editor->textCursor();
        const int repeats = scenario == "repeat" ? 3 : 1;
        for (int i = 0; i < repeats; ++i) {
            observe("before-search");
            keys("@"); QTRY_VERIFY(panel->isVisible()); observe("search-open");
            QCOMPARE(editor->toPlainText(), expected);
            QCOMPARE(editor->textCursor().position(), expectedCursor.position());
            QCOMPARE(editor->textCursor().anchor(), expectedCursor.anchor());
            if (scenario == "changed-source") {
                // Simulate a shared-document edit while the popup owns focus.
                const int start = region(*editor->templateModel()->instance(owner), area).start;
                QTextCursor other(editor->document()); other.setPosition(start);
                other.insertText("// concurrent\n");
                expected.insert(start, "// concurrent\n");
            } else if (scenario == "changed-cursor") {
                expectedCursor.setPosition(editor->toPlainText().indexOf("preserved = 1;") + 4);
                editor->setTextCursor(expectedCursor);
            }
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
            QTRY_VERIFY(!panel->isVisible()); QTRY_VERIFY(editor->hasFocus()); observe("search-cancelled");
            QCOMPARE(editor->toPlainText(), expected);
            QCOMPARE(editor->textCursor().position(), expectedCursor.position());
            QCOMPARE(editor->textCursor().anchor(), expectedCursor.anchor());
            QVERIFY(editor->templateAuthoringLocked()); QVERIFY(editor->templateInputEditing());
        }
        const auto inserted = scenario == "native-event" ? QString("@(posedge clk)") : QString("// comment");
        expected.replace(expectedCursor.selectionStart(), expectedCursor.selectionEnd() - expectedCursor.selectionStart(), inserted);
        if (scenario == "native-event") keys(inserted);
        else { QTest::keyClick(editor, Qt::Key_Slash); keys("/ comment"); }
        observe("continued-input");
        QCOMPARE(editor->toPlainText(), expected);
        QVERIFY(editor->templateAuthoringLocked()); QCOMPARE(editor->selectedTemplateRegion(), area);
        QVERIFY(!panel->isVisible());
        const auto metadata = editor->templateModel()->serialize(path);
        // Fixed text and a selection crossing the Code boundary remain protected.
        const int fixed = editor->toPlainText().lastIndexOf("endmodule");
        place(*editor, fixed + 3); QTest::keyClick(editor, Qt::Key_X);
        QCOMPARE(editor->toPlainText(), expected);
        QTextCursor crossing(editor->document()); crossing.setPosition(fixed - 1);
        crossing.setPosition(fixed + 3, QTextCursor::KeepAnchor); editor->setTextCursor(crossing);
        QTest::keyClick(editor, Qt::Key_Backspace); QCOMPARE(editor->toPlainText(), expected);
        QCOMPARE(editor->templateModel()->serialize(path), metadata);
        observe("fixed-code-protected");
        QVERIFY(window.tabManager->saveCurrentTab());
        QVERIFY(window.tabManager->closeTabsInWorkspace(dir.path()));
        QVERIFY(window.tabManager->openFileInTab(path)); editor = window.tabManager->getCurrentEditor();
        QCOMPARE(editor->toPlainText(), expected); QCOMPARE(editor->templateModel()->serialize(path), metadata);
        observe("saved-and-reopened");
    }
    void realUartMainWindow() {
        const auto fixture = qEnvironmentVariable("TB_TEST_UART_INPUT");
        if (fixture.isEmpty()) QSKIP("Supply the isolated UART input directory.");
        QTemporaryDir dir;
        for (const auto& name : {"_uart_package.sv", "uart_tx_phy.sv"})
            QVERIFY(QFile::copy(QDir(fixture).filePath(name), dir.filePath(name)));
        const auto compilation = qEnvironmentVariable("TB_TEST_UART_COMPILATION");
        if (!compilation.isEmpty()) {
            QFile manifest(QDir(compilation).filePath("manifest.json")); QVERIFY(manifest.open(QIODevice::ReadOnly));
            QJsonArray sources;
            for (const auto& value : QJsonDocument::fromJson(manifest.readAll()).array()) {
                const auto name = value.toObject()["relative"].toString();
                const auto target = dir.filePath(name);
                QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
                if (!QFileInfo::exists(target)) QVERIFY(QFile::copy(QDir(compilation).filePath(name), target));
                if (name != "sim/tb_uart_tx_phy.sv") sources << name;
            }
            const auto storage = dir.filePath(".simdock/projects/UART"); QVERIFY(QDir().mkpath(storage));
            put(QDir(storage).filePath("project.json"), QJsonDocument(QJsonObject{
                {"schema","simdock.project/v1"}, {"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
                {"name","UART"}, {"sources",sources}, {"dutFile","uart_tx_phy.sv"}, {"dutName","uart_tx_phy"},
                {"tbFile","sim/tb_uart_tx_phy.sv"}, {"tbName","tb_uart_tx_phy"}}).toJson());
        }
        put(dir.filePath("tb.sv"), {});
        auto* service = UserTemplateService::getInstance(); const auto original = service->globalTemplateLocation();
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        window.tabManager->openFileInTab(dir.filePath("tb.sv"));
        if (!compilation.isEmpty()) {
            const auto inputs = window.simulationWorkspace->workbench()->activeCompilationInputs();
            QCOMPARE(inputs.size(), 7); evidence("uart-window-compilation-inputs.json", QJsonDocument(QJsonArray::fromStringList(inputs)).toJson());
        }
        QStringList messages;
        connect(window.simulationCommands.get(), &SimulationCommandService::message, &window,
            [&](const QString& text) { messages << text; evidence("uart-window-messages.txt", messages.join('\n').toUtf8()); });
        auto* editor = window.tabManager->getCurrentEditor(); editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        QTest::keyPress(editor, Qt::Key_F24); QTest::keyClick(editor, Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
        keys("tb"); QCOMPARE(editor->toPlainText(), QString()); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 1, 20000);
        keys("tb_uart");
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_instances"), Qt::LeftButton);
        const auto before = editor->toPlainText();
        messages.clear();
        keys("@dut"); enter();
        QPointer<QFrame> panel;
        QTRY_VERIFY_WITH_TIMEOUT((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->isVisible()
            && panel->accessibleName().contains("DUT module"), 20000);
        keys("uart_tx_phy"); enter();
        QTRY_VERIFY_WITH_TIMEOUT(editor->templateModel()->instances().size() == 3 || !messages.isEmpty(), 20000);
        evidence("uart-window-source.sv", editor->toPlainText().toUtf8());
        const auto artifacts = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) window.grab().save(QDir(artifacts).filePath("uart-mainwindow.png"));
        QVERIFY2(editor->templateModel()->instances().size() == 3, qPrintable(messages.join('\n')));
        const auto generated = editor->toPlainText();
        editor->undo(); QCOMPARE(editor->toPlainText(), before); editor->redo(); QCOMPARE(editor->toPlainText(), generated);
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_behaviors"), Qt::LeftButton);
        messages.clear(); keys("@clk_init"); enter();
        QTRY_VERIFY_WITH_TIMEOUT(editor->templateModel()->instances().size() == 4 || !messages.isEmpty(), 20000);
        QVERIFY2(editor->templateModel()->instances().size() == 4, qPrintable(messages.join('\n')));
        QVERIFY(editor->toPlainText().contains("u_uart_tx_phy_clk ="));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_behaviors"), Qt::LeftButton);
        messages.clear(); keys("@reset"); enter();
        QTRY_VERIFY_WITH_TIMEOUT(editor->templateModel()->instances().size() == 5 || !messages.isEmpty(), 20000);
        QVERIFY2(editor->templateModel()->instances().size() == 5, qPrintable(messages.join('\n')));
        QVERIFY(editor->toPlainText().contains("u_uart_tx_phy_rst ="));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        const auto complete = editor->toPlainText(); evidence("uart-window-with-behaviors.sv", complete.toUtf8());
        QVERIFY(window.tabManager->saveCurrentTab());
        QVERIFY(window.tabManager->closeTabsInWorkspace(dir.path()));
        QVERIFY(window.tabManager->openFileInTab(dir.filePath("tb.sv")));
        editor = window.tabManager->getCurrentEditor(); QCOMPARE(editor->toPlainText(), complete);
        QCOMPARE(editor->templateModel()->instances().size(), 5);
        editor->document()->setModified(false); service->setGlobalTemplateFilePath(original);
    }
    void realUartDefaultGeneration() {
        const auto fixture = qEnvironmentVariable("TB_TEST_UART_INPUT");
        if (fixture.isEmpty()) QSKIP("Supply the isolated UART input directory.");
        QTemporaryDir dir;
        for (const auto& name : {"_uart_package.sv", "uart_tx_phy.sv"})
            QVERIFY(QFile::copy(QDir(fixture).filePath(name), dir.filePath(name)));
        MyCodeEditor editor; const auto root = installRoot(editor);
        auto request = requestFor(editor, root, dir.path());
        request.orderedFiles = {dir.filePath("_uart_package.sv"), dir.filePath("uart_tx_phy.sv"), request.context.fileName};
        const auto compilation = qEnvironmentVariable("TB_TEST_UART_COMPILATION");
        if (!compilation.isEmpty()) {
            QFile manifest(QDir(compilation).filePath("manifest.json")); QVERIFY(manifest.open(QIODevice::ReadOnly));
            request.orderedFiles.clear();
            for (const auto& value : QJsonDocument::fromJson(manifest.readAll()).array()) {
                const auto name = value.toObject()["relative"].toString();
                const auto target = dir.filePath(name);
                QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
                if (!QFileInfo::exists(target)) QVERIFY(QFile::copy(QDir(compilation).filePath(name), target));
                request.orderedFiles << target;
            }
            request.orderedFiles << request.context.fileName;
        }
        DutTemplateOptions options; options.module = "uart_tx_phy";
        const auto analysis = DutTemplatePlanner::analyze(request, editor.templateModel()->instances(), options);
        QJsonArray ports; for (const auto& port : analysis.ports)
            ports.append(QJsonObject{{"name",port.name},{"type",port.type},{"suffix",port.suffix}});
        evidence("uart-analysis.json", QJsonDocument(QJsonObject{{"error",analysis.error},{"ports",ports},
            {"parameters",QJsonArray::fromStringList(analysis.parameters)}}).toJson());
        QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error));
        const auto plan = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        evidence("uart-plan-error.txt", plan.error.toUtf8());
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QString error; QVERIFY2(editor.applyTemplateEdits(plan.edits, &error), qPrintable(error));
        evidence("uart-generated.sv", editor.toPlainText().toUtf8());
        QCOMPARE(analysis.ports.size(), 16);
        QCOMPARE(analysis.parameters, QStringList{"P_CLK_PERIOD_UNIT_NS"});
        QVERIFY(!editor.toPlainText().contains(".P_CLK_PERIOD_UNIT_NS(P_CLK_PERIOD_UNIT_NS)"));
        QVERIFY(editor.toPlainText().contains("uart_package::stop_bit_e"));
        SemanticAnalysisRequest roleRequest; roleRequest.sourceOverrides[request.context.fileName] = editor.toPlainText();
        SemanticInputCapture roleCapture(roleRequest); SlangManager slang;
        const auto roleAnalysis = slang.analyzeCapturedWorkspace(roleCapture, request.orderedFiles, {}, {}, {}, {}, {true, true, false});
        QVERIFY2(roleAnalysis.error.isEmpty(), qPrintable(roleAnalysis.error));
        QJsonArray diagnosticEvidence;
        for (const auto& diagnostic : roleAnalysis.diagnostics)
            diagnosticEvidence << QJsonObject{{"file",diagnostic.fileName},{"line",diagnostic.line},
                {"severity",int(diagnostic.severity)},{"message",diagnostic.message}};
        evidence("uart-multiple-top-diagnostics.json", QJsonDocument(diagnosticEvidence).toJson());
        for (const auto& diagnostic : roleAnalysis.diagnostics)
            QVERIFY2(diagnostic.severity != SemanticDiagnostic::Error, qPrintable(diagnostic.message));
        QSet<QString> roles;
        for (const auto& record : roleAnalysis.symbols)
            for (auto it = record.presentation.objectInfoByPath.cbegin(); it != record.presentation.objectInfoByPath.cend(); ++it)
                if (it.key().startsWith("tb.u_uart_tx_phy_"))
                    for (const auto& role : it->signalRoles) roles.insert(role.role);
        QVERIFY(roles.contains("Clock")); QVERIFY(roles.contains("Reset"));
        const auto savedUart = qEnvironmentVariable("TB_TEST_SAVED_UART");
        if (!savedUart.isEmpty()) {
            QFile saved(savedUart); QVERIFY(saved.open(QIODevice::ReadOnly));
            const auto bytes = saved.readAll(); const auto nativeRoot = QFileInfo(savedUart).absoluteDir();
            QStringList files;
            for (const auto& file : request.orderedFiles)
                files << nativeRoot.filePath(QDir(dir.path()).relativeFilePath(file));
            SemanticAnalysisRequest savedRequest; SemanticInputCapture savedCapture(savedRequest);
            const auto checked = slang.analyzeCapturedWorkspace(savedCapture, files, {}, {}, "tb_native_uart", {}, {true, true, false});
            QJsonArray diagnostics;
            for (const auto& diagnostic : checked.diagnostics)
                diagnostics << QJsonObject{{"file",diagnostic.fileName},{"line",diagnostic.line},
                    {"severity",int(diagnostic.severity)},{"message",diagnostic.message}};
            evidence("native-slang.json", QJsonDocument(QJsonObject{
                {"source",savedUart},{"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
                {"files",QJsonArray::fromStringList(files)},{"error",checked.error},{"diagnostics",diagnostics}}).toJson());
            QVERIFY2(checked.error.isEmpty(), qPrintable(checked.error));
            QVERIFY(savedCapture.stillMatchesDisk({}));
            for (const auto& diagnostic : checked.diagnostics)
                QVERIFY2(diagnostic.severity != SemanticDiagnostic::Error, qPrintable(diagnostic.message));
        }
    }
    void signalRoleDiagnosticPolicy_data() {
        QTest::addColumn<QString>("extra"); QTest::addColumn<bool>("blocked");
        QTest::newRow("package-without-timescale") << QString() << false;
        QTest::newRow("module-without-timescale") << QString("module untimed; endmodule\n") << true;
        QTest::newRow("undeclared-value") << QString("module broken; timeunit 1ns; timeprecision 1ps; logic q; assign q = missing_value; endmodule\n") << true;
    }
    void signalRoleDiagnosticPolicy() {
        QFETCH(QString, extra); QFETCH(bool, blocked);
        QTemporaryDir dir; const auto file = dir.filePath("roles.sv");
        SemanticAnalysisRequest request;
        request.sourceOverrides[file] = QString("package types; typedef logic [7:0] data_t; endpackage\n")
            + QString::fromUtf8(timingDut) + QString(
                "module tb; timeunit 1ns; timeprecision 1ps; logic clk, rst; types::data_t data, q;\n"
                "core unit(.a(clk),.b(rst),.data_in(data),.data_out(q)); endmodule\n") + extra;
        SemanticInputCapture capture(request); SlangManager slang;
        const auto analysis = slang.analyzeCapturedWorkspace(capture, {file}, {}, {}, {}, {}, {true, true, false});
        QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error));
        bool hasError = false, clock = false, reset = false, roleBlocked = false;
        for (const auto& diagnostic : analysis.diagnostics) hasError |= diagnostic.severity == SemanticDiagnostic::Error;
        for (const auto& record : analysis.symbols) {
            const auto info = record.presentation.objectInfoByPath.value("tb.clk");
            roleBlocked |= info.signalRoleIssues.contains("Slang compilation has errors; role inference is unavailable.");
            for (const auto& role : info.signalRoles) clock |= role.role == "Clock";
            for (const auto& role : record.presentation.objectInfoByPath.value("tb.rst").signalRoles) reset |= role.role == "Reset";
        }
        QCOMPARE(hasError, blocked); QCOMPARE(roleBlocked, blocked);
        QCOMPARE(clock, !blocked); QCOMPARE(reset, !blocked);
    }
    void parameterFactsWithInactiveTop() {
        QTemporaryDir dir; put(dir.filePath("dut.sv"), dut);
        put(dir.filePath("old_tb.sv"), "module old_tb; timeunit 1ns; timeprecision 1ps; core #(.W(4)) old_dut(); endmodule\n");
        MyCodeEditor editor; const auto root = installRoot(editor);
        auto request = requestFor(editor, root, dir.path());
        request.orderedFiles.insert(1, dir.filePath("old_tb.sv"));
        const auto original = editor.toPlainText();
        DutTemplateOptions options; options.module = "core";
        auto plan = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.edits.first().text.contains(".W(8)"));
        QVERIFY(plan.edits.last().text.contains("[7:0]"));
        options.parameters["W"] = "12";
        plan = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
        QVERIFY(plan.edits.first().text.contains(".W(12)"));
        QVERIFY(plan.edits.last().text.contains("[11:0]"));
        options.parameters["missing"] = "1";
        plan = DutTemplatePlanner::prepare(request, editor.templateModel()->instances(), options);
        QVERIFY(plan.error.contains("Unknown or non-overridable")); QVERIFY(plan.edits.isEmpty());
        QCOMPARE(editor.toPlainText(), original);
    }
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
        auto* panel = editor.findChild<QFrame*>("dutTemplateInputs");
        QVERIFY(panel && panel->isVisible()); QVERIFY(panel->accessibleName().contains("Source identity changed"));
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QTRY_VERIFY(!editor.findChild<QFrame*>("dutTemplateInputs"));
    }
    void confirmationAndModeFeedback() {
        QTemporaryDir dir;
        put(dir.filePath("tb.sv"), {});
        auto* service = UserTemplateService::getInstance();
        const auto original = service->globalTemplateLocation();
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        window.tabManager->openFileInTab(dir.filePath("tb.sv"));
        auto* editor = window.tabManager->getCurrentEditor();
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        auto* panel = window.globalControlCoordinator->panel.get();
        const auto badge = [&] { return editor->findChild<QLabel*>("templateModeBadge"); };
        const auto capture = [&](const QString& name) {
            const auto path = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
            if (!path.isEmpty()) window.grab().save(QDir(path).filePath(name + ".png"));
        };
        QTest::keyPress(editor, Qt::Key_F24);
        QVERIFY(badge() && badge()->isVisible()); QVERIFY(badge()->text().contains("F24 HELD"));
        capture("mainwindow-f24-held");
        QTest::keyRelease(editor, Qt::Key_F24);
        QVERIFY(!badge()->isVisible());
        QTest::keyPress(editor, Qt::Key_F24);
        QEvent deactivate(QEvent::ApplicationDeactivate); QApplication::sendEvent(qApp, &deactivate);
        QVERIFY(!badge()->isVisible()); QTest::keyRelease(editor, Qt::Key_F24);
        for (int mode = 0; mode < 3; ++mode) {
            editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
            if (mode == 0) QTest::keyPress(editor, Qt::Key_F24);
            else {
                QTest::keyPress(editor, Qt::Key_F24); QTest::keyClick(editor, Qt::Key_Space);
                QTest::keyRelease(editor, Qt::Key_F24);
                QVERIFY(badge()->text().contains("TEMPLATE LOCK"));
                if (mode == 2) QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
            }
            keys("tb");
            if (mode == 0) QTest::keyRelease(QApplication::focusWidget(), Qt::Key_F24);
            QVERIFY(panel->isVisible()); QCOMPARE(panel->currentItems.size(), 1);
            QCOMPARE(editor->toPlainText(), QString());
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
            QCoreApplication::processEvents(); QCOMPARE(editor->toPlainText(), QString());
            QCOMPARE(editor->templateAuthoringLocked(), mode != 0);
            QVERIFY(!panel->isVisible());
            if (mode != 0) {
                QTest::keyPress(editor, Qt::Key_F24); QTest::keyClick(editor, Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
                QVERIFY(!editor->templateAuthoringLocked());
            }
        }
        QTest::keyPress(editor, Qt::Key_F24); QTest::keyClick(editor, Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
        keys("tb"); enter(); QTRY_COMPARE(editor->templateModel()->instances().size(), 1);
        auto* stableBadge = badge(); keys("tb_confirmed"); QCOMPARE(badge(), stableBadge);
        QVERIFY(badge()->text().contains("EDITING INPUT"));
        capture("mainwindow-inputs-empty-regions");
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(editor->templateAuthoringLocked()); QVERIFY(editor->templateInputEditing());
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(badge()->text().contains("BROWSE"));
        QTest::keyClick(editor, Qt::Key_Escape); QVERIFY(editor->templateAuthoringLocked());
        capture("mainwindow-locked-navigation");
        auto* defs = editor->findChild<QToolButton*>("templateRegion_definitions");
        QVERIFY(defs && defs->isVisible()); QTest::mouseClick(defs, Qt::LeftButton);
        const auto before = editor->toPlainText(); keys("@dut");
        QVERIFY(panel->isVisible()); QVERIFY(panel->availabilityNotice->text().contains("instance", Qt::CaseInsensitive));
        QCOMPARE(editor->toPlainText(), before);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QVERIFY(editor->templateAuthoringLocked()); QVERIFY(editor->templateInputEditing());
        editor->document()->setModified(false);
        service->setGlobalTemplateFilePath(original);
    }
    void regionHierarchyAndRecovery() {
        MyCodeEditor editor; editor.resize(900, 650); editor.show(); editor.activateWindow(); editor.setFocus();
        QTRY_VERIFY(editor.hasFocus());
        const auto firstRoot = installRoot(editor);
        place(editor, editor.toPlainText().size()); const auto secondRoot = installRoot(editor);
        const auto add = [&](const QString& parent, const QString& name, bool container) {
            const auto slot = region(*editor.templateModel()->instance(parent), name);
            editor.editTemplateInstance(parent, name, slot.start + slot.length);
            const QString source = container ? "initial begin\n  \nend\n" : "hits += 1;\n";
            CodeTemplateSlotList inputs = container ? CodeTemplateSlotList{{"body", int(source.indexOf("  \n")), 3, 1, true, {{"kind", "Code"}}}}
                : CodeTemplateSlotList{{"count", 8, 1, 1, true, {{"kind", "Choice"}, {"choices", QStringList{"1", "2"}}}}};
            TemplateInsertionSpec spec; spec.label = container ? "@process" : "@step";
            spec.snapshot = {{"kind", "snippet"}}; spec.fragments = {{0, int(source.size()), false}};
            QString error;
            if (!editor.insertSimulationTemplate(source, inputs, spec, parent, name, &error)) qFatal("%s", qPrintable(error));
            return editor.selectedTemplateId();
        };
        const auto branch = add(firstRoot, "behaviors", true);
        const auto one = add(branch, "body", false), two = add(branch, "body", false);
        const auto otherBranch = add(secondRoot, "behaviors", true);
        editor.setTemplateAuthoringLocked(true); QVERIFY(editor.editTemplateInstance(one));
        QTest::keyClick(&editor, Qt::Key_Down); QVERIFY(editor.toPlainText().contains("hits += 2;"));
        QCOMPARE(editor.selectedTemplateId(), one);
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        QTest::keyClick(&editor, Qt::Key_Down); QCOMPARE(editor.selectedTemplateId(), two);
        QTest::keyClick(&editor, Qt::Key_Down); QCOMPARE(editor.selectedTemplateId(), one);
        QTest::keyClick(&editor, Qt::Key_Escape); QVERIFY(editor.templateAuthoringLocked());
        QCOMPARE(editor.selectedTemplateId(), one);
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor.selectedTemplateId(), branch); QCOMPARE(editor.selectedTemplateRegion(), "body");
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor.selectedTemplateId(), branch); QVERIFY(editor.selectedTemplateRegion().isEmpty());
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor.selectedTemplateId(), firstRoot); QCOMPARE(editor.selectedTemplateRegion(), "behaviors");
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.selectedTemplateId(), firstRoot); QVERIFY(editor.selectedTemplateRegion().isEmpty());
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        QTest::keyClick(&editor, Qt::Key_Down); QCOMPARE(editor.selectedTemplateId(), secondRoot);
        QTest::keyClick(&editor, Qt::Key_Return);
        for (int i = 0; i < 3; ++i) QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.selectedTemplateRegion(), "behaviors");
        QTest::keyClick(&editor, Qt::Key_Return); QVERIFY(editor.templateInputEditing());
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor.selectedTemplateId(), otherBranch);
        QTest::keyClick(&editor, Qt::Key_Return); QCOMPARE(editor.selectedTemplateRegion(), "body");
        QTest::keyClick(&editor, Qt::Key_Return);
        QVERIFY(editor.templateInputEditing());
        const auto before = editor.toPlainText(); QTest::keyClicks(&editor, "// handwritten"); QVERIFY(editor.toPlainText() != before);
        editor.undo(); editor.redo();
        const auto child = *editor.templateModel()->instance(otherBranch);
        TemplateDocumentEdit remove; remove.start = child.fragments.first().start; remove.length = child.fragments.first().length;
        remove.parentId = child.parentId; remove.parentSlot = child.parentSlot; remove.replaceId = child.id;
        QString error; QVERIFY2(editor.applyTemplateEdits({remove}, &error), qPrintable(error));
        auto* empty = editor.findChild<QToolButton*>("templateRegion_behaviors"); QVERIFY(empty && empty->text().contains("Empty"));
        editor.undo(); QVERIFY(editor.templateModel()->instance(otherBranch)); editor.redo();
        empty = editor.findChild<QToolButton*>("templateRegion_behaviors"); QVERIFY(empty && empty->text().contains("Empty"));
        QTest::mouseClick(empty, Qt::LeftButton); QVERIFY(editor.templateSelectionEditable());
        const auto text = editor.toPlainText(); const auto metadata = editor.templateModel()->serialize("hierarchy.sv");
        MyCodeEditor restored; restored.setPlainText(text); QVERIFY(restored.templateModel()->restore(metadata, "hierarchy.sv"));
        QVERIFY(restored.findChild<QToolButton*>("templateRegion_instances"));
    }
    void savedRootCompatibility() {
        const auto directory = qEnvironmentVariable("TB_TEST_SAVED_ROOT");
        if (directory.isEmpty()) QSKIP("Set TB_TEST_SAVED_ROOT to a retained 0.32.4 source/metadata pair.");
        QFile source(QDir(directory).filePath("main-window-tb.sv")), metadata(QDir(directory).filePath("main-window-metadata.json"));
        QVERIFY(source.open(QIODevice::ReadOnly)); QVERIFY(metadata.open(QIODevice::ReadOnly));
        const auto text = QString::fromUtf8(source.readAll());
        const auto saved = QJsonDocument::fromJson(metadata.readAll()).object();
        MyCodeEditor editor; editor.resize(900, 650); editor.show(); editor.activateWindow(); editor.setPlainText(text);
        QVERIFY(editor.templateModel()->restore(saved, saved["file"].toString()));
        QCOMPARE(editor.templateModel()->instances().size(), saved["instances"].toArray().size());
        QCOMPARE(editor.toPlainText(), text);
        QCOMPARE(editor.templateModel()->serialize(saved["file"].toString()), saved);
        auto* button = editor.findChild<QToolButton*>("templateRegion_instances");
        QVERIFY(button && button->isVisible()); QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(editor.templateInputEditing()); QCOMPARE(editor.templateContainerContent(), "instances");
        if (editor.toPlainText() != text) {
            const auto prepared = editor.toPlainText();
            qInfo() << "before undo" << editor.selectedTemplateId() << editor.selectedTemplateRegion() << editor.templateInputEditing();
            editor.undo(); QCOMPARE(editor.toPlainText(), text);
            qInfo() << "after undo" << editor.selectedTemplateId() << editor.selectedTemplateRegion() << editor.templateInputEditing();
            QCOMPARE(editor.templateModel()->serialize(saved["file"].toString()), saved);
            editor.redo(); QCOMPARE(editor.toPlainText(), prepared);
            qInfo() << "after redo" << editor.selectedTemplateId() << editor.selectedTemplateRegion() << editor.templateInputEditing();
        }
        if (!editor.templateInputEditing()) QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        const auto* child = editor.templateModel()->instance(editor.selectedTemplateId());
        QVERIFY(child && !child->parentId.isEmpty()); QCOMPARE(child->parentSlot, "instances");
    }
    void dutCancellationAndTargetChanges_data() {
        QTest::addColumn<QString>("change");
        for (const auto& name : {"escape", "unlock", "document", "workspace", "edit"})
            QTest::newRow(name) << QString(name);
    }
    void dutCancellationAndTargetChanges() {
        QFETCH(QString, change);
        QTemporaryDir dir, nextWorkspace;
        put(dir.filePath("dut.sv"), timingDut); put(dir.filePath("tb.sv"), {});
        put(dir.filePath("another.sv"), "module another; endmodule\n");
        auto* service = UserTemplateService::getInstance(); const auto original = service->globalTemplateLocation();
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        window.tabManager->openFileInTab(dir.filePath("tb.sv"));
        QPointer<MyCodeEditor> editor = window.tabManager->getCurrentEditor(); installRoot(*editor);
        editor->setTemplateAuthoringLocked(true);
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_instances"), Qt::LeftButton);
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        const auto before = editor->toPlainText();
        QStringList messages;
        connect(window.simulationCommands.get(), &SimulationCommandService::message, &window,
            [&](const QString& value) { messages << value; });
        keys("@dut"); enter();
        QPointer<QFrame> panel;
        QTRY_VERIFY_WITH_TIMEOUT((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->accessibleName().contains("DUT module"), 20000);
        keys("core");
        QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(QApplication::focusWidget(), &accept);
        QVERIFY(panel && panel->accessibleName().contains("Preparing DUT"));
        auto expected = before;
        if (change == "escape") QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        else if (change == "unlock") {
            QTest::keyPress(QApplication::focusWidget(), Qt::Key_F24);
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
        } else if (change == "document") {
            QVERIFY(window.tabManager->openFileInTab(dir.filePath("another.sv")));
        } else if (change == "workspace") {
            editor->document()->setModified(false);
            window.workspaceSessionCoordinator->openWorkspace(nextWorkspace.path());
        } else {
            QVERIFY(editor->replaceTemplateInput(editor->textCursor(), "// target changed\n"));
            expected = editor->toPlainText();
        }
        QVERIFY(QThreadPool::globalInstance()->waitForDone(20000)); QCoreApplication::processEvents();
        if (change == "edit") {
            QTRY_VERIFY(panel && panel->isVisible() && panel->accessibleName().contains("changed"));
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        } else if (panel && panel->isVisible()) QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QTRY_VERIFY(!panel);
        if (editor) {
            QCOMPARE(editor->toPlainText(), expected); QCOMPARE(editor->templateModel()->instances().size(), 1);
            QCOMPARE(editor->templateAuthoringLocked(), change != "unlock");
            editor->document()->setModified(false);
        }
        QVERIFY(!messages.join('\n').contains("DUT applied"));
        evidence("cancel-" + change + ".txt", messages.join('\n').toUtf8());
        service->setGlobalTemplateFilePath(original);
    }
    void optionalDutInputsAndRecovery() {
        QTemporaryDir dir;
        put(dir.filePath("dut.sv"), timingDut); put(dir.filePath("tb.sv"), {});
        auto* service = UserTemplateService::getInstance(); const auto original = service->globalTemplateLocation();
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        window.tabManager->openFileInTab(dir.filePath("tb.sv"));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto root = installRoot(*editor); editor->setTemplateAuthoringLocked(true);
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_instances"), Qt::LeftButton);
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        keys("@dut"); enter();
        QPointer<QFrame> panel;
        QTRY_VERIFY_WITH_TIMEOUT((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->accessibleName().contains("DUT module"), 20000);
        put(dir.filePath("dut.sv"), timingDut + "// changed while selecting a module\n");
        keys("core"); enter();
        QVERIFY(panel && panel->isVisible() && panel->accessibleName().contains("source changed"));
        QCOMPARE(editor->templateModel()->instances().size(), 1);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down); enter();
        QTRY_VERIFY_WITH_TIMEOUT(panel && panel->accessibleName().contains("DUT module"), 20000);
        keys("core"); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 3, 20000);
        const auto first = editor->toPlainText(), id = editor->selectedTemplateId();
        enter(); QTRY_VERIFY((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->accessibleName().contains("Enter selects"));
        enter(); QTRY_VERIFY_WITH_TIMEOUT(panel && panel->accessibleName().contains("DUT module"), 20000);
        enter(); QTRY_VERIFY_WITH_TIMEOUT(panel && panel->accessibleName().contains("instance name"), 20000);
        keys("unit"); enter(); QVERIFY(panel->accessibleName().contains("Parameter W"));
        keys("unresolved"); enter();
        QTRY_VERIFY_WITH_TIMEOUT(panel && panel->accessibleName().contains("resolved integral constant"), 20000);
        QVERIFY(panel->isVisible()); QCOMPARE(editor->toPlainText(), first);
        const auto artifacts = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            window.grab().save(QDir(artifacts).filePath("dut-visible-error.png"));
            panel->grab().save(QDir(artifacts).filePath("dut-failure-popup.png"));
            evidence("dut-visible-error.txt", panel->accessibleName().toUtf8());
        }
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down); enter();
        QTRY_VERIFY_WITH_TIMEOUT(panel && panel->accessibleName().contains("DUT module"), 20000);
        keys("core"); enter(); QTRY_VERIFY_WITH_TIMEOUT(panel && panel->accessibleName().contains("instance name"), 20000);
        keys("unit"); enter(); keys("12"); enter();
        QTRY_VERIFY_WITH_TIMEOUT(panel && panel->accessibleName().startsWith("a "), 20000);
        keys("u_core_a"); QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down); enter();
        keys("u_core_b"); QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down); enter();
        enter(); enter(); QVERIFY(panel->accessibleName().contains("Enter applies")); enter();
        QTRY_VERIFY_WITH_TIMEOUT(!panel, 20000);
        QVERIFY(editor->templateModel()->instance(id));
        QCOMPARE(editor->templateModel()->instances().size(), 4);
        QVERIFY(editor->toPlainText().contains("unit (")); QVERIFY(editor->toPlainText().contains("[11:0] unit_data_in"));
        QVERIFY(editor->toPlainText().contains(".a(u_core_a)")); QVERIFY(editor->toPlainText().contains(".b(u_core_b)"));
        const auto adjusted = editor->toPlainText(); editor->undo(); QCOMPARE(editor->toPlainText(), first);
        editor->redo(); QCOMPARE(editor->toPlainText(), adjusted);
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_instances"), Qt::LeftButton); keys("@dut"); enter();
        QTRY_VERIFY_WITH_TIMEOUT((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->accessibleName().contains("DUT module"), 20000);
        keys("core"); enter(); QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 6, 20000);
        QVERIFY(editor->toPlainText().contains("u_core (")); QVERIFY(editor->toPlainText().contains("[7:0] u_core_data_in_2"));
        evidence("optional-inputs-and-second-dut.sv", editor->toPlainText().toUtf8());
        QTest::mouseClick(editor->findChild<QToolButton*>("templateRegion_instances"), Qt::LeftButton); const auto beforeCancel = editor->toPlainText();
        keys("@dut"); enter();
        QTRY_VERIFY_WITH_TIMEOUT((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->accessibleName().contains("DUT module"), 20000);
        QTest::keyPress(QApplication::focusWidget(), Qt::Key_F24);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Space); QTest::keyRelease(editor, Qt::Key_F24);
        QTRY_VERIFY(!panel); QVERIFY(!editor->templateAuthoringLocked()); QCOMPARE(editor->toPlainText(), beforeCancel);
        editor->document()->setModified(false); service->setGlobalTemplateFilePath(original);
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
            if (TemplateCapabilities::root(record.command)) rootCommand = record.commandToken.mid(1);
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
        QCOMPARE(editor->toPlainText(), QString());
        QVERIFY(window.globalControlCoordinator->panel->isVisible());
        enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 1, 20000);
        QVERIFY(editor->templateInputEditing());
        keys("tb_example"); QCOMPARE(editor->templateModel()->instances().size(), 1);
        auto* instancesButton = editor->findChild<QToolButton*>("templateRegion_" + TemplateCapabilities::region(editor->templateModel()->instances().first().snapshot, "instances"));
        QVERIFY(instancesButton && instancesButton->isVisible());
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QTest::keyClick(editor, Qt::Key_Down); QTest::keyClick(editor, Qt::Key_Down);
        QCOMPARE(editor->selectedTemplateRegion(), instancesButton->property("regionName").toString());
        enter(); QVERIFY(editor->templateInputEditing());
        QCOMPARE(editor->templateContainerContent(), "instances");
        keys(moduleCommand);
        QCOMPARE(editor->selectedTemplateRegion(), instancesButton->property("regionName").toString());
        QVERIFY(editor->findChild<QLabel*>("templateLocation")->property("fullText").toString().contains(
            editor->selectedTemplateRegion(), Qt::CaseInsensitive));
        enter();
        QPointer<QWidget> panel;
        QTRY_VERIFY_WITH_TIMEOUT((panel = window.findChild<QFrame*>("dutTemplateInputs")) && panel->isVisible()
            && panel->accessibleName().contains("DUT module"), 20000);
        keys("CRE"); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 3, 20000);
        QVERIFY(editor->toPlainText().contains("[7:0]"));
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
        auto* behaviorsButton = editor->findChild<QToolButton*>("templateRegion_" + behaviors);
        QVERIFY(behaviorsButton && behaviorsButton->isVisible());
        QTest::mouseClick(behaviorsButton, Qt::LeftButton);
        QCOMPARE(editor->templateContainerContent(), "behaviors");
        QVERIFY(editor->templateSelectionEditable());
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
        QVERIFY(!editor->templateInputEditing()); QCOMPARE(editor->selectedTemplateId(), clockId);
        QTest::keyClick(editor, Qt::Key_Return); QVERIFY(editor->templateInputEditing());
        QTest::keyClick(editor, Qt::Key_Tab); keys("156.25");
        QVERIFY(editor->toPlainText().contains("frequency_mhz = 156.25;"));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->templateInputEditing());
        QTest::mouseClick(behaviorsButton, Qt::LeftButton);
        QCOMPARE(editor->templateContainerContent(), "behaviors");
        keys(resetCommand); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 5, 20000);
        QTest::keyClick(editor, Qt::Key_Down); QTest::keyClick(editor, Qt::Key_Tab); keys("12.5");
        QTest::keyClick(editor, Qt::Key_Tab); keys("0");
        QVERIFY(editor->toPlainText().contains("hold_ns = 12.5;"));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(editor->templateContainerContent(), "behaviors");
        const auto beforeFormat = editor->toPlainText();
        const auto beforeFormatMetadata = editor->templateModel()->serialize(dir.filePath("tb.sv"));
        QTest::keyClick(editor, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(editor->toPlainText() != beforeFormat); QCOMPARE(editor->templateModel()->instances().size(), 5);
        QVERIFY(editor->templateAuthoringLocked()); QVERIFY(!editor->templateInputEditing());
        const auto afterFormat = editor->toPlainText();
        editor->undo(); QCOMPARE(editor->toPlainText(), beforeFormat);
        QCOMPARE(editor->templateModel()->serialize(dir.filePath("tb.sv")), beforeFormatMetadata);
        editor->redo(); QCOMPARE(editor->toPlainText(), afterFormat);
        QVERIFY(editor->editTemplateInstance(clockId, "freq_mhz"));
        QTest::keyClick(editor, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(editor->toPlainText(), afterFormat); QVERIFY(editor->templateInputEditing());
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
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
