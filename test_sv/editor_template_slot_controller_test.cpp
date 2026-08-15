#include "annotationlayer.h"
#include "completiontypes.h"
#include "editormodecontroller.h"
#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "shareddocument.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QThreadPool>
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
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
    std::fflush(stdout);
}

bool sendKey(MyCodeEditor& editor,
             int key,
             Qt::KeyboardModifiers modifiers =
                 Qt::NoModifier)
{
    QKeyEvent event(QEvent::KeyPress,
                    key,
                    modifiers);
    QCoreApplication::sendEvent(&editor, &event);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 10);
    return event.isAccepted();
}

void replaceSelection(MyCodeEditor& editor,
                      const QString& text)
{
    QTextCursor cursor = editor.textCursor();
    cursor.insertText(text);
    editor.setTextCursor(cursor);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 10);
}

struct SlotVisual
{
    int start = -1;
    int end = -1;
    bool active = false;
    bool phaseVisible = true;
    int lane = -1;
};

QList<SlotVisual> slotVisuals(
    const MyCodeEditor& editor)
{
    QList<SlotVisual> result;
    AnnotationLayerQuery query;
    query.firstVisibleLine = 0;
    query.lastVisibleLine =
        qMax(0, editor.blockCount() - 1);
    query.maxAnnotationsPerLine = 64;
    query.maxLanes = 16;
    const AnnotationLayerReport report =
        editor.annotationLayerReportForTest(query);
    for (const ResolvedEditorAnnotation& resolved :
         report.annotations) {
        if (resolved.annotation.kind
            != EditorAnnotationKind::TemplateSlot) {
            continue;
        }
        result.append(SlotVisual{
            resolved.annotation.range.startPosition,
            resolved.annotation.range.endPosition,
            resolved.annotation.active,
            resolved.annotation.phaseVisible,
            resolved.lane,
        });
    }
    return result;
}

int legacySlotSelectionCount(
    const MyCodeEditor& editor)
{
    int count = 0;
    for (const QTextEdit::ExtraSelection& selection :
         editor.extraSelections()) {
        const QColor underline =
            selection.format.underlineColor();
        if (underline
                == QColor(QStringLiteral("#22C55E"))
            || underline
                   == QColor(QStringLiteral("#60A5FA"))) {
            ++count;
        }
    }
    return count;
}

