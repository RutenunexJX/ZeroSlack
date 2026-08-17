#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>
#include <QTabWidget>
#include <QtTest/QTest>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

#include "analysiscoordinator.h"
#include "analysisscheduler.h"
#include "diagnosticpublicationpolicy.h"
#include "documentmodel.h"
#include "editorfileidentity.h"
#include "effectivevalueservice.h"
#include "incrementalanalysisplanservice.h"
#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "navigationservice.h"
#include "semanticchangeclassifier.h"
#include "semanticdependencygraph.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "symbolanalyzer.h"
#include "symbolrelationshipengine.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "semantic_fixture_records.h"

class AnalysisSchedulerTestAccess
{
public:
    static void rememberPendingCleanChange(AnalysisScheduler& scheduler,
                                           const QString& fileName,
                                           const QString& text)
    {
        scheduler.rememberPendingCleanSemanticChange(
            fileName,
            text,
            0,
            SemanticAnalysisReason::Save);
    }

    static void requestSave(AnalysisScheduler& scheduler,
                            const QString& triggerFile,
                            const ProjectSnapshot& project)
    {
        scheduler.requestSemanticAnalysis(
            SemanticAnalysisReason::Save,
            SemanticChangeImpact::Unknown,
            triggerFile,
            {triggerFile},
            project);
    }
};

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", label);
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
            return true;
        QTest::qWait(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

void waitForDuration(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(10);
    }
}

bool containsFile(const QStringList& files, const QString& fileName)
{
    const QString target = QFileInfo(fileName).absoluteFilePath();
    for (const QString& candidate : files) {
        if (QFileInfo(candidate).absoluteFilePath().compare(
                target, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void runAnalysisRuntimePolicyPlanning()
{
    const QString root =
        QDir::temp().absoluteFilePath(
            QStringLiteral("analysis_runtime_policy_planning"));
    const QString child =
        QDir(root).absoluteFilePath(QStringLiteral("child.sv"));
    const QString unrelated =
        QDir(root).absoluteFilePath(QStringLiteral("unrelated.sv"));
    ProjectSnapshot project;
    project.workspaceRoot = root;
    project.systemVerilogFiles = {child, unrelated};
    project.allFiles = project.systemVerilogFiles;
    project.includeDirs = {root};

    const SemanticDependencyGraph graph =
        SemanticDependencyGraph::build(
            project,
            {{child,
              QStringLiteral("module child; logic value; endmodule\n")},
             {unrelated,
              QStringLiteral("module unrelated; endmodule\n")}});
    SemanticAnalysisRequest request;
    request.generation = 1;
    request.reason = SemanticAnalysisReason::Save;
    request.project = project;
    request.triggerFile = child;
    request.changedFiles = {child};
    SemanticChangeClassification classification;
    classification.impact = SemanticChangeImpact::LocalBody;

    const IncrementalAnalysisPlan incremental =
        IncrementalAnalysisPlanService().plan(
            request,
            classification,
            graph);
    expect("enabled dependency-aware policy retains incremental plan",
           !incremental.fullWorkspace
               && incremental.affectedFiles.size() == 1
               && containsFile(incremental.affectedFiles, child));

    request.runtimePolicy.planningMode =
        SemanticAnalysisPlanningMode::FullWorkspace;
    const IncrementalAnalysisPlan forcedFull =
        IncrementalAnalysisPlanService().plan(
            request,
            classification,
            graph);
    expect("disabled incremental policy forces authoritative full plan",
           forcedFull.fullWorkspace
               && forcedFull.authoritativeWorkspaceReplace
               && forcedFull.compilationFiles.size()
                      == project.systemVerilogFiles.size()
               && forcedFull.fallbackReason.contains(
                   QStringLiteral("runtime policy")));
}

void runDiagnosticPublicationPolicyOrdering()
{
    auto diagnostic = [](SemanticDiagnostic::Severity severity,
                         const QString& message) {
        SemanticDiagnostic value;
        value.fileName = QStringLiteral("diagnostic_policy.sv");
        value.message = message;
        value.severity = severity;
        return value;
    };
    const QList<SemanticDiagnostic> produced{
        diagnostic(SemanticDiagnostic::Info,
                   QStringLiteral("info-first")),
        diagnostic(SemanticDiagnostic::Error,
                   QStringLiteral("error-first")),
        diagnostic(SemanticDiagnostic::Warning,
                   QStringLiteral("warning-first")),
        diagnostic(SemanticDiagnostic::Error,
                   QStringLiteral("error-second")),
        diagnostic(SemanticDiagnostic::Info,
                   QStringLiteral("info-second")),
    };
    const DiagnosticPublicationSelection selected =
        DiagnosticPublicationPolicy::select(produced, 3);
    expect("diagnostic publication limit reports produced published and suppressed",
           selected.producedCount == 5
               && selected.publishedCount == 3
               && selected.suppressedCount == 2);
    expect("diagnostic publication retains highest severity in stable order",
           selected.diagnostics.size() == 3
               && selected.diagnostics.at(0).message
                      == QStringLiteral("error-first")
               && selected.diagnostics.at(1).message
                      == QStringLiteral("error-second")
               && selected.diagnostics.at(2).message
                      == QStringLiteral("warning-first"));
}

void runAnalysisRuntimeEnableDisableAndPublicationLimit()
{
    QTemporaryDir directory;
    expect("analysis runtime policy fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileName =
        directory.filePath(QStringLiteral("runtime_policy.sv"));
    const QString initialSource =
        QStringLiteral("module runtime_policy; endmodule\n");
    QString diagnosticSource =
        QStringLiteral(
            "`default_nettype none\n"
            "module runtime_policy;\n");
    for (int index = 0; index < 8; ++index) {
        diagnosticSource += QStringLiteral(
            "  assign unresolved_%1 = missing_%1;\n").arg(index);
    }
    diagnosticSource += QStringLiteral(
        "endmodule\n"
        "`default_nettype wire\n");
    QFile file(fileName);
    expect("analysis runtime policy fixture writes",
           file.open(QIODevice::WriteOnly | QIODevice::Text)
               && file.write(initialSource.toUtf8())
                      == initialSource.toUtf8().size());
    file.close();

    SemanticIndex::getInstance()->clearSemanticState();
    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    SemanticAnalysisRuntimePolicy policy;
    policy.enabled = false;
    policy.planningMode =
        SemanticAnalysisPlanningMode::FullWorkspace;
    policy.maxDiagnostics = 2;
    scheduler.setSemanticAnalysisRuntimePolicy(policy);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);

    QSignalSpy workerStartedSpy(
        &analyzer,
        &SymbolAnalyzer::analysisStarted);
    int suppressedSchedulingRequests = 0;
    int publicationProduced = 0;
    int publicationPublished = 0;
    int publicationSuppressed = 0;
    bool publicationInvokedSlang = false;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisTelemetry,
        &scheduler,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            if (telemetry.stage == SemanticAnalysisStage::Scheduling
                && telemetry.detail.contains(
                    QStringLiteral("policy=disabled"))) {
                ++suppressedSchedulingRequests;
            }
            if (telemetry.stage == SemanticAnalysisStage::Publication) {
                publicationProduced = telemetry.diagnosticsProduced;
                publicationPublished = telemetry.diagnosticsPublished;
                publicationSuppressed = telemetry.diagnosticsSuppressed;
                publicationInvokedSlang = telemetry.slangInvoked;
            }
        });

    MyCodeEditor editor;
    editor.setPlainText(initialSource);
    documents.registerEditor(&editor, fileName);
    editor.setPlainText(diagnosticSource);
    documents.markSaved(&editor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    expect("disabled analysis starts no saved-file Slang worker",
           workerStartedSpy.isEmpty()
               && !scheduler.isSemanticAnalysisActive()
               && suppressedSchedulingRequests >= 2);
    const DocumentSemanticStatus disabledStatus =
        scheduler.semanticStatus(fileName);
    expect("disabled analysis leaves clean document in explicit stable state",
           disabledStatus.state == DocumentSemanticState::Stale
               && disabledStatus.state != DocumentSemanticState::Queued
               && disabledStatus.state != DocumentSemanticState::Analyzing);

    ProjectSnapshot project;
    project.workspaceRoot = directory.path();
    project.systemVerilogFiles = {fileName};
    project.allFiles = project.systemVerilogFiles;
    project.includeDirs = {directory.path()};

    std::atomic_bool releaseWorker{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&releaseWorker](const std::function<bool()>& isCancelled) {
            while (!releaseWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });
    policy.enabled = true;
    scheduler.setSemanticAnalysisRuntimePolicy(policy);
    QSignalSpy expiredSpy(
        &analyzer,
        &SymbolAnalyzer::workspaceAnalysisExpired);
    scheduler.requestWorkspaceAnalysis(project);
    expect("re-enabled explicit request starts semantic worker",
           waitUntil([&]() { return workerStartedSpy.size() == 1; },
                     3000));
    const std::uint64_t revisionBeforeDisable =
        SemanticIndex::getInstance()->snapshotRevision();
    policy.enabled = false;
    scheduler.setSemanticAnalysisRuntimePolicy(policy);
    releaseWorker.store(true, std::memory_order_relaxed);
    expect("disabling active analysis expires its generation",
           waitUntil([&]() { return !expiredSpy.isEmpty(); }, 5000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expect("expired disabled generation cannot publish",
           SemanticIndex::getInstance()->snapshotRevision()
                   == revisionBeforeDisable
               && !scheduler.isSemanticAnalysisActive()
               && scheduler.semanticStatus(fileName).state
                      == DocumentSemanticState::Stale);

    policy.enabled = true;
    scheduler.setSemanticAnalysisRuntimePolicy(policy);
    QSignalSpy finishedSpy(
        &scheduler,
        &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    IncrementalAnalysisPlan publishedPlan;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisPlanPrepared,
        &scheduler,
        [&publishedPlan](const IncrementalAnalysisPlan& plan) {
            publishedPlan = plan;
        });
    scheduler.requestWorkspaceAnalysis(project);
    expect("subsequent legal request publishes after re-enable",
           waitUntil([&]() { return !finishedSpy.isEmpty(); }, 15000));
    expect("runtime policy reaches worker as authoritative full plan",
           publishedPlan.fullWorkspace
               && publishedPlan.authoritativeWorkspaceReplace);
    const QList<SemanticDiagnostic> publishedDiagnostics =
        SemanticIndex::getInstance()->getDiagnostics();
    expect("diagnostic limit applies only at publication after full Slang analysis",
           publicationInvokedSlang
               && publicationProduced > publicationPublished
               && publicationPublished == 2
               && publicationSuppressed
                      == publicationProduced - publicationPublished
               && publishedDiagnostics.size() == 2);
    scheduler.shutdown();
    SemanticIndex::getInstance()->clearSemanticState();
}

void runEditDoesNotScheduleSemanticWork()
{
    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral(
        "module scheduler_edit_guard;\n"
        "  logic value;\n"
        "endmodule\n"));
    const QString fileName =
        QDir::temp().absoluteFilePath(QStringLiteral("scheduler_edit_guard.sv"));
    documents.registerEditor(&editor, fileName);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);

    QSignalSpy completionSpy(&analyzer, &SymbolAnalyzer::analysisCompleted);
    QSignalSpy workerStartSpy(&analyzer, &SymbolAnalyzer::analysisStarted);
    QSignalSpy workspaceRestartSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisStarted);
    const std::uint64_t snapshotRevisionBefore =
        SemanticIndex::getInstance()->snapshotRevision();

    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    editor.insertPlainText(QStringLiteral("x"));
    QString triviaBurst;
    for (int index = 0; index < 100; ++index)
        triviaBurst += QStringLiteral("\n  // scheduler trivia %1  ").arg(index);
    editor.insertPlainText(triviaBurst);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    expect("ordinary edit does not create relationship debounce",
           !scheduler.hasScheduledRelationshipAnalysis(fileName));
    waitForDuration(2200);
    expect("ordinary edit starts no delayed semantic worker",
           workerStartSpy.isEmpty() && completionSpy.isEmpty());
    expect("ordinary edit causes no workspace restart",
           workspaceRestartSpy.isEmpty());
    expect("ordinary edit publishes no semantic snapshot",
           SemanticIndex::getInstance()->snapshotRevision()
               == snapshotRevisionBefore);
    scheduler.shutdown();
}

void runPublicationRefreshesEditorOnce()
{
    QTemporaryDir directory;
    expect("single-refresh fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileName =
        directory.filePath(QStringLiteral("single_refresh.sv"));
    QFile file(fileName);
    expect("single-refresh fixture opens",
           file.open(QIODevice::WriteOnly | QIODevice::Text));
    if (!file.isOpen())
        return;
    file.write("module single_refresh; endmodule\n");
    file.close();

    QTabWidget tabWidget;
    TabManager tabs(&tabWidget);
    expect("single-refresh fixture opens in tab",
           tabs.openFileInTab(fileName));
    MyCodeEditor* editor = tabs.getCurrentEditor();
    expect("single-refresh fixture has editor", editor != nullptr);
    if (!editor)
        return;

    AnalysisScheduler scheduler;
    AnalysisCoordinator coordinator(&scheduler,
                                    nullptr,
                                    nullptr,
                                    &tabs,
                                    nullptr,
                                    nullptr);
    coordinator.connectSignals();
    editor->resetHotPathMetricsForTest();

    scheduler.documentRefreshRequested(fileName);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expect("one semantic publication performs one full ghost query",
           editor->hotPathMetricsForTest().fullGhostQueries == 1);
    scheduler.shutdown();
}

void runIncludeResolutionUsesConfiguredSearchOrder()
{
    QTemporaryDir directory;
    expect("include-resolution fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;

    QDir root(directory.path());
    expect("include-resolution fixture directories are created",
           root.mkpath(QStringLiteral("inc_a"))
               && root.mkpath(QStringLiteral("inc_b"))
               && root.mkpath(QStringLiteral("src")));
    const QString decoyHeader =
        root.filePath(QStringLiteral("inc_a/common.svh"));
    const QString expectedHeader =
        root.filePath(QStringLiteral("inc_b/common.svh"));
    const QString currentFile =
        root.filePath(QStringLiteral("src/top.sv"));
    const auto writeSource = [](const QString& fileName,
                                const QByteArray& content) {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        return file.write(content) == content.size();
    };
    expect("include-resolution fixture files are written",
           writeSource(decoyHeader, "`define WHICH_A 1\n")
               && writeSource(expectedHeader, "`define WHICH_B 1\n")
               && writeSource(currentFile,
                              "`include \"common.svh\"\nmodule top; endmodule\n"));

    WorkspaceManager workspace;
    workspace.setRecentWorkspacePersistenceEnabledForTesting(false);
    expect("include-resolution workspace opens",
           workspace.openWorkspace(directory.path()));
    WorkspaceConfiguration configuration = workspace.workspaceConfiguration();
    configuration.includeDirs = {QFileInfo(expectedHeader).absolutePath()};
    expect("include-resolution configuration applies",
           workspace.setWorkspaceConfiguration(configuration));
    expect("include-resolution scan state restores",
           workspace.restoreSessionScanState(
               {decoyHeader, expectedHeader, currentFile}, true));

    expect("configured include directory wins over basename fallback",
           EditorFileIdentity::same(
               workspace.resolveIncludePath(QStringLiteral("common.svh"),
                                            currentFile),
               expectedHeader));
    workspace.closeWorkspace();
}

void runDocumentOpenRequiresMatchingIndexedText()
{
    QTemporaryDir directory;
    expect("document-open content fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;

    const QString matchingFile =
        directory.filePath(QStringLiteral("matching.sv"));
    const QString staleFile = directory.filePath(QStringLiteral("stale.sv"));
    const QString uncachedFile =
        directory.filePath(QStringLiteral("uncached.sv"));
    const QString matchingText =
        QStringLiteral("module matching; endmodule\n");
    const QString indexedStaleText =
        QStringLiteral("module stale; logic old_value; endmodule\n");
    const QString openedStaleText =
        QStringLiteral("module stale; logic new_value; endmodule\n");
    const QString uncachedText =
        QStringLiteral("module uncached; logic value; endmodule\n");
    for (const auto& source : QList<QPair<QString, QString>>{
             {matchingFile, matchingText},
             {staleFile, openedStaleText},
             {uncachedFile, uncachedText}}) {
        QFile file(source.first);
        expect("document-open fixture source opens",
               file.open(QIODevice::WriteOnly | QIODevice::Text));
        if (file.isOpen()) {
            file.write(source.second.toUtf8());
            file.close();
        }
    }

    const auto installIndexedText = [&](const QString& fileName,
                                        const QString& text,
                                        const QString& moduleName) {
        const SemanticSymbolRecord module =
            SemanticFixtureRecordBuilder(
                moduleName,
                SymbolTaxonomy::DeclarationKind::Module)
                .withFile(fileName)
                .withLocalHandle(1)
                .withTextSpan(7, moduleName.size())
                .record();
        SemanticIndex::getInstance()->installPreparedSnapshot(
            std::make_shared<const SemanticIndexSnapshot>(
                SemanticIndexSnapshot::fromSymbolRecords(
                    {module}, {}, {}, {{fileName, text}})),
            {fileName});
    };

    SemanticIndex::getInstance()->clearSemanticState();
    installIndexedText(matchingFile,
                       matchingText,
                       QStringLiteral("matching"));
    {
        AnalysisScheduler scheduler;
        SymbolAnalyzer analyzer;
        DocumentModel documents;
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setDocumentModel(&documents);
        int documentOpenRequests = 0;
        QObject::connect(
            &scheduler,
            &AnalysisScheduler::semanticAnalysisTelemetry,
            &scheduler,
            [&](const SemanticAnalysisTelemetry& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Scheduling
                    && telemetry.reason
                        == SemanticAnalysisReason::DocumentOpen) {
                    ++documentOpenRequests;
                }
            });
        QSignalSpy workerStartSpy(&analyzer, &SymbolAnalyzer::analysisStarted);
        MyCodeEditor editor;
        editor.setPlainText(matchingText);
        documents.registerEditor(&editor, matchingFile);
        waitForDuration(100);
        expect("matching indexed text opens as Current",
               scheduler.semanticStatus(matchingFile).state
                   == DocumentSemanticState::Current);
        expect("matching indexed text schedules no DocumentOpen analysis",
               documentOpenRequests == 0 && workerStartSpy.isEmpty());
        scheduler.shutdown();
    }

    SemanticIndex::getInstance()->clearSemanticState();
    installIndexedText(staleFile,
                       indexedStaleText,
                       QStringLiteral("stale"));
    {
        AnalysisScheduler scheduler;
        SymbolAnalyzer analyzer;
        DocumentModel documents;
        std::atomic_bool releaseWorker{false};
        analyzer.setWorkspaceWorkerStartGateForTesting(
            [&releaseWorker](const std::function<bool()>& isCancelled) {
                while (!releaseWorker.load(std::memory_order_relaxed)
                       && !isCancelled()) {
                    QThread::msleep(2);
                }
            });
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setDocumentModel(&documents);
        int documentOpenRequests = 0;
        QObject::connect(
            &scheduler,
            &AnalysisScheduler::semanticAnalysisTelemetry,
            &scheduler,
            [&](const SemanticAnalysisTelemetry& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Scheduling
                    && telemetry.reason
                        == SemanticAnalysisReason::DocumentOpen) {
                    ++documentOpenRequests;
                }
            });
        MyCodeEditor editor;
        editor.setPlainText(openedStaleText);
        documents.registerEditor(&editor, staleFile);
        waitForDuration(100);
        expect("mismatching indexed text is never exposed as Current",
               scheduler.semanticStatus(staleFile).state
                   != DocumentSemanticState::Current);
        expect("mismatching indexed text schedules exactly one DocumentOpen analysis",
               documentOpenRequests == 1);
        releaseWorker.store(true, std::memory_order_relaxed);
        scheduler.cancelWorkspaceAnalysis();
        scheduler.shutdown();
    }


    SemanticIndex::getInstance()->clearSemanticState();
    installIndexedText(staleFile,
                       indexedStaleText,
                       QStringLiteral("stale"));
    {
        AnalysisScheduler scheduler;
        SymbolAnalyzer analyzer;
        DocumentModel documents;
        std::atomic_bool releaseWorker{false};
        analyzer.setWorkspaceWorkerStartGateForTesting(
            [&releaseWorker](const std::function<bool()>& isCancelled) {
                while (!releaseWorker.load(std::memory_order_relaxed)
                       && !isCancelled()) {
                    QThread::msleep(2);
                }
            });
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setDocumentModel(&documents);
        ProjectSnapshot project;
        project.workspaceRoot = directory.path();
        project.systemVerilogFiles = {staleFile};
        project.allFiles = project.systemVerilogFiles;
        project.includeDirs = {directory.path()};
        int documentOpenRequests = 0;
        QObject::connect(
            &scheduler,
            &AnalysisScheduler::semanticAnalysisTelemetry,
            &scheduler,
            [&](const SemanticAnalysisTelemetry& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Scheduling
                    && telemetry.reason
                        == SemanticAnalysisReason::DocumentOpen) {
                    ++documentOpenRequests;
                }
            });
        QSignalSpy workerStartSpy(&analyzer, &SymbolAnalyzer::analysisStarted);
        scheduler.requestWorkspaceAnalysis(project);
        expect("active-workspace DocumentOpen fixture starts baseline worker",
               waitUntil([&]() { return !workerStartSpy.isEmpty(); }, 3000));
        MyCodeEditor editor;
        editor.setPlainText(openedStaleText);
        documents.registerEditor(&editor, staleFile);
        expect("mismatching open during active workspace queues one explicit DocumentOpen",
               documentOpenRequests == 1
                   && scheduler.semanticStatus(staleFile).state
                       != DocumentSemanticState::Current);
        releaseWorker.store(true, std::memory_order_relaxed);
        expect("explicit DocumentOpen publishes the opened revision after superseding workspace work",
               waitUntil(
                   [&]() {
                       return scheduler.semanticStatus(staleFile).state
                                  == DocumentSemanticState::Current
                           && SemanticIndex::getInstance()
                                  ->getCachedFileContent(staleFile)
                               == openedStaleText;
                   },
                   10000));
        expect("active-workspace mismatch still schedules exactly one DocumentOpen request",
               documentOpenRequests == 1);
        scheduler.cancelWorkspaceAnalysis();
        scheduler.shutdown();
    }

    SemanticIndex::getInstance()->clearSemanticState();
    {
        AnalysisScheduler scheduler;
        SymbolAnalyzer analyzer;
        DocumentModel documents;
        std::atomic_bool releaseWorker{false};
        analyzer.setWorkspaceWorkerStartGateForTesting(
            [&releaseWorker](const std::function<bool()>& isCancelled) {
                while (!releaseWorker.load(std::memory_order_relaxed)
                       && !isCancelled()) {
                    QThread::msleep(2);
                }
            });
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setDocumentModel(&documents);
        ProjectSnapshot project;
        project.workspaceRoot = directory.path();
        project.systemVerilogFiles = {uncachedFile};
        project.allFiles = project.systemVerilogFiles;
        project.includeDirs = {directory.path()};
        int documentOpenRequests = 0;
        QObject::connect(
            &scheduler,
            &AnalysisScheduler::semanticAnalysisTelemetry,
            &scheduler,
            [&](const SemanticAnalysisTelemetry& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Scheduling
                    && telemetry.reason
                        == SemanticAnalysisReason::DocumentOpen) {
                    ++documentOpenRequests;
                }
            });
        QSignalSpy workerStartSpy(&analyzer, &SymbolAnalyzer::analysisStarted);
        scheduler.requestWorkspaceAnalysis(project);
        expect("uncached active-workspace fixture starts baseline worker",
               waitUntil([&]() { return !workerStartSpy.isEmpty(); }, 3000));
        MyCodeEditor editor;
        editor.setPlainText(uncachedText);
        documents.registerEditor(&editor, uncachedFile);
        const DocumentSemanticStatus boundQueued =
            scheduler.semanticStatus(uncachedFile);
        expect("uncached open binds its revision to the active workspace generation",
               boundQueued.state == DocumentSemanticState::Queued
                   && boundQueued.analysisGeneration > 0
                   && boundQueued.documentRevision
                       == static_cast<std::uint64_t>(
                           documents.documentForFile(uncachedFile)
                               .textVersion));
        expect("covered uncached open schedules no duplicate DocumentOpen analysis",
               documentOpenRequests == 0);
        releaseWorker.store(true, std::memory_order_relaxed);
        expect("covered uncached open converges to Current after active worker publication",
               waitUntil(
                   [&]() {
                       const DocumentSemanticStatus status =
                           scheduler.semanticStatus(uncachedFile);
                       return status.state == DocumentSemanticState::Current
                           && status.documentRevision
                               == static_cast<std::uint64_t>(
                                   documents.documentForFile(uncachedFile)
                                       .textVersion)
                           && SemanticIndex::getInstance()
                                  ->getCachedFileContent(uncachedFile)
                               == uncachedText;
                   },
                   10000));
        expect("uncached active-workspace completion retains zero duplicate requests",
               documentOpenRequests == 0);
        scheduler.cancelWorkspaceAnalysis();
        scheduler.shutdown();
    }
}

void runWorkspaceEditDoesNotRestartWorker()
{
    QTemporaryDir directory;
    expect("workspace scheduler fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileName = directory.filePath(QStringLiteral("top.sv"));
    QFile file(fileName);
    expect("workspace scheduler fixture opens",
           file.open(QIODevice::WriteOnly | QIODevice::Text));
    if (!file.isOpen())
        return;
    file.write("module top; endmodule\n");
    file.close();

    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral("module top; endmodule\n"));
    documents.registerEditor(&editor, fileName);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);

    std::atomic_bool releaseWorker{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&releaseWorker](const std::function<bool()>& isCancelled) {
            while (!releaseWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });

    ProjectSnapshot project;
    project.workspaceRoot = directory.path();
    project.systemVerilogFiles = {fileName};
    project.allFiles = project.systemVerilogFiles;

    QSignalSpy startedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisStarted);
    QSignalSpy queuedSpy(
        &scheduler, &AnalysisScheduler::workspaceAnalysisRequestQueued);
    scheduler.requestWorkspaceAnalysis(project);
    expect("workspace worker starts",
           waitUntil([&startedSpy]() { return !startedSpy.isEmpty(); }, 1000));

    editor.insertPlainText(QStringLiteral("\n"));
    waitForDuration(150);
    expect("editing while worker runs does not queue workspace restart",
           queuedSpy.isEmpty());

    releaseWorker.store(true, std::memory_order_relaxed);
    scheduler.cancelWorkspaceAnalysis();
    scheduler.shutdown();
}

