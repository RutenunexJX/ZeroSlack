#include "editorruntime.h"

#include "effectivevalueservice.h"
#include "insightvisualstyle.h"
#include "mycodeeditor.h"
#include "semanticindex.h"
#include "tsdocument.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QHash>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
constexpr const char* kDiagnosticsEmptyProperty =
    "zeroslackDiagnosticsSelectionsEmpty";
constexpr const char* kSemanticDecorationsEmptyProperty =
    "zeroslackSemanticDecorationsEmpty";
constexpr const char* kDiagnosticAnnotationSource =
    "semantic-diagnostics";
constexpr const char* kGhostAnnotationSource =
    "semantic-ghosts";
constexpr int kDiagnosticOverviewBucketCount = 1024;

int diagnosticSeverityRank(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return 2;
    case SemanticDiagnostic::Warning:
        return 1;
    case SemanticDiagnostic::Info:
    default:
        return 0;
    }
}

QString diagnosticSeverityLabel(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

EditorAnnotationPlacement annotationPlacement(
    GhostAnnotationPlacement placement)
{
    switch (placement) {
    case GhostAnnotationPlacement::LeftOfAnchor:
        return EditorAnnotationPlacement::InlineBefore;
    case GhostAnnotationPlacement::RightOfAnchor:
        return EditorAnnotationPlacement::InlineAfter;
    case GhostAnnotationPlacement::RightOfLine:
    default:
        return EditorAnnotationPlacement::EndOfLine;
    }
}

qreal annotationSpaceAdvance(const MyCodeEditor* editor)
{
    if (!editor)
        return 1.0;
    const QFontMetricsF metrics(editor->font());
    return qMax<qreal>(
        1.0,
        metrics.horizontalAdvance(QLatin1Char(' ')));
}

QTextLine annotationBlockTextLine(
    const QTextBlock& block)
{
    QTextLayout* layout =
        block.isValid() ? block.layout() : nullptr;
    if (!layout || layout->lineCount() <= 0)
        return {};
    return layout->lineAt(0);
}

int annotationVisualColumnForOffset(
    const MyCodeEditor* editor,
    const QTextBlock& block,
    int offset)
{
    const QTextLine line =
        annotationBlockTextLine(block);
    if (!line.isValid())
        return qMax(0, offset);
    int bounded =
        qBound(0, offset, block.text().size());
    int zero = 0;
    const qreal x =
        line.cursorToX(&bounded, QTextLine::Leading)
        - line.cursorToX(&zero, QTextLine::Leading);
    return qMax(
        0,
        qRound(x / annotationSpaceAdvance(editor)));
}

int annotationXForVisualColumn(
    MyCodeEditor* editor,
    const QTextBlock& block,
    int visualColumn)
{
    if (!editor || !block.isValid())
        return 0;
    QTextCursor cursor(block);
    cursor.setPosition(block.position());
    return qRound(
        editor->cursorRect(cursor).left()
        + qMax(0, visualColumn)
              * annotationSpaceAdvance(editor));
}

void paintTemplateSlotAnnotation(
    MyCodeEditor* editor,
    QPainter& painter,
    QPaintEvent* event,
    const ResolvedEditorAnnotation& resolved)
{
    if (!editor
        || !editor->document()
        || !event) {
        return;
    }
    const EditorAnnotation& annotation =
        resolved.annotation;
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    const int startPosition =
        qBound(0,
               annotation.range.startPosition,
               documentEnd);
    const int endPosition =
        qBound(startPosition,
               annotation.range.endPosition,
               documentEnd);
    const QTextBlock block =
        editor->document()->findBlock(startPosition);
    if (!block.isValid()
        || !editor->sourceLineVisible(block.blockNumber()))
        return;

    QTextCursor startCursor(editor->document());
    startCursor.setPosition(startPosition);
    QTextCursor endCursor(editor->document());
    endCursor.setPosition(endPosition);
    const QRect startRect =
        editor->cursorRect(startCursor);
    const QRect endRect =
        editor->cursorRect(endCursor);
    const int left =
        qMin(startRect.left(), endRect.left());
    int right =
        qMax(startRect.left(), endRect.left());
    if (endPosition <= startPosition || right <= left) {
        right = left
            + qMax(1,
                   qRound(
                       annotationSpaceAdvance(editor)));
    }
    const int laneOffset =
        qMin(qMax(0, resolved.lane) * 2,
             qMax(0, startRect.height() - 3));
    const QRect highlight(
        left,
        startRect.top() + laneOffset,
        qMax(1, right - left),
        qMax(2, startRect.height() - laneOffset));
    if (!event->rect().intersects(highlight))
        return;

    const int weakAlpha =
        annotation.phaseVisible ? 58 : 26;
    const int activeAlpha =
        annotation.phaseVisible ? 118 : 82;
    QColor background = annotation.active
        ? InsightVisualStyle::theme().semantic.read
        : InsightVisualStyle::theme().accent;
    background.setAlpha(
        annotation.active ? activeAlpha : weakAlpha);
    painter.fillRect(highlight, background);

    QPen underline(
        annotation.active
            ? InsightVisualStyle::theme().semantic.read
            : InsightVisualStyle::theme().accent);
    underline.setStyle(
        annotation.active
            ? Qt::DashLine : Qt::SolidLine);
    underline.setWidth(annotation.active ? 2 : 1);
    painter.setPen(underline);
    painter.drawLine(
        highlight.left(),
        highlight.bottom(),
        highlight.right(),
        highlight.bottom());
}

void paintColumnCaretAnnotation(
    MyCodeEditor* editor,
    QPainter& painter,
    QPaintEvent* event,
    const ResolvedEditorAnnotation& resolved)
{
    if (!editor
        || !editor->document()
        || !event) {
        return;
    }
    const EditorAnnotation& annotation =
        resolved.annotation;
    if (annotation.visualColumn < 0)
        return;

    const QTextBlock block =
        editor->document()->findBlockByNumber(
            annotation.range.firstLine);
    if (!block.isValid()
        || !editor->sourceLineVisible(block.blockNumber()))
        return;

    QTextCursor endCursor(block);
    endCursor.setPosition(
        block.position() + block.text().size());
    const QRect endRect =
        editor->cursorRect(endCursor);
    const int lineEndColumn =
        annotationVisualColumnForOffset(
            editor,
            block,
            block.text().size());
    const int selectionLeft =
        qMax(0,
             annotation.visualRangeStartColumn);
    const int selectionRight =
        qMax(selectionLeft,
             annotation.visualRangeEndColumn);
    const int laneOffset = 0;
    const int targetX =
        annotationXForVisualColumn(
            editor,
            block,
            annotation.visualColumn)
        + laneOffset;
    const int selectionLeftX =
        annotationXForVisualColumn(
            editor,
            block,
            selectionLeft)
        + laneOffset;
    const int selectionRightX =
        annotationXForVisualColumn(
            editor,
            block,
            selectionRight)
        + laneOffset;
    const int affectedLeft =
        std::min({endRect.left(),
                  targetX,
                  selectionLeftX});
    const int affectedRight =
        std::max({endRect.left(),
                  targetX,
                  selectionRightX});
    const QRect affected(
        affectedLeft - 3,
        endRect.top(),
        affectedRight - affectedLeft + 7,
        endRect.height());
    if (!event->rect().intersects(affected))
        return;

    const QColor accent =
        editor->palette().color(QPalette::Highlight);
    if (selectionRight > selectionLeft) {
        const int actualRight =
            qMin(selectionRight, lineEndColumn);
        if (actualRight > selectionLeft) {
            QColor selected = accent;
            selected.setAlpha(
                annotation.active ? 86 : 68);
            const int actualRightX =
                annotationXForVisualColumn(
                    editor,
                    block,
                    actualRight)
                + laneOffset;
            painter.fillRect(
                QRect(selectionLeftX,
                      endRect.top() + 1,
                      qMax(1,
                           actualRightX
                               - selectionLeftX),
                      qMax(1,
                           endRect.height() - 2)),
                selected);
        }
    }

    const int virtualEndX =
        annotationXForVisualColumn(
            editor,
            block,
            selectionRight)
        + laneOffset;
    if (selectionRight > lineEndColumn) {
        QColor fill = accent;
        fill.setAlpha(
            annotation.active ? 40 : 24);
        painter.fillRect(
            QRect(endRect.left(),
                  endRect.top() + 2,
                  qMax(1,
                       virtualEndX
                           - endRect.left()),
                  qMax(1,
                       endRect.height() - 4)),
            fill);
        QColor guide = accent;
        guide.setAlpha(
            annotation.active ? 95 : 54);
        QPen guidePen(guide);
        guidePen.setStyle(Qt::DotLine);
        guidePen.setWidth(1);
        painter.setPen(guidePen);
        painter.drawLine(
            endRect.left(),
            endRect.bottom() - 2,
            virtualEndX,
            endRect.bottom() - 2);
    }

    QColor caret(220, 38, 38);
    caret.setAlpha(annotation.active ? 255 : 205);
    QPen caretPen(caret);
    caretPen.setWidth(annotation.active ? 3 : 2);
    painter.setPen(caretPen);
    painter.drawLine(
        targetX,
        endRect.top(),
        targetX,
        endRect.bottom() + 1);
}
}

