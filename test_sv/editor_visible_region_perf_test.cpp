#include "annotationlayer.h"
#include "editorsyntaxstate.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextCursor>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool condition)
{
    ++checks;
    if (!condition)
        ++failures;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                name);
}

EditorAnnotation annotationAtLine(int line)
{
    EditorAnnotation annotation;
    annotation.kind = EditorAnnotationKind::EffectiveValue;
    annotation.placement = EditorAnnotationPlacement::EndOfLine;
    annotation.range.startPosition = line * 16;
    annotation.range.endPosition = line * 16 + 4;
    annotation.range.firstLine = line;
    annotation.range.lastLine = line;
    annotation.text = QStringLiteral("value_%1").arg(line);
    annotation.semanticKey = QStringLiteral("signal_%1").arg(line);
    return annotation;
}

void exerciseBoundedAnnotationResolve()
{
    constexpr int sourceCount = 120000;
    QList<EditorAnnotation> annotations;
    annotations.reserve(sourceCount);
    for (int line = 0; line < sourceCount; ++line)
        annotations.append(annotationAtLine(line));
    EditorAnnotation spanning = annotationAtLine(0);
    spanning.range.endPosition = sourceCount * 16 + 4;
    spanning.range.lastLine = sourceCount - 1;
    spanning.text = QStringLiteral("spanning_value");
    spanning.semanticKey = QStringLiteral("spanning_signal");
    annotations.append(spanning);

    AnnotationLayer layer;
    layer.setSourceAnnotations(
        QStringLiteral("large-visible-source"),
        annotations);

    AnnotationLayerQuery query;
    query.firstVisibleLine = 54320;
    query.lastVisibleLine = 54359;
    query.maxAnnotationsPerLine = 2;
    query.maxLanes = 2;
    const AnnotationLayerReport report = layer.resolve(query);
    expect("visible annotation query retains full source accounting",
           report.inputCount == sourceCount + 1
               && report.offscreenCount == sourceCount - 40);
    expect("visible annotation query examines its window and one spanning range",
           report.examinedCount == 41
               && report.candidateCount == 41
               && report.annotations.size() == 41);

    QElapsedTimer timer;
    timer.start();
    int resolved = 0;
    int examined = 0;
    for (int iteration = 0; iteration < 100; ++iteration) {
        query.firstVisibleLine = 1000 + iteration * 997;
        query.lastVisibleLine = query.firstVisibleLine + 39;
        const AnnotationLayerReport sample = layer.resolve(query);
        resolved += sample.annotations.size();
        examined += sample.examinedCount;
    }
    const qint64 elapsedMs = timer.elapsed();
    std::printf("perf.annotation.visible_100_queries_ms=%lld\n",
                static_cast<long long>(elapsedMs));
    std::printf("perf.annotation.visible_examined=%d\n", examined);
    expect("100 visible annotation resolves remain work-bounded",
           resolved == 4100 && examined == 4100);
    expect("100 indexed visible annotation resolves stay below 1500 ms",
           elapsedMs < 1500);
}

struct DenseEditorFixture {
    QString text;
    QList<int> positions;
};

DenseEditorFixture denseEditorFixture(int signalCount)
{
    DenseEditorFixture fixture;
    fixture.text.reserve(signalCount * 24);
    fixture.positions.reserve(signalCount);
    fixture.text.append(QStringLiteral("module visible_perf;\n"));
    for (int index = 0; index < signalCount; ++index) {
        fixture.text.append(QStringLiteral("  logic "));
        fixture.positions.append(fixture.text.size());
        fixture.text.append(
            QStringLiteral("signal_%1;\n").arg(index));
    }
    fixture.text.append(QStringLiteral("endmodule\n"));
    return fixture;
}

