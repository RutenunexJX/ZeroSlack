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

static QStringList symbolNames(const QList<sym_list::SymbolInfo>& symbols) {
    QStringList names;
    for (const auto& symbol : symbols)
        names << symbol.symbolName;
    return names;
}

static QStringList scoredNames(const QVector<QPair<QString, int>>& scored) {
    QStringList names;
    for (const auto& item : scored)
        names << item.first;
    return names;
}

static QStringList symbolNamesFromScored(
    const QVector<QPair<sym_list::SymbolInfo, int>>& scored) {
    QStringList names;
    for (const auto& item : scored)
        names << item.first.symbolName;
    return names;
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

    expectList("CompletionService keyword names",
               CompletionService::getInstance()->findKeywordCompletions(
                   QStringLiteral("always_f")),
               {"always_ff"});
    expectList("CompletionService keyword scores",
               scoredNames(
                   CompletionService::getInstance()->findScoredKeywordCompletions(
                       QStringLiteral("always_f"))),
               {"always_ff"});
    expectList("CompletionService keyword abbrev",
               CompletionService::getInstance()->findKeywordAbbreviationMatches(
                   {"always_ff", "logic"}, QStringLiteral("af")),
               {"always_ff"});
    ++g_checks;
    const QList<int> keywordPositions =
        CompletionService::getInstance()->findCompletionAbbreviationPositions(
            QStringLiteral("always_ff"), QStringLiteral("af"));
    const bool keywordPositionsOk = keywordPositions == QList<int>({0, 7});
    if (!keywordPositionsOk)
        ++g_fails;
    printf("[%s] %-34s got=[%s]\n",
           keywordPositionsOk ? "PASS" : "FAIL",
           "CompletionService abbrev pos",
           QStringList({
               QString::number(keywordPositions.value(0, -1)),
               QString::number(keywordPositions.value(1, -1))
           }).join(",").toLocal8Bit().constData());
    ++g_checks;
    const bool managerKeywordScoreOk =
        cm->calculateMatchScore(QStringLiteral("always_ff"),
                                QStringLiteral("af"))
        == CompletionService::getInstance()->calculateCompletionMatchScore(
            QStringLiteral("always_ff"), QStringLiteral("af"));
    if (!managerKeywordScoreOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           managerKeywordScoreOk ? "PASS" : "FAIL",
           "CompletionManager score delegation");
    expectList("CompletionManager keyword names",
               cm->getKeywordCompletions(QStringLiteral("always_f")),
               {"always_ff"});
    expectList("CompletionManager keyword scores",
               scoredNames(cm->getScoredKeywordMatches(
                   QStringLiteral("always_f"))),
               {"always_ff"});
    expectList("CompletionManager keyword abbrev",
               cm->getAbbreviationMatches({"always_ff", "logic"},
                                          QStringLiteral("af")),
               {"always_ff"});

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
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_child"),
                                      sym_list::sym_module,
                                      QString(),
                                      QString(),
                                      7000));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_if"),
                                      sym_list::sym_interface,
                                      QString(),
                                      QString(),
                                      4002));
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_pkg"),
                                      sym_list::sym_package,
                                      QString(),
                                      QString(),
                                      4004));
    snapshotSymbols.append(makeSymbol(QStringLiteral("SNAP_FEATURE"),
                                      sym_list::sym_def_define,
                                      QString(),
                                      QString(),
                                      4003));
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
    snapshotSymbols.append(makeSymbol(QStringLiteral("snap_state"),
                                      sym_list::sym_enum_var,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5013));
    snapshotSymbols.append(makeSymbol(QStringLiteral("SNAP_IDLE"),
                                      sym_list::sym_enum_value,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5014));
    snapshotSymbols.append(makeSymbol(QStringLiteral("SNAP_RUN"),
                                      sym_list::sym_enum_value,
                                      QStringLiteral("snap_top"),
                                      QString(),
                                      5015));
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
    const QString snapshotScopeFile = QStringLiteral("snapshot_scope.sv");
    const QString snapshotScopeContent =
        QStringLiteral("module snap_scope;\n"
                       "  logic snap_signal;\n"
                       "endmodule\n"
                       "module other_scope;\n"
                       "endmodule\n");
    sym_list::SymbolInfo snapshotScopeModule =
        makeSymbol(QStringLiteral("snap_scope"),
                   sym_list::sym_module,
                   QString(),
                   QString(),
                   6000);
    snapshotScopeModule.fileName = snapshotScopeFile;
    snapshotScopeModule.position = snapshotScopeContent.indexOf(QStringLiteral("module snap_scope"));
    snapshotScopeModule.startLine = 1;
    snapshotScopeModule.endLine = 3;
    snapshotSymbols.append(snapshotScopeModule);
    sym_list::SymbolInfo snapshotScopeSignal =
        makeSymbol(QStringLiteral("snap_signal"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_scope"),
                   QString(),
                   6001);
    snapshotScopeSignal.fileName = snapshotScopeFile;
    snapshotScopeSignal.position = snapshotScopeContent.indexOf(QStringLiteral("snap_signal"));
    snapshotScopeSignal.startLine = 2;
    snapshotScopeSignal.endLine = 2;
    snapshotSymbols.append(snapshotScopeSignal);
    sym_list::SymbolInfo snapshotOtherModule =
        makeSymbol(QStringLiteral("other_scope"),
                   sym_list::sym_module,
                   QString(),
                   QString(),
                   6002);
    snapshotOtherModule.fileName = snapshotScopeFile;
    snapshotOtherModule.position = snapshotScopeContent.indexOf(QStringLiteral("module other_scope"));
    snapshotOtherModule.startLine = 4;
    snapshotOtherModule.endLine = 5;
    snapshotSymbols.append(snapshotOtherModule);
    sym_list::SymbolInfo snapshotClock =
        makeSymbol(QStringLiteral("snap_clk"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_top"),
                   QString(),
                   6003);
    snapshotClock.fileName = snapshotScopeFile;
    snapshotSymbols.append(snapshotClock);
    sym_list::SymbolInfo snapshotReset =
        makeSymbol(QStringLiteral("snap_rst_n"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_top"),
                   QString(),
                   6004);
    snapshotReset.fileName = snapshotScopeFile;
    snapshotSymbols.append(snapshotReset);
    QList<SemanticRelationship> snapshotRelationships;
    SemanticRelationship containsSnapEnable;
    containsSnapEnable.fromId = 4000;
    containsSnapEnable.toId = 5008;
    containsSnapEnable.type = SymbolRelationshipEngine::CONTAINS;
    snapshotRelationships.append(containsSnapEnable);
    SemanticRelationship otherReferencesEnable;
    otherReferencesEnable.fromId = 5009;
    otherReferencesEnable.toId = 5008;
    otherReferencesEnable.type = SymbolRelationshipEngine::REFERENCES;
    snapshotRelationships.append(otherReferencesEnable);
    SemanticRelationship clockDrivesTop;
    clockDrivesTop.fromId = 6003;
    clockDrivesTop.toId = 4000;
    clockDrivesTop.type = SymbolRelationshipEngine::CLOCKS;
    snapshotRelationships.append(clockDrivesTop);
    SemanticRelationship resetDrivesTop;
    resetDrivesTop.fromId = 6004;
    resetDrivesTop.toId = 4000;
    resetDrivesTop.type = SymbolRelationshipEngine::RESETS;
    snapshotRelationships.append(resetDrivesTop);
    QHash<QString, QString> snapshotFileContents;
    snapshotFileContents.insert(snapshotScopeFile, snapshotScopeContent);
    SemanticIndex snapshotIndex;
    snapshotIndex.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            snapshotSymbols,
            snapshotRelationships,
            QList<SemanticDiagnostic>{},
            snapshotFileContents));
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
    snapshotModuleQuery.prefix = QStringLiteral("snap_e");
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
    snapshotLogicCommandQuery.prefix = QStringLiteral("snap_e");
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
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
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
    CommandCompletionQuery snapshotModuleCommandQuery;
    snapshotModuleCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotModuleCommandQuery.symbolType = sym_list::sym_module;
    snapshotModuleCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command module in scope",
               snapshotCompletionService.findCommandCompletions(snapshotModuleCommandQuery),
               {"snap_child", "snap_scope", "snap_top"});
    const QList<sym_list::SymbolInfo> snapshotModuleCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotModuleCommandQuery);
    ++g_checks;
    const bool snapshotModuleCommandOk = snapshotModuleCommandSymbols.size() == 3
        && snapshotModuleCommandSymbols.first().symbolId == 7000
        && snapshotModuleCommandSymbols.at(1).symbolId == 6000
        && snapshotModuleCommandSymbols.last().symbolId == 4000;
    if (!snapshotModuleCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotModuleCommandOk ? "PASS" : "FAIL",
           "snapshot command module symbols",
           snapshotModuleCommandSymbols.size());
    CommandCompletionQuery snapshotInterfaceCommandQuery;
    snapshotInterfaceCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotInterfaceCommandQuery.symbolType = sym_list::sym_interface;
    snapshotInterfaceCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command interface in scope",
               snapshotCompletionService.findCommandCompletions(snapshotInterfaceCommandQuery),
               {"snap_if"});
    const QList<sym_list::SymbolInfo> snapshotInterfaceCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotInterfaceCommandQuery);
    ++g_checks;
    const bool snapshotInterfaceCommandOk = snapshotInterfaceCommandSymbols.size() == 1
        && snapshotInterfaceCommandSymbols.first().symbolId == 4002;
    if (!snapshotInterfaceCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotInterfaceCommandOk ? "PASS" : "FAIL",
           "snapshot command interface symbols",
           snapshotInterfaceCommandSymbols.size());
    CommandCompletionQuery snapshotPackageCommandQuery;
    snapshotPackageCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotPackageCommandQuery.symbolType = sym_list::sym_package;
    snapshotPackageCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command package in scope",
               snapshotCompletionService.findCommandCompletions(snapshotPackageCommandQuery),
               {"snap_pkg"});
    const QList<sym_list::SymbolInfo> snapshotPackageCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotPackageCommandQuery);
    ++g_checks;
    const bool snapshotPackageCommandOk = snapshotPackageCommandSymbols.size() == 1
        && snapshotPackageCommandSymbols.first().symbolId == 4004;
    if (!snapshotPackageCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotPackageCommandOk ? "PASS" : "FAIL",
           "snapshot command package symbols",
           snapshotPackageCommandSymbols.size());
    CommandCompletionQuery snapshotDefineCommandQuery;
    snapshotDefineCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotDefineCommandQuery.symbolType = sym_list::sym_def_define;
    snapshotDefineCommandQuery.prefix = QStringLiteral("SNAP");
    expectList("snapshot command define in scope",
               snapshotCompletionService.findCommandCompletions(snapshotDefineCommandQuery),
               {"SNAP_FEATURE"});
    const QList<sym_list::SymbolInfo> snapshotDefineCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbols(snapshotDefineCommandQuery);
    ++g_checks;
    const bool snapshotDefineCommandOk = snapshotDefineCommandSymbols.size() == 1
        && snapshotDefineCommandSymbols.first().symbolId == 4003;
    if (!snapshotDefineCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotDefineCommandOk ? "PASS" : "FAIL",
           "snapshot command define symbols",
           snapshotDefineCommandSymbols.size());
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
    const int snapshotScopeCursor =
        snapshotScopeContent.indexOf(QStringLiteral("snap_signal")) + 2;
    expectEq("snapshot current module",
             snapshotCompletionService.currentModuleAt(snapshotScopeFile, snapshotScopeCursor),
             "snap_scope");
    CompletionQuery snapshotScopeQuery;
    snapshotScopeQuery.prefix = QStringLiteral("snap");
    snapshotScopeQuery.fileName = snapshotScopeFile;
    snapshotScopeQuery.cursorLine = 2;
    expectList("snapshot scope completions",
               snapshotCompletionService.findScopeCompletions(snapshotScopeQuery),
               {"snap_scope", "snap_signal"});
    SemanticQueryContext snapshotFacadeQuery;
    snapshotFacadeQuery.prefix = QStringLiteral("snap");
    snapshotFacadeQuery.fileName = snapshotScopeFile;
    snapshotFacadeQuery.cursorLine = 2;
    expectList("SemanticIndex snapshot completions",
               snapshotIndex.findCompletions(snapshotFacadeQuery),
               {"snap_scope", "snap_signal"});
    expectList("snapshot child completions",
               snapshotCompletionService.findModuleChildCompletions(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap")),
               {"snap_enable"});
    expectList("snapshot related completions",
               snapshotCompletionService.findRelatedSymbolCompletions(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("snapshot reference completions",
               snapshotCompletionService.findSymbolReferenceCompletions(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_other")),
               {"snap_other_enable"});
    ++g_checks;
    const bool snapshotRelationshipsAvailable =
        snapshotCompletionService.relationshipCompletionsAvailable();
    if (!snapshotRelationshipsAvailable)
        ++g_fails;
    printf("[%s] %-34s\n",
           snapshotRelationshipsAvailable ? "PASS" : "FAIL",
           "snapshot relationship availability");
    expectList("snapshot clock completions",
               snapshotCompletionService.findClockDomainCompletions(QStringLiteral("snap_c")),
               {"snap_clk"});
    expectList("snapshot reset completions",
               snapshotCompletionService.findResetSignalCompletions(QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("snapshot module internal variables",
               snapshotCompletionService.findModuleInternalVariableCompletions(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap_")),
               {"snap_clk", "snap_enable", "snap_rst_n"});
    expectList("snapshot module symbols by type",
               snapshotCompletionService.findModuleSymbolsByType(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_e")),
               {"snap_enable"});
    expectList("snapshot global symbol names",
               snapshotCompletionService.findGlobalSymbolCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    expectList("snapshot global symbols by type",
               snapshotCompletionService.findGlobalSymbolsByType(
                   sym_list::sym_task,
                   QStringLiteral("snap")),
               {"snap_task"});
    expectList("snapshot global struct variables are not type completions",
               snapshotCompletionService.findGlobalSymbolsByType(
                   sym_list::sym_packed_struct_var,
                   QStringLiteral("snap")),
               {});
    expectList("snapshot scoped variables by type",
               snapshotCompletionService.findVariableCompletionsInScope(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("snapshot task/function completions",
               snapshotCompletionService.findTaskFunctionCompletions(QStringLiteral("snap")),
               {"snap_task"});
    expectList("snapshot instantiable modules",
               snapshotCompletionService.findInstantiableModuleCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_scope", "snap_top"});
    expectList("snapshot struct member service",
               snapshotCompletionService.findStructMemberCompletions(
                   QStringLiteral("bl"),
                   QStringLiteral("snap_pixel_t")),
               {"blue"});
    expectList("snapshot enum value service",
               snapshotCompletionService.findEnumValueCompletions(
                   QStringLiteral("SNAP_"),
                   QStringLiteral("snap_top")),
               {"SNAP_IDLE", "SNAP_RUN"});
    expectEq("snapshot enum variable type",
             snapshotCompletionService.findEnumTypeForVariable(
                 QStringLiteral("snap_state"),
                 QStringLiteral("snap_top")),
             "snap_top");
    ContextCompletionQuery snapshotStructContextQuery;
    snapshotStructContextQuery.prefix = QStringLiteral("bl");
    snapshotStructContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotStructContextQuery.context = QStringLiteral("snap_pixel.");
    expectList("snapshot context struct members",
               snapshotCompletionService.findContextAwareCompletions(snapshotStructContextQuery),
               {"blue"});
    ContextCompletionQuery snapshotEnumContextQuery;
    snapshotEnumContextQuery.prefix = QStringLiteral("SNAP_");
    snapshotEnumContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotEnumContextQuery.context = QStringLiteral("case(snap_state)");
    expectList("snapshot context enum values",
               snapshotCompletionService.findContextAwareCompletions(snapshotEnumContextQuery),
               {"SNAP_IDLE", "SNAP_RUN", "snap_enable"});
    ContextCompletionQuery snapshotGeneralContextQuery;
    snapshotGeneralContextQuery.prefix = QStringLiteral("snap_e");
    snapshotGeneralContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotGeneralContextQuery.context = QStringLiteral("general");
    expectList("snapshot context general",
               snapshotCompletionService.findContextAwareCompletions(snapshotGeneralContextQuery),
               {"snap_enable", "snap_scope", "snap_state_t"});
    expectList("snapshot all-symbol completions",
               snapshotCompletionService.findAllSymbolCompletions(QStringLiteral("snap_clk")),
               {"snap_clk"});
    expectList("snapshot scored all-symbol completions",
               scoredNames(snapshotCompletionService.findScoredAllSymbolCompletions(
                   QStringLiteral("snap_clk"))),
               {"snap_clk"});
    expectList("snapshot typed symbol completions",
               snapshotCompletionService.findSymbolCompletionsByType(
                   sym_list::sym_logic,
                   QStringLiteral("snap_e")),
               {"snap_enable", "snap_other_enable"});
    expectList("snapshot scored typed symbol completions",
               symbolNamesFromScored(snapshotCompletionService.findScoredSymbolCompletionsByType(
                   sym_list::sym_logic,
                   QStringLiteral("snap_e"))),
               {"snap_enable", "snap_other_enable"});
    expectList("snapshot smart completions no relationships",
               scoredNames(snapshotCompletionService.findSmartCompletions(
                   QStringLiteral("snap_clk"),
                   QString(),
                   -1,
                   false)),
               {"snap_clk"});
    expectList("snapshot smart completions in scope",
               scoredNames(snapshotCompletionService.findSmartCompletions(
                   QStringLiteral("snap_sig"),
                   snapshotScopeFile,
                   snapshotScopeCursor,
                   true)),
               {"snap_signal"});
    expectList("snapshot module info symbols by type",
               symbolNames(snapshotCompletionService.findModuleInternalSymbolInfosByType(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_"))),
               {"snap_clk", "snap_enable", "snap_rst_n"});
    expectList("snapshot module context info symbols",
               symbolNames(snapshotCompletionService.findModuleContextSymbolInfosByType(
                   QStringLiteral("snap_scope"),
                   snapshotScopeFile,
                   sym_list::sym_logic,
                   QStringLiteral("snap"))),
               {"snap_signal"});
    expectList("snapshot global info symbols by type",
               symbolNames(snapshotCompletionService.findGlobalSymbolInfosByType(
                   sym_list::sym_packed_struct_var,
                   QStringLiteral("snap"))),
               {"snap_pixel"});
    SemanticIndex::getInstance()->setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            snapshotSymbols,
            snapshotRelationships,
            QList<SemanticDiagnostic>{},
            snapshotFileContents));
    expectList("CompletionManager child delegation",
               cm->getModuleChildrenCompletions(QStringLiteral("snap_top"),
                                                QStringLiteral("snap")),
               {"snap_enable"});
    expectList("CompletionManager related delegation",
               cm->getRelatedSymbolCompletions(QStringLiteral("snap_enable"),
                                               QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("CompletionManager reference delegation",
               cm->getSymbolReferencesCompletions(QStringLiteral("snap_enable"),
                                                  QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("CompletionManager clock delegation",
               cm->getClockDomainCompletions(QStringLiteral("snap_c")),
               {"snap_clk"});
    expectList("CompletionManager reset delegation",
               cm->getResetSignalCompletions(QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("CompletionManager module variable delegation",
               cm->getModuleInternalVariables(QStringLiteral("snap_top"),
                                              QStringLiteral("snap_e")),
               {"snap_enable"});
    expectList("CompletionManager global delegation",
               cm->getGlobalSymbolCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    expectList("CompletionManager type delegation",
               cm->getGlobalSymbolsByType(sym_list::sym_task, QStringLiteral("snap")),
               {"snap_task"});
    expectList("CompletionManager task/function delegation",
               cm->getTaskFunctionCompletions(QStringLiteral("snap")),
               {"snap_task"});
    expectList("CompletionManager module delegation",
               cm->getInstantiableModules(QStringLiteral("snap")),
               {"snap_child", "snap_scope", "snap_top"});
    expectList("CompletionManager context struct delegation",
               cm->getContextAwareCompletions(QStringLiteral("bl"),
                                              QStringLiteral("snap_top"),
                                              QStringLiteral("snap_pixel.")),
               {"blue"});
    expectList("CompletionManager context enum delegation",
               cm->getContextAwareCompletions(QStringLiteral("SNAP_"),
                                              QStringLiteral("snap_top"),
                                              QStringLiteral("case(snap_state)")),
               {"SNAP_IDLE", "SNAP_RUN", "snap_enable"});
    expectList("CompletionManager struct member delegation",
               cm->getStructMemberCompletions(QStringLiteral("bl"),
                                              QStringLiteral("snap_pixel_t")),
               {"blue"});
    expectList("CompletionManager all-symbol delegation",
               cm->getAllSymbolCompletions(QStringLiteral("snap_clk")),
               {"snap_clk"});
    expectList("CompletionManager scored all-symbol delegation",
               scoredNames(cm->getScoredAllSymbolMatches(QStringLiteral("snap_clk"))),
               {"snap_clk"});
    expectList("CompletionManager typed symbol delegation",
               cm->getSymbolCompletions(sym_list::sym_logic, QStringLiteral("snap_e")),
               {"snap_enable", "snap_other_enable"});
    expectList("CompletionManager scored typed symbol delegation",
               symbolNamesFromScored(cm->getScoredSymbolMatches(
                   sym_list::sym_logic,
                   QStringLiteral("snap_e"))),
               {"snap_enable", "snap_other_enable"});
    expectList("CompletionManager smart delegation",
               scoredNames(cm->getSmartCompletions(QStringLiteral("snap_sig"),
                                                   snapshotScopeFile,
                                                   snapshotScopeCursor)),
               {"snap_signal"});
    expectList("CompletionManager module info delegation",
               symbolNames(cm->getModuleInternalSymbolsByType(
                   QStringLiteral("snap_top"),
                   sym_list::sym_logic,
                   QStringLiteral("snap_"))),
               {"snap_clk", "snap_enable", "snap_rst_n"});
    expectList("CompletionManager context info delegation",
               symbolNames(cm->getModuleContextSymbolsByType(
                   QStringLiteral("snap_scope"),
                   snapshotScopeFile,
                   sym_list::sym_logic,
                   QStringLiteral("snap"))),
               {"snap_signal"});
    expectList("CompletionManager global info delegation",
               symbolNames(cm->getGlobalSymbolsByType_Info(
                   sym_list::sym_packed_struct_var,
                   QStringLiteral("snap"))),
               {"snap_pixel"});
    SemanticIndex::getInstance()->clearSnapshot();

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