void MyCodeEditorState::setDiagnosticHighlights(
    MyCodeEditor* editor,
    const QList<SemanticDiagnostic>& incomingDiagnostics)
{
    if (!editor)
        return;

    std::uint64_t incomingComputationRevision = 0;
    for (const SemanticDiagnostic& diagnostic : incomingDiagnostics) {
        incomingComputationRevision =
            std::max(incomingComputationRevision,
                     diagnostic.computationRevision);
    }
    if (incomingComputationRevision != 0
        && incomingComputationRevision
            < diagnosticComputationRevision) {
        return;
    }
    if (incomingComputationRevision != 0) {
        diagnosticComputationRevision =
            incomingComputationRevision;
    }

    QList<SemanticDiagnostic> currentDiagnostics;
    currentDiagnostics.reserve(incomingDiagnostics.size());
    for (const SemanticDiagnostic& diagnostic : incomingDiagnostics) {
        if (diagnostic.documentRevision != 0
            && diagnostic.documentRevision
                != semanticDocumentRevision()) {
            continue;
        }
        currentDiagnostics.append(diagnostic);
    }

    if (currentDiagnostics.isEmpty()
        && diagnostics.isEmpty()
        && editor->property(kDiagnosticsEmptyProperty).toBool()) {
        return;
    }

    diagnostics = std::move(currentDiagnostics);
    diagnosticIndexesByLine.clear();
    diagnosticSeverityByLine.clear();
    QList<EditorAnnotation> layerDiagnostics;
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    for (int index = 0; index < diagnostics.size(); ++index) {
        const SemanticDiagnostic& diagnostic = diagnostics.at(index);
        QSet<int> lines;
        for (const SemanticSourceRange& range : diagnostic.ranges) {
            if (range.position < 0 || range.length <= 0)
                continue;
            const int start = qBound(0, range.position, documentEnd);
            const int finalCharacter = qBound(
                start,
                range.position + range.length - 1,
                documentEnd);
            const QTextBlock startBlock =
                editor->document()->findBlock(start);
            const QTextBlock endBlock =
                editor->document()->findBlock(finalCharacter);
            if (!startBlock.isValid() || !endBlock.isValid())
                continue;
            for (int line = startBlock.blockNumber();
                 line <= endBlock.blockNumber();
                 ++line) {
                lines.insert(line);
            }
        }
        if (lines.isEmpty() && diagnostic.line > 0)
            lines.insert(diagnostic.line - 1);

        for (const int line : std::as_const(lines)) {
            diagnosticIndexesByLine[line].append(index);
            const auto existing =
                diagnosticSeverityByLine.constFind(line);
            if (existing == diagnosticSeverityByLine.constEnd()
                || diagnosticSeverityRank(diagnostic.severity)
                    > diagnosticSeverityRank(existing.value())) {
                diagnosticSeverityByLine.insert(
                    line, diagnostic.severity);
            }

            const QTextBlock block =
                editor->document()->findBlockByNumber(line);
            if (!block.isValid())
                continue;
            EditorAnnotation annotation;
            annotation.kind = EditorAnnotationKind::Diagnostic;
            annotation.range.startPosition = block.position();
            annotation.range.endPosition = qMin(
                documentEnd,
                block.position() + qMax(1, block.length() - 1));
            if (annotation.range.endPosition
                < annotation.range.startPosition) {
                annotation.range.endPosition =
                    annotation.range.startPosition;
            }
            annotation.range.firstLine = line;
            annotation.range.lastLine = line;
            annotation.text = diagnostic.message;
            annotation.detail = QString::number(
                static_cast<int>(diagnostic.severity));
            annotation.semanticKey =
                diagnostic.codeName
                + QLatin1Char('\x1f')
                + diagnostic.message
                + QLatin1Char('\x1f')
                + QString::number(line);
            annotation.priority =
                AnnotationLayer::defaultPriority(annotation.kind)
                + diagnosticSeverityRank(diagnostic.severity);
            annotation.sourceGeneration =
                diagnostic.computationRevision;
            annotation.placement =
                EditorAnnotationPlacement::Gutter;
            layerDiagnostics.append(annotation);
            annotation.placement =
                EditorAnnotationPlacement::Overview;
            layerDiagnostics.append(annotation);
        }
    }
    if (layerDiagnostics.isEmpty()) {
        annotationLayer.removeSource(
            QString::fromLatin1(kDiagnosticAnnotationSource));
    } else {
        annotationLayer.setSourceAnnotations(
            QString::fromLatin1(kDiagnosticAnnotationSource),
            layerDiagnostics);
    }

    rebuildDiagnosticOverviewIndex(editor);
    refreshDiagnosticPresentation(editor);
    editor->setProperty(kDiagnosticsEmptyProperty, diagnostics.isEmpty());
    gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
    editor->viewport()->update();
}

