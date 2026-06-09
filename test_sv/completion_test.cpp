// Headless completion-logic test. Populates sym_list from Slang, then drives CompletionManager's
// public query methods and asserts the results. No GUI window is shown.
#include "slangmanager.h"
#include "completionmanager.h"
#include "completionservice.h"
#include "semanticindexsnapshot.h"
#include "syminfo.h"
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstdio>

static int g_checks = 0;
static int g_fails = 0;

static QStringList sorted(QStringList l) { l.sort(); return l; }

static void expectEq(const char* what, const QString& got, const QString& want) {
    ++g_checks;
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=\"%s\" want=\"%s\"\n", ok ? "PASS" : "FAIL", what,
           got.toLocal8Bit().constData(), want.toLocal8Bit().constData());
}

static void expectList(const char* what, QStringList got, QStringList want) {
    ++g_checks;
    got = sorted(got);
    want = sorted(want);
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", what,
           got.join(",").toLocal8Bit().constData(),
           want.join(",").toLocal8Bit().constData());
}

static void expectExcludes(const char* what, const QStringList& got, const QStringList& mustNot,
                           const QStringList& mustHave) {
    ++g_checks;
    bool ok = true;
    for (const QString& n : mustNot) if (got.contains(n)) ok = false;
    for (const QString& n : mustHave) if (!got.contains(n)) ok = false;
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=[%s]\n", ok ? "PASS" : "FAIL", what,
           got.join(",").toLocal8Bit().constData());
}

