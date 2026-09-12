// Large-file performance baseline. Guards the slang16 regression class where
// whitespace edits in a large workspace file accidentally queued Slang or
// relationship reanalysis. Idle classification is allowed; escalation is not.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QSignalSpy>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>

#define private public
#include "mainwindow.h"
#include "analysisscheduler.h"
#include "documentmodel.h"
#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "semanticchangeclassifier.h"
#include "semanticindex.h"
#include "semanticruntimecoordinator.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "workspacemanager.h"
#undef private

static int g_checks = 0;
static int g_fails = 0;

static void printMetric(const char* name, qint64 value)
{
    std::printf("perf.%s=%lld\n", name, static_cast<long long>(value));
}

static void printTextMetric(const char* name, const QString& value)
{
    std::printf("perf.%s=%s\n", name, value.toLocal8Bit().constData());
}

static void expectBool(const char* what, bool got, bool want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-56s got=%s want=%s\n",
           ok ? "PASS" : "FAIL", what, got ? "true" : "false", want ? "true" : "false");
}

static void expectInt(const char* what, int got, int want)
{
    ++g_checks;
    const bool ok = (got == want);
    if (!ok)
        ++g_fails;
    printf("[%s] %-56s got=%d want=%d\n", ok ? "PASS" : "FAIL", what, got, want);
}

static bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate())
            return true;
        QTest::qWait(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

static QString largestFile(const QStringList& files)
{
    QStringList sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).size() > QFileInfo(b).size();
    });
    return sorted.isEmpty() ? QString() : sorted.first();
}

static QString largestIndexedFile(const QStringList& files)
{
    const auto snapshot = SemanticIndex::getInstance()->snapshot();
    if (!snapshot)
        return largestFile(files);
    QStringList indexed;
    for (const QString& fileName : files) {
        if (!snapshot->getSymbolRecords(fileName).isEmpty())
            indexed.append(fileName);
    }
    return indexed.isEmpty() ? largestFile(files) : largestFile(indexed);
}

static QString readTextFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QTextStream(&file).readAll();
}

static void runRealSourceColumnProbe(const QString& fileName)
{
    const QString source = readTextFile(fileName);
    QString longest;
    QString shortest;
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.trimmed().isEmpty()
            || line.size() > 72) {
            continue;
        }
        if (longest.isEmpty()
            || line.size() > longest.size()) {
            longest = line;
        }
        if (shortest.isEmpty()
            || line.size() < shortest.size()) {
            shortest = line;
        }
    }
    expectBool("real source provides distinct long and short lines",
               !longest.isEmpty()
                   && !shortest.isEmpty()
                   && longest != shortest,
               true);
    if (longest.isEmpty()
        || shortest.isEmpty()
        || longest == shortest) {
        return;
    }

    MyCodeEditor editor;
    editor.resize(900, 150);
    editor.setLineWrapMode(QPlainTextEdit::NoWrap);
    editor.setFont(
        QFontDatabase::systemFont(
            QFontDatabase::FixedFont));
    editor.setPlainText(
        longest + QLatin1Char('\n')
        + shortest + QLatin1Char('\n'));
    editor.document()->setModified(false);
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 30);

    const qreal space = qMax<qreal>(
        1.0,
        QFontMetricsF(editor.font())
            .horizontalAdvance(QLatin1Char(' ')));
    const auto endVisual = [&](int line) {
        const QTextBlock block =
            editor.document()->findBlockByNumber(line);
        QTextCursor start(block);
        start.setPosition(block.position());
        QTextCursor end(block);
        end.setPosition(
            block.position() + block.text().size());
        return qMax(
            0,
            qRound((editor.cursorRect(end).left()
                    - editor.cursorRect(start).left())
                   / space));
    };
    const auto pointAt = [&](int line, int column) {
        const QTextBlock block =
            editor.document()->findBlockByNumber(line);
        QTextCursor start(block);
        start.setPosition(block.position());
        const QRect rect = editor.cursorRect(start);
        return QPoint(
            qRound(rect.left() + column * space),
            rect.center().y());
    };

    const int firstEnd = endVisual(0);
    const int secondEnd = endVisual(1);
    const int targetColumn =
        qMax(firstEnd, secondEnd) + 3;
    const QString before = editor.toPlainText();
    QTest::mouseClick(
        editor.viewport(),
        Qt::LeftButton,
        Qt::NoModifier,
        pointAt(0, targetColumn));
    expectBool("real long-short first endpoint stays at real EOL",
               !editor.virtualCursorActiveForTest()
                   && editor.textCursor().blockNumber() == 0
                   && editor.textCursor().positionInBlock()
                          == longest.size(),
               true);
    QTest::mouseClick(
        editor.viewport(),
        Qt::LeftButton,
        Qt::ShiftModifier | Qt::AltModifier,
        pointAt(1, targetColumn));
    expectBool("real long-short column endpoints stay virtual",
               editor.columnSelectionActive()
                   && editor.toPlainText() == before
                   && !editor.document()->isModified(),
               true);

    QTest::keyClicks(&editor, "X");
    const QString expected =
        longest
        + QString(targetColumn - firstEnd,
                  QLatin1Char(' '))
        + QStringLiteral("X\n")
        + shortest
        + QString(targetColumn - secondEnd,
                  QLatin1Char(' '))
        + QStringLiteral("X\n");
    expectBool("real long-short column edit materializes only final padding",
               editor.toPlainText() == expected,
               true);
}

