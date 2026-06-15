// Headless relationship regression test. Verifies that relationship analysis in a multi-module
// file attaches line-derived relationships to the containing module, not always the first module.
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "analysisscheduler.h"
#include "clockresetdomainservice.h"
#include "completionservice.h"
#include "completionsymbolquery.h"
#include "definitionservice.h"
#include "semanticindex.h"
#include "diagnosticservice.h"
#include "documentmodel.h"
#include "fsmgraphservice.h"
#include "hierarchyservice.h"
#include "modulebriefservice.h"
#include "navigationservice.h"
#include "referenceservice.h"
#include "relationshipservice.h"
#include "searchservice.h"
#include "semanticdiffservice.h"
#include "signaljourneyservice.h"
#include "symbolrelationshipengine.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"
#include "syminfo.h"
#include "mycodeeditor.h"
#include "projectmodel.h"
#include "symbolanalyzer.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
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

static sym_list::SymbolInfo makeModuleBriefSymbol(
    int id,
    const QString& fileName,
    const QString& name,
    sym_list::sym_type_e type,
    int line,
    const QString& moduleScope = QString())
{
    sym_list::SymbolInfo symbol;
    symbol.symbolId = id;
    symbol.fileName = fileName;
    symbol.symbolName = name;
    symbol.symbolType = type;
    symbol.startLine = line;
    symbol.endLine = line;
    symbol.startColumn = 1;
    symbol.endColumn = 1;
    symbol.moduleScope = moduleScope;
    return symbol;
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
    const QStringList topLines = contents.value(topPath).split('\n');
    int topEndModuleLine = -1;
    for (int i = 0; i < topLines.size(); ++i) {
        if (topLines.at(i).trimmed() == QStringLiteral("endmodule")) {
            topEndModuleLine = i;
            break;
        }
    }
    expectBool("fixture top endmodule located", topEndModuleLine >= 0, true);
    expectInt("semantic facade finds module end line",
              index.findEndModuleLine(topPath, index.getSymbolById(topId)), topEndModuleLine);

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
    QTemporaryDir temporaryDiagnosticDir;
    const QString temporaryBrokenPath =
        temporaryDiagnosticDir.filePath(QStringLiteral("broken_diag.sv"));
    const QList<SemanticDiagnostic> temporaryBrokenDiagnostics =
        slang.extractDiagnostics(temporaryBrokenPath, brokenContent);
    expectBool("slang diagnostics flow from temp path",
               !temporaryBrokenDiagnostics.isEmpty(), true);
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
    expectBool("snapshot diagnostic service reports diagnostics",
               brokenDiagnosticService.hasDiagnostics(brokenDiagnosticQuery), true);
    DiagnosticQuery warningOnlyBrokenDiagnosticQuery = brokenDiagnosticQuery;
    warningOnlyBrokenDiagnosticQuery.includeInfo = false;
    warningOnlyBrokenDiagnosticQuery.includeWarnings = true;
    warningOnlyBrokenDiagnosticQuery.includeErrors = false;
    expectBool("snapshot diagnostic service filters severity absence",
               brokenDiagnosticService.hasDiagnostics(warningOnlyBrokenDiagnosticQuery), false);
    if (!brokenDiagnosticResults.isEmpty()) {
        expectBool("slang diagnostic has file",
                   brokenDiagnosticResults.first().diagnostic.fileName == brokenPath, true);
        expectBool("slang diagnostic has message",
                   !brokenDiagnosticResults.first().diagnostic.message.isEmpty(), true);
        expectBool("slang diagnostic has severity",
                   brokenDiagnosticResults.first().diagnostic.severity == SemanticDiagnostic::Error,
                   true);
        expectBool("slang diagnostic exposes display metadata",
                   brokenDiagnosticResults.first().severityDisplayName == QStringLiteral("Error")
                       && brokenDiagnosticResults.first().fileDisplayName
                          == QStringLiteral("broken_diag.sv"),
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
                && !group.displayName.isEmpty()
                && !group.diagnostics.first().severityDisplayName.isEmpty()
                && !group.diagnostics.first().fileDisplayName.isEmpty();
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
    expectBool("diagnostic report exposes row display metadata",
               !diagnosticReport.diagnostics.isEmpty()
                   && diagnosticReport.diagnostics.first().severityDisplayName
                       == QStringLiteral("Error")
                   && diagnosticReport.diagnostics.first().fileDisplayName
                       == QStringLiteral("relationship_stage.sv"),
               true);

    DiagnosticQuery topOnlyDiagnosticQuery;
    topOnlyDiagnosticQuery.fileName = topPath;
    const DiagnosticReport topOnlyDiagnosticReport =
        diagnosticReportService.findDiagnosticReport(topOnlyDiagnosticQuery);
    expectInt("diagnostic report filters current file",
              topOnlyDiagnosticReport.totalCount, 2);
    expectInt("diagnostic report current file group count",
              topOnlyDiagnosticReport.fileGroups.size(), 1);
    expectBool("diagnostic report current file grouped diagnostics",
               topOnlyDiagnosticReport.fileGroups.size() == 1
                   && topOnlyDiagnosticReport.fileGroups.first().fileKey == topPath
                   && topOnlyDiagnosticReport.fileGroups.first().count == 2
                   && topOnlyDiagnosticReport.fileGroups.first().diagnostics.size() == 2
                   && topOnlyDiagnosticReport.fileGroups.first()
                           .diagnostics.first()
                           .diagnostic.fileName == topPath
                   && topOnlyDiagnosticReport.fileGroups.first()
                           .diagnostics.last()
                           .diagnostic.fileName == topPath,
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
    expectBool("diagnostic service has workspace diagnostics",
               diagnosticReportService.hasDiagnostics(workspaceOnlyDiagnosticQuery), true);
    workspaceOnlyDiagnosticQuery.workspaceFiles = {stagePath};
    expectInt("diagnostic report keeps workspace error file",
              diagnosticReportService.findDiagnosticReport(workspaceOnlyDiagnosticQuery).totalCount,
              1);
    workspaceOnlyDiagnosticQuery.workspaceFiles = {normalizedPath(
        fixtureDir.filePath(QStringLiteral("not_in_workspace.sv")))};
    expectBool("diagnostic service rejects missing workspace file",
               diagnosticReportService.hasDiagnostics(workspaceOnlyDiagnosticQuery), false);
    DiagnosticPanelQueryOptions currentFilePanelOptions;
    currentFilePanelOptions.scope = DiagnosticPanelScope::CurrentFile;
    currentFilePanelOptions.severity = DiagnosticSeverityFilter::Warnings;
    currentFilePanelOptions.currentFileName = topPath;
    const DiagnosticQuery currentFilePanelQuery =
        diagnosticReportService.queryForPanel(currentFilePanelOptions);
    expectBool("diagnostic panel query selects current file",
               currentFilePanelQuery.fileName == topPath
                   && !currentFilePanelQuery.workspaceFilesOnly
                   && !currentFilePanelQuery.includeErrors
                   && currentFilePanelQuery.includeWarnings
                   && !currentFilePanelQuery.includeInfo,
               true);
    expectInt("diagnostic panel current warning count",
              diagnosticReportService
                  .findDiagnosticReport(currentFilePanelQuery)
                  .totalCount,
              1);

    DiagnosticPanelQueryOptions workspacePanelOptions;
    workspacePanelOptions.scope = DiagnosticPanelScope::WorkspaceFiles;
    workspacePanelOptions.workspaceFiles = {stagePath};
    const DiagnosticQuery workspacePanelQuery =
        diagnosticReportService.queryForPanel(workspacePanelOptions);
    expectBool("diagnostic panel query selects workspace files",
               workspacePanelQuery.workspaceFilesOnly
                   && workspacePanelQuery.workspaceFiles == QStringList{stagePath},
               true);
    expectInt("diagnostic panel workspace count",
              diagnosticReportService
                  .findDiagnosticReport(workspacePanelQuery)
                  .totalCount,
              1);

    DiagnosticPanelQueryOptions allFilesPanelOptions;
    allFilesPanelOptions.scope = DiagnosticPanelScope::AllFiles;
    allFilesPanelOptions.severity = DiagnosticSeverityFilter::Info;
    const DiagnosticQuery allFilesPanelQuery =
        diagnosticReportService.queryForPanel(allFilesPanelOptions);
    expectBool("diagnostic panel query selects all info",
               allFilesPanelQuery.fileName.isEmpty()
                   && !allFilesPanelQuery.workspaceFilesOnly
                   && !allFilesPanelQuery.includeErrors
                   && !allFilesPanelQuery.includeWarnings
                   && allFilesPanelQuery.includeInfo,
               true);
    expectInt("diagnostic panel all info count",
              diagnosticReportService
                  .findDiagnosticReport(allFilesPanelQuery)
                  .totalCount,
              1);

    sym_list injectedRelationshipSymbols;
    const QString injectedRelationshipPath =
        normalizedPath(fixtureDir.filePath(QStringLiteral("injected_relationships.sv")));
    sym_list::SymbolInfo injectedModule;
    injectedModule.fileName = injectedRelationshipPath;
    injectedModule.symbolName = QStringLiteral("injected_top");
    injectedModule.symbolType = sym_list::sym_module;
    injectedModule.startLine = 1;
    injectedModule.endLine = 9;
    injectedModule.symbolId = 7201;

    sym_list::SymbolInfo injectedSignal;
    injectedSignal.fileName = injectedRelationshipPath;
    injectedSignal.symbolName = QStringLiteral("injected_signal");
    injectedSignal.symbolType = sym_list::sym_logic;
    injectedSignal.startLine = 3;
    injectedSignal.endLine = 3;
    injectedSignal.symbolId = 7202;
    injectedRelationshipSymbols.setSymbolsForFile(
        injectedRelationshipPath,
        {injectedModule, injectedSignal});

    SymbolRelationshipEngine injectedRelationshipEngine(&injectedRelationshipSymbols);
    injectedRelationshipEngine.buildFileRelationships(injectedRelationshipPath);
    expectBool("relationship engine uses injected symbol db",
               injectedRelationshipEngine.hasRelationship(
                   injectedModule.symbolId,
                   injectedSignal.symbolId,
                   SymbolRelationshipEngine::CONTAINS),
               true);

    SearchService searchService(&index);
    SearchQuery moduleSearchQuery;
    moduleSearchQuery.text = QStringLiteral("rel_");
    moduleSearchQuery.intent = SymbolTaxonomy::SymbolSearchIntent::ModuleDeclarations;
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

    SearchQuery outlineSearchQuery;
    outlineSearchQuery.text = QStringLiteral("capture");
    outlineSearchQuery.intent = SymbolTaxonomy::SymbolSearchIntent::OutlineSymbols;
    const QList<SearchResult> outlineSearchResults =
        searchService.findSymbols(outlineSearchQuery);
    expectBool("search service outline intent finds task",
               outlineSearchResults.size() == 1
                   && outlineSearchResults.first().symbol.symbolId == captureId,
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

    index.attachRelationshipEngine(&engine);
    expectBool("semantic index attaches relationship engine",
               db->getRelationshipEngine() == &engine, true);
    const std::unique_ptr<SmartRelationshipBuilder> facadeBuilder =
        index.createRelationshipBuilder(&engine, &slang);
    expectBool("semantic index creates relationship builder",
               facadeBuilder != nullptr, true);
    const QVector<RelationshipToAdd> facadeBuilderRels =
        facadeBuilder->computeRelationships(topPath, contents.value(topPath), topSymbols);
    expectBool("semantic index builder uses facade database",
               hasRel(facadeBuilderRels,
                      topId,
                      stageId,
                      SymbolRelationshipEngine::INSTANTIATES),
               true);

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
    SearchService snapshotSearchService(&snapshotIndex);
    const QList<SearchResult> snapshotSearchResults =
        snapshotSearchService.findSymbols(moduleSearchQuery);
    bool snapshotSearchFoundTop = false;
    bool snapshotSearchFoundStage = false;
    for (const SearchResult& result : snapshotSearchResults) {
        snapshotSearchFoundTop = snapshotSearchFoundTop || result.symbol.symbolId == topId;
        snapshotSearchFoundStage = snapshotSearchFoundStage || result.symbol.symbolId == stageId;
    }
    expectBool("snapshot search service finds top module",
               snapshotSearchFoundTop, true);
    expectBool("snapshot search service finds stage module",
               snapshotSearchFoundStage, true);
    expectBool("snapshot search service has module matches",
               snapshotSearchService.hasMatches(moduleSearchQuery), true);
    SearchQuery snapshotMissingSearchQuery = moduleSearchQuery;
    snapshotMissingSearchQuery.text = QStringLiteral("missing_rel_module");
    expectBool("snapshot search service has no missing match",
               snapshotSearchService.hasMatches(snapshotMissingSearchQuery), false);
    const QList<SearchResult> snapshotFileModuleSearchResults =
        snapshotSearchService.findSymbols(fileModuleSearchQuery);
    bool snapshotFileSearchFoundTop = false;
    bool snapshotFileSearchFoundStage = false;
    for (const SearchResult& result : snapshotFileModuleSearchResults) {
        snapshotFileSearchFoundTop =
            snapshotFileSearchFoundTop || result.symbol.symbolId == topId;
        snapshotFileSearchFoundStage =
            snapshotFileSearchFoundStage || result.symbol.symbolId == stageId;
    }
    expectBool("snapshot search service filters file module",
               snapshotFileSearchFoundTop, true);
    expectBool("snapshot search service excludes other file module",
               snapshotFileSearchFoundStage, false);
    const QList<SearchResult> snapshotExactTaskResults =
        snapshotSearchService.findSymbols(exactTaskSearchQuery);
    expectBool("snapshot search service exact task result",
               snapshotExactTaskResults.size() == 1
                   && snapshotExactTaskResults.first().symbol.symbolId == captureId,
               true);
    expectInt("snapshot search service exact score",
              snapshotExactTaskResults.isEmpty() ? 0 : snapshotExactTaskResults.first().score,
              100);
    const QList<SearchResult> snapshotOutlineSearchResults =
        snapshotSearchService.findSymbols(outlineSearchQuery);
    expectBool("snapshot search service outline intent finds task",
               snapshotOutlineSearchResults.size() == 1
                   && snapshotOutlineSearchResults.first().symbol.symbolId == captureId,
               true);
    SearchQuery snapshotPartialExactTaskQuery = exactTaskSearchQuery;
    snapshotPartialExactTaskQuery.text = QStringLiteral("capture");
    expectBool("snapshot search service exact rejects partial",
               snapshotSearchService.findSymbols(snapshotPartialExactTaskQuery).isEmpty(),
               true);
    SearchQuery snapshotCaseInsensitiveQuery = moduleSearchQuery;
    snapshotCaseInsensitiveQuery.text = QStringLiteral("REL_");
    expectBool("snapshot search service case-insensitive match",
               snapshotSearchService.hasMatches(snapshotCaseInsensitiveQuery), true);
    SearchQuery snapshotCaseSensitiveQuery = snapshotCaseInsensitiveQuery;
    snapshotCaseSensitiveQuery.caseSensitive = true;
    expectBool("snapshot search service case-sensitive reject",
               snapshotSearchService.hasMatches(snapshotCaseSensitiveQuery), false);
    SearchQuery snapshotLimitedModuleQuery = moduleSearchQuery;
    snapshotLimitedModuleQuery.maxResults = 1;
    expectInt("snapshot search service max results",
              snapshotSearchService.findSymbols(snapshotLimitedModuleQuery).size(), 1);
    SearchQuery snapshotEmptyModuleQuery;
    snapshotEmptyModuleQuery.intent =
        SymbolTaxonomy::SymbolSearchIntent::ModuleDeclarations;
    const QList<SearchResult> snapshotEmptyModuleResults =
        snapshotSearchService.findSymbols(snapshotEmptyModuleQuery);
    bool snapshotEmptySearchFoundTop = false;
    bool snapshotEmptySearchFoundStage = false;
    bool snapshotEmptySearchUsesDefaultScore = !snapshotEmptyModuleResults.isEmpty();
    for (const SearchResult& result : snapshotEmptyModuleResults) {
        snapshotEmptySearchFoundTop =
            snapshotEmptySearchFoundTop || result.symbol.symbolId == topId;
        snapshotEmptySearchFoundStage =
            snapshotEmptySearchFoundStage || result.symbol.symbolId == stageId;
        snapshotEmptySearchUsesDefaultScore =
            snapshotEmptySearchUsesDefaultScore && result.score == 1;
    }
    expectBool("snapshot search service empty text keeps typed modules",
               snapshotEmptySearchFoundTop && snapshotEmptySearchFoundStage, true);
    expectBool("snapshot search service empty text score",
               snapshotEmptySearchUsesDefaultScore, true);
    SearchQuery snapshotEmptyFileModuleQuery = snapshotEmptyModuleQuery;
    snapshotEmptyFileModuleQuery.fileName = stagePath;
    const QList<SearchResult> snapshotEmptyFileModuleResults =
        snapshotSearchService.findSymbols(snapshotEmptyFileModuleQuery);
    bool snapshotEmptyFileFoundStage = false;
    bool snapshotEmptyFileFoundTop = false;
    for (const SearchResult& result : snapshotEmptyFileModuleResults) {
        snapshotEmptyFileFoundStage =
            snapshotEmptyFileFoundStage || result.symbol.symbolId == stageId;
        snapshotEmptyFileFoundTop =
            snapshotEmptyFileFoundTop || result.symbol.symbolId == topId;
    }
    expectBool("snapshot search service empty text filters file module",
               snapshotEmptyFileFoundStage && !snapshotEmptyFileFoundTop, true);
    NavigationService snapshotNavigationService(&snapshotIndex);
    const NavigationModuleTarget snapshotNavigationTarget =
        snapshotNavigationService.resolveModuleTarget(QStringLiteral("rel_stage"));
    expectBool("snapshot navigation service resolves module target",
               snapshotNavigationTarget.found
                   && snapshotNavigationTarget.symbol.symbolId == stageId
                   && snapshotNavigationTarget.symbol.fileName == stagePath,
               true);
    NavigationSymbolOutlineQuery snapshotOutlineQuery;
    snapshotOutlineQuery.fileName = topPath;
    const QList<SymbolOutlineGroup> snapshotOutlineGroups =
        snapshotNavigationService.findSymbolOutline(snapshotOutlineQuery);
    bool snapshotOutlineHasDisplayName = false;
    for (const SymbolOutlineGroup& group : snapshotOutlineGroups) {
        snapshotOutlineHasDisplayName =
            snapshotOutlineHasDisplayName
            || (group.symbolType == sym_list::sym_module
                && group.displayName == QStringLiteral("Module")
                && !group.symbols.isEmpty());
    }
    expectBool("snapshot navigation outline exposes display model",
               snapshotOutlineHasDisplayName,
               true);
    expectBool("semantic snapshot returns cached file content",
               snapshotIndex.getCachedFileContent(topPath) == contents.value(topPath), true);
    expectBool("semantic snapshot returns scope symbols",
               snapshotIndex.getScopeSymbolNames(topPath, 20).contains(QStringLiteral("stage_data")),
               true);
    sym_list::SymbolInfo snapshotTopSymbol = snapshotIndex.getSymbolById(topId);
    snapshotTopSymbol.endLine = 0;
    expectInt("semantic snapshot finds module end line from cached content",
              snapshotIndex.findEndModuleLine(topPath, snapshotTopSymbol), topEndModuleLine);
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
    const QList<SemanticRelationshipResult> snapshotTopRelationshipResults =
        snapshotIndex.getRelationshipResults(topId, true);
    bool snapshotFoundStageResult = false;
    for (const SemanticRelationshipResult& relationship : snapshotTopRelationshipResults) {
        snapshotFoundStageResult = snapshotFoundStageResult
            || (relationship.relationship.fromId == topId
                && relationship.relationship.toId == stageId
                && relationship.relationship.type == SymbolRelationshipEngine::INSTANTIATES
                && relationship.fromSymbol.symbolId == topId
                && relationship.toSymbol.symbolId == stageId);
    }
    expectBool("semantic snapshot returns relationship endpoint symbols",
               snapshotFoundStageResult, true);
    RelationshipService snapshotRelationshipService(&snapshotIndex);
    RelationshipQuery snapshotRelationshipQuery;
    snapshotRelationshipQuery.symbolId = topId;
    snapshotRelationshipQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<RelationshipResult> snapshotRelationshipResults =
        snapshotRelationshipService.findOutgoingRelationships(snapshotRelationshipQuery);
    bool snapshotRelationshipFoundStage = false;
    for (const RelationshipResult& relationship : snapshotRelationshipResults) {
        snapshotRelationshipFoundStage = snapshotRelationshipFoundStage
            || (relationship.relationship.fromId == topId
                && relationship.relationship.toId == stageId
                && relationship.fromSymbol.symbolId == topId
                && relationship.toSymbol.symbolId == stageId);
    }
    expectBool("snapshot relationship service finds stage instantiation",
               snapshotRelationshipFoundStage, true);
    expectBool("snapshot relationship service has relationships",
               snapshotRelationshipService.hasRelationships(snapshotRelationshipQuery), true);
    expectBool("snapshot relationship service exact relationship",
               snapshotRelationshipService.hasRelationship(
                   topId, stageId, SymbolRelationshipEngine::INSTANTIATES),
               true);
    expectBool("snapshot relationship service rejects reversed relationship",
               snapshotRelationshipService.hasRelationship(
                   stageId, topId, SymbolRelationshipEngine::INSTANTIATES),
               false);
    RelationshipBrowseQuery snapshotRelationshipBrowseQuery;
    snapshotRelationshipBrowseQuery.symbolId = topId;
    snapshotRelationshipBrowseQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const RelationshipReport snapshotRelationshipReport =
        snapshotRelationshipService.findRelationshipReport(snapshotRelationshipBrowseQuery);
    expectInt("snapshot relationship report subject id",
              snapshotRelationshipReport.subjectSymbolId, topId);
    expectBool("snapshot relationship report subject symbol",
               snapshotRelationshipReport.subjectSymbol.symbolId == topId, true);
    expectInt("snapshot relationship report outgoing count",
              snapshotRelationshipReport.outgoingCount, 1);
    expectInt("snapshot relationship report total count",
              snapshotRelationshipReport.totalCount, 1);
    expectBool("snapshot relationship report keeps peer symbol",
               !snapshotRelationshipReport.relationships.isEmpty()
                   && snapshotRelationshipReport.relationships.first()
                          .peerSymbol.symbolId == stageId,
               true);
    expectBool("snapshot relationship report groups outgoing type",
               snapshotRelationshipReport.directionGroups.size() == 1
                   && snapshotRelationshipReport.directionGroups.first().direction
                       == DirectedRelationshipResult::Outgoing
                   && snapshotRelationshipReport.directionGroups.first().displayName
                       == QStringLiteral("Outgoing")
                   && snapshotRelationshipReport.directionGroups.first().count == 1
                   && snapshotRelationshipReport.directionGroups.first().typeGroups.size() == 1
                   && snapshotRelationshipReport.directionGroups.first()
                          .typeGroups.first()
                          .type == SymbolRelationshipEngine::INSTANTIATES
                   && snapshotRelationshipReport.directionGroups.first()
                          .typeGroups.first()
                          .displayName == QStringLiteral("Instantiates")
                   && snapshotRelationshipReport.directionGroups.first()
                          .typeGroups.first()
                          .count == 1
                   && !snapshotRelationshipReport.directionGroups.first()
                           .typeGroups.first()
                           .relationships.isEmpty()
                   && snapshotRelationshipReport.directionGroups.first()
                          .typeGroups.first()
                          .relationships.first()
                          .directionDisplayName == QStringLiteral("Outgoing")
                   && snapshotRelationshipReport.directionGroups.first()
                          .typeGroups.first()
                          .relationships.first()
                          .typeDisplayName == QStringLiteral("Instantiates")
                   && snapshotRelationshipReport.directionGroups.first()
                          .typeGroups.first()
                          .relationships.first()
                          .peerSymbol.symbolId == stageId,
               true);
    RelationshipBrowseQuery snapshotIncomingStageBrowseQuery;
    snapshotIncomingStageBrowseQuery.symbolId = stageId;
    snapshotIncomingStageBrowseQuery.includeOutgoing = false;
    snapshotIncomingStageBrowseQuery.includeIncoming = true;
    snapshotIncomingStageBrowseQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const RelationshipReport snapshotIncomingStageReport =
        snapshotRelationshipService.findRelationshipReport(snapshotIncomingStageBrowseQuery);
    expectInt("snapshot relationship report incoming-only total",
              snapshotIncomingStageReport.totalCount, 1);
    expectInt("snapshot relationship report incoming-only count",
              snapshotIncomingStageReport.incomingCount, 1);
    expectBool("snapshot relationship report incoming peer symbol",
               !snapshotIncomingStageReport.relationships.isEmpty()
                   && snapshotIncomingStageReport.relationships.first()
                          .peerSymbol.symbolId == topId,
               true);
    expectBool("snapshot relationship report groups incoming type",
               snapshotIncomingStageReport.directionGroups.size() == 1
                   && snapshotIncomingStageReport.directionGroups.first().direction
                       == DirectedRelationshipResult::Incoming
                   && snapshotIncomingStageReport.directionGroups.first().displayName
                       == QStringLiteral("Incoming")
                   && snapshotIncomingStageReport.directionGroups.first().count == 1
                   && snapshotIncomingStageReport.directionGroups.first().typeGroups.size() == 1
                   && snapshotIncomingStageReport.directionGroups.first()
                          .typeGroups.first()
                          .type == SymbolRelationshipEngine::INSTANTIATES
                   && snapshotIncomingStageReport.directionGroups.first()
                          .typeGroups.first()
                          .displayName == QStringLiteral("Instantiates")
                   && snapshotIncomingStageReport.directionGroups.first()
                          .typeGroups.first()
                          .count == 1
                   && !snapshotIncomingStageReport.directionGroups.first()
                           .typeGroups.first()
                           .relationships.isEmpty()
                   && snapshotIncomingStageReport.directionGroups.first()
                          .typeGroups.first()
                          .relationships.first()
                          .directionDisplayName == QStringLiteral("Incoming")
                   && snapshotIncomingStageReport.directionGroups.first()
                          .typeGroups.first()
                          .relationships.first()
                          .typeDisplayName == QStringLiteral("Instantiates")
                   && snapshotIncomingStageReport.directionGroups.first()
                          .typeGroups.first()
                          .relationships.first()
                          .peerSymbol.symbolId == topId,
               true);
    RelationshipQuery snapshotNamedRelationshipQuery;
    snapshotNamedRelationshipQuery.symbolName = QStringLiteral("rel_top");
    snapshotNamedRelationshipQuery.fileName = topPath;
    snapshotNamedRelationshipQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<RelationshipResult> snapshotNamedRelationshipResults =
        snapshotRelationshipService.findOutgoingRelationships(snapshotNamedRelationshipQuery);
    expectBool("snapshot relationship service resolves query symbol name",
               snapshotNamedRelationshipResults.size() == 1
                   && snapshotNamedRelationshipResults.first().relationship.fromId == topId
                   && snapshotNamedRelationshipResults.first().relationship.toId == stageId,
               true);
    RelationshipBrowseQuery snapshotNamedRelationshipBrowseQuery;
    snapshotNamedRelationshipBrowseQuery.symbolName = QStringLiteral("rel_top");
    snapshotNamedRelationshipBrowseQuery.fileName = topPath;
    snapshotNamedRelationshipBrowseQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const RelationshipReport snapshotNamedRelationshipReport =
        snapshotRelationshipService.findRelationshipReport(snapshotNamedRelationshipBrowseQuery);
    expectBool("snapshot relationship report resolves query symbol name",
               snapshotNamedRelationshipReport.subjectSymbolId == topId
                   && snapshotNamedRelationshipReport.totalCount == 1
                   && !snapshotNamedRelationshipReport.relationships.isEmpty()
                   && snapshotNamedRelationshipReport.relationships.first()
                          .peerSymbol.symbolId == stageId,
               true);
    ReferenceService snapshotReferenceService(&snapshotIndex);
    ReferenceQuery snapshotReferenceQuery;
    snapshotReferenceQuery.symbolId = stageId;
    snapshotReferenceQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<ReferenceResult> snapshotReferenceResults =
        snapshotReferenceService.findReferences(snapshotReferenceQuery);
    bool snapshotReferenceFoundTop = false;
    for (const ReferenceResult& reference : snapshotReferenceResults) {
        snapshotReferenceFoundTop = snapshotReferenceFoundTop
            || (reference.relationship.relationship.fromId == topId
                && reference.relationship.relationship.toId == stageId
                && reference.referencingSymbol.symbolId == topId
                && reference.referencedSymbol.symbolId == stageId);
    }
    expectBool("snapshot reference service finds stage instantiation",
               snapshotReferenceFoundTop, true);
    expectBool("snapshot reference service has references",
               snapshotReferenceService.hasReferences(snapshotReferenceQuery), true);
    const ReferenceReport snapshotReferenceReport =
        snapshotReferenceService.findReferenceReport(snapshotReferenceQuery);
    expectInt("snapshot reference report subject id",
              snapshotReferenceReport.subjectSymbolId, stageId);
    expectBool("snapshot reference report subject symbol",
               snapshotReferenceReport.subjectSymbol.symbolId == stageId, true);
    expectInt("snapshot reference report total count",
              snapshotReferenceReport.totalCount, 1);
    expectInt("snapshot reference report file count",
              snapshotReferenceReport.fileCounts.value(topPath), 1);
    expectInt("snapshot reference report type count",
              snapshotReferenceReport.typeCounts.value(SymbolRelationshipEngine::INSTANTIATES), 1);
    expectInt("snapshot reference report file/type count",
              snapshotReferenceReport.fileTypeCounts
                  .value(topPath)
                  .value(SymbolRelationshipEngine::INSTANTIATES),
              1);
    expectBool("snapshot reference report groups file metadata",
               snapshotReferenceReport.fileGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first().fileKey == topPath
                   && snapshotReferenceReport.fileGroups.first().displayName
                       == QStringLiteral("relationship_top.sv")
                   && snapshotReferenceReport.fileGroups.first().count == 1
                   && snapshotReferenceReport.fileGroups.first().typeGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .type == SymbolRelationshipEngine::INSTANTIATES
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .displayName == QStringLiteral("Instantiates")
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .count == 1,
               true);
    expectBool("snapshot reference report keeps grouped symbols",
               snapshotReferenceReport.fileGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first().typeGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencingSymbol.symbolId == topId
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencedSymbol.symbolId == stageId,
               true);
    ReferenceQuery snapshotCurrentFileReferenceQuery = snapshotReferenceQuery;
    snapshotCurrentFileReferenceQuery.currentFileOnly = true;
    snapshotCurrentFileReferenceQuery.fileName = topPath;
    expectInt("snapshot reference report current file filter",
              snapshotReferenceService.findReferenceReport(snapshotCurrentFileReferenceQuery)
                  .totalCount,
              1);
    snapshotCurrentFileReferenceQuery.fileName = stagePath;
    expectInt("snapshot reference report current file hides other file",
              snapshotReferenceService.findReferenceReport(snapshotCurrentFileReferenceQuery)
                  .totalCount,
              0);
    ReferenceQuery snapshotWorkspaceReferenceQuery = snapshotReferenceQuery;
    snapshotWorkspaceReferenceQuery.workspaceFilesOnly = true;
    snapshotWorkspaceReferenceQuery.workspaceFiles = {topPath};
    expectInt("snapshot reference report workspace filter",
              snapshotReferenceService.findReferenceReport(snapshotWorkspaceReferenceQuery)
                  .totalCount,
              1);
    snapshotWorkspaceReferenceQuery.workspaceFiles = {stagePath};
    expectInt("snapshot reference report workspace hides other file",
              snapshotReferenceService.findReferenceReport(snapshotWorkspaceReferenceQuery)
                  .totalCount,
              0);
    ReferenceQuery snapshotNamedReferenceQuery;
    snapshotNamedReferenceQuery.symbolName = QStringLiteral("rel_stage");
    snapshotNamedReferenceQuery.fileName = stagePath;
    snapshotNamedReferenceQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const ReferenceReport snapshotNamedReferenceReport =
        snapshotReferenceService.findReferenceReport(snapshotNamedReferenceQuery);
    expectBool("snapshot reference service resolves query symbol name",
               snapshotNamedReferenceReport.subjectSymbolId == stageId
                   && snapshotNamedReferenceReport.totalCount == 1
                   && !snapshotNamedReferenceReport.references.isEmpty()
                   && snapshotNamedReferenceReport.references.first()
                          .referencingSymbol.symbolId == topId,
               true);
    HierarchyService snapshotHierarchyService(&snapshotIndex);
    HierarchyQuery snapshotHierarchyQuery;
    snapshotHierarchyQuery.symbolId = topId;
    snapshotHierarchyQuery.maxDepth = 1;
    snapshotHierarchyQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<HierarchyNode> snapshotHierarchy =
        snapshotHierarchyService.getHierarchy(snapshotHierarchyQuery);
    bool snapshotHierarchyFoundStage = false;
    for (const HierarchyNode& node : snapshotHierarchy) {
        snapshotHierarchyFoundStage = snapshotHierarchyFoundStage
            || (node.depth == 1
                && node.parentSymbolId == topId
                && node.symbol.symbolId == stageId
                && node.direction == HierarchyQuery::Children
                && node.viaType == SymbolRelationshipEngine::INSTANTIATES
                && node.directionDisplayName == QStringLiteral("Outgoing")
                && node.relationshipTypeDisplayName == QStringLiteral("Instantiates"));
    }
    expectBool("snapshot hierarchy service finds stage child",
               snapshotHierarchyFoundStage, true);
    const HierarchyReport snapshotHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotHierarchyQuery);
    expectInt("snapshot hierarchy report total count",
              snapshotHierarchyReport.totalCount, 2);
    expectInt("snapshot hierarchy report depth zero count",
              snapshotHierarchyReport.depthCounts.value(0), 1);
    expectInt("snapshot hierarchy report depth one count",
              snapshotHierarchyReport.depthCounts.value(1), 1);
    expectInt("snapshot hierarchy report type count",
              snapshotHierarchyReport.typeCounts.value(SymbolRelationshipEngine::INSTANTIATES), 1);
    expectInt("snapshot hierarchy report child direction count",
              snapshotHierarchyReport.directionCounts.value(HierarchyQuery::Children), 1);
    expectInt("snapshot hierarchy report child root direction count",
              snapshotHierarchyReport.rootDirectionCounts.value(HierarchyQuery::Children), 1);
    expectBool("snapshot hierarchy report groups root child direction",
               snapshotHierarchyReport.rootDirectionGroups.size() == 1
                   && snapshotHierarchyReport.rootDirectionGroups.first().direction
                       == HierarchyQuery::Children
                   && snapshotHierarchyReport.rootDirectionGroups.first().displayName
                       == QStringLiteral("Outgoing")
                   && snapshotHierarchyReport.rootDirectionGroups.first().count == 1
                   && snapshotHierarchyReport.rootDirectionGroups.first().nodes.size() == 1
                   && snapshotHierarchyReport.rootDirectionGroups.first()
                          .nodes.first()
                          .directionDisplayName == QStringLiteral("Outgoing")
                   && snapshotHierarchyReport.rootDirectionGroups.first()
                          .nodes.first()
                          .relationshipTypeDisplayName == QStringLiteral("Instantiates")
                   && snapshotHierarchyReport.rootDirectionGroups.first()
                          .nodes.first()
                          .symbol.symbolId == stageId,
               true);
    expectBool("snapshot hierarchy report keeps child identity",
               snapshotHierarchyReport.nodes.size() == 2
                   && snapshotHierarchyReport.nodes.last().symbol.symbolId == stageId
                   && snapshotHierarchyReport.nodes.last().parentSymbolId == topId,
               true);
    expectBool("snapshot hierarchy report keeps child node links",
               snapshotHierarchyReport.nodes.size() == 2
                   && snapshotHierarchyReport.nodes.first().nodeId == 0
                   && snapshotHierarchyReport.nodes.first().parentNodeId == -1
                   && snapshotHierarchyReport.nodes.first().relationshipTypeDisplayName
                       == QStringLiteral("Root")
                   && snapshotHierarchyReport.nodes.last().nodeId == 1
                   && snapshotHierarchyReport.nodes.last().parentNodeId == 0,
               true);
    HierarchyQuery snapshotParentHierarchyQuery;
    snapshotParentHierarchyQuery.symbolId = stageId;
    snapshotParentHierarchyQuery.direction = HierarchyQuery::Parents;
    snapshotParentHierarchyQuery.maxDepth = 1;
    snapshotParentHierarchyQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const HierarchyReport snapshotParentHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotParentHierarchyQuery);
    expectInt("snapshot hierarchy parent report total count",
              snapshotParentHierarchyReport.totalCount, 2);
    expectInt("snapshot hierarchy parent report root direction count",
              snapshotParentHierarchyReport.rootDirectionCounts.value(HierarchyQuery::Parents),
              1);
    expectInt("snapshot hierarchy parent report direction count",
              snapshotParentHierarchyReport.directionCounts.value(HierarchyQuery::Parents),
              1);
    expectBool("snapshot hierarchy report groups root parent direction",
               snapshotParentHierarchyReport.rootDirectionGroups.size() == 1
                   && snapshotParentHierarchyReport.rootDirectionGroups.first().direction
                       == HierarchyQuery::Parents
                   && snapshotParentHierarchyReport.rootDirectionGroups.first().displayName
                       == QStringLiteral("Incoming")
                   && snapshotParentHierarchyReport.rootDirectionGroups.first().count == 1
                   && snapshotParentHierarchyReport.rootDirectionGroups.first().nodes.size() == 1
                   && snapshotParentHierarchyReport.rootDirectionGroups.first()
                          .nodes.first()
                          .directionDisplayName == QStringLiteral("Incoming")
                   && snapshotParentHierarchyReport.rootDirectionGroups.first()
                          .nodes.first()
                          .relationshipTypeDisplayName == QStringLiteral("Instantiates")
                   && snapshotParentHierarchyReport.rootDirectionGroups.first()
                          .nodes.first()
                          .symbol.symbolId == topId,
               true);
    expectBool("snapshot hierarchy parent report keeps parent identity",
               snapshotParentHierarchyReport.nodes.size() == 2
                   && snapshotParentHierarchyReport.nodes.last().symbol.symbolId == topId
                   && snapshotParentHierarchyReport.nodes.last().parentSymbolId == stageId
                   && snapshotParentHierarchyReport.nodes.last().direction == HierarchyQuery::Parents,
               true);
    HierarchyQuery snapshotNamedHierarchyQuery;
    snapshotNamedHierarchyQuery.symbolName = QStringLiteral("rel_top");
    snapshotNamedHierarchyQuery.fileName = topPath;
    snapshotNamedHierarchyQuery.maxDepth = 1;
    snapshotNamedHierarchyQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const HierarchyReport snapshotNamedHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotNamedHierarchyQuery);
    expectBool("snapshot hierarchy service resolves query symbol name",
               snapshotNamedHierarchyReport.totalCount == 2
                   && snapshotNamedHierarchyReport.nodes.first().symbol.symbolId == topId
                   && snapshotNamedHierarchyReport.nodes.last().symbol.symbolId == stageId,
               true);
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
    SemanticIndex guardedIndex(db);
    const auto guardedBaseSnapshot =
        std::make_shared<SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolDatabase(db));
    guardedIndex.setSnapshot(guardedBaseSnapshot);
    const SemanticSnapshotToken staleToken = guardedIndex.snapshotToken();
    const auto newerSnapshot =
        std::make_shared<SemanticIndexSnapshot>(
            guardedBaseSnapshot->withAdditionalRelationships({newTaskRelationship}));
    guardedIndex.setSnapshot(newerSnapshot);
    const auto staleRelationshipSnapshot =
        std::make_shared<SemanticIndexSnapshot>(
            guardedBaseSnapshot->withAdditionalRelationships({duplicateStageRelationship}));
    expectBool("semantic index rejects stale snapshot token",
               guardedIndex.publishSnapshotIfCurrent(staleToken,
                                                     staleRelationshipSnapshot),
               false);
    expectBool("semantic index keeps newer snapshot after stale token",
               guardedIndex.snapshot() == newerSnapshot,
               true);
    const SemanticSnapshotToken currentToken = guardedIndex.snapshotToken();
    expectBool("semantic index accepts current snapshot token",
               guardedIndex.publishSnapshotIfCurrent(currentToken,
                                                     staleRelationshipSnapshot),
               true);
    expectBool("semantic index publishes current token snapshot",
               guardedIndex.snapshot() == staleRelationshipSnapshot,
               true);
    snapshotIndex.clearSnapshot();
    expectInt("semantic snapshot clear restores live index",
              snapshotIndex.findSymbolId(QStringLiteral("rel_stage"), queryContext), stageId);

    engine.clearAllRelationships();
    expectBool("scheduler test starts from empty relationship engine",
               engine.hasRelationship(topId,
                                      stageId,
                                      SymbolRelationshipEngine::INSTANTIATES),
               false);

    AnalysisScheduler scheduler;
    scheduler.setRelationshipEngine(&engine);
    scheduler.setRelationshipBuilder(&builder);
    SingleFileRelationshipAnalysisResult singleFileSchedulerResult;
    bool singleFileSchedulerFinished = false;
    bool singleFileSchedulerProgress = false;
    QEventLoop singleFileSchedulerLoop;
    QObject::connect(&scheduler,
                     &AnalysisScheduler::relationshipAnalysisProgress,
                     &singleFileSchedulerLoop,
                     [&](const QString& fileName, int relationshipsFound) {
                         singleFileSchedulerProgress = singleFileSchedulerProgress
                             || (fileName == topPath && relationshipsFound >= 0);
                     });
    QObject::connect(&scheduler,
                     &AnalysisScheduler::relationshipAnalysisFinished,
                     &singleFileSchedulerLoop,
                     [&](const SingleFileRelationshipAnalysisResult& result) {
                         singleFileSchedulerResult = result;
                         singleFileSchedulerFinished = true;
                         singleFileSchedulerLoop.quit();
                     });
    QTimer::singleShot(5000, &singleFileSchedulerLoop, &QEventLoop::quit);
    scheduler.requestRelationshipAnalysis(topPath, contents.value(topPath));
    singleFileSchedulerLoop.exec();
    QApplication::processEvents();
    expectBool("scheduler single-file relationship finishes",
               singleFileSchedulerFinished, true);
    expectBool("scheduler forwards single-file relationship progress",
               singleFileSchedulerProgress, true);
    bool singleFileSchedulerFoundStageRelationship = false;
    if (singleFileSchedulerResult.semanticSnapshot) {
        const QList<SemanticRelationship> schedulerTopRelationships =
            singleFileSchedulerResult.semanticSnapshot->getRelationships(topId, true);
        for (const SemanticRelationship& relationship : schedulerTopRelationships) {
            singleFileSchedulerFoundStageRelationship =
                singleFileSchedulerFoundStageRelationship
                || (relationship.toId == stageId
                    && relationship.type == SymbolRelationshipEngine::INSTANTIATES);
        }
    }
    expectBool("scheduler single-file snapshot merges relationships",
               singleFileSchedulerFoundStageRelationship, true);
    expectBool("scheduler single-file applies relationships",
               engine.hasRelationship(topId,
                                      stageId,
                                      SymbolRelationshipEngine::INSTANTIATES),
               true);

    ProjectModel diagnosticProject;
    scheduler.setProjectModel(&diagnosticProject);
    int diagnosticsRefreshRequests = 0;
    QEventLoop diagnosticsRefreshLoop;
    QObject::connect(&scheduler,
                     &AnalysisScheduler::diagnosticsRefreshRequested,
                     &diagnosticsRefreshLoop,
                     [&](const QString& fileName) {
                         if (fileName.isEmpty()) {
                             ++diagnosticsRefreshRequests;
                             diagnosticsRefreshLoop.quit();
                         }
                     });
    diagnosticProject.setWorkspaceRoot(fixtureDir.absolutePath());
    diagnosticProject.closeProject();
    QTimer::singleShot(1000, &diagnosticsRefreshLoop, &QEventLoop::quit);
    diagnosticsRefreshLoop.exec();
    expectBool("scheduler requests diagnostics refresh on project close",
               diagnosticsRefreshRequests > 0, true);

    const auto workspaceMergePreviousSnapshot = SemanticIndex::getInstance()->snapshot();
    QTemporaryDir workspaceMergeDir;
    expectBool("workspace merge temp dir created", workspaceMergeDir.isValid(), true);
    const QString workspaceMergeExternalPath =
        workspaceMergeDir.filePath(QStringLiteral("workspace_merge_external.sv"));
    SemanticDiagnostic workspaceMergeExternalDiagnostic;
    workspaceMergeExternalDiagnostic.fileName = workspaceMergeExternalPath;
    workspaceMergeExternalDiagnostic.line = 1;
    workspaceMergeExternalDiagnostic.column = 1;
    workspaceMergeExternalDiagnostic.message = QStringLiteral("external diagnostic");
    workspaceMergeExternalDiagnostic.severity = SemanticDiagnostic::Error;
    SemanticIndex::getInstance()->setSnapshot(
        std::make_shared<const SemanticIndexSnapshot>(
            QList<sym_list::SymbolInfo>(),
            QList<SemanticRelationship>(),
            QList<SemanticDiagnostic>{workspaceMergeExternalDiagnostic},
            QHash<QString, QString>()));

    AnalysisScheduler emptyWorkspaceScheduler;
    SymbolAnalyzer emptyWorkspaceAnalyzer;
    ProjectModel emptyWorkspaceModel;
    emptyWorkspaceScheduler.setSymbolAnalyzer(&emptyWorkspaceAnalyzer);
    emptyWorkspaceScheduler.setProjectModel(&emptyWorkspaceModel);
    emptyWorkspaceModel.setWorkspaceRoot(workspaceMergeDir.path());
    expectBool("workspace request preserves visible snapshot before results",
               SemanticIndex::getInstance()->snapshot()
                   && !SemanticIndex::getInstance()
                           ->snapshot()
                           ->getDiagnostics(workspaceMergeExternalPath)
                           .isEmpty(),
               true);

    const QString workspaceMergePath =
        workspaceMergeDir.filePath(QStringLiteral("workspace_merge_file.sv"));
    QFile workspaceMergeFile(workspaceMergePath);
    expectBool("workspace merge file writable",
               workspaceMergeFile.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (workspaceMergeFile.isOpen()) {
        workspaceMergeFile.write("module workspace_merge_file; endmodule\n");
        workspaceMergeFile.close();
    }
    SymbolAnalyzer workspaceMergeAnalyzer;
    ProjectModel workspaceMergeProject;
    workspaceMergeProject.setWorkspaceRoot(workspaceMergeDir.path());
    workspaceMergeProject.setScannedFiles({workspaceMergePath});
    workspaceMergeAnalyzer.analyzeProject(workspaceMergeProject.snapshot());
    expectBool("workspace publish preserves external diagnostics",
               SemanticIndex::getInstance()->snapshot()
                   && !SemanticIndex::getInstance()
                           ->snapshot()
                           ->getDiagnostics(workspaceMergeExternalPath)
                           .isEmpty(),
               true);
    if (workspaceMergePreviousSnapshot)
        SemanticIndex::getInstance()->setSnapshot(workspaceMergePreviousSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();

    QTemporaryDir dirtyWorkspaceMergeDir;
    expectBool("dirty workspace merge temp dir created",
               dirtyWorkspaceMergeDir.isValid(),
               true);
    const QString dirtyWorkspaceFilePath =
        dirtyWorkspaceMergeDir.filePath(QStringLiteral("dirty_workspace_file.sv"));
    QFile dirtyWorkspaceFile(dirtyWorkspaceFilePath);
    expectBool("dirty workspace file writable",
               dirtyWorkspaceFile.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (dirtyWorkspaceFile.isOpen()) {
        dirtyWorkspaceFile.write(
            "module dirty_disk; logic disk_signal; endmodule\n");
        dirtyWorkspaceFile.close();
    }

    AnalysisScheduler dirtyWorkspaceScheduler;
    SymbolAnalyzer dirtyWorkspaceAnalyzer;
    DocumentModel dirtyWorkspaceDocuments;
    ProjectModel dirtyWorkspaceProject;
    MyCodeEditor dirtyWorkspaceEditor;
    dirtyWorkspaceEditor.setPlainText(
        QStringLiteral("module dirty_disk; logic disk_signal; endmodule\n"));
    dirtyWorkspaceScheduler.setSymbolAnalyzer(&dirtyWorkspaceAnalyzer);
    dirtyWorkspaceScheduler.setDocumentModel(&dirtyWorkspaceDocuments);
    dirtyWorkspaceScheduler.setProjectModel(&dirtyWorkspaceProject);
    dirtyWorkspaceDocuments.registerEditor(&dirtyWorkspaceEditor,
                                           dirtyWorkspaceFilePath);
    dirtyWorkspaceEditor.setPlainText(
        QStringLiteral("module dirty_open; logic dirty_signal; endmodule\n"));
    QApplication::processEvents();
    expectBool("dirty workspace document is dirty",
               dirtyWorkspaceDocuments.documentForFile(dirtyWorkspaceFilePath).dirty,
               true);
    dirtyWorkspaceAnalyzer.analyzeFileContent(
        dirtyWorkspaceFilePath,
        dirtyWorkspaceEditor.toPlainText());

    int dirtyWorkspaceFilesAnalyzed = -1;
    bool dirtyWorkspaceFinished = false;
    QEventLoop dirtyWorkspaceLoop;
    QObject::connect(&dirtyWorkspaceScheduler,
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &dirtyWorkspaceLoop,
                     [&](const ProjectSnapshot&, int filesAnalyzed, int) {
                         dirtyWorkspaceFilesAnalyzed = filesAnalyzed;
                         dirtyWorkspaceFinished = true;
                         dirtyWorkspaceLoop.quit();
                     });
    dirtyWorkspaceProject.setWorkspaceRoot(dirtyWorkspaceMergeDir.path());
    dirtyWorkspaceProject.setScannedFiles({dirtyWorkspaceFilePath});
    QTimer::singleShot(5000, &dirtyWorkspaceLoop, &QEventLoop::quit);
    dirtyWorkspaceLoop.exec();
    QApplication::processEvents();
    bool dirtySnapshotHasOpen = false;
    bool dirtySnapshotHasDisk = false;
    if (const auto snapshot = SemanticIndex::getInstance()->snapshot()) {
        for (const sym_list::SymbolInfo& symbol :
             snapshot->getSymbols(dirtyWorkspaceFilePath)) {
            dirtySnapshotHasOpen = dirtySnapshotHasOpen
                || symbol.symbolName == QStringLiteral("dirty_open")
                || symbol.symbolName == QStringLiteral("dirty_signal");
            dirtySnapshotHasDisk = dirtySnapshotHasDisk
                || symbol.symbolName == QStringLiteral("dirty_disk")
                || symbol.symbolName == QStringLiteral("disk_signal");
        }
    }
    expectBool("workspace skips dirty open document publication",
               dirtyWorkspaceFinished && dirtyWorkspaceFilesAnalyzed == 0,
               true);
    expectBool("workspace preserves dirty open document symbols",
               dirtySnapshotHasOpen && !dirtySnapshotHasDisk,
               true);
    if (workspaceMergePreviousSnapshot)
        SemanticIndex::getInstance()->setSnapshot(workspaceMergePreviousSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();

    QTemporaryDir dirtyExternalDir;
    expectBool("dirty external temp dir created",
               dirtyExternalDir.isValid(),
               true);
    const QString dirtyExternalPath =
        dirtyExternalDir.filePath(QStringLiteral("dirty_external_file.sv"));
    QFile dirtyExternalFile(dirtyExternalPath);
    expectBool("dirty external file writable",
               dirtyExternalFile.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (dirtyExternalFile.isOpen()) {
        dirtyExternalFile.write(
            "module external_disk; logic external_disk_signal; endmodule\n");
        dirtyExternalFile.close();
    }

    AnalysisScheduler dirtyExternalScheduler;
    SymbolAnalyzer dirtyExternalAnalyzer;
    DocumentModel dirtyExternalDocuments;
    MyCodeEditor dirtyExternalEditor;
    dirtyExternalEditor.setPlainText(
        QStringLiteral("module external_disk; logic external_disk_signal; endmodule\n"));
    dirtyExternalScheduler.setSymbolAnalyzer(&dirtyExternalAnalyzer);
    dirtyExternalScheduler.setDocumentModel(&dirtyExternalDocuments);
    dirtyExternalDocuments.registerEditor(&dirtyExternalEditor,
                                          dirtyExternalPath);
    dirtyExternalEditor.setPlainText(
        QStringLiteral("module external_open; logic external_dirty_signal; endmodule\n"));
    QApplication::processEvents();
    dirtyExternalAnalyzer.analyzeFileContent(
        dirtyExternalPath,
        dirtyExternalEditor.toPlainText());
    int dirtyExternalAnalysisCount = 0;
    QObject::connect(&dirtyExternalAnalyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     [&](const QString& fileName, int) {
                         if (fileName == dirtyExternalPath)
                             ++dirtyExternalAnalysisCount;
                     });
    dirtyExternalScheduler.handleExternalFileChanged(dirtyExternalPath, 10);
    QEventLoop dirtyExternalLoop;
    QTimer::singleShot(250, &dirtyExternalLoop, &QEventLoop::quit);
    dirtyExternalLoop.exec();
    QApplication::processEvents();
    bool dirtyExternalSnapshotHasOpen = false;
    bool dirtyExternalSnapshotHasDisk = false;
    if (const auto snapshot = SemanticIndex::getInstance()->snapshot()) {
        for (const sym_list::SymbolInfo& symbol :
             snapshot->getSymbols(dirtyExternalPath)) {
            dirtyExternalSnapshotHasOpen = dirtyExternalSnapshotHasOpen
                || symbol.symbolName == QStringLiteral("external_open")
                || symbol.symbolName == QStringLiteral("external_dirty_signal");
            dirtyExternalSnapshotHasDisk = dirtyExternalSnapshotHasDisk
                || symbol.symbolName == QStringLiteral("external_disk")
                || symbol.symbolName == QStringLiteral("external_disk_signal");
        }
    }
    expectBool("external change skips dirty open document analysis",
               dirtyExternalAnalysisCount == 0,
               true);
    expectBool("external change preserves dirty open document symbols",
               dirtyExternalSnapshotHasOpen && !dirtyExternalSnapshotHasDisk,
               true);
    if (workspaceMergePreviousSnapshot)
        SemanticIndex::getInstance()->setSnapshot(workspaceMergePreviousSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();

    AnalysisScheduler projectCloseScheduler;
    ProjectModel projectCloseModel;
    SymbolRelationshipEngine projectCloseEngine;
    projectCloseEngine.addRelationship(2001,
                                       2002,
                                       SymbolRelationshipEngine::INSTANTIATES,
                                       QStringLiteral("close fixture"),
                                       100);
    projectCloseScheduler.setRelationshipEngine(&projectCloseEngine);
    projectCloseScheduler.setProjectModel(&projectCloseModel);
    int projectCloseRelationshipInvalidations = 0;
    int projectCloseRelationshipRefreshes = 0;
    int projectCloseDiagnosticsRefreshes = 0;
    QEventLoop projectCloseLoop;
    QObject::connect(&projectCloseScheduler,
                     &AnalysisScheduler::relationshipDataInvalidated,
                     &projectCloseLoop,
                     [&]() {
                         ++projectCloseRelationshipInvalidations;
                     });
    QObject::connect(&projectCloseScheduler,
                     &AnalysisScheduler::relationshipDataRefreshRequested,
                     &projectCloseLoop,
                     [&]() {
                         ++projectCloseRelationshipRefreshes;
                     });
    QObject::connect(&projectCloseScheduler,
                     &AnalysisScheduler::diagnosticsRefreshRequested,
                     &projectCloseLoop,
                     [&](const QString& fileName) {
                         if (fileName.isEmpty()) {
                             ++projectCloseDiagnosticsRefreshes;
                             projectCloseLoop.quit();
                         }
                     });
    projectCloseModel.setWorkspaceRoot(fixtureDir.absolutePath());
    projectCloseModel.closeProject();
    QTimer::singleShot(1000, &projectCloseLoop, &QEventLoop::quit);
    projectCloseLoop.exec();
    expectInt("scheduler clears relationships on project close",
              projectCloseEngine.getRelationshipCount(), 0);
    expectInt("scheduler invalidates relationship data on project close",
              projectCloseRelationshipInvalidations, 1);
    expectInt("scheduler refreshes relationship data on project close",
              projectCloseRelationshipRefreshes, 1);
    expectBool("scheduler refreshes diagnostics on project close",
               projectCloseDiagnosticsRefreshes > 0, true);

    const auto documentClosePreviousSnapshot = SemanticIndex::getInstance()->snapshot();
    AnalysisScheduler documentCloseScheduler;
    DocumentModel documentCloseModel;
    SymbolAnalyzer documentCloseAnalyzer;
    MyCodeEditor closedDocumentEditor;
    MyCodeEditor remainingDocumentEditor;
    const QString documentCloseClosedPath =
        fixtureDir.absoluteFilePath(QStringLiteral("document_close_closed.sv"));
    const QString documentCloseRemainingPath =
        fixtureDir.absoluteFilePath(QStringLiteral("document_close_remaining.sv"));
    const QString documentCloseClosedContent =
        QStringLiteral("module document_close_closed; endmodule\n");
    const QString documentCloseRemainingContent =
        QStringLiteral("module document_close_remaining; logic keep_signal; endmodule\n");
    closedDocumentEditor.setPlainText(documentCloseClosedContent);
    remainingDocumentEditor.setPlainText(documentCloseRemainingContent);
    documentCloseModel.registerEditor(&closedDocumentEditor,
                                      documentCloseClosedPath);
    documentCloseModel.registerEditor(&remainingDocumentEditor,
                                      documentCloseRemainingPath);

    bool requestedClosedDocumentContent = false;
    bool requestedRemainingDocumentContent = false;
    documentCloseScheduler.setOpenFileContentProvider(
        [&](const QString& fileName) -> QString {
            if (fileName == documentCloseClosedPath) {
                requestedClosedDocumentContent = true;
                return closedDocumentEditor.toPlainText();
            }
            if (fileName == documentCloseRemainingPath) {
                requestedRemainingDocumentContent = true;
                return remainingDocumentEditor.toPlainText();
            }
            return QString();
        });
    documentCloseScheduler.setSymbolAnalyzer(&documentCloseAnalyzer);
    documentCloseScheduler.setDocumentModel(&documentCloseModel);

    QString routedSymbolFile;
    int routedSymbolCount = -1;
    QObject::connect(&documentCloseScheduler,
                     &AnalysisScheduler::fileSymbolAnalysisFinished,
                     [&](const QString& fileName, int symbolCount) {
                         routedSymbolFile = fileName;
                         routedSymbolCount = symbolCount;
                     });
    QString routedProgressFile;
    int routedProgressDone = -1;
    int routedProgressTotal = -1;
    QObject::connect(&documentCloseScheduler,
                     &AnalysisScheduler::workspaceSymbolAnalysisProgress,
                     [&](const QString& fileName, int filesDone, int totalFiles) {
                         routedProgressFile = fileName;
                         routedProgressDone = filesDone;
                         routedProgressTotal = totalFiles;
                     });
    documentCloseAnalyzer.analysisCompleted(QStringLiteral("scheduler_route.sv"), 7);
    documentCloseAnalyzer.batchProgress(3, 5, QStringLiteral("scheduler_progress.sv"));
    expectBool("scheduler routes symbol analysis completion",
               routedSymbolFile == QStringLiteral("scheduler_route.sv")
                   && routedSymbolCount == 7,
               true);
    expectBool("scheduler routes symbol progress",
               routedProgressFile == QStringLiteral("scheduler_progress.sv")
                   && routedProgressDone == 3
                   && routedProgressTotal == 5,
               true);

    QString documentCloseAnalysisName;
    int documentCloseSymbols = -1;
    QObject::connect(&documentCloseAnalyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     [&](const QString& fileName, int symbolsFound) {
                         documentCloseAnalysisName = fileName;
                         documentCloseSymbols = symbolsFound;
                     });
    documentCloseModel.unregisterEditor(&closedDocumentEditor);
    expectBool("scheduler uses model text for remaining document on close",
               requestedRemainingDocumentContent, false);
    expectBool("scheduler skips closed document fallback content on close",
               requestedClosedDocumentContent, false);
    expectBool("scheduler reanalyzes open documents on close",
               documentCloseAnalysisName == QStringLiteral("open_tabs"), true);
    expectBool("scheduler reanalyzes remaining document symbols on close",
               documentCloseSymbols > 0, true);
    if (documentClosePreviousSnapshot)
        SemanticIndex::getInstance()->setSnapshot(documentClosePreviousSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();

    AnalysisScheduler documentSaveScheduler;
    SymbolAnalyzer documentSaveAnalyzer;
    SymbolRelationshipEngine documentSaveEngine;
    SmartRelationshipBuilder documentSaveBuilder(
        &documentSaveEngine,
        db,
        &slang);
    DocumentModel documentSaveModel;
    MyCodeEditor documentSaveEditor;
    const QString documentSavePath =
        fixtureDir.absoluteFilePath(QStringLiteral("document_save_package.sv"));
    documentSaveEditor.setPlainText(QStringLiteral(
        "package document_save_pkg;\n"
        "  parameter int P_SAVE = 1;\n"
        "endpackage\n"));
    documentSaveModel.registerEditor(&documentSaveEditor, documentSavePath);
    documentSaveScheduler.setSymbolAnalyzer(&documentSaveAnalyzer);
    documentSaveScheduler.setRelationshipEngine(&documentSaveEngine);
    documentSaveScheduler.setRelationshipBuilder(&documentSaveBuilder);
    documentSaveScheduler.setDocumentModel(&documentSaveModel);

    bool documentSaveSymbolsRefreshed = false;
    bool documentSaveRelationshipFinished = false;
    QEventLoop documentSaveLoop;
    QObject::connect(&documentSaveAnalyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     &documentSaveLoop,
                     [&](const QString& fileName, int symbolsFound) {
                         if (fileName == documentSavePath && symbolsFound > 0) {
                             documentSaveSymbolsRefreshed = true;
                             documentSaveLoop.quit();
                         }
                     });
    QObject::connect(&documentSaveScheduler,
                     &AnalysisScheduler::relationshipAnalysisFinished,
                     &documentSaveLoop,
                     [&](const SingleFileRelationshipAnalysisResult& result) {
                         if (result.fileName == documentSavePath)
                             documentSaveRelationshipFinished = true;
                     });
    documentSaveModel.markSaved(&documentSaveEditor);
    QTimer::singleShot(500, &documentSaveLoop, &QEventLoop::quit);
    documentSaveLoop.exec();
    QApplication::processEvents();
    expectBool("scheduler save refreshes symbols",
               documentSaveSymbolsRefreshed, true);
    expectBool("scheduler save skips relationship popup path",
               documentSaveRelationshipFinished, false);

    SymbolAnalyzer staleFileAnalyzer;
    const QString staleFilePath =
        fixtureDir.absoluteFilePath(QStringLiteral("stale_async_file.sv"));
    QString staleOldContent;
    for (int i = 0; i < 400; ++i) {
        staleOldContent += QStringLiteral(
                               "module stale_old_%1; logic old_signal_%1; endmodule\n")
                               .arg(i);
    }
    const QString staleNewContent =
        QStringLiteral("module stale_new; logic new_signal; endmodule\n");
    staleFileAnalyzer.analyzeFileContentAsync(staleFilePath, staleOldContent);
    staleFileAnalyzer.analyzeFileContentAsync(staleFilePath, staleNewContent);
    QEventLoop staleFileLoop;
    QObject::connect(&staleFileAnalyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     &staleFileLoop,
                     [&](const QString& fileName, int symbolsFound) {
                         if (fileName == staleFilePath && symbolsFound > 0) {
                             const auto snapshot = SemanticIndex::getInstance()->snapshot();
                             if (snapshot && !snapshot->getSymbols(staleFilePath).isEmpty())
                                 staleFileLoop.quit();
                         }
                     });
    QTimer::singleShot(3000, &staleFileLoop, &QEventLoop::quit);
    staleFileLoop.exec();
    QApplication::processEvents();
    bool staleSnapshotHasNew = false;
    bool staleSnapshotHasOld = false;
    if (const auto snapshot = SemanticIndex::getInstance()->snapshot()) {
        const QList<sym_list::SymbolInfo> symbols =
            snapshot->getSymbols(staleFilePath);
        for (const sym_list::SymbolInfo& symbol : symbols) {
            staleSnapshotHasNew = staleSnapshotHasNew
                || symbol.symbolName == QStringLiteral("stale_new")
                || symbol.symbolName == QStringLiteral("new_signal");
            staleSnapshotHasOld = staleSnapshotHasOld
                || symbol.symbolName.startsWith(QStringLiteral("stale_old_"))
                || symbol.symbolName.startsWith(QStringLiteral("old_signal_"));
        }
    }
    expectBool("stale async file analysis keeps latest symbols",
               staleSnapshotHasNew && !staleSnapshotHasOld,
               true);

    AnalysisScheduler refreshScheduler;
    SymbolRelationshipEngine refreshEngine;
    refreshScheduler.setRelationshipEngine(&refreshEngine);
    int relationshipInvalidations = 0;
    int relationshipRefreshes = 0;
    QEventLoop relationshipRefreshLoop;
    QObject::connect(&refreshScheduler,
                     &AnalysisScheduler::relationshipDataInvalidated,
                     &relationshipRefreshLoop,
                     [&]() {
                         ++relationshipInvalidations;
                     });
    QObject::connect(&refreshScheduler,
                     &AnalysisScheduler::relationshipDataRefreshRequested,
                     &relationshipRefreshLoop,
                     [&]() {
                         ++relationshipRefreshes;
                         relationshipRefreshLoop.quit();
                     });
    refreshEngine.addRelationship(1001,
                                  1002,
                                  SymbolRelationshipEngine::INSTANTIATES,
                                  QStringLiteral("fixture"),
                                  100);
    refreshEngine.addRelationship(1001,
                                  1003,
                                  SymbolRelationshipEngine::CALLS,
                                  QStringLiteral("fixture"),
                                  100);
    QTimer::singleShot(1500, &relationshipRefreshLoop, &QEventLoop::quit);
    relationshipRefreshLoop.exec();
    expectInt("scheduler invalidates relationship data on additions",
              relationshipInvalidations, 2);
    expectInt("scheduler coalesces relationship refresh requests",
              relationshipRefreshes, 1);
    relationshipRefreshes = 0;
    refreshEngine.clearAllRelationships();
    QApplication::processEvents();
    expectInt("scheduler refreshes relationship data on clear",
              relationshipRefreshes, 1);

    ProjectSnapshot schedulerProject;
    schedulerProject.workspaceRoot = fixtureDir.absolutePath();
    schedulerProject.allFiles = paths;
    schedulerProject.systemVerilogFiles = paths;
    WorkspaceRelationshipAnalysisResult schedulerResult;
    bool schedulerFinished = false;
    bool schedulerWorkspaceProgress = false;
    int schedulerLastProcessedFiles = 0;
    int schedulerProgressTotalFiles = 0;
    QEventLoop schedulerLoop;
    QObject::connect(&scheduler,
                     &AnalysisScheduler::workspaceRelationshipAnalysisProgress,
                     &schedulerLoop,
                     [&](const QString& fileName,
                         int relationshipsFound,
                         int processedFiles,
                         int totalFiles) {
                         Q_UNUSED(relationshipsFound)
                         schedulerWorkspaceProgress = schedulerWorkspaceProgress
                             || (paths.contains(fileName)
                                 && processedFiles > 0
                                 && totalFiles == paths.size());
                         schedulerLastProcessedFiles = processedFiles;
                         schedulerProgressTotalFiles = totalFiles;
                     });
    QObject::connect(&scheduler,
                     &AnalysisScheduler::workspaceRelationshipAnalysisFinished,
                     &schedulerLoop,
                     [&](const WorkspaceRelationshipAnalysisResult& result) {
                         schedulerResult = result;
                         schedulerFinished = true;
                         schedulerLoop.quit();
                     });
    QTimer::singleShot(5000, &schedulerLoop, &QEventLoop::quit);
    scheduler.requestWorkspaceRelationshipAnalysis(schedulerProject);
    schedulerLoop.exec();
    expectBool("scheduler workspace relationship finishes",
               schedulerFinished, true);
    expectBool("scheduler workspace reports progress",
               schedulerWorkspaceProgress, true);
    expectInt("scheduler workspace progress reaches total",
              schedulerLastProcessedFiles, static_cast<int>(paths.size()));
    expectInt("scheduler workspace progress total",
              schedulerProgressTotalFiles, static_cast<int>(paths.size()));
    expectInt("scheduler workspace result total files",
              schedulerResult.totalFiles, static_cast<int>(paths.size()));
    bool schedulerFoundStageRelationship = false;
    if (schedulerResult.semanticSnapshot) {
        const QList<SemanticRelationship> schedulerTopRelationships =
            schedulerResult.semanticSnapshot->getRelationships(topId, true);
        for (const SemanticRelationship& relationship : schedulerTopRelationships) {
            schedulerFoundStageRelationship = schedulerFoundStageRelationship
                || (relationship.toId == stageId
                    && relationship.type == SymbolRelationshipEngine::INSTANTIATES);
        }
    }
    expectBool("scheduler workspace snapshot merges relationships",
               schedulerFoundStageRelationship, true);
    expectBool("scheduler workspace applies timing relationship",
               engine.hasRelationship(topClkId,
                                      topId,
                                      SymbolRelationshipEngine::CLOCKS),
               true);

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
    expectInt("relationship report subject id",
              relationshipReport.subjectSymbolId, topId);
    expectBool("relationship report subject symbol",
               relationshipReport.subjectSymbol.symbolName == QStringLiteral("rel_top"),
               true);
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
    expectInt("relationship report direction group count",
              relationshipReport.directionGroups.size(), 1);
    expectBool("relationship report direction display name",
               !relationshipReport.directionGroups.isEmpty()
                   && relationshipReport.directionGroups.first().displayName
                       == QStringLiteral("Outgoing"),
               true);
    expectInt("relationship report type group count",
              relationshipReport.directionGroups.isEmpty()
                  ? 0
                  : relationshipReport.directionGroups.first().typeGroups.size(),
              relationshipReport.typeCounts.size());
    expectBool("relationship report type group display name",
               !relationshipReport.directionGroups.isEmpty()
                   && !relationshipReport.directionGroups.first().typeGroups.isEmpty()
                   && !relationshipReport.directionGroups.first()
                           .typeGroups.first()
                           .displayName.isEmpty(),
               true);
    expectInt("relationship report grouped total count",
              relationshipReport.directionGroups.isEmpty()
                  || relationshipReport.directionGroups.first().typeGroups.isEmpty()
                  ? 0
                  : relationshipReport.directionGroups.first().count,
              serviceRels.size());
    expectBool("relationship report keeps peer symbol",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().peerSymbol.symbolId == stageId,
               true);
    expectBool("relationship report explains relationship",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().explanation
                       == QStringLiteral("rel_top instantiates rel_stage"),
               true);
    expectBool("relationship report relationship display names",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().directionDisplayName
                       == QStringLiteral("Outgoing")
                   && !relationshipReport.relationships.first().typeDisplayName.isEmpty(),
               true);
    expectBool("relationship report subject role",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().subjectRole
                       == QStringLiteral("instantiator"),
               true);
    expectBool("relationship report peer role",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().peerRole
                       == QStringLiteral("instantiated"),
               true);
    RelationshipPanelQueryOptions outgoingPanelRelationshipOptions;
    outgoingPanelRelationshipOptions.symbolName = QStringLiteral("rel_top");
    outgoingPanelRelationshipOptions.fileName = topPath;
    outgoingPanelRelationshipOptions.direction = RelationshipPanelDirection::Outgoing;
    outgoingPanelRelationshipOptions.typeFilter =
        static_cast<int>(SymbolRelationshipEngine::CALLS);
    const RelationshipBrowseQuery outgoingPanelRelationshipQuery =
        relationshipService.queryForPanel(outgoingPanelRelationshipOptions);
    expectBool("relationship panel query selects outgoing type",
               outgoingPanelRelationshipQuery.symbolName == QStringLiteral("rel_top")
                   && outgoingPanelRelationshipQuery.includeOutgoing
                   && !outgoingPanelRelationshipQuery.includeIncoming
                   && outgoingPanelRelationshipQuery.types
                       == QList<SymbolRelationshipEngine::RelationType>{
                              SymbolRelationshipEngine::CALLS},
               true);
    expectInt("relationship panel outgoing count",
              relationshipService
                  .findRelationshipReport(outgoingPanelRelationshipQuery)
                  .totalCount,
              1);

    RelationshipPanelQueryOptions incomingPanelRelationshipOptions;
    incomingPanelRelationshipOptions.symbolName = QStringLiteral("rel_stage");
    incomingPanelRelationshipOptions.fileName = stagePath;
    incomingPanelRelationshipOptions.direction = RelationshipPanelDirection::Incoming;
    incomingPanelRelationshipOptions.typeFilter =
        static_cast<int>(SymbolRelationshipEngine::INSTANTIATES);
    const RelationshipBrowseQuery incomingPanelRelationshipQuery =
        relationshipService.queryForPanel(incomingPanelRelationshipOptions);
    expectBool("relationship panel query selects incoming type",
               !incomingPanelRelationshipQuery.includeOutgoing
                   && incomingPanelRelationshipQuery.includeIncoming
                   && incomingPanelRelationshipQuery.types
                       == QList<SymbolRelationshipEngine::RelationType>{
                              SymbolRelationshipEngine::INSTANTIATES},
               true);
    expectInt("relationship panel incoming count",
              relationshipService
                  .findRelationshipReport(incomingPanelRelationshipQuery)
                  .totalCount,
              1);

    RelationshipBrowseQuery callsOnlyBrowseQuery;
    callsOnlyBrowseQuery.symbolId = topId;
    callsOnlyBrowseQuery.includeOutgoing = true;
    callsOnlyBrowseQuery.includeIncoming = false;
    callsOnlyBrowseQuery.types = {SymbolRelationshipEngine::CALLS};
    const RelationshipReport callsOnlyReport =
        relationshipService.findRelationshipReport(callsOnlyBrowseQuery);
    expectInt("relationship report calls-only total",
              callsOnlyReport.totalCount, 1);
    expectInt("relationship report calls-only outgoing count",
              callsOnlyReport.outgoingCount, 1);
    expectInt("relationship report calls-only type count",
              callsOnlyReport.typeCounts.value(SymbolRelationshipEngine::CALLS), 1);
    expectInt("relationship report calls-only grouped count",
              callsOnlyReport.directionGroups.isEmpty()
                  || callsOnlyReport.directionGroups.first().typeGroups.isEmpty()
                  ? 0
                  : callsOnlyReport.directionGroups.first().typeGroups.first().relationships.size(),
              1);
    expectBool("relationship report calls-only peer symbol",
               !callsOnlyReport.relationships.isEmpty()
                   && callsOnlyReport.relationships.first().peerSymbol.symbolId == captureId,
               true);

    RelationshipBrowseQuery timingBrowseQuery;
    timingBrowseQuery.symbolId = topId;
    timingBrowseQuery.includeOutgoing = false;
    timingBrowseQuery.includeIncoming = true;
    timingBrowseQuery.types = {
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
    };
    const RelationshipReport timingReport =
        relationshipService.findRelationshipReport(timingBrowseQuery);
    expectInt("relationship report timing total",
              timingReport.totalCount, 2);
    expectInt("relationship report timing incoming count",
              timingReport.incomingCount, 2);
    expectInt("relationship report timing clock count",
              timingReport.typeCounts.value(SymbolRelationshipEngine::CLOCKS), 1);
    expectInt("relationship report timing reset count",
              timingReport.typeCounts.value(SymbolRelationshipEngine::RESETS), 1);
    expectInt("relationship report timing type group count",
              timingReport.directionGroups.isEmpty()
                  ? 0
                  : timingReport.directionGroups.first().typeGroups.size(),
              2);
    bool timingReportHasClockPeer = false;
    bool timingReportHasResetPeer = false;
    for (const DirectedRelationshipResult& relationship : timingReport.relationships) {
        timingReportHasClockPeer = timingReportHasClockPeer
            || (relationship.relationship.relationship.type == SymbolRelationshipEngine::CLOCKS
                && relationship.peerSymbol.symbolId == topClkId);
        timingReportHasResetPeer = timingReportHasResetPeer
            || (relationship.relationship.relationship.type == SymbolRelationshipEngine::RESETS
                && relationship.peerSymbol.symbolId == topRstId);
    }
    expectBool("relationship report timing clock peer",
               timingReportHasClockPeer, true);
    expectBool("relationship report timing reset peer",
               timingReportHasResetPeer, true);

    RelationshipBrowseQuery incomingStageBrowseQuery;
    incomingStageBrowseQuery.symbolId = stageId;
    incomingStageBrowseQuery.includeOutgoing = false;
    incomingStageBrowseQuery.includeIncoming = true;
    incomingStageBrowseQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const RelationshipReport incomingStageReport =
        relationshipService.findRelationshipReport(incomingStageBrowseQuery);
    expectInt("relationship report incoming-only total",
              incomingStageReport.totalCount, 1);
    expectInt("relationship report incoming-only count",
              incomingStageReport.incomingCount, 1);
    expectInt("relationship report incoming-only direction group",
              incomingStageReport.directionGroups.isEmpty()
                  ? -1
                  : incomingStageReport.directionGroups.first().direction,
              DirectedRelationshipResult::Incoming);
    expectBool("relationship report incoming-only direction display name",
               !incomingStageReport.directionGroups.isEmpty()
                   && incomingStageReport.directionGroups.first().displayName
                       == QStringLiteral("Incoming"),
               true);
    expectInt("relationship report incoming-only type count",
              incomingStageReport.typeCounts.value(SymbolRelationshipEngine::INSTANTIATES),
              1);
    expectInt("relationship report incoming-only type group",
              incomingStageReport.directionGroups.isEmpty()
                  || incomingStageReport.directionGroups.first().typeGroups.isEmpty()
                  ? -1
                  : incomingStageReport.directionGroups.first().typeGroups.first().type,
              SymbolRelationshipEngine::INSTANTIATES);
    expectBool("relationship report incoming-only type display name",
               !incomingStageReport.directionGroups.isEmpty()
                   && !incomingStageReport.directionGroups.first().typeGroups.isEmpty()
                   && incomingStageReport.directionGroups.first()
                          .typeGroups.first()
                          .displayName == QStringLiteral("Instantiates"),
               true);
    expectBool("relationship report incoming peer symbol",
               !incomingStageReport.relationships.isEmpty()
                   && incomingStageReport.relationships.first().peerSymbol.symbolId == topId,
               true);
    expectBool("relationship report incoming explanation",
               !incomingStageReport.relationships.isEmpty()
                   && incomingStageReport.relationships.first().explanation
                       == QStringLiteral("rel_top instantiates rel_stage"),
               true);
    expectBool("relationship report incoming subject role",
               !incomingStageReport.relationships.isEmpty()
                   && incomingStageReport.relationships.first().subjectRole
                       == QStringLiteral("instantiated"),
               true);
    expectBool("relationship report incoming peer role",
               !incomingStageReport.relationships.isEmpty()
                   && incomingStageReport.relationships.first().peerRole
                       == QStringLiteral("instantiator"),
               true);
    expectBool("relationship report incoming grouped peer symbol",
               !incomingStageReport.directionGroups.isEmpty()
                   && !incomingStageReport.directionGroups.first().typeGroups.isEmpty()
                   && !incomingStageReport.directionGroups.first()
                           .typeGroups.first()
                           .relationships.isEmpty()
                   && incomingStageReport.directionGroups.first()
                           .typeGroups.first()
                           .relationships.first()
                           .direction == DirectedRelationshipResult::Incoming
                   && incomingStageReport.directionGroups.first()
                           .typeGroups.first()
                           .relationships.first()
                           .peerSymbol.symbolId == topId,
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
    const QList<HierarchyNode> moduleInstantiationChildren =
        hierarchyService.moduleInstantiationChildren(topId);
    bool moduleInstantiationChildFoundStage = false;
    for (const HierarchyNode& node : moduleInstantiationChildren) {
        moduleInstantiationChildFoundStage =
            moduleInstantiationChildFoundStage
            || (node.symbol.symbolId == stageId
                && node.viaType == SymbolRelationshipEngine::INSTANTIATES);
    }
    expectBool("hierarchy service module instantiation children",
               moduleInstantiationChildFoundStage, true);
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
    expectBool("hierarchy report root direction display name",
               !hierarchyReport.rootDirectionGroups.isEmpty()
                   && hierarchyReport.rootDirectionGroups.first().displayName
                       == QStringLiteral("Outgoing"),
               true);
    bool hierarchyReportHasStageChild = false;
    for (const HierarchyNode& node : hierarchyReport.nodes) {
        hierarchyReportHasStageChild = hierarchyReportHasStageChild
            || (node.depth == 1
                && node.parentSymbolId == topId
                && node.symbol.symbolId == stageId
                && node.direction == HierarchyQuery::Children
                && node.viaType == SymbolRelationshipEngine::INSTANTIATES
                && node.directionDisplayName == QStringLiteral("Outgoing")
                && node.relationshipTypeDisplayName == QStringLiteral("Instantiates"));
    }
    expectBool("hierarchy report keeps child row identity",
               hierarchyReportHasStageChild, true);
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
    HierarchyPanelQueryOptions incomingHierarchyPanelOptions;
    incomingHierarchyPanelOptions.symbolName = QStringLiteral("rel_stage");
    incomingHierarchyPanelOptions.fileName = stagePath;
    incomingHierarchyPanelOptions.maxDepth = 1;
    incomingHierarchyPanelOptions.direction = HierarchyPanelDirection::Incoming;
    incomingHierarchyPanelOptions.typeFilter =
        static_cast<int>(SymbolRelationshipEngine::INSTANTIATES);
    const HierarchyQuery incomingHierarchyPanelQuery =
        hierarchyService.queryForPanel(incomingHierarchyPanelOptions);
    expectBool("hierarchy panel query selects incoming type",
               incomingHierarchyPanelQuery.symbolName == QStringLiteral("rel_stage")
                   && incomingHierarchyPanelQuery.direction == HierarchyQuery::Parents
                   && incomingHierarchyPanelQuery.maxDepth == 1
                   && incomingHierarchyPanelQuery.types
                       == QList<SymbolRelationshipEngine::RelationType>{
                              SymbolRelationshipEngine::INSTANTIATES},
               true);
    expectInt("hierarchy panel incoming count",
              hierarchyService
                  .getHierarchyReport(incomingHierarchyPanelQuery)
                  .rootDirectionCounts.value(HierarchyQuery::Parents),
              1);

    HierarchyPanelQueryOptions allHierarchyPanelOptions;
    allHierarchyPanelOptions.symbolName = QStringLiteral("rel_top");
    allHierarchyPanelOptions.fileName = topPath;
    allHierarchyPanelOptions.maxDepth = 2;
    allHierarchyPanelOptions.direction = HierarchyPanelDirection::All;
    allHierarchyPanelOptions.typeFilter = -1;
    const HierarchyQuery allHierarchyPanelQuery =
        hierarchyService.queryForPanel(allHierarchyPanelOptions);
    expectBool("hierarchy panel query selects all tree types",
               allHierarchyPanelQuery.direction == HierarchyQuery::Both
                   && allHierarchyPanelQuery.types.contains(SymbolRelationshipEngine::READS_FROM)
                   && allHierarchyPanelQuery.types.contains(SymbolRelationshipEngine::INSTANTIATES),
               true);

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
    expectInt("reference report subject id",
              stageReferenceReport.subjectSymbolId, stageId);
    expectBool("reference report subject symbol",
               stageReferenceReport.subjectSymbol.symbolName == QStringLiteral("rel_stage"),
               true);
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
    expectInt("reference report file group count",
              stageReferenceReport.fileGroups.size(), 1);
    expectInt("reference report type group count",
              stageReferenceReport.fileGroups.isEmpty()
                  ? 0
                  : stageReferenceReport.fileGroups.first().typeGroups.size(),
              1);
    expectBool("reference report type group display name",
               !stageReferenceReport.fileGroups.isEmpty()
                   && !stageReferenceReport.fileGroups.first().typeGroups.isEmpty()
                   && stageReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .displayName == QStringLiteral("Instantiates"),
               true);
    expectInt("reference report grouped result count",
              stageReferenceReport.fileGroups.isEmpty()
                  || stageReferenceReport.fileGroups.first().typeGroups.isEmpty()
                  ? 0
                  : stageReferenceReport.fileGroups.first().typeGroups.first().references.size(),
              1);
    expectBool("reference report grouped symbols",
               !stageReferenceReport.fileGroups.isEmpty()
                   && !stageReferenceReport.fileGroups.first().typeGroups.isEmpty()
                   && !stageReferenceReport.fileGroups.first()
                           .typeGroups.first()
                           .references.isEmpty()
                   && stageReferenceReport.fileGroups.first()
                           .typeGroups.first()
                           .references.first()
                           .referencingSymbol.symbolId == topId
                   && stageReferenceReport.fileGroups.first()
                           .typeGroups.first()
                           .references.first()
                           .referencedSymbol.symbolId == stageId,
               true);

    ReferenceQuery currentFileStageReferenceQuery = stageReferenceQuery;
    currentFileStageReferenceQuery.fileName = stagePath;
    currentFileStageReferenceQuery.currentFileOnly = true;
    expectInt("reference report current file filter",
              referenceService.findReferenceReport(currentFileStageReferenceQuery).totalCount, 0);
    currentFileStageReferenceQuery.fileName = topPath;
    expectInt("reference report current file keeps matching file",
              referenceService.findReferenceReport(currentFileStageReferenceQuery).totalCount, 1);

    ReferenceQuery workspaceStageReferenceQuery = stageReferenceQuery;
    workspaceStageReferenceQuery.workspaceFilesOnly = true;
    workspaceStageReferenceQuery.workspaceFiles = {topPath};
    expectInt("reference report workspace filter",
              referenceService.findReferenceReport(workspaceStageReferenceQuery).totalCount, 1);
    workspaceStageReferenceQuery.workspaceFiles = {stagePath};
    expectInt("reference report workspace filter hides other file",
              referenceService.findReferenceReport(workspaceStageReferenceQuery).totalCount, 0);

    ReferencePanelQueryOptions currentFileReferenceOptions;
    currentFileReferenceOptions.symbolName = QStringLiteral("rel_stage");
    currentFileReferenceOptions.fileName = topPath;
    currentFileReferenceOptions.scope = ReferencePanelScope::CurrentFile;
    currentFileReferenceOptions.typeFilter =
        static_cast<int>(SymbolRelationshipEngine::INSTANTIATES);
    const ReferenceQuery currentFileReferencePanelQuery =
        referenceService.queryForPanel(currentFileReferenceOptions);
    expectBool("reference panel query selects current file/type",
               currentFileReferencePanelQuery.currentFileOnly
                   && !currentFileReferencePanelQuery.workspaceFilesOnly
                   && currentFileReferencePanelQuery.fileName == topPath
                   && currentFileReferencePanelQuery.types
                       == QList<SymbolRelationshipEngine::RelationType>{
                              SymbolRelationshipEngine::INSTANTIATES},
               true);
    expectInt("reference panel current file count",
              referenceService
                  .findReferenceReport(currentFileReferencePanelQuery)
                  .totalCount,
              1);

    ReferencePanelQueryOptions workspaceReferenceOptions = currentFileReferenceOptions;
    workspaceReferenceOptions.scope = ReferencePanelScope::WorkspaceFiles;
    workspaceReferenceOptions.workspaceFiles = {stagePath};
    const ReferenceQuery workspaceReferencePanelQuery =
        referenceService.queryForPanel(workspaceReferenceOptions);
    expectBool("reference panel query selects workspace files",
               workspaceReferencePanelQuery.workspaceFilesOnly
                   && !workspaceReferencePanelQuery.currentFileOnly
                   && workspaceReferencePanelQuery.workspaceFiles == QStringList{stagePath},
               true);
    expectInt("reference panel workspace count",
              referenceService
                  .findReferenceReport(workspaceReferencePanelQuery)
                  .totalCount,
              0);

    ReferencePanelQueryOptions allReferenceOptions = currentFileReferenceOptions;
    allReferenceOptions.scope = ReferencePanelScope::AllFiles;
    allReferenceOptions.typeFilter = -1;
    const ReferenceQuery allReferencePanelQuery =
        referenceService.queryForPanel(allReferenceOptions);
    expectBool("reference panel query selects all refs",
               !allReferencePanelQuery.workspaceFilesOnly
                   && !allReferencePanelQuery.currentFileOnly
                   && allReferencePanelQuery.types.isEmpty(),
               true);

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
    const ReferenceReport reqValidReferenceReport =
        referenceService.findReferenceReport(reqValidReferenceQuery);
    expectInt("reference report condition read total",
              reqValidReferenceReport.totalCount, 1);
    expectInt("reference report condition read type count",
              reqValidReferenceReport.typeCounts.value(SymbolRelationshipEngine::READS_FROM), 1);
    expectInt("reference report condition read file count",
              reqValidReferenceReport.fileCounts.value(topPath), 1);
    expectInt("reference report condition read grouped count",
              reqValidReferenceReport.fileGroups.isEmpty()
                  || reqValidReferenceReport.fileGroups.first().typeGroups.isEmpty()
                  ? 0
                  : reqValidReferenceReport.fileGroups.first().typeGroups.first().references.size(),
              1);
    expectBool("reference report condition read subject symbol",
               reqValidReferenceReport.subjectSymbol.symbolName == QStringLiteral("req_valid"),
               true);

    ReferenceQuery topTimingReferenceQuery;
    topTimingReferenceQuery.symbolId = topId;
    topTimingReferenceQuery.types = {SymbolRelationshipEngine::CLOCKS};
    const ReferenceReport topClockReferenceReport =
        referenceService.findReferenceReport(topTimingReferenceQuery);
    expectInt("reference report clock total",
              topClockReferenceReport.totalCount, 1);
    expectInt("reference report clock type count",
              topClockReferenceReport.typeCounts.value(SymbolRelationshipEngine::CLOCKS), 1);
    expectInt("reference report clock file count",
              topClockReferenceReport.fileCounts.value(topPath), 1);
    expectInt("reference report clock grouped count",
              topClockReferenceReport.fileGroups.isEmpty()
                  || topClockReferenceReport.fileGroups.first().typeGroups.isEmpty()
                  ? 0
                  : topClockReferenceReport.fileGroups.first().typeGroups.first().references.size(),
              1);
    expectBool("reference report clock subject symbol",
               topClockReferenceReport.subjectSymbol.symbolName == QStringLiteral("rel_top"),
               true);
    expectBool("reference report clock referencing symbol",
               !topClockReferenceReport.references.isEmpty()
                   && topClockReferenceReport.references.first().referencingSymbol.symbolId == topClkId,
               true);

    topTimingReferenceQuery.types = {SymbolRelationshipEngine::RESETS};
    const ReferenceReport topResetReferenceReport =
        referenceService.findReferenceReport(topTimingReferenceQuery);
    expectInt("reference report reset total",
              topResetReferenceReport.totalCount, 1);
    expectInt("reference report reset type count",
              topResetReferenceReport.typeCounts.value(SymbolRelationshipEngine::RESETS), 1);
    expectInt("reference report reset file count",
              topResetReferenceReport.fileCounts.value(topPath), 1);
    expectInt("reference report reset grouped count",
              topResetReferenceReport.fileGroups.isEmpty()
                  || topResetReferenceReport.fileGroups.first().typeGroups.isEmpty()
                  ? 0
                  : topResetReferenceReport.fileGroups.first().typeGroups.first().references.size(),
              1);
    expectBool("reference report reset subject symbol",
               topResetReferenceReport.subjectSymbol.symbolName == QStringLiteral("rel_top"),
               true);
    expectBool("reference report reset referencing symbol",
               !topResetReferenceReport.references.isEmpty()
                   && topResetReferenceReport.references.first().referencingSymbol.symbolId == topRstId,
               true);

    topTimingReferenceQuery.types.clear();
    const ReferenceReport topDefaultReferenceReport =
        referenceService.findReferenceReport(topTimingReferenceQuery);
    expectBool("reference report default includes timing total",
               topDefaultReferenceReport.totalCount >= 2, true);
    expectInt("reference report default clock count",
              topDefaultReferenceReport.typeCounts.value(SymbolRelationshipEngine::CLOCKS), 1);
    expectInt("reference report default reset count",
              topDefaultReferenceReport.typeCounts.value(SymbolRelationshipEngine::RESETS), 1);
    expectBool("reference report default includes timing file count",
               topDefaultReferenceReport.fileCounts.value(topPath) >= 2, true);
    bool defaultHasClockGroup = false;
    bool defaultHasResetGroup = false;
    for (const ReferenceFileGroup& fileGroup : topDefaultReferenceReport.fileGroups) {
        if (fileGroup.fileKey != topPath)
            continue;
        for (const ReferenceTypeGroup& typeGroup : fileGroup.typeGroups) {
            defaultHasClockGroup = defaultHasClockGroup
                || typeGroup.type == SymbolRelationshipEngine::CLOCKS;
            defaultHasResetGroup = defaultHasResetGroup
                || typeGroup.type == SymbolRelationshipEngine::RESETS;
        }
    }
    expectBool("reference report default clock group",
               defaultHasClockGroup, true);
    expectBool("reference report default reset group",
               defaultHasResetGroup, true);

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
    const ReferenceReport rspDataReferenceReport =
        referenceService.findReferenceReport(rspDataReferenceQuery);
    expectInt("reference report assignment write total",
              rspDataReferenceReport.totalCount, 1);
    expectInt("reference report assignment write type count",
              rspDataReferenceReport.typeCounts.value(SymbolRelationshipEngine::ASSIGNS_TO), 1);
    expectInt("reference report assignment write file count",
              rspDataReferenceReport.fileCounts.value(topPath), 1);
    expectInt("reference report assignment write grouped count",
              rspDataReferenceReport.fileGroups.isEmpty()
                  || rspDataReferenceReport.fileGroups.first().typeGroups.isEmpty()
                  ? 0
                  : rspDataReferenceReport.fileGroups.first().typeGroups.first().references.size(),
              1);
    expectBool("reference report assignment write subject symbol",
               rspDataReferenceReport.subjectSymbol.symbolName == QStringLiteral("rsp_data"),
               true);
}

static void runModuleBriefServiceFixture()
{
    printf("\n-- module brief service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/module_brief_fixture.sv");
    QList<sym_list::SymbolInfo> symbols;
    sym_list::SymbolInfo module = makeModuleBriefSymbol(
        9001,
        fileName,
        QStringLiteral("brief_top"),
        sym_list::sym_module,
        10);
    module.endLine = 80;
    symbols.append(module);
    symbols.append(makeModuleBriefSymbol(
        9002,
        fileName,
        QStringLiteral("WIDTH"),
        sym_list::sym_parameter,
        11,
        QStringLiteral("brief_top")));
    symbols.append(makeModuleBriefSymbol(
        9003,
        fileName,
        QStringLiteral("clk"),
        sym_list::sym_port_input,
        12,
        QStringLiteral("brief_top")));
    symbols.append(makeModuleBriefSymbol(
        9004,
        fileName,
        QStringLiteral("rst_n"),
        sym_list::sym_port_input,
        13,
        QStringLiteral("brief_top")));
    symbols.append(makeModuleBriefSymbol(
        9005,
        fileName,
        QStringLiteral("data_o"),
        sym_list::sym_port_output,
        14,
        QStringLiteral("brief_top")));
    symbols.append(makeModuleBriefSymbol(
        9006,
        fileName,
        QStringLiteral("u_stage"),
        sym_list::sym_inst,
        30,
        QStringLiteral("brief_top")));
    symbols.append(makeModuleBriefSymbol(
        9007,
        fileName,
        QStringLiteral("brief_pkg"),
        sym_list::sym_package,
        1));
    symbols.append(makeModuleBriefSymbol(
        9008,
        fileName,
        QStringLiteral("outside_port"),
        sym_list::sym_port_input,
        90,
        QStringLiteral("other_module")));

    QList<SemanticRelationship> relationships;
    SemanticRelationship importRel;
    importRel.fromId = 9001;
    importRel.toId = 9007;
    importRel.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(importRel);

    SemanticRelationship instRel;
    instRel.fromId = 9001;
    instRel.toId = 9006;
    instRel.type = SymbolRelationshipEngine::INSTANTIATES;
    relationships.append(instRel);

    SemanticRelationship clockRel;
    clockRel.fromId = 9003;
    clockRel.toId = 9001;
    clockRel.type = SymbolRelationshipEngine::CLOCKS;
    relationships.append(clockRel);

    QList<SemanticDiagnostic> diagnostics;
    SemanticDiagnostic moduleDiagnostic;
    moduleDiagnostic.fileName = fileName;
    moduleDiagnostic.line = 32;
    moduleDiagnostic.column = 5;
    moduleDiagnostic.severity = SemanticDiagnostic::Warning;
    moduleDiagnostic.message = QStringLiteral("width truncation");
    diagnostics.append(moduleDiagnostic);

    SemanticDiagnostic outsideDiagnostic;
    outsideDiagnostic.fileName = fileName;
    outsideDiagnostic.line = 100;
    outsideDiagnostic.column = 1;
    outsideDiagnostic.severity = SemanticDiagnostic::Error;
    outsideDiagnostic.message = QStringLiteral("outside module");
    diagnostics.append(outsideDiagnostic);

    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        symbols,
        relationships,
        diagnostics));
    ModuleBriefService service(&index);

    ModuleBriefQuery query;
    query.moduleName = QStringLiteral("brief_top");
    query.fileName = fileName;
    const ModuleBriefReport report = service.buildModuleBrief(query);

    expectBool("module brief found module", report.found, true);
    expectBool("module brief subject name",
               report.moduleSymbol.symbolName == QStringLiteral("brief_top"), true);
    const SymbolTaxonomy::SemanticMetadata moduleMetadata =
        SymbolTaxonomy::semanticMetadata(report.moduleSymbol);
    expectBool("semantic metadata keeps raw module kind",
               moduleMetadata.rawCollectorKind == sym_list::sym_module,
               true);
    expectBool("semantic metadata classifies module declaration",
               moduleMetadata.declarationKind
                   == SymbolTaxonomy::DeclarationKind::Module,
               true);
    expectBool("semantic metadata marks module as global",
               moduleMetadata.ownerScope == SymbolTaxonomy::SymbolOwnerScope::Global
                   && moduleMetadata.visibility == SymbolTaxonomy::SymbolVisibility::Global,
               true);
    expectBool("taxonomy recognizes module brief port",
               SymbolTaxonomy::isPortDeclaration(symbols.at(2).symbolType),
               true);
    const SymbolTaxonomy::SemanticMetadata portMetadata =
        SymbolTaxonomy::semanticMetadata(symbols.at(2));
    expectBool("semantic metadata classifies port declaration",
               portMetadata.declarationKind
                   == SymbolTaxonomy::DeclarationKind::Port,
               true);
    expectBool("semantic metadata keeps port as declaration usage",
               portMetadata.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration,
               true);
    expectBool("semantic metadata classifies design source",
               portMetadata.sourceRole == SymbolTaxonomy::SourceRole::DesignSource,
               true);
    expectBool("taxonomy groups module brief port",
               SymbolTaxonomy::declarationGroup(symbols.at(2).symbolType)
                   == SymbolTaxonomy::DeclarationGroup::Port,
               true);
    expectBool("taxonomy recognizes module brief parameter",
               SymbolTaxonomy::isParameterDeclaration(symbols.at(1).symbolType),
               true);
    expectBool("taxonomy groups module brief parameter",
               SymbolTaxonomy::declarationGroup(symbols.at(1).symbolType)
                   == SymbolTaxonomy::DeclarationGroup::Parameter,
               true);
    expectBool("taxonomy recognizes module brief instance",
               SymbolTaxonomy::isInstanceDeclaration(symbols.at(5).symbolType),
               true);
    expectBool("taxonomy groups module brief instance",
               SymbolTaxonomy::declarationGroup(symbols.at(5).symbolType)
                   == SymbolTaxonomy::DeclarationGroup::Instance,
               true);
    expectInt("module brief port count", report.ports.size(), 3);
    expectInt("module brief parameter count", report.parameters.size(), 1);
    expectInt("module brief instance count", report.instances.size(), 1);
    expectInt("module brief import count", report.imports.size(), 1);
    expectInt("module brief diagnostic count", report.diagnostics.size(), 1);
    expectBool("module brief import package",
               !report.imports.isEmpty()
                   && report.imports.first().symbolName == QStringLiteral("brief_pkg"),
               true);
    expectInt("module brief outgoing relationships",
              report.relationshipSummary.outgoingCount, 2);
    expectInt("module brief incoming relationships",
              report.relationshipSummary.incomingCount, 1);
    expectInt("module brief instantiates count",
              report.relationshipSummary.outgoingTypeCounts.value(
                  SymbolRelationshipEngine::INSTANTIATES),
              1);
    expectInt("module brief clocks count",
              report.relationshipSummary.incomingTypeCounts.value(
                  SymbolRelationshipEngine::CLOCKS),
              1);
}

static void runSignalJourneyServiceFixture()
{
    printf("\n-- signal journey service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/signal_journey_fixture.sv");
    QList<sym_list::SymbolInfo> symbols;
    sym_list::SymbolInfo module = makeModuleBriefSymbol(
        9101,
        fileName,
        QStringLiteral("journey_top"),
        sym_list::sym_module,
        1);
    module.endLine = 80;
    symbols.append(module);
    symbols.append(makeModuleBriefSymbol(
        9102,
        fileName,
        QStringLiteral("data_q"),
        sym_list::sym_logic,
        10,
        QStringLiteral("journey_top")));
    symbols.append(makeModuleBriefSymbol(
        9103,
        fileName,
        QStringLiteral("next_data"),
        sym_list::sym_logic,
        20,
        QStringLiteral("journey_top")));
    symbols.append(makeModuleBriefSymbol(
        9104,
        fileName,
        QStringLiteral("consumer"),
        sym_list::sym_always_ff,
        30,
        QStringLiteral("journey_top")));
    symbols.append(makeModuleBriefSymbol(
        9105,
        fileName,
        QStringLiteral("u_stage.data_i"),
        sym_list::sym_inst_pin,
        40,
        QStringLiteral("journey_top")));

    QList<SemanticRelationship> relationships;
    SemanticRelationship assignment;
    assignment.fromId = 9103;
    assignment.toId = 9102;
    assignment.type = SymbolRelationshipEngine::ASSIGNS_TO;
    relationships.append(assignment);

    SemanticRelationship read;
    read.fromId = 9104;
    read.toId = 9102;
    read.type = SymbolRelationshipEngine::READS_FROM;
    relationships.append(read);

    SemanticRelationship portConnection;
    portConnection.fromId = 9105;
    portConnection.toId = 9102;
    portConnection.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(portConnection);

    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        symbols,
        relationships,
        QList<SemanticDiagnostic>()));
    SignalJourneyService service(&index);

    SignalJourneyQuery query;
    query.signalName = QStringLiteral("data_q");
    query.fileName = fileName;
    query.moduleName = QStringLiteral("journey_top");
    const SignalJourneyReport report = service.buildSignalJourney(query);

    expectBool("signal journey found declaration", report.found, true);
    expectBool("signal journey declaration name",
               report.declaration.symbolName == QStringLiteral("data_q"), true);
    expectBool("signal journey declaration display type",
               !report.declarationTypeDisplayName.isEmpty(), true);
    expectBool("taxonomy recognizes signal journey declaration",
               SymbolTaxonomy::isSignalDeclaration(report.declaration.symbolType),
               true);
    expectBool("taxonomy recognizes signal journey port peer",
               SymbolTaxonomy::isPortConnectionPeer(symbols.at(4).symbolType),
               true);
    expectInt("signal journey assignment count", report.assignments.size(), 1);
    expectInt("signal journey read count", report.reads.size(), 1);
    expectInt("signal journey port connection count",
              report.portConnections.size(), 1);
    expectBool("signal journey assignment peer",
               !report.assignments.isEmpty()
                   && report.assignments.first().peerSymbol.symbolName
                       == QStringLiteral("next_data")
                   && report.assignments.first().directionDisplayName
                       == QStringLiteral("incoming")
                   && report.assignments.first().relationshipTypeDisplayName
                       == QStringLiteral("Assigns To")
                   && report.assignments.first().detailDisplayName
                       == QStringLiteral("incoming Assigns To"),
               true);
    expectBool("signal journey read peer",
               !report.reads.isEmpty()
                   && report.reads.first().peerSymbol.symbolName
                       == QStringLiteral("consumer")
                   && report.reads.first().detailDisplayName
                       == QStringLiteral("incoming Reads From"),
               true);
    expectBool("signal journey port peer",
               !report.portConnections.isEmpty()
                   && report.portConnections.first().peerSymbol.symbolName
                       == QStringLiteral("u_stage.data_i")
                   && !report.portConnections.first().detailDisplayName.isEmpty(),
               true);
}

static void runClockResetDomainServiceFixture()
{
    printf("\n-- clock reset domain service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/clock_reset_domain_fixture.sv");
    QList<sym_list::SymbolInfo> symbols;
    sym_list::SymbolInfo top = makeModuleBriefSymbol(
        9201,
        fileName,
        QStringLiteral("domain_top"),
        sym_list::sym_module,
        1);
    top.endLine = 80;
    symbols.append(top);
    symbols.append(makeModuleBriefSymbol(
        9202,
        fileName,
        QStringLiteral("other_domain"),
        sym_list::sym_module,
        90));
    symbols.append(makeModuleBriefSymbol(
        9203,
        fileName,
        QStringLiteral("clk_i"),
        sym_list::sym_port_input,
        10,
        QStringLiteral("domain_top")));
    symbols.append(makeModuleBriefSymbol(
        9204,
        fileName,
        QStringLiteral("rst_ni"),
        sym_list::sym_port_input,
        11,
        QStringLiteral("domain_top")));
    symbols.append(makeModuleBriefSymbol(
        9205,
        fileName,
        QStringLiteral("other_clk"),
        sym_list::sym_port_input,
        95,
        QStringLiteral("other_domain")));

    QList<SemanticRelationship> relationships;
    SemanticRelationship topClock;
    topClock.fromId = 9203;
    topClock.toId = 9201;
    topClock.type = SymbolRelationshipEngine::CLOCKS;
    relationships.append(topClock);

    SemanticRelationship topReset;
    topReset.fromId = 9204;
    topReset.toId = 9201;
    topReset.type = SymbolRelationshipEngine::RESETS;
    relationships.append(topReset);

    SemanticRelationship otherClock;
    otherClock.fromId = 9205;
    otherClock.toId = 9202;
    otherClock.type = SymbolRelationshipEngine::CLOCKS;
    relationships.append(otherClock);

    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        symbols,
        relationships,
        QList<SemanticDiagnostic>()));
    ClockResetDomainService service(&index);

    const ClockResetDomainReport allReport =
        service.buildClockResetDomainMap();
    expectBool("clock reset all found", allReport.found, true);
    expectInt("clock reset all clock domains", allReport.clockDomains.size(), 2);
    expectInt("clock reset all reset domains", allReport.resetDomains.size(), 1);
    expectInt("clock reset all clock count",
              allReport.clockRelationshipCount, 2);
    expectInt("clock reset all reset count",
              allReport.resetRelationshipCount, 1);

    ClockResetDomainQuery topQuery;
    topQuery.moduleName = QStringLiteral("domain_top");
    topQuery.fileName = fileName;
    const ClockResetDomainReport topReport =
        service.buildClockResetDomainMap(topQuery);

    expectBool("clock reset top found", topReport.found, true);
    expectInt("clock reset top clock domains", topReport.clockDomains.size(), 1);
    expectInt("clock reset top reset domains", topReport.resetDomains.size(), 1);
    expectBool("clock reset top clock signal",
               !topReport.clockDomains.isEmpty()
                   && topReport.clockDomains.first().domainSignal.symbolName
                       == QStringLiteral("clk_i"),
               true);
    expectBool("clock reset top reset signal",
               !topReport.resetDomains.isEmpty()
                   && topReport.resetDomains.first().domainSignal.symbolName
                       == QStringLiteral("rst_ni"),
               true);
    expectBool("clock reset top clock target",
               !topReport.clockDomains.isEmpty()
                   && !topReport.clockDomains.first().modules.isEmpty()
                   && topReport.clockDomains.first().modules.first()
                       .moduleSymbol.symbolName == QStringLiteral("domain_top"),
               true);

    ClockResetDomainQuery idQuery;
    idQuery.moduleSymbolId = 9202;
    const ClockResetDomainReport otherReport =
        service.buildClockResetDomainMap(idQuery);
    expectInt("clock reset id clock domains", otherReport.clockDomains.size(), 1);
    expectBool("clock reset id clock signal",
               !otherReport.clockDomains.isEmpty()
                   && otherReport.clockDomains.first().domainSignal.symbolName
                       == QStringLiteral("other_clk"),
               true);
    expectInt("clock reset id reset domains", otherReport.resetDomains.size(), 0);
}

static void runFsmGraphServiceFixture()
{
    printf("\n-- fsm graph service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/fsm_graph_fixture.sv");
    const QString content = QStringLiteral(
        "module fsm_top(input logic clk, input logic rst_n, input logic start, output logic done);\n"
        "  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;\n"
        "  state_t state_q;\n"
        "  state_t state_d;\n"
        "  always_comb begin\n"
        "    state_d = state_q;\n"
        "    case (state_q)\n"
        "      IDLE: begin\n"
        "        if (start) state_d = RUN;\n"
        "      end\n"
        "      RUN: state_d = DONE;\n"
        "      DONE: if (!start) state_d = IDLE;\n"
        "      default: state_d = IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");

    QList<sym_list::SymbolInfo> symbols;
    sym_list::SymbolInfo module = makeModuleBriefSymbol(
        9301,
        fileName,
        QStringLiteral("fsm_top"),
        sym_list::sym_module,
        1);
    module.endLine = 16;
    symbols.append(module);

    sym_list::SymbolInfo stateQ = makeModuleBriefSymbol(
        9302,
        fileName,
        QStringLiteral("state_q"),
        sym_list::sym_enum_var,
        3,
        QStringLiteral("fsm_top"));
    stateQ.dataType = QStringLiteral("state_t");
    symbols.append(stateQ);

    sym_list::SymbolInfo stateD = makeModuleBriefSymbol(
        9303,
        fileName,
        QStringLiteral("state_d"),
        sym_list::sym_enum_var,
        4,
        QStringLiteral("fsm_top"));
    stateD.dataType = QStringLiteral("state_t");
    symbols.append(stateD);

    sym_list::SymbolInfo idle = makeModuleBriefSymbol(
        9304,
        fileName,
        QStringLiteral("IDLE"),
        sym_list::sym_enum_value,
        2,
        QStringLiteral("fsm_top"));
    idle.dataType = QStringLiteral("state_t");
    symbols.append(idle);

    sym_list::SymbolInfo run = makeModuleBriefSymbol(
        9305,
        fileName,
        QStringLiteral("RUN"),
        sym_list::sym_enum_value,
        2,
        QStringLiteral("fsm_top"));
    run.dataType = QStringLiteral("state_t");
    symbols.append(run);

    sym_list::SymbolInfo done = makeModuleBriefSymbol(
        9306,
        fileName,
        QStringLiteral("DONE"),
        sym_list::sym_enum_value,
        2,
        QStringLiteral("fsm_top"));
    done.dataType = QStringLiteral("state_t");
    symbols.append(done);

    QHash<QString, QString> fileContents;
    fileContents.insert(fileName, content);
    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        symbols,
        QList<SemanticRelationship>(),
        QList<SemanticDiagnostic>(),
        fileContents));
    FsmGraphService service(&index);

    FsmGraphQuery query;
    query.moduleName = QStringLiteral("fsm_top");
    query.fileName = fileName;
    const FsmGraphReport report = service.buildFsmGraph(query);

    expectBool("fsm graph found", report.found, true);
    expectBool("taxonomy recognizes fsm state register",
               SymbolTaxonomy::isFsmStateRegisterDeclaration(stateQ.symbolType),
               true);
    expectBool("taxonomy recognizes fsm state value",
               SymbolTaxonomy::isFsmStateValueDeclaration(idle.symbolType),
               true);
    expectInt("fsm graph count", report.graphs.size(), 1);
    expectBool("fsm graph state register",
               !report.graphs.isEmpty()
                   && report.graphs.first().stateRegister.symbolName
                       == QStringLiteral("state_q"),
               true);
    expectBool("fsm graph next state",
               !report.graphs.isEmpty()
                   && report.graphs.first().nextStateSignal.symbolName
                       == QStringLiteral("state_d"),
               true);
    expectInt("fsm graph state count",
              report.graphs.isEmpty() ? 0 : report.graphs.first().states.size(),
              3);
    expectInt("fsm graph transition count",
              report.graphs.isEmpty() ? 0 : report.graphs.first().transitions.size(),
              4);
    expectBool("fsm graph first transition",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitions.isEmpty()
                   && report.graphs.first().transitions.first().fromState
                       == QStringLiteral("IDLE")
                   && report.graphs.first().transitions.first().toState
                       == QStringLiteral("RUN")
                   && report.graphs.first().transitions.first().condition
                       == QStringLiteral("start"),
               true);
    expectBool("fsm graph default transition",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitions.isEmpty()
                   && report.graphs.first().transitions.last().fromState
                       == QStringLiteral("default")
                   && report.graphs.first().transitions.last().toState
                       == QStringLiteral("IDLE"),
               true);
}

