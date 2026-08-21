#include "formattercursoranchor.h"
#include "formatterservice.h"
#include "mycodeeditor.h"
#include "structuredwhitespaceformatter.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QPoint>
#include <QScrollBar>
#include <QTextCursor>
#include <QVector>

#include <cmath>
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

struct FixtureTokenMapping {
    QString identity;
    int ordinal = -1;
    int oldStart = -1;
    int newStart = -1;
    int length = 0;
};

class FixturePositionMapper final : public FormatterPositionMapper
{
public:
    void addToken(const QString& identity,
                  int ordinal,
                  int oldStart,
                  int newStart,
                  int length)
    {
        FixtureTokenMapping token;
        token.identity = identity;
        token.ordinal = ordinal;
        token.oldStart = oldStart;
        token.newStart = newStart;
        token.length = length;
        mappings.append(token);
    }

    FormatterLogicalPosition capturePosition(
        int oldPosition,
        FormatterPositionAffinity affinity) const override
    {
        return captureFrom(oldPosition, affinity, false);
    }

    int restorePosition(
        const FormatterLogicalPosition& position,
        int newDocumentLength) const override
    {
        if (!position.hasTokenIdentity())
            return -1;
        for (const FixtureTokenMapping& token : mappings) {
            if (token.identity != position.tokenIdentity
                || token.ordinal != position.tokenOrdinal) {
                continue;
            }
            return qBound(0,
                          token.newStart
                              + qBound(0,
                                       position.tokenOffset,
                                       token.length),
                          newDocumentLength);
        }
        return -1;
    }

    FormatterLogicalPosition captureNewPosition(
        int newPosition,
        FormatterPositionAffinity affinity) const
    {
        return captureFrom(newPosition, affinity, true);
    }

private:
    FormatterLogicalPosition captureFrom(
        int position,
        FormatterPositionAffinity affinity,
        bool useNewPositions) const
    {
        FormatterLogicalPosition logical;
        logical.absoluteFallback = position;
        logical.affinity = affinity;

        for (const FixtureTokenMapping& token : mappings) {
            const int start =
                useNewPositions ? token.newStart : token.oldStart;
            if (position >= start
                && position < start + token.length) {
                logical.tokenIdentity = token.identity;
                logical.tokenOrdinal = token.ordinal;
                logical.tokenOffset = position - start;
                return logical;
            }
        }

        if (affinity == FormatterPositionAffinity::Trailing) {
            const FixtureTokenMapping* previous = nullptr;
            for (const FixtureTokenMapping& token : mappings) {
                const int start =
                    useNewPositions ? token.newStart : token.oldStart;
                if (start + token.length > position)
                    continue;
                if (!previous) {
                    previous = &token;
                    continue;
                }
                const int previousStart =
                    useNewPositions
                        ? previous->newStart
                        : previous->oldStart;
                if (start > previousStart)
                    previous = &token;
            }
            if (previous) {
                logical.tokenIdentity = previous->identity;
                logical.tokenOrdinal = previous->ordinal;
                logical.tokenOffset = previous->length;
            }
            return logical;
        }

        const FixtureTokenMapping* next = nullptr;
        for (const FixtureTokenMapping& token : mappings) {
            const int start =
                useNewPositions ? token.newStart : token.oldStart;
            if (start < position)
                continue;
            if (!next) {
                next = &token;
                continue;
            }
            const int nextStart =
                useNewPositions ? next->newStart : next->oldStart;
            if (start < nextStart)
                next = &token;
        }
        if (next) {
            logical.tokenIdentity = next->identity;
            logical.tokenOrdinal = next->ordinal;
            logical.tokenOffset = 0;
        }
        return logical;
    }

    QVector<FixtureTokenMapping> mappings;
};

