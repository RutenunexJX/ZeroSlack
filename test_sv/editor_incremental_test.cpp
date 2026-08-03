#include "documentmodel.h"
#include "completionmodel.h"
#include "documentregistry.h"
#include "editoranchoredrangeindex.h"
#include "editorselection.h"
#include "editorsyntaxstate.h"
#include "mycodeeditor.h"
#include "tabmanager.h"
#include "tsdocument.h"
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

bool hasFutureWatcherChild(const QObject& object)
{
    const QList<QObject*> children = object.findChildren<QObject*>();
    return std::any_of(
        children.cbegin(),
        children.cend(),
        [](const QObject* child) {
            return child && child->inherits("QFutureWatcherBase");
        });
}

struct AnchoredRangeFixture {
    int startPosition = 0;
    int length = 0;
    int line = 0;
    int serial = 0;
};

struct AnchoredRangeFixtureTraits {
    static int start(const AnchoredRangeFixture& item)
    {
        return item.startPosition;
    }

    static int effectiveEnd(const AnchoredRangeFixture& item)
    {
        return item.startPosition + qMax(1, item.length);
    }

    static int firstLine(const AnchoredRangeFixture& item)
    {
        return item.line;
    }

    static int lastLine(const AnchoredRangeFixture& item)
    {
        return item.line;
    }

    static void shift(AnchoredRangeFixture& item,
                      int characterDelta,
                      int lineDelta)
    {
        item.startPosition += characterDelta;
        item.line += lineDelta;
    }
};

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

LatencySummary summarizeLatency(QList<qint64> samples)
{
    std::sort(samples.begin(), samples.end());
    LatencySummary summary;
    summary.p50Us = samples.at(samples.size() / 2);
    summary.p95Us =
        samples.at((samples.size() * 95 + 99) / 100 - 1);
    summary.maxUs = samples.constLast();
    return summary;
}

struct TypingReport {
    LatencySummary latency;
    EditorHotPathMetrics metrics;
    TSTextStorageMetrics textStorageMetrics;
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
    if (const TSDocument* syntax = editor.syntaxDocument())
        syntax->resetTextStorageMetricsForTest();

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
    if (const TSDocument* syntax = editor.syntaxDocument())
        report.textStorageMetrics =
            syntax->textStorageMetricsForTest();
    return report;
}

struct InlineFilterReport {
    LatencySummary latency;
    EditorHotPathMetrics metrics;
    EditorHotPathMetrics postCancelMetrics;
    TSTextStorageMetrics textStorageMetrics;
    int initialCandidateCount = 0;
    int filteredCandidateCount = 0;
    bool sessionStayedActive = false;
    bool cancelSynchronizesCachedText = false;
};