static void runSemanticDiffServiceFixture()
{
    printf("\n-- semantic diff service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/semantic_diff_fixture.sv");

    QList<sym_list::SymbolInfo> beforeSymbols;
    beforeSymbols.append(makeModuleBriefSymbol(
        9401,
        fileName,
        QStringLiteral("diff_top"),
        sym_list::sym_module,
        1));
    beforeSymbols.append(makeModuleBriefSymbol(
        9402,
        fileName,
        QStringLiteral("clk"),
        sym_list::sym_port_input,
        2,
        QStringLiteral("diff_top")));
    beforeSymbols.append(makeModuleBriefSymbol(
        9403,
        fileName,
        QStringLiteral("data"),
        sym_list::sym_port_input,
        3,
        QStringLiteral("diff_top")));
    beforeSymbols.append(makeModuleBriefSymbol(
        9404,
        fileName,
        QStringLiteral("OLD_PARAM"),
        sym_list::sym_parameter,
        4,
        QStringLiteral("diff_top")));
    beforeSymbols.append(makeModuleBriefSymbol(
        9405,
        fileName,
        QStringLiteral("u_old"),
        sym_list::sym_inst,
        10,
        QStringLiteral("diff_top")));
    beforeSymbols.append(makeModuleBriefSymbol(
        9406,
        fileName,
        QStringLiteral("stale_q"),
        sym_list::sym_logic,
        12,
        QStringLiteral("diff_top")));

    QList<sym_list::SymbolInfo> afterSymbols;
    afterSymbols.append(makeModuleBriefSymbol(
        9501,
        fileName,
        QStringLiteral("diff_top"),
        sym_list::sym_module,
        1));
    afterSymbols.append(makeModuleBriefSymbol(
        9502,
        fileName,
        QStringLiteral("clk"),
        sym_list::sym_port_input,
        2,
        QStringLiteral("diff_top")));
    afterSymbols.append(makeModuleBriefSymbol(
        9503,
        fileName,
        QStringLiteral("data"),
        sym_list::sym_port_output,
        3,
        QStringLiteral("diff_top")));
    afterSymbols.append(makeModuleBriefSymbol(
        9504,
        fileName,
        QStringLiteral("DEPTH"),
        sym_list::sym_parameter,
        4,
        QStringLiteral("diff_top")));
    afterSymbols.append(makeModuleBriefSymbol(
        9505,
        fileName,
        QStringLiteral("u_new"),
        sym_list::sym_inst,
        10,
        QStringLiteral("diff_top")));
    afterSymbols.append(makeModuleBriefSymbol(
        9506,
        fileName,
        QStringLiteral("state_q"),
        sym_list::sym_logic,
        12,
        QStringLiteral("diff_top")));

    QList<SemanticRelationship> beforeRelationships;
    SemanticRelationship beforeInst;
    beforeInst.fromId = 9401;
    beforeInst.toId = 9405;
    beforeInst.type = SymbolRelationshipEngine::INSTANTIATES;
    beforeRelationships.append(beforeInst);

    QList<SemanticRelationship> afterRelationships;
    SemanticRelationship afterInst;
    afterInst.fromId = 9501;
    afterInst.toId = 9505;
    afterInst.type = SymbolRelationshipEngine::INSTANTIATES;
    afterRelationships.append(afterInst);

    QList<SemanticDiagnostic> beforeDiagnostics;
    SemanticDiagnostic beforeDiagnostic;
    beforeDiagnostic.fileName = fileName;
    beforeDiagnostic.line = 20;
    beforeDiagnostic.column = 5;
    beforeDiagnostic.severity = SemanticDiagnostic::Warning;
    beforeDiagnostic.message = QStringLiteral("old warning");
    beforeDiagnostics.append(beforeDiagnostic);

    QList<SemanticDiagnostic> afterDiagnostics;
    SemanticDiagnostic afterDiagnostic;
    afterDiagnostic.fileName = fileName;
    afterDiagnostic.line = 22;
    afterDiagnostic.column = 7;
    afterDiagnostic.severity = SemanticDiagnostic::Error;
    afterDiagnostic.message = QStringLiteral("new error");
    afterDiagnostics.append(afterDiagnostic);

    auto beforeSnapshot = std::make_shared<SemanticIndexSnapshot>(
        beforeSymbols,
        beforeRelationships,
        beforeDiagnostics);
    auto afterSnapshot = std::make_shared<SemanticIndexSnapshot>(
        afterSymbols,
        afterRelationships,
        afterDiagnostics);

    SemanticDiffQuery query;
    query.beforeSnapshot = beforeSnapshot;
    query.afterSnapshot = afterSnapshot;
    query.moduleName = QStringLiteral("diff_top");
    query.beforeFileName = fileName;
    query.afterFileName = fileName;

    SemanticDiffService service;
    const SemanticDiffReport report = service.buildSemanticDiff(query);

    int addedSymbols = 0;
    int removedSymbols = 0;
    int modifiedSymbols = 0;
    bool dataPortModified = false;
    bool newSignalAdded = false;
    for (const SemanticDiffSymbolChange& change : report.symbolChanges) {
        if (change.kind == SemanticDiffChangeKind::Added)
            ++addedSymbols;
        if (change.kind == SemanticDiffChangeKind::Removed)
            ++removedSymbols;
        if (change.kind == SemanticDiffChangeKind::Modified)
            ++modifiedSymbols;
        if (change.kind == SemanticDiffChangeKind::Modified
            && change.category == SemanticDiffSymbolCategory::Port
            && change.beforeSymbol.symbolName == QStringLiteral("data")
            && change.beforeSymbol.symbolType == sym_list::sym_port_input
            && change.afterSymbol.symbolType == sym_list::sym_port_output) {
            dataPortModified = true;
        }
        if (change.kind == SemanticDiffChangeKind::Added
            && change.category == SemanticDiffSymbolCategory::Signal
            && change.afterSymbol.symbolName == QStringLiteral("state_q")) {
            newSignalAdded = true;
        }
    }

    int addedRelationships = 0;
    int removedRelationships = 0;
    bool addedRelationshipHasEndpoints = false;
    bool removedRelationshipHasEndpoints = false;
    for (const SemanticDiffRelationshipChange& change : report.relationshipChanges) {
        if (change.kind == SemanticDiffChangeKind::Added) {
            ++addedRelationships;
            addedRelationshipHasEndpoints =
                change.afterFromSymbol.symbolName == QStringLiteral("diff_top")
                && change.afterToSymbol.symbolName == QStringLiteral("u_new");
        }
        if (change.kind == SemanticDiffChangeKind::Removed) {
            ++removedRelationships;
            removedRelationshipHasEndpoints =
                change.beforeFromSymbol.symbolName == QStringLiteral("diff_top")
                && change.beforeToSymbol.symbolName == QStringLiteral("u_old");
        }
    }

    int addedDiagnostics = 0;
    int removedDiagnostics = 0;
    for (const SemanticDiffDiagnosticChange& change : report.diagnosticChanges) {
        if (change.kind == SemanticDiffChangeKind::Added)
            ++addedDiagnostics;
        if (change.kind == SemanticDiffChangeKind::Removed)
            ++removedDiagnostics;
    }

    expectBool("semantic diff found", report.found, true);
    expectInt("semantic diff added symbols", addedSymbols, 3);
    expectInt("semantic diff removed symbols", removedSymbols, 3);
    expectInt("semantic diff modified symbols", modifiedSymbols, 1);
    expectBool("semantic diff modified data port", dataPortModified, true);
    expectBool("semantic diff added state signal", newSignalAdded, true);
    expectInt("semantic diff added relationships", addedRelationships, 1);
    expectInt("semantic diff removed relationships", removedRelationships, 1);
    expectBool("semantic diff added relationship endpoints",
               addedRelationshipHasEndpoints,
               true);
    expectBool("semantic diff removed relationship endpoints",
               removedRelationshipHasEndpoints,
               true);
    expectInt("semantic diff added diagnostics", addedDiagnostics, 1);
    expectInt("semantic diff removed diagnostics", removedDiagnostics, 1);
}