double scrollRatio(const QScrollBar* scrollBar)
{
    if (!scrollBar
        || scrollBar->maximum() <= scrollBar->minimum()) {
        return 0.0;
    }
    return static_cast<double>(
               scrollBar->value() - scrollBar->minimum())
        / static_cast<double>(
               scrollBar->maximum() - scrollBar->minimum());
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    {
        const QString oldText =
            QStringLiteral(
                "module m;\n"
                "always_comb begin\n"
                "case (sel)\n"
                "2'b0:\n"
                "if (enable)\n"
                "target = selected_value;\n"
                "else\n"
                "target = fallback_value;\n"
                "endcase\n"
                "end\n"
                "endmodule\n");
        const QString formattedText =
            QStringLiteral(
                "module m;\n"
                "    always_comb begin\n"
                "        case (sel)\n"
                "            2'b0:\n"
                "                if (enable)\n"
                "                    target = selected_value;\n"
                "                else\n"
                "                    target = fallback_value;\n"
                "        endcase\n"
                "    end\n"
                "endmodule\n");

        const int oldModule =
            oldText.indexOf(QStringLiteral("module"));
        const int newModule =
            formattedText.indexOf(QStringLiteral("module"));
        const int oldTarget =
            oldText.indexOf(QStringLiteral("target"));
        const int newTarget =
            formattedText.indexOf(QStringLiteral("target"));
        const int oldFallback =
            oldText.indexOf(QStringLiteral("fallback_value"));
        const int newFallback =
            formattedText.indexOf(QStringLiteral("fallback_value"));

        FixturePositionMapper mapper;
        mapper.addToken(QStringLiteral("keyword:module"),
                        0,
                        oldModule,
                        newModule,
                        6);
        mapper.addToken(QStringLiteral("identifier:target"),
                        8,
                        oldTarget,
                        newTarget,
                        6);
        mapper.addToken(QStringLiteral("identifier:fallback_value"),
                        13,
                        oldFallback,
                        newFallback,
                        14);

        QPlainTextEdit editor;
        editor.setPlainText(oldText);
        QTextCursor reverseSelection(editor.document());
        reverseSelection.setPosition(oldFallback + 5);
        reverseSelection.setPosition(oldTarget + 2,
                                     QTextCursor::KeepAnchor);
        editor.setTextCursor(reverseSelection);

        FormatterCursorAnchor anchor;
        expect("capture accepts a live editor and token mapper",
               anchor.capture(&editor, mapper)
                   && anchor.isValid());
        expect("capture records token identity, ordinal and inner offset",
               anchor.state().cursorPosition.tokenIdentity
                       == QStringLiteral("identifier:target")
                   && anchor.state().cursorPosition.tokenOrdinal == 8
                   && anchor.state().cursorPosition.tokenOffset == 2
                   && anchor.state().selectionAnchor.tokenIdentity
                       == QStringLiteral(
                           "identifier:fallback_value")
                   && anchor.state().selectionAnchor.tokenOffset == 5);

        editor.setPlainText(formattedText);
        const FormatterCursorRestoreResult restored =
            anchor.restore(&editor, mapper);
        const QTextCursor cursor = editor.textCursor();
        expect("case and nested-if formatting restores both endpoints",
               restored.cursorMapped
                   && restored.selectionAnchorMapped
                   && cursor.position() == newTarget + 2
                   && cursor.anchor() == newFallback + 5);
        expect("reverse selection direction survives formatting",
               cursor.hasSelection()
                   && cursor.position() < cursor.anchor());
    }

    {
        const QString oldText =
            QStringLiteral(
                "module comment_anchor;\n"
                "// keep cursor inside this comment\n"
                "endmodule\n");
        const QString newText =
            QStringLiteral(
                "module comment_anchor;\n"
                "    // keep cursor inside this comment\n"
                "endmodule\n");
        FormatterTriviaPositionMapper mapper(
            oldText,
            newText);
        const int oldPosition =
            oldText.indexOf(
                QStringLiteral("cursor"))
            + 3;
        const int expectedPosition =
            newText.indexOf(
                QStringLiteral("cursor"))
            + 3;
        const FormatterLogicalPosition logical =
            mapper.capturePosition(
                oldPosition,
                FormatterPositionAffinity::Leading);
        expect("formatter mapping preserves offsets inside comments",
               mapper.isCompatible()
                   && logical.hasTokenIdentity()
                   && mapper.restorePosition(
                          logical,
                          static_cast<int>(
                              newText.size()))
                          == expectedPosition);
    }

    {
        QString oldText =
            QStringLiteral(
                "module scroll_test;\n"
                "always_comb begin\n"
                "case (sel)\n");
        QString formattedText =
            QStringLiteral(
                "module scroll_test;\n"
                "    always_comb begin\n"
                "        case (sel)\n");

        int cursorOldPosition = -1;
        int cursorNewPosition = -1;
        for (int line = 0; line < 120; ++line) {
            const QString signal =
                QStringLiteral("signal_%1")
                    .arg(line, 3, 10, QLatin1Char('0'));
            const QString value =
                QStringLiteral("very_long_value_%1")
                    .arg(line, 3, 10, QLatin1Char('0'));
            const QString oldLine =
                signal
                + QLatin1Char('=')
                + value
                + QStringLiteral(
                    " + another_very_long_expression;\n");
            const QString newLine =
                QStringLiteral("            ")
                + signal
                + QStringLiteral(" = ")
                + value
                + QStringLiteral(
                    " + another_very_long_expression;\n");
            const int oldStart = oldText.size();
            const int newStart =
                formattedText.size() + 12;
            if (line == 75) {
                cursorOldPosition = oldStart + 3;
                cursorNewPosition = newStart + 3;
            }
            oldText += oldLine;
            formattedText += newLine;
        }
        oldText += QStringLiteral(
            "endcase\n"
            "end\n"
            "endmodule\n");
        formattedText += QStringLiteral(
            "        endcase\n"
            "    end\n"
            "endmodule\n");

        FormatterTriviaPositionMapper mapper(oldText,
                                             formattedText);
        expect("extracted Tree-sitter trivia map accepts whitespace-only text",
               mapper.isCompatible());

        QPlainTextEdit editor;
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.resize(300, 180);
        editor.setPlainText(oldText);
        editor.show();
        QCoreApplication::processEvents();

        QTextCursor cursor(editor.document());
        cursor.setPosition(cursorOldPosition);
        editor.setTextCursor(cursor);
        editor.ensureCursorVisible();
        QCoreApplication::processEvents();
        editor.verticalScrollBar()->setValue(
            qMax(editor.verticalScrollBar()->minimum(),
                 editor.verticalScrollBar()->value() - 3));
        editor.horizontalScrollBar()->setValue(
            qMin(30, editor.horizontalScrollBar()->maximum()));
        QCoreApplication::processEvents();

        FormatterCursorAnchor anchor;
        expect("scrolled editor state is captured",
               anchor.capture(&editor, mapper)
                   && anchor.state()
                          .topVisiblePosition
                          .hasTokenIdentity()
                   && anchor.state().verticalScroll.ratio > 0.0);
        const int expectedTopOrdinal =
            anchor.state().topVisiblePosition.tokenOrdinal;
        const int expectedHorizontal =
            anchor.state().horizontalScroll.value;
        const double expectedVerticalRatio =
            anchor.state().verticalScroll.ratio;

        editor.setPlainText(formattedText);
        QCoreApplication::processEvents();
        const FormatterCursorRestoreResult restored =
            anchor.restore(&editor, mapper);
        QCoreApplication::processEvents();

        FormatterTriviaPositionMapper formattedIdentity(
            formattedText,
            formattedText);
        const FormatterLogicalPosition newTop =
            formattedIdentity.capturePosition(
                editor.cursorForPosition(QPoint(0, 0)).position(),
                FormatterPositionAffinity::Leading);
        expect("logical visible-top token survives whitespace reflow",
               restored.viewportMapped
                   && newTop.hasTokenIdentity()
                   && newTop.tokenOrdinal == expectedTopOrdinal);
        expect("vertical scrollbar ratio remains stable",
               std::abs(scrollRatio(editor.verticalScrollBar())
                        - expectedVerticalRatio)
                   < 0.04);
        expect("horizontal scrollbar value is restored",
               editor.horizontalScrollBar()->value()
                   == qBound(
                       editor.horizontalScrollBar()->minimum(),
                       expectedHorizontal,
                       editor.horizontalScrollBar()->maximum()));
        expect("main cursor keeps its token-local offset while scrolled",
               restored.cursorMapped
                   && editor.textCursor().position()
                       == cursorNewPosition);
    }

    {
        const QString source =
            QStringLiteral(
                "module m;\n"
                "always_comb begin\n"
                "value = 1'b0;\n"
                "end\n"
                "endmodule\n");
        MyCodeEditor editor;
        editor.setPlainText(source);
        const int oldValue =
            source.indexOf(QStringLiteral("value"));
        QTextCursor cursor(editor.document());
        cursor.setPosition(oldValue + 3);
        editor.setTextCursor(cursor);

        const FormatterReport firstFormat =
            editor.formatDocument();
        const QString formatted = editor.toPlainText();
        const int newValue =
            formatted.indexOf(QStringLiteral("value"));
        expect("Format Document restores the logical cursor",
               formatted != source
                   && firstFormat.outcome
                          == FormatterOutcome::Applied
                   && firstFormat.changed
                   && newValue >= 0
                   && editor.textCursor().position()
                       == newValue + 3);
        const FormatterReport secondFormat =
            editor.formatDocument();
        expect("Format Document reports a deterministic unchanged outcome",
               secondFormat.accepted()
                   && secondFormat.outcome
                          == FormatterOutcome::Unchanged
                   && !secondFormat.changed
                   && editor.toPlainText() == formatted);
        editor.undo();
        expect("Format Document remains one undo transaction",
               editor.toPlainText() == source);
    }

    std::printf("%d checks, %d failed\n",
                checks,
                failures);
    return failures == 0 ? 0 : 1;
}
