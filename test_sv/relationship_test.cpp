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
#include "editorsourcenavigationquery.h"
#include "fsmgraphservice.h"
#include "hierarchyservice.h"
#include "insightvisualstyle.h"
#include "moduleblockdiagramservice.h"
#include "modulebriefservice.h"
#include "navigationservice.h"
#include "relationshipanalysisworker.h"
#include "relationshipresultpublisher.h"
#include "referenceservice.h"
#include "relationshipservice.h"
#include "searchservice.h"
#include "semantic_fixture_records.h"
#include "semanticdiffservice.h"
#include "semanticdecorationservice.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphservice.h"
#include "signaljourneyservice.h"
#include "signalusagehotspotservice.h"
#include "statetransitiongraphservice.h"
#include "symbolrelationshipengine.h"
#include "semanticindexsnapshot.h"
#include "symbolhoverservice.h"
#include "symboltaxonomy.h"
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
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <cstdio>
#include <limits>
#include <memory>

static int g_checks = 0;
static int g_fails = 0;

static std::shared_ptr<SemanticIndexSnapshot> sharedSnapshotFromRecords(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships = {},
    const QList<SemanticDiagnostic>& diagnostics = {},
    const QHash<QString, QString>& fileContents = {})
{
    return std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records,
                                                 relationships,
                                                 diagnostics,
                                                 fileContents));
}

static SemanticIndexSnapshot snapshotFromSemanticIndex(
    SemanticIndex& index,
    const QList<SemanticDiagnostic>& diagnostics = {},
    SymbolRelationshipEngine* relationshipEngine = nullptr)
{
    const std::shared_ptr<const SemanticIndexSnapshot> captured =
        index.captureSnapshotPreservingDiagnostics();
    QList<SemanticRelationship> relationships;
    if (relationshipEngine) {
        static const QList<SymbolRelationshipEngine::RelationType> relationshipTypes = {
            SymbolRelationshipEngine::CONTAINS,
            SymbolRelationshipEngine::REFERENCES,
            SymbolRelationshipEngine::INSTANTIATES,
            SymbolRelationshipEngine::CALLS,
            SymbolRelationshipEngine::INHERITS,
            SymbolRelationshipEngine::IMPLEMENTS,
            SymbolRelationshipEngine::ASSIGNS_TO,
            SymbolRelationshipEngine::READS_FROM,
            SymbolRelationshipEngine::CLOCKS,
            SymbolRelationshipEngine::RESETS,
            SymbolRelationshipEngine::GENERATES,
            SymbolRelationshipEngine::CONSTRAINS,
        };
        QSet<QString> seen;
        for (const SemanticSymbolRecord& record : captured->getSymbolRecords()) {
            const int symbolHandle = record.localHandle;
            if (symbolHandle < 0)
                continue;
            for (SymbolRelationshipEngine::RelationType type : relationshipTypes) {
                const QList<int> related =
                    relationshipEngine->getRelatedSymbols(symbolHandle, type, true);
                for (int relatedHandle : related) {
                    SemanticRelationship relationship;
                    relationship.fromId = symbolHandle;
                    relationship.toId = relatedHandle;
                    relationship.type = type;
                    const SymbolRelationshipEngine::RelationshipEdgeMetadata metadata =
                        relationshipEngine->getRelationshipMetadata(
                            relationship.fromId,
                            relationship.toId,
                            relationship.type);
                    if (metadata.found) {
                        relationship.provenance =
                            RelationshipProvenance::Inferred;
                        relationship.confidence = metadata.confidence;
                        relationship.evidenceText = metadata.context;
                    }
                    const QString key = QStringLiteral("%1:%2:%3")
                                            .arg(relationship.fromId)
                                            .arg(relationship.toId)
                                            .arg(static_cast<int>(relationship.type));
                    if (seen.contains(key))
                        continue;
                    seen.insert(key);
                    relationships.append(relationship);
                }
            }
        }
    }
    return SemanticIndexSnapshot::fromSymbolRecords(captured->getSymbolRecords(),
                                                    relationships,
                                                    diagnostics,
                                                    captured->fileContents());
}

static std::shared_ptr<SemanticIndexSnapshot> sharedSnapshotFromSymbols(
    const SemanticIndexSnapshot& snapshot)
{
    return std::make_shared<SemanticIndexSnapshot>(snapshot);
}

static void expectBool(const char* what, bool got, bool want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-46s got=%s want=%s\n", ok ? "PASS" : "FAIL", what,
           got ? "true" : "false", want ? "true" : "false");
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

static QTreeWidgetItem* findTreeItem(
    QTreeWidgetItem* item,
    const QString& column0,
    const QString& column1 = QString(),
    const QString& column2 = QString())
{
    if (!item)
        return nullptr;
    const bool matches = item->text(0) == column0
        && (column1.isNull() || item->text(1) == column1)
        && (column2.isNull() || item->text(2) == column2);
    if (matches)
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem* found =
                findTreeItem(item->child(i), column0, column1, column2)) {
            return found;
        }
    }
    return nullptr;
}

static QTreeWidgetItem* findTreeItem(
    QTreeWidget* tree,
    const QString& column0,
    const QString& column1 = QString(),
    const QString& column2 = QString())
{
    if (!tree)
        return nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (QTreeWidgetItem* found =
                findTreeItem(tree->topLevelItem(i),
                             column0,
                             column1,
                             column2)) {
            return found;
        }
    }
    return nullptr;
}

static QString loadTextFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

static bool writeTextFile(const QString& path, const QString& text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    return f.write(text.toUtf8()) >= 0;
}

static QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

static bool relationshipInfoEmpty(const RelationshipExtractionInfo& info)
{
    return info.moduleInstantiations.isEmpty()
        && info.subroutineCalls.isEmpty()
        && info.assignments.isEmpty()
        && info.conditionReferences.isEmpty()
        && info.timingSignals.isEmpty();
}