void MyCodeEditorState::clearDiagnosticHighlights(MyCodeEditor* editor)
{
    if (!editor)
        return;
    diagnostics.clear();
    diagnosticIndexesByLine.clear();
    diagnosticSeverityByLine.clear();
    diagnosticOverviewSeverityByBucket.clear();
    annotationLayer.removeSource(
        QString::fromLatin1(kDiagnosticAnnotationSource));
    selections.highlightDiagnostics(editor, {});
    editor->setProperty(kDiagnosticsEmptyProperty, true);
    gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
    editor->viewport()->update();
}

void MyCodeEditorState::rebuildDiagnosticOverviewIndex(
    const MyCodeEditor* editor)
{
    diagnosticOverviewSeverityByBucket.clear();
    if (!editor || editor->blockCount() <= 0)
        return;

    const int denominator = qMax(1, editor->blockCount() - 1);
    for (auto it = diagnosticSeverityByLine.cbegin();
         it != diagnosticSeverityByLine.cend();
         ++it) {
        const int bucket = qBound(
            0,
            qRound(static_cast<qreal>(it.key())
                   / denominator
                   * (kDiagnosticOverviewBucketCount - 1)),
            kDiagnosticOverviewBucketCount - 1);
        const auto existing =
            diagnosticOverviewSeverityByBucket.constFind(bucket);
        if (existing
                == diagnosticOverviewSeverityByBucket.constEnd()
            || diagnosticSeverityRank(it.value())
                   > diagnosticSeverityRank(existing.value())) {
            diagnosticOverviewSeverityByBucket.insert(
                bucket, it.value());
        }
    }
}

