#include "mycodeeditor.h"

#include <QApplication>
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
}

void placeCursor(MyCodeEditor* editor, int position)
{
    QTextCursor cursor(editor->document());
    cursor.setPosition(position);
    editor->setTextCursor(cursor);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    MyCodeEditor editor;
    EditorAnnotationDisplayOptions annotationOptions;
    annotationOptions.enabled = false;
    annotationOptions.maxAnnotationsPerLine = 0;
    annotationOptions.maxLanes = 99;
    editor.setAnnotationDisplayOptions(annotationOptions);
    const EditorAnnotationDisplayOptions normalizedOptions =
        editor.annotationDisplayOptions();
    expect("editor annotation settings are normalized and applied",
           !normalizedOptions.enabled
               && normalizedOptions.maxAnnotationsPerLine == 1
               && normalizedOptions.maxLanes == 16);

    const QString source =
        QStringLiteral(
            "module top;\n"
            "  logic value;\n"
            "  assign value = 1'b0;\n"
            "  always_comb begin\n"
            "    value <= 1'b1;\n"
            "  end\n"
            "endmodule\n");
    editor.setPlainText(source);

    const int declaration =
        source.indexOf(QStringLiteral("value"));
    const int continuousLhs =
        source.indexOf(
            QStringLiteral("value"),
            declaration + 1);
    const int proceduralLhs =
        source.indexOf(
            QStringLiteral("value"),
            continuousLhs + 1);
    placeCursor(&editor, declaration);

    QString message;
    expect("editor navigates to next selected-signal assignment",
           editor.goToNextAssignmentForSelectedSignal(
               &message)
               && editor.textCursor().selectionStart()
                   == continuousLhs
               && editor.textCursor().selectedText()
                   == QStringLiteral("value")
               && message
                   == QStringLiteral("Next assignment"));
    expect("repeating next assignment advances from selection",
           editor.goToNextAssignmentForSelectedSignal(
               &message)
               && editor.textCursor().selectionStart()
                   == proceduralLhs);
    expect("next assignment wraps and reports the wrap",
           editor.goToNextAssignmentForSelectedSignal(
               &message)
               && editor.textCursor().selectionStart()
                   == continuousLhs
               && message.contains(
                   QStringLiteral("wrapped")));
    expect("previous assignment wraps in reverse",
           editor.goToPreviousAssignmentForSelectedSignal(
               &message)
               && editor.textCursor().selectionStart()
                   == proceduralLhs
               && message.contains(
                   QStringLiteral("wrapped")));
    placeCursor(&editor, declaration);
    QTest::keyClick(
        &editor, Qt::Key_F7, Qt::AltModifier);
    expect("registered next-assignment shortcut uses the same route",
           editor.textCursor().selectionStart()
               == continuousLhs);

    const QString conditional =
        QStringLiteral(
            "`ifdef FEATURE\n"
            "logic a;\n"
            "`elsif FALLBACK\n"
            "logic b;\n"
            "`else\n"
            "logic c;\n"
            "`endif\n");
    editor.setPlainText(conditional);
    const int ifdef =
        conditional.indexOf(
            QStringLiteral("`ifdef"));
    const int elsif =
        conditional.indexOf(
            QStringLiteral("`elsif"));
    const int elseBranch =
        conditional.indexOf(
            QStringLiteral("`else"));
    const int endif =
        conditional.indexOf(
            QStringLiteral("`endif"));
    placeCursor(&editor, ifdef);

    expect("editor navigates to the next conditional branch",
           editor.goToNextConditionalBranch(&message)
               && editor.textCursor().selectionStart()
                   == elsif
               && editor.textCursor().selectedText()
                   == QStringLiteral("`elsif"));
    expect("conditional navigation can advance through else",
           editor.goToNextConditionalBranch(&message)
               && editor.textCursor().selectionStart()
                   == elseBranch);
    expect("conditional navigation includes endif",
           editor.goToNextConditionalBranch(&message)
               && editor.textCursor().selectionStart()
                   == endif);
    expect("conditional navigation wraps to opening branch",
           editor.goToNextConditionalBranch(&message)
               && editor.textCursor().selectionStart()
                   == ifdef
               && message.contains(
                   QStringLiteral("wrapped")));
    placeCursor(&editor, ifdef);
    QTest::keyClick(
        &editor, Qt::Key_F8, Qt::AltModifier);
    expect("registered conditional shortcut uses the same route",
           editor.textCursor().selectionStart()
               == elsif);

    editor.setPlainText(
        QStringLiteral("module plain; endmodule\n"));
    placeCursor(&editor, 0);
    expect("editor rejects navigation outside a conditional group",
           !editor.goToNextConditionalBranch(&message)
               && message.contains(
                   QStringLiteral(
                       "not inside a conditional")));

    std::printf("%d checks, %d failures\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
