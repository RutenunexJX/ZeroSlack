#include "mycodeeditor.h"
#include "shareddocument.h"

#include <QApplication>
#include <QClipboard>
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

QString largePayload()
{
    QString payload;
    payload.reserve(220000);
    for (int i = 0; i < 4096; ++i) {
        payload += QStringLiteral(
                       "logic [31:0] pasted_%1 = 32'h%2;\n")
                       .arg(i, 4, 10, QLatin1Char('0'))
                       .arg(i, 8, 16, QLatin1Char('0'));
    }
    return payload;
}

void resetEditor(MyCodeEditor& editor,
                 const QString& text)
{
    editor.setPlainText(text);
    editor.document()->clearUndoRedoStacks();
}

bool oneUndoRestores(MyCodeEditor& editor,
                     const QString& original)
{
    editor.undo();
    return editor.toPlainText() == original
        && !editor.document()->isUndoAvailable();
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    QClipboard* clipboard = QApplication::clipboard();
    const QString payload = largePayload();
    expect("large paste fixture is materially large",
           payload.size() > 150000
               && payload.count(QLatin1Char('\n'))
                      == 4096);

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral(
                "module direct_paste;\n"
                "endmodule\n");
        resetEditor(editor, original);
        QTextCursor cursor(editor.document());
        const int insertion =
            original.indexOf(
                QStringLiteral("endmodule"));
        cursor.setPosition(insertion);
        editor.setTextCursor(cursor);
        clipboard->setText(payload);

        editor.paste();
        const QString expected =
            QString(original).insert(insertion,
                                     payload);
        expect("public paste inserts the complete large payload",
               editor.toPlainText() == expected
                   && editor.textCursor().position()
                          == insertion + payload.size()
                   && editor.document()->isUndoAvailable());
        expect("public large paste is one undo transaction",
               oneUndoRestores(editor, original));
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral(
                "module replace_selection;\n"
                "  old_payload\n"
                "endmodule\n");
        resetEditor(editor, original);
        const int start =
            original.indexOf(
                QStringLiteral("old_payload"));
        const int end =
            start
            + QStringLiteral("old_payload").size();
        QTextCursor selection(editor.document());
        selection.setPosition(start);
        selection.setPosition(
            end, QTextCursor::KeepAnchor);
        editor.setTextCursor(selection);
        clipboard->setText(payload);

        editor.paste();
        QString expected = original;
        expected.replace(start,
                         end - start,
                         payload);
        expect("paste replaces a selection with the full payload",
               editor.toPlainText() == expected
                   && !editor.textCursor().hasSelection()
                   && editor.textCursor().position()
                          == start + payload.size());
        expect("selection replacement is one undo transaction",
               oneUndoRestores(editor, original));
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral("before\nafter\n");
        const QString crlfPayload =
            QStringLiteral(
                "line_a\r\n"
                "line_b\r\n"
                "line_c\r\n");
        const QString normalizedPayload =
            QStringLiteral(
                "line_a\n"
                "line_b\n"
                "line_c\n");
        resetEditor(editor, original);
        QTextCursor cursor(editor.document());
        const int insertion =
            original.indexOf(
                QStringLiteral("after"));
        cursor.setPosition(insertion);
        editor.setTextCursor(cursor);
        clipboard->setText(crlfPayload);

        editor.paste();
        expect("paste normalizes CRLF through QTextDocument",
               editor.toPlainText()
                   == QString(original).insert(
                       insertion,
                       normalizedPayload)
                   && !editor.toPlainText()
                           .contains(QLatin1Char('\r')));
        expect("CRLF paste is one undo transaction",
               oneUndoRestores(editor, original));
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral("prefix\nsuffix\n");
        resetEditor(editor, original);
        QTextCursor cursor(editor.document());
        const int insertion =
            original.indexOf(
                QStringLiteral("suffix"));
        cursor.setPosition(insertion);
        editor.setTextCursor(cursor);
        editor.show();
        editor.setFocus();
        clipboard->setText(payload);

        QTest::keyClick(&editor,
                        Qt::Key_V,
                        Qt::ControlModifier);
        expect("registered paste shortcut uses the transaction path",
               editor.toPlainText()
                   == QString(original).insert(
                       insertion,
                       payload));
        expect("shortcut large paste is one undo transaction",
               oneUndoRestores(editor, original));
    }

    {
        const QString initialText =
            QStringLiteral(
                "module shared_paste;\n"
                "endmodule\n");
        SharedDocument shared(
            QStringLiteral("paste-shared-document"),
            QStringLiteral("shared_paste.sv"),
            initialText);
        MyCodeEditor left;
        MyCodeEditor right;
        shared.attachView(&left);
        shared.attachView(&right);
        shared.textDocument()->clearUndoRedoStacks();

        const int insertion =
            initialText.indexOf(
                QStringLiteral("endmodule"));
        QTextCursor leftCursor(left.document());
        leftCursor.setPosition(insertion);
        left.setTextCursor(leftCursor);
        QTextCursor rightCursor(right.document());
        rightCursor.setPosition(0);
        right.setTextCursor(rightCursor);
        clipboard->setText(payload);

        left.paste();
        const QString expected =
            QString(initialText).insert(insertion,
                                        payload);
        expect("one shared view publishes the complete paste",
               left.document() == right.document()
                   && left.toPlainText() == expected
                   && right.toPlainText() == expected
                   && shared.dirty()
                   && shared.textDocument()
                          ->isUndoAvailable());

        right.undo();
        expect("either shared view undoes the whole paste once",
               left.toPlainText() == initialText
                   && right.toPlainText() == initialText
                   && !shared.textDocument()
                           ->isUndoAvailable()
                   && !shared.dirty());
    }

    {
        MyCodeEditor editor;
        const QString original =
            QStringLiteral("read_only\n");
        resetEditor(editor, original);
        editor.setReadOnly(true);
        clipboard->setText(payload);

        editor.paste();
        expect("public paste preserves a read-only view",
               editor.toPlainText() == original
                   && !editor.document()
                           ->isUndoAvailable());
    }

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
