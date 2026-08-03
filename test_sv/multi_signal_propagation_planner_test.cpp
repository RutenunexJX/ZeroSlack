#include "hierarchyservice.h"
#include "multisignalpropagationplanner.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tsdocument.h"

#include <rtledit/text_edit.h>
#include <rtledit/workspace_edit_transaction.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

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

QString normalized(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool writeUtf8(const QString& fileName,
               const QString& text)
{
    QDir().mkpath(
        QFileInfo(fileName).absolutePath());
    QFile file(fileName);
    return file.open(QIODevice::WriteOnly)
        && file.write(text.toUtf8())
               == text.toUtf8().size();
}

class MemoryDocuments final
    : public rtledit::WorkspaceDocumentManager
{
public:
    struct Document {
        QString text;
        std::uint64_t revision = 1;
    };

    QHash<QString, Document> docs;
    QString failApplyFile;

    void add(const QString& fileName,
             const QString& text,
             std::uint64_t revision = 1)
    {
        docs.insert(
            normalized(fileName),
            {text, revision});
    }

    std::optional<rtledit::WorkspaceDocumentSnapshot>
    snapshot(const std::string& filePath) const override
    {
        const QString fileName = normalized(
            QString::fromUtf8(
                filePath.data(),
                static_cast<qsizetype>(
                    filePath.size())));
        const auto found = docs.constFind(fileName);
        if (found == docs.constEnd())
            return std::nullopt;
        return rtledit::WorkspaceDocumentSnapshot{
            {found->revision},
            found->text.toUtf8().toStdString()};
    }

    bool applyTextEdits(
        const std::string& filePath,
        rtledit::DocumentVersion expectedVersion,
        const std::vector<
            rtledit::WorkspaceTextEdit>& edits) override
    {
        const QString fileName = normalized(
            QString::fromUtf8(
                filePath.data(),
                static_cast<qsizetype>(
                    filePath.size())));
        auto found = docs.find(fileName);
        if (found == docs.end()
            || found->revision
                   != expectedVersion.value
            || (!failApplyFile.isEmpty()
                && normalized(failApplyFile)
                       == fileName)) {
            return false;
        }
        const auto next =
            rtledit::applyTextEditsToString(
                found->text.toUtf8().toStdString(),
                edits);
        if (!next)
            return false;
        found->text = QString::fromUtf8(
            next->data(),
            static_cast<qsizetype>(
                next->size()));
        ++found->revision;
        return true;
    }

    bool restoreSnapshot(
        const std::string& filePath,
        const rtledit::WorkspaceDocumentSnapshot&
            snapshot) override
    {
        const QString fileName = normalized(
            QString::fromUtf8(
                filePath.data(),
                static_cast<qsizetype>(
                    filePath.size())));
        auto found = docs.find(fileName);
        if (found == docs.end())
            return false;
        found->text = QString::fromUtf8(
            snapshot.text.data(),
            static_cast<qsizetype>(
                snapshot.text.size()));
        found->revision = snapshot.version.value;
        return true;
    }
};

bool isModule(const SemanticSymbolRecord& record)
{
    return record.declarationKind
        == SymbolTaxonomy::DeclarationKind::Module;
}

bool isInstance(const SemanticSymbolRecord& record)
{
    return record.collectorKind
        == SymbolTaxonomy::CollectorKind::Inst;
}

void assignLocalHandles(
    QList<SemanticSymbolRecord>* records)
{
    int next = 1;
    for (SemanticSymbolRecord& record : *records) {
        if (record.localHandle < 0)
            record.localHandle = next;
        next = std::max(
            next, record.localHandle + 1);
    }
}

QList<SemanticRelationship>
instantiationRelationships(
    const QList<SemanticSymbolRecord>& records)
{
    QHash<QString, QList<SemanticSymbolRecord>>
        modulesByName;
    for (const SemanticSymbolRecord& record :
         records) {
        if (isModule(record))
            modulesByName[record.name].append(record);
    }

    QList<SemanticRelationship> result;
    for (const SemanticSymbolRecord& instance :
         records) {
        if (!isInstance(instance)
            || instance.owner.kind
                   != SymbolTaxonomy::
                       SymbolOwnerScope::Module
            || instance.owner.name.isEmpty()
            || instance.type.resolvedTypeName
                   .isEmpty()) {
            continue;
        }
        const QList<SemanticSymbolRecord> owners =
            modulesByName.value(
                instance.owner.name);
        if (owners.size() != 1)
            continue;
        SemanticRelationship relationship;
        relationship.fromId =
            owners.constFirst().localHandle;
        relationship.toId = instance.localHandle;
        relationship.type =
            SymbolRelationshipEngine::INSTANTIATES;
        relationship.fromStableKey =
            owners.constFirst().stableKey;
        relationship.toStableKey =
            instance.stableKey;
        relationship.provenance =
            RelationshipProvenance::SlangExtracted;
        relationship.confidence = 100;
        relationship.evidenceText =
            QStringLiteral(
                "Slang-resolved module instantiation");
        relationship.evidenceRange.fileName =
            instance.location.fileName;
        relationship.evidenceRange.line =
            instance.location.startLine;
        relationship.evidenceRange.column =
            instance.location.startColumn;
        relationship.evidenceRange.endLine =
            instance.location.endLine;
        relationship.evidenceRange.endColumn =
            instance.location.endColumn;
        relationship.evidenceRange.position =
            instance.location.position;
        relationship.evidenceRange.length =
            instance.location.length;
        result.append(std::move(relationship));
    }
    return result;
}

enum class FixtureVariant {
    Normal,
    ReuseCompatiblePort,
    DirectionConflict,
    PositionalConnection,
    WildcardConnection,
    MixedConnection,
    MultiInstanceWidthDifference
};

struct Fixture {
    QString root;
    QString leafFile;
    QString middleFile;
    QString topFile;
    QString leafText;
    QString middleText;
    QString topText;
    QStringList orderedFiles;
    QSet<QString> workspaceFiles;
    QHash<QString, QString> semanticContents;
    QList<SemanticSymbolRecord> records;
    std::shared_ptr<const SemanticIndexSnapshot>
        snapshot;
    SemanticIndex index;
    std::unique_ptr<HierarchyService> hierarchy;
    MemoryDocuments documents;
    QString sourceInstancePath;
};

std::unique_ptr<Fixture> makeFixture(
    const QString& root,
    FixtureVariant variant =
        FixtureVariant::Normal)
{
    auto fixture = std::make_unique<Fixture>();
    fixture->root = normalized(root);
    fixture->leafFile = normalized(
        QDir(root).filePath(
            QStringLiteral("leaf.sv")));
    fixture->middleFile = normalized(
        QDir(root).filePath(
            QStringLiteral("middle.sv")));
    fixture->topFile = normalized(
        QDir(root).filePath(
            QStringLiteral("top.sv")));

    fixture->leafText = QStringLiteral(
        "module leaf #(\n"
        "    parameter int WIDTH = 8\n"
        ") (\n"
        "    input logic clk,\n"
        "    input logic enable\n"
        ");\n"
        "    logic [WIDTH-1:0] payload;\n"
        "    logic signed [3:0] flags;\n"
        "    logic [3:0] lanes [0:1];\n"
        "endmodule\n");

    QString middlePorts =
        QStringLiteral(
            "    input logic clk,\n"
            "    input logic enable");
    if (variant
            == FixtureVariant::
                ReuseCompatiblePort) {
        middlePorts += QStringLiteral(
            ",\n"
            "    output logic [7:0] debug_payload");
    } else if (variant
               == FixtureVariant::
                   DirectionConflict) {
        middlePorts += QStringLiteral(
            ",\n"
            "    input logic [3:0] debug_flags");
    }

    const QString firstInstance =
        variant
                == FixtureVariant::
                    PositionalConnection
            ? QStringLiteral(
                  "    leaf #(.WIDTH(8)) "
                  "u_leaf(clk, enable);\n")
            : variant
                      == FixtureVariant::
                          WildcardConnection
                  ? QStringLiteral(
                        "    leaf #(.WIDTH(8)) "
                        "u_leaf(.*);\n")
                  : variant
                            == FixtureVariant::
                                MixedConnection
                        ? QStringLiteral(
                              "    leaf #(.WIDTH(8)) "
                              "u_leaf(\n"
                              "        .clk(clk),\n"
                              "        enable\n"
                              "    );\n")
                        : QStringLiteral(
                  "    leaf #(.WIDTH(8)) u_leaf(\n"
                  "        .clk(clk),\n"
                  "        .enable(enable)\n"
                  "    );\n");
    const int secondWidth =
        variant
                == FixtureVariant::
                    MultiInstanceWidthDifference
            ? 16 : 8;
    fixture->middleText =
        QStringLiteral("module middle(\n")
        + middlePorts
        + QStringLiteral(
              "\n"
              ");\n")
        + firstInstance
        + QStringLiteral(
              "    leaf #(.WIDTH(%1)) u_other(\n"
              "        .clk(clk),\n"
              "        .enable(enable)\n"
              "    );\n"
              "endmodule\n")
              .arg(secondWidth);
    fixture->topText = QStringLiteral(
        "module top(\n"
        "    input logic clk,\n"
        "    input logic enable\n"
        ");\n"
        "    middle u_middle(\n"
        "        .clk(clk),\n"
        "        .enable(enable)\n"
        "    );\n"
        "endmodule\n");

    for (const auto& file : {
             qMakePair(
                 fixture->leafFile,
                 fixture->leafText),
             qMakePair(
                 fixture->middleFile,
                 fixture->middleText),
             qMakePair(
                 fixture->topFile,
                 fixture->topText)}) {
        if (!writeUtf8(file.first, file.second))
            return {};
        fixture->orderedFiles.append(file.first);
        fixture->workspaceFiles.insert(file.first);
        fixture->semanticContents.insert(
            file.first, file.second);
        fixture->documents.add(
            file.first, file.second);
    }

    SlangManager slang;
    fixture->records =
        slang.extractOverlayWorkspaceSymbolRecords(
            fixture->semanticContents,
            {},
            {},
            nullptr,
            nullptr,
            fixture->orderedFiles);
    assignLocalHandles(&fixture->records);
    fixture->snapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                fixture->records,
                instantiationRelationships(
                    fixture->records),
                {},
                fixture->semanticContents));
    fixture->index.setSnapshot(fixture->snapshot);
    fixture->hierarchy =
        std::make_unique<HierarchyService>(
            &fixture->index);

    const DesignHierarchyReport design =
        fixture->hierarchy
            ->getDesignHierarchyReport(
                QStringLiteral("top"),
                fixture->workspaceFiles);
    for (const DesignHierarchyNode& node :
         design.nodes) {
        if (node.moduleType
                == QStringLiteral("leaf")
            && node.instanceName
                == QStringLiteral("u_leaf")) {
            fixture->sourceInstancePath =
                node.instancePath;
        }
    }
    return fixture;
}

