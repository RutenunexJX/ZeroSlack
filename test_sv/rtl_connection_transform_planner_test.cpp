#include "rtlconnectiontransformplanner.h"

#include "semanticindexsnapshot.h"
#include "slangmanager.h"

#include <rtledit/mock_workspace_document_manager.h>
#include <rtledit/workspace_edit_transaction.h>

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace {

using DeclarationKind =
    SymbolTaxonomy::DeclarationKind;
using rtledit::MockWorkspaceDocumentManager;

int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                label);
}

std::string utf8(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
}

QString fromUtf8(const std::string& text)
{
    return QString::fromUtf8(
        text.data(),
        static_cast<qsizetype>(text.size()));
}

SemanticSymbolRecord findRecord(
    const QList<SemanticSymbolRecord>& records,
    const QString& name,
    DeclarationKind kind,
    const QString& owner = {})
{
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == name
            && record.declarationKind == kind
            && (owner.isEmpty()
                || record.owner.name == owner)) {
            return record;
        }
    }
    return {};
}

QString firstInstancePath(
    const SemanticSymbolRecord& record,
    const QString& preferredSuffix,
    const QString& fallback)
{
    QStringList paths =
        record.presentation.instanceInfoByPath.keys();
    std::sort(paths.begin(), paths.end());
    for (const QString& path : paths) {
        if (path.endsWith(preferredSuffix))
            return path;
    }
    return paths.isEmpty()
        ? fallback : paths.constFirst();
}

struct Fixture {
    QString fileName;
    QString source;
    QList<SemanticSymbolRecord> records;
    QList<SemanticDiagnostic> diagnostics;
    std::shared_ptr<SemanticIndexSnapshot> snapshot;
    SemanticSymbolRecord instance;
    QString selectedInstancePath;
    QString parentInstancePath;
};

Fixture makeFixture(
    const QString& leafName,
    const QString& source,
    const QString& instanceName)
{
    Fixture fixture;
    fixture.fileName =
        QDir::current().absoluteFilePath(leafName);
    fixture.source = source;

    SlangManager slang;
    const QHash<QString, QString> sources{
        {fixture.fileName, fixture.source}};
    fixture.records =
        slang.extractOverlayWorkspaceSymbolRecords(
            sources, {}, {}, nullptr, nullptr,
            {fixture.fileName});
    fixture.diagnostics =
        slang.extractOverlayWorkspaceDiagnostics(
            sources, {}, {}, nullptr,
            {fixture.fileName});
    fixture.instance = findRecord(
        fixture.records,
        instanceName,
        DeclarationKind::Instance,
        QStringLiteral("top"));

    const SemanticSymbolRecord formal =
        findRecord(
            fixture.records,
            QStringLiteral("data"),
            DeclarationKind::Port,
            QStringLiteral("child"));
    fixture.selectedInstancePath =
        firstInstancePath(
            formal,
            QLatin1Char('.') + instanceName,
            QStringLiteral("top.")
                + instanceName);
    const SemanticSymbolRecord actual =
        findRecord(
            fixture.records,
            QStringLiteral("data"),
            DeclarationKind::Signal,
            QStringLiteral("top"));
    fixture.parentInstancePath =
        firstInstancePath(
            actual,
            QStringLiteral("top"),
            QStringLiteral("top"));

    for (SemanticSymbolRecord& record :
         fixture.records) {
        record.presentation.declarationText =
            QStringLiteral("renderer_only_bogus");
        record.presentation.packedDimensionsText =
            QStringLiteral("[renderer_only_bogus]");
        record.presentation.unpackedDimensionsText =
            QStringLiteral("[renderer_only_bogus]");
        record.presentation.defaultInfo
            .resolvedTypeText =
            QStringLiteral("renderer_only_bogus");
        record.presentation.defaultInfo
            .bitWidthText =
            QStringLiteral("renderer_only_bogus");
        record.presentation.defaultInfo
            .signednessText =
            QStringLiteral("renderer_only_bogus");
        for (auto info =
                 record.presentation
                     .instanceInfoByPath.begin();
             info != record.presentation
                         .instanceInfoByPath.end();
             ++info) {
            info->resolvedTypeText =
                QStringLiteral("renderer_only_bogus");
            info->bitWidthText =
                QStringLiteral("renderer_only_bogus");
            info->signednessText =
                QStringLiteral("renderer_only_bogus");
            info->packedDimensionsText =
                QStringLiteral(
                    "[renderer_only_bogus]");
            info->unpackedDimensionsText =
                QStringLiteral(
                    "[renderer_only_bogus]");
        }
    }

    fixture.snapshot =
        std::make_shared<SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                fixture.records,
                {},
                fixture.diagnostics,
                sources));
    return fixture;
}

