// Headless smoke test for the A1 foundation (TSDocument incremental tree-sitter model).
#include "tsdocument.h"
#include <QString>
#include <cstdio>
#include <cstring>

static int checks = 0, fails = 0;
static void check(const char* what, bool ok) {
    ++checks; if (!ok) ++fails;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
}

int main() {
    TSDocument doc;

    // 1) Valid SV parses to a tree with named children and no error.
    doc.setText(QStringLiteral("module top;\n  logic a;\n  wire b;\nendmodule\n"));
    TSNode root = doc.rootNode();
    printf("root type=%s namedChildren=%u hasError=%d\n",
           ts_node_type(root), ts_node_named_child_count(root), doc.hasError());
    check("root node is non-null", !ts_node_is_null(root));
    check("valid SV has named children", ts_node_named_child_count(root) > 0);
    check("valid SV parses without error", !doc.hasError());

    // 2) namedNodeTypeAt resolves a node at the 'module' keyword (byte 0).
    printf("namedNodeTypeAt(0)=%s\n", doc.namedNodeTypeAt(0));
    check("namedNodeTypeAt(0) returns a type", doc.namedNodeTypeAt(0)[0] != '\0');

    // 3) Error tolerance: half-typed code still yields a usable tree (tree-sitter's key advantage
    //    over Slang for the live layer).
    doc.setText(QStringLiteral("module top;\n  logic \n"));  // incomplete declaration
    printf("half-typed: hasError=%d namedChildren=%u\n",
           doc.hasError(), ts_node_named_child_count(doc.rootNode()));
    check("half-typed code still produces a tree", !ts_node_is_null(doc.rootNode()));

    // 4) Incremental edit smoke: append "x;" inside the doc and reparse incrementally.
    QString before = QStringLiteral("module m;\nendmodule\n");
    doc.setText(before);
    // Insert "logic q;\n" right after "module m;\n" (byte offset 10).
    QString after = QStringLiteral("module m;\nlogic q;\nendmodule\n");
    uint32_t at = 10;                 // byte offset of insertion (start of line 2)
    QByteArray ins = QByteArrayLiteral("logic q;\n");
    doc.applyEdit(at, at, at + (uint32_t)ins.size(),
                  TSPoint{1, 0}, TSPoint{1, 0}, TSPoint{2, 0}, after);
    printf("after incremental edit: hasError=%d namedChildren=%u\n",
           doc.hasError(), ts_node_named_child_count(doc.rootNode()));
    check("incremental edit yields valid tree", !doc.hasError());

    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
