#include "../src/simulation/simdock/core/simulationbuildplanner.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

using namespace simdock;
namespace {
struct Workspace {
    QTemporaryDir directory;
    SimulationBuildRequest request;
    Workspace() {
        request.project.workspaceRoot = directory.path();
        request.project.includeDirs = {directory.path()};
        request.target = {"tb.sv", "tb"};
    }
    QString path(const QString& file) const { return SemanticInputCapture::pathKey(directory.filePath(file)); }
    bool put(const QString& name, const QByteArray& text) {
        const auto file = path(name);
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size()) return false;
        if (!request.project.systemVerilogFiles.contains(file)) request.project.systemVerilogFiles << file;
        request.project.allFiles = request.project.systemVerilogFiles;
        return true;
    }
    std::shared_ptr<const SimulationBuildPlan> plan() const { return SimulationBuildPlanner::plan(request); }
};
}
class SimulationBuildPlannerTest : public QObject {
    Q_OBJECT
private slots:
    void selectsOnlyTheTbClosure()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; dut d(); endmodule\n"));
        QVERIFY(w.put("dut.sv", "module dut; leaf l(); endmodule\n"));
        QVERIFY(w.put("leaf.sv", "module leaf; endmodule\n"));
        QVERIFY(w.put("other_tb.sv", "module unrelated; missing m(); endmodule\n"));
        QVERIFY(w.put("broken.sv", "module broken; this is invalid @@@; endmodule\n"));
        const auto plan = w.plan();
        QVERIFY2(plan->valid(), qPrintable(plan->error()));
        QCOMPARE(plan->compilation.files.size(), 3);
        QCOMPARE(plan->elaborationDependencies.value("tb.d.l"), w.path("leaf.sv"));
        QVERIFY(!plan->participatingFiles.contains(w.path("broken.sv")));
        QVERIFY(plan->input->sources.contains(w.path("broken.sv")));
    }
    void ordersPackagesAndCapturesIncludes()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; import outer_pkg::*; dut #(.N(VALUE)) d(); endmodule\n"));
        QVERIFY(w.put("a_outer.sv", "package outer_pkg; import inner_pkg::*; parameter VALUE=N; endpackage\n"));
        QVERIFY(w.put("z_inner.sv", "`include \"value.svh\"\npackage inner_pkg; parameter N=`VALUE; endpackage\n"));
        QVERIFY(w.put("value.svh", "`define VALUE 3\n"));
        QVERIFY(w.put("dut.sv", "module dut #(parameter N=1); logic [N-1:0] value; endmodule\n"));
        const auto plan = w.plan();
        QVERIFY2(plan->valid(), qPrintable(plan->error()));
        const auto files = plan->compilation.files;
        QVERIFY(files.indexOf(w.path("z_inner.sv")) < files.indexOf(w.path("a_outer.sv")));
        QVERIFY(files.indexOf(w.path("a_outer.sv")) < files.indexOf(w.path("tb.sv")));
        QVERIFY(!files.contains(w.path("value.svh")));
        QVERIFY(plan->participatingFiles.contains(w.path("value.svh")));
        QVERIFY(plan->compilation.dependencies.value(w.path("dut.sv")).isEmpty());
    }
    void followsParameterSpecializationAndInactiveGenerate()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; dut #(.USE(0)) d(); endmodule\n"));
        QVERIFY(w.put("dut.sv", "module dut #(parameter USE=1); if(USE) begin missing m(); end else begin leaf l(); end endmodule\n"));
        QVERIFY(w.put("leaf.sv", "module leaf; endmodule\n"));
        const auto good = w.plan();
        QVERIFY2(good->valid(), qPrintable(good->error()));
        QCOMPARE(good->compilation.files.size(), 3);
        w.request.overrides.insert(w.path("tb.sv"), "module tb; dut #(.USE(1)) d(); endmodule\n");
        w.request.documentRevisions.insert(w.path("tb.sv"), 9);
        const auto bad = w.plan();
        QVERIFY(!bad->valid());
        QVERIFY2(bad->error().contains("missing"), qPrintable(bad->error()));
        QCOMPARE(bad->input->documentRevisions.value(w.path("tb.sv")), quint64(9));
        QVERIFY(w.put("dut.sv", "module dut #(parameter USE=0); if(USE) begin logic @; end endmodule\n"));
        w.request.overrides.clear();
        QVERIFY(!w.plan()->valid());
    }
    void recursiveInstancesAreNotCompilationCycles()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; chain #(.N(3)) d(); endmodule\n"));
        QVERIFY(w.put("chain.sv", "module chain #(parameter N=0); if(N>0) chain #(.N(N-1)) child(); endmodule\n"));
        const auto plan = w.plan();
        QVERIFY2(plan->valid(), qPrintable(plan->error()));
        QCOMPARE(plan->compilation.files.size(), 2);
        QVERIFY(plan->elaborationDependencies.size() >= 5);
    }
    void resolvesInterfacesAndBindSources()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; bus_if bus(); dut d(bus); endmodule\n"));
        QVERIFY(w.put("interface.sv", "interface bus_if; logic ready; endinterface\n"));
        QVERIFY(w.put("dut.sv", "module dut(bus_if bus); endmodule\n"));
        QVERIFY(w.put("bound.sv", "module monitor(input logic ready); endmodule\n"));
        QVERIFY(w.put("binding.sv", "bind dut monitor check_ready(.ready(bus.ready));\n"));
        QVERIFY(w.put("unrelated_bind.sv", "bind other missing check_it();\n"));
        const auto plan = w.plan();
        QVERIFY2(plan->valid(), qPrintable(plan->error()));
        QCOMPARE(plan->compilation.files.size(), 5);
        QCOMPARE(plan->elaborationDependencies.value("tb.d.check_ready"), w.path("bound.sv"));
        QVERIFY(!plan->compilation.files.contains(w.path("unrelated_bind.sv")));
    }
    void duplicateDefinitionsRequireExplicitMapping()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; dut d(); endmodule\n"));
        QVERIFY(w.put("a.sv", "module dut; endmodule\n"));
        QVERIFY(w.put("b.sv", "module dut; missing m(); endmodule\n"));
        const auto ambiguous = w.plan();
        QVERIFY(!ambiguous->valid());
        QVERIFY(ambiguous->error().contains("a.sv"));
        QVERIFY(ambiguous->error().contains("b.sv"));
        w.request.definitionSources.insert("dut", "a.sv");
        const auto resolved = w.plan();
        QVERIFY2(resolved->valid(), qPrintable(resolved->error()));
        QCOMPARE(resolved->compilation.files.size(), 2);
        QVERIFY(!resolved->compilation.files.contains(w.path("b.sv")));
        w.request.definitionSources["dut"] = "../outside.sv";
        QVERIFY(!w.plan()->valid());
    }
    void rejectsInvalidPackageOrderButNotInstanceOrder()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; import p::*; endmodule\n"));
        QVERIFY(w.put("packages.sv", "package p; import q::*; endpackage\npackage q; endpackage\n"));
        const auto bad = w.plan();
        QVERIFY(!bad->valid());
        QVERIFY2(bad->error().contains("before"), qPrintable(bad->error()));
        QVERIFY(w.put("packages.sv", "package q; endpackage\npackage p; import q::*; endpackage\n"));
        QVERIFY2(w.plan()->valid(), qPrintable(w.plan()->error()));
    }
    void preprocessorUnitsDoNotLeakMacros()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "`define WIDTH 3\nmodule tb; dut d(); endmodule\n"));
        QVERIFY(w.put("dut.sv", "module dut; logic [`WIDTH-1:0] value; endmodule\n"));
        QVERIFY(!w.plan()->valid());
        w.request.project.defines.insert("WIDTH", "3");
        const auto plan = w.plan();
        QVERIFY2(plan->valid(), qPrintable(plan->error()));
        QCOMPARE(plan->defines.value("WIDTH"), "3");
    }
    void cacheTracksIncludesMissingCandidatesAndEnvironment()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "`include \"choice.svh\"\nmodule tb; `IMPLEMENTATION d(); endmodule\n"));
        QVERIFY(w.put("fallback/choice.svh", "`define IMPLEMENTATION good\n"));
        QVERIFY(w.put("good.sv", "module good; endmodule\n"));
        QDir().mkpath(w.directory.filePath("first"));
        w.request.project.includeDirs = {w.directory.filePath("first"), w.directory.filePath("fallback")};
        const auto initial = w.plan();
        QVERIFY2(initial->valid(), qPrintable(initial->error()));
        QVERIFY(SemanticInputCapture::matchesFingerprint(initial->input->fingerprint(), w.request.project));
        QVERIFY(w.put("first/choice.svh", "`define IMPLEMENTATION missing\n"));
        QVERIFY(!SemanticInputCapture::matchesFingerprint(initial->input->fingerprint(), w.request.project));
        const auto changed = w.plan();
        QVERIFY(!changed->valid());
        QVERIFY(changed->error().contains("missing"));
        w.request.project.includeDirs = {w.directory.filePath("fallback"), w.directory.filePath("first")};
        const auto reordered = w.plan();
        QVERIFY2(reordered->valid(), qPrintable(reordered->error()));
        QVERIFY(w.put("good.sv", "module good; missing nested(); endmodule\n"));
        QVERIFY(!w.plan()->valid());
    }
    void sourceCatalogAndTargetsAreExplicit()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; dut d(); endmodule\nprogram alternative; endprogram\n"));
        QVERIFY(w.put("disabled/dut.sv", "module dut; endmodule\n"));
        w.request.project.ignoredPaths = {w.directory.filePath("disabled")};
        const auto missing = w.plan();
        QVERIFY(!missing->valid());
        QVERIFY(missing->error().contains("no workspace definition"));
        w.request.target.top = "alternative";
        const auto program = w.plan();
        QVERIFY2(program->valid(), qPrintable(program->error()));
        QCOMPARE(program->compilation.files.size(), 1);
        w.request.target.top = "filename_guess";
        QVERIFY(!w.plan()->valid());
        w.request.target.top = "alternative";
        w.request.project.sourceDiscoveryComplete = false;
        QVERIFY(!w.plan()->valid());
        w.request.project.sourceDiscoveryComplete = true;
        const auto cancelled = SimulationBuildPlanner::plan(w.request, [] { return true; });
        QVERIFY(!cancelled->valid());
    }
    void nestedDefinitionsRetainLexicalScope()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; module inner; endmodule inner i(); endmodule\n"));
        const auto plan = w.plan();
        QVERIFY2(plan->valid(), qPrintable(plan->error()));
        QCOMPARE(plan->compilation.files, QStringList{w.path("tb.sv")});
        QCOMPARE(plan->elaborationDependencies.value("tb.i"), w.path("tb.sv"));
        QVERIFY(w.put("unused.sv", "module inner; unavailable m(); endmodule\n"));
        QVERIFY(w.put("another.sv", "module inner; endmodule\n"));
        const auto shadowed = w.plan();
        QVERIFY2(shadowed->valid(), qPrintable(shadowed->error()));
        QCOMPARE(shadowed->compilation.files, QStringList{w.path("tb.sv")});
        QCOMPARE(shadowed->elaborationDependencies.value("tb.i"), w.path("tb.sv"));
    }
    void verilogFilesUseVerilogKeywords()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; dut d(); endmodule\n"));
        QVERIFY(w.put("dut.v", "module dut; wire logic; endmodule\n"));
        const auto valid = w.plan();
        QVERIFY2(valid->valid(), qPrintable(valid->error()));
        QVERIFY(w.put("dut.v", "module dut; logic value; endmodule\n"));
        const auto invalid = w.plan();
        QVERIFY(!invalid->valid());
    }
    void workspaceDiscoveryUsesConfiguredInputs()
    {
        Workspace w;
        QVERIFY(w.put("tb.sv", "module tb; dut d(); endmodule\n"));
        QVERIFY(w.put("dut.sv", "module dut; endmodule\n"));
        QString error;
        const auto request = SimulationBuildPlanner::workspaceRequest(w.directory.path(), {"tb.sv", "tb"}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto plan = SimulationBuildPlanner::plan(request);
        QVERIFY2(plan->valid(), qPrintable(plan->error()));
        QCOMPARE(plan->compilation.files.size(), 2);
        SimulationBuildPlanner::workspaceRequest(w.directory.path(), {"tb.sv", "tb"}, &error, [] { return true; });
        QVERIFY(error.contains("cancelled"));
    }
};
QTEST_GUILESS_MAIN(SimulationBuildPlannerTest)
#include "simulation_build_planner_test.moc"