SemanticSymbolRecord signalRecord(
    const Fixture& fixture,
    const QString& name)
{
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record :
         fixture.records) {
        if (record.name == name
            && record.owner.kind
                   == SymbolTaxonomy::
                       SymbolOwnerScope::Module
            && record.owner.name
                   == QStringLiteral("leaf")
            && normalized(
                   record.location.fileName)
                   == fixture.leafFile
            && record.declarationKind
                   == SymbolTaxonomy::
                       DeclarationKind::Signal) {
            matches.append(record);
        }
    }
    return matches.size() == 1
        ? matches.constFirst()
        : SemanticSymbolRecord{};
}

EditorSemanticContext contextFor(
    const Fixture& fixture,
    const SemanticSymbolRecord& signal)
{
    EditorSemanticContext context;
    context.fileName =
        normalized(signal.location.fileName);
    context.moduleName =
        QStringLiteral("leaf");
    context.documentText =
        fixture.documents.docs.value(
            context.fileName).text;
    context.cursorPosition =
        signal.location.position
        + std::max(0, signal.location.length / 2);
    const int lineStart =
        context.documentText.lastIndexOf(
            QLatin1Char('\n'),
            std::max(0,
                     context.cursorPosition - 1))
        + 1;
    int lineEnd =
        context.documentText.indexOf(
            QLatin1Char('\n'),
            context.cursorPosition);
    if (lineEnd < 0)
        lineEnd = context.documentText.size();
    context.lineText =
        context.documentText.mid(
            lineStart, lineEnd - lineStart);
    context.lineUpToCursor =
        context.documentText.mid(
            lineStart,
            context.cursorPosition - lineStart);
    context.cursorLine =
        context.documentText.left(lineStart)
            .count(QLatin1Char('\n'))
        + 1;
    context.column =
        context.cursorPosition - lineStart;
    context.documentRevision =
        fixture.documents.docs.value(
            context.fileName).revision;
    context.hierarchyInstance = {
        fixture.root,
        QStringLiteral("top"),
        fixture.sourceInstancePath};
    return context;
}