RtlConnectionTransformRequest requestFor(
    const Fixture& fixture,
    std::uint64_t generation,
    std::uint64_t documentRevision)
{
    RtlConnectionTransformRequest request;
    request.instanceStableKey =
        fixture.instance.stableKey;
    request.selectedInstancePath =
        fixture.selectedInstancePath;
    request.parentInstancePath =
        fixture.parentInstancePath;
    request.expectedSemanticGeneration =
        generation;
    request.expectedDocumentRevision =
        documentRevision;
    request.convertOrderedToNamed = true;
    request.addMissingPorts = true;
    request.missingPortPolicy =
        RtlMissingPortConnectionPolicy::
            LeaveUnconnected;
    request.castPolicy =
        RtlExplicitCastPolicy::
            InsertWhenRequired;
    return request;
}

QString positiveSource()
{
    return QStringLiteral(
        "module child #(parameter int W = 8) (\n"
        "  input  logic signed [W-1:0] data,\n"
        "  input  logic enable,\n"
        "  output logic [W-1:0] result,\n"
        "  input  logic extra\n"
        ");\n"
        "  assign result = data;\n"
        "endmodule\n"
        "\n"
        "module top;\n"
        "  logic signed [3:0] data;\n"
        "  logic enable;\n"
        "  logic [7:0] result;\n"
        "  logic extra;\n"
        "  child #(.W(8)) u_child(\n"
        "    data,\n"
        "    enable,\n"
        "    result\n"
        "  );\n"
        "endmodule\n");
}

QString namedCastSource(
    const QString& dataDeclaration =
        QStringLiteral("logic signed [3:0] data"),
    const QString& dataActual =
        QStringLiteral("data"))
{
    return QStringLiteral(
               "module child (\n"
               "  input  logic signed [7:0] data,\n"
               "  input  logic enable,\n"
               "  output logic [7:0] result,\n"
               "  input  logic extra\n"
               ");\n"
               "  assign result = data;\n"
               "endmodule\n"
               "\n"
               "module top;\n"
               "  %1;\n"
               "  logic enable;\n"
               "  logic [7:0] result;\n"
               "  child u_child(\n"
               "    .data(%2),\n"
               "    .enable(enable),\n"
               "    .result(result),\n"
               "    .extra()\n"
               "  );\n"
               "endmodule\n")
        .arg(dataDeclaration, dataActual);
}

QString orderedCastSource(
    const QString& dataDeclaration =
        QStringLiteral("logic signed [3:0] data"))
{
    return QStringLiteral(
               "module child (\n"
               "  input  logic signed [7:0] data,\n"
               "  input  logic enable,\n"
               "  output logic [7:0] result\n"
               ");\n"
               "  assign result = data;\n"
               "endmodule\n"
               "\n"
               "module top;\n"
               "  %1;\n"
               "  logic enable;\n"
               "  logic [7:0] result;\n"
               "  child u_child(\n"
               "    data,\n"
               "    enable,\n"
               "    result\n"
               "  );\n"
               "endmodule\n")
        .arg(dataDeclaration);
}

RtlConnectionTransformRequest pureCastRequest(
    const Fixture& fixture,
    std::uint64_t generation,
    std::uint64_t documentRevision)
{
    RtlConnectionTransformRequest request =
        requestFor(
            fixture, generation, documentRevision);
    request.convertOrderedToNamed = false;
    request.addMissingPorts = false;
    request.castPolicy =
        RtlExplicitCastPolicy::InsertWhenRequired;
    return request;
}