static bool sourceHasTreeSitterComment(const QString& source)
{
    if (source.isEmpty())
        return false;
    TSDocument syntax;
    syntax.setText(source);
    int probe = 0;
    while (probe < source.size()) {
        const int line = source.indexOf(
            QStringLiteral("//"), probe);
        const int block = source.indexOf(
            QStringLiteral("/*"), probe);
        int candidate = -1;
        if (line >= 0 && block >= 0)
            candidate = qMin(line, block);
        else
            candidate = qMax(line, block);
        if (candidate < 0)
            break;
        if (syntax.isCommentAt(candidate + 1))
            return true;
        probe = candidate + 2;
    }
    return false;
}

static qint64 totalFileBytes(const QStringList& files)
{
    qint64 total = 0;
    for (const QString& fileName : files) {
        const qint64 size = QFileInfo(fileName).size();
        if (size > 0)
            total += size;
    }
    return total;
}

static bool exceedsAutomaticWorkspaceBudget(const QStringList& files)
{
    return files.size() > 160
        || totalFileBytes(files) > 8 * 1024 * 1024;
}

static void drainRelationshipWork(MainWindow& window)
{
    SmartRelationshipBuilder* builder = window.semanticRuntime
        ? window.semanticRuntime->relationshipBuilder()
        : nullptr;
    if (builder)
        builder->cancelAnalysis();
    if (window.analysisScheduler) {
        window.analysisScheduler->cancelAllScheduledRelationshipAnalyses();
        window.analysisScheduler->cancelRelationshipAnalysis();
        window.analysisScheduler->cancelWorkspaceRelationshipAnalysis();
    }
}

static QString normalizedPath(const QString& fileName)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

static bool hasActiveRelationshipDebounce(MainWindow& window, const QString& fileName)
{
    if (!window.analysisScheduler)
        return false;

    return window.analysisScheduler->hasScheduledRelationshipAnalysis(
        normalizedPath(fileName));
}

