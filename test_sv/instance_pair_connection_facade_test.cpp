#include "hierarchyservice.h"
#include "instancepairconnectionfacade.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tsdocument.h"
#include "workspaceedittransactionservice.h"

#include <rtledit/text_edit.h>

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
    std::printf(
        "[%s] %s\n",
        condition ? "PASS" : "FAIL",
        label);
}

QString normalized(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(path).absoluteFilePath()));
}

bool writeUtf8(
    const QString& fileName,
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

    void add(
        const QString& fileName,
        const QString& text)
    {
        docs.insert(
            normalized(fileName), {text, 1});
    }

    std::optional<rtledit::WorkspaceDocumentSnapshot>
    snapshot(
        const std::string& filePath) const override
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

bool isModule(
    const SemanticSymbolRecord& record)
{
    return record.declarationKind
        == SymbolTaxonomy::DeclarationKind::Module;
}

bool isInstance(
    const SemanticSymbolRecord& record)
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
        modules;
    for (const SemanticSymbolRecord& record :
         records) {
        if (isModule(record))
            modules[record.name].append(record);
    }

    QList<SemanticRelationship> result;
    for (const SemanticSymbolRecord& instance :
         records) {
        if (!isInstance(instance)
            || instance.owner.kind
                != SymbolTaxonomy::
                    SymbolOwnerScope::Module
            || modules.value(
                   instance.owner.name).size()
                   != 1) {
            continue;
        }
        const SemanticSymbolRecord owner =
            modules.value(
                instance.owner.name).constFirst();
        SemanticRelationship relationship;
        relationship.fromId = owner.localHandle;
        relationship.toId = instance.localHandle;
        relationship.type =
            SymbolRelationshipEngine::INSTANTIATES;
        relationship.fromStableKey =
            owner.stableKey;
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
    CompatibleLcaSignal,
    ConflictingLcaSignal,
    RightDirectionConflict,
    RightNameConflict,
    OrderedRightConnection,
    WildcardRightConnection,
    MultiInstanceContext
};

struct Fixture {
    QString root;
    QString sourceFile;
    QString leftFile;
    QString sinkFile;
    QString rightFile;
    QString topFile;
    QStringList files;
    QSet<QString> workspaceFiles;
    QHash<QString, QString> semanticContents;
    QList<SemanticSymbolRecord> records;
    std::shared_ptr<const SemanticIndexSnapshot>
        snapshot;
    SemanticIndex index;
    std::unique_ptr<HierarchyService> hierarchy;
    MemoryDocuments documents;
    QHash<QString, QString> pathsByInstanceName;
};

std::unique_ptr<Fixture> makeFixture(
    const QString& root,
    FixtureVariant variant =
        FixtureVariant::Normal)
{
    auto fixture = std::make_unique<Fixture>();
    fixture->root = normalized(root);
    fixture->sourceFile =
        normalized(QDir(root).filePath(
            QStringLiteral("source_leaf.sv")));
    fixture->leftFile =
        normalized(QDir(root).filePath(
            QStringLiteral("left_mid.sv")));
    fixture->sinkFile =
        normalized(QDir(root).filePath(
            QStringLiteral("sink_leaf.sv")));
    fixture->rightFile =
        normalized(QDir(root).filePath(
            QStringLiteral("right_mid.sv")));
    fixture->topFile =
        normalized(QDir(root).filePath(
            QStringLiteral("top.sv")));

    const QString sourceText = QStringLiteral(
        "module source_leaf(\n"
        "    input logic clk\n"
        ");\n"
        "    logic [7:0] payload;\n"
        "endmodule\n");

    QString sinkExtra;
    if (variant
        == FixtureVariant::RightDirectionConflict) {
        sinkExtra = QStringLiteral(
            ",\n"
            "    output logic [7:0] link");
    }
    QString sinkBody;
    if (variant
        == FixtureVariant::RightNameConflict) {
        sinkBody = QStringLiteral(
            "    logic [7:0] link;\n");
    }
    const QString sinkText =
        QStringLiteral(
            "module sink_leaf(\n"
            "    input logic clk")
        + sinkExtra
        + QStringLiteral(
            "\n"
            ");\n")
        + sinkBody
        + QStringLiteral("endmodule\n");

    QString sourceInstances = QStringLiteral(
        "    source_leaf u_source(\n"
        "        .clk(clk)\n"
        "    );\n");
    if (variant
        == FixtureVariant::MultiInstanceContext) {
        sourceInstances += QStringLiteral(
            "    source_leaf u_source_other(\n"
            "        .clk(clk)\n"
            "    );\n");
    }
    const QString leftText =
        QStringLiteral(
            "module left_mid(\n"
            "    input logic clk\n"
            ");\n"
            "    logic [7:0] mid_payload;\n")
        + sourceInstances
        + QStringLiteral("endmodule\n");

    QString sinkInstance;
    if (variant
        == FixtureVariant::OrderedRightConnection) {
        sinkInstance = QStringLiteral(
            "    sink_leaf u_sink(clk);\n");
    } else if (variant
               == FixtureVariant::
                   WildcardRightConnection) {
        sinkInstance = QStringLiteral(
            "    sink_leaf u_sink(.*);\n");
    } else {
        sinkInstance = QStringLiteral(
            "    sink_leaf u_sink(\n"
            "        .clk(clk)\n"
            "    );\n");
    }
    const QString rightText =
        QStringLiteral(
            "module right_mid(\n"
            "    input logic clk\n"
            ");\n")
        + sinkInstance
        + QStringLiteral("endmodule\n");

    QString topSignal =
        QStringLiteral(
            "    logic [7:0] top_payload;\n");
    if (variant
        == FixtureVariant::CompatibleLcaSignal) {
        topSignal += QStringLiteral(
            "    logic [7:0] link;\n");
    } else if (variant
               == FixtureVariant::
                   ConflictingLcaSignal) {
        topSignal += QStringLiteral(
            "    logic [3:0] link;\n");
    }
    const QString topText =
        QStringLiteral(
            "module top(\n"
            "    input logic clk\n"
            ");\n")
        + topSignal
        + QStringLiteral(
            "    left_mid u_left(\n"
            "        .clk(clk)\n"
            "    );\n"
            "    right_mid u_right(\n"
            "        .clk(clk)\n"
            "    );\n"
            "endmodule\n");

    const QList<QPair<QString, QString>> sources{
        {fixture->sourceFile, sourceText},
        {fixture->leftFile, leftText},
        {fixture->sinkFile, sinkText},
        {fixture->rightFile, rightText},
        {fixture->topFile, topText}};
    for (const auto& source : sources) {
        if (!writeUtf8(
                source.first, source.second)) {
            return {};
        }
        fixture->files.append(source.first);
        fixture->workspaceFiles.insert(
            source.first);
        fixture->semanticContents.insert(
            source.first, source.second);
        fixture->documents.add(
            source.first, source.second);
    }

    SlangManager slang;
    fixture->records =
        slang.extractOverlayWorkspaceSymbolRecords(
            fixture->semanticContents,
            {}, {}, nullptr, nullptr,
            fixture->files);
    assignLocalHandles(&fixture->records);
    fixture->snapshot =
        std::make_shared<
            const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::
                fromSymbolRecords(
                    fixture->records,
                    instantiationRelationships(
                        fixture->records),
                    {},
                    fixture->semanticContents));
    fixture->index.setSnapshot(
        fixture->snapshot);
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
        fixture->pathsByInstanceName.insert(
            node.instanceName,
            node.instancePath);
        if (node.isTop) {
            fixture->pathsByInstanceName.insert(
                QStringLiteral("$top"),
                node.instancePath);
        }
    }
    return fixture;
}