static void runInlineRelationshipRegression(SlangManager& slang,
                                            SymbolRelationshipEngine& engine)
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

    QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(path, content);
    for (SemanticSymbolRecord& record : records)
        record.location.fileName = path;
    int nextLocalHandle = 1;
    for (SemanticSymbolRecord& record : records) {
        if (record.localHandle <= 0)
            record.localHandle = nextLocalHandle;
        if (nextLocalHandle <= record.localHandle)
            nextLocalHandle = record.localHandle + 1;
    }

    const auto recordByNameAndKind = [&](const QString& name,
                                         SymbolTaxonomy::CollectorKind kind) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name && record.collectorKind == kind)
                return record;
        }
        return SemanticSymbolRecord{};
    };
    const auto recordByNameKindAndOwner =
        [&](const QString& name,
            SymbolTaxonomy::CollectorKind kind,
            const QString& ownerName) {
            for (const SemanticSymbolRecord& record : records) {
                if (record.name == name
                    && record.collectorKind == kind
                    && record.owner.name == ownerName) {
                    return record;
                }
            }
            return SemanticSymbolRecord{};
        };

    const int alphaId =
        recordByNameAndKind(QStringLiteral("alpha"),
                            SymbolTaxonomy::CollectorKind::Module).localHandle;
    const int betaId =
        recordByNameAndKind(QStringLiteral("beta"),
                            SymbolTaxonomy::CollectorKind::Module).localHandle;
    const int leafId =
        recordByNameAndKind(QStringLiteral("leaf"),
                            SymbolTaxonomy::CollectorKind::Module).localHandle;
    const int doBetaId =
        recordByNameAndKind(QStringLiteral("do_beta"),
                            SymbolTaxonomy::CollectorKind::Task).localHandle;
    const int condId =
        recordByNameKindAndOwner(QStringLiteral("cond"),
                                 SymbolTaxonomy::CollectorKind::PortInput,
                                 QStringLiteral("beta")).localHandle;
    const int srcId =
        recordByNameKindAndOwner(QStringLiteral("src"),
                                 SymbolTaxonomy::CollectorKind::PortInput,
                                 QStringLiteral("beta")).localHandle;
    const int dstId =
        recordByNameKindAndOwner(QStringLiteral("dst"),
                                 SymbolTaxonomy::CollectorKind::PortOutput,
                                 QStringLiteral("beta")).localHandle;
    const int betaClkId =
        recordByNameKindAndOwner(QStringLiteral("clk"),
                                 SymbolTaxonomy::CollectorKind::PortInput,
                                 QStringLiteral("beta")).localHandle;
    const int rstId =
        recordByNameKindAndOwner(QStringLiteral("rst_n"),
                                 SymbolTaxonomy::CollectorKind::PortInput,
                                 QStringLiteral("beta")).localHandle;

    expectBool("symbols include alpha", alphaId > 0, true);
    expectBool("symbols include beta", betaId > 0, true);
    expectBool("symbols include leaf", leafId > 0, true);
    expectBool("symbols include do_beta", doBetaId > 0, true);
    expectBool("symbols include cond", condId > 0, true);
    expectBool("symbols include src", srcId > 0, true);
    expectBool("symbols include dst", dstId > 0, true);
    expectBool("symbols include beta clk", betaClkId > 0, true);
    expectBool("symbols include beta rst_n", rstId > 0, true);

    SmartRelationshipBuilder builder(
        &engine,
        &slang,
        [&records, &path](const QString& fileName) {
            if (normalizedPath(fileName) == normalizedPath(path))
                return records;
            return QList<SemanticSymbolRecord>{};
        });
    QVector<RelationshipToAdd> rels =
        builder.computeRelationships(path,
                                     content,
                                     records,
                                     nullptr);

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
                                            SymbolRelationshipEngine& engine)
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
    const QList<SemanticSymbolRecord> workspaceRecords =
        slang.extractWorkspaceSymbolRecords(paths);
    expectBool("workspace symbol records extracted", !workspaceRecords.isEmpty(), true);

    QHash<QString, QList<SemanticSymbolRecord>> recordsByFile;
    for (SemanticSymbolRecord record : workspaceRecords) {
        const QString recordPath = normalizedPath(record.location.fileName);
        QString matchedPath;
        for (const QString& path : paths) {
            if (recordPath == path
                || QFileInfo(recordPath).fileName() == QFileInfo(path).fileName()) {
                matchedPath = path;
                break;
            }
        }
        if (matchedPath.isEmpty())
            continue;
        record.location.fileName = matchedPath;
        recordsByFile[matchedPath].append(record);
    }

    for (const QString& path : paths) {
        if (!recordsByFile.value(path).isEmpty())
            continue;

        QList<SemanticSymbolRecord> fileRecords =
            slang.extractSymbolRecords(path, contents.value(path));
        for (SemanticSymbolRecord& record : fileRecords)
            record.location.fileName = path;
        recordsByFile[path] = fileRecords;
    }

    int nextLocalHandle = 1;
    for (const QString& path : paths) {
        QList<SemanticSymbolRecord>& fileRecords = recordsByFile[path];
        for (SemanticSymbolRecord& record : fileRecords)
            record.localHandle = nextLocalHandle++;
    }

    SemanticIndex index;
    for (const QString& path : paths) {
        index.updateSymbolRecordsForFile(
            path,
            recordsByFile.value(path),
            contents.value(path));
    }

    const QList<SemanticSymbolRecord> allRecords = index.getSymbolRecords();
    const QList<SemanticSymbolRecord> topRecords =
        index.getSymbolRecords(topPath);
    const auto recordInFile = [&](const QString& name,
                                  SymbolTaxonomy::CollectorKind kind,
                                  const QString& fileName) {
        for (const SemanticSymbolRecord& record : allRecords) {
            if (record.name == name
                && record.collectorKind == kind
                && normalizedPath(record.location.fileName)
                    == normalizedPath(fileName)) {
                return record;
            }
        }
        return SemanticSymbolRecord{};
    };
    const auto recordInScope = [&](const QList<SemanticSymbolRecord>& records,
                                   const QString& name,
                                   SymbolTaxonomy::CollectorKind kind,
                                   const QString& ownerName) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name
                && record.collectorKind == kind
                && record.owner.name == ownerName) {
                return record;
            }
        }
        return SemanticSymbolRecord{};
    };

    const SemanticSymbolRecord packageRecord =
        recordInFile(QStringLiteral("rel_pkg"),
                     SymbolTaxonomy::CollectorKind::Package,
                     pkgPath);
    const SemanticSymbolRecord stateTypeRecord =
        recordInFile(QStringLiteral("state_t"),
                     SymbolTaxonomy::CollectorKind::Typedef,
                     pkgPath);
    const SemanticSymbolRecord stateVarRecord =
        recordInFile(QStringLiteral("state"),
                     SymbolTaxonomy::CollectorKind::EnumVariable,
                     stagePath);
    const SemanticSymbolRecord topRecord =
        recordInFile(QStringLiteral("rel_top"),
                     SymbolTaxonomy::CollectorKind::Module,
                     topPath);
    const SemanticSymbolRecord stageRecord =
        recordInFile(QStringLiteral("rel_stage"),
                     SymbolTaxonomy::CollectorKind::Module,
                     stagePath);
    const SemanticSymbolRecord captureRecord =
        recordInFile(QStringLiteral("capture_sample"),
                     SymbolTaxonomy::CollectorKind::Task,
                     topPath);
    const SemanticSymbolRecord reqValidRecord =
        recordInScope(topRecords,
                      QStringLiteral("req_valid"),
                      SymbolTaxonomy::CollectorKind::PortInput,
                      QStringLiteral("rel_top"));
    const SemanticSymbolRecord stageDataRecord =
        recordInScope(topRecords,
                      QStringLiteral("stage_data"),
                      SymbolTaxonomy::CollectorKind::Logic,
                      QStringLiteral("rel_top"));
    const SemanticSymbolRecord rspDataRecord =
        recordInScope(topRecords,
                      QStringLiteral("rsp_data"),
                      SymbolTaxonomy::CollectorKind::PortOutput,
                      QStringLiteral("rel_top"));
    const SemanticSymbolRecord topClkRecord =
        recordInScope(topRecords,
                      QStringLiteral("top_clk"),
                      SymbolTaxonomy::CollectorKind::PortInput,
                      QStringLiteral("rel_top"));
    const SemanticSymbolRecord topRstRecord =
        recordInScope(topRecords,
                      QStringLiteral("top_rst_n"),
                      SymbolTaxonomy::CollectorKind::PortInput,
                      QStringLiteral("rel_top"));
    const SemanticSymbolRecord stageInstanceRecord =
        recordInScope(topRecords,
                      QStringLiteral("u_stage"),
                      SymbolTaxonomy::CollectorKind::Inst,
                      QStringLiteral("rel_top"));

    const int packageId = packageRecord.localHandle;
    const int stateTypeId = stateTypeRecord.localHandle;
    const int stateVarId = stateVarRecord.localHandle;
    const int topId = topRecord.localHandle;
    const int stageId = stageRecord.localHandle;
    const int captureId = captureRecord.localHandle;
    const int reqValidId = reqValidRecord.localHandle;
    const int stageDataId = stageDataRecord.localHandle;
    const int rspDataId = rspDataRecord.localHandle;
    const int topClkId = topClkRecord.localHandle;
    const int topRstId = topRstRecord.localHandle;
    const int stageInstanceId = stageInstanceRecord.localHandle;

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
    expectBool("top stage instance extracted", stageInstanceId > 0, true);

    SmartRelationshipBuilder fixtureBuilder(
        &engine,
        &slang,
        [&index](const QString& fileName) {
            return index.getSymbolRecords(fileName);
        });
    SemanticQueryContext queryContext;
    queryContext.fileName = topPath;
    queryContext.moduleName = QStringLiteral("rel_top");
    const QList<SemanticSymbolRecord> facadeStageDefs =
        index.findDefinitionRecords(QStringLiteral("rel_stage"), queryContext);
    expectBool("semantic facade returns top symbols",
               index.getSymbolRecords(topPath).size() == topRecords.size(), true);
    expectBool("semantic facade finds cross-file module",
               !facadeStageDefs.isEmpty()
                   && facadeStageDefs.first().localHandle == stageId,
               true);
    expectBool("semantic facade uses fixture symbol record",
               topRecord.name == QStringLiteral("rel_top"), true);
    const SymbolStableKey topFacadeStableKey = topRecord.stableKey;
    const SemanticSymbolRecord indexedTopRecord =
        index.getSymbolRecordByStableKey(topFacadeStableKey);
    const SemanticSymbolRecord indexedStageDataRecord =
        index.getSymbolRecordByStableKey(stageDataRecord.stableKey);
    expectBool("semantic facade exposes symbol record",
               indexedTopRecord.isValid()
                   && indexedTopRecord.name == QStringLiteral("rel_top")
                   && indexedTopRecord.localHandle == topId
                   && indexedTopRecord.stableKey == topFacadeStableKey
                   && indexedTopRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && indexedTopRecord.sourceRole
                       == SymbolTaxonomy::SourceRole::DesignSource,
               true);
    expectBool("semantic facade record carries owner and type",
               indexedStageDataRecord.isValid()
                   && indexedStageDataRecord.owner.name == QStringLiteral("rel_top")
                   && indexedStageDataRecord.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Module
                   && indexedStageDataRecord.type.rawTypeText
                       == stageDataRecord.type.rawTypeText,
               true);
    expectBool("semantic facade finds symbol definition",
               !facadeStageDefs.isEmpty()
                   && facadeStageDefs.first().localHandle == stageId,
               true);
    expectBool("semantic facade returns missing symbol definition",
               index.findDefinitionRecords(QStringLiteral("missing_symbol"),
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
              index.findEndModuleLine(topPath, topRecord), topEndModuleLine);

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
    SemanticIndex diagnosticIndex;
    diagnosticIndex.setSnapshot(sharedSnapshotFromSymbols(
        snapshotFromSemanticIndex(index, brokenDiagnostics)));
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

    SemanticIndex diagnosticReportIndex;
    diagnosticReportIndex.setSnapshot(sharedSnapshotFromSymbols(
        snapshotFromSemanticIndex(index, {
            infoDiagnostic,
            warningDiagnostic,
            errorDiagnostic,
        })));
    QHash<QString, SemanticAnalysisBandMetadata> diagnosticAnalysisBands;
    SemanticAnalysisBandMetadata currentDiagnosticBand;
    currentDiagnosticBand.label = QStringLiteral("current");
    currentDiagnosticBand.displayName = QStringLiteral("current");
    currentDiagnosticBand.priority = true;
    currentDiagnosticBand.publicationCheckpoint = 1;
    diagnosticAnalysisBands.insert(topPath, currentDiagnosticBand);
    SemanticAnalysisBandMetadata backgroundDiagnosticBand;
    backgroundDiagnosticBand.label = QStringLiteral("background");
    backgroundDiagnosticBand.displayName = QStringLiteral("background");
    backgroundDiagnosticBand.priority = false;
    diagnosticAnalysisBands.insert(stagePath, backgroundDiagnosticBand);
    diagnosticReportIndex.setWorkspaceFileAnalysisBands(diagnosticAnalysisBands);
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
    expectInt("diagnostic report analysis band count",
              diagnosticReport.analysisBandCounts.value(QStringLiteral("current")), 2);
    expectInt("diagnostic report background band count",
              diagnosticReport.analysisBandCounts.value(QStringLiteral("background")), 1);
    expectInt("diagnostic report analysis band group count",
              diagnosticReport.analysisBandGroups.size(), 2);
    expectBool("diagnostic report groups analysis bands",
               diagnosticReport.analysisBandGroups.size() == 2
                   && diagnosticReport.analysisBandGroups.first().label
                       == QStringLiteral("current")
                   && diagnosticReport.analysisBandGroups.first().count == 2
                   && diagnosticReport.analysisBandGroups.first()
                          .severityCounts.value(SemanticDiagnostic::Warning) == 1
                   && diagnosticReport.analysisBandGroups.last().label
                       == QStringLiteral("background")
                   && diagnosticReport.analysisBandGroups.last().count == 1
                   && diagnosticReport.analysisBandGroups.last()
                          .severityCounts.value(SemanticDiagnostic::Error) == 1,
               true);
    expectBool("diagnostic report formats analysis band summary",
               diagnosticReport.analysisBandSummaryText()
                   == QStringLiteral("diagnostic bands current 2 diagnostics (1 warning, 1 info), background 1 diagnostic (1 error)"),
               true);
    DiagnosticQuery currentBandDiagnosticQuery;
    currentBandDiagnosticQuery.analysisBandLabel = QStringLiteral("current");
    expectInt("diagnostic report filters current band",
              diagnosticReportService
                  .findDiagnosticReport(currentBandDiagnosticQuery)
                  .totalCount,
              2);
    DiagnosticQuery backgroundBandDiagnosticQuery;
    backgroundBandDiagnosticQuery.analysisBandLabel =
        QStringLiteral("background");
    expectInt("diagnostic report filters background band",
              diagnosticReportService
                  .findDiagnosticReport(backgroundBandDiagnosticQuery)
                  .totalCount,
              1);
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
    expectBool("diagnostic report exposes analysis band metadata",
               !diagnosticReport.diagnostics.isEmpty()
                   && diagnosticReport.diagnostics.first().analysisBand.label
                       == QStringLiteral("background")
                   && diagnosticReport.diagnostics.first()
                          .analysisBandDisplayName == QStringLiteral("background"),
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
    DiagnosticPanelQueryOptions backgroundBandPanelOptions;
    backgroundBandPanelOptions.scope = DiagnosticPanelScope::AllFiles;
    backgroundBandPanelOptions.analysisBandLabel =
        QStringLiteral("background");
    const DiagnosticQuery backgroundBandPanelQuery =
        diagnosticReportService.queryForPanel(backgroundBandPanelOptions);
    expectBool("diagnostic panel query carries band filter",
               backgroundBandPanelQuery.analysisBandLabel
                   == QStringLiteral("background"),
               true);
    expectInt("diagnostic panel band filter count",
              diagnosticReportService
                  .findDiagnosticReport(backgroundBandPanelQuery)
                  .totalCount,
              1);

    const QString injectedRelationshipPath =
        normalizedPath(fixtureDir.filePath(QStringLiteral("injected_relationships.sv")));
    const SemanticSymbolRecord injectedModule =
        SemanticFixtureRecordBuilder(QStringLiteral("injected_top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(injectedRelationshipPath)
            .withLocalHandle(7201)
            .withRange(1, 1, 9, 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord injectedSignal =
        SemanticFixtureRecordBuilder(QStringLiteral("injected_signal"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(injectedRelationshipPath)
            .withLocalHandle(7202)
            .withLine(3)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("injected_top"))
            .record();
    const QList<SemanticSymbolRecord> injectedRelationshipRecords{
        injectedModule,
        injectedSignal,
    };

    SymbolRelationshipEngine injectedRelationshipEngine(
        [&injectedRelationshipPath, &injectedRelationshipRecords](
            const QString& fileName) {
            if (fileName == injectedRelationshipPath)
                return injectedRelationshipRecords;
            return QList<SemanticSymbolRecord>{};
        });
    injectedRelationshipEngine.buildFileRelationships(injectedRelationshipPath);
    expectBool("relationship engine uses injected symbol provider",
               injectedRelationshipEngine.hasRelationship(
                   injectedModule.localHandle,
                   injectedSignal.localHandle,
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
        searchFoundTop = searchFoundTop || result.symbolRecord.localHandle == topId;
        searchFoundStage = searchFoundStage || result.symbolRecord.localHandle == stageId;
        searchFoundTopStableKey = searchFoundTopStableKey
            || (result.symbolRecord.localHandle == topId
                && result.symbolStableKey == result.symbolRecord.stableKey);
        searchFoundTopRecord = searchFoundTopRecord
            || (result.symbolRecord.localHandle == topId
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
        fileSearchFoundTop =
            fileSearchFoundTop || result.symbolRecord.localHandle == topId;
        fileSearchFoundStage =
            fileSearchFoundStage || result.symbolRecord.localHandle == stageId;
    }
    expectBool("search service filters file module",
               fileSearchFoundTop, true);
    expectBool("search service excludes other file module",
               fileSearchFoundStage, false);

    SearchQuery exactTaskSearchQuery;
    exactTaskSearchQuery.text = QStringLiteral("capture_sample");
    exactTaskSearchQuery.declarationKinds = {
        SymbolTaxonomy::DeclarationKind::Task};
    exactTaskSearchQuery.exactMatch = true;
    exactTaskSearchQuery.maxResults = 1;
    const QList<SearchResult> exactTaskResults =
        searchService.findSymbols(exactTaskSearchQuery);
    expectBool("search service exact task result",
               exactTaskResults.size() == 1
                   && exactTaskResults.first().symbolRecord.localHandle == captureId,
               true);

    SearchQuery outlineSearchQuery;
    outlineSearchQuery.text = QStringLiteral("capture");
    outlineSearchQuery.intent = SymbolTaxonomy::SymbolSearchIntent::OutlineSymbols;
    const QList<SearchResult> outlineSearchResults =
        searchService.findSymbols(outlineSearchQuery);
    expectBool("search service outline intent finds task",
               outlineSearchResults.size() == 1
                   && outlineSearchResults.first().symbolRecord.localHandle == captureId,
               true);

    QVector<RelationshipToAdd> rels =
        fixtureBuilder.computeRelationships(topPath,
                                            contents.value(topPath),
                                            index.getSymbolRecords(topPath),
                                            nullptr);

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
               index.relationshipEngine() == &engine, true);
    const std::unique_ptr<SmartRelationshipBuilder> facadeBuilder =
        index.createRelationshipBuilder(&engine, &slang);
    expectBool("semantic index creates relationship builder",
               facadeBuilder != nullptr, true);
    const QVector<RelationshipToAdd> facadeBuilderRels =
        facadeBuilder->computeRelationships(topPath,
                                            contents.value(topPath),
                                            index.getSymbolRecords(topPath),
                                            nullptr);
    expectBool("semantic index builder uses facade records",
               hasRel(facadeBuilderRels,
                      topId,
                      stageId,
                      SymbolRelationshipEngine::INSTANTIATES),
               true);

    const auto symbolOnlySnapshot = sharedSnapshotFromSymbols(
        snapshotFromSemanticIndex(index));
    SmartRelationshipBuilder snapshotBuilder(&engine, &slang);
    const QVector<RelationshipToAdd> snapshotBackedRels =
        snapshotBuilder.computeRelationships(topPath,
                                             contents.value(topPath),
                                             symbolOnlySnapshot->getSymbolRecords(topPath),
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

    SemanticIndex snapshotIndex;
    for (const QString& path : paths) {
        snapshotIndex.updateSymbolRecordsForFile(
            path,
            recordsByFile.value(path),
            contents.value(path));
    }
    const auto snapshot = sharedSnapshotFromSymbols(
        snapshotFromSemanticIndex(index, {}, &engine));
    snapshotIndex.setSnapshot(snapshot);
    expectBool("semantic snapshot returns top symbols",
               snapshotIndex.getSymbolRecords(topPath).size() == topRecords.size(), true);
    const QList<SemanticSymbolRecord> snapshotTopRecords =
        snapshotIndex.getSymbolRecords(topPath);
    const SemanticSymbolRecord snapshotTopRecord =
        snapshotIndex.getSymbolRecordByStableKey(topRecord.stableKey);
    expectBool("semantic snapshot exposes symbol records",
               snapshotTopRecords.size() == topRecords.size()
                   && snapshotTopRecord.isValid()
                   && snapshotTopRecord.localHandle == topId
                   && snapshotTopRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module,
               true);
    const QList<SemanticSymbolRecord> snapshotStageDefs =
        snapshotIndex.findDefinitionRecords(QStringLiteral("rel_stage"),
                                            queryContext);
    expectBool("semantic snapshot finds symbol definition",
               !snapshotStageDefs.isEmpty()
                   && snapshotStageDefs.first().localHandle == stageId,
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
        snapshotSearchFoundTop =
            snapshotSearchFoundTop || result.symbolRecord.localHandle == topId;
        snapshotSearchFoundStage =
            snapshotSearchFoundStage || result.symbolRecord.localHandle == stageId;
        snapshotSearchFoundTopStableKey = snapshotSearchFoundTopStableKey
            || (result.symbolRecord.localHandle == topId
                && result.symbolStableKey == result.symbolRecord.stableKey);
        snapshotSearchFoundStageStableKey = snapshotSearchFoundStageStableKey
            || (result.symbolRecord.localHandle == stageId
                && result.symbolStableKey == result.symbolRecord.stableKey);
        snapshotSearchFoundStageRecord = snapshotSearchFoundStageRecord
            || (result.symbolRecord.localHandle == stageId
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
            snapshotFileSearchFoundTop || result.symbolRecord.localHandle == topId;
        snapshotFileSearchFoundStage =
            snapshotFileSearchFoundStage || result.symbolRecord.localHandle == stageId;
    }
    expectBool("snapshot search service filters file module",
               snapshotFileSearchFoundTop, true);
    expectBool("snapshot search service excludes other file module",
               snapshotFileSearchFoundStage, false);
    const QList<SearchResult> snapshotExactTaskResults =
        snapshotSearchService.findSymbols(exactTaskSearchQuery);
    expectBool("snapshot search service exact task result",
               snapshotExactTaskResults.size() == 1
                   && snapshotExactTaskResults.first()
                          .symbolRecord.localHandle == captureId,
               true);
    expectInt("snapshot search service exact score",
              snapshotExactTaskResults.isEmpty() ? 0 : snapshotExactTaskResults.first().score,
              100);
    const QList<SearchResult> snapshotOutlineSearchResults =
        snapshotSearchService.findSymbols(outlineSearchQuery);
    expectBool("snapshot search service outline intent finds task",
               snapshotOutlineSearchResults.size() == 1
                   && snapshotOutlineSearchResults.first()
                          .symbolRecord.localHandle == captureId,
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
            snapshotEmptySearchFoundTop || result.symbolRecord.localHandle == topId;
        snapshotEmptySearchFoundStage =
            snapshotEmptySearchFoundStage || result.symbolRecord.localHandle == stageId;
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
            snapshotEmptyFileFoundStage || result.symbolRecord.localHandle == stageId;
        snapshotEmptyFileFoundTop =
            snapshotEmptyFileFoundTop || result.symbolRecord.localHandle == topId;
    }
    expectBool("snapshot search service empty text filters file module",
               snapshotEmptyFileFoundStage && !snapshotEmptyFileFoundTop, true);
    NavigationService snapshotNavigationService(&snapshotIndex);
    const NavigationModuleTarget snapshotNavigationTarget =
        snapshotNavigationService.resolveModuleTarget(QStringLiteral("rel_stage"));
    expectBool("snapshot navigation service resolves module target",
               snapshotNavigationTarget.found
                   && snapshotNavigationTarget.symbolRow.symbolRecord.isValid()
                   && snapshotNavigationTarget.symbolRow.symbolRecord.localHandle
                       == stageId
                   && snapshotNavigationTarget.symbolRow.symbolRecord.location.fileName
                       == stagePath
                   && snapshotNavigationTarget.symbolRow.symbolStableKey
                       == snapshotNavigationTarget.symbolRow.symbolRecord.stableKey,
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
            || (group.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Module
                && group.displayName == QStringLiteral("Module")
                && !group.symbolRows.isEmpty());
        if (group.declarationKind == SymbolTaxonomy::DeclarationKind::Module
            && group.displayName == QStringLiteral("Module")
            && group.iconKind == SymbolOutlineIconKind::Module
            && !group.symbolRows.isEmpty()) {
            const SymbolOutlineSymbolRow& row = group.symbolRows.first();
            snapshotOutlineHasRowMetadata =
                row.symbolRecord.isValid()
                && row.symbolRecord.localHandle == topId
                && row.symbolStableKey == row.symbolRecord.stableKey
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
    QList<SemanticSymbolRecord> metadataOutlineRecords =
        snapshot->getSymbolRecords();
    const SemanticSymbolRecord metadataOutlineModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("metadata_rel_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(topPath)
            .withLine(1)
            .withLocalHandle(900001)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::User)
            .record();
    metadataOutlineRecords.append(metadataOutlineModule);
    SemanticIndex metadataOutlineIndex;
    metadataOutlineIndex.setSnapshot(
        sharedSnapshotFromRecords(
            metadataOutlineRecords,
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
                   && metadataOutlineSearchResults.first().symbolRecord.localHandle
                       == metadataOutlineModule.localHandle,
               true);
    SearchQuery metadataTypedSearchQuery;
    metadataTypedSearchQuery.text = QStringLiteral("metadata_rel_top");
    metadataTypedSearchQuery.declarationKinds = {
        SymbolTaxonomy::DeclarationKind::Module};
    const QList<SearchResult> metadataTypedSearchResults =
        metadataOutlineSearchService.findSymbols(metadataTypedSearchQuery);
    expectBool("metadata typed search finds module",
               metadataTypedSearchResults.size() == 1
                   && metadataTypedSearchResults.first().symbolRecord.localHandle
                       == metadataOutlineModule.localHandle
                   && metadataTypedSearchResults.first().symbolRecord.isValid()
                   && metadataTypedSearchResults.first().symbolRecord.stableKey
                       == metadataTypedSearchResults.first().symbolStableKey
                   && metadataTypedSearchResults.first().symbolRecord.name
                       == QStringLiteral("metadata_rel_top")
                   && metadataTypedSearchResults.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && metadataTypedSearchResults.first().symbolRecord.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Global
                   && metadataTypedSearchResults.first().symbolDisplayName
                       == QStringLiteral("metadata_rel_top")
                   && metadataTypedSearchResults.first().symbolTypeDisplayName
                       == QStringLiteral("module")
                   && metadataTypedSearchResults.first().sourceRoleDisplayName
                       == QStringLiteral("design source")
                   && metadataTypedSearchResults.first().codeLink.fileName == topPath
                   && metadataTypedSearchResults.first().codeLink.line == 1
                   && metadataTypedSearchResults.first().codeLink.column == 1
                   && metadataTypedSearchResults.first().codeLink.fileDisplayName
                       == QStringLiteral("relationship_top.sv")
                   && metadataTypedSearchResults.first().codeLink.lineDisplayName
                       == QStringLiteral("1"),
               true);
    SearchQuery metadataDefinitionSearchQuery;
    metadataDefinitionSearchQuery.text = QStringLiteral("metadata_rel_top");
    metadataDefinitionSearchQuery.intent =
        SymbolTaxonomy::SymbolSearchIntent::DefinitionCandidates;
    const QList<SearchResult> metadataDefinitionSearchResults =
        metadataOutlineSearchService.findSymbols(metadataDefinitionSearchQuery);
    expectBool("metadata definition search finds module",
               metadataDefinitionSearchResults.size() == 1
                   && metadataDefinitionSearchResults.first().symbolRecord.localHandle
                       == metadataOutlineModule.localHandle
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
        if (group.declarationKind != SymbolTaxonomy::DeclarationKind::Module
            || group.displayName != QStringLiteral("Module")) {
            continue;
        }
        for (const SymbolOutlineSymbolRow& row : group.symbolRows) {
            metadataOutlineGroupedAsModule =
                metadataOutlineGroupedAsModule
                || (row.symbolRecord.isValid()
                    && row.symbolRecord.localHandle
                        == metadataOutlineModule.localHandle
                    && row.symbolStableKey == row.symbolRecord.stableKey
                    && row.symbolRecord.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Module
                    && row.symbolRecord.collectorKind
                        == SymbolTaxonomy::CollectorKind::User
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
    SemanticSymbolRecord snapshotTopRecordWithoutEnd = topRecord;
    snapshotTopRecordWithoutEnd.location.endLine = 0;
    expectInt("semantic snapshot finds module end line from cached content",
              snapshotIndex.findEndModuleLine(topPath, snapshotTopRecordWithoutEnd),
              topEndModuleLine);
    const SymbolStableKey topStableKey = topRecord.stableKey;
    const SymbolStableKey stageStableKey = stageRecord.stableKey;
    const QList<SemanticRelationship> snapshotTopRelationships =
        snapshotIndex.relationshipsForStableKey(topStableKey, true);
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
                && relationship.fromSymbolRecord.localHandle == topId
                && relationship.toSymbolRecord.localHandle == stageId);
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
                && relationship.fromSymbolRecord.localHandle == topId
                && relationship.toSymbolRecord.localHandle == stageId);
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
            || (reference.relationshipType == SymbolRelationshipEngine::INSTANTIATES
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
                && node.symbolRecord.localHandle == stageId
                && node.symbolRecord.isValid()
                && node.symbolRecord.localHandle == stageId
                && node.symbolRecord.stableKey == stageStableKey
                && node.symbolRecord.name == QStringLiteral("rel_stage")
                && node.symbolStableKey == stageStableKey
                && node.parentStableKey == topStableKey
                && node.direction == HierarchyQuery::Children
                && node.viaType == SymbolRelationshipEngine::INSTANTIATES
                && node.directionDisplayName == QStringLiteral("Outgoing")
                && node.relationshipTypeDisplayName == QStringLiteral("Instantiates")
                && node.symbolDisplayName == QStringLiteral("rel_stage")
                && node.symbolTypeDisplayName == QStringLiteral("module")
                && node.sourceRoleDisplayName == QStringLiteral("design source")
                && node.codeLink.fileName == stagePath
                && node.codeLink.line > 0
                && node.codeLink.fileDisplayName
                    == QStringLiteral("relationship_stage.sv")
                && !node.codeLink.lineDisplayName.isEmpty());
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
                   && snapshotStableHierarchy.first().symbolRecord.localHandle == topId,
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
                          .symbolRecord.localHandle == stageId,
               true);
    expectBool("snapshot hierarchy report keeps child identity",
               snapshotHierarchyReport.nodes.size() == 2
                   && snapshotHierarchyReport.nodes.last()
                          .symbolRecord.localHandle == stageId
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
    expectBool("snapshot hierarchy report exposes node metadata",
               snapshotHierarchyReport.nodes.size() == 2
                   && snapshotHierarchyReport.nodes.first().symbolDisplayName
                       == QStringLiteral("rel_top")
                   && snapshotHierarchyReport.nodes.first().symbolTypeDisplayName
                       == QStringLiteral("module")
                   && snapshotHierarchyReport.nodes.first().sourceRoleDisplayName
                       == QStringLiteral("design source")
                   && snapshotHierarchyReport.nodes.first().codeLink.fileName == topPath
                   && snapshotHierarchyReport.nodes.first().codeLink.line > 0
                   && snapshotHierarchyReport.nodes.first().codeLink.fileDisplayName
                       == QStringLiteral("relationship_top.sv")
                   && !snapshotHierarchyReport.nodes.first()
                           .codeLink.lineDisplayName.isEmpty()
                   && snapshotHierarchyReport.nodes.last().symbolDisplayName
                       == QStringLiteral("rel_stage")
                   && snapshotHierarchyReport.nodes.last().symbolTypeDisplayName
                       == QStringLiteral("module")
                   && snapshotHierarchyReport.nodes.last().sourceRoleDisplayName
                       == QStringLiteral("design source")
                   && snapshotHierarchyReport.nodes.last().codeLink.fileName == stagePath
                   && snapshotHierarchyReport.nodes.last().codeLink.line > 0
                   && snapshotHierarchyReport.nodes.last().codeLink.fileDisplayName
                       == QStringLiteral("relationship_stage.sv")
                   && !snapshotHierarchyReport.nodes.last()
                           .codeLink.lineDisplayName.isEmpty(),
               true);
    const HierarchyReport snapshotStableHierarchyReport =
        snapshotHierarchyService.getHierarchyReport(snapshotStableHierarchyQuery);
    expectBool("snapshot hierarchy report resolves stable query key",
               snapshotStableHierarchyReport.totalCount
                   == snapshotHierarchyReport.totalCount
                   && snapshotStableHierarchyReport.rootStableKey == topStableKey
                   && !snapshotStableHierarchyReport.nodes.isEmpty()
                   && snapshotStableHierarchyReport.nodes.first()
                          .symbolRecord.localHandle == topId,
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
                          .symbolRecord.localHandle == topId,
               true);
    expectBool("snapshot hierarchy parent report keeps parent identity",
               snapshotParentHierarchyReport.nodes.size() == 2
                   && snapshotParentHierarchyReport.nodes.last()
                          .symbolRecord.localHandle == topId
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
                   && snapshotNamedHierarchyReport.nodes.first()
                          .symbolRecord.localHandle == topId
                   && snapshotNamedHierarchyReport.nodes.last()
                          .symbolRecord.localHandle == stageId,
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
    SemanticSymbolRecord reboundTopRecord = topRecord;
    SemanticSymbolRecord reboundStageRecord =
        snapshotIndex.getSymbolRecordByStableKey(stageStableKey);
    reboundTopRecord.localHandle = topId + 100000;
    reboundStageRecord.localHandle = stageId + 100000;
    SemanticRelationship driftingStableRelationship;
    driftingStableRelationship.fromId = topId;
    driftingStableRelationship.toId = stageId;
    driftingStableRelationship.type = SymbolRelationshipEngine::INSTANTIATES;
    driftingStableRelationship.fromStableKey = topStableKey;
    driftingStableRelationship.toStableKey = stageStableKey;
    const auto reboundSnapshot = sharedSnapshotFromRecords(
        QList<SemanticSymbolRecord>{reboundTopRecord, reboundStageRecord},
        QList<SemanticRelationship>{driftingStableRelationship});
    const SemanticRelationship reboundRelationship =
        reboundSnapshot->rebindRelationship(driftingStableRelationship);
    expectBool("semantic snapshot rebinds stable relationship handles",
               reboundRelationship.fromId == reboundTopRecord.localHandle
                   && reboundRelationship.toId == reboundStageRecord.localHandle
                   && reboundRelationship.fromStableKey == topStableKey
                   && reboundRelationship.toStableKey == stageStableKey,
               true);
    const QList<SemanticRelationship> reboundRelationships =
        reboundSnapshot->relationshipsForStableKey(topStableKey, true);
    expectBool("semantic snapshot queries rebound relationship",
               reboundRelationships.size() == 1
                   && reboundRelationships.first().fromId
                       == reboundTopRecord.localHandle
                   && reboundRelationships.first().toId
                       == reboundStageRecord.localHandle,
               true);
    const QList<SemanticRelationship> reboundStableRelationships =
        reboundSnapshot->relationshipsForStableKey(topStableKey, true);
    expectBool("semantic snapshot queries rebound relationship by stable key",
               reboundStableRelationships.size() == 1
                   && reboundStableRelationships.first().fromId
                       == reboundTopRecord.localHandle
                   && reboundStableRelationships.first().toId
                       == reboundStageRecord.localHandle,
               true);
    SemanticIndex reboundIndex;
    reboundIndex.setSnapshot(reboundSnapshot);
    expectBool("semantic index resolves rebound stable key",
               reboundIndex.getSymbolRecordByStableKey(topStableKey).localHandle
                       == reboundTopRecord.localHandle
                   && reboundIndex.getSymbolRecordByStableKey(stageStableKey).localHandle
                       == reboundStageRecord.localHandle,
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
    const SymbolStableKey captureStableKey = captureRecord.stableKey;
    for (const SemanticRelationship& relationship :
         enrichedSnapshot.relationshipsForStableKey(stageStableKey, true)) {
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
        snapshotFromSemanticIndex(index, {
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
    SemanticIndex captureIndex;
    captureIndex.setSnapshot(sharedSnapshotFromSymbols(
        snapshotFromSemanticIndex(index, {}, &engine)));
    const auto capturedSnapshot =
        captureIndex.captureSnapshotPreservingDiagnostics();
    captureIndex.setSnapshot(sharedSnapshotFromRecords(
        capturedSnapshot->getSymbolRecords(),
        capturedSnapshot->relationships(),
        QList<SemanticDiagnostic>{errorDiagnostic},
        capturedSnapshot->fileContents()));
    const auto recapturedSnapshot =
        captureIndex.captureSnapshotPreservingDiagnostics();
    expectInt("semantic index capture preserves diagnostics",
              recapturedSnapshot->diagnostics().size(), 1);
    captureIndex.setSnapshot(sharedSnapshotFromRecords(
        recapturedSnapshot->getSymbolRecords(),
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
    SemanticIndex guardedIndex;
    const auto guardedBaseSnapshot =
        sharedSnapshotFromSymbols(
            snapshotFromSemanticIndex(index, {}, &engine));
    guardedIndex.setSnapshot(guardedBaseSnapshot);
    const SemanticSnapshotToken staleToken = guardedIndex.snapshotToken();
    const auto newerSnapshot =
        sharedSnapshotFromSymbols(
            guardedBaseSnapshot->withAdditionalRelationships({newTaskRelationship}));
    guardedIndex.setSnapshot(newerSnapshot);
    const auto staleRelationshipSnapshot =
        sharedSnapshotFromSymbols(
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
    const QList<SemanticSymbolRecord> restoredStageDefs =
        snapshotIndex.findDefinitionRecords(QStringLiteral("rel_stage"),
                                            queryContext);
    expectBool("semantic snapshot clear restores live index",
               !restoredStageDefs.isEmpty()
                   && restoredStageDefs.first().localHandle == stageId,
               true);

    engine.clearAllRelationships();
    expectBool("scheduler test starts from empty relationship engine",
               engine.hasRelationship(topId,
                                      stageId,
                                      SymbolRelationshipEngine::INSTANTIATES),
               false);

    const auto previousGlobalSnapshot = SemanticIndex::getInstance()->snapshot();
    SemanticIndex::getInstance()->setSnapshot(snapshot);

    AnalysisScheduler scheduler;
    scheduler.setRelationshipEngine(&engine);
    scheduler.setRelationshipBuilder(&fixtureBuilder);
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
            singleFileSchedulerResult.semanticSnapshot
                ->relationshipsForStableKey(topStableKey, true);
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
        sharedSnapshotFromRecords(
            QList<SemanticSymbolRecord>(),
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
        for (const SemanticSymbolRecord& record :
             snapshot->getSymbolRecords(dirtyWorkspaceFilePath)) {
            dirtySnapshotHasOpen = dirtySnapshotHasOpen
                || record.name == QStringLiteral("dirty_open")
                || record.name == QStringLiteral("dirty_signal");
            dirtySnapshotHasDisk = dirtySnapshotHasDisk
                || record.name == QStringLiteral("dirty_disk")
                || record.name == QStringLiteral("disk_signal");
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
        for (const SemanticSymbolRecord& record :
             snapshot->getSymbolRecords(dirtyExternalPath)) {
            dirtyExternalSnapshotHasOpen = dirtyExternalSnapshotHasOpen
                || record.name == QStringLiteral("external_open")
                || record.name == QStringLiteral("external_dirty_signal");
            dirtyExternalSnapshotHasDisk = dirtyExternalSnapshotHasDisk
                || record.name == QStringLiteral("external_disk")
                || record.name == QStringLiteral("external_disk_signal");
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
    SemanticIndex documentSaveIndex;
    documentSaveIndex.setSnapshot(sharedSnapshotFromSymbols(
        snapshotFromSemanticIndex(index, {}, &engine)));
    documentSaveIndex.attachRelationshipEngine(&documentSaveEngine);
    SmartRelationshipBuilder documentSaveBuilder(
        &documentSaveEngine,
        &slang,
        [&documentSaveIndex](const QString& fileName) {
            return documentSaveIndex.getSymbolRecords(fileName);
        });
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
                             if (snapshot && !snapshot->getSymbolRecords(staleFilePath).isEmpty())
                                 staleFileLoop.quit();
                         }
                     });
    QTimer::singleShot(3000, &staleFileLoop, &QEventLoop::quit);
    staleFileLoop.exec();
    QApplication::processEvents();
    bool staleSnapshotHasNew = false;
    bool staleSnapshotHasOld = false;
    if (const auto snapshot = SemanticIndex::getInstance()->snapshot()) {
        const QList<SemanticSymbolRecord> records =
            snapshot->getSymbolRecords(staleFilePath);
        for (const SemanticSymbolRecord& record : records) {
            staleSnapshotHasNew = staleSnapshotHasNew
                || record.name == QStringLiteral("stale_new")
                || record.name == QStringLiteral("new_signal");
            staleSnapshotHasOld = staleSnapshotHasOld
                || record.name.startsWith(QStringLiteral("stale_old_"))
                || record.name.startsWith(QStringLiteral("old_signal_"));
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
    SemanticIndex::getInstance()->setSnapshot(snapshot);
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
    expectInt("scheduler workspace result processed files",
              schedulerResult.processedFiles, static_cast<int>(paths.size()));
    expectBool("scheduler workspace result relationship telemetry",
               schedulerResult.relationshipCount > 0
                   && schedulerResult.elapsedMs >= 0,
               true);
    bool schedulerFoundStageRelationship = false;
    if (schedulerResult.semanticSnapshot) {
        const QList<SemanticRelationship> schedulerTopRelationships =
            schedulerResult.semanticSnapshot
                ->relationshipsForStableKey(topStableKey, true);
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

    QTemporaryDir automaticRelationshipWarmupDir;
    expectBool("automatic relationship warmup temp dir created",
               automaticRelationshipWarmupDir.isValid(),
               true);
    if (automaticRelationshipWarmupDir.isValid()) {
        QStringList warmupFiles;
        bool warmupFilesWritten = true;
        for (int i = 0; i < 161; ++i) {
            const QString fileName = normalizedPath(
                automaticRelationshipWarmupDir.filePath(
                    QStringLiteral("auto_rel_%1.sv").arg(i)));
            warmupFiles.append(fileName);
            warmupFilesWritten =
                warmupFilesWritten
                && writeTextFile(
                    fileName,
                    QStringLiteral("module auto_rel_%1; endmodule\n").arg(i));
        }
        expectBool("automatic relationship warmup files written",
                   warmupFilesWritten,
                   true);

        const auto automaticRelationshipPreviousSnapshot =
            SemanticIndex::getInstance()->snapshot();
        AnalysisScheduler automaticRelationshipScheduler;
        SymbolAnalyzer automaticRelationshipAnalyzer;
        SlangManager automaticRelationshipSlang;
        SmartRelationshipBuilder automaticRelationshipBuilder(
            nullptr,
            &automaticRelationshipSlang);
        automaticRelationshipScheduler.setSymbolAnalyzer(
            &automaticRelationshipAnalyzer);
        automaticRelationshipScheduler.setRelationshipBuilder(
            &automaticRelationshipBuilder);

        bool automaticRelationshipStarted = false;
        QEventLoop automaticRelationshipLoop;
        QObject::connect(
            &automaticRelationshipScheduler,
            &AnalysisScheduler::workspaceRelationshipAnalysisStarted,
            &automaticRelationshipLoop,
            [&](const ProjectSnapshot&, int) {
                automaticRelationshipStarted = true;
                automaticRelationshipLoop.quit();
            });

        ProjectSnapshot automaticRelationshipProject;
        automaticRelationshipProject.workspaceRoot =
            normalizedPath(automaticRelationshipWarmupDir.path());
        automaticRelationshipProject.allFiles = warmupFiles;
        automaticRelationshipProject.systemVerilogFiles = warmupFiles;
        automaticRelationshipProject.includeDirs = {
            automaticRelationshipProject.workspaceRoot};
        QTimer::singleShot(10000,
                           &automaticRelationshipLoop,
                           &QEventLoop::quit);
        automaticRelationshipScheduler.requestWorkspaceAnalysis(
            automaticRelationshipProject);
        automaticRelationshipLoop.exec();
        expectBool("large workspace starts automatic relationship analysis",
                   automaticRelationshipStarted,
                   true);
        automaticRelationshipScheduler.cancelWorkspaceAnalysis();
        automaticRelationshipScheduler.cancelWorkspaceRelationshipAnalysis();
        if (automaticRelationshipPreviousSnapshot)
            SemanticIndex::getInstance()->setSnapshot(
                automaticRelationshipPreviousSnapshot);
        else
            SemanticIndex::getInstance()->clearSnapshot();
    }

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
                && rel.toSymbolRecord.name == QStringLiteral("rel_stage"));
        serviceFoundTask = serviceFoundTask
            || (rel.relationship.toId == captureId
                && rel.relationship.type == SymbolRelationshipEngine::CALLS
                && rel.toSymbolRecord.name == QStringLiteral("capture_sample"));
        serviceFoundRead = serviceFoundRead
            || (rel.relationship.toId == reqValidId
                && rel.relationship.type == SymbolRelationshipEngine::READS_FROM
                && rel.toSymbolRecord.name == QStringLiteral("req_valid"));
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
            || (relationship.relationshipType == SymbolRelationshipEngine::CLOCKS
                && relationship.peerSymbolRecord.localHandle == topClkId);
        timingReportHasResetPeer = timingReportHasResetPeer
            || (relationship.relationshipType == SymbolRelationshipEngine::RESETS
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
            || (node.depth == 0 && node.symbolRecord.localHandle == topId);
        hierarchyFoundStage = hierarchyFoundStage
            || (node.depth == 1
                && node.parentStableKey == topStableKey
                && node.symbolRecord.localHandle == stageId);
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
            || (node.symbolRecord.localHandle == stageId
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
                && node.symbolRecord.localHandle == stageId
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
                   && stableHierarchyReport.nodes.first().symbolRecord.name
                       == QStringLiteral("rel_top"),
               true);
    expectBool("hierarchy service exposes all tree types",
               HierarchyService::allRelationshipTypes().contains(SymbolRelationshipEngine::READS_FROM),
               true);

    ModuleBlockDiagramService moduleBlockDiagramService(&index);
    ModuleBlockDiagramQuery moduleBlockQuery;
    moduleBlockQuery.moduleStableKey = topStableKey;
    moduleBlockQuery.maxDepth = 1;
    const ModuleBlockDiagramReport moduleBlockReport =
        moduleBlockDiagramService.buildModuleBlockDiagram(moduleBlockQuery);
    expectBool("module block diagram report found",
               moduleBlockReport.found
                   && moduleBlockReport.notFoundReason
                       == ModuleBlockDiagramNotFoundReason::None,
               true);
    expectInt("module block diagram module count",
              moduleBlockReport.moduleCount,
              2);
    expectInt("module block diagram edge count",
              moduleBlockReport.edgeCount,
              1);
    expectInt("module block diagram resolved instance count",
              moduleBlockReport.resolvedInstanceCount,
              1);
    expectInt("module block diagram unresolved instance count",
              moduleBlockReport.unresolvedInstanceCount,
              0);
    expectBool("module block diagram keeps root definition link",
               moduleBlockReport.root.moduleSymbolRecord.localHandle == topId
                   && moduleBlockReport.root.moduleStableKey == topStableKey
                   && moduleBlockReport.root.moduleDisplayName
                       == QStringLiteral("rel_top")
                   && moduleBlockReport.root.definitionCodeLink.fileName == topPath
                   && moduleBlockReport.root.definitionCodeLink.line > 0,
               true);
    bool moduleBlockHasStage = false;
    bool moduleBlockHasOnlyModuleNodes = true;
    for (const ModuleBlockDiagramNode& node : moduleBlockReport.nodes) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForSymbolRecord(node.moduleSymbolRecord);
        moduleBlockHasOnlyModuleNodes = moduleBlockHasOnlyModuleNodes
            && (metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Module
                || metadata.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Interface);
        moduleBlockHasStage = moduleBlockHasStage
            || (node.depth == 1
                && node.parentNodeId == moduleBlockReport.root.nodeId
                && node.moduleSymbolRecord.localHandle == stageId
                && node.moduleStableKey == stageStableKey
                && node.moduleDisplayName == QStringLiteral("rel_stage")
                && node.instanceDisplayName == QStringLiteral("u_stage")
                && node.definitionCodeLink.fileName == stagePath
                && node.definitionCodeLink.line > 0
                && node.instanceCodeLink.fileName == topPath
                && node.instanceCodeLink.line
                       == stageInstanceRecord.location.startLine);
    }
    expectBool("module block diagram keeps child module definition",
               moduleBlockHasStage,
               true);
    expectBool("module block diagram excludes signal nodes",
               moduleBlockHasOnlyModuleNodes,
               true);
    expectBool("module block diagram edge carries navigation link",
               !moduleBlockReport.edges.isEmpty()
                   && moduleBlockReport.edges.first().relationshipType
                       == SymbolRelationshipEngine::INSTANTIATES
                   && moduleBlockReport.edges.first().relationshipDisplayName
                       == QStringLiteral("Instantiates")
                   && moduleBlockReport.edges.first().fromStableKey
                       == topStableKey
                   && moduleBlockReport.edges.first().toStableKey
                       == stageStableKey
                   && moduleBlockReport.edges.first()
                          .childDefinitionCodeLink.fileName == stagePath
                   && moduleBlockReport.edges.first()
                          .childDefinitionCodeLink.line > 0
                   && moduleBlockReport.edges.first()
                          .childInstanceDisplayName == QStringLiteral("u_stage")
                   && moduleBlockReport.edges.first()
                          .childInstanceCodeLink.fileName == topPath
                   && moduleBlockReport.edges.first()
                          .childInstanceCodeLink.line
                          == stageInstanceRecord.location.startLine,
               true);
    ModuleBlockDiagramQuery namedModuleBlockQuery;
    namedModuleBlockQuery.moduleName = QStringLiteral("rel_top");
    namedModuleBlockQuery.fileName =
        QDir(QFileInfo(topPath).dir()).filePath(QStringLiteral("./relationship_top.sv"));
    namedModuleBlockQuery.maxDepth = 1;
    const ModuleBlockDiagramReport namedModuleBlockReport =
        moduleBlockDiagramService.buildModuleBlockDiagram(
            namedModuleBlockQuery);
    expectBool("module block diagram resolves named query",
               namedModuleBlockReport.found
                   && namedModuleBlockReport.root.moduleStableKey == topStableKey
                   && namedModuleBlockReport.edgeCount == 1,
               true);
    ModuleBlockDiagramQuery missingModuleBlockQuery;
    missingModuleBlockQuery.moduleName = QStringLiteral("missing_module");
    const ModuleBlockDiagramReport missingModuleBlockReport =
        moduleBlockDiagramService.buildModuleBlockDiagram(
            missingModuleBlockQuery);
    expectBool("module block diagram reports missing root",
               !missingModuleBlockReport.found
                   && missingModuleBlockReport.notFoundReason
                       == ModuleBlockDiagramNotFoundReason::NoRootModule,
               true);

    const QString blackboxTopPath =
        normalizedPath(fixtureDir.filePath(QStringLiteral("blackbox_top.sv")));
    const SemanticSymbolRecord blackboxTopRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("blackbox_top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(blackboxTopPath)
            .withRange(1, 1, 4, 10)
            .withLocalHandle(9601)
            .record();
    const SemanticSymbolRecord blackboxInstanceRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("u_ip"),
                                     SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(blackboxTopPath)
            .withLine(2, 3)
            .withLocalHandle(9602)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("blackbox_top"),
                       blackboxTopRecord.stableKey)
            .withType(QStringLiteral("vendor_ip"))
            .record();
    SemanticIndex blackboxIndex;
    blackboxIndex.updateSymbolRecordsForFile(
        blackboxTopPath,
        {blackboxTopRecord, blackboxInstanceRecord},
        QStringLiteral("module blackbox_top;\n  vendor_ip u_ip();\nendmodule\n"));
    ModuleBlockDiagramService blackboxService(&blackboxIndex);
    ModuleBlockDiagramQuery blackboxQuery;
    blackboxQuery.moduleName = QStringLiteral("blackbox_top");
    blackboxQuery.fileName = blackboxTopPath;
    blackboxQuery.maxDepth = 2;
    const ModuleBlockDiagramReport blackboxReport =
        blackboxService.buildModuleBlockDiagram(blackboxQuery);
    expectBool("module block diagram keeps unresolved blackbox node",
               blackboxReport.found
                   && blackboxReport.moduleCount == 2
                   && blackboxReport.edgeCount == 1
                   && blackboxReport.resolvedInstanceCount == 0
                   && blackboxReport.unresolvedInstanceCount == 1
                   && blackboxReport.nodes.size() == 2
                   && blackboxReport.nodes.at(1).unresolved
                   && blackboxReport.nodes.at(1).moduleDisplayName
                       == QStringLiteral("vendor_ip")
                   && blackboxReport.nodes.at(1).instanceDisplayName
                       == QStringLiteral("u_ip")
                   && blackboxReport.nodes.at(1).instanceCodeLink.fileName
                       == blackboxTopPath
                   && blackboxReport.nodes.at(1).instanceCodeLink.line
                       == blackboxInstanceRecord.location.startLine
                   && blackboxReport.notFoundReason
                       == ModuleBlockDiagramNotFoundReason::None,
               true);

    const QString wrappedTopPath =
        normalizedPath(fixtureDir.filePath(QStringLiteral("wrapped_top.sv")));
    const SemanticSymbolRecord wrappedTopRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("wrapped_top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(wrappedTopPath)
            .withLocalHandle(9650)
            .withRange(1, 1, 12, 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord wrappedInterfaceRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("wrapped_if"),
                                     SymbolTaxonomy::DeclarationKind::Interface)
            .withFile(wrappedTopPath)
            .withLocalHandle(9651)
            .withLine(20)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Interface)
            .record();
    QList<SemanticSymbolRecord> wrappedRecords{
        wrappedTopRecord,
        wrappedInterfaceRecord,
    };
    for (int i = 0; i < 6; ++i) {
        const QString moduleName =
            QStringLiteral("wrapped_child_%1").arg(i);
        wrappedRecords.append(
            SemanticFixtureRecordBuilder(moduleName,
                                         SymbolTaxonomy::DeclarationKind::Module)
                .withFile(wrappedTopPath)
                .withLocalHandle(9660 + i)
                .withLine(30 + i)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
                .record());
        wrappedRecords.append(
            SemanticFixtureRecordBuilder(
                QStringLiteral("u_wrapped_child_%1").arg(i),
                SymbolTaxonomy::DeclarationKind::Instance)
                .withFile(wrappedTopPath)
                .withLocalHandle(9670 + i)
                .withLine(3 + i)
                .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
                .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                           QStringLiteral("wrapped_top"),
                           wrappedTopRecord.stableKey)
                .withType(moduleName)
                .record());
    }
    const SemanticSymbolRecord wrappedInterfaceInstance =
        SemanticFixtureRecordBuilder(QStringLiteral("u_wrapped_if"),
                                     SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(wrappedTopPath)
            .withLocalHandle(9680)
            .withLine(10)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("wrapped_top"),
                       wrappedTopRecord.stableKey)
            .withType(QStringLiteral("wrapped_if"),
                      QStringLiteral("wrapped_if"),
                      SymbolTaxonomy::DeclarationKind::Interface)
            .record();
    wrappedRecords.append(wrappedInterfaceInstance);
    SemanticIndex wrappedIndex;
    wrappedIndex.setSnapshot(sharedSnapshotFromRecords(wrappedRecords));
    ModuleBlockDiagramService wrappedService(&wrappedIndex);
    ModuleBlockDiagramQuery wrappedQuery;
    wrappedQuery.moduleName = QStringLiteral("wrapped_top");
    wrappedQuery.fileName = wrappedTopPath;
    wrappedQuery.maxDepth = 1;
    const ModuleBlockDiagramReport wrappedReport =
        wrappedService.buildModuleBlockDiagram(wrappedQuery);
    bool wrappedReportHasInterface = false;
    for (const ModuleBlockDiagramNode& node : wrappedReport.nodes) {
        wrappedReportHasInterface = wrappedReportHasInterface
            || node.moduleDisplayName.contains(QStringLiteral("wrapped_if"))
            || node.instanceDisplayName.contains(QStringLiteral("wrapped_if"));
    }
    expectBool("module block diagram filters interface report nodes",
               wrappedReport.found
                   && wrappedReport.moduleCount == 7
                   && wrappedReport.edgeCount == 6
                   && !wrappedReportHasInterface,
               true);
    ModuleBlockDiagramQuery interfaceRootQuery;
    interfaceRootQuery.moduleName = QStringLiteral("wrapped_if");
    interfaceRootQuery.fileName = wrappedTopPath;
    const ModuleBlockDiagramReport interfaceRootReport =
        wrappedService.buildModuleBlockDiagram(interfaceRootQuery);
    expectBool("module block diagram rejects interface root",
               !interfaceRootReport.found
                   && interfaceRootReport.notFoundReason
                       == ModuleBlockDiagramNotFoundReason::NoRootModule,
               true);
    ModuleBlockDiagramService::getInstance()->setSemanticIndex(&wrappedIndex);
    QWidget wrappedPanelHost;
    RtlInsightsPanelCoordinator wrappedPanel(&wrappedPanelHost);
    wrappedPanel.showModuleBlockDiagramForModule(
        wrappedTopPath,
        QStringLiteral("wrapped_top"));
    const QStringList wrappedSummaries =
        wrappedPanel.graphElementSummariesForTest();
    QGraphicsView* wrappedGraphView = wrappedPanel.graphView();
    const QRectF wrappedBounds =
        wrappedGraphView && wrappedGraphView->scene()
            ? wrappedGraphView->scene()->itemsBoundingRect()
            : QRectF();
    QSet<int> wrappedChildXs;
    QSet<int> wrappedChildYs;
    bool wrappedPanelHasInterface = false;
    for (const QString& summary : wrappedSummaries) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 8 || parts.at(0) != QStringLiteral("module"))
            continue;
        wrappedPanelHasInterface = wrappedPanelHasInterface
            || summary.contains(QStringLiteral("wrapped_if"));
        if (parts.at(1).startsWith(QStringLiteral("wrapped_child_"))) {
            wrappedChildXs.insert(parts.at(4).toInt());
            wrappedChildYs.insert(parts.at(5).toInt());
        }
    }
    expectBool("module block diagram wraps sibling child nodes",
               wrappedPanel.graphNodeItemCountForTest() == 7
                   && wrappedChildXs.size() > 1
                   && wrappedChildYs.size() > 1,
               true);
    expectBool("module block diagram wrapped layout stays compact",
               wrappedBounds.isValid()
                   && wrappedBounds.width() < 760.0
                   && wrappedBounds.height() < 420.0,
               true);
    expectBool("module block diagram panel hides interface instance",
               !wrappedPanelHasInterface
                   && !wrappedPanel.graphTableRowsForTest()
                           .join(QLatin1Char('\n'))
                           .contains(QStringLiteral("wrapped_if")),
               true);
    const bool hoveredWrappedRoot =
        wrappedPanel.setGraphItemHoveredForTest(QStringLiteral("module"),
                                                QStringLiteral("wrapped_top"),
                                                QString(),
                                                true);
    expectBool("module block diagram hover keeps nested nodes readable",
               hoveredWrappedRoot
                   && wrappedPanel.graphItemsReadableForTest()
                   && wrappedPanel.graphNestedNodeStackingReadableForTest(),
               true);
    QLineEdit* wrappedSearchEdit =
        wrappedPanel.dock()
            ? wrappedPanel.dock()->findChild<QLineEdit*>(
                  QStringLiteral("rtlGraphSearchEdit"))
            : nullptr;
    if (wrappedSearchEdit)
        wrappedSearchEdit->setText(QStringLiteral("wrapped_top"));
    expectBool("module block diagram selection keeps nodes readable",
               wrappedSearchEdit
                   && wrappedPanel.graphSelectedItemCountForTest() > 0
                   && wrappedPanel.graphItemsReadableForTest()
                   && wrappedPanel.graphNestedNodeStackingReadableForTest(),
               true);

    ModuleBlockDiagramService::getInstance()->setSemanticIndex(&index);
    QWidget moduleBlockPanelHost;
    RtlInsightsPanelCoordinator moduleBlockPanel(&moduleBlockPanelHost);
    QString moduleBlockNavigatedFileName;
    int moduleBlockNavigatedLine = 0;
    int moduleBlockNavigatedColumn = 0;
    moduleBlockPanel.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            moduleBlockNavigatedFileName = fileName;
            moduleBlockNavigatedLine = line;
            moduleBlockNavigatedColumn = column;
            return true;
        });
    moduleBlockPanel.showModuleBlockDiagramForModule(
        topPath,
        QStringLiteral("rel_top"));
    QGraphicsView* moduleBlockGraphView = moduleBlockPanel.graphView();
    expectBool("module block diagram panel renders module-only report",
               moduleBlockGraphView
                   && moduleBlockGraphView->scene()
                   && moduleBlockPanel.graphNodeItemCountForTest() == 2
                   && moduleBlockPanel.graphEdgeItemCountForTest() == 1,
               true);
    expectBool("module block diagram inspector shows root",
               moduleBlockPanel.graphInspectorRowsForTest().join(
                   QLatin1Char('\n')).contains(
                       QStringLiteral("Selected=rel_top"))
                   && moduleBlockPanel.graphInspectorRowsForTest().join(
                       QLatin1Char('\n')).contains(
                       QStringLiteral("Children=1")),
               true);
    expectBool("module block diagram instances table has child",
               moduleBlockPanel.graphTableRowsForTest().join(
                   QLatin1Char('\n')).contains(
                       QStringLiteral("u_stage|rel_stage|rel_top")),
               true);
    const bool selectedStageFromTable =
        moduleBlockPanel.selectGraphTableRowForTest(
            QStringLiteral("rel_stage"),
            QStringLiteral("u_stage"));
    expectBool("module block diagram table row selects graph node",
               selectedStageFromTable
                   && moduleBlockPanel.graphSelectedItemCountForTest() == 1
                   && moduleBlockPanel.graphInspectorRowsForTest().join(
                       QLatin1Char('\n')).contains(
                       QStringLiteral("Selected=u_stage : rel_stage")),
               true);
    QLineEdit* moduleBlockSearchEdit =
        moduleBlockPanel.dock()
            ? moduleBlockPanel.dock()->findChild<QLineEdit*>(
                  QStringLiteral("rtlGraphSearchEdit"))
            : nullptr;
    if (moduleBlockSearchEdit)
        moduleBlockSearchEdit->setText(QStringLiteral("rel_stage"));
    expectBool("module block diagram graph search highlights node",
               moduleBlockSearchEdit
                   && moduleBlockPanel.graphSelectedItemCountForTest() > 0,
               true);
    if (moduleBlockSearchEdit)
        moduleBlockSearchEdit->clear();
    QPushButton* moduleBlockZoomIn =
        moduleBlockPanel.dock()
            ? moduleBlockPanel.dock()->findChild<QPushButton*>(
                  QStringLiteral("rtlGraphZoomInButton"))
            : nullptr;
    const qreal moduleBlockScaleBeforeZoom = moduleBlockGraphView
        ? moduleBlockGraphView->transform().m11()
        : 0.0;
    if (moduleBlockZoomIn)
        moduleBlockZoomIn->click();
    expectBool("module block diagram zoom control scales graph",
               moduleBlockZoomIn
                   && moduleBlockGraphView
                   && moduleBlockGraphView->transform().m11()
                       > moduleBlockScaleBeforeZoom,
               true);
    moduleBlockNavigatedFileName.clear();
    moduleBlockNavigatedLine = 0;
    moduleBlockNavigatedColumn = 0;
    const bool invokedModuleBlockTopNavigation =
        moduleBlockPanel.triggerGraphNavigationForTest(
            QStringLiteral("module"),
            QStringLiteral("rel_top"));
    expectBool("module block diagram root module navigation",
               invokedModuleBlockTopNavigation
                   && moduleBlockNavigatedFileName == topPath
                   && moduleBlockNavigatedLine
                       == topRecord.location.startLine
                   && moduleBlockNavigatedColumn
                       == topRecord.location.startColumn,
               true);
    moduleBlockPanel.showModuleBlockDiagramForModule(
        topPath,
        QStringLiteral("rel_top"));
    moduleBlockNavigatedFileName.clear();
    moduleBlockNavigatedLine = 0;
    moduleBlockNavigatedColumn = 0;
    const bool invokedModuleBlockStageNavigation =
        moduleBlockPanel.triggerGraphNavigationForTest(
            QStringLiteral("module"),
            QStringLiteral("rel_stage"));
    expectBool("module block diagram child module navigation",
               invokedModuleBlockStageNavigation
                   && moduleBlockNavigatedFileName == topPath
                   && moduleBlockNavigatedLine
                       == stageInstanceRecord.location.startLine
                   && moduleBlockNavigatedColumn
                       == stageInstanceRecord.location.startColumn
                   && moduleBlockPanel.graphNodeItemCountForTest() == 1
                   && moduleBlockPanel.graphEdgeItemCountForTest() == 0,
               true);
    expectBool("module block diagram no-child reason visible",
               moduleBlockPanel.graphTextItemsForTest().join(QLatin1Char('\n'))
                   .contains(QStringLiteral("No child modules")),
               true);

    ModuleBlockDiagramService::getInstance()->setSemanticIndex(&blackboxIndex);
    QWidget blackboxPanelHost;
    RtlInsightsPanelCoordinator blackboxPanel(&blackboxPanelHost);
    QString blackboxNavigatedFileName;
    int blackboxNavigatedLine = 0;
    int blackboxNavigatedColumn = 0;
    blackboxPanel.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            blackboxNavigatedFileName = fileName;
            blackboxNavigatedLine = line;
            blackboxNavigatedColumn = column;
            return true;
        });
    blackboxPanel.showModuleBlockDiagramForModule(
        blackboxTopPath,
        QStringLiteral("blackbox_top"));
    const bool invokedBlackboxNavigation =
        blackboxPanel.triggerGraphNavigationForTest(
            QStringLiteral("module"),
            QStringLiteral("vendor_ip"));
    expectBool("module block diagram unresolved instance navigation",
               blackboxPanel.graphNodeItemCountForTest() == 2
                   && blackboxPanel.graphEdgeItemCountForTest() == 1
                   && invokedBlackboxNavigation
                   && blackboxNavigatedFileName == blackboxTopPath
                   && blackboxNavigatedLine
                       == blackboxInstanceRecord.location.startLine
                   && blackboxNavigatedColumn
                       == blackboxInstanceRecord.location.startColumn
                   && blackboxPanel.graphNodeItemCountForTest() == 2
                   && blackboxPanel.graphEdgeItemCountForTest() == 1,
               true);

    const QString diagramTopPath =
        normalizedPath(fixtureDir.filePath(QStringLiteral("diagram_top.sv")));
    const QString diagramStagePath =
        normalizedPath(fixtureDir.filePath(QStringLiteral("diagram_stage.sv")));
    const QString diagramLeafPath =
        normalizedPath(fixtureDir.filePath(QStringLiteral("diagram_leaf.sv")));
    const SemanticSymbolRecord diagramTopRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("diagram_top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(diagramTopPath)
            .withLine(1)
            .withLocalHandle(9701)
            .record();
    const SemanticSymbolRecord diagramStageRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("diagram_stage"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(diagramStagePath)
            .withLine(3)
            .withLocalHandle(9702)
            .record();
    const SemanticSymbolRecord diagramLeafRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("diagram_leaf"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(diagramLeafPath)
            .withLine(5)
            .withLocalHandle(9703)
            .record();
    SemanticIndex diagramIndex;
    diagramIndex.updateSymbolRecordsForFile(
        diagramTopPath,
        {diagramTopRecord},
        QStringLiteral("module diagram_top; endmodule\n"));
    diagramIndex.updateSymbolRecordsForFile(
        diagramStagePath,
        {diagramStageRecord},
        QStringLiteral("module diagram_stage; endmodule\n"));
    diagramIndex.updateSymbolRecordsForFile(
        diagramLeafPath,
        {diagramLeafRecord},
        QStringLiteral("module diagram_leaf; endmodule\n"));
    SymbolRelationshipEngine diagramEngine;
    diagramIndex.attachRelationshipEngine(&diagramEngine);
    diagramEngine.addRelationship(diagramTopRecord.localHandle,
                                  diagramStageRecord.localHandle,
                                  SymbolRelationshipEngine::INSTANTIATES,
                                  QStringLiteral("diagram_top instantiates diagram_stage"));
    diagramEngine.addRelationship(diagramStageRecord.localHandle,
                                  diagramLeafRecord.localHandle,
                                  SymbolRelationshipEngine::INSTANTIATES,
                                  QStringLiteral("diagram_stage instantiates diagram_leaf"));

    ModuleBlockDiagramService::getInstance()->setSemanticIndex(&diagramIndex);
    QWidget drillPanelHost;
    RtlInsightsPanelCoordinator drillPanel(&drillPanelHost);
    QString drillNavigatedFileName;
    int drillNavigatedLine = 0;
    int drillNavigatedColumn = 0;
    drillPanel.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            drillNavigatedFileName = fileName;
            drillNavigatedLine = line;
            drillNavigatedColumn = column;
            return true;
        });
    drillPanel.showModuleBlockDiagramForModule(
        diagramTopPath,
        QStringLiteral("diagram_top"));
    expectBool("module block diagram renders nested container graph",
               drillPanel.graphNodeItemCountForTest() == 3
                   && drillPanel.graphEdgeItemCountForTest() == 2,
               true);
    const bool invokedStageDrill =
        drillPanel.triggerGraphNavigationForTest(
            QStringLiteral("module"),
            QStringLiteral("diagram_stage"));
    expectBool("module block diagram drills into jumped module children",
               invokedStageDrill
                   && drillNavigatedFileName == diagramStagePath
                   && drillNavigatedLine
                       == diagramStageRecord.location.startLine
                   && drillPanel.graphNodeItemCountForTest() == 2
                   && drillPanel.graphEdgeItemCountForTest() == 1,
               true);
    ModuleBlockDiagramService::getInstance()->setSemanticIndex(&index);

    EditorSemanticContext moduleBlockActionContext;
    moduleBlockActionContext.fileName = topPath;
    moduleBlockActionContext.moduleName = QStringLiteral("rel_top");
    moduleBlockActionContext.lineText =
        QStringLiteral("  rel_stage u_stage();");
    moduleBlockActionContext.column = 3;
    const EditorSourceSymbolActionRequestState moduleBlockActionState =
        EditorSourceNavigationQuery::sourceSymbolActionRequestState(
            SourceSymbolAction::ShowModuleBlockDiagram,
            moduleBlockActionContext);
    expectBool("module block diagram source action accepts module name",
               moduleBlockActionState.available
                   && moduleBlockActionState.symbolName
                       == QStringLiteral("rel_stage")
                   && moduleBlockActionState.fileName == topPath
                   && moduleBlockActionState.moduleName
                       == QStringLiteral("rel_top"),
               true);
    EditorSemanticContext signalBlockActionContext;
    signalBlockActionContext.fileName = topPath;
    signalBlockActionContext.moduleName = QStringLiteral("rel_top");
    signalBlockActionContext.lineText = QStringLiteral("  req_valid");
    signalBlockActionContext.column = 3;
    const EditorSourceSymbolActionRequestState signalBlockActionState =
        EditorSourceNavigationQuery::sourceSymbolActionRequestState(
            SourceSymbolAction::ShowModuleBlockDiagram,
            signalBlockActionContext);
    expectBool("module block diagram source action rejects signal name",
               !signalBlockActionState.available,
               true);
    ModuleBlockDiagramService::getInstance()->setSemanticIndex(
        SemanticIndex::getInstance());

    HierarchyQuery parentQuery;
    parentQuery.symbolStableKey = stageStableKey;
    parentQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
    const QList<HierarchyNode> parents = hierarchyService.getParents(parentQuery);
    bool parentFoundTop = false;
    for (const HierarchyNode& node : parents) {
        parentFoundTop = parentFoundTop
            || (node.symbolRecord.localHandle == topId
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
            || (node.depth == 0 && node.symbolRecord.localHandle == stageId);
        parentTreeFoundTop = parentTreeFoundTop
            || (node.depth == 1
                && node.parentStableKey == stageStableKey
                && node.symbolRecord.localHandle == topId
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
        if (node.symbolRecord.localHandle == topId)
            ++cycleTopCount;
        cycleSawOutgoingStage = cycleSawOutgoingStage
            || (node.symbolRecord.localHandle == stageId
                && node.direction == HierarchyQuery::Children);
        cycleSawIncomingStage = cycleSawIncomingStage
            || (node.symbolRecord.localHandle == stageId
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
                && ref.relationshipType == SymbolRelationshipEngine::INSTANTIATES);
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
    reqValidReferenceQuery.symbolStableKey = reqValidRecord.stableKey;
    reqValidReferenceQuery.types = {SymbolRelationshipEngine::READS_FROM};
    const QList<ReferenceResult> reqValidReferences =
        referenceService.findReferences(reqValidReferenceQuery);
    bool referenceFoundReqRead = false;
    for (const ReferenceResult& ref : reqValidReferences) {
        referenceFoundReqRead = referenceFoundReqRead
            || (ref.referencingSymbolRecord.localHandle == topId
                && ref.referencedSymbolRecord.localHandle == reqValidId
                && ref.relationshipType == SymbolRelationshipEngine::READS_FROM);
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
    rspDataReferenceQuery.symbolStableKey = rspDataRecord.stableKey;
    rspDataReferenceQuery.types = {SymbolRelationshipEngine::ASSIGNS_TO};
    const QList<ReferenceResult> rspDataReferences =
        referenceService.findReferences(rspDataReferenceQuery);
    bool referenceFoundRspWrite = false;
    for (const ReferenceResult& ref : rspDataReferences) {
        referenceFoundRspWrite = referenceFoundRspWrite
            || (ref.referencingSymbolRecord.localHandle == stageDataId
                && ref.referencedSymbolRecord.localHandle == rspDataId
                && ref.relationshipType == SymbolRelationshipEngine::ASSIGNS_TO);
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

    if (previousGlobalSnapshot)
        SemanticIndex::getInstance()->setSnapshot(previousGlobalSnapshot);
    else
        SemanticIndex::getInstance()->clearSnapshot();
}

static void runModuleBriefServiceFixture()
{
    printf("\n-- module brief service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/module_brief_fixture.sv");
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using OwnerScope = SymbolTaxonomy::SymbolOwnerScope;
    using Visibility = SymbolTaxonomy::SymbolVisibility;

    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(QStringLiteral("brief_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9001)
            .withRange(10, 1, 80, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord widthParam =
        SemanticFixtureRecordBuilder(QStringLiteral("WIDTH"),
                                     DeclarationKind::Parameter)
            .withFile(fileName)
            .withLocalHandle(9002)
            .withLine(11)
            .withCollectorKind(CollectorKind::Parameter)
            .inModule(QStringLiteral("brief_top"))
            .record();
    const SemanticSymbolRecord clk =
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9003)
            .withLine(12)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("brief_top"))
            .record();
    const SemanticSymbolRecord rstN =
        SemanticFixtureRecordBuilder(QStringLiteral("rst_n"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9004)
            .withLine(13)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("brief_top"))
            .record();
    const SemanticSymbolRecord dataO =
        SemanticFixtureRecordBuilder(QStringLiteral("data_o"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9005)
            .withLine(14)
            .withCollectorKind(CollectorKind::PortOutput)
            .inModule(QStringLiteral("brief_top"))
            .record();
    const SemanticSymbolRecord stageInst =
        SemanticFixtureRecordBuilder(QStringLiteral("u_stage"),
                                     DeclarationKind::Instance)
            .withFile(fileName)
            .withLocalHandle(9006)
            .withLine(30)
            .withCollectorKind(CollectorKind::Inst)
            .inModule(QStringLiteral("brief_top"))
            .record();
    const SemanticSymbolRecord package =
        SemanticFixtureRecordBuilder(QStringLiteral("brief_pkg"),
                                     DeclarationKind::Package)
            .withFile(fileName)
            .withLocalHandle(9007)
            .withLine(1)
            .withCollectorKind(CollectorKind::Package)
            .record();
    const SemanticSymbolRecord packageWidth =
        SemanticFixtureRecordBuilder(QStringLiteral("PKG_WIDTH"),
                                     DeclarationKind::Parameter)
            .withFile(fileName)
            .withLocalHandle(9012)
            .withLine(2)
            .withCollectorKind(CollectorKind::Parameter)
            .inPackage(QStringLiteral("brief_pkg"))
            .record();
    const SemanticSymbolRecord packageTypedef =
        SemanticFixtureRecordBuilder(QStringLiteral("brief_t"),
                                     DeclarationKind::Typedef)
            .withFile(fileName)
            .withLocalHandle(9013)
            .withLine(4)
            .withCollectorKind(CollectorKind::Typedef)
            .inPackage(QStringLiteral("brief_pkg"))
            .record();
    const SemanticSymbolRecord briefInterface =
        SemanticFixtureRecordBuilder(QStringLiteral("brief_if"),
                                     DeclarationKind::Interface)
            .withFile(fileName)
            .withLocalHandle(9009)
            .withLine(3)
            .withCollectorKind(CollectorKind::Interface)
            .record();
    const SemanticSymbolRecord interfacePort =
        SemanticFixtureRecordBuilder(QStringLiteral("if_port"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9010)
            .withLine(15)
            .withCollectorKind(CollectorKind::PortInterfaceModport)
            .withOwner(OwnerScope::Module,
                       QStringLiteral("brief_top"),
                       {},
                       true)
            .withType(QStringLiteral("brief_if.master"),
                      QStringLiteral("brief_if"),
                      DeclarationKind::Interface,
                      QStringLiteral("master"))
            .record();
    const SemanticSymbolRecord interfaceInstance =
        SemanticFixtureRecordBuilder(QStringLiteral("if_bus"),
                                     DeclarationKind::Instance)
            .withFile(fileName)
            .withLocalHandle(9011)
            .withLine(31)
            .withCollectorKind(CollectorKind::Inst)
            .inModule(QStringLiteral("brief_top"))
            .withType(QStringLiteral("brief_if"),
                      QStringLiteral("brief_if"),
                      DeclarationKind::Interface)
            .record();
    const SemanticSymbolRecord outsidePort =
        SemanticFixtureRecordBuilder(QStringLiteral("outside_port"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9008)
            .withLine(90)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("other_module"))
            .record();
    const QList<SemanticSymbolRecord> records{
        module,
        widthParam,
        clk,
        rstN,
        dataO,
        stageInst,
        package,
        packageWidth,
        packageTypedef,
        briefInterface,
        interfacePort,
        interfaceInstance,
        outsidePort,
    };

    QList<SemanticRelationship> relationships;
    relationships.append(semanticFixtureRelationship(
        module,
        package,
        SymbolRelationshipEngine::REFERENCES,
        RelationshipProvenance::Inferred,
        80,
        QStringLiteral("Package import at line 1")));
    relationships.append(semanticFixtureRelationship(
        module,
        stageInst,
        SymbolRelationshipEngine::INSTANTIATES,
        RelationshipProvenance::Inferred,
        90,
        QStringLiteral("Instance: u_stage at line 30")));
    relationships.append(semanticFixtureRelationship(
        clk,
        module,
        SymbolRelationshipEngine::CLOCKS,
        RelationshipProvenance::Inferred,
        95,
        QStringLiteral("Clock signal clk at line 12")));

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
    index.setSnapshot(sharedSnapshotFromRecords(
        records,
        relationships,
        diagnostics));
    ModuleBriefService service(&index);

    ModuleBriefQuery query;
    query.moduleName = QStringLiteral("brief_top");
    query.fileName = fileName;
    const ModuleBriefReport report = service.buildModuleBrief(query);

    expectBool("module brief found module", report.found, true);
    expectBool("module brief subject name",
               report.moduleDisplayName == QStringLiteral("brief_top"), true);
    expectBool("module brief subject stable key",
               report.moduleStableKey == report.moduleSymbolRecord.stableKey,
               true);
    expectBool("module brief subject semantic record",
               report.moduleSymbolRecord.isValid()
                   && report.moduleSymbolRecord.localHandle
                       == module.localHandle
                   && report.moduleSymbolRecord.stableKey == report.moduleStableKey
                   && report.moduleSymbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && report.moduleSymbolRecord.name == QStringLiteral("brief_top"),
               true);
    ModuleBriefQuery stableModuleBriefQuery;
    stableModuleBriefQuery.moduleStableKey = module.stableKey;
    const ModuleBriefReport stableModuleBriefReport =
        service.buildModuleBrief(stableModuleBriefQuery);
    expectBool("module brief resolves stable module key",
               stableModuleBriefReport.found
                   && stableModuleBriefReport.moduleDisplayName
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
        semanticFixtureMetadata(DeclarationKind::Module,
                                OwnerScope::Global,
                                CollectorKind::Module);
    expectBool("semantic metadata keeps raw module kind",
               moduleMetadata.collectorKind == CollectorKind::Module,
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
               report.moduleSymbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && report.moduleSymbolRecord.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Global
                   && report.moduleSymbolRecord.visibility
                       == Visibility::Global
                   && report.moduleSymbolRecord.collectorKind
                       == CollectorKind::Module,
               true);
    expectBool("taxonomy recognizes module brief port",
               SymbolTaxonomy::isPortDeclaration(
                   semanticFixtureMetadata(DeclarationKind::Port,
                                           OwnerScope::Module,
                                           CollectorKind::PortInput)),
               true);
    const SymbolTaxonomy::SemanticMetadata portMetadata =
        semanticFixtureMetadata(DeclarationKind::Port,
                                OwnerScope::Module,
                                CollectorKind::PortInput);
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
    const QList<SemanticSymbolRecord> snapshotRecords =
        index.snapshot()->getSymbolRecords(fileName);
    SemanticSymbolRecord snapshotPort;
    for (const SemanticSymbolRecord& record : snapshotRecords) {
        if (record.name == QStringLiteral("clk")
            && record.collectorKind == CollectorKind::PortInput) {
            snapshotPort = record;
            break;
        }
    }
    expectBool("snapshot attaches port semantic metadata",
               snapshotPort.isValid()
                   && snapshotPort.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Port
                   && snapshotPort.usageRole
                       == SymbolTaxonomy::SymbolUsageRole::Declaration
                   && snapshotPort.sourceRole
                       == SymbolTaxonomy::SourceRole::DesignSource
                   && snapshotPort.collectorKind == CollectorKind::PortInput,
               true);
    expectBool("taxonomy groups module brief port",
               SymbolTaxonomy::declarationGroup(
                   semanticFixtureMetadata(DeclarationKind::Port,
                                           OwnerScope::Module,
                                           CollectorKind::PortInput))
                   == SymbolTaxonomy::DeclarationGroup::Port,
               true);
    expectBool("taxonomy recognizes module brief parameter",
               SymbolTaxonomy::declarationGroup(
                   semanticFixtureMetadata(DeclarationKind::Parameter,
                                           OwnerScope::Module,
                                           CollectorKind::Parameter))
                   == SymbolTaxonomy::DeclarationGroup::Parameter,
               true);
    expectBool("taxonomy groups module brief parameter",
               SymbolTaxonomy::declarationGroup(
                   semanticFixtureMetadata(DeclarationKind::Parameter,
                                           OwnerScope::Module,
                                           CollectorKind::Parameter))
                   == SymbolTaxonomy::DeclarationGroup::Parameter,
               true);
    expectBool("taxonomy recognizes module brief instance",
               SymbolTaxonomy::isInstanceDeclaration(
                   semanticFixtureMetadata(DeclarationKind::Instance,
                                           OwnerScope::Module,
                                           CollectorKind::Inst)),
               true);
    expectBool("taxonomy groups module brief instance",
               SymbolTaxonomy::declarationGroup(
                   semanticFixtureMetadata(DeclarationKind::Instance,
                                           OwnerScope::Module,
                                           CollectorKind::Inst))
                   == SymbolTaxonomy::DeclarationGroup::Instance,
               true);
    const QList<SemanticSymbolRecord> packageRecords{
        SemanticFixtureRecordBuilder(QStringLiteral("brief_pkg"),
                                     SymbolTaxonomy::DeclarationKind::Package)
            .withFile(fileName)
            .withLocalHandle(100)
            .withLine(1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Package)
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("WIDTH"),
                                     SymbolTaxonomy::DeclarationKind::Parameter)
            .withFile(fileName)
            .withLocalHandle(101)
            .withLine(2)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Parameter)
            .inPackage(QStringLiteral("brief_pkg"))
            .record(),
    };
    const SemanticIndexSnapshot packageSnapshot =
        SemanticIndexSnapshot::fromSymbolRecords(packageRecords);
    const QList<SemanticSymbolRecord> annotatedPackageRecords =
        packageSnapshot.getSymbolRecords(fileName);
    SemanticSymbolRecord packageParameter;
    for (const SemanticSymbolRecord& record : annotatedPackageRecords) {
        if (record.name == QStringLiteral("WIDTH")) {
            packageParameter = record;
            break;
        }
    }
    expectBool("snapshot attaches package visibility metadata",
               packageParameter.isValid()
                   && packageParameter.owner.kind
                       == SymbolTaxonomy::SymbolOwnerScope::Package
                   && packageParameter.visibility
                       == SymbolTaxonomy::SymbolVisibility::PackageVisible,
               true);
    expectInt("module brief port count", report.portRows.size(), 4);
    expectInt("module brief parameter count", report.parameterRows.size(), 1);
    expectInt("module brief instance count", report.instanceRows.size(), 2);
    expectInt("module brief import count", report.importRows.size(), 1);
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
                       == report.portRows.first().symbolStableKey
                   && report.portRows.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Port
                   && report.portRows.first().symbolRecord.name == QStringLiteral("clk")
                   && report.portRows.first().symbolDisplayName == QStringLiteral("clk")
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
                   && report.parameterRows.first().symbolDisplayName
                       == QStringLiteral("WIDTH")
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
               !report.importRows.isEmpty()
                   && report.importRows.first().symbolRecord.name
                          == QStringLiteral("brief_pkg"),
               true);
    expectBool("module brief import row semantic record",
               !report.importRows.isEmpty()
                   && report.importRows.first().symbolRecord.isValid()
                   && report.importRows.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Package
                   && report.importRows.first().symbolRecord.name
                       == QStringLiteral("brief_pkg")
                   && report.importRows.first().symbolDisplayName
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
                && row.type == SymbolRelationshipEngine::INSTANTIATES
                && row.fromStableKey == row.fromSymbolRecord.stableKey
                && row.toStableKey == row.toSymbolRecord.stableKey
                && row.peerStableKey == row.toStableKey
                && row.peerSymbolRecord.isValid()
                && row.peerSymbolRecord.stableKey == row.peerStableKey
                && row.fromSymbolRecord.isValid()
                && row.fromSymbolRecord.stableKey == row.fromStableKey
                && row.toSymbolRecord.isValid()
                && row.toSymbolRecord.stableKey == row.toStableKey
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
                && row.peerSymbolRecord.isValid()
                && row.peerSymbolRecord.name == QStringLiteral("clk")
                && row.fromSymbolRecord.isValid()
                && row.fromSymbolRecord.name == QStringLiteral("clk")
                && row.toSymbolRecord.isValid()
                && row.toSymbolRecord.name == QStringLiteral("brief_top")
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
    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(QStringLiteral("scope_top"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(1)
            .withRange(1, 1, 5, 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    const SemanticSymbolRecord logic =
        SemanticFixtureRecordBuilder(QStringLiteral("enable"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(2)
            .withLine(2)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("scope_top"))
            .record();
    const SemanticSymbolRecord wire =
        SemanticFixtureRecordBuilder(QStringLiteral("raw_wire"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(3)
            .withLine(3)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Wire)
            .inModule(QStringLiteral("scope_top"))
            .record();
    const QList<SemanticSymbolRecord> records{module, logic, wire};

    QHash<QString, QString> fileContents;
    fileContents.insert(
        fileName,
        QStringLiteral("module scope_top;\nlogic enable;\nwire raw_wire;\nendmodule\n"));
    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        records,
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
                       == module.localHandle
                   && report.modules.first().symbolRecord.stableKey
                       == report.modules.first().symbolStableKey
                   && report.modules.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Module
                   && report.modules.first().symbolRecord.name
                       == QStringLiteral("scope_top")
                   && report.modules.first().symbolDisplayName
                       == QStringLiteral("scope_top")
                   && report.modules.first().symbolTypeDisplayName
                       == QStringLiteral("module")
                   && report.modules.first().sourceRoleDisplayName
                       == QStringLiteral("design source")
                   && report.modules.first().codeLink.fileName == fileName
                   && report.modules.first().codeLink.line == 1
                   && report.modules.first().codeLink.fileDisplayName
                       == QStringLiteral("scope_band_fixture.sv")
                   && report.modules.first().codeLink.lineDisplayName
                       == QStringLiteral("1")
                   && report.modules.first().endLine
                       >= module.location.startLine,
               true);
    expectInt("scope band logic count", report.logics.size(), 1);
    expectBool("scope band logic metadata",
               !report.logics.isEmpty()
                   && report.logics.first().symbolRecord.isValid()
                   && report.logics.first().symbolRecord.localHandle
                       == logic.localHandle
                   && report.logics.first().symbolRecord.stableKey
                       == report.logics.first().symbolStableKey
                   && report.logics.first().symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal
                   && report.logics.first().symbolRecord.name
                       == QStringLiteral("enable")
                   && report.logics.first().symbolDisplayName
                       == QStringLiteral("enable")
                   && report.logics.first().symbolTypeDisplayName
                       == QStringLiteral("logic")
                   && report.logics.first().sourceRoleDisplayName
                       == QStringLiteral("design source")
                   && report.logics.first().codeLink.fileName == fileName
                   && report.logics.first().codeLink.line == 2
                   && report.logics.first().codeLink.fileDisplayName
                       == QStringLiteral("scope_band_fixture.sv")
                   && report.logics.first().codeLink.lineDisplayName
                       == QStringLiteral("2")
                   && report.logics.first().endLine == 2,
               true);
}

static void runSignalJourneyServiceFixture()
{
    printf("\n-- signal journey service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/signal_journey_fixture.sv");
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using OwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(QStringLiteral("journey_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9101)
            .withRange(1, 1, 80, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord dataQ =
        SemanticFixtureRecordBuilder(QStringLiteral("data_q"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9102)
            .withLine(10)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("journey_top"))
            .record();
    const SemanticSymbolRecord nextData =
        SemanticFixtureRecordBuilder(QStringLiteral("next_data"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9103)
            .withLine(20)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("journey_top"))
            .record();
    const SemanticSymbolRecord consumer =
        SemanticFixtureRecordBuilder(QStringLiteral("consumer"),
                                     DeclarationKind::Process)
            .withFile(fileName)
            .withLocalHandle(9104)
            .withLine(30)
            .withCollectorKind(CollectorKind::AlwaysFf)
            .inModule(QStringLiteral("journey_top"))
            .record();
    const SemanticSymbolRecord stageDataPin =
        SemanticFixtureRecordBuilder(QStringLiteral("u_stage.data_i"),
                                     DeclarationKind::Instance)
            .withFile(fileName)
            .withLocalHandle(9105)
            .withLine(40)
            .withCollectorKind(CollectorKind::InstPin)
            .inModule(QStringLiteral("journey_top"))
            .record();
    const SemanticSymbolRecord journeyIf =
        SemanticFixtureRecordBuilder(QStringLiteral("journey_if"),
                                     DeclarationKind::Interface)
            .withFile(fileName)
            .withLocalHandle(9106)
            .withLine(45)
            .withCollectorKind(CollectorKind::Interface)
            .record();
    const SemanticSymbolRecord interfaceInstance =
        SemanticFixtureRecordBuilder(QStringLiteral("if_bus"),
                                     DeclarationKind::Instance)
            .withFile(fileName)
            .withLocalHandle(9107)
            .withLine(50)
            .withCollectorKind(CollectorKind::Inst)
            .inModule(QStringLiteral("journey_top"))
            .withType(QStringLiteral("journey_if"),
                      QStringLiteral("journey_if"),
                      DeclarationKind::Interface)
            .record();
    const SemanticSymbolRecord ready =
        SemanticFixtureRecordBuilder(QStringLiteral("ready"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9108)
            .withLine(55)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("journey_if"))
            .record();
    const SemanticSymbolRecord clk =
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9109)
            .withLine(60)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("journey_top"))
            .record();
    const SemanticSymbolRecord rstN =
        SemanticFixtureRecordBuilder(QStringLiteral("rst_n"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9110)
            .withLine(61)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("journey_top"))
            .record();
    const QList<SemanticSymbolRecord> records{
        module,
        dataQ,
        nextData,
        consumer,
        stageDataPin,
        journeyIf,
        interfaceInstance,
        ready,
        clk,
        rstN,
    };
    auto evidenceRange = [&fileName](int line, int column, int endColumn) {
        SemanticSourceRange range;
        range.fileName = fileName;
        range.line = line;
        range.column = column;
        range.endLine = line;
        range.endColumn = endColumn;
        return range;
    };

    QList<SemanticRelationship> relationships;
    relationships.append(semanticFixtureRelationship(
        nextData,
        dataQ,
        SymbolRelationshipEngine::ASSIGNS_TO,
        RelationshipProvenance::Inferred,
        85,
        QStringLiteral("Assigned to data_q at line 20"),
        evidenceRange(20, 12, 28)));
    relationships.append(semanticFixtureRelationship(
        consumer,
        dataQ,
        SymbolRelationshipEngine::READS_FROM,
        RelationshipProvenance::Inferred,
        80,
        QStringLiteral("Read data_q at line 30"),
        evidenceRange(30, 18, 24)));
    relationships.append(semanticFixtureRelationship(
        stageDataPin,
        dataQ,
        SymbolRelationshipEngine::REFERENCES,
        RelationshipProvenance::Inferred,
        100,
        QStringLiteral("u_stage.data_i(data_q)"),
        evidenceRange(40, 22, 28)));
    relationships.append(semanticFixtureRelationship(
        interfaceInstance,
        dataQ,
        SymbolRelationshipEngine::REFERENCES,
        RelationshipProvenance::Inferred,
        100,
        QStringLiteral("if_bus drives data_q"),
        evidenceRange(50, 9, 15)));
    relationships.append(semanticFixtureRelationship(
        dataQ,
        ready,
        SymbolRelationshipEngine::REFERENCES,
        RelationshipProvenance::Inferred,
        100,
        QStringLiteral("data_q feeds interface ready"),
        evidenceRange(55, 5, 11)));
    relationships.append(semanticFixtureRelationship(
        clk,
        module,
        SymbolRelationshipEngine::CLOCKS));
    relationships.append(semanticFixtureRelationship(
        rstN,
        module,
        SymbolRelationshipEngine::RESETS));

    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        records,
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
               report.declarationDisplayName == QStringLiteral("data_q"),
               true);
    expectBool("signal journey declaration stable key",
               report.declarationStableKey
                   == report.declarationSymbolRecord.stableKey,
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
    stableSignalQuery.signalStableKey = report.declarationStableKey;
    stableSignalQuery.signalName = QStringLiteral("consumer");
    const SignalJourneyReport stableSignalReport =
        service.buildSignalJourney(stableSignalQuery);
    expectBool("signal journey resolves stable signal key",
               stableSignalReport.found
                   && stableSignalReport.declarationDisplayName
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
               report.declarationSymbolRecord.declarationKind
                   == SymbolTaxonomy::DeclarationKind::Signal,
               true);
    expectBool("taxonomy recognizes signal journey port peer",
               SymbolTaxonomy::isPortConnectionPeer(
                   semanticFixtureMetadata(DeclarationKind::Instance,
                                           OwnerScope::Module,
                                           CollectorKind::InstPin)),
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
                   && report.assignments.first().peerSymbolDisplayName
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
                       == report.assignments.first().fromSymbolRecord.stableKey
                   && report.assignments.first().toStableKey
                       == report.assignments.first().toSymbolRecord.stableKey
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
                   && report.assignments.first().relationshipType
                       == SymbolRelationshipEngine::ASSIGNS_TO
                   && report.assignments.first().evidenceRange.line == 20
                   && report.assignments.first().evidenceRange.column == 12
                   && report.assignments.first().evidenceCodeLink.fileName == fileName
                   && report.assignments.first().evidenceCodeLink.line == 20
                   && report.assignments.first().evidenceCodeLink.column == 12
                   && report.assignments.first().provenanceDisplayName
                       == QStringLiteral("inferred")
                   && report.assignments.first().confidenceDisplayName
                       == QStringLiteral("85%")
                   && report.assignments.first().evidenceDisplayName.contains(
                       QStringLiteral("Assigned to data_q")),
               true);
    expectBool("signal journey read peer",
               !report.reads.isEmpty()
                   && report.reads.first().peerSymbolDisplayName
                       == QStringLiteral("consumer")
                   && report.reads.first().detailDisplayName
                       == QStringLiteral("incoming Reads From"),
               true);
    expectBool("signal journey port peer",
               !report.portConnections.isEmpty()
                   && report.portConnections.first().peerSymbolDisplayName
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

    SignalKernelGraphService graphService(&index);
    SignalKernelGraphQuery graphQuery;
    graphQuery.signalName = QStringLiteral("data_q");
    graphQuery.fileName = fileName;
    graphQuery.moduleName = QStringLiteral("journey_top");
    const SignalKernelGraphReport graphReport =
        graphService.buildSignalKernelGraph(graphQuery);

    const SignalKernelGraphNode* nextDataInput = nullptr;
    const SignalKernelGraphNode* interfaceInput = nullptr;
    const SignalKernelGraphNode* stageOutput = nullptr;
    const SignalKernelGraphNode* readyOutput = nullptr;
    for (const SignalKernelGraphNode& node : graphReport.inputs) {
        if (node.displayName == QStringLiteral("next_data"))
            nextDataInput = &node;
        if (node.displayName == QStringLiteral("if_bus"))
            interfaceInput = &node;
    }
    for (const SignalKernelGraphNode& node : graphReport.outputs) {
        if (node.displayName == QStringLiteral("u_stage.data_i"))
            stageOutput = &node;
        if (node.displayName == QStringLiteral("ready"))
            readyOutput = &node;
    }

    bool sawInputEdge = false;
    bool sawOutputEdge = false;
    for (const SignalKernelGraphEdge& edge : graphReport.edges) {
        if (nextDataInput
            && edge.fromNodeId == nextDataInput->id
            && edge.toNodeId == graphReport.kernel.id
            && edge.label == QStringLiteral("Assigns To")) {
            sawInputEdge = true;
        }
        if (stageOutput
            && edge.fromNodeId == graphReport.kernel.id
            && edge.toNodeId == stageOutput->id
            && edge.label == QStringLiteral("References")) {
            sawOutputEdge = true;
        }
    }

    bool sawJourneyInterfaceOutputGroup = false;
    for (const SignalKernelGraphModuleGroup& group
         : graphReport.outputModuleGroups) {
        sawJourneyInterfaceOutputGroup =
            sawJourneyInterfaceOutputGroup
            || (group.moduleName == QStringLiteral("journey_if")
                && group.crossModule
                && readyOutput
                && group.nodeIds.contains(readyOutput->id));
    }

    expectBool("signal kernel graph found",
               graphReport.found
                   && graphReport.kernel.displayName == QStringLiteral("data_q")
                   && graphReport.kernel.role == SignalKernelGraphNodeRole::Kernel
                   && graphReport.kernel.navigateCodeLink.fileName == fileName
                   && graphReport.kernel.navigateCodeLink.line == 10
                   && graphReport.kernel.preciseEvidence,
               true);
    expectBool("signal kernel graph node directions",
               nextDataInput
                   && interfaceInput
                   && stageOutput
                   && readyOutput
                   && graphReport.inputs.size() == 2
                   && graphReport.outputs.size() == 2,
               true);
    expectBool("signal kernel graph data input lanes",
               nextDataInput
                   && nextDataInput->inputLane
                          == SignalKernelGraphInputLane::Data
                   && interfaceInput
                   && interfaceInput->inputLane
                          == SignalKernelGraphInputLane::Data,
               true);
    expectBool("signal kernel graph precise input evidence",
               nextDataInput
                   && nextDataInput->preciseEvidence
                   && nextDataInput->previewCodeLink.fileName == fileName
                   && nextDataInput->previewCodeLink.line == 20
                   && nextDataInput->previewCodeLink.column == 12
                   && nextDataInput->navigateCodeLink.line == 20
                   && nextDataInput->evidenceRange.endColumn == 28,
               true);
    expectBool("signal kernel graph precise output evidence",
               stageOutput
                   && stageOutput->preciseEvidence
                   && stageOutput->previewCodeLink.fileName == fileName
                   && stageOutput->previewCodeLink.line == 40
                   && stageOutput->previewCodeLink.column == 22
                   && stageOutput->navigateCodeLink.line == 40,
               true);
    expectBool("signal kernel graph port input is output consumer",
               stageOutput
                   && stageOutput->role == SignalKernelGraphNodeRole::Output
                   && stageOutput->previewCodeLink.line == 40,
               true);
    expectBool("signal kernel graph cross module output group",
               readyOutput
                   && readyOutput->crossModule
                   && readyOutput->moduleDisplayName
                       == QStringLiteral("journey_if")
                   && sawJourneyInterfaceOutputGroup,
               true);
    expectBool("signal kernel graph edge directions",
               sawInputEdge && sawOutputEdge && graphReport.edges.size() == 4,
               true);
    expectBool("signal kernel graph normal fanout ungrouped",
               graphReport.fanoutGroupingThreshold > 0
                   && graphReport.inputFanoutGroups.isEmpty()
                   && graphReport.outputFanoutGroups.isEmpty(),
               true);

    const QString fanoutFileName =
        QStringLiteral("test_sv/signal_kernel_fanout_fixture.sv");
    const SemanticSymbolRecord fanoutModule =
        SemanticFixtureRecordBuilder(QStringLiteral("fanout_top"),
                                     DeclarationKind::Module)
            .withFile(fanoutFileName)
            .withLocalHandle(9300)
            .withRange(1, 1, 80, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord fanoutKernel =
        SemanticFixtureRecordBuilder(QStringLiteral("fanout_sig"),
                                     DeclarationKind::Signal)
            .withFile(fanoutFileName)
            .withLocalHandle(9301)
            .withLine(10)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("fanout_top"))
            .withType(QStringLiteral("logic"))
            .record();
    QList<SemanticSymbolRecord> fanoutRecords{fanoutModule, fanoutKernel};
    QList<SemanticRelationship> fanoutRelationships;
    for (int i = 0; i < 6; ++i) {
        const QString readerName =
            QStringLiteral("reader_%1").arg(i + 1);
        const SemanticSymbolRecord reader =
            SemanticFixtureRecordBuilder(readerName, DeclarationKind::Signal)
                .withFile(fanoutFileName)
                .withLocalHandle(9310 + i)
                .withLine(20 + i)
                .withCollectorKind(CollectorKind::Logic)
                .inModule(QStringLiteral("fanout_top"))
                .withType(QStringLiteral("logic"))
                .record();
        fanoutRecords.append(reader);
        SemanticSourceRange evidence;
        evidence.fileName = fanoutFileName;
        evidence.line = 40 + i;
        evidence.column = 8;
        evidence.endLine = 40 + i;
        evidence.endColumn = 18;
        fanoutRelationships.append(
            semanticFixtureRelationship(
                reader,
                fanoutKernel,
                SymbolRelationshipEngine::READS_FROM,
                RelationshipProvenance::Inferred,
                90,
                QStringLiteral("reader fanout"),
                evidence));
    }

    SemanticIndex fanoutIndex;
    fanoutIndex.setSnapshot(sharedSnapshotFromRecords(
        fanoutRecords,
        fanoutRelationships));
    SignalKernelGraphService fanoutGraphService(&fanoutIndex);
    SignalKernelGraphQuery fanoutGraphQuery;
    fanoutGraphQuery.signalName = QStringLiteral("fanout_sig");
    fanoutGraphQuery.fileName = fanoutFileName;
    fanoutGraphQuery.moduleName = QStringLiteral("fanout_top");
    const SignalKernelGraphReport fanoutGraph =
        fanoutGraphService.buildSignalKernelGraph(fanoutGraphQuery);
    const SignalKernelGraphFanoutGroup outputFanoutGroup =
        fanoutGraph.outputFanoutGroups.isEmpty()
            ? SignalKernelGraphFanoutGroup{}
            : fanoutGraph.outputFanoutGroups.first();
    bool fanoutGroupContainsAllOutputs = true;
    for (const SignalKernelGraphNode& node : fanoutGraph.outputs)
        fanoutGroupContainsAllOutputs =
            fanoutGroupContainsAllOutputs
            && outputFanoutGroup.nodeIds.contains(node.id);
    expectBool("signal kernel graph high fanout grouping report",
               fanoutGraph.found
                   && fanoutGraph.outputs.size() == 6
                   && fanoutGraph.outputFanoutGroups.size() == 1
                   && fanoutGraph.inputFanoutGroups.isEmpty()
                   && outputFanoutGroup.highFanout
                   && outputFanoutGroup.role
                       == SignalKernelGraphNodeRole::Output
                   && outputFanoutGroup.moduleName
                       == QStringLiteral("fanout_top")
                   && outputFanoutGroup.nodeCount == 6
                   && outputFanoutGroup.totalRoleNodeCount == 6
                   && outputFanoutGroup.nodeIds.size() == 6
                   && outputFanoutGroup.displayName
                       == QStringLiteral("Outputs in fanout_top")
                   && fanoutGroupContainsAllOutputs,
               true);

    const SemanticSymbolRecord packetType =
        SemanticFixtureRecordBuilder(QStringLiteral("packet_t"),
                                     DeclarationKind::Struct)
            .withFile(fileName)
            .withLocalHandle(9121)
            .withLine(64)
            .withCollectorKind(CollectorKind::PackedStruct)
            .record();
    const SemanticSymbolRecord payloadMember =
        SemanticFixtureRecordBuilder(QStringLiteral("payload"),
                                     DeclarationKind::StructMember)
            .withFile(fileName)
            .withLocalHandle(9122)
            .withLine(65)
            .withCollectorKind(CollectorKind::StructMember)
            .inStruct(QStringLiteral("packet_t"))
            .withType(QStringLiteral("logic [7:0]"))
            .record();
    const SemanticSymbolRecord busStruct =
        SemanticFixtureRecordBuilder(QStringLiteral("bus"),
                                     DeclarationKind::StructVariable)
            .withFile(fileName)
            .withLocalHandle(9123)
            .withLine(70)
            .withCollectorKind(CollectorKind::PackedStructVariable)
            .inModule(QStringLiteral("journey_top"))
            .withType(QStringLiteral("packet_t"),
                      QStringLiteral("packet_t"),
                      DeclarationKind::Struct)
            .record();
    const SemanticSymbolRecord payloadSrc =
        SemanticFixtureRecordBuilder(QStringLiteral("payload_src"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9124)
            .withLine(75)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("journey_top"))
            .record();
    const SemanticSymbolRecord payloadSink =
        SemanticFixtureRecordBuilder(QStringLiteral("payload_sink"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9125)
            .withLine(76)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("journey_top"))
            .record();
    QList<SemanticRelationship> memberRelationships;
    SemanticRelationship payloadInput = semanticFixtureRelationship(
        payloadSrc,
        busStruct,
        SymbolRelationshipEngine::ASSIGNS_TO,
        RelationshipProvenance::Inferred,
        85,
        QStringLiteral("Assigned to bus.payload at line 75"),
        evidenceRange(75, 9, 30));
    payloadInput.fromAccessPath = QStringLiteral("payload_src");
    payloadInput.toAccessPath = QStringLiteral("bus.payload");
    memberRelationships.append(payloadInput);
    SemanticRelationship payloadOutput = semanticFixtureRelationship(
        busStruct,
        payloadSink,
        SymbolRelationshipEngine::ASSIGNS_TO,
        RelationshipProvenance::Inferred,
        85,
        QStringLiteral("Assigned to payload_sink at line 76"),
        evidenceRange(76, 18, 29));
    payloadOutput.fromAccessPath = QStringLiteral("bus.payload");
    payloadOutput.toAccessPath = QStringLiteral("payload_sink");
    memberRelationships.append(payloadOutput);

    SemanticIndex memberIndex;
    memberIndex.setSnapshot(sharedSnapshotFromRecords(
        {module, packetType, payloadMember, busStruct, payloadSrc, payloadSink},
        memberRelationships,
        QList<SemanticDiagnostic>()));
    SignalJourneyService memberJourneyService(&memberIndex);
    SignalJourneyQuery memberJourneyQuery;
    memberJourneyQuery.signalName = QStringLiteral("bus");
    memberJourneyQuery.signalAccessPath = QStringLiteral("bus.payload");
    memberJourneyQuery.fileName = fileName;
    memberJourneyQuery.moduleName = QStringLiteral("journey_top");
    const SignalJourneyReport memberJourney =
        memberJourneyService.buildSignalJourney(memberJourneyQuery);
    expectBool("signal journey struct member found",
               memberJourney.found
                   && memberJourney.declarationDisplayName
                       == QStringLiteral("bus.payload")
                   && memberJourney.declarationCodeLink.line == 65,
               true);
    expectBool("signal journey struct member directions",
               memberJourney.assignments.size() == 1
                   && memberJourney.drivenAssignments.size() == 1
                   && memberJourney.assignments.first().peerSymbolDisplayName
                       == QStringLiteral("payload_src")
                   && memberJourney.drivenAssignments.first().peerSymbolDisplayName
                       == QStringLiteral("payload_sink"),
               true);

    SignalKernelGraphService memberGraphService(&memberIndex);
    SignalKernelGraphQuery memberGraphQuery;
    memberGraphQuery.signalName = QStringLiteral("bus");
    memberGraphQuery.signalAccessPath = QStringLiteral("bus.payload");
    memberGraphQuery.fileName = fileName;
    memberGraphQuery.moduleName = QStringLiteral("journey_top");
    const SignalKernelGraphReport memberGraph =
        memberGraphService.buildSignalKernelGraph(memberGraphQuery);
    const bool memberGraphHasInput =
        !memberGraph.inputs.isEmpty()
        && memberGraph.inputs.first().displayName
            == QStringLiteral("payload_src");
    const bool memberGraphHasOutput =
        !memberGraph.outputs.isEmpty()
        && memberGraph.outputs.first().displayName
            == QStringLiteral("payload_sink");
    expectBool("signal kernel graph struct member",
               memberGraph.found
                   && memberGraph.kernel.displayName
                       == QStringLiteral("bus.payload")
                   && memberGraph.kernel.navigateCodeLink.line == 65
                   && memberGraphHasInput
                   && memberGraphHasOutput
                   && memberGraph.inputs.size() == 1
                   && memberGraph.outputs.size() == 1
                   && memberGraph.edges.size() == 2,
               true);

    const QString endpointFileName =
        QStringLiteral("test_sv/signal_kernel_endpoint_fixture.sv");
    const SemanticSymbolRecord endpointModule =
        SemanticFixtureRecordBuilder(QStringLiteral("endpoint_top"),
                                     DeclarationKind::Module)
            .withFile(endpointFileName)
            .withLocalHandle(9400)
            .withLine(1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord endpointInterface =
        SemanticFixtureRecordBuilder(QStringLiteral("endpoint_if"),
                                     DeclarationKind::Interface)
            .withFile(endpointFileName)
            .withLocalHandle(9401)
            .withLine(2)
            .withCollectorKind(CollectorKind::Interface)
            .record();
    const SemanticSymbolRecord endpointPackage =
        SemanticFixtureRecordBuilder(QStringLiteral("endpoint_pkg"),
                                     DeclarationKind::Package)
            .withFile(endpointFileName)
            .withLocalHandle(9402)
            .withLine(3)
            .withCollectorKind(CollectorKind::Package)
            .record();
    const SemanticSymbolRecord regDriver =
        SemanticFixtureRecordBuilder(QStringLiteral("reg_driver"),
                                     DeclarationKind::Signal)
            .withFile(endpointFileName)
            .withLocalHandle(9403)
            .withLine(10)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("endpoint_top"))
            .withType(QStringLiteral("logic"))
            .record();
    const SemanticSymbolRecord activeOut =
        SemanticFixtureRecordBuilder(QStringLiteral("active_out"),
                                     DeclarationKind::Port)
            .withFile(endpointFileName)
            .withLocalHandle(9404)
            .withLine(11)
            .withCollectorKind(CollectorKind::PortOutput)
            .inModule(QStringLiteral("endpoint_top"))
            .withType(QStringLiteral("logic"))
            .record();
    const SemanticSymbolRecord enumType =
        SemanticFixtureRecordBuilder(QStringLiteral("state_e"),
                                     DeclarationKind::Enum)
            .withFile(endpointFileName)
            .withLocalHandle(9405)
            .withLine(12)
            .withCollectorKind(CollectorKind::Enum)
            .record();
    const SemanticSymbolRecord enumValue =
        SemanticFixtureRecordBuilder(QStringLiteral("E_IDLE"),
                                     DeclarationKind::User)
            .withFile(endpointFileName)
            .withLocalHandle(9406)
            .withLine(13)
            .withCollectorKind(CollectorKind::EnumValue)
            .inPackage(QStringLiteral("endpoint_pkg"))
            .record();
    const SemanticSymbolRecord enumVar =
        SemanticFixtureRecordBuilder(QStringLiteral("mcs"),
                                     DeclarationKind::Signal)
            .withFile(endpointFileName)
            .withLocalHandle(9407)
            .withLine(14)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("endpoint_top"))
            .withType(QStringLiteral("state_e"),
                      QStringLiteral("state_e"),
                      DeclarationKind::Enum)
            .record();
    const SemanticSymbolRecord enumSrc =
        SemanticFixtureRecordBuilder(QStringLiteral("mns"),
                                     DeclarationKind::Signal)
            .withFile(endpointFileName)
            .withLocalHandle(9408)
            .withLine(15)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("endpoint_top"))
            .withType(QStringLiteral("state_e"),
                      QStringLiteral("state_e"),
                      DeclarationKind::Enum)
            .record();
    const SemanticSymbolRecord packetVar =
        SemanticFixtureRecordBuilder(QStringLiteral("packet_q"),
                                     DeclarationKind::StructVariable)
            .withFile(endpointFileName)
            .withLocalHandle(9409)
            .withLine(16)
            .withCollectorKind(CollectorKind::PackedStructVariable)
            .inModule(QStringLiteral("endpoint_top"))
            .withType(QStringLiteral("packet_t"),
                      QStringLiteral("packet_t"),
                      DeclarationKind::Struct)
            .record();
    const SemanticSymbolRecord endpointSink =
        SemanticFixtureRecordBuilder(QStringLiteral("endpoint_sink"),
                                     DeclarationKind::Signal)
            .withFile(endpointFileName)
            .withLocalHandle(9410)
            .withLine(17)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("endpoint_top"))
            .record();
    QList<SemanticRelationship> endpointRelationships;
    endpointRelationships.append(semanticFixtureRelationship(
        regDriver,
        activeOut,
        SymbolRelationshipEngine::ASSIGNS_TO));
    endpointRelationships.append(semanticFixtureRelationship(
        enumValue,
        enumVar,
        SymbolRelationshipEngine::ASSIGNS_TO));
    endpointRelationships.append(semanticFixtureRelationship(
        enumSrc,
        enumVar,
        SymbolRelationshipEngine::ASSIGNS_TO));
    endpointRelationships.append(semanticFixtureRelationship(
        packetVar,
        enumVar,
        SymbolRelationshipEngine::ASSIGNS_TO));
    endpointRelationships.append(semanticFixtureRelationship(
        enumVar,
        endpointSink,
        SymbolRelationshipEngine::ASSIGNS_TO));
    endpointRelationships.append(semanticFixtureRelationship(
        endpointPackage,
        enumVar,
        SymbolRelationshipEngine::ASSIGNS_TO));
    endpointRelationships.append(semanticFixtureRelationship(
        enumVar,
        endpointModule,
        SymbolRelationshipEngine::ASSIGNS_TO));
    endpointRelationships.append(semanticFixtureRelationship(
        enumVar,
        endpointInterface,
        SymbolRelationshipEngine::READS_FROM));

    SemanticIndex endpointIndex;
    endpointIndex.setSnapshot(sharedSnapshotFromRecords(
        {endpointModule,
         endpointInterface,
         endpointPackage,
         regDriver,
         activeOut,
         enumType,
         enumValue,
         enumVar,
         enumSrc,
         packetVar,
         endpointSink},
        endpointRelationships,
        QList<SemanticDiagnostic>()));
    SignalKernelGraphService endpointGraphService(&endpointIndex);

    SignalKernelGraphQuery outputPortGraphQuery;
    outputPortGraphQuery.signalName = QStringLiteral("reg_driver");
    outputPortGraphQuery.fileName = endpointFileName;
    outputPortGraphQuery.moduleName = QStringLiteral("endpoint_top");
    const SignalKernelGraphReport outputPortGraph =
        endpointGraphService.buildSignalKernelGraph(outputPortGraphQuery);
    bool activeOutInInputs = false;
    bool activeOutInOutputs = false;
    for (const SignalKernelGraphNode& node : outputPortGraph.inputs)
        activeOutInInputs = activeOutInInputs
            || node.displayName == QStringLiteral("active_out");
    for (const SignalKernelGraphNode& node : outputPortGraph.outputs)
        activeOutInOutputs = activeOutInOutputs
            || node.displayName == QStringLiteral("active_out");
    expectBool("signal kernel graph output port is output endpoint",
               outputPortGraph.found
                   && activeOutInOutputs
                   && !activeOutInInputs,
               true);

    SignalKernelGraphQuery enumGraphQuery;
    enumGraphQuery.signalName = QStringLiteral("mcs");
    enumGraphQuery.fileName = endpointFileName;
    enumGraphQuery.moduleName = QStringLiteral("endpoint_top");
    const SignalKernelGraphReport enumGraph =
        endpointGraphService.buildSignalKernelGraph(enumGraphQuery);
    QSet<QString> enumInputNames;
    QSet<QString> enumOutputNames;
    for (const SignalKernelGraphNode& node : enumGraph.inputs)
        enumInputNames.insert(node.displayName);
    for (const SignalKernelGraphNode& node : enumGraph.outputs)
        enumOutputNames.insert(node.displayName);
    expectBool("signal kernel graph filters enum values",
               enumGraph.found
                   && enumInputNames.contains(QStringLiteral("mns"))
                   && enumInputNames.contains(QStringLiteral("packet_q"))
                   && enumOutputNames.contains(QStringLiteral("endpoint_sink"))
                   && !enumInputNames.contains(QStringLiteral("E_IDLE"))
                   && !enumOutputNames.contains(QStringLiteral("E_IDLE")),
               true);
    expectBool("signal kernel graph filters container endpoints",
               !enumInputNames.contains(QStringLiteral("endpoint_pkg"))
                   && !enumInputNames.contains(QStringLiteral("endpoint_top"))
                   && !enumInputNames.contains(QStringLiteral("endpoint_if"))
                   && !enumOutputNames.contains(QStringLiteral("endpoint_pkg"))
                   && !enumOutputNames.contains(QStringLiteral("endpoint_top"))
                   && !enumOutputNames.contains(QStringLiteral("endpoint_if")),
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

static void runSignalUsageHotspotServiceFixture()
{
    printf("\n-- signal usage hotspot service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/signal_hotspot_fixture.sv");
    const QString auxFileName =
        QStringLiteral("test_sv/signal_hotspot_aux_fixture.sv");
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    auto evidenceRange = [](const QString& fileName,
                            int line,
                            int column,
                            int endColumn) {
        SemanticSourceRange range;
        range.fileName = fileName;
        range.line = line;
        range.column = column;
        range.endLine = line;
        range.endColumn = endColumn;
        return range;
    };

    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(QStringLiteral("hotspot_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9500)
            .withRange(1, 1, 90, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord auxModule =
        SemanticFixtureRecordBuilder(QStringLiteral("hotspot_aux"),
                                     DeclarationKind::Module)
            .withFile(auxFileName)
            .withLocalHandle(9501)
            .withRange(1, 1, 30, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord hotSig =
        SemanticFixtureRecordBuilder(QStringLiteral("hot_sig"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9502)
            .withLine(5)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    const SemanticSymbolRecord writer =
        SemanticFixtureRecordBuilder(QStringLiteral("src_data"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9503)
            .withLine(11)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    const SemanticSymbolRecord reader =
        SemanticFixtureRecordBuilder(QStringLiteral("reader_proc"),
                                     DeclarationKind::Process)
            .withFile(fileName)
            .withLocalHandle(9504)
            .withLine(20)
            .withCollectorKind(CollectorKind::AlwaysComb)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    const SemanticSymbolRecord portPin =
        SemanticFixtureRecordBuilder(QStringLiteral("u_child.data_i"),
                                     DeclarationKind::Instance)
            .withFile(fileName)
            .withLocalHandle(9505)
            .withLine(30)
            .withCollectorKind(CollectorKind::InstPin)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    const SemanticSymbolRecord conditionProc =
        SemanticFixtureRecordBuilder(QStringLiteral("condition_proc"),
                                     DeclarationKind::Process)
            .withFile(fileName)
            .withLocalHandle(9506)
            .withLine(40)
            .withCollectorKind(CollectorKind::AlwaysComb)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    const SemanticSymbolRecord caseProc =
        SemanticFixtureRecordBuilder(QStringLiteral("case_proc"),
                                     DeclarationKind::Process)
            .withFile(fileName)
            .withLocalHandle(9507)
            .withLine(50)
            .withCollectorKind(CollectorKind::AlwaysComb)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    const SemanticSymbolRecord constraintProc =
        SemanticFixtureRecordBuilder(QStringLiteral("constraint_proc"),
                                     DeclarationKind::Constraint)
            .withFile(fileName)
            .withLocalHandle(9508)
            .withLine(70)
            .withCollectorKind(CollectorKind::XilinxConstraint)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    const SemanticSymbolRecord auxReader =
        SemanticFixtureRecordBuilder(QStringLiteral("aux_reader"),
                                     DeclarationKind::Process)
            .withFile(auxFileName)
            .withLocalHandle(9509)
            .withLine(8)
            .withCollectorKind(CollectorKind::AlwaysComb)
            .inModule(QStringLiteral("hotspot_aux"))
            .record();

    QList<SemanticRelationship> relationships;
    relationships.append(semanticFixtureRelationship(
        writer,
        hotSig,
        SymbolRelationshipEngine::ASSIGNS_TO,
        RelationshipProvenance::Inferred,
        90,
        QStringLiteral("assign hot_sig = src_data"),
        evidenceRange(fileName, 12, 10, 30)));
    relationships.append(semanticFixtureRelationship(
        reader,
        hotSig,
        SymbolRelationshipEngine::READS_FROM,
        RelationshipProvenance::Inferred,
        80,
        QStringLiteral("reader consumes hot_sig"),
        evidenceRange(fileName, 20, 18, 25)));
    relationships.append(semanticFixtureRelationship(
        portPin,
        hotSig,
        SymbolRelationshipEngine::REFERENCES,
        RelationshipProvenance::Inferred,
        100,
        QStringLiteral("u_child.data_i(hot_sig)"),
        evidenceRange(fileName, 30, 22, 29)));
    relationships.append(semanticFixtureRelationship(
        conditionProc,
        hotSig,
        SymbolRelationshipEngine::READS_FROM,
        RelationshipProvenance::Inferred,
        85,
        QStringLiteral("if condition guard uses hot_sig"),
        evidenceRange(fileName, 40, 9, 16)));
    relationships.append(semanticFixtureRelationship(
        caseProc,
        hotSig,
        SymbolRelationshipEngine::READS_FROM,
        RelationshipProvenance::Inferred,
        85,
        QStringLiteral("case selector hot_sig"),
        evidenceRange(fileName, 50, 11, 18)));
    relationships.append(semanticFixtureRelationship(
        hotSig,
        module,
        SymbolRelationshipEngine::CLOCKS,
        RelationshipProvenance::Inferred,
        100,
        QStringLiteral("posedge hot_sig"),
        evidenceRange(fileName, 60, 19, 26)));
    relationships.append(semanticFixtureRelationship(
        constraintProc,
        hotSig,
        SymbolRelationshipEngine::CONSTRAINS,
        RelationshipProvenance::Inferred,
        60,
        QStringLiteral("constraint evidence for hot_sig"),
        evidenceRange(fileName, 70, 7, 14)));
    relationships.append(semanticFixtureRelationship(
        auxReader,
        hotSig,
        SymbolRelationshipEngine::READS_FROM,
        RelationshipProvenance::Workspace,
        75,
        QStringLiteral("aux module reads hot_sig"),
        evidenceRange(auxFileName, 8, 12, 19)));

    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        {module,
         auxModule,
         hotSig,
         writer,
         reader,
         portPin,
         conditionProc,
         caseProc,
         constraintProc,
         auxReader},
        relationships,
        QList<SemanticDiagnostic>()));
    SignalUsageHotspotService service(&index);
    SignalUsageHotspotQuery query;
    query.signalName = QStringLiteral("hot_sig");
    query.fileName = fileName;
    query.moduleName = QStringLiteral("hotspot_top");
    const SignalUsageHotspotReport report =
        service.buildSignalUsageHotspot(query);

    auto roleCount = [&report](SignalUsageHotspotRole role) {
        for (const SignalUsageHotspotRoleSummary& summary
             : report.roleSummaries) {
            if (summary.role == role)
                return summary.count;
        }
        return 0;
    };
    auto moduleCount = [&report](const QString& moduleName) {
        for (const SignalUsageHotspotModuleSummary& summary
             : report.moduleSummaries) {
            if (summary.moduleName == moduleName)
                return summary.count;
        }
        return 0;
    };
    auto fileCount = [&report](const QString& name) {
        for (const SignalUsageHotspotFileSummary& summary
             : report.fileSummaries) {
            if (summary.fileName == name)
                return summary.count;
        }
        return 0;
    };
    auto matrixCount = [&report](const QString& moduleName,
                                 const QString& name,
                                 SignalUsageHotspotRole role) {
        for (const SignalUsageHotspotMatrixCell& cell : report.matrixCells) {
            if (cell.moduleName == moduleName
                && cell.fileName == name
                && cell.role == role) {
                return cell.count;
            }
        }
        return 0;
    };

    expectBool("signal usage hotspot found",
               report.found
                   && report.declarationDisplayName == QStringLiteral("hot_sig")
                   && report.declarationStableKey == hotSig.stableKey,
               true);
    expectInt("signal usage hotspot total item count",
              report.items.size(),
              8);
    expectBool("signal usage hotspot role counts",
               roleCount(SignalUsageHotspotRole::Write) == 1
                   && roleCount(SignalUsageHotspotRole::Read) == 2
                   && roleCount(SignalUsageHotspotRole::Port) == 1
                   && roleCount(SignalUsageHotspotRole::Condition) == 1
                   && roleCount(SignalUsageHotspotRole::Case) == 1
                   && roleCount(SignalUsageHotspotRole::Timing) == 1
                   && roleCount(SignalUsageHotspotRole::Unknown) == 1,
               true);
    expectBool("signal usage hotspot module and file counts",
               moduleCount(QStringLiteral("hotspot_top")) == 7
                   && moduleCount(QStringLiteral("hotspot_aux")) == 1
                   && fileCount(fileName) == 7
                   && fileCount(auxFileName) == 1,
               true);
    expectBool("signal usage hotspot matrix cells",
               matrixCount(QStringLiteral("hotspot_top"),
                           fileName,
                           SignalUsageHotspotRole::Write) == 1
                   && matrixCount(QStringLiteral("hotspot_top"),
                                  fileName,
                                  SignalUsageHotspotRole::Condition) == 1
                   && matrixCount(QStringLiteral("hotspot_aux"),
                                  auxFileName,
                                  SignalUsageHotspotRole::Read) == 1,
               true);

    const SignalUsageHotspotTrackLane* topLane = nullptr;
    for (const SignalUsageHotspotTrackLane& lane : report.trackLanes) {
        if (lane.moduleName == QStringLiteral("hotspot_top")
            && lane.fileName == fileName) {
            topLane = &lane;
            break;
        }
    }
    expectBool("signal usage hotspot track lane stable range",
               topLane
                   && topLane->count == 7
                   && topLane->startLine == 1
                   && topLane->endLine == 90
                   && topLane->positions.size() == 7
                   && topLane->positions.first().line == 12
                   && topLane->positions.last().line == 70,
               true);

    bool sawUnknownEvidence = false;
    bool sawPrecisePort = false;
    for (const SignalUsageHotspotItem& item : report.items) {
        sawUnknownEvidence = sawUnknownEvidence
            || (item.role == SignalUsageHotspotRole::Unknown
                && item.roleReasonDisplayName
                    == QStringLiteral("unclassified relationship")
                && item.evidenceText.contains(QStringLiteral("constraint"))
                && item.preciseEvidence);
        sawPrecisePort = sawPrecisePort
            || (item.role == SignalUsageHotspotRole::Port
                && item.line == 30
                && item.peerSymbolRecord.localHandle == portPin.localHandle
                && item.relationshipType == SymbolRelationshipEngine::REFERENCES);
    }
    expectBool("signal usage hotspot evidence is retained",
               sawUnknownEvidence && sawPrecisePort,
               true);

    const SemanticSymbolRecord packetType =
        SemanticFixtureRecordBuilder(QStringLiteral("hot_packet_t"),
                                     DeclarationKind::Struct)
            .withFile(fileName)
            .withLocalHandle(9520)
            .withLine(80)
            .withCollectorKind(CollectorKind::PackedStruct)
            .record();
    const SemanticSymbolRecord payloadMember =
        SemanticFixtureRecordBuilder(QStringLiteral("payload"),
                                     DeclarationKind::StructMember)
            .withFile(fileName)
            .withLocalHandle(9521)
            .withLine(81)
            .withCollectorKind(CollectorKind::StructMember)
            .inStruct(QStringLiteral("hot_packet_t"))
            .record();
    const SemanticSymbolRecord bus =
        SemanticFixtureRecordBuilder(QStringLiteral("bus"),
                                     DeclarationKind::StructVariable)
            .withFile(fileName)
            .withLocalHandle(9522)
            .withLine(82)
            .withCollectorKind(CollectorKind::PackedStructVariable)
            .inModule(QStringLiteral("hotspot_top"))
            .withType(QStringLiteral("hot_packet_t"),
                      QStringLiteral("hot_packet_t"),
                      DeclarationKind::Struct)
            .record();
    const SemanticSymbolRecord payloadDriver =
        SemanticFixtureRecordBuilder(QStringLiteral("payload_driver"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9523)
            .withLine(83)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("hotspot_top"))
            .record();
    SemanticRelationship memberWrite = semanticFixtureRelationship(
        payloadDriver,
        bus,
        SymbolRelationshipEngine::ASSIGNS_TO,
        RelationshipProvenance::Inferred,
        90,
        QStringLiteral("assign bus.payload"),
        evidenceRange(fileName, 83, 11, 25));
    memberWrite.toAccessPath = QStringLiteral("bus.payload");

    const SemanticSymbolRecord enumType =
        SemanticFixtureRecordBuilder(QStringLiteral("hot_state_e"),
                                     DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(9530)
            .withLine(84)
            .withCollectorKind(CollectorKind::Enum)
            .record();
    const SemanticSymbolRecord enumValue =
        SemanticFixtureRecordBuilder(QStringLiteral("HOT_IDLE"),
                                     DeclarationKind::User)
            .withFile(fileName)
            .withLocalHandle(9531)
            .withLine(85)
            .withCollectorKind(CollectorKind::EnumValue)
            .record();
    const SemanticSymbolRecord enumVar =
        SemanticFixtureRecordBuilder(QStringLiteral("state_q"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9532)
            .withLine(86)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("hotspot_top"))
            .withType(QStringLiteral("hot_state_e"),
                      QStringLiteral("hot_state_e"),
                      DeclarationKind::Enum)
            .record();
    const SemanticSymbolRecord enumNext =
        SemanticFixtureRecordBuilder(QStringLiteral("state_d"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9533)
            .withLine(87)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("hotspot_top"))
            .withType(QStringLiteral("hot_state_e"),
                      QStringLiteral("hot_state_e"),
                      DeclarationKind::Enum)
            .record();
    QList<SemanticRelationship> memberAndEnumRelationships{memberWrite};
    memberAndEnumRelationships.append(semanticFixtureRelationship(
        enumNext,
        enumVar,
        SymbolRelationshipEngine::ASSIGNS_TO,
        RelationshipProvenance::Inferred,
        90,
        QStringLiteral("state_q <= state_d"),
        evidenceRange(fileName, 88, 9, 25)));

    SemanticIndex memberAndEnumIndex;
    memberAndEnumIndex.setSnapshot(sharedSnapshotFromRecords(
        {module,
         packetType,
         payloadMember,
         bus,
         payloadDriver,
         enumType,
         enumValue,
         enumVar,
         enumNext},
        memberAndEnumRelationships,
        QList<SemanticDiagnostic>()));
    SignalUsageHotspotService memberAndEnumService(&memberAndEnumIndex);

    SignalUsageHotspotQuery memberQuery;
    memberQuery.signalName = QStringLiteral("bus");
    memberQuery.signalAccessPath = QStringLiteral("bus.payload");
    memberQuery.fileName = fileName;
    memberQuery.moduleName = QStringLiteral("hotspot_top");
    const SignalUsageHotspotReport memberReport =
        memberAndEnumService.buildSignalUsageHotspot(memberQuery);
    expectBool("signal usage hotspot struct member",
               memberReport.found
                   && memberReport.declarationDisplayName
                       == QStringLiteral("bus.payload")
                   && memberReport.declarationCodeLink.line == 81
                   && memberReport.items.size() == 1
                   && memberReport.items.first().role
                       == SignalUsageHotspotRole::Write,
               true);

    SignalUsageHotspotQuery enumVarQuery;
    enumVarQuery.signalName = QStringLiteral("state_q");
    enumVarQuery.fileName = fileName;
    enumVarQuery.moduleName = QStringLiteral("hotspot_top");
    const SignalUsageHotspotReport enumVarReport =
        memberAndEnumService.buildSignalUsageHotspot(enumVarQuery);
    expectBool("signal usage hotspot enum variable",
               enumVarReport.found
                   && enumVarReport.items.size() == 1
                   && enumVarReport.items.first().role
                       == SignalUsageHotspotRole::Write,
               true);

    SignalUsageHotspotQuery enumValueQuery;
    enumValueQuery.signalStableKey = enumValue.stableKey;
    const SignalUsageHotspotReport enumValueReport =
        memberAndEnumService.buildSignalUsageHotspot(enumValueQuery);
    expectBool("signal usage hotspot rejects enum value query",
               !enumValueReport.found
                   && enumValueReport.items.isEmpty()
                   && enumValueReport.notFoundReason
                       == SignalUsageHotspotNotFoundReason::UnsupportedSymbolKind,
               true);
}

static void runClockResetDomainServiceFixture()
{
    printf("\n-- clock reset domain service fixture --\n");

    const QString fileName = QStringLiteral("test_sv/clock_reset_domain_fixture.sv");
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    const SemanticSymbolRecord top =
        SemanticFixtureRecordBuilder(QStringLiteral("domain_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9201)
            .withRange(1, 1, 80, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord otherDomain =
        SemanticFixtureRecordBuilder(QStringLiteral("other_domain"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9202)
            .withLine(90)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord clkI =
        SemanticFixtureRecordBuilder(QStringLiteral("clk_i"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9203)
            .withLine(10)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("domain_top"))
            .record();
    const SemanticSymbolRecord rstNi =
        SemanticFixtureRecordBuilder(QStringLiteral("rst_ni"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9204)
            .withLine(11)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("domain_top"))
            .record();
    const SemanticSymbolRecord otherClk =
        SemanticFixtureRecordBuilder(QStringLiteral("other_clk"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9205)
            .withLine(95)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("other_domain"))
            .record();
    const SemanticSymbolRecord altClkI =
        SemanticFixtureRecordBuilder(QStringLiteral("alt_clk_i"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9206)
            .withLine(12)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("domain_top"))
            .record();
    const SemanticSymbolRecord scanClk =
        SemanticFixtureRecordBuilder(QStringLiteral("scan_clk"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9207)
            .withLine(13)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("domain_top"))
            .record();
    const SemanticSymbolRecord porRstN =
        SemanticFixtureRecordBuilder(QStringLiteral("por_rst_n"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9208)
            .withLine(14)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("domain_top"))
            .record();
    const SemanticSymbolRecord noTiming =
        SemanticFixtureRecordBuilder(QStringLiteral("no_timing_domain"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9209)
            .withRange(120, 1, 122, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const QList<SemanticSymbolRecord> records{
        top,
        otherDomain,
        clkI,
        rstNi,
        otherClk,
        altClkI,
        scanClk,
        porRstN,
        noTiming,
    };

    QList<SemanticRelationship> relationships;
    relationships.append(semanticFixtureRelationship(
        clkI,
        top,
        SymbolRelationshipEngine::CLOCKS,
        RelationshipProvenance::Inferred,
        85,
        QStringLiteral("posedge clk_i")));
    relationships.append(semanticFixtureRelationship(
        altClkI,
        top,
        SymbolRelationshipEngine::CLOCKS,
        RelationshipProvenance::Inferred,
        80,
        QStringLiteral("posedge alt_clk_i")));
    relationships.append(semanticFixtureRelationship(
        rstNi,
        top,
        SymbolRelationshipEngine::RESETS,
        RelationshipProvenance::SlangExtracted,
        95,
        QStringLiteral("negedge rst_ni")));
    relationships.append(semanticFixtureRelationship(
        otherClk,
        otherDomain,
        SymbolRelationshipEngine::CLOCKS,
        RelationshipProvenance::Inferred,
        75,
        QStringLiteral("posedge other_clk")));

    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        records,
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
    stableTopQuery.moduleStableKey = top.stableKey;
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
                   && topReport.clockDomains.first().domainSignalDisplayName
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
                       == topReport.clockDomains.first()
                              .domainSignalRecord.stableKey
                   && topReport.clockDomains.first().sectionDisplayName
                       == QStringLiteral("Clock")
                   && !topReport.clockDomains.first()
                           .domainSignalDisplayName.isEmpty()
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
                   && topReport.resetDomains.first().domainSignalDisplayName
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
                          .moduleDisplayName == QStringLiteral("domain_top")
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
                       == topReport.clockDomains.first()
                              .domainSignalRecord.stableKey
                   && topReport.clockDomains.first().modules.first()
                          .moduleStableKey
                       == topReport.clockDomains.first()
                              .modules.first()
                              .moduleSymbolRecord.stableKey
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
                       == topReport.evidenceRows.first()
                              .domainSignalRecord.stableKey
                   && topReport.evidenceRows.first().moduleStableKey
                       == topReport.evidenceRows.first()
                              .moduleSymbolRecord.stableKey
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
                    == row.domainSignalRecord.stableKey
                && row.moduleStableKey
                    == row.moduleSymbolRecord.stableKey
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
    otherStableQuery.moduleStableKey = otherDomain.stableKey;
    const ClockResetDomainReport otherReport =
        service.buildClockResetDomainMap(otherStableQuery);
    expectInt("clock reset stable key clock domains",
              otherReport.clockDomains.size(),
              1);
    expectBool("clock reset stable key clock signal",
               !otherReport.clockDomains.isEmpty()
                   && otherReport.clockDomains.first().domainSignalDisplayName
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
    unsupportedModuleQuery.moduleStableKey = clkI.stableKey;
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
        "  state_t status_reg;\n"
        "  always_ff @(posedge clk or negedge rst_n) begin\n"
        "    if (!rst_n) state_q <= IDLE;\n"
        "    else state_q <= state_d;\n"
        "  end\n"
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

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using OwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(QStringLiteral("fsm_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9301)
            .withRange(1, 1, 21, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord stateQ =
        SemanticFixtureRecordBuilder(QStringLiteral("state_q"),
                                     DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(9302)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();
    const SemanticSymbolRecord stateD =
        SemanticFixtureRecordBuilder(QStringLiteral("state_d"),
                                     DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(9303)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();
    const SemanticSymbolRecord statusReg =
        SemanticFixtureRecordBuilder(QStringLiteral("status_reg"),
                                     DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(9307)
            .withLine(5)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();
    const SemanticSymbolRecord idle =
        SemanticFixtureRecordBuilder(QStringLiteral("IDLE"),
                                     DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(9304)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();
    const SemanticSymbolRecord run =
        SemanticFixtureRecordBuilder(QStringLiteral("RUN"),
                                     DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(9305)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();
    const SemanticSymbolRecord done =
        SemanticFixtureRecordBuilder(QStringLiteral("DONE"),
                                     DeclarationKind::Enum)
            .withFile(fileName)
            .withLocalHandle(9306)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("fsm_top"))
            .withType(QStringLiteral("state_t"))
            .record();

    const QString packageFileName = QStringLiteral("test_sv/fsm_pkg_fixture.sv");
    const QString packageModuleFileName =
        QStringLiteral("test_sv/fsm_pkg_module_fixture.sv");
    const QString packageModuleContent = QStringLiteral(
        "module pkg_fsm_top(input logic clk, input logic go);\n"
        "  pkg_state_e cs;\n"
        "  pkg_state_e ns;\n"
        "  always_ff @(posedge clk) begin\n"
        "    cs <= #TP ns;\n"
        "  end\n"
        "  always_comb begin\n"
        "    case (cs)\n"
        "      IDLE: ns = go ? RUN : IDLE;\n"
        "      RUN: ns = IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    const QString dualFileName =
        QStringLiteral("test_sv/state_transition_dual_fixture.sv");
    const QString dualContent = QStringLiteral(
        "module dual_fsm_top(input logic clk, input logic go);\n"
        "  typedef enum logic [0:0] {A_IDLE, A_RUN} dual_state_a_e;\n"
        "  typedef enum logic [0:0] {B_IDLE, B_RUN} dual_state_b_e;\n"
        "  dual_state_a_e cs;\n"
        "  dual_state_a_e ns;\n"
        "  dual_state_b_e current_state;\n"
        "  dual_state_b_e next_state;\n"
        "  always_ff @(posedge clk) begin\n"
        "    cs <= #(1) ns;\n"
        "    current_state <= # TP next_state;\n"
        "  end\n"
        "  always_comb begin\n"
        "    case (cs)\n"
        "      A_IDLE: ns = go ? A_RUN : A_IDLE;\n"
        "      A_RUN: ns = A_IDLE;\n"
        "    endcase\n"
        "    case (current_state)\n"
        "      B_IDLE: next_state = go ? B_RUN : B_IDLE;\n"
        "      B_RUN: next_state = B_IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    const QString paramFileName =
        QStringLiteral("test_sv/state_transition_param_fixture.sv");
    const QString paramContent = QStringLiteral(
        "module param_fsm_top(input logic clk, input logic go);\n"
        "  localparam logic [1:0] ST_IDLE = 2'd0;\n"
        "  localparam logic [1:0] ST_BUSY = 2'd1;\n"
        "  logic [1:0] current_state;\n"
        "  logic [1:0] next_state;\n"
        "  always_ff @(posedge clk) begin\n"
        "    current_state <= #TP next_state;\n"
        "  end\n"
        "  always_comb begin\n"
        "    case (current_state)\n"
        "      ST_IDLE: next_state = go ? ST_BUSY : ST_IDLE;\n"
        "      ST_BUSY: next_state = ST_IDLE;\n"
        "      default: next_state = ST_IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    const QString oddFileName =
        QStringLiteral("test_sv/state_transition_odd_fixture.sv");
    const QString oddContent = QStringLiteral(
        "module odd_fsm_top(input logic clk, input logic go);\n"
        "  typedef enum logic [0:0] {APPLE, PEAR} odd_state_e;\n"
        "  odd_state_e foo;\n"
        "  odd_state_e bar;\n"
        "  logic grant_ns;\n"
        "  always_ff @(posedge clk) begin\n"
        "    foo <= bar;\n"
        "  end\n"
        "  always_comb begin\n"
        "    case (foo)\n"
        "      APPLE: bar = go ? PEAR : APPLE;\n"
        "      PEAR: bar = APPLE;\n"
        "    endcase\n"
        "    grant_ns = go;\n"
        "  end\n"
        "endmodule\n");
    const QString deceptiveFileName =
        QStringLiteral("test_sv/state_transition_name_only_fixture.sv");
    const QString deceptiveContent = QStringLiteral(
        "module name_only_fsm_top(input logic clk, input logic go);\n"
        "  typedef enum logic [0:0] {S0, S1} name_state_e;\n"
        "  name_state_e cs;\n"
        "  name_state_e ns;\n"
        "  always_comb begin\n"
        "    case (cs)\n"
        "      S0: ns = go ? S1 : S0;\n"
        "      S1: ns = S0;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    const QString complexFileName =
        QStringLiteral("test_sv/state_transition_complex_fixture.sv");
    const QString complexContent = QStringLiteral(
        "module complex_fsm_top(input logic clk, input logic start, input logic done, input logic retry, input logic err);\n"
        "  typedef enum logic [2:0] {C_IDLE, C_LOAD, C_WAIT, C_RUN, C_RETRY, C_DONE, C_ERROR} complex_state_e;\n"
        "  complex_state_e state_q;\n"
        "  complex_state_e state_d;\n"
        "  always_ff @(posedge clk) begin\n"
        "    state_q <= state_d;\n"
        "  end\n"
        "  always_comb begin\n"
        "    state_d = state_q;\n"
        "    case (state_q)\n"
        "      C_IDLE: state_d = start ? C_LOAD : C_IDLE;\n"
        "      C_LOAD: state_d = err ? C_ERROR : C_WAIT;\n"
        "      C_WAIT: state_d = done ? C_DONE : C_RUN;\n"
        "      C_RUN: state_d = err ? C_ERROR : (retry ? C_RETRY : C_DONE);\n"
        "      C_RETRY: state_d = C_LOAD;\n"
        "      C_DONE: state_d = C_IDLE;\n"
        "      C_ERROR: state_d = C_IDLE;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    const QString deadFileName =
        QStringLiteral("test_sv/state_transition_dead_fixture.sv");
    const QString deadContent = QStringLiteral(
        "module dead_fsm_top(input logic clk, input logic go, input logic err);\n"
        "  typedef enum logic [1:0] {D_IDLE, D_BUSY, D_ERROR} dead_state_e;\n"
        "  dead_state_e state_q;\n"
        "  dead_state_e state_d;\n"
        "  always_ff @(posedge clk) begin\n"
        "    state_q <= state_d;\n"
        "  end\n"
        "  always_comb begin\n"
        "    state_d = state_q;\n"
        "    case (state_q)\n"
        "      D_IDLE: state_d = go ? D_BUSY : D_IDLE;\n"
        "      D_BUSY: state_d = err ? D_ERROR : D_IDLE;\n"
        "      D_ERROR: state_d = D_ERROR;\n"
        "    endcase\n"
        "  end\n"
        "endmodule\n");
    const SemanticSymbolRecord packageModule =
        SemanticFixtureRecordBuilder(QStringLiteral("pkg_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(packageModuleFileName)
            .withLocalHandle(9310)
            .withRange(1, 1, 13, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord packageCs =
        SemanticFixtureRecordBuilder(QStringLiteral("cs"),
                                     DeclarationKind::Enum)
            .withFile(packageModuleFileName)
            .withLocalHandle(9311)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("pkg_fsm_top"))
            .withType(QStringLiteral("pkg_state_e"))
            .record();
    const SemanticSymbolRecord packageNs =
        SemanticFixtureRecordBuilder(QStringLiteral("ns"),
                                     DeclarationKind::Enum)
            .withFile(packageModuleFileName)
            .withLocalHandle(9312)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("pkg_fsm_top"))
            .withType(QStringLiteral("pkg_state_e"))
            .record();
    const SemanticSymbolRecord packageIdle =
        SemanticFixtureRecordBuilder(QStringLiteral("IDLE"),
                                     DeclarationKind::Enum)
            .withFile(packageFileName)
            .withLocalHandle(9313)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumValue)
            .inPackage(QStringLiteral("fsm_pkg"))
            .withType(QStringLiteral("pkg_state_e"))
            .record();
    const SemanticSymbolRecord packageRun =
        SemanticFixtureRecordBuilder(QStringLiteral("RUN"),
                                     DeclarationKind::Enum)
            .withFile(packageFileName)
            .withLocalHandle(9314)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumValue)
            .inPackage(QStringLiteral("fsm_pkg"))
            .withType(QStringLiteral("pkg_state_e"))
            .record();
    const SemanticSymbolRecord dualModule =
        SemanticFixtureRecordBuilder(QStringLiteral("dual_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(dualFileName)
            .withLocalHandle(9320)
            .withRange(1, 1, 22, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord dualCs =
        SemanticFixtureRecordBuilder(QStringLiteral("cs"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9321)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_a_e"))
            .record();
    const SemanticSymbolRecord dualNs =
        SemanticFixtureRecordBuilder(QStringLiteral("ns"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9322)
            .withLine(5)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_a_e"))
            .record();
    const SemanticSymbolRecord dualCurrentState =
        SemanticFixtureRecordBuilder(QStringLiteral("current_state"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9323)
            .withLine(6)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_b_e"))
            .record();
    const SemanticSymbolRecord dualNextState =
        SemanticFixtureRecordBuilder(QStringLiteral("next_state"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9324)
            .withLine(7)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_b_e"))
            .record();
    const SemanticSymbolRecord dualAIdle =
        SemanticFixtureRecordBuilder(QStringLiteral("A_IDLE"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9325)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_a_e"))
            .record();
    const SemanticSymbolRecord dualARun =
        SemanticFixtureRecordBuilder(QStringLiteral("A_RUN"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9326)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_a_e"))
            .record();
    const SemanticSymbolRecord dualBIdle =
        SemanticFixtureRecordBuilder(QStringLiteral("B_IDLE"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9327)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_b_e"))
            .record();
    const SemanticSymbolRecord dualBRun =
        SemanticFixtureRecordBuilder(QStringLiteral("B_RUN"),
                                     DeclarationKind::Enum)
            .withFile(dualFileName)
            .withLocalHandle(9328)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("dual_fsm_top"))
            .withType(QStringLiteral("dual_state_b_e"))
            .record();
    const SemanticSymbolRecord paramModule =
        SemanticFixtureRecordBuilder(QStringLiteral("param_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(paramFileName)
            .withLocalHandle(9330)
            .withRange(1, 1, 16, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord paramCurrentState =
        SemanticFixtureRecordBuilder(QStringLiteral("current_state"),
                                     DeclarationKind::Signal)
            .withFile(paramFileName)
            .withLocalHandle(9331)
            .withLine(4)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("param_fsm_top"))
            .withType(QStringLiteral("logic [1:0]"))
            .record();
    const SemanticSymbolRecord paramNextState =
        SemanticFixtureRecordBuilder(QStringLiteral("next_state"),
                                     DeclarationKind::Signal)
            .withFile(paramFileName)
            .withLocalHandle(9332)
            .withLine(5)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("param_fsm_top"))
            .withType(QStringLiteral("logic [1:0]"))
            .record();
    const SemanticSymbolRecord paramIdle =
        SemanticFixtureRecordBuilder(QStringLiteral("ST_IDLE"),
                                     DeclarationKind::Localparam)
            .withFile(paramFileName)
            .withLocalHandle(9333)
            .withLine(2)
            .withCollectorKind(CollectorKind::Localparam)
            .inModule(QStringLiteral("param_fsm_top"))
            .withType(QStringLiteral("logic [1:0]"))
            .record();
    const SemanticSymbolRecord paramBusy =
        SemanticFixtureRecordBuilder(QStringLiteral("ST_BUSY"),
                                     DeclarationKind::Localparam)
            .withFile(paramFileName)
            .withLocalHandle(9334)
            .withLine(3)
            .withCollectorKind(CollectorKind::Localparam)
            .inModule(QStringLiteral("param_fsm_top"))
            .withType(QStringLiteral("logic [1:0]"))
            .record();
    const SemanticSymbolRecord oddModule =
        SemanticFixtureRecordBuilder(QStringLiteral("odd_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(oddFileName)
            .withLocalHandle(9340)
            .withRange(1, 1, 16, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord oddFoo =
        SemanticFixtureRecordBuilder(QStringLiteral("foo"),
                                     DeclarationKind::Enum)
            .withFile(oddFileName)
            .withLocalHandle(9341)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("odd_fsm_top"))
            .withType(QStringLiteral("odd_state_e"))
            .record();
    const SemanticSymbolRecord oddBar =
        SemanticFixtureRecordBuilder(QStringLiteral("bar"),
                                     DeclarationKind::Enum)
            .withFile(oddFileName)
            .withLocalHandle(9342)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("odd_fsm_top"))
            .withType(QStringLiteral("odd_state_e"))
            .record();
    const SemanticSymbolRecord oddGrantNs =
        SemanticFixtureRecordBuilder(QStringLiteral("grant_ns"),
                                     DeclarationKind::Signal)
            .withFile(oddFileName)
            .withLocalHandle(9343)
            .withLine(5)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("odd_fsm_top"))
            .withType(QStringLiteral("logic"))
            .record();
    const SemanticSymbolRecord oddApple =
        SemanticFixtureRecordBuilder(QStringLiteral("APPLE"),
                                     DeclarationKind::Enum)
            .withFile(oddFileName)
            .withLocalHandle(9344)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("odd_fsm_top"))
            .withType(QStringLiteral("odd_state_e"))
            .record();
    const SemanticSymbolRecord oddPear =
        SemanticFixtureRecordBuilder(QStringLiteral("PEAR"),
                                     DeclarationKind::Enum)
            .withFile(oddFileName)
            .withLocalHandle(9345)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("odd_fsm_top"))
            .withType(QStringLiteral("odd_state_e"))
            .record();
    const SemanticSymbolRecord deceptiveModule =
        SemanticFixtureRecordBuilder(QStringLiteral("name_only_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(deceptiveFileName)
            .withLocalHandle(9350)
            .withRange(1, 1, 11, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord deceptiveCs =
        SemanticFixtureRecordBuilder(QStringLiteral("cs"),
                                     DeclarationKind::Enum)
            .withFile(deceptiveFileName)
            .withLocalHandle(9351)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("name_only_fsm_top"))
            .withType(QStringLiteral("name_state_e"))
            .record();
    const SemanticSymbolRecord deceptiveNs =
        SemanticFixtureRecordBuilder(QStringLiteral("ns"),
                                     DeclarationKind::Enum)
            .withFile(deceptiveFileName)
            .withLocalHandle(9352)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("name_only_fsm_top"))
            .withType(QStringLiteral("name_state_e"))
            .record();
    const SemanticSymbolRecord deceptiveS0 =
        SemanticFixtureRecordBuilder(QStringLiteral("S0"),
                                     DeclarationKind::Enum)
            .withFile(deceptiveFileName)
            .withLocalHandle(9353)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("name_only_fsm_top"))
            .withType(QStringLiteral("name_state_e"))
            .record();
    const SemanticSymbolRecord deceptiveS1 =
        SemanticFixtureRecordBuilder(QStringLiteral("S1"),
                                     DeclarationKind::Enum)
            .withFile(deceptiveFileName)
            .withLocalHandle(9354)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("name_only_fsm_top"))
            .withType(QStringLiteral("name_state_e"))
            .record();
    const SemanticSymbolRecord complexModule =
        SemanticFixtureRecordBuilder(QStringLiteral("complex_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(complexFileName)
            .withLocalHandle(9360)
            .withRange(1, 1, 20, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord complexStateQ =
        SemanticFixtureRecordBuilder(QStringLiteral("state_q"),
                                     DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9361)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record();
    const SemanticSymbolRecord complexStateD =
        SemanticFixtureRecordBuilder(QStringLiteral("state_d"),
                                     DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9362)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record();
    const QList<SemanticSymbolRecord> complexStates{
        SemanticFixtureRecordBuilder(QStringLiteral("C_IDLE"), DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9363)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("C_LOAD"), DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9364)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("C_WAIT"), DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9365)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("C_RUN"), DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9366)
            .withLine(5)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("C_RETRY"), DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9367)
            .withLine(6)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("C_DONE"), DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9368)
            .withLine(7)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("C_ERROR"), DeclarationKind::Enum)
            .withFile(complexFileName)
            .withLocalHandle(9369)
            .withLine(8)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("complex_fsm_top"))
            .withType(QStringLiteral("complex_state_e"))
            .record(),
    };
    const SemanticSymbolRecord deadModule =
        SemanticFixtureRecordBuilder(QStringLiteral("dead_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(deadFileName)
            .withLocalHandle(9370)
            .withRange(1, 1, 17, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord deadStateQ =
        SemanticFixtureRecordBuilder(QStringLiteral("state_q"),
                                     DeclarationKind::Enum)
            .withFile(deadFileName)
            .withLocalHandle(9371)
            .withLine(3)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("dead_fsm_top"))
            .withType(QStringLiteral("dead_state_e"))
            .record();
    const SemanticSymbolRecord deadStateD =
        SemanticFixtureRecordBuilder(QStringLiteral("state_d"),
                                     DeclarationKind::Enum)
            .withFile(deadFileName)
            .withLocalHandle(9372)
            .withLine(4)
            .withCollectorKind(CollectorKind::EnumVariable)
            .inModule(QStringLiteral("dead_fsm_top"))
            .withType(QStringLiteral("dead_state_e"))
            .record();
    const QList<SemanticSymbolRecord> deadStates{
        SemanticFixtureRecordBuilder(QStringLiteral("D_IDLE"), DeclarationKind::Enum)
            .withFile(deadFileName)
            .withLocalHandle(9373)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("dead_fsm_top"))
            .withType(QStringLiteral("dead_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("D_BUSY"), DeclarationKind::Enum)
            .withFile(deadFileName)
            .withLocalHandle(9374)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("dead_fsm_top"))
            .withType(QStringLiteral("dead_state_e"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("D_ERROR"), DeclarationKind::Enum)
            .withFile(deadFileName)
            .withLocalHandle(9375)
            .withLine(2)
            .withCollectorKind(CollectorKind::EnumValue)
            .inModule(QStringLiteral("dead_fsm_top"))
            .withType(QStringLiteral("dead_state_e"))
            .record(),
    };
    const SemanticSymbolRecord noFsmModule =
        SemanticFixtureRecordBuilder(QStringLiteral("no_fsm_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9315)
            .withRange(20, 1, 22, 1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const QList<SemanticSymbolRecord> records{
        module,
        stateQ,
        stateD,
        statusReg,
        idle,
        run,
        done,
        packageModule,
        packageCs,
        packageNs,
        packageIdle,
        packageRun,
        dualModule,
        dualCs,
        dualNs,
        dualCurrentState,
        dualNextState,
        dualAIdle,
        dualARun,
        dualBIdle,
        dualBRun,
        paramModule,
        paramCurrentState,
        paramNextState,
        paramIdle,
        paramBusy,
        oddModule,
        oddFoo,
        oddBar,
        oddGrantNs,
        oddApple,
        oddPear,
        deceptiveModule,
        deceptiveCs,
        deceptiveNs,
        deceptiveS0,
        deceptiveS1,
        complexModule,
        complexStateQ,
        complexStateD,
        complexStates.at(0),
        complexStates.at(1),
        complexStates.at(2),
        complexStates.at(3),
        complexStates.at(4),
        complexStates.at(5),
        complexStates.at(6),
        deadModule,
        deadStateQ,
        deadStateD,
        deadStates.at(0),
        deadStates.at(1),
        deadStates.at(2),
        noFsmModule,
    };

    QHash<QString, QString> fileContents;
    fileContents.insert(fileName, content);
    fileContents.insert(packageModuleFileName, packageModuleContent);
    fileContents.insert(dualFileName, dualContent);
    fileContents.insert(paramFileName, paramContent);
    fileContents.insert(oddFileName, oddContent);
    fileContents.insert(deceptiveFileName, deceptiveContent);
    fileContents.insert(complexFileName, complexContent);
    fileContents.insert(deadFileName, deadContent);
    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        records,
        QList<SemanticRelationship>(),
        QList<SemanticDiagnostic>(),
        fileContents));
    FsmGraphService service(&index);
    StateTransitionGraphService stateTransitionService(&index);

    FsmGraphQuery query;
    query.moduleName = QStringLiteral("fsm_top");
    query.fileName = fileName;
    const FsmGraphReport report = service.buildFsmGraph(query);

    expectBool("fsm graph found", report.found, true);
    FsmGraphQuery stableFsmQuery;
    stableFsmQuery.moduleStableKey = module.stableKey;
    const FsmGraphReport stableFsmReport = service.buildFsmGraph(stableFsmQuery);
    expectBool("fsm graph resolves stable module key",
               stableFsmReport.found
                   && !stableFsmReport.graphs.isEmpty()
                   && stableFsmReport.graphs.first().moduleStableKey
                       == stableFsmQuery.moduleStableKey
                   && stableFsmReport.graphs.first().moduleDisplayName
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
               SymbolTaxonomy::isFsmStateRegisterDeclaration(
                   semanticFixtureMetadata(DeclarationKind::Enum,
                                           OwnerScope::Module,
                                           CollectorKind::EnumVariable)),
               true);
    expectBool("taxonomy recognizes fsm state value",
               SymbolTaxonomy::isFsmStateValueDeclaration(
                   semanticFixtureMetadata(DeclarationKind::Enum,
                                           OwnerScope::Module,
                                           CollectorKind::EnumValue)),
               true);
    expectInt("fsm graph count", report.graphs.size(), 1);
    bool sawStatusRegGraph = false;
    for (const FsmGraph& graph : report.graphs) {
        sawStatusRegGraph = sawStatusRegGraph
            || graph.stateRegisterDisplayName == QStringLiteral("status_reg");
    }
    expectBool("fsm graph ignores non-case enum register",
               !sawStatusRegGraph,
               true);
    expectBool("fsm graph state register",
               !report.graphs.isEmpty()
                   && report.graphs.first().stateRegisterDisplayName
                       == QStringLiteral("state_q"),
               true);
    expectBool("fsm graph stable keys",
               !report.graphs.isEmpty()
                   && report.graphs.first().moduleStableKey
                       == report.graphs.first().moduleSymbolRecord.stableKey
                   && report.graphs.first().stateRegisterStableKey
                       == report.graphs.first().stateRegisterRecord.stableKey
                   && report.graphs.first().nextStateSignalStableKey
                       == report.graphs.first().nextStateSignalRecord.stableKey,
               true);
    expectBool("fsm graph semantic records",
               !report.graphs.isEmpty()
                   && report.graphs.first().moduleSymbolRecord.isValid()
                   && report.graphs.first().moduleSymbolRecord.localHandle == 9301
                   && report.graphs.first().moduleSymbolRecord.stableKey
                       == report.graphs.first().moduleStableKey
                   && report.graphs.first().moduleSymbolRecord.name
                       == QStringLiteral("fsm_top")
                   && report.graphs.first().moduleDisplayName
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
                   && report.graphs.first().nextStateSignalDisplayName
                       == QStringLiteral("state_d"),
               true);
    expectBool("fsm graph register display metadata",
               !report.graphs.isEmpty()
                   && report.graphs.first().stateRegisterSectionDisplayName
                       == QStringLiteral("State Register")
                   && report.graphs.first().stateRegisterDisplayName
                       == QStringLiteral("state_q")
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
              report.graphs.isEmpty() ? 0 : report.graphs.first().stateCount,
              3);
    expectBool("fsm graph state row metadata",
               !report.graphs.isEmpty()
                   && report.graphs.first().statesGroupDisplayName
                       == QStringLiteral("States")
                   && report.graphs.first().stateRows.size()
                       == report.graphs.first().stateCount
                   && !report.graphs.first().stateRows.isEmpty()
                   && report.graphs.first().stateRows.first().sectionDisplayName
                       == QStringLiteral("State")
                   && !report.graphs.first().stateRows.first()
                           .stateDisplayName.isEmpty()
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
                       == report.graphs.first().stateRows.first()
                              .stateRecord.stableKey,
               true);
    expectBool("fsm graph state row semantic record",
               !report.graphs.isEmpty()
                   && !report.graphs.first().stateRows.isEmpty()
                   && report.graphs.first().stateRows.first().stateRecord.isValid()
                   && report.graphs.first().stateRows.first().stateRecord.localHandle
                       > 0
                   && report.graphs.first().stateRows.first().stateRecord.stableKey
                       == report.graphs.first().stateRows.first().stateStableKey
                   && report.graphs.first().stateRows.first().stateRecord.name
                       == report.graphs.first().stateRows.first().stateDisplayName
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
              report.graphs.isEmpty() ? 0 : report.graphs.first().transitionCount,
              4);
    expectInt("fsm graph transition row count",
              report.graphs.isEmpty() ? 0 : report.graphs.first().transitionRows.size(),
              4);
    expectBool("fsm graph first transition",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first().fromStateDisplayName
                       == QStringLiteral("IDLE")
                   && report.graphs.first().transitionRows.first().toStateDisplayName
                       == QStringLiteral("RUN")
                   && report.graphs.first().transitionRows.first().conditionDisplayName
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
                       == QStringLiteral("line 15")
                   && report.graphs.first().transitionRows.first().sourceRoleDisplayName
                       == QStringLiteral("design source"),
               true);
    expectBool("fsm graph transition row code link",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first().codeLink.fileName
                       == fileName
                   && report.graphs.first().transitionRows.first().codeLink.line == 15
                   && report.graphs.first().transitionRows.first().codeLink.column == 1
                   && report.graphs.first().transitionRows.first().codeLink.fileDisplayName
                       == QStringLiteral("fsm_graph_fixture.sv")
                   && report.graphs.first().transitionRows.first().codeLink.lineDisplayName
                       == QStringLiteral("15"),
               true);
    expectBool("fsm graph transition state endpoints",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first()
                          .fromStateDisplayName == QStringLiteral("IDLE")
                   && report.graphs.first().transitionRows.first()
                          .toStateDisplayName == QStringLiteral("RUN")
                   && report.graphs.first().transitionRows.first()
                          .fromStateStableKey
                       == report.graphs.first().transitionRows.first()
                              .fromStateRecord.stableKey
                   && report.graphs.first().transitionRows.first()
                          .toStateStableKey
                       == report.graphs.first().transitionRows.first()
                              .toStateRecord.stableKey
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
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.first().sectionDisplayName
                       == QStringLiteral("IDLE")
                   && report.graphs.first().transitionRows.first().detailDisplayName
                       == QStringLiteral("state_d when start"),
               true);
    expectBool("fsm graph default transition",
               !report.graphs.isEmpty()
                   && !report.graphs.first().transitionRows.isEmpty()
                   && report.graphs.first().transitionRows.last().fromStateDisplayName
                       == QStringLiteral("default")
                   && report.graphs.first().transitionRows.last().toStateDisplayName
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
                   && packageReport.graphs.first().stateRegisterDisplayName
                       == QStringLiteral("cs")
                   && packageReport.graphs.first().nextStateSignalDisplayName
                       == QStringLiteral("ns"),
               true);
    expectInt("fsm graph package enum state count",
              packageReport.graphs.isEmpty()
                  ? 0
                  : packageReport.graphs.first().stateCount,
              2);
    expectInt("fsm graph package enum transition count",
              packageReport.graphs.isEmpty()
                  ? 0
                  : packageReport.graphs.first().transitionCount,
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
                && row.codeLink.line == 9);
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
                && row.codeLink.line == 9);
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

    FsmGraphService::getInstance()->setSemanticIndex(&index);
    QWidget fsmPanelHost;
    RtlInsightsPanelCoordinator fsmPanel(&fsmPanelHost);
    fsmPanel.updateModuleContext(fileName, QStringLiteral("fsm_top"));
    QPushButton* fsmGraphButton =
        fsmPanel.dock()
            ? fsmPanel.dock()->findChild<QPushButton*>(
                  QStringLiteral("rtlFsmGraphButton"))
            : nullptr;
    if (fsmGraphButton)
        fsmGraphButton->click();
    QGraphicsView* fsmGraphView = fsmPanel.graphView();
    expectBool("fsm graph panel button available",
               fsmGraphButton && fsmGraphButton->isEnabled(),
               true);
    expectBool("fsm graph panel has graph view",
               fsmGraphView && fsmGraphView->scene(),
               true);
    expectInt("fsm graph panel node count",
              fsmPanel.graphNodeItemCountForTest(),
              3);
    expectInt("fsm graph panel edge count",
              fsmPanel.graphEdgeItemCountForTest(),
              3);
    expectBool("fsm graph panel renders interactive graph",
               fsmGraphButton
                   && fsmGraphView
                   && fsmGraphView->scene()
                   && fsmPanel.graphNodeItemCountForTest() == 3
                   && fsmPanel.graphEdgeItemCountForTest() == 3,
               true);
    const QString fsmGraphSummaries =
        fsmPanel.graphElementSummariesForTest().join(QLatin1Char('\n'));
    expectBool("fsm graph panel omits current and next signal nodes",
               !fsmGraphSummaries.contains(QStringLiteral("state-register|"))
                   && !fsmGraphSummaries.contains(
                       QStringLiteral("next-state-signal|")),
               true);
    const QString fsmGraphText =
        fsmPanel.graphTextItemsForTest().join(QLatin1Char('\n'));
    expectBool("fsm graph edge labels use condition ids",
               fsmGraphText.contains(QStringLiteral("C0"))
                   && fsmGraphText.contains(QStringLiteral("C1"))
                   && !fsmGraphText.contains(QStringLiteral("start")),
               true);
    bool sawStrongConditionLabel = false;
    for (const QString& summary : fsmPanel.graphEdgeGeometrySummariesForTest()) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 12)
            continue;
        sawStrongConditionLabel = sawStrongConditionLabel
            || (parts.at(0) == QStringLiteral("transition")
                && parts.at(1) == QStringLiteral("IDLE")
                && parts.at(2) == QStringLiteral("RUN")
                && parts.at(4) == QStringLiteral("C0")
                && parts.at(5) == QStringLiteral("bold")
                && parts.at(6) == QStringLiteral("no-bg")
                && parts.at(7) == InsightVisualStyle::theme().textPrimary.name());
    }
    expectBool("fsm graph condition ids are strong labels without background",
               sawStrongConditionLabel,
               true);
    QSet<QString> normalStateFills;
    QSet<QString> normalStateStrokes;
    for (const QString& summary :
         fsmPanel.graphElementVisualSummariesForTest()) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 7)
            continue;
        if (parts.at(0) == QStringLiteral("state")) {
            normalStateFills.insert(parts.at(4));
            normalStateStrokes.insert(parts.at(5));
        }
    }
    expectBool("fsm graph ordinary states share neutral color",
               normalStateFills.size() == 1
                   && normalStateStrokes.size() == 1
                   && !normalStateFills.contains(
                       InsightVisualStyle::theme()
                           .statusBar.errorBackground.name()),
               true);
    expectBool("fsm graph transition table maps ids to conditions",
               fsmPanel.graphTableRowsForTest().join(QLatin1Char('\n'))
                   .contains(QStringLiteral("C0|IDLE|RUN|start|state_d|line 15")),
               true);
    FsmGraphService::getInstance()->setSemanticIndex(
        SemanticIndex::getInstance());

    FsmGraphQuery dualFsmQuery;
    dualFsmQuery.moduleName = QStringLiteral("dual_fsm_top");
    dualFsmQuery.fileName = dualFileName;
    const FsmGraphReport dualFsmReport = service.buildFsmGraph(dualFsmQuery);
    expectBool("state transition fixture has two fsm graphs",
               dualFsmReport.found && dualFsmReport.graphs.size() == 2,
               true);

    StateTransitionGraphQuery stateTransitionNsQuery;
    stateTransitionNsQuery.symbolName = QStringLiteral("ns");
    stateTransitionNsQuery.fileName = dualFileName;
    stateTransitionNsQuery.moduleName = QStringLiteral("dual_fsm_top");
    const StateTransitionGraphReport stateTransitionNsReport =
        stateTransitionService.buildStateTransitionGraph(
            stateTransitionNsQuery);
    expectBool("state transition graph ns found",
               stateTransitionNsReport.found
                   && stateTransitionNsReport.groupDisplayName
                       == QStringLiteral("State Transition Graph")
                   && stateTransitionNsReport.trigger.available
                   && stateTransitionNsReport.selectedSignalDisplayName
                       == QStringLiteral("ns")
                   && stateTransitionNsReport.graph.stateRegisterDisplayName
                       == QStringLiteral("cs")
                   && stateTransitionNsReport.graph.nextStateSignalDisplayName
                       == QStringLiteral("ns")
                   && stateTransitionNsReport.stateCount == 2
                   && stateTransitionNsReport.transitionCount == 3,
               true);

    StateTransitionGraphQuery stateTransitionNextQuery;
    stateTransitionNextQuery.symbolName = QStringLiteral("next_state");
    stateTransitionNextQuery.fileName = dualFileName;
    stateTransitionNextQuery.moduleName = QStringLiteral("dual_fsm_top");
    const StateTransitionGraphReport stateTransitionNextReport =
        stateTransitionService.buildStateTransitionGraph(
            stateTransitionNextQuery);
    expectBool("state transition graph next_state found",
               stateTransitionNextReport.found
                   && stateTransitionNextReport.selectedSignalDisplayName
                       == QStringLiteral("next_state")
                   && stateTransitionNextReport.graph.stateRegisterDisplayName
                       == QStringLiteral("current_state")
                   && stateTransitionNextReport.graph.nextStateSignalDisplayName
                       == QStringLiteral("next_state")
                   && stateTransitionNextReport.stateCount == 2
                   && stateTransitionNextReport.transitionCount == 3,
               true);

    StateTransitionGraphQuery stateTransitionParamQuery;
    stateTransitionParamQuery.symbolName = QStringLiteral("next_state");
    stateTransitionParamQuery.fileName = paramFileName;
    stateTransitionParamQuery.moduleName = QStringLiteral("param_fsm_top");
    const StateTransitionGraphReport stateTransitionParamReport =
        stateTransitionService.buildStateTransitionGraph(
            stateTransitionParamQuery);
    expectBool("state transition graph localparam states available",
               stateTransitionParamReport.found,
               true);
    expectInt("state transition graph localparam state count",
              stateTransitionParamReport.stateCount,
              2);
    expectInt("state transition graph localparam transition count",
              stateTransitionParamReport.transitionCount,
              4);
    expectBool("state transition graph localparam states found",
               stateTransitionParamReport.found
                   && stateTransitionParamReport.selectedSignalDisplayName
                       == QStringLiteral("next_state")
                   && stateTransitionParamReport.graph.stateRegisterDisplayName
                       == QStringLiteral("current_state")
                   && stateTransitionParamReport.graph.nextStateSignalDisplayName
                       == QStringLiteral("next_state")
                   && stateTransitionParamReport.stateCount == 2
                   && stateTransitionParamReport.transitionCount == 4,
               true);

    FsmGraphQuery oddFsmQuery;
    oddFsmQuery.moduleName = QStringLiteral("odd_fsm_top");
    oddFsmQuery.fileName = oddFileName;
    const FsmGraphReport oddFsmReport = service.buildFsmGraph(oddFsmQuery);
    expectBool("fsm graph structural odd names found",
               oddFsmReport.found
                   && oddFsmReport.graphs.size() == 1
                   && oddFsmReport.graphs.first().stateRegisterDisplayName
                       == QStringLiteral("foo")
                   && oddFsmReport.graphs.first().nextStateSignalDisplayName
                       == QStringLiteral("bar")
                   && oddFsmReport.graphs.first().stateCount == 2
                   && oddFsmReport.graphs.first().transitionCount == 3,
               true);

    StateTransitionGraphQuery oddNextQuery;
    oddNextQuery.symbolName = QStringLiteral("bar");
    oddNextQuery.fileName = oddFileName;
    oddNextQuery.moduleName = QStringLiteral("odd_fsm_top");
    const StateTransitionGraphReport oddNextReport =
        stateTransitionService.buildStateTransitionGraph(oddNextQuery);
    expectBool("state transition graph structural odd next role found",
               oddNextReport.found
                   && oddNextReport.trigger.available
                   && oddNextReport.graph.stateRegisterDisplayName
                       == QStringLiteral("foo")
                   && oddNextReport.graph.nextStateSignalDisplayName
                       == QStringLiteral("bar"),
               true);

    StateTransitionGraphQuery oddCurrentQuery;
    oddCurrentQuery.symbolName = QStringLiteral("foo");
    oddCurrentQuery.fileName = oddFileName;
    oddCurrentQuery.moduleName = QStringLiteral("odd_fsm_top");
    const StateTransitionGraphReport oddCurrentReport =
        stateTransitionService.buildStateTransitionGraph(oddCurrentQuery);
    expectBool("state transition graph structural current role rejected",
               !oddCurrentReport.found
                   && !oddCurrentReport.trigger.available
                   && oddCurrentReport.notFoundReason
                       == StateTransitionGraphNotFoundReason::TriggerRejected,
               true);

    StateTransitionGraphQuery oddFalseNameQuery;
    oddFalseNameQuery.symbolName = QStringLiteral("grant_ns");
    oddFalseNameQuery.fileName = oddFileName;
    oddFalseNameQuery.moduleName = QStringLiteral("odd_fsm_top");
    const StateTransitionGraphReport oddFalseNameReport =
        stateTransitionService.buildStateTransitionGraph(oddFalseNameQuery);
    expectBool("state transition graph ignores ns-shaped non-fsm signal",
               !oddFalseNameReport.found
                   && !oddFalseNameReport.trigger.available
                   && oddFalseNameReport.notFoundReason
                       == StateTransitionGraphNotFoundReason::TriggerRejected,
               true);

    FsmGraphQuery deceptiveFsmQuery;
    deceptiveFsmQuery.moduleName = QStringLiteral("name_only_fsm_top");
    deceptiveFsmQuery.fileName = deceptiveFileName;
    const FsmGraphReport deceptiveFsmReport =
        service.buildFsmGraph(deceptiveFsmQuery);
    expectBool("fsm graph rejects cs/ns names without state register update",
               !deceptiveFsmReport.found
                   && deceptiveFsmReport.notFoundReason
                       == FsmGraphNotFoundReason::NoFsmGraph,
               true);
    StateTransitionGraphQuery deceptiveStateTransitionQuery;
    deceptiveStateTransitionQuery.symbolName = QStringLiteral("ns");
    deceptiveStateTransitionQuery.fileName = deceptiveFileName;
    deceptiveStateTransitionQuery.moduleName =
        QStringLiteral("name_only_fsm_top");
    const StateTransitionGraphReport deceptiveStateTransitionReport =
        stateTransitionService.buildStateTransitionGraph(
            deceptiveStateTransitionQuery);
    expectBool("state transition rejects ns name without structural pair",
               !deceptiveStateTransitionReport.found
                   && !deceptiveStateTransitionReport.trigger.available
                   && deceptiveStateTransitionReport.notFoundReason
                       == StateTransitionGraphNotFoundReason::TriggerRejected,
               true);

    StateTransitionGraphQuery stateTransitionCsQuery;
    stateTransitionCsQuery.symbolName = QStringLiteral("cs");
    stateTransitionCsQuery.fileName = dualFileName;
    stateTransitionCsQuery.moduleName = QStringLiteral("dual_fsm_top");
    const StateTransitionGraphReport stateTransitionCsReport =
        stateTransitionService.buildStateTransitionGraph(
            stateTransitionCsQuery);
    expectBool("state transition graph rejects cs",
               !stateTransitionCsReport.found
                   && !stateTransitionCsReport.trigger.available
                   && stateTransitionCsReport.notFoundReason
                       == StateTransitionGraphNotFoundReason::TriggerRejected,
               true);

    StateTransitionGraphQuery stateTransitionCurrentQuery;
    stateTransitionCurrentQuery.symbolName = QStringLiteral("current_state");
    stateTransitionCurrentQuery.fileName = dualFileName;
    stateTransitionCurrentQuery.moduleName = QStringLiteral("dual_fsm_top");
    const StateTransitionGraphReport stateTransitionCurrentReport =
        stateTransitionService.buildStateTransitionGraph(
            stateTransitionCurrentQuery);
    expectBool("state transition graph rejects current_state",
               !stateTransitionCurrentReport.found
                   && !stateTransitionCurrentReport.trigger.available
                   && stateTransitionCurrentReport.notFoundReason
                       == StateTransitionGraphNotFoundReason::TriggerRejected,
               true);

    StateTransitionGraphQuery stateTransitionNoMatchQuery;
    stateTransitionNoMatchQuery.symbolName = QStringLiteral("ns");
    stateTransitionNoMatchQuery.fileName = fileName;
    stateTransitionNoMatchQuery.moduleName = QStringLiteral("fsm_top");
    const StateTransitionGraphReport stateTransitionNoMatchReport =
        stateTransitionService.buildStateTransitionGraph(
            stateTransitionNoMatchQuery);
    expectBool("state transition graph rejects non-structural selected symbol",
               !stateTransitionNoMatchReport.found
                   && !stateTransitionNoMatchReport.trigger.available
                   && stateTransitionNoMatchReport.notFoundReason
                       == StateTransitionGraphNotFoundReason::TriggerRejected,
               true);

    StateTransitionGraphQuery stateTransitionNoFsmQuery;
    stateTransitionNoFsmQuery.symbolName = QStringLiteral("ns");
    stateTransitionNoFsmQuery.fileName = fileName;
    stateTransitionNoFsmQuery.moduleName = QStringLiteral("no_fsm_top");
    const StateTransitionGraphReport stateTransitionNoFsmReport =
        stateTransitionService.buildStateTransitionGraph(
            stateTransitionNoFsmQuery);
    expectBool("state transition graph rejects module without structural fsm",
               !stateTransitionNoFsmReport.found
                   && !stateTransitionNoFsmReport.trigger.available
                   && stateTransitionNoFsmReport.notFoundReason
                       == StateTransitionGraphNotFoundReason::TriggerRejected,
               true);

    StateTransitionGraphService::getInstance()->setSemanticIndex(&index);
    QWidget stateTransitionPanelHost;
    RtlInsightsPanelCoordinator stateTransitionPanel(
        &stateTransitionPanelHost);
    QString navigatedFileName;
    int navigatedLine = 0;
    int navigatedColumn = 0;
    stateTransitionPanel.setNavigationHandler(
        [&](const QString& fileName, int line, int column) {
            navigatedFileName = fileName;
            navigatedLine = line;
            navigatedColumn = column;
            return true;
        });
    stateTransitionPanel.showStateTransitionGraphForSignal(
        dualFileName,
        QStringLiteral("dual_fsm_top"),
        QStringLiteral("next_state"));
    QGraphicsView* stateTransitionGraphView = stateTransitionPanel.graphView();
    expectBool("state transition panel renders service report",
               stateTransitionGraphView
                   && stateTransitionGraphView->scene()
                   && stateTransitionPanel.graphNodeItemCountForTest() == 2
                   && stateTransitionPanel.graphEdgeItemCountForTest() == 3,
               true);
    const QString stateTransitionCanvasText =
        stateTransitionPanel.graphTextItemsForTest().join(QLatin1Char('\n'));
    expectBool("state transition canvas omits title and cs ns caption",
               !stateTransitionCanvasText.contains(
                   QStringLiteral("State Transition Graph"))
                   && !stateTransitionCanvasText.contains(
                       QStringLiteral("current_state  ->  next_state"))
                   && !stateTransitionCanvasText.contains(
                       QStringLiteral("current_state -> next_state")),
               true);
    expectBool("state transition panel omits current and next signal nodes",
               !stateTransitionPanel.graphElementSummariesForTest()
                    .join(QLatin1Char('\n'))
                    .contains(QStringLiteral("state-register|"))
                   && !stateTransitionPanel.graphElementSummariesForTest()
                           .join(QLatin1Char('\n'))
                           .contains(QStringLiteral("next-state-signal|")),
               true);
    bool sawCurvedSelfLoop = false;
    bool sawUpperMutualArc = false;
    bool sawLowerMutualArc = false;
    bool dualTransitionLabelsOffPath = true;
    bool dualTransitionPathsClearNodes = true;
    int dualTransitionLabelChecks = 0;
    int dualTransitionClearChecks = 0;
    for (const QString& summary :
         stateTransitionPanel.graphEdgeGeometrySummariesForTest()) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 12)
            continue;
        const int arrowDegrees = parts.at(9).toInt();
        const int loopWidth = parts.at(10).toInt();
        const int loopHeight = parts.at(11).toInt();
        sawCurvedSelfLoop = sawCurvedSelfLoop
            || (parts.at(0) == QStringLiteral("transition")
                && parts.at(1) == QStringLiteral("B_IDLE")
                && parts.at(2) == QStringLiteral("B_IDLE")
                && parts.at(8) == QStringLiteral("curve")
                && arrowDegrees >= 120
                && arrowDegrees <= 150
                && loopWidth < 320
                && loopHeight < 150);
        if (parts.size() >= 17
            && parts.at(0) == QStringLiteral("transition")) {
            const int labelDistance = parts.at(14).toInt();
            const int siblingSpacing = parts.at(16).toInt();
            dualTransitionLabelsOffPath =
                dualTransitionLabelsOffPath && labelDistance >= 16;
            dualTransitionPathsClearNodes =
                dualTransitionPathsClearNodes
                && parts.at(15) == QStringLiteral("clear");
            ++dualTransitionLabelChecks;
            ++dualTransitionClearChecks;
            sawUpperMutualArc = sawUpperMutualArc
                || (parts.at(1) == QStringLiteral("B_RUN")
                    && parts.at(2) == QStringLiteral("B_IDLE")
                    && parts.at(8) == QStringLiteral("curve")
                    && parts.at(12) == QStringLiteral("reversePairUpper")
                    && labelDistance >= 16
                    && siblingSpacing >= 40
                    && arrowDegrees >= 95
                    && arrowDegrees <= 170);
            sawLowerMutualArc = sawLowerMutualArc
                || (parts.at(1) == QStringLiteral("B_IDLE")
                    && parts.at(2) == QStringLiteral("B_RUN")
                    && parts.at(8) == QStringLiteral("curve")
                    && parts.at(12) == QStringLiteral("reversePairLower")
                    && labelDistance >= 16
                    && siblingSpacing >= 40
                    && arrowDegrees >= -90
                    && arrowDegrees <= -5);
        }
    }
    expectBool("state transition self-loop is compact curved arrow",
               sawCurvedSelfLoop,
               true);
    expectBool("state transition mutual pair uses split upper/lower arcs",
               sawUpperMutualArc && sawLowerMutualArc,
               true);
    expectBool("state transition labels stay off edge paths",
               dualTransitionLabelChecks >= 3 && dualTransitionLabelsOffPath,
               true);
    expectBool("state transition edge paths avoid state nodes",
               dualTransitionClearChecks >= 3
                   && dualTransitionPathsClearNodes,
               true);
    QLineEdit* stateTransitionSearchEdit =
        stateTransitionPanel.dock()
            ? stateTransitionPanel.dock()->findChild<QLineEdit*>(
                  QStringLiteral("rtlGraphSearchEdit"))
            : nullptr;
    if (stateTransitionSearchEdit)
        stateTransitionSearchEdit->setText(QStringLiteral("B_IDLE"));
    expectBool("state transition graph search highlights elements",
               stateTransitionSearchEdit
                   && stateTransitionPanel.graphSelectedItemCountForTest() > 0,
               true);
    if (stateTransitionSearchEdit)
        stateTransitionSearchEdit->clear();
    const bool invokedTransitionNavigation =
        stateTransitionPanel.triggerGraphNavigationForTest(
            QStringLiteral("transition"),
            QStringLiteral("B_IDLE"),
            QStringLiteral("B_RUN"));
    expectBool("state transition panel transition navigation",
               invokedTransitionNavigation
                   && navigatedFileName == dualFileName
                   && navigatedLine == 18
                   && navigatedColumn == 1,
               true);
    stateTransitionPanel.showStateTransitionGraphForSignal(
        dualFileName,
        QStringLiteral("dual_fsm_top"),
        QStringLiteral("current_state"));
    expectBool("state transition current-state rejection reason visible",
               stateTransitionPanel.graphNodeItemCountForTest() == 0
                   && stateTransitionPanel.graphTextItemsForTest()
                          .join(QLatin1Char('\n'))
                          .contains(QStringLiteral("Please select the next-state signal")),
               true);

    FsmGraphQuery complexFsmQuery;
    complexFsmQuery.moduleName = QStringLiteral("complex_fsm_top");
    complexFsmQuery.fileName = complexFileName;
    const FsmGraphReport complexFsmReport =
        service.buildFsmGraph(complexFsmQuery);
    expectBool("complex fsm graph canonical statistics",
               complexFsmReport.found
                   && complexFsmReport.graphs.size() == 1
                   && complexFsmReport.graphs.first().stateCount == 7
                   && complexFsmReport.graphs.first().stateRows.size() == 7
                   && complexFsmReport.graphs.first().transitionRows.size()
                       == complexFsmReport.graphs.first().transitionCount,
               true);
    StateTransitionGraphService::getInstance()->setSemanticIndex(&index);
    QWidget complexStatePanelHost;
    RtlInsightsPanelCoordinator complexStatePanel(&complexStatePanelHost);
    QString complexAliasNavigatedFileName;
    complexStatePanel.setNavigationHandler(
        [&](const QString& fileName, int, int) {
            complexAliasNavigatedFileName = fileName;
            return true;
        });
    complexStatePanel.showStateTransitionGraphForSignal(
        complexFileName,
        QStringLiteral("complex_fsm_top"),
        QStringLiteral("state_d"));
    const QStringList complexSummaries =
        complexStatePanel.graphElementSummariesForTest();
    const QStringList complexVisualSummaries =
        complexStatePanel.graphElementVisualSummariesForTest();
    QGraphicsView* complexGraphView = complexStatePanel.graphView();
    const QRectF complexBounds =
        complexGraphView && complexGraphView->scene()
            ? complexGraphView->scene()->itemsBoundingRect()
            : QRectF();
    bool complexSawIdleAlias = false;
    QSet<int> complexCanonicalStateXs;
    QSet<int> complexCanonicalStateYs;
    for (const QString& summary : complexSummaries) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 8)
            continue;
        if (parts.at(0) == QStringLiteral("state")) {
            complexCanonicalStateXs.insert(parts.at(4).toInt());
            complexCanonicalStateYs.insert(parts.at(5).toInt());
        }
        complexSawIdleAlias = complexSawIdleAlias
            || (parts.at(0) == QStringLiteral("state-alias")
                && parts.at(1) == QStringLiteral("C_IDLE")
                && parts.at(2) == QStringLiteral("alias")
                && parts.at(3).contains(QStringLiteral("canonical state C_IDLE")));
    }
    QString complexIdleCanonicalFill;
    QString complexIdleCanonicalStroke;
    QString complexIdleAliasFill;
    QString complexIdleAliasStroke;
    QSet<QString> complexPlainCanonicalFills;
    QSet<QString> complexPlainCanonicalStrokes;
    bool complexIdleAliasDashed = false;
    for (const QString& summary : complexVisualSummaries) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 7)
            continue;
        if (parts.at(0) == QStringLiteral("state")
            && parts.at(1) == QStringLiteral("C_IDLE")) {
            complexIdleCanonicalFill = parts.at(4);
            complexIdleCanonicalStroke = parts.at(5);
        } else if (parts.at(0) == QStringLiteral("state-alias")
                   && parts.at(1) == QStringLiteral("C_IDLE")) {
            complexIdleAliasFill = parts.at(4);
            complexIdleAliasStroke = parts.at(5);
            complexIdleAliasDashed = parts.at(6) == QStringLiteral("dash");
        } else if (parts.at(0) == QStringLiteral("state")) {
            complexPlainCanonicalFills.insert(parts.at(4));
            complexPlainCanonicalStrokes.insert(parts.at(5));
        }
    }
    complexPlainCanonicalFills.remove(complexIdleCanonicalFill);
    complexPlainCanonicalStrokes.remove(complexIdleCanonicalStroke);
    bool complexLabelsOffPath = true;
    bool complexEdgesClearNodes = true;
    int complexLabelChecks = 0;
    int complexClearChecks = 0;
    for (const QString& summary :
         complexStatePanel.graphEdgeGeometrySummariesForTest()) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 17 || parts.at(0) != QStringLiteral("transition"))
            continue;
        complexLabelsOffPath =
            complexLabelsOffPath && parts.at(14).toInt() >= 16;
        complexEdgesClearNodes =
            complexEdgesClearNodes && parts.at(15) == QStringLiteral("clear");
        ++complexLabelChecks;
        ++complexClearChecks;
    }
    const bool complexAliasNavigation =
        complexStatePanel.triggerGraphNavigationForTest(
            QStringLiteral("state-alias"),
            QStringLiteral("C_IDLE"));
    expectBool("complex fsm layout creates canonical alias",
               complexSawIdleAlias
                   && complexAliasNavigation
                   && complexAliasNavigatedFileName == complexFileName,
               true);
    expectBool("complex fsm alias does not inflate transition table",
               !complexFsmReport.graphs.isEmpty()
                   && complexStatePanel.graphTableRowsForTest().size()
                       == complexFsmReport.graphs.first().transitionRows.size(),
               true);
    expectBool("complex fsm alias shares canonical color and stays dashed",
               !complexIdleCanonicalFill.isEmpty()
                   && complexIdleCanonicalFill == complexIdleAliasFill
                   && complexIdleCanonicalStroke == complexIdleAliasStroke
                   && complexIdleAliasDashed,
               true);
    expectBool("complex fsm ordinary canonical states keep neutral color",
               complexPlainCanonicalFills.size() == 1
                   && complexPlainCanonicalStrokes.size() == 1
                   && complexIdleCanonicalFill
                       != *complexPlainCanonicalFills.constBegin(),
               true);
    expectBool("complex fsm transition labels and paths avoid overlaps",
               !complexFsmReport.graphs.isEmpty()
                   && complexLabelChecks >= complexFsmReport.graphs.first()
                                             .transitionRows.size()
                   && complexClearChecks >= complexLabelChecks
                   && complexLabelsOffPath
                   && complexEdgesClearNodes,
               true);
    expectBool("complex fsm layout is path centric with lanes",
               complexCanonicalStateXs.size() >= 5
                   && complexCanonicalStateYs.size() >= 2,
               true);
    expectBool("complex fsm layout stays compact",
               complexBounds.isValid()
                   && complexBounds.width() < 2600.0
                   && complexBounds.height() < 980.0
                   && complexBounds.width()
                          / qMax<qreal>(1.0, complexBounds.height())
                       < 6.5,
               true);

    FsmGraphQuery deadFsmQuery;
    deadFsmQuery.moduleName = QStringLiteral("dead_fsm_top");
    deadFsmQuery.fileName = deadFileName;
    const FsmGraphReport deadFsmReport = service.buildFsmGraph(deadFsmQuery);
    bool sawDeadErrorState = false;
    bool sawIdleSelfLoopNotDead = false;
    for (const FsmStateRow& row : deadFsmReport.graphs.isEmpty()
             ? QList<FsmStateRow>()
             : deadFsmReport.graphs.first().stateRows) {
        sawDeadErrorState = sawDeadErrorState
            || (row.stateDisplayName == QStringLiteral("D_ERROR")
                && row.deadEndState
                && row.statusDisplayName == QStringLiteral("dead/end state"));
        sawIdleSelfLoopNotDead = sawIdleSelfLoopNotDead
            || (row.stateDisplayName == QStringLiteral("D_IDLE")
                && !row.deadEndState
                && row.statusDisplayName.isEmpty());
    }
    expectBool("dead state service marks only pure self-loop state",
               deadFsmReport.found
                   && sawDeadErrorState
                   && sawIdleSelfLoopNotDead,
               true);

    QWidget deadStatePanelHost;
    RtlInsightsPanelCoordinator deadStatePanel(&deadStatePanelHost);
    deadStatePanel.showStateTransitionGraphForSignal(
        deadFileName,
        QStringLiteral("dead_fsm_top"),
        QStringLiteral("state_d"));
    bool sawDeadStateDangerColor = false;
    for (const QString& summary :
         deadStatePanel.graphElementVisualSummariesForTest()) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 7)
            continue;
        sawDeadStateDangerColor = sawDeadStateDangerColor
            || (parts.at(0) == QStringLiteral("state")
                && parts.at(1) == QStringLiteral("D_ERROR")
                && parts.at(4)
                    == InsightVisualStyle::theme()
                           .statusBar.errorBackground.name()
                && parts.at(5)
                    == InsightVisualStyle::theme().statusBar.errorText.name());
    }
    expectBool("dead state graph uses danger color",
               sawDeadStateDangerColor,
               true);
    const bool selectedDeadState =
        deadStatePanel.selectGraphItemForTest(QStringLiteral("state"),
                                              QStringLiteral("D_ERROR"));
    expectBool("dead state inspector notes dead state",
               selectedDeadState
                   && deadStatePanel.graphInspectorRowsForTest()
                          .join(QLatin1Char('\n'))
                          .contains(QStringLiteral("dead/end state")),
               true);
    const QStringList deadTableRows = deadStatePanel.graphTableRowsForTest();
    bool sawDeadTableNote = false;
    bool sawIdleSelfLoopTableNote = false;
    for (const QString& row : deadTableRows) {
        sawDeadTableNote = sawDeadTableNote
            || (row.contains(QStringLiteral("D_ERROR|D_ERROR"))
                && row.contains(QStringLiteral("dead/end state")));
        sawIdleSelfLoopTableNote = sawIdleSelfLoopTableNote
            || (row.contains(QStringLiteral("D_IDLE|D_IDLE"))
                && row.contains(QStringLiteral("dead/end state")));
    }
    expectBool("dead state table notes only pure self-loop state",
               sawDeadTableNote && !sawIdleSelfLoopTableNote,
               true);
    StateTransitionGraphService::getInstance()->setSemanticIndex(
        SemanticIndex::getInstance());

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
    unsupportedModuleQuery.moduleStableKey = stateQ.stableKey;
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
    noFsmGraphQuery.moduleStableKey = noFsmModule.stableKey;
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

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    const SemanticSymbolRecord beforeModule =
        SemanticFixtureRecordBuilder(QStringLiteral("diff_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9401)
            .withLine(1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord beforeClk =
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9402)
            .withLine(2)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord beforeDataPort =
        SemanticFixtureRecordBuilder(QStringLiteral("data"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9403)
            .withLine(3)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("diff_top"))
            .withType(QStringLiteral("logic [7:0]"))
            .record();
    const SemanticSymbolRecord oldParam =
        SemanticFixtureRecordBuilder(QStringLiteral("OLD_PARAM"),
                                     DeclarationKind::Parameter)
            .withFile(fileName)
            .withLocalHandle(9404)
            .withLine(4)
            .withCollectorKind(CollectorKind::Parameter)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord oldInstance =
        SemanticFixtureRecordBuilder(QStringLiteral("u_old"),
                                     DeclarationKind::Instance)
            .withFile(fileName)
            .withLocalHandle(9405)
            .withLine(10)
            .withCollectorKind(CollectorKind::Inst)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord staleSignal =
        SemanticFixtureRecordBuilder(QStringLiteral("stale_q"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9406)
            .withLine(12)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord oldPackage =
        SemanticFixtureRecordBuilder(QStringLiteral("old_pkg"),
                                     DeclarationKind::Package)
            .withFile(fileName)
            .withLocalHandle(9407)
            .withLine(30)
            .withCollectorKind(CollectorKind::Package)
            .record();
    const SemanticSymbolRecord oldTypedef =
        SemanticFixtureRecordBuilder(QStringLiteral("old_t"),
                                     DeclarationKind::Typedef)
            .withFile(fileName)
            .withLocalHandle(9408)
            .withLine(31)
            .withCollectorKind(CollectorKind::Typedef)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const QList<SemanticSymbolRecord> beforeRecords{
        beforeModule,
        beforeClk,
        beforeDataPort,
        oldParam,
        oldInstance,
        staleSignal,
        oldPackage,
        oldTypedef,
    };

    const SemanticSymbolRecord afterModule =
        SemanticFixtureRecordBuilder(QStringLiteral("diff_top"),
                                     DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(9501)
            .withLine(1)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord afterClk =
        SemanticFixtureRecordBuilder(QStringLiteral("clk"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9502)
            .withLine(2)
            .withCollectorKind(CollectorKind::PortInput)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord afterDataPort =
        SemanticFixtureRecordBuilder(QStringLiteral("data"),
                                     DeclarationKind::Port)
            .withFile(fileName)
            .withLocalHandle(9503)
            .withLine(3)
            .withCollectorKind(CollectorKind::PortOutput)
            .inModule(QStringLiteral("diff_top"))
            .withType(QStringLiteral("logic [15:0]"))
            .record();
    const SemanticSymbolRecord depthParam =
        SemanticFixtureRecordBuilder(QStringLiteral("DEPTH"),
                                     DeclarationKind::Parameter)
            .withFile(fileName)
            .withLocalHandle(9504)
            .withLine(4)
            .withCollectorKind(CollectorKind::Parameter)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord newInstance =
        SemanticFixtureRecordBuilder(QStringLiteral("u_new"),
                                     DeclarationKind::Instance)
            .withFile(fileName)
            .withLocalHandle(9505)
            .withLine(10)
            .withCollectorKind(CollectorKind::Inst)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord stateSignal =
        SemanticFixtureRecordBuilder(QStringLiteral("state_q"),
                                     DeclarationKind::Signal)
            .withFile(fileName)
            .withLocalHandle(9506)
            .withLine(12)
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const SemanticSymbolRecord diffInterface =
        SemanticFixtureRecordBuilder(QStringLiteral("diff_if"),
                                     DeclarationKind::Interface)
            .withFile(fileName)
            .withLocalHandle(9507)
            .withLine(30)
            .withCollectorKind(CollectorKind::Interface)
            .record();
    const SemanticSymbolRecord stateTypedef =
        SemanticFixtureRecordBuilder(QStringLiteral("state_t"),
                                     DeclarationKind::Typedef)
            .withFile(fileName)
            .withLocalHandle(9508)
            .withLine(31)
            .withCollectorKind(CollectorKind::Typedef)
            .inModule(QStringLiteral("diff_top"))
            .record();
    const QList<SemanticSymbolRecord> afterRecords{
        afterModule,
        afterClk,
        afterDataPort,
        depthParam,
        newInstance,
        stateSignal,
        diffInterface,
        stateTypedef,
    };

    QList<SemanticRelationship> beforeRelationships;
    beforeRelationships.append(semanticFixtureRelationship(
        beforeModule,
        oldInstance,
        SymbolRelationshipEngine::INSTANTIATES,
        RelationshipProvenance::Workspace,
        70,
        QStringLiteral("old instance u_old")));

    QList<SemanticRelationship> afterRelationships;
    afterRelationships.append(semanticFixtureRelationship(
        afterModule,
        newInstance,
        SymbolRelationshipEngine::INSTANTIATES,
        RelationshipProvenance::Inferred,
        90,
        QStringLiteral("new instance u_new")));

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

    auto beforeSnapshot = sharedSnapshotFromRecords(
        beforeRecords,
        beforeRelationships,
        beforeDiagnostics);
    auto afterSnapshot = sharedSnapshotFromRecords(
        afterRecords,
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
            && change.beforeSymbolRecord.name == QStringLiteral("data")
            && change.beforeSymbolTypeDisplayName == QStringLiteral("input")
            && change.afterSymbolTypeDisplayName == QStringLiteral("output")) {
            dataPortModified = true;
            symbolDisplayMetadataFound =
                change.kindDisplayName == QStringLiteral("Modified")
                && change.categoryDisplayName == QStringLiteral("port")
                && change.symbolDisplayName == QStringLiteral("data")
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
                    == change.beforeSymbolRecord.stableKey
                && change.afterStableKey
                    == change.afterSymbolRecord.stableKey
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
                == change.displaySymbolRecord.stableKey;
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
            && change.afterSymbolRecord.name == QStringLiteral("state_q")) {
            newSignalAdded = true;
        }
        if (change.kind == SemanticDiffChangeKind::Removed
            && change.category == SemanticDiffSymbolCategory::Package
            && change.beforeSymbolRecord.name == QStringLiteral("old_pkg")) {
            oldPackageRemoved =
                change.categoryDisplayName == QStringLiteral("package")
                && change.categoryGroupDisplayName == QStringLiteral("Packages")
                && change.sourceRoleDisplayName == QStringLiteral("design source");
        }
        if (change.kind == SemanticDiffChangeKind::Added
            && change.category == SemanticDiffSymbolCategory::Interface
            && change.afterSymbolRecord.name == QStringLiteral("diff_if")) {
            newInterfaceAdded =
                change.categoryDisplayName == QStringLiteral("interface")
                && change.categoryGroupDisplayName == QStringLiteral("Interfaces");
        }
        if (change.kind == SemanticDiffChangeKind::Added
            && change.category == SemanticDiffSymbolCategory::Type
            && change.afterSymbolRecord.name == QStringLiteral("state_t")) {
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
                change.afterFromSymbolRecord.name == QStringLiteral("diff_top")
                && change.afterToSymbolRecord.name == QStringLiteral("u_new");
            relationshipDisplayMetadataFound =
                change.kindDisplayName == QStringLiteral("Added")
                && change.relationshipTypeDisplayName == QStringLiteral("Instantiates")
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
                    == change.displayFromSymbolRecord.stableKey
                && change.displayToStableKey
                    == change.displayToSymbolRecord.stableKey
                && change.afterFromStableKey
                    == change.afterFromSymbolRecord.stableKey
                && change.afterToStableKey
                    == change.afterToSymbolRecord.stableKey;
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
                change.beforeFromSymbolRecord.name == QStringLiteral("diff_top")
                && change.beforeToSymbolRecord.name == QStringLiteral("u_old")
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

static void runImportAwarePackageVisibilityFixture()
{
    printf("\n-- import-aware package visibility --\n");

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;

    const QString topFile = QStringLiteral("import_visibility_top.sv");
    const QString cfgFile = QStringLiteral("cfg_pkg.sv");
    const QString altFile = QStringLiteral("alt_pkg.sv");
    const QString topContent =
        QStringLiteral("module no_import_top;\n"
                       "  logic [DATA_W-1:0] no_import_data;\n"
                       "endmodule\n"
                       "\n"
                       "module import_top;\n"
                       "  import cfg_pkg::*;\n"
                       "  logic [DATA_W-1:0] imported_data;\n"
                       "endmodule\n"
                       "\n"
                       "module local_top;\n"
                       "  import cfg_pkg::*;\n"
                       "  localparam int DATA_W = 32;\n"
                       "  logic [DATA_W-1:0] local_data;\n"
                       "endmodule\n"
                       "\n"
                       "module conflict_top;\n"
                       "  import cfg_pkg::*;\n"
                       "  import alt_pkg::*;\n"
                       "  logic [DATA_W-1:0] conflict_data;\n"
                       "endmodule\n");

    const SemanticSymbolRecord noImportTop =
        SemanticFixtureRecordBuilder(QStringLiteral("no_import_top"),
                                     DeclarationKind::Module)
            .withFile(topFile)
            .withLocalHandle(10100)
            .withRange(1, 1, 3, 10)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord importTop =
        SemanticFixtureRecordBuilder(QStringLiteral("import_top"),
                                     DeclarationKind::Module)
            .withFile(topFile)
            .withLocalHandle(10101)
            .withRange(5, 1, 8, 10)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord localTop =
        SemanticFixtureRecordBuilder(QStringLiteral("local_top"),
                                     DeclarationKind::Module)
            .withFile(topFile)
            .withLocalHandle(10102)
            .withRange(10, 1, 14, 10)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord conflictTop =
        SemanticFixtureRecordBuilder(QStringLiteral("conflict_top"),
                                     DeclarationKind::Module)
            .withFile(topFile)
            .withLocalHandle(10103)
            .withRange(16, 1, 20, 10)
            .withCollectorKind(CollectorKind::Module)
            .record();
    const SemanticSymbolRecord cfgPkg =
        SemanticFixtureRecordBuilder(QStringLiteral("cfg_pkg"),
                                     DeclarationKind::Package)
            .withFile(cfgFile)
            .withLocalHandle(10110)
            .withLine(1)
            .withCollectorKind(CollectorKind::Package)
            .record();
    const SemanticSymbolRecord cfgDataW =
        SemanticFixtureRecordBuilder(QStringLiteral("DATA_W"),
                                     DeclarationKind::Parameter)
            .withFile(cfgFile)
            .withLocalHandle(10111)
            .withLine(2)
            .withCollectorKind(CollectorKind::Parameter)
            .inPackage(QStringLiteral("cfg_pkg"))
            .record();
    const SemanticSymbolRecord cfgType =
        SemanticFixtureRecordBuilder(QStringLiteral("cfg_word_t"),
                                     DeclarationKind::Typedef)
            .withFile(cfgFile)
            .withLocalHandle(10112)
            .withLine(3)
            .withCollectorKind(CollectorKind::Typedef)
            .inPackage(QStringLiteral("cfg_pkg"))
            .record();
    const SemanticSymbolRecord altPkg =
        SemanticFixtureRecordBuilder(QStringLiteral("alt_pkg"),
                                     DeclarationKind::Package)
            .withFile(altFile)
            .withLocalHandle(10120)
            .withLine(1)
            .withCollectorKind(CollectorKind::Package)
            .record();
    const SemanticSymbolRecord altDataW =
        SemanticFixtureRecordBuilder(QStringLiteral("DATA_W"),
                                     DeclarationKind::Parameter)
            .withFile(altFile)
            .withLocalHandle(10121)
            .withLine(2)
            .withCollectorKind(CollectorKind::Parameter)
            .inPackage(QStringLiteral("alt_pkg"))
            .record();
    const SemanticSymbolRecord localDataW =
        SemanticFixtureRecordBuilder(QStringLiteral("DATA_W"),
                                     DeclarationKind::Localparam)
            .withFile(topFile)
            .withLocalHandle(10130)
            .withLine(12)
            .withCollectorKind(CollectorKind::Localparam)
            .inModule(QStringLiteral("local_top"))
            .record();

    QHash<QString, QString> fileContents;
    fileContents.insert(topFile, topContent);
    fileContents.insert(cfgFile,
                        QStringLiteral("package cfg_pkg;\n"
                                       "  parameter int DATA_W = 16;\n"
                                       "  typedef logic [DATA_W-1:0] cfg_word_t;\n"
                                       "endpackage\n"));
    fileContents.insert(altFile,
                        QStringLiteral("package alt_pkg;\n"
                                       "  parameter int DATA_W = 8;\n"
                                       "endpackage\n"));

    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        {noImportTop,
         importTop,
         localTop,
         conflictTop,
         cfgPkg,
         cfgDataW,
         cfgType,
         altPkg,
         altDataW,
         localDataW},
        {},
        {},
        fileContents));

    CompletionService completionService(&index);
    DefinitionService definitionService(&index);
    SymbolHoverService hoverService(&index);

    CompletionQuery noImportCompletion;
    noImportCompletion.fileName = topFile;
    noImportCompletion.moduleName = QStringLiteral("no_import_top");
    noImportCompletion.prefix = QStringLiteral("DATA");
    noImportCompletion.cursorLine = 2;
    expectBool("unimported package parameter hidden from completion",
               !completionService.findCompletionResult(noImportCompletion)
                    .names.contains(QStringLiteral("DATA_W")),
               true);
    expectBool("unimported package parameter hidden from scope completion",
               !index.getScopeSymbolNames(topFile, 2)
                    .contains(QStringLiteral("DATA_W")),
               true);

    DefinitionQuery noImportDefinition;
    noImportDefinition.symbolName = QStringLiteral("DATA_W");
    noImportDefinition.fileName = topFile;
    noImportDefinition.moduleName = QStringLiteral("no_import_top");
    noImportDefinition.cursorLine = 2;
    const DefinitionResult noImportDefinitionResult =
        definitionService.resolveDefinition(noImportDefinition);
    expectBool("unimported package parameter does not jump",
               !noImportDefinitionResult.found
                   && noImportDefinitionResult.missReason
                       == SemanticDefinitionMissReason::NotVisibleInContext,
               true);

    CompletionQuery importCompletion = noImportCompletion;
    importCompletion.moduleName = QStringLiteral("import_top");
    importCompletion.cursorLine = 7;
    const CompletionResult importedResult =
        completionService.findCompletionResult(importCompletion);
    expectBool("imported package parameter completes",
               importedResult.names.contains(QStringLiteral("DATA_W")),
               true);

    CommandCompletionQuery typedefCompletion;
    typedefCompletion.fileName = topFile;
    typedefCompletion.moduleName = QStringLiteral("import_top");
    typedefCompletion.commandKind = CompletionCommandKind::Typedef;
    typedefCompletion.prefix = QStringLiteral("cfg");
    typedefCompletion.cursorLine = 7;
    expectBool("imported package typedef command completes",
               completionService.findCommandCompletions(typedefCompletion)
                   .contains(QStringLiteral("cfg_word_t")),
               true);

    DefinitionQuery importDefinition = noImportDefinition;
    importDefinition.moduleName = QStringLiteral("import_top");
    importDefinition.cursorLine = 7;
    const DefinitionResult importDefinitionResult =
        definitionService.resolveDefinition(importDefinition);
    expectBool("imported package parameter jumps",
               importDefinitionResult.found
                   && importDefinitionResult.symbolRecord.owner.name
                       == QStringLiteral("cfg_pkg"),
               true);

    EditorSemanticContext hoverContext;
    hoverContext.fileName = topFile;
    hoverContext.moduleName = QStringLiteral("import_top");
    hoverContext.cursorLine = 7;
    hoverContext.lineText = QStringLiteral("  logic [DATA_W-1:0] imported_data;");
    hoverContext.column = hoverContext.lineText.indexOf(QStringLiteral("DATA_W")) + 1;
    const SymbolHoverReport hoverReport =
        hoverService.hoverForContext(hoverContext);
    expectBool("imported package parameter hover resolves",
               hoverReport.available
                   && hoverReport.definitionFile == cfgFile
                   && hoverReport.ownerName == QStringLiteral("cfg_pkg"),
               true);

    CompletionQuery localCompletion = noImportCompletion;
    localCompletion.moduleName = QStringLiteral("local_top");
    localCompletion.cursorLine = 13;
    const CompletionResult localResult =
        completionService.findCompletionResult(localCompletion);
    bool localCompletionPrefersLocal = false;
    for (const CompletionResult::SemanticCompletionItem& item : localResult.items) {
        if (item.label == QStringLiteral("DATA_W"))
            localCompletionPrefersLocal =
                item.symbolRecord.owner.name == QStringLiteral("local_top");
    }
    expectBool("local same-name completion wins over import",
               localCompletionPrefersLocal,
               true);
    DefinitionQuery localDefinition = noImportDefinition;
    localDefinition.moduleName = QStringLiteral("local_top");
    localDefinition.cursorLine = 13;
    const DefinitionResult localDefinitionResult =
        definitionService.resolveDefinition(localDefinition);
    expectBool("local same-name definition wins over import",
               localDefinitionResult.found
                   && localDefinitionResult.symbolRecord.owner.name
                       == QStringLiteral("local_top"),
               true);

    CompletionQuery conflictCompletion = noImportCompletion;
    conflictCompletion.moduleName = QStringLiteral("conflict_top");
    conflictCompletion.cursorLine = 19;
    expectBool("conflicting imported packages hide unqualified completion",
               !completionService.findCompletionResult(conflictCompletion)
                    .names.contains(QStringLiteral("DATA_W")),
               true);
    DefinitionQuery conflictDefinition = noImportDefinition;
    conflictDefinition.moduleName = QStringLiteral("conflict_top");
    conflictDefinition.cursorLine = 19;
    const DefinitionResult conflictDefinitionResult =
        definitionService.resolveDefinition(conflictDefinition);
    expectBool("conflicting imported packages do not jump randomly",
               !conflictDefinitionResult.found
                   && conflictDefinitionResult.missReason
                       == SemanticDefinitionMissReason::AmbiguousImportedPackageSymbol,
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

    QList<SemanticSymbolRecord> records =
        slang.extractWorkspaceSymbolRecords(snapshot.systemVerilogFiles,
                                            snapshot.includeDirs,
                                            snapshot.defines);
    int nextLocalHandle = 1;
    for (SemanticSymbolRecord& record : records) {
        if (record.localHandle <= 0)
            record.localHandle = nextLocalHandle;
        if (nextLocalHandle <= record.localHandle)
            nextLocalHandle = record.localHandle + 1;
    }
    QHash<QString, QString> fileContents;
    for (const QString& fileName : files)
        fileContents.insert(fileName, loadTextFile(fileName));
    using CollectorKind = SymbolTaxonomy::CollectorKind;

    const auto recordByNameAndKind = [&](const QString& name,
                                         CollectorKind collectorKind) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name && record.collectorKind == collectorKind)
                return record;
        }
        return SemanticSymbolRecord{};
    };
    const auto recordByNameKindAndOwner = [&](const QString& name,
                                               CollectorKind collectorKind,
                                               const QString& ownerName) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name
                && record.collectorKind == collectorKind
                && record.owner.name == ownerName) {
                return record;
            }
        }
        return SemanticSymbolRecord{};
    };
    const auto recordByNameAndOwner = [&](const QString& name,
                                          const QString& ownerName) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name && record.owner.name == ownerName)
                return record;
        }
        return SemanticSymbolRecord{};
    };

    const SemanticSymbolRecord rtlTopRecord =
        recordByNameAndKind(QStringLiteral("rtl_top"), CollectorKind::Module);
    const SemanticSymbolRecord packageRecord =
        recordByNameAndKind(QStringLiteral("gl_pkg"), CollectorKind::Package);
    const SemanticSymbolRecord interfaceRecord =
        recordByNameAndKind(QStringLiteral("lr_genr_if"),
                            CollectorKind::Interface);
    SemanticSymbolRecord packageParamRecord =
        recordByNameKindAndOwner(QStringLiteral("P_SW_NUM"),
                                 CollectorKind::Parameter,
                                 QStringLiteral("gl_pkg"));
    if (!packageParamRecord.isValid()) {
        packageParamRecord =
            recordByNameKindAndOwner(QStringLiteral("P_SW_NUM"),
                                     CollectorKind::Localparam,
                                     QStringLiteral("gl_pkg"));
    }
    const SemanticSymbolRecord packageTypedefRecord =
        recordByNameKindAndOwner(QStringLiteral("cpld_sw_sp"),
                                 CollectorKind::Typedef,
                                 QStringLiteral("gl_pkg"));
    const SemanticSymbolRecord interfaceModportRecord =
        recordByNameKindAndOwner(QStringLiteral("si"),
                                 CollectorKind::InterfaceModport,
                                 QStringLiteral("lr_genr_if"));
    const SemanticSymbolRecord interfaceInstRecord =
        recordByNameKindAndOwner(QStringLiteral("LR_GENR_IF"),
                                 CollectorKind::Inst,
                                 QStringLiteral("rtl_top"));
    const SemanticSymbolRecord realClockRecord =
        recordByNameKindAndOwner(QStringLiteral("clk_main"),
                                 CollectorKind::PortInput,
                                 QStringLiteral("rtl_top"));
    const SemanticSymbolRecord realResetRecord =
        recordByNameKindAndOwner(QStringLiteral("srst_main"),
                                 CollectorKind::Logic,
                                 QStringLiteral("rtl_top"));
    const SemanticSymbolRecord hsDacRecord =
        recordByNameAndOwner(QStringLiteral("HS_DAC"),
                             QStringLiteral("rtl_top"));
    const SemanticSymbolRecord chlCtrlRecord =
        recordByNameAndKind(QStringLiteral("chl_ctrl"), CollectorKind::Module);
    const SemanticSymbolRecord phyPassCsRecord =
        recordByNameKindAndOwner(QStringLiteral("phy_pass_thrg_cfg_cs"),
                                 CollectorKind::EnumVariable,
                                 QStringLiteral("chl_ctrl"));
    const SemanticSymbolRecord phyPassNsRecord =
        recordByNameKindAndOwner(QStringLiteral("phy_pass_thrg_cfg_ns"),
                                 CollectorKind::EnumVariable,
                                 QStringLiteral("chl_ctrl"));
    const SemanticSymbolRecord mcsRecord =
        recordByNameAndOwner(QStringLiteral("mcs"),
                             QStringLiteral("chl_ctrl"));

    expectBool("real workspace extracts symbols", records.size() > 20, true);
    expectBool("real workspace has rtl_top module",
               rtlTopRecord.isValid(), true);
    expectBool("real workspace has gl_pkg package",
               packageRecord.isValid(), true);
    expectBool("real workspace has interface",
               interfaceRecord.isValid(), true);
    expectBool("real workspace has package parameter",
               packageParamRecord.isValid(), true);
    expectBool("real workspace has package typedef",
               packageTypedefRecord.isValid(), true);
    expectBool("real workspace has interface modport",
               interfaceModportRecord.isValid(), true);
    expectBool("real workspace has interface instance",
               interfaceInstRecord.isValid(), true);
    expectBool("real workspace has top clock",
               realClockRecord.isValid(), true);
    expectBool("real workspace has top reset",
               realResetRecord.isValid(), true);
    expectBool("real workspace has top HS_DAC struct port",
               hsDacRecord.isValid(), true);
    expectBool("real workspace has chl_ctrl module",
               chlCtrlRecord.isValid(), true);
    expectBool("real workspace has chl_ctrl current fsm state",
               phyPassCsRecord.isValid(), true);
    expectBool("real workspace has chl_ctrl next fsm state",
               phyPassNsRecord.isValid(), true);
    expectBool("real workspace has chl_ctrl mcs signal",
               mcsRecord.isValid(), true);
    expectBool("real workspace taxonomy marks global package",
               packageRecord.owner.kind
                   == SymbolTaxonomy::SymbolOwnerScope::Global,
               true);
    expectBool("real workspace taxonomy marks package parameter visibility",
               packageParamRecord.visibility
                   == SymbolTaxonomy::SymbolVisibility::PackageVisible,
               true);
    expectBool("real workspace taxonomy marks package typedef visibility",
               packageTypedefRecord.visibility
                   == SymbolTaxonomy::SymbolVisibility::PackageVisible,
               true);
    expectBool("real workspace taxonomy marks interface modport member",
               interfaceModportRecord.owner.kind
                   == SymbolTaxonomy::SymbolOwnerScope::Interface,
               true);
    expectBool("real workspace taxonomy marks module instance local",
               interfaceInstRecord.visibility
                   == SymbolTaxonomy::SymbolVisibility::ScopeLocal,
               true);

    QList<SemanticRelationship> realRelationships;
    if (rtlTopRecord.isValid() && packageRecord.isValid()) {
        realRelationships.append(semanticFixtureRelationship(
            rtlTopRecord,
            packageRecord,
            SymbolRelationshipEngine::REFERENCES));
    }
    if (realClockRecord.isValid() && rtlTopRecord.isValid()) {
        realRelationships.append(semanticFixtureRelationship(
            realClockRecord,
            rtlTopRecord,
            SymbolRelationshipEngine::CLOCKS));
    }
    if (realResetRecord.isValid() && rtlTopRecord.isValid()) {
        realRelationships.append(semanticFixtureRelationship(
            realResetRecord,
            rtlTopRecord,
            SymbolRelationshipEngine::RESETS));
    }
    if (interfaceInstRecord.isValid() && interfaceModportRecord.isValid()) {
        realRelationships.append(semanticFixtureRelationship(
            interfaceInstRecord,
            interfaceModportRecord,
            SymbolRelationshipEngine::REFERENCES));
    }

    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(
        records,
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
                   && packageParam.symbolRecord.owner.name == QStringLiteral("gl_pkg"),
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
                   && interfaceResult.symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Interface,
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
                   && modportTypeResult.symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Modport
                   && modportTypeResult.symbolRecord.owner.name
                       == QStringLiteral("lr_genr_if"),
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
                   && modportInstResult.symbolRecord.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Modport
                   && modportInstResult.symbolRecord.owner.name
                       == QStringLiteral("lr_genr_if"),
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
    parameterCompletion.fileName = topPath;
    parameterCompletion.moduleName = QStringLiteral("rtl_top");
    parameterCompletion.commandKind = CompletionCommandKind::Parameter;
    parameterCompletion.prefix = QStringLiteral("P_SW");
    parameterCompletion.cursorLine = rtlTopRecord.location.startLine + 1;
    expectBool("real workspace completes package parameter",
               CompletionSymbolQuery::namesFromRecords(
                   completionService.findCommandCompletionSymbolRecords(
                       parameterCompletion))
                   .contains(QStringLiteral("P_SW_NUM")),
               true);

    CommandCompletionQuery typedefCompletion;
    typedefCompletion.fileName = topPath;
    typedefCompletion.moduleName = QStringLiteral("rtl_top");
    typedefCompletion.commandKind = CompletionCommandKind::Typedef;
    typedefCompletion.prefix = QStringLiteral("cpld");
    typedefCompletion.cursorLine = rtlTopRecord.location.startLine + 1;
    expectBool("real workspace completes package typedef",
               CompletionSymbolQuery::namesFromRecords(
                   completionService.findCommandCompletionSymbolRecords(
                       typedefCompletion))
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
    expectBool("real workspace module brief subject display metadata",
               moduleBrief.moduleDisplayName == QStringLiteral("rtl_top"),
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
    bool sawRealClockRelationshipEndpointRecords = false;
    bool sawRealResetRelationshipEndpointRecords = false;
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
        sawRealClockRelationshipEndpointRecords =
            sawRealClockRelationshipEndpointRecords
            || (!row.outgoing
                && row.fromSymbolDisplayName == QStringLiteral("clk_main")
                && row.toSymbolDisplayName == QStringLiteral("rtl_top")
                && row.peerSymbolRecord.isValid()
                && row.peerSymbolRecord.stableKey == row.peerStableKey
                && row.fromSymbolRecord.isValid()
                && row.fromSymbolRecord.stableKey == row.fromStableKey
                && row.toSymbolRecord.isValid()
                && row.toSymbolRecord.stableKey == row.toStableKey);
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
        sawRealResetRelationshipEndpointRecords =
            sawRealResetRelationshipEndpointRecords
            || (!row.outgoing
                && row.fromSymbolDisplayName == QStringLiteral("srst_main")
                && row.toSymbolDisplayName == QStringLiteral("rtl_top")
                && row.peerSymbolRecord.isValid()
                && row.peerSymbolRecord.stableKey == row.peerStableKey
                && row.fromSymbolRecord.isValid()
                && row.fromSymbolRecord.stableKey == row.fromStableKey
                && row.toSymbolRecord.isValid()
                && row.toSymbolRecord.stableKey == row.toStableKey);
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
    expectBool("real workspace module brief clock relationship endpoint records",
               sawRealClockRelationshipEndpointRecords,
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
    expectBool("real workspace module brief reset relationship endpoint records",
               sawRealResetRelationshipEndpointRecords,
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
        if (domain.domainSignalDisplayName != QStringLiteral("clk_main"))
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
        if (domain.domainSignalDisplayName != QStringLiteral("srst_main"))
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
    interfaceJourneyQuery.signalStableKey = interfaceInstRecord.stableKey;
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

    QList<SemanticSymbolRecord> topFileRecords;
    for (const SemanticSymbolRecord& record : records) {
        if (normalizedPath(record.location.fileName) == normalizedPath(topPath))
            topFileRecords.append(record);
    }
    auto realBaseSnapshot = sharedSnapshotFromRecords(
        records,
        QList<SemanticRelationship>(),
        QList<SemanticDiagnostic>(),
        fileContents);
    SmartRelationshipBuilder realRelationshipBuilder(
        nullptr,
        &slang,
        [&records](const QString& fileName) {
            if (fileName.isEmpty())
                return records;
            QList<SemanticSymbolRecord> fileRecords;
            for (const SemanticSymbolRecord& record : records) {
                if (normalizedPath(record.location.fileName)
                    == normalizedPath(fileName)) {
                    fileRecords.append(record);
                }
            }
            return fileRecords;
        });
    const QHash<QString, RelationshipExtractionInfo> realWorkspaceRelationshipInfo =
        slang.extractWorkspaceRelationshipInfo(snapshot.systemVerilogFiles,
                                              snapshot.includeDirs,
                                              snapshot.defines);
    const RelationshipExtractionInfo realRawRelationshipInfo =
        realWorkspaceRelationshipInfo.value(normalizedPath(topPath));
    const QVector<RelationshipToAdd> realTopRelationships =
        realRelationshipBuilder.computeRelationships(
            topPath,
            fileContents.value(topPath),
            topFileRecords,
            realBaseSnapshot.get(),
            snapshot.includeDirs,
            snapshot.defines,
            &realRawRelationshipInfo);
    QList<SemanticRelationship> realComputedRelationships;
    bool sawHsDacClockMemberRelationship = false;
    for (const RelationshipToAdd& relationship : realTopRelationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        SemanticRelationship item;
        item.fromId = relationship.fromId;
        item.toId = relationship.toId;
        item.type = relationship.type;
        item.confidence = relationship.confidence;
        item.evidenceText = relationship.context;
        item.evidenceRange = relationship.evidenceRange;
        item.fromAccessPath = relationship.fromAccessPath;
        item.toAccessPath = relationship.toAccessPath;
        item.provenance = RelationshipProvenance::Inferred;
        realComputedRelationships.append(item);
        sawHsDacClockMemberRelationship =
            sawHsDacClockMemberRelationship
            || (relationship.type == SymbolRelationshipEngine::ASSIGNS_TO
                && relationship.fromAccessPath == QStringLiteral("clk_main")
                && relationship.toAccessPath
                    == QStringLiteral("HS_DAC.DA_clk_11"));
    }
    SemanticIndex realComputedIndex;
    realComputedIndex.setSnapshot(sharedSnapshotFromRecords(
        records,
        realComputedRelationships,
        QList<SemanticDiagnostic>(),
        fileContents));

    QList<SemanticSymbolRecord> chlCtrlFileRecords;
    for (const SemanticSymbolRecord& record : records) {
        if (normalizedPath(record.location.fileName) == normalizedPath(chlCtrlPath))
            chlCtrlFileRecords.append(record);
    }
    const RelationshipExtractionInfo realChlCtrlRelationshipInfo =
        realWorkspaceRelationshipInfo.value(normalizedPath(chlCtrlPath));
    const QVector<RelationshipToAdd> realChlCtrlRelationships =
        realRelationshipBuilder.computeRelationships(
            chlCtrlPath,
            fileContents.value(chlCtrlPath),
            chlCtrlFileRecords,
            realBaseSnapshot.get(),
            snapshot.includeDirs,
            snapshot.defines,
            &realChlCtrlRelationshipInfo);
    QList<SemanticRelationship> realChlCtrlComputedRelationships =
        realComputedRelationships;
    for (const RelationshipToAdd& relationship : realChlCtrlRelationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        SemanticRelationship item;
        item.fromId = relationship.fromId;
        item.toId = relationship.toId;
        item.type = relationship.type;
        item.confidence = relationship.confidence;
        item.evidenceText = relationship.context;
        item.evidenceRange = relationship.evidenceRange;
        item.fromAccessPath = relationship.fromAccessPath;
        item.toAccessPath = relationship.toAccessPath;
        item.provenance = RelationshipProvenance::Inferred;
        realChlCtrlComputedRelationships.append(item);
    }
    SemanticIndex realHotspotIndex;
    realHotspotIndex.setSnapshot(sharedSnapshotFromRecords(
        records,
        realChlCtrlComputedRelationships,
        QList<SemanticDiagnostic>(),
        fileContents));
    SignalUsageHotspotService realHotspotService(&realHotspotIndex);
    SignalUsageHotspotQuery realMcsHotspotQuery;
    realMcsHotspotQuery.signalName = QStringLiteral("mcs");
    realMcsHotspotQuery.fileName = chlCtrlPath;
    realMcsHotspotQuery.moduleName = QStringLiteral("chl_ctrl");
    const SignalUsageHotspotReport realMcsHotspot =
        realHotspotService.buildSignalUsageHotspot(realMcsHotspotQuery);
    bool sawRealMcsWrite = false;
    bool sawRealMcsRead = false;
    bool sawRealMcsCodeLink = false;
    bool sawEnumValuePollution = false;
    for (const SignalUsageHotspotItem& item : realMcsHotspot.items) {
        sawRealMcsWrite = sawRealMcsWrite
            || item.role == SignalUsageHotspotRole::Write;
        sawRealMcsRead = sawRealMcsRead
            || item.role == SignalUsageHotspotRole::Read
            || item.role == SignalUsageHotspotRole::Condition
            || item.role == SignalUsageHotspotRole::Case;
        sawRealMcsCodeLink = sawRealMcsCodeLink
            || (normalizedPath(item.codeLink.fileName) == normalizedPath(chlCtrlPath)
                && item.codeLink.line > 0
                && !item.codeLink.fileDisplayName.isEmpty()
                && !item.codeLink.lineDisplayName.isEmpty());
        sawEnumValuePollution = sawEnumValuePollution
            || item.peerSymbolRecord.name == QStringLiteral("M_IDLE")
            || item.peerAccessPath == QStringLiteral("M_IDLE");
    }
    expectBool("real workspace hotspot mcs found",
               realMcsHotspot.found,
               true);
    expectBool("real workspace hotspot mcs track matrix",
               !realMcsHotspot.trackLanes.isEmpty()
                   && !realMcsHotspot.matrixCells.isEmpty(),
               true);
    expectBool("real workspace hotspot mcs roles",
               sawRealMcsWrite && sawRealMcsRead,
               true);
    expectBool("real workspace hotspot mcs code link",
               sawRealMcsCodeLink,
               true);
    expectBool("real workspace hotspot mcs filters enum values",
               sawEnumValuePollution,
               false);

    SignalKernelGraphService realComputedGraphService(&realComputedIndex);
    SignalKernelGraphQuery realClockGraphQuery;
    realClockGraphQuery.signalName = QStringLiteral("clk_main");
    realClockGraphQuery.fileName = topPath;
    realClockGraphQuery.moduleName = QStringLiteral("rtl_top");
    const SignalKernelGraphReport realClockGraph =
        realComputedGraphService.buildSignalKernelGraph(realClockGraphQuery);
    bool sawRealClockGraphHsDacMember = false;
    for (const SignalKernelGraphNode& node : realClockGraph.outputs) {
        sawRealClockGraphHsDacMember =
            sawRealClockGraphHsDacMember
            || (node.displayName == QStringLiteral("HS_DAC.DA_clk_11")
                && node.preciseEvidence
                && node.previewCodeLink.line == 125);
    }

    SignalKernelGraphQuery realHsDacMemberGraphQuery;
    realHsDacMemberGraphQuery.signalName = QStringLiteral("HS_DAC");
    realHsDacMemberGraphQuery.signalAccessPath =
        QStringLiteral("HS_DAC.DA_clk_11");
    realHsDacMemberGraphQuery.fileName = topPath;
    realHsDacMemberGraphQuery.moduleName = QStringLiteral("rtl_top");
    const SignalKernelGraphReport realHsDacMemberGraph =
        realComputedGraphService.buildSignalKernelGraph(
            realHsDacMemberGraphQuery);
    bool sawRealHsDacMemberInput = false;
    for (const SignalKernelGraphNode& node : realHsDacMemberGraph.inputs) {
        sawRealHsDacMemberInput =
            sawRealHsDacMemberInput
            || (node.displayName == QStringLiteral("clk_main")
                && node.preciseEvidence
                && node.previewCodeLink.line == 125);
    }
    expectBool("real workspace relationship extracts struct member access",
               sawHsDacClockMemberRelationship,
               true);
    expectBool("real workspace signal kernel graph assignment outputs",
               realClockGraph.found
                   && !realClockGraph.outputs.isEmpty()
                   && sawRealClockGraphHsDacMember,
               true);
    expectBool("real workspace signal kernel graph struct member input",
               realHsDacMemberGraph.found
                   && realHsDacMemberGraph.kernel.displayName
                       == QStringLiteral("HS_DAC.DA_clk_11")
                   && sawRealHsDacMemberInput,
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
    bool sawRealBogusRegIsIniFsm = false;
    bool sawRealEmptyTransitionFsm = false;
    for (const FsmGraph& graph : realFsmReport.graphs) {
        sawRealBogusRegIsIniFsm = sawRealBogusRegIsIniFsm
            || graph.stateRegisterDisplayName == QStringLiteral("reg_is_ini");
        sawRealEmptyTransitionFsm = sawRealEmptyTransitionFsm
            || graph.transitionRows.isEmpty();
        if (graph.stateRegisterDisplayName
            != QStringLiteral("phy_pass_thrg_cfg_cs")) {
            continue;
        }
        sawRealPhyPassFsm = graph.nextStateSignalDisplayName
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
            && !graph.stateRows.first().stateDisplayName.isEmpty()
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
    expectBool("real workspace fsm graph filters non-case registers",
               !sawRealBogusRegIsIniFsm && !sawRealEmptyTransitionFsm,
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

    StateTransitionGraphService::getInstance()->setSemanticIndex(&index);
    QWidget realStatePanelHost;
    RtlInsightsPanelCoordinator realStatePanel(&realStatePanelHost);
    realStatePanel.showStateTransitionGraphForSignal(
        chlCtrlPath,
        QStringLiteral("chl_ctrl"),
        QStringLiteral("phy_pass_thrg_cfg_ns"));
    const QStringList realStateLayoutSummaries =
        realStatePanel.graphElementSummariesForTest();
    QGraphicsView* realStateGraphView = realStatePanel.graphView();
    const QRectF realStateBounds =
        realStateGraphView && realStateGraphView->scene()
            ? realStateGraphView->scene()->itemsBoundingRect()
            : QRectF();
    const QRectF realStateFitRect = realStatePanel.graphLastFitRectForTest();
    const QRectF realStateSceneRect =
        realStateGraphView && realStateGraphView->scene()
            ? realStateGraphView->scene()->sceneRect()
            : QRectF();
    const qreal realStateInitialZoom =
        realStatePanel.graphCurrentZoomForTest();
    bool sawRealPhyPassAliasLayout = false;
    bool sawRealPhyPassAliasCanonicalDetail = false;
    bool sawRealPhyPassFarAlias = false;
    QSet<int> realPhyPassStateXs;
    QSet<int> realPhyPassStateYs;
    int realPhyPassMaxStateX = std::numeric_limits<int>::min();
    int realPhyPassMinStateY = std::numeric_limits<int>::max();
    int realPhyPassMaxStateY = std::numeric_limits<int>::min();
    for (const QString& summary : realStateLayoutSummaries) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 8)
            continue;
        if (parts.at(0) == QStringLiteral("state")) {
            const int x = parts.at(4).toInt();
            const int y = parts.at(5).toInt();
            realPhyPassStateXs.insert(x);
            realPhyPassStateYs.insert(y);
            realPhyPassMaxStateX = qMax(realPhyPassMaxStateX, x);
            realPhyPassMinStateY = qMin(realPhyPassMinStateY, y);
            realPhyPassMaxStateY = qMax(realPhyPassMaxStateY, y);
        }
    }
    for (const QString& summary : realStateLayoutSummaries) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 8)
            continue;
        if (parts.at(0) == QStringLiteral("state-alias")) {
            sawRealPhyPassAliasLayout = true;
            sawRealPhyPassAliasCanonicalDetail =
                sawRealPhyPassAliasCanonicalDetail
                || (parts.at(2) == QStringLiteral("alias")
                    && parts.at(3).contains(QStringLiteral("canonical state")));
            const int aliasX = parts.at(4).toInt();
            const int aliasY = parts.at(5).toInt();
            sawRealPhyPassFarAlias = sawRealPhyPassFarAlias
                || aliasX > realPhyPassMaxStateX + 320
                || aliasY < realPhyPassMinStateY - 220
                || aliasY > realPhyPassMaxStateY + 220;
        }
    }
    bool realPhyPassLabelsOffPath = true;
    bool realPhyPassPathsClearNodes = true;
    bool sawRealPhyPassOuterRoute = false;
    int realPhyPassLabelChecks = 0;
    int realPhyPassClearChecks = 0;
    for (const QString& summary :
         realStatePanel.graphEdgeGeometrySummariesForTest()) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 17 || parts.at(0) != QStringLiteral("transition"))
            continue;
        realPhyPassLabelsOffPath =
            realPhyPassLabelsOffPath && parts.at(14).toInt() >= 16;
        realPhyPassPathsClearNodes =
            realPhyPassPathsClearNodes
            && parts.at(15) == QStringLiteral("clear");
        sawRealPhyPassOuterRoute = sawRealPhyPassOuterRoute
            || parts.at(12) == QStringLiteral("outerBackEdge");
        ++realPhyPassLabelChecks;
        ++realPhyPassClearChecks;
    }
    expectBool("real workspace fsm graph chl_ctrl layout spreads states",
               realPhyPassStateXs.size() >= 4
                   && realPhyPassStateYs.size() >= 2,
               true);
    expectBool("real workspace fsm graph chl_ctrl layout stays compact",
               realStateBounds.isValid()
                   && realStateBounds.width() < 2700.0
                   && realStateBounds.height() < 820.0
                   && realStateBounds.width()
                          / qMax<qreal>(1.0, realStateBounds.height())
                       < 6.0,
               true);
    expectBool("real workspace fsm graph initial fit uses state body",
               realStateFitRect.isValid()
                   && realStateBounds.isValid()
                   && realStateSceneRect.isValid()
                   && realStateFitRect.height() < realStateSceneRect.height()
                   && realStateFitRect.width() <= realStateSceneRect.width()
                   && realStateInitialZoom > 0.08,
               true);
    expectBool("real workspace fsm graph chl_ctrl avoids far aliases",
               (!sawRealPhyPassAliasLayout
                || (sawRealPhyPassAliasCanonicalDetail
                    && !sawRealPhyPassFarAlias)),
               true);
    expectBool("real workspace phy_pass_thrg edges route outside cleanly",
               realPhyPassLabelChecks == realStatePanel.graphEdgeItemCountForTest()
                   && realPhyPassClearChecks == realPhyPassLabelChecks
                   && realPhyPassLabelsOffPath
                   && realPhyPassPathsClearNodes
                   && sawRealPhyPassOuterRoute,
               true);

    QWidget realPhyCfgPanelHost;
    RtlInsightsPanelCoordinator realPhyCfgPanel(&realPhyCfgPanelHost);
    realPhyCfgPanel.showStateTransitionGraphForSignal(
        chlCtrlPath,
        QStringLiteral("chl_ctrl"),
        QStringLiteral("phy_cfg_ns"));
    const QString realPhyCfgSummaries =
        realPhyCfgPanel.graphElementSummariesForTest().join(QLatin1Char('\n'));
    bool realPhyCfgLabelsOffPath = true;
    bool realPhyCfgPathsClearNodes = true;
    bool sawRealPhyCfgOuterRoute = false;
    bool realPhyCfgC1AvoidsC9 = false;
    bool realPhyCfgC9AvoidsC1 = false;
    bool realPhyCfgC17AvoidsC7 = false;
    bool realPhyCfgC7AvoidsC17 = false;
    bool sawRealPhyCfgC18Direct = false;
    int realPhyCfgLabelChecks = 0;
    int realPhyCfgClearChecks = 0;
    const QString realPhyCfgCanvasText =
        realPhyCfgPanel.graphTextItemsForTest().join(QLatin1Char('\n'));
    for (const QString& summary :
         realPhyCfgPanel.graphEdgeGeometrySummariesForTest()) {
        const QStringList parts = summary.split(QLatin1Char('|'));
        if (parts.size() < 17 || parts.at(0) != QStringLiteral("transition"))
            continue;
        realPhyCfgLabelsOffPath =
            realPhyCfgLabelsOffPath && parts.at(14).toInt() >= 16;
        realPhyCfgPathsClearNodes =
            realPhyCfgPathsClearNodes && parts.at(15) == QStringLiteral("clear");
        sawRealPhyCfgOuterRoute = sawRealPhyCfgOuterRoute
            || parts.at(12) == QStringLiteral("outerBackEdge");
        if (parts.size() >= 19) {
            const QString badge = parts.at(3);
            const QString crossingIds = parts.at(18);
            realPhyCfgC1AvoidsC9 =
                realPhyCfgC1AvoidsC9
                || (badge == QStringLiteral("C1")
                    && !crossingIds.split(QLatin1Char(','),
                                          Qt::SkipEmptyParts)
                            .contains(QStringLiteral("C9")));
            realPhyCfgC9AvoidsC1 =
                realPhyCfgC9AvoidsC1
                || (badge == QStringLiteral("C9")
                    && !crossingIds.split(QLatin1Char(','),
                                          Qt::SkipEmptyParts)
                            .contains(QStringLiteral("C1")));
            realPhyCfgC17AvoidsC7 =
                realPhyCfgC17AvoidsC7
                || (badge == QStringLiteral("C17")
                    && !crossingIds.split(QLatin1Char(','),
                                          Qt::SkipEmptyParts)
                            .contains(QStringLiteral("C7")));
            realPhyCfgC7AvoidsC17 =
                realPhyCfgC7AvoidsC17
                || (badge == QStringLiteral("C7")
                    && !crossingIds.split(QLatin1Char(','),
                                          Qt::SkipEmptyParts)
                            .contains(QStringLiteral("C17")));
            sawRealPhyCfgC18Direct =
                sawRealPhyCfgC18Direct
                || (badge == QStringLiteral("C18")
                    && parts.at(8) == QStringLiteral("line")
                    && parts.at(12) == QStringLiteral("normal"));
        }
        ++realPhyCfgLabelChecks;
        ++realPhyCfgClearChecks;
    }
    expectBool("real workspace phy_cfg canvas omits duplicated title caption",
               !realPhyCfgCanvasText.contains(
                   QStringLiteral("State Transition Graph"))
                   && !realPhyCfgCanvasText.contains(
                       QStringLiteral("phy_cfg_cs  ->  phy_cfg_ns"))
                   && !realPhyCfgCanvasText.contains(
                       QStringLiteral("phy_cfg_cs -> phy_cfg_ns")),
               true);
    expectBool("real workspace phy_cfg graph covers layer shield state",
               realPhyCfgPanel.graphNodeItemCountForTest() >= 8
                   && realPhyCfgSummaries.contains(
                       QStringLiteral("S_ALL_LAYER_INJ_SHIELD")),
               true);
    expectBool("real workspace phy_cfg edges route outside cleanly",
               realPhyCfgLabelChecks == realPhyCfgPanel.graphEdgeItemCountForTest()
                   && realPhyCfgClearChecks == realPhyCfgLabelChecks
                   && realPhyCfgLabelsOffPath
                   && realPhyCfgPathsClearNodes
                   && sawRealPhyCfgOuterRoute,
               true);
    expectBool("real workspace phy_cfg C1 C9 avoid sampled crossing",
               realPhyCfgC1AvoidsC9 && realPhyCfgC9AvoidsC1,
               true);
    expectBool("real workspace phy_cfg C17 C7 avoid sampled crossing",
               realPhyCfgC17AvoidsC7 && realPhyCfgC7AvoidsC17,
               true);
    expectBool("real workspace phy_cfg C18 stays direct normal line",
               sawRealPhyCfgC18Direct,
               true);
    StateTransitionGraphService::getInstance()->setSemanticIndex(
        SemanticIndex::getInstance());

    QList<SemanticSymbolRecord> realBeforeDiffRecords;
    realBeforeDiffRecords.append(rtlTopRecord);
    realBeforeDiffRecords.append(packageRecord);
    realBeforeDiffRecords.append(interfaceInstRecord);
    QList<SemanticSymbolRecord> realAfterDiffRecords = realBeforeDiffRecords;
    realAfterDiffRecords.append(interfaceRecord);
    realAfterDiffRecords.append(packageTypedefRecord);
    QList<SemanticRelationship> realAfterDiffRelationships;
    if (rtlTopRecord.isValid() && interfaceInstRecord.isValid()) {
        realAfterDiffRelationships.append(semanticFixtureRelationship(
            rtlTopRecord,
            interfaceInstRecord,
            SymbolRelationshipEngine::REFERENCES));
    }
    SemanticDiagnostic realAfterDiffDiagnostic;
    realAfterDiffDiagnostic.fileName = topPath;
    realAfterDiffDiagnostic.line = 63;
    realAfterDiffDiagnostic.column = 13;
    realAfterDiffDiagnostic.message = QStringLiteral("real diff diagnostic");
    realAfterDiffDiagnostic.severity = SemanticDiagnostic::Warning;
    auto realBeforeDiffSnapshot = sharedSnapshotFromRecords(
        realBeforeDiffRecords,
        QList<SemanticRelationship>(),
        QList<SemanticDiagnostic>());
    auto realAfterDiffSnapshot = sharedSnapshotFromRecords(
        realAfterDiffRecords,
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
                && change.symbolDisplayName == QStringLiteral("lr_genr_if")
                && change.categoryGroupDisplayName == QStringLiteral("Interfaces"));
        sawRealDiffInterfaceMetadata = sawRealDiffInterfaceMetadata
            || (change.symbolDisplayName == QStringLiteral("lr_genr_if")
                && change.symbolTypeDisplayName == QStringLiteral("interface")
                && change.scopeDisplayName == QStringLiteral("global")
                && change.sourceRoleDisplayName == QStringLiteral("design source")
                && change.detailDisplayName.contains(QStringLiteral("global")));
        sawRealDiffInterfaceAfterMetadata = sawRealDiffInterfaceAfterMetadata
            || (change.symbolDisplayName == QStringLiteral("lr_genr_if")
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
            || (change.symbolDisplayName == QStringLiteral("lr_genr_if")
                && !change.codeLink.fileName.isEmpty()
                && change.codeLink.line > 0
                && !change.codeLink.fileDisplayName.isEmpty()
                && !change.codeLink.lineDisplayName.isEmpty());
        sawRealDiffType = sawRealDiffType
            || (change.category == SemanticDiffSymbolCategory::Type
                && change.symbolDisplayName == QStringLiteral("cpld_sw_sp")
                && change.categoryGroupDisplayName == QStringLiteral("Types"));
        sawRealDiffTypeMetadata = sawRealDiffTypeMetadata
            || (change.symbolDisplayName == QStringLiteral("cpld_sw_sp")
                && change.symbolTypeDisplayName == QStringLiteral("typedef")
                && change.scopeDisplayName == QStringLiteral("scope gl_pkg")
                && change.sourceRoleDisplayName == QStringLiteral("design source")
                && change.detailDisplayName.contains(QStringLiteral("scope gl_pkg")));
        sawRealDiffTypeAfterMetadata = sawRealDiffTypeAfterMetadata
            || (change.symbolDisplayName == QStringLiteral("cpld_sw_sp")
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
            || (change.symbolDisplayName == QStringLiteral("cpld_sw_sp")
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

static void runMacroDefineSemanticFixture()
{
    printf("\n-- macro / define semantic fixture --\n");

    QTemporaryDir macroDir;
    expectBool("macro temp dir created", macroDir.isValid(), true);
    if (!macroDir.isValid())
        return;

    const QString headerPath =
        normalizedPath(macroDir.filePath(QStringLiteral("macro_defs.svh")));
    const QString sourcePath =
        normalizedPath(macroDir.filePath(QStringLiteral("macro_use.sv")));
    const QString headerContent = QStringLiteral(
        "`define SHARED_MACRO 42\n"
        "`define FUNC_MACRO(a, b) ((a) + (b))\n"
        "`define LOCAL_MACRO 99\n");
    const QString sourceContent = QStringLiteral(
        "`include \"macro_defs.svh\"\n"
        "`define LOCAL_MACRO 8\n"
        "`define LOCAL_SWITCH\n"
        "`ifdef WSDEF\n"
        "module macro_top;\n"
        "  localparam int A = `LOCAL_MACRO;\n"
        "  localparam int B = `FUNC_MACRO(1, 2);\n"
        "  localparam int C = `SHARED_MACRO;\n"
        "`ifdef LOCAL_SWITCH\n"
        "  wire active_local;\n"
        "`else\n"
        "  wire inactive_local;\n"
        "`endif\n"
        "`undef LOCAL_SWITCH\n"
        "`ifdef LOCAL_SWITCH\n"
        "  wire inactive_after_undef;\n"
        "`endif\n"
        "`else\n"
        "  wire inactive_workspace;\n"
        "`endif\n"
        "`ifndef WSDEF\n"
        "  wire inactive_ifndef;\n"
        "`elsif BAR\n"
        "  wire inactive_elsif;\n"
        "`else\n"
        "  wire active_else;\n"
        "`endif\n"
        "endmodule\n");

    expectBool("macro header written",
               writeTextFile(headerPath, headerContent),
               true);
    expectBool("macro source written",
               writeTextFile(sourcePath, sourceContent),
               true);

    const QStringList files{headerPath, sourcePath};
    const QStringList includeDirs{macroDir.path()};
    QHash<QString, QString> defines;
    defines.insert(QStringLiteral("WSDEF"), QStringLiteral("1"));
    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractWorkspaceSymbolRecords(files, includeDirs, defines);
    QHash<QString, QString> fileContents;
    fileContents.insert(headerPath, headerContent);
    fileContents.insert(sourcePath, sourceContent);
    SemanticIndex index;
    index.setSnapshot(sharedSnapshotFromRecords(records, {}, {}, fileContents));

    auto lineNumberContaining = [](const QString& content, const QString& needle) {
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).contains(needle))
                return i + 1;
        }
        return -1;
    };
    auto lineTextContaining = [](const QString& content, const QString& needle) {
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (line.contains(needle))
                return line;
        }
        return QString();
    };

    NavigationService navigationService(&index);
    NavigationSymbolOutlineQuery headerOutlineQuery;
    headerOutlineQuery.fileName = headerPath;
    const QList<SymbolOutlineGroup> headerOutline =
        navigationService.findSymbolOutline(headerOutlineQuery);
    QSet<QString> headerMacroNames;
    bool sawDirectiveOutline = false;
    for (const SymbolOutlineGroup& group : headerOutline) {
        for (const SymbolOutlineSymbolRow& row : group.symbolRows) {
            if (row.symbolRecord.declarationKind
                == SymbolTaxonomy::DeclarationKind::Macro) {
                headerMacroNames.insert(row.displayName);
            }
            sawDirectiveOutline = sawDirectiveOutline
                || row.displayName == QStringLiteral("ifdef")
                || row.displayName == QStringLiteral("ifndef")
                || row.displayName == QStringLiteral("else")
                || row.displayName == QStringLiteral("endif");
        }
    }
    expectBool("macro outline includes object macro",
               headerMacroNames.contains(QStringLiteral("SHARED_MACRO")),
               true);
    expectBool("macro outline includes function macro name",
               headerMacroNames.contains(QStringLiteral("FUNC_MACRO")),
               true);
    expectBool("macro outline excludes preprocessor branches",
               sawDirectiveOutline,
               false);

    NavigationSymbolOutlineQuery sourceOutlineQuery;
    sourceOutlineQuery.fileName = sourcePath;
    const QList<SymbolOutlineGroup> sourceOutline =
        navigationService.findSymbolOutline(sourceOutlineQuery);
    bool sourceOutlineHasLocalMacro = false;
    for (const SymbolOutlineGroup& group : sourceOutline) {
        for (const SymbolOutlineSymbolRow& row : group.symbolRows) {
            sourceOutlineHasLocalMacro = sourceOutlineHasLocalMacro
                || (row.symbolRecord.declarationKind
                    == SymbolTaxonomy::DeclarationKind::Macro
                    && row.displayName == QStringLiteral("LOCAL_MACRO"));
        }
    }
    expectBool("macro outline includes current file define",
               sourceOutlineHasLocalMacro,
               true);

    const QString localLine =
        lineTextContaining(sourceContent, QStringLiteral("`LOCAL_MACRO"));
    const int localColumn = localLine.indexOf(QStringLiteral("`LOCAL_MACRO"));
    const SourceIdentifierTarget localIdentifier =
        SourceNavigationService::getInstance()->identifierAtColumn(localLine,
                                                                   localColumn);
    expectBool("macro identifier matches at backtick",
               localIdentifier.matched
                   && localIdentifier.identifier == QStringLiteral("LOCAL_MACRO"),
               true);

    DefinitionNavigationService definitionService(&index);
    DefinitionNavigationContext localContext;
    localContext.symbolName = localIdentifier.identifier;
    localContext.fileName = sourcePath;
    localContext.moduleName = QStringLiteral("macro_top");
    localContext.lineText = localLine;
    localContext.cursorLine =
        lineNumberContaining(sourceContent, QStringLiteral("`LOCAL_MACRO"));
    localContext.column = localColumn;
    const DefinitionNavigationTarget localTarget =
        definitionService.resolveTarget(
            definitionService.navigationQueryForContext(localContext));
    expectBool("macro goto prefers current file define",
               localTarget.found
                   && normalizedPath(localTarget.fileName) == sourcePath
                   && localTarget.line == 2,
               true);

    const QString sharedLine =
        lineTextContaining(sourceContent, QStringLiteral("`SHARED_MACRO"));
    const int sharedColumn = sharedLine.indexOf(QStringLiteral("SHARED_MACRO"));
    const SourceIdentifierTarget sharedIdentifier =
        SourceNavigationService::getInstance()->identifierAtColumn(sharedLine,
                                                                   sharedColumn);
    DefinitionNavigationContext sharedContext;
    sharedContext.symbolName = sharedIdentifier.identifier;
    sharedContext.fileName = sourcePath;
    sharedContext.moduleName = QStringLiteral("macro_top");
    sharedContext.lineText = sharedLine;
    sharedContext.cursorLine =
        lineNumberContaining(sourceContent, QStringLiteral("`SHARED_MACRO"));
    sharedContext.column = sharedColumn;
    const DefinitionNavigationTarget sharedTarget =
        definitionService.resolveTarget(
            definitionService.navigationQueryForContext(sharedContext));
    expectBool("macro goto resolves workspace include define",
               sharedTarget.found
                   && normalizedPath(sharedTarget.fileName) == headerPath
                   && sharedTarget.line == 1,
               true);

    SymbolHoverService hoverService(&index);
    const QString functionLine =
        lineTextContaining(sourceContent, QStringLiteral("`FUNC_MACRO"));
    EditorSemanticContext hoverContext;
    hoverContext.fileName = sourcePath;
    hoverContext.moduleName = QStringLiteral("macro_top");
    hoverContext.documentText = sourceContent;
    hoverContext.lineText = functionLine;
    hoverContext.cursorLine =
        lineNumberContaining(sourceContent, QStringLiteral("`FUNC_MACRO"));
    hoverContext.column = functionLine.indexOf(QStringLiteral("`FUNC_MACRO"));
    const SymbolHoverReport hoverReport =
        hoverService.hoverForContext(hoverContext);
    expectBool("macro hover resolves function macro",
               hoverReport.available
                   && hoverReport.displayKind == QStringLiteral("macro")
                   && normalizedPath(hoverReport.definitionFile) == headerPath
                   && hoverReport.definitionLine == 2,
               true);
    expectBool("macro hover shows params and body",
               hoverReport.macroSignatureText
                       == QStringLiteral("FUNC_MACRO(a, b)")
                   && hoverReport.macroBodyText.contains(
                       QStringLiteral("((a) + (b))")),
               true);

    ReferenceService referenceService(&index);
    ReferenceQuery referenceQuery;
    referenceQuery.symbolName = QStringLiteral("FUNC_MACRO");
    referenceQuery.fileName = sourcePath;
    referenceQuery.moduleName = QStringLiteral("macro_top");
    referenceQuery.workspaceFiles = files;
    const ReferenceReport referenceReport =
        referenceService.findReferenceReport(referenceQuery);
    bool sawMacroDefinitionReference = false;
    bool sawMacroUseReference = false;
    for (const ReferenceResult& reference : referenceReport.references) {
        sawMacroDefinitionReference = sawMacroDefinitionReference
            || (normalizedPath(
                    reference.referencingSymbolRecord.location.fileName)
                    == headerPath
                && reference.referencingSymbolRecord.location.startLine == 2);
        sawMacroUseReference = sawMacroUseReference
            || (normalizedPath(
                    reference.referencingSymbolRecord.location.fileName)
                    == sourcePath
                && reference.referencingSymbolRecord.location.startLine
                    == hoverContext.cursorLine);
    }
    expectBool("macro references include define and use",
               referenceReport.totalCount == 2
                   && sawMacroDefinitionReference
                   && sawMacroUseReference,
               true);

    const QString badMacroContent = QStringLiteral("module bad_macro;\n"
                                                   "  `UNDEFINED_MACRO\n"
                                                   "endmodule\n");
    const QList<SemanticDiagnostic> macroDiagnostics =
        slang.extractDiagnostics(
            normalizedPath(macroDir.filePath(QStringLiteral("bad_macro.sv"))),
            badMacroContent);
    bool sawUndefinedMacroDiagnostic = false;
    for (const SemanticDiagnostic& diagnostic : macroDiagnostics) {
        sawUndefinedMacroDiagnostic = sawUndefinedMacroDiagnostic
            || (diagnostic.owner == SemanticDiagnostic::SemanticIndexOwner
                && diagnostic.line == 2
                && diagnostic.column == 4
                && diagnostic.message.contains(
                    QStringLiteral("Undefined macro `UNDEFINED_MACRO`")));
    }
    expectBool("undefined macro diagnostic is explicit",
               sawUndefinedMacroDiagnostic,
               true);

    SemanticDecorationService decorationService(&index);
    SemanticDecorationQuery decorationQuery;
    decorationQuery.fileName = sourcePath;
    decorationQuery.documentText = sourceContent;
    decorationQuery.configuredDefines = defines;
    const SemanticDecorationReport decorationReport =
        decorationService.decorationsForDocument(decorationQuery);
    bool sawInactiveWorkspaceElse = false;
    bool sawInactiveLocalElse = false;
    bool sawInactiveAfterUndef = false;
    bool sawInactiveIfndef = false;
    bool sawInactiveElsif = false;
    bool sawActiveLocalGrayed = false;
    bool sawActiveElseGrayed = false;
    for (const SemanticDecoration& decoration : decorationReport.decorations) {
        if (decoration.role != SemanticDecorationRole::InactivePreprocessorBranch)
            continue;
        const QString decoratedText =
            sourceContent.mid(decoration.startPosition, decoration.length);
        sawInactiveWorkspaceElse = sawInactiveWorkspaceElse
            || decoratedText.contains(QStringLiteral("inactive_workspace"));
        sawInactiveLocalElse = sawInactiveLocalElse
            || decoratedText.contains(QStringLiteral("inactive_local"));
        sawInactiveAfterUndef = sawInactiveAfterUndef
            || decoratedText.contains(QStringLiteral("inactive_after_undef"));
        sawInactiveIfndef = sawInactiveIfndef
            || decoratedText.contains(QStringLiteral("inactive_ifndef"));
        sawInactiveElsif = sawInactiveElsif
            || decoratedText.contains(QStringLiteral("inactive_elsif"));
        sawActiveLocalGrayed = sawActiveLocalGrayed
            || decoratedText.contains(QStringLiteral("wire active_local"));
        sawActiveElseGrayed = sawActiveElseGrayed
            || decoratedText.contains(QStringLiteral("wire active_else"));
    }
    expectBool("inactive branch grays configured define else",
               sawInactiveWorkspaceElse,
               true);
    expectBool("inactive branch honors local define",
               sawInactiveLocalElse && !sawActiveLocalGrayed,
               true);
    expectBool("inactive branch honors local undef",
               sawInactiveAfterUndef,
               true);
    expectBool("inactive branch handles ifndef elsif else",
               sawInactiveIfndef && sawInactiveElsif && !sawActiveElseGrayed,
               true);
}

static void runWorkspaceRelationshipCancellationFixture()
{
    printf("\n-- workspace relationship cancellation fixture --\n");

    QTemporaryDir relationshipDir;
    expectBool("workspace relationship cancel temp dir created",
               relationshipDir.isValid(),
               true);
    if (!relationshipDir.isValid())
        return;

    const QString leafPath =
        normalizedPath(relationshipDir.filePath(QStringLiteral("rel_leaf.sv")));
    const QString topPath =
        normalizedPath(relationshipDir.filePath(QStringLiteral("rel_top.sv")));
    expectBool("workspace relationship cancel leaf written",
               writeTextFile(
                   leafPath,
                   QStringLiteral(
                       "module rel_leaf(input logic i, output logic o);\n"
                       "  assign o = i;\n"
                       "endmodule\n")),
               true);
    expectBool("workspace relationship cancel top written",
               writeTextFile(
                   topPath,
                   QStringLiteral(
                       "module rel_top(input logic a, output logic b);\n"
                       "  rel_leaf u_leaf(.i(a), .o(b));\n"
                       "  assign b = a;\n"
                       "endmodule\n")),
               true);

    const QStringList files{leafPath, topPath};
    const QStringList includeDirs{relationshipDir.path()};
    SlangManager slang;
    const QHash<QString, RelationshipExtractionInfo> uncancelled =
        slang.extractWorkspaceRelationshipInfo(files, includeDirs);
    expectBool("workspace relationship fixture extracts facts",
               !relationshipInfoEmpty(uncancelled.value(topPath)),
               true);

    int cancelChecks = 0;
    const QHash<QString, RelationshipExtractionInfo> cancelled =
        slang.extractWorkspaceRelationshipInfo(
            files,
            includeDirs,
            QHash<QString, QString>{},
            [&cancelChecks]() {
                ++cancelChecks;
                return cancelChecks >= 8;
            });
    bool cancelledFactsEmpty = true;
    for (auto it = cancelled.constBegin(); it != cancelled.constEnd(); ++it)
        cancelledFactsEmpty =
            cancelledFactsEmpty && relationshipInfoEmpty(it.value());
    expectBool("workspace relationship Slang honors cancel boundary",
               cancelledFactsEmpty && cancelChecks >= 8,
               true);

    SmartRelationshipBuilder builder(nullptr, &slang);
    builder.cancelAnalysis();
    const QHash<QString, RelationshipExtractionInfo> builderCancelled =
        builder.extractWorkspaceRelationshipInfo(files,
                                                 includeDirs,
                                                 QHash<QString, QString>{});
    bool builderCancelledFactsEmpty = true;
    for (auto it = builderCancelled.constBegin();
         it != builderCancelled.constEnd();
         ++it) {
        builderCancelledFactsEmpty =
            builderCancelledFactsEmpty && relationshipInfoEmpty(it.value());
    }
    expectBool("relationship builder forwards workspace cancel",
               builderCancelledFactsEmpty,
               true);

    const QList<SemanticSymbolRecord> records =
        slang.extractWorkspaceSymbolRecords(files, includeDirs);
    QHash<QString, QString> fileContents;
    fileContents.insert(leafPath, loadTextFile(leafPath));
    fileContents.insert(topPath, loadTextFile(topPath));
    const auto baseSnapshot =
        sharedSnapshotFromRecords(records, {}, {}, fileContents);
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    const auto previousSnapshot = semanticIndex->snapshot();
    semanticIndex->setSnapshot(baseSnapshot);
    const SemanticSnapshotToken baseToken =
        semanticIndex->beginRelationshipAnalysisSnapshot();
    ProjectSnapshot project;
    project.workspaceRoot = relationshipDir.path();
    project.allFiles = files;
    project.systemVerilogFiles = files;
    project.includeDirs = includeDirs;
    SmartRelationshipBuilder workerBuilder(nullptr, &slang);
    workerBuilder.cancelAnalysis();
    const WorkspaceRelationshipAnalysisResult workerCancelled =
        RelationshipAnalysisWorker::analyzeWorkspace(&workerBuilder,
                                                     project,
                                                     baseToken);
    expectBool("relationship worker marks cancelled result",
               workerCancelled.cancelled
                   && workerCancelled.fileRelationships.isEmpty()
                   && workerCancelled.semanticSnapshot == baseToken.snapshot,
               true);
    expectBool("relationship worker cancelled telemetry",
               workerCancelled.totalFiles == files.size()
                   && workerCancelled.processedFiles == 0
                   && workerCancelled.relationshipCount == 0
                   && workerCancelled.elapsedMs >= 0,
               true);

    SymbolRelationshipEngine cancelledEngine;
    RelationshipResultPublisher publisher;
    publisher.setRelationshipEngine(&cancelledEngine);
    expectBool("relationship publisher rejects cancelled workspace result",
               !publisher.applyWorkspaceResult(workerCancelled)
                   && semanticIndex->snapshot() == baseSnapshot,
               true);
    semanticIndex->setSnapshot(previousSnapshot);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    SlangManager slang;

    SymbolRelationshipEngine engine;

    runInlineRelationshipRegression(slang, engine);
    runMultiFileRelationshipFixture(slang, engine);
    runModuleBriefServiceFixture();
    runScopeBandServiceFixture();
    runSignalJourneyServiceFixture();
    runSignalUsageHotspotServiceFixture();
    runClockResetDomainServiceFixture();
    runFsmGraphServiceFixture();
    runSemanticDiffServiceFixture();
    runImportAwarePackageVisibilityFixture();
    runRealWorkspaceIncludeFixture();
    runPostWorkspaceDiagnosticFixture();
    runMacroDefineSemanticFixture();
    runWorkspaceRelationshipCancellationFixture();

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