static sym_list::SymbolInfo makeSymbol(const QString& name,
                                       sym_list::sym_type_e type,
                                       const QString& moduleScope,
                                       const QString& dataType,
                                       int symbolId)
{
    sym_list::SymbolInfo symbol;
    symbol.fileName = QStringLiteral("snapshot_only.sv");
    symbol.symbolName = name;
    symbol.symbolType = type;
    symbol.moduleScope = moduleScope;
    symbol.dataType = dataType;
    symbol.startLine = symbolId;
    symbol.startColumn = 1;
    symbol.endLine = symbolId;
    symbol.endColumn = 1;
    symbol.position = 0;
    symbol.length = name.size();
    symbol.symbolId = symbolId;
    return symbol;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) {
        fprintf(stderr, "cannot open %s\n", path.toLocal8Bit().constData());
        return 2;
    }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    QList<sym_list::SymbolInfo> syms = mgr.extractSymbols(path, content);
    sym_list::getInstance()->setSymbolsForFile(path, syms, content);

    CompletionManager* cm = CompletionManager::getInstance();

    // --- struct member completion (typedef'd) ---
    expectEq("getStructTypeForVariable(pixel)", cm->getStructTypeForVariable("pixel", "top"), "pixel_t");
    expectList("members of pixel_t", cm->getStructMemberCompletions("", "pixel_t"),
               {"red", "green", "blue"});

    // --- struct member completion (inline anonymous) ---
    expectEq("getStructTypeForVariable(byte_split)", cm->getStructTypeForVariable("byte_split", "top"), "byte_split");
    expectList("members of byte_split", cm->getStructMemberCompletions("", "byte_split"),
               {"hi", "lo"});

    // --- prefix filtering on members (note: matching is fuzzy/abbreviation, not strict prefix) ---
    // 'bl' is a subsequence only of "blue"; "green"/"red" don't contain b..l in order.
    expectList("members of pixel_t prefix 'bl'", cm->getStructMemberCompletions("bl", "pixel_t"),
               {"blue"});

    // --- module-internal logic must NOT leak function locals (x, add_one return var) ---
    QStringList logicNames;
    for (const auto& s : cm->getModuleInternalSymbolsByType("top", sym_list::sym_logic, ""))
        logicNames << s.symbolName;
    expectExcludes("top logic excludes fn-locals", logicNames,
                   /*mustNot*/ {"x", "add_one"}, /*mustHave*/ {"enable", "result"});

    CompletionQuery query;
    query.prefix = "en";
    query.fileName = path;
    query.moduleName = "top";
    expectList("CompletionService module prefix", CompletionService::getInstance()->findCompletions(query),
               {"enable"});

    CompletionQuery memberQuery;
    memberQuery.structTypeNameForMember = "pixel_t";
    expectList("CompletionService struct members",
               CompletionService::getInstance()->findCompletions(memberQuery),
               {"red", "green", "blue"});

    memberQuery.prefix = "bl";
    const QList<sym_list::SymbolInfo> memberSymbols =
        CompletionService::getInstance()->findCompletionSymbols(memberQuery);
    expectList("CompletionService struct prefix",
               CompletionService::getInstance()->findCompletions(memberQuery),
               {"blue"});
    ++g_checks;
    const bool serviceSymbolOk = memberSymbols.size() == 1
        && memberSymbols.first().symbolName == QStringLiteral("blue")
        && memberSymbols.first().symbolType == sym_list::sym_struct_member
        && memberSymbols.first().moduleScope == QStringLiteral("pixel_t");
    if (!serviceSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           serviceSymbolOk ? "PASS" : "FAIL",
           "CompletionService struct symbols",
           memberSymbols.size());
    QString parsedVariableName;
    QString parsedMemberPrefix;
    ++g_checks;
    const bool parsedMemberContext =
        CompletionService::getInstance()->tryParseStructMemberContext(
            QStringLiteral("assign result = pixel.bl"),
            parsedVariableName,
            parsedMemberPrefix);
    const bool parseOk = parsedMemberContext
        && parsedVariableName == QStringLiteral("pixel")
        && parsedMemberPrefix == QStringLiteral("bl");
    if (!parseOk)
        ++g_fails;
    printf("[%s] %-34s var=\"%s\" prefix=\"%s\"\n",
           parseOk ? "PASS" : "FAIL",
           "CompletionService member parse",
           parsedVariableName.toLocal8Bit().constData(),
           parsedMemberPrefix.toLocal8Bit().constData());
    ++g_checks;
    const bool rejectsNonMemberContext =
        !CompletionService::getInstance()->tryParseStructMemberContext(
            QStringLiteral("assign result = pixel"),
            parsedVariableName,
            parsedMemberPrefix);
    if (!rejectsNonMemberContext)
        ++g_fails;
    printf("[%s] %-34s\n",
           rejectsNonMemberContext ? "PASS" : "FAIL",
           "CompletionService member parse reject");

    CommandCompletionQuery commandQuery;
    commandQuery.fileName = path;
    commandQuery.moduleName = "top";
    commandQuery.symbolType = sym_list::sym_logic;
    commandQuery.prefix = "en";
    expectList("CompletionService command logic",
               CompletionService::getInstance()->findCommandCompletions(commandQuery),
               {"enable"});

    const QList<sym_list::SymbolInfo> commandLogicSymbols =
        CompletionService::getInstance()->findCommandCompletionSymbols(commandQuery);
    ++g_checks;
    const bool commandLogicOk = commandLogicSymbols.size() == 1
        && commandLogicSymbols.first().symbolName == QStringLiteral("enable")
        && commandLogicSymbols.first().symbolType == sym_list::sym_logic
        && commandLogicSymbols.first().moduleScope == QStringLiteral("top");
    if (!commandLogicOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           commandLogicOk ? "PASS" : "FAIL",
           "CompletionService command symbols",
           commandLogicSymbols.size());

    commandQuery.symbolType = sym_list::sym_packed_struct_var;
    commandQuery.prefix = "pix";
    commandQuery.documentText = content;
    const QList<sym_list::SymbolInfo> packedStructVars =
        CompletionService::getInstance()->findCommandCompletionSymbols(commandQuery);
    ++g_checks;
    const bool packedStructOk = packedStructVars.size() == 1
        && packedStructVars.first().symbolName == QStringLiteral("pixel")
        && packedStructVars.first().symbolType == sym_list::sym_packed_struct_var
        && packedStructVars.first().moduleScope == QStringLiteral("top");
    if (!packedStructOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           packedStructOk ? "PASS" : "FAIL",
           "CompletionService command struct",
           packedStructVars.size());

    commandQuery.moduleName.clear();
    ++g_checks;
    const bool structGlobalHidden =
        CompletionService::getInstance()->findCommandCompletionSymbols(commandQuery).isEmpty();
    if (!structGlobalHidden)
        ++g_fails;
    printf("[%s] %-34s\n",
           structGlobalHidden ? "PASS" : "FAIL",
           "CompletionService command struct hidden");

    QList<sym_list::SymbolInfo> snapshotSymbols;
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_top"),
                                      sym_list::sym_module,
                                      QString(),
                                      QString(),
                                      4000));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pixel"),
                                      sym_list::sym_packed_struct_var,
                                      QString(),
                                      QStringLiteral("global_pixel_t"),
                                      5001));
    snapshotSymbols.last().fileName = QStringLiteral("other_snapshot.sv");
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pixel"),
                                      sym_list::sym_packed_struct_var,
                                      QStringLiteral("snap_top"),
                                      QStringLiteral("snap_pixel_t"),
                                      5002));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pair"),
                                      sym_list::sym_unpacked_struct_var,
                                      QStringLiteral("snap_top"),
                                      QStringLiteral("snap_pair_t"),
                                      5003));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_enable"),
                                      sym_list::sym_logic,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5008));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_other_enable"),
                                      sym_list::sym_logic,
                                      QStringLiteral("other_top"),
                                      QString(),
                                      5009));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_task"),
                                      sym_list::sym_task,
                                      QString(),
                                      QString(),
                                      5010));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_state_t"),
                                      sym_list::sym_typedef,
                                      QString(),
                                      QStringLiteral("enum"),
                                      5011));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_local_state_t"),
                                      sym_list::sym_typedef,
                                      QStringLiteral("snap_top"),
                                      QStringLiteral("enum"),
                                      5012));
    snapshotSymbols.append(makeSymbol(QStringLiteral("red"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("other_t"),
                                      QString(),
                                      5004));
    snapshotSymbols.append(makeSymbol(QStringLiteral("red"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("snap_pixel_t"),
                                      QString(),
                                      5005));
    snapshotSymbols.append(makeSymbol(QStringLiteral("green"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("snap_pixel_t"),
                                      QString(),
                                      5006));
    snapshotSymbols.append(makeSymbol(QStringLiteral("blue"),
                                      sym_list::sym_struct_member,
                                      QStringLiteral("snap_pixel_t"),
                                      QString(),
                                      5007));
    SemanticIndex snapshotIndex;
    snapshotIndex.setSnapshot(std::make_shared<SemanticIndexSnapshot>(snapshotSymbols));
    CompletionService snapshotCompletionService(&snapshotIndex);
    expectEq("snapshot struct prefers module",
             snapshotCompletionService.getStructTypeForVariable("snap_pixel", "snap_top"),
             "snap_pixel_t");
    expectEq("snapshot struct fallback",
             snapshotCompletionService.getStructTypeForVariable("snap_pixel", "other_top"),
             "global_pixel_t");
    expectEq("snapshot unpacked struct var",
             snapshotCompletionService.getStructTypeForVariable("snap_pair", "snap_top"),
             "snap_pair_t");
    CompletionQuery snapshotModuleQuery;
    snapshotModuleQuery.prefix = QStringLiteral("snap");
    snapshotModuleQuery.moduleName = QStringLiteral("snap_top");
    expectList("snapshot module completions",
               snapshotCompletionService.findCompletions(snapshotModuleQuery),
               {"snap_enable"});
    const QList<sym_list::SymbolInfo> snapshotModuleSymbols =
        snapshotCompletionService.findCompletionSymbols(snapshotModuleQuery);
    ++g_checks;
    const bool snapshotModuleSymbolOk = snapshotModuleSymbols.size() == 1
        && snapshotModuleSymbols.first().symbolName == QStringLiteral("snap_enable")
        && snapshotModuleSymbols.first().symbolType == sym_list::sym_logic
        && snapshotModuleSymbols.first().moduleScope == QStringLiteral("snap_top");
    if (!snapshotModuleSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotModuleSymbolOk ? "PASS" : "FAIL",
           "snapshot module symbols",
           snapshotModuleSymbols.size());
    CommandCompletionQuery snapshotLogicCommandQuery;
    snapshotLogicCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotLogicCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotLogicCommandQuery.symbolType = sym_list::sym_logic;
    snapshotLogicCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command logic names",
               snapshotCompletionService.findCommandCompletions(snapshotLogicCommandQuery),
               {"snap_enable"});
    const QList<sym_list::SymbolInfo> snapshotLogicCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotLogicCommandQuery);
    ++g_checks;
    const bool snapshotLogicCommandOk = snapshotLogicCommandSymbols.size() == 1
        && snapshotLogicCommandSymbols.first().symbolName == QStringLiteral("snap_enable")
        && snapshotLogicCommandSymbols.first().symbolType == sym_list::sym_logic
        && snapshotLogicCommandSymbols.first().moduleScope == QStringLiteral("snap_top");
    if (!snapshotLogicCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotLogicCommandOk ? "PASS" : "FAIL",
           "snapshot command logic symbols",
           snapshotLogicCommandSymbols.size());
    CompletionQuery snapshotGlobalQuery;
    snapshotGlobalQuery.prefix = QStringLiteral("snap");
    expectList("snapshot global completions",
               snapshotCompletionService.findCompletions(snapshotGlobalQuery),
               {"snap_task", "snap_top"});
    CommandCompletionQuery snapshotTaskCommandQuery;
    snapshotTaskCommandQuery.symbolType = sym_list::sym_task;
    snapshotTaskCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command task names",
               snapshotCompletionService.findCommandCompletions(snapshotTaskCommandQuery),
               {"snap_task"});
    const QList<sym_list::SymbolInfo> snapshotTaskCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotTaskCommandQuery);
    ++g_checks;
    const bool snapshotTaskCommandOk = snapshotTaskCommandSymbols.size() == 1
        && snapshotTaskCommandSymbols.first().symbolName == QStringLiteral("snap_task")
        && snapshotTaskCommandSymbols.first().symbolType == sym_list::sym_task
        && snapshotTaskCommandSymbols.first().moduleScope.isEmpty();
    if (!snapshotTaskCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotTaskCommandOk ? "PASS" : "FAIL",
           "snapshot command task symbols",
           snapshotTaskCommandSymbols.size());
    CommandCompletionQuery snapshotEnumCommandQuery;
    snapshotEnumCommandQuery.symbolType = sym_list::sym_enum;
    snapshotEnumCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command enum typedef",
               snapshotCompletionService.findCommandCompletions(snapshotEnumCommandQuery),
               {"snap_state_t"});
    snapshotEnumCommandQuery.moduleName = QStringLiteral("snap_top");
    expectList("snapshot command local enum typedef",
               snapshotCompletionService.findCommandCompletions(snapshotEnumCommandQuery),
               {"snap_local_state_t"});
    CompletionQuery snapshotMemberQuery;
    snapshotMemberQuery.structTypeNameForMember = QStringLiteral("snap_pixel_t");
    expectList("snapshot struct members",
               snapshotCompletionService.findCompletions(snapshotMemberQuery),
               {"red", "green", "blue"});
    snapshotMemberQuery.prefix = QStringLiteral("bl");
    const QList<sym_list::SymbolInfo> snapshotMemberSymbols =
        snapshotCompletionService.findCompletionSymbols(snapshotMemberQuery);
    expectList("snapshot struct member prefix",
               snapshotCompletionService.findCompletions(snapshotMemberQuery),
               {"blue"});
    ++g_checks;
    const bool snapshotMemberSymbolOk = snapshotMemberSymbols.size() == 1
        && snapshotMemberSymbols.first().symbolName == QStringLiteral("blue")
        && snapshotMemberSymbols.first().symbolType == sym_list::sym_struct_member
        && snapshotMemberSymbols.first().moduleScope == QStringLiteral("snap_pixel_t");
    if (!snapshotMemberSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotMemberSymbolOk ? "PASS" : "FAIL",
           "snapshot struct member symbols",
           snapshotMemberSymbols.size());

    CommandCompletionQuery snapshotCommandQuery;
    snapshotCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotCommandQuery.documentText = QStringLiteral("module snap_top;\nendmodule\n");
    snapshotCommandQuery.symbolType = sym_list::sym_packed_struct_var;
    snapshotCommandQuery.prefix = QStringLiteral("snap");
    const QList<sym_list::SymbolInfo> snapshotCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotCommandQuery);
    ++g_checks;
    const bool snapshotCommandOk = snapshotCommandSymbols.size() == 1
        && snapshotCommandSymbols.first().symbolName == QStringLiteral("snap_pixel")
        && snapshotCommandSymbols.first().symbolType == sym_list::sym_packed_struct_var
        && snapshotCommandSymbols.first().moduleScope == QStringLiteral("snap_top")
        && snapshotCommandSymbols.first().dataType == QStringLiteral("snap_pixel_t");
    if (!snapshotCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotCommandOk ? "PASS" : "FAIL",
           "snapshot command struct symbols",
           snapshotCommandSymbols.size());

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
