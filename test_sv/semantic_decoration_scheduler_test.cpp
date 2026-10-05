#include <QtWidgets>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#define private public
#include "mainwindow.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "workspacesessioncoordinator.h"
#include "analysisscheduler.h"
#include "editorruntime.h"
#include "mycodeeditor.h"
#include "semanticindexsnapshot.h"
#undef private
#include "testuistyle.h"
#include "slangmanager.h"
#include <QFutureWatcher>
#include <QSemaphore>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace {
bool waitFor(const std::function<bool()>& ready, int ms = 15000) {
    QElapsedTimer clock; clock.start();
    do { QCoreApplication::processEvents(); if (ready()) return true; QThread::msleep(1); }
    while (clock.elapsed() < ms);
    return false;
}
QByteArray decorationsDigest(MyCodeEditor* editor) {
    QByteArray bytes;
    for (const auto& row : editor->state->semanticDecorations.toList())
        bytes += QByteArray::number(int(row.role)) + ':' + row.text.toUtf8() + ':'
            + QByteArray::number(row.startPosition) + ':' + QByteArray::number(row.length) + '\n';
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
std::pair<quint64, quint64> processMemory() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        return {counters.WorkingSetSize, counters.PrivateUsage};
#endif
    return {};
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", profile.filePath("session.ini").toUtf8());
    if (!initializeUiStyleForTest()) return 3;
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { ++failures; fprintf(stderr, "FAIL: %s\n", message); }
    };
    auto window = std::make_unique<MainWindow>();
    window->workspaceManager->setRecentWorkspacePersistenceEnabledForTesting(false);
    window->workspaceSessionCoordinator->setRestoreOnActivation(false);
    window->analysisScheduler->shutdown();
    const QString padding = QString::fromUtf8("// 中文 padding 0123456789\n").repeated(18000);
    const QString source = "module top(input logic clock);\n" + padding + "endmodule\n";
    QList<MyCodeEditor*> editors;
    QStringList paths;
    for (int i = 0; i < 2; ++i) {
        const auto file = profile.filePath(QString("source%1.sv").arg(i));
        QFile stream(file); if (!stream.open(QIODevice::WriteOnly)) return 4;
        stream.write(source.toUtf8()); stream.close(); paths.append(file);
        if (!window->tabManager->openFileInTab(file)) return 5;
        editors.append(window->tabManager->getCurrentEditor());
    }
    QHash<QString, QString> sourceTexts;
    QList<SemanticSymbolRecord> records;
    SlangManager slang;
    for (const auto& path : paths) {
        sourceTexts.insert(path, source);
        records.append(slang.extractSymbolRecords(path, source));
    }
    const auto finalSnapshot = std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, sourceTexts));
    SemanticIndex::getInstance()->setSnapshot(std::make_shared<SemanticIndexSnapshot>());
    auto* pool = QThreadPool::globalInstance();
    const int oldMaximum = pool->maxThreadCount();
    check(waitFor([&] { return window->findChildren<QFutureWatcherBase*>(QString(), Qt::FindDirectChildrenOnly).isEmpty(); }),
        "initial decoration work drains");
    pool->waitForDone(); pool->setMaxThreadCount(1);
    QSemaphore entered, release;
    pool->start([&] { entered.release(); release.acquire(); });
    entered.acquire();
    int submitted = 0, completed = 0, maxRetained = 0, maxInputs = 0;
    const auto memoryBefore = processMemory();
    auto memoryPeak = memoryBefore;
    const auto observe = [&] {
        auto watchers = window->findChildren<QFutureWatcherBase*>(QString(), Qt::FindDirectChildrenOnly);
        for (auto* watcher : watchers) if (!watcher->property("schedulerObserved").toBool()) {
            watcher->setProperty("schedulerObserved", true); ++submitted;
            // These futures are never cancelled via QFuture. Completion means
            // the actual worker ran, including the atomic cancellation early exit.
            if (watcher->isFinished()) ++completed;
            else QObject::connect(watcher, &QFutureWatcherBase::finished, &app, [&] { ++completed; });
        }
        maxRetained = qMax(maxRetained, int(watchers.size()));
        int inputs = 0;
        for (const auto* watcher : watchers) inputs += !watcher->isFinished();
        maxInputs = qMax(maxInputs, inputs);
        const auto memory = processMemory();
        memoryPeak.first = qMax(memoryPeak.first, memory.first);
        memoryPeak.second = qMax(memoryPeak.second, memory.second);
        return watchers.size();
    };
    QElapsedTimer events; events.start();
    qint64 scheduleNs = 0;
    constexpr int rounds = 80;
    for (int i = 0; i < rounds; ++i) {
        if (i == rounds - 1) SemanticIndex::getInstance()->setSnapshot(finalSnapshot);
        window->tabManager->openFileInTab(paths[i % 2]);
        auto* editor = editors[i % 2];
        QTextCursor cursor(editor->document()); cursor.movePosition(QTextCursor::End); cursor.insertText(" ");
        QElapsedTimer capture; capture.start();
        window->refreshActiveEditorSemanticDecorations();
        scheduleNs += capture.nsecsElapsed();
        // Deliver a distinct event-loop round while the global worker pool is saturated.
        QCoreApplication::processEvents(); observe();
#ifndef ZEROSLACK_BASELINE_BENCHMARK
        check(window->semanticDecorationRunning, "one worker remains owned");
        if (i > 0) check(window->semanticDecorationPending, "latest request retained without capture");
        check(observe() <= 1, "cross-round submitted worker bound");
#endif
    }
    const auto eventNs = events.nsecsElapsed();
    const int capturedWhileBlocked = submitted;
