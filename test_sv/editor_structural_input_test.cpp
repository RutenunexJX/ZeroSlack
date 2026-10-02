#include "annotationlayer.h"
#include "columnnumbertool.h"
#include "editorcolumnmodecontroller.h"
#include "editorlexicalboundary.h"
#include "editormodecontroller.h"
#include "editormulticursorcontroller.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

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

int selectedTextCount(const MyCodeEditor& editor,
                      const QString& text)
{
    int count = 0;
    for (const QTextEdit::ExtraSelection& selection :
         editor.extraSelections()) {
        if (selection.cursor.selectedText() == text)
            ++count;
    }
    return count;
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    {
        const QString text = QStringLiteral("aaa_bbb_ccc AXIReadData data32Width");
        const EditorLexicalBoundary::Range bbb =
            EditorLexicalBoundary::unitAt(text, 5);
        const EditorLexicalBoundary::Range acronym =
            EditorLexicalBoundary::unitAt(text, 13);
        const EditorLexicalBoundary::Range read =
            EditorLexicalBoundary::unitAt(text, 16);
        expect("shared lexical boundaries split underscore fragments",
               bbb.isValid()
                   && text.mid(bbb.start, bbb.length())
                          == QStringLiteral("bbb"));
        expect("shared lexical boundaries split acronym and camel fragments",
               acronym.isValid()
                   && text.mid(acronym.start, acronym.length())
                          == QStringLiteral("AXI")
                   && read.isValid()
                   && text.mid(read.start, read.length())
                          == QStringLiteral("Read"));
        expect("Ctrl-style movement skips underscore and horizontal whitespace",
               EditorLexicalBoundary::moveRight(text, 0) == 4
                   && EditorLexicalBoundary::moveLeft(text, 11) == 8
                   && EditorLexicalBoundary::moveRight(text, 8) == 12);
        const EditorLexicalBoundary::Range backward =
            EditorLexicalBoundary::deleteBackward(text, 11);
        expect("Ctrl-style deletion removes one adjacent fragment",
               backward.isValid()
                   && text.mid(backward.start, backward.length())
                          == QStringLiteral("ccc"));
        const EditorLexicalBoundary::Range whitespace =
            EditorLexicalBoundary::horizontalWhitespaceAt(
                QStringLiteral("a  \t b"), 2);
        expect("horizontal whitespace is one line-local lexical unit",
               whitespace.start == 1 && whitespace.end == 5);

        const QString numericText = QStringLiteral(
            "16'hff_00 1.25e-3 10ns 'x data32Width");
        const auto literalText = [&numericText](int position) {
            const EditorLexicalBoundary::Range range =
                EditorLexicalBoundary::unitAt(numericText, position);
            return range.isValid()
                ? numericText.mid(range.start, range.length())
                : QString();
        };
        expect("based numeric literal is one lexical unit",
               literalText(4) == QStringLiteral("16'hff_00"));
        expect("real exponent literal is one lexical unit",
               literalText(numericText.indexOf(QStringLiteral("25")))
                   == QStringLiteral("1.25e-3"));
        expect("time literal is one lexical unit",
               literalText(numericText.indexOf(QStringLiteral("10ns")) + 2)
                   == QStringLiteral("10ns"));
        expect("unbased four-state literal is one lexical unit",
               literalText(numericText.indexOf(QStringLiteral("'x")) + 1)
                   == QStringLiteral("'x"));
    }

    {
        MyCodeEditor editor;
        const QString source = QStringLiteral(
            "module top #(\n"
            "    parameter int WIDTH = 8,\n"
            "    parameter logic MODE = 1\n"
            ")(\n"
            "    input logic [WIDTH - 1:0] data_i,\n"
            "    output logic valid_o\n"
            ");\n"
            "child #(\n"
            "    .P_WIDTH(WIDTH),\n"
            "    .P_MODE(MODE)\n"
            ") u_child (\n"
            "    .data_i(data_i),\n"
            "    .valid_o(valid_o)\n"
            ");\n"
            "always_comb begin\n"
            "    result = ready ? data_i : valid_o;\n"
            "    if (ready && valid_o) result = data_i;\n"
            "end\n"
            "endmodule\n");
        editor.setPlainText(source);
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents();

        const TSDocument* syntax = editor.syntaxDocument();
        const int moduleType = source.indexOf(QStringLiteral("child #"));
        const TSStructuralNavigationTarget parameterFormal =
            syntax->structuralNavigationTarget(
                moduleType,
                TSStructuralNavigationDirection::NextField);
        expect("Tree-sitter structural navigation exposes parameter formal fields",
               parameterFormal.ok()
                   && parameterFormal.role
                          == TSStructuralFieldRole::ParameterFormal
                   && source.mid(parameterFormal.startChar,
                                 parameterFormal.endChar
                                     - parameterFormal.startChar)
                          == QStringLiteral("P_WIDTH"));

        const TSStructuralNavigationTarget parameterActual =
            syntax->structuralNavigationTarget(
                parameterFormal.startChar,
                TSStructuralNavigationDirection::NextField);
        expect("Tree-sitter structural navigation separates formal and actual",
               parameterActual.ok()
                   && parameterActual.role
                          == TSStructuralFieldRole::ParameterActual
                   && source.mid(parameterActual.startChar,
                                 parameterActual.endChar
                                     - parameterActual.startChar)
                          == QStringLiteral("WIDTH"));

        const TSStructuralNavigationTarget nextParameterFormal =
            syntax->structuralNavigationTarget(
                parameterFormal.startChar,
                TSStructuralNavigationDirection::NextItem);
        expect("vertical structural navigation matches parameter field roles",
               nextParameterFormal.ok()
                   && nextParameterFormal.role
                          == TSStructuralFieldRole::ParameterFormal
                   && source.mid(nextParameterFormal.startChar,
                                 nextParameterFormal.endChar
                                     - nextParameterFormal.startChar)
                          == QStringLiteral("P_MODE"));

        const int firstPortActual = source.indexOf(
            QStringLiteral("data_i),"), moduleType);
        const TSStructuralNavigationTarget nextPortActual =
            syntax->structuralNavigationTarget(
                firstPortActual,
                TSStructuralNavigationDirection::NextItem);
        expect("vertical structural navigation matches port actual roles",
               nextPortActual.ok()
                   && nextPortActual.role
                          == TSStructuralFieldRole::PortActual
                   && source.mid(nextPortActual.startChar,
                                 nextPortActual.endChar
                                     - nextPortActual.startChar)
                          == QStringLiteral("valid_o"));

        setCursor(editor, moduleType);
        QTest::keyClick(&editor,
                        Qt::Key_Right,
                        Qt::ControlModifier | Qt::AltModifier);
        expect("Ctrl+Alt+Right moves to the next Tree-sitter field",
               editor.textCursor().position()
                   == parameterFormal.startChar);
        QTest::keyClick(&editor,
                        Qt::Key_Down,
                        Qt::ControlModifier | Qt::AltModifier);
        expect("Ctrl+Alt+Down moves to the same role in the next item",
               editor.textCursor().position()
                   == nextParameterFormal.startChar);
        QTest::keyClick(&editor,
                        Qt::Key_Right,
                        Qt::ControlModifier
                            | Qt::AltModifier
                            | Qt::ShiftModifier);
        expect("Shift extends selection through structural movement",
               editor.textCursor().hasSelection()
                   && editor.textCursor().selectedText().contains(
                       QStringLiteral("MODE")));

        const int ternaryCondition = source.indexOf(
            QStringLiteral("ready ?"));
        const TSStructuralNavigationTarget ternaryTrue =
            syntax->structuralNavigationTarget(
                ternaryCondition,
                TSStructuralNavigationDirection::NextField);
        expect("ternary branches are independent structural fields",
               ternaryTrue.ok()
                   && ternaryTrue.role
                          == TSStructuralFieldRole::TernaryTrue
                   && source.mid(ternaryTrue.startChar,
                                 ternaryTrue.endChar
                                     - ternaryTrue.startChar)
                          == QStringLiteral("data_i"));
    }

    {
        const ColumnNumberConfig octal =
            inferColumnNumberConfig(QStringLiteral("0o0017"));
        expect("column number inference supports C-like octal",
               octal.base == ColumnNumberBase::Oct
                   && octal.style == ColumnNumberStyle::CLike
                   && octal.start == 15
                   && octal.fixedDigitWidth
                   && octal.digitWidth == 4
                   && formatColumnNumber(octal.start, octal)
                          == QStringLiteral("0o0017"));
    }

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
        expect("Tab accepts the unique keyword suffix and trailing space",
               editor.toPlainText().mid(cursor - 3, 6)
                   == QStringLiteral("begin ")
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
            QStringLiteral("module m;\n"
                           "always_comb begin\n"
                           "    beg\n"
                           "    beg\n"
                           "end\n"
                           "endmodule\n");
        editor.setPlainText(original);
        const int cursor =
            original.indexOf(QStringLiteral("    beg")) + 7;
        setCursor(editor, cursor);
        QTest::keyClick(&editor,
                        Qt::Key_Down,
                        Qt::ShiftModifier | Qt::AltModifier);
        QTest::keyClick(&editor, Qt::Key_Tab);
        expect("Tab accepts structural keyword completion on every column row",
               editor.toPlainText()
                   == QStringLiteral("module m;\n"
                                     "always_comb begin\n"
                                     "    begin \n"
                                     "    begin \n"
                                     "end\n"
                                     "endmodule\n"));
        editor.undo();
        expect("column keyword completion is one undo transaction",
               editor.toPlainText() == original);
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral("module m;\n    logic value;\nendmodule\n");
        editor.setPlainText(original);
        setCursor(editor,
                  original.indexOf(QStringLiteral("value")) + 2);
        QTest::keyClick(&editor, Qt::Key_Backtab);
        expect("ordinary Shift+Tab unindents the current line",
               editor.toPlainText()
                   == QStringLiteral(
                       "module m;\nlogic value;\nendmodule\n"));
        editor.undo();
        expect("ordinary Shift+Tab is one undo transaction",
               editor.toPlainText() == original);
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
        const QString original =
            QStringLiteral("module m;\n"
                           "always_comb begin\n"
                           "    logic value;\n"
                           "end\n"
                           "endmodule\n");
        const int lineStart =
            original.indexOf(QStringLiteral("    logic value;"));
        const auto verifyEnter =
            [&editor, &original, lineStart](
                int indentOffset,
                Qt::Key key,
                const QString& expected,
                int expectedCursor) {
                editor.setPlainText(original);
                setCursor(editor, lineStart + indentOffset);
                QTest::keyClick(&editor, key);
                const bool inserted =
                    editor.toPlainText() == expected
                    && editor.textCursor().position() == expectedCursor;
                editor.undo();
                return inserted
                    && editor.toPlainText() == original;
            };
        expect("Return at physical indented-line start does not duplicate indent",
               verifyEnter(
                   0,
                   Qt::Key_Return,
                   QString(original).insert(lineStart, QStringLiteral("\n")),
                   lineStart + 1));
        expect("Enter inside indentation preserves one total indent",
               verifyEnter(
                   2,
                   Qt::Key_Enter,
                   QString(original).replace(lineStart,
                                             2,
                                             QStringLiteral("\n  ")),
                   lineStart + 3));
        expect("Return at logical line start inherits one indent",
               verifyEnter(
                   4,
                   Qt::Key_Return,
                   QString(original).replace(lineStart,
                                             4,
                                             QStringLiteral("\n    ")),
                   lineStart + 5));

        const QString whitespaceOriginal =
            QStringLiteral("module m;\n"
                           "always_comb begin\n"
                           "    \n"
                           "end\n"
                           "endmodule\n");
        const int whitespaceStart =
            whitespaceOriginal.indexOf(QStringLiteral("    \n"));
        editor.setPlainText(whitespaceOriginal);
        setCursor(editor, whitespaceStart);
        QTest::keyClick(&editor, Qt::Key_Return);
        expect("Return at whitespace-line start does not duplicate indent and undoes once",
               editor.toPlainText()
                       == QString(whitespaceOriginal)
                              .insert(whitespaceStart, QStringLiteral("\n")));
        editor.undo();
        expect("whitespace-line structural Enter is one undo transaction",
               editor.toPlainText() == whitespaceOriginal);
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

        editor.setPlainText(QStringLiteral(")"));
        setCursor(editor, 0);
        sendTextKey(editor,
                    Qt::Key_ParenRight,
                    QStringLiteral(")"));
        expect("an arbitrary adjacent closer is inserted rather than skipped",
               editor.toPlainText() == QStringLiteral("))")
                   && editor.textCursor().position() == 1);

        editor.setPlainText(QStringLiteral("signal"));
        setCursor(editor, 0);
        sendTextKey(editor,
                    Qt::Key_ParenLeft,
                    QStringLiteral("("));
        expect("opening parenthesis before an identifier inserts only the opener",
               editor.toPlainText() == QStringLiteral("(signal")
                   && editor.textCursor().position() == 1);

        editor.setPlainText(QStringLiteral("signal"));
        setCursor(editor, 3);
        sendTextKey(editor,
                    Qt::Key_ParenLeft,
                    QStringLiteral("("));
        expect("opening parenthesis inside an identifier wraps the complete token",
               editor.toPlainText() == QStringLiteral("(signal)"));

        const QString memberExpression =
            QStringLiteral("module m;\n"
                           "assign out = ~object.member[index];\n"
                           "endmodule\n");
        editor.setPlainText(memberExpression);
        setCursor(editor,
                  memberExpression.indexOf(QStringLiteral("member")) + 3);
        sendTextKey(editor,
                    Qt::Key_ParenLeft,
                    QStringLiteral("("));
        expect("opening parenthesis wraps the smallest complete expression atom",
               editor.toPlainText().contains(
                   QStringLiteral("~(object.member[index])"))
                   && !editor.toPlainText().contains(
                       QStringLiteral("(~object.member[index])")));

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

        QString occurrenceFailure;
        expect("next-occurrence command selects the structural identifier",
               editor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty()
                   && editor.textCursor().selectedText()
                          == QStringLiteral("sig")
                   && !editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor));
        expect("next-occurrence command enters centralized multi-cursor mode",
               editor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty()
                   && editor.editorModeActiveForTest(
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
            Qt::Key_D,
            Qt::ControlModifier);
        expect("Ctrl+D duplicates the current logical line",
               editor.toPlainText().contains(
                   QStringLiteral(
                       "    sig = sig + 1;\n"
                       "    sig = sig + 1;\n"
                       "    sig = sig + 2;"))
                   && !editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor));
        editor.undo();
        expect("Ctrl+D duplicate is one undo transaction",
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
        const QString source =
            QStringLiteral("module m;\n"
                           "logic sig;   \n"
                           "assign out = sig;  \n"
                           "endmodule\n");
        editor.setPlainText(source);
        const int firstSignal =
            source.indexOf(QStringLiteral("sig"));
        setCursor(editor, firstSignal + 1);
        QString occurrenceFailure;
        expect("multi-cursor line-boundary fixture selects the first occurrence",
               editor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty());
        expect("multi-cursor line-boundary fixture adds the second occurrence",
               editor.addNextSymbolOccurrence(&occurrenceFailure)
                   && occurrenceFailure.isEmpty()
                   && editor.editorModeActiveForTest(
                       EditorModeId::MultiCursor));
        QTest::keyClick(&editor,
                        Qt::Key_Right,
                        Qt::AltModifier);
        QString deleteFailure;
        expect("Alt+Right selects to each caret's content boundary independently",
               editor.deleteSelectedContent(&deleteFailure)
                   && deleteFailure.isEmpty()
                   && editor.toPlainText()
                          == QStringLiteral("module m;\n"
                                            "logic sig   \n"
                                            "assign out = sig  \n"
                                            "endmodule\n"));
        MyCodeEditor physicalEdgeEditor;
        physicalEdgeEditor.setPlainText(source);
        setCursor(physicalEdgeEditor, firstSignal + 1);
        occurrenceFailure.clear();
        physicalEdgeEditor.addNextSymbolOccurrence(&occurrenceFailure);
        physicalEdgeEditor.addNextSymbolOccurrence(&occurrenceFailure);
        QTest::keyClick(&physicalEdgeEditor,
                        Qt::Key_Right,
                        Qt::AltModifier);
        const EditorMultiCursorSnapshot contentBoundarySnapshot =
            physicalEdgeEditor.multiCursorSnapshotForTest();
        QTest::keyClick(&physicalEdgeEditor,
                        Qt::Key_Right,
                        Qt::AltModifier);
        const EditorMultiCursorSnapshot physicalBoundarySnapshot =
            physicalEdgeEditor.multiCursorSnapshotForTest();
        deleteFailure.clear();
        const bool physicalDeleted =
            physicalEdgeEditor.deleteSelectedContent(&deleteFailure);
        expect("multi-cursor Alt+Right retains both independent carets",
               contentBoundarySnapshot.active
                   && contentBoundarySnapshot.carets.size() == 2
                   && physicalBoundarySnapshot.active
                   && physicalBoundarySnapshot.carets.size() == 2);
        expect("repeated multi-cursor Alt+Right advances beyond content boundary",
               contentBoundarySnapshot.carets.size() == 2
                   && physicalBoundarySnapshot.carets.size() == 2
                   && physicalBoundarySnapshot.carets.at(0).position
                          > contentBoundarySnapshot.carets.at(0).position
                   && physicalBoundarySnapshot.carets.at(1).position
                          > contentBoundarySnapshot.carets.at(1).position);
        expect("repeated multi-cursor Alt+Right includes each physical line edge",
               physicalDeleted
                   && deleteFailure.isEmpty()
                   && physicalEdgeEditor.toPlainText()
                          == QStringLiteral("module m;\n"
                                            "logic sig\n"
                                            "assign out = sig\n"
                                            "endmodule\n"));
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        const QString source =
            QStringLiteral("module m;\n"
                           "logic sig;\n"
                           "assign sig = sig;\n"
                           "endmodule");
        editor.setPlainText(source);
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents();
        const int firstSignal = source.indexOf(QStringLiteral("sig"));
        setCursor(editor, firstSignal);
        const QPoint signalPoint = editor.cursorRect().center();

        QTest::mouseClick(editor.viewport(),
                          Qt::LeftButton,
                          Qt::NoModifier,
                          signalPoint);
        expect("single click does not highlight same-name signal occurrences",
               selectedTextCount(editor, QStringLiteral("sig")) == 0);
        QTest::mouseDClick(editor.viewport(),
                          Qt::LeftButton,
                          Qt::NoModifier,
                          signalPoint);
        expect("double click activates same-name signal occurrence highlighting",
               selectedTextCount(editor, QStringLiteral("sig")) >= 3);
        QTest::mouseClick(editor.viewport(),
                          Qt::LeftButton,
                          Qt::NoModifier,
                          signalPoint);
        const QTextBlock signalBlock =
            editor.document()->findBlock(firstSignal);
        const QTextCursor lineSelection = editor.textCursor();
        expect("triple click selects the complete logical line",
               lineSelection.selectionStart() == signalBlock.position()
                   && lineSelection.selectionEnd()
                          == signalBlock.next().position()
                   && lineSelection.selectedText()
                          == QStringLiteral("logic sig;\u2029"));
        expect("triple click clears same-name signal occurrence highlighting",
               selectedTextCount(editor, QStringLiteral("sig")) == 0);
        const QTextBlock moduleBlock = editor.document()->firstBlock();
        QTextCursor moduleCursor(moduleBlock);
        moduleCursor.setPosition(moduleBlock.position());
        const QPoint modulePoint = editor.cursorRect(moduleCursor).center();
        QTest::mouseClick(editor.viewport(),
                          Qt::LeftButton,
                          Qt::NoModifier,
                          modulePoint);
        expect("the next single click clears same-name signal highlighting",
               selectedTextCount(editor, QStringLiteral("sig")) == 0);

        const int finalLineStart = source.indexOf(QStringLiteral("endmodule"));
        setCursor(editor, finalLineStart);
        const QPoint finalLinePoint = editor.cursorRect().center();
        QTest::mouseDClick(editor.viewport(),
                          Qt::LeftButton,
                          Qt::NoModifier,
                          finalLinePoint);
        QTest::mouseClick(editor.viewport(),
                          Qt::LeftButton,
                          Qt::NoModifier,
                          finalLinePoint);
        const QTextCursor finalLineSelection = editor.textCursor();
        expect("triple click selects a final line without a trailing newline",
               finalLineSelection.selectionStart() == finalLineStart
                   && finalLineSelection.selectionEnd()
                          == editor.document()->characterCount() - 1
                   && finalLineSelection.selectedText()
                          == QStringLiteral("endmodule"));
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        editor.setPlainText(QStringLiteral("a\nbb"));
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents();
        setCursor(editor, 0);

        QTest::keyClick(&editor,
                        Qt::Key_Down,
                        Qt::ShiftModifier | Qt::AltModifier);
        QTest::keyClick(&editor,
                        Qt::Key_Right,
                        Qt::ShiftModifier | Qt::AltModifier);
        EditorColumnModeSnapshot keyboardColumn =
            editor.columnModeSnapshotForTest();
        expect("Shift+Alt+Arrow enters and extends column selection",
               editor.editorModeActiveForTest(
                   EditorModeId::ColumnSelection)
                   && keyboardColumn.selectionActive
                   && keyboardColumn.anchorLine == 0
                   && keyboardColumn.currentLine == 1
                   && keyboardColumn.anchorColumn == 0
                   && keyboardColumn.currentColumn == 1);
        QTest::keyClick(&editor,
                        Qt::Key_Right,
                        Qt::ShiftModifier);
        keyboardColumn = editor.columnModeSnapshotForTest();
        expect("Shift+Right extends an existing column selection",
               keyboardColumn.selectionActive
                   && keyboardColumn.anchorColumn == 0
                   && keyboardColumn.currentColumn == 2);
        QTest::keyClick(&editor,
                        Qt::Key_Left,
                        Qt::ShiftModifier);
        keyboardColumn = editor.columnModeSnapshotForTest();
        expect("Shift+Left contracts an existing column selection",
               keyboardColumn.selectionActive
                   && keyboardColumn.anchorColumn == 0
                   && keyboardColumn.currentColumn == 1);
        sendTextKey(editor,
                    Qt::Key_Z,
                    QStringLiteral("z"));
        EditorColumnModeSnapshot replacedColumn =
            editor.columnModeSnapshotForTest();
        expect("rectangular replacement leaves the column caret after inserted text",
               replacedColumn.selectionActive
                   && replacedColumn.anchorColumn == 1
                   && replacedColumn.currentColumn == 1);
        editor.undo();
        EditorColumnModeSnapshot undoneColumn =
            editor.columnModeSnapshotForTest();
        expect("undo restores the pre-edit column selection instead of document start",
               editor.toPlainText() == QStringLiteral("a\nbb")
                   && undoneColumn.selectionActive
                   && undoneColumn.anchorLine == 0
                   && undoneColumn.currentLine == 1
                   && undoneColumn.anchorColumn == 0
                   && undoneColumn.currentColumn == 1);
        editor.redo();
        EditorColumnModeSnapshot redoneColumn =
            editor.columnModeSnapshotForTest();
        expect("redo restores the post-edit logical column caret",
               redoneColumn.selectionActive
                   && redoneColumn.anchorColumn == 1
                   && redoneColumn.currentColumn == 1);
        editor.undo();
        QTest::keyClick(&editor, Qt::Key_Escape);
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
        expect("Alt click beyond EOL creates one explicit virtual caret",
               editor.editorModeActiveForTest(
                   EditorModeId::VirtualCursor)
                   && !editor.editorModeActiveForTest(
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
                          == QStringLiteral("a")
                   && virtualLine.startsWith(
                       QStringLiteral("bb"))
                   && virtualLine.endsWith(
                       QLatin1Char('x'))
                   && !virtualPadding.isEmpty()
                   && virtualPadding.trimmed().isEmpty());
        editor.undo();
        expect("Alt virtual-caret input is one undo transaction",
               editor.toPlainText()
                   == QStringLiteral("a\nbb"));
    }

    {
        MyCodeEditor editor;
        editor.setPlainText(QStringLiteral("abc\ndef"));
        setCursor(editor, 6, 1);
        QString reason;
        expect("ordinary selection can be replaced with equal spaces",
               editor.replaceSelectionWithSpaces(&reason)
                   && reason.isEmpty()
                   && editor.toPlainText()
                          == QStringLiteral("a  \n  f")
                   && editor.textCursor().hasSelection()
                   && editor.textCursor().selectionStart() == 1
                   && editor.textCursor().selectionEnd() == 6);
        editor.undo();
        expect("ordinary space replacement is one undo transaction",
               editor.toPlainText() == QStringLiteral("abc\ndef"));
    }

    {
        MyCodeEditor editor;
        editor.resize(520, 160);
        editor.setPlainText(QStringLiteral("abcd\nwxyz"));
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents();
        setCursor(editor, 1);
        QTest::keyClick(&editor,
                        Qt::Key_Down,
                        Qt::ShiftModifier | Qt::AltModifier);
        QTest::keyClick(&editor,
                        Qt::Key_Right,
                        Qt::ShiftModifier | Qt::AltModifier);
        QTest::keyClick(&editor,
                        Qt::Key_Right,
                        Qt::ShiftModifier);
        const EditorColumnModeSnapshot before =
            editor.columnModeSnapshotForTest();
        QString reason;
        expect("column selection can be replaced with equal-width spaces",
               before.selectionActive
                   && before.anchorColumn == 1
                   && before.currentColumn == 3
                   && editor.replaceSelectionWithSpaces(&reason)
                   && reason.isEmpty()
                   && editor.toPlainText()
                          == QStringLiteral("a  d\nw  z"));
        const EditorColumnModeSnapshot after =
            editor.columnModeSnapshotForTest();
        expect("column space replacement preserves the rectangular selection",
               after.selectionActive
                   && after.anchorLine == before.anchorLine
                   && after.currentLine == before.currentLine
                   && after.anchorColumn == before.anchorColumn
                   && after.currentColumn == before.currentColumn);
        editor.undo();
        expect("column space replacement is one undo transaction",
               editor.toPlainText() == QStringLiteral("abcd\nwxyz"));
    }

    {
        MyCodeEditor editor;
        const QString source =
            QStringLiteral("module organize_ui;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  assign y = late;\n")
            + QStringLiteral("  wire late;\n")
            + QStringLiteral("endmodule\n");
        editor.setPlainText(source);
        QCoreApplication::processEvents();
        QString reason;
        const int cursorPosition =
            source.indexOf(QStringLiteral("assign"));
        expect("editor exposes a safe organize-signals action",
               editor.canOrganizeSignalDeclarationsAt(
                   cursorPosition, &reason)
                   && reason.isEmpty());
        expect("editor organizes module signals through one structural edit",
               editor.organizeSignalDeclarationsAt(
                   cursorPosition, &reason)
                   && editor.toPlainText()
                          == QStringLiteral(
                              "module organize_ui;\n"
                              "  logic early;\n"
                              "  wire late;\n"
                              "  assign y = late;\n"
                              "endmodule\n"));
        editor.undo();
        expect("organize signals is one undo transaction",
               editor.toPlainText() == source);
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

    {
        MyCodeEditor editor;
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.resize(420, 220);
        QString source;
        for (int line = 0; line < 240; ++line) {
            source.append(
                QStringLiteral("logic row_%1 = value_%2_%3;\n")
                    .arg(line)
                    .arg(line)
                    .arg(QString(120, QLatin1Char('x'))));
        }
        editor.setPlainText(source);
        editor.show();
        QCoreApplication::processEvents();

        QTextCursor cursor(
            editor.document()->findBlockByNumber(120));
        cursor.movePosition(QTextCursor::EndOfBlock);
        editor.setTextCursor(cursor);
        editor.insertPlainText(QStringLiteral("X"));
        QScrollBar* vertical = editor.verticalScrollBar();
        QScrollBar* horizontal = editor.horizontalScrollBar();
        vertical->setValue(vertical->maximum() / 2);
        horizontal->setValue(horizontal->maximum() / 2);
        const int verticalBeforeUndo = vertical->value();
        const int horizontalBeforeUndo = horizontal->value();

        editor.undo();
        QCoreApplication::processEvents();
        expect("undo preserves both editor viewport axes",
               vertical->value() == verticalBeforeUndo
                   && horizontal->value() == horizontalBeforeUndo);

        const int verticalBeforeRedo = vertical->value();
        const int horizontalBeforeRedo = horizontal->value();
        editor.redo();
        QCoreApplication::processEvents();
        expect("redo preserves both editor viewport axes",
               vertical->value() == verticalBeforeRedo
                   && horizontal->value() == horizontalBeforeRedo);
    }

    // Exercise the same key dispatch used by a new, unsaved RTL document.
    const auto typeRtl = [](MyCodeEditor& editor, const QString& text) {
        for (QChar ch : text) {
            if (ch == QLatin1Char('\n'))
                QTest::keyClick(&editor, Qt::Key_Return);
            else
                sendTextKey(editor, ch.toUpper().unicode(), QString(ch));
        }
    };
    const auto expectEnter = [&](const char* label, const QString& source,
                                 const QString& insertion) {
        MyCodeEditor editor;
        editor.setPlainText(source);
        setCursor(editor, source.size());
        typeRtl(editor, QStringLiteral("\n"));
        const QString result = editor.toPlainText();
        const int caret = editor.textCursor().position();
        editor.undo();
        const bool undid = editor.toPlainText() == source;
        editor.redo();
        expect(label, result == source + insertion && undid
               && editor.toPlainText() == result
               && editor.textCursor().position() == caret);
    };
    expectEnter("unfinished parameter list continues at one indent",
                QStringLiteral("module m #(\n    parameter W = 8,"),
                QStringLiteral("\n    "));
    expectEnter("opening parameter list indents from module header",
                QStringLiteral("module m #("), QStringLiteral("\n    "));
    expectEnter("nested port expression preserves its opener indent",
                QStringLiteral("module m (\n    input logic ["),
                QStringLiteral("\n        "));
    expectEnter("completed module header opens module body",
                QStringLiteral("module m (\n    input clk\n);"),
                QStringLiteral("\n    "));
    expectEnter("incomplete if condition keeps continuation indent",
                QStringLiteral("always_comb begin\n    if (ready &&"),
                QStringLiteral("\n        "));
    expectEnter("if condition starts a single statement body",
                QStringLiteral("always_comb begin\n    if (ready)"),
                QStringLiteral("\n        "));
    expectEnter("completed unbraced if returns to the if level",
                QStringLiteral("always_comb begin\n    if (ready)\n        q = d;"),
                QStringLiteral("\n    "));
    expectEnter("always event control opens statement body",
                QStringLiteral("  always @(posedge clk)"),
                QStringLiteral("\n      "));
    expectEnter("incomplete case opens item level without a closing keyword",
                QStringLiteral("always_comb begin\n    case (state)"),
                QStringLiteral("\n        "));
    expectEnter("case item opens its statement body",
                QStringLiteral("case (state)\n    0:"),
                QStringLiteral("\n        "));
    expectEnter("completed case item returns to label level",
                QStringLiteral("case (state)\n    0:\n        q = d;"),
                QStringLiteral("\n    "));
    expectEnter("comments do not hide the if header on Enter",
                QStringLiteral("if (ready) // endcase begin"),
                QStringLiteral("\n    "));
    expectEnter("manual tabs are retained before the default indent step",
                QStringLiteral("\tif (ready)"), QStringLiteral("\n\t    "));

    // Unlike prefix-only fixtures, real typing already has the auto-paired
    // closer to the right of the caret when Enter is pressed.
    const auto expectPairedEnter = [&](const char* label, const QString& prefix,
                                       int column) {
        MyCodeEditor editor;
        typeRtl(editor, prefix);
        const QString before = editor.toPlainText();
        const int beforeCaret = editor.textCursor().position();
        const bool pairedCloserPresent = beforeCaret < before.size()
            && QStringLiteral(")]}").contains(before.at(beforeCaret));
        const QString newline = QStringLiteral("\n") + QString(column, QLatin1Char(' '));
        const QString expected = QString(before).insert(beforeCaret, newline);
        typeRtl(editor, QStringLiteral("\n"));
        const bool inserted = editor.toPlainText() == expected
            && editor.textCursor().position() == beforeCaret + newline.size();
        editor.undo();
        const bool undid = editor.toPlainText() == before
            && editor.textCursor().position() == beforeCaret;
        editor.redo();
        expect(label, pairedCloserPresent && inserted && undid && editor.toPlainText() == expected
               && editor.textCursor().position() == beforeCaret + newline.size());
    };
    expectPairedEnter("typed parameter comma before paired closer continues the list",
                      QStringLiteral("module m #(parameter W = 8,"), 4);
    expectPairedEnter("typed port comma before paired closer continues the list",
                      QStringLiteral("module m (input logic clk,"), 4);
    expectPairedEnter("typed logical and before paired closer continues the condition",
                      QStringLiteral("if (ready &&"), 4);
    expectPairedEnter("typed logical or preserves the existing opener indent",
                      QStringLiteral("  if (ready ||"), 6);
    expectPairedEnter("typed comparison before paired closer expects an operand",
                      QStringLiteral("if (ready =="), 4);
    expectPairedEnter("typed arithmetic operator before paired closer expects an operand",
                      QStringLiteral("if (count +"), 4);
    expectPairedEnter("typed unary operator before paired closer expects an operand",
                      QStringLiteral("if (!"), 4);
    expectPairedEnter("typed prefix increment before paired closer expects an operand",
                      QStringLiteral("if (++"), 4);
    expectPairedEnter("typed ternary colon before paired closer expects an operand",
                      QStringLiteral("if (a ? b :"), 4);
    expectPairedEnter("typed range colon before paired bracket continues the range",
                      QStringLiteral("logic [7:"), 4);
    expectPairedEnter("typed concatenation comma before paired brace continues its items",
                      QStringLiteral("assign q = {a,"), 4);
    expectPairedEnter("typed event or before paired closer continues the event control",
                      QStringLiteral("always @(posedge clk or"), 4);
    expectPairedEnter("typed inside before paired closer expects a set expression",
                      QStringLiteral("if (state inside"), 4);
    expectPairedEnter("typed shift before paired closer expects an operand",
                      QStringLiteral("if (count <<"), 4);
    expectPairedEnter("typed member separator before paired closer expects a member",
                      QStringLiteral("if (bus."), 4);
    expectPairedEnter("comment after a typed operator does not finish the expression",
                      QStringLiteral("if (ready && /* keep going */"), 4);
    expectPairedEnter("completed parameter before paired closer enters closing level",
                      QStringLiteral("module m #(parameter W = 8"), 0);
    expectPairedEnter("completed condition before paired closer enters closing level",
                      QStringLiteral("if (ready"), 0);
    expectPairedEnter("postfix increment before paired closer is a completed expression",
                      QStringLiteral("if (count++"), 0);
    expectPairedEnter("postfix decrement before paired closer is a completed expression",
                      QStringLiteral("if (count--"), 0);
    expectPairedEnter("operator characters inside a string do not imply continuation",
                      QStringLiteral("if (name == \"&&\""), 0);

    for (const auto& continuation : {
             qMakePair(QStringLiteral("module m #(parameter W = 8,"),
                       QStringLiteral("parameter D = 4")),
             qMakePair(QStringLiteral("if (ready &&"), QStringLiteral("valid"))}) {
        MyCodeEditor editor;
        typeRtl(editor, continuation.first + QStringLiteral("\n") + continuation.second
                            + QStringLiteral("\n"));
        const QString beforeCloser = editor.toPlainText();
        const int beforeCaret = editor.textCursor().position();
        typeRtl(editor, QStringLiteral("    )"));
        const QString completed = editor.toPlainText();
        const int completedCaret = editor.textCursor().position();
        const QString expected = continuation.first + QStringLiteral("\n    ")
            + continuation.second + QStringLiteral("\n)");
        editor.undo();
        typeRtl(editor, QStringLiteral(")"));
        expect(qPrintable(QStringLiteral("typed continuation finishes and retains closer tracking: ")
                          + continuation.first),
               beforeCloser == expected && beforeCaret == expected.size() - 1
                   && completed == expected && completedCaret == expected.size()
                   && editor.toPlainText() == completed
                   && editor.textCursor().position() == completedCaret);
    }

    const auto expectClosing = [&](const char* label, const QString& source,
                                   const QString& keys, const QString& expected,
                                   bool checkUndo = true) {
        MyCodeEditor editor;
        editor.setPlainText(source);
        setCursor(editor, source.size());
        typeRtl(editor, keys.left(keys.size() - 1));
        const QString beforeCommit = editor.toPlainText();
        typeRtl(editor, keys.right(1));
        const QString result = editor.toPlainText();
        const int caret = editor.textCursor().position();
        editor.undo();
        const bool undid = editor.toPlainText() == beforeCommit;
        editor.redo();
        expect(label, result == expected && (!checkUndo || (undid
               && editor.toPlainText() == result
               && editor.textCursor().position() == caret)));
    };
    expectClosing("manual end aligns and commits with one undo/redo",
                  QStringLiteral("  always_comb begin\n      "),
                  QStringLiteral("end\n"),
                  QStringLiteral("  always_comb begin\n  end\n  "));
    expectClosing("manual endcase aligns on a space boundary",
                  QStringLiteral("case (s)\n    0: q = 0;\n        "),
                  QStringLiteral("endcase "),
                  QStringLiteral("case (s)\n    0: q = 0;\nendcase "));
    expectClosing("manual endmodule aligns on Enter",
                  QStringLiteral("module m;\n    "), QStringLiteral("endmodule\n"),
                  QStringLiteral("module m;\nendmodule\n"));
    expectClosing("standalone port closer aligns and undoes once",
                  QStringLiteral("module m (\n    input clk\n    "),
                  QStringLiteral(")"),
                  QStringLiteral("module m (\n    input clk\n)"));
    expectClosing("end-prefixed identifier retains its indentation",
                  QStringLiteral("begin\n    "), QStringLiteral("end_signal "),
                  QStringLiteral("begin\n    end_signal "), false);
    expectClosing("keyword in comment never aligns",
                  QStringLiteral("begin\n    // "), QStringLiteral("end "),
                  QStringLiteral("begin\n    // end "), false);
    expectClosing("escaped end identifier is not a terminator",
                  QStringLiteral("begin\n    \\"), QStringLiteral("end "),
                  QStringLiteral("begin\n    \\end "), false);
    expectClosing("unterminated string cannot move the line",
                  QStringLiteral("begin\n    $display(\""), QStringLiteral("end "),
                  QStringLiteral("begin\n    $display(\"end "), false);
    expectClosing("nested closing delimiter uses the nearest matching opener",
                  QStringLiteral("module m (\n    input logic [\n        "),
                  QStringLiteral("]"),
                  QStringLiteral("module m (\n    input logic [\n    ]"));
    expectClosing("dangling else aligns to the nearest unbraced if",
                  QStringLiteral("if (a)\n    if (b)\n        q = 0;\n"),
                  QStringLiteral("else "),
                  QStringLiteral("if (a)\n    if (b)\n        q = 0;\n    else "));
    expectEnter("nested single-statement if returns to the outer level",
                QStringLiteral("if (a)\n    if (b)\n        q = 0;"),
                QStringLiteral("\n"));
    expectEnter("else opens the matching single statement",
                QStringLiteral("if (a)\n    q = 0;\nelse"),
                QStringLiteral("\n    "));
    expectEnter("else-if condition opens its body",
                QStringLiteral("if (a)\n    q = 0;\nelse if (b)"),
                QStringLiteral("\n    "));
    expectEnter("unterminated block comment keeps its current indentation",
                QStringLiteral("module m;\n    /* begin if ("),
                QStringLiteral("\n    "));
    expectEnter("keywords and delimiters inside a string are opaque",
                QStringLiteral("module m;\n    $display(\"begin endcase (\");"),
                QStringLiteral("\n    "));
    expectEnter("ordinary manually indented statement is not reformatted",
                QStringLiteral("module m;\n  logic q;"),
                QStringLiteral("\n  "));
    expectEnter("existing two-space parameter items keep their chosen indent",
                QStringLiteral("module m #(\n  parameter N = 1,"),
                QStringLiteral("\n  "));
    expectEnter("an inner range does not move the following port item",
                QStringLiteral("module m (\n  input logic [\n      7:0] data,"),
                QStringLiteral("\n  "));
    expectEnter("qualified case keeps the same structural input rule",
                QStringLiteral("    unique case (state)"), QStringLiteral("\n        "));
    expectEnter("unrelated multiline statement retains its current indent",
                QStringLiteral("module m;\n    assign q =\n        a | b;"),
                QStringLiteral("\n        "));
    expectClosing("case branch dangling else retains its if alignment",
                  QStringLiteral("case (s)\n    0:\n        if (a)\n            q = 0;\n    "),
                  QStringLiteral("else "),
                  QStringLiteral("case (s)\n    0:\n        if (a)\n            q = 0;\n        else "));
    {
        MyCodeEditor editor;
        editor.setPlainText(QString());
        typeRtl(editor, QStringLiteral("always_comb begin\nq = 0;\nend"));
        const QString before = editor.toPlainText();
        typeRtl(editor, QStringLiteral("\n"));
        const QString after = editor.toPlainText();
        editor.undo();
        const bool undid = editor.toPlainText() == before;
        editor.redo();
        expect("manually committed end reuses generated end in one undo transaction",
               after == QStringLiteral("always_comb begin\n    q = 0;\nend\n")
                   && undid && editor.toPlainText() == after);
        editor.undo();
        typeRtl(editor, QStringLiteral("\n"));
        expect("undo then retyping end boundary still reuses generated end",
               editor.toPlainText() == after);
    }
    {
        MyCodeEditor editor;
        const QString source = QStringLiteral("always_comb begin // begin\nend");
        editor.setPlainText(source);
        setCursor(editor, source.indexOf(QLatin1Char('\n')));
        typeRtl(editor, QStringLiteral("\n"));
        expect("Enter keeps an existing end despite a trailing comment",
               editor.toPlainText()
                   == QStringLiteral("always_comb begin // begin\n    \nend"));
        editor.undo();
        expect("existing end split undoes once", editor.toPlainText() == source);
    }
    {
        MyCodeEditor editor;
        const QString source = QStringLiteral("begin\n    end\nend");
        editor.setPlainText(source);
        setCursor(editor, source.indexOf(QStringLiteral("end")) + 3);
        typeRtl(editor, QStringLiteral(" "));
        expect("alignment never deletes an adjacent user-authored end",
               editor.toPlainText() == QStringLiteral("begin\nend \nend"));
    }
    {
        MyCodeEditor editor;
        editor.setPlainText(QString());
        typeRtl(editor, QStringLiteral("module m #(\nparameter N = 1"));
        QTest::keyClick(&editor, Qt::Key_Down);
        QTest::keyClick(&editor, Qt::Key_Home);
        typeRtl(editor, QStringLiteral("    "));
        const QString before = editor.toPlainText();
        typeRtl(editor, QStringLiteral(")"));
        const QString after = editor.toPlainText();
        const int position = editor.textCursor().position();
        editor.undo();
        const bool undid = editor.toPlainText() == before;
        editor.redo();
        expect("tracked list closer alignment, skip and undo/redo are atomic",
               after == QStringLiteral("module m #(\n    parameter N = 1\n)")
                   && undid && editor.toPlainText() == after
                   && editor.textCursor().position() == position);
    }

    // A skip does not destroy the generated character. Its provenance must
    // survive undo/redo of indentation and subsequent edits at that boundary.
    for (const auto& closerCase : {
             qMakePair(QStringLiteral("module m #(\nparameter N = 1"), QLatin1Char(')')),
             qMakePair(QStringLiteral("logic [\n7:0"), QLatin1Char(']')),
             qMakePair(QStringLiteral("assign q = {\n1'b0"), QLatin1Char('}'))}) {
        MyCodeEditor editor;
        typeRtl(editor, closerCase.first);
        QTest::keyClick(&editor, Qt::Key_Down);
        QTest::keyClick(&editor, Qt::Key_Home);
        typeRtl(editor, QStringLiteral("    "));
        const QString before = editor.toPlainText();
        const int beforeCaret = editor.textCursor().position();
        const QString close(closerCase.second);
        typeRtl(editor, close);
        const QString first = editor.toPlainText();
        const int firstCaret = editor.textCursor().position();
        const auto check = [&](const char* label, bool condition) {
            expect(qPrintable(QString::fromLatin1(label) + QStringLiteral(" [") + close
                              + QStringLiteral("]")), condition);
        };
        check("generated closer first alignment skips exactly one character",
              first == QString(before).remove(beforeCaret - 4, 4)
                  && firstCaret == beforeCaret - 3);

        editor.undo();
        check("undo restores pre-alignment text and caret",
              editor.toPlainText() == before && editor.textCursor().position() == beforeCaret);
        typeRtl(editor, close);
        check("undo then retype generated closer preserves text and caret",
              editor.toPlainText() == first && editor.textCursor().position() == firstCaret);
        editor.undo();
        editor.redo();
        check("redo restores generated closer alignment and caret",
              editor.toPlainText() == first && editor.textCursor().position() == firstCaret);
        typeRtl(editor, QStringLiteral(";"));
        check("typing after redo edits after the generated closer",
              editor.toPlainText() == first + QLatin1Char(';'));
        editor.undo();
        editor.undo();
        typeRtl(editor, close);
        check("undo past post-redo typing then retype still skips generated closer",
              editor.toPlainText() == first && editor.textCursor().position() == firstCaret);

        typeRtl(editor, close);
        setCursor(editor, firstCaret);
        typeRtl(editor, close);
        check("newly typed adjacent user closer is inserted rather than skipped",
              editor.toPlainText() == first + close + close);

        // Same text loaded as user source has no generated-closer provenance.
        editor.setPlainText(before);
        setCursor(editor, beforeCaret);
        typeRtl(editor, close);
        const QString userFirst = editor.toPlainText();
        check("matching user-authored closer is never treated as generated",
              userFirst == first + close);
        editor.undo();
        typeRtl(editor, close);
        check("undo then retype user-authored closer retains ordinary insertion",
              editor.toPlainText() == userFirst);

        MyCodeEditor deletionEditor;
        const QString open = close == QLatin1String(")") ? QStringLiteral("(")
            : close == QLatin1String("]") ? QStringLiteral("[") : QStringLiteral("{");
        typeRtl(deletionEditor, open + close + close);
        setCursor(deletionEditor, 2);
        QTest::keyClick(&deletionEditor, Qt::Key_Backspace);
        typeRtl(deletionEditor, close);
        check("deleting the generated closer cannot transfer tracking to its user neighbor",
              deletionEditor.toPlainText() == open + close + close
                  && deletionEditor.textCursor().position() == 2);
    }

    {
        MyCodeEditor editor;
        editor.setPlainText(QString());
        typeRtl(editor, QStringLiteral("module counter #(\n"
                                      "parameter WIDTH = 8,\n"
                                      "parameter STEP = 1"));
        // Move to the existing paired delimiter instead of inserting a second one.
        QTest::keyClick(&editor, Qt::Key_Down);
        QTest::keyClick(&editor, Qt::Key_Home);
        typeRtl(editor, QStringLiteral(") (\n"
                                      "input logic clk,\n"
                                      "input logic rst_n,\n"
                                      "output logic [WIDTH-1:0] q"));
        QTest::keyClick(&editor, Qt::Key_Down);
        QTest::keyClick(&editor, Qt::Key_Home);
        typeRtl(editor, QStringLiteral(");\n"
                                      "always_ff @(posedge clk) begin\n"
                                      "if (!rst_n)\n"
                                      "q <= '0;\n"
                                      "else begin\n"
                                      "case (q)\n"
                                      "0:\n"
                                      "q <= STEP;\n"
                                      "default:\n"
                                      "q <= q + STEP;\n"
                                      "endcase\n"
                                      "end\n"
                                      "end\n"
                                      "endmodule\n"));
        const QString expected = QStringLiteral(
            "module counter #(\n"
            "    parameter WIDTH = 8,\n"
            "    parameter STEP = 1\n"
            ") (\n"
            "    input logic clk,\n"
            "    input logic rst_n,\n"
            "    output logic [WIDTH-1:0] q\n"
            ");\n"
            "    always_ff @(posedge clk) begin\n"
            "        if (!rst_n)\n"
            "            q <= '0;\n"
            "        else begin\n"
            "            case (q)\n"
            "                0:\n"
            "                    q <= STEP;\n"
            "                default:\n"
            "                    q <= q + STEP;\n"
            "            endcase\n"
            "        end\n"
            "    end\n"
            "endmodule\n");
        expect("blank-file continuous module typing has correct structure and indentation",
               editor.toPlainText() == expected
                   && editor.textCursor().position() == expected.size());
        std::printf("[RTL TYPING BEGIN]\n%s[RTL TYPING END]\n",
                    editor.toPlainText().toUtf8().constData());
    }

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