SemanticSymbolRecord moduleSignal(
    const Fixture& fixture,
    const QString& moduleName,
    const QString& signalName)
{
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record :
         fixture.records) {
        if (record.name == signalName
            && record.owner.kind
                == SymbolTaxonomy::
                    SymbolOwnerScope::Module
            && record.owner.name == moduleName
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
    const SemanticSymbolRecord& signal,
    const QString& instancePath)
{
    EditorSemanticContext context;
    context.fileName =
        normalized(signal.location.fileName);
    context.moduleName =
        signal.owner.name;
    context.documentText =
        fixture.documents.docs.value(
            context.fileName).text;
    context.cursorPosition =
        signal.location.position
        + std::max(
            0, signal.location.length / 2);
    const int lineStart =
        context.documentText.lastIndexOf(
            QLatin1Char('\n'),
            std::max(
                0, context.cursorPosition - 1))
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
        instancePath};
    return context;
}

InstancePairDocumentSnapshot capturedDocument(
    const QString& fileName,
    const MemoryDocuments::Document& document)
{
    auto syntax = std::make_shared<TSDocument>();
    syntax->setText(document.text);
    return {
        fileName,
        document.revision,
        document.text,
        std::move(syntax),
        false};
}

InstancePairConnectionQuery queryFor(
    const Fixture& fixture,
    const QString& leftInstanceName =
        QStringLiteral("u_source"),
    const QString& rightInstanceName =
        QStringLiteral("u_sink"),
    const QString& sourceModule =
        QStringLiteral("source_leaf"),
    const QString& sourceSignal =
        QStringLiteral("payload"),
    bool dryRun = true)
{
    InstancePairConnectionQuery query;
    query.leftInstancePath =
        fixture.pathsByInstanceName.value(
            leftInstanceName);
    query.rightInstancePath =
        fixture.pathsByInstanceName.value(
            rightInstanceName);
    query.connectionName =
        QStringLiteral("link");
    query.workspaceFiles =
        fixture.workspaceFiles;
    query.semanticToken =
        fixture.index.snapshotToken();
    query.dryRun = dryRun;
    query.leftSignalContext = contextFor(
        fixture,
        moduleSignal(
            fixture,
            sourceModule,
            sourceSignal),
        query.leftInstancePath);
    for (auto it =
             fixture.documents.docs.constBegin();
         it != fixture.documents.docs.constEnd();
         ++it) {
        query.documents.insert(
            it.key(),
            capturedDocument(
                it.key(), it.value()));
    }
    return query;
}

