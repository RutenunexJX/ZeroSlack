#include "annotationlayer.h"
#include "editorinsighttargetpickcontroller.h"
#include "editormodecontroller.h"
#include "mycodeeditor.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    std::fflush(stdout);
}

bool sendKey(MyCodeEditor& editor,
             int key,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&editor, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return event.isAccepted();
}

const QString kFileName = QStringLiteral("pick_top.sv");

// A module with ports, signals, a child instance and an always block, then a
// second module far below so "visible region only" can be told apart from
// "whole file".
QString fixtureSource()
{
    QString source = QStringLiteral(
        "module pick_top(input logic clk,\n"          // line 0
        "                input logic rst_n,\n"        // line 1
        "                output logic done);\n"       // line 2
        "    logic state_q;\n"                        // line 3
        "    logic other_q;\n"                        // line 4
        "    pick_child child_inst(.clk(clk));\n"     // line 5
        "    always_ff @(posedge clk) begin\n"        // line 6
        "        state_q <= !state_q;\n"              // line 7
        "    end\n"                                   // line 8
        "endmodule\n");                               // line 9
    for (int filler = 0; filler < 40; ++filler)
        source.append(QStringLiteral("// filler\n"));
    source.append(QStringLiteral(
        "module far_away;\n"
        "    logic hidden_q;\n"
        "endmodule\n"));
    return source;
}

SemanticSymbolRecord makeRecord(const QString& name,
                                SymbolTaxonomy::DeclarationKind kind)
{
    SemanticSymbolRecord record;
    record.name = name;
    record.declarationKind = kind;
    record.location.fileName = kFileName;
    return record;
}

std::shared_ptr<const SemanticIndexSnapshot> fixtureSnapshot()
{
    QList<SemanticSymbolRecord> records = {
        makeRecord(QStringLiteral("clk"),
                   SymbolTaxonomy::DeclarationKind::Port),
        makeRecord(QStringLiteral("rst_n"),
                   SymbolTaxonomy::DeclarationKind::Port),
        makeRecord(QStringLiteral("done"),
                   SymbolTaxonomy::DeclarationKind::Port),
        makeRecord(QStringLiteral("state_q"),
                   SymbolTaxonomy::DeclarationKind::Signal),
        makeRecord(QStringLiteral("other_q"),
                   SymbolTaxonomy::DeclarationKind::Signal),
        makeRecord(QStringLiteral("hidden_q"),
                   SymbolTaxonomy::DeclarationKind::Signal),
        makeRecord(QStringLiteral("pick_top"),
                   SymbolTaxonomy::DeclarationKind::Module),
        makeRecord(QStringLiteral("pick_child"),
                   SymbolTaxonomy::DeclarationKind::Module),
        makeRecord(QStringLiteral("far_away"),
                   SymbolTaxonomy::DeclarationKind::Module),
        makeRecord(QStringLiteral("child_inst"),
                   SymbolTaxonomy::DeclarationKind::Instance),
    };
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(std::move(records)));
}

QStringList candidateNames(const MyCodeEditor& editor)
{
    QStringList names;
    for (const EditorInsightTargetCandidate& candidate :
         editor.insightTargetCandidatesForTest()) {
        names.append(candidate.name);
    }
    return names;
}

struct TargetVisual {
    int start = -1;
    int end = -1;
    bool active = false;
    bool phaseVisible = true;
    bool valid = false;
};

QList<TargetVisual> targetVisuals(const MyCodeEditor& editor)
{
    QList<TargetVisual> result;
    AnnotationLayerQuery query;
    query.firstVisibleLine = 0;
    query.lastVisibleLine = qMax(0, editor.blockCount() - 1);
    query.maxAnnotationsPerLine = 64;
    query.maxLanes = 16;
    const AnnotationLayerReport report =
        editor.annotationLayerReportForTest(query);
    for (const ResolvedEditorAnnotation& resolved : report.annotations) {
        if (resolved.annotation.kind != EditorAnnotationKind::InsightTarget)
            continue;
        result.append(TargetVisual{
            resolved.annotation.range.startPosition,
            resolved.annotation.range.endPosition,
            resolved.annotation.active,
            resolved.annotation.phaseVisible,
            resolved.annotation.isValid(),
        });
    }
    return result;
}