void runSupersededRequestsConvergeDocumentStates()
{
    QTemporaryDir directory;
    expect("superseded-state fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileA = directory.filePath(QStringLiteral("a.sv"));
    const QString fileB = directory.filePath(QStringLiteral("b.sv"));
    const QString fileC = directory.filePath(QStringLiteral("c.sv"));
    const QString textA = QStringLiteral("module a; endmodule\n");
    const QString textB = QStringLiteral("module b; endmodule\n");
    const QString textC = QStringLiteral("module c; endmodule\n");
    for (const auto& source : QList<QPair<QString, QString>>{
             {fileA, textA}, {fileB, textB}, {fileC, textC}}) {
        QFile file(source.first);
        expect("superseded-state source opens",
               file.open(QIODevice::WriteOnly | QIODevice::Text));
        if (file.isOpen()) {
            file.write(source.second.toUtf8());
            file.close();
        }
    }
    auto moduleRecord = [](const QString& fileName,
                           const QString& name,
                           int handle) {
        return SemanticFixtureRecordBuilder(
                   name, SymbolTaxonomy::DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(handle)
            .withTextSpan(7, name.size())
            .record();
    };
    SemanticIndex::getInstance()->installPreparedSnapshot(
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {moduleRecord(fileA, QStringLiteral("a"), 1),
                 moduleRecord(fileB, QStringLiteral("b"), 2),
                 moduleRecord(fileC, QStringLiteral("c"), 3)},
                {},
                {},
                {{fileA, textA}, {fileB, textB}, {fileC, textC}})),
        {fileA, fileB, fileC});

    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor editorA;
    MyCodeEditor editorB;
    MyCodeEditor editorC;
    editorA.setPlainText(textA);
    editorB.setPlainText(textB);
    editorC.setPlainText(textC);
    documents.registerEditor(&editorA, fileA);
    documents.registerEditor(&editorB, fileB);
    documents.registerEditor(&editorC, fileC);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    ProjectModel projectModel;
    projectModel.setWorkspaceState(
        directory.path(), {fileA, fileB, fileC});
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    scheduler.setProjectModel(&projectModel);
    expect("superseded-state workspace baseline completes",
           waitUntil([&]() { return finishedSpy.size() == 1; }, 10000));

    std::atomic_bool releaseWorker{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&releaseWorker](const std::function<bool()>& isCancelled) {
            while (!releaseWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });

    editorA.insertPlainText(QStringLiteral("logic changed_a;\n"));
    documents.markSaved(&editorA);
    editorB.insertPlainText(QStringLiteral("logic changed_b;\n"));
    documents.markSaved(&editorB);
    editorC.insertPlainText(QStringLiteral("logic dirty_c;\n"));
    releaseWorker.store(true, std::memory_order_relaxed);
    expect("editing unrelated C does not strand coalesced clean saves",
           waitUntil(
               [&]() {
                   const DocumentSemanticState stateA =
                       scheduler.semanticStatus(fileA).state;
                   const DocumentSemanticState stateB =
                       scheduler.semanticStatus(fileB).state;
                   return stateA == DocumentSemanticState::Current
                       && stateB == DocumentSemanticState::Current;
               },
               10000));
    expect("superseded save A is carried into the latest request",
           scheduler.semanticStatus(fileA).state
                   == DocumentSemanticState::Current
               && SemanticIndex::getInstance()->getCachedFileContent(fileA)
                   == editorA.toPlainText());
    expect("latest save B publishes with coalesced save A",
           scheduler.semanticStatus(fileB).state
                   == DocumentSemanticState::Current
               && SemanticIndex::getInstance()->getCachedFileContent(fileB)
                   == editorB.toPlainText());
    expect("editing C remains Dirty",
           scheduler.semanticStatus(fileC).state
               == DocumentSemanticState::Dirty);

    std::atomic_int mergedGateEntries{0};
    std::atomic_bool releaseContinuation{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&mergedGateEntries, &releaseContinuation](
            const std::function<bool()>& isCancelled) {
            const int entry = mergedGateEntries.fetch_add(
                                  1, std::memory_order_relaxed)
                + 1;
            while (!isCancelled()
                   && (entry < 3
                       || !releaseContinuation.load(
                           std::memory_order_relaxed))) {
                QThread::msleep(2);
            }
        });
    editorA.insertPlainText(QStringLiteral("logic saved_then_dirty_a;\n"));
    documents.markSaved(&editorA);
    expect("first save enters the active worker before merged continuation test",
           waitUntil(
               [&]() {
                   return mergedGateEntries.load(std::memory_order_relaxed)
                       >= 1;
               },
               3000));
    editorB.insertPlainText(QStringLiteral("logic latest_clean_b;\n"));
    documents.markSaved(&editorB);
    expect("coalesced A+B request becomes active",
           waitUntil(
               [&]() {
                   return mergedGateEntries.load(std::memory_order_relaxed)
                       >= 2;
               },
               5000));
    editorA.insertPlainText(QStringLiteral("// unsaved A edit\n"));
    expect("editing A requeues remaining clean B from active merged request",
           waitUntil(
               [&]() {
                   return mergedGateEntries.load(std::memory_order_relaxed)
                       >= 3;
               },
               5000));
    releaseContinuation.store(true, std::memory_order_relaxed);
    expect("remaining clean B publishes after merged A is invalidated",
           waitUntil(
               [&]() {
                   return scheduler.semanticStatus(fileB).state
                              == DocumentSemanticState::Current
                       && SemanticIndex::getInstance()
                                  ->getCachedFileContent(fileB)
                              == editorB.toPlainText();
               },
               10000));
    expect("edited member A remains Dirty after B continuation publishes",
           scheduler.semanticStatus(fileA).state
               == DocumentSemanticState::Dirty);
    scheduler.cancelWorkspaceAnalysis();
    scheduler.shutdown();

    const QString sameFile =
        directory.filePath(QStringLiteral("same_file.sv"));
    const QString sameText =
        QStringLiteral("module same_file; endmodule\n");
    QFile sameSource(sameFile);
    expect("same-file generation fixture opens",
           sameSource.open(QIODevice::WriteOnly | QIODevice::Text));
    if (sameSource.isOpen()) {
        sameSource.write(sameText.toUtf8());
        sameSource.close();
    }
    SemanticIndex::getInstance()->installPreparedSnapshot(
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {moduleRecord(sameFile,
                              QStringLiteral("same_file"),
                              1)},
                {},
                {},
                {{sameFile, sameText}})),
        {sameFile});
    AnalysisScheduler sameScheduler;
    SymbolAnalyzer sameAnalyzer;
    DocumentModel sameDocuments;
    MyCodeEditor sameEditor;
    sameEditor.setPlainText(sameText);
    sameDocuments.registerEditor(&sameEditor, sameFile);
    std::atomic_bool releaseSameWorker{false};
    sameAnalyzer.setWorkspaceWorkerStartGateForTesting(
        [&releaseSameWorker](const std::function<bool()>& isCancelled) {
            while (!releaseSameWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });
    sameScheduler.setSymbolAnalyzer(&sameAnalyzer);
    sameScheduler.setDocumentModel(&sameDocuments);
    QList<std::uint64_t> saveGenerations;
    QObject::connect(
        &sameScheduler,
        &AnalysisScheduler::semanticAnalysisTelemetry,
        &sameScheduler,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            if (telemetry.stage == SemanticAnalysisStage::Scheduling
                && telemetry.reason == SemanticAnalysisReason::Save) {
                saveGenerations.append(telemetry.generation);
            }
        });
    sameEditor.insertPlainText(QStringLiteral("logic first;\n"));
    sameDocuments.markSaved(&sameEditor);
    sameEditor.insertPlainText(QStringLiteral("logic second;\n"));
    sameDocuments.markSaved(&sameEditor);
    const DocumentSemanticStatus newestQueued =
        sameScheduler.semanticStatus(sameFile);
    expect("superseded generation callback cannot overwrite newer queued state",
           saveGenerations.size() == 2
               && newestQueued.analysisGeneration == saveGenerations.last()
               && (newestQueued.state == DocumentSemanticState::Queued
                   || newestQueued.state
                       == DocumentSemanticState::Analyzing));
    releaseSameWorker.store(true, std::memory_order_relaxed);
    expect("latest same-file save completes as Current",
           waitUntil(
               [&]() {
                   return sameScheduler.semanticStatus(sameFile).state
                       == DocumentSemanticState::Current;
               },
               10000));
    waitForDuration(200);
    expect("late old-generation events cannot overwrite latest Current state",
           sameScheduler.semanticStatus(sameFile).state
                   == DocumentSemanticState::Current
               && sameScheduler.semanticStatus(sameFile).analysisGeneration
                   == saveGenerations.last());
    sameScheduler.cancelWorkspaceAnalysis();
    sameScheduler.shutdown();
}

