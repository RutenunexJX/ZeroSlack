#include "editorlocation.h"
#include "mycodeeditor.h"
#include "shareddocument.h"
#include "tabmanager.h"
#include "temporaryeditorsession.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QInputMethodEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>
#include <QWheelEvent>
#include <QtTest/QTest>

#include <cstdio>
#include <memory>

namespace {
int checks = 0;
int failures = 0;

class ProjectionTestEditor : public MyCodeEditor
{
public:
    using MyCodeEditor::MyCodeEditor;
    using MyCodeEditor::inputMethodQuery;
    using MyCodeEditor::wheelEvent;
};

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
}

void pumpEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QString foldingFixture(const QString& moduleName,
                       int leadingLines,
                       const QString& firstLabel,
                       const QString& secondLabel)
{
    QString text;
    for (int index = 0; index < leadingLines; ++index)
        text += QStringLiteral("// leading %1\n").arg(index);
    text += QStringLiteral(
                "module %1;\n"
                "initial begin : %2\n"
                "  logic first_a;\n"
                "  logic first_b;\n"
                "end\n"
                "  logic between;\n"
                "initial begin : %3\n"
                "  logic second_a;\n"
                "  logic second_b;\n"
                "  logic second_c;\n"
                "  logic second_d;\n"
                "end\n"
                "endmodule\n")
                .arg(moduleName, firstLabel, secondLabel);
    for (int index = 0; index < 80; ++index)
        text += QStringLiteral("// trailing %1\n").arg(index);
    return text;
}

bool writeTextFile(const QString& fileName, const QString& text)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly
                   | QIODevice::Text
                   | QIODevice::Truncate)) {
        return false;
    }
    return file.write(text.toUtf8()) == text.toUtf8().size();
}

void renderEditor(MyCodeEditor* editor)
{
    if (!editor)
        return;
    QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    editor->render(&painter);
}

