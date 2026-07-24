#include "documentmodel.h"
#include "completionmodel.h"
#include "documentregistry.h"
#include "editorselection.h"
#include "editorsyntaxstate.h"
#include "mycodeeditor.h"
#include "tabmanager.h"
#include "wavepreviewpanelcoordinator.h"

#include <QApplication>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFormat>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cstdio>
#include <functional>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* label, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    std::fflush(stdout);
}

void expect(const QString& label, bool condition)
{
    expect(label.toLocal8Bit().constData(), condition);
}

QString readText(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QTextStream(&file).readAll();
}

QString largestSourceFile(const QString& root)
{
    QString largest;
    qint64 largestBytes = -1;
    QDir directory(root);
    QDirIterator iterator(root,
                          {QStringLiteral("*.sv"),
                           QStringLiteral("*.svh"),
                           QStringLiteral("*.v"),
                           QStringLiteral("*.vh")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString candidate = iterator.next();
        const qint64 bytes = QFileInfo(candidate).size();
        if (bytes > largestBytes) {
            largest = candidate;
            largestBytes = bytes;
        }
    }
    return largest;
}

struct LatencySummary {
    qint64 p50Us = 0;
    qint64 p95Us = 0;
    qint64 maxUs = 0;
};

struct TypingReport {
    LatencySummary latency;
    EditorHotPathMetrics metrics;
};

TypingReport measureTyping(const QString& fileName)
{
    MyCodeEditor editor;
    editor.resize(960, 640);
    editor.setDocumentFileName(fileName);
    editor.setPlainText(readText(fileName));
    editor.acceptLoadedTextAsSemanticBaseline();

    DocumentModel documents;
    documents.registerEditor(&editor, fileName);
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::Start);
    editor.setTextCursor(cursor);
    editor.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    editor.resetHotPathMetricsForTest();

    QList<qint64> samples;
    constexpr int warmupCount = 5;
    constexpr int sampleCount = 40;
    samples.reserve(sampleCount);
    for (int index = 0; index < warmupCount + sampleCount; ++index) {
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(&editor, Qt::Key_X);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 elapsedUs = timer.nsecsElapsed() / 1000;
        if (index >= warmupCount)
            samples.append(elapsedUs);
    }

    std::sort(samples.begin(), samples.end());
    TypingReport report;
    report.latency.p50Us = samples.at(samples.size() / 2);
    report.latency.p95Us =
        samples.at((samples.size() * 95 + 99) / 100 - 1);
    report.latency.maxUs = samples.constLast();
    report.metrics = editor.hotPathMetricsForTest();
    return report;
}

struct InlineFilterReport {
    LatencySummary latency;
    EditorHotPathMetrics metrics;
    EditorHotPathMetrics postCancelMetrics;
    int initialCandidateCount = 0;
    int filteredCandidateCount = 0;
    bool sessionStayedActive = false;
    bool cancelSynchronizesCachedText = false;
};

struct VisibleWaveTypingReport {
    LatencySummary latency;
    EditorHotPathMetrics editorMetrics;
    DocumentTextCopyMetrics textCopyMetrics;
    WavePreviewRefreshMetrics waveMetrics;
    qsizetype documentCharacterCount = 0;
    bool metadataTextStayedEmpty = false;
    bool initialScopeValid = false;
};

InlineFilterReport measureInlineCandidateFiltering(const QString& fileName)
{
    MyCodeEditor editor;
    editor.resize(960, 640);
    editor.setDocumentFileName(fileName);
    editor.setPlainText(QStringLiteral(";;p -\n") + readText(fileName));
    editor.acceptLoadedTextAsSemanticBaseline();

    QTextCursor cursor(editor.document());
    cursor.setPosition(QStringLiteral(";;p -").size());
    editor.setTextCursor(cursor);
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    editor.resetHotPathMetricsForTest();
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    QCompleter* completer = editor.findChild<QCompleter*>();
    InlineFilterReport report;
    report.initialCandidateCount =
        completer && completer->model() ? completer->model()->rowCount() : 0;

    QList<qint64> samples;
    constexpr int warmupCount = 4;
    constexpr int sampleCount = 40;
    samples.reserve(sampleCount);
    for (int index = 0; index < warmupCount + sampleCount; ++index) {
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(&editor,
                        index % 2 == 0 ? Qt::Key_L
                                       : Qt::Key_Backspace);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 elapsedUs = timer.nsecsElapsed() / 1000;
        if (index >= warmupCount)
            samples.append(elapsedUs);
    }

    std::sort(samples.begin(), samples.end());
    report.latency.p50Us = samples.at(samples.size() / 2);
    report.latency.p95Us =
        samples.at((samples.size() * 95 + 99) / 100 - 1);
    report.latency.maxUs = samples.constLast();
    report.filteredCandidateCount =
        completer && completer->model() ? completer->model()->rowCount() : 0;
    report.sessionStayedActive = completer && completer->popup()->isVisible()
        && editor.inlineFilterTextOverlayActiveForTest()
        && editor.cachedDocumentSlice(0, 6)
               == QStringLiteral(";;p -\n");
    report.metrics = editor.hotPathMetricsForTest();
    QTest::keyClick(&editor, Qt::Key_L);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QTest::keyClick(&editor, Qt::Key_Escape);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    report.cancelSynchronizesCachedText =
        editor.cachedDocumentText() == editor.toPlainText();
    report.postCancelMetrics = editor.hotPathMetricsForTest();
    return report;
}

bool isOccurrenceIdentifierStartForTest(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_');
}

