#include <zeroslack/semantic/compilationorderservice.h>
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include "../src/simulation/simdock/core/dependencies.h"
#include "../src/simulation/simdock/core/workspace.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "navigationwidget.h"
#include "testuistyle.h"
#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QPushButton>
#include <QListView>
#include <QStandardItemModel>
#include <QUuid>

namespace {
void put(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) qFatal("Cannot write fixture");
}
struct Fixture {
    QTemporaryDir directory;
    simdock::Project project = simdock::newProject("Order fixture");
    QString file(const QString& name) const { return directory.filePath(name); }
    void add(const QString& name, const QByteArray& bytes) { put(file(name), bytes); project.sources << name; }
    simdock::BuildInputPlan plan() const { return simdock::planBuildInputs(directory.path(), project); }
    QStringList relative(const QStringList& files) const {
        QStringList result; for (const auto& path : files) result << QDir(directory.path()).relativeFilePath(path); return result;
    }
};
}

class BuildInputsContractTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(initializeUiStyleForTest()); }
    void stablePackageOrderAndSeparateUnits() {
        Fixture f;
        f.add("a_tb.sv", "module tb; import derived::*; leaf u(); endmodule\n");
        f.add("b_derived.sv", "package derived; import base::*; typedef word_t result_t; endpackage\n");
        f.add("z_base.sv", "package base; typedef logic [7:0] word_t; endpackage\n");
        f.add("zz_leaf.sv", "module leaf; endmodule\n");
        f.add("defs.svh", "`define VALUE 1\n");
        auto plan = f.plan(); QVERIFY2(plan.order.valid(), qPrintable(plan.error()));
        QCOMPARE(f.relative(plan.order.files), (QStringList{"z_base.sv", "b_derived.sv", "a_tb.sv", "zz_leaf.sv"}));
        QCOMPARE(f.relative(plan.order.headers), QStringList{"defs.svh"});
        plan.apply(f.project, f.directory.path());
        QCOMPARE(f.project.compilationUnits.size(), 4);
        QVERIFY(!f.project.compilationUnits.contains("defs.svh"));
        std::reverse(f.project.sources.begin(), f.project.sources.end());
        QCOMPARE(f.plan().order.files, plan.order.files);
        QCOMPARE(plan.order.dependencies.value(f.file("a_tb.sv")), QStringList{f.file("b_derived.sv")});
    }
    void macrosIncludesAndConditionalDependencies() {
        Fixture f;
        f.add("top.sv", "`include `HEADER\nmodule top;\n`ifdef ENABLE\nimport config_pkg::*; word_t x;\n`else\nimport absent_pkg::*;\n`endif\nendmodule\n");
        f.add("z_pkg.sv", "package config_pkg; typedef logic word_t; endpackage\n");
        put(f.file("inc/select.svh"), "`define ENABLE\n");
        WorkspaceConfigurationService service;
        auto config = service.defaultConfiguration(f.directory.path());
        config.includeDirs = {f.file("inc")}; config.defines.insert("HEADER", "\"select.svh\"");
        QVERIFY(service.save(config));
        auto plan = f.plan(); QVERIFY2(plan.order.valid(), qPrintable(plan.error()));
        QCOMPARE(f.relative(plan.order.files), (QStringList{"z_pkg.sv", "top.sv"}));
        QVERIFY(plan.order.input->sources.contains(SemanticInputCapture::pathKey(f.file("inc/select.svh"))));
        put(f.file("inc/select.svh"), "// ENABLE deliberately absent\n");
        QVERIFY(f.plan().error().contains("absent_pkg"));
        f.add("a_define.sv", "`define ENABLE\nmodule defines; endmodule\n");
        QVERIFY(f.plan().error().contains("absent_pkg")); // no macro leakage across vlog calls
    }
    void classScopesAreNotPackageDependencies() {
        Fixture f;
        f.add("a_top.sv", "module top; import types::*; localparam int X=object_t::value; leaf dut(); initial $display(dut.VALUE); endmodule module leaf; localparam VALUE=42; endmodule\n");
        f.add("z_types.sv", "package types; class object_t; localparam int value=42; endclass endpackage\n");
        auto plan = f.plan(); QVERIFY2(plan.order.valid(), qPrintable(plan.error()));
        QCOMPARE(f.relative(plan.order.files), (QStringList{"z_types.sv", "a_top.sv"}));
    }
    void dependencyFailuresAndRecovery() {
        Fixture f;
        f.add("a.sv", "package a; import b::*; endpackage\n");
        QVERIFY(f.plan().error().contains("no selected provider"));
        f.add("b.sv", "package b; import a::*; endpackage\n");
        QVERIFY(f.plan().error().contains("cycle"));
        put(f.file("b.sv"), "package b; endpackage\n");
        QVERIFY2(f.plan().order.valid(), qPrintable(f.plan().error()));
        f.add("duplicate.sv", "package b; endpackage\n");
        QVERIFY(f.plan().error().contains("compiled more than once"));
        f.project.sources.removeLast();
        put(f.file("a.sv"), "import local_pkg::*; package local_pkg; endpackage\n");
        QVERIFY(f.plan().error().contains("before its declaration"));
        put(f.file("a.sv"), "`include \"missing.svh\"\nmodule top; endmodule\n");
        auto missing = f.plan(); QVERIFY(!missing.order.valid());
        QVERIFY(missing.error().contains("missing.svh"));
        QVERIFY(missing.order.input->sources.contains(SemanticInputCapture::pathKey(f.file("missing.svh"))));
        put(f.file("missing.svh"), "// recovered\n");
        QVERIFY2(f.plan().order.valid(), qPrintable(f.plan().error()));
        f.project.sources << "absent.svh";
        QVERIFY(f.plan().error().contains("Cannot read build input"));
    }
    void explicitRootIdentity() {
        Fixture f;
        const auto a = SemanticFixtureRecordBuilder("shared", SymbolTaxonomy::DeclarationKind::Module).withFile(f.file("a.sv")).record();
        const auto b = SemanticFixtureRecordBuilder("shared", SymbolTaxonomy::DeclarationKind::Module).withFile(f.file("b.sv")).record();
        SemanticIndex index;
        index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords({a, b})));
        HierarchyService hierarchy(&index);
        const auto idA = QUuid::createUuid().toString(QUuid::WithoutBraces), idB = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QList<DesignHierarchyRoot> roots{{idA,"A","shared",a.location.fileName,false},{idB,"B","shared",b.location.fileName,true}};
        auto report = hierarchy.explicitDesignHierarchy(roots, idB, {});
        QCOMPARE(report.nodes.size(), 2);
        QVERIFY(!report.nodes[0].unresolved && !report.nodes[1].unresolved);
        QVERIFY(!report.nodes[0].inSelectedTop && report.nodes[1].inSelectedTop);
        QCOMPARE(report.nodes[0].definitionFile, a.location.fileName);
        roots[0].file.clear();
        report = hierarchy.explicitDesignHierarchy(roots, {}, {});
        QVERIFY(report.nodes[0].unresolved);
        QVERIFY(!report.nodes[0].inSelectedTop && !report.nodes[1].inSelectedTop);
        roots[0].file = f.file("deleted.sv");
        QVERIFY(hierarchy.explicitDesignHierarchy(roots, idA, {}).nodes[0].unresolved);
    }
    void projectPersistenceAndWorkspaceIsolation() {
        Fixture f; Fixture other;
        f.add("top.sv", "module top; leaf u(); endmodule module leaf; endmodule\n");
        QString first, second;
        {
            simdock::Workbench workbench;
            QSignalSpy scanned(&workbench, &simdock::Workbench::scanFinished);
            QVERIFY(workbench.setContext(f.directory.path()).isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!scanned.isEmpty(), 10000);
            QVERIFY(workbench.activeProjectId().isEmpty());
            QVERIFY(workbench.addDesignProject(f.file("top.sv"), "top").isEmpty()); first = workbench.activeProjectId();
            QVERIFY(workbench.addDesignProject(f.file("top.sv"), "leaf").isEmpty()); second = workbench.activeProjectId();
            QVERIFY(first != second); QCOMPARE(workbench.designRoots().size(), 2);
            workbench.setDesignExpanded(first, false);
            QCOMPARE(workbench.activeProjectId(), second);
            QVERIFY(workbench.openProject(first).isEmpty());
            QVERIFY(workbench.setContext(other.directory.path()).isEmpty());
            QVERIFY(workbench.activeProjectId().isEmpty()); QVERIFY(workbench.designRoots().isEmpty());
            QVERIFY(workbench.setContext(f.directory.path()).isEmpty());
            QCOMPARE(workbench.activeProjectId(), first);
            auto* projects = workbench.findChild<QPushButton*>("projectsDisclosure");
            auto* inputs = workbench.findChild<QPushButton*>("inputsDisclosure");
            QVERIFY(projects && inputs); projects->setChecked(false); inputs->setChecked(false);
        }
        simdock::Workbench reopened;
        QVERIFY(reopened.setContext(f.directory.path()).isEmpty());
        QCOMPARE(reopened.activeProjectId(), first); QCOMPARE(reopened.designRoots().size(), 2);
        for (const auto& root : reopened.designRoots()) QCOMPARE(root.expanded, root.projectId != first);
        QVERIFY(!reopened.findChild<QPushButton*>("projectsDisclosure")->isChecked());
        QVERIFY(!reopened.findChild<QPushButton*>("inputsDisclosure")->isChecked());
        QVERIFY(reopened.removeDesignRoot(first).isEmpty());
        QCOMPARE(reopened.designRoots().size(), 1);
        QCOMPARE(simdock::loadProjects(f.directory.path()).size(), 2);
    }
    void displayedOrderIncludesImplicitTestbench() {
        Fixture f;
        f.add("a_top.sv","module top; import base::*; word_t data; endmodule\n");
        f.add("z_base.sv","package base; typedef logic word_t; endpackage\n");
        f.add("defs.svh","`define VALUE 1\n");
        put(f.file("00_tb.sv"),"module tb; top dut(); endmodule\n");
        f.project.tbFile="00_tb.sv"; f.project.tbName="tb";
        QString error; QVERIFY2(simdock::saveProject(f.directory.path(),f.project,&error),qPrintable(error));
        simdock::Workbench workbench;
        QSignalSpy scanned(&workbench,&simdock::Workbench::scanFinished);
        QVERIFY(workbench.setContext(f.directory.path()).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!scanned.isEmpty(),10000);
        QVERIFY(workbench.openProject(f.project.id).isEmpty());
        auto* list=workbench.findChild<QListView*>("sourceList"); QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->model()->index(0,0).data(Qt::UserRole).toString(),QString("00_tb.sv"),10000);
        QCOMPARE(list->model()->index(1,0).data(Qt::UserRole).toString(),QString("z_base.sv"));
        QCOMPARE(list->model()->index(2,0).data(Qt::UserRole).toString(),QString("a_top.sv"));
        QCOMPARE(list->model()->index(3,0).data(Qt::UserRole).toString(),QString("defs.svh"));
        QVERIFY(!simdock::loadProjects(f.directory.path()).first().sources.contains("00_tb.sv"));
    }
    void folderAndProjectRowInteractions() {
        Fixture f;
        NavigationWidget navigation;
        navigation.resize(420,600); navigation.setWorkspaceRoot(f.directory.path());
        navigation.updateFileHierarchy({f.file("rtl/top.sv")}); navigation.show();
        auto* files = navigation.findChild<QTreeWidget*>("navigationFileTree"); QVERIFY(files);
        QTRY_VERIFY(files->topLevelItemCount() == 1 && files->topLevelItem(0)->childCount() == 1);
        auto* folder = files->topLevelItem(0); const auto position = files->visualItemRect(folder).center();
        const bool expanded = folder->isExpanded();
        QTest::mouseClick(files->viewport(), Qt::LeftButton, {}, position);
        QCOMPARE(folder->isExpanded(), !expanded);
        QTest::mouseClick(files->viewport(), Qt::LeftButton, {}, position);
        QCOMPARE(folder->isExpanded(), expanded);
        DesignHierarchyReport report;
        DesignHierarchyNode root; root.id="root"; root.rootId="root"; root.projectId="project"; root.projectName="Project"; root.moduleType="top"; root.instanceName="top"; root.isTop=true; root.expanded=true;
        DesignHierarchyNode child; child.id="child"; child.parentId="root"; child.rootId="root"; child.instanceName="u"; child.moduleType="leaf";
        report.nodes={root,child}; navigation.updateDesignHierarchy(report); navigation.setActiveTab(NavigationWidget::DesignTab);
        auto* design=navigation.findChild<QTreeWidget*>("navigationDesignTree"); QVERIFY(design);
        QTRY_COMPARE(design->topLevelItemCount(),1); QCOMPARE(design->columnCount(),1); QVERIFY(design->isHeaderHidden());
        QSignalSpy activation(&navigation,&NavigationWidget::designProjectActivated);
        auto* row=design->topLevelItem(0); const QRect bounds=design->visualItemRect(row);
        QTest::mouseClick(design->viewport(),Qt::LeftButton,{},bounds.center()); QVERIFY(!row->isExpanded()); QVERIFY(activation.isEmpty());
        QTest::mouseClick(design->viewport(),Qt::LeftButton,{},QPoint(bounds.right()-12,bounds.center().y()));
        QCOMPARE(activation.size(),1); QVERIFY(!row->isExpanded());
        QCOMPARE(row->child(0)->text(0),QString("u[leaf]"));
    }
};

int main(int argc,char** argv) {
    QApplication app(argc,argv); QTemporaryDir settings;
    QCoreApplication::setOrganizationName("ZeroSlackTests"); QCoreApplication::setApplicationName("BuildInputsContract");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QStandardPaths::setTestModeEnabled(true);
    BuildInputsContractTest test; return QTest::qExec(&test,argc,argv);
}
#include "build_inputs_contract_test.moc"
