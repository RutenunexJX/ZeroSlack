#include "effectivevalueservice.h"
#include "ghostannotationservice.h"
#include "documentmodel.h"
#include "mycodeeditor.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "slangsymbolpresentation.h"
#include "symbolanalyzer.h"
#include "symbolhoverservice.h"
#include "workspacesymbolanalysiscontroller.h"
#include "editorsemanticcontextservice.h"

#include <slang/ast/Compilation.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/text/SourceManager.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>

namespace {
int failures = 0;

void expect(const char* name, bool condition)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

SemanticSymbolRecord findRecord(
    const QList<SemanticSymbolRecord>& records,
    const QString& name,
    SymbolTaxonomy::CollectorKind kind)
{
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == name && record.collectorKind == kind)
            return record;
    }
    return {};
}

QList<SemanticSymbolRecord> findRecords(
    const QList<SemanticSymbolRecord>& records,
    const QString& name,
    SymbolTaxonomy::CollectorKind kind)
{
    QList<SemanticSymbolRecord> result;
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == name && record.collectorKind == kind)
            result.append(record);
    }
    return result;
}

std::shared_ptr<const SemanticIndexSnapshot> snapshot(
    const QList<SemanticSymbolRecord>& records,
    const QString& fileName,
    const QString& source)
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records, {}, {}, {{fileName, source}}));
}

HierarchyInstanceContext instance(const QString& path)
{
    HierarchyInstanceContext context;
    context.workspacePath = QStringLiteral("workspace");
    context.activeTopModule = QStringLiteral("top");
    context.instancePath = path;
    return context;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}

EffectiveValueResult resolve(EffectiveValueService& service,
                             const SemanticSymbolRecord& record,
                             const QString& source,
                             const HierarchyInstanceContext& context = {},
                             std::uint64_t documentRevision = 0)
{
    EffectiveValueQuery query;
    query.symbol = record;
    query.documentText = source;
    query.instanceContext = context;
    query.documentRevision = documentRevision;
    return service.resolve(query);
}

void runSlangEffectiveValueRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_value_fixture.sv"));
    const QString source = QStringLiteral(
        "localparam int CU_VALUE = 17;\n"
        "typedef enum logic [7:0] { CU_ENUM = CU_VALUE + 1 } cu_e;\n"
        "package constants_pkg;\n"
        "  parameter logic [3:0] PKG_P = 4'ha;\n"
        "  localparam logic [3:0] PKG_L = PKG_P ^ 4'hf;\n"
        "  typedef enum logic [4:0] {\n"
        "    PKG_ENUM = (PKG_P << 1) | PKG_L\n"
        "  } pkg_e;\n"
        "endpackage\n"
        "module child #(parameter logic [3:0] P = 4'h1) (\n"
        "  input logic [P * 2 - 1:0] data_i\n"
        ");\n"
        "  import constants_pkg::*;\n"
        "  function automatic logic [7:0] bump(input logic [7:0] x);\n"
        "    bump = x + 1;\n"
        "  endfunction\n"
        "  localparam logic signed [79:0] WIDE = {20{P}};\n"
        "  localparam logic [7:0] FOUR_STATE = 8'b10xz_01z1;\n"
        "  localparam string TEXT = \"effective value\";\n"
        "  typedef enum logic [15:0] {\n"
        "    E_CONCAT = {constants_pkg::PKG_P, constants_pkg::PKG_L, 8'h3},\n"
        "    E_ARITH = (P << 8) | bump(8'h2),\n"
        "    E_SHIFT = (16'(P) << 4),\n"
        "    E_COND = P[0] ? 16'h1111 : 16'h2222,\n"
        "    E_NEXT\n"
        "  } e_t;\n"
        "  typedef enum logic signed [15:0] { E_NEG = -16'sd7 } signed_e;\n"
        "  typedef enum logic [79:0] {\n"
        "    E_WIDE_ENUM = 80'h1234_5678_9abc_def0_1234\n"
        "  } wide_e;\n"
        "  typedef enum logic [7:0] { E_XZ = 8'b10xz_01z1 } xz_e;\n"
        "  logic [P * 2 - 1:0] packed_value;\n"
        "  logic [7:0] unpacked_value [P:0];\n"
        "  constants_pkg::pkg_e pkg_state;\n"
        "  cu_e cu_state;\n"
        "  if (1) begin : g_left\n"
        "    localparam int DUP = 11;\n"
        "  end\n"
        "  if (1) begin : g_right\n"
        "    localparam int DUP = 22;\n"
        "  end\n"
        "endmodule\n"
        "module top;\n"
        "  child #(.P(4'h2)) u0(.data_i('0));\n"
        "  child #(.P(4'h3)) u1(.data_i('0));\n"
        "endmodule\n");

    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source);
    SemanticIndex index;
    index.setSnapshot(snapshot(records, fileName, source));
    EffectiveValueService service(&index);

    const SemanticSymbolRecord packageParameter = findRecord(
        records,
        QStringLiteral("PKG_P"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord packageLocalparam = findRecord(
        records,
        QStringLiteral("PKG_L"),
        SymbolTaxonomy::CollectorKind::Localparam);
    expect("package parameter and localparam are collected",
           packageParameter.isValid() && packageLocalparam.isValid());
    const EffectiveValueResult packageParameterValue = resolve(
        service, packageParameter, source, instance(QStringLiteral("top.u1")));
    const EffectiveValueResult packageValue = resolve(
        service, packageLocalparam, source, instance(QStringLiteral("top.u0")));
    expect("package values ignore module instance binding and remain exact",
           packageParameterValue.current()
               && packageParameterValue.valueText
                   == QStringLiteral("4'b1010")
               && !packageParameterValue.instanceBound
               && packageValue.current()
               && !packageValue.instanceBound
               && packageValue.effectiveScopeKind
                   == SemanticEffectiveScopeKind::Package
               && packageValue.qualifiedScopePath
                   == QStringLiteral("constants_pkg")
               && packageValue.scopePath == QStringLiteral("constants_pkg")
               && packageValue.valueText == QStringLiteral("4'b101"));

    const SemanticSymbolRecord packageEnum = findRecord(
        records,
        QStringLiteral("PKG_ENUM"),
        SymbolTaxonomy::CollectorKind::EnumValue);
    const EffectiveValueResult packageEnumValue = resolve(
        service, packageEnum, source, instance(QStringLiteral("top.u0")));
    expect("package typedef enum keeps package effective scope",
           packageEnum.isValid()
               && packageEnum.presentation.effectiveScopeKind
                   == SemanticEffectiveScopeKind::Package
               && packageEnumValue.current()
               && !packageEnumValue.instanceBound
               && packageEnumValue.qualifiedScopePath.startsWith(
                   QStringLiteral("constants_pkg"))
               && packageEnumValue.valueText
                   == QStringLiteral("5'b10101")
               && packageEnumValue.displayValueText
                   == QStringLiteral("21"));

    const SemanticSymbolRecord compilationUnitValue = findRecord(
        records,
        QStringLiteral("CU_VALUE"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const EffectiveValueResult compilationUnitResult = resolve(
        service,
        compilationUnitValue,
        source,
        instance(QStringLiteral("top.u1")));
    expect("compilation-unit value ignores Design instance binding",
           compilationUnitResult.current()
               && !compilationUnitResult.instanceBound
               && compilationUnitResult.effectiveScopeKind
                   == SemanticEffectiveScopeKind::CompilationUnit
               && compilationUnitResult.qualifiedScopePath
                   == QStringLiteral("$unit")
               && compilationUnitResult.valueText
                   == QStringLiteral("17"));

    const SemanticSymbolRecord compilationUnitEnum = findRecord(
        records,
        QStringLiteral("CU_ENUM"),
        SymbolTaxonomy::CollectorKind::EnumValue);
    const EffectiveValueResult compilationUnitEnumResult = resolve(
        service,
        compilationUnitEnum,
        source,
        instance(QStringLiteral("top.u0")));
    expect("compilation-unit typedef enum remains static when referenced by an instance",
           compilationUnitEnumResult.current()
               && !compilationUnitEnumResult.instanceBound
               && compilationUnitEnumResult.effectiveScopeKind
                   == SemanticEffectiveScopeKind::CompilationUnit
               && compilationUnitEnumResult.qualifiedScopePath.startsWith(
                   QStringLiteral("$unit"))
               && compilationUnitEnumResult.valueText
                   == QStringLiteral("8'b10010")
               && compilationUnitEnumResult.displayValueText
                   == QStringLiteral("18"));

    const QList<SemanticSymbolRecord> duplicateLocalparams = findRecords(
        records,
        QStringLiteral("DUP"),
        SymbolTaxonomy::CollectorKind::Localparam);
    QSet<QString> duplicateScopePaths;
    QSet<QString> duplicateValues;
    QSet<QString> duplicateIdentities;
    for (const SemanticSymbolRecord& duplicate : duplicateLocalparams) {
        duplicateScopePaths.insert(
            duplicate.presentation.qualifiedScopePath);
        duplicateValues.insert(
            duplicate.presentation.defaultInfo.valueText);
        duplicateIdentities.insert(
            resolve(service,
                    duplicate,
                    source,
                    instance(QStringLiteral("top.u0")))
                .symbolIdentity);
    }
    expect("same-name nested localparams keep distinct lexical identities",
           duplicateLocalparams.size() == 2
               && duplicateScopePaths.size() == 2
               && duplicateValues
                   == QSet<QString>{QStringLiteral("11"),
                                    QStringLiteral("22")}
               && duplicateIdentities.size() == 2);

    const auto enumValue = [&](const QString& name,
                               const QString& path) {
        return resolve(
            service,
            findRecord(records,
                       name,
                       SymbolTaxonomy::CollectorKind::EnumValue),
            source,
            instance(path));
    };
    const EffectiveValueResult concatU0 = enumValue(
        QStringLiteral("E_CONCAT"), QStringLiteral("top.u0"));
    const EffectiveValueResult concatU1 = enumValue(
        QStringLiteral("E_CONCAT"), QStringLiteral("top.u1"));
    const EffectiveValueResult enumU0 = enumValue(
        QStringLiteral("E_ARITH"), QStringLiteral("top.u0"));
    const EffectiveValueResult enumU1 = enumValue(
        QStringLiteral("E_ARITH"), QStringLiteral("top.u1"));
    const EffectiveValueResult shiftU0 = enumValue(
        QStringLiteral("E_SHIFT"), QStringLiteral("top.u0"));
    const EffectiveValueResult shiftU1 = enumValue(
        QStringLiteral("E_SHIFT"), QStringLiteral("top.u1"));
    const EffectiveValueResult conditionalU0 = enumValue(
        QStringLiteral("E_COND"), QStringLiteral("top.u0"));
    const EffectiveValueResult conditionalU1 = enumValue(
        QStringLiteral("E_COND"), QStringLiteral("top.u1"));
    const EffectiveValueResult nextU0 = enumValue(
        QStringLiteral("E_NEXT"), QStringLiteral("top.u0"));
    const EffectiveValueResult nextU1 = enumValue(
        QStringLiteral("E_NEXT"), QStringLiteral("top.u1"));
    const EffectiveValueResult negativeEnum = enumValue(
        QStringLiteral("E_NEG"), QStringLiteral("top.u0"));
    const EffectiveValueResult wideEnum = enumValue(
        QStringLiteral("E_WIDE_ENUM"), QStringLiteral("top.u0"));
    const EffectiveValueResult unknownEnum = enumValue(
        QStringLiteral("E_XZ"), QStringLiteral("top.u0"));
    expect("enum concat and package references have exact Slang values",
           concatU0.current() && concatU1.current()
               && concatU0.valueText
                   == QStringLiteral("16'b1010010100000011")
               && concatU1.valueText == concatU0.valueText);
    expect("enum arithmetic bitwise expression and constant function follow overrides",
           enumU0.current() && enumU1.current()
               && enumU0.valueText == QStringLiteral("16'b1000000011")
               && enumU1.valueText == QStringLiteral("16'b1100000011")
               && enumU0.displayValueText == QStringLiteral("515")
               && enumU1.displayValueText == QStringLiteral("771")
               && enumU0.bitWidthText == QStringLiteral("16"));
    expect("enum cast and shift have exact per-instance values",
           shiftU0.current() && shiftU1.current()
               && shiftU0.valueText == QStringLiteral("16'b100000")
               && shiftU1.valueText == QStringLiteral("16'b110000"));
    expect("enum conditional expression has exact per-instance values",
           conditionalU0.current() && conditionalU1.current()
               && conditionalU0.valueText
                   == QStringLiteral("16'b10001000100010")
               && conditionalU1.valueText
                   == QStringLiteral("16'b1000100010001"));
    expect("enum implicit increment follows each elaborated predecessor",
           nextU0.current() && nextU1.current()
               && nextU0.valueText
                   == QStringLiteral("16'b10001000100011")
               && nextU1.valueText
                   == QStringLiteral("16'b1000100010010"));
    expect("signed and wide enum display values are exact decimal",
           negativeEnum.current() && wideEnum.current()
               && negativeEnum.valueText
                   == QStringLiteral("-16'sb111")
               && negativeEnum.displayValueText
                   == QStringLiteral("-7")
               && wideEnum.valueText
                   == QStringLiteral(
                       "80'b10010001101000101011001111000100110101011110011011110111100000001001000110100")
               && wideEnum.displayValueText
                   == QStringLiteral("85968058283706962416180"));
    expect("enum display preserves exact X and Z positions when decimal is impossible",
           unknownEnum.current()
               && unknownEnum.valueText.toLower()
                   == QStringLiteral("8'b10xz01z1")
               && unknownEnum.displayValueText
                   == unknownEnum.valueText);

    const SemanticSymbolRecord parameter = findRecord(
        records,
        QStringLiteral("P"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const EffectiveValueResult parameterU0 = resolve(
        service, parameter, source, instance(QStringLiteral("top.u0")));
    const EffectiveValueResult parameterU1 = resolve(
        service, parameter, source, instance(QStringLiteral("top.u1")));
    expect("module parameter uses the exact bound instance value",
           parameterU0.current() && parameterU1.current()
               && parameterU0.valueText == QStringLiteral("4'b10")
               && parameterU1.valueText == QStringLiteral("4'b11")
               && parameterU0.displayValueText.isEmpty()
               && parameterU1.displayValueText.isEmpty());
    const EffectiveValueResult parameterDefault = resolve(
        service, parameter, source);
    expect("unbound module parameter is explicitly the Slang default",
           parameterDefault.current()
               && parameterDefault.defaultEvaluation
               && !parameterDefault.instanceBound
               && parameterDefault.valueText == QStringLiteral("4'b1")
               && parameterDefault.instancePath.contains(
                   QStringLiteral("Unbound instance")));

    const SemanticSymbolRecord dataPort = findRecord(
        records,
        QStringLiteral("data_i"),
        SymbolTaxonomy::CollectorKind::PortInput);
    const SemanticSymbolRecord packedValue = findRecord(
        records,
        QStringLiteral("packed_value"),
        SymbolTaxonomy::CollectorKind::Logic);
    const SemanticSymbolRecord unpackedValue = findRecord(
        records,
        QStringLiteral("unpacked_value"),
        SymbolTaxonomy::CollectorKind::Logic);
    const EffectiveValueResult portU0 = resolve(
        service, dataPort, source, instance(QStringLiteral("top.u0")));
    const EffectiveValueResult portU1 = resolve(
        service, dataPort, source, instance(QStringLiteral("top.u1")));
    const EffectiveValueResult packedU0 = resolve(
        service, packedValue, source, instance(QStringLiteral("top.u0")));
    const EffectiveValueResult packedU1 = resolve(
        service, packedValue, source, instance(QStringLiteral("top.u1")));
    const EffectiveValueResult unpackedU0 = resolve(
        service, unpackedValue, source, instance(QStringLiteral("top.u0")));
    const EffectiveValueResult unpackedU1 = resolve(
        service, unpackedValue, source, instance(QStringLiteral("top.u1")));
    expect("parameterized port and packed type widths differ by instance",
           portU0.current() && portU1.current()
               && portU0.bitWidthText == QStringLiteral("4")
               && portU1.bitWidthText == QStringLiteral("6")
               && portU0.packedDimensionsText == QStringLiteral("[3:0]")
               && portU1.packedDimensionsText == QStringLiteral("[5:0]")
               && packedU0.bitWidthText == QStringLiteral("4")
               && packedU1.bitWidthText == QStringLiteral("6")
               && packedU0.packedDimensionsText
                   == QStringLiteral("[3:0]")
               && packedU1.packedDimensionsText
                   == QStringLiteral("[5:0]"));
    expect("parameterized unpacked array dimensions differ by instance",
           unpackedU0.current() && unpackedU1.current()
               && unpackedU0.unpackedDimensionsText
                   == QStringLiteral("[2:0]")
               && unpackedU1.unpackedDimensionsText
                   == QStringLiteral("[3:0]")
               && unpackedU0.unpackedElementCountText
                   == QStringLiteral("3")
               && unpackedU1.unpackedElementCountText
                   == QStringLiteral("4"));

    const SemanticSymbolRecord enumArithmeticRecord = findRecord(
        records,
        QStringLiteral("E_ARITH"),
        SymbolTaxonomy::CollectorKind::EnumValue);
    GhostAnnotationService ghost(&index);
    const auto boundEnumGhostText = [&](const QString& path) {
        GhostAnnotationQuery query;
        query.fileName = fileName;
        query.documentText = source;
        query.instanceContext = instance(path);
        const GhostAnnotationReport report =
            ghost.annotationsForDocument(query);
        for (const GhostAnnotation& annotation : report.annotations) {
            if (annotation.kind == GhostAnnotationKind::EnumValue
                && annotation.line
                    == enumArithmeticRecord.location.startLine) {
                return annotation.text;
            }
        }
        return QString();
    };
    expect("bound enum Ghost displays decimal u0 and u1 Slang values",
           boundEnumGhostText(QStringLiteral("top.u0"))
                   == QStringLiteral("= 515")
               && boundEnumGhostText(QStringLiteral("top.u1"))
                   == QStringLiteral("= 771")
               && enumU0.valueText != enumU1.valueText);

    const SemanticSymbolRecord wide = findRecord(
        records,
        QStringLiteral("WIDE"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const SemanticSymbolRecord fourState = findRecord(
        records,
        QStringLiteral("FOUR_STATE"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const SemanticSymbolRecord text = findRecord(
        records,
        QStringLiteral("TEXT"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const EffectiveValueResult wideValue = resolve(
        service, wide, source, instance(QStringLiteral("top.u1")));
    const EffectiveValueResult fourStateValue = resolve(
        service, fourState, source, instance(QStringLiteral("top.u1")));
    const EffectiveValueResult textValue = resolve(
        service, text, source, instance(QStringLiteral("top.u1")));
    expect("wide value preserves more than 64 bits",
           wideValue.current()
               && wideValue.bitWidthText == QStringLiteral("80")
               && wideValue.signednessText == QStringLiteral("signed")
               && wideValue.valueText
                   == QStringLiteral("80'sd241785163922925834941235"));
    expect("four-state value preserves X and Z",
           fourStateValue.current()
               && fourStateValue.bitWidthText == QStringLiteral("8")
               && fourStateValue.signednessText
                   == QStringLiteral("unsigned")
               && fourStateValue.valueText.toLower()
                   == QStringLiteral("8'b10xz01z1"));
    expect("string value is preserved",
           textValue.current()
               && textValue.valueText
                   == QStringLiteral("\"effective value\"")
               && textValue.signednessText
                   == QStringLiteral("not applicable"));

    EffectiveValueQuery baselineRevisionQuery;
    baselineRevisionQuery.symbol = wide;
    baselineRevisionQuery.instanceContext = instance(
        QStringLiteral("top.u0"));
    baselineRevisionQuery.documentText = source;
    baselineRevisionQuery.documentRevision = 18;
    const EffectiveValueResult baselineRevision =
        service.resolve(baselineRevisionQuery);
    expect("revision-zero disk baseline cannot revive after A-B-A edit",
           baselineRevision.status == EffectiveValueStatus::Stale
               && !baselineRevision.failureReason.isEmpty());

    EffectiveValueQuery staleQuery;
    staleQuery.symbol = wide;
    staleQuery.instanceContext = instance(QStringLiteral("top.u0"));
    staleQuery.documentText = source + QStringLiteral("// unsaved edit\n");
    staleQuery.documentRevision = 18;
    const EffectiveValueResult stale = service.resolve(staleQuery);
    expect("mismatched document revision is explicitly stale",
           stale.status == EffectiveValueStatus::Stale
               && stale.requestedDocumentRevision == 18
               && stale.computedDocumentRevision == 0
               && stale.computationRevision == index.snapshotRevision()
               && !stale.stableSourceIdentity.isEmpty());

    SemanticSymbolRecord revisionStamped = wide;
    revisionStamped.presentation.documentRevision = 17;
    revisionStamped.presentation.computationRevision = 81;
    EffectiveValueQuery revertedTextQuery;
    revertedTextQuery.symbol = revisionStamped;
    revertedTextQuery.instanceContext = instance(QStringLiteral("top.u0"));
    revertedTextQuery.documentText = source;
    revertedTextQuery.documentRevision = 18;
    const EffectiveValueResult revertedText = service.resolve(
        revertedTextQuery);
    expect("same text from a different document revision is stale",
           revertedText.status == EffectiveValueStatus::Stale
               && revertedText.computationRevision == 81
               && revertedText.requestedDocumentRevision == 18
               && revertedText.computedDocumentRevision == 17);

    SemanticSymbolRecord computationStamped = wide;
    const std::uint64_t publishedRevision =
        service.beginComputation({wide.location.fileName});
    computationStamped.presentation.computationRevision = publishedRevision;
    EffectiveValueQuery computationQuery;
    computationQuery.symbol = computationStamped;
    computationQuery.instanceContext = instance(QStringLiteral("top.u0"));
    computationQuery.documentText = source;
    expect("latest completed computation revision is current",
           service.resolve(computationQuery).current());
    service.beginComputation({wide.location.fileName});
    expect("superseded computation is stale before replacement publishes",
           service.resolve(computationQuery).status
               == EffectiveValueStatus::Stale);
}

void runLiveOverlayRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_overlay_fixture.sv"));
    const QString original = QStringLiteral(
        "module child #(parameter int P = 2);\n"
        "  localparam int L = P * 3;\n"
        "endmodule\n"
        "module top; child #(.P(4)) u0(); endmodule\n");
    const QString edited = QStringLiteral(
        "module child #(parameter int P = 2);\n"
        "  localparam int L = P * 5;\n"
        "endmodule\n"
        "module top; child #(.P(4)) u0(); endmodule\n");
    SlangManager slang;
    const QList<SemanticSymbolRecord> originalRecords =
        slang.extractSymbolRecords(fileName, original);
    const QList<SemanticSymbolRecord> editedRecords =
        slang.extractSymbolRecords(fileName, edited);
    SemanticIndex index;
    index.setSnapshot(snapshot(originalRecords, fileName, original));
    index.updateSymbolRecordsForFile(fileName, editedRecords, edited);
    EffectiveValueService service(&index);
    const SemanticSymbolRecord editedLocalparam = findRecord(
        index.getSymbolRecords(fileName),
        QStringLiteral("L"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const EffectiveValueResult value = resolve(
        service,
        editedLocalparam,
        edited,
        instance(QStringLiteral("top.u0")));
    expect("unsaved overlay publishes current bound value",
           value.current() && value.valueText == QStringLiteral("20"));
}

void runDocumentModelWorkspaceOverlayRegression()
{
    const QString packageFile = QDir::current().absoluteFilePath(
        QStringLiteral("effective_document_overlay_0_pkg.sv"));
    const QString consumerFile = QDir::current().absoluteFilePath(
        QStringLiteral("effective_document_overlay_1_consumer.sv"));
    const QString packageOnDisk = QStringLiteral(
        "package overlay_pkg;\n"
        "  parameter int P = 2;\n"
        "endpackage\n");
    const QString packageUnsaved = QStringLiteral(
        "package overlay_pkg;\n"
        "  parameter int P = 5;\n"
        "endpackage\n");
    const QString consumer = QStringLiteral(
        "module overlay_top;\n"
        "  localparam int L = overlay_pkg::P * 4;\n"
        "endmodule\n");

    SlangManager slang;
    const QHash<QString, QString> diskWorkspace{
        {packageFile, packageOnDisk},
        {consumerFile, consumer},
    };
    const QList<SemanticSymbolRecord> diskRecords =
        slang.extractOverlayWorkspaceSymbolRecords(diskWorkspace);

    SemanticIndex* globalIndex = SemanticIndex::getInstance();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    const std::shared_ptr<const SemanticIndexSnapshot> previous =
        globalIndex->snapshot();
    globalIndex->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            diskRecords, {}, {}, diskWorkspace)));

    MyCodeEditor packageEditor;
    MyCodeEditor consumerEditor;
    packageEditor.setPlainText(packageUnsaved);
    consumerEditor.setPlainText(consumer);
    DocumentModel documents;
    documents.registerEditor(&packageEditor, packageFile);
    documents.registerEditor(&consumerEditor, consumerFile);
    const DocumentSnapshot packageDocument =
        documents.documentForFile(packageFile);
    const DocumentSnapshot consumerDocument =
        documents.documentForFile(consumerFile);

    SymbolAnalyzer analyzer;
    int completions = 0;
    QSet<QString> completedOverlayFiles;
    QObject::connect(&analyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     &analyzer,
                     [&](const QString& fileName, int) {
                         ++completions;
                         completedOverlayFiles.insert(
                             QFileInfo(fileName).absoluteFilePath());
                     });
    analyzer.analyzeOpenDocuments({
        {packageFile,
         packageUnsaved,
         static_cast<std::uint64_t>(packageDocument.textVersion)},
        {consumerFile,
         consumer,
         static_cast<std::uint64_t>(consumerDocument.textVersion)},
    });

    const bool published = waitUntil(
        [&]() {
            const SemanticSymbolRecord packageParameter = findRecord(
                globalIndex->getSymbolRecords(packageFile),
                QStringLiteral("P"),
                SymbolTaxonomy::CollectorKind::Parameter);
            const SemanticSymbolRecord consumerLocalparam = findRecord(
                globalIndex->getSymbolRecords(consumerFile),
                QStringLiteral("L"),
                SymbolTaxonomy::CollectorKind::Localparam);
            return completions >= 2
                && completedOverlayFiles.contains(
                    QFileInfo(packageFile).absoluteFilePath())
                && completedOverlayFiles.contains(
                    QFileInfo(consumerFile).absoluteFilePath())
                && packageParameter.isValid()
                && consumerLocalparam.isValid()
                && packageParameter.presentation.documentRevision
                    == static_cast<std::uint64_t>(
                        packageDocument.textVersion)
                && consumerLocalparam.presentation.documentRevision
                    == static_cast<std::uint64_t>(
                        consumerDocument.textVersion);
        },
        10000);

    const SemanticSymbolRecord packageParameter = findRecord(
        globalIndex->getSymbolRecords(packageFile),
        QStringLiteral("P"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord consumerLocalparam = findRecord(
        globalIndex->getSymbolRecords(consumerFile),
        QStringLiteral("L"),
        SymbolTaxonomy::CollectorKind::Localparam);
    EffectiveValueService values(globalIndex);
    const EffectiveValueResult packageValue = resolve(
        values,
        packageParameter,
        packageUnsaved,
        {},
        static_cast<std::uint64_t>(packageDocument.textVersion));
    HierarchyInstanceContext overlayInstance;
    overlayInstance.workspacePath = QStringLiteral("workspace");
    overlayInstance.activeTopModule = QStringLiteral("overlay_top");
    overlayInstance.instancePath = QStringLiteral("overlay_top");
    const EffectiveValueResult consumerValue = resolve(
        values,
        consumerLocalparam,
        consumer,
        overlayInstance,
        static_cast<std::uint64_t>(consumerDocument.textVersion));
    expect("DocumentModel workspace overlay compiles all open documents atomically",
           published
               && globalIndex->getCachedFileContent(packageFile)
                   == packageUnsaved
               && packageValue.current()
               && packageValue.valueText == QStringLiteral("5")
                && consumerValue.current()
                && consumerValue.valueText == QStringLiteral("20"));
    expect("workspace overlay emits completion for every open affected document",
           completedOverlayFiles.contains(
               QFileInfo(packageFile).absoluteFilePath())
               && completedOverlayFiles.contains(
                   QFileInfo(consumerFile).absoluteFilePath()));

    analyzer.cancelAllAnalysesAndWait();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    if (previous)
        globalIndex->setSnapshot(previous);
    else
        globalIndex->clearSnapshot();
}

void runInitialWorkspaceOverlayAtomicRegression()
{
    QTemporaryDir workspace;
    expect("initial workspace overlay fixture directory is available",
           workspace.isValid());
    if (!workspace.isValid())
        return;

    const QString packageFile =
        workspace.filePath(QStringLiteral("0_overlay_pkg.sv"));
    const QString consumerFile =
        workspace.filePath(QStringLiteral("1_overlay_consumer.sv"));
    const QString diagnosticProbeFile =
        workspace.filePath(QStringLiteral("2_overlay_diagnostic_probe.sv"));
    const QString externalOpenFile =
        workspace.filePath(QStringLiteral("external_open_overlay.sv"));
    const QString packageOnDisk = QStringLiteral(
        "`define DISK_ONLY_OVERLAY_MACRO 1\n"
        "package initial_overlay_pkg;\n"
        "  parameter int P = 2;\n"
        "endpackage\n");
    const QString packageUnsaved = QStringLiteral(
        "package initial_overlay_pkg;\n"
        "  parameter int P = 5;\n"
        "  localparam int OVERLAY_ONLY_ERROR = OVERLAY_ONLY_UNDECLARED;\n"
        "endpackage\n");
    const QString packageEditedDuringAnalysis = QStringLiteral(
        "package initial_overlay_pkg;\n"
        "  parameter int P = 7;\n"
        "endpackage\n");
    const QString consumer = QStringLiteral(
        "module initial_overlay_top;\n"
        "  localparam int L = initial_overlay_pkg::P * 4;\n"
        "endmodule\n");
    const QString diagnosticProbe = QStringLiteral(
        "module initial_overlay_diagnostic_probe;\n"
        "  localparam int PROBE = `DISK_ONLY_OVERLAY_MACRO;\n"
        "endmodule\n");
    const QString externalOpenSource = QStringLiteral(
        "module external_open_overlay;\n"
        "  localparam int EXTERNAL_VALUE = 33;\n"
        "endmodule\n");
    auto writeFile = [](const QString& fileName, const QString& content) {
        QFile file(fileName);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(content.toUtf8()) == content.toUtf8().size();
    };
    expect("initial workspace overlay disk package written",
           writeFile(packageFile, packageOnDisk));
    expect("initial workspace overlay disk consumer written",
           writeFile(consumerFile, consumer));
    expect("initial workspace overlay diagnostic probe written",
           writeFile(diagnosticProbeFile, diagnosticProbe));
    expect("external open overlay file written but excluded from project",
           writeFile(externalOpenFile, externalOpenSource));

    SlangManager slang;
    const QHash<QString, QString> diskWorkspace{
        {packageFile, packageOnDisk},
        {consumerFile, consumer},
        {diagnosticProbeFile, diagnosticProbe},
    };
    const QList<SemanticSymbolRecord> diskRecords =
        slang.extractOverlayWorkspaceSymbolRecords(
            diskWorkspace, {}, {}, nullptr, nullptr,
            {packageFile, consumerFile, diagnosticProbeFile});
    QHash<QString, QString> unsavedWorkspace = diskWorkspace;
    unsavedWorkspace.insert(packageFile, packageUnsaved);
    const QStringList orderedWorkspaceFiles{
        packageFile, consumerFile, diagnosticProbeFile};
    const QList<SemanticDiagnostic> diskDiagnostics =
        slang.extractOverlayWorkspaceDiagnostics(diskWorkspace,
                                                 {},
                                                 {},
                                                 nullptr,
                                                 orderedWorkspaceFiles);
    const QList<SemanticDiagnostic> unsavedDiagnostics =
        slang.extractOverlayWorkspaceDiagnostics(unsavedWorkspace,
                                                 {},
                                                 {},
                                                 nullptr,
                                                 orderedWorkspaceFiles);
    const auto hasProbeUndefinedMacro =
        [&](const QList<SemanticDiagnostic>& diagnostics) {
            return std::any_of(
                diagnostics.cbegin(),
                diagnostics.cend(),
                [&](const SemanticDiagnostic& diagnostic) {
                    return QFileInfo(diagnostic.fileName).absoluteFilePath()
                                == QFileInfo(diagnosticProbeFile)
                                       .absoluteFilePath()
                        && diagnostic.message.contains(
                            QStringLiteral("DISK_ONLY_OVERLAY_MACRO"));
                });
        };
    expect("overlay diagnostics use unsaved macro visibility and absolute path",
           !hasProbeUndefinedMacro(diskDiagnostics)
               && hasProbeUndefinedMacro(unsavedDiagnostics));
    int diagnosticCancelChecks = 0;
    const QList<SemanticDiagnostic> cancelledDiagnostics =
        slang.extractOverlayWorkspaceDiagnostics(
            unsavedWorkspace,
            {},
            {},
            [&]() {
                ++diagnosticCancelChecks;
                return diagnosticCancelChecks >= 4;
            },
            orderedWorkspaceFiles);
    expect("overlay diagnostics cancellation publishes no partial batch",
           diagnosticCancelChecks >= 4 && cancelledDiagnostics.isEmpty());
    SemanticDiagnostic dirtyDiagnostic;
    dirtyDiagnostic.fileName = packageFile;
    dirtyDiagnostic.line = 2;
    dirtyDiagnostic.column = 3;
    dirtyDiagnostic.message = QStringLiteral("dirty-overlay-sentinel");
    dirtyDiagnostic.severity = SemanticDiagnostic::Warning;

    SemanticIndex* globalIndex = SemanticIndex::getInstance();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    const std::shared_ptr<const SemanticIndexSnapshot> previous =
        globalIndex->snapshot();
    globalIndex->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            diskRecords, {}, {dirtyDiagnostic}, diskWorkspace)));

    MyCodeEditor packageEditor;
    MyCodeEditor consumerEditor;
    packageEditor.setPlainText(packageOnDisk);
    consumerEditor.setPlainText(consumer);
    DocumentModel documents;
    documents.registerEditor(&packageEditor, packageFile);
    documents.registerEditor(&consumerEditor, consumerFile);
    packageEditor.setPlainText(packageUnsaved);

    SymbolAnalyzer analyzer;
    WorkspaceSymbolAnalysisController controller;
    controller.setDocumentModel(&documents);
    controller.setSymbolAnalyzer(&analyzer);
    ProjectSnapshot project;
    project.workspaceRoot = workspace.path();
    project.allFiles = {
        packageFile, consumerFile, diagnosticProbeFile};
    project.systemVerilogFiles = project.allFiles;
    project.includeDirs = {workspace.path()};
    project.fileExtensions = {QStringLiteral("sv")};
    project.topModule = QStringLiteral("initial_overlay_top");

    int finishedCount = 0;
    int relationshipRequests = 0;
    bool mixedPublicationObserved = false;
    HierarchyInstanceContext initialOverlayInstance;
    initialOverlayInstance.workspacePath = workspace.path();
    initialOverlayInstance.activeTopModule =
        QStringLiteral("initial_overlay_top");
    initialOverlayInstance.instancePath =
        QStringLiteral("initial_overlay_top");
    QObject::connect(
        &controller,
        &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisFinished,
        &controller,
        [&](const ProjectSnapshot&, int, int) { ++finishedCount; });
    QObject::connect(
        &controller,
        &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisRequested,
        &controller,
        [&](const ProjectSnapshot&) {
            ++relationshipRequests;
        });
    QObject::connect(
        &analyzer,
        &SymbolAnalyzer::batchProgress,
        &controller,
        [&](int, int, const QString&) {
            const bool packageOverlayPublished =
                globalIndex->getCachedFileContent(packageFile)
                == packageUnsaved;
            const SemanticSymbolRecord consumerLocalparam = findRecord(
                globalIndex->getSymbolRecords(consumerFile),
                QStringLiteral("L"),
                SymbolTaxonomy::CollectorKind::Localparam);
            const EffectiveValueResult value = resolve(
                *EffectiveValueService::getInstance(),
                consumerLocalparam,
                consumer,
                initialOverlayInstance,
                static_cast<std::uint64_t>(
                    documents.documentForFile(consumerFile).textVersion));
            if (packageOverlayPublished
                != (value.current()
                    && value.valueText == QStringLiteral("20"))) {
                mixedPublicationObserved = true;
            }
        });

    controller.requestWorkspaceAnalysis(project);
    const bool initialFinished =
        waitUntil([&]() { return finishedCount == 1; }, 10000);
    const SemanticSymbolRecord packageParameter = findRecord(
        globalIndex->getSymbolRecords(packageFile),
        QStringLiteral("P"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord consumerLocalparam = findRecord(
        globalIndex->getSymbolRecords(consumerFile),
        QStringLiteral("L"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const EffectiveValueResult packageValue = resolve(
        *EffectiveValueService::getInstance(),
        packageParameter,
        packageOnDisk,
        {},
        0);
    const EffectiveValueResult consumerValue = resolve(
        *EffectiveValueService::getInstance(),
        consumerLocalparam,
        consumer,
        initialOverlayInstance,
        0);
    const QList<SemanticDiagnostic> packageDiagnostics =
        globalIndex->getDiagnostics(packageFile);
    const QList<SemanticDiagnostic> probeDiagnostics =
        globalIndex->getDiagnostics(diagnosticProbeFile);
    expect("initial workspace analysis publishes the saved workspace atomically",
           initialFinished
               && !mixedPublicationObserved
               && globalIndex->getCachedFileContent(packageFile)
                   == packageOnDisk
               && packageValue.current()
               && packageValue.valueText == QStringLiteral("2")
               && consumerValue.current()
               && consumerValue.valueText == QStringLiteral("8"));
    expect("workspace analysis does not stamp dirty editor revisions as current",
           packageParameter.presentation.documentRevision == 0
               && consumerLocalparam.presentation.documentRevision == 0);
    const bool staleDirtyDiagnosticRetained = std::any_of(
        packageDiagnostics.cbegin(),
        packageDiagnostics.cend(),
        [](const SemanticDiagnostic& diagnostic) {
            return diagnostic.message
                == QStringLiteral("dirty-overlay-sentinel");
        });
    const bool overlayDiagnosticPublished = std::any_of(
        packageDiagnostics.cbegin(),
        packageDiagnostics.cend(),
        [&](const SemanticDiagnostic& diagnostic) {
            return QFileInfo(diagnostic.fileName).absoluteFilePath()
                    == QFileInfo(packageFile).absoluteFilePath()
                && diagnostic.message.contains(
                    QStringLiteral("OVERLAY_ONLY_UNDECLARED"));
        });
    expect("workspace diagnostics follow saved source rather than dirty overlay",
           !staleDirtyDiagnosticRetained && !overlayDiagnosticPublished);
    expect("async workspace diagnostics retain saved macro visibility",
           !hasProbeUndefinedMacro(probeDiagnostics));

    std::atomic_bool workerEntered{false};
    std::atomic_bool releaseWorker{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&](const std::function<bool()>& cancelled) {
            workerEntered.store(true, std::memory_order_release);
            while (!releaseWorker.load(std::memory_order_acquire)
                   && !cancelled()) {
                QThread::msleep(1);
            }
        });
    int expiredCount = 0;
    QObject::connect(&analyzer,
                     &SymbolAnalyzer::workspaceAnalysisExpired,
                     &controller,
                     [&]() { ++expiredCount; });
    ProjectSnapshot editedProject = project;
    editedProject.defines.insert(QStringLiteral("OVERLAY_PASS"),
                                 QStringLiteral("1"));
    controller.requestWorkspaceAnalysis(editedProject);
    const bool workerStarted = waitUntil(
        [&]() { return workerEntered.load(std::memory_order_acquire); },
        5000);
    packageEditor.setPlainText(packageEditedDuringAnalysis);
    releaseWorker.store(true, std::memory_order_release);
    const bool staleTaskExpired = expiredCount > 0;
    analyzer.setWorkspaceWorkerStartGateForTesting({});
    const bool editedFinished =
        waitUntil([&]() { return finishedCount == 2; }, 10000);

    const SemanticSymbolRecord editedPackageParameter = findRecord(
        globalIndex->getSymbolRecords(packageFile),
        QStringLiteral("P"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord editedConsumerLocalparam = findRecord(
        globalIndex->getSymbolRecords(consumerFile),
        QStringLiteral("L"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const EffectiveValueResult editedPackageValue = resolve(
        *EffectiveValueService::getInstance(),
        editedPackageParameter,
        packageOnDisk,
        {},
        0);
    const EffectiveValueResult editedConsumerValue = resolve(
        *EffectiveValueService::getInstance(),
        editedConsumerLocalparam,
        consumer,
        initialOverlayInstance,
        0);
    expect("executor does not auto-restart when an editor changes",
           workerStarted && !staleTaskExpired && editedFinished
               && relationshipRequests == 0
               && editedPackageValue.current()
               && editedPackageValue.valueText == QStringLiteral("2")
               && editedConsumerValue.current()
               && editedConsumerValue.valueText == QStringLiteral("8"));

    // Opening a SystemVerilog document while the full worker is gated must
    // restart that one transaction. The opened file is outside the project
    // budget, but still participates in symbol / effective-value publication;
    // relationship analysis remains scoped to the original project.
    workerEntered.store(false, std::memory_order_release);
    releaseWorker.store(false, std::memory_order_release);
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&](const std::function<bool()>& cancelled) {
            workerEntered.store(true, std::memory_order_release);
            while (!releaseWorker.load(std::memory_order_acquire)
                   && !cancelled()) {
                QThread::msleep(1);
            }
        });
    ProjectSnapshot openedProject = editedProject;
    openedProject.defines.insert(QStringLiteral("OPEN_DOCUMENT_PASS"),
                                 QStringLiteral("1"));
    const int expiredBeforeOpen = expiredCount;
    controller.requestWorkspaceAnalysis(openedProject);
    const bool openWorkerStarted = waitUntil(
        [&]() { return workerEntered.load(std::memory_order_acquire); },
        5000);
    MyCodeEditor externalOpenEditor;
    externalOpenEditor.setPlainText(externalOpenSource);
    documents.registerEditor(&externalOpenEditor, externalOpenFile);
    releaseWorker.store(true, std::memory_order_release);
    const bool openTaskExpired = expiredCount > expiredBeforeOpen;
    analyzer.setWorkspaceWorkerStartGateForTesting({});
    const bool openedFinished =
        waitUntil([&]() { return finishedCount == 3; }, 10000);
    const DocumentSnapshot externalDocument =
        documents.documentForFile(externalOpenFile);
    const SemanticSymbolRecord externalValueRecord = findRecord(
        globalIndex->getSymbolRecords(externalOpenFile),
        QStringLiteral("EXTERNAL_VALUE"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const EffectiveValueResult externalValue = resolve(
        *EffectiveValueService::getInstance(),
        externalValueRecord,
        externalOpenSource,
        {},
        static_cast<std::uint64_t>(externalDocument.textVersion));
    expect("executor does not auto-restart when a document opens",
           openWorkerStarted && !openTaskExpired && openedFinished
               && relationshipRequests == 0);
    expect("opening an external SV does not widen an explicit workspace request",
           !externalValue.current()
               && !externalValueRecord.isValid()
               && !project.systemVerilogFiles.contains(externalOpenFile));

    workerEntered.store(false, std::memory_order_release);
    releaseWorker.store(false, std::memory_order_release);
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&](const std::function<bool()>& cancelled) {
            workerEntered.store(true, std::memory_order_release);
            while (!releaseWorker.load(std::memory_order_acquire)
                   && !cancelled()) {
                QThread::msleep(1);
            }
        });
    ProjectSnapshot closedProject = openedProject;
    closedProject.defines.insert(QStringLiteral("CLOSE_DOCUMENT_PASS"),
                                 QStringLiteral("1"));
    const int expiredBeforeClose = expiredCount;
    controller.requestWorkspaceAnalysis(closedProject);
    const bool closeWorkerStarted = waitUntil(
        [&]() { return workerEntered.load(std::memory_order_acquire); },
        5000);
    documents.unregisterEditor(&externalOpenEditor);
    releaseWorker.store(true, std::memory_order_release);
    const bool closeTaskExpired = expiredCount > expiredBeforeClose;
    analyzer.setWorkspaceWorkerStartGateForTesting({});
    const bool closedFinished =
        waitUntil([&]() { return finishedCount == 4; }, 10000);
    expect("executor does not auto-restart when a document closes",
           closeWorkerStarted && !closeTaskExpired && closedFinished
               && relationshipRequests == 0);

    analyzer.cancelAllAnalysesAndWait();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    if (previous)
        globalIndex->setSnapshot(previous);
    else
        globalIndex->clearSnapshot();
}

void runLargeDocumentRevisionRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_large_revision_fixture.sv"));
    QString source = QStringLiteral(
        "module large_revision_top;\n"
        "  localparam int L = 5;\n"
        "endmodule\n/*");
    source.append(QString(2 * 1024 * 1024, QLatin1Char('x')));
    source.append(QStringLiteral("*/\n"));

    SlangManager slang;
    const QList<SemanticSymbolRecord> baselineRecords =
        slang.extractSymbolRecords(fileName, source);
    SemanticIndex* globalIndex = SemanticIndex::getInstance();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    const std::shared_ptr<const SemanticIndexSnapshot> previous =
        globalIndex->snapshot();
    globalIndex->setSnapshot(snapshot(baselineRecords, fileName, source));

    SymbolAnalyzer analyzer;
    int completions = 0;
    QObject::connect(&analyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     &analyzer,
                     [&](const QString& completedFile, int) {
                         if (QFileInfo(completedFile).absoluteFilePath()
                             == QFileInfo(fileName).absoluteFilePath()) {
                             ++completions;
                         }
                     });
    analyzer.analyzeFileContentAsync(fileName, source, 77);

    const bool published = waitUntil(
        [&]() {
            const SemanticSymbolRecord localparam = findRecord(
                globalIndex->getSymbolRecords(fileName),
                QStringLiteral("L"),
                SymbolTaxonomy::CollectorKind::Localparam);
            return completions > 0
                && localparam.isValid()
                && localparam.presentation.documentRevision == 77;
        },
        15000);
    const SemanticSymbolRecord localparam = findRecord(
        globalIndex->getSymbolRecords(fileName),
        QStringLiteral("L"),
        SymbolTaxonomy::CollectorKind::Localparam);
    EffectiveValueService values(globalIndex);
    const EffectiveValueResult value = resolve(
        values, localparam, source, {}, 77);
    expect("large asynchronous document publication preserves revision",
           published && value.current()
               && value.requestedDocumentRevision == 77
               && value.computedDocumentRevision == 77
               && value.valueText == QStringLiteral("5"));

    analyzer.cancelAllAnalysesAndWait();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    if (previous)
        globalIndex->setSnapshot(previous);
    else
        globalIndex->clearSnapshot();
}

void runUtf16AndCrLfOffsetRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_utf16_crlf_fixture.sv"));
    const QString slangSource = QStringLiteral(
        "// \u7b2c\u4e00\u884c\u4e2d\u6587\r\n"
        "/* \u4e2d\u6587\u524d\u7f00 */ localparam logic [7:0] CU_UTF = {4'ha, 4'h5};\r\n"
        "module utf_anchor;\r\n"
        "endmodule\r\n");
    QString documentText = slangSource;
    documentText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    SlangManager slang;
    QList<EffectiveValueFact> facts;
    const QList<SemanticSymbolRecord> records = slang.extractSymbolRecords(
        fileName, slangSource, {}, {}, &facts);
    const SemanticSymbolRecord value = findRecord(
        records,
        QStringLiteral("CU_UTF"),
        SymbolTaxonomy::CollectorKind::Localparam);
    const int expectedPosition = documentText.indexOf(
        QStringLiteral("CU_UTF"));
    const int lineStart = documentText.lastIndexOf(
        QLatin1Char('\n'), expectedPosition - 1) + 1;
    expect("Slang UTF-8/CRLF symbol anchor maps to Qt UTF-16/LF",
           value.isValid()
               && value.location.position == expectedPosition
               && value.location.length == QStringLiteral("CU_UTF").size()
               && value.location.startLine == 2
               && value.location.startColumn
                   == expectedPosition - lineStart + 1);

    const int expectedFactStart = documentText.indexOf(
        QStringLiteral("{4'ha, 4'h5}"));
    const int expectedFactEnd = expectedFactStart
        + QStringLiteral("{4'ha, 4'h5}").size();
    EffectiveValueFact concatFact;
    for (const EffectiveValueFact& fact : std::as_const(facts)) {
        if (fact.kind == EffectiveValueFactKind::ConcatenationWidth
            && fact.startPosition == expectedFactStart) {
            concatFact = fact;
            break;
        }
    }
    expect("Slang UTF-8/CRLF fact range maps to Qt UTF-16/LF",
           concatFact.isValid()
               && concatFact.endPosition == expectedFactEnd
               && concatFact.line == 2
               && concatFact.effectiveScopeKind
                   == SemanticEffectiveScopeKind::CompilationUnit
               && concatFact.qualifiedScopePath
                   == QStringLiteral("$unit"));

    SemanticIndex index;
    EffectiveValueService service(&index);
    service.publishDocumentFacts(fileName, documentText, facts, 23, 17);
    const QList<EffectiveValueFact> boundFacts = service.factsForDocument(
        fileName,
        documentText,
        instance(QStringLiteral("top.u0")),
        17);
    bool staticFactSurvivedBinding = false;
    for (const EffectiveValueFact& fact : boundFacts) {
        staticFactSurvivedBinding = staticFactSurvivedBinding
            || (fact.startPosition == expectedFactStart
                && fact.effectiveScopeKind
                    == SemanticEffectiveScopeKind::CompilationUnit);
        if (fact.effectiveScopeKind
            == SemanticEffectiveScopeKind::Instance) {
            const QString anchorPath = fact.anchorInstancePath.isEmpty()
                ? fact.instancePath
                : fact.anchorInstancePath;
            expect("bound facts use the exact instance only",
                   anchorPath == QStringLiteral("top.u0"));
        }
    }
    expect("static compilation-unit facts survive Design binding",
           staticFactSurvivedBinding);
    expect("same fact text from a different document revision is stale",
           service.factsForDocument(
                      fileName,
                      documentText,
                      instance(QStringLiteral("top.u0")),
                      18)
               .isEmpty());
    service.publishDocumentFacts(fileName,
                                 documentText,
                                 facts,
                                 24,
                                 0);
    expect("revision-zero fact baseline cannot revive after A-B-A edit",
           service.factsForDocument(
                      fileName,
                      documentText,
                      instance(QStringLiteral("top.u0")),
                      17)
               .isEmpty());
    service.clearPublishedFacts();
    expect("effective-value fact invalidation clears all publications",
           service.factsForDocument(fileName, documentText).isEmpty());
}

void runHoverRevisionRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_hover_revision_fixture.sv"));
    const QString source = QStringLiteral(
        "localparam int HOVER_VALUE = 17;\n"
        "module hover_top; endmodule\n");
    SlangManager slang;
    QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source);
    for (SemanticSymbolRecord& record : records) {
        record.presentation.computationRevision = 23;
        record.presentation.documentRevision = 17;
    }

    SemanticIndex index;
    index.setSnapshot(snapshot(records, fileName, source));
    SymbolHoverService hover(&index);
    EditorSemanticContext context;
    context.fileName = fileName;
    context.documentText = source;
    context.lineText = QStringLiteral("localparam int HOVER_VALUE = 17;");
    context.column = context.lineText.indexOf(
        QStringLiteral("HOVER_VALUE")) + 2;
    context.cursorLine = 1;
    context.documentRevision = 18;
    const SymbolHoverReport report = hover.hoverForContext(context);
    expect("hover propagates the current document revision",
           report.available
               && report.effectiveValueStatus
                   == EffectiveValueStatus::Stale
               && !report.evaluationFailureReason.isEmpty());
}

void runGhostUsesSlangRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_ghost_fixture.sv"));
    const QString source = QStringLiteral(
        "module top;\n"
        "  localparam logic [3:0] P_0 = 4'ha;\n"
        "  localparam logic [3:0] P_1 = 4'h5;\n"
        "  typedef enum logic [7:0] {\n"
        "    E_0 = {P_0, P_1},\n"
        "    E_1 = (E_0 << 1) | 8'b1,\n"
        "    E_2\n"
        "  } test_e;\n"
        "endmodule\n");
    SlangManager slang;
    QList<EffectiveValueFact> facts;
    QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source, {}, {}, &facts);
    SemanticIndex index;
    EffectiveValueService values(&index);
    const std::uint64_t computationRevision =
        values.beginComputation({fileName});
    for (SemanticSymbolRecord& record : records)
        record.presentation.computationRevision = computationRevision;
    index.setSnapshot(snapshot(records, fileName, source));
    values.publishDocumentFacts(fileName,
                                source,
                                std::move(facts),
                                computationRevision);
    GhostAnnotationService ghost(&index, &values);
    GhostAnnotationQuery query;
    query.fileName = fileName;
    query.documentText = source;
    const GhostAnnotationReport report = ghost.annotationsForDocument(query);

    QStringList enumTexts;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.kind == GhostAnnotationKind::EnumValue)
            enumTexts.append(annotation.text.toLower());
    }
    if (!enumTexts.contains(QStringLiteral("= 165"))
        || enumTexts.size() < 3) {
        std::printf("enum ghost annotations: %s\n",
                    qPrintable(enumTexts.join(QStringLiteral(" | "))));
    }
    expect("enum ghost displays Slang concatenation value in decimal",
           enumTexts.contains(QStringLiteral("= 165")));
    expect("enum ghost uses Slang expression and implicit increment",
           enumTexts.size() >= 3
               && enumTexts.contains(QStringLiteral("= 75"))
               && enumTexts.contains(QStringLiteral("= 76")));
}

void runParameterSourceDisplayEquivalenceRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_parameter_display_fixture.sv"));
    const QString source = QStringLiteral(
        "module child #(\n"
        "  parameter logic [3:0] TRUNC = 8'h1f,\n"
        "  parameter logic signed [7:0] SIGNED_VALUE = 8'hff,\n"
        "  parameter logic [15:0] SAME = 8'd7,\n"
        "  parameter int EXPRESSION = 3 + 4\n"
        ");\n"
        "endmodule\n"
        "module parent #(\n"
        "  parameter logic [7:0] TRUNC_SOURCE = 8'h1e,\n"
        "  parameter logic [7:0] SIGNED_SOURCE = 8'h70\n"
        ");\n"
        "  child #(\n"
        "    .TRUNC(TRUNC_SOURCE + 0),\n"
        "    .SIGNED_VALUE(SIGNED_SOURCE + 0),\n"
        "    .SAME(8'd9)\n"
        "  ) u_child();\n"
        "endmodule\n"
        "module top;\n"
        "  parent #(\n"
        "    .TRUNC_SOURCE(8'h2e),\n"
        "    .SIGNED_SOURCE(8'h80)\n"
        "  ) p0();\n"
        "endmodule\n");
    const auto lineOf = [&](const QString& needle) {
        const int position = source.indexOf(needle);
        return position < 0
            ? -1
            : source.left(position).count(QLatin1Char('\n')) + 1;
    };
    const int truncDeclarationLine = lineOf(
        QStringLiteral("parameter logic [3:0] TRUNC"));
    const int signedDeclarationLine = lineOf(
        QStringLiteral("parameter logic signed [7:0] SIGNED_VALUE"));
    const int sameDeclarationLine = lineOf(
        QStringLiteral("parameter logic [15:0] SAME"));
    const int expressionDeclarationLine = lineOf(
        QStringLiteral("parameter int EXPRESSION"));
    SlangManager slang;
    QList<EffectiveValueFact> facts;
    QList<SemanticSymbolRecord> records = slang.extractSymbolRecords(
        fileName, source, {}, {}, &facts);
    const SemanticSymbolRecord truncRecord = findRecord(
        records,
        QStringLiteral("TRUNC"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord signedRecord = findRecord(
        records,
        QStringLiteral("SIGNED_VALUE"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord sameRecord = findRecord(
        records,
        QStringLiteral("SAME"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord expressionRecord = findRecord(
        records,
        QStringLiteral("EXPRESSION"),
        SymbolTaxonomy::CollectorKind::Parameter);
    expect("Slang marks only equivalent direct declaration values redundant",
           truncRecord.isValid() && signedRecord.isValid()
               && sameRecord.isValid() && expressionRecord.isValid()
               && !truncRecord.presentation.defaultInfo
                       .sourceTextDisplaysEffectiveValue
               && !signedRecord.presentation.defaultInfo
                       .sourceTextDisplaysEffectiveValue
               && sameRecord.presentation.defaultInfo
                      .sourceTextDisplaysEffectiveValue
               && !expressionRecord.presentation.defaultInfo
                       .sourceTextDisplaysEffectiveValue);
    SemanticIndex index;
    EffectiveValueService values(&index);
    const std::uint64_t computationRevision =
        values.beginComputation({fileName});
    for (SemanticSymbolRecord& record : records)
        record.presentation.computationRevision = computationRevision;
    index.setSnapshot(snapshot(records, fileName, source));
    values.publishDocumentFacts(fileName,
                                source,
                                std::move(facts),
                                computationRevision);
    GhostAnnotationService ghost(&index, &values);

    const auto reportFor = [&](const HierarchyInstanceContext& context) {
        GhostAnnotationQuery query;
        query.fileName = fileName;
        query.documentText = source;
        query.instanceContext = context;
        return ghost.annotationsForDocument(query);
    };
    const auto hasAnnotation = [](const GhostAnnotationReport& report,
                                  GhostAnnotationKind kind,
                                  int line,
                                  const QString& text) {
        return std::any_of(
            report.annotations.cbegin(),
            report.annotations.cend(),
            [&](const GhostAnnotation& annotation) {
                return annotation.kind == kind
                    && annotation.line == line
                    && annotation.text == text;
            });
    };
    const auto hasKindOnLine = [](const GhostAnnotationReport& report,
                                  GhostAnnotationKind kind,
                                  int line) {
        return std::any_of(
            report.annotations.cbegin(),
            report.annotations.cend(),
            [&](const GhostAnnotation& annotation) {
                return annotation.kind == kind
                    && annotation.line == line;
            });
    };

    const GhostAnnotationReport unbound = reportFor({});
    const QList<SemanticSymbolRecord> indexedRecords =
        index.getSymbolRecords(fileName);
    const SemanticSymbolRecord indexedTrunc = findRecord(
        indexedRecords,
        QStringLiteral("TRUNC"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord indexedSame = findRecord(
        indexedRecords,
        QStringLiteral("SAME"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const EffectiveValueResult unboundTrunc = resolve(
        values, indexedTrunc, source);
    const EffectiveValueResult unboundSame = resolve(
        values, indexedSame, source);
    const EffectiveValueResult boundSame = resolve(
        values,
        indexedSame,
        source,
        instance(QStringLiteral("top.p0.u_child")));
    const bool serviceMetadataCorrect =
        unboundTrunc.current() && unboundSame.current()
        && boundSame.current()
        && !unboundTrunc.sourceTextDisplaysEffectiveValue
        && unboundSame.sourceTextDisplaysEffectiveValue
        && !boundSame.sourceTextDisplaysEffectiveValue;
    if (!serviceMetadataCorrect) {
        std::printf(
            "parameter service metadata: trunc status=%d equivalent=%d value=%s; "
            "same status=%d equivalent=%d value=%s; bound status=%d equivalent=%d value=%s reason=%s\n",
            static_cast<int>(unboundTrunc.status),
            unboundTrunc.sourceTextDisplaysEffectiveValue,
            qPrintable(unboundTrunc.valueText),
            static_cast<int>(unboundSame.status),
            unboundSame.sourceTextDisplaysEffectiveValue,
            qPrintable(unboundSame.valueText),
            static_cast<int>(boundSame.status),
            boundSame.sourceTextDisplaysEffectiveValue,
            qPrintable(boundSame.valueText),
            qPrintable(boundSame.failureReason));
    }
    expect("EffectiveValueService propagates declaration-anchor equivalence",
           serviceMetadataCorrect);
    expect("narrow direct literal still shows the truncated effective value",
           hasAnnotation(unbound,
                         GhostAnnotationKind::ParameterValue,
                         truncDeclarationLine,
                         QStringLiteral("= 4'b1111")));
    expect("unsigned literal assigned to signed parameter shows coercion",
           hasAnnotation(unbound,
                         GhostAnnotationKind::ParameterValue,
                         signedDeclarationLine,
                         QStringLiteral("= -8'sd1")));
    expect("numerically equivalent direct literal remains suppressed",
           !hasKindOnLine(unbound,
                          GhostAnnotationKind::ParameterValue,
                          sameDeclarationLine));
    expect("nonliteral parameter expression still shows its effective value",
           hasAnnotation(unbound,
                         GhostAnnotationKind::ParameterValue,
                         expressionDeclarationLine,
                         QStringLiteral("= 7")));
    const GhostAnnotationReport bound = reportFor(
        instance(QStringLiteral("top.p0.u_child")));
    expect("bound declaration shows truncated override value",
           hasAnnotation(bound,
                         GhostAnnotationKind::ParameterValue,
                         truncDeclarationLine,
                         QStringLiteral("= 4'b1110")));
    expect("bound declaration shows signed override value",
           hasAnnotation(bound,
                         GhostAnnotationKind::ParameterValue,
                         signedDeclarationLine,
                         QStringLiteral("= -8'sd128")));
    expect("bound declaration shows a direct override absent from declaration text",
           hasAnnotation(bound,
                         GhostAnnotationKind::ParameterValue,
                         sameDeclarationLine,
                         QStringLiteral("= 16'd9")));
}

void runInvalidEnumIsolationRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_invalid_enum_fixture.sv"));
    const QString source = QStringLiteral(
        "module invalid_top;\n"
        "  localparam logic [3:0] P_0 = 4'ha;\n"
        "  localparam logic [3:0] P_1 = 4'h5;\n"
        "  typedef enum logic [7:0] {\n"
        "    VALID_ENUM = {P_0, P_1},\n"
        "    OUT_OF_RANGE = (VALID_ENUM << 1) | 1,\n"
        "    INVALID_SUCCESSOR\n"
        "  } invalid_e;\n"
        "endmodule\n");

    SlangManager slang;
    const QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source);
    const SemanticSymbolRecord valid = findRecord(
        records,
        QStringLiteral("VALID_ENUM"),
        SymbolTaxonomy::CollectorKind::EnumValue);
    const SemanticSymbolRecord invalidSuccessor = findRecord(
        records,
        QStringLiteral("INVALID_SUCCESSOR"),
        SymbolTaxonomy::CollectorKind::EnumValue);

    expect("invalid enum member does not discard valid sibling values",
           valid.isValid()
               && valid.presentation.defaultInfo.available
               && valid.presentation.defaultInfo.valueText
                   == QStringLiteral("8'b10100101")
               && valid.presentation.defaultInfo.displayValueText
                   == QStringLiteral("165"));
    expect("invalid implicit enum successor is published as unavailable",
           invalidSuccessor.isValid()
               && !invalidSuccessor.presentation.defaultInfo.available
               && !invalidSuccessor.presentation.defaultInfo.failureReason
                       .isEmpty());
}

void runTopOverrideDefaultRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_top_override_fixture.sv"));
    const QString source = QStringLiteral(
        "module child #(parameter int CP = 1);\n"
        "  localparam int CL = CP + 1;\n"
        "endmodule\n"
        "module top #(parameter int P = 3);\n"
        "  localparam int L = P + 1;\n"
        "  child #(.CP(P)) u_child();\n"
        "endmodule\n");

    SlangManager slang;
    QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source);

    const QByteArray sourceBytes = source.toUtf8();
    const QByteArray nameBytes = fileName.toUtf8();
    slang::SourceManager sourceManager;
    const std::shared_ptr<slang::syntax::SyntaxTree> tree =
        slang::syntax::SyntaxTree::fromText(
            std::string_view(sourceBytes.constData(),
                             static_cast<std::size_t>(sourceBytes.size())),
            sourceManager,
            std::string_view(nameBytes.constData(),
                             static_cast<std::size_t>(nameBytes.size())),
            std::string_view{});
    slang::ast::CompilationOptions options;
    options.paramOverrides.push_back("P=9");
    slang::ast::Compilation compilation(options);
    compilation.addSyntaxTree(tree);
    slang_symbols::populateSymbolPresentations(compilation, records);

    const SemanticSymbolRecord parameter = findRecord(
        records,
        QStringLiteral("P"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord childParameter = findRecord(
        records,
        QStringLiteral("CP"),
        SymbolTaxonomy::CollectorKind::Parameter);
    const SemanticSymbolRecord childLocalparam = findRecord(
        records,
        QStringLiteral("CL"),
        SymbolTaxonomy::CollectorKind::Localparam);
    SemanticIndex index;
    index.setSnapshot(snapshot(records, fileName, source));
    EffectiveValueService values(&index);
    const EffectiveValueResult bound = resolve(
        values, parameter, source, instance(QStringLiteral("top")));
    const EffectiveValueResult unbound = resolve(
        values, parameter, source);
    const EffectiveValueResult boundChildParameter = resolve(
        values,
        childParameter,
        source,
        instance(QStringLiteral("top.u_child")));
    const EffectiveValueResult unboundChildParameter = resolve(
        values, childParameter, source);
    const EffectiveValueResult boundChildLocalparam = resolve(
        values,
        childLocalparam,
        source,
        instance(QStringLiteral("top.u_child")));
    const EffectiveValueResult unboundChildLocalparam = resolve(
        values, childLocalparam, source);
    expect("top parameter keeps instance override separate from declaration default",
           bound.current() && bound.instanceBound
               && bound.valueText == QStringLiteral("9")
               && unbound.current() && unbound.defaultEvaluation
               && unbound.valueText == QStringLiteral("3")
               && bound.valueText != unbound.valueText);
    expect("detached defaults do not overwrite or pollute real nested instance maps",
           boundChildParameter.current()
               && boundChildParameter.valueText == QStringLiteral("9")
               && boundChildLocalparam.current()
               && boundChildLocalparam.valueText == QStringLiteral("10")
               && unboundChildParameter.current()
               && unboundChildParameter.valueText == QStringLiteral("1")
               && unboundChildLocalparam.current()
               && unboundChildLocalparam.valueText == QStringLiteral("2")
               && childParameter.presentation.instanceInfoByPath.size() == 1
               && childParameter.presentation.instanceInfoByPath.contains(
                   QStringLiteral("top.u_child"))
               && !childParameter.presentation.instanceInfoByPath.contains(
                   QStringLiteral("child")));
}

void runParameterOverrideAnchorRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_override_anchor_fixture.sv"));
    const QString source = QStringLiteral(
        "module leaf #(parameter int P = 1);\n"
        "endmodule\n"
        "module parent #(parameter int BASE = 10);\n"
        "  leaf #(.P(BASE + 1)) u_leaf();\n"
        "endmodule\n"
        "module top;\n"
        "  parent #(.BASE(10)) p0();\n"
        "  parent #(.BASE(20)) p1();\n"
        "endmodule\n");

    SlangManager slang;
    QList<EffectiveValueFact> facts;
    QList<SemanticSymbolRecord> records = slang.extractSymbolRecords(
        fileName, source, {}, {}, &facts);
    SemanticIndex index;
    EffectiveValueService values(&index);
    const std::uint64_t computationRevision =
        values.beginComputation({fileName});
    for (SemanticSymbolRecord& record : records)
        record.presentation.computationRevision = computationRevision;
    index.setSnapshot(snapshot(records, fileName, source));
    values.publishDocumentFacts(fileName,
                                source,
                                facts,
                                computationRevision);

    const auto childOverrideFor = [&](const QString& parentPath) {
        const QList<EffectiveValueFact> boundFacts =
            values.factsForDocument(fileName,
                                    source,
                                    instance(parentPath));
        for (const EffectiveValueFact& fact : boundFacts) {
            if (fact.kind == EffectiveValueFactKind::ParameterOverride
                && fact.expressionText.contains(QStringLiteral("BASE"))) {
                return fact;
            }
        }
        return EffectiveValueFact{};
    };
    const EffectiveValueFact p0Override = childOverrideFor(
        QStringLiteral("top.p0"));
    const EffectiveValueFact p1Override = childOverrideFor(
        QStringLiteral("top.p1"));
    expect("parameter override facts bind by parent source anchor and retain child provenance",
           p0Override.isValid() && p1Override.isValid()
               && p0Override.anchorInstancePath == QStringLiteral("top.p0")
               && p1Override.anchorInstancePath == QStringLiteral("top.p1")
               && p0Override.instancePath
                    == QStringLiteral("top.p0.u_leaf")
               && p1Override.instancePath
                    == QStringLiteral("top.p1.u_leaf")
               && p0Override.valueText == QStringLiteral("11")
               && p1Override.valueText == QStringLiteral("21"));

    GhostAnnotationService ghost(&index, &values);
    const auto overrideGhostText = [&](const QString& parentPath) {
        GhostAnnotationQuery query;
        query.fileName = fileName;
        query.documentText = source;
        query.instanceContext = instance(parentPath);
        const GhostAnnotationReport report =
            ghost.annotationsForDocument(query);
        for (const GhostAnnotation& annotation : report.annotations) {
            if (annotation.kind == GhostAnnotationKind::ParameterOverride
                && annotation.line == 4) {
                return annotation.text;
            }
        }
        return QString();
    };
    expect("parameter override Ghost follows the bound parent instance",
           overrideGhostText(QStringLiteral("top.p0"))
                   == QStringLiteral("= 11")
               && overrideGhostText(QStringLiteral("top.p1"))
                   == QStringLiteral("= 21"));
}

void runPresentationCancellationRegression()
{
    const QString fileName = QDir::current().absoluteFilePath(
        QStringLiteral("effective_cancel_fixture.sv"));
    const QString source = QStringLiteral(
        "module cancel_top #(parameter int P = 3);\n"
        "  localparam logic [7:0] L = {4'(P), 4'h5};\n"
        "endmodule\n");

    SlangManager slang;
    QList<SemanticSymbolRecord> records =
        slang.extractSymbolRecords(fileName, source);
    expect("cancellation fixture produces records", !records.isEmpty());
    if (records.isEmpty())
        return;

    for (SemanticSymbolRecord& record : records)
        record.presentation = {};
    records.first().presentation.declarationText =
        QStringLiteral("sentinel-presentation");
    QList<EffectiveValueFact> facts;
    EffectiveValueFact sentinelFact;
    sentinelFact.stableSourceIdentity = QStringLiteral("sentinel-fact");
    facts.append(sentinelFact);

    const QByteArray sourceBytes = source.toUtf8();
    const QByteArray nameBytes = fileName.toUtf8();
    slang::SourceManager sourceManager;
    const std::shared_ptr<slang::syntax::SyntaxTree> tree =
        slang::syntax::SyntaxTree::fromText(
            std::string_view(sourceBytes.constData(),
                             static_cast<std::size_t>(sourceBytes.size())),
            sourceManager,
            std::string_view(nameBytes.constData(),
                             static_cast<std::size_t>(nameBytes.size())),
            std::string_view{});
    slang::ast::Compilation compilation;
    compilation.addSyntaxTree(tree);

    int cancellationChecks = 0;
    slang_symbols::populateSymbolPresentations(
        compilation,
        records,
        &facts,
        [&]() { return ++cancellationChecks >= 5; });
    expect("cancelled presentation traversal is transactionally discarded",
           cancellationChecks >= 5
               && records.first().presentation.declarationText
                   == QStringLiteral("sentinel-presentation")
               && facts.size() == 1
               && facts.first().stableSourceIdentity
                   == QStringLiteral("sentinel-fact"));
}

void runDebouncedOverlayPublicationRegression()
{
    const QString childFile = QDir::current().absoluteFilePath(
        QStringLiteral("effective_async_child.sv"));
    const QString topFile = QDir::current().absoluteFilePath(
        QStringLiteral("effective_async_top.sv"));
    const QString original = QStringLiteral(
        "module child #(parameter int P = 2);\n"
        "  localparam int L = P * 3;\n"
        "  logic [7:0] data;\n"
        "  assign data = {4'ha, 4'h5};\n"
        "endmodule\n");
    const QString firstEdit = QStringLiteral(
        "module child #(parameter int P = 2);\n"
        "  localparam int L = P * 4;\n"
        "  logic [7:0] data;\n"
        "  assign data = {4'ha, 4'h5};\n"
        "endmodule\n");
    const QString latestEdit = original;
    const QString top = QStringLiteral(
        "module top; child #(.P(4)) u0(); endmodule\n");

    SlangManager slang;
    const QHash<QString, QString> originalWorkspace{
        {childFile, original},
        {topFile, top},
    };
    const QList<SemanticSymbolRecord> originalRecords =
        slang.extractOverlayWorkspaceSymbolRecords(originalWorkspace);

    SemanticIndex* globalIndex = SemanticIndex::getInstance();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    const std::shared_ptr<const SemanticIndexSnapshot> previous =
        globalIndex->snapshot();
    globalIndex->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            originalRecords, {}, {}, originalWorkspace)));

    QString overlayText = firstEdit;
    SymbolAnalyzer analyzer;
    int completions = 0;
    bool latestRequested = false;
    QStringList publicationsAfterLatestRequest;
    QObject::connect(&analyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     &analyzer,
                     [&](const QString& fileName, int) {
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(childFile).absoluteFilePath()) {
                             ++completions;
                             if (latestRequested) {
                                 publicationsAfterLatestRequest.append(
                                     globalIndex->getCachedFileContent(
                                         childFile));
                             }
                         }
                     });

    constexpr std::uint64_t firstRevision = 31;
    constexpr std::uint64_t latestRevision = 32;
    analyzer.analyzeFileContentAsync(childFile, overlayText, firstRevision);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    overlayText = latestEdit;
    latestRequested = true;
    analyzer.analyzeFileContentAsync(childFile, overlayText, latestRevision);

    const bool publishedLatest = waitUntil(
        [&]() {
            const SemanticSymbolRecord latestLocalparam = findRecord(
                globalIndex->getSymbolRecords(childFile),
                QStringLiteral("L"),
                SymbolTaxonomy::CollectorKind::Localparam);
            return completions > 0
                && latestLocalparam.isValid()
                && latestLocalparam.presentation.documentRevision
                    == latestRevision
                && globalIndex->getCachedFileContent(childFile) == latestEdit;
        },
        10000);
    const SemanticSymbolRecord localparam = findRecord(
        globalIndex->getSymbolRecords(childFile),
        QStringLiteral("L"),
        SymbolTaxonomy::CollectorKind::Localparam);
    EffectiveValueService values(globalIndex);
    const EffectiveValueResult value = resolve(
        values,
        localparam,
        latestEdit,
        instance(QStringLiteral("top.u0")),
        latestRevision);
    expect("debounced overlay publishes only the latest document revision",
           publishedLatest && value.current()
                && localparam.presentation.documentRevision == latestRevision
                && value.requestedDocumentRevision == latestRevision
                && value.computedDocumentRevision == latestRevision
                && value.valueText == QStringLiteral("12")
                && !publicationsAfterLatestRequest.isEmpty()
                && std::all_of(
                    publicationsAfterLatestRequest.cbegin(),
                    publicationsAfterLatestRequest.cend(),
                    [&](const QString& publishedContent) {
                        return publishedContent == latestEdit;
                    }));
    const QList<EffectiveValueFact> latestFacts =
        EffectiveValueService::getInstance()->factsForDocument(
            childFile,
            latestEdit,
            instance(QStringLiteral("top.u0")),
            latestRevision);
    expect("A-B-A overlay republishes facts at the final revision",
           !latestFacts.isEmpty()
               && std::all_of(
                   latestFacts.cbegin(),
                   latestFacts.cend(),
                   [&](const EffectiveValueFact& fact) {
                       return fact.documentRevision == latestRevision
                           && fact.computationRevision
                               == localparam.presentation.computationRevision;
                   }));

    EffectiveValueService::getInstance()->invalidateDocumentFacts(childFile);
    if (previous)
        globalIndex->setSnapshot(previous);
    else
        globalIndex->clearSnapshot();
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    runSlangEffectiveValueRegression();
    runLiveOverlayRegression();
    runDocumentModelWorkspaceOverlayRegression();
    runInitialWorkspaceOverlayAtomicRegression();
    runLargeDocumentRevisionRegression();
    runUtf16AndCrLfOffsetRegression();
    runHoverRevisionRegression();
    runGhostUsesSlangRegression();
    runParameterSourceDisplayEquivalenceRegression();
    runInvalidEnumIsolationRegression();
    runTopOverrideDefaultRegression();
    runParameterOverrideAnchorRegression();
    runPresentationCancellationRegression();
    runDebouncedOverlayPublicationRegression();
    std::printf("effective_value_test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