MultiSignalPropagationDocumentSnapshot
capturedDocument(
    const QString& fileName,
    const MemoryDocuments::Document& document,
    bool unsaved = false)
{
    auto syntax = std::make_shared<TSDocument>();
    syntax->setText(document.text);
    return {
        fileName,
        document.revision,
        document.text,
        std::move(syntax),
        unsaved};
}

MultiSignalPropagationQuery queryFor(
    const Fixture& fixture,
    const QStringList& signalNames,
    bool dryRun = true,
    MultiSignalPropagationMode mode =
        MultiSignalPropagationMode::PortGroup)
{
    MultiSignalPropagationQuery query;
    query.mode = mode;
    query.groupName = QStringLiteral("debug");
    query.targetAncestorInstancePath =
        QStringLiteral("top.u_middle");
    query.workspaceFiles =
        fixture.workspaceFiles;
    query.semanticToken =
        fixture.index.snapshotToken();
    query.dryRun = dryRun;
    for (auto it =
             fixture.documents.docs.constBegin();
         it != fixture.documents.docs.constEnd();
         ++it) {
        query.documents.insert(
            it.key(),
            capturedDocument(
                it.key(), it.value()));
    }
    for (const QString& name : signalNames) {
        MultiSignalPropagationMemberRequest member;
        member.context = contextFor(
            fixture,
            signalRecord(fixture, name));
        if (mode
                == MultiSignalPropagationMode::
                    IndependentPorts) {
            member.exportedPortName =
                name + QStringLiteral("_trace");
        }
        query.members.append(std::move(member));
    }
    return query;
}

