// Headless relationship regression test. Verifies that relationship analysis in a multi-module
// file attaches line-derived relationships to the containing module, not always the first module.
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include "diagnosticservice.h"
#include "hierarchyservice.h"
#include "referenceservice.h"
#include "relationshipservice.h"
#include "searchservice.h"
#include "symbolrelationshipengine.h"
#include "semanticindexsnapshot.h"
#include "syminfo.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <cstdio>
#include <memory>

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

static int symbolIdInFile(const QList<sym_list::SymbolInfo>& symbols,
                          const QString& name,
                          sym_list::sym_type_e type,
                          const QString& fileName)
{
    for (const auto& s : symbols) {
        if (s.symbolName == name && s.symbolType == type && s.fileName == fileName)
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

static void applyRelationships(SymbolRelationshipEngine& engine,
                               const QVector<RelationshipToAdd>& rels)
{
    engine.beginUpdate();
    for (const auto& r : rels)
        engine.addRelationship(r.fromId, r.toId, r.type, r.context, r.confidence);
    engine.endUpdate();
}

static void expectInt(const char* what, int got, int want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-46s got=%d want=%d\n", ok ? "PASS" : "FAIL", what, got, want);
}

static QString loadTextFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

static QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

static void runInlineRelationshipRegression(SlangManager& slang,
                                            sym_list* db,
                                            SmartRelationshipBuilder& builder)
{
    printf("\n-- inline multi-module regression --\n");

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
}

static void runMultiFileRelationshipFixture(SlangManager& slang,
                                            sym_list* db,
                                            SymbolRelationshipEngine& engine,
                                            SmartRelationshipBuilder& builder)
{
    printf("\n-- multi-file relationship fixture --\n");

    const QDir fixtureDir(QFileInfo(QString::fromLocal8Bit(__FILE__)).dir()
                              .filePath(QStringLiteral("relationship_fixture")));
    const QString pkgPath = normalizedPath(fixtureDir.filePath(QStringLiteral("relationship_pkg.sv")));
    const QString stagePath = normalizedPath(fixtureDir.filePath(QStringLiteral("relationship_stage.sv")));
    const QString topPath = normalizedPath(fixtureDir.filePath(QStringLiteral("relationship_top.sv")));

    const QHash<QString, QString> contents = {
        {pkgPath, loadTextFile(pkgPath)},
        {stagePath, loadTextFile(stagePath)},
        {topPath, loadTextFile(topPath)},
    };
    expectBool("fixture package loaded", !contents.value(pkgPath).isEmpty(), true);
    expectBool("fixture stage loaded", !contents.value(stagePath).isEmpty(), true);
    expectBool("fixture top loaded", !contents.value(topPath).isEmpty(), true);

    const QStringList paths = {pkgPath, stagePath, topPath};
    const QList<sym_list::SymbolInfo> workspaceSymbols = slang.extractWorkspaceSymbols(paths);
    expectBool("workspace symbols extracted", !workspaceSymbols.isEmpty(), true);

    QHash<QString, QList<sym_list::SymbolInfo>> symbolsByFile;
    for (auto s : workspaceSymbols) {
        s.fileName = normalizedPath(s.fileName);
        symbolsByFile[s.fileName].append(s);
    }

    for (const QString& path : paths)
        db->setSymbolsForFile(path, symbolsByFile.value(path), contents.value(path));

    const QList<sym_list::SymbolInfo> allSymbols = db->getAllSymbols();
    const QList<sym_list::SymbolInfo> topSymbols = db->findSymbolsByFileName(topPath);

    const int packageId = symbolIdInFile(allSymbols, QStringLiteral("rel_pkg"),
                                         sym_list::sym_package, pkgPath);
    const int stateTypeId = symbolIdInFile(allSymbols, QStringLiteral("state_t"),
                                           sym_list::sym_typedef, pkgPath);
    const int stateVarId = symbolIdInFile(allSymbols, QStringLiteral("state"),
                                          sym_list::sym_enum_var, stagePath);
    const int topId = symbolIdInFile(allSymbols, QStringLiteral("rel_top"),
                                     sym_list::sym_module, topPath);
    const int stageId = symbolIdInFile(allSymbols, QStringLiteral("rel_stage"),
                                       sym_list::sym_module, stagePath);
    const int captureId = symbolIdInFile(allSymbols, QStringLiteral("capture_sample"),
                                         sym_list::sym_task, topPath);
    const int reqValidId = symbolIdInScope(topSymbols, QStringLiteral("req_valid"),
                                           sym_list::sym_port_input, QStringLiteral("rel_top"));
    const int stageDataId = symbolIdInScope(topSymbols, QStringLiteral("stage_data"),
                                            sym_list::sym_logic, QStringLiteral("rel_top"));
    const int rspDataId = symbolIdInScope(topSymbols, QStringLiteral("rsp_data"),
                                          sym_list::sym_port_output, QStringLiteral("rel_top"));
    const int topClkId = symbolIdInScope(topSymbols, QStringLiteral("top_clk"),
                                         sym_list::sym_port_input, QStringLiteral("rel_top"));
    const int topRstId = symbolIdInScope(topSymbols, QStringLiteral("top_rst_n"),
                                         sym_list::sym_port_input, QStringLiteral("rel_top"));

    expectBool("package symbol extracted", packageId > 0, true);
    expectBool("package typedef extracted", stateTypeId > 0, true);
    expectBool("imported enum var extracted", stateVarId > 0, true);
    expectBool("top module extracted", topId > 0, true);
    expectBool("cross-file stage module extracted", stageId > 0, true);
    expectBool("top task extracted", captureId > 0, true);
    expectBool("top req_valid extracted", reqValidId > 0, true);
    expectBool("top stage_data extracted", stageDataId > 0, true);
    expectBool("top rsp_data extracted", rspDataId > 0, true);
    expectBool("top clock extracted", topClkId > 0, true);
    expectBool("top reset extracted", topRstId > 0, true);

    SemanticIndex index(db);
    SemanticQueryContext queryContext;
    queryContext.fileName = topPath;
    queryContext.moduleName = QStringLiteral("rel_top");
    const QList<sym_list::SymbolInfo> facadeStageDefs =
        index.findDefinitions(QStringLiteral("rel_stage"), queryContext);
    expectBool("semantic facade returns top symbols",
               index.getSymbols(topPath).size() == topSymbols.size(), true);
    expectBool("semantic facade finds cross-file module",
               !facadeStageDefs.isEmpty() && facadeStageDefs.first().symbolId == stageId, true);
    expectBool("semantic facade gets symbol by id",
               index.getSymbolById(topId).symbolName == QStringLiteral("rel_top"), true);
    expectInt("semantic facade finds symbol id",
              index.findSymbolId(QStringLiteral("rel_stage"), queryContext), stageId);
    expectInt("semantic facade returns missing symbol id",
              index.findSymbolId(QStringLiteral("missing_symbol"), queryContext), -1);
    expectBool("semantic facade returns cached file content",
               index.getCachedFileContent(topPath) == contents.value(topPath), true);
    const QStringList scopeNames = index.getScopeSymbolNames(topPath, 20);
    expectBool("semantic facade returns scope symbols",
               scopeNames.contains(QStringLiteral("stage_data")), true);

    DiagnosticService diagnosticService(&index);
    DiagnosticQuery diagnosticQuery;
    diagnosticQuery.fileName = topPath;
    expectBool("diagnostic service has no fixture diagnostics",
               diagnosticService.findDiagnostics(diagnosticQuery).isEmpty(), true);
    expectBool("diagnostic service reports no diagnostics",
               diagnosticService.hasDiagnostics(diagnosticQuery), false);

    const QString brokenPath = normalizedPath(fixtureDir.filePath(QStringLiteral("broken_diag.sv")));
    const QString brokenContent = QStringLiteral(
        "module broken_diag(input logic clk);\n"
        "  logic bad;\n"
        "  assign bad = ;\n"
        "endmodule\n");
    const QList<SemanticDiagnostic> brokenDiagnostics =
        slang.extractDiagnostics(brokenPath, brokenContent);
    SemanticIndex diagnosticIndex(db);
    diagnosticIndex.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(db, brokenDiagnostics)));
    DiagnosticService brokenDiagnosticService(&diagnosticIndex);
    DiagnosticQuery brokenDiagnosticQuery;
    brokenDiagnosticQuery.fileName = brokenPath;
    const QList<DiagnosticResult> brokenDiagnosticResults =
        brokenDiagnosticService.findDiagnostics(brokenDiagnosticQuery);
    expectBool("slang diagnostics flow into service",
               !brokenDiagnosticResults.isEmpty(), true);
    if (!brokenDiagnosticResults.isEmpty()) {
        expectBool("slang diagnostic has file",
                   brokenDiagnosticResults.first().diagnostic.fileName == brokenPath, true);
        expectBool("slang diagnostic has message",
                   !brokenDiagnosticResults.first().diagnostic.message.isEmpty(), true);
        expectBool("slang diagnostic has severity",
                   brokenDiagnosticResults.first().diagnostic.severity == SemanticDiagnostic::Error,
                   true);
    }

    SemanticDiagnostic infoDiagnostic;
    infoDiagnostic.fileName = topPath;
    infoDiagnostic.line = 9;
    infoDiagnostic.column = 3;
    infoDiagnostic.message = QStringLiteral("info message");
    infoDiagnostic.severity = SemanticDiagnostic::Info;

    SemanticDiagnostic warningDiagnostic;
    warningDiagnostic.fileName = topPath;
    warningDiagnostic.line = 2;
    warningDiagnostic.column = 1;
    warningDiagnostic.message = QStringLiteral("warning message");
    warningDiagnostic.severity = SemanticDiagnostic::Warning;

    SemanticDiagnostic errorDiagnostic;
    errorDiagnostic.fileName = stagePath;
    errorDiagnostic.line = 4;
    errorDiagnostic.column = 7;
    errorDiagnostic.message = QStringLiteral("error message");
    errorDiagnostic.severity = SemanticDiagnostic::Error;

    SemanticIndex diagnosticReportIndex(db);
    diagnosticReportIndex.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(db, {
            infoDiagnostic,
            warningDiagnostic,
            errorDiagnostic,
        })));
    DiagnosticService diagnosticReportService(&diagnosticReportIndex);
    const DiagnosticReport diagnosticReport =
        diagnosticReportService.findDiagnosticReport();
    expectInt("diagnostic report total count",
              diagnosticReport.totalCount, 3);
    expectInt("diagnostic report file count",
              diagnosticReport.fileCounts.value(topPath), 2);
    expectInt("diagnostic report file group count",
              diagnosticReport.fileGroups.size(), 2);
    bool diagnosticReportFoundTopGroup = false;
    for (const DiagnosticFileGroup& group : diagnosticReport.fileGroups) {
        if (group.fileKey == topPath) {
            diagnosticReportFoundTopGroup =
                group.count == 2
                && group.diagnostics.size() == 2
                && !group.displayName.isEmpty();
        }
    }
    expectBool("diagnostic report groups file diagnostics",
               diagnosticReportFoundTopGroup, true);
    expectInt("diagnostic report severity count",
              diagnosticReport.severityCounts.value(SemanticDiagnostic::Error), 1);
    expectBool("diagnostic report sorts errors first",
               !diagnosticReport.diagnostics.isEmpty()
                   && diagnosticReport.diagnostics.first().diagnostic.severity
                       == SemanticDiagnostic::Error,
               true);

    DiagnosticQuery errorOnlyDiagnosticQuery;
    errorOnlyDiagnosticQuery.includeInfo = false;
    errorOnlyDiagnosticQuery.includeWarnings = false;
    errorOnlyDiagnosticQuery.includeErrors = true;
    expectInt("diagnostic report filters severity",
              diagnosticReportService.findDiagnosticReport(errorOnlyDiagnosticQuery).totalCount,
              1);
    DiagnosticQuery workspaceOnlyDiagnosticQuery;
    workspaceOnlyDiagnosticQuery.workspaceFilesOnly = true;
    workspaceOnlyDiagnosticQuery.workspaceFiles = {topPath};
    expectInt("diagnostic report filters workspace files",
              diagnosticReportService.findDiagnosticReport(workspaceOnlyDiagnosticQuery).totalCount,
              2);
    workspaceOnlyDiagnosticQuery.workspaceFiles = {stagePath};
    expectInt("diagnostic report keeps workspace error file",
              diagnosticReportService.findDiagnosticReport(workspaceOnlyDiagnosticQuery).totalCount,
              1);

    SearchService searchService(&index);
    SearchQuery moduleSearchQuery;
    moduleSearchQuery.text = QStringLiteral("rel_");
    moduleSearchQuery.types = {sym_list::sym_module};
    const QList<SearchResult> moduleSearchResults =
        searchService.findSymbols(moduleSearchQuery);
    bool searchFoundTop = false;
    bool searchFoundStage = false;
    for (const SearchResult& result : moduleSearchResults) {
        searchFoundTop = searchFoundTop || result.symbol.symbolId == topId;
        searchFoundStage = searchFoundStage || result.symbol.symbolId == stageId;
    }
    expectBool("search service finds top module",
               searchFoundTop, true);
    expectBool("search service finds stage module",
               searchFoundStage, true);

    SearchQuery fileModuleSearchQuery = moduleSearchQuery;
    fileModuleSearchQuery.fileName = topPath;
    const QList<SearchResult> fileModuleSearchResults =
        searchService.findSymbols(fileModuleSearchQuery);
    bool fileSearchFoundTop = false;
    bool fileSearchFoundStage = false;
    for (const SearchResult& result : fileModuleSearchResults) {
        fileSearchFoundTop = fileSearchFoundTop || result.symbol.symbolId == topId;
        fileSearchFoundStage = fileSearchFoundStage || result.symbol.symbolId == stageId;
    }
    expectBool("search service filters file module",
               fileSearchFoundTop, true);
    expectBool("search service excludes other file module",
               fileSearchFoundStage, false);

    SearchQuery exactTaskSearchQuery;
    exactTaskSearchQuery.text = QStringLiteral("capture_sample");
    exactTaskSearchQuery.types = {sym_list::sym_task};
    exactTaskSearchQuery.exactMatch = true;
    exactTaskSearchQuery.maxResults = 1;
    const QList<SearchResult> exactTaskResults =
        searchService.findSymbols(exactTaskSearchQuery);
    expectBool("search service exact task result",
               exactTaskResults.size() == 1
                   && exactTaskResults.first().symbol.symbolId == captureId,
               true);

    QVector<RelationshipToAdd> rels = builder.computeRelationships(topPath,
                                                                    contents.value(topPath),
                                                                    topSymbols);

    expectBool("top instantiates cross-file stage",
               hasRel(rels, topId, stageId, SymbolRelationshipEngine::INSTANTIATES), true);
    expectBool("top calls local task",
               hasRel(rels, topId, captureId, SymbolRelationshipEngine::CALLS), true);
    expectBool("top reads request condition",
               hasRel(rels, topId, reqValidId, SymbolRelationshipEngine::READS_FROM), true);
    expectBool("stage data assigns response",
               hasRel(rels, stageDataId, rspDataId, SymbolRelationshipEngine::ASSIGNS_TO), true);
    expectBool("top clocked by top_clk",
               hasRel(rels, topClkId, topId, SymbolRelationshipEngine::CLOCKS), true);
    expectBool("top reset by top_rst_n",
               hasRel(rels, topRstId, topId, SymbolRelationshipEngine::RESETS), true);

    const auto symbolOnlySnapshot = std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(db));
    SmartRelationshipBuilder snapshotBuilder(&engine, nullptr, &slang);
    const QVector<RelationshipToAdd> snapshotBackedRels =
        snapshotBuilder.computeRelationships(topPath,
                                             contents.value(topPath),
                                             symbolOnlySnapshot->getSymbols(topPath),
                                             symbolOnlySnapshot.get());
    expectBool("snapshot builder resolves cross-file stage",
               hasRel(snapshotBackedRels, topId, stageId, SymbolRelationshipEngine::INSTANTIATES),
               true);
    expectBool("snapshot builder resolves local task",
               hasRel(snapshotBackedRels, topId, captureId, SymbolRelationshipEngine::CALLS),
               true);

    applyRelationships(engine, rels);
    engine.addRelationship(topId, stageId, SymbolRelationshipEngine::REFERENCES,
                           QStringLiteral("direction cache probe"));
    engine.addRelationship(reqValidId, topId, SymbolRelationshipEngine::REFERENCES,
                           QStringLiteral("direction cache probe"));
    const QList<int> outgoingReferenceIds =
        engine.getRelatedSymbols(topId, SymbolRelationshipEngine::REFERENCES, true);
    const QList<int> incomingReferenceIds =
        engine.getRelatedSymbols(topId, SymbolRelationshipEngine::REFERENCES, false);
    expectBool("relationship cache keeps outgoing direction",
               outgoingReferenceIds.contains(stageId), true);
    expectBool("relationship cache keeps incoming direction",
               incomingReferenceIds.contains(reqValidId), true);
    expectBool("relationship cache separates incoming direction",
               incomingReferenceIds.contains(stageId), false);

    SemanticIndex snapshotIndex(db);
    const auto snapshot = std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(db));
    snapshotIndex.setSnapshot(snapshot);
    expectBool("semantic snapshot returns top symbols",
               snapshotIndex.getSymbols(topPath).size() == topSymbols.size(), true);
    expectInt("semantic snapshot finds symbol id",
              snapshotIndex.findSymbolId(QStringLiteral("rel_stage"), queryContext), stageId);
    expectBool("semantic snapshot returns cached file content",
               snapshotIndex.getCachedFileContent(topPath) == contents.value(topPath), true);
    expectBool("semantic snapshot returns scope symbols",
               snapshotIndex.getScopeSymbolNames(topPath, 20).contains(QStringLiteral("stage_data")),
               true);
    const QList<SemanticRelationship> snapshotTopRelationships =
        snapshotIndex.getRelationships(topId, true);
    bool snapshotFoundStage = false;
    for (const SemanticRelationship& relationship : snapshotTopRelationships) {
        snapshotFoundStage = snapshotFoundStage
            || (relationship.toId == stageId
                && relationship.type == SymbolRelationshipEngine::INSTANTIATES);
    }
    expectBool("semantic snapshot captures relationships",
               snapshotFoundStage, true);
    SemanticRelationship duplicateStageRelationship;
    duplicateStageRelationship.fromId = topId;
    duplicateStageRelationship.toId = stageId;
    duplicateStageRelationship.type = SymbolRelationshipEngine::INSTANTIATES;
    SemanticRelationship newTaskRelationship;
    newTaskRelationship.fromId = stageId;
    newTaskRelationship.toId = captureId;
    newTaskRelationship.type = SymbolRelationshipEngine::CALLS;
    const SemanticIndexSnapshot enrichedSnapshot =
        snapshot->withAdditionalRelationships({
            duplicateStageRelationship,
            newTaskRelationship,
        });
    expectInt("semantic snapshot merge deduplicates relationship",
              enrichedSnapshot.relationships().size(),
              snapshot->relationships().size() + 1);
    bool enrichedFoundTask = false;
    for (const SemanticRelationship& relationship :
         enrichedSnapshot.getRelationships(stageId, true)) {
        enrichedFoundTask = enrichedFoundTask
            || (relationship.toId == captureId
                && relationship.type == SymbolRelationshipEngine::CALLS);
    }
    expectBool("semantic snapshot merge keeps new relationship",
               enrichedFoundTask, true);
    SemanticDiagnostic replacementDiagnostic;
    replacementDiagnostic.fileName = topPath;
    replacementDiagnostic.line = 12;
    replacementDiagnostic.column = 5;
    replacementDiagnostic.message = QStringLiteral("replacement message");
    replacementDiagnostic.severity = SemanticDiagnostic::Error;
    const SemanticIndexSnapshot diagnosticReplacementSnapshot =
        SemanticIndexSnapshot::fromSymbolDatabase(db, {
            infoDiagnostic,
            warningDiagnostic,
            errorDiagnostic,
        }).withReplacedDiagnostics({topPath}, {replacementDiagnostic});
    expectInt("semantic snapshot replaces file diagnostics",
              diagnosticReplacementSnapshot.getDiagnostics(topPath).size(), 1);
    expectBool("semantic snapshot keeps other file diagnostics",
               diagnosticReplacementSnapshot.getDiagnostics(stagePath).size() == 1
                   && diagnosticReplacementSnapshot.getDiagnostics(stagePath).first().message
                       == errorDiagnostic.message,
               true);
    SemanticIndex captureIndex(db);
    const auto capturedSnapshot =
        captureIndex.captureSnapshotPreservingDiagnostics();
    captureIndex.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        capturedSnapshot->getSymbols(),
        capturedSnapshot->relationships(),
        QList<SemanticDiagnostic>{errorDiagnostic},
        capturedSnapshot->fileContents()));
    const auto recapturedSnapshot =
        captureIndex.captureSnapshotPreservingDiagnostics();
    expectInt("semantic index capture preserves diagnostics",
              recapturedSnapshot->diagnostics().size(), 1);
    captureIndex.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        recapturedSnapshot->getSymbols(),
        recapturedSnapshot->relationships(),
        QList<SemanticDiagnostic>{warningDiagnostic, errorDiagnostic},
        recapturedSnapshot->fileContents()));
    const auto replacedCaptureSnapshot =
        captureIndex.captureSnapshotReplacingDiagnostics({topPath}, {replacementDiagnostic});
    expectInt("semantic index capture replaces target diagnostics",
              replacedCaptureSnapshot->getDiagnostics(topPath).size(), 1);
    expectBool("semantic index capture preserves other diagnostics",
               replacedCaptureSnapshot->getDiagnostics(stagePath).size() == 1
                   && replacedCaptureSnapshot->getDiagnostics(stagePath).first().message
                       == errorDiagnostic.message,
               true);
    snapshotIndex.clearSnapshot();
    expectInt("semantic snapshot clear restores live index",
              snapshotIndex.findSymbolId(QStringLiteral("rel_stage"), queryContext), stageId);

    RelationshipService relationshipService(&index);

    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolId = topId;
    relationshipQuery.outgoing = true;
    relationshipQuery.types = {
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::READS_FROM,
    };

    const QList<RelationshipResult> serviceRels =
        relationshipService.findRelationships(relationshipQuery);
    bool serviceFoundStage = false;
    bool serviceFoundTask = false;
    bool serviceFoundRead = false;
    for (const RelationshipResult& rel : serviceRels) {
        serviceFoundStage = serviceFoundStage
            || (rel.relationship.toId == stageId
                && rel.relationship.type == SymbolRelationshipEngine::INSTANTIATES
                && rel.toSymbol.symbolName == QStringLiteral("rel_stage"));
        serviceFoundTask = serviceFoundTask
            || (rel.relationship.toId == captureId
                && rel.relationship.type == SymbolRelationshipEngine::CALLS
                && rel.toSymbol.symbolName == QStringLiteral("capture_sample"));
        serviceFoundRead = serviceFoundRead
            || (rel.relationship.toId == reqValidId
                && rel.relationship.type == SymbolRelationshipEngine::READS_FROM
                && rel.toSymbol.symbolName == QStringLiteral("req_valid"));
    }

    expectBool("relationship service finds instantiation",
               serviceFoundStage, true);
    expectBool("relationship service finds call",
               serviceFoundTask, true);
    expectBool("relationship service finds condition read",
               serviceFoundRead, true);
    expectBool("relationship service sorts first by type",
               !serviceRels.isEmpty()
                   && serviceRels.first().relationship.type
                       == SymbolRelationshipEngine::INSTANTIATES,
               true);
    RelationshipQuery relatedIdsQuery;
    relatedIdsQuery.symbolId = topId;
    relatedIdsQuery.outgoing = true;
    relatedIdsQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    expectBool("relationship service returns related ids",
               relationshipService.findRelatedSymbolIds(relatedIdsQuery).contains(stageId), true);
    expectBool("relationship service exact relationship",
               relationshipService.hasRelationship(topId,
                                                   stageId,
                                                   SymbolRelationshipEngine::INSTANTIATES),
               true);
    expectBool("relationship service rejects reversed relationship",
               relationshipService.hasRelationship(stageId,
                                                   topId,
                                                   SymbolRelationshipEngine::INSTANTIATES),
               false);
    RelationshipBrowseQuery browseQuery;
    browseQuery.symbolId = topId;
    browseQuery.includeOutgoing = true;
    browseQuery.includeIncoming = true;
    browseQuery.types = {
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::READS_FROM,
    };
    const RelationshipReport relationshipReport =
        relationshipService.findRelationshipReport(browseQuery);
    expectInt("relationship report total count",
              relationshipReport.totalCount, serviceRels.size());
    expectInt("relationship report outgoing count",
              relationshipReport.outgoingCount, serviceRels.size());
    expectInt("relationship report incoming count",
              relationshipReport.incomingCount, 0);
    expectInt("relationship report direction map count",
              relationshipReport.directionCounts.value(DirectedRelationshipResult::Outgoing),
              serviceRels.size());
    expectInt("relationship report type count",
              relationshipReport.typeCounts.value(SymbolRelationshipEngine::INSTANTIATES), 1);
    expectInt("relationship report direction type count",
              relationshipReport.directionTypeCounts
                  .value(DirectedRelationshipResult::Outgoing)
                  .value(SymbolRelationshipEngine::INSTANTIATES),
              1);
    expectBool("relationship report keeps peer symbol",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().peerSymbol.symbolId == stageId,
               true);

    HierarchyService hierarchyService(&index);
    HierarchyQuery hierarchyQuery;
    hierarchyQuery.symbolId = topId;
    hierarchyQuery.maxDepth = 1;
    hierarchyQuery.types = {SymbolRelationshipEngine::INSTANTIATES};

    const QList<HierarchyNode> hierarchy = hierarchyService.getHierarchy(hierarchyQuery);
    bool hierarchyFoundRoot = false;
    bool hierarchyFoundStage = false;
    for (const HierarchyNode& node : hierarchy) {
        hierarchyFoundRoot = hierarchyFoundRoot
            || (node.depth == 0 && node.symbol.symbolId == topId);
        hierarchyFoundStage = hierarchyFoundStage
            || (node.depth == 1
                && node.parentSymbolId == topId
                && node.symbol.symbolId == stageId);
    }
    expectBool("hierarchy service includes root",
               hierarchyFoundRoot, true);
    expectBool("hierarchy service finds child instance",
               hierarchyFoundStage, true);
    const HierarchyReport hierarchyReport =
        hierarchyService.getHierarchyReport(hierarchyQuery);
    expectInt("hierarchy report total count",
              hierarchyReport.totalCount, hierarchy.size());
    expectInt("hierarchy report depth count",
              hierarchyReport.depthCounts.value(1), 1);
    expectInt("hierarchy report direction count",
              hierarchyReport.directionCounts.value(HierarchyQuery::Children), 1);
    expectInt("hierarchy report root direction count",
              hierarchyReport.rootDirectionCounts.value(HierarchyQuery::Children), 1);
    expectInt("hierarchy report type count",
              hierarchyReport.typeCounts.value(SymbolRelationshipEngine::INSTANTIATES), 1);
    expectBool("hierarchy service exposes all tree types",
               HierarchyService::allRelationshipTypes().contains(SymbolRelationshipEngine::READS_FROM),
               true);

    HierarchyQuery parentQuery;
    parentQuery.symbolId = stageId;
    parentQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<HierarchyNode> parents = hierarchyService.getParents(parentQuery);
    bool parentFoundTop = false;
    for (const HierarchyNode& node : parents) {
        parentFoundTop = parentFoundTop
            || (node.symbol.symbolId == topId && node.parentSymbolId == stageId);
    }
    expectBool("hierarchy service finds parent instance",
               parentFoundTop, true);

    HierarchyQuery parentTreeQuery;
    parentTreeQuery.symbolId = stageId;
    parentTreeQuery.maxDepth = 1;
    parentTreeQuery.direction = HierarchyQuery::Parents;
    parentTreeQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<HierarchyNode> parentTree =
        hierarchyService.getHierarchy(parentTreeQuery);
    bool parentTreeFoundRoot = false;
    bool parentTreeFoundTop = false;
    for (const HierarchyNode& node : parentTree) {
        parentTreeFoundRoot = parentTreeFoundRoot
            || (node.depth == 0 && node.symbol.symbolId == stageId);
        parentTreeFoundTop = parentTreeFoundTop
            || (node.depth == 1
                && node.parentSymbolId == stageId
                && node.symbol.symbolId == topId
                && node.direction == HierarchyQuery::Parents);
    }
    expectBool("hierarchy service parent tree includes root",
               parentTreeFoundRoot, true);
    expectBool("hierarchy service parent tree finds incoming parent",
               parentTreeFoundTop, true);

    engine.addRelationship(stageId, topId, SymbolRelationshipEngine::INSTANTIATES,
                           QStringLiteral("cycle guard probe"));
    HierarchyQuery cycleQuery;
    cycleQuery.symbolId = topId;
    cycleQuery.maxDepth = 4;
    cycleQuery.direction = HierarchyQuery::Both;
    cycleQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<HierarchyNode> cycleTree = hierarchyService.getHierarchy(cycleQuery);
    int cycleTopCount = 0;
    bool cycleSawOutgoingStage = false;
    bool cycleSawIncomingStage = false;
    for (const HierarchyNode& node : cycleTree) {
        if (node.symbol.symbolId == topId)
            ++cycleTopCount;
        cycleSawOutgoingStage = cycleSawOutgoingStage
            || (node.symbol.symbolId == stageId
                && node.direction == HierarchyQuery::Children);
        cycleSawIncomingStage = cycleSawIncomingStage
            || (node.symbol.symbolId == stageId
                && node.direction == HierarchyQuery::Parents);
    }
    expectInt("hierarchy service keeps cycle root once", cycleTopCount, 1);
    expectBool("hierarchy service keeps outgoing branch",
               cycleSawOutgoingStage, true);
    expectBool("hierarchy service keeps incoming branch",
               cycleSawIncomingStage, true);
    const HierarchyReport cycleReport =
        hierarchyService.getHierarchyReport(cycleQuery);
    expectInt("hierarchy report keeps outgoing root branch count",
              cycleReport.rootDirectionCounts.value(HierarchyQuery::Children), 1);
    expectInt("hierarchy report keeps incoming root branch count",
              cycleReport.rootDirectionCounts.value(HierarchyQuery::Parents), 1);

    ReferenceService referenceService(&index);

    ReferenceQuery stageReferenceQuery;
    stageReferenceQuery.symbolId = stageId;
    stageReferenceQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<ReferenceResult> stageReferences =
        referenceService.findReferences(stageReferenceQuery);
    bool referenceFoundTopInstance = false;
    for (const ReferenceResult& ref : stageReferences) {
        referenceFoundTopInstance = referenceFoundTopInstance
            || (ref.referencingSymbol.symbolId == topId
                && ref.referencedSymbol.symbolId == stageId
                && ref.relationship.relationship.type == SymbolRelationshipEngine::INSTANTIATES);
    }
    expectBool("reference service finds stage instantiation",
               referenceFoundTopInstance, true);
    const ReferenceReport stageReferenceReport =
        referenceService.findReferenceReport(stageReferenceQuery);
    expectInt("reference report total count",
              stageReferenceReport.totalCount, 1);
    expectInt("reference report file count",
              stageReferenceReport.fileCounts.value(topPath), 1);
    expectInt("reference report type count",
              stageReferenceReport.typeCounts.value(SymbolRelationshipEngine::INSTANTIATES), 1);
    expectInt("reference report file type count",
              stageReferenceReport.fileTypeCounts
                  .value(topPath)
                  .value(SymbolRelationshipEngine::INSTANTIATES),
              1);

    ReferenceQuery currentFileStageReferenceQuery = stageReferenceQuery;
    currentFileStageReferenceQuery.fileName = stagePath;
    currentFileStageReferenceQuery.currentFileOnly = true;
    expectInt("reference report current file filter",
              referenceService.findReferenceReport(currentFileStageReferenceQuery).totalCount, 0);

    ReferenceQuery workspaceStageReferenceQuery = stageReferenceQuery;
    workspaceStageReferenceQuery.workspaceFilesOnly = true;
    workspaceStageReferenceQuery.workspaceFiles = {topPath};
    expectInt("reference report workspace filter",
              referenceService.findReferenceReport(workspaceStageReferenceQuery).totalCount, 1);

    ReferenceQuery reqValidReferenceQuery;
    reqValidReferenceQuery.symbolId = reqValidId;
    reqValidReferenceQuery.types = {SymbolRelationshipEngine::READS_FROM};
    const QList<ReferenceResult> reqValidReferences =
        referenceService.findReferences(reqValidReferenceQuery);
    bool referenceFoundReqRead = false;
    for (const ReferenceResult& ref : reqValidReferences) {
        referenceFoundReqRead = referenceFoundReqRead
            || (ref.referencingSymbol.symbolId == topId
                && ref.referencedSymbol.symbolId == reqValidId
                && ref.relationship.relationship.type == SymbolRelationshipEngine::READS_FROM);
    }
    expectBool("reference service finds condition read",
               referenceFoundReqRead, true);

    ReferenceQuery rspDataReferenceQuery;
    rspDataReferenceQuery.symbolId = rspDataId;
    rspDataReferenceQuery.types = {SymbolRelationshipEngine::ASSIGNS_TO};
    const QList<ReferenceResult> rspDataReferences =
        referenceService.findReferences(rspDataReferenceQuery);
    bool referenceFoundRspWrite = false;
    for (const ReferenceResult& ref : rspDataReferences) {
        referenceFoundRspWrite = referenceFoundRspWrite
            || (ref.referencingSymbol.symbolId == stageDataId
                && ref.referencedSymbol.symbolId == rspDataId
                && ref.relationship.relationship.type == SymbolRelationshipEngine::ASSIGNS_TO);
    }
    expectBool("reference service finds assignment write",
               referenceFoundRspWrite, true);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    SlangManager slang;
    auto* db = sym_list::getInstance();

    SymbolRelationshipEngine engine;
    db->setRelationshipEngine(&engine);
    SmartRelationshipBuilder builder(&engine, db, &slang);

    runInlineRelationshipRegression(slang, db, builder);
    runMultiFileRelationshipFixture(slang, db, engine, builder);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
