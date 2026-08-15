#include "editormodecontroller.h"
#include "editormulticursorcontroller.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

#include <QApplication>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
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

EditorMultiCursorCaret point(int position,
                             int virtualColumn = -1)
{
    return EditorMultiCursorCaret{
        position,
        position,
        virtualColumn,
    };
}

EditorMultiCursorCaret selection(int start, int end)
{
    return EditorMultiCursorCaret{start, end, -1};
}

void sendTextKey(MyCodeEditor& editor,
                 int key,
                 Qt::KeyboardModifiers modifiers,
                 const QString& text)
{
    QKeyEvent event(QEvent::KeyPress,
                    key,
                    modifiers,
                    text);
    QApplication::sendEvent(&editor, &event);
    QApplication::processEvents();
}

QPoint pointBeyondLineEnd(MyCodeEditor& editor,
                          int line,
                          int spaceColumns)
{
    const QTextBlock block =
        editor.document()->findBlockByNumber(line);
    QTextCursor end(block);
    end.setPosition(block.position() + block.text().size());
    const QRect endRect = editor.cursorRect(end);
    const int spaceWidth = qMax(
        1,
        editor.fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    return QPoint(
        endRect.left() + qMax(1, spaceColumns) * spaceWidth,
        endRect.center().y());
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    {
        MyCodeEditor editor;
        const QString original = QStringLiteral("abc\n");
        editor.resize(520, 160);
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.setPlainText(original);
        editor.document()->setModified(false);
        const QTextBlock block = editor.document()->firstBlock();
        QTextCursor end(block);
        end.setPosition(block.position() + block.text().size());
        QTextCursor initial(block);
        initial.setPosition(block.position() + 1);
        editor.setTextCursor(initial);
        editor.show();
        editor.setFocus();
        QApplication::processEvents();

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::NoModifier,
            pointBeyondLineEnd(editor, 0, 4));
        expect("ordinary click beyond EOL stays at the real EOL",
               !editor.virtualCursorActiveForTest()
                   && editor.textCursor().position()
                          == block.position() + block.text().size()
                   && editor.toPlainText() == original
                   && !editor.document()->isModified());

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            pointBeyondLineEnd(editor, 0, 4));
        const int virtualColumn =
            editor.virtualCursorColumnForTest();
        expect("Alt click beyond the current EOL retains a virtual cursor",
               editor.virtualCursorActiveForTest()
                   && virtualColumn > block.text().size()
                   && editor.toPlainText() == original
                   && !editor.document()->isModified());

        QTextCursor realPosition(block);
        realPosition.setPosition(block.position() + 1);
        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::NoModifier,
            editor.cursorRect(realPosition).center());
        expect("ordinary click on real text cancels the virtual caret",
               !editor.virtualCursorActiveForTest()
                   && editor.textCursor().position()
                          == realPosition.position()
                   && editor.toPlainText() == original
                   && !editor.document()->isModified());

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            pointBeyondLineEnd(editor, 0, 4));
        expect("Alt click replaces a real caret with one virtual caret",
               editor.virtualCursorActiveForTest()
                   && editor.virtualCursorColumnForTest() == virtualColumn);

        QTest::keyClicks(&editor, QStringLiteral("X"));
        const QString firstLine =
            editor.toPlainText().section(QLatin1Char('\n'), 0, 0);
        const QString padding =
            firstLine.size() > 4
            ? firstLine.mid(3, firstLine.size() - 4)
            : QString();
        expect("typing materializes only the retained virtual padding",
               firstLine.startsWith(QStringLiteral("abc"))
                   && firstLine.endsWith(QLatin1Char('X'))
                   && !padding.isEmpty()
                   && padding.trimmed().isEmpty()
                   && !editor.virtualCursorActiveForTest());
        editor.undo();
        expect("same-EOL virtual input is one undo transaction",
               editor.toPlainText() == original);

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            pointBeyondLineEnd(editor, 0, 4));
        QTest::keyClick(&editor, Qt::Key_Escape);
        expect("Escape cancels a virtual caret without editing",
               !editor.virtualCursorActiveForTest()
                   && editor.toPlainText() == original);

        editor.setTextCursor(end);
        editor.document()->setModified(false);
        QTest::keyClick(&editor, Qt::Key_Right);
        expect("Right at real EOL uses ordinary Qt navigation",
               !editor.virtualCursorActiveForTest()
                   && editor.textCursor().blockNumber() == 1
                   && editor.textCursor().positionInBlock() == 0
                   && editor.toPlainText() == original
                   && !editor.document()->isModified());
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.setPlainText(QStringLiteral("abc\ndef\n"));
        QTextCursor first(editor.document()->firstBlock());
        first.setPosition(first.block().position() + 1);
        const QTextBlock second =
            editor.document()->findBlockByNumber(1);
        QTextCursor secondCaret(second);
        secondCaret.setPosition(second.position() + 1);
        editor.setTextCursor(first);
        editor.show();
        editor.setFocus();
        QApplication::processEvents();

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            editor.cursorRect(secondCaret).center());
        expect("real Alt click establishes the prerequisite multi-cursor",
               editor.editorModeActiveForTest(EditorModeId::MultiCursor));
        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            pointBeyondLineEnd(editor, 1, 6));
        expect("Alt EOL replaces an existing multi-cursor with one virtual caret",
               editor.virtualCursorActiveForTest()
                   && !editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor));
        QTest::keyClicks(&editor, QStringLiteral("X"));
        const QString replacementFirst =
            editor.toPlainText().section(QLatin1Char('\n'), 0, 0);
        const QString replacementSecond =
            editor.toPlainText().section(QLatin1Char('\n'), 1, 1);
        const QString replacementPadding =
            replacementSecond.mid(
                3, qMax(0, replacementSecond.size() - 4));
        expect("replacement virtual caret materializes only its target",
               replacementFirst == QStringLiteral("abc")
                   && replacementSecond.startsWith(QStringLiteral("def"))
                   && replacementSecond.endsWith(QLatin1Char('X'))
                   && !replacementPadding.isEmpty()
                   && replacementPadding.trimmed().isEmpty());
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.setPlainText(QStringLiteral("abc\ndef\n"));
        editor.show();
        editor.setFocus();
        QApplication::processEvents();

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::NoModifier,
            pointBeyondLineEnd(editor, 0, 6));
        QTest::keyClick(&editor, Qt::Key_Left);
        QTest::keyClick(&editor, Qt::Key_Right);
        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::ShiftModifier | Qt::AltModifier,
            pointBeyondLineEnd(editor, 1, 6));
        QTest::keyClicks(&editor, QStringLiteral("X"));
        expect("leaving a pending EOL anchor prevents stale virtual-column reuse",
               editor.toPlainText()
                   == QStringLiteral("abcX\ndefX\n"));
    }

    {
        QTextDocument replacement;
        MyCodeEditor editor;
        replacement.setDefaultFont(editor.font());
        replacement.setPlainText(QStringLiteral("abc\ndef\n"));
        editor.resize(520, 160);
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.setPlainText(QStringLiteral("abc\ndef\n"));
        editor.show();
        editor.setFocus();
        QApplication::processEvents();

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::NoModifier,
            pointBeyondLineEnd(editor, 0, 6));
        editor.attachSharedDocument(&replacement, 1);
        QTextCursor replacementEnd(replacement.firstBlock());
        replacementEnd.setPosition(
            replacementEnd.block().position()
            + replacementEnd.block().text().size());
        editor.setTextCursor(replacementEnd);
        QApplication::processEvents();
        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::ShiftModifier | Qt::AltModifier,
            pointBeyondLineEnd(editor, 1, 6));
        QTest::keyClicks(&editor, QStringLiteral("X"));
        const QString reboundText = editor.toPlainText();
        if (reboundText != QStringLiteral("abcX\ndefX\n")) {
            std::printf("[DIAG] rebound text: %s\n",
                        qPrintable(
                            QString(reboundText)
                                .replace(QLatin1Char('\n'),
                                         QStringLiteral("\\n"))));
        }
        expect("document rebind discards a pending EOL anchor",
               reboundText == QStringLiteral("abcX\ndefX\n"));
    }

    {
        MyCodeEditor editor;
        const QString original = QStringLiteral("abc\ndef\n");
        editor.resize(520, 160);
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.setPlainText(original);
        editor.document()->setModified(false);
        QTextCursor initial(editor.document()->firstBlock());
        initial.setPosition(initial.block().position() + 1);
        editor.setTextCursor(initial);
        editor.show();
        editor.setFocus();
        QApplication::processEvents();

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            pointBeyondLineEnd(editor, 1, 4));
        const int targetColumn = editor.virtualCursorColumnForTest();
        expect("Alt click beyond another line creates one virtual caret",
               editor.virtualCursorActiveForTest()
                   && editor.virtualCursorLineForTest() == 1
                   && targetColumn > 3
                   && editor.textCursor().blockNumber() == 1
                   && editor.textCursor().positionInBlock() == 3
                   && editor.toPlainText() == original
                   && !editor.document()->isModified());

        QTest::keyClicks(&editor, QStringLiteral("X"));
        const QString firstLine =
            editor.toPlainText().section(QLatin1Char('\n'), 0, 0);
        const QString secondLine =
            editor.toPlainText().section(QLatin1Char('\n'), 1, 1);
        const QString padding =
            secondLine.size() > 4
            ? secondLine.mid(3, secondLine.size() - 4)
            : QString();
        expect("single explicit virtual caret materializes only its target",
               firstLine == QStringLiteral("abc")
                   && secondLine.startsWith(QStringLiteral("def"))
                   && secondLine.endsWith(QLatin1Char('X'))
                   && !padding.isEmpty()
                   && padding.trimmed().isEmpty()
                   && !editor.virtualCursorActiveForTest());
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        editor.setPlainText(QStringLiteral("abc\ndef\n"));
        QTextCursor first(editor.document()->firstBlock());
        first.setPosition(first.block().position() + 1);
        const QTextBlock second =
            editor.document()->findBlockByNumber(1);
        QTextCursor secondCaret(second);
        secondCaret.setPosition(second.position() + 1);
        editor.setTextCursor(first);
        editor.show();
        editor.setFocus();
        QApplication::processEvents();

        QTest::mouseClick(
            editor.viewport(),
            Qt::LeftButton,
            Qt::AltModifier,
            editor.cursorRect(secondCaret).center());
        expect("Alt click on real text retains multi-cursor priority",
               editor.editorModeActiveForTest(EditorModeId::MultiCursor)
                   && !editor.virtualCursorActiveForTest());
        QTest::keyClicks(&editor, QStringLiteral("X"));
        expect("real Alt-click caret edits both physical positions",
               editor.toPlainText()
                   == QStringLiteral("aXbc\ndXef\n"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("abcdef"));
        controller.bind(&modes, &editor);

        expect("overlapping selections normalize to one edit range",
               controller.setCarets(
                   {selection(1, 4),
                    selection(3, 5),
                    point(6),
                    point(6)},
                   0)
                   && controller.caretCount() == 2
                   && controller.snapshot()
                          .carets.at(0)
                          .selectionStart()
                          == 1
                   && controller.snapshot()
                          .carets.at(0)
                          .selectionEnd()
                          == 5);
        expect("multiple carets enter the centralized mode",
               controller.active()
                   && modes.isActive(
                       EditorModeId::MultiCursor));
        expect("Esc retains only the primary caret",
               controller.escapeToSingleCursor()
                   && controller.caretCount() == 1
                   && !modes.isActive(
                       EditorModeId::MultiCursor)
                   && editor.textCursor().selectedText()
                          == QStringLiteral("bcde"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(
            QStringLiteral("sig sig sig other"));
        controller.bind(&modes, &editor);
        controller.setCarets({point(1)});
        const QList<EditorMultiCursorOccurrence> occurrences = {
            {0, 3},
            {4, 7},
            {8, 11},
            {8, 11},
        };

        expect("first next-occurrence core step selects current structured occurrence",
               controller.addNextOccurrence(
                   occurrences, 0, 11)
                   && controller.caretCount() == 1
                   && controller.snapshot()
                          .carets.constFirst()
                          .selectionStart()
                          == 0);
        expect("subsequent next-occurrence core steps add one occurrence",
               controller.addNextOccurrence(
                   occurrences, 0, 11)
                   && controller.caretCount() == 2
                   && controller.addNextOccurrence(
                       occurrences, 0, 11)
                   && controller.caretCount() == 3
                   && !controller.addNextOccurrence(
                       occurrences, 0, 11));
        expect("scope selection filters caller-provided structured ranges",
               controller.selectAllOccurrences(
                   occurrences, 4, 11)
                       == 2
                   && controller.snapshot()
                          .carets.constFirst()
                          .selectionStart()
                          == 4);
    }

    {
        MyCodeEditor editor;
        editor.resize(640, 240);
        const QString source = QStringLiteral(
            "module occurrence_route;\n"
            "  logic sig;\n"
            "  always_comb begin\n"
            "    sig = sig + 1;\n"
            "    sig = sig + 2;\n"
            "  end\n"
            "endmodule\n");
        editor.setPlainText(source);
        editor.show();
        editor.setFocus();
        QApplication::processEvents();

        QTextCursor numericCursor(editor.document());
        numericCursor.setPosition(
            source.indexOf(QStringLiteral("+ 1")) + 2);
        QTest::mouseMove(
            editor.viewport(),
            editor.cursorRect(numericCursor).center());
        QTest::keyPress(&editor, Qt::Key_Control);
        QApplication::processEvents();
        expect("Control hover enters transient SourceNavigation",
               editor.editorModeActiveForTest(
                   EditorModeId::SourceNavigation));
        QTest::keyRelease(&editor, Qt::Key_Control);

        const int firstAssignment =
            source.indexOf(QStringLiteral("sig ="));
        QTextCursor occurrenceCursor(editor.document());
        occurrenceCursor.setPosition(firstAssignment + 1);
        editor.setTextCursor(occurrenceCursor);
        QTest::keyClick(
            &editor,
            Qt::Key_D,
            Qt::ControlModifier);
        const QString duplicatedSource = QString(source)
            .insert(source.indexOf(QLatin1Char('\n'), firstAssignment) + 1,
                    QStringLiteral("    sig = sig + 1;\n"));
        expect("Ctrl+D duplicate preempts transient SourceNavigation",
               editor.toPlainText() == duplicatedSource
                   && !editor.editorModeActiveForTest(
                       EditorModeId::SourceNavigation)
                   && !editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor));
        editor.undo();
        expect("Ctrl+D duplicate is one undo transaction",
               editor.toPlainText() == source);

        occurrenceCursor.setPosition(firstAssignment + 1);
        editor.setTextCursor(occurrenceCursor);
        QString occurrenceFailure;
        expect("next-occurrence command selects then enters MultiCursor",
               editor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty()
                   && editor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty()
                   && editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor));
        QTest::keyClicks(&editor, QStringLiteral("x"));
        const QString replaced = QString(source)
            .replace(firstAssignment, 3, QStringLiteral("x"))
            .replace(firstAssignment + 4, 3, QStringLiteral("x"));
        expect("next-occurrence command distributes replacement structurally",
               editor.toPlainText() == replaced);
        editor.undo();
        expect("next-occurrence replacement is one undo transaction",
               editor.toPlainText() == source);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("aa bb"));
        controller.bind(&modes, &editor);
        controller.setCarets({point(1), point(4)});

        expect("text is inserted at independent carets",
               controller.insertText(
                   QStringLiteral("X"))
                   && editor.toPlainText()
                          == QStringLiteral("aXa bXb"));
        const EditorMultiCursorSnapshot snapshot =
            controller.snapshot();
        expect("caret positions remap after descending edits",
               snapshot.carets.size() == 2
                   && snapshot.carets.at(0).position == 2
                   && snapshot.carets.at(1).position == 6);
        editor.undo();
        expect("multi-caret text insertion is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("aa bb"));

        editor.setPlainText(QStringLiteral("abcde"));
        controller.setCarets({point(2), point(5)});
        expect("Backspace is distributed across carets",
               controller.backspace()
                   && editor.toPlainText()
                          == QStringLiteral("acd"));
        editor.undo();
        expect("distributed Backspace is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("abcde"));

        controller.setCarets({point(1), point(3)});
        expect("Delete is distributed across carets",
               controller.deleteForward()
                   && editor.toPlainText()
                          == QStringLiteral("ace"));
        editor.undo();
        expect("distributed Delete is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("abcde"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral("x\ny");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        controller.setCarets({point(1), point(3)});

        expect("opening parentheses pair at every independent caret",
               controller.insertTextStructurally(
                   QStringLiteral("("), &syntax)
                   && editor.toPlainText()
                          == QStringLiteral("x()\ny()")
                   && controller.snapshot()
                          .carets.at(0).position == 2
                   && controller.snapshot()
                          .carets.at(1).position == 6);
        editor.undo();
        expect("distributed parenthesis pairing is one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral("x\ny");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        controller.setCarets({point(1), point(3)});

        expect("double quotes pair at every independent caret",
               controller.insertTextStructurally(
                   QStringLiteral("\""), &syntax)
                   && editor.toPlainText()
                          == QStringLiteral("x\"\"\ny\"\"")
                   && controller.snapshot()
                          .carets.at(0).position == 2
                   && controller.snapshot()
                          .carets.at(1).position == 6);
        editor.undo();
        expect("distributed quote pairing is one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral("aa bb");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        controller.setCarets(
            {selection(0, 2), selection(3, 5)});

        expect("opening brackets surround every independent selection",
               controller.insertTextStructurally(
                   QStringLiteral("["), &syntax)
                   && editor.toPlainText()
                          == QStringLiteral("[aa] [bb]")
                   && controller.snapshot()
                          .carets.at(0).position == 4
                   && controller.snapshot()
                          .carets.at(1).position == 9);
        editor.undo();
        expect("distributed selection surrounding is one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral(
                "module m;\n"
                "logic foo;\n"
                "assign foo = foo;\n"
                "endmodule\n");
        editor.setPlainText(original);
        const int firstFoo =
            original.indexOf(
                QStringLiteral("foo"));
        QTextCursor cursor(editor.document());
        cursor.setPosition(firstFoo + 1);
        editor.setTextCursor(cursor);
        QString failure;

        const bool selected =
            editor.selectAllSymbolOccurrences(
                &failure);
        sendTextKey(editor,
                    Qt::Key_9,
                    Qt::ShiftModifier,
                    QStringLiteral("("));
        expect("runtime routes paired input through the multi-cursor controller",
               selected
                   && failure.isEmpty()
                   && editor.toPlainText()
                          == QStringLiteral(
                              "module m;\n"
                              "logic (foo);\n"
                              "assign (foo) = (foo);\n"
                              "endmodule\n"));
        editor.undo();
        expect("runtime multi-cursor pairing is one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        controller.bind(&modes, &editor);
        const QList<QPair<QString, QChar>> cases = {
            {QStringLiteral("() ()"), QLatin1Char(')')},
            {QStringLiteral("[] []"), QLatin1Char(']')},
            {QStringLiteral("{} {}"), QLatin1Char('}')},
            {QStringLiteral("\"\" \"\""), QLatin1Char('"')},
        };
        bool allClosersSkipped = true;
        for (const auto& entry : cases) {
            editor.setPlainText(entry.first);
            TSDocument syntax;
            syntax.setText(entry.first);
            controller.setCarets(
                {point(1), point(4)});
            allClosersSkipped =
                controller.insertTextStructurally(
                    QString(entry.second), &syntax)
                && editor.toPlainText() == entry.first
                && controller.snapshot()
                       .carets.at(0).position == 2
                && controller.snapshot()
                       .carets.at(1).position == 5
                && allClosersSkipped;
        }
        expect("all supported existing closers are skipped per caret",
               allClosersSkipped);

        const QString mixed =
            QStringLiteral("() x");
        editor.setPlainText(mixed);
        TSDocument mixedSyntax;
        mixedSyntax.setText(mixed);
        controller.setCarets({point(1), point(4)});
        expect("one closer input can skip and insert independently",
               controller.insertTextStructurally(
                   QStringLiteral(")"),
                   &mixedSyntax)
                   && editor.toPlainText()
                          == QStringLiteral("() x)")
                   && controller.snapshot()
                          .carets.at(0).position == 2
                   && controller.snapshot()
                          .carets.at(1).position == 5);
        editor.undo();
        expect("mixed closer insertion remains one undo unit",
               editor.toPlainText() == mixed);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral("// a\n\"ss\"");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        controller.setCarets({point(4), point(7)});

        expect("Tree-sitter literal contexts suppress automatic pairing",
               controller.insertTextStructurally(
                   QStringLiteral("("), &syntax)
                   && editor.toPlainText()
                          == QStringLiteral(
                              "// a(\n\"s(s\""));
        editor.undo();
        expect("literal-context distributed input is one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("A\nB\nC"));
        controller.bind(&modes, &editor);
        controller.setCarets(
            {point(1), point(3), point(5)});

        expect("matching clipboard rows distribute by caret order",
               controller.pasteText(
                   QStringLiteral("one\ntwo\nthree"))
                   && editor.toPlainText()
                          == QStringLiteral(
                              "Aone\nBtwo\nCthree"));
        editor.undo();
        expect("distributed paste is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("A\nB\nC"));

        editor.setPlainText(QStringLiteral("a1 b2"));
        controller.setCarets(
            {selection(0, 2), selection(3, 5)});
        const EditorMultiCursorSnapshot beforeEmptyPaste =
            controller.snapshot();
        const bool emptyPasteHandled =
            controller.pasteText(QString());
        const EditorMultiCursorSnapshot afterEmptyPaste =
            controller.snapshot();
        expect("empty clipboard leaves multi-caret selections untouched",
               !emptyPasteHandled
                   && editor.toPlainText()
                          == QStringLiteral("a1 b2")
                   && afterEmptyPaste.carets.size() == 2
                   && beforeEmptyPaste.carets.size() == 2
                   && afterEmptyPaste.carets.at(0).anchor
                          == beforeEmptyPaste.carets.at(0).anchor
                   && afterEmptyPaste.carets.at(0).position
                          == beforeEmptyPaste.carets.at(0).position
                   && afterEmptyPaste.carets.at(1).anchor
                          == beforeEmptyPaste.carets.at(1).anchor
                   && afterEmptyPaste.carets.at(1).position
                          == beforeEmptyPaste.carets.at(1).position
                   && afterEmptyPaste.primaryIndex
                          == beforeEmptyPaste.primaryIndex
                   && !editor.document()->isUndoAvailable());
        QString copied;
        expect("copy and cut preserve per-caret row order",
               controller.cutSelections(&copied)
                   && copied == QStringLiteral("a1\nb2")
                   && editor.toPlainText()
                          == QStringLiteral(" "));
        editor.undo();
        expect("distributed cut is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("a1 b2"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        const QString original =
            QStringLiteral("alpha\nbeta\ngamma");
        editor.setPlainText(original);
        controller.bind(&modes, &editor);
        controller.setCarets(
            {point(1), point(3), point(8)});

        expect("point carets copy whole lines once per line",
               controller.copySelections()
                   == QStringLiteral(
                       "alpha\nbeta\n"));
        QString copied;
        expect("point carets cut deduplicated whole lines",
               controller.cutSelections(&copied)
                   && copied
                          == QStringLiteral(
                              "alpha\nbeta\n")
                   && editor.toPlainText()
                          == QStringLiteral("gamma"));
        editor.undo();
        expect("whole-line multi-caret cut is one undo unit",
               editor.toPlainText() == original);

        editor.setPlainText(
            QStringLiteral("alpha\nbeta"));
        controller.setCarets({point(8)});
        expect("cutting the final whole line removes its preceding newline",
               controller.cutSelections(&copied)
                   && copied
                          == QStringLiteral("beta\n")
                   && editor.toPlainText()
                          == QStringLiteral("alpha"));
        editor.undo();
        expect("final-line cut is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("alpha\nbeta"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("x\ny"));
        controller.bind(&modes, &editor);
        controller.setCarets({point(1), point(3)});

        expect("whole-line clipboard trailing newline still distributes",
               controller.pasteText(
                   QStringLiteral("left\nright\n"))
                   && editor.toPlainText()
                          == QStringLiteral(
                              "xleft\nyright"));
        editor.undo();
        expect("whole-line clipboard distribution is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("x\ny"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        const QString original =
            QStringLiteral(
                "    one\n    two\nzero");
        editor.setPlainText(original);
        controller.bind(&modes, &editor);
        controller.setCarets(
            {point(5), point(7),
             point(13), point(20)});

        expect("Shift+Tab core unindents each affected line once",
               controller.unindent()
                   && editor.toPlainText()
                          == QStringLiteral(
                              "one\ntwo\nzero"));
        const EditorMultiCursorSnapshot shifted =
            controller.snapshot();
        expect("unindent remaps every independent caret",
               shifted.carets.size() == 4
                   && shifted.carets.at(0).position == 1
                   && shifted.carets.at(1).position == 3
                   && shifted.carets.at(2).position == 5
                   && shifted.carets.at(3).position == 12);
        editor.undo();
        expect("multi-caret Shift+Tab is one undo unit",
               editor.toPlainText() == original);

        editor.setPlainText(
            QStringLiteral("    abc\n    def"));
        controller.setCarets({selection(4, 15)});
        expect("Shift+Tab preserves a multi-line logical selection",
               controller.unindent()
                   && editor.toPlainText()
                          == QStringLiteral("abc\ndef")
                   && controller.snapshot()
                          .carets.constFirst().anchor
                          == 0
                   && controller.snapshot()
                          .carets.constFirst().position
                          == 7);
        editor.undo();
        expect("selection unindent is one undo unit",
               editor.toPlainText()
                   == QStringLiteral(
                       "    abc\n    def"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("abc\ndef"));
        controller.bind(&modes, &editor);
        controller.setCarets({point(1), point(5)});

        expect("movement advances every caret independently",
               controller.moveCarets(
                   EditorMultiCursorMove::Right)
                   && controller.snapshot()
                          .carets.at(0).position
                          == 2
                   && controller.snapshot()
                          .carets.at(1).position
                          == 6);
        expect("reverse movement restores every caret",
               controller.moveCarets(
                   EditorMultiCursorMove::Left)
                   && controller.snapshot()
                          .carets.at(0).position
                          == 1
                   && controller.snapshot()
                          .carets.at(1).position
                          == 5);
        expect("selection movement keeps an anchor per caret",
               controller.moveCarets(
                   EditorMultiCursorMove::Right,
                   true)
                   && controller.snapshot()
                          .carets.at(0).anchor
                          == 1
                   && controller.snapshot()
                          .carets.at(0).position
                          == 2
                   && controller.snapshot()
                          .carets.at(1).anchor
                          == 5
                   && controller.snapshot()
                          .carets.at(1).position
                          == 6);
        expect("line movement collapses each selection at its own line end",
               controller.moveCarets(
                   EditorMultiCursorMove::LineEnd)
                   && controller.snapshot()
                          .carets.at(0).position
                          == 3
                   && controller.snapshot()
                          .carets.at(1).position
                          == 7);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("a\nbb"));
        controller.bind(&modes, &editor);
        controller.setCarets(
            {point(1, 4), point(4, 5)});

        expect("virtual Backspace changes each hint without materializing text",
               controller.backspace()
                   && editor.toPlainText()
                          == QStringLiteral("a\nbb")
                   && controller.snapshot()
                          .carets.at(0)
                          .virtualColumn
                          == 3
                   && controller.snapshot()
                          .carets.at(1)
                          .virtualColumn
                          == 4);
        expect("typing materializes independent virtual padding",
               controller.insertText(
                   QStringLiteral("x"))
                   && editor.toPlainText()
                          == QStringLiteral(
                              "a  x\nbb  x"));
        editor.undo();
        expect("virtual padding and typed text share one undo unit",
               editor.toPlainText()
                   == QStringLiteral("a\nbb"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral("a\nbb");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        controller.setCarets(
            {point(1, 4), point(4, 5)});

        expect("paired braces materialize each virtual column independently",
               controller.insertTextStructurally(
                   QStringLiteral("{"), &syntax)
                   && editor.toPlainText()
                          == QStringLiteral(
                              "a   {}\nbb   {}")
                   && controller.snapshot()
                          .carets.at(0).position == 5
                   && controller.snapshot()
                          .carets.at(1).position == 13);
        editor.undo();
        expect("virtual padding and paired braces share one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("ab"));
        controller.bind(&modes, &editor);
        controller.setCarets({point(0), point(2)});

        expect("Enter is distributed through one controller operation",
               controller.insertNewline()
                   && editor.toPlainText()
                          == QStringLiteral("\nab\n"));
        editor.undo();
        expect("distributed Enter is one undo unit",
               editor.toPlainText()
                   == QStringLiteral("ab"));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral(
                "module m;\n"
                "initial begin\n"
                "    logic x;\n"
                "endmodule\n");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        const int beginCaret =
            original.indexOf(
                QStringLiteral("begin"))
            + 5;
        const int declarationCaret =
            original.indexOf(
                QStringLiteral("logic x;"))
            + 8;
        const TSStructuralNewlineTarget beginPlan =
            syntax.structuralNewlineTarget(
                beginCaret, 4);
        const TSStructuralNewlineTarget declarationPlan =
            syntax.structuralNewlineTarget(
                declarationCaret, 4);
        expect("Tree-sitter identifies the incomplete begin plan",
               beginPlan.ok()
                   && beginPlan.insertedClosingKeyword
                   && declarationPlan.ok());
        controller.setCarets(
            {point(beginCaret),
             point(declarationCaret)});

        QString expected = original;
        expected.insert(
            declarationCaret,
            declarationPlan.insertionText);
        expected.insert(
            beginCaret,
            beginPlan.insertionText);
        expect("Enter applies each caret's Tree-sitter structural plan",
               controller.insertStructuralNewline(
                   &syntax, 4)
                   && editor.toPlainText() == expected
                   && controller.snapshot()
                          .carets.at(0).position
                          == beginCaret
                             + beginPlan.caretOffset
                   && controller.snapshot()
                          .carets.at(1).position
                          == declarationCaret
                             + beginPlan.insertionText.size()
                             + declarationPlan.caretOffset);
        editor.undo();
        expect("multi-caret structural Enter is one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral("module m;\n"
                           "initial begin\n"
                           "    logic a;\n"
                           "    logic b;\n"
                           "end\n"
                           "endmodule\n");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        const int firstLineStart =
            original.indexOf(QStringLiteral("    logic a;"));
        const int secondLineStart =
            original.indexOf(QStringLiteral("    logic b;"));
        controller.setCarets(
            {point(firstLineStart), point(secondLineStart)});
        QString expected = original;
        expected.insert(secondLineStart, QLatin1Char('\n'));
        expected.insert(firstLineStart, QLatin1Char('\n'));
        expect("multi-caret Enter at physical line starts does not stack indentation",
               controller.insertStructuralNewline(&syntax, 4)
                   && editor.toPlainText() == expected);
        editor.undo();
        expect("physical-line multi-caret Enter is one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        TSDocument syntax;
        const QString original =
            QStringLiteral("a\nbb");
        editor.setPlainText(original);
        syntax.setText(original);
        controller.bind(&modes, &editor);
        controller.setCarets(
            {point(1, 4), point(4, 5)});

        expect("structural Enter materializes independent virtual columns",
               controller.insertStructuralNewline(
                   &syntax, 4)
                   && editor.toPlainText()
                          == QStringLiteral(
                              "a   \n\nbb   \n"));
        editor.undo();
        expect("virtual padding and structural Enter share one undo unit",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        const QString source = QStringLiteral(
            "module readonly_keyword;\n"
            "  always_comb begin\n"
            "    beg\n"
            "  end\n"
            "endmodule\n");
        editor.setPlainText(source);
        editor.resize(480, 220);
        editor.show();
        editor.setFocus();
        QTextCursor cursor(editor.document());
        const int prefixEnd =
            source.lastIndexOf(QStringLiteral("beg")) + 3;
        cursor.setPosition(prefixEnd);
        editor.setTextCursor(cursor);
        QApplication::processEvents();
        expect("editable keyword prefix enters ghost mode",
               editor.editorModeActiveForTest(
                   EditorModeId::KeywordGhost));

        editor.setReadOnly(true);
        QTest::keyClick(&editor, Qt::Key_Tab);
        QApplication::processEvents();
        expect("read-only keyword ghost leaves source text unchanged",
               editor.toPlainText() == source);
        expect("read-only keyword ghost exits without accepting",
               !editor.editorModeActiveForTest(
                   EditorModeId::KeywordGhost));

        cursor.setPosition(0);
        editor.setTextCursor(cursor);
        cursor.setPosition(prefixEnd);
        editor.setTextCursor(cursor);
        QApplication::processEvents();
        expect("read-only keyword prefix does not republish a ghost",
               !editor.editorModeActiveForTest(
                   EditorModeId::KeywordGhost));
    }

    {
        MyCodeEditor editor;
        EditorModeController modes;
        EditorMultiCursorController controller;
        editor.setPlainText(QStringLiteral("read only"));
        controller.bind(&modes, &editor);
        controller.setCarets({point(0), point(5)});
        editor.setReadOnly(true);
        TSDocument syntax;
        syntax.setText(editor.toPlainText());

        expect("read-only views reject multi-cursor document edits",
               !controller.insertText(
                   QStringLiteral("x"))
                   && !controller.insertTextStructurally(
                       QStringLiteral("("),
                       &syntax)
                   && !controller.insertStructuralNewline(
                       &syntax, 4)
                   && !controller.cutSelections()
                   && !controller.unindent()
                   && editor.toPlainText()
                          == QStringLiteral("read only"));
    }

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
