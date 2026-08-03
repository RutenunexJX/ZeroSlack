#include "rtlrenameplanner.h"

#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tsdocument.h"

#include <rtledit/text_edit.h>
#include <rtledit/workspace_edit_transaction.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using DeclarationKind =
    SymbolTaxonomy::DeclarationKind;

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
        const auto found = docs.constFind(
            normalized(
                QString::fromUtf8(
                    filePath.data(),
                    static_cast<qsizetype>(
                        filePath.size()))));
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
        const QString file = normalized(
            QString::fromUtf8(
                filePath.data(),
                static_cast<qsizetype>(
                    filePath.size())));
        auto found = docs.find(file);
        if (found == docs.end()
            || found->revision
                   != expectedVersion.value
            || (!failApplyFile.isEmpty()
                && normalized(failApplyFile)
                       == file)) {
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
        const QString file = normalized(
            QString::fromUtf8(
                filePath.data(),
                static_cast<qsizetype>(
                    filePath.size())));
        auto found = docs.find(file);
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

struct Fixture {
    QString childFile;
    QString topFile;
    QString childText;
    QString topText;
    QHash<QString, QString> savedContents;
    QList<SemanticSymbolRecord> records;
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    SemanticIndex index;
    MemoryDocuments documents;
};

SemanticSymbolRecord record(
    const QList<SemanticSymbolRecord>& records,
    const QString& name,
    DeclarationKind kind,
    const QString& owner)
{
    SemanticSymbolRecord result;
    int matches = 0;
    for (const SemanticSymbolRecord& candidate :
         records) {
        if (candidate.name != name
            || candidate.declarationKind != kind
            || candidate.owner.name != owner) {
            continue;
        }
        result = candidate;
        ++matches;
    }
    return matches == 1
        ? result : SemanticSymbolRecord{};
}

std::unique_ptr<Fixture> makeFixture(
    const QString& root,
    bool ordered = false)
{
    auto result = std::make_unique<Fixture>();
    result->childFile = normalized(
        QDir(root).filePath(
            QStringLiteral("child.sv")));
    result->topFile = normalized(
        QDir(root).filePath(
            QStringLiteral("top.sv")));
    result->childText = QStringLiteral(
        "module child #(\n"
        "    parameter int WIDTH = 8,\n"
        "    localparam int DOUBLE = WIDTH * 2\n"
        ") (\n"
        "    input  logic [WIDTH-1:0] data_i,\n"
        "    output logic [WIDTH-1:0] data_o\n"
        ");\n"
        "    function automatic int shadow(\n"
        "        input int WIDTH\n"
        "    );\n"
        "        return WIDTH + 1;\n"
        "    endfunction\n"
        "    assign data_o = data_i[WIDTH-1:0];\n"
        "    logic [DOUBLE-1:0] doubled;\n"
        "endmodule\n");
    result->topText = ordered
        ? QStringLiteral(
              "module top;\n"
              "    localparam int WIDTH = 3;\n"
              "    logic [15:0] data_i;\n"
              "    logic [15:0] data_o;\n"
              "    child #(8) u0(data_i, data_o);\n"
              "endmodule\n")
        : QStringLiteral(
              "module top;\n"
              "    localparam int WIDTH = 3;\n"
              "    logic [15:0] data_i;\n"
              "    logic [15:0] data_o;\n"
              "    child #(.WIDTH(8)) u0(\n"
              "        .data_i(data_i),\n"
              "        .data_o(data_o)\n"
              "    );\n"
              "    child #(.WIDTH(16)) u1(\n"
              "        .data_i,\n"
              "        .data_o(data_o)\n"
              "    );\n"
              "endmodule\n");
    result->savedContents = {
        {result->childFile, result->childText},
        {result->topFile, result->topText}};

    SlangManager slang;
    result->records =
        slang.extractOverlayWorkspaceSymbolRecords(
            result->savedContents,
            {},
            {},
            nullptr,
            nullptr,
            {result->childFile, result->topFile});
    result->snapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                result->records,
                {},
                {},
                result->savedContents));
    result->index.setSnapshot(result->snapshot);
    result->documents.add(
        result->childFile, result->childText);
    result->documents.add(
        result->topFile, result->topText);
    return result;
}

RtlRenameDocumentSnapshot capturedDocument(
    const QString& fileName,
    const QString& text,
    std::uint64_t revision,
    bool unsaved = false)
{
    auto syntax = std::make_shared<TSDocument>();
    syntax->setText(text);
    return {
        fileName,
        revision,
        text,
        std::move(syntax),
        unsaved};
}