// Publishing is driven by the paint path in the product; the tests drive the
// same entry point with an explicit visible range.
void publish(MyCodeEditor& editor, int firstLine, int lastLine)
{
    editor.publishInsightTargetAnnotationsForTest(firstLine, lastLine);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    const std::shared_ptr<const SemanticIndexSnapshot> previousSnapshot =
        semanticIndex->snapshot();
    semanticIndex->setSnapshot(fixtureSnapshot());
    const QString source = fixtureSource();

    {
        MyCodeEditor editor;
        editor.setDocumentFileName(kFileName);
        editor.setPlainText(source);
        QString message;
        const bool started = editor.startInsightTargetPickMode(
            EditorInsightTargetClass::Signal, {}, {}, &message);
        publish(editor, 0, 9);
        const QStringList names = candidateNames(editor);
        expect("signal class blinks signals and ports from the visible region",
               started && editor.insightTargetPickModeActive()
                   && names.contains(QStringLiteral("clk"))
                   && names.contains(QStringLiteral("state_q"))
                   && names.contains(QStringLiteral("other_q")));
        expect("signal class does not offer module names as targets",
               !names.contains(QStringLiteral("pick_top"))
                   && !names.contains(QStringLiteral("pick_child")));
        expect("candidates outside the visible region are not enumerated",
               !names.contains(QStringLiteral("hidden_q"))
                   && editor.insightTargetEnumeratedLineRangeForTest()
                          == QPair<int, int>(0, 9));

        if (const QString artifacts =
                qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
            !artifacts.isEmpty()) {
            // The overlay only exists while painting, so the evidence shot is
            // taken from the live editor with the mode running.
            QDir().mkpath(artifacts);
            editor.resize(640, 260);
            editor.show();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            publish(editor, 0, 9);
            editor.grab().save(artifacts + "/pick_mode_blink.png");
        }

        const QList<TargetVisual> visuals = targetVisuals(editor);
        int activeCount = 0;
        bool allValid = !visuals.isEmpty();
        for (const TargetVisual& visual : visuals) {
            activeCount += visual.active ? 1 : 0;
            allValid = allValid && visual.valid && visual.end > visual.start;
        }
        expect("every candidate publishes a valid overlay annotation",
               allValid && visuals.size() == names.size() && activeCount == 1);

        // Scrolling is what moves the candidate set; the published range is
        // the only thing the controller looks at.
        const int hiddenLine = source.left(
            source.indexOf(QStringLiteral("hidden_q"))).count(QLatin1Char('\n'));
        publish(editor, hiddenLine - 1, hiddenLine + 1);
        const QStringList scrolled = candidateNames(editor);
        expect("candidates follow the visible region after scrolling",
               hiddenLine > 40
                   && scrolled.contains(QStringLiteral("hidden_q"))
                   && !scrolled.contains(QStringLiteral("state_q"))
                   && editor.insightTargetEnumeratedLineRangeForTest()
                          == QPair<int, int>(hiddenLine - 1, hiddenLine + 1));
        editor.cancelInsightTargetPickMode();
    }

    {
        MyCodeEditor editor;
        editor.setDocumentFileName(kFileName);
        editor.setPlainText(source);
        editor.startInsightTargetPickMode(
            EditorInsightTargetClass::Module, {}, {});
        publish(editor, 0, 9);
        const QStringList names = candidateNames(editor);
        expect("module class blinks the declaration and the instance type",
               names.contains(QStringLiteral("pick_top"))
                   && names.contains(QStringLiteral("pick_child"))
                   && !names.contains(QStringLiteral("state_q"))
                   && !names.contains(QStringLiteral("clk")));
        editor.cancelInsightTargetPickMode();
    }

    {
        MyCodeEditor editor;
        editor.setDocumentFileName(kFileName);
        editor.setPlainText(source);
        editor.startInsightTargetPickMode(
            EditorInsightTargetClass::Scope, {}, {});
        publish(editor, 0, 9);
        const QList<EditorInsightTargetCandidate> candidates =
            editor.insightTargetCandidatesForTest();
        bool everyCandidateIsAScope = !candidates.isEmpty();
        bool hasAlwaysBlock = false;
        for (const EditorInsightTargetCandidate& candidate : candidates) {
            everyCandidateIsAScope = everyCandidateIsAScope
                && candidate.scopeStartChar >= 0
                && candidate.scopeEndChar > candidate.scopeStartChar
                && !candidate.scopeLabel.isEmpty();
            hasAlwaysBlock = hasAlwaysBlock || candidate.startLine == 6;
        }
        expect("scope class blinks scope heads, not identifiers",
               everyCandidateIsAScope && hasAlwaysBlock);
        editor.cancelInsightTargetPickMode();
    }

    {
        MyCodeEditor editor;
        editor.setDocumentFileName(kFileName);
        editor.setPlainText(source);
        editor.startInsightTargetPickMode(
            EditorInsightTargetClass::Signal, {}, {});
        publish(editor, 0, 9);
        const QStringList names = candidateNames(editor);
        const int count = names.size();
        expect("picking starts on the first candidate in document order",
               count > 2 && editor.insightTargetActiveIndexForTest() == 0);
        expect("Tab moves to the next candidate",
               sendKey(editor, Qt::Key_Tab)
                   && editor.insightTargetActiveIndexForTest() == 1);
        expect("Shift+Tab moves back, it does not advance",
               sendKey(editor, Qt::Key_Backtab, Qt::ShiftModifier)
                   && editor.insightTargetActiveIndexForTest() == 0);
        expect("Shift+Tab from the first candidate wraps to the last",
               sendKey(editor, Qt::Key_Backtab, Qt::ShiftModifier)
                   && editor.insightTargetActiveIndexForTest() == count - 1);
        expect("Tab from the last candidate wraps to the first",
               sendKey(editor, Qt::Key_Tab)
                   && editor.insightTargetActiveIndexForTest() == 0);

        const bool blinkBefore = editor.insightTargetBlinkOnForTest();
        bool phaseBefore = true;
        for (const TargetVisual& visual : targetVisuals(editor))
            phaseBefore = visual.phaseVisible;
        QTimer* blinkTimer = nullptr;
        for (QTimer* timer : editor.findChildren<QTimer*>()) {
            if (timer && timer->isActive() && timer->interval() == 500)
                blinkTimer = timer;
        }
        // Fire the real 500 ms timer's signal instead of waiting for it: the
        // controller's phase flip is what is under test, not Qt's clock.
        if (blinkTimer)
            QMetaObject::invokeMethod(blinkTimer, "timeout");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        bool phaseAfter = phaseBefore;
        for (const TargetVisual& visual : targetVisuals(editor))
            phaseAfter = visual.phaseVisible;
        expect("a 500 ms blink tick inverts the published phase",
               blinkTimer
                   && editor.insightTargetBlinkOnForTest() != blinkBefore
                   && phaseAfter != phaseBefore);

        expect("Esc leaves the mode and removes the overlay",
               sendKey(editor, Qt::Key_Escape)
                   && !editor.insightTargetPickModeActive()
                   && targetVisuals(editor).isEmpty());
    }

    {
        MyCodeEditor editor;
        editor.setDocumentFileName(kFileName);
        editor.setPlainText(source);
        int pickedCount = 0;
        QString pickedName;
        int validatorCalls = 0;
        auto validator = [&validatorCalls](
                             const EditorInsightTargetCandidate& candidate,
                             QString* reason) {
            ++validatorCalls;
            if (candidate.name == QStringLiteral("state_q"))
                return true;
            if (reason)
                *reason = QStringLiteral("%1 drives no state register")
                              .arg(candidate.name);
            return false;
        };
        auto handler = [&pickedCount, &pickedName](
                           const EditorInsightTargetCandidate& candidate) {
            ++pickedCount;
            pickedName = candidate.name;
        };
        QString lastStatus;
        QObject::connect(&editor,
                         &MyCodeEditor::editorStatusMessageRequested,
                         &editor,
                         [&lastStatus](const QString& text) {
                             lastStatus = text;
                         });
        editor.startInsightTargetPickMode(
            EditorInsightTargetClass::Signal, validator, handler);
        publish(editor, 0, 9);
        const QStringList names = candidateNames(editor);
        const int candidateCount = names.size();
        const QString firstName = names.value(0);

        sendKey(editor, Qt::Key_Return);
        expect("a rejected candidate reports the validator's own reason",
               validatorCalls == 1 && pickedCount == 0
                   && lastStatus.contains(firstName)
                   && lastStatus.contains(
                       QStringLiteral("drives no state register")));
        expect("rejection keeps the mode and the candidate set intact",
               editor.insightTargetPickModeActive()
                   && candidateNames(editor).size() == candidateCount
                   && editor.insightTargetActiveIndexForTest() == 0);

        int guard = 0;
        while (editor.insightTargetPickModeActive()
               && candidateNames(editor).value(
                      editor.insightTargetActiveIndexForTest())
                      != QStringLiteral("state_q")
               && guard++ < candidateCount) {
            sendKey(editor, Qt::Key_Tab);
        }
        sendKey(editor, Qt::Key_Return);
        expect("an accepted candidate is reported once and ends the mode",
               pickedCount == 1
                   && pickedName == QStringLiteral("state_q")
                   && !editor.insightTargetPickModeActive()
                   && targetVisuals(editor).isEmpty());
    }

    semanticIndex->setSnapshot(previousSnapshot);
    std::printf("%d/%d insight target pick checks passed\n",
                checks - failures,
                checks);
    std::fflush(stdout);
    return failures == 0 ? 0 : 1;
}
