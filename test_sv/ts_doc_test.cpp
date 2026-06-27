// Headless test for the A1/A2 foundation: TSDocument incremental model + tree-sitter highlight spans.
#include "tsdocument.h"
#include <QString>
#include <QStringList>
#include <cstdio>
#include <cstring>

static int checks = 0, fails = 0;
static void check(const char* what, bool ok) {
    ++checks; if (!ok) ++fails;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
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

static QString applyInstanceInsertEdit(const QString& src,
                                       const TSInstanceInsertTarget& target) {
    QString result = src;
    result.insert(target.insertChar, target.insertText);
    return result;
}

static QString applyAssignInsertEdit(const QString& src,
                                     const TSAssignInsertTarget& target) {
    QString result = src;
    result.insert(target.insertChar, target.insertText);
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

static QString applyModuleEndInsertEdit(const QString& src,
                                        const TSModuleEndInsertTarget& target) {
    QString result = src;
    result.replace(target.replaceStartChar,
                   target.replaceEndChar - target.replaceStartChar,
                   target.replacementText);
    return result;
}



int main() {
    TSDocument doc;

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

    // 5) Incremental applyEditChars must produce the same highlight as a full re-parse (validates
    //    the editor's edit path: byte/point derivation from char positions).
    {
        QString A = QStringLiteral("module m;\nendmodule\n");
        QString ins = QStringLiteral("  logic x;\n");
        int pos = 10;  // char offset just after "module m;\n"
        QString B = A.left(pos) + ins + A.mid(pos);

        TSDocument inc; inc.setText(A);
        inc.applyEditChars(pos, pos, pos + ins.length(), B);
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
        check("incremental applyEditChars == full parse (highlight spans)", same);
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

    // 7) COM gpo foundation: clear multi-line ANSI module port append points.
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
        check("gpo target: multi-line ANSI ok", target.ok());
        check("gpo target: adds missing comma", target.needsTrailingComma);
        check("gpo target: applies expected edit",
              target.ok() && applyPortAppendEdit(src, target) == expected);
        check("gpo target: caret after inserted indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("    \n);")) + 4);
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
        check("gpo target: existing comma ok",
              target.ok() && !target.needsTrailingComma);
        check("gpo target: does not duplicate comma",
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
        check("gpo target: empty ANSI list ok", target.ok());
        check("gpo target: empty ANSI list edit",
              target.ok() && applyPortAppendEdit(src, target) == expected);
    }

    {
        TSDocument singleLine;
        singleLine.setText(QStringLiteral("module demo (input logic clk);\nendmodule\n"));
        const TSPortAppendTarget singleLineTarget =
            singleLine.portAppendTarget(12);
        check("gpo target: rejects single-line ANSI list",
              singleLineTarget.status
                  == TSPortAppendStatus::NoClearPortAppendPoint);

        TSDocument nonAnsi;
        nonAnsi.setText(QStringLiteral("module demo (clk);\n"
                                       "  input clk;\n"
                                       "endmodule\n"));
        const TSPortAppendTarget nonAnsiTarget =
            nonAnsi.portAppendTarget(nonAnsi.text().indexOf(QStringLiteral("input")));
        check("gpo target: rejects non-ANSI list",
              nonAnsiTarget.status
                  == TSPortAppendStatus::NoClearPortAppendPoint);

        TSDocument noModule;
        noModule.setText(QStringLiteral("logic clk;\n"));
        const TSPortAppendTarget noModuleTarget =
            noModule.portAppendTarget(0);
        check("gpo target: reports no current module",
              noModuleTarget.status == TSPortAppendStatus::NoCurrentModule);
    }

    // 8) COM gsi foundation: signal declaration insert points stay before module body logic.
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
        check("gsi target: after last signal declaration", target.ok());
        check("gsi target: signal edit",
              target.ok() && applySignalInsertEdit(src, target) == expected);
        check("gsi target: signal caret at indent",
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
        check("gsi target: falls back after parameters",
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
        check("gsi target: inserts before first body item",
              target.ok() && applySignalInsertEdit(src, target) == expected);
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
        check("gsi target: does not cross body logic",
              target.ok() && applySignalInsertEdit(src, target) == expected);

        TSDocument noModule;
        noModule.setText(QStringLiteral("logic stray;\n"));
        const TSSignalInsertTarget noModuleTarget =
            noModule.signalInsertTarget(0);
        check("gsi target: reports no current module",
              noModuleTarget.status == TSSignalInsertStatus::NoCurrentModule);
    }

    // 9) COM gii foundation: instance insert points stay before procedural body logic.
    {
        QString src =
            QStringLiteral("module inst_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  child u_child0();\n")
            + QStringLiteral("  child u_child1();\n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSInstanceInsertTarget target =
            d.instanceInsertTarget(src.indexOf(QStringLiteral("assign")));
        const QString expected =
            QStringLiteral("module inst_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  child u_child0();\n")
            + QStringLiteral("  child u_child1();\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        check("gii target: after last instance", target.ok());
        check("gii target: instance edit",
              target.ok() && applyInstanceInsertEdit(src, target) == expected);
        check("gii target: instance caret at indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("  \n  assign")) + 2);
    }

    {
        QString src =
            QStringLiteral("module decl_inst_demo;\n")
            + QStringLiteral("  parameter int W = 8;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSInstanceInsertTarget target =
            d.instanceInsertTarget(src.indexOf(QStringLiteral("assign")));
        const QString expected =
            QStringLiteral("module decl_inst_demo;\n")
            + QStringLiteral("  parameter int W = 8;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        check("gii target: falls back after declarations",
              target.ok() && applyInstanceInsertEdit(src, target) == expected);
    }

    {
        QString src =
            QStringLiteral("module body_inst_demo;\n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSInstanceInsertTarget target =
            d.instanceInsertTarget(src.indexOf(QStringLiteral("assign")));
        const QString expected =
            QStringLiteral("module body_inst_demo;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("endmodule\n");
        check("gii target: inserts before first body item",
              target.ok() && applyInstanceInsertEdit(src, target) == expected);
    }

    {
        QString src =
            QStringLiteral("module late_inst_demo;\n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("  child u_late();\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSInstanceInsertTarget target =
            d.instanceInsertTarget(src.indexOf(QStringLiteral("u_late")));
        const QString expected =
            QStringLiteral("module late_inst_demo;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  assign y = a;\n")
            + QStringLiteral("  child u_late();\n")
            + QStringLiteral("endmodule\n");
        check("gii target: does not cross body logic",
              target.ok() && applyInstanceInsertEdit(src, target) == expected);

        TSDocument noModule;
        noModule.setText(QStringLiteral("child u0();\n"));
        const TSInstanceInsertTarget noModuleTarget =
            noModule.instanceInsertTarget(0);
        check("gii target: reports no current module",
              noModuleTarget.status == TSInstanceInsertStatus::NoCurrentModule);
    }

    // 10) COM gac foundation: continuous assign insert points stay before procedural body logic.
    {
        QString src =
            QStringLiteral("module assign_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  assign y0 = a;\n")
            + QStringLiteral("  assign y1 = a;\n")
            + QStringLiteral("  always_comb y2 = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSAssignInsertTarget target =
            d.assignInsertTarget(src.indexOf(QStringLiteral("always")));
        const QString expected =
            QStringLiteral("module assign_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  assign y0 = a;\n")
            + QStringLiteral("  assign y1 = a;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  always_comb y2 = a;\n")
            + QStringLiteral("endmodule\n");
        check("gac target: after last assign", target.ok());
        check("gac target: assign edit",
              target.ok() && applyAssignInsertEdit(src, target) == expected);
        check("gac target: assign caret at indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("  \n  always")) + 2);
    }

    {
        QString src =
            QStringLiteral("module assign_fallback_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  child u_child();\n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSAssignInsertTarget target =
            d.assignInsertTarget(src.indexOf(QStringLiteral("always")));
        const QString expected =
            QStringLiteral("module assign_fallback_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  child u_child();\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("endmodule\n");
        check("gac target: falls back after declarations and instances",
              target.ok() && applyAssignInsertEdit(src, target) == expected);
    }

    {
        QString src =
            QStringLiteral("module assign_body_demo;\n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSAssignInsertTarget target =
            d.assignInsertTarget(src.indexOf(QStringLiteral("always")));
        const QString expected =
            QStringLiteral("module assign_body_demo;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("endmodule\n");
        check("gac target: inserts before first body item",
              target.ok() && applyAssignInsertEdit(src, target) == expected);
    }

    {
        QString src =
            QStringLiteral("module assign_late_demo;\n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("  assign late_y = a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSAssignInsertTarget target =
            d.assignInsertTarget(src.indexOf(QStringLiteral("late_y")));
        const QString expected =
            QStringLiteral("module assign_late_demo;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("  always_comb y = a;\n")
            + QStringLiteral("  assign late_y = a;\n")
            + QStringLiteral("endmodule\n");
        check("gac target: does not cross procedural body logic",
              target.ok() && applyAssignInsertEdit(src, target) == expected);

        TSDocument noModule;
        noModule.setText(QStringLiteral("assign y = a;\n"));
        const TSAssignInsertTarget noModuleTarget =
            noModule.assignInsertTarget(0);
        check("gac target: reports no current module",
              noModuleTarget.status == TSAssignInsertStatus::NoCurrentModule);
    }

    // 11) COM gpi foundation: parameter insert points prefer #(...) before body declarations.
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
        check("gpi target: parameter port list wins", target.ok());
        check("gpi target: parameter port edit",
              target.ok()
                  && applyParameterInsertEdit(src, target) == expected);
        check("gpi target: parameter port caret at indent",
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
        check("gpi target: empty parameter port list",
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
        check("gpi target: internal parameter declaration section",
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
        check("gpi target: package parameter declaration section",
              target.ok()
                  && applyParameterInsertEdit(src, target) == expected);
    }

    {
        TSDocument noScope;
        noScope.setText(QStringLiteral("parameter int WIDTH = 8;\n"));
        const TSParameterInsertTarget noScopeTarget =
            noScope.parameterInsertTarget(0);
        check("gpi target: reports no current parameter scope",
              noScopeTarget.status
                  == TSParameterInsertStatus::NoCurrentParameterScope);

        TSDocument noParameter;
        noParameter.setText(QStringLiteral("module no_param;\n"
                                           "  logic data;\n"
                                           "endmodule\n"));
        const TSParameterInsertTarget noClearTarget =
            noParameter.parameterInsertTarget(
                noParameter.text().indexOf(QStringLiteral("data")));
        check("gpi target: reports no clear parameter insert point",
              noClearTarget.status
                  == TSParameterInsertStatus::NoClearParameterInsertPoint);
    }

    // 12) COM gef foundation: final module-body insertion point sits before endmodule.
    {
        QString src =
            QStringLiteral("module end_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSModuleEndInsertTarget target =
            d.moduleEndInsertTarget(src.indexOf(QStringLiteral("logic")));
        const QString expected =
            QStringLiteral("module end_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("endmodule\n");
        check("gef target: inserts blank before endmodule", target.ok());
        check("gef target: module end edit",
              target.ok()
                  && applyModuleEndInsertEdit(src, target) == expected);
        check("gef target: caret at body indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("  \nendmodule")) + 2);
    }

    {
        QString src =
            QStringLiteral("module end_blank_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSModuleEndInsertTarget target =
            d.moduleEndInsertTarget(src.indexOf(QStringLiteral("logic")));
        const QString expected =
            QStringLiteral("module end_blank_demo;\n")
            + QStringLiteral("  logic a;\n")
            + QStringLiteral("  \n")
            + QStringLiteral("endmodule\n");
        check("gef target: reuses existing blank before endmodule",
              target.ok()
                  && applyModuleEndInsertEdit(src, target) == expected);
        check("gef target: existing blank caret at body indent",
              target.caretCharAfterEdit
                  == expected.indexOf(QStringLiteral("  \nendmodule")) + 2);
    }

    {
        QString src =
            QStringLiteral("module end_empty_demo;\n")
            + QStringLiteral("endmodule\n");
        TSDocument d; d.setText(src);
        const TSModuleEndInsertTarget target =
            d.moduleEndInsertTarget(src.indexOf(QStringLiteral("module")));
        const QString expected =
            QStringLiteral("module end_empty_demo;\n")
            + QStringLiteral("    \n")
            + QStringLiteral("endmodule\n");
        check("gef target: empty module uses body indent fallback",
              target.ok()
                  && applyModuleEndInsertEdit(src, target) == expected);
    }

    {
        TSDocument noModule;
        noModule.setText(QStringLiteral("logic a;\n"));
        const TSModuleEndInsertTarget noModuleTarget =
            noModule.moduleEndInsertTarget(0);
        check("gef target: reports no current module",
              noModuleTarget.status == TSModuleEndInsertStatus::NoCurrentModule);
    }

    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
