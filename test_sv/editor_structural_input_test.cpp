#include "annotationlayer.h"
#include "editormodecontroller.h"
#include "mycodeeditor.h"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QtTest/QTest>

#include <cstdio>

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

void setCursor(MyCodeEditor& editor,
               int position,
               int anchor = -1)
{
    QTextCursor cursor(editor.document());
    if (anchor >= 0) {
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(position);
    }
    editor.setTextCursor(cursor);
    QCoreApplication::processEvents();
}

void sendTextKey(MyCodeEditor& editor,
                 int key,
                 const QString& text)
{
    QKeyEvent press(QEvent::KeyPress,
                    key,
                    Qt::NoModifier,
                    text);
    QApplication::sendEvent(&editor, &press);
    QKeyEvent release(QEvent::KeyRelease,
                      key,
                      Qt::NoModifier,
                      text);
    QApplication::sendEvent(&editor, &release);
    QCoreApplication::processEvents();
}

bool hasKeywordGhost(const MyCodeEditor& editor,
                     const QString& suffix)
{
    AnnotationLayerQuery query;
    query.firstVisibleLine = 0;
    query.lastVisibleLine = editor.blockCount() - 1;
    const AnnotationLayerReport report =
        editor.annotationLayerReportForTest(query);
    for (const ResolvedEditorAnnotation& resolved :
         report.annotations) {
        if (resolved.annotation.kind
                == EditorAnnotationKind::KeywordGhost
            && resolved.annotation.text == suffix) {
            return true;
        }
    }
    return false;
}

QList<ResolvedEditorAnnotation> annotationsOfKind(
    const MyCodeEditor& editor,
    EditorAnnotationKind kind)
{
    AnnotationLayerQuery query;
    query.firstVisibleLine = 0;
    query.lastVisibleLine =
        qMax(0, editor.blockCount() - 1);
    query.maxAnnotationsPerLine = 64;
    query.maxLanes = 16;
    const AnnotationLayerReport report =
        editor.annotationLayerReportForTest(query);
    QList<ResolvedEditorAnnotation> result;
    for (const ResolvedEditorAnnotation& resolved :
         report.annotations) {
        if (resolved.annotation.kind == kind)
            result.append(resolved);
    }
    return result;
}