#ifndef ZEROSLACK_BASELINE_BENCHMARK
    check(capturedWhileBlocked == 1, "only one input captured under saturation");
#endif
    auto* active = window->tabManager->getCurrentEditor();
    const auto expected = SemanticDecorationService::getInstance()->decorationsForDocument(
        {active->documentFileName(), active->cachedDocumentText(), {}});
    active->setSemanticDecorations(expected.decorations);
    const auto expectedDigest = decorationsDigest(active);
    check(!expected.decorations.isEmpty(), "fixture has observable semantic decoration");
    active->setSemanticDecorations({});
    QElapsedTimer drain; drain.start(); release.release();
    check(waitFor([&] { return observe() == 0; }), "cancelled worker and latest input complete");
    const auto completionNs = drain.nsecsElapsed();
    check(completed == submitted, "every submitted worker completed");
    check(decorationsDigest(active) == expectedDigest, "latest active editor receives exactly the current decoration result");
#ifndef ZEROSLACK_BASELINE_BENCHMARK
    check(!window->semanticDecorationRunning && !window->semanticDecorationPending, "scheduler returns to idle");
    check(submitted == 2, "first cancelled plus latest request are the only two captures");
#endif
    QJsonObject metrics{{"rounds", rounds}, {"sourceUtf8Bytes", source.toUtf8().size()},
        {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex())},
        {"capturedWhileBlocked", capturedWhileBlocked}, {"submittedTotal", submitted},
        {"completedWorkerStarts", completed},
        {"peakRetainedWatchers", maxRetained}, {"peakUnfinishedInputs", maxInputs},
        {"documentInputLogicalByteUpperBound", double(maxInputs) * (source.size() + rounds) * 2},
        {"scheduleNanoseconds", double(scheduleNs)}, {"editAndEventNanoseconds", double(eventNs)},
        {"backgroundDrainNanoseconds", double(completionNs)}, {"resultSha256", QString::fromLatin1(expectedDigest)}};
    metrics.insert("workingSetBefore", double(memoryBefore.first));
    metrics.insert("workingSetPeak", double(memoryPeak.first));
    metrics.insert("privateBytesBefore", double(memoryBefore.second));
    metrics.insert("privateBytesPeak", double(memoryPeak.second));
    // Pending close and destruction must cancel safely even while the worker cannot start.
    pool->start([&] { entered.release(); release.acquire(); }); entered.acquire();
    window->refreshActiveEditorSemanticDecorations();
    window->refreshActiveEditorSemanticDecorations();
    for (auto* editor : editors) editor->document()->setModified(false);
    check(window->tabManager->closeAllTabs(), "close all test documents");
    window->refreshActiveEditorSemanticDecorations();
#ifndef ZEROSLACK_BASELINE_BENCHMARK
    check(!window->semanticDecorationPending, "closing final editor cancels latest pending input");
#endif
    window.reset(); release.release(); pool->waitForDone();
    QCoreApplication::processEvents(); pool->setMaxThreadCount(oldMaximum);
    metrics.insert("failures", failures);
    const auto output = QJsonDocument(metrics).toJson(QJsonDocument::Compact);
    fprintf(stdout, "%s\n", output.constData());
    if (argc > 1) { QFile file(QString::fromLocal8Bit(argv[1])); if (file.open(QIODevice::WriteOnly)) file.write(output); }
    return failures ? 1 : 0;
}