const SlotVisual* visualAt(
    const QList<SlotVisual>& visuals,
    int start)
{
    for (const SlotVisual& visual : visuals) {
        if (visual.start == start)
            return &visual;
    }
    return nullptr;
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    const QString original =
        QStringLiteral(
            "posedge rst_n; if (!rst_n) begin\n"
            "    \n"
            "end");
    const int firstReset =
        original.indexOf(QStringLiteral("rst_n"));
    const int secondReset =
        original.indexOf(QStringLiteral("rst_n"),
                         firstReset + 1);
    const int bodyPosition =
        original.indexOf(QStringLiteral("\n    \n"))
        + 5;

    CodeTemplateSlot body;
    body.name = QStringLiteral("body");
    body.start = bodyPosition;
    body.length = 0;
    body.tabStop = 20;
    body.visibleWhenEmpty = true;

    CodeTemplateSlot reset;
    reset.name = QStringLiteral("reset");
    reset.start = firstReset;
    reset.length = QStringLiteral("rst_n").size();
    reset.tabStop = 10;

    CodeTemplateSlot linkedReset = reset;
    linkedReset.start = secondReset;

    CodeTemplateSlotList templateSlots;
    templateSlots.append(body);
    templateSlots.append(reset);
    templateSlots.append(linkedReset);

    {
        MyCodeEditor editor;
        editor.setPlainText(original);
        editor.startTemplateSlotMode(
            0, original.size(), templateSlots);

        expect("explicit tab-stop numbering is stable, not metadata order",
               editor.templateSlotModeActive()
                   && editor.templateSlotModeSlotCount() == 2
                   && editor.templateSlotModeActiveIndex() == 0
                   && editor.textCursor().selectedText()
                          == QStringLiteral("rst_n"));

        const QList<SlotVisual> initialVisuals =
            slotVisuals(editor);
        const SlotVisual* firstVisual =
            visualAt(initialVisuals, firstReset);
        const SlotVisual* secondVisual =
            visualAt(initialVisuals, secondReset);
        const SlotVisual* bodyVisual =
            visualAt(initialVisuals, bodyPosition);
        expect("linked physical slots publish one active annotation state",
               initialVisuals.size() == 3
                   && firstVisual
                   && secondVisual
                   && firstVisual->active
                   && secondVisual->active
                   && firstVisual->lane == 0
                   && secondVisual->lane == 0
                   && firstVisual->phaseVisible
                          == secondVisual->phaseVisible);
        expect("template slots have no parallel ExtraSelection source",
               legacySlotSelectionCount(editor) == 0);
        expect("visible zero-length body slot remains a layout marker",
               bodyVisual
                   && bodyVisual->end
                          == bodyVisual->start
                   && !bodyVisual->active);

        QTextCursor linkedCursor(editor.document());
        linkedCursor.setPosition(secondReset + 1);
        editor.setTextCursor(linkedCursor);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 10);
        expect("moving into a linked range retains its logical number",
               editor.templateSlotModeActiveIndex() == 0);

        expect("Tab owns navigation before editor indentation",
               sendKey(editor, Qt::Key_Tab)
                   && editor.templateSlotModeActiveIndex() == 1
                   && editor.textCursor().position()
                          == bodyPosition
                   && !editor.textCursor().hasSelection()
                   && editor.toPlainText() == original);
        const QList<SlotVisual> bodyActiveVisuals =
            slotVisuals(editor);
        const SlotVisual* activeBody =
            visualAt(bodyActiveVisuals, bodyPosition);
        expect("zero-length body receives the active annotation state",
               activeBody
                   && activeBody->active);

        expect("Shift+Tab owns reverse navigation",
               sendKey(editor,
                       Qt::Key_Backtab,
                       Qt::ShiftModifier)
                   && editor.templateSlotModeActiveIndex() == 0
                   && editor.textCursor().selectedText()
                          == QStringLiteral("rst_n")
                   && editor.toPlainText() == original);
        expect("Esc exits slot mode without rolling back text",
               sendKey(editor, Qt::Key_Escape)
                   && !editor.templateSlotModeActive()
                   && slotVisuals(editor).isEmpty()
                   && editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        editor.setPlainText(original);
        editor.startTemplateSlotMode(
            0, original.size(), templateSlots);
        replaceSelection(
            editor, QStringLiteral("reset_n"));
        expect("editing one linked slot updates every physical range",
               editor.toPlainText()
                   == QStringLiteral(
                       "posedge reset_n; if (!reset_n) begin\n"
                       "    \n"
                       "end")
                   && editor.templateSlotModeActive()
                   && editor.templateSlotModeSlotCount() == 2);

        expect("Undo has priority and reverts the complete linked edit once",
               sendKey(editor,
                       Qt::Key_Z,
                       Qt::ControlModifier)
                   && editor.toPlainText() == original
                   && !editor.templateSlotModeActive());
    }

    {
        MyCodeEditor editor;
        editor.setPlainText(QStringLiteral("xy"));
        CodeTemplateSlot visible;
        visible.name = QStringLiteral("visible");
        visible.start = 1;
        visible.length = 0;
        visible.tabStop = 1;
        visible.visibleWhenEmpty = true;
        CodeTemplateSlot hidden = visible;
        hidden.name = QStringLiteral("hidden");
        hidden.start = 2;
        hidden.tabStop = 2;
        hidden.visibleWhenEmpty = false;
        editor.startTemplateSlotMode(
            0, 2, {visible, hidden});

        const QList<SlotVisual> visuals =
            slotVisuals(editor);
        expect("zero-length visibility flag filters presentation only",
               editor.templateSlotModeSlotCount() == 2
                   && visuals.size() == 1
                   && visuals.constFirst().start == 1);
    }

    {
        MyCodeEditor editor;
        const QString original = QStringLiteral("abc");
        editor.setPlainText(original);

        CodeTemplateSlot first;
        first.name = QStringLiteral("first");
        first.start = 0;
        first.length = 1;
        first.tabStop = 1;
        CodeTemplateSlot adjacentEmpty;
        adjacentEmpty.name = QStringLiteral("adjacent-empty");
        adjacentEmpty.start = 1;
        adjacentEmpty.length = 0;
        adjacentEmpty.tabStop = 2;
        adjacentEmpty.visibleWhenEmpty = true;
        CodeTemplateSlot third;
        third.name = QStringLiteral("third");
        third.start = 2;
        third.length = 1;
        third.tabStop = 3;
        editor.startTemplateSlotMode(
            0, original.size(), {first, adjacentEmpty, third});

        expect("Tab selects an adjacent zero-length slot",
               sendKey(editor, Qt::Key_Tab)
                   && editor.templateSlotModeActiveIndex() == 1
                   && !editor.textCursor().hasSelection()
                   && editor.textCursor().position() == 1);
        expect("Tab+Shift reverses from a shared slot endpoint",
               sendKey(editor,
                       Qt::Key_Tab,
                       Qt::ShiftModifier)
                   && editor.templateSlotModeActiveIndex() == 0
                   && editor.textCursor().selectedText()
                          == QStringLiteral("a")
                   && editor.toPlainText() == original);
        expect("Shift+Tab wraps from the first slot to the last",
               sendKey(editor,
                       Qt::Key_Backtab,
                       Qt::ShiftModifier)
                   && editor.templateSlotModeActiveIndex() == 2
                   && editor.textCursor().selectedText()
                          == QStringLiteral("c"));
        expect("Tab wraps from the last slot to the first",
               sendKey(editor, Qt::Key_Tab)
                   && editor.templateSlotModeActiveIndex() == 0
                   && editor.textCursor().selectedText()
                          == QStringLiteral("a"));
    }

    {
        MyCodeEditor editor;
        const QString original = QStringLiteral("x");
        editor.setPlainText(original);
        CodeTemplateSlot only;
        only.name = QStringLiteral("only");
        only.start = 0;
        only.length = 1;
        only.tabStop = 1;
        editor.startTemplateSlotMode(0, original.size(), {only});

        expect("single-slot Tab remains in the only slot",
               sendKey(editor, Qt::Key_Tab)
                   && editor.templateSlotModeActive()
                   && editor.templateSlotModeActiveIndex() == 0
                   && editor.textCursor().selectedText()
                          == QStringLiteral("x"));
        expect("single-slot Tab+Shift remains in the only slot",
               sendKey(editor,
                       Qt::Key_Tab,
                       Qt::ShiftModifier)
                   && editor.templateSlotModeActive()
                   && editor.templateSlotModeActiveIndex() == 0
                   && editor.textCursor().selectedText()
                          == QStringLiteral("x")
                   && editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        editor.setPlainText(QStringLiteral("abc"));
        CodeTemplateSlot finalSlot;
        finalSlot.name = QStringLiteral("final");
        finalSlot.start = 2;
        finalSlot.length = 1;
        finalSlot.tabStop = 0;
        CodeTemplateSlot legacySlot;
        legacySlot.name = QStringLiteral("legacy");
        legacySlot.start = 1;
        legacySlot.length = 1;
        CodeTemplateSlot firstSlot;
        firstSlot.name = QStringLiteral("first");
        firstSlot.start = 0;
        firstSlot.length = 1;
        firstSlot.tabStop = 1;
        editor.startTemplateSlotMode(
            0,
            3,
            {finalSlot, legacySlot, firstSlot});

        expect("tab-stop zero remains final even with legacy slots",
               editor.templateSlotModeSlotCount() == 3
                   && editor.textCursor().selectedText()
                          == QStringLiteral("a")
                   && sendKey(editor, Qt::Key_Tab)
                   && editor.textCursor().selectedText()
                          == QStringLiteral("b")
                   && sendKey(editor, Qt::Key_Tab)
                   && editor.textCursor().selectedText()
                           == QStringLiteral("c"));
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        editor.setLineWrapMode(
            QPlainTextEdit::NoWrap);
        QString source;
        source.reserve(48000);
        for (int line = 0; line < 4000; ++line) {
            source.append(
                QStringLiteral("slot_row_%1\n")
                    .arg(line));
        }
        editor.setPlainText(source);
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents();

        CodeTemplateSlot spanningSlot;
        spanningSlot.name =
            QStringLiteral("spanning");
        spanningSlot.start = 0;
        spanningSlot.length =
            static_cast<int>(source.size());
        spanningSlot.tabStop = 1;
        editor.startTemplateSlotMode(
            0,
            static_cast<int>(source.size()),
            {spanningSlot});
        QCoreApplication::processEvents();

        const QTextBlock firstVisible =
            editor.cursorForPosition(QPoint(0, 0)).block();
        const QTextBlock lastVisible =
            editor.cursorForPosition(
                QPoint(
                    0,
                    qMax(
                        0,
                        editor.viewport()->height() - 1)))
                .block();
        AnnotationLayerQuery query;
        query.firstVisibleLine = 0;
        query.lastVisibleLine =
            editor.blockCount() - 1;
        query.maxAnnotationsPerLine = 64;
        query.maxLanes = 16;
        const AnnotationLayerReport report =
            editor.annotationLayerReportForTest(query);
        int visibleSlotCount = 0;
        bool onlyVisibleRows = true;
        for (const ResolvedEditorAnnotation& resolved :
             report.annotations) {
            if (resolved.annotation.kind
                != EditorAnnotationKind::TemplateSlot) {
                continue;
            }
            ++visibleSlotCount;
            const int line =
                resolved.annotation.range.firstLine;
            onlyVisibleRows =
                onlyVisibleRows
                && line >= firstVisible.blockNumber()
                && line <= lastVisible.blockNumber();
        }
        expect("large spanning slots publish only visible-row annotations",
               firstVisible.isValid()
                   && lastVisible.isValid()
                   && visibleSlotCount > 0
                   && visibleSlotCount
                          <= lastVisible.blockNumber()
                                 - firstVisible.blockNumber()
                                 + 1
                   && visibleSlotCount
                          < editor.blockCount()
                   && onlyVisibleRows);
    }

    {
        SharedDocument shared(
            QStringLiteral("shared-highlighter-lifecycle"),
            QStringLiteral("shared_highlighter.sv"),
            QStringLiteral("module shared_highlighter;\nendmodule\n"));
        auto owner = std::make_unique<MyCodeEditor>();
        auto remainingView = std::make_unique<MyCodeEditor>();
        shared.attachView(owner.get());
        shared.attachView(remainingView.get());

        const QList<MyHighlighter*> initialHighlighters =
            shared.textDocument()->findChildren<MyHighlighter*>(
                QString(), Qt::FindDirectChildrenOnly);
        QPointer<MyHighlighter> initialHighlighter =
            initialHighlighters.isEmpty()
                ? nullptr
                : initialHighlighters.constFirst();
        expect("shared document installs exactly one syntax highlighter",
               initialHighlighters.size() == 1
                   && initialHighlighter);

        owner.reset();
        const QList<MyHighlighter*> transferredHighlighters =
            shared.textDocument()->findChildren<MyHighlighter*>(
                QString(), Qt::FindDirectChildrenOnly);
        expect("closing the highlighter owner transfers one replacement",
               initialHighlighter.isNull()
                   && transferredHighlighters.size() == 1);

        const QString replacement =
            QStringLiteral("module replacement;\nlogic value;\nendmodule\n");
        remainingView->setPlainText(replacement);
        expect("transferred highlighter keeps the remaining syntax cache current",
               remainingView->syntaxTextForTest() == replacement);

        remainingView.reset();
        expect("closing the last view removes the shared highlighter",
               shared.textDocument()
                   ->findChildren<MyHighlighter*>(
                       QString(), Qt::FindDirectChildrenOnly)
                   .isEmpty());
    }

    {
        SemanticIndex* semanticIndex =
            SemanticIndex::getInstance();
        const std::shared_ptr<const SemanticIndexSnapshot>
            previousSnapshot = semanticIndex->snapshot();
        semanticIndex->setSnapshot(
            std::make_shared<const SemanticIndexSnapshot>());

        QPointer<MyCodeEditor> editorGuard;
        QPointer<QTimer> blinkTimerGuard;
        QPointer<QObject> ghostWatcherGuard;
        {
            auto editor = std::make_unique<MyCodeEditor>();
            editorGuard = editor.get();
            editor->setDocumentFileName(
                QStringLiteral("pending_teardown.sv"));
            QString source = QStringLiteral(
                "module pending_teardown;\nlogic value;\nendmodule\n");
            source.append(
                QString(2 * 1024 * 1024,
                        QLatin1Char(' ')));
            editor->setPlainText(source);

            CodeTemplateSlot activeSlot;
            activeSlot.name = QStringLiteral("value");
            activeSlot.start = source.indexOf(
                QStringLiteral("value"));
            activeSlot.length =
                QStringLiteral("value").size();
            activeSlot.tabStop = 1;
            editor->startTemplateSlotMode(
                0, source.size(), {activeSlot});
            editor->flashLine(1);
            editor->refreshSemanticPresentation();

            for (QTimer* timer :
                 editor->findChildren<QTimer*>()) {
                if (timer && timer->interval() == 500) {
                    blinkTimerGuard = timer;
                    break;
                }
            }
            for (QObject* child :
                 editor->findChildren<QObject*>()) {
                if (child
                    && child->inherits(
                        "QFutureWatcherBase")) {
                    ghostWatcherGuard = child;
                    break;
                }
            }
            expect("teardown fixture owns active timer and Ghost worker",
                   blinkTimerGuard
                       && blinkTimerGuard->isActive()
                       && ghostWatcherGuard);
        }

        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents(
            nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 20);
        expect("editor teardown disconnects pending state callbacks",
               editorGuard.isNull()
                   && blinkTimerGuard.isNull()
                   && ghostWatcherGuard.isNull());
        semanticIndex->setSnapshot(previousSnapshot);
    }

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