void runNamedConnectionCastRegression()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_named_cast_fixture.sv"),
        namedCastSource(),
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    const std::uint64_t generation =
        index.snapshotRevision();
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{31});

    const auto report =
        RtlConnectionTransformPlanner(&index).plan(
            pureCastRequest(
                fixture, generation, 31),
            documents);
    const bool hasSingleExpressionEdit =
        report.workspaceEdit.edits.size() == 1
        && report.workspaceEdit.provenance.size() == 1;
    expect("pure named-port cast request produces one expression edit",
           report.ready()
               && hasSingleExpressionEdit
               && report.workspaceEdit.riskLevel
                      == rtledit::RiskLevel::High
               && report.workspaceEdit.previewPolicy
                      == rtledit::PreviewPolicy::Diff);
    bool dataCast = false;
    bool emptyConnectionPreserved = false;
    for (const RtlConnectionFormalView& formal :
         report.formals) {
        if (formal.formalName
                == QStringLiteral("data")) {
            dataCast =
                formal.originallyConnected
                && formal.explicitCastInserted
                && formal.actualText.endsWith(
                    QStringLiteral("'(data)"));
        } else if (formal.formalName
                       == QStringLiteral("extra")) {
            emptyConnectionPreserved =
                formal.originallyConnected
                && formal.actualText.isEmpty()
                && !formal.explicitCastInserted;
        }
    }
    expect("Slang width and signedness facts drive named input cast",
           dataCast);
    expect("empty named connection remains unchanged",
           emptyConnectionPreserved);
    expect("named cast provenance anchors only the actual expression",
           hasSingleExpressionEdit
               && report.workspaceEdit.edits.front()
                      .expectedText
                      == "data"
               && report.workspaceEdit.provenance.front()
                      .anchorName
                      == "connection.named.explicit-cast"
               && report.workspaceEdit.provenance.front()
                      .anchor.source
                      == rtledit::AnchorResolutionSource::
                          TreeSitter);
    expect("pure cast planning is preview-only",
           documents.text(utf8(fixture.fileName))
               == utf8(fixture.source));

    rtledit::WorkspaceEditTransactionCoordinator
        transactions;
    auto prepared = transactions.prepare(
        report.workspaceEdit,
        rtledit::SemanticIndexSnapshot{
            std::to_string(generation)},
        documents);
    expect("pure cast retains high-risk diff preview gate",
           prepared.ready()
               && prepared.preview.built()
               && prepared.sourceDiff.built()
               && transactions.confirmPreview(&prepared));
    const auto applied = transactions.apply(
        prepared,
        rtledit::SemanticIndexSnapshot{
            std::to_string(generation)},
        documents);
    const QString after = fromUtf8(
        documents.text(utf8(fixture.fileName)));
    QString expectedAfter = fixture.source;
    if (hasSingleExpressionEdit) {
        expectedAfter.replace(
            QStringLiteral(".data(data)"),
            QStringLiteral(".data(%1)")
                .arg(fromUtf8(
                    report.workspaceEdit.edits.front()
                        .newText)));
    }
    expect("confirmed pure cast replaces only the named actual",
           applied.status
                   == rtledit::TransactionStatus::Applied
               && hasSingleExpressionEdit
               && after == expectedAfter
               && fromUtf8(
                      report.workspaceEdit.edits.front()
                          .newText)
                      .endsWith(
                          QStringLiteral("'(data)")));
    const auto undone = transactions.undo(documents);
    expect("one undo restores pure named cast transaction",
           undone.status
                   == rtledit::TransactionStatus::Undone
               && documents.text(utf8(fixture.fileName))
                      == utf8(fixture.source));
}

void runNamedConnectionNoChangesRegression()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_named_same_type_fixture.sv"),
        namedCastSource(
            QStringLiteral(
                "logic signed [7:0] data")),
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{32});

    const auto report =
        RtlConnectionTransformPlanner(&index).plan(
            pureCastRequest(
                fixture,
                index.snapshotRevision(),
                32),
            documents);
    expect("same-type and empty named actuals yield NoChanges",
           report.status
                   == RtlConnectionTransformStatus::
                       NoChanges
               && report.failure
                      == RtlConnectionTransformFailure::None
               && report.workspaceEdit.edits.empty()
               && documents.text(utf8(fixture.fileName))
                      == utf8(fixture.source));
}

void runNamedConnectionComplexActualRejection()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_named_complex_fixture.sv"),
        namedCastSource(
            QStringLiteral(
                "logic signed [3:0] data"),
            QStringLiteral("data + 1'b1")),
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{33});

    const auto report =
        RtlConnectionTransformPlanner(&index).plan(
            pureCastRequest(
                fixture,
                index.snapshotRevision(),
                33),
            documents);
    expect("complex named actual is rejected without textual inference",
           report.status
                   == RtlConnectionTransformStatus::
                       Rejected
               && report.failure
                      == RtlConnectionTransformFailure::
                          CastNotProvable
               && report.workspaceEdit.edits.empty()
               && documents.text(utf8(fixture.fileName))
                      == utf8(fixture.source));
}