QHash<QString, MemoryDocuments::Document>
documentState(const Fixture& fixture)
{
    return fixture.documents.docs;
}

bool sameDocumentState(
    const QHash<QString, MemoryDocuments::Document>& left,
    const QHash<QString, MemoryDocuments::Document>& right)
{
    if (left.size() != right.size())
        return false;
    for (auto it = left.constBegin();
         it != left.constEnd(); ++it) {
        const auto found = right.constFind(it.key());
        if (found == right.constEnd()
            || found->revision != it->revision
            || found->text != it->text) {
            return false;
        }
    }
    return true;
}

bool hasAnchor(
    const rtledit::WorkspaceEditPlan& plan,
    const char* anchor)
{
    for (const auto& item : plan.provenance) {
        if (item.actionId
                == "signal.connectInstancePair"
            && item.anchorName == anchor
            && item.anchor.source
                == rtledit::
                    AnchorResolutionSource::
                        TreeSitter) {
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
    expect(
        "temporary fixture root is available",
        temporary.isValid());
    if (!temporary.isValid())
        return 1;

    auto readyFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("ready")));
    expect(
        "real Slang hierarchy fixture is available",
        readyFixture
            && !readyFixture->records.isEmpty()
            && readyFixture->pathsByInstanceName
                   .value(QStringLiteral("u_source"))
                   == QStringLiteral(
                       "top.u_left.u_source")
            && readyFixture->pathsByInstanceName
                   .value(QStringLiteral("u_sink"))
                   == QStringLiteral(
                       "top.u_right.u_sink"));
    if (!readyFixture)
        return 1;

    InstancePairConnectionFacade facade(
        &readyFixture->index,
        readyFixture->hierarchy.get());
    const InstancePairConnectionQuery readyQuery =
        queryFor(*readyFixture);
    const auto readyAnalysis =
        facade.analyze(
            readyQuery,
            readyFixture->documents);
    expect(
        "different branches resolve one LCA and structured side-by-side blocks",
        readyAnalysis.ready()
            && readyAnalysis.blockView
                   .lcaInstancePath
                   == QStringLiteral("top")
            && readyAnalysis.blockView.left
                   .moduleName
                   == QStringLiteral("source_leaf")
            && readyAnalysis.blockView.right
                   .moduleName
                   == QStringLiteral("sink_leaf")
            && readyAnalysis.blockView
                   .leftPathToLca
                   == QStringList{
                       QStringLiteral(
                           "top.u_left.u_source"),
                       QStringLiteral("top.u_left"),
                       QStringLiteral("top")}
            && readyAnalysis.blockView
                   .rightPathFromLca
                   == QStringList{
                       QStringLiteral("top"),
                       QStringLiteral("top.u_right"),
                       QStringLiteral(
                           "top.u_right.u_sink")});
    const auto readyPlan =
        facade.plan(
            readyAnalysis,
            readyFixture->documents);
    expect(
        "plan is High+Diff and contains LCA, left-output, right-input anchors",
        readyPlan.ready()
            && readyPlan.workspaceEdit.riskLevel
                == rtledit::RiskLevel::High
            && readyPlan.workspaceEdit.previewPolicy
                == rtledit::PreviewPolicy::Diff
            && !readyPlan.transaction
                    .previewConfirmed
            && hasAnchor(
                readyPlan.workspaceEdit,
                "lca.signal.insert")
            && hasAnchor(
                readyPlan.workspaceEdit,
                "left.connection.insert")
            && hasAnchor(
                readyPlan.workspaceEdit,
                "right.port.insert")
            && hasAnchor(
                readyPlan.workspaceEdit,
                "right.connection.insert")
            && readyPlan.renderedDiff.contains(
                QStringLiteral(
                    "output logic [7:0] link"))
            && readyPlan.renderedDiff.contains(
                QStringLiteral(
                    "input logic [7:0] link"))
            && readyPlan.renderedDiff.contains(
                QStringLiteral(
                    "logic [7:0] link;")));

    auto sameLayerFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("same_layer")));
    InstancePairConnectionFacade sameLayerFacade(
        &sameLayerFixture->index,
        sameLayerFixture->hierarchy.get());
    const auto sameLayerAnalysis =
        sameLayerFacade.analyze(
            queryFor(
                *sameLayerFixture,
                QStringLiteral("u_left"),
                QStringLiteral("u_right"),
                QStringLiteral("left_mid"),
                QStringLiteral("mid_payload")),
            sameLayerFixture->documents);
    expect(
        "same-level sibling instances form one-step left and right branches",
        sameLayerAnalysis.ready()
            && sameLayerAnalysis.blockView
                   .lcaInstancePath
                   == QStringLiteral("top")
            && sameLayerAnalysis.steps.size() == 2);

    auto leftAncestorFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("left_ancestor")));
    InstancePairConnectionFacade leftAncestorFacade(
        &leftAncestorFixture->index,
        leftAncestorFixture->hierarchy.get());
    const auto leftAncestorAnalysis =
        leftAncestorFacade.analyze(
            queryFor(
                *leftAncestorFixture,
                QStringLiteral("$top"),
                QStringLiteral("u_sink"),
                QStringLiteral("top"),
                QStringLiteral("top_payload")),
            leftAncestorFixture->documents);
    expect(
        "left-ancestor relation does not invent a left branch",
        leftAncestorAnalysis.ready()
            && leftAncestorAnalysis.blockView
                   .lcaInstancePath
                   == QStringLiteral("top")
            && leftAncestorAnalysis.blockView
                   .leftPathToLca
                   == QStringList{
                       QStringLiteral("top")}
            && std::none_of(
                leftAncestorAnalysis.steps.begin(),
                leftAncestorAnalysis.steps.end(),
                [](const auto& step) {
                    return step.side
                        == InstancePairSide::Left;
                }));

    auto rightAncestorFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("right_ancestor")));
    InstancePairConnectionFacade rightAncestorFacade(
        &rightAncestorFixture->index,
        rightAncestorFixture->hierarchy.get());
    const auto rightAncestorAnalysis =
        rightAncestorFacade.analyze(
            queryFor(
                *rightAncestorFixture,
                QStringLiteral("u_source"),
                QStringLiteral("$top")),
            rightAncestorFixture->documents);
    expect(
        "right-ancestor relation does not invent a right branch",
        rightAncestorAnalysis.ready()
            && rightAncestorAnalysis.blockView
                   .rightPathFromLca
                   == QStringList{
                       QStringLiteral("top")}
            && std::none_of(
                rightAncestorAnalysis.steps.begin(),
                rightAncestorAnalysis.steps.end(),
                [](const auto& step) {
                    return step.side
                        == InstancePairSide::Right;
                }));

    auto compatibleFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("compatible_lca")),
        FixtureVariant::CompatibleLcaSignal);
    InstancePairConnectionFacade compatibleFacade(
        &compatibleFixture->index,
        compatibleFixture->hierarchy.get());
    const auto compatibleAnalysis =
        compatibleFacade.analyze(
            queryFor(*compatibleFixture),
            compatibleFixture->documents);
    const auto compatiblePlan =
        compatibleFacade.plan(
            compatibleAnalysis,
            compatibleFixture->documents);
    expect(
        "compatible undriven LCA signal is reused",
        compatibleAnalysis.ready()
            && compatibleAnalysis.localSignal.state
                == rtledit::
                    ExposeEndpointState::Reuse
            && compatiblePlan.ready()
            && !hasAnchor(
                compatiblePlan.workspaceEdit,
                "lca.signal.insert"));

    auto lcaConflictFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("lca_conflict")),
        FixtureVariant::ConflictingLcaSignal);
    InstancePairConnectionFacade lcaConflictFacade(
        &lcaConflictFixture->index,
        lcaConflictFixture->hierarchy.get());
    const auto lcaConflictBefore =
        documentState(*lcaConflictFixture);
    const auto lcaConflict =
        lcaConflictFacade.analyze(
            queryFor(*lcaConflictFixture),
            lcaConflictFixture->documents);
    expect(
        "incompatible same-name LCA signal rejects without edits",
        !lcaConflict.ready()
            && lcaConflict.failure
                == InstancePairConnectionFailure::
                    TypeMismatch
            && sameDocumentState(
                lcaConflictBefore,
                lcaConflictFixture->documents.docs)
            && lcaConflictFacade
                   .plan(
                       lcaConflict,
                       lcaConflictFixture->documents)
                   .workspaceEdit.edits.empty());

    for (const auto& variant :
         QList<QPair<
             FixtureVariant,
             InstancePairConnectionFailure>>{
             {FixtureVariant::
                  RightDirectionConflict,
              InstancePairConnectionFailure::
                  DirectionConflict},
             {FixtureVariant::RightNameConflict,
              InstancePairConnectionFailure::
                  NameConflict},
             {FixtureVariant::
                  OrderedRightConnection,
              InstancePairConnectionFailure::
                  OrderedConnection},
             {FixtureVariant::
                  WildcardRightConnection,
              InstancePairConnectionFailure::
                  WildcardConnection},
             {FixtureVariant::
                  MultiInstanceContext,
              InstancePairConnectionFailure::
                  MultiInstanceContext}}) {
        auto fixture = makeFixture(
            QDir(temporary.path()).filePath(
                QStringLiteral("reject_%1")
                    .arg(
                        static_cast<int>(
                            variant.first))),
            variant.first);
        InstancePairConnectionFacade rejectingFacade(
            &fixture->index,
            fixture->hierarchy.get());
        const auto before = documentState(*fixture);
        const auto analysis =
            rejectingFacade.analyze(
                queryFor(*fixture),
                fixture->documents);
        expect(
            "direction, name, ordered, wildcard, and multi-instance conflicts reject atomically",
            !analysis.ready()
                && analysis.failure
                    == variant.second
                && sameDocumentState(
                    before,
                    fixture->documents.docs)
                && rejectingFacade
                       .plan(
                           analysis,
                           fixture->documents)
                       .workspaceEdit.edits.empty());
    }

    auto staleGenerationFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("stale_generation")));
    InstancePairConnectionQuery staleGeneration =
        queryFor(*staleGenerationFixture);
    staleGenerationFixture->index.setSnapshot(
        staleGenerationFixture->snapshot);
    InstancePairConnectionFacade staleGenerationFacade(
        &staleGenerationFixture->index,
        staleGenerationFixture->hierarchy.get());
    expect(
        "stale semantic generation is rejected",
        staleGenerationFacade
                .analyze(
                    staleGeneration,
                    staleGenerationFixture->documents)
                .failure
            == InstancePairConnectionFailure::
                StaleSemanticGeneration);

    auto staleRevisionFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("stale_revision")));
    InstancePairConnectionQuery staleRevision =
        queryFor(*staleRevisionFixture);
    ++staleRevisionFixture->documents
          .docs[staleRevisionFixture->rightFile]
          .revision;
    InstancePairConnectionFacade staleRevisionFacade(
        &staleRevisionFixture->index,
        staleRevisionFixture->hierarchy.get());
    expect(
        "stale document revision is rejected",
        staleRevisionFacade
                .analyze(
                    staleRevision,
                    staleRevisionFixture->documents)
                .failure
            == InstancePairConnectionFailure::
                StaleDocumentRevision);

    auto staleSourceFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("stale_source")));
    InstancePairConnectionQuery staleSource =
        queryFor(*staleSourceFixture);
    auto& staleSourceDocument =
        staleSourceFixture->documents
            .docs[staleSourceFixture->rightFile];
    staleSourceDocument.text.prepend(
        QStringLiteral("// external source replacement\n"));
    staleSource.documents[
        staleSourceFixture->rightFile] =
        capturedDocument(
            staleSourceFixture->rightFile,
            staleSourceDocument);
    InstancePairConnectionFacade staleSourceFacade(
        &staleSourceFixture->index,
        staleSourceFixture->hierarchy.get());
    expect(
        "saved-source mismatch against Slang is rejected",
        staleSourceFacade
                .analyze(
                    staleSource,
                    staleSourceFixture->documents)
                .failure
            == InstancePairConnectionFailure::
                StaleSemanticSource);

    WorkspaceEditTransactionService::getInstance()
        ->clearHistory();
    const auto dryRunBefore =
        documentState(*readyFixture);
    const auto dryRunResult =
        WorkspaceEditTransactionService::getInstance()
            ->applyConfirmed(
                readyPlan.transaction,
                rtledit::SemanticIndexSnapshot{
                    std::to_string(
                        readyQuery.semanticToken
                            .revision)},
                readyFixture->documents);
    expect(
        "dry-run transaction never mutates source",
        dryRunResult.status
                == rtledit::
                    TransactionStatus::DryRunOnly
            && sameDocumentState(
                dryRunBefore,
                readyFixture->documents.docs)
            && !WorkspaceEditTransactionService::
                    getInstance()->canUndo());

    auto applyFixture = makeFixture(
        QDir(temporary.path()).filePath(
            QStringLiteral("apply")));
    InstancePairConnectionFacade applyFacade(
        &applyFixture->index,
        applyFixture->hierarchy.get());
    const InstancePairConnectionQuery applyQuery =
        queryFor(
            *applyFixture,
            QStringLiteral("u_source"),
            QStringLiteral("u_sink"),
            QStringLiteral("source_leaf"),
            QStringLiteral("payload"),
            false);
    const auto applyAnalysis =
        applyFacade.analyze(
            applyQuery,
            applyFixture->documents);
    const auto applyPlan =
        applyFacade.plan(
            applyAnalysis,
            applyFixture->documents);
    const auto applyBefore =
        documentState(*applyFixture);
    WorkspaceEditTransactionService::getInstance()
        ->clearHistory();
    const auto applied =
        WorkspaceEditTransactionService::getInstance()
            ->applyConfirmed(
                applyPlan.transaction,
                rtledit::SemanticIndexSnapshot{
                    std::to_string(
                        applyQuery.semanticToken
                            .revision)},
                applyFixture->documents);
    const auto undone =
        WorkspaceEditTransactionService::getInstance()
            ->undo(applyFixture->documents);
    expect(
        "all files apply atomically and one undo restores the pair connection",
        applyPlan.ready()
            && applied.status
                == rtledit::
                    TransactionStatus::Applied
            && applied.changedFiles.size() >= 4
            && undone.status
                == rtledit::
                    TransactionStatus::Undone
            && sameDocumentState(
                applyBefore,
                applyFixture->documents.docs)
            && !WorkspaceEditTransactionService::
                    getInstance()->canUndo());

    std::printf(
        "%d checks, %d failures\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