void MyCodeEditorState::refreshDiagnosticPresentation(
    MyCodeEditor* editor)
{
    if (!editor)
        return;

    const EditorVisibleDocumentRange range =
        visibleDocumentRange(editor);
    if (!range.valid() || diagnostics.isEmpty()) {
        selections.highlightDiagnostics(editor, {});
        return;
    }

    QSet<int> uniqueIndexes;
    for (int line = range.firstLine;
         line <= range.lastLine;
         ++line) {
        ++hotPathMetrics.diagnosticVisibleLineProbes;
        const auto lineIt =
            diagnosticIndexesByLine.constFind(line);
        if (lineIt == diagnosticIndexesByLine.constEnd())
            continue;
        for (const int index : lineIt.value())
            uniqueIndexes.insert(index);
    }

    QList<int> orderedIndexes = uniqueIndexes.values();
    std::sort(orderedIndexes.begin(), orderedIndexes.end());
    QList<SemanticDiagnostic> visibleDiagnostics;
    visibleDiagnostics.reserve(orderedIndexes.size());
    for (const int index : std::as_const(orderedIndexes)) {
        ++hotPathMetrics.diagnosticCandidatesExamined;
        if (index >= 0 && index < diagnostics.size())
            visibleDiagnostics.append(diagnostics.at(index));
    }
    selections.highlightDiagnostics(
        editor,
        visibleDiagnostics,
        range.startPosition,
        range.endPosition);
}

