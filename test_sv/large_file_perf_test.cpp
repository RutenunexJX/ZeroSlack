// Large-file performance baseline. Guards the slang16 regression class where
// whitespace edits in a large workspace file accidentally queued semantic or
// relationship reanalysis.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTimer>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>

#define private public
#include "mainwindow.h"
#include "analysisscheduler.h"
#include "mycodeeditor.h"
#include "semanticruntimecoordinator.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#undef private

static int g_checks = 0;
static int g_fails = 0;

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

static void drainRelationshipWork(MainWindow& window)
{
    SmartRelationshipBuilder* builder = window.semanticRuntime
        ? window.semanticRuntime->relationshipBuilder()
        : nullptr;
    if (builder)
        builder->cancelAnalysis();
    if (window.analysisScheduler) {
        for (QTimer* timer : window.analysisScheduler->relationshipAnalysisTimers) {
            if (timer) {
                timer->stop();
                timer->deleteLater();
            }
        }
        window.analysisScheduler->relationshipAnalysisTimers.clear();
        window.analysisScheduler->pendingRelationshipAnalysisContent.clear();
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

    QTimer* timer =
        window.analysisScheduler->relationshipAnalysisTimers.value(normalizedPath(fileName), nullptr);
    return timer && timer->isActive();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString workspacePath = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QDir::current().absoluteFilePath(QStringLiteral("test_sv/new"));

    expectBool("workspace fixture exists", QFileInfo(workspacePath).isDir(), true);

    MainWindow window;
    bool workspaceSymbolsDone = false;
    QObject::connect(window.analysisScheduler.get(),
                     &AnalysisScheduler::workspaceSymbolAnalysisFinished,
                     &window,
                     [&](const ProjectSnapshot&, int filesAnalyzed, int totalSymbols) {
                         Q_UNUSED(filesAnalyzed)
                         Q_UNUSED(totalSymbols)
                         workspaceSymbolsDone = true;
                     });

    window.resize(1100, 760);
    window.show();
    expectBool("main window visible", waitUntil([&]() { return window.isVisible(); }, 2000), true);

    expectBool("open workspace", window.workspaceManager->openWorkspace(workspacePath), true);
    expectBool("workspace symbol analysis completes",
               waitUntil([&]() { return workspaceSymbolsDone; }, 60000), true);

    const QString largeFile = largestFile(window.workspaceManager->getSystemVerilogFiles());
    expectBool("large file selected", QFileInfo(largeFile).size() > 20000, true);

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
    expectBool("initial large-file analysis completes",
               waitUntil([&]() { return largeFileAnalysisCount > 0; }, 10000), true);

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
        drainRelationshipWork(window);

        const int beforeLength = editor->toPlainText().size();
        QTest::keyClick(editor, Qt::Key_Return);
        QTest::keyClicks(editor, "  ");
        for (int i = 0; i < 20; ++i) {
            QTest::keyClick(editor, Qt::Key_Down);
            QTest::keyClick(editor, Qt::Key_Up);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        expectBool("whitespace edit applied", editor->toPlainText().size() >= beforeLength + 3, true);
        expectBool("whitespace edit does not start relationship debounce",
                   hasActiveRelationshipDebounce(window, largeFile),
                   false);
        expectInt("whitespace edit queues no symbol analysis",
                  symbolAnalysisStarted.count(), 0);

        // Sanity check the guard: a semantic-looking edit should still enter the delayed
        // relationship path, so this test is not merely proving all analysis is disabled.
        QTest::keyClicks(editor, "x");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        expectBool("non-whitespace edit still starts relationship debounce",
                   hasActiveRelationshipDebounce(window, largeFile),
                   true);
        drainRelationshipWork(window);
    }

    drainRelationshipWork(window);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
