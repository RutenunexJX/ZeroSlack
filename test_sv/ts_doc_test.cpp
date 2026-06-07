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
    }
    // line 1 "module top;": Keyword "module" at local 0 len 6.
    {
        int s = lineStartChar(src, 1); auto sp = doc.highlightSpans(s, lines[1].length());
        check("line1 'module' -> Keyword @0 len6", hasSpan(sp, HlCategory::Keyword, 0, 6));
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

    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
