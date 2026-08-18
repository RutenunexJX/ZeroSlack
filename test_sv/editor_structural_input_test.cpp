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
                           "endmodule\n");
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

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
