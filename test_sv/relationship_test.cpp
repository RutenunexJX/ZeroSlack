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
#include "scopebandservice.h"
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

static sym_list::SymbolInfo symbolById(const QList<sym_list::SymbolInfo>& symbols,
                                       int symbolId)
{
    for (const auto& symbol : symbols) {
        if (symbol.symbolId == symbolId)
            return symbol;
    }

    sym_list::SymbolInfo missing;
    missing.symbolId = -1;
    return missing;
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
    const QList<sym_list::SymbolInfo> stageSymbols = db->findSymbolsByFileName(stagePath);

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
    const sym_list::SymbolInfo topSymbol = symbolById(topSymbols, topId);
    const sym_list::SymbolInfo stageSymbol = symbolById(stageSymbols, stageId);
    const sym_list::SymbolInfo captureSymbol = symbolById(topSymbols, captureId);
    const sym_list::SymbolInfo stageDataSymbol =
        symbolById(topSymbols, stageDataId);
    expectBool("semantic facade uses fixture symbol record",
               topSymbol.symbolName == QStringLiteral("rel_top"), true);
    const SymbolStableKey topFacadeStableKey =
        symbolStableKeyForSymbol(topSymbol);
    const SemanticSymbolRecord topRecord =
        index.getSymbolRecordByStableKey(topFacadeStableKey);
    const SemanticSymbolRecord stageDataRecord =
        index.getSymbolRecordByStableKey(
            symbolStableKeyForSymbol(stageDataSymbol));
    expectBool("semantic facade exposes symbol record",
               topRecord.isValid()
                   && topRecord.name == QStringLiteral("rel_top")
                   && topRecord.localHandle == topId
                   && topRecord.stableKey == topFacadeStableKey
                   && topRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && topRecord.sourceRole
                       == SymbolTaxonomy::SourceRole::DesignSource,
               true);
    expectBool("semantic facade record carries owner and type",
               stageDataRecord.isValid()
                   && stageDataRecord.owner.name == QStringLiteral("rel_top")
                   && stageDataRecord.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Module
                   && stageDataRecord.type.rawTypeText
                       == stageDataSymbol.dataType,
               true);
    expectBool("semantic facade finds symbol definition",
               !facadeStageDefs.isEmpty()
                   && facadeStageDefs.first().symbolId == stageId,
               true);
    expectBool("semantic facade returns missing symbol definition",
               index.findDefinitions(QStringLiteral("missing_symbol"),
                                     queryContext).isEmpty(),
               true);
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
              index.findEndModuleLine(topPath, topSymbol), topEndModuleLine);

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
        expectBool("slang diagnostic has owner",
                   brokenDiagnosticResults.first().diagnostic.owner
                       == SemanticDiagnostic::SlangCompiler,
                   true);
        expectBool("slang diagnostic exposes display metadata",
                   brokenDiagnosticResults.first().severityDisplayName == QStringLiteral("Error")
                       && brokenDiagnosticResults.first().fileDisplayName
                          == QStringLiteral("broken_diag.sv")
                       && brokenDiagnosticResults.first().ownerDisplayName
                          == QStringLiteral("Slang"),
                   true);
    }

    SemanticDiagnostic infoDiagnostic;
    infoDiagnostic.fileName = topPath;
    infoDiagnostic.line = 9;
    infoDiagnostic.column = 3;
    infoDiagnostic.message = QStringLiteral("info message");
    infoDiagnostic.severity = SemanticDiagnostic::Info;
    infoDiagnostic.owner = SemanticDiagnostic::SemanticIndexOwner;

    SemanticDiagnostic warningDiagnostic;
    warningDiagnostic.fileName = topPath;
    warningDiagnostic.line = 2;
    warningDiagnostic.column = 1;
    warningDiagnostic.message = QStringLiteral("warning message");
    warningDiagnostic.severity = SemanticDiagnostic::Warning;
    warningDiagnostic.owner = SemanticDiagnostic::SlangCompiler;

    SemanticDiagnostic errorDiagnostic;
    errorDiagnostic.fileName = stagePath;
    errorDiagnostic.line = 4;
    errorDiagnostic.column = 7;
    errorDiagnostic.message = QStringLiteral("error message");
    errorDiagnostic.severity = SemanticDiagnostic::Error;
    errorDiagnostic.owner = SemanticDiagnostic::SlangCompiler;

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
    expectInt("diagnostic report owner count",
              diagnosticReport.ownerCounts.value(SemanticDiagnostic::SlangCompiler), 2);
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
    expectBool("diagnostic report exposes location display metadata",
               !diagnosticReport.diagnostics.isEmpty()
                   && diagnosticReport.diagnostics.first().lineDisplayName
                       == QStringLiteral("4")
                   && diagnosticReport.diagnostics.first().columnDisplayName
                       == QStringLiteral("7")
                   && diagnosticReport.diagnostics.first().messageDisplayName
                       == QStringLiteral("error message")
                   && diagnosticReport.diagnostics.first().ownerDisplayName
                       == QStringLiteral("Slang"),
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
    currentFilePanelOptions.currentFileName =
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv"));
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
    DiagnosticQuery unnormalizedTopOnlyDiagnosticQuery;
    unnormalizedTopOnlyDiagnosticQuery.fileName =
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv"));
    expectInt("diagnostic report normalizes direct current file query",
              diagnosticReportService
                  .findDiagnosticReport(unnormalizedTopOnlyDiagnosticQuery)
                  .totalCount,
              2);

    DiagnosticPanelQueryOptions workspacePanelOptions;
    workspacePanelOptions.scope = DiagnosticPanelScope::WorkspaceFiles;
    workspacePanelOptions.workspaceFiles = {
        QDir(QFileInfo(stagePath).dir()).filePath(QStringLiteral("./relationship_stage.sv")),
        stagePath,
    };
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
    bool searchFoundTopStableKey = false;
    bool searchFoundTopRecord = false;
    for (const SearchResult& result : moduleSearchResults) {
        searchFoundTop = searchFoundTop || result.symbol.symbolId == topId;
        searchFoundStage = searchFoundStage || result.symbol.symbolId == stageId;
        searchFoundTopStableKey = searchFoundTopStableKey
            || (result.symbol.symbolId == topId
                && result.symbolStableKey == symbolStableKeyForSymbol(result.symbol));
        searchFoundTopRecord = searchFoundTopRecord
            || (result.symbol.symbolId == topId
                && result.symbolRecord.localHandle == topId
                && result.symbolRecord.stableKey == result.symbolStableKey
                && result.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Module);
    }
    expectBool("search service finds top module",
               searchFoundTop, true);
    expectBool("search service finds stage module",
               searchFoundStage, true);
    expectBool("search service result carries stable key",
               searchFoundTopStableKey, true);
    expectBool("search service result carries semantic record",
               searchFoundTopRecord, true);

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
    const QList<SemanticSymbolRecord> snapshotTopRecords =
        snapshotIndex.getSymbolRecords(topPath);
    const SemanticSymbolRecord snapshotTopRecord =
        snapshotIndex.getSymbolRecordByStableKey(symbolStableKeyForSymbol(topSymbol));
    expectBool("semantic snapshot exposes symbol records",
               snapshotTopRecords.size() == topSymbols.size()
                   && snapshotTopRecord.isValid()
                   && snapshotTopRecord.localHandle == topId
                   && snapshotTopRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module,
               true);
    const QList<sym_list::SymbolInfo> snapshotStageDefs =
        snapshotIndex.findDefinitions(QStringLiteral("rel_stage"), queryContext);
    expectBool("semantic snapshot finds symbol definition",
               !snapshotStageDefs.isEmpty()
                   && snapshotStageDefs.first().symbolId == stageId,
               true);
    SearchService snapshotSearchService(&snapshotIndex);
    const QList<SearchResult> snapshotSearchResults =
        snapshotSearchService.findSymbols(moduleSearchQuery);
    bool snapshotSearchFoundTop = false;
    bool snapshotSearchFoundStage = false;
    bool snapshotSearchFoundTopStableKey = false;
    bool snapshotSearchFoundStageStableKey = false;
    bool snapshotSearchFoundStageRecord = false;
    for (const SearchResult& result : snapshotSearchResults) {
        snapshotSearchFoundTop = snapshotSearchFoundTop || result.symbol.symbolId == topId;
        snapshotSearchFoundStage = snapshotSearchFoundStage || result.symbol.symbolId == stageId;
        snapshotSearchFoundTopStableKey = snapshotSearchFoundTopStableKey
            || (result.symbol.symbolId == topId
                && result.symbolStableKey == symbolStableKeyForSymbol(result.symbol));
        snapshotSearchFoundStageStableKey = snapshotSearchFoundStageStableKey
            || (result.symbol.symbolId == stageId
                && result.symbolStableKey == symbolStableKeyForSymbol(result.symbol));
        snapshotSearchFoundStageRecord = snapshotSearchFoundStageRecord
            || (result.symbol.symbolId == stageId
                && result.symbolRecord.localHandle == stageId
                && result.symbolRecord.stableKey == result.symbolStableKey
                && result.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Module);
    }
    expectBool("snapshot search service finds top module",
               snapshotSearchFoundTop, true);
    expectBool("snapshot search service finds stage module",
               snapshotSearchFoundStage, true);
    expectBool("snapshot search service result carries top stable key",
               snapshotSearchFoundTopStableKey, true);
    expectBool("snapshot search service result carries stage stable key",
               snapshotSearchFoundStageStableKey, true);
    expectBool("snapshot search service result carries semantic record",
               snapshotSearchFoundStageRecord, true);
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
    bool snapshotOutlineHasRowMetadata = false;
    for (const SymbolOutlineGroup& group : snapshotOutlineGroups) {
        snapshotOutlineHasDisplayName =
            snapshotOutlineHasDisplayName
            || (group.symbolType == sym_list::sym_module
                && group.displayName == QStringLiteral("Module")
                && !group.symbols.isEmpty());
        if (group.symbolType == sym_list::sym_module
            && group.displayName == QStringLiteral("Module")
            && group.iconKind == SymbolOutlineIconKind::Module
            && !group.symbolRows.isEmpty()) {
            const SymbolOutlineSymbolRow& row = group.symbolRows.first();
            snapshotOutlineHasRowMetadata =
                row.symbol.symbolId == topId
                && row.symbolRecord.isValid()
                && row.symbolRecord.localHandle == topId
                && row.symbolRecord.stableKey == symbolStableKeyForSymbol(row.symbol)
                && row.symbolRecord.name == QStringLiteral("rel_top")
                && row.displayName == QStringLiteral("rel_top")
                && row.typeDisplayName == QStringLiteral("Module")
                && row.iconKind == SymbolOutlineIconKind::Module
                && !row.detailDisplayName.isEmpty();
        }
    }
    expectBool("snapshot navigation outline exposes display model",
               snapshotOutlineHasDisplayName,
               true);
    expectBool("snapshot navigation outline exposes row metadata",
               snapshotOutlineHasRowMetadata,
               true);
    QList<sym_list::SymbolInfo> metadataOutlineSymbols = snapshot->getSymbols();
    sym_list::SymbolInfo metadataOutlineModule;
    metadataOutlineModule.fileName = topPath;
    metadataOutlineModule.symbolName = QStringLiteral("metadata_rel_top");
    metadataOutlineModule.symbolType = sym_list::sym_user;
    metadataOutlineModule.startLine = 1;
    metadataOutlineModule.startColumn = 1;
    metadataOutlineModule.endLine = 1;
    metadataOutlineModule.endColumn = 1;
    metadataOutlineModule.symbolId = 900001;
    metadataOutlineModule.hasSemanticMetadata = true;
    metadataOutlineModule.semanticDeclarationKind =
        SymbolSemanticMetadata::DeclarationKind::Module;
    metadataOutlineModule.semanticUsageRole =
        SymbolSemanticMetadata::SymbolUsageRole::Declaration;
    metadataOutlineModule.semanticOwnerScope =
        SymbolSemanticMetadata::SymbolOwnerScope::Global;
    metadataOutlineModule.semanticVisibility =
        SymbolSemanticMetadata::SymbolVisibility::Global;
    metadataOutlineModule.semanticSourceRole =
        SymbolSemanticMetadata::SourceRole::DesignSource;
    metadataOutlineModule.rawCollectorKind = sym_list::sym_user;
    metadataOutlineSymbols.append(metadataOutlineModule);
    SemanticIndex metadataOutlineIndex;
    metadataOutlineIndex.setSnapshot(
        std::make_shared<SemanticIndexSnapshot>(
            metadataOutlineSymbols,
            snapshot->relationships(),
            snapshot->diagnostics(),
            snapshot->fileContents()));
    SearchService metadataOutlineSearchService(&metadataOutlineIndex);
    SearchQuery metadataOutlineSearchQuery;
    metadataOutlineSearchQuery.text = QStringLiteral("metadata_rel_top");
    metadataOutlineSearchQuery.intent =
        SymbolTaxonomy::SymbolSearchIntent::OutlineSymbols;
    const QList<SearchResult> metadataOutlineSearchResults =
        metadataOutlineSearchService.findSymbols(metadataOutlineSearchQuery);
    expectBool("metadata outline search finds module",
               metadataOutlineSearchResults.size() == 1
                   && metadataOutlineSearchResults.first().symbol.symbolId
                       == metadataOutlineModule.symbolId,
               true);
    SearchQuery metadataTypedSearchQuery;
    metadataTypedSearchQuery.text = QStringLiteral("metadata_rel_top");
    metadataTypedSearchQuery.types = {sym_list::sym_module};
    const QList<SearchResult> metadataTypedSearchResults =
        metadataOutlineSearchService.findSymbols(metadataTypedSearchQuery);
    expectBool("metadata typed search finds module",
               metadataTypedSearchResults.size() == 1
                   && metadataTypedSearchResults.first().symbol.symbolId
                       == metadataOutlineModule.symbolId
                   && metadataTypedSearchResults.first().symbolRecord.isValid()
                   && metadataTypedSearchResults.first().symbolRecord.stableKey
                       == metadataTypedSearchResults.first().symbolStableKey
                   && metadataTypedSearchResults.first().symbolRecord.name
                       == QStringLiteral("metadata_rel_top")
                   && metadataTypedSearchResults.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && metadataTypedSearchResults.first().symbolRecord.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Global,
               true);
    SearchQuery metadataDefinitionSearchQuery;
    metadataDefinitionSearchQuery.text = QStringLiteral("metadata_rel_top");
    metadataDefinitionSearchQuery.intent =
        SymbolTaxonomy::SymbolSearchIntent::DefinitionCandidates;
    const QList<SearchResult> metadataDefinitionSearchResults =
        metadataOutlineSearchService.findSymbols(metadataDefinitionSearchQuery);
    expectBool("metadata definition search finds module",
               metadataDefinitionSearchResults.size() == 1
                   && metadataDefinitionSearchResults.first().symbol.symbolId
                       == metadataOutlineModule.symbolId
                   && metadataDefinitionSearchResults.first()
                          .symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module,
               true);
    NavigationService metadataOutlineNavigationService(&metadataOutlineIndex);
    NavigationSymbolOutlineQuery metadataOutlineQuery;
    metadataOutlineQuery.fileName = topPath;
    metadataOutlineQuery.filter = QStringLiteral("metadata");
    const QList<SymbolOutlineGroup> metadataOutlineGroups =
        metadataOutlineNavigationService.findSymbolOutline(metadataOutlineQuery);
    bool metadataOutlineGroupedAsModule = false;
    for (const SymbolOutlineGroup& group : metadataOutlineGroups) {
        if (group.symbolType != sym_list::sym_module
            || group.displayName != QStringLiteral("Module")) {
            continue;
        }
        for (const SymbolOutlineSymbolRow& row : group.symbolRows) {
            metadataOutlineGroupedAsModule =
                metadataOutlineGroupedAsModule
                || (row.symbol.symbolId == metadataOutlineModule.symbolId
                    && row.symbolRecord.isValid()
                    && row.symbolRecord.localHandle
                        == metadataOutlineModule.symbolId
                    && row.symbolRecord.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Module
                    && row.symbolRecord.rawCollectorKind == sym_list::sym_user
                    && row.displayName == QStringLiteral("metadata_rel_top")
                    && row.typeDisplayName == group.displayName
                    && row.iconKind == SymbolOutlineIconKind::Module);
        }
    }
    expectBool("metadata navigation outline groups module",
               metadataOutlineGroupedAsModule,
               true);
    expectBool("semantic snapshot returns cached file content",
               snapshotIndex.getCachedFileContent(topPath) == contents.value(topPath), true);
    expectBool("semantic snapshot returns scope symbols",
               snapshotIndex.getScopeSymbolNames(topPath, 20).contains(QStringLiteral("stage_data")),
               true);
    sym_list::SymbolInfo snapshotTopSymbol = topSymbol;
    snapshotTopSymbol.endLine = 0;
    expectInt("semantic snapshot finds module end line from cached content",
              snapshotIndex.findEndModuleLine(topPath, snapshotTopSymbol), topEndModuleLine);
    const SymbolStableKey topStableKey = symbolStableKeyForSymbol(topSymbol);
    const SymbolStableKey stageStableKey = symbolStableKeyForSymbol(stageSymbol);
    const QList<SemanticRelationship> snapshotTopRelationships =
        snapshotIndex.getRelationships(topStableKey, true);
    bool snapshotFoundStage = false;
    bool snapshotFoundStageStableKey = false;
    for (const SemanticRelationship& relationship : snapshotTopRelationships) {
        snapshotFoundStage = snapshotFoundStage
            || (relationship.toId == stageId
                && relationship.type == SymbolRelationshipEngine::INSTANTIATES);
        snapshotFoundStageStableKey = snapshotFoundStageStableKey
            || (relationship.toId == stageId
                && relationship.type == SymbolRelationshipEngine::INSTANTIATES
                && relationship.fromStableKey == topStableKey
                && relationship.toStableKey == stageStableKey
                && !semanticRelationshipStableKeyText(relationship).isEmpty());
    }
    expectBool("semantic snapshot captures relationships",
               snapshotFoundStage, true);
    expectBool("semantic snapshot captures relationship stable keys",
               snapshotFoundStageStableKey, true);
    const QList<SemanticRelationshipResult> snapshotTopRelationshipResults =
        snapshotIndex.getRelationshipResults(topStableKey, true);
    const QList<SemanticRelationshipResult> snapshotTopStableRelationshipResults =
        snapshotIndex.getRelationshipResults(topStableKey, true);
    bool snapshotFoundStageResult = false;
    bool snapshotFoundStageResultStableKey = false;
    bool snapshotFoundStageResultRecords = false;
    bool snapshotFoundStageResultMetadata = false;
    for (const SemanticRelationshipResult& relationship : snapshotTopRelationshipResults) {
        snapshotFoundStageResult = snapshotFoundStageResult
            || (relationship.relationship.fromId == topId
                && relationship.relationship.toId == stageId
                && relationship.relationship.type == SymbolRelationshipEngine::INSTANTIATES
                && relationship.fromSymbol.symbolId == topId
                && relationship.toSymbol.symbolId == stageId);
        snapshotFoundStageResultStableKey = snapshotFoundStageResultStableKey
            || (relationship.relationship.fromId == topId
                && relationship.relationship.toId == stageId
                && relationship.fromStableKey == topStableKey
                && relationship.toStableKey == stageStableKey
                && relationship.relationship.fromStableKey == topStableKey
                && relationship.relationship.toStableKey == stageStableKey);
        snapshotFoundStageResultRecords = snapshotFoundStageResultRecords
            || (relationship.relationship.fromId == topId
                && relationship.relationship.toId == stageId
                && relationship.relationship.type == SymbolRelationshipEngine::INSTANTIATES
                && relationship.fromSymbolRecord.isValid()
                && relationship.fromSymbolRecord.localHandle == topId
                && relationship.fromSymbolRecord.stableKey == relationship.fromStableKey
                && relationship.fromSymbolRecord.name == QStringLiteral("rel_top")
                && relationship.toSymbolRecord.isValid()
                && relationship.toSymbolRecord.localHandle == stageId
                && relationship.toSymbolRecord.stableKey == relationship.toStableKey
                && relationship.toSymbolRecord.name == QStringLiteral("rel_stage")
                && relationship.toSymbolRecord.owner.kind
                    == SymbolTaxonomy::SymbolOwnerScope::Global
                && relationship.toSymbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Module);
        snapshotFoundStageResultMetadata = snapshotFoundStageResultMetadata
            || (relationship.relationship.fromId == topId
                && relationship.relationship.toId == stageId
                && relationship.relationship.type == SymbolRelationshipEngine::INSTANTIATES
                && relationship.provenance == RelationshipProvenance::Inferred
                && relationship.confidence == 90
                && relationship.evidenceText.contains(QStringLiteral("Instance:")));
    }
    expectBool("semantic snapshot returns relationship endpoint symbols",
               snapshotFoundStageResult, true);
    expectBool("semantic snapshot queries relationship results by stable key",
               snapshotTopStableRelationshipResults.size()
                   == snapshotTopRelationshipResults.size(),
               true);
    expectBool("semantic snapshot returns relationship stable keys",
               snapshotFoundStageResultStableKey, true);
    expectBool("semantic snapshot returns relationship endpoint records",
               snapshotFoundStageResultRecords, true);
    expectBool("semantic snapshot returns relationship metadata",
               snapshotFoundStageResultMetadata, true);
    RelationshipService snapshotRelationshipService(&snapshotIndex);
    RelationshipQuery snapshotRelationshipQuery;
    snapshotRelationshipQuery.symbolStableKey = topStableKey;
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
                   topStableKey, stageStableKey, SymbolRelationshipEngine::INSTANTIATES),
               true);
    expectBool("snapshot relationship service exact named relationship",
               snapshotRelationshipService.hasNamedRelationship(
                   QStringLiteral("rel_top"),
                   QStringLiteral("rel_stage"),
                   SymbolRelationshipEngine::INSTANTIATES),
               true);
    expectBool("snapshot relationship service rejects reversed relationship",
               snapshotRelationshipService.hasRelationship(
                   stageStableKey, topStableKey, SymbolRelationshipEngine::INSTANTIATES),
               false);
    RelationshipBrowseQuery snapshotRelationshipBrowseQuery;
    snapshotRelationshipBrowseQuery.symbolStableKey = topStableKey;
    snapshotRelationshipBrowseQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const RelationshipReport snapshotRelationshipReport =
        snapshotRelationshipService.findRelationshipReport(snapshotRelationshipBrowseQuery);
    expectBool("snapshot relationship report subject local handle",
               snapshotRelationshipReport.subjectSymbolRecord.localHandle == topId,
               true);
    expectBool("snapshot relationship report subject stable key",
               snapshotRelationshipReport.subjectStableKey == topStableKey,
               true);
    expectBool("snapshot relationship report subject record",
               snapshotRelationshipReport.subjectSymbolRecord.isValid()
                   && snapshotRelationshipReport.subjectSymbolRecord.localHandle == topId
                   && snapshotRelationshipReport.subjectSymbolRecord.stableKey == topStableKey
                   && snapshotRelationshipReport.subjectSymbolRecord.name
                       == QStringLiteral("rel_top"),
               true);
    expectBool("snapshot relationship report subject display name",
               snapshotRelationshipReport.subjectDisplayName
                   == QStringLiteral("rel_top"),
               true);
    expectBool("snapshot relationship report found reason metadata",
               snapshotRelationshipReport.notFoundReason
                       == RelationshipReportNotFoundReason::None
                   && snapshotRelationshipReport.notFoundReasonDisplayName.isEmpty(),
               true);
    expectInt("snapshot relationship report outgoing count",
              snapshotRelationshipReport.outgoingCount, 1);
    expectInt("snapshot relationship report total count",
              snapshotRelationshipReport.totalCount, 1);
    expectBool("snapshot relationship report keeps peer record",
               !snapshotRelationshipReport.relationships.isEmpty()
                   && snapshotRelationshipReport.relationships.first()
                          .peerSymbolRecord.isValid()
                   && snapshotRelationshipReport.relationships.first()
                          .peerSymbolRecord.localHandle == stageId
                   && snapshotRelationshipReport.relationships.first()
                          .peerSymbolRecord.stableKey == stageStableKey
                   && snapshotRelationshipReport.relationships.first()
                          .peerSymbolRecord.name == QStringLiteral("rel_stage"),
               true);
    expectBool("snapshot relationship report keeps stable identity",
               !snapshotRelationshipReport.relationships.isEmpty()
                   && snapshotRelationshipReport.relationships.first()
                          .subjectStableKey == topStableKey
                   && snapshotRelationshipReport.relationships.first()
                          .peerStableKey == stageStableKey
                   && snapshotRelationshipReport.relationships.first()
                          .peerSymbolRecord.localHandle == stageId,
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
                          .peerSymbolRecord.localHandle == stageId
                   && snapshotRelationshipReport.directionGroups.first()
                          .typeGroups.first()
                          .relationships.first()
                          .peerSymbolRecord.stableKey == stageStableKey,
               true);
    RelationshipBrowseQuery snapshotIncomingStageBrowseQuery;
    snapshotIncomingStageBrowseQuery.symbolStableKey = stageStableKey;
    snapshotIncomingStageBrowseQuery.includeOutgoing = false;
    snapshotIncomingStageBrowseQuery.includeIncoming = true;
    snapshotIncomingStageBrowseQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const RelationshipReport snapshotIncomingStageReport =
        snapshotRelationshipService.findRelationshipReport(snapshotIncomingStageBrowseQuery);
    expectInt("snapshot relationship report incoming-only total",
              snapshotIncomingStageReport.totalCount, 1);
    expectInt("snapshot relationship report incoming-only count",
              snapshotIncomingStageReport.incomingCount, 1);
    expectBool("snapshot relationship report incoming peer record",
               !snapshotIncomingStageReport.relationships.isEmpty()
                   && snapshotIncomingStageReport.relationships.first()
                          .peerSymbolRecord.isValid()
                   && snapshotIncomingStageReport.relationships.first()
                          .peerSymbolRecord.localHandle == topId
                   && snapshotIncomingStageReport.relationships.first()
                          .peerSymbolRecord.stableKey == topStableKey,
               true);
    expectBool("snapshot incoming relationship report keeps stable identity",
               snapshotIncomingStageReport.subjectStableKey == stageStableKey
                   && !snapshotIncomingStageReport.relationships.isEmpty()
                   && snapshotIncomingStageReport.relationships.first()
                          .subjectStableKey == stageStableKey
                   && snapshotIncomingStageReport.relationships.first()
                          .peerStableKey == topStableKey
                   && snapshotIncomingStageReport.subjectSymbolRecord.stableKey
                          == stageStableKey,
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
                          .peerSymbolRecord.localHandle == topId,
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
               snapshotNamedRelationshipReport.subjectStableKey == topStableKey
                   && snapshotNamedRelationshipReport.totalCount == 1
                   && !snapshotNamedRelationshipReport.relationships.isEmpty()
                   && snapshotNamedRelationshipReport.relationships.first()
                          .peerSymbolRecord.localHandle == stageId,
               true);
    RelationshipBrowseQuery snapshotMissingRelationshipBrowseQuery;
    snapshotMissingRelationshipBrowseQuery.symbolName =
        QStringLiteral("missing_rel_subject");
    snapshotMissingRelationshipBrowseQuery.fileName = topPath;
    const RelationshipReport snapshotMissingRelationshipReport =
        snapshotRelationshipService.findRelationshipReport(
            snapshotMissingRelationshipBrowseQuery);
    expectBool("snapshot relationship report missing subject reason",
               !snapshotMissingRelationshipReport.subjectStableKey.isValid()
                   && !snapshotMissingRelationshipReport.subjectSymbolRecord.isValid()
                   && snapshotMissingRelationshipReport.notFoundReason
                       == RelationshipReportNotFoundReason::NoSubjectSymbol
                   && snapshotMissingRelationshipReport.notFoundReasonDisplayName
                       == QStringLiteral("no subject symbol"),
               true);
    expectBool("snapshot relationship report missing subject display name",
               snapshotMissingRelationshipReport.subjectDisplayName
                   == QStringLiteral("missing_rel_subject"),
               true);
    RelationshipBrowseQuery snapshotNoRelationshipBrowseQuery;
    snapshotNoRelationshipBrowseQuery.symbolStableKey = topStableKey;
    snapshotNoRelationshipBrowseQuery.types = {SymbolRelationshipEngine::CONSTRAINS};
    const RelationshipReport snapshotNoRelationshipReport =
        snapshotRelationshipService.findRelationshipReport(
            snapshotNoRelationshipBrowseQuery);
    expectBool("snapshot relationship report no relationships reason",
               snapshotNoRelationshipReport.subjectStableKey == topStableKey
                   && snapshotNoRelationshipReport.totalCount == 0
                   && snapshotNoRelationshipReport.notFoundReason
                       == RelationshipReportNotFoundReason::NoRelationships
                   && snapshotNoRelationshipReport.notFoundReasonDisplayName
                       == QStringLiteral("no relationships"),
               true);
    ReferenceService snapshotReferenceService(&snapshotIndex);
    ReferenceQuery snapshotReferenceQuery;
    snapshotReferenceQuery.symbolStableKey = stageStableKey;
    snapshotReferenceQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<ReferenceResult> snapshotReferenceResults =
        snapshotReferenceService.findReferences(snapshotReferenceQuery);
    bool snapshotReferenceFoundTop = false;
    for (const ReferenceResult& reference : snapshotReferenceResults) {
        snapshotReferenceFoundTop = snapshotReferenceFoundTop
            || (reference.relationship.relationship.fromId == topId
                && reference.relationship.relationship.toId == stageId
                && reference.referencingSymbolRecord.isValid()
                && reference.referencingSymbolRecord.localHandle == topId
                && reference.referencingSymbolRecord.stableKey == topStableKey
                && reference.referencedSymbolRecord.isValid()
                && reference.referencedSymbolRecord.localHandle == stageId
                && reference.referencedSymbolRecord.stableKey == stageStableKey);
    }
    expectBool("snapshot reference service finds stage instantiation",
               snapshotReferenceFoundTop, true);
    expectBool("snapshot reference service has references",
               snapshotReferenceService.hasReferences(snapshotReferenceQuery), true);
    ReferenceQuery snapshotStableReferenceQuery = snapshotReferenceQuery;
    snapshotStableReferenceQuery.symbolStableKey = stageStableKey;
    const QList<ReferenceResult> snapshotStableReferenceResults =
        snapshotReferenceService.findReferences(snapshotStableReferenceQuery);
    expectBool("snapshot reference service resolves stable query key",
               snapshotStableReferenceResults.size()
                   == snapshotReferenceResults.size()
                   && !snapshotStableReferenceResults.isEmpty()
                   && snapshotStableReferenceResults.first().referencedStableKey
                       == stageStableKey,
               true);
    const ReferenceReport snapshotReferenceReport =
        snapshotReferenceService.findReferenceReport(snapshotReferenceQuery);
    expectBool("snapshot reference report subject local handle",
               snapshotReferenceReport.subjectSymbolRecord.localHandle == stageId,
               true);
    expectBool("snapshot reference report subject stable key",
               snapshotReferenceReport.subjectStableKey == stageStableKey,
               true);
    expectBool("snapshot reference report subject record",
               snapshotReferenceReport.subjectSymbolRecord.isValid()
                   && snapshotReferenceReport.subjectSymbolRecord.localHandle == stageId
                   && snapshotReferenceReport.subjectSymbolRecord.stableKey == stageStableKey
                   && snapshotReferenceReport.subjectSymbolRecord.name
                       == QStringLiteral("rel_stage"),
               true);
    expectBool("snapshot reference report subject display name",
               snapshotReferenceReport.subjectDisplayName
                   == QStringLiteral("rel_stage"),
               true);
    expectBool("snapshot reference report found reason metadata",
               snapshotReferenceReport.notFoundReason
                       == ReferenceReportNotFoundReason::None
                   && snapshotReferenceReport.notFoundReasonDisplayName.isEmpty(),
               true);
    const ReferenceReport snapshotStableReferenceReport =
        snapshotReferenceService.findReferenceReport(snapshotStableReferenceQuery);
    expectBool("snapshot reference report resolves stable query key",
               snapshotStableReferenceReport.totalCount
                   == snapshotReferenceReport.totalCount
                   && snapshotStableReferenceReport.subjectStableKey == stageStableKey
                   && snapshotStableReferenceReport.subjectSymbolRecord.localHandle
                       == stageId,
               true);
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
    expectBool("snapshot reference report keeps grouped records",
               snapshotReferenceReport.fileGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first().typeGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencingSymbolRecord.localHandle == topId
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencedSymbolRecord.localHandle == stageId,
               true);
    expectBool("snapshot reference report keeps grouped records",
               snapshotReferenceReport.fileGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first().typeGroups.size() == 1
                   && !snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.isEmpty()
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencingSymbolRecord.stableKey == topStableKey
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencedSymbolRecord.stableKey == stageStableKey,
               true);
    expectBool("snapshot reference report keeps stable identity",
               !snapshotReferenceReport.references.isEmpty()
                   && snapshotReferenceReport.references.first().referencingStableKey
                          == topStableKey
                   && snapshotReferenceReport.references.first().referencedStableKey
                          == stageStableKey
                   && snapshotReferenceReport.references.first().referencingSymbolRecord.name
                          == QStringLiteral("rel_top")
                   && snapshotReferenceReport.references.first().referencedSymbolRecord.name
                          == QStringLiteral("rel_stage")
                   && snapshotReferenceReport.fileGroups.size() == 1
                   && snapshotReferenceReport.fileGroups.first().typeGroups.size() == 1
                   && !snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.isEmpty()
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencingStableKey == topStableKey
                   && snapshotReferenceReport.fileGroups.first()
                          .typeGroups.first()
                          .references.first()
                          .referencedStableKey == stageStableKey,
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
               snapshotNamedReferenceReport.subjectStableKey == stageStableKey
                   && snapshotNamedReferenceReport.totalCount == 1
                   && !snapshotNamedReferenceReport.references.isEmpty()
                   && snapshotNamedReferenceReport.references.first()
                          .referencingSymbolRecord.localHandle == topId,
               true);
    ReferenceQuery snapshotMissingReferenceQuery;
    snapshotMissingReferenceQuery.symbolName = QStringLiteral("missing_reference_subject");
    snapshotMissingReferenceQuery.fileName = topPath;
    const ReferenceReport snapshotMissingReferenceReport =
        snapshotReferenceService.findReferenceReport(snapshotMissingReferenceQuery);
    expectBool("snapshot reference report missing subject reason",
               !snapshotMissingReferenceReport.subjectStableKey.isValid()
                   && !snapshotMissingReferenceReport.subjectSymbolRecord.isValid()
                   && snapshotMissingReferenceReport.notFoundReason
                       == ReferenceReportNotFoundReason::NoSubjectSymbol
                   && snapshotMissingReferenceReport.notFoundReasonDisplayName
                       == QStringLiteral("no subject symbol"),
               true);
    expectBool("snapshot reference report missing subject display name",
               snapshotMissingReferenceReport.subjectDisplayName
                   == QStringLiteral("missing_reference_subject"),
               true);
    ReferenceQuery snapshotNoReferenceQuery;
    snapshotNoReferenceQuery.symbolStableKey = topStableKey;
    snapshotNoReferenceQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const ReferenceReport snapshotNoReferenceReport =
        snapshotReferenceService.findReferenceReport(snapshotNoReferenceQuery);
    expectBool("snapshot reference report no references reason",
               snapshotNoReferenceReport.subjectStableKey == topStableKey
                   && snapshotNoReferenceReport.totalCount == 0
                   && snapshotNoReferenceReport.notFoundReason
                       == ReferenceReportNotFoundReason::NoReferences
                   && snapshotNoReferenceReport.notFoundReasonDisplayName
                       == QStringLiteral("no references"),
               true);
    HierarchyService snapshotHierarchyService(&snapshotIndex);
    HierarchyQuery snapshotHierarchyQuery;
    snapshotHierarchyQuery.symbolStableKey = topStableKey;
    snapshotHierarchyQuery.maxDepth = 1;
    snapshotHierarchyQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<HierarchyNode> snapshotHierarchy =
        snapshotHierarchyService.getHierarchy(snapshotHierarchyQuery);
    bool snapshotHierarchyFoundStage = false;
    for (const HierarchyNode& node : snapshotHierarchy) {
        snapshotHierarchyFoundStage = snapshotHierarchyFoundStage
            || (node.depth == 1
                && node.parentStableKey == topStableKey
                && node.symbol.symbolId == stageId
                && node.symbolRecord.isValid()
                && node.symbolRecord.localHandle == stageId
                && node.symbolRecord.stableKey == stageStableKey
                && node.symbolRecord.name == QStringLiteral("rel_stage")
                && node.symbolStableKey == stageStableKey
                && node.parentStableKey == topStableKey
                && node.direction == HierarchyQuery::Children
                && node.viaType == SymbolRelationshipEngine::INSTANTIATES
                && node.directionDisplayName == QStringLiteral("Outgoing")
                && node.relationshipTypeDisplayName == QStringLiteral("Instantiates"));
    }
    expectBool("snapshot hierarchy service finds stage child",
               snapshotHierarchyFoundStage, true);
    HierarchyQuery snapshotStableHierarchyQuery = snapshotHierarchyQuery;
    snapshotStableHierarchyQuery.symbolStableKey = topStableKey;
    const QList<HierarchyNode> snapshotStableHierarchy =
        snapshotHierarchyService.getHierarchy(snapshotStableHierarchyQuery);
    expectBool("snapshot hierarchy service resolves stable query key",
               snapshotStableHierarchy.size() == snapshotHierarchy.size()
                   && !snapshotStableHierarchy.isEmpty()
                   && snapshotStableHierarchy.first().symbolStableKey == topStableKey
                   && snapshotStableHierarchy.first().symbol.symbolId == topId,
               true);
    const HierarchyReport snapshotHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotHierarchyQuery);
    expectBool("snapshot hierarchy report found reason metadata",
               snapshotHierarchyReport.notFoundReason
                       == HierarchyReportNotFoundReason::None
                   && snapshotHierarchyReport.notFoundReasonDisplayName.isEmpty(),
               true);
    expectBool("snapshot hierarchy report root semantic record",
               snapshotHierarchyReport.rootSymbolRecord.isValid()
                   && snapshotHierarchyReport.rootSymbolRecord.localHandle == topId
                   && snapshotHierarchyReport.rootSymbolRecord.stableKey == topStableKey
                   && snapshotHierarchyReport.rootSymbolRecord.name
                       == QStringLiteral("rel_top")
                   && snapshotHierarchyReport.rootStableKey == topStableKey,
               true);
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
                   && snapshotHierarchyReport.nodes.last().symbolRecord.isValid()
                   && snapshotHierarchyReport.nodes.last().symbolRecord.localHandle
                       == stageId
                   && snapshotHierarchyReport.nodes.last().symbolRecord.stableKey
                       == stageStableKey
                   && snapshotHierarchyReport.nodes.last().parentStableKey
                       == topStableKey,
               true);
    expectBool("snapshot hierarchy report keeps stable identity",
               snapshotHierarchyReport.nodes.size() == 2
                   && snapshotHierarchyReport.nodes.first().symbolStableKey == topStableKey
                   && !snapshotHierarchyReport.nodes.first().parentStableKey.isValid()
                   && snapshotHierarchyReport.nodes.last().symbolStableKey == stageStableKey
                   && snapshotHierarchyReport.nodes.last().parentStableKey == topStableKey,
               true);
    const HierarchyReport snapshotStableHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotStableHierarchyQuery);
    expectBool("snapshot hierarchy report resolves stable query key",
               snapshotStableHierarchyReport.totalCount
                   == snapshotHierarchyReport.totalCount
                   && snapshotStableHierarchyReport.rootStableKey == topStableKey
                   && !snapshotStableHierarchyReport.nodes.isEmpty()
                   && snapshotStableHierarchyReport.nodes.first().symbol.symbolId == topId,
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
    snapshotParentHierarchyQuery.symbolStableKey = stageStableKey;
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
                   && snapshotParentHierarchyReport.nodes.last().parentStableKey
                       == stageStableKey
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
    HierarchyQuery snapshotMissingHierarchyQuery;
    snapshotMissingHierarchyQuery.symbolName = QStringLiteral("missing_hierarchy_root");
    snapshotMissingHierarchyQuery.fileName = topPath;
    const HierarchyReport snapshotMissingHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotMissingHierarchyQuery);
    expectBool("snapshot hierarchy report missing root reason",
               snapshotMissingHierarchyReport.totalCount == 0
                   && snapshotMissingHierarchyReport.notFoundReason
                       == HierarchyReportNotFoundReason::NoRootSymbol
                   && snapshotMissingHierarchyReport.notFoundReasonDisplayName
                       == QStringLiteral("no root symbol"),
               true);
    HierarchyQuery snapshotNoHierarchyQuery;
    snapshotNoHierarchyQuery.symbolStableKey = stageStableKey;
    snapshotNoHierarchyQuery.maxDepth = 1;
    snapshotNoHierarchyQuery.types = {SymbolRelationshipEngine::CALLS};
    const HierarchyReport snapshotNoHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotNoHierarchyQuery);
    expectBool("snapshot hierarchy report no hierarchy reason",
               snapshotNoHierarchyReport.totalCount == 1
                   && snapshotNoHierarchyReport.notFoundReason
                       == HierarchyReportNotFoundReason::NoHierarchy
                   && snapshotNoHierarchyReport.notFoundReasonDisplayName
                       == QStringLiteral("no hierarchy"),
               true);
    SemanticRelationship duplicateStableRelationship;
    duplicateStableRelationship.fromId = topId + 100000;
    duplicateStableRelationship.toId = stageId + 100000;
    duplicateStableRelationship.type = SymbolRelationshipEngine::INSTANTIATES;
    duplicateStableRelationship.fromStableKey = topStableKey;
    duplicateStableRelationship.toStableKey = stageStableKey;
    const SemanticIndexSnapshot stableDedupedSnapshot =
        snapshot->withAdditionalRelationships({duplicateStableRelationship});
    expectInt("semantic snapshot merge deduplicates stable relationship",
              stableDedupedSnapshot.relationships().size(),
              snapshot->relationships().size());
    sym_list::SymbolInfo reboundTopSymbol = topSymbol;
    sym_list::SymbolInfo reboundStageSymbol = stageSymbol;
    reboundTopSymbol.symbolId = topId + 100000;
    reboundStageSymbol.symbolId = stageId + 100000;
    SemanticRelationship driftingStableRelationship;
    driftingStableRelationship.fromId = topId;
    driftingStableRelationship.toId = stageId;
    driftingStableRelationship.type = SymbolRelationshipEngine::INSTANTIATES;
    driftingStableRelationship.fromStableKey = topStableKey;
    driftingStableRelationship.toStableKey = stageStableKey;
    const auto reboundSnapshot = std::make_shared<SemanticIndexSnapshot>(
        QList<sym_list::SymbolInfo>{reboundTopSymbol, reboundStageSymbol},
        QList<SemanticRelationship>{driftingStableRelationship});
    const SemanticRelationship reboundRelationship =
        reboundSnapshot->rebindRelationship(driftingStableRelationship);
    expectBool("semantic snapshot rebinds stable relationship handles",
               reboundRelationship.fromId == reboundTopSymbol.symbolId
                   && reboundRelationship.toId == reboundStageSymbol.symbolId
                   && reboundRelationship.fromStableKey == topStableKey
                   && reboundRelationship.toStableKey == stageStableKey,
               true);
    const QList<SemanticRelationship> reboundRelationships =
        reboundSnapshot->getRelationships(topStableKey, true);
    expectBool("semantic snapshot queries rebound relationship",
               reboundRelationships.size() == 1
                   && reboundRelationships.first().fromId == reboundTopSymbol.symbolId
                   && reboundRelationships.first().toId == reboundStageSymbol.symbolId,
               true);
    const QList<SemanticRelationship> reboundStableRelationships =
        reboundSnapshot->getRelationships(topStableKey, true);
    expectBool("semantic snapshot queries rebound relationship by stable key",
               reboundStableRelationships.size() == 1
                   && reboundStableRelationships.first().fromId
                       == reboundTopSymbol.symbolId
                   && reboundStableRelationships.first().toId
                       == reboundStageSymbol.symbolId,
               true);
    SemanticIndex reboundIndex;
    reboundIndex.setSnapshot(reboundSnapshot);
    expectBool("semantic index resolves rebound stable key",
               reboundIndex.getSymbolByStableKey(topStableKey).symbolId
                       == reboundTopSymbol.symbolId
                   && reboundIndex.getSymbolByStableKey(stageStableKey).symbolId
                       == reboundStageSymbol.symbolId,
               true);
    const QList<SemanticRelationshipResult> reboundStableResults =
        reboundIndex.getRelationshipResults(topStableKey, true);
    expectBool("semantic index returns rebound relationship results by stable key",
               reboundStableResults.size() == 1
                   && reboundStableResults.first().fromSymbolRecord.stableKey
                       == topStableKey
                   && reboundStableResults.first().toSymbolRecord.stableKey
                       == stageStableKey,
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
    bool enrichedTaskHasStableKeys = false;
    const SymbolStableKey captureStableKey = symbolStableKeyForSymbol(captureSymbol);
    for (const SemanticRelationship& relationship :
         enrichedSnapshot.getRelationships(stageStableKey, true)) {
        enrichedFoundTask = enrichedFoundTask
            || (relationship.toId == captureId
                && relationship.type == SymbolRelationshipEngine::CALLS);
        enrichedTaskHasStableKeys = enrichedTaskHasStableKeys
            || (relationship.toId == captureId
                && relationship.type == SymbolRelationshipEngine::CALLS
                && relationship.fromStableKey == stageStableKey
                && relationship.toStableKey == captureStableKey);
    }
    expectBool("semantic snapshot merge keeps new relationship",
               enrichedFoundTask, true);
    expectBool("semantic snapshot merge fills new stable keys",
               enrichedTaskHasStableKeys, true);
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
    const QList<sym_list::SymbolInfo> restoredStageDefs =
        snapshotIndex.findDefinitions(QStringLiteral("rel_stage"), queryContext);
    expectBool("semantic snapshot clear restores live index",
               !restoredStageDefs.isEmpty()
                   && restoredStageDefs.first().symbolId == stageId,
               true);

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
            singleFileSchedulerResult.semanticSnapshot->getRelationships(topStableKey, true);
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
            schedulerResult.semanticSnapshot->getRelationships(topStableKey, true);
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
    relationshipQuery.symbolStableKey = topStableKey;
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
    bool serviceFoundStageMetadata = false;
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
        serviceFoundStageMetadata = serviceFoundStageMetadata
            || (rel.relationship.toId == stageId
                && rel.relationship.type == SymbolRelationshipEngine::INSTANTIATES
                && rel.provenance == RelationshipProvenance::Inferred
                && rel.confidence == 90
                && rel.evidenceText.contains(QStringLiteral("Instance:")));
    }

    expectBool("relationship service finds instantiation",
               serviceFoundStage, true);
    expectBool("relationship service finds call",
               serviceFoundTask, true);
    expectBool("relationship service finds condition read",
               serviceFoundRead, true);
    expectBool("relationship service carries relationship metadata",
               serviceFoundStageMetadata, true);
    expectBool("relationship service sorts first by type",
               !serviceRels.isEmpty()
                   && serviceRels.first().relationship.type
                       == SymbolRelationshipEngine::INSTANTIATES,
               true);
    RelationshipQuery relatedIdsQuery;
    relatedIdsQuery.symbolStableKey = topStableKey;
    relatedIdsQuery.outgoing = true;
    relatedIdsQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const SymbolStableKey stageQueryKey = stageStableKey;
    expectBool("relationship service returns related stable keys",
               relationshipService.findRelatedSymbolKeys(relatedIdsQuery).contains(stageQueryKey),
               true);
    RelationshipQuery stableRelatedIdsQuery = relatedIdsQuery;
    stableRelatedIdsQuery.symbolStableKey = topStableKey;
    expectBool("relationship service resolves stable query key",
               relationshipService
                   .findRelatedSymbolKeys(stableRelatedIdsQuery)
                   .contains(stageQueryKey),
               true);
    expectBool("relationship service keeps stable-key query result",
               relationshipService
                   .findRelatedSymbolKeys(stableRelatedIdsQuery)
                   .contains(stageQueryKey),
               true);
    expectBool("relationship service exact relationship",
               relationshipService.hasRelationship(topStableKey,
                                                   stageStableKey,
                                                   SymbolRelationshipEngine::INSTANTIATES),
               true);
    expectBool("relationship service exact named relationship",
               relationshipService.hasNamedRelationship(
                   QStringLiteral("rel_top"),
                   QStringLiteral("rel_stage"),
                   SymbolRelationshipEngine::INSTANTIATES),
               true);
    expectBool("relationship service rejects reversed relationship",
               relationshipService.hasRelationship(stageStableKey,
                                                   topStableKey,
                                                   SymbolRelationshipEngine::INSTANTIATES),
               false);
    expectBool("relationship service rejects reversed named relationship",
               relationshipService.hasNamedRelationship(
                   QStringLiteral("rel_stage"),
                   QStringLiteral("rel_top"),
                   SymbolRelationshipEngine::INSTANTIATES),
               false);
    RelationshipBrowseQuery browseQuery;
    browseQuery.symbolStableKey = topStableKey;
    browseQuery.includeOutgoing = true;
    browseQuery.includeIncoming = true;
    browseQuery.types = {
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::READS_FROM,
    };
    const RelationshipReport relationshipReport =
        relationshipService.findRelationshipReport(browseQuery);
    expectBool("relationship report subject display name",
               relationshipReport.subjectDisplayName == QStringLiteral("rel_top"),
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
    expectBool("relationship report keeps peer record",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first()
                          .peerSymbolRecord.localHandle == stageId,
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
    expectBool("relationship report row display metadata",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().peerSymbolDisplayName
                       == QStringLiteral("rel_stage")
                   && relationshipReport.relationships.first().peerFileDisplayName
                       == QFileInfo(stagePath).fileName()
                   && !relationshipReport.relationships.first()
                          .peerLineDisplayName.isEmpty(),
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
    expectBool("relationship report provenance metadata",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().provenance
                       == RelationshipProvenance::Inferred
                   && relationshipReport.relationships.first().provenanceDisplayName
                       == QStringLiteral("inferred"),
               true);
    expectBool("relationship report confidence metadata",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().confidence == 90
                   && relationshipReport.relationships.first().confidenceDisplayName
                       == QStringLiteral("90%"),
               true);
    expectBool("relationship report evidence metadata",
               !relationshipReport.relationships.isEmpty()
                   && relationshipReport.relationships.first().evidenceText.contains(
                       QStringLiteral("Instance:"))
                   && relationshipReport.relationships.first().evidenceDisplayName.contains(
                       QStringLiteral("Instance:")),
               true);
    RelationshipBrowseQuery stableRelationshipReportQuery = browseQuery;
    stableRelationshipReportQuery.symbolStableKey = topStableKey;
    const RelationshipReport stableRelationshipReport =
        relationshipService.findRelationshipReport(stableRelationshipReportQuery);
    expectBool("relationship report resolves stable query key",
               stableRelationshipReport.totalCount == relationshipReport.totalCount
                   && stableRelationshipReport.subjectStableKey
                       == stableRelationshipReportQuery.symbolStableKey
                   && stableRelationshipReport.subjectDisplayName
                       == QStringLiteral("rel_top"),
               true);
    RelationshipPanelQueryOptions outgoingPanelRelationshipOptions;
    outgoingPanelRelationshipOptions.symbolName = QStringLiteral("rel_top");
    outgoingPanelRelationshipOptions.fileName =
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv"));
    outgoingPanelRelationshipOptions.direction = RelationshipPanelDirection::Outgoing;
    outgoingPanelRelationshipOptions.typeFilter =
        static_cast<int>(SymbolRelationshipEngine::CALLS);
    const RelationshipBrowseQuery outgoingPanelRelationshipQuery =
        relationshipService.queryForPanel(outgoingPanelRelationshipOptions);
    expectBool("relationship panel query selects outgoing type",
               outgoingPanelRelationshipQuery.symbolName == QStringLiteral("rel_top")
                   && outgoingPanelRelationshipQuery.fileName == topPath
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
    incomingPanelRelationshipOptions.fileName =
        QDir(QFileInfo(stagePath).dir()).filePath(QStringLiteral("./relationship_stage.sv"));
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

    RelationshipBrowseQuery unnormalizedNamedBrowseQuery;
    unnormalizedNamedBrowseQuery.symbolName = QStringLiteral("rel_stage");
    unnormalizedNamedBrowseQuery.fileName =
        QDir(QFileInfo(stagePath).dir()).filePath(QStringLiteral("./relationship_stage.sv"));
    unnormalizedNamedBrowseQuery.includeOutgoing = false;
    unnormalizedNamedBrowseQuery.includeIncoming = true;
    unnormalizedNamedBrowseQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    expectInt("relationship report normalizes named browse file",
              relationshipService
                  .findRelationshipReport(unnormalizedNamedBrowseQuery)
                  .totalCount,
              1);

    RelationshipBrowseQuery callsOnlyBrowseQuery;
    callsOnlyBrowseQuery.symbolStableKey = topStableKey;
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
    expectBool("relationship report calls-only peer record",
               !callsOnlyReport.relationships.isEmpty()
                   && callsOnlyReport.relationships.first()
                          .peerSymbolRecord.localHandle == captureId,
               true);

    RelationshipBrowseQuery timingBrowseQuery;
    timingBrowseQuery.symbolStableKey = topStableKey;
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
                && relationship.peerSymbolRecord.localHandle == topClkId);
        timingReportHasResetPeer = timingReportHasResetPeer
            || (relationship.relationship.relationship.type == SymbolRelationshipEngine::RESETS
                && relationship.peerSymbolRecord.localHandle == topRstId);
    }
    expectBool("relationship report timing clock peer",
               timingReportHasClockPeer, true);
    expectBool("relationship report timing reset peer",
               timingReportHasResetPeer, true);

    RelationshipBrowseQuery incomingStageBrowseQuery;
    incomingStageBrowseQuery.symbolStableKey = stageStableKey;
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
    expectBool("relationship report incoming peer record",
               !incomingStageReport.relationships.isEmpty()
                   && incomingStageReport.relationships.first()
                          .peerSymbolRecord.localHandle == topId,
               true);
    expectBool("relationship report incoming explanation",
               !incomingStageReport.relationships.isEmpty()
                   && incomingStageReport.relationships.first().explanation
                       == QStringLiteral("rel_top instantiates rel_stage"),
               true);
    expectBool("relationship report incoming row metadata",
               !incomingStageReport.relationships.isEmpty()
                   && incomingStageReport.relationships.first().peerSymbolDisplayName
                       == QStringLiteral("rel_top")
                   && incomingStageReport.relationships.first().peerFileDisplayName
                       == QFileInfo(topPath).fileName()
                   && !incomingStageReport.relationships.first()
                          .peerLineDisplayName.isEmpty(),
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
                           .peerSymbolRecord.localHandle == topId,
               true);

    HierarchyService hierarchyService(&index);
    HierarchyQuery hierarchyQuery;
    hierarchyQuery.symbolStableKey = topStableKey;
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
                && node.parentStableKey == topStableKey
                && node.symbol.symbolId == stageId);
    }
    expectBool("hierarchy service includes root",
               hierarchyFoundRoot, true);
    expectBool("hierarchy service finds child instance",
               hierarchyFoundStage, true);
    const QList<HierarchyNode> stableModuleInstantiationChildren =
        hierarchyService.moduleInstantiationChildren(
            topStableKey);
    bool stableModuleInstantiationChildFoundStage = false;
    for (const HierarchyNode& node : stableModuleInstantiationChildren) {
        stableModuleInstantiationChildFoundStage =
            stableModuleInstantiationChildFoundStage
            || (node.symbol.symbolId == stageId
                && node.viaType == SymbolRelationshipEngine::INSTANTIATES);
    }
    expectBool("hierarchy service stable module instantiation children",
               stableModuleInstantiationChildFoundStage, true);
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
                && node.parentStableKey == topStableKey
                && node.symbol.symbolId == stageId
                && node.symbolRecord.isValid()
                && node.symbolRecord.localHandle == stageId
                && node.symbolRecord.stableKey == stageStableKey
                && node.symbolRecord.name == QStringLiteral("rel_stage")
                && node.direction == HierarchyQuery::Children
                && node.viaType == SymbolRelationshipEngine::INSTANTIATES
                && node.directionDisplayName == QStringLiteral("Outgoing")
                && node.relationshipTypeDisplayName == QStringLiteral("Instantiates")
                && node.symbolDisplayName == QStringLiteral("rel_stage")
                && node.fileDisplayName == QFileInfo(stagePath).fileName()
                && !node.lineDisplayName.isEmpty());
    }
    expectBool("hierarchy report keeps child row identity",
               hierarchyReportHasStageChild, true);
    expectBool("hierarchy report keeps root row metadata",
               !hierarchyReport.nodes.isEmpty()
                   && hierarchyReport.nodes.first().depth == 0
                   && hierarchyReport.nodes.first().symbolRecord.isValid()
                   && hierarchyReport.nodes.first().symbolRecord.localHandle == topId
                   && hierarchyReport.nodes.first().symbolRecord.stableKey == topStableKey
                   && hierarchyReport.nodes.first().symbolDisplayName
                       == QStringLiteral("rel_top")
                   && hierarchyReport.nodes.first().fileDisplayName
                       == QFileInfo(topPath).fileName()
                   && hierarchyReport.nodes.first().relationshipTypeDisplayName
                       == QStringLiteral("Root"),
               true);
    HierarchyQuery stableHierarchyQuery = hierarchyQuery;
    stableHierarchyQuery.symbolStableKey = topStableKey;
    const HierarchyReport stableHierarchyReport =
        hierarchyService.getHierarchyReport(stableHierarchyQuery);
    expectBool("hierarchy report resolves stable query key",
               stableHierarchyReport.totalCount == hierarchyReport.totalCount
                   && !stableHierarchyReport.nodes.isEmpty()
                   && stableHierarchyReport.nodes.first().symbolStableKey
                       == stableHierarchyQuery.symbolStableKey
                   && stableHierarchyReport.nodes.first().symbol.symbolName
                       == QStringLiteral("rel_top"),
               true);
    expectBool("hierarchy service exposes all tree types",
               HierarchyService::allRelationshipTypes().contains(SymbolRelationshipEngine::READS_FROM),
               true);

    HierarchyQuery parentQuery;
    parentQuery.symbolStableKey = stageStableKey;
    parentQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<HierarchyNode> parents = hierarchyService.getParents(parentQuery);
    bool parentFoundTop = false;
    for (const HierarchyNode& node : parents) {
        parentFoundTop = parentFoundTop
            || (node.symbol.symbolId == topId
                && node.parentStableKey == stageStableKey);
    }
    expectBool("hierarchy service finds parent instance",
               parentFoundTop, true);

    HierarchyQuery parentTreeQuery;
    parentTreeQuery.symbolStableKey = stageStableKey;
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
                && node.parentStableKey == stageStableKey
                && node.symbol.symbolId == topId
                && node.direction == HierarchyQuery::Parents);
    }
    expectBool("hierarchy service parent tree includes root",
               parentTreeFoundRoot, true);
    expectBool("hierarchy service parent tree finds incoming parent",
               parentTreeFoundTop, true);
    HierarchyPanelQueryOptions incomingHierarchyPanelOptions;
    incomingHierarchyPanelOptions.symbolName = QStringLiteral("rel_stage");
    incomingHierarchyPanelOptions.fileName =
        QDir(QFileInfo(stagePath).dir()).filePath(QStringLiteral("./relationship_stage.sv"));
    incomingHierarchyPanelOptions.maxDepth = 1;
    incomingHierarchyPanelOptions.direction = HierarchyPanelDirection::Incoming;
    incomingHierarchyPanelOptions.typeFilter =
        static_cast<int>(SymbolRelationshipEngine::INSTANTIATES);
    const HierarchyQuery incomingHierarchyPanelQuery =
        hierarchyService.queryForPanel(incomingHierarchyPanelOptions);
    expectBool("hierarchy panel query selects incoming type",
               incomingHierarchyPanelQuery.symbolName == QStringLiteral("rel_stage")
                   && incomingHierarchyPanelQuery.fileName == stagePath
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
    HierarchyQuery unnormalizedNamedHierarchyQuery;
    unnormalizedNamedHierarchyQuery.symbolName = QStringLiteral("rel_stage");
    unnormalizedNamedHierarchyQuery.fileName =
        QDir(QFileInfo(stagePath).dir()).filePath(QStringLiteral("./relationship_stage.sv"));
    unnormalizedNamedHierarchyQuery.maxDepth = 1;
    unnormalizedNamedHierarchyQuery.direction = HierarchyQuery::Parents;
    unnormalizedNamedHierarchyQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    expectInt("hierarchy report normalizes named query file",
              hierarchyService
                  .getHierarchyReport(unnormalizedNamedHierarchyQuery)
                  .rootDirectionCounts.value(HierarchyQuery::Parents),
              1);

    HierarchyPanelQueryOptions allHierarchyPanelOptions;
    allHierarchyPanelOptions.symbolName = QStringLiteral("rel_top");
    allHierarchyPanelOptions.fileName =
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv"));
    allHierarchyPanelOptions.maxDepth = 2;
    allHierarchyPanelOptions.direction = HierarchyPanelDirection::All;
    allHierarchyPanelOptions.typeFilter = -1;
    const HierarchyQuery allHierarchyPanelQuery =
        hierarchyService.queryForPanel(allHierarchyPanelOptions);
    expectBool("hierarchy panel query selects all tree types",
               allHierarchyPanelQuery.direction == HierarchyQuery::Both
                   && allHierarchyPanelQuery.fileName == topPath
                   && allHierarchyPanelQuery.types.contains(SymbolRelationshipEngine::READS_FROM)
                   && allHierarchyPanelQuery.types.contains(SymbolRelationshipEngine::INSTANTIATES),
               true);

    engine.addRelationship(stageId, topId, SymbolRelationshipEngine::INSTANTIATES,
                           QStringLiteral("cycle guard probe"));
    HierarchyQuery cycleQuery;
    cycleQuery.symbolStableKey = topStableKey;
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
    stageReferenceQuery.symbolStableKey = stageStableKey;
    stageReferenceQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<ReferenceResult> stageReferences =
        referenceService.findReferences(stageReferenceQuery);
    bool referenceFoundTopInstance = false;
    for (const ReferenceResult& ref : stageReferences) {
        referenceFoundTopInstance = referenceFoundTopInstance
            || (ref.referencingSymbolRecord.localHandle == topId
                && ref.referencedSymbolRecord.localHandle == stageId
                && ref.relationship.relationship.type == SymbolRelationshipEngine::INSTANTIATES);
    }
    expectBool("reference service finds stage instantiation",
               referenceFoundTopInstance, true);
    const ReferenceReport stageReferenceReport =
        referenceService.findReferenceReport(stageReferenceQuery);
    expectBool("reference report subject record",
               stageReferenceReport.subjectSymbolRecord.name == QStringLiteral("rel_stage"),
               true);
    expectBool("reference report subject display name",
               stageReferenceReport.subjectDisplayName == QStringLiteral("rel_stage"),
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
    expectBool("reference report row display metadata",
               !stageReferenceReport.references.isEmpty()
                   && stageReferenceReport.references.first().symbolDisplayName
                       == QStringLiteral("rel_top")
                   && stageReferenceReport.references.first().fileDisplayName
                       == QFileInfo(topPath).fileName()
                   && !stageReferenceReport.references.first().lineDisplayName.isEmpty()
                   && stageReferenceReport.references.first()
                          .relationshipTypeDisplayName
                       == QStringLiteral("Instantiates"),
               true);
    expectInt("reference report grouped result count",
              stageReferenceReport.fileGroups.isEmpty()
                  || stageReferenceReport.fileGroups.first().typeGroups.isEmpty()
                  ? 0
                  : stageReferenceReport.fileGroups.first().typeGroups.first().references.size(),
              1);
    expectBool("reference report grouped records",
               !stageReferenceReport.fileGroups.isEmpty()
                   && !stageReferenceReport.fileGroups.first().typeGroups.isEmpty()
                   && !stageReferenceReport.fileGroups.first()
                           .typeGroups.first()
                           .references.isEmpty()
                   && stageReferenceReport.fileGroups.first()
                           .typeGroups.first()
                           .references.first()
                           .referencingSymbolRecord.localHandle == topId
                   && stageReferenceReport.fileGroups.first()
                           .typeGroups.first()
                           .references.first()
                           .referencedSymbolRecord.localHandle == stageId,
               true);
    ReferenceQuery stableStageReferenceQuery = stageReferenceQuery;
    stableStageReferenceQuery.symbolStableKey = stageStableKey;
    const ReferenceReport stableStageReferenceReport =
        referenceService.findReferenceReport(stableStageReferenceQuery);
    expectBool("reference report resolves stable query key",
               stableStageReferenceReport.totalCount == stageReferenceReport.totalCount
                   && stableStageReferenceReport.subjectStableKey
                       == stableStageReferenceQuery.symbolStableKey
                   && stableStageReferenceReport.subjectDisplayName
                       == QStringLiteral("rel_stage"),
               true);

    ReferenceQuery currentFileStageReferenceQuery = stageReferenceQuery;
    currentFileStageReferenceQuery.fileName = stagePath;
    currentFileStageReferenceQuery.currentFileOnly = true;
    expectInt("reference report current file filter",
              referenceService.findReferenceReport(currentFileStageReferenceQuery).totalCount, 0);
    currentFileStageReferenceQuery.fileName =
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv"));
    expectInt("reference report current file keeps matching file",
              referenceService.findReferenceReport(currentFileStageReferenceQuery).totalCount, 1);

    ReferenceQuery workspaceStageReferenceQuery = stageReferenceQuery;
    workspaceStageReferenceQuery.workspaceFilesOnly = true;
    workspaceStageReferenceQuery.workspaceFiles = {
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv")),
        topPath,
    };
    expectInt("reference report workspace filter",
              referenceService.findReferenceReport(workspaceStageReferenceQuery).totalCount, 1);
    workspaceStageReferenceQuery.workspaceFiles = {stagePath};
    expectInt("reference report workspace filter hides other file",
              referenceService.findReferenceReport(workspaceStageReferenceQuery).totalCount, 0);

    ReferencePanelQueryOptions currentFileReferenceOptions;
    currentFileReferenceOptions.symbolName = QStringLiteral("rel_stage");
    currentFileReferenceOptions.fileName =
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv"));
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
    workspaceReferenceOptions.workspaceFiles = {
        QDir(QFileInfo(stagePath).dir()).filePath(QStringLiteral("./relationship_stage.sv")),
        stagePath,
    };
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
    reqValidReferenceQuery.symbolStableKey =
        symbolStableKeyForSymbol(symbolById(topSymbols, reqValidId));
    reqValidReferenceQuery.types = {SymbolRelationshipEngine::READS_FROM};
    const QList<ReferenceResult> reqValidReferences =
        referenceService.findReferences(reqValidReferenceQuery);
    bool referenceFoundReqRead = false;
    for (const ReferenceResult& ref : reqValidReferences) {
        referenceFoundReqRead = referenceFoundReqRead
            || (ref.referencingSymbolRecord.localHandle == topId
                && ref.referencedSymbolRecord.localHandle == reqValidId
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
    expectBool("reference report condition read subject display name",
               reqValidReferenceReport.subjectDisplayName == QStringLiteral("req_valid"),
               true);

    ReferenceQuery topTimingReferenceQuery;
    topTimingReferenceQuery.symbolStableKey = topStableKey;
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
    expectBool("reference report clock subject display name",
               topClockReferenceReport.subjectDisplayName == QStringLiteral("rel_top"),
               true);
    expectBool("reference report clock referencing symbol",
               !topClockReferenceReport.references.isEmpty()
                   && topClockReferenceReport.references.first()
                          .referencingSymbolRecord.localHandle == topClkId,
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
    expectBool("reference report reset subject display name",
               topResetReferenceReport.subjectDisplayName == QStringLiteral("rel_top"),
               true);
    expectBool("reference report reset referencing symbol",
               !topResetReferenceReport.references.isEmpty()
                   && topResetReferenceReport.references.first()
                          .referencingSymbolRecord.localHandle == topRstId,
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
    rspDataReferenceQuery.symbolStableKey =
        symbolStableKeyForSymbol(symbolById(topSymbols, rspDataId));
    rspDataReferenceQuery.types = {SymbolRelationshipEngine::ASSIGNS_TO};
    const QList<ReferenceResult> rspDataReferences =
        referenceService.findReferences(rspDataReferenceQuery);
    bool referenceFoundRspWrite = false;
    for (const ReferenceResult& ref : rspDataReferences) {
        referenceFoundRspWrite = referenceFoundRspWrite
            || (ref.referencingSymbolRecord.localHandle == stageDataId
                && ref.referencedSymbolRecord.localHandle == rspDataId
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
    expectBool("reference report assignment write subject display name",
               rspDataReferenceReport.subjectDisplayName == QStringLiteral("rsp_data"),
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
        9012,
        fileName,
        QStringLiteral("PKG_WIDTH"),
        sym_list::sym_parameter,
        2,
        QStringLiteral("brief_pkg")));
    symbols.append(makeModuleBriefSymbol(
        9013,
        fileName,
        QStringLiteral("brief_t"),
        sym_list::sym_typedef,
        4,
        QStringLiteral("brief_pkg")));
    symbols.append(makeModuleBriefSymbol(
        9009,
        fileName,
        QStringLiteral("brief_if"),
        sym_list::sym_interface,
        3));
    sym_list::SymbolInfo interfacePort = makeModuleBriefSymbol(
        9010,
        fileName,
        QStringLiteral("if_port"),
        sym_list::sym_port_interface_modport,
        15,
        QStringLiteral("brief_top"));
    interfacePort.dataType = QStringLiteral("brief_if.master");
    symbols.append(interfacePort);
    sym_list::SymbolInfo interfaceInstance = makeModuleBriefSymbol(
        9011,
        fileName,
        QStringLiteral("if_bus"),
        sym_list::sym_inst,
        31,
        QStringLiteral("brief_top"));
    interfaceInstance.dataType = QStringLiteral("brief_if");
    symbols.append(interfaceInstance);
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
    importRel.provenance = RelationshipProvenance::Inferred;
    importRel.confidence = 80;
    importRel.evidenceText = QStringLiteral("Package import at line 1");
    relationships.append(importRel);

    SemanticRelationship instRel;
    instRel.fromId = 9001;
    instRel.toId = 9006;
    instRel.type = SymbolRelationshipEngine::INSTANTIATES;
    instRel.provenance = RelationshipProvenance::Inferred;
    instRel.confidence = 90;
    instRel.evidenceText = QStringLiteral("Instance: u_stage at line 30");
    relationships.append(instRel);

    SemanticRelationship clockRel;
    clockRel.fromId = 9003;
    clockRel.toId = 9001;
    clockRel.type = SymbolRelationshipEngine::CLOCKS;
    clockRel.provenance = RelationshipProvenance::Inferred;
    clockRel.confidence = 95;
    clockRel.evidenceText = QStringLiteral("Clock signal clk at line 12");
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
    expectBool("module brief subject stable key",
               report.moduleStableKey == symbolStableKeyForSymbol(report.moduleSymbol),
               true);
    expectBool("module brief subject semantic record",
               report.moduleSymbolRecord.isValid()
                   && report.moduleSymbolRecord.localHandle == report.moduleSymbol.symbolId
                   && report.moduleSymbolRecord.stableKey == report.moduleStableKey
                   && report.moduleSymbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && report.moduleSymbolRecord.name == QStringLiteral("brief_top"),
               true);
    ModuleBriefQuery stableModuleBriefQuery;
    stableModuleBriefQuery.moduleStableKey = symbolStableKeyForSymbol(module);
    const ModuleBriefReport stableModuleBriefReport =
        service.buildModuleBrief(stableModuleBriefQuery);
    expectBool("module brief resolves stable module key",
               stableModuleBriefReport.found
                   && stableModuleBriefReport.moduleSymbol.symbolName
                       == QStringLiteral("brief_top")
                   && stableModuleBriefReport.moduleStableKey
                       == stableModuleBriefQuery.moduleStableKey,
               true);
    expectBool("module brief stable query keeps relationship metadata",
               stableModuleBriefReport.relationshipSummary.totalCount
                       == report.relationshipSummary.totalCount
                   && stableModuleBriefReport.relationshipEvidenceRows.size()
                       == report.relationshipEvidenceRows.size()
                   && !stableModuleBriefReport.relationshipEvidenceRows.isEmpty()
                   && stableModuleBriefReport.relationshipEvidenceRows.first().fromStableKey
                       == report.relationshipEvidenceRows.first().fromStableKey,
               true);
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
    expectBool("snapshot attaches module semantic metadata",
               report.moduleSymbol.hasSemanticMetadata
                   && report.moduleSymbol.semanticDeclarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && report.moduleSymbol.semanticOwnerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Global
                   && report.moduleSymbol.semanticVisibility
                       == SymbolTaxonomy::SymbolVisibility::Global
                   && report.moduleSymbol.rawCollectorKind == sym_list::sym_module,
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
    const QList<sym_list::SymbolInfo> snapshotSymbols =
        index.snapshot()->getSymbols(fileName);
    sym_list::SymbolInfo snapshotPort;
    for (const sym_list::SymbolInfo& symbol : snapshotSymbols) {
        if (symbol.symbolName == QStringLiteral("clk")
            && symbol.symbolType == sym_list::sym_port_input) {
            snapshotPort = symbol;
            break;
        }
    }
    expectBool("snapshot attaches port semantic metadata",
               snapshotPort.hasSemanticMetadata
                   && snapshotPort.semanticDeclarationKind
                       == SymbolTaxonomy::DeclarationKind::Port
                   && snapshotPort.semanticUsageRole
                       == SymbolTaxonomy::SymbolUsageRole::Declaration
                   && snapshotPort.semanticSourceRole
                       == SymbolTaxonomy::SourceRole::DesignSource
                   && snapshotPort.rawCollectorKind == sym_list::sym_port_input,
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
    QList<sym_list::SymbolInfo> packageSymbols;
    packageSymbols.append(makeModuleBriefSymbol(100,
                                                fileName,
                                                QStringLiteral("brief_pkg"),
                                                sym_list::sym_package,
                                                1));
    packageSymbols.append(makeModuleBriefSymbol(101,
                                                fileName,
                                                QStringLiteral("WIDTH"),
                                                sym_list::sym_parameter,
                                                2,
                                                QStringLiteral("brief_pkg")));
    const SemanticIndexSnapshot packageSnapshot(packageSymbols);
    const QList<sym_list::SymbolInfo> annotatedPackageSymbols =
        packageSnapshot.getSymbols(fileName);
    sym_list::SymbolInfo packageParameter;
    for (const sym_list::SymbolInfo& symbol : annotatedPackageSymbols) {
        if (symbol.symbolName == QStringLiteral("WIDTH")) {
            packageParameter = symbol;
            break;
        }
    }
    expectBool("snapshot attaches package visibility metadata",
               packageParameter.hasSemanticMetadata
                   && packageParameter.semanticOwnerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Package
                   && packageParameter.semanticVisibility
                       == SymbolTaxonomy::SymbolVisibility::PackageVisible,
               true);
    expectInt("module brief port count", report.ports.size(), 4);
    expectInt("module brief parameter count", report.parameters.size(), 1);
    expectInt("module brief instance count", report.instances.size(), 2);
    expectInt("module brief import count", report.imports.size(), 1);
    expectInt("module brief diagnostic count", report.diagnostics.size(), 1);
    expectInt("module brief port row count", report.portRows.size(), 4);
    expectInt("module brief parameter row count", report.parameterRows.size(), 1);
    expectInt("module brief instance row count", report.instanceRows.size(), 2);
    expectInt("module brief import row count", report.importRows.size(), 1);
    expectInt("module brief diagnostic row count", report.diagnosticRows.size(), 1);
    expectInt("module brief context row count", report.contextRows.size(), 5);
    expectInt("module brief relationship evidence row count",
              report.relationshipEvidenceRows.size(),
              3);
    expectBool("module brief port row display metadata",
               !report.portRows.isEmpty()
                   && report.portRows.first().symbolRecord.isValid()
                   && report.portRows.first().symbolRecord.stableKey
                       == symbolStableKeyForSymbol(report.portRows.first().symbol)
                   && report.portRows.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Port
                   && report.portRows.first().symbolRecord.name == QStringLiteral("clk")
                   && report.portRows.first().sectionDisplayName == QStringLiteral("Port")
                   && !report.portRows.first().typeDisplayName.isEmpty()
                   && !report.portRows.first().detailDisplayName.isEmpty(),
               true);
    expectBool("module brief port row code link",
               !report.portRows.isEmpty()
                   && report.portRows.first().codeLink.fileName == fileName
                   && report.portRows.first().codeLink.line == 12
                   && report.portRows.first().codeLink.column == 1
                   && report.portRows.first().codeLink.fileDisplayName
                       == QStringLiteral("module_brief_fixture.sv")
                   && report.portRows.first().codeLink.lineDisplayName
                       == QStringLiteral("12"),
               true);
    expectBool("module brief parameter row display metadata",
               !report.parameterRows.isEmpty()
                   && report.parameterRows.first().symbolRecord.isValid()
                   && report.parameterRows.first().symbolRecord.localHandle == 9002
                   && report.parameterRows.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Parameter
                   && report.parameterRows.first().sectionDisplayName
                       == QStringLiteral("Parameter")
                   && !report.parameterRows.first().detailDisplayName.isEmpty(),
               true);
    expectBool("module brief parameter row code link",
               !report.parameterRows.isEmpty()
                   && report.parameterRows.first().codeLink.fileName == fileName
                   && report.parameterRows.first().codeLink.line == 11
                   && report.parameterRows.first().codeLink.column == 1
                   && report.parameterRows.first().codeLink.fileDisplayName
                       == QStringLiteral("module_brief_fixture.sv")
                   && report.parameterRows.first().codeLink.lineDisplayName
                       == QStringLiteral("11"),
               true);
    expectBool("module brief diagnostic row display metadata",
               !report.diagnosticRows.isEmpty()
                   && report.diagnosticRows.first().severityDisplayName
                       == QStringLiteral("Warning")
                   && report.diagnosticRows.first().detailDisplayName
                       == QStringLiteral("diagnostic")
                   && report.diagnosticRows.first().sourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);
    expectBool("module brief diagnostic row code link",
               !report.diagnosticRows.isEmpty()
                   && report.diagnosticRows.first().codeLink.fileName == fileName
                   && report.diagnosticRows.first().codeLink.line == 32
                   && report.diagnosticRows.first().codeLink.column == 5
                   && report.diagnosticRows.first().codeLink.fileDisplayName
                       == QStringLiteral("module_brief_fixture.sv")
                   && report.diagnosticRows.first().codeLink.lineDisplayName
                       == QStringLiteral("32"),
               true);
    expectBool("module brief import package",
               !report.imports.isEmpty()
                   && report.imports.first().symbolName == QStringLiteral("brief_pkg"),
               true);
    expectBool("module brief import row semantic record",
               !report.importRows.isEmpty()
                   && report.importRows.first().symbolRecord.isValid()
                   && report.importRows.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Package
                   && report.importRows.first().symbolRecord.name
                       == QStringLiteral("brief_pkg"),
               true);
    bool hasPackageContext = false;
    bool hasInterfacePortContext = false;
    bool hasInterfaceInstanceContext = false;
    bool hasPackageContextLink = false;
    bool hasInterfacePortContextLink = false;
    bool hasInterfaceInstanceContextLink = false;
    bool hasPackageContextMetadata = false;
    bool hasInterfacePortContextMetadata = false;
    bool hasInterfaceInstanceContextMetadata = false;
    bool hasPackageParameterContext = false;
    bool hasPackageTypedefContext = false;
    for (const ModuleBriefContextRow& row : report.contextRows) {
        hasPackageContext = hasPackageContext
            || (row.sectionDisplayName == QStringLiteral("Package")
                && row.symbolRecord.isValid()
                && row.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Package
                && row.symbolRecord.name == QStringLiteral("brief_pkg")
                && row.symbolDisplayName == QStringLiteral("brief_pkg")
                && row.detailDisplayName == QStringLiteral("package import"));
        hasPackageContextMetadata = hasPackageContextMetadata
            || (row.sectionDisplayName == QStringLiteral("Package")
                && row.symbolDisplayName == QStringLiteral("brief_pkg")
                && row.symbolRecord.sourceRole
                    == SymbolTaxonomy::SourceRole::DesignSource
                && row.contextKindDisplayName == QStringLiteral("package import")
                && row.symbolTypeDisplayName == QStringLiteral("package")
                && row.sourceRoleDisplayName == QStringLiteral("design source"));
        hasPackageContextLink = hasPackageContextLink
            || (row.sectionDisplayName == QStringLiteral("Package")
                && row.symbolDisplayName == QStringLiteral("brief_pkg")
                && row.codeLink.fileName == fileName
                && row.codeLink.line == 1
                && row.codeLink.column == 1
                && row.codeLink.fileDisplayName
                    == QStringLiteral("module_brief_fixture.sv")
                && row.codeLink.lineDisplayName == QStringLiteral("1"));
        hasPackageParameterContext = hasPackageParameterContext
            || (row.sectionDisplayName == QStringLiteral("Package Member")
                && row.symbolRecord.isValid()
                && row.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Parameter
                && row.symbolRecord.owner.kind
                    == SymbolTaxonomy::SymbolOwnerScope::Package
                && row.symbolRecord.owner.name == QStringLiteral("brief_pkg")
                && row.symbolDisplayName == QStringLiteral("PKG_WIDTH")
                && row.contextKindDisplayName == QStringLiteral("package parameter")
                && row.symbolTypeDisplayName == QStringLiteral("parameter")
                && row.sourceRoleDisplayName == QStringLiteral("design source")
                && row.codeLink.fileName == fileName
                && row.codeLink.line == 2
                && row.codeLink.lineDisplayName == QStringLiteral("2"));
        hasPackageTypedefContext = hasPackageTypedefContext
            || (row.sectionDisplayName == QStringLiteral("Package Member")
                && row.symbolRecord.isValid()
                && row.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Typedef
                && row.symbolRecord.owner.kind
                    == SymbolTaxonomy::SymbolOwnerScope::Package
                && row.symbolRecord.owner.name == QStringLiteral("brief_pkg")
                && row.symbolDisplayName == QStringLiteral("brief_t")
                && row.contextKindDisplayName == QStringLiteral("package typedef")
                && row.symbolTypeDisplayName == QStringLiteral("typedef")
                && row.sourceRoleDisplayName == QStringLiteral("design source")
                && row.codeLink.fileName == fileName
                && row.codeLink.line == 4
                && row.codeLink.lineDisplayName == QStringLiteral("4"));
        hasInterfacePortContext = hasInterfacePortContext
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && row.symbolRecord.isValid()
                && row.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Port
                && row.symbolRecord.owner.interfaceLike
                && row.symbolDisplayName == QStringLiteral("if_port")
                && row.detailDisplayName.contains(QStringLiteral("brief_if.master")));
        hasInterfacePortContextMetadata = hasInterfacePortContextMetadata
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && row.symbolDisplayName == QStringLiteral("if_port")
                && row.contextKindDisplayName == QStringLiteral("interface port")
                && row.symbolTypeDisplayName == QStringLiteral("modport port")
                && row.sourceRoleDisplayName == QStringLiteral("design source"));
        hasInterfacePortContextLink = hasInterfacePortContextLink
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && row.symbolDisplayName == QStringLiteral("if_port")
                && row.codeLink.fileName == fileName
                && row.codeLink.line == 15
                && row.codeLink.column == 1
                && row.codeLink.fileDisplayName
                    == QStringLiteral("module_brief_fixture.sv")
                && row.codeLink.lineDisplayName == QStringLiteral("15"));
        hasInterfaceInstanceContext = hasInterfaceInstanceContext
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && row.symbolRecord.isValid()
                && row.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Instance
                && row.symbolRecord.type.resolvedTypeName
                    == QStringLiteral("brief_if")
                && row.symbolDisplayName == QStringLiteral("if_bus")
                && row.detailDisplayName.contains(QStringLiteral("brief_if")));
        hasInterfaceInstanceContextMetadata = hasInterfaceInstanceContextMetadata
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && row.symbolDisplayName == QStringLiteral("if_bus")
                && row.contextKindDisplayName == QStringLiteral("interface instance")
                && row.symbolTypeDisplayName == QStringLiteral("instance")
                && row.sourceRoleDisplayName == QStringLiteral("design source"));
        hasInterfaceInstanceContextLink = hasInterfaceInstanceContextLink
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && row.symbolDisplayName == QStringLiteral("if_bus")
                && row.codeLink.fileName == fileName
                && row.codeLink.line == 31
                && row.codeLink.column == 1
                && row.codeLink.fileDisplayName
                    == QStringLiteral("module_brief_fixture.sv")
                && row.codeLink.lineDisplayName == QStringLiteral("31"));
    }
    expectBool("module brief package context row", hasPackageContext, true);
    expectBool("module brief package context metadata",
               hasPackageContextMetadata,
               true);
    expectBool("module brief package context code link",
               hasPackageContextLink,
               true);
    expectBool("module brief package parameter context",
               hasPackageParameterContext,
               true);
    expectBool("module brief package typedef context",
               hasPackageTypedefContext,
               true);
    expectBool("module brief interface port context row", hasInterfacePortContext, true);
    expectBool("module brief interface port context metadata",
               hasInterfacePortContextMetadata,
               true);
    expectBool("module brief interface port context code link",
               hasInterfacePortContextLink,
               true);
    expectBool("module brief interface instance context row",
               hasInterfaceInstanceContext,
               true);
    expectBool("module brief interface instance context metadata",
               hasInterfaceInstanceContextMetadata,
               true);
    expectBool("module brief interface instance context code link",
               hasInterfaceInstanceContextLink,
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
    bool hasPackageRelationshipEvidence = false;
    bool hasInstanceRelationshipEvidence = false;
    bool hasInstanceRelationshipMetadata = false;
    bool hasClockRelationshipEvidence = false;
    bool hasClockRelationshipEvidenceLink = false;
    bool hasClockRelationshipEndpointLinks = false;
    for (const ModuleBriefRelationshipEvidenceRow& row
         : report.relationshipEvidenceRows) {
        hasPackageRelationshipEvidence = hasPackageRelationshipEvidence
            || (row.outgoing
                && row.peerDisplayName == QStringLiteral("brief_pkg")
                && row.typeDisplayName == QStringLiteral("References")
                && row.detailDisplayName == QStringLiteral("Outgoing References"));
        hasInstanceRelationshipEvidence = hasInstanceRelationshipEvidence
            || (row.outgoing
                && row.peerDisplayName == QStringLiteral("u_stage")
                && row.typeDisplayName == QStringLiteral("Instantiates")
                && row.detailDisplayName == QStringLiteral("Outgoing Instantiates"));
        hasInstanceRelationshipMetadata = hasInstanceRelationshipMetadata
            || (row.outgoing
                && row.peerDisplayName == QStringLiteral("u_stage")
                && row.fromStableKey == symbolStableKeyForSymbol(row.fromSymbol)
                && row.toStableKey == symbolStableKeyForSymbol(row.toSymbol)
                && row.peerStableKey == row.toStableKey
                && row.provenance == RelationshipProvenance::Inferred
                && row.confidence == 90
                && row.evidenceText.contains(QStringLiteral("Instance: u_stage"))
                && row.provenanceDisplayName == QStringLiteral("inferred")
                && row.confidenceDisplayName == QStringLiteral("90%")
                && row.evidenceDisplayName.contains(QStringLiteral("Instance: u_stage")));
        hasClockRelationshipEvidence = hasClockRelationshipEvidence
            || (!row.outgoing
                && row.peerDisplayName == QStringLiteral("clk")
                && row.typeDisplayName == QStringLiteral("Clocks")
                && row.detailDisplayName == QStringLiteral("Incoming Clocks"));
        hasClockRelationshipEvidenceLink = hasClockRelationshipEvidenceLink
            || (!row.outgoing
                && row.peerDisplayName == QStringLiteral("clk")
                && row.peerCodeLink.fileName == fileName
                && row.peerCodeLink.line == 12
                && row.peerCodeLink.column == 1
                && row.peerCodeLink.fileDisplayName
                    == QStringLiteral("module_brief_fixture.sv")
                && row.peerCodeLink.lineDisplayName == QStringLiteral("12"));
        hasClockRelationshipEndpointLinks = hasClockRelationshipEndpointLinks
            || (!row.outgoing
                && row.fromSymbolDisplayName == QStringLiteral("clk")
                && row.toSymbolDisplayName == QStringLiteral("brief_top")
                && row.fromCodeLink.fileName == fileName
                && row.fromCodeLink.line == 12
                && row.fromCodeLink.fileDisplayName
                    == QStringLiteral("module_brief_fixture.sv")
                && row.toCodeLink.fileName == fileName
                && row.toCodeLink.line == 10
                && row.toCodeLink.fileDisplayName
                    == QStringLiteral("module_brief_fixture.sv"));
    }
    expectBool("module brief package relationship evidence",
               hasPackageRelationshipEvidence,
               true);
    expectBool("module brief instance relationship evidence",
               hasInstanceRelationshipEvidence,
               true);
    expectBool("module brief instance relationship metadata",
               hasInstanceRelationshipMetadata,
               true);
    expectBool("module brief clock relationship evidence",
               hasClockRelationshipEvidence,
               true);
    expectBool("module brief clock relationship evidence code link",
               hasClockRelationshipEvidenceLink,
               true);
    expectBool("module brief clock relationship endpoint links",
               hasClockRelationshipEndpointLinks,
               true);

    ModuleBriefQuery emptyModuleQuery;
    const ModuleBriefReport emptyModuleReport =
        service.buildModuleBrief(emptyModuleQuery);
    expectBool("module brief empty module reason",
               !emptyModuleReport.found
                   && emptyModuleReport.notFoundReason
                       == ModuleBriefNotFoundReason::EmptyModuleName
                   && emptyModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("empty module name"),
               true);

    ModuleBriefQuery missingModuleQuery;
    missingModuleQuery.moduleName = QStringLiteral("missing_module");
    missingModuleQuery.fileName = fileName;
    const ModuleBriefReport missingModuleReport =
        service.buildModuleBrief(missingModuleQuery);
    expectBool("module brief missing module reason",
               !missingModuleReport.found
                   && missingModuleReport.notFoundReason
                       == ModuleBriefNotFoundReason::NoMatchingModule
                   && missingModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("no matching module"),
               true);

    ModuleBriefQuery unsupportedModuleQuery;
    unsupportedModuleQuery.moduleName = QStringLiteral("clk");
    unsupportedModuleQuery.fileName = fileName;
    const ModuleBriefReport unsupportedModuleReport =
        service.buildModuleBrief(unsupportedModuleQuery);
    expectBool("module brief unsupported symbol reason",
               !unsupportedModuleReport.found
                   && unsupportedModuleReport.notFoundReason
                       == ModuleBriefNotFoundReason::UnsupportedSymbolKind
                   && unsupportedModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("unsupported symbol kind"),
               true);
}

static void runScopeBandServiceFixture()
{
    printf("\n-- scope band service fixture --\n");

    const QString fileName = QStringLiteral("scope_band_fixture.sv");
    QList<sym_list::SymbolInfo> symbols;

    sym_list::SymbolInfo module;
    module.symbolId = 1;
    module.symbolName = QStringLiteral("scope_top");
    module.symbolType = sym_list::sym_module;
    module.fileName = fileName;
    module.startLine = 1;
    module.endLine = 5;
    symbols.append(module);

    sym_list::SymbolInfo logic;
    logic.symbolId = 2;
    logic.symbolName = QStringLiteral("enable");
    logic.symbolType = sym_list::sym_logic;
    logic.fileName = fileName;
    logic.moduleScope = QStringLiteral("scope_top");
    logic.startLine = 2;
    logic.endLine = 2;
    symbols.append(logic);

    sym_list::SymbolInfo wire;
    wire.symbolId = 3;
    wire.symbolName = QStringLiteral("raw_wire");
    wire.symbolType = sym_list::sym_wire;
    wire.fileName = fileName;
    wire.moduleScope = QStringLiteral("scope_top");
    wire.startLine = 3;
    wire.endLine = 3;
    symbols.append(wire);

    QHash<QString, QString> fileContents;
    fileContents.insert(
        fileName,
        QStringLiteral("module scope_top;\nlogic enable;\nwire raw_wire;\nendmodule\n"));
    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        symbols,
        QList<SemanticRelationship>(),
        QList<SemanticDiagnostic>(),
        fileContents));

    ScopeBandService service(&index);
    ScopeBandQuery query;
    query.fileName = fileName;
    const ScopeBandReport report = service.scopeBands(query);

    expectInt("scope band module count", report.modules.size(), 1);
    expectBool("scope band module metadata",
               !report.modules.isEmpty()
                   && report.modules.first().symbolRecord.isValid()
                   && report.modules.first().symbolRecord.localHandle
                       == report.modules.first().symbol.symbolId
                   && report.modules.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && report.modules.first().symbolRecord.name
                       == QStringLiteral("scope_top")
                   && report.modules.first().symbol.symbolName
                       == QStringLiteral("scope_top")
                   && report.modules.first().endLine >= module.startLine,
               true);
    expectInt("scope band logic count", report.logics.size(), 1);
    expectBool("scope band logic metadata",
               !report.logics.isEmpty()
                   && report.logics.first().symbolRecord.isValid()
                   && report.logics.first().symbolRecord.localHandle
                       == report.logics.first().symbol.symbolId
                   && report.logics.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal
                   && report.logics.first().symbolRecord.name
                       == QStringLiteral("enable")
                   && report.logics.first().symbol.symbolName
                       == QStringLiteral("enable")
                   && report.logics.first().endLine == 2,
               true);
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
    symbols.append(makeModuleBriefSymbol(
        9106,
        fileName,
        QStringLiteral("journey_if"),
        sym_list::sym_interface,
        45));
    sym_list::SymbolInfo interfaceInstance = makeModuleBriefSymbol(
        9107,
        fileName,
        QStringLiteral("if_bus"),
        sym_list::sym_inst,
        50,
        QStringLiteral("journey_top"));
    interfaceInstance.dataType = QStringLiteral("journey_if");
    symbols.append(interfaceInstance);
    symbols.append(makeModuleBriefSymbol(
        9108,
        fileName,
        QStringLiteral("ready"),
        sym_list::sym_logic,
        55,
        QStringLiteral("journey_if")));
    symbols.append(makeModuleBriefSymbol(
        9109,
        fileName,
        QStringLiteral("clk"),
        sym_list::sym_port_input,
        60,
        QStringLiteral("journey_top")));
    symbols.append(makeModuleBriefSymbol(
        9110,
        fileName,
        QStringLiteral("rst_n"),
        sym_list::sym_port_input,
        61,
        QStringLiteral("journey_top")));

    QList<SemanticRelationship> relationships;
    SemanticRelationship assignment;
    assignment.fromId = 9103;
    assignment.toId = 9102;
    assignment.type = SymbolRelationshipEngine::ASSIGNS_TO;
    assignment.provenance = RelationshipProvenance::Inferred;
    assignment.confidence = 85;
    assignment.evidenceText = QStringLiteral("Assigned to data_q at line 20");
    relationships.append(assignment);

    SemanticRelationship read;
    read.fromId = 9104;
    read.toId = 9102;
    read.type = SymbolRelationshipEngine::READS_FROM;
    read.provenance = RelationshipProvenance::Inferred;
    read.confidence = 80;
    read.evidenceText = QStringLiteral("Read data_q at line 30");
    relationships.append(read);

    SemanticRelationship portConnection;
    portConnection.fromId = 9105;
    portConnection.toId = 9102;
    portConnection.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(portConnection);

    SemanticRelationship interfaceConnection;
    interfaceConnection.fromId = 9107;
    interfaceConnection.toId = 9102;
    interfaceConnection.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(interfaceConnection);

    SemanticRelationship interfaceMemberConnection;
    interfaceMemberConnection.fromId = 9102;
    interfaceMemberConnection.toId = 9108;
    interfaceMemberConnection.type = SymbolRelationshipEngine::REFERENCES;
    relationships.append(interfaceMemberConnection);

    SemanticRelationship clockConnection;
    clockConnection.fromId = 9109;
    clockConnection.toId = 9101;
    clockConnection.type = SymbolRelationshipEngine::CLOCKS;
    relationships.append(clockConnection);

    SemanticRelationship resetConnection;
    resetConnection.fromId = 9110;
    resetConnection.toId = 9101;
    resetConnection.type = SymbolRelationshipEngine::RESETS;
    relationships.append(resetConnection);

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
    expectBool("signal journey declaration stable key",
               report.declarationStableKey == symbolStableKeyForSymbol(report.declaration),
               true);
    expectBool("signal journey declaration semantic record",
               report.declarationSymbolRecord.isValid()
                   && report.declarationSymbolRecord.localHandle == 9102
                   && report.declarationSymbolRecord.stableKey
                       == report.declarationStableKey
                   && report.declarationSymbolRecord.name
                       == QStringLiteral("data_q")
                   && report.declarationSymbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal,
               true);
    SignalJourneyQuery stableSignalQuery;
    stableSignalQuery.signalStableKey = symbolStableKeyForSymbol(report.declaration);
    stableSignalQuery.signalName = QStringLiteral("consumer");
    const SignalJourneyReport stableSignalReport =
        service.buildSignalJourney(stableSignalQuery);
    expectBool("signal journey resolves stable signal key",
               stableSignalReport.found
                   && stableSignalReport.declaration.symbolName
                       == QStringLiteral("data_q")
                   && stableSignalReport.declarationStableKey
                       == stableSignalQuery.signalStableKey
                   && stableSignalReport.declarationSymbolRecord.stableKey
                       == stableSignalQuery.signalStableKey,
               true);
    expectBool("signal journey stable key preserves relationships",
               stableSignalReport.assignments.size() == report.assignments.size()
                   && stableSignalReport.reads.size() == report.reads.size()
                   && stableSignalReport.portConnections.size()
                       == report.portConnections.size()
                   && stableSignalReport.interfaceConnections.size()
                       == report.interfaceConnections.size()
                   && !stableSignalReport.assignments.isEmpty()
                   && stableSignalReport.assignments.first().toStableKey
                       == stableSignalQuery.signalStableKey,
               true);
    expectBool("signal journey declaration display type",
               !report.declarationTypeDisplayName.isEmpty(), true);
    expectBool("signal journey declaration source role",
               report.declarationSourceRoleDisplayName
                   == QStringLiteral("design source"),
               true);
    expectBool("signal journey declaration code link",
               report.declarationCodeLink.fileName == fileName
                   && report.declarationCodeLink.line == 10
                   && report.declarationCodeLink.column == 1
                   && report.declarationCodeLink.fileDisplayName
                       == QStringLiteral("signal_journey_fixture.sv")
                   && report.declarationCodeLink.lineDisplayName
                       == QStringLiteral("10"),
               true);
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
    expectInt("signal journey interface connection count",
              report.interfaceConnections.size(), 2);
    expectInt("signal journey timing connection count",
              report.timingConnections.size(), 0);
    expectBool("signal journey assignment peer",
               !report.assignments.isEmpty()
                   && report.assignments.first().peerSymbol.symbolName
                       == QStringLiteral("next_data")
                   && report.assignments.first().directionDisplayName
                       == QStringLiteral("incoming")
                   && report.assignments.first().relationshipTypeDisplayName
                       == QStringLiteral("Assigns To")
                   && report.assignments.first().detailDisplayName
                       == QStringLiteral("incoming Assigns To")
                   && report.assignments.first().peerCodeLink.fileName == fileName
                   && report.assignments.first().peerCodeLink.line == 20
                   && report.assignments.first().peerCodeLink.column == 1
                   && report.assignments.first().peerCodeLink.fileDisplayName
                       == QStringLiteral("signal_journey_fixture.sv")
                   && report.assignments.first().peerCodeLink.lineDisplayName
                       == QStringLiteral("20"),
               true);
    expectBool("signal journey assignment endpoints",
               !report.assignments.isEmpty()
                   && report.assignments.first().fromSymbolDisplayName
                       == QStringLiteral("next_data")
                   && report.assignments.first().toSymbolDisplayName
                       == QStringLiteral("data_q")
                   && report.assignments.first().fromCodeLink.fileName == fileName
                   && report.assignments.first().fromCodeLink.line == 20
                   && report.assignments.first().fromCodeLink.fileDisplayName
                       == QStringLiteral("signal_journey_fixture.sv")
                   && report.assignments.first().fromCodeLink.lineDisplayName
                       == QStringLiteral("20")
                   && report.assignments.first().toCodeLink.fileName == fileName
                   && report.assignments.first().toCodeLink.line == 10
                   && report.assignments.first().toCodeLink.fileDisplayName
                       == QStringLiteral("signal_journey_fixture.sv")
                   && report.assignments.first().toCodeLink.lineDisplayName
                       == QStringLiteral("10"),
               true);
    expectBool("signal journey assignment endpoint metadata",
               !report.assignments.isEmpty()
                   && report.assignments.first().fromTypeDisplayName
                       == QStringLiteral("logic")
                   && report.assignments.first().toTypeDisplayName
                       == QStringLiteral("logic")
                   && report.assignments.first().fromSourceRoleDisplayName
                       == QStringLiteral("design source")
                   && report.assignments.first().toSourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);
    expectBool("signal journey assignment stable keys",
               !report.assignments.isEmpty()
                   && report.assignments.first().fromStableKey
                       == symbolStableKeyForSymbol(report.assignments.first().fromSymbol)
                   && report.assignments.first().toStableKey
                       == symbolStableKeyForSymbol(report.assignments.first().toSymbol)
                   && report.assignments.first().peerStableKey
                       == report.assignments.first().fromStableKey,
               true);
    expectBool("signal journey assignment semantic records",
               !report.assignments.isEmpty()
                   && report.assignments.first().fromSymbolRecord.isValid()
                   && report.assignments.first().fromSymbolRecord.localHandle == 9103
                   && report.assignments.first().fromSymbolRecord.stableKey
                       == report.assignments.first().fromStableKey
                   && report.assignments.first().toSymbolRecord.isValid()
                   && report.assignments.first().toSymbolRecord.localHandle == 9102
                   && report.assignments.first().toSymbolRecord.stableKey
                       == report.assignments.first().toStableKey
                   && report.assignments.first().peerSymbolRecord.isValid()
                   && report.assignments.first().peerSymbolRecord.stableKey
                       == report.assignments.first().peerStableKey
                   && report.assignments.first().peerSymbolRecord.name
                       == QStringLiteral("next_data"),
               true);
    expectBool("signal journey assignment relationship metadata",
               !report.assignments.isEmpty()
                   && report.assignments.first().provenance
                       == RelationshipProvenance::Inferred
                   && report.assignments.first().confidence == 85
                   && report.assignments.first().evidenceText.contains(
                       QStringLiteral("Assigned to data_q"))
                   && report.assignments.first().provenanceDisplayName
                       == QStringLiteral("inferred")
                   && report.assignments.first().confidenceDisplayName
                       == QStringLiteral("85%")
                   && report.assignments.first().evidenceDisplayName.contains(
                       QStringLiteral("Assigned to data_q")),
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
    bool sawInterfaceInstance = false;
    bool sawInterfaceMember = false;
    bool sawInterfaceInstanceLink = false;
    bool sawInterfaceMemberLink = false;
    for (const SignalJourneyItem& item : report.interfaceConnections) {
        sawInterfaceInstance = sawInterfaceInstance
            || (item.peerSymbolDisplayName == QStringLiteral("if_bus")
                && item.detailDisplayName == QStringLiteral("interface incoming References"));
        sawInterfaceInstanceLink = sawInterfaceInstanceLink
            || (item.peerSymbolDisplayName == QStringLiteral("if_bus")
                && item.peerCodeLink.fileName == fileName
                && item.peerCodeLink.line == 50
                && item.peerCodeLink.column == 1
                && item.peerCodeLink.fileDisplayName
                    == QStringLiteral("signal_journey_fixture.sv")
                && item.peerCodeLink.lineDisplayName == QStringLiteral("50"));
        sawInterfaceMember = sawInterfaceMember
            || (item.peerSymbolDisplayName == QStringLiteral("ready")
                && item.detailDisplayName == QStringLiteral("interface outgoing References"));
        sawInterfaceMemberLink = sawInterfaceMemberLink
            || (item.peerSymbolDisplayName == QStringLiteral("ready")
                && item.peerCodeLink.fileName == fileName
                && item.peerCodeLink.line == 55
                && item.peerCodeLink.column == 1
                && item.peerCodeLink.fileDisplayName
                    == QStringLiteral("signal_journey_fixture.sv")
                && item.peerCodeLink.lineDisplayName == QStringLiteral("55"));
    }
    expectBool("signal journey interface instance peer",
               sawInterfaceInstance,
               true);
    expectBool("signal journey interface instance code link",
               sawInterfaceInstanceLink,
               true);
    expectBool("signal journey interface member peer",
               sawInterfaceMember,
               true);
    expectBool("signal journey interface member code link",
               sawInterfaceMemberLink,
               true);
    bool sawInterfaceInstanceEndpoints = false;
    bool sawInterfaceMemberEndpoints = false;
    bool sawInterfaceInstanceMetadata = false;
    bool sawInterfaceMemberMetadata = false;
    for (const SignalJourneyItem& item : report.interfaceConnections) {
        sawInterfaceInstanceEndpoints = sawInterfaceInstanceEndpoints
            || (item.peerSymbolDisplayName == QStringLiteral("if_bus")
                && item.fromSymbolDisplayName == QStringLiteral("if_bus")
                && item.toSymbolDisplayName == QStringLiteral("data_q")
                && item.fromCodeLink.fileName == fileName
                && item.fromCodeLink.line == 50
                && item.toCodeLink.fileName == fileName
                && item.toCodeLink.line == 10);
        sawInterfaceMemberEndpoints = sawInterfaceMemberEndpoints
            || (item.peerSymbolDisplayName == QStringLiteral("ready")
                && item.fromSymbolDisplayName == QStringLiteral("data_q")
                && item.toSymbolDisplayName == QStringLiteral("ready")
                && item.fromCodeLink.fileName == fileName
                && item.fromCodeLink.line == 10
                && item.toCodeLink.fileName == fileName
                && item.toCodeLink.line == 55);
        sawInterfaceInstanceMetadata = sawInterfaceInstanceMetadata
            || (item.peerSymbolDisplayName == QStringLiteral("if_bus")
                && item.connectionKindDisplayName
                    == QStringLiteral("interface instance")
                && item.peerTypeDisplayName == QStringLiteral("instance")
                && item.interfaceBaseDisplayName == QStringLiteral("journey_if")
                && item.peerSymbolRecord.type.resolvedTypeName
                    == QStringLiteral("journey_if")
                && item.peerSourceRoleDisplayName
                    == QStringLiteral("design source")
                && item.fromTypeDisplayName == QStringLiteral("instance")
                && item.toTypeDisplayName == QStringLiteral("logic")
                && item.fromSourceRoleDisplayName
                    == QStringLiteral("design source")
                && item.toSourceRoleDisplayName
                    == QStringLiteral("design source"));
        sawInterfaceMemberMetadata = sawInterfaceMemberMetadata
            || (item.peerSymbolDisplayName == QStringLiteral("ready")
                && item.connectionKindDisplayName
                    == QStringLiteral("interface member")
                && item.peerTypeDisplayName == QStringLiteral("logic")
                && item.interfaceBaseDisplayName == QStringLiteral("journey_if")
                && item.peerSymbolRecord.owner.name
                    == QStringLiteral("journey_if")
                && item.peerSourceRoleDisplayName
                    == QStringLiteral("design source")
                && item.fromTypeDisplayName == QStringLiteral("logic")
                && item.toTypeDisplayName == QStringLiteral("logic")
                && item.fromSourceRoleDisplayName
                    == QStringLiteral("design source")
                && item.toSourceRoleDisplayName
                    == QStringLiteral("design source"));
    }
    expectBool("signal journey interface instance endpoints",
               sawInterfaceInstanceEndpoints,
               true);
    expectBool("signal journey interface member endpoints",
               sawInterfaceMemberEndpoints,
               true);
    expectBool("signal journey interface instance metadata",
               sawInterfaceInstanceMetadata,
               true);
    expectBool("signal journey interface member metadata",
               sawInterfaceMemberMetadata,
               true);

    SignalJourneyQuery clockQuery;
    clockQuery.signalName = QStringLiteral("clk");
    clockQuery.fileName = fileName;
    clockQuery.moduleName = QStringLiteral("journey_top");
    const SignalJourneyReport clockReport = service.buildSignalJourney(clockQuery);
    expectBool("signal journey clock found",
               clockReport.found,
               true);
    expectInt("signal journey clock timing count",
              clockReport.timingConnections.size(),
              1);
    expectBool("signal journey clock timing peer",
               !clockReport.timingConnections.isEmpty()
                   && clockReport.timingConnections.first().peerSymbolDisplayName
                       == QStringLiteral("journey_top")
                   && clockReport.timingConnections.first().relationshipTypeDisplayName
                       == QStringLiteral("Clocks")
                   && clockReport.timingConnections.first().detailDisplayName
                       == QStringLiteral("timing outgoing Clocks")
                   && clockReport.timingConnections.first().peerCodeLink.fileName == fileName
                   && clockReport.timingConnections.first().peerCodeLink.line == 1,
               true);

    SignalJourneyQuery resetQuery;
    resetQuery.signalName = QStringLiteral("rst_n");
    resetQuery.fileName = fileName;
    resetQuery.moduleName = QStringLiteral("journey_top");
    const SignalJourneyReport resetReport = service.buildSignalJourney(resetQuery);
    expectBool("signal journey reset found",
               resetReport.found,
               true);
    expectInt("signal journey reset timing count",
              resetReport.timingConnections.size(),
              1);
    expectBool("signal journey reset timing peer",
               !resetReport.timingConnections.isEmpty()
                   && resetReport.timingConnections.first().peerSymbolDisplayName
                       == QStringLiteral("journey_top")
                   && resetReport.timingConnections.first().relationshipTypeDisplayName
                       == QStringLiteral("Resets")
                   && resetReport.timingConnections.first().detailDisplayName
                       == QStringLiteral("timing outgoing Resets"),
               true);

    SignalJourneyQuery emptySignalQuery;
    const SignalJourneyReport emptySignalReport =
        service.buildSignalJourney(emptySignalQuery);
    expectBool("signal journey empty signal reason",
               !emptySignalReport.found
                   && emptySignalReport.notFoundReason
                       == SignalJourneyNotFoundReason::EmptySignalName
                   && emptySignalReport.notFoundReasonDisplayName
                       == QStringLiteral("empty signal name"),
               true);

    SignalJourneyQuery missingSignalQuery;
    missingSignalQuery.signalName = QStringLiteral("missing_signal");
    missingSignalQuery.fileName = fileName;
    missingSignalQuery.moduleName = QStringLiteral("journey_top");
    const SignalJourneyReport missingSignalReport =
        service.buildSignalJourney(missingSignalQuery);
    expectBool("signal journey missing signal reason",
               !missingSignalReport.found
                   && missingSignalReport.notFoundReason
                       == SignalJourneyNotFoundReason::NoMatchingSignal
                   && missingSignalReport.notFoundReasonDisplayName
                       == QStringLiteral("no matching signal"),
               true);

    SignalJourneyQuery unsupportedSignalQuery;
    unsupportedSignalQuery.signalName = QStringLiteral("journey_top");
    unsupportedSignalQuery.fileName = fileName;
    const SignalJourneyReport unsupportedSignalReport =
        service.buildSignalJourney(unsupportedSignalQuery);
    expectBool("signal journey unsupported symbol reason",
               !unsupportedSignalReport.found
                   && unsupportedSignalReport.notFoundReason
                       == SignalJourneyNotFoundReason::UnsupportedSymbolKind
                   && unsupportedSignalReport.notFoundReasonDisplayName
                       == QStringLiteral("unsupported symbol kind"),
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
    symbols.append(makeModuleBriefSymbol(
        9206,
        fileName,
        QStringLiteral("alt_clk_i"),
        sym_list::sym_port_input,
        12,
        QStringLiteral("domain_top")));
    symbols.append(makeModuleBriefSymbol(
        9207,
        fileName,
        QStringLiteral("scan_clk"),
        sym_list::sym_port_input,
        13,
        QStringLiteral("domain_top")));
    symbols.append(makeModuleBriefSymbol(
        9208,
        fileName,
        QStringLiteral("por_rst_n"),
        sym_list::sym_logic,
        14,
        QStringLiteral("domain_top")));
    sym_list::SymbolInfo noTiming = makeModuleBriefSymbol(
        9209,
        fileName,
        QStringLiteral("no_timing_domain"),
        sym_list::sym_module,
        120);
    noTiming.endLine = 122;
    symbols.append(noTiming);

    QList<SemanticRelationship> relationships;
    SemanticRelationship topClock;
    topClock.fromId = 9203;
    topClock.toId = 9201;
    topClock.type = SymbolRelationshipEngine::CLOCKS;
    topClock.provenance = RelationshipProvenance::Inferred;
    topClock.confidence = 85;
    topClock.evidenceText = QStringLiteral("posedge clk_i");
    relationships.append(topClock);

    SemanticRelationship altTopClock;
    altTopClock.fromId = 9206;
    altTopClock.toId = 9201;
    altTopClock.type = SymbolRelationshipEngine::CLOCKS;
    altTopClock.provenance = RelationshipProvenance::Inferred;
    altTopClock.confidence = 80;
    altTopClock.evidenceText = QStringLiteral("posedge alt_clk_i");
    relationships.append(altTopClock);

    SemanticRelationship topReset;
    topReset.fromId = 9204;
    topReset.toId = 9201;
    topReset.type = SymbolRelationshipEngine::RESETS;
    topReset.provenance = RelationshipProvenance::SlangExtracted;
    topReset.confidence = 95;
    topReset.evidenceText = QStringLiteral("negedge rst_ni");
    relationships.append(topReset);

    SemanticRelationship otherClock;
    otherClock.fromId = 9205;
    otherClock.toId = 9202;
    otherClock.type = SymbolRelationshipEngine::CLOCKS;
    otherClock.provenance = RelationshipProvenance::Inferred;
    otherClock.confidence = 75;
    otherClock.evidenceText = QStringLiteral("posedge other_clk");
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
    expectBool("clock reset all found reason metadata",
               allReport.notFoundReason == ClockResetDomainNotFoundReason::None
                   && allReport.notFoundReasonDisplayName.isEmpty(),
               true);
    expectInt("clock reset all clock domains", allReport.clockDomains.size(), 3);
    expectInt("clock reset all reset domains", allReport.resetDomains.size(), 1);
    expectInt("clock reset all clock count",
              allReport.clockRelationshipCount, 3);
    expectInt("clock reset all reset count",
              allReport.resetRelationshipCount, 1);
    expectInt("clock reset all evidence rows", allReport.evidenceRows.size(), 4);
    expectInt("clock reset all ambiguity rows", allReport.ambiguityRows.size(), 2);
    expectBool("clock reset all unmapped rows",
               allReport.unmappedRows.size() >= 2,
               true);
    expectBool("clock reset all group display metadata",
               allReport.clockGroupDisplayName == QStringLiteral("Clock Domains")
                   && allReport.resetGroupDisplayName == QStringLiteral("Reset Domains")
                   && allReport.evidenceGroupDisplayName
                       == QStringLiteral("Domain Evidence")
                   && allReport.ambiguityGroupDisplayName
                       == QStringLiteral("Ambiguity")
                   && allReport.unmappedGroupDisplayName
                       == QStringLiteral("Unmapped Timing Signals"),
               true);

    ClockResetDomainQuery topQuery;
    topQuery.moduleName = QStringLiteral("domain_top");
    topQuery.fileName = fileName;
    const ClockResetDomainReport topReport =
        service.buildClockResetDomainMap(topQuery);

    expectBool("clock reset top found", topReport.found, true);
    ClockResetDomainQuery stableTopQuery;
    stableTopQuery.moduleStableKey = symbolStableKeyForSymbol(top);
    stableTopQuery.moduleName = QStringLiteral("other_domain");
    const ClockResetDomainReport stableTopReport =
        service.buildClockResetDomainMap(stableTopQuery);
    expectBool("clock reset resolves stable module key",
               stableTopReport.found
                   && stableTopReport.clockDomains.size() == 2
                   && stableTopReport.resetDomains.size() == 1
                   && stableTopReport.evidenceRows.size()
                       == topReport.evidenceRows.size()
                   && !stableTopReport.clockDomains.isEmpty()
                   && !stableTopReport.clockDomains.first().modules.isEmpty()
                   && stableTopReport.clockDomains.first()
                          .modules.first()
                          .moduleStableKey == stableTopQuery.moduleStableKey,
               true);
    expectBool("clock reset stable key preserves relationship evidence",
               !stableTopReport.evidenceRows.isEmpty()
                   && stableTopReport.evidenceRows.first().moduleStableKey
                       == stableTopQuery.moduleStableKey
                   && stableTopReport.evidenceRows.first()
                          .domainSignalStableKey.isValid()
                   && stableTopReport.evidenceRows.first().evidenceReasonDisplayName
                       == QStringLiteral("relationship"),
               true);
    expectInt("clock reset top clock domains", topReport.clockDomains.size(), 2);
    expectInt("clock reset top reset domains", topReport.resetDomains.size(), 1);
    expectInt("clock reset top evidence rows", topReport.evidenceRows.size(), 3);
    expectInt("clock reset top ambiguity rows", topReport.ambiguityRows.size(), 2);
    expectInt("clock reset top unmapped rows", topReport.unmappedRows.size(), 2);
    expectBool("clock reset top clock signal",
               !topReport.clockDomains.isEmpty()
                   && topReport.clockDomains.first().domainSignal.symbolName
                       == QStringLiteral("clk_i")
                   && topReport.clockDomains.first().domainSignalRecord.isValid()
                   && topReport.clockDomains.first().domainSignalRecord.localHandle
                       == 9203
                   && topReport.clockDomains.first().domainSignalRecord.stableKey
                       == topReport.clockDomains.first().domainSignalStableKey
                   && topReport.clockDomains.first().domainSignalRecord.name
                       == QStringLiteral("clk_i")
                   && topReport.clockDomains.first().domainSignalRecord.owner.name
                       == QStringLiteral("domain_top")
                   && topReport.clockDomains.first().domainSignalStableKey
                       == symbolStableKeyForSymbol(
                           topReport.clockDomains.first().domainSignal)
                   && topReport.clockDomains.first().sectionDisplayName
                       == QStringLiteral("Clock")
                   && topReport.clockDomains.first().detailDisplayName
                       == QStringLiteral("drives 1 modules"),
               true);
    expectBool("clock reset top clock signal code link",
               !topReport.clockDomains.isEmpty()
                   && topReport.clockDomains.first().domainSignalCodeLink.fileName
                       == fileName
                   && topReport.clockDomains.first().domainSignalCodeLink.line == 10
                   && topReport.clockDomains.first().domainSignalCodeLink.column == 1
                   && topReport.clockDomains.first().domainSignalCodeLink.fileDisplayName
                       == QStringLiteral("clock_reset_domain_fixture.sv")
                   && topReport.clockDomains.first().domainSignalCodeLink.lineDisplayName
                       == QStringLiteral("10"),
               true);
    expectBool("clock reset top reset signal",
               !topReport.resetDomains.isEmpty()
                   && topReport.resetDomains.first().domainSignal.symbolName
                       == QStringLiteral("rst_ni")
                   && topReport.resetDomains.first().sectionDisplayName
                       == QStringLiteral("Reset")
                   && topReport.resetDomains.first().detailDisplayName
                       == QStringLiteral("resets 1 modules"),
               true);
    expectBool("clock reset top reset signal code link",
               !topReport.resetDomains.isEmpty()
                   && topReport.resetDomains.first().domainSignalCodeLink.fileName
                       == fileName
                   && topReport.resetDomains.first().domainSignalCodeLink.line == 11
                   && topReport.resetDomains.first().domainSignalCodeLink.column == 1
                   && topReport.resetDomains.first().domainSignalCodeLink.fileDisplayName
                       == QStringLiteral("clock_reset_domain_fixture.sv")
                   && topReport.resetDomains.first().domainSignalCodeLink.lineDisplayName
                       == QStringLiteral("11"),
               true);
    expectBool("clock reset top clock target",
               !topReport.clockDomains.isEmpty()
                   && !topReport.clockDomains.first().modules.isEmpty()
                   && topReport.clockDomains.first().modules.first()
                          .moduleSymbol.symbolName == QStringLiteral("domain_top")
                   && topReport.clockDomains.first().modules.first()
                          .domainSignalRecord.isValid()
                   && topReport.clockDomains.first().modules.first()
                          .domainSignalRecord.stableKey
                       == topReport.clockDomains.first().modules.first()
                          .domainSignalStableKey
                   && topReport.clockDomains.first().modules.first()
                          .moduleSymbolRecord.isValid()
                   && topReport.clockDomains.first().modules.first()
                          .moduleSymbolRecord.localHandle == 9201
                   && topReport.clockDomains.first().modules.first()
                          .moduleSymbolRecord.stableKey
                       == topReport.clockDomains.first().modules.first()
                          .moduleStableKey
                   && topReport.clockDomains.first().modules.first()
                          .domainSignalStableKey
                       == symbolStableKeyForSymbol(
                           topReport.clockDomains.first().domainSignal)
                   && topReport.clockDomains.first().modules.first()
                          .moduleStableKey
                       == symbolStableKeyForSymbol(
                           topReport.clockDomains.first().modules.first()
                              .moduleSymbol)
                   && topReport.clockDomains.first().modules.first()
                          .sectionDisplayName == QStringLiteral("Module")
                   && topReport.clockDomains.first().modules.first()
                          .moduleDisplayName == QStringLiteral("domain_top")
                   && topReport.clockDomains.first().modules.first()
                          .relationshipTypeDisplayName == QStringLiteral("Clock")
                   && topReport.clockDomains.first().modules.first()
                          .provenance == RelationshipProvenance::Inferred
                   && topReport.clockDomains.first().modules.first()
                          .provenanceDisplayName == QStringLiteral("inferred")
                   && topReport.clockDomains.first().modules.first()
                          .confidence == 85
                   && topReport.clockDomains.first().modules.first()
                          .confidenceDisplayName == QStringLiteral("85%")
                   && topReport.clockDomains.first().modules.first()
                          .evidenceText == QStringLiteral("posedge clk_i")
                   && topReport.clockDomains.first().modules.first()
                          .evidenceDisplayName == QStringLiteral("posedge clk_i")
                   && topReport.clockDomains.first().modules.first()
                          .detailDisplayName == QStringLiteral("clocked")
                   && topReport.clockDomains.first().modules.first()
                          .sourceRoleDisplayName == QStringLiteral("design source"),
               true);
    expectBool("clock reset top clock target code link",
               !topReport.clockDomains.isEmpty()
                   && !topReport.clockDomains.first().modules.isEmpty()
                   && topReport.clockDomains.first().modules.first()
                          .moduleCodeLink.fileName == fileName
                   && topReport.clockDomains.first().modules.first()
                          .moduleCodeLink.line == 1
                   && topReport.clockDomains.first().modules.first()
                          .moduleCodeLink.column == 1
                   && topReport.clockDomains.first().modules.first()
                          .moduleCodeLink.fileDisplayName
                       == QStringLiteral("clock_reset_domain_fixture.sv")
                   && topReport.clockDomains.first().modules.first()
                          .moduleCodeLink.lineDisplayName == QStringLiteral("1"),
               true);
    expectBool("clock reset top reset target metadata",
               !topReport.resetDomains.isEmpty()
                   && !topReport.resetDomains.first().modules.isEmpty()
                   && topReport.resetDomains.first().modules.first()
                          .moduleDisplayName == QStringLiteral("domain_top")
                   && topReport.resetDomains.first().modules.first()
                          .relationshipTypeDisplayName == QStringLiteral("Reset")
                   && topReport.resetDomains.first().modules.first()
                          .detailDisplayName == QStringLiteral("reset")
                   && topReport.resetDomains.first().modules.first()
                          .sourceRoleDisplayName == QStringLiteral("design source"),
               true);
    expectBool("clock reset top evidence row metadata",
               !topReport.evidenceRows.isEmpty()
                   && topReport.evidenceRows.first().sectionDisplayName
                       == QStringLiteral("Clock")
                   && topReport.evidenceRows.first().signalDisplayName
                       == QStringLiteral("clk_i")
                   && topReport.evidenceRows.first().moduleDisplayName
                       == QStringLiteral("domain_top")
                   && topReport.evidenceRows.first().domainSignalRecord.isValid()
                   && topReport.evidenceRows.first().domainSignalRecord.localHandle
                       == 9203
                   && topReport.evidenceRows.first().domainSignalRecord.stableKey
                       == topReport.evidenceRows.first().domainSignalStableKey
                   && topReport.evidenceRows.first().moduleSymbolRecord.isValid()
                   && topReport.evidenceRows.first().moduleSymbolRecord.localHandle
                       == 9201
                   && topReport.evidenceRows.first().moduleSymbolRecord.stableKey
                       == topReport.evidenceRows.first().moduleStableKey
                   && topReport.evidenceRows.first().domainSignalStableKey
                       == symbolStableKeyForSymbol(
                           topReport.evidenceRows.first().domainSignal)
                   && topReport.evidenceRows.first().moduleStableKey
                       == symbolStableKeyForSymbol(
                           topReport.evidenceRows.first().moduleSymbol)
                   && topReport.evidenceRows.first().relationshipTypeDisplayName
                       == QStringLiteral("Clock")
                   && topReport.evidenceRows.first().provenance
                       == RelationshipProvenance::Inferred
                   && topReport.evidenceRows.first().provenanceDisplayName
                       == QStringLiteral("inferred")
                   && topReport.evidenceRows.first().confidence == 85
                   && topReport.evidenceRows.first().confidenceDisplayName
                       == QStringLiteral("85%")
                   && topReport.evidenceRows.first().evidenceText
                       == QStringLiteral("posedge clk_i")
                   && topReport.evidenceRows.first().evidenceDisplayName
                       == QStringLiteral("posedge clk_i")
                   && topReport.evidenceRows.first().categoryDisplayName
                       == QStringLiteral("mapped domain")
                   && topReport.evidenceRows.first().evidenceReasonDisplayName
                       == QStringLiteral("relationship")
                   && topReport.evidenceRows.first().detailDisplayName
                       == QStringLiteral("clk_i clocks domain_top")
                   && topReport.evidenceRows.first().sourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);
    expectBool("clock reset top evidence row code link",
               !topReport.evidenceRows.isEmpty()
                   && topReport.evidenceRows.first().signalCodeLink.fileName
                       == fileName
                   && topReport.evidenceRows.first().signalCodeLink.line == 10
                   && topReport.evidenceRows.first().signalCodeLink.column == 1
                   && topReport.evidenceRows.first().signalCodeLink.fileDisplayName
                       == QStringLiteral("clock_reset_domain_fixture.sv")
                   && topReport.evidenceRows.first().signalCodeLink.lineDisplayName
                       == QStringLiteral("10")
                   && topReport.evidenceRows.first().moduleCodeLink.fileName
                       == fileName
                   && topReport.evidenceRows.first().moduleCodeLink.line == 1
                   && topReport.evidenceRows.first().moduleCodeLink.column == 1
                   && topReport.evidenceRows.first().moduleCodeLink.fileDisplayName
                       == QStringLiteral("clock_reset_domain_fixture.sv")
                   && topReport.evidenceRows.first().moduleCodeLink.lineDisplayName
                       == QStringLiteral("1"),
               true);
    expectBool("clock reset top ambiguity row metadata",
               !topReport.ambiguityRows.isEmpty()
                   && topReport.ambiguityRows.first().sectionDisplayName
                       == QStringLiteral("Multiple Clocks")
                   && topReport.ambiguityRows.first().relationshipTypeDisplayName
                       == QStringLiteral("Clock")
                   && topReport.ambiguityRows.first().categoryDisplayName
                       == QStringLiteral("ambiguous domain")
                   && topReport.ambiguityRows.first().evidenceReasonDisplayName
                       == QStringLiteral("ambiguous domain membership")
                   && topReport.ambiguityRows.first().detailDisplayName
                       == QStringLiteral("domain_top has 2 clock domains"),
               true);
    bool sawUnmappedClock = false;
    bool sawUnmappedReset = false;
    bool sawUnmappedClockLink = false;
    for (const ClockResetDomainEvidenceRow& row : topReport.unmappedRows) {
        sawUnmappedClock = sawUnmappedClock
            || (row.sectionDisplayName == QStringLiteral("Unmapped Clock")
                && row.signalDisplayName == QStringLiteral("scan_clk")
                && row.moduleDisplayName == QStringLiteral("domain_top")
                && row.domainSignalRecord.isValid()
                && row.domainSignalRecord.localHandle == 9207
                && row.domainSignalRecord.owner.name
                    == QStringLiteral("domain_top")
                && row.domainSignalRecord.stableKey
                    == row.domainSignalStableKey
                && row.moduleSymbolRecord.isValid()
                && row.moduleSymbolRecord.localHandle == 9201
                && row.moduleSymbolRecord.name == QStringLiteral("domain_top")
                && row.moduleSymbolRecord.stableKey
                    == row.moduleStableKey
                && row.domainSignalStableKey
                    == symbolStableKeyForSymbol(row.domainSignal)
                && row.moduleStableKey
                    == symbolStableKeyForSymbol(row.moduleSymbol)
                && row.relationshipTypeDisplayName == QStringLiteral("Clock")
                && row.provenance
                    == RelationshipProvenance::FeatureGenerated
                && row.provenanceDisplayName
                    == QStringLiteral("feature generated")
                && row.confidence == 100
                && row.confidenceDisplayName == QStringLiteral("100%")
                && row.evidenceText
                    == QStringLiteral(
                        "timing-name candidate without mapped relationship")
                && row.evidenceDisplayName
                    == QStringLiteral(
                        "timing-name candidate without mapped relationship")
                && row.categoryDisplayName == QStringLiteral("unmapped timing")
                && row.evidenceReasonDisplayName
                    == QStringLiteral("missing relationship")
                && row.sourceRoleDisplayName == QStringLiteral("design source")
                && row.detailDisplayName
                    == QStringLiteral("scan_clk has no clock domain relationship"));
        sawUnmappedClockLink = sawUnmappedClockLink
            || (row.signalDisplayName == QStringLiteral("scan_clk")
                && row.signalCodeLink.fileName == fileName
                && row.signalCodeLink.line == 13
                && row.signalCodeLink.column == 1
                && row.signalCodeLink.fileDisplayName
                    == QStringLiteral("clock_reset_domain_fixture.sv")
                && row.signalCodeLink.lineDisplayName == QStringLiteral("13"));
        sawUnmappedReset = sawUnmappedReset
            || (row.sectionDisplayName == QStringLiteral("Unmapped Reset")
                && row.signalDisplayName == QStringLiteral("por_rst_n")
                && row.moduleDisplayName == QStringLiteral("domain_top")
                && row.relationshipTypeDisplayName == QStringLiteral("Reset")
                && row.categoryDisplayName == QStringLiteral("unmapped timing")
                && row.evidenceReasonDisplayName
                    == QStringLiteral("missing relationship")
                && row.sourceRoleDisplayName == QStringLiteral("design source")
                && row.detailDisplayName
                    == QStringLiteral("por_rst_n has no reset domain relationship"));
    }
    expectBool("clock reset top unmapped clock row", sawUnmappedClock, true);
    expectBool("clock reset top unmapped clock code link",
               sawUnmappedClockLink,
               true);
    expectBool("clock reset top unmapped reset row", sawUnmappedReset, true);

    ClockResetDomainQuery otherStableQuery;
    otherStableQuery.moduleStableKey = symbolStableKeyForSymbol(symbols.at(1));
    const ClockResetDomainReport otherReport =
        service.buildClockResetDomainMap(otherStableQuery);
    expectInt("clock reset stable key clock domains",
              otherReport.clockDomains.size(),
              1);
    expectBool("clock reset stable key clock signal",
               !otherReport.clockDomains.isEmpty()
                   && otherReport.clockDomains.first().domainSignal.symbolName
                       == QStringLiteral("other_clk"),
               true);
    expectInt("clock reset stable key reset domains",
              otherReport.resetDomains.size(),
              0);

    ClockResetDomainQuery missingModuleQuery;
    missingModuleQuery.moduleName = QStringLiteral("missing_domain");
    missingModuleQuery.fileName = fileName;
    const ClockResetDomainReport missingModuleReport =
        service.buildClockResetDomainMap(missingModuleQuery);
    expectBool("clock reset missing module reason",
               !missingModuleReport.found
                   && missingModuleReport.notFoundReason
                       == ClockResetDomainNotFoundReason::NoMatchingModule
                   && missingModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("no matching module"),
               true);

    ClockResetDomainQuery unsupportedModuleQuery;
    unsupportedModuleQuery.moduleStableKey =
        symbolStableKeyForSymbol(symbols.at(2));
    const ClockResetDomainReport unsupportedModuleReport =
        service.buildClockResetDomainMap(unsupportedModuleQuery);
    expectBool("clock reset unsupported symbol reason",
               !unsupportedModuleReport.found
                   && unsupportedModuleReport.notFoundReason
                       == ClockResetDomainNotFoundReason::UnsupportedSymbolKind
                   && unsupportedModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("unsupported symbol kind"),
               true);

    ClockResetDomainQuery noTimingQuery;
    noTimingQuery.moduleName = QStringLiteral("no_timing_domain");
    noTimingQuery.fileName = fileName;
    const ClockResetDomainReport noTimingReport =
        service.buildClockResetDomainMap(noTimingQuery);
    expectBool("clock reset no timing domains reason",
               !noTimingReport.found
                   && noTimingReport.notFoundReason
                       == ClockResetDomainNotFoundReason::NoTimingDomains
                   && noTimingReport.notFoundReasonDisplayName
                       == QStringLiteral("no timing domains"),
               true);
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
        "        if (start)\n"
        "          state_d = RUN;\n"
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
    module.endLine = 17;
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

    const QString packageFileName = QStringLiteral("test_sv/fsm_pkg_fixture.sv");
    const QString packageModuleFileName =
        QStringLiteral("test_sv/fsm_pkg_module_fixture.sv");
    const QString packageModuleContent = QStringLiteral(
        "module pkg_fsm_top(input logic go);\n"
        "  pkg_state_e cs;\n"
        "  pkg_state_e ns;\n"
        "  always_comb begin\n"
        "    case (cs)\n"
        "      IDLE: ns = go ? RUN : IDLE;\n"
        "      RUN: ns = IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    sym_list::SymbolInfo packageModule = makeModuleBriefSymbol(
        9310,
        packageModuleFileName,
        QStringLiteral("pkg_fsm_top"),
        sym_list::sym_module,
        1);
    packageModule.endLine = 10;
    symbols.append(packageModule);

    sym_list::SymbolInfo packageCs = makeModuleBriefSymbol(
        9311,
        packageModuleFileName,
        QStringLiteral("cs"),
        sym_list::sym_enum_var,
        2,
        QStringLiteral("pkg_fsm_top"));
    packageCs.dataType = QStringLiteral("pkg_state_e");
    symbols.append(packageCs);

    sym_list::SymbolInfo packageNs = makeModuleBriefSymbol(
        9312,
        packageModuleFileName,
        QStringLiteral("ns"),
        sym_list::sym_enum_var,
        3,
        QStringLiteral("pkg_fsm_top"));
    packageNs.dataType = QStringLiteral("pkg_state_e");
    symbols.append(packageNs);

    sym_list::SymbolInfo packageIdle = makeModuleBriefSymbol(
        9313,
        packageFileName,
        QStringLiteral("IDLE"),
        sym_list::sym_enum_value,
        3,
        QStringLiteral("fsm_pkg"));
    packageIdle.dataType = QStringLiteral("pkg_state_e");
    symbols.append(packageIdle);

    sym_list::SymbolInfo packageRun = makeModuleBriefSymbol(
        9314,
        packageFileName,
        QStringLiteral("RUN"),
        sym_list::sym_enum_value,
        4,
        QStringLiteral("fsm_pkg"));
    packageRun.dataType = QStringLiteral("pkg_state_e");
    symbols.append(packageRun);

    sym_list::SymbolInfo noFsmModule = makeModuleBriefSymbol(
        9315,
        fileName,
        QStringLiteral("no_fsm_top"),
        sym_list::sym_module,
        20);
    noFsmModule.endLine = 22;
    symbols.append(noFsmModule);

    QHash<QString, QString> fileContents;
    fileContents.insert(fileName, content);
    fileContents.insert(packageModuleFileName, packageModuleContent);
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
    FsmGraphQuery stableFsmQuery;
    stableFsmQuery.moduleStableKey = symbolStableKeyForSymbol(module);
    const FsmGraphReport stableFsmReport = service.buildFsmGraph(stableFsmQuery);
    expectBool("fsm graph resolves stable module key",
               stableFsmReport.found
                   && !stableFsmReport.graphs.isEmpty()
                   && stableFsmReport.graphs.first().moduleStableKey
                       == stableFsmQuery.moduleStableKey
                   && stableFsmReport.graphs.first().moduleSymbol.symbolName
                       == QStringLiteral("fsm_top"),
               true);
    expectBool("fsm graph group metadata",
               report.groupDisplayName == QStringLiteral("FSM Graphs"),
               true);
    expectBool("fsm graph found reason metadata",
               report.notFoundReason == FsmGraphNotFoundReason::None
                   && report.notFoundReasonDisplayName.isEmpty(),
               true);
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
    expectBool("fsm graph stable keys",
               !report.graphs.isEmpty()
                   && report.graphs.first().moduleStableKey
                       == symbolStableKeyForSymbol(report.graphs.first().moduleSymbol)
                   && report.graphs.first().stateRegisterStableKey
                       == symbolStableKeyForSymbol(
                           report.graphs.first().stateRegister)
                   && report.graphs.first().nextStateSignalStableKey
                       == symbolStableKeyForSymbol(
                           report.graphs.first().nextStateSignal),
               true);
    expectBool("fsm graph semantic records",
               !report.graphs.isEmpty()
                   && report.graphs.first().moduleSymbolRecord.isValid()
                   && report.graphs.first().moduleSymbolRecord.localHandle == 9301
                   && report.graphs.first().moduleSymbolRecord.stableKey
                       == report.graphs.first().moduleStableKey
                   && report.graphs.first().moduleSymbolRecord.name
                       == QStringLiteral("fsm_top")
                   && report.graphs.first().stateRegisterRecord.isValid()
                   && report.graphs.first().stateRegisterRecord.localHandle == 9302
                   && report.graphs.first().stateRegisterRecord.stableKey
                       == report.graphs.first().stateRegisterStableKey
                   && report.graphs.first().stateRegisterRecord.name
                       == QStringLiteral("state_q")
                   && report.graphs.first().stateRegisterRecord.type.rawTypeText
                       == QStringLiteral("state_t")
                   && report.graphs.first().nextStateSignalRecord.isValid()
                   && report.graphs.first().nextStateSignalRecord.localHandle == 9303
                   && report.graphs.first().nextStateSignalRecord.stableKey
                       == report.graphs.first().nextStateSignalStableKey
                   && report.graphs.first().nextStateSignalRecord.name
                       == QStringLiteral("state_d")
                   && report.graphs.first().nextStateSignalRecord.type.rawTypeText
                       == QStringLiteral("state_t"),
               true);
    expectBool("fsm graph next state",
               !report.graphs.isEmpty()
                   && report.graphs.first().nextStateSignal.symbolName
                       == QStringLiteral("state_d"),
               true);
    expectBool("fsm graph register display metadata",
               !report.graphs.isEmpty()
                   && report.graphs.first().stateRegisterSectionDisplayName
                       == QStringLiteral("State Register")
                   && report.graphs.first().stateRegisterDetailDisplayName
                       == QStringLiteral("next state_d")
                   && report.graphs.first().stateRegisterTypeDisplayName
                       == QStringLiteral("enum")
                   && report.graphs.first().stateRegisterSourceRoleDisplayName
                       == QStringLiteral("design source")
                   && report.graphs.first().nextStateSignalDisplayName
                       == QStringLiteral("state_d")
                   && report.graphs.first().nextStateSignalTypeDisplayName
                       == QStringLiteral("enum")
                   && report.graphs.first().nextStateSignalSourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);
    expectBool("fsm graph register code link",
               !report.graphs.isEmpty()
                   && report.graphs.first().stateRegisterCodeLink.fileName == fileName
                   && report.graphs.first().stateRegisterCodeLink.line == 3
                   && report.graphs.first().stateRegisterCodeLink.column == 1
                   && report.graphs.first().stateRegisterCodeLink.fileDisplayName
                       == QStringLiteral("fsm_graph_fixture.sv")
                   && report.graphs.first().stateRegisterCodeLink.lineDisplayName
                       == QStringLiteral("3"),
               true);
    expectBool("fsm graph next state code link",
               !report.graphs.isEmpty()
                   && report.graphs.first().nextStateSignalCodeLink.fileName == fileName
                   && report.graphs.first().nextStateSignalCodeLink.line == 4
                   && report.graphs.first().nextStateSignalCodeLink.column == 1
                   && report.graphs.first().nextStateSignalCodeLink.fileDisplayName
                       == QStringLiteral("fsm_graph_fixture.sv")
                   && report.graphs.first().nextStateSignalCodeLink.lineDisplayName
                       == QStringLiteral("4"),
               true);
    expectInt("fsm graph state count",
              report.graphs.isEmpty() ? 0 : report.graphs.first().states.size(),
              3);
    expectBool("fsm graph state row metadata",
               !report.graphs.isEmpty()
                   && report.graphs.first().statesGroupDisplayName
                       == QStringLiteral("States")
                   && report.graphs.first().stateRows.size()
                       == report.graphs.first().states.size()
                   && !report.graphs.first().stateRows.isEmpty()
                   && report.graphs.first().stateRows.first().sectionDisplayName
                       == QStringLiteral("State")
                   && report.graphs.first().stateRows.first().detailDisplayName
                       == QStringLiteral("state_t")
                   && report.graphs.first().stateRows.first().typeDisplayName
                       == QStringLiteral("enum value")
                   && report.graphs.first().stateRows.first().sourceRoleDisplayName
                       == QStringLiteral("design source")
                   && report.graphs.first().stateRows.first().moduleDisplayName
                       == QStringLiteral("fsm_top"),
               true);
    expectBool("fsm graph state row stable key",
               !report.graphs.isEmpty()
                   && !report.graphs.first().stateRows.isEmpty()
                   && report.graphs.first().stateRows.first().stateStableKey
                       == symbolStableKeyForSymbol(
                           report.graphs.first().stateRows.first().state),
               true);
    expectBool("fsm graph state row semantic record",
               !report.graphs.isEmpty()
                   && !report.graphs.first().stateRows.isEmpty()
                   && report.graphs.first().stateRows.first().stateRecord.isValid()
                   && report.graphs.first().stateRows.first().stateRecord.localHandle
                       == report.graphs.first().stateRows.first().state.symbolId
                   && report.graphs.first().stateRows.first().stateRecord.stableKey
                       == report.graphs.first().stateRows.first().stateStableKey
                   && report.graphs.first().stateRows.first().stateRecord.name
                       == report.graphs.first().stateRows.first().state.symbolName
                   && report.graphs.first().stateRows.first()
                          .stateRecord.type.rawTypeText
                       == QStringLiteral("state_t")
                   && report.graphs.first().stateRows.first()
                          .stateRecord.owner.name == QStringLiteral("fsm_top"),
               true);
    expectBool("fsm graph state row code link",
               !report.graphs.isEmpty()
                   && !report.graphs.first().stateRows.isEmpty()
                   && report.graphs.first().stateRows.first().codeLink.fileName
                       == fileName
                   && report.graphs.first().stateRows.first().codeLink.line == 2
                   && report.graphs.first().stateRows.first().codeLink.column == 1
                   && report.graphs.first().stateRows.first().codeLink.fileDisplayName
                       == QStringLiteral("fsm_graph_fixture.sv")
                   && report.graphs.first().stateRows.first().codeLink.lineDisplayName
                       == QStringLiteral("2"),
               true);
    expectInt("fsm graph transition count",
              report.graphs.isEmpty() ? 0 : report.graphs.first().transitions.size(),
              4);
    expectInt("fsm graph transition row count",
              report.graphs.isEmpty() ? 0 : report.graphs.first().transitionRows.size(),
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
    expectBool("fsm graph transition row evidence",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first().fromStateDisplayName
                       == QStringLiteral("IDLE")
                   && report.graphs.first().transitionRows.first().toStateDisplayName
                       == QStringLiteral("RUN")
                   && report.graphs.first().transitionRows.first().conditionDisplayName
                       == QStringLiteral("start")
                   && report.graphs.first().transitionRows.first().sourceLineDisplayName
                       == QStringLiteral("line 10")
                   && report.graphs.first().transitionRows.first().sourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);
    expectBool("fsm graph transition row code link",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first().codeLink.fileName
                       == fileName
                   && report.graphs.first().transitionRows.first().codeLink.line == 10
                   && report.graphs.first().transitionRows.first().codeLink.column == 1
                   && report.graphs.first().transitionRows.first().codeLink.fileDisplayName
                       == QStringLiteral("fsm_graph_fixture.sv")
                   && report.graphs.first().transitionRows.first().codeLink.lineDisplayName
                       == QStringLiteral("10"),
               true);
    expectBool("fsm graph transition state endpoints",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first()
                          .fromStateSymbol.symbolName == QStringLiteral("IDLE")
                   && report.graphs.first().transitionRows.first()
                          .toStateSymbol.symbolName == QStringLiteral("RUN")
                   && report.graphs.first().transitionRows.first()
                          .fromStateStableKey
                       == symbolStableKeyForSymbol(
                           report.graphs.first().transitionRows.first()
                              .fromStateSymbol)
                   && report.graphs.first().transitionRows.first()
                          .toStateStableKey
                       == symbolStableKeyForSymbol(
                           report.graphs.first().transitionRows.first()
                              .toStateSymbol)
                   && report.graphs.first().transitionRows.first()
                          .fromStateCodeLink.fileName == fileName
                   && report.graphs.first().transitionRows.first()
                          .fromStateCodeLink.line == 2
                   && report.graphs.first().transitionRows.first()
                          .fromStateCodeLink.fileDisplayName
                       == QStringLiteral("fsm_graph_fixture.sv")
                   && report.graphs.first().transitionRows.first()
                          .fromStateCodeLink.lineDisplayName == QStringLiteral("2")
                   && report.graphs.first().transitionRows.first()
                          .toStateCodeLink.fileName == fileName
                   && report.graphs.first().transitionRows.first()
                          .toStateCodeLink.line == 2
                   && report.graphs.first().transitionRows.first()
                          .toStateCodeLink.fileDisplayName
                       == QStringLiteral("fsm_graph_fixture.sv")
                   && report.graphs.first().transitionRows.first()
                          .toStateCodeLink.lineDisplayName == QStringLiteral("2"),
               true);
    expectBool("fsm graph transition semantic records",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first()
                          .moduleSymbolRecord.isValid()
                   && report.graphs.first().transitionRows.first()
                          .moduleSymbolRecord.stableKey
                       == report.graphs.first().moduleStableKey
                   && report.graphs.first().transitionRows.first()
                          .fromStateRecord.isValid()
                   && report.graphs.first().transitionRows.first()
                          .fromStateRecord.stableKey
                       == report.graphs.first().transitionRows.first()
                          .fromStateStableKey
                   && report.graphs.first().transitionRows.first()
                          .fromStateRecord.name == QStringLiteral("IDLE")
                   && report.graphs.first().transitionRows.first()
                          .toStateRecord.isValid()
                   && report.graphs.first().transitionRows.first()
                          .toStateRecord.stableKey
                       == report.graphs.first().transitionRows.first()
                          .toStateStableKey
                   && report.graphs.first().transitionRows.first()
                          .toStateRecord.name == QStringLiteral("RUN"),
               true);
    expectBool("fsm graph transition display metadata",
               !report.graphs.isEmpty()
                   && report.graphs.first().transitionsGroupDisplayName
                       == QStringLiteral("Transitions")
                   && !report.graphs.first().transitions.isEmpty()
                   && report.graphs.first().transitions.first().sectionDisplayName
                       == QStringLiteral("IDLE")
                   && report.graphs.first().transitions.first().detailDisplayName
                       == QStringLiteral("state_d when start"),
               true);
    expectBool("fsm graph default transition",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitions.isEmpty()
                   && report.graphs.first().transitions.last().fromState
                       == QStringLiteral("default")
                   && report.graphs.first().transitions.last().toState
                       == QStringLiteral("IDLE"),
               true);

    FsmGraphQuery packageQuery;
    packageQuery.moduleName = QStringLiteral("pkg_fsm_top");
    packageQuery.fileName = packageModuleFileName;
    const FsmGraphReport packageReport = service.buildFsmGraph(packageQuery);
    expectBool("fsm graph package enum found", packageReport.found, true);
    expectInt("fsm graph package enum graph count",
              packageReport.graphs.size(),
              1);
    expectBool("fsm graph package enum state register",
               !packageReport.graphs.isEmpty()
                   && packageReport.graphs.first().stateRegister.symbolName
                       == QStringLiteral("cs")
                   && packageReport.graphs.first().nextStateSignal.symbolName
                       == QStringLiteral("ns"),
               true);
    expectInt("fsm graph package enum state count",
              packageReport.graphs.isEmpty()
                  ? 0
                  : packageReport.graphs.first().states.size(),
              2);
    expectInt("fsm graph package enum transition count",
              packageReport.graphs.isEmpty()
                  ? 0
                  : packageReport.graphs.first().transitions.size(),
              3);
    bool sawPackageTernaryTransition = false;
    bool sawPackageTernaryTransitionStateEndpoints = false;
    for (const FsmTransitionRow& row : packageReport.graphs.first().transitionRows) {
        sawPackageTernaryTransition = sawPackageTernaryTransition
            || (row.fromStateDisplayName == QStringLiteral("IDLE")
                && row.toStateDisplayName == QStringLiteral("RUN")
                && row.conditionDisplayName == QStringLiteral("go")
                && row.sourceRoleDisplayName == QStringLiteral("design source")
                && row.codeLink.fileName == packageModuleFileName
                && row.codeLink.line == 6);
        sawPackageTernaryTransitionStateEndpoints =
            sawPackageTernaryTransitionStateEndpoints
            || (row.fromStateDisplayName == QStringLiteral("IDLE")
                && row.toStateDisplayName == QStringLiteral("RUN")
                && row.fromStateCodeLink.fileName == packageFileName
                && row.fromStateCodeLink.line == 3
                && row.fromStateCodeLink.fileDisplayName
                    == QStringLiteral("fsm_pkg_fixture.sv")
                && row.fromStateCodeLink.lineDisplayName == QStringLiteral("3")
                && row.toStateCodeLink.fileName == packageFileName
                && row.toStateCodeLink.line == 4
                && row.toStateCodeLink.fileDisplayName
                    == QStringLiteral("fsm_pkg_fixture.sv")
                && row.toStateCodeLink.lineDisplayName == QStringLiteral("4"));
    }
    expectBool("fsm graph package enum transition evidence",
               sawPackageTernaryTransition,
               true);
    expectBool("fsm graph package enum transition state endpoints",
               sawPackageTernaryTransitionStateEndpoints,
               true);
    bool sawPackageTernaryElseTransition = false;
    for (const FsmTransitionRow& row : packageReport.graphs.first().transitionRows) {
        sawPackageTernaryElseTransition = sawPackageTernaryElseTransition
            || (row.fromStateDisplayName == QStringLiteral("IDLE")
                && row.toStateDisplayName == QStringLiteral("IDLE")
                && row.conditionDisplayName == QStringLiteral("else go")
                && row.codeLink.line == 6);
    }
    expectBool("fsm graph package enum ternary else evidence",
               sawPackageTernaryElseTransition,
               true);
    expectBool("fsm graph package enum state link",
               !packageReport.graphs.isEmpty()
                   && !packageReport.graphs.first().stateRows.isEmpty()
                   && packageReport.graphs.first().stateRows.first()
                          .codeLink.fileName == packageFileName
                   && packageReport.graphs.first().stateRows.first()
                          .codeLink.line > 0,
               true);
    expectBool("fsm graph package enum state metadata",
               !packageReport.graphs.isEmpty()
                   && !packageReport.graphs.first().stateRows.isEmpty()
                   && packageReport.graphs.first().stateRows.first()
                          .stateRecord.isValid()
                   && packageReport.graphs.first().stateRows.first()
                          .stateRecord.stableKey
                       == packageReport.graphs.first().stateRows.first()
                          .stateStableKey
                   && packageReport.graphs.first().stateRows.first()
                          .stateRecord.type.rawTypeText
                       == QStringLiteral("pkg_state_e")
                   && packageReport.graphs.first().stateRows.first()
                          .stateRecord.owner.name == QStringLiteral("fsm_pkg")
                   && packageReport.graphs.first().stateRows.first()
                          .typeDisplayName == QStringLiteral("enum value")
                   && packageReport.graphs.first().stateRows.first()
                          .sourceRoleDisplayName == QStringLiteral("design source")
                   && packageReport.graphs.first().stateRows.first()
                          .moduleDisplayName == QStringLiteral("fsm_pkg"),
               true);

    FsmGraphQuery emptyModuleQuery;
    const FsmGraphReport emptyModuleReport =
        service.buildFsmGraph(emptyModuleQuery);
    expectBool("fsm graph empty module reason",
               !emptyModuleReport.found
                   && emptyModuleReport.notFoundReason
                       == FsmGraphNotFoundReason::EmptyModuleName
                   && emptyModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("empty module name"),
               true);

    FsmGraphQuery missingModuleQuery;
    missingModuleQuery.moduleName = QStringLiteral("missing_fsm_top");
    missingModuleQuery.fileName = fileName;
    const FsmGraphReport missingModuleReport =
        service.buildFsmGraph(missingModuleQuery);
    expectBool("fsm graph missing module reason",
               !missingModuleReport.found
                   && missingModuleReport.notFoundReason
                       == FsmGraphNotFoundReason::NoMatchingModule
                   && missingModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("no matching module"),
               true);

    FsmGraphQuery unsupportedModuleQuery;
    unsupportedModuleQuery.moduleStableKey = symbolStableKeyForSymbol(stateQ);
    const FsmGraphReport unsupportedModuleReport =
        service.buildFsmGraph(unsupportedModuleQuery);
    expectBool("fsm graph unsupported symbol reason",
               !unsupportedModuleReport.found
                   && unsupportedModuleReport.notFoundReason
                       == FsmGraphNotFoundReason::UnsupportedSymbolKind
                   && unsupportedModuleReport.notFoundReasonDisplayName
                       == QStringLiteral("unsupported symbol kind"),
               true);

    FsmGraphQuery noFsmGraphQuery;
    noFsmGraphQuery.moduleStableKey = symbolStableKeyForSymbol(noFsmModule);
    const FsmGraphReport noFsmGraphReport =
        service.buildFsmGraph(noFsmGraphQuery);
    expectBool("fsm graph no graph reason",
               !noFsmGraphReport.found
                   && noFsmGraphReport.notFoundReason
                       == FsmGraphNotFoundReason::NoFsmGraph
                   && noFsmGraphReport.notFoundReasonDisplayName
                       == QStringLiteral("no FSM graph"),
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
    sym_list::SymbolInfo beforeDataPort = makeModuleBriefSymbol(
        9403,
        fileName,
        QStringLiteral("data"),
        sym_list::sym_port_input,
        3,
        QStringLiteral("diff_top"));
    beforeDataPort.dataType = QStringLiteral("logic [7:0]");
    beforeSymbols.append(beforeDataPort);
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
    beforeSymbols.append(makeModuleBriefSymbol(
        9407,
        fileName,
        QStringLiteral("old_pkg"),
        sym_list::sym_package,
        30));
    beforeSymbols.append(makeModuleBriefSymbol(
        9408,
        fileName,
        QStringLiteral("old_t"),
        sym_list::sym_typedef,
        31,
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
    sym_list::SymbolInfo afterDataPort = makeModuleBriefSymbol(
        9503,
        fileName,
        QStringLiteral("data"),
        sym_list::sym_port_output,
        3,
        QStringLiteral("diff_top"));
    afterDataPort.dataType = QStringLiteral("logic [15:0]");
    afterSymbols.append(afterDataPort);
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
    afterSymbols.append(makeModuleBriefSymbol(
        9507,
        fileName,
        QStringLiteral("diff_if"),
        sym_list::sym_interface,
        30));
    afterSymbols.append(makeModuleBriefSymbol(
        9508,
        fileName,
        QStringLiteral("state_t"),
        sym_list::sym_typedef,
        31,
        QStringLiteral("diff_top")));

    QList<SemanticRelationship> beforeRelationships;
    SemanticRelationship beforeInst;
    beforeInst.fromId = 9401;
    beforeInst.toId = 9405;
    beforeInst.type = SymbolRelationshipEngine::INSTANTIATES;
    beforeInst.provenance = RelationshipProvenance::Workspace;
    beforeInst.confidence = 70;
    beforeInst.evidenceText = QStringLiteral("old instance u_old");
    beforeRelationships.append(beforeInst);

    QList<SemanticRelationship> afterRelationships;
    SemanticRelationship afterInst;
    afterInst.fromId = 9501;
    afterInst.toId = 9505;
    afterInst.type = SymbolRelationshipEngine::INSTANTIATES;
    afterInst.provenance = RelationshipProvenance::Inferred;
    afterInst.confidence = 90;
    afterInst.evidenceText = QStringLiteral("new instance u_new");
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
    bool oldPackageRemoved = false;
    bool newInterfaceAdded = false;
    bool newTypeAdded = false;
    bool symbolDisplayMetadataFound = false;
    bool symbolCodeLinkFound = false;
    bool symbolScopeMetadataFound = false;
    bool symbolTypeMetadataFound = false;
    bool symbolBeforeAfterMetadataFound = false;
    bool symbolStableKeyFound = false;
    bool symbolRecordMetadataFound = false;
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
            symbolDisplayMetadataFound =
                change.kindDisplayName == QStringLiteral("Modified")
                && change.categoryDisplayName == QStringLiteral("port")
                && change.displaySymbol.symbolName == QStringLiteral("data")
                && change.detailDisplayName.contains(QStringLiteral("port"))
                && change.detailDisplayName.contains(QStringLiteral("scope diff_top"))
                && change.detailDisplayName.contains(QStringLiteral("design source"));
            symbolScopeMetadataFound =
                change.scopeDisplayName == QStringLiteral("scope diff_top");
            symbolTypeMetadataFound =
                change.symbolTypeDisplayName == QStringLiteral("output");
            symbolBeforeAfterMetadataFound =
                change.beforeSymbolTypeDisplayName == QStringLiteral("input")
                && change.afterSymbolTypeDisplayName == QStringLiteral("output")
                && change.beforeStableKey
                    == symbolStableKeyForSymbol(change.beforeSymbol)
                && change.afterStableKey
                    == symbolStableKeyForSymbol(change.afterSymbol)
                && change.beforeScopeDisplayName == QStringLiteral("scope diff_top")
                && change.afterScopeDisplayName == QStringLiteral("scope diff_top")
                && change.beforeSourceRoleDisplayName
                    == QStringLiteral("design source")
                && change.afterSourceRoleDisplayName
                    == QStringLiteral("design source")
                && change.beforeDataTypeDisplayName
                    == QStringLiteral("logic [7:0]")
                && change.afterDataTypeDisplayName
                    == QStringLiteral("logic [15:0]")
                && change.beforeCodeLink.fileName == fileName
                && change.beforeCodeLink.line == 3
                && change.beforeCodeLink.fileDisplayName
                    == QStringLiteral("semantic_diff_fixture.sv")
                && change.afterCodeLink.fileName == fileName
                && change.afterCodeLink.line == 3
                && change.afterCodeLink.fileDisplayName
                    == QStringLiteral("semantic_diff_fixture.sv");
            symbolCodeLinkFound =
                change.codeLink.fileName == fileName
                && change.codeLink.line == 3
                && change.codeLink.column == 1
                && change.codeLink.fileDisplayName
                    == QStringLiteral("semantic_diff_fixture.sv")
                && change.codeLink.lineDisplayName == QStringLiteral("3");
            symbolStableKeyFound =
                change.displayStableKey
                == symbolStableKeyForSymbol(change.displaySymbol);
            symbolRecordMetadataFound =
                change.beforeSymbolRecord.isValid()
                && change.beforeSymbolRecord.localHandle == 9403
                && change.beforeSymbolRecord.stableKey
                    == change.beforeStableKey
                && change.beforeSymbolRecord.name == QStringLiteral("data")
                && change.beforeSymbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Port
                && change.beforeSymbolRecord.type.rawTypeText
                    == QStringLiteral("logic [7:0]")
                && change.afterSymbolRecord.isValid()
                && change.afterSymbolRecord.localHandle == 9503
                && change.afterSymbolRecord.stableKey
                    == change.afterStableKey
                && change.afterSymbolRecord.name == QStringLiteral("data")
                && change.afterSymbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Port
                && change.afterSymbolRecord.type.rawTypeText
                    == QStringLiteral("logic [15:0]")
                && change.displaySymbolRecord.isValid()
                && change.displaySymbolRecord.localHandle == 9503
                && change.displaySymbolRecord.stableKey
                    == change.displayStableKey
                && change.displaySymbolRecord.owner.name
                    == QStringLiteral("diff_top")
                && change.displaySymbolRecord.sourceRole
                    == SymbolTaxonomy::SourceRole::DesignSource;
        }
        if (change.kind == SemanticDiffChangeKind::Added
            && change.category == SemanticDiffSymbolCategory::Signal
            && change.afterSymbol.symbolName == QStringLiteral("state_q")) {
            newSignalAdded = true;
        }
        if (change.kind == SemanticDiffChangeKind::Removed
            && change.category == SemanticDiffSymbolCategory::Package
            && change.beforeSymbol.symbolName == QStringLiteral("old_pkg")) {
            oldPackageRemoved =
                change.categoryDisplayName == QStringLiteral("package")
                && change.categoryGroupDisplayName == QStringLiteral("Packages")
                && change.sourceRoleDisplayName == QStringLiteral("design source");
        }
        if (change.kind == SemanticDiffChangeKind::Added
            && change.category == SemanticDiffSymbolCategory::Interface
            && change.afterSymbol.symbolName == QStringLiteral("diff_if")) {
            newInterfaceAdded =
                change.categoryDisplayName == QStringLiteral("interface")
                && change.categoryGroupDisplayName == QStringLiteral("Interfaces");
        }
        if (change.kind == SemanticDiffChangeKind::Added
            && change.category == SemanticDiffSymbolCategory::Type
            && change.afterSymbol.symbolName == QStringLiteral("state_t")) {
            newTypeAdded =
                change.categoryDisplayName == QStringLiteral("type")
                && change.categoryGroupDisplayName == QStringLiteral("Types");
        }
    }

    int addedRelationships = 0;
    int removedRelationships = 0;
    bool addedRelationshipHasEndpoints = false;
    bool removedRelationshipHasEndpoints = false;
    bool relationshipDisplayMetadataFound = false;
    bool relationshipCodeLinkFound = false;
    bool relationshipEndpointLinksFound = false;
    bool relationshipStableKeyFound = false;
    bool relationshipRecordMetadataFound = false;
    bool relationshipEvidenceMetadataFound = false;
    for (const SemanticDiffRelationshipChange& change : report.relationshipChanges) {
        if (change.kind == SemanticDiffChangeKind::Added) {
            ++addedRelationships;
            addedRelationshipHasEndpoints =
                change.afterFromSymbol.symbolName == QStringLiteral("diff_top")
                && change.afterToSymbol.symbolName == QStringLiteral("u_new");
            relationshipDisplayMetadataFound =
                change.kindDisplayName == QStringLiteral("Added")
                && change.relationshipTypeDisplayName == QStringLiteral("Instantiates")
                && change.displayFromSymbol.symbolName == QStringLiteral("diff_top")
                && change.fromSymbolDisplayName == QStringLiteral("diff_top")
                && change.toSymbolDisplayName == QStringLiteral("u_new")
                && change.sourceRoleDisplayName == QStringLiteral("design source")
                && change.detailDisplayName == QStringLiteral("diff_top -> u_new");
            relationshipCodeLinkFound =
                change.codeLink.fileName == fileName
                && change.codeLink.line == 1
                && change.codeLink.column == 1
                && change.codeLink.fileDisplayName
                    == QStringLiteral("semantic_diff_fixture.sv")
                && change.codeLink.lineDisplayName == QStringLiteral("1");
            relationshipEndpointLinksFound =
                change.fromCodeLink.fileName == fileName
                && change.fromCodeLink.line == 1
                && change.fromCodeLink.fileDisplayName
                    == QStringLiteral("semantic_diff_fixture.sv")
                && change.toCodeLink.fileName == fileName
                && change.toCodeLink.line == 10
                && change.toCodeLink.fileDisplayName
                    == QStringLiteral("semantic_diff_fixture.sv");
            relationshipStableKeyFound =
                change.displayFromStableKey
                    == symbolStableKeyForSymbol(change.displayFromSymbol)
                && change.displayToStableKey
                    == symbolStableKeyForSymbol(change.displayToSymbol)
                && change.afterFromStableKey
                    == symbolStableKeyForSymbol(change.afterFromSymbol)
                && change.afterToStableKey
                    == symbolStableKeyForSymbol(change.afterToSymbol);
            relationshipRecordMetadataFound =
                change.afterFromSymbolRecord.isValid()
                && change.afterFromSymbolRecord.localHandle == 9501
                && change.afterFromSymbolRecord.stableKey
                    == change.afterFromStableKey
                && change.afterFromSymbolRecord.name
                    == QStringLiteral("diff_top")
                && change.afterToSymbolRecord.isValid()
                && change.afterToSymbolRecord.localHandle == 9505
                && change.afterToSymbolRecord.stableKey
                    == change.afterToStableKey
                && change.afterToSymbolRecord.name == QStringLiteral("u_new")
                && change.displayFromSymbolRecord.isValid()
                && change.displayFromSymbolRecord.stableKey
                    == change.displayFromStableKey
                && change.displayToSymbolRecord.isValid()
                && change.displayToSymbolRecord.stableKey
                    == change.displayToStableKey
                && change.displayFromSymbolRecord.sourceRole
                    == SymbolTaxonomy::SourceRole::DesignSource;
            relationshipEvidenceMetadataFound =
                change.provenance == RelationshipProvenance::Inferred
                && change.provenanceDisplayName == QStringLiteral("inferred")
                && change.confidence == 90
                && change.confidenceDisplayName == QStringLiteral("90%")
                && change.evidenceText == QStringLiteral("new instance u_new")
                && change.evidenceDisplayName
                    == QStringLiteral("new instance u_new");
        }
        if (change.kind == SemanticDiffChangeKind::Removed) {
            ++removedRelationships;
            removedRelationshipHasEndpoints =
                change.beforeFromSymbol.symbolName == QStringLiteral("diff_top")
                && change.beforeToSymbol.symbolName == QStringLiteral("u_old")
                && change.beforeFromSymbolRecord.isValid()
                && change.beforeFromSymbolRecord.localHandle == 9401
                && change.beforeFromSymbolRecord.stableKey
                    == change.beforeFromStableKey
                && change.beforeToSymbolRecord.isValid()
                && change.beforeToSymbolRecord.localHandle == 9405
                && change.beforeToSymbolRecord.stableKey
                    == change.beforeToStableKey
                && change.displayFromSymbolRecord.stableKey
                    == change.beforeFromStableKey
                && change.displayToSymbolRecord.stableKey
                    == change.beforeToStableKey;
        }
    }

    int addedDiagnostics = 0;
    int removedDiagnostics = 0;
    bool diagnosticDisplayMetadataFound = false;
    bool diagnosticCodeLinkFound = false;
    for (const SemanticDiffDiagnosticChange& change : report.diagnosticChanges) {
        if (change.kind == SemanticDiffChangeKind::Added) {
            ++addedDiagnostics;
            diagnosticDisplayMetadataFound =
                change.kindDisplayName == QStringLiteral("Added")
                && change.severityDisplayName == QStringLiteral("Error")
                && change.sourceRoleDisplayName == QStringLiteral("design source")
                && change.detailDisplayName == QStringLiteral("Error, design source")
                && change.displayDiagnostic.message == QStringLiteral("new error");
            diagnosticCodeLinkFound =
                change.codeLink.fileName == fileName
                && change.codeLink.line == 22
                && change.codeLink.column == 7
                && change.codeLink.fileDisplayName
                    == QStringLiteral("semantic_diff_fixture.sv")
                && change.codeLink.lineDisplayName == QStringLiteral("22");
        }
        if (change.kind == SemanticDiffChangeKind::Removed)
            ++removedDiagnostics;
    }

    expectBool("semantic diff found", report.found, true);
    expectBool("semantic diff found reason metadata",
               report.notFoundReason == SemanticDiffNotFoundReason::None
                   && report.notFoundReasonDisplayName.isEmpty(),
               true);
    expectBool("semantic diff report metadata",
               report.symbolGroupDisplayName == QStringLiteral("Semantic Diff Symbols")
                   && report.relationshipGroupDisplayName
                       == QStringLiteral("Semantic Diff Relationships")
                   && report.diagnosticGroupDisplayName
                       == QStringLiteral("Semantic Diff Diagnostics")
                   && report.symbolChangeCount == report.symbolChanges.size()
                   && report.relationshipChangeCount
                       == report.relationshipChanges.size()
                   && report.diagnosticChangeCount
                       == report.diagnosticChanges.size(),
               true);
    expectInt("semantic diff added symbols", addedSymbols, 5);
    expectInt("semantic diff removed symbols", removedSymbols, 5);
    expectInt("semantic diff modified symbols", modifiedSymbols, 1);
    expectBool("semantic diff modified data port", dataPortModified, true);
    expectBool("semantic diff added state signal", newSignalAdded, true);
    expectBool("semantic diff removed package metadata",
               oldPackageRemoved,
               true);
    expectBool("semantic diff added interface metadata",
               newInterfaceAdded,
               true);
    expectBool("semantic diff added type metadata",
               newTypeAdded,
               true);
    expectBool("semantic diff symbol display metadata",
               symbolDisplayMetadataFound, true);
    expectBool("semantic diff symbol scope metadata",
               symbolScopeMetadataFound,
               true);
    expectBool("semantic diff symbol type metadata",
               symbolTypeMetadataFound,
               true);
    expectBool("semantic diff symbol before after metadata",
               symbolBeforeAfterMetadataFound,
               true);
    expectBool("semantic diff symbol stable key",
               symbolStableKeyFound,
               true);
    expectBool("semantic diff symbol semantic records",
               symbolRecordMetadataFound,
               true);
    expectBool("semantic diff symbol code link",
               symbolCodeLinkFound,
               true);
    expectInt("semantic diff added relationships", addedRelationships, 1);
    expectInt("semantic diff removed relationships", removedRelationships, 1);
    expectBool("semantic diff added relationship endpoints",
               addedRelationshipHasEndpoints,
               true);
    expectBool("semantic diff removed relationship endpoints",
               removedRelationshipHasEndpoints,
               true);
    expectBool("semantic diff relationship display metadata",
               relationshipDisplayMetadataFound,
               true);
    expectBool("semantic diff relationship code link",
               relationshipCodeLinkFound,
               true);
    expectBool("semantic diff relationship endpoint links",
               relationshipEndpointLinksFound,
               true);
    expectBool("semantic diff relationship stable key",
               relationshipStableKeyFound,
               true);
    expectBool("semantic diff relationship semantic records",
               relationshipRecordMetadataFound,
               true);
    expectBool("semantic diff relationship evidence metadata",
               relationshipEvidenceMetadataFound,
               true);
    expectInt("semantic diff added diagnostics", addedDiagnostics, 1);
    expectInt("semantic diff removed diagnostics", removedDiagnostics, 1);
    expectBool("semantic diff diagnostic display metadata",
               diagnosticDisplayMetadataFound, true);
    expectBool("semantic diff diagnostic code link",
               diagnosticCodeLinkFound,
               true);

    SemanticDiffQuery noChangesQuery = query;
    noChangesQuery.afterSnapshot = beforeSnapshot;
    const SemanticDiffReport noChangesReport =
        service.buildSemanticDiff(noChangesQuery);
    expectBool("semantic diff no changes reason",
               !noChangesReport.found
                   && noChangesReport.notFoundReason
                       == SemanticDiffNotFoundReason::NoChanges
                   && noChangesReport.notFoundReasonDisplayName
                       == QStringLiteral("no semantic changes"),
               true);

    SemanticDiffQuery missingSnapshotQuery = query;
    missingSnapshotQuery.afterSnapshot.reset();
    const SemanticDiffReport missingSnapshotReport =
        service.buildSemanticDiff(missingSnapshotQuery);
    expectBool("semantic diff missing snapshot reason",
               !missingSnapshotReport.found
                   && missingSnapshotReport.notFoundReason
                       == SemanticDiffNotFoundReason::MissingSnapshot
                   && missingSnapshotReport.notFoundReasonDisplayName
                       == QStringLiteral("missing snapshot"),
               true);
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
    const QString chlCtrlPath = normalizedPath(
        QDir(workspaceRoot).filePath(QStringLiteral("elec_phy_import/ctrl/chl_ctrl.sv")));
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

    QList<sym_list::SymbolInfo> symbols =
        slang.extractWorkspaceSymbols(snapshot.systemVerilogFiles,
                                      snapshot.includeDirs,
                                      snapshot.defines);
    for (int i = 0; i < symbols.size(); ++i)
        symbols[i].symbolId = i + 1;
    QHash<QString, QString> fileContents;
    for (const QString& fileName : files)
        fileContents.insert(fileName, loadTextFile(fileName));
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
    const int realClockId =
        symbolIdInScope(symbols,
                        QStringLiteral("clk_main"),
                        sym_list::sym_port_input,
                        QStringLiteral("rtl_top"));
    const int realResetId =
        symbolIdInScope(symbols,
                        QStringLiteral("srst_main"),
                        sym_list::sym_logic,
                        QStringLiteral("rtl_top"));
    const int chlCtrlId =
        symbolId(symbols, QStringLiteral("chl_ctrl"), sym_list::sym_module);
    const int phyPassCsId =
        symbolIdInScope(symbols,
                        QStringLiteral("phy_pass_thrg_cfg_cs"),
                        sym_list::sym_enum_var,
                        QStringLiteral("chl_ctrl"));
    const int phyPassNsId =
        symbolIdInScope(symbols,
                        QStringLiteral("phy_pass_thrg_cfg_ns"),
                        sym_list::sym_enum_var,
                        QStringLiteral("chl_ctrl"));
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
    const sym_list::SymbolInfo rtlTopSymbol =
        symbolByNameAndType(QStringLiteral("rtl_top"), sym_list::sym_module);
    const sym_list::SymbolInfo interfaceSymbol =
        symbolByNameAndType(QStringLiteral("lr_genr_if"), sym_list::sym_interface);
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
    expectBool("real workspace has top clock",
               realClockId >= 0, true);
    expectBool("real workspace has top reset",
               realResetId >= 0, true);
    expectBool("real workspace has chl_ctrl module",
               chlCtrlId >= 0, true);
    expectBool("real workspace has chl_ctrl current fsm state",
               phyPassCsId >= 0, true);
    expectBool("real workspace has chl_ctrl next fsm state",
               phyPassNsId >= 0, true);
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

    QList<SemanticRelationship> realRelationships;
    if (packageId >= 0) {
        for (const sym_list::SymbolInfo& symbol : symbols) {
            if (symbol.symbolName != QStringLiteral("rtl_top")
                || symbol.symbolType != sym_list::sym_module) {
                continue;
            }
            SemanticRelationship packageImportRelationship;
            packageImportRelationship.fromId = symbol.symbolId;
            packageImportRelationship.toId = packageId;
            packageImportRelationship.type = SymbolRelationshipEngine::REFERENCES;
            realRelationships.append(packageImportRelationship);
            if (realClockId >= 0) {
                SemanticRelationship clockRelationship;
                clockRelationship.fromId = realClockId;
                clockRelationship.toId = symbol.symbolId;
                clockRelationship.type = SymbolRelationshipEngine::CLOCKS;
                realRelationships.append(clockRelationship);
            }
            if (realResetId >= 0) {
                SemanticRelationship resetRelationship;
                resetRelationship.fromId = realResetId;
                resetRelationship.toId = symbol.symbolId;
                resetRelationship.type = SymbolRelationshipEngine::RESETS;
                realRelationships.append(resetRelationship);
            }
        }
    }
    if (interfaceInstId >= 0 && interfaceModportId >= 0) {
        SemanticRelationship interfaceModportRelationship;
        interfaceModportRelationship.fromId = interfaceInstId;
        interfaceModportRelationship.toId = interfaceModportId;
        interfaceModportRelationship.type = SymbolRelationshipEngine::REFERENCES;
        realRelationships.append(interfaceModportRelationship);
    }

    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(
        symbols,
        realRelationships,
        QList<SemanticDiagnostic>(),
        fileContents));
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
    expectBool("real workspace package parameter definition record",
               packageParam.symbolRecord.isValid()
                   && packageParam.symbolRecord.stableKey == packageParam.symbolStableKey
                   && packageParam.symbolRecord.owner.name == QStringLiteral("gl_pkg")
                   && packageParam.symbolRecord.sourceRole
                       == SymbolTaxonomy::SourceRole::DesignSource,
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
    expectBool("real workspace interface definition record",
               interfaceResult.symbolRecord.isValid()
                   && interfaceResult.symbolRecord.stableKey
                       == interfaceResult.symbolStableKey
                   && interfaceResult.symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Interface
                   && interfaceResult.symbolRecord.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Global,
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
    expectBool("real workspace modport type definition record",
               modportTypeResult.symbolRecord.isValid()
                   && modportTypeResult.symbolRecord.owner.name
                       == QStringLiteral("lr_genr_if")
                   && modportTypeResult.symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Modport,
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
    expectBool("real workspace modport instance definition record",
               modportInstResult.symbolRecord.isValid()
                   && modportInstResult.symbolRecord.stableKey
                       == modportTypeResult.symbolRecord.stableKey
                   && modportInstResult.symbolRecord.owner.name
                       == QStringLiteral("lr_genr_if"),
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

    ModuleBriefService moduleBriefService(&index);
    ModuleBriefQuery moduleBriefQuery;
    moduleBriefQuery.moduleName = QStringLiteral("rtl_top");
    moduleBriefQuery.fileName = topPath;
    const ModuleBriefReport moduleBrief =
        moduleBriefService.buildModuleBrief(moduleBriefQuery);
    bool sawRealPackageContext = false;
    bool sawRealInterfaceContext = false;
    bool sawRealPackageContextLink = false;
    bool sawRealInterfaceContextLink = false;
    bool sawRealPackageContextMetadata = false;
    bool sawRealInterfaceContextMetadata = false;
    bool sawRealPackageParameterContext = false;
    bool sawRealPackageTypedefContext = false;
    for (const ModuleBriefContextRow& row : moduleBrief.contextRows) {
        sawRealPackageContext = sawRealPackageContext
            || (row.sectionDisplayName == QStringLiteral("Package")
                && row.symbolDisplayName == QStringLiteral("gl_pkg"));
        sawRealPackageContextMetadata = sawRealPackageContextMetadata
            || (row.sectionDisplayName == QStringLiteral("Package")
                && row.symbolDisplayName == QStringLiteral("gl_pkg")
                && row.contextKindDisplayName == QStringLiteral("package import")
                && row.symbolTypeDisplayName == QStringLiteral("package")
                && !row.sourceRoleDisplayName.isEmpty());
        sawRealPackageContextLink = sawRealPackageContextLink
            || (row.sectionDisplayName == QStringLiteral("Package")
                && row.symbolDisplayName == QStringLiteral("gl_pkg")
                && !row.codeLink.fileName.isEmpty()
                && row.codeLink.line > 0
                && !row.codeLink.fileDisplayName.isEmpty()
                && !row.codeLink.lineDisplayName.isEmpty());
        sawRealPackageParameterContext = sawRealPackageParameterContext
            || (row.sectionDisplayName == QStringLiteral("Package Member")
                && row.symbolDisplayName == QStringLiteral("P_SW_NUM")
                && (row.contextKindDisplayName == QStringLiteral("package parameter")
                    || row.contextKindDisplayName
                        == QStringLiteral("package localparam"))
                && (row.symbolTypeDisplayName == QStringLiteral("parameter")
                    || row.symbolTypeDisplayName == QStringLiteral("localparam"))
                && !row.sourceRoleDisplayName.isEmpty()
                && !row.codeLink.fileName.isEmpty()
                && row.codeLink.line > 0
                && !row.codeLink.fileDisplayName.isEmpty()
                && !row.codeLink.lineDisplayName.isEmpty());
        sawRealPackageTypedefContext = sawRealPackageTypedefContext
            || (row.sectionDisplayName == QStringLiteral("Package Member")
                && row.symbolDisplayName == QStringLiteral("cpld_sw_sp")
                && row.contextKindDisplayName == QStringLiteral("package typedef")
                && row.symbolTypeDisplayName == QStringLiteral("typedef")
                && !row.sourceRoleDisplayName.isEmpty()
                && !row.codeLink.fileName.isEmpty()
                && row.codeLink.line > 0
                && !row.codeLink.fileDisplayName.isEmpty()
                && !row.codeLink.lineDisplayName.isEmpty());
        sawRealInterfaceContext = sawRealInterfaceContext
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && (row.symbolDisplayName == QStringLiteral("LR_GENR_IF")
                    || row.detailDisplayName.contains(QStringLiteral("lr_genr_if"))));
        sawRealInterfaceContextMetadata = sawRealInterfaceContextMetadata
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && (row.symbolDisplayName == QStringLiteral("LR_GENR_IF")
                    || row.detailDisplayName.contains(QStringLiteral("lr_genr_if")))
                && row.contextKindDisplayName.contains(QStringLiteral("interface"))
                && !row.symbolTypeDisplayName.isEmpty()
                && !row.sourceRoleDisplayName.isEmpty());
        sawRealInterfaceContextLink = sawRealInterfaceContextLink
            || (row.sectionDisplayName == QStringLiteral("Interface")
                && (row.symbolDisplayName == QStringLiteral("LR_GENR_IF")
                    || row.detailDisplayName.contains(QStringLiteral("lr_genr_if")))
                && !row.codeLink.fileName.isEmpty()
                && row.codeLink.line > 0
                && !row.codeLink.fileDisplayName.isEmpty()
                && !row.codeLink.lineDisplayName.isEmpty());
    }
    expectBool("real workspace module brief found",
               moduleBrief.found,
               true);
    expectBool("real workspace module brief package context",
               sawRealPackageContext,
               true);
    expectBool("real workspace module brief package context metadata",
               sawRealPackageContextMetadata,
               true);
    expectBool("real workspace module brief package context code link",
               sawRealPackageContextLink,
               true);
    expectBool("real workspace module brief package parameter context",
               sawRealPackageParameterContext,
               true);
    expectBool("real workspace module brief package typedef context",
               sawRealPackageTypedefContext,
               true);
    expectBool("real workspace module brief interface context",
               sawRealInterfaceContext,
               true);
    expectBool("real workspace module brief interface context metadata",
               sawRealInterfaceContextMetadata,
               true);
    expectBool("real workspace module brief interface context code link",
               sawRealInterfaceContextLink,
               true);
    bool sawRealPackageRelationshipEvidence = false;
    bool sawRealClockRelationshipEvidence = false;
    bool sawRealResetRelationshipEvidence = false;
    bool sawRealPackageRelationshipEvidenceLink = false;
    bool sawRealClockRelationshipEvidenceLink = false;
    bool sawRealResetRelationshipEvidenceLink = false;
    bool sawRealClockRelationshipEndpointLinks = false;
    bool sawRealResetRelationshipEndpointLinks = false;
    for (const ModuleBriefRelationshipEvidenceRow& row
         : moduleBrief.relationshipEvidenceRows) {
        sawRealPackageRelationshipEvidence = sawRealPackageRelationshipEvidence
            || (row.outgoing
                && row.peerDisplayName == QStringLiteral("gl_pkg")
                && row.typeDisplayName == QStringLiteral("References")
                && row.detailDisplayName == QStringLiteral("Outgoing References"));
        sawRealPackageRelationshipEvidenceLink = sawRealPackageRelationshipEvidenceLink
            || (row.outgoing
                && row.peerDisplayName == QStringLiteral("gl_pkg")
                && !row.peerCodeLink.fileName.isEmpty()
                && row.peerCodeLink.line > 0
                && !row.peerCodeLink.fileDisplayName.isEmpty()
                && !row.peerCodeLink.lineDisplayName.isEmpty());
        sawRealClockRelationshipEvidence = sawRealClockRelationshipEvidence
            || (!row.outgoing
                && row.peerDisplayName == QStringLiteral("clk_main")
                && row.typeDisplayName == QStringLiteral("Clocks")
                && row.detailDisplayName == QStringLiteral("Incoming Clocks"));
        sawRealClockRelationshipEvidenceLink = sawRealClockRelationshipEvidenceLink
            || (!row.outgoing
                && row.peerDisplayName == QStringLiteral("clk_main")
                && !row.peerCodeLink.fileName.isEmpty()
                && row.peerCodeLink.line > 0
                && !row.peerCodeLink.fileDisplayName.isEmpty()
                && !row.peerCodeLink.lineDisplayName.isEmpty());
        sawRealClockRelationshipEndpointLinks = sawRealClockRelationshipEndpointLinks
            || (!row.outgoing
                && row.fromSymbolDisplayName == QStringLiteral("clk_main")
                && row.toSymbolDisplayName == QStringLiteral("rtl_top")
                && !row.fromCodeLink.fileName.isEmpty()
                && row.fromCodeLink.line > 0
                && !row.fromCodeLink.fileDisplayName.isEmpty()
                && !row.fromCodeLink.lineDisplayName.isEmpty()
                && !row.toCodeLink.fileName.isEmpty()
                && row.toCodeLink.line > 0
                && !row.toCodeLink.fileDisplayName.isEmpty()
                && !row.toCodeLink.lineDisplayName.isEmpty());
        sawRealResetRelationshipEvidence = sawRealResetRelationshipEvidence
            || (!row.outgoing
                && row.peerDisplayName == QStringLiteral("srst_main")
                && row.typeDisplayName == QStringLiteral("Resets")
                && row.detailDisplayName == QStringLiteral("Incoming Resets"));
        sawRealResetRelationshipEvidenceLink = sawRealResetRelationshipEvidenceLink
            || (!row.outgoing
                && row.peerDisplayName == QStringLiteral("srst_main")
                && !row.peerCodeLink.fileName.isEmpty()
                && row.peerCodeLink.line > 0
                && !row.peerCodeLink.fileDisplayName.isEmpty()
                && !row.peerCodeLink.lineDisplayName.isEmpty());
        sawRealResetRelationshipEndpointLinks = sawRealResetRelationshipEndpointLinks
            || (!row.outgoing
                && row.fromSymbolDisplayName == QStringLiteral("srst_main")
                && row.toSymbolDisplayName == QStringLiteral("rtl_top")
                && !row.fromCodeLink.fileName.isEmpty()
                && row.fromCodeLink.line > 0
                && !row.fromCodeLink.fileDisplayName.isEmpty()
                && !row.fromCodeLink.lineDisplayName.isEmpty()
                && !row.toCodeLink.fileName.isEmpty()
                && row.toCodeLink.line > 0
                && !row.toCodeLink.fileDisplayName.isEmpty()
                && !row.toCodeLink.lineDisplayName.isEmpty());
    }
    expectBool("real workspace module brief package relationship evidence",
               sawRealPackageRelationshipEvidence,
               true);
    expectBool("real workspace module brief package relationship evidence link",
               sawRealPackageRelationshipEvidenceLink,
               true);
    expectBool("real workspace module brief clock relationship evidence",
               sawRealClockRelationshipEvidence,
               true);
    expectBool("real workspace module brief clock relationship evidence link",
               sawRealClockRelationshipEvidenceLink,
               true);
    expectBool("real workspace module brief clock relationship endpoint links",
               sawRealClockRelationshipEndpointLinks,
               true);
    expectBool("real workspace module brief reset relationship evidence",
               sawRealResetRelationshipEvidence,
               true);
    expectBool("real workspace module brief reset relationship evidence link",
               sawRealResetRelationshipEvidenceLink,
               true);
    expectBool("real workspace module brief reset relationship endpoint links",
               sawRealResetRelationshipEndpointLinks,
               true);

    ClockResetDomainService clockResetService(&index);
    ClockResetDomainQuery clockResetQuery;
    clockResetQuery.moduleName = QStringLiteral("rtl_top");
    clockResetQuery.fileName = topPath;
    const ClockResetDomainReport clockResetReport =
        clockResetService.buildClockResetDomainMap(clockResetQuery);
    bool sawRealClockEvidence = false;
    bool sawRealResetEvidence = false;
    bool sawRealClockEvidenceLink = false;
    bool sawRealResetEvidenceLink = false;
    bool sawRealClockEvidenceMetadata = false;
    bool sawRealResetEvidenceMetadata = false;
    bool sawRealClockDomainMemberMetadata = false;
    bool sawRealResetDomainMemberMetadata = false;
    bool sawRealUnmappedClock = false;
    bool sawRealUnmappedClockLink = false;
    bool sawRealUnmappedClockMetadata = false;
    for (const ClockResetDomainEntry& domain : clockResetReport.clockDomains) {
        if (domain.domainSignal.symbolName != QStringLiteral("clk_main"))
            continue;
        for (const ClockResetDomainMember& member : domain.modules) {
            sawRealClockDomainMemberMetadata =
                sawRealClockDomainMemberMetadata
                || (member.moduleDisplayName == QStringLiteral("rtl_top")
                    && member.relationshipTypeDisplayName == QStringLiteral("Clock")
                    && member.detailDisplayName == QStringLiteral("clocked")
                    && !member.sourceRoleDisplayName.isEmpty()
                    && !member.moduleCodeLink.fileName.isEmpty()
                    && member.moduleCodeLink.line > 0
                    && !member.moduleCodeLink.fileDisplayName.isEmpty()
                    && !member.moduleCodeLink.lineDisplayName.isEmpty());
        }
    }
    for (const ClockResetDomainEntry& domain : clockResetReport.resetDomains) {
        if (domain.domainSignal.symbolName != QStringLiteral("srst_main"))
            continue;
        for (const ClockResetDomainMember& member : domain.modules) {
            sawRealResetDomainMemberMetadata =
                sawRealResetDomainMemberMetadata
                || (member.moduleDisplayName == QStringLiteral("rtl_top")
                    && member.relationshipTypeDisplayName == QStringLiteral("Reset")
                    && member.detailDisplayName == QStringLiteral("reset")
                    && !member.sourceRoleDisplayName.isEmpty()
                    && !member.moduleCodeLink.fileName.isEmpty()
                    && member.moduleCodeLink.line > 0
                    && !member.moduleCodeLink.fileDisplayName.isEmpty()
                    && !member.moduleCodeLink.lineDisplayName.isEmpty());
        }
    }
    for (const ClockResetDomainEvidenceRow& row : clockResetReport.evidenceRows) {
        sawRealClockEvidence = sawRealClockEvidence
            || (row.sectionDisplayName == QStringLiteral("Clock")
                && row.signalDisplayName == QStringLiteral("clk_main")
                && row.moduleDisplayName == QStringLiteral("rtl_top"));
        sawRealClockEvidenceMetadata = sawRealClockEvidenceMetadata
            || (row.sectionDisplayName == QStringLiteral("Clock")
                && row.signalDisplayName == QStringLiteral("clk_main")
                && row.moduleDisplayName == QStringLiteral("rtl_top")
                && row.relationshipTypeDisplayName == QStringLiteral("Clock")
                && row.categoryDisplayName == QStringLiteral("mapped domain")
                && row.evidenceReasonDisplayName == QStringLiteral("relationship")
                && !row.sourceRoleDisplayName.isEmpty());
        sawRealClockEvidenceLink = sawRealClockEvidenceLink
            || (row.sectionDisplayName == QStringLiteral("Clock")
                && row.signalDisplayName == QStringLiteral("clk_main")
                && row.moduleDisplayName == QStringLiteral("rtl_top")
                && !row.signalCodeLink.fileName.isEmpty()
                && row.signalCodeLink.line > 0
                && !row.signalCodeLink.fileDisplayName.isEmpty()
                && !row.signalCodeLink.lineDisplayName.isEmpty()
                && !row.moduleCodeLink.fileName.isEmpty()
                && row.moduleCodeLink.line > 0
                && !row.moduleCodeLink.fileDisplayName.isEmpty()
                && !row.moduleCodeLink.lineDisplayName.isEmpty());
        sawRealResetEvidence = sawRealResetEvidence
            || (row.sectionDisplayName == QStringLiteral("Reset")
                && row.signalDisplayName == QStringLiteral("srst_main")
                && row.moduleDisplayName == QStringLiteral("rtl_top"));
        sawRealResetEvidenceMetadata = sawRealResetEvidenceMetadata
            || (row.sectionDisplayName == QStringLiteral("Reset")
                && row.signalDisplayName == QStringLiteral("srst_main")
                && row.moduleDisplayName == QStringLiteral("rtl_top")
                && row.relationshipTypeDisplayName == QStringLiteral("Reset")
                && row.categoryDisplayName == QStringLiteral("mapped domain")
                && row.evidenceReasonDisplayName == QStringLiteral("relationship")
                && !row.sourceRoleDisplayName.isEmpty());
        sawRealResetEvidenceLink = sawRealResetEvidenceLink
            || (row.sectionDisplayName == QStringLiteral("Reset")
                && row.signalDisplayName == QStringLiteral("srst_main")
                && row.moduleDisplayName == QStringLiteral("rtl_top")
                && !row.signalCodeLink.fileName.isEmpty()
                && row.signalCodeLink.line > 0
                && !row.signalCodeLink.fileDisplayName.isEmpty()
                && !row.signalCodeLink.lineDisplayName.isEmpty()
                && !row.moduleCodeLink.fileName.isEmpty()
                && row.moduleCodeLink.line > 0
                && !row.moduleCodeLink.fileDisplayName.isEmpty()
                && !row.moduleCodeLink.lineDisplayName.isEmpty());
    }
    for (const ClockResetDomainEvidenceRow& row : clockResetReport.unmappedRows) {
        sawRealUnmappedClock = sawRealUnmappedClock
            || (row.sectionDisplayName == QStringLiteral("Unmapped Clock")
                && row.signalDisplayName.startsWith(QStringLiteral("clk_cpld"))
                && row.moduleDisplayName == QStringLiteral("rtl_top")
                && row.detailDisplayName.contains(
                    QStringLiteral("no clock domain relationship")));
        sawRealUnmappedClockMetadata = sawRealUnmappedClockMetadata
            || (row.sectionDisplayName == QStringLiteral("Unmapped Clock")
                && row.signalDisplayName.startsWith(QStringLiteral("clk_cpld"))
                && row.relationshipTypeDisplayName == QStringLiteral("Clock")
                && row.categoryDisplayName == QStringLiteral("unmapped timing")
                && row.evidenceReasonDisplayName
                    == QStringLiteral("missing relationship")
                && !row.sourceRoleDisplayName.isEmpty());
        sawRealUnmappedClockLink = sawRealUnmappedClockLink
            || (row.sectionDisplayName == QStringLiteral("Unmapped Clock")
                && row.signalDisplayName.startsWith(QStringLiteral("clk_cpld"))
                && !row.signalCodeLink.fileName.isEmpty()
                && row.signalCodeLink.line > 0
                && !row.signalCodeLink.fileDisplayName.isEmpty()
                && !row.signalCodeLink.lineDisplayName.isEmpty());
    }
    expectBool("real workspace clock reset found",
               clockResetReport.found,
               true);
    expectBool("real workspace clock evidence row",
               sawRealClockEvidence,
               true);
    expectBool("real workspace clock evidence row metadata",
               sawRealClockEvidenceMetadata,
               true);
    expectBool("real workspace clock evidence row code link",
               sawRealClockEvidenceLink,
               true);
    expectBool("real workspace reset evidence row",
               sawRealResetEvidence,
               true);
    expectBool("real workspace reset evidence row metadata",
               sawRealResetEvidenceMetadata,
               true);
    expectBool("real workspace reset evidence row code link",
               sawRealResetEvidenceLink,
               true);
    expectBool("real workspace clock domain member metadata",
               sawRealClockDomainMemberMetadata,
               true);
    expectBool("real workspace reset domain member metadata",
               sawRealResetDomainMemberMetadata,
               true);
    expectBool("real workspace clock reset unmapped clock",
               sawRealUnmappedClock,
               true);
    expectBool("real workspace clock reset unmapped clock metadata",
               sawRealUnmappedClockMetadata,
               true);
    expectBool("real workspace clock reset unmapped clock code link",
               sawRealUnmappedClockLink,
               true);

    SignalJourneyService signalJourneyService(&index);
    SignalJourneyQuery interfaceJourneyQuery;
    interfaceJourneyQuery.signalStableKey =
        symbolStableKeyForSymbol(symbolByName(QStringLiteral("LR_GENR_IF"),
                                              sym_list::sym_inst,
                                              QStringLiteral("rtl_top")));
    interfaceJourneyQuery.fileName = topPath;
    interfaceJourneyQuery.moduleName = QStringLiteral("rtl_top");
    const SignalJourneyReport interfaceJourney =
        signalJourneyService.buildSignalJourney(interfaceJourneyQuery);
    bool sawRealInterfaceModportJourney = false;
    bool sawRealInterfaceModportJourneyLink = false;
    bool sawRealInterfaceModportJourneyMetadata = false;
    bool sawRealInterfaceModportJourneyEndpointMetadata = false;
    for (const SignalJourneyItem& item : interfaceJourney.interfaceConnections) {
        sawRealInterfaceModportJourney = sawRealInterfaceModportJourney
            || (item.peerSymbolDisplayName == QStringLiteral("si")
                && item.detailDisplayName == QStringLiteral("interface outgoing References"));
        sawRealInterfaceModportJourneyMetadata =
            sawRealInterfaceModportJourneyMetadata
            || (item.peerSymbolDisplayName == QStringLiteral("si")
                && item.connectionKindDisplayName
                    == QStringLiteral("interface modport")
                && item.interfaceBaseDisplayName == QStringLiteral("lr_genr_if")
                && !item.peerTypeDisplayName.isEmpty()
                && !item.peerSourceRoleDisplayName.isEmpty());
        sawRealInterfaceModportJourneyEndpointMetadata =
            sawRealInterfaceModportJourneyEndpointMetadata
            || (item.peerSymbolDisplayName == QStringLiteral("si")
                && !item.fromTypeDisplayName.isEmpty()
                && !item.toTypeDisplayName.isEmpty()
                && !item.fromSourceRoleDisplayName.isEmpty()
                && !item.toSourceRoleDisplayName.isEmpty());
        sawRealInterfaceModportJourneyLink = sawRealInterfaceModportJourneyLink
            || (item.peerSymbolDisplayName == QStringLiteral("si")
                && !item.peerCodeLink.fileName.isEmpty()
                && item.peerCodeLink.line > 0
                && !item.peerCodeLink.fileDisplayName.isEmpty()
                && !item.peerCodeLink.lineDisplayName.isEmpty());
    }
    expectBool("real workspace signal journey interface found",
               interfaceJourney.found,
               true);
    expectBool("real workspace signal journey declaration code link",
               !interfaceJourney.declarationCodeLink.fileName.isEmpty()
                   && interfaceJourney.declarationCodeLink.line > 0
                   && !interfaceJourney.declarationCodeLink.fileDisplayName.isEmpty()
                   && !interfaceJourney.declarationCodeLink.lineDisplayName.isEmpty(),
               true);
    expectBool("real workspace signal journey declaration source role",
               !interfaceJourney.declarationSourceRoleDisplayName.isEmpty(),
               true);
    expectBool("real workspace signal journey interface modport",
               sawRealInterfaceModportJourney,
               true);
    expectBool("real workspace signal journey interface modport metadata",
               sawRealInterfaceModportJourneyMetadata,
               true);
    expectBool("real workspace signal journey interface modport endpoint metadata",
               sawRealInterfaceModportJourneyEndpointMetadata,
               true);
    expectBool("real workspace signal journey interface modport code link",
               sawRealInterfaceModportJourneyLink,
               true);

    SignalJourneyQuery clockJourneyQuery;
    clockJourneyQuery.signalName = QStringLiteral("clk_main");
    clockJourneyQuery.fileName = topPath;
    clockJourneyQuery.moduleName = QStringLiteral("rtl_top");
    const SignalJourneyReport clockJourney =
        signalJourneyService.buildSignalJourney(clockJourneyQuery);
    bool sawRealClockJourney = false;
    bool sawRealClockJourneyLink = false;
    for (const SignalJourneyItem& item : clockJourney.timingConnections) {
        sawRealClockJourney = sawRealClockJourney
            || (item.peerSymbolDisplayName == QStringLiteral("rtl_top")
                && item.relationshipTypeDisplayName == QStringLiteral("Clocks")
                && item.detailDisplayName == QStringLiteral("timing outgoing Clocks"));
        sawRealClockJourneyLink = sawRealClockJourneyLink
            || (item.peerSymbolDisplayName == QStringLiteral("rtl_top")
                && !item.peerCodeLink.fileName.isEmpty()
                && item.peerCodeLink.line > 0
                && !item.peerCodeLink.fileDisplayName.isEmpty()
                && !item.peerCodeLink.lineDisplayName.isEmpty());
    }
    expectBool("real workspace signal journey clock found",
               clockJourney.found,
               true);
    expectBool("real workspace signal journey clock source role",
               !clockJourney.declarationSourceRoleDisplayName.isEmpty(),
               true);
    expectBool("real workspace signal journey clock timing",
               sawRealClockJourney,
               true);
    expectBool("real workspace signal journey clock timing code link",
               sawRealClockJourneyLink,
               true);

    SignalJourneyQuery resetJourneyQuery;
    resetJourneyQuery.signalName = QStringLiteral("srst_main");
    resetJourneyQuery.fileName = topPath;
    resetJourneyQuery.moduleName = QStringLiteral("rtl_top");
    const SignalJourneyReport resetJourney =
        signalJourneyService.buildSignalJourney(resetJourneyQuery);
    bool sawRealResetJourney = false;
    bool sawRealResetJourneyLink = false;
    for (const SignalJourneyItem& item : resetJourney.timingConnections) {
        sawRealResetJourney = sawRealResetJourney
            || (item.peerSymbolDisplayName == QStringLiteral("rtl_top")
                && item.relationshipTypeDisplayName == QStringLiteral("Resets")
                && item.detailDisplayName == QStringLiteral("timing outgoing Resets"));
        sawRealResetJourneyLink = sawRealResetJourneyLink
            || (item.peerSymbolDisplayName == QStringLiteral("rtl_top")
                && !item.peerCodeLink.fileName.isEmpty()
                && item.peerCodeLink.line > 0
                && !item.peerCodeLink.fileDisplayName.isEmpty()
                && !item.peerCodeLink.lineDisplayName.isEmpty());
    }
    expectBool("real workspace signal journey reset found",
               resetJourney.found,
               true);
    expectBool("real workspace signal journey reset source role",
               !resetJourney.declarationSourceRoleDisplayName.isEmpty(),
               true);
    expectBool("real workspace signal journey reset timing",
               sawRealResetJourney,
               true);
    expectBool("real workspace signal journey reset timing code link",
               sawRealResetJourneyLink,
               true);

    FsmGraphService realFsmService(&index);
    FsmGraphQuery realFsmQuery;
    realFsmQuery.moduleName = QStringLiteral("chl_ctrl");
    realFsmQuery.fileName = chlCtrlPath;
    const FsmGraphReport realFsmReport =
        realFsmService.buildFsmGraph(realFsmQuery);
    bool sawRealPhyPassFsm = false;
    bool sawRealPhyPassFsmStateLink = false;
    bool sawRealPhyPassFsmStateMetadata = false;
    bool sawRealPhyPassFsmRegisterMetadata = false;
    bool sawRealPhyPassFsmNextStateLink = false;
    bool sawRealPhyPassFsmTransition = false;
    bool sawRealPhyPassFsmTransitionSourceRole = false;
    bool sawRealPhyPassFsmTransitionStateLink = false;
    bool sawRealPhyPassTernaryTransition = false;
    bool sawRealPhyPassTernaryElseTransition = false;
    for (const FsmGraph& graph : realFsmReport.graphs) {
        if (graph.stateRegister.symbolName
            != QStringLiteral("phy_pass_thrg_cfg_cs")) {
            continue;
        }
        sawRealPhyPassFsm = graph.nextStateSignal.symbolName
            == QStringLiteral("phy_pass_thrg_cfg_ns");
        sawRealPhyPassFsmRegisterMetadata =
            graph.stateRegisterTypeDisplayName == QStringLiteral("enum")
            && !graph.stateRegisterSourceRoleDisplayName.isEmpty()
            && graph.nextStateSignalDisplayName
                == QStringLiteral("phy_pass_thrg_cfg_ns")
            && graph.nextStateSignalTypeDisplayName == QStringLiteral("enum")
            && !graph.nextStateSignalSourceRoleDisplayName.isEmpty();
        sawRealPhyPassFsmNextStateLink =
            !graph.nextStateSignalCodeLink.fileName.isEmpty()
            && graph.nextStateSignalCodeLink.line > 0
            && !graph.nextStateSignalCodeLink.fileDisplayName.isEmpty()
            && !graph.nextStateSignalCodeLink.lineDisplayName.isEmpty();
        sawRealPhyPassFsmStateLink = !graph.stateRows.isEmpty()
            && !graph.stateRows.first().codeLink.fileName.isEmpty()
            && graph.stateRows.first().codeLink.line > 0
            && !graph.stateRows.first().codeLink.fileDisplayName.isEmpty()
            && !graph.stateRows.first().codeLink.lineDisplayName.isEmpty();
        sawRealPhyPassFsmStateMetadata = !graph.stateRows.isEmpty()
            && !graph.stateRows.first().typeDisplayName.isEmpty()
            && !graph.stateRows.first().sourceRoleDisplayName.isEmpty()
            && !graph.stateRows.first().moduleDisplayName.isEmpty();
        sawRealPhyPassFsmTransition = !graph.transitionRows.isEmpty()
            && !graph.transitionRows.first().codeLink.fileName.isEmpty()
            && graph.transitionRows.first().codeLink.line > 0
            && !graph.transitionRows.first().sourceLineDisplayName.isEmpty();
        for (const FsmTransitionRow& row : graph.transitionRows) {
            sawRealPhyPassFsmTransitionSourceRole =
                sawRealPhyPassFsmTransitionSourceRole
                || (!row.sourceRoleDisplayName.isEmpty()
                    && !row.sourceLineDisplayName.isEmpty()
                    && !row.codeLink.fileName.isEmpty()
                    && row.codeLink.line > 0);
            sawRealPhyPassFsmTransitionStateLink =
                sawRealPhyPassFsmTransitionStateLink
                || (!row.fromStateDisplayName.isEmpty()
                    && !row.toStateDisplayName.isEmpty()
                    && !row.fromStateCodeLink.fileName.isEmpty()
                    && row.fromStateCodeLink.line > 0
                    && !row.fromStateCodeLink.fileDisplayName.isEmpty()
                    && !row.fromStateCodeLink.lineDisplayName.isEmpty()
                    && !row.toStateCodeLink.fileName.isEmpty()
                    && row.toStateCodeLink.line > 0
                    && !row.toStateCodeLink.fileDisplayName.isEmpty()
                    && !row.toStateCodeLink.lineDisplayName.isEmpty());
            sawRealPhyPassTernaryTransition = sawRealPhyPassTernaryTransition
                || (row.fromStateDisplayName == QStringLiteral("S_IDLE")
                    && row.toStateDisplayName == QStringLiteral("S_PRE_DEASSERT_DONE")
                    && row.conditionDisplayName.contains(QStringLiteral("E_CFG_TYPE_PRE_DEASSERT"))
                    && row.codeLink.line > 0);
            sawRealPhyPassTernaryElseTransition =
                sawRealPhyPassTernaryElseTransition
                || (row.fromStateDisplayName == QStringLiteral("S_IDLE")
                    && row.toStateDisplayName == QStringLiteral("S_PROT_EXT_SWITCH_DIS")
                    && row.conditionDisplayName.contains(QStringLiteral("else"))
                    && row.conditionDisplayName.contains(QStringLiteral("E_CFG_TYPE_PRE_DEASSERT"))
                    && row.codeLink.line > 0);
        }
    }
    expectBool("real workspace fsm graph found",
               realFsmReport.found,
               true);
    expectBool("real workspace fsm graph phy pass current next pair",
               sawRealPhyPassFsm,
               true);
    expectBool("real workspace fsm graph state code link",
               sawRealPhyPassFsmStateLink,
               true);
    expectBool("real workspace fsm graph state metadata",
               sawRealPhyPassFsmStateMetadata,
               true);
    expectBool("real workspace fsm graph register metadata",
               sawRealPhyPassFsmRegisterMetadata,
               true);
    expectBool("real workspace fsm graph next state code link",
               sawRealPhyPassFsmNextStateLink,
               true);
    expectBool("real workspace fsm graph transition evidence",
               sawRealPhyPassFsmTransition,
               true);
    expectBool("real workspace fsm graph transition source role",
               sawRealPhyPassFsmTransitionSourceRole,
               true);
    expectBool("real workspace fsm graph transition state links",
               sawRealPhyPassFsmTransitionStateLink,
               true);
    expectBool("real workspace fsm graph ternary transition evidence",
               sawRealPhyPassTernaryTransition,
               true);
    expectBool("real workspace fsm graph ternary else evidence",
               sawRealPhyPassTernaryElseTransition,
               true);

    QList<sym_list::SymbolInfo> realBeforeDiffSymbols;
    realBeforeDiffSymbols.append(rtlTopSymbol);
    realBeforeDiffSymbols.append(packageSymbol);
    realBeforeDiffSymbols.append(interfaceInstSymbol);
    QList<sym_list::SymbolInfo> realAfterDiffSymbols = realBeforeDiffSymbols;
    realAfterDiffSymbols.append(interfaceSymbol);
    realAfterDiffSymbols.append(packageTypedefSymbol);
    QList<SemanticRelationship> realAfterDiffRelationships;
    if (rtlTopSymbol.symbolId >= 0 && interfaceInstSymbol.symbolId >= 0) {
        SemanticRelationship interfaceReference;
        interfaceReference.fromId = rtlTopSymbol.symbolId;
        interfaceReference.toId = interfaceInstSymbol.symbolId;
        interfaceReference.type = SymbolRelationshipEngine::REFERENCES;
        realAfterDiffRelationships.append(interfaceReference);
    }
    SemanticDiagnostic realAfterDiffDiagnostic;
    realAfterDiffDiagnostic.fileName = topPath;
    realAfterDiffDiagnostic.line = 63;
    realAfterDiffDiagnostic.column = 13;
    realAfterDiffDiagnostic.message = QStringLiteral("real diff diagnostic");
    realAfterDiffDiagnostic.severity = SemanticDiagnostic::Warning;
    auto realBeforeDiffSnapshot = std::make_shared<SemanticIndexSnapshot>(
        realBeforeDiffSymbols,
        QList<SemanticRelationship>(),
        QList<SemanticDiagnostic>());
    auto realAfterDiffSnapshot = std::make_shared<SemanticIndexSnapshot>(
        realAfterDiffSymbols,
        realAfterDiffRelationships,
        QList<SemanticDiagnostic>{realAfterDiffDiagnostic});
    SemanticDiffQuery realDiffQuery;
    realDiffQuery.beforeSnapshot = realBeforeDiffSnapshot;
    realDiffQuery.afterSnapshot = realAfterDiffSnapshot;
    realDiffQuery.moduleName = QStringLiteral("rtl_top");
    const SemanticDiffReport realDiffReport =
        SemanticDiffService().buildSemanticDiff(realDiffQuery);
    bool sawRealDiffInterface = false;
    bool sawRealDiffType = false;
    bool sawRealDiffInterfaceLink = false;
    bool sawRealDiffTypeLink = false;
    bool sawRealDiffInterfaceMetadata = false;
    bool sawRealDiffTypeMetadata = false;
    bool sawRealDiffInterfaceAfterMetadata = false;
    bool sawRealDiffTypeAfterMetadata = false;
    bool sawRealDiffRelationshipEndpointLinks = false;
    bool sawRealDiffDiagnosticMetadata = false;
    for (const SemanticDiffSymbolChange& change : realDiffReport.symbolChanges) {
        sawRealDiffInterface = sawRealDiffInterface
            || (change.category == SemanticDiffSymbolCategory::Interface
                && change.displaySymbol.symbolName == QStringLiteral("lr_genr_if")
                && change.categoryGroupDisplayName == QStringLiteral("Interfaces"));
        sawRealDiffInterfaceMetadata = sawRealDiffInterfaceMetadata
            || (change.displaySymbol.symbolName == QStringLiteral("lr_genr_if")
                && change.symbolTypeDisplayName == QStringLiteral("interface")
                && change.scopeDisplayName == QStringLiteral("global")
                && change.sourceRoleDisplayName == QStringLiteral("design source")
                && change.detailDisplayName.contains(QStringLiteral("global")));
        sawRealDiffInterfaceAfterMetadata = sawRealDiffInterfaceAfterMetadata
            || (change.displaySymbol.symbolName == QStringLiteral("lr_genr_if")
                && change.beforeSymbolTypeDisplayName.isEmpty()
                && change.afterSymbolTypeDisplayName == QStringLiteral("interface")
                && change.afterScopeDisplayName == QStringLiteral("global")
                && change.afterSourceRoleDisplayName
                    == QStringLiteral("design source")
                && !change.afterCodeLink.fileName.isEmpty()
                && change.afterCodeLink.line > 0
                && !change.afterCodeLink.fileDisplayName.isEmpty()
                && !change.afterCodeLink.lineDisplayName.isEmpty());
        sawRealDiffInterfaceLink = sawRealDiffInterfaceLink
            || (change.displaySymbol.symbolName == QStringLiteral("lr_genr_if")
                && !change.codeLink.fileName.isEmpty()
                && change.codeLink.line > 0
                && !change.codeLink.fileDisplayName.isEmpty()
                && !change.codeLink.lineDisplayName.isEmpty());
        sawRealDiffType = sawRealDiffType
            || (change.category == SemanticDiffSymbolCategory::Type
                && change.displaySymbol.symbolName == QStringLiteral("cpld_sw_sp")
                && change.categoryGroupDisplayName == QStringLiteral("Types"));
        sawRealDiffTypeMetadata = sawRealDiffTypeMetadata
            || (change.displaySymbol.symbolName == QStringLiteral("cpld_sw_sp")
                && change.symbolTypeDisplayName == QStringLiteral("typedef")
                && change.scopeDisplayName == QStringLiteral("scope gl_pkg")
                && change.sourceRoleDisplayName == QStringLiteral("design source")
                && change.detailDisplayName.contains(QStringLiteral("scope gl_pkg")));
        sawRealDiffTypeAfterMetadata = sawRealDiffTypeAfterMetadata
            || (change.displaySymbol.symbolName == QStringLiteral("cpld_sw_sp")
                && change.beforeSymbolTypeDisplayName.isEmpty()
                && change.afterSymbolTypeDisplayName == QStringLiteral("typedef")
                && change.afterScopeDisplayName == QStringLiteral("scope gl_pkg")
                && change.afterSourceRoleDisplayName
                    == QStringLiteral("design source")
                && !change.afterCodeLink.fileName.isEmpty()
                && change.afterCodeLink.line > 0
                && !change.afterCodeLink.fileDisplayName.isEmpty()
                && !change.afterCodeLink.lineDisplayName.isEmpty());
        sawRealDiffTypeLink = sawRealDiffTypeLink
            || (change.displaySymbol.symbolName == QStringLiteral("cpld_sw_sp")
                && !change.codeLink.fileName.isEmpty()
                && change.codeLink.line > 0
                && !change.codeLink.fileDisplayName.isEmpty()
                && !change.codeLink.lineDisplayName.isEmpty());
    }
    for (const SemanticDiffRelationshipChange& change
         : realDiffReport.relationshipChanges) {
        sawRealDiffRelationshipEndpointLinks = sawRealDiffRelationshipEndpointLinks
            || (change.relationshipTypeDisplayName == QStringLiteral("References")
                && change.fromSymbolDisplayName == QStringLiteral("rtl_top")
                && change.toSymbolDisplayName == QStringLiteral("LR_GENR_IF")
                && !change.sourceRoleDisplayName.isEmpty()
                && !change.fromCodeLink.fileName.isEmpty()
                && change.fromCodeLink.line > 0
                && !change.fromCodeLink.fileDisplayName.isEmpty()
                && !change.fromCodeLink.lineDisplayName.isEmpty()
                && !change.toCodeLink.fileName.isEmpty()
                && change.toCodeLink.line > 0
                && !change.toCodeLink.fileDisplayName.isEmpty()
                && !change.toCodeLink.lineDisplayName.isEmpty());
    }
    for (const SemanticDiffDiagnosticChange& change
         : realDiffReport.diagnosticChanges) {
        sawRealDiffDiagnosticMetadata = sawRealDiffDiagnosticMetadata
            || (change.kindDisplayName == QStringLiteral("Added")
                && change.severityDisplayName == QStringLiteral("Warning")
                && !change.sourceRoleDisplayName.isEmpty()
                && change.detailDisplayName.contains(QStringLiteral("Warning"))
                && change.displayDiagnostic.message
                    == QStringLiteral("real diff diagnostic")
                && !change.codeLink.fileName.isEmpty()
                && change.codeLink.line == 63
                && !change.codeLink.fileDisplayName.isEmpty()
                && !change.codeLink.lineDisplayName.isEmpty());
    }
    expectBool("real workspace semantic diff found",
               realDiffReport.found,
               true);
    expectBool("real workspace semantic diff interface category",
               sawRealDiffInterface,
               true);
    expectBool("real workspace semantic diff interface link",
               sawRealDiffInterfaceLink,
               true);
    expectBool("real workspace semantic diff interface metadata",
               sawRealDiffInterfaceMetadata,
               true);
    expectBool("real workspace semantic diff interface after metadata",
               sawRealDiffInterfaceAfterMetadata,
               true);
    expectBool("real workspace semantic diff type category",
               sawRealDiffType,
               true);
    expectBool("real workspace semantic diff type link",
               sawRealDiffTypeLink,
               true);
    expectBool("real workspace semantic diff type metadata",
               sawRealDiffTypeMetadata,
               true);
    expectBool("real workspace semantic diff type after metadata",
               sawRealDiffTypeAfterMetadata,
               true);
    expectBool("real workspace semantic diff relationship endpoint links",
               sawRealDiffRelationshipEndpointLinks,
               true);
    expectBool("real workspace semantic diff diagnostic metadata",
               sawRealDiffDiagnosticMetadata,
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
    runScopeBandServiceFixture();
    runSignalJourneyServiceFixture();
    runClockResetDomainServiceFixture();
    runFsmGraphServiceFixture();
    runSemanticDiffServiceFixture();
    runRealWorkspaceIncludeFixture();
    runPostWorkspaceDiagnosticFixture();

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
