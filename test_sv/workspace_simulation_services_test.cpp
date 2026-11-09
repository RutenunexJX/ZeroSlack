#include "../src/workspace/designprojectservice.h"
#include "../src/simulation/simdock/core/workspacesimulationcontroller.h"
#include "../src/simulation/simdock/core/simulationeventmodel.h"
#include "../src/simulation/simdock/core/workspace.h"
#include "../src/simulation/simdock/ui/workbench.h"
#include "../src/simulation/simdock/ui/simulationeventspanel.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextDocument>

namespace {
bool put(const QString& path, const QByteArray& value)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(value) == value.size();
}
QByteArray read(const QString& path)
{ QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QString legacyKey(const QString& path)
{
    return QStringLiteral("integrations/simdock/workspaces/") + QString::fromLatin1(QCryptographicHash::hash(
        QDir::fromNativeSeparators(QFileInfo(path).canonicalFilePath()).toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
}
}
class WorkspaceSimulationServicesTest : public QObject {
    Q_OBJECT
private slots:
    void designAndTargetAreIndependentWithoutViews()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(put(directory.filePath("design.sv"), "module design; endmodule\n"));
        QVERIFY(put(directory.filePath("tb.sv"), "module tb; endmodule\n"));
        DesignProjectService design;
        simdock::WorkspaceSimulationController simulation;
        QVERIFY(design.setWorkspace(directory.path()).isEmpty());
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QVERIFY(simulation.selectTarget({"tb.sv", "tb"}).isEmpty());
        QVERIFY(design.addDesignProject(directory.filePath("design.sv"), "design").isEmpty());
        const auto id = design.activeProjectId();
        QVERIFY(!id.isEmpty());
        QVERIFY(design.setDesignExpanded(id, false).isEmpty());
        QCOMPARE(simulation.target().file, "tb.sv");
        QVERIFY(design.activateProject({}).isEmpty());
        QCOMPARE(simulation.target().top, "tb");
        QVERIFY(design.activateProject(id).isEmpty());
        DesignProjectService reopened;
        QVERIFY(reopened.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(reopened.activeProjectId(), id);
        QCOMPARE(reopened.designRoots().size(), 1);
        QVERIFY(!reopened.designRoots().first().expanded);
        QVERIFY(reopened.removeDesignRoot(id).isEmpty());
        QVERIFY(QFileInfo::exists(directory.filePath("design.sv")));
        QCOMPARE(simulation.target().top, "tb");
    }
    void legacyMigrationIsAtomicAndIdempotent()
    {
        QTemporaryDir directory;
        QVERIFY(put(directory.filePath("tb.sv"), "module tb; endmodule\n"));
        auto project = simdock::newProject("Preserved project");
        project.tbFile = "tb.sv"; project.tbName = "tb";
        project.designTopFile = "tb.sv"; project.designTopModule = "tb";
        project.sources = {"tb.sv"};
        project.preservedInputs = {{"stimulus", QJsonObject{{"retained", true}}}};
        QString error;
        QVERIFY2(simdock::saveProject(directory.path(), project, &error), qPrintable(error));
        const auto manifest = simdock::projectDirectory(directory.path()) + "/" + project.storageName + "/project.json";
        const auto previous = read(manifest);
        QSettings().setValue(legacyKey(directory.path()) + "/activeProject", project.id);
        QSettings().setValue(legacyKey(directory.path()) + "/expanded/" + project.id, false);
        QLockFile lock(directory.filePath(".simdock/design.json.lock"));
        QVERIFY(lock.tryLock());
        DesignProjectService design;
        QVERIFY(!design.setWorkspace(directory.path()).isEmpty());
        QVERIFY(!QFileInfo::exists(directory.filePath(".simdock/design.json")));
        QVERIFY(design.workspace().isEmpty());
        lock.unlock();
        QVERIFY(design.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(design.activeProjectId(), project.id);
        QCOMPARE(design.designRoots().size(), 1);
        QVERIFY(!design.designRoots().first().expanded);
        simdock::WorkspaceSimulationController simulation;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(simulation.target().top, "tb");
        QCOMPARE(simulation.legacyProjectId(), project.id);
        const auto designBytes = read(directory.filePath(".simdock/design.json"));
        const auto simulationBytes = read(directory.filePath(".simdock/simulation.json"));
        QVERIFY(design.setWorkspace({}).isEmpty());
        QVERIFY(simulation.setWorkspace({}).isEmpty());
        QVERIFY(design.setWorkspace(directory.path()).isEmpty());
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(read(manifest), previous);
        QCOMPARE(read(directory.filePath(".simdock/design.json")), designBytes);
        QCOMPARE(read(directory.filePath(".simdock/simulation.json")), simulationBytes);
        QCOMPARE(design.designRoots().size(), 1);
    }
    void conflictingLegacyIdentityNeverSelectsAFile()
    {
        QTemporaryDir directory;
        QVERIFY(put(directory.filePath("a.sv"), "module a; endmodule\n"));
        QVERIFY(put(directory.filePath("b.sv"), "module b; endmodule\n"));
        auto first = simdock::newProject("First");
        first.tbFile = "a.sv"; first.tbName = "a";
        QString error;
        QVERIFY(simdock::saveProject(directory.path(), first, &error));
        auto conflict = first;
        conflict.name = "Conflicting copy"; conflict.tbFile = "b.sv"; conflict.tbName = "b";
        QVERIFY(put(directory.filePath(".simdock/projects/Conflicting copy/project.json"),
            QJsonDocument(simdock::projectJson(conflict)).toJson()));
        QSettings().setValue(legacyKey(directory.path()) + "/activeProject", first.id);
        simdock::WorkspaceSimulationController simulation;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QVERIFY(simulation.target().isEmpty());
        QVERIFY(simulation.legacyProjectId().isEmpty());
        QVERIFY(QFileInfo::exists(directory.filePath("a.sv")));
        QVERIFY(QFileInfo::exists(directory.filePath("b.sv")));
    }
    void revisionsRejectConcurrentWriters()
    {
        QTemporaryDir directory;
        QVERIFY(put(directory.filePath("a.sv"), "module a; endmodule module b; endmodule\n"));
        DesignProjectService first, stale;
        QVERIFY(first.setWorkspace(directory.path()).isEmpty());
        QVERIFY(stale.setWorkspace(directory.path()).isEmpty());
        QVERIFY(first.addDesignProject(directory.filePath("a.sv"), "a").isEmpty());
        QVERIFY(!stale.addDesignProject(directory.filePath("a.sv"), "b").isEmpty());
        QVERIFY(stale.designRoots().isEmpty());
        simdock::WorkspaceSimulationController run, staleRun;
        QVERIFY(run.setWorkspace(directory.path()).isEmpty());
        QVERIFY(staleRun.setWorkspace(directory.path()).isEmpty());
        QVERIFY(run.selectTarget({"a.sv", "a"}).isEmpty());
        QVERIFY(!staleRun.selectTarget({"a.sv", "b"}).isEmpty());
        QVERIFY(staleRun.target().isEmpty());
        simdock::WorkspaceSimulationController reopened;
        QVERIFY(reopened.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(reopened.target().top, "a");
    }
    void targetSelectionUsesDeclarationsAndNeverGuesses()
    {
        QTemporaryDir directory;
        QVERIFY(put(directory.filePath("different_name.sv"), "program actual; initial $finish; endprogram\ninterface pins; endinterface\nmodule second; endmodule\n"));
        simdock::WorkspaceSimulationController simulation;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QVERIFY(simulation.target().isEmpty());
        QVERIFY(simulation.selectTarget({"different_name.sv", "actual"}).isEmpty());
        QVERIFY(!simulation.selectTarget({"different_name.sv", "pins"}).isEmpty());
        QVERIFY(!simulation.selectTarget({"different_name.sv", "different_name"}).isEmpty());
        QCOMPARE(simulation.target().top, "actual");
        QVERIFY(put(directory.filePath("different_name.sv"), "module second; endmodule\n"));
        simdock::WorkspaceSimulationController reopened;
        QVERIFY(reopened.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(reopened.target().top, "actual");
        QVERIFY(!simdock::validateSimulationTarget(directory.path(), reopened.target()).isEmpty());
    }
    void logAndEventsOutliveDestroyedViews()
    {
        QTemporaryDir directory;
        simdock::WorkspaceSimulationController simulation;
        DesignProjectService design;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QVERIFY(design.setWorkspace(directory.path()).isEmpty());
        auto view = std::make_unique<simdock::Workbench>(nullptr, false, &simulation, &design);
        auto* log = view->runLogWidget()->findChild<QPlainTextEdit*>("simulationLog");
        QVERIFY(log);
        QCOMPARE(log->document(), simulation.logDocument());
        QCOMPARE(qobject_cast<simdock::SimulationEventsPanel*>(view->eventsWidget())->eventModel(), simulation.eventModel());
        simulation.session()->eventRunStarted("run");
        simdock::SimulationEvent event;
        event.runId = "run"; event.message = "before view close";
        simulation.session()->eventsReady({event});
        view.reset();
        simulation.appendLog("after view close\n");
        event.message = "after view close";
        simulation.session()->eventsReady({event});
        simulation.session()->eventRecordsRejected(2);
        QTRY_VERIFY(simulation.logDocument()->toPlainText().contains("after view close"));
        QCOMPARE(simulation.eventModel()->rowCount(), 2);
        QCOMPARE(simulation.eventModel()->rejectedCount(), quint64(2));
        view = std::make_unique<simdock::Workbench>(nullptr, false, &simulation, &design);
        QCOMPARE(qobject_cast<simdock::SimulationEventsPanel*>(view->eventsWidget())->eventModel()->rowCount(), 2);
        QVERIFY(view->runLogWidget()->findChild<QPlainTextEdit*>("simulationLog")->toPlainText().contains("after view close"));
    }
    void preparationIsOwnedByWorkspace()
    {
        QTemporaryDir directory;
        simdock::WorkspaceSimulationController simulation;
        DesignProjectService design;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QVERIFY(design.setWorkspace(directory.path()).isEmpty());
        auto view = std::make_unique<simdock::Workbench>(nullptr, false, &simulation, &design);
        QSignalSpy done(&simulation, &simdock::WorkspaceSimulationController::finished);
        simulation.prepare(directory.filePath("missing-vsim.exe"), simdock::newProject("fixture"), simdock::WaveTheme::QuestaDefault);
        QVERIFY(simulation.preparing());
        QVERIFY(!simulation.canChangeWorkspace().isEmpty());
        view.reset();
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!simulation.preparing());
        QVERIFY(!done.first().first().toBool());
        QVERIFY(simulation.canChangeWorkspace().isEmpty());
        simulation.prepare(directory.filePath("missing-vsim.exe"), simdock::newProject("cancel"), simdock::WaveTheme::QuestaDefault);
        simulation.cancelPreparation();
        QTest::qWait(50);
        QCOMPARE(done.size(), 2);
        QVERIFY(!done.last().first().toBool());
        QCOMPARE(done.last().at(1).toString(), "Preparation cancelled");
        QCOMPARE(simulation.state(), "Preparation cancelled");
    }
    void simulationMappingsPersistWithoutChangingDesignOrTarget()
    {
        QTemporaryDir directory;
        QVERIFY(put(directory.filePath("tb.sv"), "module tb; endmodule\n"));
        QVERIFY(put(directory.filePath("a.sv"), "module dut; endmodule\n"));
        QVERIFY(put(directory.filePath("b.sv"), "module dut; endmodule\n"));
        QVERIFY(QDir().mkpath(directory.filePath("include")));
        simdock::WorkspaceSimulationController simulation;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QVERIFY(simulation.selectTarget({"tb.sv", "tb"}).isEmpty());
        QVERIFY(simulation.setSettings({"include"}, {{"dut", "a.sv"}}).isEmpty());
        QCOMPARE(simulation.target().top, "tb");
        simdock::WorkspaceSimulationController reopened;
        QVERIFY(reopened.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(reopened.includeDirectories(), QStringList{"include"});
        QCOMPARE(reopened.definitionSources().value("dut"), "a.sv");
        QVERIFY(reopened.setSettings({"include"}, {{"dut", "b.sv"}}).isEmpty());
        QVERIFY(!simulation.setSettings({}, {{"dut", "b.sv"}}).isEmpty());
        QCOMPARE(simulation.definitionSources().value("dut"), "a.sv");
        QCOMPARE(simulation.includeDirectories(), QStringList{"include"});
        QVERIFY(reopened.clearTarget().isEmpty());
        QCOMPARE(reopened.definitionSources().value("dut"), "b.sv");
        QCOMPARE(reopened.includeDirectories(), QStringList{"include"});
    }
    void targetCanBeSelectedFromTheCurrentEditorRevision()
    {
        QTemporaryDir directory;
        QVERIFY(put(directory.filePath("tb.sv"), "module original; endmodule\n"));
        simdock::WorkspaceSimulationController simulation;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        const QMap<QString, QByteArray> overrides{{directory.filePath("tb.sv"), "module draft; endmodule\n"}};
        QVERIFY(simulation.selectTarget({"tb.sv", "draft"}, {}, overrides).isEmpty());
        QCOMPARE(simulation.target().top, "draft");
        QCOMPARE(read(directory.filePath("tb.sv")), QByteArray("module original; endmodule\n"));
        simdock::WorkspaceSimulationController reopened;
        QVERIFY(reopened.setWorkspace(directory.path()).isEmpty());
        QCOMPARE(reopened.target().top, "draft");
    }
    void saveConflictAndCancellationPreventSimulationLaunch()
    {
        QTemporaryDir directory;
        QVERIFY(put(directory.filePath("tb.sv"), "module tb; endmodule\n"));
        simdock::WorkspaceSimulationController simulation;
        QVERIFY(simulation.setWorkspace(directory.path()).isEmpty());
        QString error;
        const auto request = simdock::SimulationBuildPlanner::workspaceRequest(directory.path(), {"tb.sv", "tb"}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        int saveCalls = 0;
        const auto save = [&](const simdock::SimulationBuildPlan& plan, simdock::SimulationBuildRequest&) {
            ++saveCalls;
            if (plan.compilation.files.size() != 1) return QStringLiteral("wrong input closure");
            return QStringLiteral("fixture save conflict");
        };
        QSignalSpy done(&simulation, &simdock::WorkspaceSimulationController::finished);
        QSignalSpy launched(simulation.session(), &simdock::QuestaSession::eventRunStarted);
        simulation.prepare(directory.filePath("unused-vsim.exe"), request, simdock::WaveTheme::QuestaDefault, save);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(saveCalls, 1);
        QCOMPARE(done.first().at(1).toString(), "fixture save conflict");
        QVERIFY(!simulation.busy()); QVERIFY(!simulation.session()->alive()); QVERIFY(launched.isEmpty());
        simulation.prepare(directory.filePath("unused-vsim.exe"), request, simdock::WaveTheme::QuestaDefault, save);
        simulation.cancelPreparation();
        QTest::qWait(100);
        QCOMPARE(saveCalls, 1); QCOMPARE(done.size(), 2); QVERIFY(launched.isEmpty());
        QVERIFY(!done.last().first().toBool());
        QCOMPARE(done.last().at(1).toString(), "Preparation cancelled");
    }
    void pendingLogDoesNotCallDestroyedView()
    {
        auto owned = std::make_unique<simdock::Workbench>();
        owned->controller()->appendLog("pending when the view and its standalone controller are destroyed\n");
        owned.reset();
        simdock::WorkspaceSimulationController simulation;
        DesignProjectService design;
        auto observing = std::make_unique<simdock::Workbench>(nullptr, false, &simulation, &design);
        delete observing->runLogWidget();
        delete observing->eventsWidget();
        simulation.appendLog("all display widgets have been closed\n");
        QTRY_VERIFY(simulation.logDocument()->toPlainText().contains("display widgets"));
        QVERIFY(!observing->runLogWidget());
        QVERIFY(!observing->eventsWidget());
        observing.reset();
        QVERIFY(simulation.logDocument()->toPlainText().contains("display widgets"));
    }
};
QTEST_MAIN(WorkspaceSimulationServicesTest)
#include "workspace_simulation_services_test.moc"
