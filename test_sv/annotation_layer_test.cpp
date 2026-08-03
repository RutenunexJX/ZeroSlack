#include "annotationlayer.h"

#include <QString>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

EditorAnnotation annotation(EditorAnnotationKind kind,
                            int start,
                            int end,
                            int line,
                            const QString& text,
                            const QString& semanticKey = {})
{
    EditorAnnotation result;
    result.kind = kind;
    result.placement = EditorAnnotationPlacement::InlineAfter;
    result.range = {start, end, line, line};
    result.text = text;
    result.semanticKey = semanticKey;
    return result;
}
}

int main()
{
    AnnotationLayer layer;

    EditorAnnotation oldValue = annotation(
        EditorAnnotationKind::EffectiveValue,
        40, 44, 4, QStringLiteral("4'hf"),
        QStringLiteral("instance-port:value"));
    oldValue.sourceGeneration = 3;
    EditorAnnotation repeatedGenerateValue = oldValue;
    repeatedGenerateValue.text = QStringLiteral("15");
    repeatedGenerateValue.sourceGeneration = 8;
    EditorAnnotation diagnostic = annotation(
        EditorAnnotationKind::Diagnostic,
        40, 44, 4, QStringLiteral("width mismatch"),
        QStringLiteral("diagnostic:width"));
    EditorAnnotation offscreen = annotation(
        EditorAnnotationKind::PortDefinition,
        900, 904, 90, QStringLiteral("input logic"),
        QStringLiteral("port:data"));

    layer.setSourceAnnotations(
        QStringLiteral("effective-values"),
        {oldValue, repeatedGenerateValue});
    layer.setSourceAnnotations(
        QStringLiteral("diagnostics"),
        {diagnostic, offscreen});

    AnnotationLayerQuery visible;
    visible.firstVisibleLine = 0;
    visible.lastVisibleLine = 20;
    visible.maxAnnotationsPerLine = 6;
    visible.maxLanes = 3;
    const AnnotationLayerReport report = layer.resolve(visible);
    expect("same generated-instance annotation is deduplicated",
           report.inputCount == 4
               && report.duplicateCount == 1
               && report.annotations.size() == 2);
    expect("offscreen annotations are excluded before layout",
           report.offscreenCount == 1);
    expect("diagnostics win priority and conflicting annotations use lanes",
           report.annotations.first().annotation.kind
                   == EditorAnnotationKind::Diagnostic
               && report.annotations.first().lane == 0
               && report.annotations.last().annotation.kind
                      == EditorAnnotationKind::EffectiveValue
               && report.annotations.last().lane == 1
               && report.annotations.last().annotation.text
                      == QStringLiteral("15")
               && report.annotations.last().annotation.sourceGeneration
                      == 8);

    AnnotationLayer dense;
    QList<EditorAnnotation> denseItems;
    for (int index = 0; index < 5; ++index) {
        EditorAnnotation item = annotation(
            index == 4 ? EditorAnnotationKind::Diagnostic
                       : EditorAnnotationKind::EffectiveValue,
            10 + index, 20 + index, 2,
            QStringLiteral("item-%1").arg(index),
            QStringLiteral("item-%1").arg(index));
        denseItems.append(item);
    }
    dense.setSourceAnnotations(QStringLiteral("dense"), denseItems);
    AnnotationLayerQuery constrained;
    constrained.firstVisibleLine = 2;
    constrained.lastVisibleLine = 2;
    constrained.maxAnnotationsPerLine = 2;
    constrained.maxLanes = 2;
    const AnnotationLayerReport denseReport =
        dense.resolve(constrained);
    expect("density budget keeps the two highest-priority candidates",
           denseReport.annotations.size() == 2
               && denseReport.annotations.first().annotation.kind
                      == EditorAnnotationKind::Diagnostic
               && denseReport.densitySuppressedCount == 3);

    AnnotationLayer conflicts;
    conflicts.setSourceAnnotations(
        QStringLiteral("conflicts"),
        {annotation(EditorAnnotationKind::Diagnostic,
                    0, 10, 0, QStringLiteral("a")),
         annotation(EditorAnnotationKind::TemplateSlot,
                    0, 10, 0, QString()),
         annotation(EditorAnnotationKind::KeywordGhost,
                    0, 10, 0, QStringLiteral("always_comb"))});
    AnnotationLayerQuery oneLane;
    oneLane.firstVisibleLine = 0;
    oneLane.lastVisibleLine = 0;
    oneLane.maxAnnotationsPerLine = 6;
    oneLane.maxLanes = 1;
    const AnnotationLayerReport conflictReport =
        conflicts.resolve(oneLane);
    expect("lane exhaustion suppresses lower-priority overlaps",
           conflictReport.annotations.size() == 1
               && conflictReport.annotations.first().annotation.kind
                       == EditorAnnotationKind::Diagnostic
                && conflictReport.conflictSuppressedCount == 2);

    AnnotationLayer unifiedOverlays;
    EditorAnnotation activeSlot = annotation(
        EditorAnnotationKind::TemplateSlot,
        50, 54, 5, QString(),
        QStringLiteral("slot:reset"));
    activeSlot.placement =
        EditorAnnotationPlacement::Overlay;
    activeSlot.priority =
        AnnotationLayer::defaultPriority(
            activeSlot.kind) + 10;
    activeSlot.sourceGeneration = 9;
    activeSlot.active = true;
    EditorAnnotation staleSlot = activeSlot;
    staleSlot.priority =
        AnnotationLayer::defaultPriority(
            staleSlot.kind);
    staleSlot.sourceGeneration = 4;
    EditorAnnotation columnCaret = annotation(
        EditorAnnotationKind::ColumnCaret,
        50, 54, 5, QString(),
        QStringLiteral("column:5"));
    columnCaret.placement =
        EditorAnnotationPlacement::Overlay;
    columnCaret.visualColumn = 8;
    EditorAnnotation overlayGhost = annotation(
        EditorAnnotationKind::KeywordGhost,
        50, 54, 5, QStringLiteral("comb"),
        QStringLiteral("ghost:comb"));
    overlayGhost.placement =
        EditorAnnotationPlacement::Overlay;
    unifiedOverlays.setSourceAnnotations(
        QStringLiteral("template-slots"),
        {activeSlot});
    unifiedOverlays.setSourceAnnotations(
        QStringLiteral("stale-template-cache"),
        {staleSlot});
    unifiedOverlays.setSourceAnnotations(
        QStringLiteral("column-carets"),
        {columnCaret});
    unifiedOverlays.setSourceAnnotations(
        QStringLiteral("keyword-ghosts"),
        {overlayGhost});
    AnnotationLayerQuery twoOverlayLanes;
    twoOverlayLanes.firstVisibleLine = 5;
    twoOverlayLanes.lastVisibleLine = 5;
    twoOverlayLanes.maxAnnotationsPerLine = 8;
    twoOverlayLanes.maxLanes = 2;
    const AnnotationLayerReport overlayReport =
        unifiedOverlays.resolve(twoOverlayLanes);
    expect("slot facts deduplicate across producers by generation and priority",
           overlayReport.duplicateCount == 1
               && overlayReport.annotations.size() == 2
               && overlayReport.annotations.first()
                          .annotation.kind
                      == EditorAnnotationKind::TemplateSlot
               && overlayReport.annotations.first()
                          .annotation.sourceGeneration
                      == 9);
    expect("slot priority owns lane zero and column caret avoids it",
           overlayReport.annotations.at(0).lane == 0
               && overlayReport.annotations.at(1)
                          .annotation.kind
                      == EditorAnnotationKind::ColumnCaret
               && overlayReport.annotations.at(1).lane == 1
               && overlayReport.conflictSuppressedCount == 1);

    AnnotationLayer separatePlacements;
    EditorAnnotation gutterDiagnostic = annotation(
        EditorAnnotationKind::Diagnostic,
        0, 4, 0, QStringLiteral("error"));
    gutterDiagnostic.placement =
        EditorAnnotationPlacement::Gutter;
    EditorAnnotation overlaySlot = annotation(
        EditorAnnotationKind::TemplateSlot,
        0, 4, 0, QString());
    overlaySlot.placement =
        EditorAnnotationPlacement::Overlay;
    separatePlacements.setSourceAnnotations(
        QStringLiteral("placements"),
        {gutterDiagnostic, overlaySlot});
    expect("independent placement planes do not create false conflicts",
           separatePlacements.resolve(oneLane)
                       .annotations.size()
                   == 2
               && separatePlacements.resolve(oneLane)
                          .conflictSuppressedCount
                      == 0);

    AnnotationLayer indexedVisible;
    QList<EditorAnnotation> indexedItems;
    indexedItems.reserve(10000);
    for (int line = 0; line < 10000; ++line) {
        indexedItems.append(
            annotation(
                EditorAnnotationKind::EffectiveValue,
                line * 8,
                line * 8 + 2,
                line,
                QStringLiteral("v%1").arg(line),
                QStringLiteral("value:%1").arg(line)));
    }
    indexedVisible.setSourceAnnotations(
        QStringLiteral("indexed-visible"),
        indexedItems);
    AnnotationLayerQuery narrowVisible;
    narrowVisible.firstVisibleLine = 4321;
    narrowVisible.lastVisibleLine = 4325;
    const AnnotationLayerReport narrowReport =
        indexedVisible.resolve(narrowVisible);
    expect("large sources examine only the requested visible window",
           narrowReport.inputCount == 10000
               && narrowReport.examinedCount == 5
               && narrowReport.candidateCount == 5
               && narrowReport.annotations.size() == 5
               && narrowReport.offscreenCount == 9995);

    layer.removeSource(QStringLiteral("diagnostics"));
    expect("annotation sources have deterministic lifecycle",
           layer.sourceIds()
                   == QStringList{QStringLiteral("effective-values")}
               && layer.resolve(visible).annotations.size() == 1);
    layer.clear();
    expect("annotation layer clears all sources atomically",
           layer.sourceIds().isEmpty()
               && layer.resolve(visible).annotations.isEmpty());

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
