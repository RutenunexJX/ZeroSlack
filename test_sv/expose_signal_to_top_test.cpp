#include "exposesignaltotopservice.h"
#include "hierarchyservice.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"

#include <rtledit/text_edit.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>
#include <optional>

namespace {
int checks = 0;
int failures = 0;

void check(const char* name, bool condition)
{
    ++checks;
    failures += condition ? 0 : 1;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
}

QString norm(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

class MemoryDocuments final : public rtledit::WorkspaceDocumentManager
{
public:
    struct Doc { QString text; std::uint64_t version = 1; };
    QHash<QString, Doc> docs;

    void add(const QString& path, const QString& text)
    {
        docs.insert(norm(path), {text, 1});
    }

    std::optional<rtledit::WorkspaceDocumentSnapshot> snapshot(
        const std::string& path) const override
    {
        const auto it = docs.constFind(norm(QString::fromStdString(path)));
        if (it == docs.constEnd())
            return std::nullopt;
        return rtledit::WorkspaceDocumentSnapshot{
            {it->version}, it->text.toUtf8().toStdString()};
    }

    bool applyTextEdits(
        const std::string& path,
        rtledit::DocumentVersion version,
        const std::vector<rtledit::WorkspaceTextEdit>& edits) override
    {
        auto it = docs.find(norm(QString::fromStdString(path)));
        if (it == docs.end() || it->version != version.value)
            return false;
        const auto result = rtledit::applyTextEditsToString(
            it->text.toUtf8().toStdString(), edits);
        if (!result)
            return false;
        it->text = QString::fromUtf8(result->data(),
                                     static_cast<qsizetype>(result->size()));
        ++it->version;
        return true;
    }

    bool restoreSnapshot(
        const std::string& path,
        const rtledit::WorkspaceDocumentSnapshot& snapshot) override
    {
        auto it = docs.find(norm(QString::fromStdString(path)));
        if (it == docs.end())
            return false;
        it->text = QString::fromUtf8(
            snapshot.text.data(),
            static_cast<qsizetype>(snapshot.text.size()));
        it->version = snapshot.version.value;
        return true;
    }
};

SemanticElaboratedSymbolInfo typeInfo(int width)
{
    SemanticElaboratedSymbolInfo result;
    result.available = true;
    result.fixedSize = true;
    result.integral = true;
    result.bitWidthText = QString::number(width);
    result.bitWidth = static_cast<std::uint64_t>(width);
    result.signedIntegral = false;
    result.resolvedTypeText = QStringLiteral("logic");
    result.signednessText = QStringLiteral("unsigned");
    return result;
}

struct Fixture {
    QString leafFile, midFile, topFile;
    QString leafText, midText, topText;
    SemanticSymbolRecord signal;
    SemanticIndex index;
    std::unique_ptr<HierarchyService> hierarchy;
    MemoryDocuments documents;
    EditorSemanticContext context;
    QSet<QString> files;
    bool nativeMiddleOutput = false;
    SemanticElaboratedSymbolInfo nativeMiddleOutputInfo;
};

enum class DriverScenario {
    None,
    SourceExact,
    SourceExactPlusConstant,
    SourceUndriven,
    SourceOther,
    MiddleOther,
    MiddleUndriven,
    MiddleUnknown,
    MiddleConstantContinuous,
    MiddleConstantProcedural,
    MiddleChildOutput
};

std::unique_ptr<Fixture> fixture(const QString& root,
                                 bool positional = false,
                                 bool widthConflict = false,
                                 DriverScenario driverScenario =
                                     DriverScenario::None)
{
    QDir().mkpath(root);
    auto f = std::make_unique<Fixture>();
    f->leafFile = norm(root + QStringLiteral("/leaf.sv"));
    f->midFile = norm(root + QStringLiteral("/mid.sv"));
    f->topFile = norm(root + QStringLiteral("/top.sv"));
    f->leafText = QStringLiteral(
        "module leaf(\n  input logic clk\n);\n"
        "  // 中文 source anchor\n  logic [7:0] payload;\nendmodule\n");
    f->midText = QStringLiteral(
        "module mid(\n  input logic clk\n);\n"
        "  leaf u_leaf(\n    .clk(clk)\n  );\n"
        "  leaf u_other(\n    .clk(clk)\n  );\nendmodule\n");
    const bool existingSourceOutput =
        driverScenario == DriverScenario::SourceExact
        || driverScenario == DriverScenario::SourceExactPlusConstant
        || driverScenario == DriverScenario::SourceUndriven
        || driverScenario == DriverScenario::SourceOther;
    if (existingSourceOutput) {
        f->leafText = QStringLiteral(
            "module leaf(\n  input logic clk,\n"
            "  output logic [7:0] payload_out\n);\n"
            "  logic [7:0] payload;\n");
        if (driverScenario == DriverScenario::SourceExact
            || driverScenario
                == DriverScenario::SourceExactPlusConstant) {
            f->leafText += QStringLiteral(
                "  assign payload_out = payload;\n");
            if (driverScenario
                == DriverScenario::SourceExactPlusConstant) {
                f->leafText += QStringLiteral(
                    "  assign payload_out = 8'h00;\n");
            }
        } else if (driverScenario == DriverScenario::SourceOther) {
            f->leafText += QStringLiteral(
                "  logic [7:0] unrelated;\n"
                "  assign payload_out = unrelated;\n");
        }
        f->leafText += QStringLiteral("endmodule\n");
    }
    if (driverScenario == DriverScenario::MiddleOther) {
        f->midText = QStringLiteral(
            "module mid(\n  input logic clk,\n"
            "  output logic [7:0] payload_out\n);\n"
            "  leaf u_leaf(\n    .clk(clk)\n  );\n"
            "  leaf u_other(\n    .clk(clk)\n  );\n"
            "  logic [7:0] unrelated;\n"
            "  assign payload_out = unrelated;\n"
            "endmodule\n");
    }
    if (driverScenario == DriverScenario::MiddleUndriven
        || driverScenario == DriverScenario::MiddleUnknown) {
        f->midText = QStringLiteral(
            "module mid(\n  input logic clk,\n"
            "  output logic [7:0] payload_out\n);\n"
            "  leaf u_leaf(\n    .clk(clk)\n  );\n"
            "  leaf u_other(\n    .clk(clk)\n  );\n"
            "endmodule\n");
    }
    if (driverScenario == DriverScenario::MiddleConstantContinuous) {
        f->midText = QStringLiteral(
            "module mid(\n  input logic clk,\n"
            "  output logic [7:0] payload_out\n);\n"
            "  leaf u_leaf(\n    .clk(clk)\n  );\n"
            "  leaf u_other(\n    .clk(clk)\n  );\n"
            "  assign payload_out = 8'h00;\n"
            "endmodule\n");
    }
    if (driverScenario == DriverScenario::MiddleConstantProcedural) {
        f->midText = QStringLiteral(
            "module mid(\n  input logic clk,\n"
            "  output logic [7:0] payload_out\n);\n"
            "  leaf u_leaf(\n    .clk(clk)\n  );\n"
            "  leaf u_other(\n    .clk(clk)\n  );\n"
            "  always_comb begin\n"
            "    payload_out = '0;\n"
            "  end\n"
            "endmodule\n");
    }
    if (driverScenario == DriverScenario::MiddleChildOutput) {
        f->midText = QStringLiteral(
            "module driver_child(\n"
            "  output logic [7:0] child_out\n"
            ");\n"
            "  assign child_out = 8'h5a;\n"
            "endmodule\n"
            "module mid(\n  input logic clk,\n"
            "  output logic [7:0] payload_out\n);\n"
            "  leaf u_leaf(\n    .clk(clk)\n  );\n"
            "  leaf u_other(\n    .clk(clk)\n  );\n"
            "  driver_child u_driver(\n"
            "    .child_out(payload_out)\n"
            "  );\n"
            "endmodule\n");
    }
    f->topText = positional
        ? QStringLiteral(
              "module top(\n  input logic clk\n);\n  mid u_mid(clk);\nendmodule\n")
        : QStringLiteral(
              "module top(\n  input logic clk\n);\n"
              "  mid u_mid(\n    .clk(clk)\n  );\nendmodule\n");
    for (const auto& pair : {
             qMakePair(f->leafFile, f->leafText),
             qMakePair(f->midFile, f->midText),
             qMakePair(f->topFile, f->topText)}) {
        QFile file(pair.first);
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        file.write(pair.second.toUtf8());
        f->documents.add(pair.first, pair.second);
        f->files.insert(pair.first);
    }

    auto module = [](int id, const QString& file, const QString& text,
                     const QString& name) {
        return SemanticFixtureRecordBuilder(
                   name, SymbolTaxonomy::DeclarationKind::Module)
            .withFile(file).withLocalHandle(id).withLine(1)
            .withTextSpan(text.indexOf(name), name.size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    };
    const SemanticSymbolRecord leaf =
        module(1, f->leafFile, f->leafText, QStringLiteral("leaf"));
    const SemanticSymbolRecord mid =
        module(2, f->midFile, f->midText, QStringLiteral("mid"));
    const SemanticSymbolRecord top =
        module(3, f->topFile, f->topText, QStringLiteral("top"));

    auto instance = [](int id, const QString& file, const QString& text,
                       const QString& name, const QString& owner,
                       const SymbolStableKey& ownerKey,
                       const QString& child) {
        return SemanticFixtureRecordBuilder(
                   name, SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(file).withLocalHandle(id).withLine(4)
            .withTextSpan(text.indexOf(name), name.size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Inst)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       owner, ownerKey)
            .withType(child, child,
                      SymbolTaxonomy::DeclarationKind::Module)
            .record();
    };
    const SemanticSymbolRecord uMid =
        instance(4, f->topFile, f->topText, QStringLiteral("u_mid"),
                 QStringLiteral("top"), top.stableKey,
                 QStringLiteral("mid"));
    const SemanticSymbolRecord uLeaf =
        instance(5, f->midFile, f->midText, QStringLiteral("u_leaf"),
                 QStringLiteral("mid"), mid.stableKey,
                 QStringLiteral("leaf"));
    const SemanticSymbolRecord uOther =
        instance(6, f->midFile, f->midText, QStringLiteral("u_other"),
                 QStringLiteral("mid"), mid.stableKey,
                 QStringLiteral("leaf"));

    const int payloadPosition =
        f->leafText.indexOf(QStringLiteral("logic [7:0] payload;"))
        + QStringLiteral("logic [7:0] ").size();
    const int payloadLine =
        f->leafText.left(payloadPosition).count(QLatin1Char('\n')) + 1;
    f->signal = SemanticFixtureRecordBuilder(
                    QStringLiteral("payload"),
                    SymbolTaxonomy::DeclarationKind::Signal)
        .withFile(f->leafFile).withLocalHandle(7)
        .withLine(payloadLine, 15)
        .withTextSpan(payloadPosition, 7)
        .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
        .inModule(QStringLiteral("leaf"))
        .withType(QStringLiteral("logic [7:0]"))
        .record();
    f->signal.presentation.instanceInfoByPath.insert(
        QStringLiteral("top.u_mid.u_leaf"), typeInfo(8));
    f->signal.presentation.instanceInfoByPath.insert(
        QStringLiteral("top.u_mid.u_other"),
        typeInfo(widthConflict ? 16 : 8));
    SemanticSymbolRecord sameName =
        SemanticFixtureRecordBuilder(
            QStringLiteral("payload"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(f->topFile).withLocalHandle(8).withLine(1)
            .withTextSpan(0, 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(QStringLiteral("unrelated")).record();

    QList<SemanticSymbolRecord> records{
        leaf, mid, top, uMid, uLeaf, uOther, f->signal, sameName};
    QList<SemanticRelationship> relationships{
        semanticFixtureRelationship(
            top, uMid, SymbolRelationshipEngine::INSTANTIATES,
            RelationshipProvenance::SlangExtracted),
        semanticFixtureRelationship(
            mid, uLeaf, SymbolRelationshipEngine::INSTANTIATES,
            RelationshipProvenance::SlangExtracted),
        semanticFixtureRelationship(
            mid, uOther, SymbolRelationshipEngine::INSTANTIATES,
            RelationshipProvenance::SlangExtracted)};

    auto outputPort = [](int id,
                         const QString& file,
                         const QString& text,
                         const QString& moduleName,
                         const QStringList& instancePaths) {
        SemanticSymbolRecord record =
            SemanticFixtureRecordBuilder(
                QStringLiteral("payload_out"),
                SymbolTaxonomy::DeclarationKind::Port)
                .withFile(file).withLocalHandle(id)
                .withLine(2, 22)
                .withTextSpan(
                    text.indexOf(QStringLiteral("payload_out")),
                    QStringLiteral("payload_out").size())
                .withCollectorKind(
                    SymbolTaxonomy::CollectorKind::PortOutput)
                .inModule(moduleName)
                .withType(QStringLiteral("logic [7:0]"))
                .record();
        for (const QString& path : instancePaths)
            record.presentation.instanceInfoByPath.insert(
                path, typeInfo(8));
        return record;
    };
    auto internalSignal = [](int id,
                             const QString& file,
                             const QString& text,
                             const QString& moduleName) {
        return SemanticFixtureRecordBuilder(
                   QStringLiteral("unrelated"),
                   SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(file).withLocalHandle(id)
            .withLine(7, 15)
            .withTextSpan(
                text.indexOf(QStringLiteral("unrelated")),
                QStringLiteral("unrelated").size())
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .inModule(moduleName)
            .withType(QStringLiteral("logic [7:0]"))
            .record();
    };
    auto assignment = [](const SemanticSymbolRecord& from,
                         const SemanticSymbolRecord& to) {
        SemanticRelationship relationship =
            semanticFixtureRelationship(
                from, to, SymbolRelationshipEngine::ASSIGNS_TO,
                RelationshipProvenance::SlangExtracted);
        relationship.fromAccessPath = from.name;
        relationship.toAccessPath = to.name;
        relationship.exactValueForward = true;
        return relationship;
    };

    if (existingSourceOutput) {
        SemanticSymbolRecord port = outputPort(
            9, f->leafFile, f->leafText, QStringLiteral("leaf"),
            {QStringLiteral("top.u_mid.u_leaf"),
             QStringLiteral("top.u_mid.u_other")});
        for (auto it = port.presentation.instanceInfoByPath.begin();
             it != port.presentation.instanceInfoByPath.end(); ++it) {
            if (driverScenario == DriverScenario::SourceUndriven) {
                it->driverPresence =
                    SemanticDriverPresenceState::ProvenZero;
            } else {
                it->driverPresence =
                    SemanticDriverPresenceState::Present;
                const std::uint64_t count =
                    driverScenario
                            == DriverScenario::SourceExactPlusConstant
                        ? 2 : 1;
                it->driverCount = count;
                it->continuousDriverCount = count;
            }
        }
        records.append(port);
        if (driverScenario == DriverScenario::SourceExact
            || driverScenario
                == DriverScenario::SourceExactPlusConstant) {
            relationships.append(assignment(f->signal, port));
        }
        if (driverScenario == DriverScenario::SourceOther) {
            const SemanticSymbolRecord unrelated = internalSignal(
                10, f->leafFile, f->leafText, QStringLiteral("leaf"));
            records.append(unrelated);
            relationships.append(assignment(unrelated, port));
        }
    }
    const bool nativeMiddleDriver =
        driverScenario == DriverScenario::MiddleConstantContinuous
        || driverScenario == DriverScenario::MiddleConstantProcedural
        || driverScenario == DriverScenario::MiddleChildOutput
        || driverScenario == DriverScenario::MiddleUndriven;
    if (nativeMiddleDriver) {
        SlangManager slang;
        QList<SemanticSymbolRecord> nativeRecords =
            slang.extractOverlayWorkspaceSymbolRecords(
                {{f->leafFile, f->leafText},
                 {f->midFile, f->midText},
                 {f->topFile, f->topText}},
                {}, {}, {}, nullptr,
                {f->leafFile, f->midFile, f->topFile});
        for (SemanticSymbolRecord& record : nativeRecords) {
            if (record.name != QStringLiteral("payload_out")
                || record.collectorKind
                    != SymbolTaxonomy::CollectorKind::PortOutput
                || record.owner.kind
                    != SymbolTaxonomy::SymbolOwnerScope::Module
                || record.owner.name != QStringLiteral("mid")) {
                continue;
            }
            record.localHandle = 9;
            const auto info =
                record.presentation.instanceInfoByPath.constFind(
                    QStringLiteral("top.u_mid"));
            if (info
                != record.presentation.instanceInfoByPath.constEnd()) {
                f->nativeMiddleOutputInfo = info.value();
            }
            records.append(record);
            f->nativeMiddleOutput = true;
            break;
        }
    } else if (driverScenario == DriverScenario::MiddleOther
               || driverScenario == DriverScenario::MiddleUnknown) {
        SemanticSymbolRecord port = outputPort(
            9, f->midFile, f->midText, QStringLiteral("mid"),
            {QStringLiteral("top.u_mid")});
        records.append(port);
        if (driverScenario == DriverScenario::MiddleOther) {
            auto middleInfo =
                records.last().presentation.instanceInfoByPath.find(
                    QStringLiteral("top.u_mid"));
            middleInfo->driverPresence =
                SemanticDriverPresenceState::Present;
            middleInfo->driverCount = 1;
            middleInfo->continuousDriverCount = 1;
            const SemanticSymbolRecord unrelated = internalSignal(
                10, f->midFile, f->midText, QStringLiteral("mid"));
            records.append(unrelated);
            relationships.append(
                assignment(unrelated, records.at(records.size() - 2)));
        }
    }
    f->index.setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records, relationships, {},
            {{f->leafFile, f->leafText},
             {f->midFile, f->midText},
             {f->topFile, f->topText}})));
    f->hierarchy = std::make_unique<HierarchyService>(&f->index);

    f->context.fileName = f->leafFile;
    f->context.moduleName = QStringLiteral("leaf");
    f->context.documentText = f->leafText;
    f->context.cursorPosition =
        payloadPosition + 2;
    f->context.cursorLine = payloadLine;
    f->context.column = 17;
    f->context.lineText = QStringLiteral("  logic [7:0] payload;");
    f->context.lineUpToCursor = QStringLiteral("  logic [7:0] pa");
    f->context.documentRevision = 1;
    f->context.hierarchyInstance = {
        root, QStringLiteral("top"),
        QStringLiteral("top.u_mid.u_leaf")};
    return f;
}
} // namespace

int main()
{
    QTemporaryDir temp;
    check("temporary fixture", temp.isValid());
    auto f = fixture(temp.path() + QStringLiteral("/ready"));
    ExposeSignalToTopService service(&f->index, f->hierarchy.get());
    ExposeSignalToTopQuery query{
        f->context, QStringLiteral("payload_out"), f->files};
    const ExposeSignalToTopReport report =
        service.plan(query, f->documents);
    check("three-level plan ready", report.ready());
    check("specific instance and scope isolation",
          report.sourceInstancePath == QStringLiteral("top.u_mid.u_leaf")
              && report.signalRecord.stableKey == f->signal.stableKey);
    check("two hierarchy steps", report.hierarchySteps.size() == 2);
    check("multi-instance impact",
          report.affectedInstancePaths.contains(
              QStringLiteral("top.u_mid.u_other")));
    check("three-file diff",
          report.sourceDiff.built()
              && report.sourceDiff.files.size() == 3);
    const ExposeSignalToTopApplyReport apply =
        service.apply(report, f->documents);
    check("atomic core apply", apply.applied());
    check("source bridge",
          f->documents.docs.value(f->leafFile).text.contains(
              QStringLiteral("assign payload_out = payload;")));
    check("mid connection",
          f->documents.docs.value(f->midFile).text.contains(
              QStringLiteral(".payload_out(payload_out)")));
    check("top output",
          f->documents.docs.value(f->topFile).text.contains(
              QStringLiteral("output logic [7:0] payload_out")));

    auto p = fixture(temp.path() + QStringLiteral("/positional"), true);
    ExposeSignalToTopService ps(&p->index, p->hierarchy.get());
    query = {p->context, QStringLiteral("payload_out"), p->files};
    check("positional instance blocked",
          ps.plan(query, p->documents).failureReason
              == rtledit::ExposeSignalFailureReason::PositionalInstantiation);

    auto w = fixture(temp.path() + QStringLiteral("/width"), false, true);
    ExposeSignalToTopService ws(&w->index, w->hierarchy.get());
    query = {w->context, QStringLiteral("payload_out"), w->files};
    const auto width = ws.plan(query, w->documents);
    check("width conflict blocked with instance",
          width.failureReason
                  == rtledit::ExposeSignalFailureReason::ParameterWidthConflict
              && width.message.contains(QStringLiteral("top.u_mid.u_other")));

    auto exact = fixture(
        temp.path() + QStringLiteral("/source_exact"),
        false, false, DriverScenario::SourceExact);
    ExposeSignalToTopService exactService(
        &exact->index, exact->hierarchy.get());
    query = {exact->context, QStringLiteral("payload_out"), exact->files};
    const auto exactReport =
        exactService.plan(query, exact->documents);
    bool exactSourceEdit = false;
    for (const auto& edit
         : exactReport.planResult.plan.workspaceEdit.edits) {
        exactSourceEdit = exactSourceEdit
            || QString::fromStdString(edit.filePath)
                == exact->leafFile;
    }
    check("exact source bridge is idempotently reused",
          exactReport.ready() && !exactSourceEdit);

    auto hiddenSourceDriver = fixture(
        temp.path() + QStringLiteral("/source_hidden_second_driver"),
        false, false, DriverScenario::SourceExactPlusConstant);
    ExposeSignalToTopService hiddenSourceDriverService(
        &hiddenSourceDriver->index,
        hiddenSourceDriver->hierarchy.get());
    query = {hiddenSourceDriver->context,
             QStringLiteral("payload_out"),
             hiddenSourceDriver->files};
    check("exact source relationship with hidden second driver conflicts",
          hiddenSourceDriverService
                  .plan(query, hiddenSourceDriver->documents).failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    auto undriven = fixture(
        temp.path() + QStringLiteral("/source_undriven"),
        false, false, DriverScenario::SourceUndriven);
    ExposeSignalToTopService undrivenService(
        &undriven->index, undriven->hierarchy.get());
    query = {undriven->context, QStringLiteral("payload_out"),
             undriven->files};
    check("undriven existing source output is blocked",
          undrivenService.plan(query, undriven->documents).failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    auto other = fixture(
        temp.path() + QStringLiteral("/source_other"),
        false, false, DriverScenario::SourceOther);
    ExposeSignalToTopService otherService(
        &other->index, other->hierarchy.get());
    query = {other->context, QStringLiteral("payload_out"), other->files};
    check("different source driver conflicts",
          otherService.plan(query, other->documents).failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    auto middle = fixture(
        temp.path() + QStringLiteral("/middle_other"),
        false, false, DriverScenario::MiddleOther);
    ExposeSignalToTopService middleService(
        &middle->index, middle->hierarchy.get());
    query = {middle->context, QStringLiteral("payload_out"), middle->files};
    check("middle unrelated driver conflicts",
          middleService.plan(query, middle->documents).failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    auto middleUnknown = fixture(
        temp.path() + QStringLiteral("/middle_unknown"),
        false, false, DriverScenario::MiddleUnknown);
    ExposeSignalToTopService middleUnknownService(
        &middleUnknown->index, middleUnknown->hierarchy.get());
    query = {middleUnknown->context, QStringLiteral("payload_out"),
             middleUnknown->files};
    check("unknown middle driver facts conflict",
          middleUnknownService.plan(query, middleUnknown->documents)
                  .failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    auto middleUndriven = fixture(
        temp.path() + QStringLiteral("/middle_undriven"),
        false, false, DriverScenario::MiddleUndriven);
    ExposeSignalToTopService middleUndrivenService(
        &middleUndriven->index, middleUndriven->hierarchy.get());
    query = {middleUndriven->context, QStringLiteral("payload_out"),
             middleUndriven->files};
    const ExposeSignalToTopReport middleUndrivenReport =
        middleUndrivenService.plan(query, middleUndriven->documents);
    check("Slang proves middle output has zero drivers",
          middleUndriven->nativeMiddleOutput
              && middleUndriven->nativeMiddleOutputInfo.driverPresence
                  == SemanticDriverPresenceState::ProvenZero
              && middleUndriven->nativeMiddleOutputInfo.driverCount == 0);
    check("proven-undriven middle output is reusable",
          middleUndrivenReport.ready());

    auto middleConstant = fixture(
        temp.path() + QStringLiteral("/middle_constant"),
        false, false, DriverScenario::MiddleConstantContinuous);
    ExposeSignalToTopService middleConstantService(
        &middleConstant->index, middleConstant->hierarchy.get());
    query = {middleConstant->context, QStringLiteral("payload_out"),
             middleConstant->files};
    check("Slang extracted middle constant output",
          middleConstant->nativeMiddleOutput);
    check("Slang summarized middle constant driver",
          middleConstant->nativeMiddleOutputInfo.driverPresence
                  == SemanticDriverPresenceState::Present
              && middleConstant->nativeMiddleOutputInfo.driverCount == 1
              && middleConstant->nativeMiddleOutputInfo
                     .continuousDriverCount == 1
              && middleConstant->nativeMiddleOutputInfo
                     .proceduralDriverCount == 0
              && middleConstant->nativeMiddleOutputInfo
                     .portConnectionDriverCount == 0);
    check("middle constant continuous driver conflicts",
          middleConstantService.plan(query, middleConstant->documents)
                  .failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    auto middleProcedural = fixture(
        temp.path() + QStringLiteral("/middle_procedural"),
        false, false, DriverScenario::MiddleConstantProcedural);
    ExposeSignalToTopService middleProceduralService(
        &middleProcedural->index, middleProcedural->hierarchy.get());
    query = {middleProcedural->context, QStringLiteral("payload_out"),
             middleProcedural->files};
    check("Slang extracted middle procedural output",
          middleProcedural->nativeMiddleOutput);
    check("Slang summarized middle procedural driver",
          middleProcedural->nativeMiddleOutputInfo.driverPresence
                  == SemanticDriverPresenceState::Present
              && middleProcedural->nativeMiddleOutputInfo.driverCount == 1
              && middleProcedural->nativeMiddleOutputInfo
                     .continuousDriverCount == 0
              && middleProcedural->nativeMiddleOutputInfo
                     .proceduralDriverCount == 1
              && middleProcedural->nativeMiddleOutputInfo
                     .portConnectionDriverCount == 0);
    check("middle procedural constant driver conflicts",
          middleProceduralService.plan(query, middleProcedural->documents)
                  .failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    auto middleChild = fixture(
        temp.path() + QStringLiteral("/middle_child"),
        false, false, DriverScenario::MiddleChildOutput);
    ExposeSignalToTopService middleChildService(
        &middleChild->index, middleChild->hierarchy.get());
    query = {middleChild->context, QStringLiteral("payload_out"),
             middleChild->files};
    check("Slang extracted child-driven middle output",
          middleChild->nativeMiddleOutput);
    check("Slang summarized child output connection driver",
          middleChild->nativeMiddleOutputInfo.driverPresence
                  == SemanticDriverPresenceState::Present
              && middleChild->nativeMiddleOutputInfo.driverCount == 1
              && middleChild->nativeMiddleOutputInfo
                     .continuousDriverCount == 1
              && middleChild->nativeMiddleOutputInfo
                     .proceduralDriverCount == 0
              && middleChild->nativeMiddleOutputInfo
                     .portConnectionDriverCount == 1);
    check("middle child output driver conflicts",
          middleChildService.plan(query, middleChild->documents).failureReason
              == rtledit::ExposeSignalFailureReason::DriverConflict);

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