void runSinglePendingCleanChangeMergesIntoDifferentRequest()
{
    QTemporaryDir directory;
    expect("single-pending fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;

    const QString pendingFile =
        directory.filePath(QStringLiteral("pending.sv"));
    const QString requestedFile =
        directory.filePath(QStringLiteral("requested.sv"));
    const QString pendingText =
        QStringLiteral("module pending; logic changed; endmodule\n");
    const QString requestedText =
        QStringLiteral("module requested; endmodule\n");
    for (const auto& source : QList<QPair<QString, QString>>{
             {pendingFile, pendingText}, {requestedFile, requestedText}}) {
        QFile file(source.first);
        expect("single-pending source opens",
               file.open(QIODevice::WriteOnly | QIODevice::Text));
        if (file.isOpen()) {
            file.write(source.second.toUtf8());
            file.close();
        }
    }

    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    scheduler.setSymbolAnalyzer(&analyzer);
    ProjectSnapshot project;
    project.workspaceRoot = directory.path();
    project.systemVerilogFiles = {pendingFile, requestedFile};
    project.allFiles = project.systemVerilogFiles;

    QStringList scheduledFiles;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisTelemetry,
        &scheduler,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            if (telemetry.stage == SemanticAnalysisStage::Scheduling
                && telemetry.reason == SemanticAnalysisReason::Save) {
                scheduledFiles = telemetry.changedFiles;
            }
        });
    AnalysisSchedulerTestAccess::rememberPendingCleanChange(
        scheduler, pendingFile, pendingText);
    AnalysisSchedulerTestAccess::requestSave(
        scheduler, requestedFile, project);
    expect("one pending clean file is merged into a different request",
           containsFile(scheduledFiles, pendingFile)
               && containsFile(scheduledFiles, requestedFile));
    scheduler.shutdown();
}

void runSaveQueuesExactlyOnceAndRejectsStaleRevision()
{
    QTemporaryDir directory;
    expect("save scheduler fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileName = directory.filePath(QStringLiteral("saved.sv"));
    QFile file(fileName);
    expect("save scheduler fixture opens",
           file.open(QIODevice::WriteOnly | QIODevice::Text));
    if (!file.isOpen())
        return;
    file.write("module saved; logic value; endmodule\n");
    file.close();

    SemanticIndex::getInstance()->clearSemanticState();
    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral(
        "module saved; logic value; endmodule\n"));
    documents.registerEditor(&editor, fileName);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);

    int saveSchedulingCount = 0;
    int publicationCount = 0;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisTelemetry,
        &scheduler,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            if (telemetry.stage == SemanticAnalysisStage::Scheduling
                && telemetry.reason == SemanticAnalysisReason::Save) {
                ++saveSchedulingCount;
            }
            if (telemetry.stage == SemanticAnalysisStage::Publication)
                ++publicationCount;
        });
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);

    std::atomic_bool releaseFirstWorker{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&releaseFirstWorker](const std::function<bool()>& isCancelled) {
            while (!releaseFirstWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });

    editor.insertPlainText(QStringLiteral("\n"));
    QElapsedTimer saveTimer;
    saveTimer.start();
    documents.markSaved(&editor);
    const qint64 saveHandlerMs = saveTimer.elapsed();
    expect("documentSaved queues exactly one save request",
           saveSchedulingCount == 1);
    expect("save handler returns without waiting for worker",
           saveHandlerMs < 100);
    expect("saved document enters analyzing state",
           scheduler.semanticStatus(fileName).state
               == DocumentSemanticState::Analyzing);

    scheduler.handleExternalFileChanged(fileName, 20);
    waitForDuration(100);
    expect("self-write watcher event is deduplicated",
           saveSchedulingCount == 1);
    QTabWidget responsivenessTabs;
    QWidget firstPage;
    QWidget secondPage;
    responsivenessTabs.addTab(&firstPage, QStringLiteral("first"));
    responsivenessTabs.addTab(&secondPage, QStringLiteral("second"));
    bool uiActionsProcessed = false;
    QTimer::singleShot(0, &scheduler, [&]() {
        editor.verticalScrollBar()->setValue(
            editor.verticalScrollBar()->maximum());
        responsivenessTabs.setCurrentIndex(1);
        uiActionsProcessed = true;
    });
    expect("scrolling and tab switching remain responsive during analysis",
           waitUntil([&]() { return uiActionsProcessed; }, 500)
               && responsivenessTabs.currentIndex() == 1);

    releaseFirstWorker.store(true, std::memory_order_relaxed);
    expect("save request completes in background",
           waitUntil([&finishedSpy]() { return finishedSpy.size() == 1; },
                     10000));
    expect("save publishes one immutable snapshot",
           publicationCount == 1);
    expect("save publishes a worker-prepared analysis band report",
           SemanticIndex::getInstance()->hasPreparedAnalysisBandReport());
    const SemanticAnalysisBandReport preparedBandReport =
        SemanticIndex::getInstance()->analysisBandReport();
    expect("prepared analysis band report covers the saved file",
           preparedBandReport.totalFileCount == 1
               && preparedBandReport.totalSymbolCount > 0);
    expect("completed saved revision is current",
           scheduler.semanticStatus(fileName).state
               == DocumentSemanticState::Current);

    const std::uint64_t revisionBeforeStaleRequest =
        SemanticIndex::getInstance()->snapshotRevision();
    std::atomic_bool releaseStaleWorker{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&releaseStaleWorker](const std::function<bool()>& isCancelled) {
            while (!releaseStaleWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });
    editor.insertPlainText(QStringLiteral("logic late_value;\n"));
    documents.markSaved(&editor);
    expect("second save creates one additional request",
           saveSchedulingCount == 2);
    QElapsedTimer editResponsivenessTimer;
    editResponsivenessTimer.start();
    editor.insertPlainText(QStringLiteral("\n"));
    expect("typing remains responsive while stale worker is cancelled",
           editResponsivenessTimer.elapsed() < 100);
    waitForDuration(100);
    releaseStaleWorker.store(true, std::memory_order_relaxed);
    waitForDuration(300);
    expect("edit during analysis rejects old snapshot publication",
           SemanticIndex::getInstance()->snapshotRevision()
               == revisionBeforeStaleRequest);
    expect("edit during analysis leaves semantic state dirty",
           scheduler.semanticStatus(fileName).state
               == DocumentSemanticState::Dirty);
    expect("rejected stale request emits no publication telemetry",
           publicationCount == 1);

    scheduler.cancelWorkspaceAnalysis();
    scheduler.shutdown();
}

void runExternalChangesConvergeAndCoalesce()
{
    QTemporaryDir directory;
    expect("external-change fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;

    const auto writeSource = [](const QString& fileName,
                                const QString& text) {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        return file.write(text.toUtf8()) == text.toUtf8().size();
    };
    const QString staleFile =
        directory.filePath(QStringLiteral("external_stale.sv"));
    const QString staleOld =
        QStringLiteral("module external_stale; logic old_value; endmodule\n");
    const QString staleNew =
        QStringLiteral("module external_stale; logic new_value; endmodule\n");
    expect("external stale source writes baseline",
           writeSource(staleFile, staleOld));
    const SemanticSymbolRecord staleModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("external_stale"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(staleFile)
            .withLocalHandle(1)
            .withTextSpan(7, 14)
            .record();
    SemanticIndex::getInstance()->installPreparedSnapshot(
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {staleModule}, {}, {}, {{staleFile, staleOld}})),
        {staleFile});
    {
        AnalysisScheduler scheduler;
        SymbolAnalyzer analyzer;
        DocumentModel documents;
        MyCodeEditor editor;
        editor.setPlainText(staleOld);
        documents.registerEditor(&editor, staleFile);
        scheduler.setSymbolAnalyzer(&analyzer);
        scheduler.setDocumentModel(&documents);
        expect("external stale source writes changed disk text",
               writeSource(staleFile, staleNew));
        scheduler.handleExternalFileChanged(staleFile, 0);
        expect("external change publishes disk text",
               waitUntil(
                   [&]() {
                       return SemanticIndex::getInstance()
                                  ->getCachedFileContent(staleFile)
                           == staleNew;
                   },
                   10000));
        expect("external change with an unreloaded clean editor converges Stale",
               scheduler.semanticStatus(staleFile).state
                   == DocumentSemanticState::Stale);
        scheduler.shutdown();
    }

    const QString fileA =
        directory.filePath(QStringLiteral("external_a.sv"));
    const QString fileB =
        directory.filePath(QStringLiteral("external_b.sv"));
    const QString oldA = QStringLiteral("module external_a; endmodule\n");
    const QString oldB = QStringLiteral("module external_b; endmodule\n");
    const QString newA =
        QStringLiteral("module external_a; logic changed_a; endmodule\n");
    const QString newB =
        QStringLiteral("module external_b; logic changed_b; endmodule\n");
    expect("rapid external source A writes baseline",
           writeSource(fileA, oldA));
    expect("rapid external source B writes baseline",
           writeSource(fileB, oldB));
    ProjectModel projectModel;
    projectModel.setWorkspaceState(directory.path(), {fileA, fileB});
    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    scheduler.setProjectModel(&projectModel);
    expect("rapid external fixture baseline completes",
           waitUntil([&]() { return finishedSpy.size() == 1; }, 10000));

    std::atomic_bool releaseWorker{false};
    analyzer.setWorkspaceWorkerStartGateForTesting(
        [&releaseWorker](const std::function<bool()>& isCancelled) {
            while (!releaseWorker.load(std::memory_order_relaxed)
                   && !isCancelled()) {
                QThread::msleep(2);
            }
        });
    int externalSchedulingCount = 0;
    IncrementalAnalysisPlan finalPlan;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisTelemetry,
        &scheduler,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            if (telemetry.stage == SemanticAnalysisStage::Scheduling
                && telemetry.reason
                    == SemanticAnalysisReason::ExternalFileChange) {
                ++externalSchedulingCount;
            }
        });
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisPlanPrepared,
        &scheduler,
        [&](const IncrementalAnalysisPlan& plan) { finalPlan = plan; });
    expect("rapid external source A writes changed text",
           writeSource(fileA, newA));
    scheduler.handleExternalFileChanged(fileA, 0);
    expect("first rapid external request becomes active",
           waitUntil(
               [&]() {
                   return externalSchedulingCount == 1
                       && scheduler.isSemanticAnalysisActive();
               },
               3000));
    expect("rapid external source B writes changed text",
           writeSource(fileB, newB));
    scheduler.handleExternalFileChanged(fileB, 0);
    expect("second rapid external event submits a latest request",
           waitUntil([&]() { return externalSchedulingCount == 2; }, 3000));
    releaseWorker.store(true, std::memory_order_relaxed);
    expect("both rapid external changes reach the final immutable snapshot",
           waitUntil(
               [&]() {
                   return SemanticIndex::getInstance()
                                  ->getCachedFileContent(fileA)
                              == newA
                       && SemanticIndex::getInstance()
                                  ->getCachedFileContent(fileB)
                              == newB;
               },
               10000));
    expect("coalesced external request contains both changed files",
           finalPlan.impact == SemanticChangeImpact::FullFallback
               && containsFile(finalPlan.changedFiles, fileA)
               && containsFile(finalPlan.changedFiles, fileB));
    scheduler.shutdown();
}

