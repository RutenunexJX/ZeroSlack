// Headless jump-resolution test. Builds sym_list from Slang, constructs a MyCodeEditor offscreen
// (no window shown), sets its file/text/cursor, and drives canJumpToDefinition / jumpToDefinition.
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QTextCursor>
#include <QString>
#include <QPlainTextEdit>
#include <QCompleter>
#include <QTimer>
#include <QMouseEvent>
#include <cstdio>
#include "slangmanager.h"
#include "syminfo.h"
#include "completionmodel.h"
// Test-only: reach the editor's private jump methods. Non-virtual, so ABI is unaffected and the
// calls bind to the real symbols in the already-compiled mycodeeditor.cpp.obj. Qt headers are
// included above (under normal access) so the macro only affects mycodeeditor.h.
#define private public
#include "mycodeeditor.h"
#undef private

static int g_checks = 0, g_fails = 0;

static void expectBool(const char* what, bool got, bool want) {
    ++g_checks; bool ok = (got == want); if (!ok) ++g_fails;
    printf("[%s] %-46s got=%s want=%s\n", ok ? "PASS" : "FAIL", what,
           got ? "true" : "false", want ? "true" : "false");
}

// Put the caret on a given 0-based block (line) so getCurrentModuleScope resolves the module.
static void placeCursor(MyCodeEditor& ed, int block) {
    QTextCursor c = ed.textCursor();
    c.movePosition(QTextCursor::Start);
    c.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, block);
    ed.setTextCursor(c);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) { fprintf(stderr, "cannot open\n"); return 2; }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    sym_list::getInstance()->setSymbolsForFile(path, mgr.extractSymbols(path, content), content);

    MyCodeEditor ed;
    ed.setFileName(path);
    ed.setPlainText(content);

    // Caret inside module `top` (top spans ~line 66..end; block 95 is well inside).
    placeCursor(ed, 95);
    printf("-- caret inside top --\n");
    expectBool("canJump(counter) [module var]",      ed.canJumpToDefinition("counter"), true);
    expectBool("canJump(clk) [module port]",         ed.canJumpToDefinition("clk"),     true);
    expectBool("canJump(DATA_WIDTH) [module param]", ed.canJumpToDefinition("DATA_WIDTH"), true);
    expectBool("canJump(a) [only in adder] isolated",ed.canJumpToDefinition("a"),       false);
    expectBool("canJump(nonexistent)",               ed.canJumpToDefinition("nope_xyz"),false);
    // Type-name-scoped symbols (current behavior — surfaces the moduleScope vs module filter):
    expectBool("canJump(STATE_IDLE) [enum value]",   ed.canJumpToDefinition("STATE_IDLE"), true);
    expectBool("canJump(red) [struct member]",       ed.canJumpToDefinition("red"),     true);

    // Local jump landing: jumpToDefinition moves the caret to the definition.
    sym_list::SymbolInfo counter;
    for (const auto& s : sym_list::getInstance()->findSymbolsByName("counter"))
        if (s.symbolType == sym_list::sym_reg) counter = s;
    placeCursor(ed, 95);
    ed.jumpToDefinition("counter", ed.textCursor().position());
    printf("-- jump(counter): landed block=%d, symbol.startLine=%d (1-based) --\n",
           ed.textCursor().blockNumber(), counter.startLine);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