bool planTouchesFile(
    const rtledit::WorkspaceEditPlan& plan,
    const QString& fileName)
{
    const std::string target =
        normalized(fileName)
            .toUtf8().toStdString();
    for (const rtledit::WorkspaceTextEdit& edit :
         plan.edits) {
        if (edit.filePath == target)
            return true;
    }
    return false;
}

bool hasProvenance(
    const rtledit::WorkspaceEditPlan& plan,
    const char* anchor,
    const QString& signal)
{
    for (const rtledit::TextEditProvenance& item :
         plan.provenance) {
        if (item.actionId
                == "signal.propagateBatch"
            && item.anchorName == anchor
            && QString::fromStdString(
                   item.signalQualifiedName)
                   .endsWith(
                       QStringLiteral(".")
                       + signal)
            && item.anchor.source
                   == rtledit::
                       AnchorResolutionSource::
                           TreeSitter) {
            return true;
        }
    }
    return false;
}

bool hasProvenanceInFile(
    const rtledit::WorkspaceEditPlan& plan,
    const char* anchor,
    const QString& signal,
    const QString& fileName)
{
    const std::string expectedFile =
        normalized(fileName)
            .toUtf8().toStdString();
    for (const rtledit::TextEditProvenance& item :
         plan.provenance) {
        if (item.editIndex >= plan.edits.size()
            || plan.edits[item.editIndex].filePath
                   != expectedFile
            || item.anchorName != anchor
            || !QString::fromStdString(
                    item.signalQualifiedName)
                    .endsWith(
                        QStringLiteral(".")
                        + signal)) {
            continue;
        }
        return true;
    }
    return false;
}

QHash<QString, QString> currentContents(
    const Fixture& fixture)
{
    QHash<QString, QString> result;
    for (auto it =
             fixture.documents.docs.constBegin();
         it != fixture.documents.docs.constEnd();
         ++it) {
        result.insert(it.key(), it->text);
    }
    return result;
}