void runFullWorkspaceRebuildDropsRemovedFilesAndKeepsGraph()
{
    QTemporaryDir directory;
    expect("full rebuild fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;
    const QString child = directory.filePath(QStringLiteral("child.sv"));
    const QString top = directory.filePath(QStringLiteral("top.sv"));
    const QString removed =
        directory.filePath(QStringLiteral("removed.sv"));
    const QString childText =
        QStringLiteral("module child; endmodule\n");
    const QString topText = QStringLiteral(
        "module top; child u_child(); removed u_removed(); endmodule\n");
    const QString removedText =
        QStringLiteral("module removed; logic legacy; endmodule\n");
    const auto writeSource = [](const QString& fileName,
                                const QString& text) {
        QFile file(fileName);
        return file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(text.toUtf8()) == text.toUtf8().size();
    };
    expect("full rebuild child source writes", writeSource(child, childText));
    expect("full rebuild top source writes", writeSource(top, topText));
    expect("full rebuild removable source writes",
           writeSource(removed, removedText));

    ProjectModel projectModel;
    projectModel.setWorkspaceState(directory.path(), {child, top, removed});
    projectModel.setWorkspaceConfiguration(
        {directory.path()}, {}, {QStringLiteral("sv")},
        QStringLiteral("top"), {});
    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    scheduler.setProjectModel(&projectModel);
    expect("initial full rebuild baseline completes",
           waitUntil([&]() { return finishedSpy.size() == 1; }, 10000));
    expect("initial full rebuild publishes removable file",
           SemanticIndex::getInstance()->getCachedFileContent(removed)
               == removedText);

    scheduler.requestWorkspaceAnalysis(projectModel.snapshot());
    expect("same-configuration explicit full rebuild completes",
           waitUntil([&]() { return finishedSpy.size() == 2; }, 10000));

    MyCodeEditor childEditor;
    childEditor.setPlainText(childText);
    documents.registerEditor(&childEditor, child);
    IncrementalAnalysisPlan savedPlan;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisPlanPrepared,
        &scheduler,
        [&](const IncrementalAnalysisPlan& plan) {
            if (plan.reason == SemanticAnalysisReason::Save)
                savedPlan = plan;
        });
    childEditor.setPlainText(
        QStringLiteral("module child(input logic enable); endmodule\n"));
    documents.markSaved(&childEditor);
    expect("incremental save after same-config full rebuild completes",
           waitUntil([&]() { return finishedSpy.size() == 3; }, 10000));
    expect("same-config full rebuild preserves dependency graph edges",
           savedPlan.impact == SemanticChangeImpact::ModuleInterface
               && containsFile(savedPlan.affectedFiles, child)
               && containsFile(savedPlan.affectedFiles, top));

    projectModel.setScannedFiles({child, top});
    expect("workspace file-list removal full rebuild completes",
           waitUntil([&]() { return finishedSpy.size() == 4; }, 10000));
    const auto snapshot = SemanticIndex::getInstance()->snapshot();
    bool relationshipTouchesRemoved = false;
    if (snapshot) {
        for (const SemanticRelationship& relationship
             : snapshot->relationships()) {
            relationshipTouchesRemoved = relationshipTouchesRemoved
                || QFileInfo(relationship.fromStableKey.fileName)
                       .absoluteFilePath()
                       .compare(QFileInfo(removed).absoluteFilePath(),
                                Qt::CaseInsensitive)
                    == 0
                || QFileInfo(relationship.toStableKey.fileName)
                       .absoluteFilePath()
                       .compare(QFileInfo(removed).absoluteFilePath(),
                                Qt::CaseInsensitive)
                    == 0
                || QFileInfo(relationship.evidenceRange.fileName)
                       .absoluteFilePath()
                       .compare(QFileInfo(removed).absoluteFilePath(),
                                Qt::CaseInsensitive)
                    == 0;
        }
    }
    bool diagnosticOwnedByRemoved = false;
    if (snapshot) {
        for (const SemanticDiagnostic& diagnostic : snapshot->diagnostics()) {
            diagnosticOwnedByRemoved = diagnosticOwnedByRemoved
                || QFileInfo(diagnostic.fileName).absoluteFilePath().compare(
                       QFileInfo(removed).absoluteFilePath(),
                       Qt::CaseInsensitive)
                    == 0;
        }
    }
    expect("removed workspace file has no cached content or records",
           snapshot
               && SemanticIndex::getInstance()->getCachedFileContent(removed)
                      .isNull()
               && snapshot->getSymbolRecords(removed).isEmpty());
    expect("removed workspace file leaves no diagnostics or relationships",
           !diagnosticOwnedByRemoved && !relationshipTouchesRemoved);
    scheduler.shutdown();
}

void runIncrementalEffectiveFactsKeepInstanceContext()
{
    QTemporaryDir directory;
    expect("incremental effective-facts fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;
    const QString child = directory.filePath(QStringLiteral("child.sv"));
    const QString top = directory.filePath(QStringLiteral("top.sv"));
    const QString childText = QStringLiteral(
        "module child #(parameter logic [3:0] P = 4'h1) (\n"
        "  input logic [P * 2 - 1:0] data_i,\n"
        "  output logic data_o\n"
        ");\n"
        "  always_comb begin data_o = data_i[0]; end\n"
        "endmodule\n");
    const QString topText = QStringLiteral(
        "module top;\n"
        "  logic [7:0] data;\n"
        "  logic y0, y1;\n"
        "  child #(.P(4'h2)) u0(.data_i(data), .data_o(y0));\n"
        "  child #(.P(4'h3)) u1(.data_i(data), .data_o(y1));\n"
        "endmodule\n");
    const auto writeSource = [](const QString& fileName,
                                const QString& text) {
        QFile file(fileName);
        return file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(text.toUtf8()) == text.toUtf8().size();
    };
    expect("incremental effective-facts child source writes",
           writeSource(child, childText));
    expect("incremental effective-facts top source writes",
           writeSource(top, topText));

    SemanticIndex::getInstance()->clearSemanticState();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    ProjectModel projectModel;
    projectModel.setWorkspaceState(directory.path(), {child, top});
    projectModel.setWorkspaceConfiguration(
        {directory.path()}, {}, {QStringLiteral("sv")},
        QStringLiteral("top"), {});
    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor childEditor;
    MyCodeEditor topEditor;
    childEditor.setPlainText(childText);
    topEditor.setPlainText(topText);
    documents.registerEditor(&childEditor, child);
    documents.registerEditor(&topEditor, top);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    QList<IncrementalAnalysisPlan> savePlans;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisPlanPrepared,
        &scheduler,
        [&](const IncrementalAnalysisPlan& plan) {
            if (plan.reason == SemanticAnalysisReason::Save)
                savePlans.append(plan);
        });
    scheduler.setProjectModel(&projectModel);
    expect("incremental effective-facts baseline completes",
           waitUntil([&]() { return finishedSpy.size() == 1; }, 10000));

    const auto findRecord = [](const QString& fileName,
                               const QString& name,
                               SymbolTaxonomy::CollectorKind kind) {
        const auto snapshot = SemanticIndex::getInstance()->snapshot();
        if (!snapshot)
            return SemanticSymbolRecord();
        for (const SemanticSymbolRecord& record
             : snapshot->getSymbolRecords(fileName)) {
            if (record.name == name && record.collectorKind == kind)
                return record;
        }
        return SemanticSymbolRecord();
    };
    const auto resolve = [&](const SemanticSymbolRecord& record,
                             const QString& source,
                             const QString& instancePath,
                             std::uint64_t revision) {
        EffectiveValueQuery query;
        query.symbol = record;
        query.documentText = source;
        query.documentRevision = revision;
        query.instanceContext.workspacePath = directory.path();
        query.instanceContext.activeTopModule = QStringLiteral("top");
        query.instanceContext.instancePath = instancePath;
        return EffectiveValueService::getInstance()->resolve(query);
    };
    const auto verifyBoundValues = [&](const QString& source,
                                       const QString& expectedU0,
                                       const QString& expectedU1,
                                       const QString& widthU0,
                                       const QString& widthU1) {
        const SemanticSymbolRecord parameter = findRecord(
            child,
            QStringLiteral("P"),
            SymbolTaxonomy::CollectorKind::Parameter);
        const SemanticSymbolRecord port = findRecord(
            child,
            QStringLiteral("data_i"),
            SymbolTaxonomy::CollectorKind::PortInput);
        const std::uint64_t revision = static_cast<std::uint64_t>(
            documents.documentForFile(child).textVersion);
        const EffectiveValueResult parameterU0 = resolve(
            parameter, source, QStringLiteral("top.u0"), revision);
        const EffectiveValueResult parameterU1 = resolve(
            parameter, source, QStringLiteral("top.u1"), revision);
        const EffectiveValueResult portU0 = resolve(
            port, source, QStringLiteral("top.u0"), revision);
        const EffectiveValueResult portU1 = resolve(
            port, source, QStringLiteral("top.u1"), revision);
        return parameter.isValid() && port.isValid()
            && parameterU0.current() && parameterU1.current()
            && parameterU0.instanceBound && parameterU1.instanceBound
            && !parameterU0.defaultEvaluation
            && !parameterU1.defaultEvaluation
            && parameterU0.valueText == expectedU0
            && parameterU1.valueText == expectedU1
            && portU0.current() && portU1.current()
            && portU0.instanceBound && portU1.instanceBound
            && portU0.bitWidthText == widthU0
            && portU1.bitWidthText == widthU1;
    };
    expect("baseline effective facts distinguish both parameter overrides",
           verifyBoundValues(childText,
                             QStringLiteral("4'b10"),
                             QStringLiteral("4'b11"),
                             QStringLiteral("4"),
                             QStringLiteral("6")));

    const QString localBodyText = QString(childText).replace(
        QStringLiteral("data_o = data_i[0]"),
        QStringLiteral("data_o = ~data_i[0]"));
    childEditor.setPlainText(localBodyText);
    documents.markSaved(&childEditor);
    expect("child LocalBody save completes",
           waitUntil([&]() { return finishedSpy.size() == 2; }, 10000));
    const IncrementalAnalysisPlan localPlan = savePlans.isEmpty()
        ? IncrementalAnalysisPlan()
        : savePlans.constLast();
    expect("child LocalBody publishes only child but compiles parent context",
           localPlan.impact == SemanticChangeImpact::LocalBody
               && localPlan.affectedFiles.size() == 1
               && containsFile(localPlan.affectedFiles, child)
               && containsFile(localPlan.compilationFiles, child)
               && containsFile(localPlan.compilationFiles, top)
               && !containsFile(localPlan.affectedFiles, top));
    expect("child LocalBody save preserves per-instance effective facts",
           verifyBoundValues(localBodyText,
                             QStringLiteral("4'b10"),
                             QStringLiteral("4'b11"),
                             QStringLiteral("4"),
                             QStringLiteral("6")));

    const QString changedTopText = QString(topText).replace(
        QStringLiteral(".P(4'h2)"),
        QStringLiteral(".P(4'h4)"));
    topEditor.setPlainText(changedTopText);
    documents.markSaved(&topEditor);
    expect("parent override save completes",
           waitUntil([&]() { return finishedSpy.size() == 3; }, 10000));
    const IncrementalAnalysisPlan overridePlan = savePlans.isEmpty()
        ? IncrementalAnalysisPlan()
        : savePlans.constLast();
    expect("parent override publication includes elaborated child outputs",
           overridePlan.impact == SemanticChangeImpact::ModuleInterface
               && containsFile(overridePlan.affectedFiles, top)
               && containsFile(overridePlan.affectedFiles, child)
               && containsFile(overridePlan.compilationFiles, top)
               && containsFile(overridePlan.compilationFiles, child));
    expect("parent override republishes both child instance effective values",
           verifyBoundValues(localBodyText,
                             QStringLiteral("4'b100"),
                             QStringLiteral("4'b11"),
                             QStringLiteral("8"),
                             QStringLiteral("6")));
    expect("incremental effective-facts documents finish Current",
           scheduler.semanticStatus(child).state
                   == DocumentSemanticState::Current
               && scheduler.semanticStatus(top).state
                   == DocumentSemanticState::Current);
    scheduler.shutdown();
    EffectiveValueService::getInstance()->clearPublishedFacts();
    SemanticIndex::getInstance()->clearSemanticState();
}