void runNamedConnectionStaleRejections()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_named_stale_fixture.sv"),
        namedCastSource(),
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    const std::uint64_t generation =
        index.snapshotRevision();
    RtlConnectionTransformPlanner planner(&index);

    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{34});
    auto request =
        pureCastRequest(
            fixture, generation + 1, 34);
    const auto semanticGeneration =
        planner.plan(request, documents);
    expect("pure named cast rejects stale semantic generation",
           semanticGeneration.failure
               == RtlConnectionTransformFailure::
                   StaleSemanticGeneration);

    request.expectedSemanticGeneration = generation;
    request.expectedDocumentRevision = 35;
    const auto documentRevision =
        planner.plan(request, documents);
    expect("pure named cast rejects stale document revision",
           documentRevision.failure
               == RtlConnectionTransformFailure::
                   StaleDocumentRevision);

    MockWorkspaceDocumentManager staleSourceDocuments;
    const QString staleSource =
        fixture.source
        + QStringLiteral("\n");
    staleSourceDocuments.openDocument(
        utf8(fixture.fileName),
        utf8(staleSource),
        rtledit::DocumentVersion{34});
    request.expectedDocumentRevision = 34;
    const auto semanticSource =
        planner.plan(request, staleSourceDocuments);
    expect("pure named cast rejects stale semantic source",
           semanticSource.failure
                   == RtlConnectionTransformFailure::
                       StaleSemanticSource
               && staleSourceDocuments.text(
                      utf8(fixture.fileName))
                      == utf8(staleSource));
}

void runOrderedConnectionPureCastRegression()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_ordered_pure_cast_fixture.sv"),
        orderedCastSource(),
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    const std::uint64_t generation =
        index.snapshotRevision();
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{36});

    const auto report =
        RtlConnectionTransformPlanner(&index).plan(
            pureCastRequest(
                fixture, generation, 36),
            documents);
    const bool hasSingleExpressionEdit =
        report.workspaceEdit.edits.size() == 1
        && report.workspaceEdit.provenance.size() == 1;
    expect("pure ordered cast produces one expression-level edit",
           report.ready()
               && hasSingleExpressionEdit
               && report.workspaceEdit.riskLevel
                      == rtledit::RiskLevel::High
               && report.workspaceEdit.previewPolicy
                      == rtledit::PreviewPolicy::Diff
               && report.workspaceEdit.edits.front()
                      .expectedText
                      == "data"
               && report.workspaceEdit.provenance.front()
                      .anchorName
                      == "connection.ordered.explicit-cast"
               && report.workspaceEdit.provenance.front()
                      .anchor.source
                      == rtledit::AnchorResolutionSource::
                          TreeSitter);
    expect("pure ordered cast planning does not mutate the document",
           documents.text(utf8(fixture.fileName))
               == utf8(fixture.source));

    rtledit::WorkspaceEditTransactionCoordinator
        transactions;
    auto prepared = transactions.prepare(
        report.workspaceEdit,
        rtledit::SemanticIndexSnapshot{
            std::to_string(generation)},
        documents);
    const bool previewConfirmed =
        prepared.ready()
        && prepared.preview.built()
        && prepared.sourceDiff.built()
        && transactions.confirmPreview(&prepared);
    expect("pure ordered cast retains high-risk diff preview",
           previewConfirmed);
    const auto applied = transactions.apply(
        prepared,
        rtledit::SemanticIndexSnapshot{
            std::to_string(generation)},
        documents);
    const QString after = fromUtf8(
        documents.text(utf8(fixture.fileName)));
    QString expectedAfter = fixture.source;
    if (hasSingleExpressionEdit) {
        expectedAfter.replace(
            QStringLiteral("    data,\n"),
            QStringLiteral("    %1,\n")
                .arg(fromUtf8(
                    report.workspaceEdit.edits.front()
                        .newText)));
    }
    expect("ordered pure cast applies atomically without named conversion",
           applied.status
                   == rtledit::TransactionStatus::Applied
               && hasSingleExpressionEdit
               && after == expectedAfter
               && !after.contains(
                   QStringLiteral("\n    .data("))
               && !after.contains(
                   QStringLiteral("\n    .enable("))
               && !after.contains(
                   QStringLiteral("\n    .result(")));
    const auto undone = transactions.undo(documents);
    expect("one undo restores the ordered pure cast",
           undone.status
                   == rtledit::TransactionStatus::Undone
               && documents.text(utf8(fixture.fileName))
                      == utf8(fixture.source));
}