QImage renderViewport(MyCodeEditor* editor)
{
    if (!editor)
        return {};
    QImage image(editor->viewport()->size(),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    editor->viewport()->render(&painter);
    return image;
}

bool allBlocksCanonicalVisible(QTextDocument* document)
{
    if (!document)
        return false;
    for (QTextBlock block = document->begin();
         block.isValid();
         block = block.next()) {
        if (!block.isVisible() || block.lineCount() <= 0)
            return false;
    }
    return true;
}

void setFoldedScrollValue(MyCodeEditor* editor, int value)
{
    if (!editor || !editor->verticalScrollBar())
        return;
    editor->viewProjectionActive();
    editor->verticalScrollBar()->setValue(value);
}

void exerciseTwoViewsOwnTheirFoldPresentation()
{
    SharedDocument document(
        QStringLiteral("view-local-folding"),
        QStringLiteral("view_local.sv"),
        foldingFixture(QStringLiteral("view_local"),
                       0,
                       QStringLiteral("first"),
                       QStringLiteral("second")));
    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    ProjectionTestEditor mainView;
    ProjectionTestEditor drawerView;
    layout->addWidget(&mainView);
    layout->addWidget(&drawerView);
    host.resize(720, 420);
    host.show();
    document.attachView(&mainView);
    document.attachView(&drawerView);
    pumpEvents();

    const int firstFoldStart = 1;
    const int secondFoldStart = 6;
    expect("two folding views share one QTextDocument",
           mainView.document() == drawerView.document()
               && mainView.document() == document.textDocument());
    expect("main view collapses only the first range",
           mainView.toggleFoldAtLineForTest(firstFoldStart));
    expect("drawer view collapses only the second range",
           drawerView.toggleFoldAtLineForTest(secondFoldStart));

    bool canonicalDuringSynchronousChange = true;
    int synchronousChangeObservations = 0;
    const QMetaObject::Connection synchronousChangeConnection = QObject::connect(
        document.textDocument(),
        &QTextDocument::contentsChange,
        &host,
        [&](int, int, int) {
            ++synchronousChangeObservations;
            canonicalDuringSynchronousChange =
                canonicalDuringSynchronousChange
                && allBlocksCanonicalVisible(document.textDocument());
            mainView.viewProjectionActive();
            drawerView.blockGeometry(drawerView.textCursor().blockNumber());
            canonicalDuringSynchronousChange =
                canonicalDuringSynchronousChange
                && allBlocksCanonicalVisible(document.textDocument());
        },
        Qt::DirectConnection);

    const EditorBlockGeometry mainFirstBody = mainView.blockGeometry(2);
    const EditorBlockGeometry drawerFirstBody = drawerView.blockGeometry(2);
    const EditorBlockGeometry mainSecondBody = mainView.blockGeometry(7);
    const EditorBlockGeometry drawerSecondBody = drawerView.blockGeometry(7);
    expect("view-local block geometry compresses only the owner's hidden rows",
           !mainView.foldLineVisibleForTest(2)
               && drawerView.foldLineVisibleForTest(2)
               && mainFirstBody.height <= 0.0
               && drawerFirstBody.height > 0.0
               && mainView.foldLineVisibleForTest(7)
               && !drawerView.foldLineVisibleForTest(7)
               && mainSecondBody.height > 0.0
               && drawerSecondBody.height <= 0.0);

    const int firstBodyY = qRound(
        drawerFirstBody.top + drawerFirstBody.height / 2.0);
    QTest::mouseClick(mainView.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPoint(8, firstBodyY));
    QTest::mouseClick(drawerView.viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      QPoint(8, firstBodyY));
    expect("mouse positioning cannot enter this view's hidden line while the other view can",
           mainView.textCursor().blockNumber() != 2
               && mainView.foldLineVisibleForTest(
                   mainView.textCursor().blockNumber())
               && drawerView.textCursor().blockNumber() == 2);
    expect("clicking the projected editor gives it mouse focus",
           drawerView.hasFocus());

    mainView.setTextCursor(
        QTextCursor(mainView.document()->findBlockByNumber(firstFoldStart)));
    drawerView.setTextCursor(
        QTextCursor(drawerView.document()->findBlockByNumber(firstFoldStart)));
    QTest::keyClick(&mainView, Qt::Key_Down);
    QTest::keyClick(&drawerView, Qt::Key_Down);
    expect("keyboard navigation skips only the current view's hidden rows",
           mainView.textCursor().blockNumber() == 5
               && drawerView.textCursor().blockNumber() == 2);

    QTextCursor rightBoundary(
        mainView.document()->findBlockByNumber(firstFoldStart));
    rightBoundary.movePosition(QTextCursor::EndOfBlock);
    mainView.setTextCursor(rightBoundary);
    const int drawerCursorBeforeShift = drawerView.textCursor().position();
    QTest::keyClick(&mainView, Qt::Key_Right, Qt::ShiftModifier);
    expect("Shift+Right crosses a local fold without losing its selection",
           mainView.textCursor().blockNumber() == 5
               && mainView.textCursor().hasSelection()
               && drawerView.textCursor().position() == drawerCursorBeforeShift);
    QTextCursor leftBoundary(
        mainView.document()->findBlockByNumber(5));
    mainView.setTextCursor(leftBoundary);
    QTest::keyClick(&mainView, Qt::Key_Left, Qt::ShiftModifier);
    expect("Shift+Left returns to the fold header end with selection intact",
           mainView.textCursor().blockNumber() == firstFoldStart
               && mainView.textCursor().positionInBlock()
                      == mainView.textCursor().block().text().size()
               && mainView.textCursor().hasSelection());

    QTextCursor imeCursor(
        mainView.document()->findBlockByNumber(firstFoldStart));
    imeCursor.movePosition(QTextCursor::EndOfBlock);
    mainView.setTextCursor(imeCursor);
    const QRect imeCursorRect = mainView.inputMethodQuery(
        Qt::ImCursorRectangle).toRect();
    const QRect imeAnchorRect = mainView.inputMethodQuery(
        Qt::ImAnchorRectangle).toRect();
    const QRect imeClipRect = mainView.inputMethodQuery(
        Qt::ImInputItemClipRectangle).toRect();
    expect("IME cursor anchor and clip rectangles use projected coordinates",
           !imeCursorRect.isEmpty()
               && !imeAnchorRect.isEmpty()
               && imeClipRect == mainView.viewport()->rect());
    QInputMethodEvent imeEvent;
    imeEvent.setCommitString(QStringLiteral("x"));
    QApplication::sendEvent(&mainView, &imeEvent);
    expect("IME input preserves the view-local fold and visible caret",
           mainView.foldCollapsedAtLineForTest(firstFoldStart)
               && mainView.foldLineVisibleForTest(
                   mainView.textCursor().blockNumber())
               && !mainView.cursorRect().isEmpty());

    QTextCursor drawerEdit = drawerView.textCursor();
    drawerEdit.movePosition(QTextCursor::EndOfBlock);
    drawerView.setTextCursor(drawerEdit);
    drawerView.insertPlainText(QStringLiteral(" // edited in drawer"));
    pumpEvents();
    expect("editing a line hidden only in the main view uses the shared text authority",
           mainView.document() == drawerView.document()
               && mainView.toPlainText() == drawerView.toPlainText()
               && mainView.toPlainText().contains(
                   QStringLiteral("edited in drawer"))
               && !mainView.foldLineVisibleForTest(2)
               && drawerView.foldLineVisibleForTest(2));
    expect("synchronous contentsChange projection queries never mutate raw blocks",
           synchronousChangeObservations > 0
               && canonicalDuringSynchronousChange);

    QTextCursor mainCursor(mainView.document()->findBlockByNumber(5));
    QTextCursor drawerCursor(drawerView.document()->findBlockByNumber(12));
    mainView.setTextCursor(mainCursor);
    drawerView.setTextCursor(drawerCursor);
    setFoldedScrollValue(&mainView, 3);
    setFoldedScrollValue(&drawerView, 11);
    const int mainPosition = mainView.textCursor().position();
    const int drawerPosition = drawerView.textCursor().position();
    const int mainScroll = mainView.verticalScrollBar()->value();
    const int drawerScroll = drawerView.verticalScrollBar()->value();

    renderEditor(&mainView);
    renderEditor(&drawerView);
    expect("simultaneous repaint keeps independent folded scroll ranges",
           mainView.verticalScrollBar()->maximum()
                   > drawerView.verticalScrollBar()->maximum()
               && mainView.verticalScrollBar()->maximum() > 0
               && drawerView.verticalScrollBar()->maximum() > 0);
    expect("fold paint scopes restore canonical shared QTextBlocks",
           allBlocksCanonicalVisible(document.textDocument()));
    expect("switching fold presentation preserves independent cursors and scroll",
           mainView.textCursor().position() == mainPosition
               && drawerView.textCursor().position() == drawerPosition
               && mainView.verticalScrollBar()->value() == mainScroll
               && drawerView.verticalScrollBar()->value() == drawerScroll);

    setFoldedScrollValue(
        &mainView, mainView.verticalScrollBar()->maximum());
    setFoldedScrollValue(
        &drawerView, drawerView.verticalScrollBar()->maximum());
    renderEditor(&mainView);
    renderEditor(&drawerView);
    const int lastLine = document.textDocument()->blockCount() - 1;
    const EditorBlockGeometry mainLast = mainView.blockGeometry(lastLine);
    const EditorBlockGeometry drawerLast = drawerView.blockGeometry(lastLine);
    expect("each folded view can scroll to its own final visible row",
           mainView.verticalScrollBar()->value()
                   == mainView.verticalScrollBar()->maximum()
               && drawerView.verticalScrollBar()->value()
                      == drawerView.verticalScrollBar()->maximum()
               && mainLast.top < mainView.viewport()->height()
               && mainLast.top + mainLast.height > 0.0
               && drawerLast.top < drawerView.viewport()->height()
               && drawerLast.top + drawerLast.height > 0.0);

    const int wheelStart = 0;
    mainView.verticalScrollBar()->setValue(wheelStart);
    QWheelEvent wheel(
        QPointF(12, 12),
        mainView.mapToGlobal(QPoint(12, 12)),
        QPoint(),
        QPoint(0, -120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false);
    mainView.wheelEvent(&wheel);
    expect("one folded wheel notch advances by rows rather than 120 source lines",
           mainView.verticalScrollBar()->value()
                   - wheelStart > 0
               && mainView.verticalScrollBar()->value()
                      - wheelStart <= 3);

    QPalette darkPalette = mainView.palette();
    darkPalette.setColor(QPalette::Base, QColor(QStringLiteral("#101318")));
    darkPalette.setColor(QPalette::Text, QColor(QStringLiteral("#f2f4f8")));
    mainView.setPalette(darkPalette);
    mainView.viewport()->setPalette(darkPalette);
    mainView.verticalScrollBar()->setValue(0);
    const QImage darkFolded = renderViewport(&mainView);
    int brightTextPixels = 0;
    for (int y = 0; y < darkFolded.height(); ++y) {
        for (int x = 0; x < qMin(240, darkFolded.width()); ++x) {
            const QColor pixel = darkFolded.pixelColor(x, y);
            if (pixel.lightness() > 170)
                ++brightTextPixels;
        }
    }
    expect("Dark folded projection paints default text with palette contrast",
           brightTextPixels > 8);

    QTextCursor prefix(document.textDocument());
    prefix.movePosition(QTextCursor::Start);
    prefix.insertText(QStringLiteral("// inserted before folds\n"));
    pumpEvents();
    expect("shared edit remaps each view's different collapsed range",
           mainView.foldCollapsedAtLineForTest(firstFoldStart + 1)
               && !mainView.foldCollapsedAtLineForTest(secondFoldStart + 1)
               && drawerView.foldCollapsedAtLineForTest(secondFoldStart + 1)
               && !drawerView.foldCollapsedAtLineForTest(firstFoldStart + 1));
    expect("shared edit remains one text authority",
           mainView.toPlainText() == drawerView.toPlainText()
               && mainView.document() == drawerView.document());

    expect("remapped presentation geometry stays view-local after edit",
           mainView.blockGeometry(3).height <= 0.0
               && drawerView.blockGeometry(3).height > 0.0
               && mainView.blockGeometry(8).height > 0.0
               && drawerView.blockGeometry(8).height <= 0.0);
    expect("edit and geometry scopes leave canonical blocks restored",
           allBlocksCanonicalVisible(document.textDocument()));
    mainView.setTextCursor(
        QTextCursor(mainView.document()->findBlockByNumber(2)));
    drawerView.setTextCursor(
        QTextCursor(drawerView.document()->findBlockByNumber(2)));
    QTest::keyClick(&mainView, Qt::Key_Down);
    QTest::keyClick(&drawerView, Qt::Key_Down);
    expect("cursor navigation remains view-local after fold remapping",
           mainView.textCursor().blockNumber() == 6
               && drawerView.textCursor().blockNumber() == 3);
    QObject::disconnect(synchronousChangeConnection);
}

void exerciseFoldAnchorBoundariesAndFindReveal()
{
    SharedDocument document(
        QStringLiteral("fold-anchor-boundaries"),
        QStringLiteral("fold_anchor.sv"),
        foldingFixture(QStringLiteral("fold_anchor"),
                       0,
                       QStringLiteral("anchor"),
                       QStringLiteral("other")));
    ProjectionTestEditor editor;
    document.attachView(&editor);
    expect("anchor fixture fold collapses", editor.toggleFoldAtLineForTest(1));
    EditorFoldViewState saved = editor.foldingViewState();
    expect("fold history captures start and end anchors",
           saved.collapsedRanges.size() == 1);
    if (saved.collapsedRanges.size() != 1)
        return;

    const int originalStartPosition =
        saved.collapsedRanges.first().startAnchor.position();
    QTextCursor startEdit(document.textDocument());
    startEdit.setPosition(originalStartPosition);
    startEdit.insertText(QStringLiteral("// inserted at start boundary\n"));
    expect("start anchor follows original fold start inserted at its boundary",
           saved.collapsedRanges.first().startAnchor.position()
                   > originalStartPosition
               && saved.collapsedRanges.first().startAnchor.blockNumber() == 2);

    QTextCursor interiorEdit(document.textDocument()->findBlockByNumber(3));
    interiorEdit.movePosition(QTextCursor::EndOfBlock);
    interiorEdit.insertText(QStringLiteral(" interior-edit"));
    const int endBefore = saved.collapsedRanges.first().endAnchor.position();
    QTextCursor endEdit(document.textDocument());
    endEdit.setPosition(endBefore);
    endEdit.insertText(QStringLiteral("\n// inserted at end boundary"));
    expect("end anchor keeps the opposite insertion affinity",
           saved.collapsedRanges.first().endAnchor.position() == endBefore);
    expect("boundary and interior edits keep raw shared blocks canonical",
           allBlocksCanonicalVisible(document.textDocument()));
    pumpEvents();
    editor.restoreFoldingViewState(saved);
    expect("edited anchor state restores the remapped fold start",
           editor.foldCollapsedAtLineForTest(2));

    expect("find can locate hidden fold text",
           editor.find(QStringLiteral("first_b")));
    expect("find reveals its hidden result before centering",
           editor.foldLineVisibleForTest(editor.textCursor().blockNumber())
               && !editor.foldCollapsedAtLineForTest(2)
               && !editor.cursorRect().isEmpty());
}

void exerciseLargeFoldOrdinaryInputDoesNotRebuildProjection()
{
    QString text = QStringLiteral(
        "module huge;\ninitial begin : huge\n/* projection fixture\n");
    text.reserve(1700000);
    for (int line = 0; line < 100000; ++line)
        text += QStringLiteral("folded body %1\n").arg(line);
    text += QStringLiteral("*/\nend\nendmodule\n");

    ProjectionTestEditor editor;
    editor.resize(720, 420);
    editor.show();
    editor.setPlainText(text);
    editor.acceptLoadedTextAsSemanticBaseline();
    expect("100k-line fold fixture collapses",
           editor.toggleFoldAtLineForTest(1));
    const EditorViewProjectionMetrics before =
        editor.viewProjectionMetricsForTest();
    renderEditor(&editor);
    const EditorViewProjectionMetrics afterRender =
        editor.viewProjectionMetricsForTest();
    const qint64 paintedRows =
        afterRender.rowsPainted - before.rowsPainted;
    expect("100k-line folded paint visits only viewport rows",
           paintedRows > 0 && paintedRows < 128);
    QTextCursor cursor(
        editor.document()->findBlockByNumber(editor.document()->blockCount() - 1));
    editor.setTextCursor(cursor);
    bool stable = true;
    for (int index = 0; index < 100; ++index) {
        editor.insertPlainText(QStringLiteral("x"));
        const EditorViewProjectionMetrics current =
            editor.viewProjectionMetricsForTest();
        stable = stable
            && current.rebuilds == afterRender.rebuilds
            && current.sourceRowsVisitedDuringRebuild
                   == afterRender.sourceRowsVisitedDuringRebuild;
    }
    expect("100 ordinary inputs do not rescan a 100k-line folded projection",
           stable);
    expect("large folded gutter and viewport remain canonical",
           allBlocksCanonicalVisible(editor.document()));
}

void exerciseDrawerHistoryRestoresPerEntryFolds()
{
    QTemporaryDir temporaryDirectory;
    const QString firstFile = temporaryDirectory.filePath(
        QStringLiteral("first.sv"));
    const QString secondFile = temporaryDirectory.filePath(
        QStringLiteral("second.sv"));
    const bool fixturesReady = temporaryDirectory.isValid()
        && writeTextFile(
            firstFile,
            foldingFixture(QStringLiteral("first"),
                           0,
                           QStringLiteral("first_a"),
                           QStringLiteral("first_b")))
        && writeTextFile(
            secondFile,
            foldingFixture(QStringLiteral("second"),
                           3,
                           QStringLiteral("second_a"),
                           QStringLiteral("second_b")));
    expect("drawer history folding fixtures are writable", fixturesReady);
    if (!fixturesReady)
        return;

    QWidget editorRegion;
    auto* layout = new QVBoxLayout(&editorRegion);
    auto* tabs = new QTabWidget(&editorRegion);
    layout->addWidget(tabs);
    TabManager manager(tabs);
    editorRegion.resize(760, 460);
    editorRegion.show();
    expect("main fixture opens", manager.openFileInTab(firstFile));

    TemporaryEditorSession session(&manager, &editorRegion);
    session.setViewParent(&editorRegion);
    EditorLocation firstLocation;
    firstLocation.filePath = firstFile;
    firstLocation.line = 1;
    EditorLocation secondLocation;
    secondLocation.filePath = secondFile;
    secondLocation.line = 1;

    expect("drawer opens first history target",
           session.openLocation(firstLocation));
    pumpEvents();
    MyCodeEditor* drawerEditor = session.editor();
    expect("first history item owns its first fold",
           drawerEditor
               && drawerEditor->toggleFoldAtLineForTest(1));
    QTextCursor hiddenHistoryCursor(
        drawerEditor->document()->findBlockByNumber(2));
    static_cast<QPlainTextEdit*>(drawerEditor)
        ->setTextCursor(hiddenHistoryCursor);

    expect("drawer opens second history target",
           session.openLocation(secondLocation));
    pumpEvents();
    expect("second history item owns a different fold",
           drawerEditor
               && drawerEditor->toggleFoldAtLineForTest(9));
    MyCodeEditor* mainEditor = manager.getCurrentEditor();
    if (mainEditor) {
        QTextCursor nonActiveEdit(mainEditor->document());
        nonActiveEdit.movePosition(QTextCursor::Start);
        nonActiveEdit.insertText(
            QStringLiteral("// non-active history prefix\n"));
    }
    pumpEvents();
    session.goBack();
    pumpEvents();
    expect("Back restores and remaps the non-active first history fold",
           drawerEditor
               && drawerEditor->foldCollapsedAtLineForTest(2)
               && !drawerEditor->foldCollapsedAtLineForTest(7)
               && drawerEditor->textCursor().blockNumber() == 2);
    session.goForward();
    pumpEvents();
    expect("Forward restores only the second history item's fold collection",
           drawerEditor
               && drawerEditor->foldCollapsedAtLineForTest(9)
               && !drawerEditor->foldCollapsedAtLineForTest(4));
    session.goBack();
    pumpEvents();
    expect("A to B to A restoration is stable across repeated traversal",
           drawerEditor
               && drawerEditor->foldCollapsedAtLineForTest(2)
               && !drawerEditor->foldCollapsedAtLineForTest(7));
}

void exerciseShiftWheelHorizontalScroll()
{
    ProjectionTestEditor editor;
    editor.setLineWrapMode(QPlainTextEdit::NoWrap);
    editor.resize(320, 180);
    editor.setPlainText(QStringLiteral("module wide;\n")
                        + QString(300, QLatin1Char('x'))
                        + QStringLiteral("\nendmodule\n")
                        + QString(40, QLatin1Char('\n')));
    editor.show();
    pumpEvents();

    QScrollBar* horizontal = editor.horizontalScrollBar();
    QScrollBar* vertical = editor.verticalScrollBar();
    expect("unwrapped fixture supports both scroll directions",
           horizontal->maximum() > 0 && vertical->maximum() > 0);
    const int verticalBefore = vertical->value();
    QWheelEvent shiftWheel(QPointF(12, 12),
                           editor.mapToGlobal(QPoint(12, 12)),
                           QPoint(), QPoint(0, -120), Qt::NoButton,
                           Qt::ShiftModifier, Qt::NoScrollPhase, false);
    editor.wheelEvent(&shiftWheel);
    expect("Shift and wheel scroll horizontally without changing the row",
           shiftWheel.isAccepted() && horizontal->value() > 0
               && vertical->value() == verticalBefore);

    QWheelEvent shiftPixelWheel(QPointF(12, 12),
                                editor.mapToGlobal(QPoint(12, 12)),
                                QPoint(0, -20), QPoint(), Qt::NoButton,
                                Qt::ShiftModifier, Qt::NoScrollPhase, false);
    const int angleScroll = horizontal->value();
    editor.wheelEvent(&shiftPixelWheel);
    expect("Shift and precision wheel use horizontal pixel delta",
           horizontal->value() == angleScroll + 20
               && vertical->value() == verticalBefore);

    int zoomRequests = 0;
    QObject::connect(&editor, &MyCodeEditor::fontZoomRequested,
                     &editor, [&](int) { ++zoomRequests; });
    const int beforeZoom = horizontal->value();
    QWheelEvent zoomWheel(QPointF(12, 12),
                          editor.mapToGlobal(QPoint(12, 12)),
                          QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::ControlModifier | Qt::ShiftModifier,
                          Qt::NoScrollPhase, false);
    editor.wheelEvent(&zoomWheel);
    expect("Ctrl Shift wheel preserves font zoom",
           zoomRequests == 1 && horizontal->value() == beforeZoom);

    const int horizontalBefore = horizontal->value();
    QWheelEvent normalWheel(QPointF(12, 12),
                            editor.mapToGlobal(QPoint(12, 12)),
                            QPoint(), QPoint(0, -120), Qt::NoButton,
                            Qt::NoModifier, Qt::NoScrollPhase, false);
    editor.wheelEvent(&normalWheel);
    expect("unmodified wheel keeps vertical scrolling",
           vertical->value() > verticalBefore
               && horizontal->value() == horizontalBefore);
}
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication application(argc, argv);
    exerciseTwoViewsOwnTheirFoldPresentation();
    exerciseFoldAnchorBoundariesAndFindReveal();
    exerciseLargeFoldOrdinaryInputDoesNotRebuildProjection();
    exerciseDrawerHistoryRestoresPerEntryFolds();
    exerciseShiftWheelHorizontalScroll();
    std::printf("Checks: %d, Failures: %d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