void runFallbackAndAuthoritativePublicationScopes()
{
    QTemporaryDir directory;
    expect("publication-scope fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;
    const QString projectFile =
        directory.filePath(QStringLiteral("project.sv"));
    const QString outsideFile =
        directory.filePath(QStringLiteral("outside.sv"));
    const QString projectOld = QStringLiteral(
        "typedef logic [1:0] global_t; module project; endmodule\n");
    const QString projectNew = QStringLiteral(
        "typedef logic [3:0] global_t; module project; endmodule\n");
    const QString outsideText = QStringLiteral(
        "module outside; logic retained; endmodule\n");
    const auto writeSource = [](const QString& fileName,
                                const QString& text) {
        QFile file(fileName);
        return file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(text.toUtf8()) == text.toUtf8().size();
    };
    expect("publication-scope project source writes",
           writeSource(projectFile, projectOld));
    expect("publication-scope outside source writes",
           writeSource(outsideFile, outsideText));
    const SemanticSymbolRecord projectModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("project"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(projectFile)
            .withLocalHandle(1)
            .withTextSpan(projectOld.indexOf(QStringLiteral("project")), 7)
            .record();
    const SemanticSymbolRecord outsideModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("outside"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(outsideFile)
            .withLocalHandle(2)
            .withTextSpan(outsideText.indexOf(QStringLiteral("outside")), 7)
            .record();
    const SemanticSymbolRecord outsideSignal =
        SemanticFixtureRecordBuilder(
            QStringLiteral("retained"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(outsideFile)
            .withLocalHandle(3)
            .withTextSpan(outsideText.indexOf(QStringLiteral("retained")), 8)
            .inModule(QStringLiteral("outside"))
            .record();
    SemanticRelationship outsideRelationship;
    outsideRelationship.fromId = outsideModule.localHandle;
    outsideRelationship.toId = outsideSignal.localHandle;
    outsideRelationship.fromStableKey = outsideModule.stableKey;
    outsideRelationship.toStableKey = outsideSignal.stableKey;
    outsideRelationship.type = SymbolRelationshipEngine::CONTAINS;
    outsideRelationship.evidenceRange.fileName = outsideFile;
    SemanticIndex::getInstance()->installPreparedSnapshot(
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {projectModule, outsideModule, outsideSignal},
                {outsideRelationship},
                {},
                {{projectFile, projectOld}, {outsideFile, outsideText}})),
        {projectFile, outsideFile});
    EffectiveValueService* values = EffectiveValueService::getInstance();
    values->clearPublishedFacts();
    EffectiveValueFact outsideFact;
    outsideFact.kind = EffectiveValueFactKind::GenerateCount;
    outsideFact.status = EffectiveValueStatus::Current;
    outsideFact.fileName = outsideFile;
    outsideFact.startPosition = outsideText.indexOf(
        QStringLiteral("retained"));
    outsideFact.endPosition = outsideFact.startPosition + 8;
    outsideFact.valueText = QStringLiteral("1");
    const std::uint64_t outsideComputation =
        values->beginComputation({outsideFile});
    values->publishDocumentFacts(outsideFile,
                                 outsideText,
                                 {outsideFact},
                                 outsideComputation,
                                 0);

    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor editor;
    editor.setPlainText(projectOld);
    documents.registerEditor(&editor, projectFile);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    IncrementalAnalysisPlan fallbackPlan;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisPlanPrepared,
        &scheduler,
        [&](const IncrementalAnalysisPlan& plan) {
            if (plan.reason == SemanticAnalysisReason::Save)
                fallbackPlan = plan;
        });
    editor.setPlainText(projectNew);
    documents.markSaved(&editor);
    expect("standalone FullFallback save completes",
           waitUntil([&]() { return finishedSpy.size() == 1; }, 10000));
    const auto fallbackSnapshot = SemanticIndex::getInstance()->snapshot();
    expect("standalone FullFallback is full project but not authoritative",
           fallbackPlan.fullWorkspace
               && !fallbackPlan.authoritativeWorkspaceReplace
               && fallbackPlan.impact
                   == SemanticChangeImpact::FullFallback);
    expect("standalone FullFallback preserves out-of-scope snapshot and relationships",
           fallbackSnapshot
               && fallbackSnapshot->getCachedFileContent(outsideFile)
                      == outsideText
               && !fallbackSnapshot->getSymbolRecords(outsideFile).isEmpty()
               && !fallbackSnapshot->relationships().isEmpty());
    expect("standalone FullFallback preserves out-of-scope effective facts",
           !values->factsForDocument(outsideFile, outsideText).isEmpty());

    ProjectSnapshot authoritativeProject;
    authoritativeProject.workspaceRoot = directory.path();
    authoritativeProject.systemVerilogFiles = {projectFile};
    authoritativeProject.allFiles = authoritativeProject.systemVerilogFiles;
    scheduler.requestWorkspaceAnalysis(authoritativeProject);
    expect("authoritative WorkspaceConfig rebuild completes",
           waitUntil([&]() { return finishedSpy.size() == 2; }, 10000));
    const auto authoritativeSnapshot = SemanticIndex::getInstance()->snapshot();
    expect("WorkspaceConfig removes out-of-scope snapshot and relationships",
           authoritativeSnapshot
               && authoritativeSnapshot->getCachedFileContent(outsideFile)
                      .isNull()
               && authoritativeSnapshot->getSymbolRecords(outsideFile).isEmpty()
               && authoritativeSnapshot->relationships().isEmpty());
    expect("WorkspaceConfig removes out-of-scope effective facts",
           values->factsForDocument(outsideFile, outsideText).isEmpty());
    scheduler.shutdown();
    values->clearPublishedFacts();
    SemanticIndex::getInstance()->clearSemanticState();
}

void runClassifierAndImpactPlanning()
{
    SemanticChangeClassifier classifier;
    const QString bodyOld = QStringLiteral(
        "module child(input logic a, output logic y);\n"
        "  assign y = a;\n"
        "endmodule\n");
    expect("whitespace and comments classify as TriviaOnly",
           classifier.classify(
               QStringLiteral("child.sv"),
               bodyOld,
               QStringLiteral("\n// trivia\n") + bodyOld).impact
               == SemanticChangeImpact::TriviaOnly);
    expect("procedural or assignment body edit classifies as LocalBody",
           classifier.classify(
               QStringLiteral("child.sv"),
               bodyOld,
               QString(bodyOld).replace(QStringLiteral("assign y = a"),
                                        QStringLiteral("assign y = ~a")))
                   .impact
               == SemanticChangeImpact::LocalBody);
    expect("port signature edit classifies as ModuleInterface",
           classifier.classify(
               QStringLiteral("child.sv"),
               bodyOld,
               QStringLiteral(
                   "module child(input logic a, input logic b, output logic y);\n"
                   "  assign y = a;\n"
                   "endmodule\n")).impact
               == SemanticChangeImpact::ModuleInterface);
    expect("package typedef edit classifies as PackageApi",
           classifier.classify(
               QStringLiteral("p.sv"),
               QStringLiteral(
                   "package p; typedef logic [7:0] word_t; endpackage\n"),
               QStringLiteral(
                   "package p; typedef logic [15:0] word_t; endpackage\n"))
                   .impact
               == SemanticChangeImpact::PackageApi);
    const QString packageFunctionOld = QStringLiteral(
        "package p;\n"
        "  function automatic int constant_width(input int x);\n"
        "    return x + 1;\n"
        "  endfunction\n"
        "endpackage\n");
    const SemanticChangeClassification packageFunctionBody =
        classifier.classify(
            QStringLiteral("p.sv"),
            packageFunctionOld,
            QString(packageFunctionOld)
                .replace(QStringLiteral("return x + 1"),
                         QStringLiteral("return x + 2")));
    expect("package constant function body edit classifies as PackageApi",
           packageFunctionBody.impact
               == SemanticChangeImpact::PackageApi);
    const QString packageTaskOld = QStringLiteral(
        "package p;\n"
        "  task automatic adjust(output int x);\n"
        "    x = 1;\n"
        "  endtask\n"
        "endpackage\n");
    const SemanticChangeClassification packageTaskBody =
        classifier.classify(
            QStringLiteral("p.sv"),
            packageTaskOld,
            QString(packageTaskOld)
                .replace(QStringLiteral("x = 1"),
                         QStringLiteral("x = 2")));
    expect("package task body edit classifies as PackageApi",
           packageTaskBody.impact == SemanticChangeImpact::PackageApi);
    expect("header structural edit classifies as HeaderMacro",
           classifier.classify(
               QStringLiteral("defs.svh"),
               QStringLiteral("`define WIDTH 8\n"),
               QStringLiteral("`define WIDTH 16\n")).impact
               == SemanticChangeImpact::HeaderMacro);
    const QString conditionalMacroOld = QStringLiteral(
        "module conditional_macro;\n"
        "`ifdef FEATURE_A\n"
        "  logic selected;\n"
        "`else\n"
        "  logic fallback;\n"
        "`endif\n"
        "endmodule\n");
    expect("conditional compilation AST edit classifies as HeaderMacro",
           classifier.classify(
               QStringLiteral("conditional_macro.sv"),
               conditionalMacroOld,
               QString(conditionalMacroOld)
                   .replace(QStringLiteral("FEATURE_A"),
                            QStringLiteral("FEATURE_B")))
                   .impact
               == SemanticChangeImpact::HeaderMacro);

    const QString nonAnsiModule = QStringLiteral(
        "module legacy(a, y);\n"
        "  input wire [3:0] a;\n"
        "  output logic y;\n"
        "endmodule\n");
    expect("non-ANSI module port direction edit classifies as ModuleInterface",
           classifier.classify(
               QStringLiteral("legacy.sv"),
               nonAnsiModule,
               QString(nonAnsiModule)
                   .replace(QStringLiteral("input wire [3:0] a"),
                            QStringLiteral("inout wire [3:0] a")))
                   .impact
               == SemanticChangeImpact::ModuleInterface);
    expect("non-ANSI module port width edit classifies as ModuleInterface",
           classifier.classify(
               QStringLiteral("legacy.sv"),
               nonAnsiModule,
               QString(nonAnsiModule)
                   .replace(QStringLiteral("[3:0] a"),
                            QStringLiteral("[7:0] a")))
                   .impact
               == SemanticChangeImpact::ModuleInterface);
    expect("non-ANSI module port type edit classifies as ModuleInterface",
           classifier.classify(
               QStringLiteral("legacy.sv"),
               nonAnsiModule,
               QString(nonAnsiModule)
                   .replace(QStringLiteral("input wire [3:0] a"),
                            QStringLiteral("input logic signed [3:0] a")))
                   .impact
               == SemanticChangeImpact::ModuleInterface);

    const QString nonAnsiInterface = QStringLiteral(
        "interface legacy_if(clk, data);\n"
        "  input wire clk;\n"
        "  inout wire [3:0] data;\n"
        "endinterface\n");
    expect("non-ANSI interface port direction edit classifies as ModuleInterface",
           classifier.classify(
               QStringLiteral("legacy_if.sv"),
               nonAnsiInterface,
               QString(nonAnsiInterface)
                   .replace(QStringLiteral("inout wire [3:0] data"),
                            QStringLiteral("input wire [3:0] data")))
                   .impact
               == SemanticChangeImpact::ModuleInterface);
    expect("non-ANSI interface port width edit classifies as ModuleInterface",
           classifier.classify(
               QStringLiteral("legacy_if.sv"),
               nonAnsiInterface,
               QString(nonAnsiInterface)
                   .replace(QStringLiteral("[3:0] data"),
                            QStringLiteral("[7:0] data")))
                   .impact
               == SemanticChangeImpact::ModuleInterface);
    expect("non-ANSI interface port type edit classifies as ModuleInterface",
           classifier.classify(
               QStringLiteral("legacy_if.sv"),
               nonAnsiInterface,
               QString(nonAnsiInterface)
                   .replace(QStringLiteral("inout wire [3:0] data"),
                            QStringLiteral("inout tri [3:0] data")))
                   .impact
               == SemanticChangeImpact::ModuleInterface);
    const QString compilationUnitFunction = QStringLiteral(
        "function automatic int global_constant(input int x);\n"
        "  return x + 1;\n"
        "endfunction\n"
        "module uses_global; localparam int V = global_constant(1); endmodule\n");
    expect("compilation-unit constant function body is never LocalBody",
           classifier.classify(
               QStringLiteral("global_function.sv"),
               compilationUnitFunction,
               QString(compilationUnitFunction)
                   .replace(QStringLiteral("return x + 1"),
                            QStringLiteral("return x + 2")))
                   .impact
               == SemanticChangeImpact::FullFallback);
    const QString compilationUnitTypedef = QStringLiteral(
        "typedef logic [3:0] global_word_t;\n"
        "module uses_type(input global_word_t value); endmodule\n");
    expect("compilation-unit typedef edit is never LocalBody",
           classifier.classify(
               QStringLiteral("global_type.sv"),
               compilationUnitTypedef,
               QString(compilationUnitTypedef)
                   .replace(QStringLiteral("[3:0]"),
                            QStringLiteral("[7:0]")))
                   .impact
               == SemanticChangeImpact::FullFallback);
    const QString compilationUnitParameter = QStringLiteral(
        "parameter int GLOBAL_WIDTH = 4;\n"
        "module uses_parameter; logic [GLOBAL_WIDTH-1:0] value; endmodule\n");
    expect("compilation-unit parameter edit is never LocalBody",
           classifier.classify(
               QStringLiteral("global_parameter.sv"),
               compilationUnitParameter,
               QString(compilationUnitParameter)
                   .replace(QStringLiteral("GLOBAL_WIDTH = 4"),
                            QStringLiteral("GLOBAL_WIDTH = 8")))
                   .impact
               == SemanticChangeImpact::FullFallback);
    const QString compilationUnitImport = QStringLiteral(
        "import p::*;\n"
        "module imported_type_user(input word_t value); endmodule\n");
    expect("compilation-unit import edit is never LocalBody",
           classifier.classify(
               QStringLiteral("global_import.sv"),
               compilationUnitImport,
               QString(compilationUnitImport)
                   .replace(QStringLiteral("p::*"),
                            QStringLiteral("q::*")))
                   .impact
               == SemanticChangeImpact::FullFallback);
    const QString moduleAlwaysBody = QStringLiteral(
        "module local_process(input logic a, output logic y);\n"
        "  always_comb begin y = a; end\n"
        "endmodule\n");
    expect("ordinary module always body remains LocalBody",
           classifier.classify(
               QStringLiteral("local_process.sv"),
               moduleAlwaysBody,
               QString(moduleAlwaysBody)
                   .replace(QStringLiteral("y = a"),
                            QStringLiteral("y = ~a")))
                   .impact
               == SemanticChangeImpact::LocalBody);
    expect("unprovable parse edit falls back to full workspace",
           classifier.classify(
               QStringLiteral("child.sv"),
               bodyOld,
               QStringLiteral("module child(\n")).impact
               == SemanticChangeImpact::FullFallback);

    QTemporaryDir directory;
    expect("impact planning fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;
    const QString header = directory.filePath(QStringLiteral("defs.svh"));
    const QString packageFile = directory.filePath(QStringLiteral("p.sv"));
    const QString child = directory.filePath(QStringLiteral("child.sv"));
    const QString parent = directory.filePath(QStringLiteral("parent.sv"));
    const QString legacyModule =
        directory.filePath(QStringLiteral("legacy.sv"));
    const QString legacyInterface =
        directory.filePath(QStringLiteral("legacy_if.sv"));
    const QString macroModule =
        directory.filePath(QStringLiteral("macro_child.sv"));
    const QString consumer = directory.filePath(QStringLiteral("consumer.sv"));
    const QString top = directory.filePath(QStringLiteral("top.sv"));
    const QString independent =
        directory.filePath(QStringLiteral("independent.sv"));
    ProjectSnapshot project;
    project.workspaceRoot = directory.path();
    project.systemVerilogFiles = {
        header,
        packageFile,
        child,
        parent,
        legacyModule,
        legacyInterface,
        macroModule,
        consumer,
        top,
        independent};
    project.allFiles = project.systemVerilogFiles;
    project.includeDirs = {directory.path()};
    project.topModule = QStringLiteral("top");
    QHash<QString, QString> contents;
    contents.insert(header, QStringLiteral("`define WIDTH 8\n"));
    contents.insert(packageFile, QStringLiteral(
        "package p;\n"
        "  typedef logic [7:0] word_t;\n"
        "  function automatic int constant_width(input int x);\n"
        "    return x + 1;\n"
        "  endfunction\n"
        "  task automatic adjust(output int x); x = 1; endtask\n"
        "endpackage\n"));
    contents.insert(child, QStringLiteral(
        "`include \"defs.svh\"\n"
        "module child #(parameter int W = `WIDTH)"
        "(input logic a, output logic y); assign y = a; endmodule\n"));
    contents.insert(parent, QStringLiteral(
        "module parent; logic a, y;"
        " child #(.W(12)) u_child(.a(a), .y(y)); endmodule\n"));
    contents.insert(legacyModule, nonAnsiModule);
    contents.insert(legacyInterface, nonAnsiInterface);
    contents.insert(macroModule, QStringLiteral(
        "`define INTERNAL_FEATURE 1\n"
        "module macro_child;\n"
        "`ifdef INTERNAL_FEATURE\n"
        "  logic enabled;\n"
        "`endif\n"
        "endmodule\n"));
    contents.insert(consumer, QStringLiteral(
        "module consumer #(parameter int W = p::constant_width(2))"
        "(input p::word_t a); int adjusted;"
        " initial p::adjust(adjusted); endmodule\n"));
    contents.insert(top, QStringLiteral(
        "module top; logic a, y;"
        " parent u_parent();"
        " legacy u_legacy(); legacy_if u_legacy_if();"
        " macro_child u_macro_child();"
        " consumer u_consumer(.a(a)); endmodule\n"));
    contents.insert(independent,
                    QStringLiteral("module independent; endmodule\n"));
    const SemanticDependencyGraph graph =
        SemanticDependencyGraph::build(project, contents);
    expect("parameterized instance records its module dependency",
           containsFile(
               graph.dependenciesOf({parent},
                                    SemanticDependencyKind::Instantiation,
                                    false),
               child));
    expect("active top dependency closure includes instantiated child",
           containsFile(
               graph.dependenciesOf({top},
                                    SemanticDependencyKind::ActiveTop,
                                    false),
               child));

    auto planFor = [&](const QString& changed,
                       SemanticChangeImpact impact,
                       SemanticAnalysisReason reason =
                           SemanticAnalysisReason::Save) {
        SemanticAnalysisRequest request;
        request.generation = 1;
        request.reason = reason;
        request.project = project;
        request.triggerFile = changed;
        request.changedFiles = changed.isEmpty()
            ? project.systemVerilogFiles
            : QStringList{changed};
        SemanticChangeClassification classification;
        classification.impact = impact;
        return IncrementalAnalysisPlanService().plan(request,
                                                     classification,
                                                     graph);
    };

    const IncrementalAnalysisPlan local =
        planFor(child, SemanticChangeImpact::LocalBody);
    expect("LocalBody affects only changed compilation unit",
           local.affectedFiles.size() == 1
               && containsFile(local.affectedFiles, child)
               && !containsFile(local.affectedFiles, top));
    expect("LocalBody compilation includes required header dependency",
           containsFile(local.compilationFiles, header));
    expect("LocalBody compilation restores direct parent instance context",
           containsFile(local.compilationFiles, parent));
    expect("LocalBody compilation restores multi-level active-top context",
           containsFile(local.compilationFiles, top));
    expect("LocalBody publication scope still excludes parent context files",
           !containsFile(local.affectedFiles, parent)
               && !containsFile(local.affectedFiles, top));

    const IncrementalAnalysisPlan moduleApi =
        planFor(child, SemanticChangeImpact::ModuleInterface);
    expect("ModuleInterface reaches instantiating dependents",
           containsFile(moduleApi.affectedFiles, child)
               && containsFile(moduleApi.affectedFiles, top)
               && !containsFile(moduleApi.affectedFiles, independent));
    const IncrementalAnalysisPlan parentOverride =
        planFor(parent, SemanticChangeImpact::ModuleInterface);
    expect("ModuleInterface publication follows forward instantiation outputs",
           containsFile(parentOverride.affectedFiles, parent)
               && containsFile(parentOverride.affectedFiles, child)
               && containsFile(parentOverride.affectedFiles, top));
    const IncrementalAnalysisPlan nonAnsiModuleApi = planFor(
        legacyModule,
        classifier.classify(
                      legacyModule,
                      nonAnsiModule,
                      QString(nonAnsiModule)
                          .replace(QStringLiteral("[3:0] a"),
                                   QStringLiteral("[7:0] a")))
            .impact);
    expect("non-ANSI module API edit reaches its instantiating top",
           containsFile(nonAnsiModuleApi.affectedFiles, legacyModule)
               && containsFile(nonAnsiModuleApi.affectedFiles, top));
    const IncrementalAnalysisPlan nonAnsiInterfaceApi = planFor(
        legacyInterface,
        classifier.classify(
                      legacyInterface,
                      nonAnsiInterface,
                      QString(nonAnsiInterface)
                          .replace(QStringLiteral("[3:0] data"),
                                   QStringLiteral("[7:0] data")))
            .impact);
    expect("non-ANSI interface API edit reaches its instantiating top",
           containsFile(nonAnsiInterfaceApi.affectedFiles, legacyInterface)
               && containsFile(nonAnsiInterfaceApi.affectedFiles, top));

    const IncrementalAnalysisPlan packageApi =
        planFor(packageFile, SemanticChangeImpact::PackageApi);
    expect("PackageApi keeps changed package in scope",
           containsFile(packageApi.affectedFiles, packageFile));
    expect("PackageApi reaches qualified users",
           containsFile(packageApi.affectedFiles, consumer));
    expect("PackageApi reaches instantiators of qualified users",
           containsFile(packageApi.affectedFiles, top));
    expect("PackageApi excludes independent units",
           !containsFile(packageApi.affectedFiles, independent));
    const IncrementalAnalysisPlan packageFunctionPlan =
        planFor(packageFile, packageFunctionBody.impact);
    expect("package function body change reaches consumer and its instantiating top",
           containsFile(packageFunctionPlan.affectedFiles, consumer)
               && containsFile(packageFunctionPlan.affectedFiles, top));
    const IncrementalAnalysisPlan packageTaskPlan =
        planFor(packageFile, packageTaskBody.impact);
    expect("package task body change reaches consumer and its instantiating top",
           containsFile(packageTaskPlan.affectedFiles, consumer)
               && containsFile(packageTaskPlan.affectedFiles, top));

    const IncrementalAnalysisPlan headerMacro =
        planFor(header, SemanticChangeImpact::HeaderMacro);
    expect("HeaderMacro reaches include users and reverse closure",
           containsFile(headerMacro.affectedFiles, header)
               && containsFile(headerMacro.affectedFiles, child)
               && containsFile(headerMacro.affectedFiles, top)
               && !containsFile(headerMacro.affectedFiles, independent));
    const IncrementalAnalysisPlan macroModulePlan =
        planFor(macroModule, SemanticChangeImpact::HeaderMacro);
    expect("HeaderMacro in a module reaches normal instantiating dependents",
           containsFile(macroModulePlan.affectedFiles, macroModule)
               && containsFile(macroModulePlan.affectedFiles, top)
               && !containsFile(macroModulePlan.affectedFiles, independent));

    const QString defineA =
        directory.filePath(QStringLiteral("define_a.sv"));
    const QString defineB =
        directory.filePath(QStringLiteral("define_b.sv"));
    const QString defineC =
        directory.filePath(QStringLiteral("define_c.sv"));
    const QString defineD =
        directory.filePath(QStringLiteral("define_d.sv"));
    const QString conditionalUser =
        directory.filePath(QStringLiteral("conditional_user.sv"));
    ProjectSnapshot conditionalProject;
    conditionalProject.workspaceRoot = directory.path();
    conditionalProject.systemVerilogFiles = {
        defineA, defineB, defineC, defineD, conditionalUser};
    conditionalProject.allFiles = conditionalProject.systemVerilogFiles;
    conditionalProject.includeDirs = {directory.path()};
    const QHash<QString, QString> conditionalContents = {
        {defineA, QStringLiteral("`define FEATURE_A 1\n")},
        {defineB, QStringLiteral("`define FEATURE_B 1\n")},
        {defineC, QStringLiteral("`define FEATURE_C 1\n")},
        {defineD, QStringLiteral("`define FEATURE_D 1\n")},
        {conditionalUser,
         QStringLiteral(
             "`ifdef (FEATURE_A && !FEATURE_B)\n"
             "module conditional_user; endmodule\n"
             "`elsif FEATURE_C\n"
             "module conditional_user_c; endmodule\n"
             "`endif\n"
             "`ifndef FEATURE_D\n"
             "module conditional_user_d; endmodule\n"
             "`endif\n")}};
    const SemanticDependencyGraph conditionalGraph =
        SemanticDependencyGraph::build(conditionalProject,
                                       conditionalContents);
    const QStringList conditionalMacroDependencies =
        conditionalGraph.dependenciesOf(
            {conditionalUser}, SemanticDependencyKind::Macro, false);
    expect("compound ifdef and elsif collect every macro identifier dependency",
           containsFile(conditionalMacroDependencies, defineA)
               && containsFile(conditionalMacroDependencies, defineB)
               && containsFile(conditionalMacroDependencies, defineC));
    expect("ifndef-only macro use creates a dependency edge",
           containsFile(conditionalMacroDependencies, defineD));

    const QString renamedModule =
        directory.filePath(QStringLiteral("renamed_module.sv"));
    const QString oldModuleUser =
        directory.filePath(QStringLiteral("old_module_user.sv"));
    const QString newModuleUser =
        directory.filePath(QStringLiteral("new_module_user.sv"));
    const QString renamedPackage =
        directory.filePath(QStringLiteral("renamed_package.sv"));
    const QString oldPackageUser =
        directory.filePath(QStringLiteral("old_package_user.sv"));
    const QString newPackageUser =
        directory.filePath(QStringLiteral("new_package_user.sv"));
    const QString renamedMacro =
        directory.filePath(QStringLiteral("renamed_macro.svh"));
    const QString oldMacroUser =
        directory.filePath(QStringLiteral("old_macro_user.sv"));
    const QString newMacroUser =
        directory.filePath(QStringLiteral("new_macro_user.sv"));
    const QString graphIndependent =
        directory.filePath(QStringLiteral("graph_independent.sv"));
    ProjectSnapshot renameProject;
    renameProject.workspaceRoot = directory.path();
    renameProject.systemVerilogFiles = {
        renamedModule,
        oldModuleUser,
        newModuleUser,
        renamedPackage,
        oldPackageUser,
        newPackageUser,
        renamedMacro,
        oldMacroUser,
        newMacroUser,
        graphIndependent};
    renameProject.allFiles = renameProject.systemVerilogFiles;
    renameProject.includeDirs = {directory.path()};
    QHash<QString, QString> previousContents = {
        {renamedModule, QStringLiteral("module old_child; endmodule\n")},
        {oldModuleUser,
         QStringLiteral("module old_module_user; old_child u(); endmodule\n")},
        {newModuleUser,
         QStringLiteral("module new_module_user; new_child u(); endmodule\n")},
        {renamedPackage,
         QStringLiteral("package old_pkg; typedef int value_t; endpackage\n")},
        {oldPackageUser,
         QStringLiteral("module old_package_user; import old_pkg::*; value_t v; endmodule\n")},
        {newPackageUser,
         QStringLiteral("module new_package_user; import new_pkg::*; value_t v; endmodule\n")},
        {renamedMacro, QStringLiteral("`define OLD_FEATURE 1\n")},
        {oldMacroUser,
         QStringLiteral("`ifdef OLD_FEATURE\nmodule old_macro_user; endmodule\n`endif\n")},
        {newMacroUser,
         QStringLiteral("`ifdef NEW_FEATURE\nmodule new_macro_user; endmodule\n`endif\n")},
        {graphIndependent,
         QStringLiteral("module graph_independent; endmodule\n")}};
    QHash<QString, QString> nextContents = previousContents;
    nextContents[renamedModule] =
        QStringLiteral("module new_child; endmodule\n");
    nextContents[renamedPackage] =
        QStringLiteral("package new_pkg; typedef int value_t; endpackage\n");
    nextContents[renamedMacro] =
        QStringLiteral("`define NEW_FEATURE 1\n");
    const SemanticDependencyGraph previousRenameGraph =
        SemanticDependencyGraph::build(renameProject, previousContents);
    const SemanticDependencyGraph nextRenameGraph =
        SemanticDependencyGraph::build(renameProject, nextContents);
    auto dualGraphPlan = [&](const QString& changed,
                             SemanticChangeImpact impact) {
        SemanticAnalysisRequest request;
        request.generation = 1;
        request.reason = SemanticAnalysisReason::Save;
        request.project = renameProject;
        request.triggerFile = changed;
        request.changedFiles = {changed};
        SemanticChangeClassification classification;
        classification.impact = impact;
        return IncrementalAnalysisPlanService().plan(
            request,
            classification,
            previousRenameGraph,
            nextRenameGraph);
    };
    const IncrementalAnalysisPlan renamedModulePlan = dualGraphPlan(
        renamedModule, SemanticChangeImpact::ModuleInterface);
    expect("module rename retains old instantiator through previous graph",
           containsFile(renamedModulePlan.affectedFiles, oldModuleUser));
    expect("module rename discovers new instantiator through next graph",
           containsFile(renamedModulePlan.affectedFiles, newModuleUser));
    expect("module rename union excludes independent files",
           !containsFile(renamedModulePlan.affectedFiles, graphIndependent));
    const IncrementalAnalysisPlan renamedPackagePlan = dualGraphPlan(
        renamedPackage, SemanticChangeImpact::PackageApi);
    expect("package rename retains old import consumer through previous graph",
           containsFile(renamedPackagePlan.affectedFiles, oldPackageUser));
    expect("package rename discovers new import consumer through next graph",
           containsFile(renamedPackagePlan.affectedFiles, newPackageUser));
    const IncrementalAnalysisPlan renamedMacroPlan = dualGraphPlan(
        renamedMacro, SemanticChangeImpact::HeaderMacro);
    expect("macro rename retains old condition user through previous graph",
           containsFile(renamedMacroPlan.affectedFiles, oldMacroUser));
    expect("macro rename discovers new condition user through next graph",
           containsFile(renamedMacroPlan.affectedFiles, newMacroUser));

    const QString repairedFile =
        directory.filePath(QStringLiteral("repaired.sv"));
    const QString repairedConsumer =
        directory.filePath(QStringLiteral("repaired_consumer.sv"));
    ProjectSnapshot repairedProject;
    repairedProject.workspaceRoot = directory.path();
    repairedProject.systemVerilogFiles = {repairedFile, repairedConsumer};
    repairedProject.allFiles = repairedProject.systemVerilogFiles;
    const SemanticDependencyGraph parseErrorGraph =
        SemanticDependencyGraph::build(
            repairedProject,
            {{repairedFile, QStringLiteral("module repaired(\n")},
             {repairedConsumer,
              QStringLiteral("module repaired_consumer; repaired u(); endmodule\n")}});
    const SemanticDependencyGraph repairedGraph =
        SemanticDependencyGraph::build(
            repairedProject,
            {{repairedFile,
              QStringLiteral("module repaired; endmodule\n")},
             {repairedConsumer,
              QStringLiteral("module repaired_consumer; repaired u(); endmodule\n")}});
    SemanticAnalysisRequest repairedRequest;
    repairedRequest.generation = 1;
    repairedRequest.reason = SemanticAnalysisReason::Save;
    repairedRequest.project = repairedProject;
    repairedRequest.triggerFile = repairedFile;
    repairedRequest.changedFiles = {repairedFile};
    SemanticChangeClassification repairedClassification;
    repairedClassification.impact = SemanticChangeImpact::ModuleInterface;
    const IncrementalAnalysisPlan repairedPlan =
        IncrementalAnalysisPlanService().plan(
            repairedRequest,
            repairedClassification,
            parseErrorGraph,
            repairedGraph);
    expect("repairing a previous parse-error file falls back to full analysis",
           repairedPlan.fullWorkspace
               && repairedPlan.impact == SemanticChangeImpact::FullFallback);

    const IncrementalAnalysisPlan config = planFor(
        QString(),
        SemanticChangeImpact::WorkspaceConfig,
        SemanticAnalysisReason::WorkspaceConfiguration);
    expect("WorkspaceConfig analyzes full workspace",
           config.fullWorkspace
               && config.affectedFiles.size()
                      == project.systemVerilogFiles.size());
    const IncrementalAnalysisPlan fallback =
        planFor(child, SemanticChangeImpact::FullFallback);
    expect("unsafe classification conservatively falls back to full workspace",
           fallback.fullWorkspace
               && fallback.affectedFiles.size()
                      == project.systemVerilogFiles.size());
}

void runTriviaSaveRemapsSourceLocationsWithoutSlang()
{
    QTemporaryDir directory;
    expect("source remap fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;
    const QString fileName = directory.filePath(QStringLiteral("remap.sv"));
    const QString oldText = QStringLiteral(
        "module remap;\n"
        "  logic value;\n"
        "  assign value = 1'b0;\n"
        "endmodule\n");
    QFile file(fileName);
    expect("source remap fixture opens",
           file.open(QIODevice::WriteOnly | QIODevice::Text));
    if (!file.isOpen())
        return;
    file.write(oldText.toUtf8());
    file.close();

    auto recordAt = [&](int position,
                        int line,
                        SymbolTaxonomy::SymbolUsageRole usage,
                        int handle) {
        SemanticSymbolRecord record;
        record.name = QStringLiteral("value");
        record.localHandle = handle;
        record.declarationKind = SymbolTaxonomy::DeclarationKind::Signal;
        record.usageRole = usage;
        record.location.fileName = fileName;
        record.location.startLine = line;
        record.location.startColumn = 3;
        record.location.endLine = line;
        record.location.endColumn = 8;
        record.location.position = position;
        record.location.length = 5;
        record.stableKey.fileName = fileName;
        record.stableKey.symbolName = record.name;
        record.stableKey.declarationKind = record.declarationKind;
        record.stableKey.ownerScope = QStringLiteral("remap");
        record.stableKey.sourcePosition = position;
        record.stableKey.sourceLength = 5;
        record.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Module;
        record.owner.name = QStringLiteral("remap");
        return record;
    };
    const int declarationPosition = oldText.indexOf(QStringLiteral("value"));
    const int referencePosition = oldText.indexOf(
        QStringLiteral("value"), declarationPosition + 1);
    const SemanticSymbolRecord declaration = recordAt(
        declarationPosition,
        2,
        SymbolTaxonomy::SymbolUsageRole::Declaration,
        1);
    const SemanticSymbolRecord reference = recordAt(
        referencePosition,
        3,
        SymbolTaxonomy::SymbolUsageRole::Reference,
        2);
    SemanticRelationship relationship;
    relationship.fromId = reference.localHandle;
    relationship.toId = declaration.localHandle;
    relationship.fromStableKey = reference.stableKey;
    relationship.toStableKey = declaration.stableKey;
    relationship.type = SymbolRelationshipEngine::REFERENCES;
    relationship.evidenceRange.fileName = fileName;
    relationship.evidenceRange.line = 3;
    relationship.evidenceRange.column = 10;
    relationship.evidenceRange.endLine = 3;
    relationship.evidenceRange.endColumn = 15;
    SemanticDiagnostic diagnostic;
    diagnostic.fileName = fileName;
    diagnostic.line = 3;
    diagnostic.column = 10;
    diagnostic.message = QStringLiteral("remapped diagnostic");
    const auto baseSnapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {declaration, reference},
                {relationship},
                {diagnostic},
                {{fileName, oldText}}));
    SemanticIndex::getInstance()->installPreparedSnapshot(baseSnapshot,
                                                          {fileName});
    EffectiveValueService* effectiveValues =
        EffectiveValueService::getInstance();
    effectiveValues->clearPublishedFacts();
    EffectiveValueFact effectiveFact;
    effectiveFact.kind = EffectiveValueFactKind::PartSelectWidth;
    effectiveFact.status = EffectiveValueStatus::Current;
    effectiveFact.fileName = fileName;
    effectiveFact.startPosition = referencePosition;
    effectiveFact.endPosition = referencePosition + 5;
    effectiveFact.line = 3;
    effectiveFact.expressionText = QStringLiteral("value");
    effectiveFact.valueText = QStringLiteral("1");
    const std::uint64_t factRevision =
        effectiveValues->beginComputation({fileName});
    effectiveValues->publishDocumentFacts(fileName,
                                          oldText,
                                          {effectiveFact},
                                          factRevision,
                                          0);

    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor editor;
    editor.setPlainText(oldText);
    documents.registerEditor(&editor, fileName);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    SemanticChangeImpact publishedImpact = SemanticChangeImpact::Unknown;
    bool workerInvokedSlang = true;
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisPlanPrepared,
        &scheduler,
        [&](const IncrementalAnalysisPlan& plan) {
            publishedImpact = plan.impact;
        });
    QObject::connect(
        &scheduler,
        &AnalysisScheduler::semanticAnalysisTelemetry,
        &scheduler,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            if (telemetry.stage == SemanticAnalysisStage::Worker)
                workerInvokedSlang = telemetry.slangInvoked;
        });
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);

    const QString prefixTrivia = QStringLiteral("\n// saved prefix trivia\n");
    const QString middleTrivia = QStringLiteral("// saved middle trivia\n\n  ");
    QString remappedText = prefixTrivia + oldText;
    remappedText.insert(remappedText.indexOf(QStringLiteral("assign")),
                        middleTrivia);
    editor.setPlainText(remappedText);
    documents.markSaved(&editor);
    expect("trivia save completes through remap worker",
           waitUntil([&finishedSpy]() { return !finishedSpy.isEmpty(); },
                     5000));
    expect("trivia save is planned as TriviaOnly",
           publishedImpact == SemanticChangeImpact::TriviaOnly);
    expect("trivia save invokes no Slang compilation",
           !workerInvokedSlang);

    const auto remapped = SemanticIndex::getInstance()->snapshot();
    const QList<SemanticSymbolRecord> records =
        remapped ? remapped->getSymbolRecords(fileName)
                 : QList<SemanticSymbolRecord>();
    bool declarationRemapped = false;
    bool referenceRemapped = false;
    const int remappedDeclarationPosition =
        remappedText.indexOf(QStringLiteral("value"));
    const int remappedReferencePosition = remappedText.indexOf(
        QStringLiteral("value"), remappedDeclarationPosition + 1);
    const int remappedDeclarationLine =
        remappedText.left(remappedDeclarationPosition).count(QLatin1Char('\n'))
        + 1;
    const int remappedReferenceLine =
        remappedText.left(remappedReferencePosition).count(QLatin1Char('\n'))
        + 1;
    for (const SemanticSymbolRecord& record : records) {
        if (record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration) {
            declarationRemapped =
                record.location.startLine == remappedDeclarationLine
                && record.location.position == remappedDeclarationPosition;
        } else if (record.usageRole
                   == SymbolTaxonomy::SymbolUsageRole::Reference) {
            referenceRemapped =
                record.location.startLine == remappedReferenceLine
                && record.location.position == remappedReferencePosition;
        }
    }
    expect("trivia remap updates definition source location",
           declarationRemapped);
    expect("trivia remap updates reference source location",
           referenceRemapped);
    expect("trivia remap updates diagnostic source location",
           remapped && !remapped->diagnostics().isEmpty()
               && remapped->diagnostics().first().line
                      == remappedReferenceLine);
    expect("trivia remap updates relationship evidence and endpoint keys",
           remapped && !remapped->relationships().isEmpty()
               && remapped->relationships().first().evidenceRange.line
                      == remappedReferenceLine
               && remapped->relationships().first()
                          .fromStableKey.sourcePosition
                      == remappedReferencePosition);
    const DocumentSnapshot savedDocument =
        documents.documentForFile(fileName);
    const QList<EffectiveValueFact> remappedFacts =
        effectiveValues->factsForDocument(
            fileName,
            remappedText,
            {},
            static_cast<std::uint64_t>(savedDocument.textVersion));
    expect("trivia remap updates effective-value fact positions and revision",
           remappedFacts.size() == 1
               && remappedFacts.first().startPosition
                      == remappedReferencePosition
               && remappedFacts.first().endPosition
                      == remappedReferencePosition + 5
               && remappedFacts.first().line == remappedReferenceLine);
    scheduler.shutdown();
    effectiveValues->clearPublishedFacts();
}