bool isOccurrenceIdentifierPartForTest(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

qsizetype documentOccurrenceHandleCount(QTextDocument* document)
{
    qsizetype count = 0;
    if (!document)
        return count;

    for (QTextBlock block = document->begin();
         block.isValid();
         block = block.next()) {
        const QString text = block.text();
        int position = 0;
        while (position < text.size()) {
            if (!isOccurrenceIdentifierStartForTest(text.at(position))) {
                ++position;
                continue;
            }
            const int start = position++;
            while (position < text.size()
                   && isOccurrenceIdentifierPartForTest(text.at(position))) {
                ++position;
            }
            if (position - start >= 2)
                ++count;
        }
    }
    return count;
}

QList<int> documentWordPositions(QTextDocument* document,
                                 const QString& word)
{
    QList<int> positions;
    if (!document || word.isEmpty())
        return positions;

    QTextCursor search(document);
    while (true) {
        search = document->find(word, search);
        if (search.isNull())
            break;
        const int start = search.selectionStart();
        const int end = search.selectionEnd();
        const bool leftBoundary = start == 0
            || !isOccurrenceIdentifierPartForTest(
                document->characterAt(start - 1));
        const bool rightBoundary = end >= document->characterCount() - 1
            || !isOccurrenceIdentifierPartForTest(
                document->characterAt(end));
        if (leftBoundary && rightBoundary)
            positions.append(start);
    }
    return positions;
}

void expectOccurrenceIndexMatchesDocument(
    const QString& label,
    MyCodeEditor& editor,
    const QStringList& probeWords,
    bool compareAllHandles)
{
    bool positionsMatch = true;
    for (const QString& word : probeWords) {
        positionsMatch = positionsMatch
            && editor.occurrencePositionsForTest(word)
                   == documentWordPositions(editor.document(), word);
    }
    expect(label + QStringLiteral(" occurrence positions are live"),
           positionsMatch);

    const EditorOccurrenceIndexStats stats =
        editor.occurrenceIndexStatsForTest();
    expect(label + QStringLiteral(" occurrence handles are internally exact"),
           stats.handleCount == stats.activeNodeCount
               && stats.allocatedNodeCount
                      == stats.activeNodeCount + stats.freeNodeCount);
    if (compareAllHandles) {
        expect(label + QStringLiteral(" occurrence handle count matches QTextDocument"),
               stats.handleCount
                   == documentOccurrenceHandleCount(editor.document()));
    }
}

QString documentTextSliceForTest(QTextDocument* document,
                                 int position,
                                 int length)
{
    if (!document || position < 0 || length < 0)
        return QString();
    QTextCursor cursor(document);
    cursor.setPosition(position);
    cursor.setPosition(position + length, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

void expectLargeFileSyntaxScopeMatchesDocument(
    const QString& label,
    MyCodeEditor& editor)
{
    const EditorLargeFileSyntaxScopeSnapshot scope =
        editor.largeFileSyntaxScopeForTest();
    const int documentLength = qMax(
        0, editor.document()->characterCount() - 1);
    expect(label + QStringLiteral(" Tree-sitter scoped text is current"),
           scope.valid()
               && scope.documentLength == documentLength
               && scope.text
                      == documentTextSliceForTest(
                          editor.document(),
                          scope.startPosition,
                          scope.text.size()));
}

void expectHugeIncrementalCaches(const QString& label,
                                 MyCodeEditor& editor,
                                 DocumentModel& documents,
                                 const QStringList& probeWords)
{
    const QString& cached = editor.cachedDocumentText();
    const QString documentText =
        editor.QPlainTextEdit::toPlainText();
    expect(label + QStringLiteral(" editor cache matches QTextDocument"),
           cached == documentText);
    expect(label + QStringLiteral(" DocumentModel text matches editor"),
           documents.documentTextForEditor(&editor) == documentText);
    const DocumentSnapshot snapshot = documents.documentForEditor(&editor);
    expect(label + QStringLiteral(" DocumentModel revision stays current"),
           snapshot.textVersion
                   == static_cast<int>(editor.semanticDocumentRevision())
               && snapshot.dirty && !snapshot.saved);
    expectLargeFileSyntaxScopeMatchesDocument(label, editor);
    expectOccurrenceIndexMatchesDocument(label,
                                         editor,
                                         probeWords,
                                         true);
}

void exerciseInlineFilterOverlayConsistency(const QString& fileName)
{
    const QString initial =
        QStringLiteral(";;p -\n") + readText(fileName);
    const QString nearWord = QStringLiteral(
        "__GUARD__VENDOR_IP_CTL_CC_CONSTANTS__SVH__");
    const QString suffixWord = QStringLiteral("CX_LTSSM_EMU_WD");
    const QStringList probeWords({nearWord, suffixWord});

    auto prepareEditor = [&](MyCodeEditor& editor,
                             DocumentModel& documents) {
        editor.resize(960, 640);
        editor.setDocumentFileName(fileName);
        editor.setPlainText(initial);
        editor.acceptLoadedTextAsSemanticBaseline();
        documents.registerEditor(&editor, fileName);
        QTextCursor cursor(editor.document());
        const QList<int> suffixPositions =
            documentWordPositions(editor.document(), suffixWord);
        expect("overlay fixture exposes a suffix syntax probe",
               !suffixPositions.isEmpty());
        if (!suffixPositions.isEmpty()) {
            cursor.setPosition(suffixPositions.constLast());
            editor.setTextCursor(cursor);
            editor.currentModuleScopeTarget();
        }
        cursor.setPosition(QStringLiteral(";;p -").size());
        editor.setTextCursor(cursor);
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        editor.resetHotPathMetricsForTest();
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    };

    {
        MyCodeEditor editor;
        DocumentModel documents;
        prepareEditor(editor, documents);
        QCompleter* completer = editor.findChild<QCompleter*>();
        expect("overlay regression opens parameter candidate filtering",
               completer && completer->popup()->isVisible()
                   && completer->model()
                   && completer->model()->rowCount() >= 4);

        const QList<int> initialSuffixPositions =
            editor.occurrencePositionsForTest(suffixWord);
        const EditorLargeFileSyntaxScopeSnapshot initialSyntaxScope =
            editor.largeFileSyntaxScopeForTest();
        int accumulatedDelta = 0;
        const QList<Qt::Key> keys({Qt::Key_L, Qt::Key_O});
        for (int index = 0; index < keys.size(); ++index) {
            QTest::keyClick(&editor, keys.at(index));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            ++accumulatedDelta;

            QList<int> shiftedSuffixPositions = initialSuffixPositions;
            for (int& position : shiftedSuffixPositions)
                position += accumulatedDelta;
            const EditorHotPathMetrics metrics =
                editor.hotPathMetricsForTest();
            expect(QStringLiteral("overlay key %1 keeps session active")
                       .arg(index + 1),
                   editor.inlineFilterTextOverlayActiveForTest());
            expect(QStringLiteral("overlay key %1 records one filter event")
                       .arg(index + 1),
                   metrics.inlineFilterKeyEvents
                       == static_cast<std::uint64_t>(index + 1));
            expect(QStringLiteral("overlay key %1 records one local overlay edit")
                       .arg(index + 1),
                   metrics.inlineFilterOverlayEdits
                       == static_cast<std::uint64_t>(index + 1));
            expect(QStringLiteral("overlay key %1 avoids overlay materialization")
                       .arg(index + 1),
                   metrics.inlineFilterOverlayMaterializations == 0);
            expect(QStringLiteral("overlay key %1 avoids forced full-text reads")
                       .arg(index + 1),
                   metrics.inlineFilterOverlayForcedTextReads == 0);
            expect(QStringLiteral("overlay key %1 shifts suffix occurrences once")
                       .arg(index + 1),
                   editor.occurrencePositionsForTest(suffixWord)
                       == shiftedSuffixPositions);
            expectOccurrenceIndexMatchesDocument(
                QStringLiteral("overlay key %1").arg(index + 1),
                editor,
                probeWords,
                index + 1 == keys.size());
            const EditorLargeFileSyntaxScopeSnapshot currentSyntaxScope =
                editor.largeFileSyntaxScopeForTest();
            expect(QStringLiteral("overlay key %1 shifts syntax scope once")
                       .arg(index + 1),
                   initialSyntaxScope.valid()
                       && currentSyntaxScope.valid()
                       && currentSyntaxScope.startPosition
                              == initialSyntaxScope.startPosition
                                     + accumulatedDelta
                       && currentSyntaxScope.endPosition
                              == initialSyntaxScope.endPosition
                                     + accumulatedDelta
                       && currentSyntaxScope.documentLength
                              == initialSyntaxScope.documentLength
                                     + accumulatedDelta
                       && currentSyntaxScope.text == initialSyntaxScope.text);
            expectLargeFileSyntaxScopeMatchesDocument(
                QStringLiteral("overlay key %1").arg(index + 1),
                editor);
        }

        QTest::keyClick(&editor, Qt::Key_Escape);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expectHugeIncrementalCaches(QStringLiteral("overlay cancel"),
                                    editor,
                                    documents,
                                    probeWords);
        editor.undo();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expectHugeIncrementalCaches(QStringLiteral("overlay cancel undo"),
                                    editor,
                                    documents,
                                    probeWords);
        editor.redo();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expectHugeIncrementalCaches(QStringLiteral("overlay cancel redo"),
                                    editor,
                                    documents,
                                    probeWords);
    }

    {
        MyCodeEditor editor;
        DocumentModel documents;
        prepareEditor(editor, documents);
        QCompleter* completer = editor.findChild<QCompleter*>();
        QTest::keyClick(&editor, Qt::Key_L);
        QTest::keyClick(&editor, Qt::Key_O);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expect("overlay confirmation session remains active",
               editor.inlineFilterTextOverlayActiveForTest());
        expect("overlay confirmation popup remains visible",
               completer && completer->popup()->isVisible());
        expect("overlay confirmation model has header plus two candidates",
               completer && completer->model()
                   && completer->model()->rowCount() == 3);
        expect("overlay confirmation keeps a current candidate",
               completer && completer->popup()->currentIndex().isValid());
        const CompletionModel::CompletionItem selectedCandidate =
            completer->popup()->currentIndex()
                .data(Qt::UserRole)
                .value<CompletionModel::CompletionItem>();
        expect("overlay confirmation selects logic metadata",
               selectedCandidate.text == QStringLiteral("logic"));
        QTest::keyClick(&editor, Qt::Key_Return);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expect("overlay candidate confirmation replaces command input",
               editor.cachedDocumentSlice(0, 12)
                   .startsWith(QStringLiteral(";;p -logic ")));
        expectHugeIncrementalCaches(QStringLiteral("overlay confirmation"),
                                    editor,
                                    documents,
                                    probeWords);
        editor.undo();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expectHugeIncrementalCaches(QStringLiteral("overlay confirmation undo"),
                                    editor,
                                    documents,
                                    probeWords);
        editor.redo();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expectHugeIncrementalCaches(QStringLiteral("overlay confirmation redo"),
                                    editor,
                                    documents,
                                    probeWords);
    }
}

VisibleWaveTypingReport measureVisibleWaveTyping(const QString& fileName)
{
    const QString localScope = QStringLiteral(
        "module wave_perf;\n"
        "  logic clk, d, q;\n"
        "  always_ff @(posedge clk) begin\n"
        "    q <= d;\n"
        "  end\n"
        "endmodule\n");

    MyCodeEditor editor;
    editor.resize(960, 640);
    editor.setDocumentFileName(fileName);
    editor.setPlainText(localScope + readText(fileName));
    editor.acceptLoadedTextAsSemanticBaseline();

    DocumentModel documents;
    documents.registerEditor(&editor, fileName);
    QTextCursor cursor(editor.document());
    cursor.setPosition(editor.cachedDocumentText().indexOf(
                           QStringLiteral("q <= d"))
                       + QStringLiteral("q <= ").size());
    editor.setTextCursor(cursor);
    editor.show();

    QWidget waveHost;
    WavePreviewPanelCoordinator coordinator(&waveHost);
    waveHost.show();
    coordinator.dock()->show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    const EditorAlwaysScopeTarget initialScope =
        editor.currentAlwaysScopeTarget();
    const DocumentSnapshot initialMetadata =
        documents.documentMetadataForEditor(&editor);
    coordinator.refreshFromDocument(initialMetadata.fileName,
                                    editor.cachedDocumentText(),
                                    initialMetadata.dirty,
                                    initialScope.startPosition,
                                    initialScope.endPosition,
                                    initialScope.label,
                                    initialScope.startLine);

    bool metadataTextStayedEmpty = initialMetadata.text.isEmpty();
    QObject::connect(
        &editor,
        &MyCodeEditor::documentChangeApplied,
        &waveHost,
        [&](const DocumentChange& change) {
            const DocumentSnapshot metadata =
                documents.documentMetadataForEditor(&editor);
            metadataTextStayedEmpty = metadataTextStayedEmpty
                && metadata.text.isEmpty() && metadata.dirty;
            const EditorAlwaysScopeTarget scope =
                editor.currentAlwaysScopeTarget();
            coordinator.applyDocumentChange(metadata.fileName,
                                            change,
                                            editor.cachedDocumentText(),
                                            metadata.dirty,
                                            scope.startPosition,
                                            scope.endPosition,
                                            scope.label,
                                            scope.startLine);
        });

    editor.resetHotPathMetricsForTest();
    resetDocumentTextCopyMetricsForTest();
    coordinator.resetRefreshMetricsForTest();

    QList<qint64> samples;
    constexpr int warmupCount = 4;
    constexpr int sampleCount = 40;
    samples.reserve(sampleCount);
    for (int index = 0; index < warmupCount + sampleCount; ++index) {
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(&editor,
                        index % 2 == 0 ? Qt::Key_X
                                       : Qt::Key_Backspace);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 elapsedUs = timer.nsecsElapsed() / 1000;
        if (index >= warmupCount)
            samples.append(elapsedUs);
    }

    std::sort(samples.begin(), samples.end());
    VisibleWaveTypingReport report;
    report.latency.p50Us = samples.at(samples.size() / 2);
    report.latency.p95Us =
        samples.at((samples.size() * 95 + 99) / 100 - 1);
    report.latency.maxUs = samples.constLast();
    report.editorMetrics = editor.hotPathMetricsForTest();
    report.textCopyMetrics = documentTextCopyMetricsForTest();
    report.waveMetrics = coordinator.refreshMetricsForTest();
    report.documentCharacterCount = editor.cachedDocumentText().size();
    report.metadataTextStayedEmpty = metadataTextStayedEmpty;
    report.initialScopeValid = initialScope.ok();
    return report;
}

void printLatency(const char* fixture, const LatencySummary& summary)
{
    std::printf("perf.typing.%s.p50_us=%lld\n",
                fixture,
                static_cast<long long>(summary.p50Us));
    std::printf("perf.typing.%s.p95_us=%lld\n",
                fixture,
                static_cast<long long>(summary.p95Us));
    std::printf("perf.typing.%s.max_us=%lld\n",
                fixture,
                static_cast<long long>(summary.maxUs));
}

void printInlineFilterLatency(const LatencySummary& summary)
{
    std::printf("perf.inline_filter.huge_after.p50_us=%lld\n",
                static_cast<long long>(summary.p50Us));
    std::printf("perf.inline_filter.huge_after.p95_us=%lld\n",
                static_cast<long long>(summary.p95Us));
    std::printf("perf.inline_filter.huge_after.max_us=%lld\n",
                static_cast<long long>(summary.maxUs));
}

void printInlineFilterMetrics(const EditorHotPathMetrics& metrics)
{
    std::printf("perf.inline_filter.metrics.key_events=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterKeyEvents));
    std::printf("perf.inline_filter.metrics.document_changes=%llu\n",
                static_cast<unsigned long long>(
                    metrics.documentChanges));
    std::printf("perf.inline_filter.metrics.retained_document_chars=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterRetainedDocumentCharactersPeak));
    std::printf("perf.inline_filter.metrics.overlay_sessions=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterOverlaySessions));
    std::printf("perf.inline_filter.metrics.overlay_edits=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterOverlayEdits));
    std::printf("perf.inline_filter.metrics.overlay_materializations=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterOverlayMaterializations));
    std::printf("perf.inline_filter.metrics.overlay_forced_text_reads=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterOverlayForcedTextReads));
    std::printf("perf.inline_filter.metrics.refreshes=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterRefreshes));
    std::printf("perf.inline_filter.metrics.service_queries=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterServiceQueries));
    std::printf("perf.inline_filter.metrics.model_updates=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterModelUpdates));
    std::printf("perf.inline_filter.metrics.highlight_updates=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterHighlightUpdates));
    std::printf("perf.inline_filter.metrics.popup_completes=%llu\n",
                static_cast<unsigned long long>(
                    metrics.inlineFilterPopupCompletes));
}

void printVisibleWaveLatency(const LatencySummary& summary)
{
    std::printf("perf.visible_wave.huge_after.p50_us=%lld\n",
                static_cast<long long>(summary.p50Us));
    std::printf("perf.visible_wave.huge_after.p95_us=%lld\n",
                static_cast<long long>(summary.p95Us));
    std::printf("perf.visible_wave.huge_after.max_us=%lld\n",
                static_cast<long long>(summary.maxUs));
}

void printVisibleWaveMetrics(const VisibleWaveTypingReport& report)
{
    std::printf("perf.visible_wave.document_changes=%llu\n",
                static_cast<unsigned long long>(
                    report.editorMetrics.documentChanges));
    std::printf("perf.visible_wave.render_count=%d\n",
                report.waveMetrics.renderCount);
    std::printf("perf.visible_wave.delta_render_count=%d\n",
                report.waveMetrics.documentChangeRenderCount);
    std::printf("perf.visible_wave.scope_delta_count=%d\n",
                report.waveMetrics.scopeDeltaUpdateCount);
    std::printf("perf.visible_wave.scope_rebuild_count=%d\n",
                report.waveMetrics.scopeRebuildCount);
    std::printf("perf.visible_wave.last_parsed_chars=%lld\n",
                static_cast<long long>(
                    report.waveMetrics.lastParsedCharacterCount));
}

void verifyIncrementalCaches(const QString& label,
                             MyCodeEditor& editor,
                             DocumentModel& documents)
{
    const QString& cached = editor.cachedDocumentText();
    expect(label + QStringLiteral(" editor cache matches QTextDocument"),
           cached == editor.QPlainTextEdit::toPlainText());
    expect(label + QStringLiteral(" Tree-sitter cache matches editor"),
           editor.syntaxTextForTest() == cached);
    expect(label + QStringLiteral(" DocumentModel cache matches editor"),
           documents.documentTextForEditor(&editor) == cached);
    const DocumentSnapshot snapshot = documents.documentForEditor(&editor);
    expect(label + QStringLiteral(" revision and dirty state advance once"),
           snapshot.textVersion
                   == static_cast<int>(editor.semanticDocumentRevision())
               && snapshot.dirty && !snapshot.saved);
}

void exerciseDeltaCorrectness()
{
    MyCodeEditor editor;
    const QString initial = QStringLiteral(
        "module top;\n  logic data;\n  assign data = 1'b0;\nendmodule\n");
    editor.setPlainText(initial);
    editor.acceptLoadedTextAsSemanticBaseline();
    DocumentModel documents;
    documents.registerEditor(&editor, QStringLiteral("incremental.sv"));
    QSignalSpy changeSpy(&editor, &MyCodeEditor::documentChangeApplied);
    QSignalSpy editedSpy(&documents, &DocumentModel::documentEdited);
    editor.resetHotPathMetricsForTest();

    auto oneDelta = [&](const QString& label,
                        const std::function<void()>& edit) {
        const int changesBefore = changeSpy.count();
        const int editsBefore = editedSpy.count();
        edit();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        expect(label + QStringLiteral(" emits one authoritative delta"),
               changeSpy.count() == changesBefore + 1
                   && editedSpy.count() == editsBefore + 1);
        verifyIncrementalCaches(label, editor, documents);
    };

    oneDelta(QStringLiteral("single character"), [&]() {
        QTextCursor cursor(editor.document());
        cursor.setPosition(editor.cachedDocumentText().indexOf(
            QStringLiteral("data")));
        cursor.insertText(QStringLiteral("x"));
    });
    expect("single-character delta carries only inserted fragment",
           changeSpy.last().at(0).value<DocumentChange>().insertedText
                   == QStringLiteral("x")
               && changeSpy.last().at(0).value<DocumentChange>()
                          .removedText.isEmpty());
    expect("edit notification carries metadata without eager full text",
           editedSpy.last().at(0).value<DocumentSnapshot>().text.isEmpty());

    oneDelta(QStringLiteral("cross-line paste and Unicode"), [&]() {
        QTextCursor cursor(editor.document());
        cursor.setPosition(editor.cachedDocumentText().indexOf(
            QStringLiteral("endmodule")));
        cursor.insertText(QStringLiteral("  logic \u4fe1\u53f7\U0001f600;\n"
                                         "  logic pasted;\n"));
    });
    expect("Unicode fragment remains UTF-16 exact",
           changeSpy.last().at(0).value<DocumentChange>().insertedText
               == QStringLiteral("  logic \u4fe1\u53f7\U0001f600;\n"
                                 "  logic pasted;\n"));

    oneDelta(QStringLiteral("cross-line deletion"), [&]() {
        const QString text = editor.cachedDocumentText();
        const int start = text.indexOf(QStringLiteral("logic \u4fe1\u53f7"));
        const int end = text.indexOf(QStringLiteral("logic pasted")) + 6;
        QTextCursor cursor(editor.document());
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    });
    expect("deletion delta carries removed newlines",
           changeSpy.last().at(0).value<DocumentChange>()
                   .removedText.contains(QLatin1Char('\n'))
               && changeSpy.last().at(0).value<DocumentChange>()
                          .insertedText.isEmpty());

    oneDelta(QStringLiteral("undo"), [&]() { editor.undo(); });
    oneDelta(QStringLiteral("redo"), [&]() { editor.redo(); });

    const EditorHotPathMetrics metrics = editor.hotPathMetricsForTest();
    expect("ordinary deltas never materialize full editor text",
           metrics.fullTextMaterializations == 0);
    expect("ordinary deltas never rebuild all folds",
           metrics.fullFoldingRebuilds == 0
               && metrics.incrementalFoldingUpdates
                      == metrics.documentChanges);
    expect("ordinary deltas remap ghosts without a full query",
           metrics.fullGhostQueries == 0
               && metrics.ghostRemaps == metrics.documentChanges);
    expect("ordinary deltas update occurrence index locally",
           metrics.occurrenceFullBuilds == 0
               && metrics.occurrenceIncrementalUpdates
                      == metrics.documentChanges);
}

void exerciseFoldGhostAndSlotState()
{
    MyCodeEditor foldingEditor;
    foldingEditor.setPlainText(QStringLiteral(
        "module folded;\n  logic value;\n  assign value = 1'b0;\nendmodule\n"));
    foldingEditor.acceptLoadedTextAsSemanticBaseline();
    expect("syntax fold can collapse", foldingEditor.toggleFoldAtLineForTest(0));
    expect("collapsed syntax fold hides its body",
           !foldingEditor.document()->findBlockByNumber(1).isVisible());
    foldingEditor.resetHotPathMetricsForTest();

    QTextCursor hiddenEdit(foldingEditor.document());
    hiddenEdit.setPosition(
        foldingEditor.document()->findBlockByNumber(1).position() + 2);
    hiddenEdit.insertText(QStringLiteral("x"));
    expect("ordinary edit preserves collapsed fold",
           foldingEditor.foldCollapsedAtLineForTest(0)
               && !foldingEditor.document()->findBlockByNumber(1).isVisible());

    QTextCursor prefix(foldingEditor.document());
    prefix.setPosition(0);
    prefix.insertText(QStringLiteral("\n"));
    expect("newline before fold remaps collapsed start",
           foldingEditor.foldCollapsedAtLineForTest(1)
               && !foldingEditor.document()->findBlockByNumber(2).isVisible());
    expect("fold edits stay incremental",
           foldingEditor.hotPathMetricsForTest().fullFoldingRebuilds == 0);

    MyCodeEditor customFoldEditor;
    customFoldEditor.setPlainText(QStringLiteral(
        "logic a;\nlogic b;\nlogic c;\nlogic d;\n"));
    customFoldEditor.acceptLoadedTextAsSemanticBaseline();
    expect("custom fold markers insert",
           customFoldEditor.insertCustomFoldMarkersForTest(
               0, 2, QStringLiteral("kept")));
    expect("custom fold can collapse",
           customFoldEditor.toggleFoldAtLineForTest(0));
    customFoldEditor.resetHotPathMetricsForTest();
    QTextCursor customBody(customFoldEditor.document());
    customBody.setPosition(
        customFoldEditor.document()->findBlockByNumber(2).position() + 2);
    customBody.insertText(QStringLiteral("x"));
    expect("ordinary edit preserves collapsed custom fold",
           customFoldEditor.foldCollapsedAtLineForTest(0)
               && !customFoldEditor.document()
                       ->findBlockByNumber(2)
                       .isVisible());
    expect("custom fold ordinary edit stays local",
           customFoldEditor.hotPathMetricsForTest().fullFoldingRebuilds == 0);

    const int markerLastChar = customFoldEditor.cachedDocumentText().indexOf(
        QStringLiteral("// fold kept")) + 6;
    QTextCursor markerEdit(customFoldEditor.document());
    markerEdit.setPosition(markerLastChar);
    markerEdit.deleteChar();
    expect("character edit invalidates only the affected custom marker",
           !customFoldEditor.toggleFoldAtLineForTest(0)
               && customFoldEditor.document()
                      ->findBlockByNumber(2)
                      .isVisible());
    customFoldEditor.undo();
    expect("undo restores a locally parsed custom fold",
           customFoldEditor.toggleFoldAtLineForTest(0));
    expect("custom marker edits never rebuild all folds",
           customFoldEditor.hotPathMetricsForTest().fullFoldingRebuilds == 0);

    MyCodeEditor deletedSyntaxFoldEditor;
    deletedSyntaxFoldEditor.setPlainText(QStringLiteral(
        "module before_fold;\n"
        "  logic before_value;\n"
        "endmodule\n"
        "module deleted_fold;\n"
        "  logic deleted_a;\n"
        "  logic deleted_b;\n"
        "endmodule\n"
        "module surviving_fold;\n"
        "  logic surviving_value;\n"
        "endmodule\n"));
    deletedSyntaxFoldEditor.acceptLoadedTextAsSemanticBaseline();
    expect("complete syntax fold can collapse before deletion",
           deletedSyntaxFoldEditor.toggleFoldAtLineForTest(3));
    QTextCursor deletedSyntaxFold(deletedSyntaxFoldEditor.document());
    deletedSyntaxFold.setPosition(
        deletedSyntaxFoldEditor.document()->findBlockByNumber(3).position());
    deletedSyntaxFold.setPosition(
        deletedSyntaxFoldEditor.document()->findBlockByNumber(7).position(),
        QTextCursor::KeepAnchor);
    deletedSyntaxFold.removeSelectedText();
    expect("deleting a complete syntax fold removes its collapsed start",
           !deletedSyntaxFoldEditor.foldCollapsedAtLineForTest(3));
    expect("deleted fold does not hide unrelated content moved to its line",
           deletedSyntaxFoldEditor.document()->findBlockByNumber(3).text()
                   .contains(QStringLiteral("surviving_fold"))
               && deletedSyntaxFoldEditor.document()
                      ->findBlockByNumber(4)
                      .isVisible());
    deletedSyntaxFoldEditor.undo();
    expect("undo of complete fold deletion has no stale collapsed state",
           !deletedSyntaxFoldEditor.foldCollapsedAtLineForTest(3)
               && deletedSyntaxFoldEditor.document()
                      ->findBlockByNumber(4)
                      .isVisible());
    deletedSyntaxFoldEditor.redo();
    expect("redo of complete fold deletion stays free of stale state",
           !deletedSyntaxFoldEditor.foldCollapsedAtLineForTest(3)
               && deletedSyntaxFoldEditor.document()
                      ->findBlockByNumber(4)
                      .isVisible());

    MyCodeEditor deletedCustomFoldEditor;
    deletedCustomFoldEditor.setPlainText(QStringLiteral(
        "logic before_custom;\n"
        "logic custom_a;\n"
        "logic custom_b;\n"
        "logic after_custom;\n"));
    deletedCustomFoldEditor.acceptLoadedTextAsSemanticBaseline();
    expect("complete custom fold markers insert",
           deletedCustomFoldEditor.insertCustomFoldMarkersForTest(
               1, 2, QStringLiteral("deleted_custom")));
    expect("complete custom fold can collapse before deletion",
           deletedCustomFoldEditor.toggleFoldAtLineForTest(1));
    const int customStartPosition =
        deletedCustomFoldEditor.cachedDocumentText().indexOf(
            QStringLiteral("// fold deleted_custom"));
    const int customEndPosition =
        deletedCustomFoldEditor.cachedDocumentText().indexOf(
            QStringLiteral("// endfold"), customStartPosition);
    QTextBlock blockAfterCustom =
        deletedCustomFoldEditor.document()
            ->findBlock(customEndPosition)
            .next();
    QTextCursor deletedCustomFold(deletedCustomFoldEditor.document());
    deletedCustomFold.setPosition(customStartPosition);
    deletedCustomFold.setPosition(
        blockAfterCustom.isValid()
            ? blockAfterCustom.position()
            : deletedCustomFoldEditor.cachedDocumentText().size(),
        QTextCursor::KeepAnchor);
    deletedCustomFold.removeSelectedText();
    expect("deleting complete custom fold removes collapsed state",
           !deletedCustomFoldEditor.foldCollapsedAtLineForTest(1));
    expect("deleting complete custom markers leaves no custom fold",
           !deletedCustomFoldEditor.toggleFoldAtLineForTest(1)
               && deletedCustomFoldEditor.document()
                      ->findBlockByNumber(1)
                      .text()
                      .contains(QStringLiteral("after_custom"))
               && deletedCustomFoldEditor.document()
                      ->findBlockByNumber(1)
                      .isVisible());

    MyCodeEditor pastedBeforeFoldEditor;
    pastedBeforeFoldEditor.setPlainText(QStringLiteral(
        "// original lead\n"
        "module pasted_fold;\n"
        "  logic pasted_value;\n"
        "endmodule\n"));
    pastedBeforeFoldEditor.acceptLoadedTextAsSemanticBaseline();
    expect("cross-line paste fixture fold can collapse",
           pastedBeforeFoldEditor.toggleFoldAtLineForTest(1));
    QTextCursor pastedPrefix(pastedBeforeFoldEditor.document());
    pastedPrefix.setPosition(0);
    pastedPrefix.insertText(QStringLiteral("// pasted a\n// pasted b\n"));
    expect("cross-line paste remaps collapsed fold start",
           pastedBeforeFoldEditor.foldCollapsedAtLineForTest(3)
               && !pastedBeforeFoldEditor.document()
                       ->findBlockByNumber(4)
                       .isVisible());
    pastedBeforeFoldEditor.undo();
    expect("undo cross-line paste restores collapsed fold coordinates",
           pastedBeforeFoldEditor.foldCollapsedAtLineForTest(1)
               && !pastedBeforeFoldEditor.document()
                       ->findBlockByNumber(2)
                       .isVisible());
    pastedBeforeFoldEditor.redo();
    expect("redo cross-line paste restores remapped collapsed coordinates",
           pastedBeforeFoldEditor.foldCollapsedAtLineForTest(3)
               && !pastedBeforeFoldEditor.document()
                       ->findBlockByNumber(4)
                       .isVisible());

    MyCodeEditor wholeReplacementFoldEditor;
    wholeReplacementFoldEditor.setPlainText(QStringLiteral(
        "module first_long;\n"
        "  logic first_value;\n"
        "endmodule\n"
        "\n"
        "module late_long;\n"
        "  logic late_a;\n"
        "  logic late_b;\n"
        "endmodule\n"));
    wholeReplacementFoldEditor.acceptLoadedTextAsSemanticBaseline();
    expect("late fold can collapse before whole-document replacement",
           wholeReplacementFoldEditor.toggleFoldAtLineForTest(4));
    wholeReplacementFoldEditor.setPlainText(QStringLiteral(
        "module short_text;\n"
        "  logic short_value;\n"
        "endmodule\n"));
    expect("whole-document replacement removes late collapsed starts",
           !wholeReplacementFoldEditor.foldCollapsedAtLineForTest(4));
    expect("whole-document replacement leaves every short block visible",
           wholeReplacementFoldEditor.document()
                   ->findBlockByNumber(0)
                   .isVisible()
               && wholeReplacementFoldEditor.document()
                      ->findBlockByNumber(1)
                      .isVisible()
               && wholeReplacementFoldEditor.document()
                      ->findBlockByNumber(2)
                      .isVisible());

    MyCodeEditor ghostEditor;
    ghostEditor.setDocumentFileName(QStringLiteral("ghost_remap.sv"));
    ghostEditor.setPlainText(QStringLiteral(
        "module ghost;\n  logic data;\nendmodule\n"));
    ghostEditor.acceptLoadedTextAsSemanticBaseline();
    GhostAnnotation annotation;
    annotation.kind = GhostAnnotationKind::SignalWidth;
    annotation.placement = GhostAnnotationPlacement::RightOfLine;
    annotation.text = QStringLiteral("8 bits");
    annotation.line = 2;
    annotation.anchorPosition = ghostEditor.cachedDocumentText().indexOf(
        QStringLiteral("data"));
    annotation.anchorLength = 4;
    ghostEditor.setGhostAnnotations({annotation});
    ghostEditor.resetHotPathMetricsForTest();

    QTextCursor ghostPrefix(ghostEditor.document());
    ghostPrefix.setPosition(0);
    ghostPrefix.insertText(QStringLiteral("//\n"));
    const QList<GhostAnnotation> shifted = ghostEditor.ghostAnnotationsForTest();
    expect("ghost anchor and line remap exactly",
           shifted.size() == 1
               && shifted.first().anchorPosition
                      == annotation.anchorPosition + 3
               && shifted.first().line == annotation.line + 1);

    QTextCursor afterAnchor(ghostEditor.document());
    afterAnchor.setPosition(shifted.first().anchorPosition
                            + shifted.first().anchorLength);
    afterAnchor.insertText(QStringLiteral("x"));
    expect("insertion at ghost half-open boundary preserves annotation",
           ghostEditor.ghostAnnotationsForTest().size() == 1
               && ghostEditor.ghostAnnotationsForTest().first()
                      .anchorPosition == shifted.first().anchorPosition);

    QTextCursor invalidate(ghostEditor.document());
    invalidate.setPosition(shifted.first().anchorPosition + 1);
    invalidate.insertText(QStringLiteral("x"));
    expect("edit intersecting ghost anchor invalidates it immediately",
           ghostEditor.ghostAnnotationsForTest().isEmpty());
    expect("ghost remap path performs no full query",
           ghostEditor.hotPathMetricsForTest().fullGhostQueries == 0);
    ghostEditor.refreshSemanticPresentation();
    expect("explicit semantic refresh performs one full ghost query",
           ghostEditor.hotPathMetricsForTest().fullGhostQueries == 1);

    MyCodeEditor largeGhostEditor;
    largeGhostEditor.setDocumentFileName(QStringLiteral("large_ghost.sv"));
    QString largeGhostText = QStringLiteral(
        "module large_ghost;\nlogic data;\nendmodule\n");
    largeGhostText.append(
        QString(2 * 1024 * 1024 + 64, QLatin1Char(' ')));
    largeGhostEditor.setPlainText(largeGhostText);
    largeGhostEditor.acceptLoadedTextAsSemanticBaseline();
    largeGhostEditor.resetHotPathMetricsForTest();
    largeGhostEditor.refreshSemanticPresentation();
    expect("large documents keep ghost analysis enabled",
           largeGhostEditor.hotPathMetricsForTest().fullGhostQueries == 1);

    MyCodeEditor slotEditor;
    slotEditor.setPlainText(QStringLiteral("foo bar"));
    slotEditor.acceptLoadedTextAsSemanticBaseline();
    CodeTemplateSlotList templateSlots;
    templateSlots.append({QStringLiteral("first"), 0, 3});
    templateSlots.append({QStringLiteral("second"), 4, 3});
    slotEditor.startTemplateSlotMode(0, 7, templateSlots);
    expect("template slot mode starts", slotEditor.templateSlotModeActive());
    QTest::keyClick(&slotEditor, Qt::Key_X);
    expect("incremental edit preserves template slot mode",
           slotEditor.templateSlotModeActive()
               && slotEditor.templateSlotModeSlotCount() == 2);
}

void exerciseRepeatedReplacementStability()
{
    {
        MyCodeEditor editor;
        editor.resize(560, 160);
        editor.show();
        editor.setFocus();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        const QStringList replacements{
            QStringLiteral("ab"),
            QStringLiteral("FOO"),
            QStringLiteral("`FOO"),
            QStringLiteral("`FOO_BAR"),
            QStringLiteral("obj.member"),
            QStringLiteral("pkg::member")
        };
        for (const QString& replacement : replacements) {
            editor.clear();
            QTest::keyClicks(&editor, replacement);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            expect(QStringLiteral("repeated clear/type keeps caches synchronized: %1")
                       .arg(replacement),
                   editor.cachedDocumentText() == replacement
                       && editor.syntaxTextForTest() == replacement);
        }
    }
    expect("single-line replacement editor destroys cleanly", true);

    {
        MyCodeEditor editor;
        const QString multiline = QStringLiteral(
            "module command_line;\n"
            "  logic a;\n"
            "  logic b;\n"
            "  assign b = a;\n"
            "endmodule\n");
        editor.setPlainText(multiline);
        editor.setPlainText(QString());
        QTest::keyClicks(&editor, QStringLiteral("`"));
        expect("multiline replacement followed by input remains stable",
               editor.cachedDocumentText() == QStringLiteral("`")
                   && editor.syntaxTextForTest() == QStringLiteral("`"));
    }
    expect("multiline replacement editor destroys cleanly", true);
}

void exercisePassiveUiSignals()
{
    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral(
        "module passive_ui;\n"
        "  logic value;\n"
        "endmodule\n"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(editor.cachedDocumentText().indexOf(
        QStringLiteral("value")));
    editor.setTextCursor(cursor);

    int packageUpdates = 0;
    QObject::connect(
        &editor,
        &MyCodeEditor::packageToolAvailabilityChanged,
        &editor,
        [&packageUpdates](const EditorPackageToolAvailability&) {
            ++packageUpdates;
        });
    QSignalSpy waveSpy(&editor, &MyCodeEditor::wavePreviewScopeChanged);
    QSignalSpy documentChangeSpy(&editor,
                                 &MyCodeEditor::documentChangeApplied);
    QTest::keyClick(&editor, Qt::Key_X);
    expect("unchanged package availability emits no UI update",
           packageUpdates == 0);
    expect("ordinary text input exposes exactly one Wave document delta",
           documentChangeSpy.size() == 1 && waveSpy.isEmpty());
}

bool currentLinePresentationAt(const MyCodeEditor& editor, int blockNumber)
{
    for (const QTextEdit::ExtraSelection& selection :
         editor.extraSelections()) {
        if (selection.format.property(QTextFormat::UserProperty).toInt()
                == 998
            && selection.cursor.blockNumber() == blockNumber) {
            return true;
        }
    }
    return false;
}

void exerciseSynchronousEditTransactions()
{
    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral(
        "module edit_transaction;\n"
        "  logic value;\n"
        "  assign value = 1'b0;\n"
        "endmodule\n"));
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    EditorSynchronousEditState before =
        editor.synchronousEditStateForTest();
    QTest::keyClick(&editor, Qt::Key_X);
    EditorSynchronousEditState afterKeyboard =
        editor.synchronousEditStateForTest();
    expect("keyboard edit commits one synchronous transaction",
           afterKeyboard.completedTransactionCount
                   == before.completedTransactionCount + 1
               && afterKeyboard.transactionDepth == 0
               && !afterKeyboard.presentationPending
               && !afterKeyboard.cursorPresentationSuppressed);

    before = afterKeyboard;
    expect("public fold edit succeeds inside synchronous transaction",
           editor.insertCustomFoldMarkersForTest(
               1, 2, QStringLiteral("transaction")));
    const EditorSynchronousEditState afterFold =
        editor.synchronousEditStateForTest();
    expect("public fold edit commits and clears presentation state",
           afterFold.completedTransactionCount
                   == before.completedTransactionCount + 1
               && afterFold.transactionDepth == 0
               && !afterFold.presentationPending
               && !afterFold.cursorPresentationSuppressed);

    const int lastBlock = editor.document()->blockCount() - 1;
    QTextCursor realCursor(editor.document()->findBlockByNumber(lastBlock));
    editor.QPlainTextEdit::setTextCursor(realCursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    expect("first cursor move after public fold edit is presented",
           currentLinePresentationAt(editor, lastBlock)
               && !editor.synchronousEditStateForTest()
                       .cursorPresentationSuppressed);
}

QList<int> currentReferenceHighlightPositions(const MyCodeEditor& editor)
{
    QList<int> positions;
    for (const QTextEdit::ExtraSelection& selection :
         editor.extraSelections()) {
        if (selection.format
                    .property(QTextFormat::UserProperty + 4)
                    .toInt() == 1004) {
            positions.append(selection.cursor.selectionStart());
        }
    }
    std::sort(positions.begin(), positions.end());
    return positions;
}

void selectWord(MyCodeEditor& editor, const QString& word)
{
    QTextCursor cursor(editor.document());
    cursor.setPosition(editor.cachedDocumentText().indexOf(word));
    editor.setTextCursor(cursor);
    editor.refreshScopeAndCurrentLineHighlight();
}

void exerciseOccurrenceIndexRemap()
{
    MyCodeEditor editor;
    editor.setPlainText(QStringLiteral(
        "alpha beta\nalpha gamma\n"));
    selectWord(editor, QStringLiteral("alpha"));
    expect("occurrence index highlights both initial references",
           currentReferenceHighlightPositions(editor)
               == QList<int>({0, 11}));

    QTextCursor prefix(editor.document());
    prefix.setPosition(0);
    prefix.insertText(QStringLiteral("\n"));
    selectWord(editor, QStringLiteral("alpha"));
    expect("occurrence index lazily remaps suffix positions",
           currentReferenceHighlightPositions(editor)
               == QList<int>({1, 12}));

    QTextCursor replace(editor.document());
    const int firstAlpha = editor.cachedDocumentText().indexOf(
        QStringLiteral("alpha"));
    replace.setPosition(firstAlpha);
    replace.setPosition(firstAlpha + 5, QTextCursor::KeepAnchor);
    replace.insertText(QStringLiteral("omega"));
    selectWord(editor, QStringLiteral("alpha"));
    expect("occurrence index rebuilds only the edited line",
           currentReferenceHighlightPositions(editor)
               == QList<int>({12}));

    editor.setPlainText(QStringLiteral("delta delta\n"));
    selectWord(editor, QStringLiteral("delta"));
    expect("whole-document replacement rebuilds safe occurrence anchors",
           currentReferenceHighlightPositions(editor)
               == QList<int>({0, 6}));
}

bool replaceOccurrenceToken(MyCodeEditor& editor,
                            const QString& from,
                            const QString& to)
{
    const int position = editor.cachedDocumentText().indexOf(from);
    if (position < 0)
        return false;
    QTextCursor cursor(editor.document());
    cursor.setPosition(position);
    cursor.setPosition(position + from.size(), QTextCursor::KeepAnchor);
    cursor.insertText(to);
    return true;
}

void expectBoundedOccurrenceStorage(const QString& label,
                                    const MyCodeEditor& editor)
{
    const EditorOccurrenceIndexStats stats =
        editor.occurrenceIndexStatsForTest();
    expect(label + QStringLiteral(" has no stale handles"),
           stats.handleCount == stats.activeNodeCount);
    expect(label + QStringLiteral(" keeps allocation within local-line slack"),
           stats.allocatedNodeCount <= stats.activeNodeCount + 16);
    expect(label + QStringLiteral(" accounts for every allocated node"),
           stats.allocatedNodeCount
               == stats.activeNodeCount + stats.freeNodeCount);
}

void exerciseOccurrenceIndexStorageBound()
{
    MyCodeEditor editor;
    QString changing = QStringLiteral("changing_0000");
    editor.setPlainText(
        QStringLiteral("module occurrence_churn;\n"
                       "  logic stable_word;\n"
                       "  assign stable_word = %1 + repeat_word"
                       " + repeat_word;\n"
                       "endmodule\n")
            .arg(changing));
    editor.acceptLoadedTextAsSemanticBaseline();

    bool replacementsSucceeded = true;
    for (int iteration = 1; iteration <= 3000; ++iteration) {
        const QString next = QStringLiteral("changing_%1")
                                 .arg(iteration, 4, 10, QLatin1Char('0'));
        replacementsSucceeded =
            replaceOccurrenceToken(editor, changing, next)
            && replacementsSucceeded;
        changing = next;
        if (iteration % 500 == 0) {
            expectBoundedOccurrenceStorage(
                QStringLiteral("changing-name churn at %1").arg(iteration),
                editor);
        }
    }

    QString repeated = QStringLiteral("repeat_word");
    for (int iteration = 1; iteration <= 1000; ++iteration) {
        const QString next = (iteration % 2 == 0)
            ? QStringLiteral("repeat_word")
            : QStringLiteral("repeat_swap");
        replacementsSucceeded =
            replaceOccurrenceToken(editor, repeated, next)
            && replacementsSucceeded;
        repeated = next;
    }
    expect("thousands of local occurrence replacements find their token",
           replacementsSucceeded);
    selectWord(editor, QStringLiteral("repeat_word"));
    expect("same-word churn retains both live repeated words",
           currentReferenceHighlightPositions(editor).size() == 2);
    expectBoundedOccurrenceStorage(QStringLiteral("same-word churn"), editor);

    for (int iteration = 0; iteration < 128; ++iteration)
        editor.undo();
    expectBoundedOccurrenceStorage(QStringLiteral("occurrence undo"), editor);
    for (int iteration = 0; iteration < 128; ++iteration)
        editor.redo();
    expectBoundedOccurrenceStorage(QStringLiteral("occurrence redo"), editor);

    const EditorOccurrenceIndexStats finalStats =
        editor.occurrenceIndexStatsForTest();
    expect("thousands of replacements do not accumulate occurrence handles",
           finalStats.handleCount < 32
               && finalStats.allocatedNodeCount < 48);
}

void exerciseGutterHitTestingBound()
{
    constexpr int lineCount = 5000;
    QString text;
    text.reserve(lineCount * 12);
    text.append(QStringLiteral("module gutter_probe;\n"));
    for (int line = 1; line < lineCount - 1; ++line)
        text.append(QStringLiteral("logic l%1;\n").arg(line));
    text.append(QStringLiteral("endmodule\n"));

    MyCodeEditor editor;
    editor.resize(320, 160);
    editor.setPlainText(text);
    editor.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    expect("large syntax fold collapses for gutter probe",
           editor.toggleFoldAtLineForTest(0));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QWidget* gutter = editor.findChild<QWidget*>(
        QStringLiteral("editorLineNumberGutter"));
    expect("editor gutter test surface is available", gutter != nullptr);
    if (!gutter)
        return;

    editor.resetHotPathMetricsForTest();
    const EditorBlockGeometry firstLineGeometry = editor.blockGeometry(0);
    const qreal secondVisibleLineY = firstLineGeometry.top
        + firstLineGeometry.height + 5;
    QMouseEvent move(QEvent::MouseMove,
                     QPointF(4, secondVisibleLineY),
                     QPointF(4, secondVisibleLineY),
                     QPointF(4, secondVisibleLineY),
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(gutter, &move);

    std::printf("perf.gutter.hidden_block_probes=%llu\n",
                static_cast<unsigned long long>(
                    editor.hotPathMetricsForTest().gutterBlockProbes));
    expect("gutter hover does not walk hidden document blocks",
           editor.hotPathMetricsForTest().gutterBlockProbes <= 2);
}

void exerciseNumericHoverDoesNotMaterializeDocument()
{
    const QString text = QStringLiteral(
        "module hover_probe;\n"
        "initial q = 16'h2a;\n"
        "endmodule\n");
    MyCodeEditor editor;
    editor.resize(480, 160);
    editor.setPlainText(text);
    editor.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QTextCursor cursor(editor.document());
    cursor.setPosition(text.indexOf(QStringLiteral("h2a")) + 1);
    editor.setTextCursor(cursor);
    editor.ensureCursorVisible();
    const QPoint point = editor.cursorRect(cursor).center();

    editor.resetHotPathMetricsForTest();
    QMouseEvent move(QEvent::MouseMove,
                     QPointF(point),
                     QPointF(point),
                     QPointF(editor.viewport()->mapToGlobal(point)),
                     Qt::NoButton,
                     Qt::NoButton,
                     Qt::ControlModifier);
    QApplication::sendEvent(editor.viewport(), &move);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const EditorHotPathMetrics metrics = editor.hotPathMetricsForTest();
    std::printf("perf.numeric_hover.full_text_materializations=%llu\n",
                static_cast<unsigned long long>(
                    metrics.fullTextMaterializations));
    expect("numeric hover does not materialize the full document",
           metrics.fullTextMaterializations == 0);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: editor_incremental_test <rtl_top.sv> <huge_prj>\n");
        return 2;
    }

    const QString rtlTop = QFileInfo(QString::fromLocal8Bit(argv[1]))
                               .absoluteFilePath();
    const QString hugeFile = largestSourceFile(
        QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
    expect("rtl_top fixture exists", QFileInfo(rtlTop).isFile());
    expect("huge fixture exists", QFileInfo(hugeFile).isFile());

    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* tabs = new QTabWidget(&window);
    layout->addWidget(tabs);
    TabManager manager(tabs, &window);
    window.setWindowTitle(QStringLiteral("before-open"));
    window.show();
    expect("first file opens", manager.openFileInTab(rtlTop));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    const DocumentSnapshot opened = manager.getCurrentDocument();
    expect("first tab title is rtl_top.sv",
           tabs->tabText(0) == QStringLiteral("rtl_top.sv"));
    expect("first file window title is never untitled",
           !window.windowTitle().isEmpty()
               && window.windowTitle() != QStringLiteral("untitled"));
    expect("first file snapshot identity is initialized",
           QFileInfo(opened.fileName).absoluteFilePath()
                   == QFileInfo(rtlTop).absoluteFilePath()
               && opened.documentId == opened.fileName
               && opened.text == manager.getCurrentEditor()
                                     ->cachedDocumentText());

#ifdef Q_OS_WIN
    MyCodeEditor* const firstEditor = manager.getCurrentEditor();
    const QString alternateCasePath = rtlTop.toUpper();
    expect("Windows file lookup treats path casing as identity-insensitive",
           manager.getDocumentModel()->editorForFile(alternateCasePath)
               == firstEditor);
    expect("Windows alternate-case open reuses the existing editor",
           manager.openFileInTab(alternateCasePath)
               && manager.editorCount() == 1
               && manager.getCurrentEditor() == firstEditor);
#endif

    exerciseDeltaCorrectness();
    exerciseFoldGhostAndSlotState();
    exerciseRepeatedReplacementStability();
    exercisePassiveUiSignals();
    exerciseSynchronousEditTransactions();
    exerciseOccurrenceIndexRemap();
    exerciseOccurrenceIndexStorageBound();
    exerciseGutterHitTestingBound();
    exerciseNumericHoverDoesNotMaterializeDocument();

    if (QFileInfo(rtlTop).isFile()) {
        const TypingReport report = measureTyping(rtlTop);
        printLatency("rtl_top_after", report.latency);
        // A keystroke receives less than one quarter of a 60 Hz frame at p95;
        // max remains below half a frame. These are fixed interaction budgets.
        expect("rtl_top typing p95 stays below 4 ms",
               report.latency.p95Us < 4000);
        expect("rtl_top typing max stays below 8 ms",
               report.latency.maxUs < 8000);
        expect("rtl_top hot path has zero full materialization/fold/ghost",
               report.metrics.fullTextMaterializations == 0
                   && report.metrics.fullFoldingRebuilds == 0
                   && report.metrics.fullGhostQueries == 0
                   && report.metrics.occurrenceFullBuilds == 0);
    }
    if (QFileInfo(hugeFile).isFile()) {
        const TypingReport report = measureTyping(hugeFile);
        printLatency("huge_after", report.latency);
        // The 4.5 MB fixture receives a stricter-than-frame p95 budget and a
        // max budget below one 60 Hz frame; no size-based threshold expansion.
        expect("huge-file typing p95 stays below 5 ms",
               report.latency.p95Us < 5000);
        expect("huge-file typing max stays below 12 ms",
               report.latency.maxUs < 12000);
        expect("huge-file hot path has zero full materialization/fold/ghost",
               report.metrics.fullTextMaterializations == 0
                   && report.metrics.fullFoldingRebuilds == 0
                   && report.metrics.fullGhostQueries == 0
                   && report.metrics.occurrenceFullBuilds == 0);

        const VisibleWaveTypingReport waveReport =
            measureVisibleWaveTyping(hugeFile);
        printVisibleWaveLatency(waveReport.latency);
        printVisibleWaveMetrics(waveReport);
        expect("visible Wave consumes exactly one scoped delta per key",
               waveReport.initialScopeValid
                   && waveReport.editorMetrics.documentChanges == 44
                   && waveReport.waveMetrics.documentChangeRenderCount == 44
                   && waveReport.waveMetrics.renderCount == 44
                   && waveReport.waveMetrics.scopeDeltaUpdateCount == 44
                   && waveReport.waveMetrics.scopeRebuildCount == 0);
        expect("visible Wave parses a small scope rather than the huge file",
               waveReport.waveMetrics.lastParsedCharacterCount > 0
                   && waveReport.waveMetrics.lastParsedCharacterCount * 100
                          < waveReport.documentCharacterCount);
        expect("visible Wave delta path keeps metadata text empty",
               waveReport.metadataTextStayedEmpty);
        expect("visible Wave delta path performs zero registry text copies",
               waveReport.textCopyMetrics.fullTextCopyCount == 0
                   && waveReport.textCopyMetrics.copiedCharacterCount == 0
                   && waveReport.editorMetrics.fullTextMaterializations == 0);
        expect("visible Wave huge-file typing p95 stays below 6 ms",
               waveReport.latency.p95Us < 6000);
        expect("visible Wave huge-file typing max stays below 12 ms",
               waveReport.latency.maxUs < 12000);

        const InlineFilterReport inlineReport =
            measureInlineCandidateFiltering(hugeFile);
        printInlineFilterLatency(inlineReport.latency);
        printInlineFilterMetrics(inlineReport.metrics);
        expect("huge inline Tab opens and preserves a filter session",
               inlineReport.initialCandidateCount >= 4
                   && inlineReport.sessionStayedActive);
        expect("huge inline candidate filter returns to the full set",
               inlineReport.filteredCandidateCount
                   == inlineReport.initialCandidateCount);
        expect("huge inline filtering performs one delta and one refresh per key",
               inlineReport.metrics.inlineFilterKeyEvents == 44
                   && inlineReport.metrics.documentChanges == 44
                   && inlineReport.metrics.inlineFilterRefreshes == 44
                   && inlineReport.metrics.inlineFilterServiceQueries == 44);
        expect("huge inline filtering updates the model without popup rebuilds",
               inlineReport.metrics.inlineFilterModelUpdates == 45
                   && inlineReport.metrics.inlineFilterPopupCompletes == 1
                   && inlineReport.metrics.inlineFilterHighlightUpdates == 1);
        expect("huge inline session retains no full document buffer",
               inlineReport.metrics
                       .inlineFilterRetainedDocumentCharactersPeak
                       == 0
                   && inlineReport.metrics.inlineFilterOverlaySessions
                       == 1
                   && inlineReport.metrics.inlineFilterOverlayEdits
                       == 44
                   && inlineReport.metrics
                          .inlineFilterOverlayMaterializations
                       == 0);
        expect("huge inline cancel materializes one pending overlay",
               inlineReport.cancelSynchronizesCachedText
                   && inlineReport.metrics
                          .inlineFilterOverlayForcedTextReads
                       == 0
                   && inlineReport.postCancelMetrics
                          .inlineFilterOverlaySessions
                       == 1
                   && inlineReport.postCancelMetrics
                          .inlineFilterOverlayEdits
                       == 45
                   && inlineReport.postCancelMetrics
                          .inlineFilterOverlayMaterializations
                       == 1);
        expect("huge inline candidate filtering p95 stays below 4 ms",
               inlineReport.latency.p95Us < 4000);
        expect("huge inline candidate filtering max stays below 8 ms",
               inlineReport.latency.maxUs < 8000);
        expect("huge inline candidate filtering does not use instrumented full text",
               inlineReport.metrics.fullTextMaterializations == 0
                   && inlineReport.metrics.cachedTextSliceReads >= 80
                   && inlineReport.metrics.cachedTextSliceCharacters < 4096);
        exerciseInlineFilterOverlayConsistency(hugeFile);
    }

    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
