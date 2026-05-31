// Headless relationship regression test. Verifies that relationship analysis in a multi-module
// file attaches line-derived relationships to the containing module, not always the first module.
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "symbolrelationshipengine.h"
#include "syminfo.h"

#include <QApplication>
#include <QString>
#include <cstdio>

static int g_checks = 0;
static int g_fails = 0;

static void expectBool(const char* what, bool got, bool want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-46s got=%s want=%s\n", ok ? "PASS" : "FAIL", what,
           got ? "true" : "false", want ? "true" : "false");
}

static int symbolId(const QList<sym_list::SymbolInfo>& symbols,
                    const QString& name,
                    sym_list::sym_type_e type)
{
    for (const auto& s : symbols) {
        if (s.symbolName == name && s.symbolType == type)
            return s.symbolId;
    }
    return -1;
}

static int symbolIdInScope(const QList<sym_list::SymbolInfo>& symbols,
                           const QString& name,
                           sym_list::sym_type_e type,
                           const QString& moduleScope)
{
    for (const auto& s : symbols) {
        if (s.symbolName == name && s.symbolType == type && s.moduleScope == moduleScope)
            return s.symbolId;
    }
    return -1;
}

static bool hasRel(const QVector<RelationshipToAdd>& rels,
                   int fromId,
                   int toId,
                   SymbolRelationshipEngine::RelationType type)
{
    for (const auto& r : rels) {
        if (r.fromId == fromId && r.toId == toId && r.type == type)
            return true;
    }
    return false;
}

static int countRel(const QVector<RelationshipToAdd>& rels,
                    int fromId,
                    int toId,
                    SymbolRelationshipEngine::RelationType type)
{
    int count = 0;
    for (const auto& r : rels) {
        if (r.fromId == fromId && r.toId == toId && r.type == type)
            ++count;
    }
    return count;
}

static void expectInt(const char* what, int got, int want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-46s got=%d want=%d\n", ok ? "PASS" : "FAIL", what, got, want);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString path = QStringLiteral("test_sv/relationship_inline.sv");
    const QString content = QStringLiteral(
        "module leaf;\n"
        "endmodule\n"
        "\n"
        "module alpha(input logic clk);\n"
        "  task do_alpha; endtask\n"
        "  always @(posedge clk) begin\n"
        "    do_alpha;\n"
        "  end\n"
        "endmodule\n"
        "\n"
        "module beta(input logic clk, input logic rst_n, input logic cond, input logic src, output logic dst);\n"
        "  task do_beta; endtask\n"
        "  leaf u_leaf();\n"
        "  always @(posedge clk or negedge rst_n) begin\n"
        "    if (cond) begin\n"
        "      do_beta;\n"
        "      dst = src; // do_beta; dst = cond;\n"
        "    end\n"
        "  end\n"
        "endmodule\n");

    SlangManager slang;
    auto* db = sym_list::getInstance();
    db->setSymbolsForFile(path, slang.extractSymbols(path, content), content);
    QList<sym_list::SymbolInfo> symbols = db->findSymbolsByFileName(path);

    const int alphaId = symbolId(symbols, QStringLiteral("alpha"), sym_list::sym_module);
    const int betaId = symbolId(symbols, QStringLiteral("beta"), sym_list::sym_module);
    const int leafId = symbolId(symbols, QStringLiteral("leaf"), sym_list::sym_module);
    const int doBetaId = symbolId(symbols, QStringLiteral("do_beta"), sym_list::sym_task);
    const int condId = symbolId(symbols, QStringLiteral("cond"), sym_list::sym_port_input);
    const int srcId = symbolId(symbols, QStringLiteral("src"), sym_list::sym_port_input);
    const int dstId = symbolId(symbols, QStringLiteral("dst"), sym_list::sym_port_output);
    const int betaClkId = symbolIdInScope(symbols, QStringLiteral("clk"), sym_list::sym_port_input,
                                          QStringLiteral("beta"));
    const int rstId = symbolIdInScope(symbols, QStringLiteral("rst_n"), sym_list::sym_port_input,
                                      QStringLiteral("beta"));

    expectBool("symbols include alpha", alphaId > 0, true);
    expectBool("symbols include beta", betaId > 0, true);
    expectBool("symbols include leaf", leafId > 0, true);
    expectBool("symbols include do_beta", doBetaId > 0, true);
    expectBool("symbols include cond", condId > 0, true);
    expectBool("symbols include src", srcId > 0, true);
    expectBool("symbols include dst", dstId > 0, true);
    expectBool("symbols include beta clk", betaClkId > 0, true);
    expectBool("symbols include beta rst_n", rstId > 0, true);

    SymbolRelationshipEngine engine;
    SmartRelationshipBuilder builder(&engine, db, &slang);
    QVector<RelationshipToAdd> rels = builder.computeRelationships(path, content, symbols);

    expectBool("beta instantiates leaf",
               hasRel(rels, betaId, leafId, SymbolRelationshipEngine::INSTANTIATES), true);
    expectBool("alpha does not instantiate leaf",
               hasRel(rels, alphaId, leafId, SymbolRelationshipEngine::INSTANTIATES), false);

    expectBool("beta calls do_beta",
               hasRel(rels, betaId, doBetaId, SymbolRelationshipEngine::CALLS), true);
    expectBool("alpha does not call do_beta",
               hasRel(rels, alphaId, doBetaId, SymbolRelationshipEngine::CALLS), false);
    expectInt("beta calls do_beta exactly once",
              countRel(rels, betaId, doBetaId, SymbolRelationshipEngine::CALLS), 1);

    expectBool("beta reads cond",
               hasRel(rels, betaId, condId, SymbolRelationshipEngine::READS_FROM), true);
    expectBool("alpha does not read cond",
               hasRel(rels, alphaId, condId, SymbolRelationshipEngine::READS_FROM), false);

    expectBool("src assigns to dst",
               hasRel(rels, srcId, dstId, SymbolRelationshipEngine::ASSIGNS_TO), true);
    expectBool("commented cond assignment ignored",
               hasRel(rels, condId, dstId, SymbolRelationshipEngine::ASSIGNS_TO), false);
    expectBool("beta clocked by clk",
               hasRel(rels, betaClkId, betaId, SymbolRelationshipEngine::CLOCKS), true);
    expectBool("beta reset by rst_n",
               hasRel(rels, rstId, betaId, SymbolRelationshipEngine::RESETS), true);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