static bool containsFile(const QStringList& files, const QString& fileName)
{
    const QString expected = normalizedPath(fileName);
    for (const QString& candidate : files) {
        if (normalizedPath(candidate).compare(expected, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QString workspacePath;
    bool waitForFullAnalysis = false;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QStringLiteral("--wait-for-analysis")) {
            waitForFullAnalysis = true;
        } else if (workspacePath.isEmpty()) {
            workspacePath = argument;
        }
    }
    if (workspacePath.isEmpty()) {
        workspacePath = QDir::current().absoluteFilePath(
            QStringLiteral("test_sv/new"));
    }

    expectBool("workspace fixture exists", QFileInfo(workspacePath).isDir(), true);

    MainWindow window;
    bool workspaceSymbolsDone = false;
    bool workspaceSymbolsStarted = false;
    bool workspaceSymbolsDeferred = false;
    bool workspaceFilesScanned = false;
    SemanticAnalysisTelemetry workerTelemetry;
    SemanticAnalysisTelemetry publicationTelemetry;
    SemanticAnalysisTelemetry navigationTelemetry;
    IncrementalAnalysisPlan latestSavePlan;
    SemanticAnalysisTelemetry saveWorkerTelemetry;
    SemanticAnalysisTelemetry savePublicationTelemetry;
    SemanticAnalysisTelemetry saveNavigationTelemetry;
    int savePlanCount = 0;
    bool sawSaveWorkerTelemetry = false;
    bool sawSavePublicationTelemetry = false;
    bool sawSaveNavigationTelemetry = false;
    bool sawWorkerTelemetry = false;
    bool sawPublicationTelemetry = false;
    bool sawNavigationTelemetry = false;
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::semanticAnalysisTelemetry,
                     &window,
                     [&](const SemanticAnalysisTelemetry& telemetry) {
                         if (telemetry.stage == SemanticAnalysisStage::Worker) {
                             workerTelemetry = telemetry;
                             sawWorkerTelemetry = true;
                         } else if (telemetry.stage
                                    == SemanticAnalysisStage::Publication) {
                             publicationTelemetry = telemetry;
                             sawPublicationTelemetry = true;
                         } else if (telemetry.stage
                                        == SemanticAnalysisStage::Navigation
                                    && telemetry.generation > 0) {
                             navigationTelemetry = telemetry;
                             sawNavigationTelemetry = true;
                         }
                         if (telemetry.reason == SemanticAnalysisReason::Save) {
                             if (telemetry.stage
                                 == SemanticAnalysisStage::Worker) {
                                 saveWorkerTelemetry = telemetry;
                                 sawSaveWorkerTelemetry = true;
                             } else if (telemetry.stage
                                        == SemanticAnalysisStage::Publication) {
                                 savePublicationTelemetry = telemetry;
                                 sawSavePublicationTelemetry = true;
                             } else if (telemetry.stage
                                        == SemanticAnalysisStage::Navigation) {
                                 saveNavigationTelemetry = telemetry;
                                 sawSaveNavigationTelemetry = true;
                             }
                         }
                     });
    QObject::connect(window.navigationManager.get(),
                     &NavigationManager::navigationTelemetry,
                     &window,
                     [&](const SemanticAnalysisTelemetry& telemetry) {
                         if (telemetry.generation > 0) {
                             navigationTelemetry = telemetry;
                             sawNavigationTelemetry = true;
                         }
                         if (telemetry.reason == SemanticAnalysisReason::Save) {
                             saveNavigationTelemetry = telemetry;
                             sawSaveNavigationTelemetry = true;
                         }
                     });
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::semanticAnalysisPlanPrepared,
                     &window,
                     [&](const IncrementalAnalysisPlan& plan) {
                         if (plan.reason == SemanticAnalysisReason::Save) {
                             latestSavePlan = plan;
                             ++savePlanCount;
                         }
                     });
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisStarted,
                     &window,
                     [&](const ProjectSnapshot&, int) {
                         workspaceSymbolsStarted = true;
                     });
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &window,
                     [&](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                         Q_UNUSED(filesAnalyzed)
                         Q_UNUSED(totalSymbols)
                         workspaceSymbolsDone = true;
                     });
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisDeferred,
                     &window,
                     [&](const ProjectSnapshot&,
                         int,
                         qint64 totalBytes,
                         qint64 largestFileBytes) {
                         Q_UNUSED(totalBytes)
                         Q_UNUSED(largestFileBytes)
                         workspaceSymbolsDeferred = true;
                     });
    QObject::connect(window.workspaceManager.get(),
                     &WorkspaceManager::filesScanned,
                     &window,
                     [&](const QStringList&) {
                         workspaceFilesScanned = true;
                     });

    window.resize(1100, 760);
    window.show();
    expectBool("main window visible", waitUntil([&]() { return window.isVisible(); }, 2000), true);

    int workspaceEventTicks = 0;
    qint64 workspaceMaxEventGapMs = 0;
    QElapsedTimer workspaceHeartbeatClock;
    workspaceHeartbeatClock.start();
    qint64 lastHeartbeatMs = workspaceHeartbeatClock.elapsed();
    QTimer workspaceHeartbeat;
    QObject::connect(&workspaceHeartbeat, &QTimer::timeout, &window, [&]() {
        const qint64 now = workspaceHeartbeatClock.elapsed();
        workspaceMaxEventGapMs = qMax(workspaceMaxEventGapMs,
                                      now - lastHeartbeatMs);
        lastHeartbeatMs = now;
        ++workspaceEventTicks;
    });
    workspaceHeartbeat.start(10);

    QElapsedTimer openTimer;
    openTimer.start();
    expectBool("open workspace", window.workspaceManager->openWorkspace(workspacePath), true);
    const qint64 openElapsedMs = openTimer.elapsed();
    expectBool("workspace open returns before full scan blocks UI",
               openElapsedMs < 1000,
               true);
    expectBool("workspace file scan completes",
               waitUntil([&]() { return workspaceFilesScanned; }, 10000),
               true);

    const QStringList workspaceFiles =
        window.workspaceManager->getSystemVerilogFiles();
    printTextMetric("workspace", normalizedPath(workspacePath));
    printMetric("workspace_files", workspaceFiles.size());
    printMetric("workspace_bytes", totalFileBytes(workspaceFiles));
    printMetric("workspace_open_ms", openElapsedMs);
    const bool budgetedWorkspace =
        exceedsAutomaticWorkspaceBudget(workspaceFiles);
    if (budgetedWorkspace) {
        expectBool("workspace exceeds automatic analysis budget",
                   workspaceFiles.size() > 160
                       && totalFileBytes(workspaceFiles) > 8 * 1024 * 1024,
                   true);
        expectBool("budgeted workspace opens promptly",
                   openElapsedMs < 5000,
                   true);
        expectBool("budgeted workspace analysis deferred",
                   workspaceSymbolsDeferred,
                   false);
        expectBool("budgeted workspace starts background symbol sweep",
                   waitUntil([&]() { return workspaceSymbolsStarted; }, 3000),
                   true);

        QElapsedTimer responsivenessTimer;
        responsivenessTimer.start();
        expectBool("budgeted workspace keeps event loop responsive",
                   waitUntil([&]() {
                       return responsivenessTimer.elapsed() >= 400
                           && workspaceEventTicks >= 10;
                   }, 2000),
                   true);

        const QString largeFile = largestFile(workspaceFiles);
        expectBool("budgeted workspace largest file selected",
                   QFileInfo(largeFile).size() > 1024 * 1024,
                   true);

        QElapsedTimer openLargeTimer;
        openLargeTimer.start();
        expectBool("budgeted workspace opens largest file",
                   window.tabManager->openFileInTab(largeFile),
                   true);
        const qint64 openLargeElapsedMs = openLargeTimer.elapsed();
        printMetric("largest_file_open_ms", openLargeElapsedMs);
        expectBool("largest file open remains bounded",
                   openLargeElapsedMs < 6000,
                   true);

        // Loading a multi-megabyte editor is a separate synchronous path.
        // Measure semantic worker/publication responsiveness from this point.
        workspaceEventTicks = 0;
        workspaceMaxEventGapMs = 0;
        lastHeartbeatMs = workspaceHeartbeatClock.elapsed();

        MyCodeEditor* editor = window.tabManager->getCurrentEditor();
        expectBool("budgeted workspace large editor exists",
                   editor != nullptr,
                   true);
        if (editor) {
            int scrollTicks = 0;
            QTimer scrollHeartbeat;
            QObject::connect(&scrollHeartbeat, &QTimer::timeout, &window, [&]() {
                ++scrollTicks;
            });
            scrollHeartbeat.start(10);

            QElapsedTimer scrollTimer;
            scrollTimer.start();
            QScrollBar* scrollBar = editor->verticalScrollBar();
            for (int i = 0; i < 80 && scrollBar; ++i) {
                scrollBar->setValue(scrollBar->value() + 25);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            }
            expectBool("largest file scroll pumps events",
                       waitUntil([&]() {
                           return scrollTimer.elapsed() >= 300
                               && scrollTicks >= 8;
                       }, 2000),
                       true);
            scrollHeartbeat.stop();
        }

        if (waitForFullAnalysis) {
            expectBool("budgeted workspace full analysis completes",
                       waitUntil([&]() { return workspaceSymbolsDone; },
                                 600000),
                       true);
            waitUntil([&]() { return sawNavigationTelemetry; }, 2000);
        } else if (window.analysisScheduler) {
            window.analysisScheduler->cancelWorkspaceAnalysis();
        }
        workspaceHeartbeat.stop();
        printMetric("workspace_event_ticks", workspaceEventTicks);
        printMetric("workspace_max_event_gap_ms", workspaceMaxEventGapMs);
        printMetric("analysis_phase_max_event_gap_ms",
                    workspaceMaxEventGapMs);
        expectBool("background analysis avoids multi-second UI stalls",
                   workspaceMaxEventGapMs < 1500,
                   true);
        if (sawWorkerTelemetry) {
            printMetric("analysis_generation",
                        static_cast<qint64>(workerTelemetry.generation));
            printTextMetric("analysis_reason",
                            semanticAnalysisReasonName(workerTelemetry.reason));
            printTextMetric("analysis_impact",
                            semanticChangeImpactName(workerTelemetry.impact));
            printMetric("analysis_files", workerTelemetry.files.size());
            printMetric("analysis_changed_files",
                        workerTelemetry.changedFiles.size());
            printMetric("analysis_worker_ms", workerTelemetry.workerMs);
            printMetric("analysis_slang_invoked",
                        workerTelemetry.slangInvoked ? 1 : 0);
        }
        if (sawPublicationTelemetry) {
            printMetric("analysis_publication_ms",
                        publicationTelemetry.publicationMs);
            printMetric("analysis_effective_facts_ms",
                        publicationTelemetry.effectiveFactsMs);
            printMetric("analysis_snapshot_install_ms",
                        publicationTelemetry.snapshotInstallMs);
        }
        printMetric("analysis_ui_refresh_ms",
                    sawNavigationTelemetry
                        ? navigationTelemetry.uiRefreshMs
                        : 0);
        printMetric("analysis_navigation_refresh_observed",
                    sawNavigationTelemetry ? 1 : 0);
        drainRelationshipWork(window);
        if (!waitForFullAnalysis) {
            printf("\n%d checks, %d failed\n", g_checks, g_fails);
            return g_fails == 0 ? 0 : 1;
        }
    }

    if (!budgetedWorkspace) {
        expectBool("workspace symbol analysis completes",
                   waitUntil([&]() { return workspaceSymbolsDone; }, 60000), true);
        waitUntil([&]() { return sawNavigationTelemetry; }, 2000);
        workspaceHeartbeat.stop();
        printMetric("workspace_event_ticks", workspaceEventTicks);
        printMetric("workspace_max_event_gap_ms", workspaceMaxEventGapMs);
        if (sawWorkerTelemetry) {
            printMetric("analysis_generation",
                        static_cast<qint64>(workerTelemetry.generation));
            printTextMetric("analysis_reason",
                            semanticAnalysisReasonName(workerTelemetry.reason));
            printTextMetric("analysis_impact",
                            semanticChangeImpactName(workerTelemetry.impact));
            printMetric("analysis_files", workerTelemetry.files.size());
            printMetric("analysis_changed_files", workerTelemetry.changedFiles.size());
            printMetric("analysis_worker_ms", workerTelemetry.workerMs);
            printMetric("analysis_slang_invoked",
                        workerTelemetry.slangInvoked ? 1 : 0);
        }
        if (sawPublicationTelemetry) {
            printMetric("analysis_publication_ms", publicationTelemetry.publicationMs);
            printMetric("analysis_effective_facts_ms",
                        publicationTelemetry.effectiveFactsMs);
            printMetric("analysis_snapshot_install_ms",
                        publicationTelemetry.snapshotInstallMs);
        }
        printMetric("analysis_ui_refresh_ms",
                    sawNavigationTelemetry ? navigationTelemetry.uiRefreshMs : 0);
        printMetric("analysis_navigation_refresh_observed",
                    sawNavigationTelemetry ? 1 : 0);
    }

    const qint64 baselineFullWorkerMs = workerTelemetry.workerMs;
    const auto realSnapshot =
        SemanticIndex::getInstance()->snapshot();
    bool hasRealModule = false;
    bool hasRealInstance = false;
    bool hasRealParameter = false;
    bool hasRealBoundInstanceValue = false;
    if (realSnapshot) {
        for (const QString& fileName : workspaceFiles) {
            const QList<SemanticSymbolRecord> records =
                realSnapshot->getSymbolRecords(fileName);
            for (const SemanticSymbolRecord& record : records) {
                const SymbolTaxonomy::SemanticMetadata metadata =
                    semanticMetadataForSymbolRecord(record);
                hasRealModule = hasRealModule
                    || SymbolTaxonomy::isModuleDeclaration(
                        metadata);
                hasRealInstance = hasRealInstance
                    || SymbolTaxonomy::isInstanceDeclaration(
                        metadata);
                hasRealParameter = hasRealParameter
                    || record.declarationKind
                           == SymbolTaxonomy::DeclarationKind::Parameter
                    || record.declarationKind
                           == SymbolTaxonomy::DeclarationKind::Localparam;
                hasRealBoundInstanceValue =
                    hasRealBoundInstanceValue
                    || !record.presentation
                            .instanceInfoByPath.isEmpty();
            }
        }
    }
    expectBool("real workspace publishes module declarations",
               hasRealModule,
               true);
    expectBool("real workspace publishes module instances",
               hasRealInstance,
               true);
    expectBool("real workspace publishes parameter declarations",
               hasRealParameter,
               true);
    expectBool("real workspace publishes bound instance values",
               hasRealBoundInstanceValue,
               true);

    const QString largeFile = largestIndexedFile(
        window.workspaceManager->getSystemVerilogFiles());
    expectBool("large file selected", QFileInfo(largeFile).size() > 20000, true);
    const QString realLargeSource =
        readTextFile(largeFile);
    expectBool("real workspace comments are Tree-sitter classified",
               sourceHasTreeSitterComment(realLargeSource),
               true);
    runRealSourceColumnProbe(largeFile);

    int largeFileAnalysisCount = 0;
    QObject::connect(window.analysisScheduler.get(), &AnalysisScheduler::fileSymbolAnalysisFinished,
                     &window, [&](const QString& fileName, int symbolsFound) {
                         Q_UNUSED(symbolsFound)
                         if (QFileInfo(fileName).absoluteFilePath()
                             == QFileInfo(largeFile).absoluteFilePath()) {
                             largeFileAnalysisCount++;
                         }
                     });

    expectBool("open large file", window.tabManager->openFileInTab(largeFile), true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    expectInt("opening workspace-cached file queues no symbol analysis",
              largeFileAnalysisCount,
              0);

    MyCodeEditor* editor = window.tabManager->getCurrentEditor();
    expectBool("large editor exists", editor != nullptr, true);

    if (editor) {
        drainRelationshipWork(window);

        const QString originalContent = editor->toPlainText();
        SymbolAnalyzer changeDetector;
        QString blankShiftedContent = originalContent;
        blankShiftedContent.insert(0, QStringLiteral("\n\n"));
        expectBool("blank-line insertion is not significant",
                   changeDetector.hasSignificantChanges(originalContent, blankShiftedContent), false);

        const QString structuralContent =
            originalContent + QStringLiteral("\nmodule __zeroslack_perf_probe; endmodule\n");
        expectBool("structural edit remains significant",
                   changeDetector.hasSignificantChanges(originalContent, structuralContent), true);

        editor->setFocus();
        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        editor->setTextCursor(cursor);

        QSignalSpy symbolAnalysisStarted(window.analysisScheduler.get(),
                                         &AnalysisScheduler::fileSymbolAnalysisStarted);
        QSignalSpy workspaceAnalysisRestarted(
            window.analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisStarted);
        int semanticWorkerStages = 0;
        int semanticPublications = 0;
        int hierarchyRebuilds = 0;
        int idleRequests = 0;
        int otherRequests = 0;
        int idleRejections = 0;
        bool idleRequestsStaySingleFile = true;
        bool idleSlangInvoked = false;
        const auto preEditSnapshot = SemanticIndex::getInstance()->snapshot();
        const auto preEditRevision = SemanticIndex::getInstance()->snapshotRevision();
        QObject::connect(
            window.analysisScheduler.get(),
            &AnalysisScheduler::semanticAnalysisTelemetry,
            &window,
            [&](const SemanticAnalysisTelemetry& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Scheduling) {
                    if (telemetry.reason == SemanticAnalysisReason::EditIdle) {
                        ++idleRequests;
                        idleRequestsStaySingleFile = idleRequestsStaySingleFile
                            && telemetry.changedFiles.size() == 1
                            && telemetry.changedFiles.contains(largeFile, Qt::CaseInsensitive);
                    } else {
                        ++otherRequests;
                    }
                }
                if (telemetry.reason == SemanticAnalysisReason::EditIdle)
                    idleSlangInvoked = idleSlangInvoked || telemetry.slangInvoked;
                if (telemetry.stage == SemanticAnalysisStage::Worker)
                    ++semanticWorkerStages;
                else if (telemetry.stage == SemanticAnalysisStage::Publication)
                    ++semanticPublications;
            });
        QObject::connect(window.analysisScheduler->symbolAnalyzer.data(),
                         &SymbolAnalyzer::semanticAnalysisDropped, &window,
                         [&](const SemanticAnalysisRequest& request,
                             SemanticAnalysisRequestDisposition disposition) {
            if (request.reason == SemanticAnalysisReason::EditIdle
                && disposition == SemanticAnalysisRequestDisposition::TriviaGateRejected)
                ++idleRejections;
        });
        QObject::connect(
            window.navigationManager.get(),
            &NavigationManager::navigationTelemetry,
            &window,
            [&](const SemanticAnalysisTelemetry& telemetry) {
                if (telemetry.detail.contains(
                        QStringLiteral("hierarchyRebuild=1"))) {
                    ++hierarchyRebuilds;
                }
            });
        drainRelationshipWork(window);

        const int beforeLength = editor->toPlainText().size();
        QString triviaBurst;
        for (int index = 0; index < 100; ++index) {
            triviaBurst += QStringLiteral("\n  // perf trivia %1  ")
                               .arg(index);
        }
        editor->insertPlainText(triviaBurst);
        for (int i = 0; i < 20; ++i) {
            QTest::keyClick(editor, Qt::Key_Down);
            QTest::keyClick(editor, Qt::Key_Up);
        }
        // The historical edit debounce was 1000 / 1500 ms. Keep pumping the
        // event loop beyond both deadlines so this assertion observes queued
        // work instead of passing before the timer fires.
        waitUntil([]() { return false; }, 2200);

        expectBool("100 whitespace/comment edits applied",
                   editor->toPlainText().size()
                       >= beforeLength + triviaBurst.size(),
                   true);
        expectBool("whitespace edit does not start relationship debounce",
                   hasActiveRelationshipDebounce(window, largeFile),
                   false);
        expectInt("whitespace burst schedules exactly one idle classification",
                  symbolAnalysisStarted.count(), 1);
        expectInt("whitespace burst uses the EditIdle reason", idleRequests, 1);

        // This burst has no final newline, so it also comments out the first
        // source line. Both it and the following identifier edit must be
        // classified and rejected without semantic or relationship publication.
        QTest::keyClicks(editor, "x");
        waitUntil([]() { return false; }, 2200);
        expectBool("ordinary edit does not start relationship debounce",
                    hasActiveRelationshipDebounce(window, largeFile),
                    false);
        expectInt("ordinary edit schedules exactly one more idle classification",
                  symbolAnalysisStarted.count(), 2);
        expectInt("both edit requests use the EditIdle reason", idleRequests, 2);
        expectInt("edits trigger no save or workspace requests", otherRequests, 0);
        expectInt("non-inert idle edits are explicitly rejected", idleRejections, 2);
        expectBool("idle requests remain single-file", idleRequestsStaySingleFile, true);
        expectBool("rejected idle requests invoke no Slang", idleSlangInvoked, false);
        expectBool("rejected idle edits retain snapshot and diagnostics",
                   SemanticIndex::getInstance()->snapshot() == preEditSnapshot
                       && SemanticIndex::getInstance()->snapshotRevision() == preEditRevision,
                   true);
        expectBool("rejected idle edits remain Dirty",
                   window.analysisScheduler->semanticStatus(largeFile).state == DocumentSemanticState::Dirty,
                   true);
        expectInt("ordinary edits run no semantic worker stage",
                  semanticWorkerStages, 0);
        expectInt("ordinary edits publish no semantic snapshot",
                  semanticPublications, 0);
        expectInt("executor starts only the two idle classification requests",
                  workspaceAnalysisRestarted.count(), idleRequests);
        expectInt("ordinary edits rebuild no Design hierarchy",
                  hierarchyRebuilds, 0);
        drainRelationshipWork(window);

        DocumentModel* documents = window.tabManager->getDocumentModel();
        expectBool("document model is available for save regression",
                   documents != nullptr,
                   true);
        const auto baselineSnapshot = SemanticIndex::getInstance()->snapshot();
        SemanticSymbolRecord locationAnchor;
        bool locationAnchorFound = false;
        if (baselineSnapshot) {
            const QList<SemanticSymbolRecord> baselineRecords =
                baselineSnapshot->getSymbolRecords(largeFile);
            for (const SemanticSymbolRecord& record : baselineRecords) {
                if (record.localHandle >= 0 && record.location.isValid()) {
                    locationAnchor = record;
                    locationAnchorFound = true;
                    break;
                }
            }
        }
        expectBool("trivia save has an indexed source-location anchor",
                   locationAnchorFound,
                   true);

        if (documents && locationAnchorFound) {
            const QString savedTriviaPrefix =
                QStringLiteral("\n// ZeroSlack saved trivia perf probe\n");
            const QString savedTriviaText = savedTriviaPrefix + originalContent;
            editor->setPlainText(savedTriviaText);
            const int planCountBeforeTrivia = savePlanCount;
            latestSavePlan = {};
            saveWorkerTelemetry = {};
            savePublicationTelemetry = {};
            saveNavigationTelemetry = {};
            sawSaveWorkerTelemetry = false;
            sawSavePublicationTelemetry = false;
            sawSaveNavigationTelemetry = false;
            hierarchyRebuilds = 0;
            QSignalSpy triviaFinished(
                window.analysisScheduler.get(),
                &AnalysisScheduler::workspaceSymbolAnalysisFinished);

            QElapsedTimer triviaSaveHandlerTimer;
            triviaSaveHandlerTimer.start();
            documents->markSaved(editor);
            const qint64 triviaSaveHandlerMs =
                triviaSaveHandlerTimer.elapsed();
            expectBool("TriviaOnly save handler returns without worker wait",
                       triviaSaveHandlerMs < 100,
                       true);
            expectBool(
                "TriviaOnly save completes through scheduler publication",
                waitUntil([&]() {
                    return !triviaFinished.isEmpty()
                        && savePlanCount > planCountBeforeTrivia
                        && sawSaveWorkerTelemetry
                        && sawSavePublicationTelemetry;
                }, budgetedWorkspace ? 120000 : 30000),
                true);
            if (sawSaveWorkerTelemetry) {
                waitUntil([&]() {
                    return sawSaveNavigationTelemetry
                        && saveNavigationTelemetry.generation
                               == saveWorkerTelemetry.generation;
                }, 5000);
            }

            expectBool("saved trivia is classified TriviaOnly",
                       latestSavePlan.impact
                           == SemanticChangeImpact::TriviaOnly,
                       true);
            expectInt("TriviaOnly save affects only saved file",
                      latestSavePlan.affectedFiles.size(),
                      1);
            expectBool("TriviaOnly affected scope contains saved file",
                       containsFile(latestSavePlan.affectedFiles, largeFile),
                       true);
            expectInt("TriviaOnly save requires no compilation files",
                      latestSavePlan.compilationFiles.size(),
                      0);
            expectBool("TriviaOnly save does not invoke Slang",
                       sawSaveWorkerTelemetry
                           && !saveWorkerTelemetry.slangInvoked,
                       true);
            expectBool("TriviaOnly save publishes current document text",
                       SemanticIndex::getInstance()
                               ->snapshot()
                               ->getCachedFileContent(largeFile)
                           == savedTriviaText,
                       true);
            expectBool("TriviaOnly save converges document state Current",
                       window.analysisScheduler->semanticStatus(largeFile).state
                           == DocumentSemanticState::Current,
                       true);
            expectBool("TriviaOnly save emits navigation/UI telemetry",
                       sawSaveNavigationTelemetry,
                       true);
            if (budgetedWorkspace && sawWorkerTelemetry) {
                expectBool("TriviaOnly remap is significantly faster than full Slang",
                           saveWorkerTelemetry.workerMs * 2
                               < baselineFullWorkerMs,
                           true);
            }
            expectInt("TriviaOnly save rebuilds no Design hierarchy",
                      hierarchyRebuilds,
                      0);

            const auto triviaSnapshot = SemanticIndex::getInstance()->snapshot();
            bool anchorRemapped = false;
            if (triviaSnapshot) {
                const QList<SemanticSymbolRecord> remappedRecords =
                    triviaSnapshot->getSymbolRecords(largeFile);
                for (const SemanticSymbolRecord& record : remappedRecords) {
                    if (record.localHandle == locationAnchor.localHandle) {
                        anchorRemapped =
                            record.location.position
                                == locationAnchor.location.position
                                       + savedTriviaPrefix.size()
                            && record.location.startLine
                                   == locationAnchor.location.startLine
                                          + savedTriviaPrefix.count(
                                              QLatin1Char('\n'));
                        break;
                    }
                }
            }
            expectBool("TriviaOnly save remaps indexed source locations",
                       anchorRemapped,
                       true);

            printTextMetric("save_trivia_file", normalizedPath(largeFile));
            printTextMetric(
                "save_trivia_impact",
                semanticChangeImpactName(latestSavePlan.impact));
            printMetric("save_trivia_generation",
                        static_cast<qint64>(saveWorkerTelemetry.generation));
            printMetric("save_trivia_affected_files",
                        latestSavePlan.affectedFiles.size());
            printMetric("save_trivia_compilation_files",
                        latestSavePlan.compilationFiles.size());
            printMetric("save_trivia_changed_files",
                        latestSavePlan.changedFiles.size());
            printMetric("save_trivia_slang_invoked",
                        saveWorkerTelemetry.slangInvoked ? 1 : 0);
            printMetric("save_trivia_save_handler_ms",
                        triviaSaveHandlerMs);
            printMetric("save_trivia_worker_ms",
                        saveWorkerTelemetry.workerMs);
            printMetric("save_trivia_publication_ms",
                        savePublicationTelemetry.publicationMs);
            printMetric("save_trivia_effective_facts_ms",
                        savePublicationTelemetry.effectiveFactsMs);
            printMetric("save_trivia_snapshot_install_ms",
                        savePublicationTelemetry.snapshotInstallMs);
            printMetric("save_trivia_ui_refresh_ms",
                        sawSaveNavigationTelemetry
                            ? saveNavigationTelemetry.uiRefreshMs
                            : 0);

            // Restore only the editor overlay. No fixture file is written.
            editor->setPlainText(originalContent);
        }

        if (documents && !budgetedWorkspace) {
            const QString localBodyFile = QDir(workspacePath).absoluteFilePath(
                QStringLiteral("elec_phy_import/top/rtl_top.sv"));
            const QString localBodyOriginal = readTextFile(localBodyFile);
            const QString oldAssignment = QStringLiteral(
                "assign chl0_prot_out_ss_OUT_side = 'd1;");
            const QString newAssignment = QStringLiteral(
                "assign chl0_prot_out_ss_OUT_side = 'd0;");
            QString localBodySavedText = localBodyOriginal;
            const bool localBodyEditApplied =
                localBodySavedText.replace(oldAssignment, newAssignment)
                    != localBodyOriginal;
            expectBool("LocalBody perf fixture statement is present",
                       localBodyEditApplied,
                       true);
            const SemanticChangeClassification localClassification =
                SemanticChangeClassifier().classify(
                    localBodyFile,
                    localBodyOriginal,
                    localBodySavedText);
            expectBool("LocalBody perf edit classifies before save",
                       localClassification.impact
                           == SemanticChangeImpact::LocalBody,
                       true);
            expectBool("open LocalBody save file",
                       window.tabManager->openFileInTab(localBodyFile),
                       true);
            MyCodeEditor* localBodyEditor =
                window.tabManager->getCurrentEditor();
            expectBool("LocalBody editor exists",
                       localBodyEditor != nullptr,
                       true);
            if (localBodyEditor && localBodyEditApplied) {
                localBodyEditor->setPlainText(localBodySavedText);
                const int planCountBeforeLocal = savePlanCount;
                latestSavePlan = {};
                saveWorkerTelemetry = {};
                savePublicationTelemetry = {};
                saveNavigationTelemetry = {};
                sawSaveWorkerTelemetry = false;
                sawSavePublicationTelemetry = false;
                sawSaveNavigationTelemetry = false;
                hierarchyRebuilds = 0;
                QSignalSpy localBodyFinished(
                    window.analysisScheduler.get(),
                    &AnalysisScheduler::workspaceSymbolAnalysisFinished);

                QElapsedTimer localSaveHandlerTimer;
                localSaveHandlerTimer.start();
                documents->markSaved(localBodyEditor);
                const qint64 localSaveHandlerMs =
                    localSaveHandlerTimer.elapsed();
                expectBool("LocalBody save handler returns without worker wait",
                           localSaveHandlerMs < 100,
                           true);
                expectBool(
                    "LocalBody save completes through scheduler publication",
                    waitUntil([&]() {
                        return !localBodyFinished.isEmpty()
                            && savePlanCount > planCountBeforeLocal
                            && sawSaveWorkerTelemetry
                            && sawSavePublicationTelemetry;
                    }, 120000),
                    true);
                if (sawSaveWorkerTelemetry) {
                    waitUntil([&]() {
                        return sawSaveNavigationTelemetry
                            && saveNavigationTelemetry.generation
                                   == saveWorkerTelemetry.generation;
                    }, 5000);
                }

                expectBool("real save path plans LocalBody incrementally",
                           latestSavePlan.impact
                                   == SemanticChangeImpact::LocalBody
                               && latestSavePlan.impact
                                      != SemanticChangeImpact::FullFallback,
                           true);
                expectInt("LocalBody save publishes only edited file",
                          latestSavePlan.affectedFiles.size(),
                          1);
                expectBool("LocalBody affected scope contains edited file",
                           containsFile(latestSavePlan.affectedFiles,
                                        localBodyFile),
                           true);
                expectBool("LocalBody compilation includes edited file",
                           containsFile(latestSavePlan.compilationFiles,
                                        localBodyFile),
                           true);
                expectBool("LocalBody save invokes Slang",
                           sawSaveWorkerTelemetry
                               && saveWorkerTelemetry.slangInvoked,
                           true);
                expectBool("LocalBody save converges document state Current",
                           window.analysisScheduler
                                   ->semanticStatus(localBodyFile)
                                   .state
                               == DocumentSemanticState::Current,
                           true);
                expectBool("LocalBody save publishes edited overlay text",
                           SemanticIndex::getInstance()
                                   ->snapshot()
                                   ->getCachedFileContent(localBodyFile)
                               == localBodySavedText,
                           true);
                expectBool("LocalBody save emits navigation/UI telemetry",
                           sawSaveNavigationTelemetry,
                           true);
                expectInt("LocalBody body-only save rebuilds no hierarchy",
                          hierarchyRebuilds,
                          0);

                printTextMetric("save_local_body_file",
                                normalizedPath(localBodyFile));
                printTextMetric(
                    "save_local_body_impact",
                    semanticChangeImpactName(latestSavePlan.impact));
                printMetric(
                    "save_local_body_generation",
                    static_cast<qint64>(saveWorkerTelemetry.generation));
                printMetric("save_local_body_affected_files",
                            latestSavePlan.affectedFiles.size());
                printMetric("save_local_body_compilation_files",
                            latestSavePlan.compilationFiles.size());
                printMetric("save_local_body_changed_files",
                            latestSavePlan.changedFiles.size());
                printMetric("save_local_body_slang_invoked",
                            saveWorkerTelemetry.slangInvoked ? 1 : 0);
                printMetric("save_local_body_save_handler_ms",
                            localSaveHandlerMs);
                printMetric("save_local_body_worker_ms",
                            saveWorkerTelemetry.workerMs);
                printMetric("save_local_body_publication_ms",
                            savePublicationTelemetry.publicationMs);
                printMetric("save_local_body_effective_facts_ms",
                            savePublicationTelemetry.effectiveFactsMs);
                printMetric("save_local_body_snapshot_install_ms",
                            savePublicationTelemetry.snapshotInstallMs);
                printMetric("save_local_body_ui_refresh_ms",
                            sawSaveNavigationTelemetry
                                ? saveNavigationTelemetry.uiRefreshMs
                                : 0);

                // Restore only the editor overlay. No fixture file is written.
                localBodyEditor->setPlainText(localBodyOriginal);
            }
        }
        drainRelationshipWork(window);
    }

    drainRelationshipWork(window);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