void runDesignHierarchyTopologyFingerprint()
{
    QTemporaryDir directory;
    expect("topology fingerprint fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;
    const QString topFile = directory.filePath(QStringLiteral("top.sv"));
    const QString childFile = directory.filePath(QStringLiteral("child.sv"));
    const QString otherFile = directory.filePath(QStringLiteral("other.sv"));
    for (const auto& entry : QList<QPair<QString, QByteArray>>{
             {topFile, QByteArray("module top; child u(); endmodule\n")},
             {childFile, QByteArray("module child; endmodule\n")},
             {otherFile, QByteArray("module other; endmodule\n")}}) {
        QFile file(entry.first);
        expect("topology fixture source opens",
               file.open(QIODevice::WriteOnly | QIODevice::Text));
        if (file.isOpen()) {
            file.write(entry.second);
            file.close();
        }
    }

    auto topologySnapshot = [&](int positionShift,
                                const QString& targetModule) {
        const SemanticSymbolRecord top =
            SemanticFixtureRecordBuilder(
                QStringLiteral("top"),
                SymbolTaxonomy::DeclarationKind::Module)
                .withFile(topFile)
                .withLocalHandle(1)
                .withLine(1 + positionShift)
                .withTextSpan(positionShift, 3)
                .record();
        const SemanticSymbolRecord child =
            SemanticFixtureRecordBuilder(
                QStringLiteral("child"),
                SymbolTaxonomy::DeclarationKind::Module)
                .withFile(childFile)
                .withLocalHandle(2)
                .withLine(1 + positionShift)
                .withTextSpan(10 + positionShift, 5)
                .record();
        const SemanticSymbolRecord other =
            SemanticFixtureRecordBuilder(
                QStringLiteral("other"),
                SymbolTaxonomy::DeclarationKind::Module)
                .withFile(otherFile)
                .withLocalHandle(3)
                .withLine(1 + positionShift)
                .withTextSpan(20 + positionShift, 5)
                .record();
        const SemanticSymbolRecord instance =
            SemanticFixtureRecordBuilder(
                QStringLiteral("u"),
                SymbolTaxonomy::DeclarationKind::Instance)
                .withFile(topFile)
                .withLocalHandle(4)
                .withLine(1 + positionShift)
                .withTextSpan(30 + positionShift, 1)
                .inModule(QStringLiteral("top"))
                .withType(targetModule,
                          targetModule,
                          SymbolTaxonomy::DeclarationKind::Module)
                .record();
        SemanticRelationship instantiates = semanticFixtureRelationship(
            top,
            instance,
            SymbolRelationshipEngine::INSTANTIATES,
            RelationshipProvenance::SlangExtracted,
            100,
            QStringLiteral("instance"));
        instantiates.fromAccessPath = QStringLiteral("top");
        instantiates.toAccessPath = QStringLiteral("top.u");
        instantiates.evidenceRange.fileName = topFile;
        instantiates.evidenceRange.line = 1 + positionShift;
        instantiates.evidenceRange.column = 13;
        instantiates.evidenceRange.endLine = 1 + positionShift;
        instantiates.evidenceRange.endColumn = 22;
        return std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {top, child, other, instance},
                {instantiates},
                {},
                {{topFile, QStringLiteral("module top; endmodule\n")},
                 {childFile, QStringLiteral("module child; endmodule\n")},
                 {otherFile, QStringLiteral("module other; endmodule\n")}}));
    };

    SemanticIndex index;
    NavigationService service(&index);
    const QSet<QString> scope = {topFile, childFile, otherFile};
    index.setSnapshot(topologySnapshot(0, QStringLiteral("child")));
    const QByteArray initialFingerprint =
        service.designStructureFingerprint(scope, QStringLiteral("top"));
    index.setSnapshot(topologySnapshot(10, QStringLiteral("child")));
    const QByteArray shiftedFingerprint =
        service.designStructureFingerprint(scope, QStringLiteral("top"));
    expect("source positions and evidence ranges do not change topology fingerprint",
           initialFingerprint == shiftedFingerprint);
    index.setSnapshot(topologySnapshot(10, QStringLiteral("other")));
    const QByteArray changedFingerprint =
        service.designStructureFingerprint(scope, QStringLiteral("top"));
    expect("instance target change changes topology fingerprint",
           changedFingerprint != shiftedFingerprint);

    QTabWidget tabWidget;
    TabManager tabs(&tabWidget);
    expect("topology fixture opens top tab", tabs.openFileInTab(topFile));
    expect("topology fixture opens child tab", tabs.openFileInTab(childFile));
    expect("topology fixture opens other tab", tabs.openFileInTab(otherFile));
    NavigationManager navigation;
    navigation.connectToTabManager(&tabs);
    navigation.setNavigationService(&service);
    index.setSnapshot(topologySnapshot(0, QStringLiteral("child")));
    navigation.setActiveView(NavigationManager::DesignHierarchyView);
    QStringList refreshDetails;
    QObject::connect(
        &navigation,
        &NavigationManager::navigationTelemetry,
        &navigation,
        [&](const SemanticAnalysisTelemetry& telemetry) {
            refreshDetails.append(telemetry.detail);
        });

    index.setSnapshot(topologySnapshot(10, QStringLiteral("child")));
    navigation.onBatchSymbolAnalysisCompleted(1, 4);
    expect("non-topology snapshot generation causes zero hierarchy rebuild",
           !refreshDetails.isEmpty()
               && refreshDetails.last().contains(
                   QStringLiteral("hierarchyRebuild=0")));
    index.setSnapshot(topologySnapshot(10, QStringLiteral("other")));
    navigation.onBatchSymbolAnalysisCompleted(1, 4);
    expect("real instance topology change causes one hierarchy rebuild",
           !refreshDetails.isEmpty()
               && refreshDetails.last().contains(
                   QStringLiteral("hierarchyRebuild=1")));
}

