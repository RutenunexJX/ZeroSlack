// Headless test for the A1/A2 foundation: TSDocument incremental model + tree-sitter highlight spans.
#include "tsdocument.h"
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstdio>
#include <cstring>

static int checks = 0, fails = 0;
static void check(const char* what, bool ok) {
    ++checks; if (!ok) ++fails;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    fflush(stdout);
}

static const char* catName(HlCategory c) {
    switch (c) {
    case HlCategory::Keyword: return "Keyword";
    case HlCategory::Comment: return "Comment";
    case HlCategory::String: return "String";
    case HlCategory::Number: return "Number";
    case HlCategory::Operator: return "Operator";
    case HlCategory::Identifier: return "Identifier";
    default: return "None";
    }
}

// char offset of the start of line `lineIdx` (0-based) in `src`.
static int lineStartChar(const QString& src, int lineIdx) {
    int off = 0;
    QStringList lines = src.split('\n');
    for (int i = 0; i < lineIdx && i < lines.size(); ++i)
        off += lines[i].length() + 1;   // +1 for '\n'
    return off;
}

static bool hasSpan(const QVector<HlSpan>& spans, HlCategory cat, int start, int len) {
    for (const auto& s : spans)
        if (s.category == cat && s.start == start && s.length == len) return true;
    return false;
}
static bool hasCatAt(const QVector<HlSpan>& spans, HlCategory cat, int start) {
    for (const auto& s : spans)
        if (s.category == cat && s.start == start) return true;
    return false;
}

static QString applyPortAppendEdit(const QString& src,
                                   const TSPortAppendTarget& target) {
    QString result = src;
    result.insert(target.insertChar, target.insertText);
    if (target.needsTrailingComma)
        result.insert(target.trailingCommaInsertChar, QStringLiteral(","));
    return result;
}

static QString applySignalInsertEdit(const QString& src,
                                     const TSSignalInsertTarget& target) {
    QString result = src;
    result.insert(target.insertChar, target.insertText);
    return result;
}

static QString applySignalOrganizationEdit(
    const QString& src,
    const TSSignalDeclarationOrganizationPlan& plan) {
    QString result = src;
    if (plan.ok()) {
        result.replace(plan.replaceStartChar,
                       plan.replaceEndChar - plan.replaceStartChar,
                       plan.replacementText);
    }
    return result;
}

static QString applyParameterInsertEdit(const QString& src,
                                        const TSParameterInsertTarget& target) {
    QString result = src;
    result.insert(target.insertChar, target.insertText);
    if (target.needsTrailingComma)
        result.insert(target.trailingCommaInsertChar, QStringLiteral(","));
    return result;
}

