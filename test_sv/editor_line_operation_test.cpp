#include "editorlineoperationcontroller.h"

#include <QApplication>
#include <QClipboard>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

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

void resetDocument(QTextDocument& document,
                   const QString& text)
{
    document.setPlainText(text);
    document.clearUndoRedoStacks();
}

QTextCursor cursorAt(QTextDocument& document,
                     int position,
                     int anchor = -1)
{
    QTextCursor cursor(&document);
    if (anchor >= 0) {
        cursor.setPosition(anchor);
        cursor.setPosition(
            position, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(position);
    }
    return cursor;
}

bool oneUndoRestores(QTextDocument& document,
                     const QString& original)
{
    QTextCursor undoCursor(&document);
    document.undo(&undoCursor);
    return document.toPlainText() == original
        && !document.isUndoAvailable();
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    EditorLineOperationController controller;
    QClipboard* clipboard = QApplication::clipboard();

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("alpha\nbeta\ngamma");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document,
                     original.indexOf(
                         QStringLiteral("beta")) + 2);
        const int originalPosition = cursor.position();

        const EditorLineOperationResult result =
            controller.copy(cursor, clipboard);
        expect("copy without a selection copies the complete line",
               result.succeeded
                   && !result.documentChanged
                   && result.clipboardText
                          == QStringLiteral("beta\n")
                   && clipboard->text()
                          == QStringLiteral("beta\n"));
        expect("copy does not change text or cursor",
               document.toPlainText() == original
                   && cursor.position()
                          == originalPosition
                   && !document.isUndoAvailable());
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("alpha\nbeta\ngamma");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document,
                     original.indexOf(
                         QStringLiteral("beta")) + 2);

        const EditorLineOperationResult result =
            controller.cut(cursor, clipboard);
        expect("cut without a selection removes the complete line",
               result.succeeded
                   && result.documentChanged
                   && result.clipboardText
                          == QStringLiteral("beta\n")
                   && document.toPlainText()
                          == QStringLiteral(
                              "alpha\ngamma"));
        expect("middle-line cut leaves cursor at the joined line start",
               cursor.position() == 6
                   && !cursor.hasSelection());
        expect("middle-line cut is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("alpha\nlast");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, original.size());

        const EditorLineOperationResult result =
            controller.cut(cursor, clipboard);
        expect("last-line cut preserves absent final newline semantics",
               result.succeeded
                   && result.clipboardText
                          == QStringLiteral("last")
                   && document.toPlainText()
                          == QStringLiteral("alpha")
                   && cursor.position() == 5);
        expect("last-line cut is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("alpha\n");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, original.size());

        const EditorLineOperationResult result =
            controller.cut(cursor, clipboard);
        expect("cut handles the trailing empty logical line",
               result.succeeded
                   && result.clipboardText
                          == QStringLiteral("\n")
                   && document.toPlainText()
                          == QStringLiteral("alpha")
                   && cursor.position() == 5);
        expect("trailing-empty-line cut is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("aa\nbb\ncc");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, 7, 1);
        const int originalPosition = cursor.position();
        const int originalAnchor = cursor.anchor();

        const EditorLineOperationResult result =
            controller.copy(cursor, clipboard);
        expect("copy with a selection keeps exact multiline contents",
               result.succeeded
                   && result.clipboardText
                          == QStringLiteral(
                              "a\nbb\nc")
                   && clipboard->text()
                          == QStringLiteral(
                              "a\nbb\nc"));
        expect("selection copy preserves selection direction",
               cursor.position() == originalPosition
                   && cursor.anchor() == originalAnchor
                   && document.toPlainText() == original);
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("aa\nbb\ncc");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, 7, 1);

        const EditorLineOperationResult result =
            controller.cut(cursor, clipboard);
        expect("cut with a selection uses exact Qt selection semantics",
               result.succeeded
                   && result.clipboardText
                          == QStringLiteral(
                              "a\nbb\nc")
                   && document.toPlainText()
                          == QStringLiteral("ac")
                   && cursor.position() == 1
                   && !cursor.hasSelection());
        expect("multiline selection cut is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("a\nbb\ncc\nd");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, 6, 3);

        const EditorLineOperationResult result =
            controller.deleteLines(cursor);
        expect("delete expands a multiline selection to touched lines",
               result.succeeded
                   && result.documentChanged
                   && document.toPlainText()
                          == QStringLiteral("a\nd")
                   && cursor.position() == 2);
        expect("multiline delete is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("a\nbb\ncc");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, 5, 2);

        const EditorLineOperationResult result =
            controller.deleteLines(cursor);
        expect("selection ending at a line start excludes that line",
               result.succeeded
                   && document.toPlainText()
                          == QStringLiteral("a\ncc"));
        expect("line-boundary delete is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("a\nlast");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, original.size());

        const EditorLineOperationResult result =
            controller.deleteLines(cursor);
        expect("delete removes a final line without leaving a separator",
               result.succeeded
                   && document.toPlainText()
                          == QStringLiteral("a")
                   && cursor.position() == 1);
        expect("final-line delete is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("foo  \n    bar\nbaz");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(document, 1);

        const EditorLineOperationResult result =
            controller.joinWithNextLine(cursor);
        expect("join normalizes the line boundary to one space",
               result.succeeded
                   && result.documentChanged
                   && document.toPlainText()
                          == QStringLiteral(
                              "foo bar\nbaz")
                   && cursor.position() == 4);
        expect("join is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral(
                "  first  \n"
                "    second   \n"
                "  third  \n"
                "fourth");
        resetDocument(document, original);
        QTextCursor cursor =
            cursorAt(
                document,
                original.indexOf(
                    QStringLiteral("third")) + 2,
                original.indexOf(
                    QStringLiteral("first")));

        const EditorLineOperationResult result =
            controller.joinLines(cursor);
        expect("join combines every logical line touched by a selection",
               result.succeeded
                   && result.documentChanged
                   && document.toPlainText()
                          == QStringLiteral(
                              "  first second third  \n"
                              "fourth")
                   && cursor.position() == 8
                   && !cursor.hasSelection());
        expect("multi-line join is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("foo\n");
        resetDocument(document, original);
        QTextCursor cursor = cursorAt(document, 1);

        const EditorLineOperationResult result =
            controller.execute(
                EditorLineOperation::JoinWithNextLine,
                cursor,
                clipboard);
        expect("join removes a trailing empty line without adding space",
               result.handled
                   && result.succeeded
                   && document.toPlainText()
                          == QStringLiteral("foo")
                   && cursor.position() == 3);
        expect("trailing-line join is one undo transaction",
               oneUndoRestores(document, original));

        cursor = cursorAt(document, document.toPlainText().size());
        const EditorLineOperationResult unavailable =
            controller.joinWithNextLine(cursor);
        expect("join reports a final line without changing it",
               unavailable.handled
                   && !unavailable.succeeded
                   && !unavailable.documentChanged
                   && !unavailable.failureReason.isEmpty()
                   && document.toPlainText() == original);
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("   \n  bar");
        resetDocument(document, original);
        QTextCursor cursor = cursorAt(document, 0);

        const EditorLineOperationResult result =
            controller.joinWithNextLine(cursor);
        expect("join handles an empty left logical line",
               result.succeeded
                   && document.toPlainText()
                          == QStringLiteral("bar")
                   && cursor.position() == 0);
        expect("empty-left join is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString crlfInput =
            QStringLiteral("a\r\nb\r\nc");
        resetDocument(document, crlfInput);
        const QString normalized =
            QStringLiteral("a\nb\nc");
        QTextCursor cursor =
            cursorAt(document,
                     document.toPlainText().indexOf(
                         QStringLiteral("b")));

        const EditorLineOperationResult result =
            controller.cut(cursor, clipboard);
        expect("CRLF input follows QTextDocument logical-line semantics",
               result.succeeded
                   && result.clipboardText
                          == QStringLiteral("b\n")
                   && document.toPlainText()
                          == QStringLiteral("a\nc")
                   && !document.toPlainText()
                           .contains(QLatin1Char('\r')));
        expect("CRLF-normalized cut is one undo transaction",
               oneUndoRestores(document, normalized));
    }

    {
        QTextDocument document;
        const QString crlfInput =
            QStringLiteral(
                "first  \r\n"
                "  second \r\n"
                " third\r\n"
                "last");
        const QString normalized =
            QStringLiteral(
                "first  \n"
                "  second \n"
                " third\n"
                "last");
        resetDocument(document, crlfInput);
        QTextCursor cursor =
            cursorAt(
                document,
                document.toPlainText().indexOf(
                    QStringLiteral("third")) + 2,
                document.toPlainText().indexOf(
                    QStringLiteral("first")));

        const EditorLineOperationResult result =
            controller.joinLines(cursor);
        expect("selected CRLF lines join through logical paragraphs",
               result.succeeded
                   && result.documentChanged
                   && document.toPlainText()
                          == QStringLiteral(
                              "first second third\n"
                              "last")
                   && !document.toPlainText()
                           .contains(QLatin1Char('\r')));
        expect("selected CRLF join is one undo transaction",
               oneUndoRestores(document, normalized));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("aa\nbb\ncc\n");
        resetDocument(document, original);
        QTextCursor cursor = cursorAt(document, 4);

        const EditorLineOperationResult movedUp =
            controller.execute(
                EditorLineOperation::MoveLinesUp,
                cursor);
        expect("move-up reorders one complete logical line",
               movedUp.handled
                   && movedUp.succeeded
                   && movedUp.documentChanged
                   && document.toPlainText()
                          == QStringLiteral(
                              "bb\naa\ncc\n"));
        expect("move-up preserves the cursor line and column",
               cursor.blockNumber() == 0
                   && cursor.position()
                          - cursor.block().position()
                          == 1);
        expect("move-up is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("aa\nbb\ncc\n");
        resetDocument(document, original);
        QTextCursor cursor = cursorAt(document, 4);

        const EditorLineOperationResult movedDown =
            controller.execute(
                EditorLineOperation::MoveLinesDown,
                cursor);
        expect("move-down reorders one complete logical line",
               movedDown.handled
                   && movedDown.succeeded
                   && movedDown.documentChanged
                   && document.toPlainText()
                          == QStringLiteral(
                              "aa\ncc\nbb\n"));
        expect("move-down preserves the cursor line and column",
               cursor.blockNumber() == 2
                   && cursor.position()
                          - cursor.block().position()
                          == 1);
        expect("move-down is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        const QString original =
            QStringLiteral("aa\nbb\ncc\n");
        resetDocument(document, original);
        QTextCursor cursor = cursorAt(document, 6, 3);

        const EditorLineOperationResult movedUp =
            controller.moveLines(cursor, true);
        expect("line-start selection end excludes the next line",
               movedUp.succeeded
                   && document.toPlainText()
                          == QStringLiteral(
                              "bb\naa\ncc\n")
                   && cursor.selectedText()
                          .replace(
                              QChar::ParagraphSeparator,
                              QLatin1Char('\n'))
                          == QStringLiteral("bb\n"));
        expect("selected move-up is one undo transaction",
               oneUndoRestores(document, original));
    }

    {
        QTextDocument document;
        resetDocument(document, QString());
        QTextCursor cursor = cursorAt(document, 0);

        const EditorLineOperationResult copied =
            controller.copy(cursor, clipboard);
        const EditorLineOperationResult cut =
            controller.cut(cursor, clipboard);
        const EditorLineOperationResult deleted =
            controller.deleteLines(cursor);
        expect("empty-document line operations are stable",
               copied.succeeded
                   && copied.clipboardText.isEmpty()
                   && cut.succeeded
                   && !cut.documentChanged
                   && deleted.succeeded
                   && !deleted.documentChanged
                   && document.toPlainText().isEmpty()
                   && !document.isUndoAvailable());
    }

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