struct VisibleWaveTypingReport {
    LatencySummary latency;
    LatencySummary synchronousKeyLatency;
    LatencySummary eventProcessingLatency;
    EditorHotPathMetrics editorMetrics;
    TSTextStorageMetrics textStorageMetrics;
    DocumentTextCopyMetrics textCopyMetrics;
    WavePreviewRefreshMetrics waveMetrics;
    std::uint64_t metadataNanoseconds = 0;
    std::uint64_t scopeRegistryNanoseconds = 0;
    std::uint64_t coordinatorNanoseconds = 0;
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
    if (const TSDocument* syntax = editor.syntaxDocument())
        syntax->resetTextStorageMetricsForTest();
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
    if (const TSDocument* syntax = editor.syntaxDocument())
        report.textStorageMetrics =
            syntax->textStorageMetricsForTest();
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

void expectLargeFileSyntaxMatchesDocument(
    const QString& label,
    MyCodeEditor& editor)
{
    const EditorLargeFileSyntaxSnapshot snapshot =
        editor.largeFileSyntaxSnapshotForTest();
    const TSDocument* syntax = editor.syntaxDocument();
    const int documentLength = qMax(
        0, editor.document()->characterCount() - 1);
    const int boundaryLength = qMin(128, documentLength);
    const int suffixStart =
        qMax(0, documentLength - boundaryLength);
    expect(label + QStringLiteral(" full Tree-sitter text is current"),
           snapshot.valid()
               && snapshot.fullDocumentSyntax
               && snapshot.documentLength == documentLength
               && snapshot.syntaxTextLength == documentLength
               && syntax
               && syntax->text().size() == documentLength
               && syntax->text().left(boundaryLength)
                      == documentTextSliceForTest(
                          editor.document(), 0, boundaryLength)
               && syntax->text().mid(
                      suffixStart, boundaryLength)
                      == documentTextSliceForTest(
                          editor.document(),
                          suffixStart,
                          boundaryLength));
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
    expectLargeFileSyntaxMatchesDocument(label, editor);
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
        const EditorLargeFileSyntaxSnapshot initialSyntaxSnapshot =
            editor.largeFileSyntaxSnapshotForTest();
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
            const EditorLargeFileSyntaxSnapshot currentSyntaxSnapshot =
                editor.largeFileSyntaxSnapshotForTest();
            expect(QStringLiteral("overlay key %1 incrementally updates full syntax once")
                       .arg(index + 1),
                   initialSyntaxSnapshot.valid()
                       && currentSyntaxSnapshot.valid()
                       && currentSyntaxSnapshot.fullBuildCount
                              == initialSyntaxSnapshot.fullBuildCount
                       && currentSyntaxSnapshot.incrementalEditCount
                              == initialSyntaxSnapshot.incrementalEditCount
                                     + static_cast<std::uint64_t>(
                                         index + 1)
                       && currentSyntaxSnapshot.documentLength
                              == initialSyntaxSnapshot.documentLength
                                     + accumulatedDelta
                       && currentSyntaxSnapshot.syntaxTextLength
                              == currentSyntaxSnapshot.documentLength);
            expectLargeFileSyntaxMatchesDocument(
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

    std::uint64_t metadataNanoseconds = 0;
    std::uint64_t scopeRegistryNanoseconds = 0;
    std::uint64_t coordinatorNanoseconds = 0;
    bool metadataTextStayedEmpty = initialMetadata.text.isEmpty();
    QObject::connect(
        &editor,
        &MyCodeEditor::documentChangeApplied,
        &waveHost,
        [&](const DocumentChange& change) {
            QElapsedTimer stageTimer;
            stageTimer.start();
            const DocumentSnapshot metadata =
                documents.documentMetadataForEditor(&editor);
            metadataNanoseconds +=
                static_cast<std::uint64_t>(stageTimer.nsecsElapsed());
            metadataTextStayedEmpty = metadataTextStayedEmpty
                && metadata.text.isEmpty() && metadata.dirty;
            stageTimer.restart();
            const EditorAlwaysScopeTarget scope =
                editor.currentAlwaysScopeTarget();
            scopeRegistryNanoseconds +=
                static_cast<std::uint64_t>(stageTimer.nsecsElapsed());
            stageTimer.restart();
            coordinator.applyDocumentChange(metadata.fileName,
                                            change,
                                            editor.cachedDocumentText(),
                                            metadata.dirty,
                                            scope.startPosition,
                                            scope.endPosition,
                                            scope.label,
                                            scope.startLine);
            coordinatorNanoseconds +=
                static_cast<std::uint64_t>(stageTimer.nsecsElapsed());
        });

    editor.resetHotPathMetricsForTest();
    if (const TSDocument* syntax = editor.syntaxDocument())
        syntax->resetTextStorageMetricsForTest();
    resetDocumentTextCopyMetricsForTest();
    coordinator.resetRefreshMetricsForTest();

    QList<qint64> samples;
    QList<qint64> synchronousKeySamples;
    QList<qint64> eventProcessingSamples;
    constexpr int warmupCount = 4;
    constexpr int sampleCount = 40;
    samples.reserve(sampleCount);
    synchronousKeySamples.reserve(sampleCount);
    eventProcessingSamples.reserve(sampleCount);
    for (int index = 0; index < warmupCount + sampleCount; ++index) {
        QElapsedTimer timer;
        timer.start();
        QElapsedTimer synchronousKeyTimer;
        synchronousKeyTimer.start();
        QTest::keyClick(&editor,
                        index % 2 == 0 ? Qt::Key_X
                                       : Qt::Key_Backspace);
        const qint64 synchronousKeyUs =
            synchronousKeyTimer.nsecsElapsed() / 1000;
        QElapsedTimer eventProcessingTimer;
        eventProcessingTimer.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 eventProcessingUs =
            eventProcessingTimer.nsecsElapsed() / 1000;
        const qint64 elapsedUs = timer.nsecsElapsed() / 1000;
        if (index >= warmupCount) {
            samples.append(elapsedUs);
            synchronousKeySamples.append(synchronousKeyUs);
            eventProcessingSamples.append(eventProcessingUs);
        }
    }

    VisibleWaveTypingReport report;
    report.latency = summarizeLatency(samples);
    report.synchronousKeyLatency =
        summarizeLatency(synchronousKeySamples);
    report.eventProcessingLatency =
        summarizeLatency(eventProcessingSamples);
    report.editorMetrics = editor.hotPathMetricsForTest();
    if (const TSDocument* syntax = editor.syntaxDocument())
        report.textStorageMetrics =
            syntax->textStorageMetricsForTest();
    report.textCopyMetrics = documentTextCopyMetricsForTest();
    report.waveMetrics = coordinator.refreshMetricsForTest();
    report.metadataNanoseconds = metadataNanoseconds;
    report.scopeRegistryNanoseconds = scopeRegistryNanoseconds;
    report.coordinatorNanoseconds = coordinatorNanoseconds;
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

void printTextStorageMetrics(
    const char* fixture,
    const TSTextStorageMetrics& metrics)
{
    std::printf("perf.syntax_storage.%s.edits=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    metrics.editCount));
    std::printf("perf.syntax_storage.%s.materializations=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    metrics.materializationCount));
    std::printf("perf.syntax_storage.%s.input_reads=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    metrics.inputReadCount));
    std::printf("perf.syntax_storage.%s.moved_characters=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    metrics.movedCharacterCount));
    std::printf("perf.syntax_storage.%s.first_edit_position=%d\n",
                fixture,
                metrics.firstEditPosition);
    std::printf("perf.syntax_storage.%s.first_edit_gap_start=%d\n",
                fixture,
                metrics.firstEditGapStart);
    const auto meanMicroseconds =
        [&metrics](std::uint64_t nanoseconds) {
            return metrics.editCount == 0
                ? std::uint64_t{0}
                : nanoseconds / metrics.editCount / 1000;
        };
    std::printf("perf.syntax_storage.%s.tree_edit.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.treeEditNanoseconds)));
    std::printf("perf.syntax_storage.%s.storage_edit.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.storageEditNanoseconds)));
    std::printf("perf.syntax_storage.%s.parse.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.parseNanoseconds)));
    std::printf("perf.syntax_storage.%s.changed_range.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.changedRangeNanoseconds)));
    std::printf("perf.syntax_storage.%s.tree_delete.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.treeDeleteNanoseconds)));
}

void printTypingCoreMetrics(const char* fixture,
                            const EditorHotPathMetrics& metrics)
{
    const std::uint64_t count = metrics.documentChanges;
    const auto meanMicroseconds = [count](std::uint64_t nanoseconds) {
        return count == 0 ? std::uint64_t{0}
                          : nanoseconds / count / 1000;
    };
    std::printf("perf.typing.%s.editor_core.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangeCoreNanoseconds)));
    std::printf("perf.typing.%s.editor_prepare.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangePrepareNanoseconds)));
    std::printf("perf.typing.%s.syntax.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangeSyntaxNanoseconds)));
    std::printf("perf.typing.%s.folding.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangeFoldingNanoseconds)));
    std::printf("perf.typing.%s.decoration.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangeDecorationNanoseconds)));
    std::printf("perf.typing.%s.occurrence.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangeOccurrenceNanoseconds)));
    std::printf("perf.typing.%s.presentation.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangePresentationNanoseconds)));
    std::printf("perf.typing.%s.derived_state.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangeDerivedStateNanoseconds)));
    std::printf("perf.typing.%s.delta_dispatch.mean_us=%llu\n",
                fixture,
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        metrics.documentChangeDispatchNanoseconds)));
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

void printLatencySegment(const char* name,
                         const LatencySummary& summary)
{
    std::printf("perf.visible_wave.%s.p50_us=%lld\n",
                name,
                static_cast<long long>(summary.p50Us));
    std::printf("perf.visible_wave.%s.p95_us=%lld\n",
                name,
                static_cast<long long>(summary.p95Us));
    std::printf("perf.visible_wave.%s.max_us=%lld\n",
                name,
                static_cast<long long>(summary.maxUs));
}

void printVisibleWaveMetrics(const VisibleWaveTypingReport& report)
{
    printLatencySegment("synchronous_key",
                        report.synchronousKeyLatency);
    printLatencySegment("event_processing",
                        report.eventProcessingLatency);
    const auto meanMicroseconds = [](std::uint64_t nanoseconds,
                                    std::uint64_t count) {
        return count == 0
            ? std::uint64_t{0}
            : nanoseconds / count / 1000;
    };
    const std::uint64_t changeCount =
        report.editorMetrics.documentChanges;
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
    std::printf("perf.visible_wave.segment.editor_core.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        report.editorMetrics.documentChangeCoreNanoseconds,
                        changeCount)));
    std::printf("perf.visible_wave.segment.delta_dispatch.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        report.editorMetrics.documentChangeDispatchNanoseconds,
                        changeCount)));
    std::printf("perf.visible_wave.segment.metadata.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(report.metadataNanoseconds,
                                     changeCount)));
    std::printf("perf.visible_wave.segment.scope_registry.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(report.scopeRegistryNanoseconds,
                                     changeCount)));
    std::printf("perf.visible_wave.segment.coordinator.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(report.coordinatorNanoseconds,
                                     changeCount)));
    std::printf("perf.visible_wave.segment.scope_cache.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        report.waveMetrics.scopeCacheUpdateNanoseconds,
                        changeCount)));
    std::printf("perf.visible_wave.segment.wave_service.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(report.waveMetrics.waveServiceNanoseconds,
                                     changeCount)));
    std::printf("perf.visible_wave.segment.model_scene.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(
                        report.waveMetrics.modelSceneRebuildNanoseconds,
                        changeCount)));
    std::printf("perf.visible_wave.segment.canvas_update.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(report.waveMetrics.canvasUpdateNanoseconds,
                                     changeCount)));
    std::printf("perf.visible_wave.segment.canvas_paint.mean_us=%llu\n",
                static_cast<unsigned long long>(
                    meanMicroseconds(report.waveMetrics.canvasPaintNanoseconds,
                                     report.waveMetrics.canvasPaintCount)));
    std::printf("perf.visible_wave.segment.canvas_paint.count=%d\n",
                report.waveMetrics.canvasPaintCount);
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

void exerciseAnchoredRangeIndex()
{
    constexpr int denseCount = 32768;
    QList<AnchoredRangeFixture> dense;
    dense.reserve(denseCount);
    for (int index = 0; index < denseCount; ++index) {
        dense.append(AnchoredRangeFixture{
            index * 4,
            2,
            index,
            index});
    }

    EditorAnchoredRangeIndex<
        AnchoredRangeFixture,
        AnchoredRangeFixtureTraits> index;
    index = dense;
    const EditorAnchoredRangeIndexStats before =
        index.stats();

    DocumentChange prefix;
    prefix.position = 0;
    prefix.insertedText = QStringLiteral("//\n");
    prefix.oldLength = denseCount * 4;
    prefix.newLength = prefix.oldLength + prefix.insertedText.size();
    prefix.lineDelta = 1;
    const EditorAnchoredRangeRemapReport shifted =
        index.remap(prefix);
    const EditorAnchoredRangeIndexStats after =
        index.stats();
    expect("anchored range suffix shift is logarithmic",
           shifted.removedItems == 0
               && shifted.shiftedItems == denseCount
               && shifted.visitedNodes < 256);
    expect("anchored range remap does not materialize dense storage",
           after.materializationCount
               == before.materializationCount);

    qsizetype visibleVisited = 0;
    const QList<AnchoredRangeFixture> visible =
        index.overlappingLines(12001, 12001, &visibleVisited);
    expect("anchored range visible query is stable and bounded",
           visible.size() == 1
               && visible.first().serial == 12000
               && visible.first().startPosition == 12000 * 4 + 3
               && visibleVisited < 128);

    const int crossedSerial = 1000;
    DocumentChange crossing;
    crossing.position = crossedSerial * 4 + 3 + 1;
    crossing.insertedText = QStringLiteral("x");
    crossing.oldLength = prefix.newLength;
    crossing.newLength = crossing.oldLength + 1;
    const EditorAnchoredRangeRemapReport crossed =
        index.remap(crossing);
    expect("anchored range crossing edit invalidates only the overlap",
           crossed.removedItems == 1
               && crossed.shiftedItems
                      == denseCount - crossedSerial - 1
               && crossed.visitedNodes < 256
               && index.size() == denseCount - 1);

    EditorAnchoredRangeIndex<
        AnchoredRangeFixture,
        AnchoredRangeFixtureTraits> equalStart;
    equalStart = {
        AnchoredRangeFixture{20, 4, 2, 7},
        AnchoredRangeFixture{20, 4, 2, 8},
        AnchoredRangeFixture{20, 4, 2, 9}};
    const QList<AnchoredRangeFixture> equalVisible =
        equalStart.overlapping(20, 24);
    expect("anchored range equal-position query preserves input order",
           equalVisible.size() == 3
               && equalVisible.at(0).serial == 7
               && equalVisible.at(1).serial == 8
               && equalVisible.at(2).serial == 9);
}

void exerciseDenseAnnotationRemap()
{
    constexpr int annotationCount = 4096;
    QString text = QStringLiteral("module dense_annotations;\n");
    QList<GhostAnnotation> ghosts;
    QList<SemanticDecoration> decorations;
    ghosts.reserve(annotationCount);
    decorations.reserve(annotationCount);
    for (int index = 0; index < annotationCount; ++index) {
        const int start = text.size();
        text.append(QStringLiteral("logic dense_signal;\n"));

        GhostAnnotation ghost;
        ghost.kind = GhostAnnotationKind::SignalWidth;
        ghost.placement = GhostAnnotationPlacement::RightOfLine;
        ghost.text = QStringLiteral("1 bit %1").arg(index);
        ghost.line = index + 2;
        ghost.anchorPosition = start;
        ghost.anchorLength = 5;
        ghosts.append(ghost);

        SemanticDecoration decoration;
        decoration.role = SemanticDecorationRole::ActualSignal;
        decoration.text = QStringLiteral("logic");
        decoration.startPosition = start;
        decoration.length = 5;
        decorations.append(decoration);
    }
    text.append(QStringLiteral("endmodule\n"));

    MyCodeEditor editor;
    editor.resize(640, 320);
    editor.setPlainText(text);
    editor.acceptLoadedTextAsSemanticBaseline();
    editor.setGhostAnnotations(ghosts);
    editor.setSemanticDecorations(decorations);

    QTextCursor prefix(editor.document());
    prefix.setPosition(0);
    prefix.insertText(QStringLiteral("//\n"));
    expect("dense Ghost suffix remap visits logarithmic nodes",
           editor.property(
                     "zeroslackGhostAnnotationRemapVisitedCount")
                       .toLongLong()
                   < 512
               && editor.property(
                     "zeroslackGhostAnnotationRemapShiftedCount")
                       .toLongLong()
                   == annotationCount
               && editor.property(
                     "zeroslackGhostAnnotationRemapMaterializationCount")
                       .toLongLong()
                   == 0);
    expect("dense semantic suffix remap visits logarithmic nodes",
           editor.property(
                     "zeroslackSemanticDecorationRemapVisitedCount")
                       .toLongLong()
                   < 256
               && editor.property(
                     "zeroslackSemanticDecorationRemapShiftedCount")
                       .toLongLong()
                   == annotationCount
               && editor.property(
                     "zeroslackSemanticDecorationRemapMaterializationCount")
                       .toLongLong()
                   == 0);
    expect("dense semantic visible query remains local",
           editor.property(
                     "zeroslackSemanticDecorationVisibleQueryVisitedCount")
                       .toLongLong()
                   < 256);

    AnnotationLayerQuery visibleQuery;
    visibleQuery.firstVisibleLine = 0;
    visibleQuery.lastVisibleLine = 8;
    const AnnotationLayerReport visibleGhosts =
        editor.annotationLayerReportForTest(visibleQuery);
    expect("dense Ghost layer materializes only visible stable rows",
           visibleGhosts.inputCount == annotationCount
               && visibleGhosts.examinedCount <= 8
               && visibleGhosts.offscreenCount
                      >= annotationCount - 8);

    const QList<GhostAnnotation> shifted =
        editor.ghostAnnotationsForTest();
    const int crossingPosition =
        shifted.at(annotationCount / 2).anchorPosition + 1;
    QTextCursor crossing(editor.document());
    crossing.setPosition(crossingPosition);
    crossing.insertText(QStringLiteral("x"));
    expect("dense crossing edit removes one Ghost and semantic anchor",
           editor.property(
                     "zeroslackGhostAnnotationRemapRemovedCount")
                       .toLongLong()
                   == 1
               && editor.property(
                     "zeroslackSemanticDecorationRemapRemovedCount")
                       .toLongLong()
                   == 1
               && editor.ghostAnnotationsForTest().size()
                      == annotationCount - 1);
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
    ghostEditor.setGhostAnnotations({annotation, annotation});
    AnnotationLayerQuery ghostLayerQuery;
    ghostLayerQuery.firstVisibleLine = 0;
    ghostLayerQuery.lastVisibleLine = 3;
    const AnnotationLayerReport ghostLayerReport =
        ghostEditor.annotationLayerReportForTest(ghostLayerQuery);
    expect("editor annotation source deduplicates repeated ghost values",
           ghostLayerReport.inputCount == 2
               && ghostLayerReport.duplicateCount == 1
               && ghostLayerReport.annotations.size() == 1
               && ghostLayerReport.annotations.first().annotation.kind
                      == EditorAnnotationKind::EffectiveValue);
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
    expect("empty semantic snapshot starts no Ghost worker",
           !hasFutureWatcherChild(ghostEditor));

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
    expect("large document without semantic snapshot starts no Ghost worker",
           !hasFutureWatcherChild(largeGhostEditor));

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

enum class LifecycleEditorPresentation {
    TopLevelVisible,
    ChildVisible,
    Hidden
};

void exerciseSingleLineReplacementLifecycle(
    int replacementLimit = -1,
    LifecycleEditorPresentation presentation =
        LifecycleEditorPresentation::TopLevelVisible,
    const QByteArray& diagnosticProfile = QByteArray())
{
    if (!diagnosticProfile.isEmpty()) {
        qputenv("ZEROSLACK_EDITOR_LIFECYCLE_PROFILE",
                diagnosticProfile);
    }
    {
        QWidget host;
        MyCodeEditor editor(
            presentation == LifecycleEditorPresentation::ChildVisible
                ? &host
                : nullptr);
        editor.resize(560, 160);
        if (presentation != LifecycleEditorPresentation::Hidden) {
            if (presentation == LifecycleEditorPresentation::ChildVisible) {
                host.resize(600, 200);
                host.show();
                editor.show();
            } else {
                editor.show();
            }
            editor.setFocus();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }

        const QStringList replacements{
            QStringLiteral("ab"),
            QStringLiteral("FOO"),
            QStringLiteral("`FOO"),
            QStringLiteral("`FOO_BAR"),
            QStringLiteral("obj.member"),
            QStringLiteral("pkg::member")
        };
        const int count = replacementLimit < 0
            ? replacements.size()
            : qBound(0, replacementLimit, replacements.size());
        for (int index = 0; index < count; ++index) {
            const QString& replacement = replacements.at(index);
            editor.clear();
            QTest::keyClicks(&editor, replacement);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            expect(QStringLiteral("repeated clear/type keeps caches synchronized: %1")
                       .arg(replacement),
                   editor.cachedDocumentText() == replacement
                       && editor.syntaxTextForTest() == replacement);
        }
    }
    if (!diagnosticProfile.isEmpty())
        qunsetenv("ZEROSLACK_EDITOR_LIFECYCLE_PROFILE");
    expect("single-line replacement editor destroys cleanly", true);
}

void exerciseMultilineReplacementLifecycle()
{
    {
        MyCodeEditor editor;
        expect("second editor constructs after the first editor is destroyed",
               true);
        const QString multiline = QStringLiteral(
            "module command_line;\n"
            "  logic a;\n"
            "  logic b;\n"
            "  assign b = a;\n"
            "endmodule\n");
        editor.setPlainText(multiline);
        expect("second editor accepts multiline replacement",
               editor.cachedDocumentText() == multiline
                   && editor.syntaxTextForTest() == multiline);
        editor.setPlainText(QString());
        expect("second editor clears multiline replacement",
               editor.cachedDocumentText().isEmpty()
                   && editor.syntaxTextForTest().isEmpty());
        QTest::keyClicks(&editor, QStringLiteral("`"));
        expect("multiline replacement followed by input remains stable",
               editor.cachedDocumentText() == QStringLiteral("`")
                   && editor.syntaxTextForTest() == QStringLiteral("`"));
    }
    expect("multiline replacement editor destroys cleanly", true);
}

void exerciseRepeatedReplacementStability()
{
    exerciseSingleLineReplacementLifecycle();
    exerciseMultilineReplacementLifecycle();
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
    if (argc == 2) {
        const QString mode = QString::fromLocal8Bit(argv[1]);
        bool handled = true;
        if (mode == QStringLiteral("--lifecycle-single"))
            exerciseSingleLineReplacementLifecycle();
        else if (mode == QStringLiteral("--lifecycle-multiline"))
            exerciseMultilineReplacementLifecycle();
        else if (mode.startsWith(
                     QStringLiteral("--lifecycle-multiline-repeat="))) {
            bool ok = false;
            const int repeatCount = mode.sliced(
                QStringLiteral("--lifecycle-multiline-repeat=").size())
                                        .toInt(&ok);
            if (ok && repeatCount >= 0) {
                for (int index = 0; index < repeatCount; ++index)
                    exerciseMultilineReplacementLifecycle();
            } else {
                handled = false;
            }
        }
        else if (mode == QStringLiteral("--lifecycle-only"))
            exerciseRepeatedReplacementStability();
        else if (mode.startsWith(QStringLiteral("--lifecycle-count="))) {
            bool ok = false;
            const int replacementCount =
                mode.sliced(QStringLiteral("--lifecycle-count=").size())
                    .toInt(&ok);
            if (ok) {
                exerciseSingleLineReplacementLifecycle(replacementCount);
                exerciseMultilineReplacementLifecycle();
            } else {
                handled = false;
            }
        }
        else if (mode.startsWith(QStringLiteral("--lifecycle-hidden-count="))) {
            bool ok = false;
            const int replacementCount =
                mode.sliced(
                        QStringLiteral("--lifecycle-hidden-count=").size())
                    .toInt(&ok);
            if (ok) {
                exerciseSingleLineReplacementLifecycle(
                    replacementCount,
                    LifecycleEditorPresentation::Hidden);
                exerciseMultilineReplacementLifecycle();
            } else {
                handled = false;
            }
        }
        else if (mode.startsWith(QStringLiteral("--lifecycle-child-count="))) {
            bool ok = false;
            const int replacementCount =
                mode.sliced(
                        QStringLiteral("--lifecycle-child-count=").size())
                    .toInt(&ok);
            if (ok) {
                exerciseSingleLineReplacementLifecycle(
                    replacementCount,
                    LifecycleEditorPresentation::ChildVisible);
                exerciseMultilineReplacementLifecycle();
            } else {
                handled = false;
            }
        }
        else if (mode.startsWith(QStringLiteral("--lifecycle-profile="))) {
            const QByteArray profile =
                mode.sliced(QStringLiteral("--lifecycle-profile=").size())
                    .toLocal8Bit();
            exerciseSingleLineReplacementLifecycle(
                0,
                LifecycleEditorPresentation::Hidden,
                profile);
            exerciseMultilineReplacementLifecycle();
        }
        else
            handled = false;
        if (handled) {
            std::printf("checks=%d failures=%d\n", checks, failures);
            return failures == 0 ? 0 : 1;
        }
    }
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

    exerciseAnchoredRangeIndex();
    exerciseDenseAnnotationRemap();
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
        printTypingCoreMetrics("rtl_top_after", report.metrics);
        printTextStorageMetrics("rtl_top_after",
                                report.textStorageMetrics);
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
        expect("rtl_top Tree-sitter reads edited gap storage without materializing it",
               report.textStorageMetrics.editCount == 45
                   && report.textStorageMetrics
                          .materializationCount == 0
                   && report.textStorageMetrics.inputReadCount
                          >= report.textStorageMetrics.editCount
                   && report.textStorageMetrics.movedCharacterCount
                          <= 128);
    }
    if (QFileInfo(hugeFile).isFile()) {
        const TypingReport report = measureTyping(hugeFile);
        printLatency("huge_after", report.latency);
        printTypingCoreMetrics("huge_after", report.metrics);
        printTextStorageMetrics("huge_after",
                                report.textStorageMetrics);
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
        expect("huge-file Tree-sitter reads local gap edits without per-key full-text movement",
               report.textStorageMetrics.editCount == 45
                   && report.textStorageMetrics
                          .materializationCount == 0
                   && report.textStorageMetrics.inputReadCount
                          >= report.textStorageMetrics.editCount
                   && report.textStorageMetrics.movedCharacterCount
                          <= 128);

        const VisibleWaveTypingReport waveReport =
            measureVisibleWaveTyping(hugeFile);
        printVisibleWaveLatency(waveReport.latency);
        printVisibleWaveMetrics(waveReport);
        printTextStorageMetrics("visible_wave",
                                waveReport.textStorageMetrics);
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
        expect("visible Wave syntax edits stay in Tree-sitter gap storage",
               waveReport.textStorageMetrics.editCount == 44
                   && waveReport.textStorageMetrics
                          .materializationCount == 0
                   && waveReport.textStorageMetrics.inputReadCount
                          >= waveReport.textStorageMetrics.editCount
                   && waveReport.textStorageMetrics.movedCharacterCount
                          <= 256);
        expect("visible Wave huge-file typing p95 stays below 6 ms",
               waveReport.latency.p95Us < 6000);
        expect("visible Wave huge-file typing max stays below 12 ms",
               waveReport.latency.maxUs < 12000);

        const InlineFilterReport inlineReport =
            measureInlineCandidateFiltering(hugeFile);
        printInlineFilterLatency(inlineReport.latency);
        printInlineFilterMetrics(inlineReport.metrics);
        printTextStorageMetrics("inline_filter",
                                inlineReport.textStorageMetrics);
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
        expect("huge inline filtering parses directly from local syntax gap edits",
               inlineReport.textStorageMetrics.editCount == 44
                   && inlineReport.textStorageMetrics
                          .materializationCount == 0
                   && inlineReport.textStorageMetrics.inputReadCount
                          >= inlineReport.textStorageMetrics.editCount
                   && inlineReport.textStorageMetrics.movedCharacterCount
                          <= 128);
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