void runOrderedConnectionPureCastNoChangesRegression()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_ordered_same_type_fixture.sv"),
        orderedCastSource(
            QStringLiteral(
                "logic signed [7:0] data")),
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{37});

    const auto report =
        RtlConnectionTransformPlanner(&index).plan(
            pureCastRequest(
                fixture,
                index.snapshotRevision(),
                37),
            documents);
    expect("same-type ordered pure cast yields NoChanges",
           report.status
                   == RtlConnectionTransformStatus::
                       NoChanges
               && report.failure
                      == RtlConnectionTransformFailure::None
               && report.workspaceEdit.edits.empty()
               && documents.text(utf8(fixture.fileName))
                      == utf8(fixture.source));
}

void runOrderedMissingPortWithoutConversionRejection()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_ordered_missing_rejection_fixture.sv"),
        positiveSource(),
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{38});
    RtlConnectionTransformRequest request =
        pureCastRequest(
            fixture,
            index.snapshotRevision(),
            38);
    request.addMissingPorts = true;

    const auto report =
        RtlConnectionTransformPlanner(&index).plan(
            request, documents);
    expect("ordered add-missing without conversion is structurally rejected",
           report.status
                   == RtlConnectionTransformStatus::
                       Rejected
               && report.failure
                      == RtlConnectionTransformFailure::
                          MixedConnectionStyle
               && report.message
                      == QStringLiteral(
                          "Missing named port connections cannot be "
                          "appended to an ordered connection list "
                          "unless ordered-to-named conversion is "
                          "enabled.")
               && report.workspaceEdit.edits.empty()
               && documents.text(utf8(fixture.fileName))
                      == utf8(fixture.source));
}

