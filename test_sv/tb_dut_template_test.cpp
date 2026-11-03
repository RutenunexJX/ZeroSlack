#include "applicationthememanager.h"
#include "editorinsertpaletteservice.h"
#include "mycodeeditor.h"
#include "testuistyle.h"
#include "usertemplateservice.h"
#include "slangmanager.h"
#include "templatecapabilities.h"
#include "editorfileidentity.h"
#include "workspacesessionstateservice.h"
#include "formatterservice.h"
#include "formattercursoranchor.h"
#include "shareddocument.h"
#include "annotationlayer.h"
#include <algorithm>
#include <cstdio>
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
    const auto requested = qEnvironmentVariable("SIMDOCK_TEST_ARTIFACT_DIR");
    const auto dir = requested.isEmpty() ? qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR") : requested;
    if (!dir.isEmpty()) put(QDir(dir).filePath(name), data);
}
class SaveObserver : public QObject {
public:
    explicit SaveObserver(MainWindow& owner) : window(owner) {
        clock.start();
        connect(window.tabManager.get(), &TabManager::fileSaveFailed, this,
            [this](const QString& file, const QString& reason) { record("fileSaveFailed", file, reason); });
        connect(window.tabManager.get(), &TabManager::fileSaved, this,
            [this](const QString& file) { record("fileSaved", file); });
    }
    ~SaveObserver() override {
        record("scope-exit");
        evidence(QString("save-%1-%2.json").arg(QTest::currentTestFunction(), QTest::currentDataTag()),
                 QJsonDocument(events).toJson());
    }
    void beforeSave() { ++attempt; record("before-save"); }
private:
    void record(const char* phase, const QString& file = {}, const QString& reason = {}) {
        auto* editor = window.tabManager->getCurrentEditor();
        auto* document = window.tabManager->sharedDocumentForEditor(editor);
        const QJsonObject event{{"phase", QString::fromLatin1(phase)},
            {"test", QString::fromLatin1(QTest::currentTestFunction())},
            {"data", QString::fromLatin1(QTest::currentDataTag())}, {"attempt", attempt},
            {"microseconds", clock.nsecsElapsed()/1000}, {"testFailed", QTest::currentTestFailed()},
            {"editorExists", editor != nullptr}, {"documentExists", document != nullptr},
            {"documentFile", document ? document->fileName() : QString()},
            {"documentDirty", document && document->dirty()},
            {"externalState", document ? int(document->externalState()) : -1},
            {"file", file}, {"reason", reason}};
        events.append(event);
        std::printf("TB_SAVE_TRACE %s\n", QJsonDocument(event).toJson(QJsonDocument::Compact).constData());
        std::fflush(stdout);
    }
    MainWindow& window;
    QElapsedTimer clock;
    QJsonArray events;
    int attempt = 0;
};
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
UserTemplateRecord containerRecord(const QString& token) {
    for (const auto& record : UserTemplateService::simulationBuiltins())
        if (record.commandToken == "@" + token) return record;
    return {};
}
CodeTemplateSlot inputSlot(const TemplateInstance& item, const QString& name) {
    for (const auto& slot : item.inputs) if (slot.name == name) return slot;
    return {};
}
void place(MyCodeEditor& editor, int position) {
    QTextCursor cursor(editor.document()); cursor.setPosition(position); editor.setTextCursor(cursor);
}
QString insertContainer(MyCodeEditor& editor, const QString& token,
                        const QString& parent = {}, const QString& input = {}) {
    const auto record = containerRecord(token);
    TemplateInsertionSpec spec; spec.label = record.label; spec.snapshot = record.command;
    spec.fragments = {{0, int(record.insertText.size()), false}};
    QString error;
    if (!editor.insertSimulationTemplate(record.insertText, record.templateSlots, spec, parent, input, &error))
        qFatal("%s", qPrintable(error));
    return editor.selectedTemplateId();
}
QString installContainer(MyCodeEditor& editor, const QString& token = "process") {
    editor.setPlainText("module tb;\n\nendmodule\n");
    place(editor, editor.toPlainText().indexOf("endmodule"));
    return insertContainer(editor, token);
}
QByteArray handwrittenTiming(bool dual) {
    return QByteArray("module tb; timeunit 1ns; timeprecision 1ps;\n"
        "logic u_core_a, u_core_b, u_core_c, u_core_d;\n"
        "logic [7:0] u_core_data_in, u_core_data_out, other_out;\n"
        "core u_core(.a(u_core_a), .b(u_core_b), .data_in(u_core_data_in), .data_out(u_core_data_out)")
        + (dual ? ", .c(u_core_c), .d(u_core_d), .other_out(other_out)" : "")
        + ");\n\nendmodule\n";
}
SlangWorkspaceAnalysis analyzeSource(const QString& file, const QString& source, const QString& top = "tb") {
    SemanticAnalysisRequest request; request.sourceOverrides[file] = source;
    SemanticInputCapture capture(request); SlangManager slang;
    return slang.analyzeCapturedWorkspace(capture, {file}, {}, {}, top, {}, {true, true, false});
}
void compileTimingSources(MainWindow& window, const QString& directory) {
    auto* build = window.simulationWorkbench();
    QVERIFY(build);
    const auto source = QDir(directory).filePath("dut.sv");
    const auto top = QDir(directory).filePath("tb.sv");
    const auto projectIssue = build->addDesignProject(top, "tb");
    QVERIFY2(projectIssue.isEmpty(), qPrintable(projectIssue));
    const auto inputIssue = build->adoptHdlInputs(build->activeProjectId(), {source, top}, {});
    QVERIFY2(inputIssue.isEmpty(), qPrintable(inputIssue));
    QTRY_VERIFY_WITH_TIMEOUT(build->activeCompilationInputs().contains(source)
        && build->activeCompilationInputs().contains(top), 15000);
}
void searchTemplates(MainWindow& window, MyCodeEditor* editor, const QString& query) {
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    window.globalControlCoordinator->open();
    auto* panel = window.globalControlCoordinator->panel.get();
    QTRY_VERIFY(panel->isVisible());
    panel->setCategory(GlobalControlCategory::Templates);
    panel->resetQuery(query);
    window.globalControlCoordinator->refreshVisibleItems();
    panel->focusSearch(false);
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
        QTemporaryDir dir; const auto path = dir.filePath("tb.sv"); put(path, handwrittenTiming(true));
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
        MainWindow window; SaveObserver saveObserver(window); window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        compileTimingSources(window, dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] { for (auto* view : window.tabManager->openEditors()) view->document()->setModified(false); });
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        
        place(*editor, editor->toPlainText().indexOf("endmodule"));
        searchTemplates(window, editor, token); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 1, 15000);
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
            const auto slot = inputSlot(*item, signalName);
            QHash<QString, QString> bindings;
            for (const auto& input : item->inputs)
                bindings[input.name] = editor->toPlainText().mid(input.start, input.length);
            return TemplateInputService::candidates(slot.input, *slot.inputContext, bindings);
        };
        QJsonArray trace; MyCodeEditor* secondary = nullptr;
        const auto record = [&](const QString& phase) {
            const auto slot = inputSlot(*editor->templateModel()->instance(child), signalName);
            const auto choices = candidates();
            trace << QJsonObject{{"phase",phase},{"module",slot.inputContext->location.moduleName},
                {"line",slot.inputContext->location.cursorLine},{"position",slot.inputContext->location.cursorPosition},
                {"cursor",editor->textCursor().position()},{"fresh",fresh()},
                {"editing",editor->templateInputEditing()},{"otherEditing",secondary && secondary->templateInputEditing()},
                {"undoSteps",editor->document()->availableUndoSteps()},
                {"values",QJsonArray::fromStringList(choices.values)},{"reason",choices.reason}};
            evidence(QString("live-format-%1.json").arg(QTest::currentDataTag()), QJsonDocument(trace).toJson());
        };
        saveObserver.beforeSave();
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("before");
        QCOMPARE(candidates().values, expected);
        QTest::keyClick(editor, Qt::Key_Down);
        QTest::keyClick(editor, Qt::Key_Tab); QTest::keyClicks(editor, token == "clk_init" ? "156.25" : "12.5");
        if (!numeric) QTest::keyClick(editor, Qt::Key_Backtab);
        auto* shared = qobject_cast<SharedDocument*>(editor->document()->parent()); QVERIFY(shared);
        MyCodeEditor other;
        secondary = &other;
        shared->attachView(&other); 
        auto detach = qScopeGuard([&] { shared->detachView(&other); });
        QVERIFY(other.editTemplateInstance(child, numeric ? signalName : numberName));
        const auto oldText = editor->toPlainText();
        const auto oldMetadata = editor->templateModel()->serialize(path);
        const auto selection = editor->textCursor().selectedText(), otherSelection = other.textCursor().selectedText();
        const auto report = editor->formatDocument();
        QVERIFY2(report.accepted(), qPrintable(report.diagnostic)); QVERIFY(report.changed);
        const auto formatted = editor->toPlainText();
        const auto formattedMetadata = editor->templateModel()->serialize(path);
        QVERIFY(editor->templateInputEditing());
        QVERIFY(other.templateInputEditing());
        QCOMPARE(editor->selectedTemplateId(), child); QCOMPARE(other.selectedTemplateId(), child);
        QCOMPARE(editor->templateSlotModeActiveIndex(), numeric ? 1 : 0);
        QCOMPARE(other.templateSlotModeActiveIndex(), numeric ? 0 : 1);
        QCOMPARE(editor->textCursor().selectedText(), selection); QCOMPARE(other.textCursor().selectedText(), otherSelection);
        saveObserver.beforeSave();
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
        saveObserver.beforeSave();
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("undo-current-analysis"); QCOMPARE(candidates().values, expected);
        editor->redo(); QCOMPARE(editor->toPlainText(), formatted);
        QCOMPARE(editor->templateModel()->serialize(path), formattedMetadata);
        QVERIFY(editor->templateInputEditing() && other.templateInputEditing());
        saveObserver.beforeSave();
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
        const auto number = inputSlot(*editor->templateModel()->instance(child), numberName);
        QCOMPARE(editor->toPlainText().mid(number.start, number.length).trimmed(), token == "clk_init" ? "125.5" : "20.5");
        saveObserver.beforeSave();
        QVERIFY(window.tabManager->saveCurrentTab()); QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("changed-inputs"); QCOMPARE(candidates().values, expected);
        shared->detachView(&other); detach.dismiss();
        const auto savedText = editor->toPlainText();
        QVERIFY(window.tabManager->closeTabsInWorkspace(dir.path()));
        QVERIFY(window.tabManager->openFileInTab(path)); editor = window.tabManager->getCurrentEditor();
        QCOMPARE(editor->toPlainText(), savedText);
        
        QVERIFY(editor->editTemplateInstance(child, signalName));
        QTRY_VERIFY_WITH_TIMEOUT(fresh(), 15000);
        record("reopened"); QCOMPARE(candidates().values, expected);
        QTest::keyClick(editor, Qt::Key_Down);
        QVERIFY(expected.contains(editor->textCursor().selectedText()));
        // A pending edit in a real DUT must invalidate proof until fresh analysis.
        saveObserver.beforeSave();
        QVERIFY(window.tabManager->saveCurrentTab());
        QVERIFY(window.tabManager->openFileInTab(dir.filePath("dut.sv")));
        const auto changedDut = source.replace("posedge a", "posedge b").replace("negedge b", "negedge a")
            .replace("if (!b)", "if (!a)").replace("posedge c", "posedge d").replace("negedge d", "negedge c").replace("if (!d)", "if (!c)");
        window.tabManager->getCurrentEditor()->setPlainText(QString::fromUtf8(changedDut));
        record("dut-source-changed");
        QVERIFY(candidates().values.isEmpty());
        const auto retained = editor->toPlainText();
        QTest::keyClick(editor, Qt::Key_Down); QCOMPARE(editor->toPlainText(), retained);
        saveObserver.beforeSave();
        QVERIFY(window.tabManager->saveCurrentTab());
        QTRY_VERIFY_WITH_TIMEOUT(fresh() && SemanticIndex::getInstance()->snapshot()->getCachedFileContent(dir.filePath("dut.sv"))
            == QString::fromUtf8(changedDut), 15000);
        record("dut-reanalyzed");
        QCOMPARE(candidates().values, token == "clk_init" ? QStringList({"u_core_b","u_core_d"}) : QStringList({"u_core_a","u_core_c"}));
        QVERIFY(!editor->templateModel()->validationIssue(child).isEmpty());
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->templateInputEditing()); 
    }
    void incompleteInputNavigation_data() {
        QTest::addColumn<QString>("token");
        QTest::newRow("clock-invalid-value") << QString("clk_init");
        QTest::newRow("reset-invalid-value") << QString("reset");
    }
    void incompleteInputNavigation() {
        QFETCH(QString, token);
        QTemporaryDir dir; const auto path = dir.filePath("tb.sv"); put(path, handwrittenTiming(false));
        put(dir.filePath("dut.sv"), timingDut);
        auto* catalog = UserTemplateService::getInstance(); const auto original = catalog->globalTemplateLocation();
        const auto restore = qScopeGuard([&] { catalog->setGlobalTemplateFilePath(original); });
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        catalog->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; SaveObserver saveObserver(window); window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        compileTimingSources(window, dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] { for (auto* view : window.tabManager->openEditors()) view->document()->setModified(false); });
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        
        place(*editor, editor->toPlainText().indexOf("endmodule"));
        QStringList messages;
        connect(editor, &MyCodeEditor::editorStatusMessageRequested, &window, [&](const QString& text) { messages << text; });
        searchTemplates(window, editor, token); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 1, 15000);
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
        const auto initial = editor->toPlainText();
        QTest::keyClick(editor, Qt::Key_Tab);
        const int afterTab = editor->templateSlotModeActiveIndex();
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        const bool afterReturn = editor->templateInputEditing();
        evidence("incomplete-" + token + ".json", QJsonDocument(QJsonObject{
            {"afterTab",afterTab},{"editingAfterReturn",afterReturn},{"messages",QJsonArray::fromStringList(messages)},
            {"source",editor->toPlainText()},{"metadata",editor->templateModel()->serialize(path)}}).toJson());
        QCOMPARE(afterTab, token == "clk_init" ? 0 : 2); QVERIFY(!afterReturn);
        QCOMPARE(editor->toPlainText(), initial);
        QCOMPARE(editor->selectedTemplateId(), child);
        QVERIFY(activeVisuals().isEmpty());
        QVERIFY(editor->templateInputFeedback().contains("Unfinished"));
        QVERIFY(editor->editTemplateInstance(child));
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
            QVERIFY(!editor->templateInputEditing()); 
            QCOMPARE(editor->toPlainText(), retained);
            QVERIFY(editor->templateInputFeedback().contains("Unfinished"));
        }
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(editor->templateModel()->instance(child));
        saveObserver.beforeSave();
        QVERIFY(window.tabManager->saveCurrentTab());
    }
    void formatPreservesTemplateIdentity_data() {
        QTest::addColumn<bool>("editing");
        QTest::newRow("ordinary") << false;
        QTest::newRow("input-editing") << true;
    }
    void formatPreservesTemplateIdentity() {
        QFETCH(bool, editing);
        MyCodeEditor editor; editor.show(); editor.setFocus();
        const auto root = installContainer(editor);
        QVERIFY(editor.replaceTemplateInput(editor.textCursor(), "if (1) begin\n$display(1);\nend\n"));
        if (editing) QVERIFY(editor.editTemplateInstance(root, "body"));
        else if (editor.templateInputEditing()) QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
        const auto before = editor.toPlainText();
        const auto report = editor.formatDocument();
        evidence(QString("format-%1.json").arg(QTest::currentDataTag()), QJsonDocument(QJsonObject{
            {"accepted",report.accepted()},{"changed",report.changed},{"diagnostic",report.diagnostic},
            {"before",before},{"after",editor.toPlainText()},{"instances",editor.templateModel()->instances().size()}}).toJson());
        QVERIFY2(report.accepted(), qPrintable(report.diagnostic)); QVERIFY(report.changed);
        QVERIFY(editor.templateModel()->instance(root));
        QCOMPARE(editor.templateInputEditing(), editing);
        const auto formatted = editor.toPlainText();
        editor.undo(); QCOMPARE(editor.toPlainText(), before); QVERIFY(editor.templateModel()->instance(root));
        QCOMPARE(editor.templateInputEditing(), editing);
        editor.redo(); QCOMPARE(editor.toPlainText(), formatted); QVERIFY(editor.templateModel()->instance(root));
        QCOMPARE(editor.templateInputEditing(), editing);
        QVERIFY(!editor.formatDocument().changed);

    }
    void formatReadOnlyAndAtomicRejection() {
        MyCodeEditor editor; const auto root = installContainer(editor);
        editor.setReadOnly(true); 
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
        QTemporaryDir dir; const auto path = dir.filePath("shared.sv");
        MyCodeEditor fixture;
        const auto process = installContainer(fixture);
        auto slot = inputSlot(*fixture.templateModel()->instance(process), "body");
        QVERIFY(fixture.editTemplateInstance(process, "body", slot.start));
        const auto root = insertContainer(fixture, "repeat", process, "body");
        slot = inputSlot(*fixture.templateModel()->instance(root), "body");
        QVERIFY(fixture.editTemplateInstance(root, "body", slot.start));
        insertContainer(fixture, "repeat", root, "body");
        fixture.finishTemplateInput();
        const auto metadata = fixture.templateModel()->serialize(path);
        SharedDocument shared("format-saved", path, fixture.toPlainText());
        MyCodeEditor editor, other;
        shared.attachView(&editor); shared.attachView(&other);
        const auto detach = qScopeGuard([&] { shared.detachView(&other); shared.detachView(&editor); });
        QVERIFY(editor.templateModel()->restore(metadata, path));
        QVERIFY(!root.isEmpty()); QCOMPARE(editor.templateModel()->instances().size(), 3);
        
        const auto body = inputSlot(*editor.templateModel()->instance(root), "body");
        QVERIFY(editor.editTemplateInstance(root, "body", body.start + body.length));
        const auto child = insertContainer(editor, "repeat", root, "body");
        QVERIFY(editor.editTemplateInstance(child, "body"));
        QVERIFY(editor.replaceTemplateInput(editor.textCursor(), "  // handwritten\n  $display(\"keep\");\n"));
        QVERIFY(other.editTemplateInstance(root, "count"));
        const auto oldSource = editor.toPlainText(); const auto oldMetadata = editor.templateModel()->serialize(path);
        const int oldPosition = editor.textCursor().position(), otherAnchor = other.textCursor().anchor();
        const auto otherSelection = other.textCursor().selectedText();
        const auto report = editor.formatDocument();
        QVERIFY2(report.accepted(), qPrintable(report.diagnostic)); QVERIFY(report.changed);
        QCOMPARE(editor.templateModel()->instances().size(), 4);
        QCOMPARE(editor.selectedTemplateId(), child); QCOMPARE(editor.templateSlotModeActiveIndex(), 1);
        QVERIFY(editor.templateInputEditing()); QVERIFY(other.templateInputEditing());
        QCOMPARE(other.textCursor().selectedText(), otherSelection);
        const auto formatted = editor.toPlainText(); const auto formattedMetadata = editor.templateModel()->serialize(path);
        for (const auto& oldItem : oldMetadata["instances"].toArray()) {
            const auto item = oldItem.toObject(); const auto* now = editor.templateModel()->instance(item["id"].toString());
            QVERIFY(now); QCOMPARE(now->parentId, item["parent"].toString()); QCOMPARE(now->parentSlot, item["parentSlot"].toString());
            QCOMPARE(now->fragments.size(), item["fragments"].toArray().size());
        }
        other.undo(); QCOMPARE(editor.toPlainText(), oldSource); QCOMPARE(editor.templateModel()->serialize(path), oldMetadata);
        QVERIFY(editor.templateInputEditing()); QVERIFY(other.templateInputEditing());
        QCOMPARE(editor.textCursor().position(), oldPosition); QCOMPARE(other.textCursor().anchor(), otherAnchor);
        editor.redo(); QCOMPARE(editor.toPlainText(), formatted); QCOMPARE(editor.templateModel()->serialize(path), formattedMetadata);
        QVERIFY(editor.templateInputEditing()); QVERIFY(other.templateInputEditing());
        MyCodeEditor reopened; reopened.setPlainText(formatted);
        QVERIFY(reopened.templateModel()->restore(formattedMetadata, path));
        QCOMPARE(reopened.templateModel()->instances().size(), 4);
        QVERIFY(!editor.formatDocument().changed);
        evidence("formatted-saved-shared.sv", formatted.toUtf8());
        evidence("formatted-saved-shared.json", QJsonDocument(formattedMetadata).toJson());
    }
    void searchCancellationPreservesCodeInput_data() {
        QTest::addColumn<QString>("area"); QTest::addColumn<QString>("scenario");
        for (const auto& name : {"definitions", "process", "repeat"})
            QTest::newRow(qPrintable(QString("empty-%1").arg(name))) << QString(name) << QString("empty");
        for (const auto& name : {"nonempty", "selection", "reverse-selection", "nested-empty",
                 "nested-selection", "repeat", "native-event", "changed-source", "changed-cursor", "saved-copy"})
            QTest::newRow(name) << QString("process") << QString(name);
    }
    void searchCancellationPreservesCodeInput() {
        QFETCH(QString, area); QFETCH(QString, scenario);
        QTemporaryDir dir;
        const auto path = dir.filePath("tb.sv");
        put(path, {});
        if (scenario == "saved-copy") {
            MyCodeEditor fixture; const auto parent = installContainer(fixture);
            const auto slot = inputSlot(*fixture.templateModel()->instance(parent), "body");
            QVERIFY(fixture.editTemplateInstance(parent, "body", slot.start));
            insertContainer(fixture, "repeat", parent, "body"); fixture.finishTemplateInput();
            put(path, fixture.toPlainText().toUtf8());
            QString error;
            QVERIFY2(WorkspaceSessionStateService().saveDocumentTemplates(path, fixture.templateModel()->serialize(path), &error), qPrintable(error));
        }
        auto* service = UserTemplateService::getInstance();
        const auto original = service->globalTemplateLocation();
        const auto restoreCatalog = qScopeGuard([&] { service->setGlobalTemplateFilePath(original); });
        QVERIFY(QFile::copy(QString::fromUtf8(TB_TEMPLATE_FILE), dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        MainWindow window; SaveObserver saveObserver(window); window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] {
            for (auto* view : window.tabManager->openEditors()) view->document()->setModified(false);
        });
        QString owner;
        if (scenario == "saved-copy") {
            QCOMPARE(editor->templateModel()->instances().size(), 2);
            for (const auto& item : editor->templateModel()->instances()) if (item.parentId.isEmpty()) owner = item.id;
        } else owner = installContainer(*editor, area == "definitions" ? "definitions" : "process");
        QVERIFY(!owner.isEmpty()); 
        if (scenario.startsWith("nested-") || area == "repeat") {
            const auto slot = inputSlot(*editor->templateModel()->instance(owner), "body");
            QVERIFY(editor->editTemplateInstance(owner, "body", slot.start));
            owner = insertContainer(*editor, "repeat", owner, "body");
        }
        area = "body";
        editor->finishTemplateInput(); QVERIFY(!editor->templateInputEditing());
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        const auto slot = inputSlot(*editor->templateModel()->instance(owner), area);
        QVERIFY(editor->editTemplateInstance(owner, area, slot.start + slot.length));
        QVERIFY(editor->templateInputEditing());
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
                {"owner",editor->selectedTemplateId()},{"input",area},
                {"search",panel->isVisible()},
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
            searchTemplates(window, editor, {}); QTRY_VERIFY(panel->isVisible()); observe("search-open");
            QCOMPARE(editor->toPlainText(), expected);
            QCOMPARE(editor->textCursor().position(), expectedCursor.position());
            QCOMPARE(editor->textCursor().anchor(), expectedCursor.anchor());
            if (scenario == "changed-source") {
                // Simulate a shared-document edit while the popup owns focus.
                const int start = inputSlot(*editor->templateModel()->instance(owner), area).start;
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
            QVERIFY(editor->templateInputEditing());
        }
        const auto inserted = scenario == "native-event" ? QString("@(posedge clk)") : QString("// comment");
        expected.replace(expectedCursor.selectionStart(), expectedCursor.selectionEnd() - expectedCursor.selectionStart(), inserted);
        if (scenario == "native-event") keys(inserted);
        else { QTest::keyClick(editor, Qt::Key_Slash); keys("/ comment"); }
        observe("continued-input");
        QCOMPARE(editor->toPlainText(), expected);
        QCOMPARE(editor->selectedTemplateId(), owner);
        QVERIFY(!panel->isVisible());
        const auto metadata = editor->templateModel()->serialize(path);
        // The input API rejects edits outside its Code segment; ordinary source
        // editing is covered separately by fixedTextInvalidation.
        const int fixed = editor->toPlainText().lastIndexOf("endmodule");
        QTextCursor outside(editor->document()); outside.setPosition(fixed + 3);
        QVERIFY(!editor->replaceTemplateInput(outside, "x"));
        QCOMPARE(editor->toPlainText(), expected);
        QTextCursor crossing(editor->document()); crossing.setPosition(fixed - 1);
        crossing.setPosition(fixed + 3, QTextCursor::KeepAnchor);
        QVERIFY(!editor->replaceTemplateInput(crossing, {}));
        QCOMPARE(editor->toPlainText(), expected);
        QCOMPARE(editor->templateModel()->serialize(path), metadata);
        observe("input-api-boundaries");
        saveObserver.beforeSave();
        QVERIFY(window.tabManager->saveCurrentTab());
        QVERIFY(window.tabManager->closeTabsInWorkspace(dir.path()));
        QVERIFY(window.tabManager->openFileInTab(path)); editor = window.tabManager->getCurrentEditor();
        QCOMPARE(editor->toPlainText(), expected); QCOMPARE(editor->templateModel()->serialize(path), metadata);
        observe("saved-and-reopened");
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
        QTemporaryDir dir; const auto path = dir.filePath("parameters.sv");
        const auto prefix = QString::fromUtf8(dut) +
            "module old_tb; timeunit 1ns; timeprecision 1ps; core #(.W(4)) old_dut(); endmodule\n";
        const auto source = [&](const QString& parameters) {
            return prefix + "module tb; timeunit 1ns; timeprecision 1ps; core " + parameters + " unit(); endmodule\n";
        };
        for (const int width : {8, 12}) {
            const auto analysis = analyzeSource(path, source(width == 8 ? QString() : QString("#(.W(12))")));
            QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error));
            SemanticElaboratedSymbolInfo parameter, port;
            for (const auto& record : analysis.symbols) {
                if (record.owner.name != "core") continue;
                if (record.name == "W") parameter = record.presentation.instanceInfoByPath.value("tb.unit");
                if (record.name == "data_in") port = record.presentation.instanceInfoByPath.value("tb.unit");
            }
            QVERIFY(parameter.available); QVERIFY(port.available);
            QCOMPARE(EffectiveValueService::evaluateLiteral(parameter.valueText).numericValueText, QString::number(width));
            QCOMPARE(port.bitWidth, std::uint64_t(width));
        }
        const auto invalid = analyzeSource(path, source("#(.missing(1))"));
        QVERIFY(std::any_of(invalid.diagnostics.begin(), invalid.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == SemanticDiagnostic::Error && diagnostic.message.contains("missing");
        }));
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
        QTemporaryDir dir; const auto path = dir.filePath("tb.sv");
        const auto analysis = analyzeSource(path, QString::fromUtf8(dut) +
            "module tb; timeunit 1ns; timeprecision 1ps; core #(.W(12)) unit(); endmodule\n");
        QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error));
        QList<SemanticSymbolRecord> ports;
        for (const auto& record : analysis.symbols)
            if (record.owner.name == "core" && record.declarationKind == SymbolTaxonomy::DeclarationKind::Port) ports << record;
        QCOMPARE(ports.size(), 5);
        for (const auto& port : ports) {
            const auto info = port.presentation.instanceInfoByPath.value("tb.unit");
            QVERIFY(info.available);
            if (port.name == "data_in") QCOMPARE(info.bitWidth, std::uint64_t(12));
            if (port.name == "pad") QCOMPARE(info.portDirection, QString("inout"));
            if (port.name == "words") QCOMPARE(info.unpackedRank, 1);
        }
        MyCodeEditor editor; editor.setDocumentFileName(path);
        const auto parent = installContainer(editor, "definitions"); editor.finishTemplateInput();
        const auto baseline = editor.toPlainText();
        const auto declaration = [&](const QString& name, int width, const QString& replace = QString()) {
            TemplateDocumentEdit edit;
            const auto body = inputSlot(*editor.templateModel()->instance(parent), "body");
            edit.start = body.start + body.length; edit.parentId = parent; edit.parentSlot = "body"; edit.replaceId = replace;
            if (const auto* old = editor.templateModel()->instance(replace)) {
                edit.start = old->fragments.first().start; edit.length = old->fragments.first().length;
            }
            edit.text = QString("logic [%1:0] %2;\n").arg(width - 1).arg(name);
            edit.instance.label = "@declaration"; edit.instance.snapshot = {{"kind","snippet"},{"content","declarations"}};
            edit.instance.fragments = {{0, int(edit.text.size()), false}};
            return edit;
        };
        QString error;
        QVERIFY2(editor.applyTemplateEdits({declaration("first",12)}, &error), qPrintable(error));
        const auto firstId = editor.selectedTemplateId(); const auto first = editor.toPlainText();
        QVERIFY(first.contains("[11:0] first;")); QVERIFY(editor.templateModel()->issue().isEmpty());
        MyCodeEditor second; second.setDocument(editor.document());
        QCOMPARE(second.templateModel(), editor.templateModel());
        editor.undo(); QCOMPARE(editor.toPlainText(), baseline); QCOMPARE(editor.templateModel()->instances().size(), 1);
        editor.redo(); QCOMPARE(second.toPlainText(), first); QVERIFY(editor.templateModel()->instance(firstId));
        auto stored = editor.templateModel()->serialize(path); QTextDocument restored; restored.setPlainText(first);
        QVERIFY(DocumentTemplateModel::get(&restored)->restore(stored, path));
        QCOMPARE(DocumentTemplateModel::get(&restored)->instances().size(), 2);
        QVERIFY2(editor.applyTemplateEdits({declaration("second",12)}, &error), qPrintable(error));
        const auto secondId = editor.selectedTemplateId();
        QVERIFY(editor.toPlainText().contains("[11:0] second;")); QVERIFY(editor.templateModel()->instance(firstId));
        QVERIFY2(editor.applyTemplateEdits({declaration("renamed",16,firstId)}, &error), qPrintable(error));
        QVERIFY(editor.toPlainText().contains("[15:0] renamed;")); QVERIFY(editor.toPlainText().contains("[11:0] second;"));
        QVERIFY(editor.templateModel()->instance(secondId));
        const auto beforeRejected = editor.toPlainText(); const auto metadataBeforeRejected = editor.templateModel()->serialize(path);
        auto invalid = declaration("invalid",8); invalid.parentId = "missing-parent";
        QVERIFY(!editor.applyTemplateEdits({declaration("must_not_appear",8),invalid}, &error));
        QCOMPARE(editor.toPlainText(), beforeRejected); QCOMPARE(editor.templateModel()->serialize(path), metadataBeforeRejected);
        const auto instance = *editor.templateModel()->instance(firstId);
        TemplateDocumentEdit remove; remove.start = instance.fragments.first().start; remove.length = instance.fragments.first().length;
        remove.parentId = parent; remove.parentSlot = "body"; remove.replaceId = firstId;
        QVERIFY(editor.applyTemplateEdits({remove}, &error));
        QVERIFY(!editor.toPlainText().contains("renamed;")); QVERIFY(editor.templateModel()->instance(secondId));
        editor.undo(); QVERIFY(editor.templateModel()->instance(firstId));
        stored = editor.templateModel()->serialize(path); restored.setPlainText(editor.toPlainText());
        QVERIFY(DocumentTemplateModel::get(&restored)->restore(stored, path));
        evidence("shared-transaction.sv", editor.toPlainText().toUtf8());
        evidence("shared-transaction-metadata.json", QJsonDocument(stored).toJson());
    }
    void typeAndValueBoundaries() {
        QTemporaryDir dir; const auto file = dir.filePath("types.sv");
        const auto source = QString(R"(package types;
 typedef enum logic [2:0] {IDLE=3'b001, BUSY=3'b101} state_t;
 typedef struct packed {logic [2:0] tag; logic [4:0] value;} payload_t;
endpackage
module core(input types::state_t mode, input types::payload_t data, output logic [7:0] q);
 assign q = data;
endmodule
module tb;
 types::state_t mode; types::payload_t data; logic [7:0] q;
 core unit(.mode(mode),.data(data),.q(q));
endmodule
)");
        const auto analysis = analyzeSource(file, source);
        QVERIFY2(analysis.error.isEmpty(), qPrintable(analysis.error));
        QJsonArray typeProofs;
        for (const auto& name : {"mode", "data"}) {
            SemanticDeclaredTypeFacts shape; SemanticElaboratedSymbolInfo info;
            for (const auto& record : analysis.symbols) if (record.owner.name == "core" && record.name == name) {
                shape = record.presentation.declaredTypeFactsByPath.value("tb.unit");
                info = record.presentation.instanceInfoByPath.value("tb.unit");
            }
            QVERIFY(shape.complete); QVERIFY(info.available);
            QCOMPARE(shape.externalDeclarationBaseText, QString(name) == "mode" ? QString("types::state_t") : QString("types::payload_t"));
            QCOMPARE(info.bitWidth, std::uint64_t(QString(name) == "mode" ? 3 : 8));
            typeProofs << QJsonObject{{"port",name},{"type",shape.externalDeclarationBaseText},{"width",double(info.bitWidth)}};
        }
        evidence("typed-port-facts.json", QJsonDocument(typeProofs).toJson());
        const auto interfaceAnalysis = analyzeSource(file,
            "interface link; logic a; endinterface\nmodule core(link p); endmodule\nmodule tb; link bus(); core unit(bus); endmodule\n");
        SemanticElaboratedSymbolInfo interfacePort;
        for (const auto& record : interfaceAnalysis.symbols)
            if (record.owner.name == "core" && record.name == "p") interfacePort = record.presentation.instanceInfoByPath.value("tb.unit");
        QVERIFY(interfacePort.interfaceType); QVERIFY(!interfacePort.fixedSize);
        const auto typeParameter = analyzeSource(file,
            "module core #(parameter type T=logic)(input T a); endmodule\nmodule tb; core unit(); endmodule\n");
        QString generationIssue;
        for (const auto& record : typeParameter.symbols)
            if (record.name == "core" && record.declarationKind == SymbolTaxonomy::DeclarationKind::Module)
                generationIssue = record.presentation.tbGenerationIssue;
        QVERIFY(generationIssue.contains("Type parameters"));
        QVariantMap frequency{{"kind","Value"},{"finite",true},{"minimum","0"},{"exclusiveMinimum",true}};
        QVERIFY(TemplateInputService::validate(frequency,"156.25",{},{}).isEmpty());
        for (const auto &value : {"0", "-1", "1e309", "NaN"}) QVERIFY(!TemplateInputService::validate(frequency,value,{},{}).isEmpty());
        frequency.remove("exclusiveMinimum");
        QVERIFY(TemplateInputService::validate(frequency,"12.5",{},{}).isEmpty());
        QVERIFY(TemplateInputService::validate(frequency,"0",{},{}).isEmpty());
        const QVariantMap polarity{{"kind","Choice"},{"choices",QStringList{"0","1"}}};
        QVERIFY(!TemplateInputService::validate(polarity,"2",{},{}).isEmpty());
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
        QTemporaryDir dir; const auto path = dir.filePath("tb.sv");
        put(path, "module tb;\n\nendmodule\n");
        MainWindow window; SaveObserver saveObserver(window); window.show();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] { for (auto* view : window.tabManager->openEditors()) view->document()->setModified(false); });
        window.activateWindow(); editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        place(*editor, editor->toPlainText().indexOf("endmodule"));
        searchTemplates(window, editor, "process");
        auto* panel = window.globalControlCoordinator->panel.get();
        QTRY_VERIFY(!panel->currentItems.isEmpty());
        const auto selected = panel->currentItems.first();
        QCOMPARE(selected.parameters.value("kind").toString(), QString("snippet"));
        QSignalSpy feedback(window.simulationCommands.get(), &SimulationCommandService::message);
        window.simulationCommands->activate(selected);
        QVERIFY(feedback.isEmpty());
        editor->insertPlainText("// changed during preparation\n");
        const auto changed = editor->toPlainText();
        QTRY_VERIFY_WITH_TIMEOUT(!feedback.isEmpty(), 15000);
        QVERIFY(feedback.last().first().toString().contains("changed", Qt::CaseInsensitive));
        QCOMPARE(editor->toPlainText(), changed); QCOMPARE(editor->templateModel()->instances().size(), 0);
        QVERIFY(!editor->toPlainText().contains("initial begin"));
        panel->hide();
    }
    void confirmationAndModeFeedback() {
        QTemporaryDir dir;
        const auto path = dir.filePath("tb.sv");
        put(path, "module tb; int hits;\n\nendmodule\n");
        MainWindow window; SaveObserver saveObserver(window); window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        QVERIFY(window.tabManager->openFileInTab(path));
        auto* editor = window.tabManager->getCurrentEditor();
        const auto discard = qScopeGuard([&] { editor->document()->setModified(false); });
        place(*editor, editor->toPlainText().indexOf("endmodule"));
        const auto original = editor->toPlainText();
        auto* panel = window.globalControlCoordinator->panel.get();
        searchTemplates(window, editor, "process");
        QCOMPARE(panel->currentItems.size(), 1);
        QCOMPARE(editor->toPlainText(), original);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(editor->toPlainText(), original);
        searchTemplates(window, editor, "process"); enter();
        QTRY_COMPARE(editor->templateModel()->instances().size(), 1);
        QVERIFY(editor->templateInputEditing());
        auto* feedback = window.findChild<QLabel*>("templateInputStatus");
        QVERIFY(feedback && feedback->isVisible());
        const auto* stable = feedback;
        keys("hits += 1;");
        QCOMPARE(window.findChild<QLabel*>("templateInputStatus"), stable);
        QVERIFY(feedback->toolTip().contains("Ctrl+Enter"));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->templateInputEditing());
        QVERIFY(!feedback->isVisible());
        QVERIFY(editor->editTemplateInstance(editor->selectedTemplateId(), "body"));
        const auto body = inputSlot(*editor->templateModel()->instance(editor->selectedTemplateId()), "body");
        place(*editor, body.start + body.length);
        QVERIFY(editor->replaceTemplateInput(editor->textCursor(), "\n  "));
        const auto before = editor->toPlainText();
        searchTemplates(window, editor, "definitions");
        QVERIFY(panel->isVisible());
        QVERIFY(panel->availabilityNotice->text().contains("container", Qt::CaseInsensitive));
        QCOMPARE(editor->toPlainText(), before);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        QTRY_VERIFY(editor->hasFocus());
        QVERIFY(editor->templateInputEditing());
    }
    void mainWindowKeyboard() {
        QTemporaryDir dir;
        put(dir.filePath("dut.sv"), timingDut); put(dir.filePath("tb.sv"), handwrittenTiming(false));
        auto *service = UserTemplateService::getInstance(); const auto original = service->globalTemplateLocation();
        const auto catalog = qEnvironmentVariable("TB_TEST_CATALOG", QString::fromUtf8(TB_TEMPLATE_FILE));
        QVERIFY(QFile::copy(catalog, dir.filePath("templates.json")));
        service->setGlobalTemplateFilePath(dir.filePath("templates.json"));
        QString clockCommand, resetCommand;
        for (const auto& record : service->records()) {
            for (const auto& slot : record.templateSlots) {
                if (slot.input.value("role") == "Clock") clockCommand = record.commandToken;
                if (slot.input.value("role") == "Reset") resetCommand = record.commandToken;
            }
        }
        QVERIFY(!clockCommand.isEmpty() && !resetCommand.isEmpty());
        MainWindow window; SaveObserver saveObserver(window); window.resize(1180, 850); window.show(); window.activateWindow();
        window.workspaceSessionCoordinator->setRestoreOnActivation(false);
        window.workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
        window.workspaceSessionCoordinator->openWorkspace(dir.path());
        compileTimingSources(window, dir.path()); window.tabManager->openFileInTab(dir.filePath("tb.sv"));
        auto *editor = window.tabManager->getCurrentEditor(); editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        
        place(*editor, editor->toPlainText().indexOf("endmodule"));
        const auto initial = editor->toPlainText();
        searchTemplates(window, editor, clockCommand);
        QVERIFY(window.globalControlCoordinator->panel->isVisible());
        QCOMPARE(editor->toPlainText(), initial); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 1, 20000);
        QVERIFY(editor->templateInputEditing());
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
        QVERIFY(editor->editTemplateInstance(clockId)); QVERIFY(editor->templateInputEditing());
        QTest::keyClick(editor, Qt::Key_Tab); keys("156.25");
        QVERIFY(editor->toPlainText().contains("frequency_mhz = 156.25;"));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!editor->templateInputEditing());
        place(*editor, editor->toPlainText().indexOf("endmodule"));
        searchTemplates(window, editor, resetCommand); enter();
        QTRY_COMPARE_WITH_TIMEOUT(editor->templateModel()->instances().size(), 2, 20000);
        QTest::keyClick(editor, Qt::Key_Down); QTest::keyClick(editor, Qt::Key_Tab); keys("12.5");
        QTest::keyClick(editor, Qt::Key_Tab); keys("0");
        QVERIFY(editor->toPlainText().contains("hold_ns = 12.5;"));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        const auto beforeFormat = editor->toPlainText();
        const auto beforeFormatMetadata = editor->templateModel()->serialize(dir.filePath("tb.sv"));
        QTest::keyClick(editor, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(editor->toPlainText() != beforeFormat); QCOMPARE(editor->templateModel()->instances().size(), 2);
        QVERIFY(!editor->templateInputEditing());
        const auto afterFormat = editor->toPlainText();
        editor->undo(); QCOMPARE(editor->toPlainText(), beforeFormat);
        QCOMPARE(editor->templateModel()->serialize(dir.filePath("tb.sv")), beforeFormatMetadata);
        editor->redo(); QCOMPARE(editor->toPlainText(), afterFormat);
        QVERIFY(editor->editTemplateInstance(clockId, "freq_mhz"));
        QTest::keyClick(editor, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(editor->toPlainText(), afterFormat); QVERIFY(editor->templateInputEditing());
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        saveObserver.beforeSave();
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
        QCOMPARE(editor->templateModel()->instances().size(), 2); QVERIFY(editor->templateModel()->instance(clockId));
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