QString MyCodeEditorState::diagnosticTooltipForLine(
    int zeroBasedLine) const
{
    const QList<int> indexes =
        diagnosticIndexesByLine.value(zeroBasedLine);
    QList<const SemanticDiagnostic*> ordered;
    ordered.reserve(indexes.size());
    for (const int index : indexes) {
        if (index >= 0 && index < diagnostics.size())
            ordered.append(&diagnostics.at(index));
    }
    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const SemanticDiagnostic* left,
           const SemanticDiagnostic* right) {
            return diagnosticSeverityRank(left->severity)
                > diagnosticSeverityRank(right->severity);
        });

    QStringList rows;
    rows.reserve(ordered.size());
    for (const SemanticDiagnostic* diagnostic : std::as_const(ordered)) {
        rows.append(
            QStringLiteral("%1: %2")
                .arg(diagnosticSeverityLabel(diagnostic->severity),
                     diagnostic->message));
    }
    return rows.join(QLatin1Char('\n'));
}

QList<int> MyCodeEditorState::diagnosticOverviewLinesForTest() const
{
    QList<int> lines = diagnosticSeverityByLine.keys();
    std::sort(lines.begin(), lines.end());
    return lines;
}

SemanticDiagnostic::Severity
MyCodeEditorState::diagnosticSeverityForLineForTest(
    int zeroBasedLine,
    bool* available) const
{
    const auto found =
        diagnosticSeverityByLine.constFind(zeroBasedLine);
    if (available)
        *available = found != diagnosticSeverityByLine.constEnd();
    return found == diagnosticSeverityByLine.constEnd()
        ? SemanticDiagnostic::Info
        : found.value();
}

void MyCodeEditorState::setSemanticDecorations(
    MyCodeEditor* editor,
    const QList<SemanticDecoration>& decorations)
{
    if (!editor)
        return;
    if (decorations.isEmpty()
        && semanticDecorations.isEmpty()
        && editor->property(kSemanticDecorationsEmptyProperty).toBool()) {
        return;
    }
    semanticDecorations = decorations;
    rebuildSemanticDecorationPositionIndex();
    refreshSemanticDecorationPresentation(editor);
}