void runPositiveTransactionRegression()
{
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_transform_fixture.sv"),
        positiveSource(),
        QStringLiteral("u_child"));
    const SemanticSymbolRecord formalData =
        findRecord(
            fixture.records,
            QStringLiteral("data"),
            DeclarationKind::Port,
            QStringLiteral("child"));
    const SemanticSymbolRecord actualData =
        findRecord(
            fixture.records,
            QStringLiteral("data"),
            DeclarationKind::Signal,
            QStringLiteral("top"));
    expect("real Slang fixture resolves selected module instance",
           fixture.instance.isValid()
               && !fixture.records.isEmpty()
               && !formalData.presentation
                       .instanceInfoByPath.isEmpty()
               && !actualData.presentation
                       .instanceInfoByPath.isEmpty()
               && formalData.presentation
                      .instanceInfoByPath.constBegin()
                      ->resolvedTypeText
                      == QStringLiteral(
                          "renderer_only_bogus")
               && fixture.selectedInstancePath
                      .endsWith(
                          QStringLiteral(".u_child")));

    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    const std::uint64_t generation =
        index.snapshotRevision();
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{19});

    RtlConnectionTransformPlanner planner(&index);
    const RtlConnectionTransformReport report =
        planner.plan(
            requestFor(fixture, generation, 19),
            documents);
    expect("ordered conversion plus missing-port plan is ready",
           report.ready()
               && report.workspaceEdit.riskLevel
                      == rtledit::RiskLevel::High
               && report.workspaceEdit.previewPolicy
                      == rtledit::PreviewPolicy::Diff
               && report.workspaceEdit.semanticSnapshot.id
                      == std::to_string(generation));
    expect("Slang declaration anchors define the formal order",
           report.formals.size() == 4
               && report.formals.at(0).formalName
                      == QStringLiteral("data")
               && report.formals.at(1).formalName
                      == QStringLiteral("enable")
               && report.formals.at(2).formalName
                      == QStringLiteral("result")
               && report.formals.at(3).formalName
                      == QStringLiteral("extra"));
    expect("planner is preview-only and does not mutate document",
           documents.text(utf8(fixture.fileName))
               == utf8(fixture.source));

    bool sawDataCast = false;
    bool sawMissingExtra = false;
    bool everyEditHasTreeSitterProvenance =
        report.workspaceEdit.edits.size()
        == report.workspaceEdit.provenance.size();
    for (int index = 0;
         index < report.formals.size(); ++index) {
        const RtlConnectionFormalView& formal =
            report.formals.at(index);
        if (formal.formalName
                == QStringLiteral("data")) {
            sawDataCast =
                formal.originallyConnected
                && formal.explicitCastInserted
                && formal.actualText.contains(
                    QStringLiteral("'(data)"));
        }
        if (formal.formalName
                == QStringLiteral("extra")) {
            sawMissingExtra =
                !formal.originallyConnected
                && formal.added
                && formal.actualText.isEmpty();
        }
    }
    for (const auto& item :
         report.workspaceEdit.provenance) {
        everyEditHasTreeSitterProvenance =
            everyEditHasTreeSitterProvenance
            && item.anchor.source
                   == rtledit::AnchorResolutionSource::
                       TreeSitter
            && item.anchor.semanticSnapshotId
                   == std::to_string(generation);
    }
    expect("Slang machine types drive explicit width/signed cast",
           sawDataCast);
    expect("missing formal is added as explicit unconnected named port",
           sawMissingExtra);
    expect("every edit carries Tree-sitter and generation provenance",
           everyEditHasTreeSitterProvenance);

    rtledit::WorkspaceEditTransactionCoordinator
        transactions;
    const auto stalePrepared =
        transactions.prepare(
            report.workspaceEdit,
            rtledit::SemanticIndexSnapshot{
                std::to_string(generation + 1)},
            documents);
    expect("transaction rejects a plan after semantic generation changes",
           stalePrepared.status
               == rtledit::TransactionPrepareStatus::
                   Stale);
    auto prepared = transactions.prepare(
        report.workspaceEdit,
        rtledit::SemanticIndexSnapshot{
            std::to_string(generation)},
        documents);
    expect("high-risk plan builds structured preview and source diff",
           prepared.ready()
               && prepared.preview.built()
               && prepared.sourceDiff.built());
    const auto unconfirmed =
        transactions.apply(
            prepared,
            rtledit::SemanticIndexSnapshot{
                std::to_string(generation)},
            documents);
    expect("unconfirmed high-risk transform cannot apply",
           unconfirmed.status
               == rtledit::TransactionStatus::
                   PreviewRequired
               && documents.text(
                      utf8(fixture.fileName))
                      == utf8(fixture.source));

    expect("preview confirmation succeeds",
           transactions.confirmPreview(&prepared));
    const auto applied =
        transactions.apply(
            prepared,
            rtledit::SemanticIndexSnapshot{
                std::to_string(generation)},
            documents);
    const QString after =
        fromUtf8(
            documents.text(
                utf8(fixture.fileName)));
    expect("confirmed transaction applies named connections atomically",
           applied.status
                   == rtledit::TransactionStatus::
                       Applied
               && after.contains(
                   QStringLiteral(".data("))
               && after.contains(
                   QStringLiteral("'(data))"))
               && after.contains(
                   QStringLiteral(".enable(enable)"))
               && after.contains(
                   QStringLiteral(".result(result)"))
               && after.contains(
                   QStringLiteral(".extra()")));
    const auto undone = transactions.undo(documents);
    expect("one undo restores the whole transform",
           undone.status
                   == rtledit::TransactionStatus::
                       Undone
               && documents.text(
                      utf8(fixture.fileName))
                      == utf8(fixture.source));

    MockWorkspaceDocumentManager sameNamedDocuments;
    sameNamedDocuments.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{23});
    RtlConnectionTransformRequest sameNamedRequest =
        requestFor(fixture, generation, 23);
    sameNamedRequest.missingPortPolicy =
        RtlMissingPortConnectionPolicy::
            ConnectSameNamedSignal;
    sameNamedRequest.castPolicy =
        RtlExplicitCastPolicy::
            PreserveExistingExpression;
    const auto sameNamed =
        planner.plan(
            sameNamedRequest,
            sameNamedDocuments);
    bool sameNamedExtra = false;
    for (const auto& formal : sameNamed.formals) {
        if (formal.formalName
                == QStringLiteral("extra")) {
            sameNamedExtra =
                formal.added
                && formal.actualText
                       == QStringLiteral("extra");
        }
    }
    expect("same-named missing port is connected only from a unique Slang signal",
           sameNamed.ready() && sameNamedExtra);
}