RtlRenamePlanQuery queryFor(
    const Fixture& fixture,
    const SemanticSymbolRecord& subject,
    const QString& newName,
    bool dryRun = true)
{
    RtlRenamePlanQuery query;
    query.subjectStableKey = subject.stableKey;
    query.newName = newName;
    query.semanticToken =
        fixture.index.snapshotToken();
    for (auto it = fixture.documents.docs.constBegin();
         it != fixture.documents.docs.constEnd(); ++it) {
        query.documents.insert(
            it.key(),
            capturedDocument(
                it.key(),
                it->text,
                it->revision));
        query.workspaceFiles.append(it.key());
    }
    query.dryRun = dryRun;
    return query;
}

int editCount(const RtlRenameProposal& proposal,
              const QString& expected,
              const QString& replacement)
{
    const std::string oldText =
        expected.toUtf8().toStdString();
    const std::string newText =
        replacement.toUtf8().toStdString();
    int result = 0;
    for (const rtledit::WorkspaceTextEdit& edit :
         proposal.workspaceEdit.edits) {
        if (edit.expectedText == oldText
            && edit.newText == newText) {
            ++result;
        }
    }
    return result;
}

bool hasAnchor(const RtlRenameProposal& proposal,
               const std::string& anchor)
{
    for (const rtledit::TextEditProvenance& item :
         proposal.workspaceEdit.provenance) {
        if (item.anchorName == anchor
            && item.anchor.source
                   == rtledit::
                       AnchorResolutionSource::
                           TreeSitter
            && item.anchor.resolver
                   == "ZeroSlack.RtlRenamePlanner/TSDocument"
            && !item.anchor.semanticSnapshotId.empty()) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    expect("temporary workspace is available",
           temporary.isValid());
    if (!temporary.isValid())
        return 1;

    std::unique_ptr<Fixture> fixture =
        makeFixture(temporary.path());
    const SemanticSymbolRecord port =
        record(fixture->records,
               QStringLiteral("data_i"),
               DeclarationKind::Port,
               QStringLiteral("child"));
    const SemanticSymbolRecord parameter =
        record(fixture->records,
               QStringLiteral("WIDTH"),
               DeclarationKind::Parameter,
               QStringLiteral("child"));
    const SemanticSymbolRecord localparam =
        record(fixture->records,
               QStringLiteral("DOUBLE"),
               DeclarationKind::Localparam,
               QStringLiteral("child"));
    expect("real Slang fixture publishes selected declarations",
           port.stableKey.isValid()
               && parameter.stableKey.isValid()
               && localparam.stableKey.isValid());

    RtlRenamePlanner planner(&fixture->index);

    RtlRenamePlanQuery portQuery =
        queryFor(*fixture,
                 port,
                 QStringLiteral("din"));
    const RtlRenameProposal portPlan =
        planner.plan(portQuery,
                     fixture->documents);
    expect("port rename produces a high-risk dry-run proposal",
           portPlan.ready()
               && portPlan.dryRun
               && portPlan.transaction.dryRun
               && portPlan.workspaceEdit.riskLevel
                      == rtledit::RiskLevel::High
               && portPlan.workspaceEdit.previewPolicy
                      == rtledit::PreviewPolicy::Diff
               && portPlan.files.size() == 2);
    expect("port declaration, body reference and both instances are synchronized",
           editCount(portPlan,
                     QStringLiteral("data_i"),
                     QStringLiteral("din")) == 3
               && editCount(
                      portPlan,
                      QStringLiteral("data_i"),
                      QStringLiteral("din(data_i)"))
                      == 1);
    expect("implicit named port keeps its parent actual explicit",
           portPlan.renderedDiff.contains(
               QStringLiteral(".din(data_i)")));
    expect("rename provenance records Tree-sitter anchors and generation",
           hasAnchor(portPlan,
                     "declaration.name")
               && hasAnchor(portPlan,
                            "reference.bound")
               && hasAnchor(portPlan,
                            "instance.port.formal")
               && hasAnchor(portPlan,
                            "instance.port.implicit"));

    const RtlRenameProposal parameterPlan =
        planner.plan(
            queryFor(*fixture,
                     parameter,
                     QStringLiteral("LANES")),
            fixture->documents);
    expect("parameter declaration, bound uses and both overrides are planned",
           parameterPlan.ready()
               && editCount(
                      parameterPlan,
                      QStringLiteral("WIDTH"),
                      QStringLiteral("LANES"))
                      == 7
               && hasAnchor(
                   parameterPlan,
                   "instance.parameter.formal"));
    expect("same-name function argument and sibling module localparam are not renamed",
           !parameterPlan.renderedDiff.contains(
               QStringLiteral(
                   "-         input int WIDTH"))
               && !parameterPlan.renderedDiff.contains(
                   QStringLiteral(
                       "-     localparam int WIDTH = 3")));

    const RtlRenameProposal localparamPlan =
        planner.plan(
            queryFor(*fixture,
                     localparam,
                     QStringLiteral("DOUBLE_WIDTH")),
            fixture->documents);
    expect("module localparam declaration and bound use are renamed",
           localparamPlan.ready()
               && editCount(
                      localparamPlan,
                      QStringLiteral("DOUBLE"),
                      QStringLiteral("DOUBLE_WIDTH"))
                      == 2
               && localparamPlan.files.size() == 1);

    RtlRenamePlanQuery staleGeneration =
        queryFor(*fixture,
                 port,
                 QStringLiteral("din"));
    fixture->index.setSnapshot(fixture->snapshot);
    const RtlRenameProposal staleGenerationPlan =
        planner.plan(staleGeneration,
                     fixture->documents);
    expect("stale semantic generation is rejected before planning",
           staleGenerationPlan.status
               == RtlRenamePlanStatus::
                   StaleSemanticGeneration);

    // Re-capture the current generation, then change only a live document
    // revision. The exact text match does not weaken the revision guard.
    RtlRenamePlanQuery staleRevision =
        queryFor(*fixture,
                 port,
                 QStringLiteral("din"));
    ++fixture->documents.docs[
        fixture->topFile].revision;
    const RtlRenameProposal staleRevisionPlan =
        planner.plan(staleRevision,
                     fixture->documents);
    expect("stale document revision is rejected even when text is unchanged",
           staleRevisionPlan.status
               == RtlRenamePlanStatus::
                   StaleDocumentRevision);
    --fixture->documents.docs[
        fixture->topFile].revision;

    // The saved Slang generation remains authoritative for instance binding;
    // the live unsaved Tree-sitter snapshot supplies the shifted anchors.
    const QString unsavedTop =
        QStringLiteral("// unsaved structural prefix\n")
        + fixture->topText;
    fixture->documents.docs[
        fixture->topFile] = {unsavedTop, 2};
    RtlRenamePlanQuery unsaved =
        queryFor(*fixture,
                 port,
                 QStringLiteral("din"));
    unsaved.documents[
        fixture->topFile] =
        capturedDocument(
            fixture->topFile,
            unsavedTop,
            2,
            true);
    const RtlRenameProposal unsavedPlan =
        planner.plan(unsaved,
                     fixture->documents);
    expect("unsaved Tree-sitter structure supplies current shifted anchors",
           unsavedPlan.ready()
               && unsavedPlan.sourceDiff.built()
               && editCount(
                      unsavedPlan,
                      QStringLiteral("data_i"),
                      QStringLiteral("din(data_i)"))
                      == 1);

    std::unique_ptr<Fixture> ordered =
        makeFixture(
            QDir(temporary.path())
                .filePath(
                    QStringLiteral("ordered")),
            true);
    const SemanticSymbolRecord orderedPort =
        record(ordered->records,
               QStringLiteral("data_i"),
               DeclarationKind::Port,
               QStringLiteral("child"));
    RtlRenamePlanner orderedPlanner(
        &ordered->index);
    const RtlRenameProposal orderedPlan =
        orderedPlanner.plan(
            queryFor(*ordered,
                     orderedPort,
                     QStringLiteral("din")),
            ordered->documents);
    expect("ordered port connections are rejected",
           orderedPlan.status
               == RtlRenamePlanStatus::
                   OrderedConnection);

    // Planner output is compatible with the existing atomic transaction.
    // Injecting a second-file failure must restore the first file.
    fixture->documents.docs[
        fixture->topFile] = {
            fixture->topText, 1};
    const QString childBefore =
        fixture->documents.docs[
            fixture->childFile].text;
    const QString topBefore =
        fixture->documents.docs[
            fixture->topFile].text;
    RtlRenamePlanQuery atomicQuery =
        queryFor(*fixture,
                 port,
                 QStringLiteral("din"),
                 false);
    const RtlRenameProposal atomicPlan =
        planner.plan(atomicQuery,
                     fixture->documents);
    expect("non-dry-run planner still stops at a previewable proposal",
           atomicPlan.ready()
               && !atomicPlan.transaction
                       .previewConfirmed);
    fixture->documents.failApplyFile =
        fixture->topFile;
    rtledit::WorkspaceEditTransactionCoordinator
        transaction;
    auto prepared = transaction.prepare(
        atomicPlan.workspaceEdit,
        rtledit::SemanticIndexSnapshot{
            std::to_string(
                atomicQuery.semanticToken.revision)},
        fixture->documents,
        false);
    expect("atomic transaction preview is built",
           prepared.ready()
               && transaction.confirmPreview(
                   &prepared));
    const rtledit::WorkspaceEditTransactionResult
        failedApply = transaction.apply(
            prepared,
            rtledit::SemanticIndexSnapshot{
                std::to_string(
                    atomicQuery.semanticToken.revision)},
            fixture->documents);
    expect("second-file failure rolls back every document",
           failedApply.status
               == rtledit::TransactionStatus::
                   ApplyFailed
               && fixture->documents.docs[
                      fixture->childFile].text
                      == childBefore
               && fixture->documents.docs[
                      fixture->topFile].text
                      == topBefore);

    std::printf("%d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