void MyCodeEditorState::refreshGhostAnnotations(MyCodeEditor* editor)
{
    ++ghostQueryGeneration;
    if (ghostQueryCancellation)
        ghostQueryCancellation->store(true);
    if (!editor || identity.current().isEmpty()) {
        setGhostAnnotations(editor, {});
        return;
    }

    GhostAnnotationQuery query;
    query.fileName = identity.current();
    query.documentText = editor->cachedDocumentText();
    query.instanceContext = hierarchyInstance;
    query.documentRevision = semanticDocumentRevision();
    ++hotPathMetrics.fullGhostQueries;

    const std::uint64_t generation = ghostQueryGeneration;
    const std::shared_ptr<std::atomic_bool> cancellation =
        std::make_shared<std::atomic_bool>(false);
    ghostQueryCancellation = cancellation;
    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        SemanticIndex::getInstance()->snapshot();
    if (!snapshot) {
        ghostQueryCancellation.reset();
        setGhostAnnotations(editor, {});
        return;
    }
    const std::shared_ptr<const EffectiveValueService::DocumentSnapshot>
        valueSnapshot = EffectiveValueService::getInstance()
                            ->snapshotForDocument(query.fileName);

    auto* watcher = new QFutureWatcher<GhostAnnotationReport>(editor);
    ghostQueryWatchers.append(QPointer<QObject>(watcher));
    QObject::connect(
        watcher,
        &QFutureWatcher<GhostAnnotationReport>::finished,
        editor,
        [this, editor, watcher, generation, cancellation, query]() {
            const GhostAnnotationReport report = watcher->result();
            ghostQueryWatchers.removeIf(
                [watcher](const QPointer<QObject>& candidate) {
                    return candidate.isNull()
                        || candidate.data() == watcher;
                });
            watcher->deleteLater();
            if (cancellation->load()
                || generation != ghostQueryGeneration
                || identity.current() != query.fileName
                || semanticDocumentRevision() != query.documentRevision) {
                return;
            }
            setGhostAnnotations(editor, report.annotations);
        });
    watcher->setFuture(QtConcurrent::run(
        [query, snapshot, valueSnapshot, cancellation]() {
            GhostAnnotationReport report;
            if (cancellation->load())
                return report;
            SemanticIndex localIndex;
            localIndex.setSnapshot(snapshot);
            EffectiveValueService localValues(&localIndex, valueSnapshot);
            GhostAnnotationService service(&localIndex, &localValues);
            report = service.annotationsForDocument(query);
            if (cancellation->load())
                report.annotations.clear();
            return report;
        }));
}

void MyCodeEditorState::shutdownGhostQueries(MyCodeEditor* editor)
{
    ++ghostQueryGeneration;
    if (ghostQueryCancellation)
        ghostQueryCancellation->store(true);
    ghostQueryCancellation.reset();

    for (const QPointer<QObject>& watcher : ghostQueryWatchers) {
        if (!watcher)
            continue;
        if (editor)
            QObject::disconnect(watcher, nullptr, editor, nullptr);
        watcher->deleteLater();
    }
    ghostQueryWatchers.clear();
}

void MyCodeEditorState::setGhostAnnotations(
    MyCodeEditor* editor,
    const QList<GhostAnnotation>& annotations)
{
    ghostAnnotations = annotations;
    refreshGhostAnnotationLayer(editor);
    ghostPresentationPending = false;
    if (editor) {
        gutter.updateViewportMargins(editor);
        handleResize(editor);
        gutter.handleUpdateRequest(editor,
                                   editor->viewport()->rect(),
                                   0);
        editor->viewport()->update();
    }
}

void MyCodeEditorState::setAnnotationDisplayOptions(
    MyCodeEditor* editor,
    const EditorAnnotationDisplayOptions& options)
{
    const EditorAnnotationDisplayOptions normalized =
        options.normalized();
    if (annotationDisplayOptions == normalized)
        return;
    annotationDisplayOptions = normalized;
    if (!editor)
        return;
    if (!annotationDisplayOptions.enabled)
        closeSemanticPopup(editor);
    gutter.handleUpdateRequest(
        editor, editor->viewport()->rect(), 0);
    editor->viewport()->update();
}

EditorAnnotationDisplayOptions
MyCodeEditorState::currentAnnotationDisplayOptions() const
{
    return annotationDisplayOptions;
}