void runWildcardAndStaleRejections()
{
    const QString wildcardSource = QStringLiteral(
        "module child(input logic data, input logic extra);\n"
        "endmodule\n"
        "module top;\n"
        "  logic data;\n"
        "  logic extra;\n"
        "  child u_child(.*);\n"
        "endmodule\n");
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_wildcard_fixture.sv"),
        wildcardSource,
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    const std::uint64_t generation =
        index.snapshotRevision();
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{7});

    RtlConnectionTransformPlanner planner(&index);
    RtlConnectionTransformRequest request =
        requestFor(fixture, generation, 7);
    request.castPolicy =
        RtlExplicitCastPolicy::
            PreserveExistingExpression;
    const auto wildcard =
        planner.plan(request, documents);
    expect("Tree-sitter wildcard connection is rejected",
           wildcard.status
                   == RtlConnectionTransformStatus::
                       Rejected
               && wildcard.failure
                   == RtlConnectionTransformFailure::
                       WildcardConnection);

    request.expectedSemanticGeneration =
        generation + 1;
    const auto staleGeneration =
        planner.plan(request, documents);
    expect("stale semantic generation is rejected before planning",
           staleGeneration.failure
               == RtlConnectionTransformFailure::
                   StaleSemanticGeneration);

    request.expectedSemanticGeneration =
        generation;
    request.expectedDocumentRevision = 8;
    const auto staleDocument =
        planner.plan(request, documents);
    expect("stale document revision is rejected before planning",
           staleDocument.failure
               == RtlConnectionTransformFailure::
                   StaleDocumentRevision);
}

void runMultiInstanceTypeDifferenceRejection()
{
    const QString source = QStringLiteral(
        "module child #(parameter int W = 8) (\n"
        "  input logic [W-1:0] data\n"
        ");\n"
        "endmodule\n"
        "module top;\n"
        "  logic [3:0] d4;\n"
        "  logic [7:0] d8;\n"
        "  child #(.W(4)) u4(d4);\n"
        "  child #(.W(8)) u8(d8);\n"
        "endmodule\n");
    Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_multi_instance_fixture.sv"),
        source,
        QStringLiteral("u4"));
    const SemanticSymbolRecord formal =
        findRecord(
            fixture.records,
            QStringLiteral("data"),
            DeclarationKind::Port,
            QStringLiteral("child"));
    fixture.selectedInstancePath =
        firstInstancePath(
            formal,
            QStringLiteral(".u4"),
            QStringLiteral("top.u4"));
    fixture.instance = findRecord(
        fixture.records,
        QStringLiteral("u4"),
        DeclarationKind::Instance,
        QStringLiteral("top"));

    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{5});
    RtlConnectionTransformRequest request =
        requestFor(
            fixture,
            index.snapshotRevision(),
            5);
    request.addMissingPorts = false;
    request.castPolicy =
        RtlExplicitCastPolicy::
            PreserveExistingExpression;

    const auto report =
        RtlConnectionTransformPlanner(&index)
            .plan(request, documents);
    expect("parameterized multi-instance formal differences are rejected",
           formal.presentation.instanceInfoByPath
                       .size() >= 2
               && report.failure
                   == RtlConnectionTransformFailure::
                       MultiInstanceTypeDifference);
}

void runUnprovableCastRejection()
{
    QString source = positiveSource();
    source.replace(
        QStringLiteral(
            "    data,\n"
            "    enable"),
        QStringLiteral(
            "    data + 1'b1,\n"
            "    enable"));
    const Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_unprovable_cast_fixture.sv"),
        source,
        QStringLiteral("u_child"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{9});
    RtlConnectionTransformRequest request =
        requestFor(
            fixture,
            index.snapshotRevision(),
            9);

    const auto report =
        RtlConnectionTransformPlanner(&index)
            .plan(request, documents);
    expect("complex actual is rejected when an explicit cast cannot be proven",
           report.failure
               == RtlConnectionTransformFailure::
                   CastNotProvable);
}