void runRelationshipReplacementUsesEvidenceOwner()
{
    QTemporaryDir directory;
    expect("relationship owner fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;
    const QString topFile = directory.filePath(QStringLiteral("top.sv"));
    const QString childFile = directory.filePath(QStringLiteral("child.sv"));

    const SemanticSymbolRecord topModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(topFile)
            .withLocalHandle(1)
            .withTextSpan(7, 3)
            .record();
    const SemanticSymbolRecord topInstance =
        SemanticFixtureRecordBuilder(
            QStringLiteral("u_child"),
            SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(topFile)
            .withLocalHandle(2)
            .withTextSpan(20, 7)
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord topReference =
        SemanticFixtureRecordBuilder(QStringLiteral("value"))
            .withFile(topFile)
            .withLocalHandle(3)
            .withTextSpan(40, 5)
            .withUsageRole(SymbolTaxonomy::SymbolUsageRole::Reference)
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord fallbackReference =
        SemanticFixtureRecordBuilder(QStringLiteral("value"))
            .withFile(topFile)
            .withLocalHandle(4)
            .withTextSpan(55, 5)
            .withUsageRole(SymbolTaxonomy::SymbolUsageRole::Reference)
            .inModule(QStringLiteral("top"))
            .record();
    const SemanticSymbolRecord childModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("child"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(childFile)
            .withLocalHandle(5)
            .withTextSpan(7, 5)
            .record();
    const SemanticSymbolRecord childValue =
        SemanticFixtureRecordBuilder(
            QStringLiteral("value"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(childFile)
            .withLocalHandle(6)
            .withTextSpan(22, 5)
            .inModule(QStringLiteral("child"))
            .record();
    const SemanticSymbolRecord childReference =
        SemanticFixtureRecordBuilder(QStringLiteral("value"))
            .withFile(childFile)
            .withLocalHandle(7)
            .withTextSpan(40, 5)
            .withUsageRole(SymbolTaxonomy::SymbolUsageRole::Reference)
            .inModule(QStringLiteral("child"))
            .record();

    const auto evidence = [](const QString& fileName, int line) {
        SemanticSourceRange range;
        range.fileName = fileName;
        range.line = line;
        range.column = 1;
        range.endLine = line;
        range.endColumn = 5;
        return range;
    };
    const SemanticRelationship instantiates = semanticFixtureRelationship(
        topInstance,
        childModule,
        SymbolRelationshipEngine::INSTANTIATES,
        RelationshipProvenance::SlangExtracted,
        100,
        QStringLiteral("child u_child"),
        evidence(topFile, 1));
    const SemanticRelationship crossReference = semanticFixtureRelationship(
        topReference,
        childValue,
        SymbolRelationshipEngine::REFERENCES,
        RelationshipProvenance::SlangExtracted,
        100,
        QStringLiteral("value"),
        evidence(topFile, 2));
    const SemanticRelationship fallbackOwnedBySource =
        semanticFixtureRelationship(
            fallbackReference,
            childValue,
            SymbolRelationshipEngine::REFERENCES,
            RelationshipProvenance::SlangExtracted,
            100,
            QStringLiteral("value"));
    const SemanticRelationship childOwned = semanticFixtureRelationship(
        childReference,
        childValue,
        SymbolRelationshipEngine::REFERENCES,
        RelationshipProvenance::SlangExtracted,
        100,
        QStringLiteral("value"),
        evidence(childFile, 2));
    const SemanticIndexSnapshot initial =
        SemanticIndexSnapshot::fromSymbolRecords(
            {topModule,
             topInstance,
             topReference,
             fallbackReference,
             childModule,
             childValue,
             childReference},
            {instantiates,
             crossReference,
             fallbackOwnedBySource,
             childOwned},
            {},
            {{topFile, QStringLiteral("module top; endmodule\n")},
             {childFile, QStringLiteral("module child; endmodule\n")}});

    const SemanticSymbolRecord updatedChildModule =
        SemanticFixtureRecordBuilder(
            QStringLiteral("child"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(childFile)
            .withLocalHandle(105)
            .withTextSpan(7, 5)
            .withType(QStringLiteral("module"), QStringLiteral("module"))
            .record();
    const SemanticSymbolRecord updatedChildValue =
        SemanticFixtureRecordBuilder(
            QStringLiteral("value"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(childFile)
            .withLocalHandle(106)
            .withTextSpan(35, 5)
            .inModule(QStringLiteral("child"))
            .record();
    const SemanticSymbolRecord updatedChildReference =
        SemanticFixtureRecordBuilder(QStringLiteral("value"))
            .withFile(childFile)
            .withLocalHandle(107)
            .withTextSpan(53, 5)
            .withUsageRole(SymbolTaxonomy::SymbolUsageRole::Reference)
            .inModule(QStringLiteral("child"))
            .record();
    const SemanticRelationship updatedChildOwned =
        semanticFixtureRelationship(
            updatedChildReference,
            updatedChildValue,
            SymbolRelationshipEngine::REFERENCES,
            RelationshipProvenance::SlangExtracted,
            100,
            QStringLiteral("updated value"),
            evidence(childFile, 3));
    SemanticFileSymbolUpdate childUpdate;
    childUpdate.fileName = childFile;
    childUpdate.symbolRecords = {
        updatedChildModule, updatedChildValue, updatedChildReference};
    childUpdate.content = QStringLiteral(
        "module child; logic inserted; logic value;"
        " assign value = value; endmodule\n");
    const SemanticIndexSnapshot replaced = initial.withReplacedFiles(
        {childUpdate},
        {},
        {},
        {childFile},
        {updatedChildOwned, updatedChildOwned});

    int topEvidenceCount = 0;
    int childEvidenceCount = 0;
    int fallbackCount = 0;
    int preservedShiftedTargetCount = 0;
    bool reboundCrossFileTarget = true;
    const SemanticSymbolRecord reboundValue =
        replaced.getSymbolRecordByStableKey(updatedChildValue.stableKey);
    for (const SemanticRelationship& relationship : replaced.relationships()) {
        if (QFileInfo(relationship.evidenceRange.fileName).absoluteFilePath()
                .compare(QFileInfo(topFile).absoluteFilePath(),
                         Qt::CaseInsensitive)
            == 0) {
            ++topEvidenceCount;
        }
        if (QFileInfo(relationship.evidenceRange.fileName).absoluteFilePath()
                .compare(QFileInfo(childFile).absoluteFilePath(),
                         Qt::CaseInsensitive)
            == 0) {
            ++childEvidenceCount;
        }
        if (relationship.fromStableKey == fallbackReference.stableKey)
            ++fallbackCount;
        const bool preservedTopOwner =
            QFileInfo(relationship.evidenceRange.fileName).absoluteFilePath()
                    .compare(QFileInfo(topFile).absoluteFilePath(),
                             Qt::CaseInsensitive)
                == 0
            || relationship.fromStableKey == fallbackReference.stableKey;
        if (preservedTopOwner
            && relationship.toStableKey == updatedChildValue.stableKey) {
            ++preservedShiftedTargetCount;
            reboundCrossFileTarget = reboundCrossFileTarget
                && relationship.toId == reboundValue.localHandle;
        }
    }
    expect("child LocalBody replacement preserves top-owned cross-file relationships",
           topEvidenceCount == 2);
    expect("missing-evidence relationship uses source endpoint as stable owner fallback",
           fallbackCount == 1);
    expect("child-owned relationships are replaced once without duplicates",
           childEvidenceCount == 1);
    bool relationshipUsesShiftedStableKey = false;
    for (const SemanticRelationship& relationship : replaced.relationships()) {
        relationshipUsesShiftedStableKey = relationshipUsesShiftedStableKey
            || relationship.toStableKey == updatedChildValue.stableKey;
    }
    expect("position-shifted target reuses its handle and updates preserved relationship stable keys",
           reboundValue.isValid()
               && reboundValue.localHandle == childValue.localHandle
               && relationshipUsesShiftedStableKey
               && preservedShiftedTargetCount == 2
               && reboundCrossFileTarget);

    const SemanticSymbolRecord renamedValue =
        SemanticFixtureRecordBuilder(
            QStringLiteral("renamed_value"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(childFile)
            .withLocalHandle(206)
            .withTextSpan(22, 13)
            .inModule(QStringLiteral("child"))
            .record();
    SemanticFileSymbolUpdate deletionUpdate;
    deletionUpdate.fileName = childFile;
    deletionUpdate.symbolRecords = {updatedChildModule, renamedValue};
    deletionUpdate.content =
        QStringLiteral("module child; logic renamed_value; endmodule\n");
    const SemanticIndexSnapshot afterDeletion = replaced.withReplacedFiles(
        {deletionUpdate}, {}, {}, {childFile}, {});
    bool danglingTargetFound = false;
    int remainingInstantiationCount = 0;
    for (const SemanticRelationship& relationship
         : afterDeletion.relationships()) {
        danglingTargetFound = danglingTargetFound
            || relationship.toStableKey == childValue.stableKey;
        if (relationship.type == SymbolRelationshipEngine::INSTANTIATES)
            ++remainingInstantiationCount;
    }
    expect("deleted target stable key removes preserved dangling relationships",
           !danglingTargetFound);
    expect("unrelated top-owned instantiation remains after child symbol deletion",
           remainingInstantiationCount == 1);
}

void runPreparedPublicationRetirementIsOrderedAndShutdownSafe()
{
    QTemporaryDir directory;
    expect("publication retirement fixture directory is valid",
           directory.isValid());
    if (!directory.isValid())
        return;

    const QString fileName =
        directory.filePath(QStringLiteral("retirement.sv"));
    QString source = QStringLiteral("module retirement;\n");
    for (int index = 0; index < 1200; ++index) {
        source += QStringLiteral("  logic retained_%1;\n").arg(index);
    }
    source += QStringLiteral("endmodule\n");
    QFile sourceFile(fileName);
    expect("publication retirement source writes",
           sourceFile.open(QIODevice::WriteOnly | QIODevice::Text)
               && sourceFile.write(source.toUtf8())
                      == source.toUtf8().size());
    sourceFile.close();

    // The gate refers to this flag from the retirement thread. Declare it
    // before every owner that can retain the gate so it outlives teardown.
    std::atomic_bool releaseRetirement{false};
    ProjectModel projectModel;
    projectModel.setWorkspaceState(directory.path(), {fileName});
    SymbolRelationshipEngine relationshipEngine;
    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    SymbolRelationshipEngine* previousRelationshipEngine =
        semanticIndex->relationshipEngine();
    semanticIndex->attachRelationshipEngine(&relationshipEngine);
    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setRelationshipEngine(&relationshipEngine);
    scheduler.setDocumentModel(&documents);
    QSignalSpy finishedSpy(
        &scheduler, &AnalysisScheduler::workspaceSymbolAnalysisFinished);
    scheduler.setProjectModel(&projectModel);
    expect("large retirement baseline completes",
           waitUntil([&]() { return finishedSpy.size() == 1; }, 15000));
    expect("baseline retirement drains before gated replacements",
           waitUntil(
               [&]() {
                   return analyzer.pendingPublicationRetirementsForTesting()
                       == 0;
               },
               5000));

    MyCodeEditor editor;
    editor.setPlainText(source);
    documents.registerEditor(&editor, fileName);
    analyzer.setPublicationRetirementGateForTesting(
        [&releaseRetirement]() {
            while (!releaseRetirement.load(std::memory_order_acquire))
                QThread::msleep(2);
        });

    const QString firstSaved = QStringLiteral("\n// retirement one\n")
        + source;
    editor.setPlainText(firstSaved);
    documents.markSaved(&editor);
    expect("first large prepared replacement publishes while retirement is gated",
           waitUntil([&]() { return finishedSpy.size() == 2; }, 15000));
    expect("first replaced snapshot is owned by retirement payload",
           analyzer.pendingPublicationRetirementsForTesting() >= 1);

    const QString secondSaved = QStringLiteral("\n// retirement two\n")
        + firstSaved;
    editor.setPlainText(secondSaved);
    documents.markSaved(&editor);
    expect("second prepared replacement is not blocked by old destruction",
           waitUntil([&]() { return finishedSpy.size() == 3; }, 15000));
    const auto latestSnapshot = SemanticIndex::getInstance()->snapshot();
    expect("consecutive replacement publishes the latest snapshot and graph",
           latestSnapshot
               && latestSnapshot->getCachedFileContent(fileName)
                      == secondSaved
               && scheduler.semanticStatus(fileName).state
                      == DocumentSemanticState::Current
               && analyzer.pendingPublicationRetirementsForTesting() >= 2
               && relationshipEngine.getRelationshipCount()
                      == latestSnapshot->relationshipCount());

    std::thread releaseThread([&releaseRetirement]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        releaseRetirement.store(true, std::memory_order_release);
    });
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    // Scheduler shutdown first seals all document/controller request paths;
    // SymbolAnalyzer::shutdown then seals publication before joining pools.
    scheduler.shutdown();
    const qint64 shutdownElapsedMs = shutdownTimer.elapsed();
    releaseThread.join();
    analyzer.setPublicationRetirementGateForTesting({});
    expect("shutdown waits for the dedicated retirement pool",
           shutdownElapsedMs >= 80
               && analyzer.pendingPublicationRetirementsForTesting() == 0);
    const int enqueueCountAfterShutdown =
        analyzer.publicationRetirementEnqueueCountForTesting();
    const std::uint64_t snapshotRevisionAfterShutdown =
        semanticIndex->snapshotRevision();
    QSignalSpy lateAnalysisStartedSpy(&analyzer,
                                      &SymbolAnalyzer::analysisStarted);
    QSignalSpy lateAnalysisCompletedSpy(&analyzer,
                                        &SymbolAnalyzer::analysisCompleted);
    SemanticAnalysisRequest lateRequest;
    lateRequest.generation = 999999;
    lateRequest.reason = SemanticAnalysisReason::Save;
    lateRequest.impactHint = SemanticChangeImpact::TriviaOnly;
    lateRequest.project = projectModel.snapshot();
    lateRequest.triggerFile = fileName;
    lateRequest.changedFiles = {fileName};
    lateRequest.sourceOverrides.insert(fileName, secondSaved);
    analyzer.startSemanticAnalysisAsync(lateRequest);
    analyzer.analyzeFileContent(fileName, secondSaved, 999999);
    editor.setPlainText(QStringLiteral("\n") + secondSaved);
    documents.markSaved(&editor);
    waitForDuration(100);
    expect("sealed shutdown rejects later work and retirement enqueue",
           lateAnalysisStartedSpy.isEmpty()
               && lateAnalysisCompletedSpy.isEmpty()
               && semanticIndex->snapshotRevision()
                      == snapshotRevisionAfterShutdown
               && analyzer.pendingPublicationRetirementsForTesting() == 0
               && analyzer.publicationRetirementEnqueueCountForTesting()
                      == enqueueCountAfterShutdown
               && analyzer.rejectedPublicationRetirementsForTesting() == 0);
    semanticIndex->attachRelationshipEngine(previousRelationshipEngine);
}

void runFailedAnalysisRetainsLastValidSnapshot()
{
    QTemporaryDir directory;
    expect("failed analysis fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;
    const QString fileName = directory.filePath(QStringLiteral("failed.sv"));
    const QString content = QStringLiteral("module failed; endmodule\n");
    QFile file(fileName);
    expect("failed analysis fixture opens",
           file.open(QIODevice::WriteOnly | QIODevice::Text));
    if (!file.isOpen())
        return;
    file.write(content.toUtf8());
    file.close();

    const SemanticSymbolRecord module =
        SemanticFixtureRecordBuilder(
            QStringLiteral("failed"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(fileName)
            .withLocalHandle(1)
            .withTextSpan(7, 6)
            .record();
    const auto validSnapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {module}, {}, {}, {{fileName, content}}));
    SemanticIndex::getInstance()->installPreparedSnapshot(validSnapshot,
                                                          {fileName});
    const std::uint64_t validRevision =
        SemanticIndex::getInstance()->snapshotRevision();

    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    MyCodeEditor editor;
    editor.setPlainText(content);
    documents.registerEditor(&editor, fileName);
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    expect("failed analysis fixture removes external source",
           QFile::remove(fileName));
    scheduler.handleExternalFileChanged(fileName, 0);
    expect("analysis failure produces Failed document state",
           waitUntil(
               [&]() {
                   return scheduler.semanticStatus(fileName).state
                       == DocumentSemanticState::Failed;
               },
               3000));
    expect("analysis failure retains last valid immutable snapshot",
           SemanticIndex::getInstance()->snapshotRevision() == validRevision
               && SemanticIndex::getInstance()->snapshot() == validSnapshot);
    scheduler.shutdown();
}

void runSavedTextMatchingSnapshotSkipsAnalysis()
{
    QTemporaryDir directory;
    expect("matching-save fixture directory is valid", directory.isValid());
    if (!directory.isValid())
        return;
    const QString fileName =
        directory.filePath(QStringLiteral("matching_save.sv"));
    const QString content =
        QStringLiteral("module matching_save; endmodule\n");
    QFile file(fileName);
    expect("matching-save fixture opens",
           file.open(QIODevice::WriteOnly | QIODevice::Text));
    if (!file.isOpen())
        return;
    file.write(content.toUtf8());
    file.close();

    const auto snapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(
                {}, {}, {}, {{fileName, content}}));
    SemanticIndex::getInstance()->installPreparedSnapshot(
        snapshot, {fileName});

    AnalysisScheduler scheduler;
    SymbolAnalyzer analyzer;
    DocumentModel documents;
    scheduler.setSymbolAnalyzer(&analyzer);
    scheduler.setDocumentModel(&documents);
    MyCodeEditor editor;
    editor.setPlainText(content);
    documents.registerEditor(&editor, fileName);
    QSignalSpy startedSpy(
        &scheduler,
        &AnalysisScheduler::workspaceSymbolAnalysisStarted);
    documents.markSaved(&editor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expect("saved text matching published snapshot schedules no worker",
           startedSpy.isEmpty());
    expect("matching saved text remains semantically current",
           scheduler.semanticStatus(fileName).state
               == DocumentSemanticState::Current);
    scheduler.shutdown();
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    runAnalysisRuntimePolicyPlanning();
    runDiagnosticPublicationPolicyOrdering();
    runAnalysisRuntimeEnableDisableAndPublicationLimit();
    runEditDoesNotScheduleSemanticWork();
    runPublicationRefreshesEditorOnce();
    runIncludeResolutionUsesConfiguredSearchOrder();
    runDocumentOpenRequiresMatchingIndexedText();
    runWorkspaceEditDoesNotRestartWorker();
    runSupersededRequestsConvergeDocumentStates();
    runSinglePendingCleanChangeMergesIntoDifferentRequest();
    runSaveQueuesExactlyOnceAndRejectsStaleRevision();
    runExternalChangesConvergeAndCoalesce();
    runFullWorkspaceRebuildDropsRemovedFilesAndKeepsGraph();
    runIncrementalEffectiveFactsKeepInstanceContext();
    runFallbackAndAuthoritativePublicationScopes();
    runClassifierAndImpactPlanning();
    runTriviaSaveRemapsSourceLocationsWithoutSlang();
    runRelationshipReplacementUsesEvidenceOwner();
    runDesignHierarchyTopologyFingerprint();
    runPreparedPublicationRetirementIsOrderedAndShutdownSafe();
    runFailedAnalysisRetainsLastValidSnapshot();
    runSavedTextMatchingSnapshotSkipsAnalysis();
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