void exerciseVisibleEditorPresentation()
{
    constexpr int signalCount = 12000;
    const DenseEditorFixture fixture =
        denseEditorFixture(signalCount);

    MyCodeEditor editor;
    editor.resize(720, 180);
    editor.setLineWrapMode(QPlainTextEdit::NoWrap);
    editor.setPlainText(fixture.text);
    editor.setDocumentFileName(
        QStringLiteral("visible_region_perf.sv"));

    QList<SemanticDecoration> decorations;
    QList<SemanticDiagnostic> diagnostics;
    decorations.reserve(signalCount);
    diagnostics.reserve(signalCount);
    for (int index = 0; index < signalCount; ++index) {
        const QString name =
            QStringLiteral("signal_%1").arg(index);
        SemanticDecoration decoration;
        decoration.role = SemanticDecorationRole::ActualSignal;
        decoration.text = name;
        decoration.startPosition = fixture.positions.at(index);
        decoration.length = name.size();
        decorations.append(decoration);

        SemanticSourceRange range;
        range.fileName = QStringLiteral("visible_region_perf.sv");
        range.line = index + 2;
        range.column = 3;
        range.endLine = range.line;
        range.endColumn = range.column + name.size();
        range.position = fixture.positions.at(index);
        range.length = name.size();
        SemanticDiagnostic diagnostic;
        diagnostic.fileName = range.fileName;
        diagnostic.line = range.line;
        diagnostic.column = range.column;
        diagnostic.message =
            QStringLiteral("visible diagnostic %1").arg(index);
        diagnostic.codeName = QStringLiteral("VisiblePerf");
        diagnostic.ranges = {range};
        diagnostic.severity = SemanticDiagnostic::Warning;
        diagnostics.append(diagnostic);
    }

    editor.setSemanticDecorations(decorations);
    editor.setDiagnosticHighlights(diagnostics);
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    QSignalSpy documentChanges(
        &editor, &MyCodeEditor::documentChangeApplied);
    QSignalSpy waveScopeChanges(
        &editor, &MyCodeEditor::wavePreviewScopeChanged);
    editor.resetHotPathMetricsForTest();

    QScrollBar* scrollBar = editor.verticalScrollBar();
    const int maximum = scrollBar ? scrollBar->maximum() : 0;
    QElapsedTimer timer;
    timer.start();
    constexpr int scrollCount = 64;
    for (int step = 1; step <= scrollCount; ++step) {
        if (scrollBar) {
            scrollBar->setValue(
                maximum * step / scrollCount);
        }
        QCoreApplication::processEvents(
            QEventLoop::AllEvents, 5);
    }
    const int waveScopeChangesAfterScroll =
        waveScopeChanges.size();

    const QTextBlock firstVisible =
        editor.cursorForPosition(QPoint(0, 0)).block();
    for (int offset = 0; offset < 8; ++offset) {
        const QTextBlock block =
            editor.document()->findBlockByNumber(
                qMin(editor.blockCount() - 1,
                     firstVisible.blockNumber() + offset));
        if (block.isValid())
            editor.setTextCursor(QTextCursor(block));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const qint64 elapsedMs = timer.elapsed();
    const EditorHotPathMetrics metrics =
        editor.hotPathMetricsForTest();

    std::printf("perf.editor.visible_scroll_ms=%lld\n",
                static_cast<long long>(elapsedMs));
    std::printf("perf.editor.visible_refreshes=%llu\n",
                static_cast<unsigned long long>(
                    metrics.visiblePresentationRefreshes));
    std::printf("perf.editor.semantic_candidates=%llu\n",
                static_cast<unsigned long long>(
                    metrics.semanticDecorationCandidatesExamined));
    std::printf("perf.editor.diagnostic_line_probes=%llu\n",
                static_cast<unsigned long long>(
                    metrics.diagnosticVisibleLineProbes));
    std::printf("perf.editor.diagnostic_candidates=%llu\n",
                static_cast<unsigned long long>(
                    metrics.diagnosticCandidatesExamined));

    expect("scroll produces immediate visible presentation refreshes",
           metrics.visiblePresentationRefreshes >= scrollCount / 2);
    expect("semantic decoration work scales with visible lines",
           metrics.semanticDecorationCandidatesExamined
               <= metrics.visiblePresentationRefreshes * 64 + 64
               && metrics.semanticDecorationSelectionsBuilt
                      <= metrics.visiblePresentationRefreshes * 64 + 64);
    expect("diagnostic presentation probes only visible lines",
           metrics.diagnosticVisibleLineProbes
                   <= metrics.visiblePresentationRefreshes * 64 + 64
               && metrics.diagnosticCandidatesExamined
                      <= metrics.visiblePresentationRefreshes * 64 + 64);
    expect("scroll performs no wave-preview scope publication",
           waveScopeChangesAfterScroll == 0);
    expect("scroll and cursor movement perform no document or full rebuild",
           documentChanges.isEmpty()
               && metrics.documentChanges == 0
               && metrics.fullTextMaterializations == 0
               && metrics.fullFoldingRebuilds == 0
               && metrics.fullGhostQueries == 0);
    expect("64 dense large-editor scrolls stay below 2500 ms",
           elapsedMs < 2500);
}

struct HugeSyntaxFixture {
    QString text;
    QString targetModule;
    QString targetPackage;
    int packageProbePosition = -1;
    int moduleProbePosition = -1;
    int editPosition = -1;
    int alwaysBeginPosition = -1;
    int keywordPrefixEndPosition = -1;
    int identifierProbePosition = -1;
    int conditionalProbePosition = -1;
};

HugeSyntaxFixture hugeSyntaxFixture()
{
    HugeSyntaxFixture fixture;
    constexpr int moduleCount = 24000;
    constexpr int targetIndex = moduleCount / 2;
    fixture.text.reserve(2300000);
    fixture.targetPackage =
        QStringLiteral("large_file_package");
    fixture.text.append(
        QStringLiteral(
            "package %1;\n"
            "  logic package_value;\n"
            "endpackage\n")
            .arg(fixture.targetPackage));
    fixture.packageProbePosition =
        fixture.text.indexOf(
            QStringLiteral("package_value"));
    for (int index = 0; index < moduleCount; ++index) {
        const QString moduleName =
            QStringLiteral("large_scope_%1")
                .arg(index, 5, 10, QLatin1Char('0'));
        if (index == targetIndex)
            fixture.targetModule = moduleName;
        const int moduleStart = fixture.text.size();
        if (index == targetIndex) {
            const QString featureModule =
                QStringLiteral(
                    "module %1;\n"
                    "  logic enable;\n"
                    "  logic selected_signal;\n"
                    "  logic sink;\n"
                    "`ifdef LARGE_FEATURE_A\n"
                    "  logic branch_a;\n"
                    "`elsif LARGE_FEATURE_B\n"
                    "  logic branch_b;\n"
                    "`else\n"
                    "  logic branch_default;\n"
                    "`endif\n"
                    "  always_comb begin\n"
                    "    selected_signal = enable;\n"
                    "    sink = selected_signal;\n"
                    "  end\n"
                    "endmodule\n"
                    "module %1_keyword_probe;\n"
                    "  always_comb beg\n"
                    "endmodule\n")
                    .arg(moduleName);
            fixture.text.append(featureModule);
            fixture.moduleProbePosition =
                moduleStart
                + featureModule.indexOf(
                    QStringLiteral("logic enable"));
            fixture.editPosition =
                moduleStart
                + featureModule.indexOf(
                    QStringLiteral(
                        "selected_signal = enable;"));
            fixture.alwaysBeginPosition =
                moduleStart
                + featureModule.indexOf(
                    QStringLiteral("begin"));
            const int keywordLine =
                featureModule.lastIndexOf(
                    QStringLiteral("always_comb beg"));
            fixture.keywordPrefixEndPosition =
                moduleStart
                + featureModule.indexOf(
                    QStringLiteral("beg"),
                    keywordLine)
                + QStringLiteral("beg").size();
            fixture.identifierProbePosition =
                moduleStart
                + featureModule.indexOf(
                    QStringLiteral(
                        "sink = selected_signal"))
                + QStringLiteral("sink = ").size();
            fixture.conditionalProbePosition =
                moduleStart
                + featureModule.indexOf(
                    QStringLiteral("logic branch_a"));
        } else {
            fixture.text.append(
                QStringLiteral(
                    "module %1;\n"
                    "  logic enable;\n"
                    "  logic value;\n"
                    "  always_comb begin\n"
                    "    value = enable;\n"
                    "  end\n"
                    "endmodule\n")
                    .arg(moduleName));
        }
    }
    return fixture;
}

void exerciseLargeFileIncrementalChangedRanges()
{
    const HugeSyntaxFixture fixture = hugeSyntaxFixture();
    expect("synthetic syntax fixture exceeds large-file threshold",
           fixture.text.size() > 2 * 1024 * 1024
               && fixture.editPosition > 0
               && fixture.packageProbePosition > 0
               && fixture.keywordPrefixEndPosition > 0);

    MyCodeEditor editor;
    editor.setPlainText(fixture.text);
    const TSDocument* fullSyntax = editor.syntaxDocument();
    expect("large file retains one complete Tree-sitter document",
           fullSyntax
               && fullSyntax->text().size() == fixture.text.size()
               && fullSyntax->text() == fixture.text);
    expect("large-file package and module lookup remain available",
           fullSyntax
               && fullSyntax->enclosingPackageName(
                      fixture.packageProbePosition)
                      == fixture.targetPackage
               && fullSyntax->enclosingModuleName(
                      fixture.moduleProbePosition)
                      == fixture.targetModule);

    const TSStructuralNewlineTarget structuralNewline =
        fullSyntax
        ? fullSyntax->structuralNewlineTarget(
              fixture.alwaysBeginPosition
              + QStringLiteral("begin").size())
        : TSStructuralNewlineTarget{};
    const TSKeywordCompletionTarget keywordCompletion =
        fullSyntax
        ? fullSyntax->uniqueKeywordCompletionAt(
              fixture.keywordPrefixEndPosition)
        : TSKeywordCompletionTarget{};
    const TSKeywordPairTarget keywordPair =
        fullSyntax
        ? fullSyntax->matchingKeywordPairAt(
              fixture.alwaysBeginPosition + 1)
        : TSKeywordPairTarget{};
    expect("large-file structural input, keyword completion, and pair matching remain available",
           structuralNewline.ok()
               && keywordCompletion.ok()
               && keywordCompletion.keyword
                      == QStringLiteral("begin")
               && keywordPair.ok()
               && keywordPair.openingKeyword
                      == QStringLiteral("begin")
               && keywordPair.closingKeyword
                      == QStringLiteral("end"));

    const TSIdentifierTarget identifier =
        fullSyntax
        ? fullSyntax->identifierAt(
              fixture.identifierProbePosition)
        : TSIdentifierTarget{};
    const TSIdentifierOccurrenceSet occurrences =
        fullSyntax
        ? fullSyntax->identifierOccurrencesAt(
              fixture.identifierProbePosition)
        : TSIdentifierOccurrenceSet{};
    const TSAssignmentNavigationTarget assignment =
        fullSyntax
        ? fullSyntax->assignmentNavigationTarget(
              fixture.identifierProbePosition,
              false)
        : TSAssignmentNavigationTarget{};
    const TSConditionalBranchNavigationTarget conditional =
        fullSyntax
        ? fullSyntax->conditionalBranchNavigationTarget(
              fixture.conditionalProbePosition,
              false)
        : TSConditionalBranchNavigationTarget{};
    expect("large-file identifier, occurrence, assignment, and conditional navigation remain available",
           identifier.ok()
               && identifier.text
                      == QStringLiteral("selected_signal")
               && occurrences.ok()
               && occurrences.occurrences.size() >= 2
               && assignment.ok()
               && assignment.identifier
                      == QStringLiteral("selected_signal")
               && conditional.ok()
               && conditional.targetDirective
                      == QStringLiteral("`elsif"));

    QTextCursor assignmentCursor(editor.document());
    assignmentCursor.setPosition(
        fixture.identifierProbePosition);
    editor.setTextCursor(assignmentCursor);
    QString assignmentMessage;
    const bool assignmentActionAvailable =
        editor.goToNextAssignmentForSelectedSignal(
            &assignmentMessage);
    QTextCursor conditionalCursor(editor.document());
    conditionalCursor.setPosition(
        fixture.conditionalProbePosition);
    editor.setTextCursor(conditionalCursor);
    QString conditionalMessage;
    const bool conditionalActionAvailable =
        editor.goToNextConditionalBranch(
            &conditionalMessage);
    expect("large-file editor routes assignment and conditional actions through full syntax",
           assignmentActionAvailable
               && assignmentMessage.contains(
                   QStringLiteral("assignment"),
                   Qt::CaseInsensitive)
               && conditionalActionAvailable
               && conditionalMessage.contains(
                   QStringLiteral("conditional"),
                   Qt::CaseInsensitive));

    QTextCursor keywordCursor(editor.document());
    keywordCursor.setPosition(
        fixture.keywordPrefixEndPosition);
    editor.setTextCursor(keywordCursor);
    QCoreApplication::processEvents(
        QEventLoop::AllEvents, 5);
    expect("large-file keyword ghost mode remains available",
           editor.editorModeActiveForTest(
               EditorModeId::KeywordGhost));
    editor.exitInteractionModes(
        EditorModeExitReason::ExternalControl);

    QTextCursor occurrenceCursor(editor.document());
    occurrenceCursor.setPosition(
        fixture.identifierProbePosition);
    editor.setTextCursor(occurrenceCursor);
    QString occurrenceFailure;
    const bool selectedAllOccurrences =
        editor.selectAllSymbolOccurrences(
            &occurrenceFailure);
    expect("large-file same-symbol multi-cursor selection remains available",
           selectedAllOccurrences
               && occurrenceFailure.isEmpty()
               && editor.editorModeActiveForTest(
                   EditorModeId::MultiCursor));
    editor.exitInteractionModes(
        EditorModeExitReason::ExternalControl);

    QTextCursor cursor(editor.document());
    cursor.setPosition(fixture.editPosition);
    editor.setTextCursor(cursor);
    const EditorAlwaysScopeTarget initialTarget =
        editor.currentAlwaysScopeTarget();
    const EditorLargeFileSyntaxSnapshot initial =
        editor.largeFileSyntaxSnapshotForTest();
    expect("large file exposes full-document Tree-sitter state",
           initialTarget.ok()
               && initial.valid()
               && initial.fullDocumentSyntax
               && initial.syntaxTextLength
                      == fixture.text.size()
               && initial.startPosition == 0
               && initial.endPosition
                      == fixture.text.size());

    editor.resetHotPathMetricsForTest();
    if (const TSDocument* syntax = editor.syntaxDocument())
        syntax->resetTextStorageMetricsForTest();
    const QString oldStatement =
        QStringLiteral("selected_signal = enable;");
    const QString newStatement =
        QStringLiteral(
            "if (enable) selected_signal = 1'b1;");
    QTextCursor edit(editor.document());
    edit.setPosition(fixture.editPosition);
    edit.setPosition(
        fixture.editPosition + oldStatement.size(),
        QTextCursor::KeepAnchor);
    QElapsedTimer editTimer;
    editTimer.start();
    edit.insertText(newStatement);
    const qint64 editElapsedMs = editTimer.elapsed();
    const TSTextStorageMetrics storageMetrics =
        editor.syntaxDocument()
        ? editor.syntaxDocument()->textStorageMetricsForTest()
        : TSTextStorageMetrics{};
    const TSTextStorageMetrics cachedStorageMetrics =
        editor.cachedTextStorageMetricsForTest();

    const EditorLargeFileSyntaxSnapshot afterEdit =
        editor.largeFileSyntaxSnapshotForTest();
    const int cachedLength = editor.cachedDocumentLength();
    const int cacheProbeStart = qMax(0, fixture.editPosition - 32);
    const int cacheProbeLength = qMin(
        cachedLength - cacheProbeStart,
        newStatement.size() + 64);
    expect("large-file edit incrementally updates the full Tree-sitter document",
           afterEdit.valid()
               && afterEdit.fullBuildCount
                      == initial.fullBuildCount
               && afterEdit.incrementalEditCount
                      == initial.incrementalEditCount + 1
               && afterEdit.syntaxTextLength
                      == fixture.text.size()
                              - oldStatement.size()
                              + newStatement.size()
               && editor.syntaxDocument()
               && cachedLength
                      == editor.syntaxDocument()->text().size()
               && editor.cachedDocumentSlice(
                      cacheProbeStart, cacheProbeLength)
                      == editor.syntaxDocument()->text().mid(
                          cacheProbeStart, cacheProbeLength));
    expect("large-file Tree-sitter reports a bounded changed range",
           afterEdit.lastChangedRangeCount > 0
               && afterEdit.lastChangedRangeCount <= 8
               && afterEdit.lastChangedCharacterCount > 0
               && afterEdit.lastChangedCharacterCount < 4096);
    expect("large-file incremental parse reads gap storage without a contiguous text rebuild",
           storageMetrics.editCount == 1
               && storageMetrics.materializationCount == 0
               && storageMetrics.inputReadCount > 0
               && storageMetrics.movedCharacterCount
                      <= static_cast<std::uint64_t>(
                             fixture.text.size()));
    expect("large-file editor cache applies the delta without materializing or moving unchanged text",
           cachedStorageMetrics.editCount == 1
               && cachedStorageMetrics.materializationCount == 0
               && cachedStorageMetrics.movedCharacterCount == 0);
    expect("large-file incremental syntax edit stays below 2000 ms",
           editElapsedMs < 2000);
    const int editDelta =
        newStatement.size() - oldStatement.size();
    const int editedIdentifierPosition =
        fixture.editPosition
        + newStatement.indexOf(
            QStringLiteral("selected_signal"));
    const TSDocument* updatedSyntax =
        editor.syntaxDocument();
    expect("large-file structural queries remain current after incremental edit",
           updatedSyntax
               && updatedSyntax->enclosingModuleName(
                      editedIdentifierPosition)
                      == fixture.targetModule
               && updatedSyntax->identifierAt(
                      editedIdentifierPosition)
                      .text
                      == QStringLiteral("selected_signal")
               && updatedSyntax->assignmentNavigationTarget(
                      editedIdentifierPosition,
                      false)
                      .ok()
               && updatedSyntax->uniqueKeywordCompletionAt(
                      fixture.keywordPrefixEndPosition
                      + editDelta)
                      .keyword
                      == QStringLiteral("begin"));

    QElapsedTimer timer;
    timer.start();
    bool targetsRemainValid = true;
    for (int offset = 0; offset < 16; ++offset) {
        QTextCursor probe(editor.document());
        probe.setPosition(
            fixture.editPosition
            + qMin(offset,
                   static_cast<int>(newStatement.size()) - 1));
        editor.setTextCursor(probe);
        targetsRemainValid =
            editor.currentAlwaysScopeTarget().ok()
            && targetsRemainValid;
    }
    const qint64 elapsedMs = timer.elapsed();
    const EditorLargeFileSyntaxSnapshot afterCursorMoves =
        editor.largeFileSyntaxSnapshotForTest();
    const EditorHotPathMetrics metrics =
        editor.hotPathMetricsForTest();
    std::printf("perf.syntax.full_document_cursor_queries_ms=%lld\n",
                static_cast<long long>(elapsedMs));
    std::printf("perf.syntax.full_document_incremental_edit_ms=%lld\n",
                static_cast<long long>(editElapsedMs));
    std::printf("perf.syntax.full_builds=%llu\n",
                static_cast<unsigned long long>(
                    afterEdit.fullBuildCount));
    std::printf("perf.syntax.incremental_edits=%llu\n",
                static_cast<unsigned long long>(
                    afterEdit.incrementalEditCount));
    std::printf("perf.syntax.changed_ranges=%d\n",
                afterEdit.lastChangedRangeCount);
    std::printf("perf.syntax.changed_characters=%d\n",
                afterEdit.lastChangedCharacterCount);
    std::printf("perf.syntax.storage_materializations=%llu\n",
                static_cast<unsigned long long>(
                    storageMetrics.materializationCount));
    std::printf("perf.syntax.storage_input_reads=%llu\n",
                static_cast<unsigned long long>(
                    storageMetrics.inputReadCount));
    std::printf("perf.syntax.storage_moved_characters=%llu\n",
                static_cast<unsigned long long>(
                    storageMetrics.movedCharacterCount));
    std::printf("perf.editor_cache.storage_materializations=%llu\n",
                static_cast<unsigned long long>(
                    cachedStorageMetrics.materializationCount));
    std::printf("perf.editor_cache.storage_moved_characters=%llu\n",
                static_cast<unsigned long long>(
                    cachedStorageMetrics.movedCharacterCount));
    expect("cursor queries reuse the incrementally updated full syntax tree",
           targetsRemainValid
               && afterCursorMoves.fullBuildCount
                      == afterEdit.fullBuildCount
               && afterCursorMoves.incrementalEditCount
                      == afterEdit.incrementalEditCount);
    expect("large-file incremental edit avoids full editor hot paths",
           metrics.documentChanges == 1
               && metrics.fullTextMaterializations == 0
               && metrics.fullFoldingRebuilds == 0
               && metrics.fullGhostQueries == 0);
    expect("16 full-document syntax cursor queries stay below 500 ms",
           elapsedMs < 500);
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    exerciseBoundedAnnotationResolve();
    exerciseVisibleEditorPresentation();
    exerciseLargeFileIncrementalChangedRanges();
    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