bool noSlangErrors(
    const Fixture& fixture)
{
    SlangManager slang;
    const QList<SemanticDiagnostic> diagnostics =
        slang.extractOverlayWorkspaceDiagnostics(
            currentContents(fixture),
            {},
            {},
            nullptr,
            fixture.orderedFiles);
    for (const SemanticDiagnostic& diagnostic :
         diagnostics) {
        if (diagnostic.severity
                == SemanticDiagnostic::Error) {
            std::fprintf(
                stderr, "Slang error: %s\n",
                diagnostic.message.toUtf8()
                    .constData());
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    expect("temporary fixture root is available",
           temporary.isValid());
    if (!temporary.isValid())
        return 1;

    std::unique_ptr<Fixture> fixture =
        makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("ready")));
    expect("real Slang fixture is available",
           fixture
               && !fixture->records.isEmpty()
               && fixture->sourceInstancePath
                      == QStringLiteral(
                          "top.u_middle.u_leaf")
               && signalRecord(
                      *fixture,
                      QStringLiteral("payload"))
                      .stableKey.isValid()
               && signalRecord(
                      *fixture,
                      QStringLiteral("flags"))
                      .stableKey.isValid());
    if (!fixture)
        return 1;

    MultiSignalPropagationPlanner planner(
        &fixture->index,
        fixture->hierarchy.get());

    // Reverse the user selection. The proposal must use declaration order so
    // previews and generated group members remain stable across invocations.
    MultiSignalPropagationQuery dryRunQuery =
        queryFor(
            *fixture,
            {QStringLiteral("flags"),
             QStringLiteral("payload")},
            true);
    const MultiSignalPropagationProposal dryRun =
        planner.plan(
            dryRunQuery,
            fixture->documents);
    expect("two-signal grouped ancestor proposal is high-risk dry-run",
           dryRun.ready()
               && dryRun.dryRun
               && dryRun.transaction.dryRun
               && dryRun.workspaceEdit.riskLevel
                      == rtledit::RiskLevel::High
               && dryRun.workspaceEdit.previewPolicy
                      == rtledit::PreviewPolicy::Diff
               && dryRun.sourceDiff.built());
    expect("stable group name, member order, and per-layer rule",
           dryRun.portGroup.groupName
                   == QStringLiteral("debug")
               && dryRun.portGroup.namingRule
                      == QStringLiteral(
                          "<group>_<member>")
               && dryRun.portGroup.orderedMembers
                      == QStringList{
                          QStringLiteral("payload"),
                          QStringLiteral("flags")}
               && dryRun.portGroup.orderedPortNames
                      == QStringList{
                          QStringLiteral(
                              "debug_payload"),
                          QStringLiteral(
                              "debug_flags")});
    expect("specified middle ancestor trims all top edits",
           dryRun.sourceInstancePath
                   == QStringLiteral(
                       "top.u_middle.u_leaf")
               && dryRun.targetAncestorInstancePath
                      == QStringLiteral(
                          "top.u_middle")
               && dryRun.members.size() == 2
               && dryRun.members.constFirst()
                      .retainedHierarchyStepCount
                      == 1
               && planTouchesFile(
                      dryRun.workspaceEdit,
                      fixture->leafFile)
               && planTouchesFile(
                      dryRun.workspaceEdit,
                      fixture->middleFile)
               && !planTouchesFile(
                      dryRun.workspaceEdit,
                      fixture->topFile));
    expect("both members retain structural port, bridge, and connection provenance",
           hasProvenance(
               dryRun.workspaceEdit,
               "port.insert",
               QStringLiteral("payload"))
               && hasProvenance(
                   dryRun.workspaceEdit,
                   "source.bridge",
                   QStringLiteral("payload"))
               && hasProvenance(
                   dryRun.workspaceEdit,
                   "connection.insert",
                   QStringLiteral("payload"))
               && hasProvenance(
                   dryRun.workspaceEdit,
                   "port.insert",
                   QStringLiteral("flags"))
               && hasProvenance(
                   dryRun.workspaceEdit,
                   "source.bridge",
                   QStringLiteral("flags"))
               && hasProvenance(
                   dryRun.workspaceEdit,
                   "connection.insert",
                   QStringLiteral("flags")));
    expect("batched same-anchor insertions are comma-separated",
           dryRun.renderedDiff.contains(
               QStringLiteral(
                   "output logic [7:0] "
                   "debug_payload,"))
               && dryRun.renderedDiff.contains(
                   QStringLiteral(
                       "output logic signed [3:0] "
                       "debug_flags"))
               && dryRun.renderedDiff.contains(
                   QStringLiteral(
                       ".debug_payload("
                       "debug_payload),"))
               && dryRun.renderedDiff.contains(
                   QStringLiteral(
                       ".debug_flags("
                       "debug_flags)")));

    const MultiSignalPropagationProposal repeated =
        planner.plan(
            dryRunQuery,
            fixture->documents);
    expect("batch preview is deterministic",
           repeated.ready()
               && repeated.renderedDiff
                      == dryRun.renderedDiff);

    MultiSignalPropagationQuery independentQuery =
        queryFor(
            *fixture,
            {QStringLiteral("payload"),
             QStringLiteral("flags")},
            true,
            MultiSignalPropagationMode::
                IndependentPorts);
    const MultiSignalPropagationProposal independent =
        planner.plan(
            independentQuery,
            fixture->documents);
    expect("independent batch propagation uses exact requested names",
           independent.ready()
               && independent.portGroup.groupName
                      .isEmpty()
               && independent.renderedDiff.contains(
                      QStringLiteral(
                          "payload_trace"))
               && independent.renderedDiff.contains(
                      QStringLiteral(
                          "flags_trace")));

    // A non-dry-run query still stops at an unconfirmed preview. The existing
    // transaction coordinator applies the whole batch atomically and records
    // one history entry for one undo.
    const QString leafBefore =
        fixture->documents.docs.value(
            fixture->leafFile).text;
    const QString middleBefore =
        fixture->documents.docs.value(
            fixture->middleFile).text;
    const QString topBefore =
        fixture->documents.docs.value(
            fixture->topFile).text;
    MultiSignalPropagationQuery applyQuery =
        queryFor(
            *fixture,
            {QStringLiteral("payload"),
             QStringLiteral("flags")},
            false);
    const MultiSignalPropagationProposal applyPlan =
        planner.plan(
            applyQuery,
            fixture->documents);
    expect("non-dry-run planner still requires preview confirmation",
           applyPlan.ready()
               && !applyPlan.dryRun
               && !applyPlan.transaction
                       .previewConfirmed);

    rtledit::WorkspaceEditTransactionCoordinator
        atomicFailure;
    auto failedPrepared =
        atomicFailure.prepare(
            applyPlan.workspaceEdit,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    applyQuery.semanticToken
                        .revision)},
            fixture->documents,
            false);
    fixture->documents.failApplyFile =
        fixture->middleFile;
    expect("atomic failure preview confirms",
           failedPrepared.ready()
               && atomicFailure.confirmPreview(
                   &failedPrepared));
    const auto failedApply =
        atomicFailure.apply(
            failedPrepared,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    applyQuery.semanticToken
                        .revision)},
            fixture->documents);
    expect("second-file failure leaves no partial signal propagation",
           failedApply.status
                   == rtledit::
                       TransactionStatus::
                           ApplyFailed
               && fixture->documents.docs.value(
                      fixture->leafFile).text
                      == leafBefore
               && fixture->documents.docs.value(
                      fixture->middleFile).text
                      == middleBefore
               && fixture->documents.docs.value(
                      fixture->topFile).text
                      == topBefore);
    fixture->documents.failApplyFile.clear();

    rtledit::WorkspaceEditTransactionCoordinator
        transaction;
    auto prepared = transaction.prepare(
        applyPlan.workspaceEdit,
        rtledit::SemanticIndexSnapshot{
            std::to_string(
                applyQuery.semanticToken.revision)},
        fixture->documents,
        false);
    expect("single atomic transaction preview confirms",
           prepared.ready()
               && transaction.confirmPreview(
                   &prepared));
    const auto applied = transaction.apply(
        prepared,
        rtledit::SemanticIndexSnapshot{
            std::to_string(
                applyQuery.semanticToken.revision)},
        fixture->documents);
    expect("both signals apply across both files as one transaction",
           applied.status
                   == rtledit::
                       TransactionStatus::Applied
               && applied.changedFiles.size() == 2
               && fixture->documents.docs.value(
                      fixture->leafFile).text
                      .contains(
                          QStringLiteral(
                              "assign debug_payload "
                              "= payload;"))
               && fixture->documents.docs.value(
                      fixture->leafFile).text
                      .contains(
                          QStringLiteral(
                              "assign debug_flags "
                              "= flags;"))
               && fixture->documents.docs.value(
                      fixture->middleFile).text
                      .contains(
                          QStringLiteral(
                              ".debug_payload("
                              "debug_payload)"))
               && fixture->documents.docs.value(
                      fixture->middleFile).text
                      .contains(
                          QStringLiteral(
                              ".debug_flags("
                              "debug_flags)"))
               && fixture->documents.docs.value(
                      fixture->topFile).text
                      == topBefore
               && transaction.undoDepth() == 1);
    expect("applied grouped propagation remains valid SystemVerilog",
           noSlangErrors(*fixture));
    const auto undone =
        transaction.undo(fixture->documents);
    expect("one undo restores the entire batch",
           undone.status
                   == rtledit::
                       TransactionStatus::Undone
               && fixture->documents.docs.value(
                      fixture->leafFile).text
                      == leafBefore
               && fixture->documents.docs.value(
                      fixture->middleFile).text
                      == middleBefore
               && fixture->documents.docs.value(
                      fixture->topFile).text
                      == topBefore
               && !transaction.canUndo());

    std::unique_ptr<Fixture> reuse =
        makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("reuse")),
            FixtureVariant::
                ReuseCompatiblePort);
    MultiSignalPropagationPlanner reusePlanner(
        &reuse->index,
        reuse->hierarchy.get());
    const MultiSignalPropagationProposal reusePlan =
        reusePlanner.plan(
            queryFor(
                *reuse,
                {QStringLiteral("payload"),
                 QStringLiteral("flags")}),
            reuse->documents);
    expect("compatible Slang-typed existing ancestor port is reused",
           reusePlan.ready()
               && hasProvenanceInFile(
                      reusePlan.workspaceEdit,
                      "port.insert",
                      QStringLiteral("payload"),
                      reuse->leafFile)
               && !hasProvenanceInFile(
                      reusePlan.workspaceEdit,
                      "port.insert",
                      QStringLiteral("payload"),
                      reuse->middleFile)
               && reusePlan.renderedDiff.contains(
                      QStringLiteral(
                          ".debug_payload("
                          "debug_payload)")));

    std::unique_ptr<Fixture> conflict =
        makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("conflict")),
            FixtureVariant::DirectionConflict);
    MultiSignalPropagationPlanner conflictPlanner(
        &conflict->index,
        conflict->hierarchy.get());
    const QString conflictLeafBefore =
        conflict->documents.docs.value(
            conflict->leafFile).text;
    const MultiSignalPropagationProposal conflictPlan =
        conflictPlanner.plan(
            queryFor(
                *conflict,
                {QStringLiteral("payload"),
                 QStringLiteral("flags")}),
            conflict->documents);
    expect("direction conflict rejects the whole batch without a partial plan",
           conflictPlan.status
                   == MultiSignalPropagationStatus::
                       Rejected
               && conflictPlan.failure
                      == MultiSignalPropagationFailure::
                          SingleSignalRejected
               && conflictPlan.workspaceEdit.edits
                      .empty()
               && conflict->documents.docs.value(
                      conflict->leafFile).text
                      == conflictLeafBefore);

    const MultiSignalPropagationProposal unpacked =
        planner.plan(
            queryFor(
                *fixture,
                {QStringLiteral("payload"),
                 QStringLiteral("lanes")}),
            fixture->documents);
    expect("unpacked member rejection is all-or-none while packed facts remain supported",
           unpacked.status
                   == MultiSignalPropagationStatus::
                       Rejected
               && unpacked.failure
                      == MultiSignalPropagationFailure::
                          SingleSignalRejected
               && unpacked.message.contains(
                      QStringLiteral(
                          "Unpacked array"))
               && unpacked.workspaceEdit.edits
                      .empty());

    std::unique_ptr<Fixture> positional =
        makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("positional")),
            FixtureVariant::
                PositionalConnection);
    MultiSignalPropagationPlanner positionalPlanner(
        &positional->index,
        positional->hierarchy.get());
    const MultiSignalPropagationProposal positionalPlan =
        positionalPlanner.plan(
            queryFor(
                *positional,
                {QStringLiteral("payload"),
                 QStringLiteral("flags")}),
            positional->documents);
    expect("ordered instance connection rejects the entire batch",
           positionalPlan.status
                   == MultiSignalPropagationStatus::
                       Rejected
               && positionalPlan.failure
                      == MultiSignalPropagationFailure::
                          SingleSignalRejected
               && positionalPlan.workspaceEdit.edits
                      .empty());

    std::unique_ptr<Fixture> wildcard =
        makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("wildcard")),
            FixtureVariant::
                WildcardConnection);
    MultiSignalPropagationPlanner wildcardPlanner(
        &wildcard->index,
        wildcard->hierarchy.get());
    const MultiSignalPropagationProposal wildcardPlan =
        wildcardPlanner.plan(
            queryFor(
                *wildcard,
                {QStringLiteral("payload"),
                 QStringLiteral("flags")}),
            wildcard->documents);
    expect("wildcard instance connection rejects the entire batch",
           wildcardPlan.status
                   == MultiSignalPropagationStatus::
                       Rejected
               && wildcardPlan.failure
                      == MultiSignalPropagationFailure::
                          SingleSignalRejected
               && wildcardPlan.workspaceEdit.edits
                      .empty());

    std::unique_ptr<Fixture> mixed =
        makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("mixed")),
            FixtureVariant::MixedConnection);
    MultiSignalPropagationPlanner mixedPlanner(
        &mixed->index,
        mixed->hierarchy.get());
    const MultiSignalPropagationProposal mixedPlan =
        mixedPlanner.plan(
            queryFor(
                *mixed,
                {QStringLiteral("payload"),
                 QStringLiteral("flags")}),
            mixed->documents);
    expect("mixed named and ordered connections reject the entire batch",
           mixedPlan.status
                   == MultiSignalPropagationStatus::
                       Rejected
               && (mixedPlan.failure
                       == MultiSignalPropagationFailure::
                           SingleSignalRejected
                   || mixedPlan.failure
                          == MultiSignalPropagationFailure::
                              SyntaxError)
               && mixedPlan.workspaceEdit.edits
                      .empty());

    std::unique_ptr<Fixture> widthDifference =
        makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("width_difference")),
            FixtureVariant::
                MultiInstanceWidthDifference);
    MultiSignalPropagationPlanner widthPlanner(
        &widthDifference->index,
        widthDifference->hierarchy.get());
    const MultiSignalPropagationProposal widthPlan =
        widthPlanner.plan(
            queryFor(
                *widthDifference,
                {QStringLiteral("payload"),
                 QStringLiteral("flags")}),
            widthDifference->documents);
    expect("Slang multi-instance effective-type difference rejects all members",
           widthPlan.status
                   == MultiSignalPropagationStatus::
                       Rejected
               && widthPlan.failure
                      == MultiSignalPropagationFailure::
                          SingleSignalRejected
               && widthPlan.workspaceEdit.edits
                      .empty());

    MultiSignalPropagationQuery staleGeneration =
        queryFor(
            *fixture,
            {QStringLiteral("payload"),
             QStringLiteral("flags")});
    fixture->index.setSnapshot(fixture->snapshot);
    const MultiSignalPropagationProposal staleSemantic =
        planner.plan(
            staleGeneration,
            fixture->documents);
    expect("old semantic generation is rejected before member analysis",
           staleSemantic.failure
                   == MultiSignalPropagationFailure::
                       StaleSemanticGeneration
               && staleSemantic.workspaceEdit.edits
                      .empty());

    // Capture the current generation, then change a live revision without
    // changing text. Exact revision identity still rejects the proposal.
    MultiSignalPropagationQuery staleRevision =
        queryFor(
            *fixture,
            {QStringLiteral("payload"),
             QStringLiteral("flags")});
    ++fixture->documents.docs[
        fixture->middleFile].revision;
    const MultiSignalPropagationProposal staleDocument =
        planner.plan(
            staleRevision,
            fixture->documents);
    expect("old document revision is rejected even when text is unchanged",
           staleDocument.failure
                   == MultiSignalPropagationFailure::
                       StaleDocumentRevision
               && staleDocument.workspaceEdit.edits
                      .empty());
    --fixture->documents.docs[
        fixture->middleFile].revision;

    MultiSignalPropagationQuery sourceMismatch =
        queryFor(
            *fixture,
            {QStringLiteral("payload"),
             QStringLiteral("flags")});
    fixture->documents.docs[
        fixture->leafFile].text.prepend(
            QStringLiteral(
                "// externally replaced saved source\n"));
    sourceMismatch.documents[
        fixture->leafFile] =
        capturedDocument(
            fixture->leafFile,
            fixture->documents.docs.value(
                fixture->leafFile),
            false);
    const MultiSignalPropagationProposal mismatch =
        planner.plan(
            sourceMismatch,
            fixture->documents);
    expect("saved source mismatch against Slang snapshot is rejected",
           mismatch.failure
                   == MultiSignalPropagationFailure::
                       StaleSemanticSource
               && mismatch.workspaceEdit.edits
                      .empty());
    fixture->documents.docs[
        fixture->leafFile].text = leafBefore;

    MultiSignalPropagationQuery syntaxError =
        queryFor(
            *fixture,
            {QStringLiteral("payload"),
             QStringLiteral("flags")});
    fixture->documents.docs[
        fixture->topFile].text =
        QStringLiteral(
            "module top(\n"
            "    input logic clk\n");
    syntaxError.documents[
        fixture->topFile] =
        capturedDocument(
            fixture->topFile,
            fixture->documents.docs.value(
                fixture->topFile),
            true);
    const MultiSignalPropagationProposal brokenSyntax =
        planner.plan(
            syntaxError,
            fixture->documents);
    expect("Tree-sitter syntax error rejects the whole batch",
           brokenSyntax.failure
                   == MultiSignalPropagationFailure::
                       SyntaxError
               && brokenSyntax.workspaceEdit.edits
                      .empty());

    std::printf("%d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