int main() {
    TSDocument doc;

    {
        TSUTF16Text text;
        QString expected =
            QStringLiteral("module piece_table; endmodule");
        text.setText(expected);
        text.resetMetricsForTest();

        text.replace(0, 0, QStringLiteral("// "));
        expected.insert(0, QStringLiteral("// "));
        const int middle = expected.indexOf(
            QStringLiteral("piece_table"));
        text.replace(middle,
                     QStringLiteral("piece_table").size(),
                     QStringLiteral("piece_storage"));
        expected.replace(middle,
                         QStringLiteral("piece_table").size(),
                         QStringLiteral("piece_storage"));
        text.replace(text.size(), 0, QStringLiteral("\n"));
        expected.append(QLatin1Char('\n'));
        const bool accessorsCorrect =
            text.at(middle) == QLatin1Char('p')
            && text.indexOf(
                   QStringLiteral("piece_storage"))
                   == middle
            && text.lastIndexOf(QLatin1Char('\n'))
                   == text.size() - 1;

        bool differentialMatch = true;
        quint32 state = 0x4a6f7921u;
        for (int edit = 0; edit < 160; ++edit) {
            state = state * 1664525u + 1013904223u;
            const int position = expected.isEmpty()
                ? 0
                : static_cast<int>(
                      state
                      % static_cast<quint32>(
                          expected.size() + 1));
            state = state * 1664525u + 1013904223u;
            const int removedLength =
                qMin(static_cast<int>(state % 6u),
                     expected.size() - position);
            const QString inserted =
                edit % 19 == 0
                ? QStringLiteral("\U0001f600")
                : edit % 7 == 0
                    ? QStringLiteral("xy")
                    : QString(
                          1,
                          QChar(
                              static_cast<char16_t>(
                                  'a' + edit % 26)));
            text.replace(position,
                         removedLength,
                         inserted);
            expected.replace(position,
                             removedLength,
                             inserted);
            differentialMatch =
                differentialMatch
                && text.size() == expected.size()
                && text.mid(0, text.size()) == expected;
        }

        const TSTextStorageMetrics metrics =
            text.metricsForTest();
        check("piece-table edits preserve exact UTF-16 text",
              accessorsCorrect
                  && differentialMatch
                  && text.mid(0, text.size()) == expected);
        check("piece-table edits move no unchanged document characters",
              metrics.editCount == 163
                  && metrics.movedCharacterCount == 0
                  && metrics.materializationCount == 0);
    }

    {
        TSUTF16Text splitSurrogate;
        splitSurrogate.setText(QStringLiteral("ab"));
        splitSurrogate.resetMetricsForTest();
        splitSurrogate.replace(
            1, 0, QString(1, QChar(0xde00)));
        splitSurrogate.replace(
            1, 0, QString(1, QChar(0xd83d)));

        uint32_t bytesRead = 0;
        const char* bytes =
            splitSurrogate.read(2u, &bytesRead);
        const auto* utf16 =
            reinterpret_cast<const char16_t*>(bytes);
        check("piece-table input joins a surrogate pair across pieces",
              bytes != nullptr
                  && bytesRead == 4u
                  && utf16[0] == char16_t(0xd83d)
                  && utf16[1] == char16_t(0xde00)
                  && splitSurrogate.mid(1, 2)
                      == QStringLiteral("\U0001f600")
                  && splitSurrogate.metricsForTest()
                             .materializationCount == 0);
    }

    // 1) Valid SV parses without error.
    doc.setText(QStringLiteral("module top;\n  logic a;\nendmodule\n"));
    check("valid SV parses without error", !doc.hasError());
    check("root has named children", ts_node_named_child_count(doc.rootNode()) > 0);

    // 2) Error tolerance on half-typed code.
    doc.setText(QStringLiteral("module top;\n  logic \n"));
    check("half-typed code still yields a tree", !ts_node_is_null(doc.rootNode()));

    QString src =
        QStringLiteral("// ascii comment abc\n")     // line 0
        + QStringLiteral("module top;\n")        // line 1
        + QStringLiteral("  logic [7:0] data;\n")// line 2
        + QStringLiteral("  // tail comment\n")      // line 3
        + QStringLiteral("endmodule\n");          // line 4
    doc.setText(src);

    QStringList lines = src.split('\n');
    for (int li = 0; li < 5; ++li) {
        int start = lineStartChar(src, li);
        int len = lines[li].length();
        QVector<HlSpan> spans = doc.highlightSpans(start, len);
        printf("line %d [start=%d len=%d] \"%s\":\n", li, start, len, lines[li].toLocal8Bit().constData());
        for (const auto& s : spans)
            printf("    %-10s @local %d len %d\n", catName(s.category), s.start, s.length);
    }

    // line 0: a comment covering the whole line (local start 0).
    {
        int s = lineStartChar(src, 0); auto sp = doc.highlightSpans(s, lines[0].length());
        check("line0 comment -> Comment @0", hasCatAt(sp, HlCategory::Comment, 0));
        check("comment query: line comment true", doc.isCommentAt(s + 3));
    }
    // line 1 "module top;": Keyword "module" at local 0 len 6.
    {
        int s = lineStartChar(src, 1); auto sp = doc.highlightSpans(s, lines[1].length());
        check("line1 'module' -> Keyword @0 len6", hasSpan(sp, HlCategory::Keyword, 0, 6));
        check("comment query: code false", !doc.isCommentAt(s + 1));
    }
    // line 2 "  logic [7:0] data;": Keyword "logic" at local 2 len 5; some Number present.
    {
        int s = lineStartChar(src, 2); auto sp = doc.highlightSpans(s, lines[2].length());
        check("line2 'logic' -> Keyword @2 len5", hasSpan(sp, HlCategory::Keyword, 2, 5));
        bool anyNum = false; for (auto& x : sp) if (x.category == HlCategory::Number) anyNum = true;
        check("line2 has a Number span (7/0)", anyNum);
    }
    {
        int s = lineStartChar(src, 3); auto sp = doc.highlightSpans(s, lines[3].length());
        check("line3 trailing comment -> Comment @2", hasCatAt(sp, HlCategory::Comment, 2));
        check("comment query: trailing comment true", doc.isCommentAt(s + 5));
    }

    {
        const QString commentSource =
            QStringLiteral("module unicode_comment;\r\n"
                           "  logic before; /* 嵌套样式 // 文本\r\n"
                           "  signal_in_comment = 16'hff;\r\n"
                           "  end marker */ logic after = 8'h2a;\r\n"
                           "endmodule\r\n");
        TSDocument comments;
        comments.setText(commentSource);
        const int middle =
            commentSource.indexOf(QStringLiteral("signal_in_comment"));
        const int after =
            commentSource.indexOf(QStringLiteral("after"));
        const int commentedNumber =
            commentSource.indexOf(QStringLiteral("16'hff")) + 3;
        const int liveNumber =
            commentSource.indexOf(QStringLiteral("8'h2a")) + 3;
        check("block comment query covers Unicode CRLF middle line",
              comments.isCommentAt(middle));
        check("block comment query stops before code on closing line",
              !comments.isCommentAt(after));
        check("numeric query excludes block-comment number",
              !comments.numericLiteralAt(commentedNumber).ok());
        const TSNumericLiteralTarget numeric =
            comments.numericLiteralAt(liveNumber);
        check("numeric query returns exact live token after block comment",
              numeric.ok()
                  && numeric.text == QStringLiteral("8'h2a")
                  && numeric.evaluationText
                      == QStringLiteral("8'h2a")
                  && numeric.startChar
                      == commentSource.indexOf(QStringLiteral("8'h2a")));
    }

    {
        const QString numericSource =
            QStringLiteral(
                "module numeric_context;\n"
                "  localparam logic signed [7:0] NEG = -8'sd1;\n"
                "  localparam logic [7:0] CAT = {4'ha, 4'h5};\n"
                "endmodule\n");
        TSDocument numericDocument;
        numericDocument.setText(numericSource);
        const TSNumericLiteralTarget negative =
            numericDocument.numericLiteralAt(
                numericSource.indexOf(
                    QStringLiteral("8'sd1")) + 3);
        check("numeric query includes structural unary sign for evaluation",
              negative.ok()
                  && negative.text == QStringLiteral("8'sd1")
                  && negative.evaluationText
                      == QStringLiteral("-8'sd1"));
        const TSNumericLiteralTarget concatenated =
            numericDocument.numericLiteralAt(
                numericSource.indexOf(
                    QStringLiteral("4'ha")) + 2);
        check("numeric query remains exact inside concatenation",
              concatenated.ok()
                  && concatenated.text == QStringLiteral("4'ha")
                  && concatenated.evaluationText
                      == QStringLiteral("4'ha"));
    }

    {
        const QString asciiSource =
            QStringLiteral(
                "module ascii_context;\n"
                "  string printable = \"A\";\n"
                "  string lower_boundary = \" \";\n"
                "  string upper_boundary = \"~\";\n"
                "  string escaped = \"\\n\";\n"
                "  string escaped_quote = \"\\\"\";\n"
                "  string non_ascii = \"中\";\n"
                "  string multiple = \"AB\";\n"
                "  `include \"A\"\n"
                "endmodule\n");
        TSDocument asciiDocument;
        asciiDocument.setText(asciiSource);
        const auto asciiTarget =
            [&asciiDocument, &asciiSource](const QString& token,
                                           int occurrence = 0) {
                int position = -1;
                int from = 0;
                for (int i = 0; i <= occurrence; ++i) {
                    position = asciiSource.indexOf(token, from);
                    if (position < 0)
                        break;
                    from = position + token.size();
                }
                return asciiDocument.numericLiteralAt(
                    position < 0 ? position : position + 1);
            };
        const TSNumericLiteralTarget printableAscii =
            asciiTarget(QStringLiteral("\"A\""));
        check("numeric query accepts one printable ASCII string token",
              printableAscii.ok()
                  && printableAscii.stringLiteral
                  && printableAscii.text == QStringLiteral("A")
                  && printableAscii.evaluationText
                      == QStringLiteral("\"A\""));
        check("numeric query accepts printable ASCII boundaries",
              asciiTarget(QStringLiteral("\" \"")).ok()
                  && asciiTarget(QStringLiteral("\"~\"")).ok());
        check("numeric query accepts one escaped ASCII character",
              asciiTarget(QStringLiteral("\"\\n\"")).ok()
                  && asciiTarget(QStringLiteral("\"\\\"\"")).ok());
        check("numeric query marks string candidates for shared evaluation",
              asciiTarget(QStringLiteral("\"中\"")).stringLiteral
                  && asciiTarget(QStringLiteral("\"AB\"")).stringLiteral);
        check("numeric query rejects include path strings",
              !asciiTarget(QStringLiteral("\"A\""), 1).ok());
    }

    {
        QString current =
            QStringLiteral("module incremental_comment;\n"
                           "  logic sig = 8'h2a;\n"
                           "endmodule\n");
        TSDocument incremental;
        incremental.setText(current);
        const int signalStart = current.indexOf(QStringLiteral("sig"));
        const QString signalText = QStringLiteral("sig = 8'h2a;");

        DocumentChange open;
        open.position = signalStart;
        open.removedLength = signalText.size();
        open.removedText = signalText;
        open.insertedText = QStringLiteral("/*sig = 8'h2a;*/");
        open.oldLength = current.size();
        open.newLength = current.size() - signalText.size()
            + open.insertedText.size();
        open.startLine = 1;
        open.startColumn = 8;
        incremental.applyEdit(open);
        current.replace(open.position, open.removedLength, open.insertedText);
        check("incremental open comment boundary suppresses signal",
              incremental.text() == current
                  && incremental.isCommentAt(signalStart + 2));

        DocumentChange close;
        close.position = signalStart;
        close.removedLength = open.insertedText.size();
        close.removedText = open.insertedText;
        close.insertedText = signalText;
        close.oldLength = current.size();
        close.newLength = current.size() - close.removedLength
            + close.insertedText.size();
        close.startLine = 1;
        close.startColumn = 8;
        incremental.applyEdit(close);
        current.replace(close.position,
                        close.removedLength,
                        close.insertedText);
        check("incremental close comment boundary restores following code",
              incremental.text() == current
                  && !incremental.isCommentAt(signalStart + 2));
    }

    // 5) A fragment delta must produce the same highlight as a full re-parse.
    {
        QString A = QStringLiteral("module m;\nendmodule\n");
        QString ins = QStringLiteral("  logic x;\n");
        int pos = 10;  // char offset just after "module m;\n"
        QString B = A.left(pos) + ins + A.mid(pos);

        DocumentChange change;
        change.position = pos;
        change.insertedText = ins;
        change.oldLength = A.size();
        change.newLength = B.size();
        change.startLine = 1;
        change.startColumn = 0;
        change.oldEndLine = 1;
        change.newEndLine = 2;
        change.lineDelta = 1;
        TSDocument inc; inc.setText(A);
        inc.applyEdit(change);
        TSDocument full; full.setText(B);

        QStringList bl = B.split('\n');
        bool same = true;
        for (int li = 0; li < bl.size() && same; ++li) {
            int s = lineStartChar(B, li);
            auto a = inc.highlightSpans(s, bl[li].length());
            auto b = full.highlightSpans(s, bl[li].length());
            if (a.size() != b.size()) { same = false; break; }
            for (int k = 0; k < a.size(); ++k)
                if (a[k].start != b[k].start || a[k].length != b[k].length || a[k].category != b[k].category) {
                    same = false; break;
                }
        }
        check("incremental fragment delta == full parse (highlight spans)", same);
    }

    // 6) Live enclosing-module scope (A3): cursor inside which module, derived from the tree.
    {
        TSDocument repeated;
        QString current;
        const QStringList replacements{
            QStringLiteral("ab"),
            QStringLiteral("FOO"),
            QStringLiteral("`FOO"),
            QStringLiteral("`FOO_BAR"),
            QStringLiteral("obj.member"),
            QStringLiteral("pkg::member")
        };
        for (const QString& replacement : replacements) {
            if (!current.isEmpty()) {
                DocumentChange clear;
                clear.removedLength = current.size();
                clear.removedText = current;
                clear.oldLength = current.size();
                repeated.applyEdit(clear);
                current.clear();
            }
            for (QChar ch : replacement) {
                DocumentChange insert;
                insert.position = current.size();
                insert.insertedText = ch;
                insert.oldLength = current.size();
                insert.newLength = current.size() + 1;
                insert.startColumn = current.size();
                repeated.applyEdit(insert);
                current.append(ch);
            }
            check("repeated fragment replacement keeps TS cache exact",
                  repeated.text() == current);
        }
    }

    {
        TSDocument multilineReplacement;
        const QString multiline = QStringLiteral(
            "module command_line;\n"
            "  logic a;\n"
            "  logic b;\n"
            "  assign b = a;\n"
            "endmodule\n");
        multilineReplacement.setText(multiline);

        DocumentChange clear;
        clear.removedLength = multiline.size();
        clear.removedText = multiline;
        clear.oldLength = multiline.size();
        clear.newLength = 0;
        clear.startLine = 0;
        clear.startColumn = 0;
        clear.oldEndLine = multiline.count(QLatin1Char('\n'));
        multilineReplacement.applyEdit(clear);
        check("multiline full deletion keeps TS cache empty",
              multilineReplacement.text().isEmpty());

        DocumentChange insert;
        insert.insertedText = QStringLiteral("`");
        insert.oldLength = 0;
        insert.newLength = 1;
        multilineReplacement.applyEdit(insert);
        check("input after multiline full deletion keeps TS cache exact",
              multilineReplacement.text() == QStringLiteral("`"));
    }

    {
        TSDocument structuralReplacement;
        QString current = QStringLiteral("sig");
        structuralReplacement.setText(current);

        const auto replaceWholeDocument =
            [&structuralReplacement, &current](const QString& replacement) {
                DocumentChange change;
                change.removedLength = current.size();
                change.removedText = current;
                change.insertedText = replacement;
                change.oldLength = current.size();
                change.newLength = replacement.size();
                change.oldEndLine =
                    current.count(QLatin1Char('\n'));
                change.newEndLine =
                    replacement.count(QLatin1Char('\n'));
                change.lineDelta =
                    change.newEndLine - change.oldEndLine;
                structuralReplacement.applyEdit(change);
                current = replacement;
                return structuralReplacement.text() == current
                    && !ts_node_is_null(
                        structuralReplacement.rootNode());
            };

        bool exact = true;
        for (int iteration = 0; iteration < 256 && exact; ++iteration) {
            exact = replaceWholeDocument(QStringLiteral("(sig)"))
                && replaceWholeDocument(QStringLiteral("sig"))
                && replaceWholeDocument(QString())
                && replaceWholeDocument(QStringLiteral("[]"))
                && !structuralReplacement.isCommentAt(0)
                && replaceWholeDocument(QStringLiteral("// ("))
                && structuralReplacement.isCommentAt(1)
                && replaceWholeDocument(QStringLiteral("sig"));
        }
        check("repeated structural whole-document replacement keeps Tree-sitter exact",
              exact);
    }

    // 6) Live enclosing-module scope (A3): cursor inside which module, derived from the tree.
    {
        QString src =
            QStringLiteral("module top;\n")        // line 0
            + QStringLiteral("  logic a;\n")        // line 1  (inside top)
            + QStringLiteral("endmodule\n")          // line 2
            + QStringLiteral("\n")                   // line 3  (between modules)
            + QStringLiteral("module adder;\n")      // line 4
            + QStringLiteral("  logic b;\n")         // line 5  (inside adder)
            + QStringLiteral("endmodule\n");          // line 6
        TSDocument d; d.setText(src);
        auto at = [&](int line) { return d.enclosingModuleName(lineStartChar(src, line) + 2); };
        check("scope: inside top -> top",     at(1) == QStringLiteral("top"));
        check("scope: inside adder -> adder", at(5) == QStringLiteral("adder"));
        check("scope: between modules -> empty", d.enclosingModuleName(lineStartChar(src, 3)).isEmpty());

        // Error tolerance: editing mid-module (an incomplete line) with endmodule present still
        QString partial =
            QStringLiteral("module fsm;\n")    // 0
            + QStringLiteral("  logic [3:\n")   // 1  incomplete declaration (being typed)
            + QStringLiteral("  logic ok;\n")   // 2
            + QStringLiteral("endmodule\n");     // 3
        TSDocument d2; d2.setText(partial);
        check("scope: incomplete line w/ endmodule still -> fsm",
              d2.enclosingModuleName(lineStartChar(partial, 1) + 2) == QStringLiteral("fsm"));
    }

    // 7) add port: clear multi-line ANSI module port append points.
    {
        QString src =
            QStringLiteral("module demo (\n")
            + QStringLiteral("    input  logic clk,\n")
            + QStringLiteral("    input  logic rst_n,\n")
            + QStringLiteral("    output logic done\n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSPortAppendTarget target =
            d.portAppendTarget(src.indexOf(QStringLiteral("done")));
        const QString expected =
            QStringLiteral("module demo (\n")
            + QStringLiteral("    input  logic clk,\n")
            + QStringLiteral("    input  logic rst_n,\n")
            + QStringLiteral("    output logic done,\n")
            + QStringLiteral("    \n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        check("add port target: multi-line ANSI ok", target.ok());
        check("add port target: adds missing comma", target.needsTrailingComma);
        check("add port target: applies expected edit",
              target.ok() && applyPortAppendEdit(src, target) == expected);
        check("add port target: caret after inserted indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("    \n);")) + 4);
    }

    {
        QString src =
            QStringLiteral("module demo #(\n")
            + QStringLiteral("    parameter int W = 8\n")
            + QStringLiteral(") (\n")
            + QStringLiteral("    // clock group\n")
            + QStringLiteral("    input logic clk, // source clock\n")
            + QStringLiteral("    output logic done // result\n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);
        const TSPortAppendTarget target =
            d.portAppendTarget(src.indexOf(QStringLiteral("done")));
        const QString edited = applyPortAppendEdit(src, target);
        check("add port target: parameterized ANSI comments ok",
              target.ok());
        check("add port target: comment-aware comma anchor",
              target.ok()
                  && target.needsTrailingComma
                  && edited.contains(
                      QStringLiteral(
                          "output logic done, // result\n"
                          "    \n);")));
    }

    {
        QString src =
            QStringLiteral("module demo (\n")
            + QStringLiteral("    input logic clk,\n")
            + QStringLiteral("    output logic done,\n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSPortAppendTarget target =
            d.portAppendTarget(src.indexOf(QStringLiteral("done")));
        const QString edited = applyPortAppendEdit(src, target);
        check("add port target: existing comma ok",
              target.ok() && !target.needsTrailingComma);
        check("add port target: does not duplicate comma",
              edited.contains(QStringLiteral("done,\n    \n);"))
                  && !edited.contains(QStringLiteral("done,,"))
        );
    }

    {
        QString src =
            QStringLiteral("module demo (\n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSPortAppendTarget target =
            d.portAppendTarget(src.indexOf(QStringLiteral("demo")));
        const QString expected =
            QStringLiteral("module demo (\n")
            + QStringLiteral("    \n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        check("add port target: empty ANSI list ok", target.ok());
        check("add port target: empty ANSI list edit",
              target.ok() && applyPortAppendEdit(src, target) == expected);
    }

    {
        TSDocument singleLine;
        singleLine.setText(QStringLiteral("module demo (input logic clk);\nendmodule\n"));
        const TSPortAppendTarget singleLineTarget =
            singleLine.portAppendTarget(12);
        check("add port target: rejects single-line ANSI list",
              singleLineTarget.status
                  == TSPortAppendStatus::NoClearPortAppendPoint);

        TSDocument nonAnsi;
        nonAnsi.setText(QStringLiteral("module demo (clk);\n"
                                       "  input clk;\n"
                                       "endmodule\n"));
        const TSPortAppendTarget nonAnsiTarget =
            nonAnsi.portAppendTarget(nonAnsi.text().indexOf(QStringLiteral("input")));
        check("add port target: rejects non-ANSI list",
              nonAnsiTarget.status
                  == TSPortAppendStatus::NoClearPortAppendPoint);

        TSDocument noModule;
        noModule.setText(QStringLiteral("logic clk;\n"));
        const TSPortAppendTarget noModuleTarget =
            noModule.portAppendTarget(0);
        check("add port target: reports no current module",
              noModuleTarget.status == TSPortAppendStatus::NoCurrentModule);
    }

    // 8) add signal: declaration insert points stay before module body logic.
    {
        QString src =
            QStringLiteral("module sig_demo;\n")
            + QStringLiteral("  parameter int W = 8;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  wire b;\n")
            + QStringLiteral("  reg c;\n")
            + QStringLiteral("  assign y = c;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSSignalInsertTarget target =
            d.signalInsertTarget(src.indexOf(QStringLiteral("assign")));
        const QString expected =
            QStringLiteral("module sig_demo;\n")
            + QStringLiteral("  parameter int W = 8;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  wire b;\n")
            + QStringLiteral("  reg c;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  assign y = c;\n")
            + QStringLiteral("endmodule\n");
        check("add signal target: after last signal declaration", target.ok());
        check("add signal target: signal edit",
              target.ok() && applySignalInsertEdit(src, target) == expected);
        check("add signal target: signal caret at indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("  \n  assign")) + 2);
    }

    {
        QString src =
            QStringLiteral("module param_demo;\n")
            + QStringLiteral("  parameter int W = 8;\n")
            + QStringLiteral("  localparam int L = 1;\n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSSignalInsertTarget target =
            d.signalInsertTarget(src.indexOf(QStringLiteral("always")));
        const QString expected =
            QStringLiteral("module param_demo;\n")
            + QStringLiteral("  parameter int W = 8;\n")
            + QStringLiteral("  localparam int L = 1;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("endmodule\n");
        check("add signal target: falls back after parameters",
              target.ok() && applySignalInsertEdit(src, target) == expected);
    }

    {
        QString src =
            QStringLiteral("module body_demo;\n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSSignalInsertTarget target =
            d.signalInsertTarget(src.indexOf(QStringLiteral("assign")));
        const QString expected =
            QStringLiteral("module body_demo;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        check("add signal target: inserts before first body item",
              target.ok() && applySignalInsertEdit(src, target) == expected);
    }

    {
        const QString src =
            QStringLiteral("module directive_demo(\n")
            + QStringLiteral("  input logic clk\n")
            + QStringLiteral(");\n")
            + QStringLiteral("`define DEBUG_MARK\n")
            + QStringLiteral("logic payload;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);
        const TSSignalInsertTarget target =
            d.signalInsertTarget(
                src.indexOf(QStringLiteral("payload")));
        check("add signal target: directive boundary is structural",
              target.ok()
                  && target.insertChar
                      == src.indexOf(QStringLiteral("`define"))
                  && target.insertText == QStringLiteral("\n")
                  && target.caretCharAfterEdit == target.insertChar);
    }

    {
        QString src =
            QStringLiteral("module late_demo;\n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("  logic late_sig;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSSignalInsertTarget target =
            d.signalInsertTarget(src.indexOf(QStringLiteral("late_sig")));
        const QString expected =
            QStringLiteral("module late_demo;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("  logic late_sig;\n")
            + QStringLiteral("endmodule\n");
        check("add signal target: does not cross body logic",
              target.ok() && applySignalInsertEdit(src, target) == expected);

        const TSSignalInsertTarget bridgeTarget =
            d.sourceBridgeInsertTarget(
                src.indexOf(QStringLiteral("late_sig")));
        QString bridged = src;
        if (bridgeTarget.ok()) {
            const int caret =
                bridgeTarget.caretCharAfterEdit
                - bridgeTarget.insertChar;
            const QString edit =
                bridgeTarget.insertText.left(caret)
                + QStringLiteral("assign late_out = late_sig;")
                + bridgeTarget.insertText.mid(caret);
            bridged.insert(bridgeTarget.insertChar, edit);
        }
        check("source bridge target: after selected declaration",
              bridgeTarget.ok()
                  && bridged.contains(QStringLiteral(
                      "  logic late_sig;\n  assign late_out = late_sig;\n")));

        TSDocument noModule;
        noModule.setText(QStringLiteral("logic stray;\n"));
        const TSSignalInsertTarget noModuleTarget =
            noModule.signalInsertTarget(0);
        check("add signal target: reports no current module",
              noModuleTarget.status == TSSignalInsertStatus::NoCurrentModule);
    }

    // Signal organization uses only direct module declarations and preserves
    // declaration order plus attached comments.
    {
        const QString src =
            QStringLiteral("module organize_demo;\n")
            + QStringLiteral("  import pkg::*;\n")
            + QStringLiteral("  typedef logic [3:0] nibble_t;\n")
            + QStringLiteral("  localparam int W = 4;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  assign y = late;\n")
            + QStringLiteral("  // late signal\n")
            + QStringLiteral("  wire [W-1:0] late; // retained\n")
            + QStringLiteral("  always_comb begin\n")
            + QStringLiteral("    logic procedural_local;\n")
            + QStringLiteral("    procedural_local = early;\n")
            + QStringLiteral("  end\n")
            + QStringLiteral("endmodule\n");
        const QString expected =
            QStringLiteral("module organize_demo;\n")
            + QStringLiteral("  import pkg::*;\n")
            + QStringLiteral("  typedef logic [3:0] nibble_t;\n")
            + QStringLiteral("  localparam int W = 4;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  // late signal\n")
            + QStringLiteral("  wire [W-1:0] late; // retained\n")
            + QStringLiteral("  assign y = late;\n")
            + QStringLiteral("  always_comb begin\n")
            + QStringLiteral("    logic procedural_local;\n")
            + QStringLiteral("    procedural_local = early;\n")
            + QStringLiteral("  end\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);
        const TSSignalDeclarationOrganizationPlan plan =
            d.signalDeclarationOrganizationPlan(
                src.indexOf(QStringLiteral("assign")));
        check("organize signals: late declaration has a safe edit plan",
              plan.ok()
                  && plan.declarationCount == 2
                  && plan.movedDeclarationCount == 2);
        check("organize signals: comments move with declarations and locals stay put",
              plan.ok()
                  && applySignalOrganizationEdit(src, plan) == expected);

        TSDocument organized;
        organized.setText(expected);
        check("organize signals: already organized module is reported",
              organized.signalDeclarationOrganizationPlan(
                           expected.indexOf(QStringLiteral("assign")))
                      .status
                  == TSSignalDeclarationOrganizationStatus::AlreadyOrganized);
    }

    {
        const QString broken =
            QStringLiteral("module broken;\n")
            + QStringLiteral("  logic missing_semicolon\n")
            + QStringLiteral("  assign y = missing_semicolon;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(broken);
        check("organize signals: syntax errors reject structural movement",
              d.signalDeclarationOrganizationPlan(
                   broken.indexOf(QStringLiteral("assign")))
                      .status
                  == TSSignalDeclarationOrganizationStatus::ModuleHasSyntaxError);
    }

    {
        const QString src =
            QStringLiteral("module macro_preamble_demo;\n")
            + QStringLiteral("`define VALUE_EXPR (early + 1'b1)\n")
            + QStringLiteral("  localparam int W = 4;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  assign y = `VALUE_EXPR;\n")
            + QStringLiteral("  logic [W-1:0] late;\n")
            + QStringLiteral("endmodule\n");
        const QString expected =
            QStringLiteral("module macro_preamble_demo;\n")
            + QStringLiteral("`define VALUE_EXPR (early + 1'b1)\n")
            + QStringLiteral("  localparam int W = 4;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  logic [W-1:0] late;\n")
            + QStringLiteral("  assign y = `VALUE_EXPR;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);
        const TSSignalDeclarationOrganizationPlan plan =
            d.signalDeclarationOrganizationPlan(
                src.indexOf(QStringLiteral("late")));
        check("organize signals: module macro definitions remain in the declaration preamble",
              plan.ok()
                  && plan.declarationCount == 2
                  && applySignalOrganizationEdit(src, plan) == expected);
    }

    {
        const QString conditional =
            QStringLiteral("module conditional_demo;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  assign y = early;\n")
            + QStringLiteral("`ifdef FEATURE\n")
            + QStringLiteral("  assign feature_y = early;\n")
            + QStringLiteral("`endif\n")
            + QStringLiteral("  logic late;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(conditional);
        check("organize signals: conditional compilation is a movement barrier",
              d.signalDeclarationOrganizationPlan(
                   conditional.indexOf(QStringLiteral("late")))
                      .status
                  == TSSignalDeclarationOrganizationStatus::UnsafeLayout);
    }

    {
        const QString src =
            QStringLiteral("module trailing_conditional_demo;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  assign y = early;\n")
            + QStringLiteral("  logic late;\n")
            + QStringLiteral("`ifdef FEATURE\n")
            + QStringLiteral("  assign feature_y = late;\n")
            + QStringLiteral("`endif\n")
            + QStringLiteral("endmodule\n");
        const QString expected =
            QStringLiteral("module trailing_conditional_demo;\n")
            + QStringLiteral("  logic early;\n")
            + QStringLiteral("  logic late;\n")
            + QStringLiteral("  assign y = early;\n")
            + QStringLiteral("`ifdef FEATURE\n")
            + QStringLiteral("  assign feature_y = late;\n")
            + QStringLiteral("`endif\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);
        const TSSignalDeclarationOrganizationPlan plan =
            d.signalDeclarationOrganizationPlan(
                src.indexOf(QStringLiteral("late")));
        check("organize signals: trailing conditional compilation is not crossed",
              plan.ok()
                  && applySignalOrganizationEdit(src, plan) == expected);
    }

    // 9) add parameter: parameter lists precede body declarations.
    {
        QString src =
            QStringLiteral("module param_port_demo #(\n")
            + QStringLiteral("  parameter int WIDTH = 8,\n")
            + QStringLiteral("  localparam int DEPTH = 16\n")
            + QStringLiteral(") (\n")
            + QStringLiteral("  input logic clk\n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSParameterInsertTarget target =
            d.parameterInsertTarget(src.indexOf(QStringLiteral("clk")));
        const QString expected =
            QStringLiteral("module param_port_demo #(\n")
            + QStringLiteral("  parameter int WIDTH = 8,\n")
            + QStringLiteral("  localparam int DEPTH = 16,\n")
            + QStringLiteral("  \n")
            + QStringLiteral(") (\n")
            + QStringLiteral("  input logic clk\n")
            + QStringLiteral(");\n")
            + QStringLiteral("endmodule\n");
        check("add parameter target: parameter port list wins", target.ok());
        check("add parameter target: parameter port edit",
              target.ok()
                  && applyParameterInsertEdit(src, target) == expected);
        check("add parameter target: parameter port caret at indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("  \n)")) + 2);
    }

    {
        QString src =
            QStringLiteral("module param_empty_demo #(\n")
            + QStringLiteral(") ();\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSParameterInsertTarget target =
            d.parameterInsertTarget(src.indexOf(QStringLiteral("endmodule")));
        const QString expected =
            QStringLiteral("module param_empty_demo #(\n")
            + QStringLiteral("    \n")
            + QStringLiteral(") ();\n")
            + QStringLiteral("endmodule\n");
        check("add parameter target: empty parameter port list",
              target.ok()
                  && applyParameterInsertEdit(src, target) == expected);
    }

    {
        QString src =
            QStringLiteral("module param_internal_demo;\n")
            + QStringLiteral("  parameter int WIDTH = 8;\n")
            + QStringLiteral("  localparam int DEPTH = 16;\n")
            + QStringLiteral("  logic data;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSParameterInsertTarget target =
            d.parameterInsertTarget(src.indexOf(QStringLiteral("data")));
        const QString expected =
            QStringLiteral("module param_internal_demo;\n")
            + QStringLiteral("  parameter int WIDTH = 8;\n")
            + QStringLiteral("  localparam int DEPTH = 16;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  logic data;\n")
            + QStringLiteral("endmodule\n");
        check("add parameter target: internal declaration section",
              target.ok()
                  && applyParameterInsertEdit(src, target) == expected);
    }

    {
        QString src =
            QStringLiteral("package param_pkg;\n")
            + QStringLiteral("  parameter int WIDTH = 8;\n")
            + QStringLiteral("  localparam int DEPTH = 16;\n")
            + QStringLiteral("  typedef int data_t;\n")
            + QStringLiteral("endpackage\n");
        TSDocument d; d.setText(src);
        const TSParameterInsertTarget target =
            d.parameterInsertTarget(src.indexOf(QStringLiteral("data_t")));
        const QString expected =
            QStringLiteral("package param_pkg;\n")
            + QStringLiteral("  parameter int WIDTH = 8;\n")
            + QStringLiteral("  localparam int DEPTH = 16;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  typedef int data_t;\n")
            + QStringLiteral("endpackage\n");
        check("add parameter target: package declaration section",
              target.ok()
                  && applyParameterInsertEdit(src, target) == expected);
    }

    {
        TSDocument noScope;
        noScope.setText(QStringLiteral("parameter int WIDTH = 8;\n"));
        const TSParameterInsertTarget noScopeTarget =
            noScope.parameterInsertTarget(0);
        check("add parameter target: reports no current parameter scope",
              noScopeTarget.status
                  == TSParameterInsertStatus::NoCurrentParameterScope);

        TSDocument noParameter;
        noParameter.setText(QStringLiteral("module no_param;\n"
                                           "  logic data;\n"
                                           "endmodule\n"));
        const TSParameterInsertTarget noClearTarget =
            noParameter.parameterInsertTarget(
                noParameter.text().indexOf(QStringLiteral("data")));
        check("add parameter target: reports no clear insert point",
              noClearTarget.status
                  == TSParameterInsertStatus::NoClearParameterInsertPoint);
    }

    // 10) go endmodule resolves navigation without editing the document.
    {
        const QString src =
            QStringLiteral("module end_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);
        const TSModuleEndNavigationTarget target =
            d.moduleEndNavigationTarget(
                src.indexOf(QStringLiteral("logic")));
        check("go endmodule target resolves", target.ok());
        check("go endmodule target is before final endmodule",
              target.caretChar
                  == src.indexOf(QStringLiteral("endmodule")));
        check("go endmodule query does not change source", d.text() == src);
    }

    {
        const QString src =
            QStringLiteral("module first;\n")
            + QStringLiteral("endmodule\n")
            + QStringLiteral("module second;\n")
            + QStringLiteral("  logic b;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);
        const TSModuleEndNavigationTarget target =
            d.moduleEndNavigationTarget(
                src.indexOf(QStringLiteral("logic b")));
        check("go endmodule selects current module final token",
              target.ok()
                  && target.caretChar
                      == src.lastIndexOf(QStringLiteral("endmodule")));
    }

    {
        TSDocument noModule;
        noModule.setText(QStringLiteral("logic a;\n"));
        const TSModuleEndNavigationTarget target =
            noModule.moduleEndNavigationTarget(0);
        check("go endmodule reports no current module",
                  target.status
                      == TSModuleEndNavigationStatus::NoCurrentModule);
    }

    // Structural identifier, undefined-signal context, and instance-slot
    // queries share the same live Tree-sitter document.
    {
        const QString src =
            QStringLiteral("module slot_demo;\n")
            + QStringLiteral("  always_ff @(posedge clk) begin\n")
            + QStringLiteral("    missing_q <= source_q;\n")
            + QStringLiteral("  end\n")
            + QStringLiteral("  child #(\n")
            + QStringLiteral("    .WIDTH(P + fn(a, {b, c})),\n")
            + QStringLiteral("    .DEPTH(4)\n")
            + QStringLiteral("  ) u_child(\n")
            + QStringLiteral("    .clk(clk),\n")
            + QStringLiteral("    .data(missing_data),\n")
            + QStringLiteral("    .ready()\n")
            + QStringLiteral("  );\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(src);

        const int dataPos =
            src.indexOf(QStringLiteral("missing_data")) + 2;
        const TSIdentifierTarget identifier = d.identifierAt(dataPos);
        check("structural identifier exact span",
              identifier.ok()
                  && identifier.text == QStringLiteral("missing_data")
                  && src.mid(identifier.startChar,
                             identifier.endChar - identifier.startChar)
                      == identifier.text);

        const int identifierEnd =
            src.indexOf(QStringLiteral("missing_data"))
            + QStringLiteral("missing_data").size();
        const TSIdentifierTarget beforePunctuation =
            d.identifierAt(identifierEnd);
        check("structural identifier resolves from the caret before punctuation",
              beforePunctuation.ok()
                  && beforePunctuation.text
                      == QStringLiteral("missing_data"));

        const TSInstantiationTarget instance =
            d.instantiationAt(dataPos);
        check("instance slot query resolves type and instance",
              instance.ok()
                  && instance.moduleType == QStringLiteral("child")
                  && instance.instanceName == QStringLiteral("u_child"));
        check("instance slot query orders parameters before ports",
              instance.parameterActuals.size() == 2
                  && instance.portActuals.size() == 3
                  && src.mid(
                         instance.parameterActuals.at(0).startChar,
                         instance.parameterActuals.at(0).endChar
                             - instance.parameterActuals.at(0).startChar)
                      == QStringLiteral("P + fn(a, {b, c})")
                  && src.mid(
                         instance.portActuals.at(1).startChar,
                         instance.portActuals.at(1).endChar
                             - instance.portActuals.at(1).startChar)
                      == QStringLiteral("missing_data"));
        check("instance slot query preserves empty named actual",
              instance.portActuals.at(2).startChar
                      == instance.portActuals.at(2).endChar
                  && instance.portActuals.at(2).name
                      == QStringLiteral("ready"));

        const TSUndefinedSignalContext portContext =
            d.undefinedSignalContextAt(dataPos);
        check("undefined named-port actual carries exact formal context",
              portContext.ok()
                  && portContext.kind
                      == TSUndefinedSignalContextKind::NamedPortActual
                  && portContext.formalName == QStringLiteral("data")
                  && src.mid(portContext.formalStartChar, 4)
                      == QStringLiteral("data")
                  && portContext.instantiation.instanceName
                      == QStringLiteral("u_child"));

        const TSUndefinedSignalContext lhsContext =
            d.undefinedSignalContextAt(
                src.indexOf(QStringLiteral("missing_q")) + 2);
        check("undefined procedural assignment lhs is recognized",
              lhsContext.ok()
                  && lhsContext.kind
                      == TSUndefinedSignalContextKind::
                          ProceduralAssignmentLhs);
        check("assignment rhs is not a creation context",
              !d.undefinedSignalContextAt(
                    src.indexOf(QStringLiteral("source_q")) + 2)
                   .ok());
        check("formal, comment, and string identifiers are excluded",
              !d.undefinedSignalContextAt(
                    src.indexOf(QStringLiteral(".data")) + 2)
                   .ok());
    }

    {
        const QString positional =
            QStringLiteral("module positional_demo;\n")
            + QStringLiteral("  child #(P, fn(a, b)) u0(\n")
            + QStringLiteral("    clk,\n")
            + QStringLiteral("    {left, right}\n")
            + QStringLiteral("  );\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(positional);
        const TSInstantiationTarget target =
            d.instantiationAt(
                positional.indexOf(QStringLiteral("u0")));
        check("positional instance slots preserve complex expressions",
              target.ok()
                  && target.parameterActuals.size() == 2
                  && target.portActuals.size() == 2
                  && positional.mid(
                         target.portActuals.at(1).startChar,
                         target.portActuals.at(1).endChar
                             - target.portActuals.at(1).startChar)
                      == QStringLiteral("{left, right}"));

        TSDocument incomplete;
        incomplete.setText(
            QStringLiteral("module bad; child #(.P(1) u0(.a(x); endmodule\n"));
        check("incomplete instantiation is rejected conservatively",
              !incomplete.instantiationAt(
                   incomplete.text().indexOf(QStringLiteral("u0")))
                   .ok());
    }

    // signal.exposeToTop relies exclusively on Tree-sitter for named
    // instance-connection insertion anchors. The query must distinguish
    // compatible reuse, conflicts, positional instances, and CRLF/Unicode
    // source coordinates without scanning SystemVerilog text.
    {
        const QString named =
            QStringLiteral("module parent;\n")
            + QStringLiteral("  child u_child(\n")
            + QStringLiteral("    .clk(clk)\n")
            + QStringLiteral("  );\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(named);
        const TSNamedPortConnectionTarget target =
            d.namedPortConnectionTarget(
                named.indexOf(QStringLiteral("u_child")),
                QStringLiteral("trace_out"));
        check("named connection target resolves exact multiline insertion",
              target.status == TSNamedPortConnectionStatus::Ok
                  && target.instanceName == QStringLiteral("u_child")
                  && target.moduleType == QStringLiteral("child")
                  && target.needsTrailingComma
                  && target.trailingCommaInsertChar
                      == named.indexOf(QStringLiteral(".clk"))
                          + QStringLiteral(".clk(clk)").size()
                  && target.insertChar
                      == named.indexOf(QStringLiteral("  );"))
                  && target.prefix == QStringLiteral("    ")
                  && target.suffix == QStringLiteral("\n"));
    }

    {
        const QString existing =
            QStringLiteral("module parent;\n")
            + QStringLiteral("  child u_child(\n")
            + QStringLiteral("    .trace_out(parent_trace)\n")
            + QStringLiteral("  );\n")
            + QStringLiteral("endmodule\n");
        TSDocument d;
        d.setText(existing);
        const TSNamedPortConnectionTarget target =
            d.namedPortConnectionTarget(
                existing.indexOf(QStringLiteral("u_child")),
                QStringLiteral("trace_out"));
        check("named connection target exposes structural existing actual",
              target.status
                      == TSNamedPortConnectionStatus::AlreadyConnected
                  && target.existingActual
                      == QStringLiteral("parent_trace"));
    }

    {
        const QString positional =
            QStringLiteral("module parent;\n"
                           "  child u_child(clk, data);\n"
                           "endmodule\n");
        TSDocument d;
        d.setText(positional);
        check("named connection target rejects positional instance",
              d.namedPortConnectionTarget(
                   positional.indexOf(QStringLiteral("u_child")),
                   QStringLiteral("trace_out"))
                      .status
                  == TSNamedPortConnectionStatus::PositionalConnections);
    }

    {
        const QString crlfUnicode =
            QStringLiteral("module parent;\r\n"
                           "  // 中文注释\r\n"
                           "  child u_child(\r\n"
                           "    .clk(clk)\r\n"
                           "  );\r\n"
                           "endmodule\r\n");
        TSDocument d;
        d.setText(crlfUnicode);
        const TSNamedPortConnectionTarget target =
            d.namedPortConnectionTarget(
                crlfUnicode.indexOf(QStringLiteral("u_child")),
                QStringLiteral("trace_out"));
        check("named connection target preserves Unicode CRLF coordinates",
              target.status == TSNamedPortConnectionStatus::Ok
                  && target.insertChar
                      == crlfUnicode.indexOf(QStringLiteral("  );"))
                  && target.prefix == QStringLiteral("    ")
                  && target.suffix == QStringLiteral("\r\n"));
    }

    // Interactive structural input is sourced from the live Tree-sitter
    // snapshot. It must not require a saved Slang compilation.
    {
        const QString incompleteBegin =
            QStringLiteral("module input_demo;\n"
                           "always_comb begin");
        TSDocument d;
        d.setText(incompleteBegin);
        const TSStructuralNewlineTarget target =
            d.structuralNewlineTarget(incompleteBegin.size());
        check("structural Enter closes a newly typed begin",
              target.ok()
                  && target.insertedClosingKeyword
                  && target.insertionText
                      == QStringLiteral("\n    \nend")
                  && target.caretOffset == 5);

        const QString pairedBegin =
            QStringLiteral("module input_demo;\n"
                           "always_comb begin\n"
                           "end\n"
                           "endmodule\n");
        d.setText(pairedBegin);
        const int pairedCursor =
            pairedBegin.indexOf(QStringLiteral("begin"))
            + QStringLiteral("begin").size();
        const TSStructuralNewlineTarget pairedTarget =
            d.structuralNewlineTarget(pairedCursor);
        check("structural Enter does not duplicate an existing end",
              pairedTarget.ok()
                  && !pairedTarget.insertedClosingKeyword
                  && pairedTarget.insertionText
                      == QStringLiteral("\n    "));

        const QString indentedLine =
            QStringLiteral("module input_demo;\n"
                           "always_comb begin\n"
                           "    logic value;\n"
                           "end\n"
                           "endmodule\n");
        d.setText(indentedLine);
        const int physicalLineStart =
            indentedLine.indexOf(QStringLiteral("    logic value;"));
        const TSStructuralNewlineTarget atPhysicalStart =
            d.structuralNewlineTarget(physicalLineStart);
        const TSStructuralNewlineTarget insideIndent =
            d.structuralNewlineTarget(physicalLineStart + 2);
        const TSStructuralNewlineTarget atLogicalStart =
            d.structuralNewlineTarget(physicalLineStart + 4);
        check("structural Enter preserves exactly the indentation before the cursor",
              atPhysicalStart.insertionText == QStringLiteral("\n")
                  && atPhysicalStart.caretOffset == 1
                  && insideIndent.insertionText == QStringLiteral("\n  ")
                  && insideIndent.caretOffset == 3
                  && atLogicalStart.insertionText
                      == QStringLiteral("\n    ")
                  && atLogicalStart.caretOffset == 5
                  && !atPhysicalStart.insertedClosingKeyword
                  && !insideIndent.insertedClosingKeyword
                  && !atLogicalStart.insertedClosingKeyword);

        const QString whitespaceLine =
            QStringLiteral("module input_demo;\n"
                           "always_comb begin\n"
                           "    \n"
                           "end\n"
                           "endmodule\n");
        d.setText(whitespaceLine);
        const int whitespaceStart =
            whitespaceLine.indexOf(QStringLiteral("    \n"));
        check("structural Enter does not duplicate a whitespace-only line indent",
              d.structuralNewlineTarget(whitespaceStart).insertionText
                      == QStringLiteral("\n")
                  && d.structuralNewlineTarget(whitespaceStart + 4)
                         .insertionText
                      == QStringLiteral("\n    "));

        const QString closingLine =
            QStringLiteral("module input_demo;\n"
                           "always_comb begin\n"
                           "end\n"
                           "endmodule\n");
        d.setText(closingLine);
        const int closingLineStart =
            closingLine.indexOf(QStringLiteral("end\n"));
        check("structural Enter at a following line does not reopen begin",
              d.structuralNewlineTarget(closingLineStart).insertionText
                      == QStringLiteral("\n")
                  && !d.structuralNewlineTarget(closingLineStart)
                          .insertedClosingKeyword);
    }

    {
        const QString caseBody =
            QStringLiteral("module case_input;\n"
                           "always_comb begin\n"
                           "    case (state)\n"
                           "        IDLE:\n"
                           "            next = RUN;\n"
                           "        default:\n"
                           "            next = IDLE;\n"
                           "    endcase\n"
                           "end\n"
                           "endmodule\n");
        TSDocument d;
        d.setText(caseBody);
        const int labelCursor =
            caseBody.indexOf(QStringLiteral("IDLE:"))
            + QStringLiteral("IDLE:").size();
        const TSStructuralNewlineTarget target =
            d.structuralNewlineTarget(labelCursor);
        check("structural Enter indents the first case-item statement",
              target.ok()
                  && target.insertionText
                      == QStringLiteral("\n            "));

        const int statementLineStart =
            caseBody.indexOf(QStringLiteral("            next = RUN;"));
        check("structural Enter on the next case line does not reapply label indent",
              d.structuralNewlineTarget(statementLineStart).insertionText
                  == QStringLiteral("\n"));
    }

    {
        const QString keywordSource =
            QStringLiteral("module keyword_demo;\n"
                           "always_comb begin\n"
                           "    beg\n"
                           "end\n"
                           "endmodule\n");
        TSDocument d;
        d.setText(keywordSource);
        const int cursor =
            keywordSource.lastIndexOf(QStringLiteral("beg"))
            + QStringLiteral("beg").size();
        const TSKeywordCompletionTarget completion =
            d.uniqueKeywordCompletionAt(cursor);
        check("Tree-sitter keyword completion resolves unique begin",
              completion.ok()
                  && completion.prefix == QStringLiteral("beg")
                  && completion.keyword == QStringLiteral("begin")
                  && completion.suffix == QStringLiteral("in"));

        const QString ambiguous =
            QStringLiteral("module keyword_demo;\n"
                           "  alw\n"
                           "endmodule\n");
        d.setText(ambiguous);
        check("ambiguous keyword prefix has no ghost completion",
              !d.uniqueKeywordCompletionAt(
                    ambiguous.indexOf(QStringLiteral("alw")) + 3)
                   .ok());

        const QString comment =
            QStringLiteral("module keyword_demo;\n"
                           "  // beg\n"
                           "endmodule\n");
        d.setText(comment);
        check("keyword ghost is suppressed in comments",
              !d.uniqueKeywordCompletionAt(
                    comment.indexOf(QStringLiteral("beg")) + 3)
                   .ok());
    }

    {
        QString largeKeywordSource =
            QStringLiteral("module keyword_perf;\n"
                           "always_comb begin\n");
        largeKeywordSource.reserve(400000);
        for (int index = 0; index < 12000; ++index) {
            largeKeywordSource +=
                QStringLiteral("    logic value_%1;\n")
                    .arg(index);
        }
        largeKeywordSource +=
            QStringLiteral("    beg\n"
                           "end\n"
                           "endmodule\n");
        TSDocument d;
        d.setText(largeKeywordSource);
        const int cursor =
            largeKeywordSource.lastIndexOf(
                QStringLiteral("beg")) + 3;
        QElapsedTimer timer;
        timer.start();
        const TSKeywordCompletionTarget completion =
            d.uniqueKeywordCompletionAt(cursor);
        const qint64 elapsedNanoseconds =
            timer.nsecsElapsed();
        std::printf(
            "keyword lookahead large-file latency: %.3f ms\n",
            static_cast<double>(elapsedNanoseconds)
                / 1000000.0);
        check("large-file keyword completion stays on parse-state lookahead",
              completion.ok()
                  && completion.keyword
                         == QStringLiteral("begin")
                  && elapsedNanoseconds < 50000000);
    }

    {
        const QString nestedPairs =
            QStringLiteral("module pair_demo;\n"
                           "always_comb begin\n"
                           "    case (state)\n"
                           "        IDLE: begin\n"
                           "        end\n"
                           "    endcase\n"
                           "end\n"
                           "endmodule\n");
        TSDocument d;
        d.setText(nestedPairs);
        const int innerBegin =
            nestedPairs.indexOf(QStringLiteral("begin"),
                                nestedPairs.indexOf(QStringLiteral("IDLE")));
        const TSKeywordPairTarget beginPair =
            d.matchingKeywordPairAt(innerBegin + 1);
        check("nested begin/end pair resolves structurally",
              beginPair.ok()
                  && beginPair.openingStartChar == innerBegin
                  && nestedPairs.mid(beginPair.closingStartChar,
                                     beginPair.closingEndChar
                                         - beginPair.closingStartChar)
                      == QStringLiteral("end"));
        const TSKeywordPairTarget boundaryPair =
            d.matchingKeywordPairAt(
                innerBegin + QStringLiteral("begin").size());
        check("begin/end pair resolves at the closing keyword boundary",
              boundaryPair.ok()
                  && boundaryPair.openingStartChar == innerBegin
                  && boundaryPair.closingStartChar
                         == beginPair.closingStartChar);

        const int caseStart =
            nestedPairs.indexOf(QStringLiteral("case"));
        const TSKeywordPairTarget casePair =
            d.matchingKeywordPairAt(caseStart + 1);
        check("case/endcase pair resolves structurally",
              casePair.ok()
                  && casePair.openingKeyword == QStringLiteral("case")
                      && casePair.closingKeyword == QStringLiteral("endcase"));
    }

    {
        const QString occurrenceSource =
            QStringLiteral(
                "module occurrence_demo;\n"
                "logic sig;\n"
                "always_comb begin\n"
                "    sig = sig;\n"
                "    if (enable) begin\n"
                "        sig = sig;\n"
                "        logic sig_extra;\n"
                "        sig_extra = sig_extra;\n"
                "        logic sig2;\n"
                "        sig2 = sig2 + 1;\n"
                "        // sig\n"
                "        text = \"sig\";\n"
                "    end\n"
                "end\n"
                "endmodule\n");
        TSDocument d;
        d.setText(occurrenceSource);
        const int innerSignal =
            occurrenceSource.indexOf(
                QStringLiteral("sig = sig"),
                occurrenceSource.indexOf(
                    QStringLiteral("if (enable)")));
        const TSIdentifierOccurrenceSet occurrences =
            d.identifierOccurrencesAt(
                innerSignal
                + QStringLiteral("sig").size());
        check("identifier occurrences use the nearest Tree-sitter lexical scope",
              occurrences.ok()
                  && occurrences.selected.text
                         == QStringLiteral("sig")
                  && occurrences.occurrences.size() == 2
                  && occurrences.occurrences.at(0).startChar
                         == innerSignal
                  && occurrences.occurrences.at(1).startChar
                         == innerSignal + 6
                  && occurrences.scopeStartChar
                         < innerSignal
                  && occurrences.scopeEndChar
                         > innerSignal + 9);
        const int extendedSignal =
            occurrenceSource.indexOf(
                QStringLiteral("sig_extra ="));
        const TSIdentifierOccurrenceSet extendedOccurrences =
            d.identifierOccurrencesAt(extendedSignal + 2);
        check("identifier occurrence boundaries do not match prefixes or adjacent identifiers",
              extendedOccurrences.ok()
                  && extendedOccurrences.selected.text
                         == QStringLiteral("sig_extra")
                  && extendedOccurrences.occurrences.size() == 3
                  && std::all_of(
                      extendedOccurrences.occurrences.cbegin(),
                      extendedOccurrences.occurrences.cend(),
                      [&occurrenceSource](
                          const TSIdentifierTarget& range) {
                          return occurrenceSource.mid(
                                     range.startChar,
                                     range.endChar
                                         - range.startChar)
                              == QStringLiteral("sig_extra");
                      }));
        check("identifier occurrence queries reject comments strings and numbers",
              !d.identifierOccurrencesAt(
                    occurrenceSource.indexOf(
                        QStringLiteral("// sig")) + 3).ok()
                  && !d.identifierOccurrencesAt(
                         occurrenceSource.indexOf(
                             QStringLiteral("\"sig\"")) + 2).ok()
                  && !d.identifierOccurrencesAt(
                         occurrenceSource.indexOf(
                             QStringLiteral("+ 1")) + 2).ok());
    }

    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