void MyCodeEditorState::refreshGhostAnnotationLayer(
    MyCodeEditor* editor)
{
    QList<EditorAnnotation> layerAnnotations;
    layerAnnotations.reserve(ghostAnnotations.size());
    QTextDocument* document = editor ? editor->document() : nullptr;
    const int documentEnd = document
        ? qMax(0, document->characterCount() - 1)
        : std::numeric_limits<int>::max();
    for (const GhostAnnotation& ghost : std::as_const(ghostAnnotations)) {
        if (!ghost.isValid())
            continue;
        const int anchorPosition =
            qBound(0, ghost.anchorPosition, documentEnd);
        int line = ghost.line > 0 ? ghost.line - 1 : -1;
        if (line < 0 && document) {
            const QTextBlock block =
                document->findBlock(anchorPosition);
            if (block.isValid())
                line = block.blockNumber();
        }
        if (line < 0)
            continue;

        EditorAnnotation annotation;
        annotation.kind =
            ghost.kind == GhostAnnotationKind::FormalPort
            ? EditorAnnotationKind::PortDefinition
            : EditorAnnotationKind::EffectiveValue;
        annotation.placement =
            annotationPlacement(ghost.placement);
        annotation.range.startPosition = anchorPosition;
        annotation.range.endPosition = qMin(
            documentEnd,
            anchorPosition + qMax(0, ghost.anchorLength));
        annotation.range.firstLine = line;
        annotation.range.lastLine = line;
        annotation.text = ghost.text;
        annotation.semanticKey =
            QString::number(static_cast<int>(annotation.kind))
            + QLatin1Char('\x1f')
            + QString::number(anchorPosition)
            + QLatin1Char('\x1f')
            + QString::number(ghost.anchorLength)
            + QLatin1Char('\x1f')
            + ghost.text;
        annotation.priority =
            AnnotationLayer::defaultPriority(annotation.kind);
        annotation.sourceGeneration = ghostQueryGeneration;
        layerAnnotations.append(annotation);
    }
    if (layerAnnotations.isEmpty()) {
        annotationLayer.removeSource(
            QString::fromLatin1(kGhostAnnotationSource));
    } else {
        annotationLayer.setSourceAnnotations(
            QString::fromLatin1(kGhostAnnotationSource),
            layerAnnotations);
    }
}

