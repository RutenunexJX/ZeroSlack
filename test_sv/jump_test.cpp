// Headless jump-resolution test. Builds sym_list from Slang, constructs a MyCodeEditor offscreen
// (no window shown), sets its file/text/cursor, and drives canJumpToDefinition / jumpToDefinition.
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QTextCursor>
#include <QString>
#include <QFileInfo>
#include <QDir>
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

    // After the full GUI path (setSymbolsForFile -> rebuildScopeAndRelationships -> analyzeModuleContainment),
    // function-local symbols must KEEP their subroutine moduleScope (not get clobbered to the module).
    auto scopeOf = [](const QString& name, sym_list::sym_type_e t) -> QString {
        for (const auto& s : sym_list::getInstance()->findSymbolsByName(name))
            if (s.symbolType == t) return s.moduleScope;
        return QStringLiteral("<none>");
    };
    QString xScope = scopeOf("x", sym_list::sym_logic);
    printf("-- DB moduleScope after setSymbolsForFile: x=%s --\n", xScope.toLocal8Bit().constData());
    ++g_checks; if (xScope != QStringLiteral("add_one")) ++g_fails;
    printf("[%s] formal-arg x keeps moduleScope=add_one (not clobbered to top)\n",
           xScope == QStringLiteral("add_one") ? "PASS" : "FAIL");

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
    // Type-name-scoped symbols (current behavior - surfaces the moduleScope vs module filter):
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
    ++g_checks;
    if (ed.textCursor().blockNumber() != counter.startLine - 1) ++g_fails;
    printf("[%s] local jump(counter) lands on block startLine-1\n",
           ed.textCursor().blockNumber() == counter.startLine - 1 ? "PASS" : "FAIL");

    // --- cross-file jump: target defined in a second file; capture the emitted (file,line) ---
    // Derive helper path from the main file's directory (robust to the run cwd).
    QString helperPath = QFileInfo(path).dir().filePath(QStringLiteral("helper_mod.sv"));
    QFile hf(helperPath);
    if (!hf.exists())
        printf("[WARN] helper file not found: %s\n", helperPath.toLocal8Bit().constData());
    if (hf.open(QIODevice::ReadOnly | QFile::Text)) {
        QString hc = QTextStream(&hf).readAll();
        hf.close();
        sym_list::getInstance()->setSymbolsForFile(helperPath, mgr.extractSymbols(helperPath, hc), hc);

        int emittedLine = -1;
        QString emittedFile;
        QObject::connect(&ed, &MyCodeEditor::definitionJumpRequested,
                         [&](const QString&, const QString& file, int line) {
                             emittedFile = file; emittedLine = line;
                         });

        int helperStartLine = -1;
        for (const auto& s : sym_list::getInstance()->findSymbolsByName("helper_mod"))
            if (s.symbolType == sym_list::sym_module) helperStartLine = s.startLine;

        placeCursor(ed, 29);  // outside any module in test_symbols.sv -> no scope filter
        ed.jumpToDefinition("helper_mod", ed.textCursor().position());
        printf("-- cross-file jump(helper_mod): emitted file=%s line=%d, startLine=%d --\n",
               emittedFile.toLocal8Bit().constData(), emittedLine, helperStartLine);
        ++g_checks;
        bool ok = (emittedLine == helperStartLine) && emittedFile.endsWith("helper_mod.sv");
        if (!ok) ++g_fails;
        printf("[%s] cross-file emits 1-based startLine of definition\n", ok ? "PASS" : "FAIL");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