void runAmbiguousFormalRejection()
{
    Fixture fixture = makeFixture(
        QStringLiteral(
            "rtl_connection_ambiguous_formal_fixture.sv"),
        positiveSource(),
        QStringLiteral("u_child"));
    SemanticSymbolRecord duplicate =
        findRecord(
            fixture.records,
            QStringLiteral("enable"),
            DeclarationKind::Port,
            QStringLiteral("child"));
    duplicate.location.position += 1;
    duplicate.stableKey.sourcePosition += 1;
    fixture.records.append(duplicate);
    fixture.snapshot =
        std::make_shared<SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                fixture.records,
                {},
                fixture.diagnostics,
                {{fixture.fileName,
                  fixture.source}}));

    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{3});
    RtlConnectionTransformRequest request =
        requestFor(
            fixture,
            index.snapshotRevision(),
            3);
    request.castPolicy =
        RtlExplicitCastPolicy::
            PreserveExistingExpression;

    const auto report =
        RtlConnectionTransformPlanner(&index)
            .plan(request, documents);
    expect("duplicate Slang formal identity is rejected",
           report.failure
               == RtlConnectionTransformFailure::
                   AmbiguousFormal);
}

void runModulePortSynchronizationRegression()
{
    const QString source = QStringLiteral(
        "module child (\n"
        "  input logic data,\n"
        "  input logic added\n"
        ");\n"
        "endmodule\n"
        "module top;\n"
        "  logic data;\n"
        "  logic obsolete;\n"
        "  child u0(\n"
        "    .data(data),\n"
        "    .obsolete(obsolete)\n"
        "  );\n"
        "  child u1(\n"
        "    .data(data),\n"
        "    .obsolete(obsolete)\n"
        "  );\n"
        "endmodule\n");
    const Fixture fixture = makeFixture(
        QStringLiteral("rtl_connection_sync_fixture.sv"),
        source,
        QStringLiteral("u0"));
    SemanticIndex index;
    index.setSnapshot(fixture.snapshot);
    const std::uint64_t generation = index.snapshotRevision();
    MockWorkspaceDocumentManager documents;
    documents.openDocument(
        utf8(fixture.fileName),
        utf8(fixture.source),
        rtledit::DocumentVersion{81});

    RtlConnectionTransformRequest request = requestFor(
        fixture, generation, 81);
    request.convertOrderedToNamed = false;
    request.addMissingPorts = true;
    request.removeUnknownPorts = true;
    request.synchronizeAllInstances = true;
    request.castPolicy =
        RtlExplicitCastPolicy::PreserveExistingExpression;

    const RtlConnectionTransformReport report =
        RtlConnectionTransformPlanner(&index).plan(request, documents);
    expect("module-port synchronization creates one High+Diff plan",
           report.ready()
               && report.workspaceEdit.riskLevel
                      == rtledit::RiskLevel::High
               && report.workspaceEdit.previewPolicy
                      == rtledit::PreviewPolicy::Diff);

    rtledit::WorkspaceEditTransactionCoordinator transactions;
    auto prepared = transactions.prepare(
        report.workspaceEdit,
        rtledit::SemanticIndexSnapshot{
            std::to_string(generation)},
        documents);
    const bool confirmed = prepared.ready()
        && prepared.preview.built()
        && prepared.sourceDiff.built()
        && transactions.confirmPreview(&prepared);
    const auto applied = transactions.apply(
        prepared,
        rtledit::SemanticIndexSnapshot{
            std::to_string(generation)},
        documents);
    const QString after = fromUtf8(
        documents.text(utf8(fixture.fileName)));
    expect("module-port synchronization removes obsolete associations",
           confirmed
               && applied.status
                      == rtledit::TransactionStatus::Applied
               && !after.contains(QStringLiteral(".obsolete(")));
    expect("module-port synchronization fills every source instance",
           after.count(QStringLiteral(".added()")) == 2
               && after.count(QStringLiteral("child u")) == 2);
}

} // namespace

int main()
{
    runNamedConnectionCastRegression();
    runNamedConnectionNoChangesRegression();
    runNamedConnectionComplexActualRejection();
    runNamedConnectionStaleRejections();
    runOrderedConnectionPureCastRegression();
    runOrderedConnectionPureCastNoChangesRegression();
    runOrderedMissingPortWithoutConversionRejection();
    runPositiveTransactionRegression();
    runWildcardAndStaleRejections();
    runMultiInstanceTypeDifferenceRejection();
    runUnprovableCastRejection();
    runAmbiguousFormalRejection();
    runModulePortSynchronizationRegression();

    std::printf(
        "rtl_connection_transform_planner_test: "
        "%d checks, %d failure(s)\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