void MyCodeEditorState::paintGhostAnnotations(
    MyCodeEditor* editor,
    QPaintEvent* event)
{
    if (!editor
        || !event
        || !annotationDisplayOptions.enabled) {
        return;
    }
    if (annotationLayer.isEmpty()
        && !templateSlots.active()
        && !columnMode.selectionActive()
        && !columnMode.virtualCursorActive()) {
        return;
    }

    QTextDocument* textDocument = editor->document();
    if (!textDocument)
        return;

    QPainter painter(editor->viewport());
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont ghostFont = editor->font();
    ghostFont.setItalic(true);
    painter.setFont(ghostFont);

    QColor color =
        editor->palette().color(QPalette::Text);
    color.setAlpha(72);
    painter.setPen(color);

    const QFontMetrics metrics(painter.font());
    const int documentEnd =
        qMax(0, textDocument->characterCount() - 1);
    const int viewportWidth =
        editor->viewport()->width();
    const int viewportHeight =
        editor->viewport()->height();
    QHash<int, qreal> rightLineEndX;
    constexpr qreal kLineTailSpacing = 8.0;

    AnnotationLayerQuery query;
    query.firstVisibleLine = qMax(
        0,
        editor->cursorForPosition(
            QPoint(0, 0)).blockNumber());
    query.lastVisibleLine = qMax(
        query.firstVisibleLine,
        editor->cursorForPosition(
            QPoint(
                0,
                qMax(0, viewportHeight - 1)))
            .blockNumber());
    query.maxAnnotationsPerLine =
        annotationDisplayOptions.maxAnnotationsPerLine;
    query.maxLanes =
        annotationDisplayOptions.maxLanes;
    templateSlots.publishVisibleAnnotations(
        editor,
        query.firstVisibleLine,
        query.lastVisibleLine);
    columnMode.publishVisibleAnnotations(
        editor,
        query.firstVisibleLine,
        query.lastVisibleLine);

    const AnnotationLayerReport report =
        annotationLayer.resolve(query);
    for (const ResolvedEditorAnnotation& resolved :
         report.annotations) {
        const EditorAnnotation& annotation =
            resolved.annotation;
        if (annotation.kind
            == EditorAnnotationKind::TemplateSlot) {
            paintTemplateSlotAnnotation(
                editor,
                painter,
                event,
                resolved);
            continue;
        }
        if (annotation.kind
            == EditorAnnotationKind::ColumnCaret) {
            paintColumnCaretAnnotation(
                editor,
                painter,
                event,
                resolved);
            continue;
        }
        if (annotation.kind
                != EditorAnnotationKind::PortDefinition
            && annotation.kind
                   != EditorAnnotationKind::EffectiveValue
            && annotation.kind
                   != EditorAnnotationKind::KeywordGhost) {
            continue;
        }

        const int anchorPosition =
            qBound(
                0,
                annotation.range.startPosition,
                documentEnd);
        const int anchorLength = qMax(
            0,
            annotation.range.endPosition
                - annotation.range.startPosition);
        const TSDocument* syntaxDocument =
            syntax.tsDocument();
        if (syntaxDocument
            && (syntaxDocument->isCommentAt(
                    anchorPosition)
                || (anchorLength > 0
                    && syntaxDocument->isCommentAt(
                        qMin(
                            documentEnd,
                            anchorPosition
                                + anchorLength - 1))))) {
            continue;
        }
        QTextBlock block =
            textDocument->findBlock(anchorPosition);
        if (!block.isValid()
            || !editor->sourceLineVisible(block.blockNumber()))
            continue;

        QTextCursor cursor(textDocument);
        cursor.setPosition(anchorPosition);
        const QRect anchorRect =
            editor->cursorRect(cursor);
        const int textWidth =
            metrics.horizontalAdvance(annotation.text);
        const qreal laneOffset =
            qMax(0, resolved.lane)
            * (textWidth + kLineTailSpacing);
        qreal x = -1;
        qreal baseline = 0;
        qreal visualTop = anchorRect.top();
        qreal visualBottom = anchorRect.bottom();
        const int line = annotation.range.firstLine;
        if (annotation.kind
            == EditorAnnotationKind::KeywordGhost) {
            x = anchorRect.left() + laneOffset;
            baseline = anchorRect.top()
                + (anchorRect.height()
                   + metrics.ascent()
                   - metrics.descent())
                      / 2.0;
        } else if (annotation.placement
                   == EditorAnnotationPlacement::InlineBefore) {
            x = anchorRect.left()
                - textWidth
                - kLineTailSpacing
                - laneOffset;
            if (x < 2)
                continue;
            baseline = anchorRect.top()
                + (anchorRect.height()
                   + metrics.ascent()
                   - metrics.descent())
                      / 2.0;
        } else {
            const EditorCodeLineTailGeometry tail =
                geometry.codeLineTailGeometry(
                    editor,
                    block.blockNumber());
            if (!tail.valid)
                continue;
            x = tail.textRight + kLineTailSpacing;
            const qreal previousEnd =
                rightLineEndX.value(line, x);
            if (x < previousEnd + kLineTailSpacing)
                x = previousEnd + kLineTailSpacing;
            baseline = tail.baseline;
            visualTop = tail.top;
            visualBottom =
                tail.top + tail.height;
        }

        if (visualBottom < 0
            || visualTop > viewportHeight) {
            continue;
        }
        if (x >= viewportWidth - 4
            || x + textWidth <= 2) {
            continue;
        }
        if (annotation.kind
                != EditorAnnotationKind::PortDefinition
            && (x < 2
                || x + textWidth
                       > viewportWidth - 4)) {
            continue;
        }
        if (annotation.kind
                != EditorAnnotationKind::KeywordGhost
            && annotation.placement
                   != EditorAnnotationPlacement::InlineBefore) {
            rightLineEndX.insert(
                line,
                x + textWidth);
        }

        QColor annotationColor = color;
        if (annotation.kind
            == EditorAnnotationKind::PortDefinition) {
            annotationColor.setAlpha(96);
        } else if (annotation.kind
                   == EditorAnnotationKind::KeywordGhost) {
            annotationColor.setAlpha(104);
        }
        painter.setPen(annotationColor);
        painter.drawText(
            QPointF(x, baseline),
            annotation.text);
    }
}