static void runRealWorkspaceIncludeFixture()
{
    printf("\n-- real workspace include fixture --\n");

    const QString workspaceRoot = normalizedPath(
        QFileInfo(QString::fromLocal8Bit(__FILE__)).dir()
            .filePath(QStringLiteral("new")));
    expectBool("real workspace fixture exists",
               QFileInfo(workspaceRoot).isDir(),
               true);
    if (!QFileInfo(workspaceRoot).isDir())
        return;

    QStringList files;
    QDirIterator it(workspaceRoot,
                    QStringList{"*.sv", "*.svh", "*.v"},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(normalizedPath(it.next()));

    ProjectModel project;
    project.setWorkspaceRoot(workspaceRoot);
    project.setScannedFiles(files);
    const ProjectSnapshot snapshot = project.snapshot();
    expectBool("real workspace include dirs contain root",
               snapshot.includeDirs.contains(workspaceRoot),
               true);
    const QString topPath = normalizedPath(
        QDir(workspaceRoot).filePath(QStringLiteral("elec_phy_import/top/rtl_top.sv")));
    const QString rootHeaderPath =
        normalizedPath(QDir(workspaceRoot).filePath(QStringLiteral("_svh.svh")));
    expectBool("real workspace records source roles",
               snapshot.sourceRoles.size() >= files.size(),
               true);
    expectBool("real workspace classifies sv as design source",
               project.sourceRoleForFile(topPath)
                   == SymbolTaxonomy::SourceRole::DesignSource,
               true);
    expectBool("real workspace classifies svh as header",
               project.sourceRoleForFile(rootHeaderPath)
                   == SymbolTaxonomy::SourceRole::Header,
               true);
    expectBool("real workspace model lists design sources",
               project.designSourceFiles().contains(topPath),
               true);
    expectBool("real workspace model lists headers",
               project.headerSourceFiles().contains(rootHeaderPath),
               true);
    expectBool("real workspace snapshot lists design sources",
               snapshot.designSourceFiles().contains(topPath),
               true);
    expectBool("real workspace snapshot lists headers",
               snapshot.headerSourceFiles().contains(rootHeaderPath),
               true);

    SlangManager slang;
    const QList<SemanticDiagnostic> diagnostics =
        slang.extractWorkspaceDiagnostics(snapshot.systemVerilogFiles,
                                          snapshot.includeDirs,
                                          snapshot.defines);

    bool missingRootHeader = false;
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        missingRootHeader = missingRootHeader
            || diagnostic.message.contains(QStringLiteral("_svh.svh"),
                                           Qt::CaseInsensitive);
    }
    expectBool("real workspace resolves root svh include",
               missingRootHeader,
               false);

    const QList<sym_list::SymbolInfo> symbols =
        slang.extractWorkspaceSymbols(snapshot.systemVerilogFiles,
                                      snapshot.includeDirs,
                                      snapshot.defines);
    const int rtlTopId =
        symbolId(symbols, QStringLiteral("rtl_top"), sym_list::sym_module);
    const int packageId =
        symbolId(symbols, QStringLiteral("gl_pkg"), sym_list::sym_package);
    const int interfaceId =
        symbolId(symbols, QStringLiteral("lr_genr_if"), sym_list::sym_interface);
    int packageParamId =
        symbolId(symbols, QStringLiteral("P_SW_NUM"), sym_list::sym_parameter);
    if (packageParamId < 0) {
        packageParamId =
            symbolId(symbols, QStringLiteral("P_SW_NUM"), sym_list::sym_localparam);
    }
    const int packageTypedefId =
        symbolId(symbols, QStringLiteral("cpld_sw_sp"), sym_list::sym_typedef);
    const int interfaceModportId =
        symbolIdInScope(symbols,
                        QStringLiteral("si"),
                        sym_list::sym_interface_modport,
                        QStringLiteral("lr_genr_if"));
    const int interfaceInstId =
        symbolIdInScope(symbols,
                        QStringLiteral("LR_GENR_IF"),
                        sym_list::sym_inst,
                        QStringLiteral("rtl_top"));
    const auto symbolByName = [&](const QString& name,
                                  sym_list::sym_type_e type,
                                  const QString& moduleScope) {
        sym_list::SymbolInfo found;
        found.symbolType = sym_list::sym_user;
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolName == name
                && symbol.symbolType == type
                && symbol.moduleScope == moduleScope) {
                return symbol;
            }
        }
        return found;
    };
    const auto symbolByNameAndType = [&](const QString& name,
                                         sym_list::sym_type_e type) {
        sym_list::SymbolInfo found;
        found.symbolType = sym_list::sym_user;
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolName == name && symbol.symbolType == type)
                return symbol;
        }
        return found;
    };
    sym_list::SymbolInfo packageParamSymbol =
        symbolByName(QStringLiteral("P_SW_NUM"),
                     sym_list::sym_parameter,
                     QStringLiteral("gl_pkg"));
    if (packageParamSymbol.symbolType == sym_list::sym_user) {
        packageParamSymbol =
            symbolByName(QStringLiteral("P_SW_NUM"),
                         sym_list::sym_localparam,
                         QStringLiteral("gl_pkg"));
    }
    const sym_list::SymbolInfo packageSymbol =
        symbolByNameAndType(QStringLiteral("gl_pkg"), sym_list::sym_package);
    const sym_list::SymbolInfo packageTypedefSymbol =
        symbolByName(QStringLiteral("cpld_sw_sp"),
                     sym_list::sym_typedef,
                     QStringLiteral("gl_pkg"));
    const sym_list::SymbolInfo interfaceModportSymbol =
        symbolByName(QStringLiteral("si"),
                     sym_list::sym_interface_modport,
                     QStringLiteral("lr_genr_if"));
    const sym_list::SymbolInfo interfaceInstSymbol =
        symbolByName(QStringLiteral("LR_GENR_IF"),
                     sym_list::sym_inst,
                     QStringLiteral("rtl_top"));

    expectBool("real workspace extracts symbols", symbols.size() > 20, true);
    expectBool("real workspace has rtl_top module",
               rtlTopId >= 0, true);
    expectBool("real workspace has gl_pkg package",
               packageId >= 0, true);
    expectBool("real workspace has interface",
               interfaceId >= 0, true);
    expectBool("real workspace has package parameter",
               packageParamId >= 0, true);
    expectBool("real workspace has package typedef",
               packageTypedefId >= 0, true);
    expectBool("real workspace has interface modport",
               interfaceModportId >= 0, true);
    expectBool("real workspace has interface instance",
               interfaceInstId >= 0, true);
    QSet<QString> packageScopes;
    packageScopes.insert(QStringLiteral("gl_pkg"));
    expectBool("real workspace taxonomy marks global package",
               SymbolTaxonomy::ownerScope(packageSymbol, packageScopes)
                   == SymbolTaxonomy::SymbolOwnerScope::Global,
               true);
    expectBool("real workspace taxonomy marks package parameter visibility",
               SymbolTaxonomy::visibility(packageParamSymbol, packageScopes)
                   == SymbolTaxonomy::SymbolVisibility::PackageVisible,
               true);
    expectBool("real workspace taxonomy marks package typedef visibility",
               SymbolTaxonomy::visibility(packageTypedefSymbol, packageScopes)
                   == SymbolTaxonomy::SymbolVisibility::PackageVisible,
               true);
    expectBool("real workspace taxonomy marks interface modport member",
               SymbolTaxonomy::ownerScope(interfaceModportSymbol, packageScopes)
                   == SymbolTaxonomy::SymbolOwnerScope::Interface,
               true);
    expectBool("real workspace taxonomy marks module instance local",
               SymbolTaxonomy::visibility(interfaceInstSymbol, packageScopes)
                   == SymbolTaxonomy::SymbolVisibility::ScopeLocal,
               true);

    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(symbols));
    DefinitionService definitionService(&index);

    DefinitionQuery packageParamQuery;
    packageParamQuery.symbolName = QStringLiteral("P_SW_NUM");
    packageParamQuery.fileName = topPath;
    packageParamQuery.moduleName = QStringLiteral("rtl_top");
    const DefinitionResult packageParam =
        definitionService.resolveDefinition(packageParamQuery);
    expectBool("real workspace jumps package parameter",
               packageParam.found
                   && packageParam.symbol.moduleScope == QStringLiteral("gl_pkg"),
               true);

    DefinitionQuery interfaceQuery;
    interfaceQuery.symbolName = QStringLiteral("lr_genr_if");
    interfaceQuery.fileName = topPath;
    interfaceQuery.moduleName = QStringLiteral("rtl_top");
    const DefinitionResult interfaceResult =
        definitionService.resolveDefinition(interfaceQuery);
    expectBool("real workspace jumps interface",
               interfaceResult.found
                   && interfaceResult.symbol.symbolType == sym_list::sym_interface,
               true);

    DefinitionQuery modportTypeQuery;
    modportTypeQuery.symbolName = QStringLiteral("si");
    modportTypeQuery.fileName = topPath;
    modportTypeQuery.moduleName = QStringLiteral("rtl_top");
    modportTypeQuery.linePrefixBeforeCursor = QStringLiteral("lr_genr_if.si");
    const DefinitionResult modportTypeResult =
        definitionService.resolveDefinition(modportTypeQuery);
    expectBool("real workspace jumps interface type modport",
               modportTypeResult.found
                   && modportTypeResult.symbol.symbolType == sym_list::sym_interface_modport
                   && modportTypeResult.symbol.moduleScope == QStringLiteral("lr_genr_if"),
               true);

    DefinitionQuery modportInstQuery = modportTypeQuery;
    modportInstQuery.linePrefixBeforeCursor = QStringLiteral("LR_GENR_IF.si");
    const DefinitionResult modportInstResult =
        definitionService.resolveDefinition(modportInstQuery);
    expectBool("real workspace jumps interface instance modport",
               modportInstResult.found
                   && modportInstResult.symbol.symbolType == sym_list::sym_interface_modport
                   && modportInstResult.symbol.moduleScope == QStringLiteral("lr_genr_if"),
               true);

    CompletionService completionService(&index);
    CommandCompletionQuery parameterCompletion;
    parameterCompletion.moduleName = QStringLiteral("rtl_top");
    parameterCompletion.symbolType = sym_list::sym_parameter;
    parameterCompletion.prefix = QStringLiteral("P_SW");
    expectBool("real workspace completes package parameter",
               CompletionSymbolQuery::namesFromSymbols(
                   completionService.findCommandCompletionSymbols(parameterCompletion))
                   .contains(QStringLiteral("P_SW_NUM")),
               true);

    CommandCompletionQuery typedefCompletion;
    typedefCompletion.moduleName = QStringLiteral("rtl_top");
    typedefCompletion.symbolType = sym_list::sym_typedef;
    typedefCompletion.prefix = QStringLiteral("cpld");
    expectBool("real workspace completes package typedef",
               CompletionSymbolQuery::namesFromSymbols(
                   completionService.findCommandCompletionSymbols(typedefCompletion))
                   .contains(QStringLiteral("cpld_sw_sp")),
               true);
}

static void runPostWorkspaceDiagnosticFixture()
{
    printf("\n-- post-workspace diagnostic fixture --\n");

    QTemporaryDir diagnosticDir;
    expectBool("post-workspace diagnostic temp dir created",
               diagnosticDir.isValid(),
               true);
    if (!diagnosticDir.isValid())
        return;

    const QString brokenPath =
        normalizedPath(diagnosticDir.filePath(QStringLiteral("broken_diag.sv")));
    const QString brokenContent = QStringLiteral(
        "module broken_diag(input logic clk);\n"
        "  logic bad;\n"
        "  assign bad = ;\n"
        "endmodule\n");
    SlangManager slang;
    const QList<SemanticDiagnostic> diagnostics =
        slang.extractDiagnostics(brokenPath, brokenContent);
    expectBool("post-workspace temp diagnostics extracted",
               !diagnostics.isEmpty(),
               true);
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
    runModuleBriefServiceFixture();
    runSignalJourneyServiceFixture();
    runClockResetDomainServiceFixture();
    runFsmGraphServiceFixture();
    runSemanticDiffServiceFixture();
    runRealWorkspaceIncludeFixture();
    runPostWorkspaceDiagnosticFixture();

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