int highlightedPairKeywordCount(const MyCodeEditor& editor)
{
    int count = 0;
    for (const QTextEdit::ExtraSelection& selection :
         editor.extraSelections()) {
        const QString text = selection.cursor.selectedText();
        const bool keyword =
            text == QStringLiteral("begin")
            || text == QStringLiteral("end")
            || text == QStringLiteral("case")
            || text == QStringLiteral("endcase");
        if (keyword
            && selection.format.underlineColor()
                   == QColor(QStringLiteral("#EAB308"))) {
            ++count;
        }
    }
    return count;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral("module m;\n"
                           "always_comb begin\n"
                           "    beg\n"
                           "end\n"
                           "endmodule\n");
        editor.setPlainText(original);
        const int cursor =
            original.lastIndexOf(QStringLiteral("beg")) + 3;
        setCursor(editor, cursor);

        expect("unique structural keyword publishes ghost annotation",
               editor.editorModeActiveForTest(
                   EditorModeId::KeywordGhost)
                   && hasKeywordGhost(editor,
                                      QStringLiteral("in")));
        QTest::keyClick(&editor, Qt::Key_Tab);
        expect("Tab accepts the unique keyword suffix",
               editor.toPlainText().mid(cursor - 3, 5)
                   == QStringLiteral("begin")
                   && !editor.editorModeActiveForTest(
                       EditorModeId::KeywordGhost));
        editor.undo();
        expect("keyword acceptance is one undo transaction",
               editor.toPlainText() == original);

        setCursor(editor, cursor);
        expect("keyword ghost reappears after undo",
               hasKeywordGhost(editor,
                               QStringLiteral("in")));
        QTest::keyClick(&editor, Qt::Key_Escape);
        expect("Esc cancels keyword ghost without changing text",
               editor.toPlainText() == original
                   && !editor.editorModeActiveForTest(
                       EditorModeId::KeywordGhost)
                   && !hasKeywordGhost(
                       editor, QStringLiteral("in")));
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral("module m;\nfoo\nendmodule\n");
        editor.setPlainText(original);
        const int cursor =
            original.indexOf(QStringLiteral("foo")) + 3;
        setCursor(editor, cursor);
        QTest::keyClick(&editor, Qt::Key_Tab);
        expect("Tab inserts four spaces when no ghost owns it",
               editor.toPlainText()
                   == QStringLiteral(
                       "module m;\nfoo    \nendmodule\n"));
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral("module m;\nalways_comb begin");
        editor.setPlainText(original);
        setCursor(editor, original.size());
        QTest::keyClick(&editor, Qt::Key_Return);
        expect("Enter inserts Tree-sitter structural indentation and end",
               editor.toPlainText()
                   == original
                      + QStringLiteral("\n    \nend")
                   && editor.textCursor().position()
                          == original.size() + 5);
        editor.undo();
        expect("structural Enter is one undo transaction",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        editor.setPlainText(QStringLiteral("sig"));
        setCursor(editor, 3, 0);
        sendTextKey(editor,
                    Qt::Key_ParenLeft,
                    QStringLiteral("("));
        expect("opening delimiter surrounds a selection",
               editor.toPlainText()
                   == QStringLiteral("(sig)"));
        editor.undo();
        expect("selection surround is one undo transaction",
               editor.toPlainText() == QStringLiteral("sig"));

        editor.setPlainText(QString());
        setCursor(editor, 0);
        sendTextKey(editor,
                    Qt::Key_BracketLeft,
                    QStringLiteral("["));
        expect("opening delimiter creates a pair",
               editor.toPlainText() == QStringLiteral("[]")
                   && editor.textCursor().position() == 1);
        sendTextKey(editor,
                    Qt::Key_BracketRight,
                    QStringLiteral("]"));
        expect("typing an existing closer skips it",
               editor.toPlainText() == QStringLiteral("[]")
                   && editor.textCursor().position() == 2);

        editor.setPlainText(QStringLiteral("// "));
        setCursor(editor, 3);
        sendTextKey(editor,
                    Qt::Key_ParenLeft,
                    QStringLiteral("("));
        expect("delimiter pairing is suppressed in comments",
               editor.toPlainText() == QStringLiteral("// ("));
    }

    {
        MyCodeEditor editor;
        const QString source =
            QStringLiteral("module m;\n"
                           "always_comb begin\n"
                           "    value = 1'b0;\n"
                           "end\n"
                           "endmodule\n");
        editor.setPlainText(source);
        const int afterBegin =
            source.indexOf(QStringLiteral("begin")) + 5;
        setCursor(editor, afterBegin);
        expect("begin/end pair is highlighted at keyword boundary",
               highlightedPairKeywordCount(editor) == 2);
    }

    {
        MyCodeEditor editor;
        const QString source =
            QStringLiteral(
                "module m;\n"
                "always_comb begin\n"
                "    sig = sig + 1;\n"
                "    sig = sig + 2;\n"
                "end\n"
                "endmodule\n");
        editor.setPlainText(source);
        const int firstSignal =
            source.indexOf(QStringLiteral("sig"));
        setCursor(editor, firstSignal + 1);

        QTest::keyClick(
            &editor,
            Qt::Key_D,
            Qt::ControlModifier);
        expect("first Ctrl+D selects the structural identifier",
               editor.textCursor().selectedText()
                   == QStringLiteral("sig")
                   && !editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor));
        QTest::keyClick(
            &editor,
            Qt::Key_D,
            Qt::ControlModifier);
        expect("second Ctrl+D enters centralized multi-cursor mode",
               editor.editorModeActiveForTest(
                   EditorModeId::MultiCursor));
        sendTextKey(editor,
                    Qt::Key_X,
                    QStringLiteral("x"));
        expect("typing replaces the selected structural occurrences",
               editor.toPlainText().contains(
                   QStringLiteral(
                       "    x = x + 1;\n"
                       "    sig = sig + 2;")));
        editor.undo();
        expect("multi-cursor occurrence replacement is one undo transaction",
               editor.toPlainText() == source);

        setCursor(editor, firstSignal + 1);
        QTest::keyClick(
            &editor,
            Qt::Key_L,
            Qt::ControlModifier
                | Qt::ShiftModifier);
        expect("scope occurrence action enters multi-cursor mode",
               editor.editorModeActiveForTest(
                   EditorModeId::MultiCursor));
        sendTextKey(editor,
                    Qt::Key_Y,
                    QStringLiteral("y"));
        expect("scope occurrence action excludes other lexical scopes",
               editor.toPlainText().contains(
                   QStringLiteral(
                       "    y = y + 1;\n"
                       "    y = y + 2;")));
        editor.undo();
        expect("scope-wide replacement is one undo transaction",
               editor.toPlainText() == source);

        setCursor(editor, firstSignal + 1);
        QTest::keyClick(
            &editor,
            Qt::Key_L,
            Qt::ControlModifier
                | Qt::ShiftModifier);
        QTest::keyClick(
            &editor,
            Qt::Key_Backtab);
        expect("Shift+Tab belongs to multi-cursor and unindents each line once",
               editor.toPlainText().contains(
                   QStringLiteral(
                       "\nsig = sig + 1;\n"
                       "sig = sig + 2;\n")));
        editor.undo();
        expect("multi-cursor Shift+Tab is one undo transaction",
               editor.toPlainText() == source);
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        editor.setPlainText(QStringLiteral("a\nbb"));
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents();
        setCursor(editor, 0);

        const QTextBlock secondBlock =
            editor.document()->findBlockByNumber(1);
        QTextCursor secondEnd(editor.document());
        secondEnd.setPosition(
            secondBlock.position()
            + static_cast<int>(
                secondBlock.text().size()));
        const QRect endRect =
            editor.cursorRect(secondEnd);
        const int spaceWidth =
            qMax(1,
                 editor.fontMetrics()
                     .horizontalAdvance(
                         QLatin1Char(' ')));
        const QPoint beyondEnd(
            endRect.left() + 4 * spaceWidth,
            endRect.center().y());

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::ShiftModifier
                | Qt::AltModifier,
            beyondEnd);
        expect("Shift+Alt mouse gesture remains column selection",
               editor.editorModeActiveForTest(
                   EditorModeId::ColumnSelection)
                   && !editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor)
                    && editor.toPlainText()
                           == QStringLiteral("a\nbb"));
        const QList<ResolvedEditorAnnotation>
            columnAnnotations =
                annotationsOfKind(
                    editor,
                    EditorAnnotationKind::ColumnCaret);
        expect("column selection publishes one unified caret fact per visible row",
               columnAnnotations.size() == 2
                   && columnAnnotations.at(0)
                              .annotation.range.firstLine
                          == 0
                   && columnAnnotations.at(1)
                              .annotation.range.firstLine
                          == 1);
        QTest::keyClick(
            &editor,
            Qt::Key_Escape);
        expect("exiting column selection removes its annotation source",
               annotationsOfKind(
                   editor,
                   EditorAnnotationKind::ColumnCaret)
                   .isEmpty());
        setCursor(editor, 0);

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            beyondEnd);
        expect("Alt click beyond EOL creates a virtual multi-caret hint",
               editor.editorModeActiveForTest(
                   EditorModeId::MultiCursor)
                   && editor.toPlainText()
                          == QStringLiteral("a\nbb"));
        sendTextKey(editor,
                    Qt::Key_X,
                    QStringLiteral("x"));
        const QStringList lines =
            editor.toPlainText().split(
                QLatin1Char('\n'));
        const QString virtualLine =
            lines.size() == 2
            ? lines.at(1) : QString();
        const QString virtualPadding =
            virtualLine.size() >= 3
            ? virtualLine.mid(
                  2,
                  virtualLine.size() - 3)
            : QStringLiteral("not-padding");
        expect("virtual space materializes independently only on input",
               lines.size() == 2
                   && lines.at(0)
                          == QStringLiteral("xa")
                   && virtualLine.startsWith(
                       QStringLiteral("bb"))
                   && virtualLine.endsWith(
                       QLatin1Char('x'))
                   && !virtualPadding.isEmpty()
                   && virtualPadding.trimmed().isEmpty());
        editor.undo();
        expect("Alt virtual multi-caret input is one undo transaction",
               editor.toPlainText()
                   == QStringLiteral("a\nbb"));
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
                QStringLiteral("row_%1\n").arg(line));
        }
        editor.setPlainText(source);
        editor.show();
        editor.setFocus();
        setCursor(editor, 0);
        if (QScrollBar* scrollBar =
                editor.verticalScrollBar()) {
            scrollBar->setValue(
                scrollBar->maximum());
        }
        QCoreApplication::processEvents();

        const QTextBlock firstVisible =
            editor.cursorForPosition(QPoint(0, 0)).block();
        const QTextBlock lastVisible =
            editor.cursorForPosition(
                QPoint(
                    0,
                    qMax(
                        0,
                        editor.viewport()->height() - 2)))
                .block();
        QPoint endpointPoint =
            editor.viewport()->rect().center();
        if (lastVisible.isValid()) {
            QTextCursor endpoint(lastVisible);
            endpoint.setPosition(
                lastVisible.position()
                + qMin(
                    2,
                    static_cast<int>(
                        lastVisible.text().size())));
            endpointPoint =
                editor.cursorRect(endpoint).center();
        }
        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::ShiftModifier
                | Qt::AltModifier,
            endpointPoint);
        QCoreApplication::processEvents();

        const QList<ResolvedEditorAnnotation>
            visibleColumnAnnotations =
                annotationsOfKind(
                    editor,
                    EditorAnnotationKind::ColumnCaret);
        bool onlyVisibleRows =
            !visibleColumnAnnotations.isEmpty();
        for (const ResolvedEditorAnnotation& resolved :
             visibleColumnAnnotations) {
            const int line =
                resolved.annotation.range.firstLine;
            onlyVisibleRows =
                onlyVisibleRows
                && line >= firstVisible.blockNumber()
                && line <= lastVisible.blockNumber();
        }
        expect("large column selections produce only visible-row annotations",
               editor.columnSelectionActive()
                   && firstVisible.isValid()
                   && lastVisible.isValid()
                   && lastVisible.blockNumber()
                          > firstVisible.blockNumber()
                   && visibleColumnAnnotations.size()
                          <= lastVisible.blockNumber()
                                 - firstVisible.blockNumber()
                                 + 1
                   && visibleColumnAnnotations.size()
                          < lastVisible.blockNumber()
                   && onlyVisibleRows);
    }

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
