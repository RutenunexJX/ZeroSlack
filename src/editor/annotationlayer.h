#ifndef ANNOTATIONLAYER_H
#define ANNOTATIONLAYER_H

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <limits>

enum class EditorAnnotationKind {
    Diagnostic,
    PinloomLink,
    PortDefinition,
    EffectiveValue,
    TemplateSlot,
    KeywordGhost,
    ColumnCaret,
    InsightTarget
};

enum class EditorAnnotationPlacement {
    Gutter,
    InlineBefore,
    InlineAfter,
    EndOfLine,
    Overlay,
    Overview
};

struct EditorAnnotationRange {
    int startPosition = -1;
    int endPosition = -1;
    int firstLine = -1;
    int lastLine = -1;

    bool isValid() const;
    bool overlaps(const EditorAnnotationRange& other) const;
};

struct EditorAnnotation {
    EditorAnnotationKind kind = EditorAnnotationKind::Diagnostic;
    EditorAnnotationPlacement placement =
        EditorAnnotationPlacement::InlineAfter;
    EditorAnnotationRange range;
    QString text;
    QString detail;
    QString semanticKey;
    int priority = 0;
    quint64 sourceGeneration = 0;
    int visualColumn = -1;
    int visualRangeStartColumn = -1;
    int visualRangeEndColumn = -1;
    bool active = false;
    bool phaseVisible = true;

    bool isValid() const;
};

struct ResolvedEditorAnnotation {
    EditorAnnotation annotation;
    int lane = 0;
};

struct AnnotationLayerQuery {
    int firstVisibleLine = 0;
    int lastVisibleLine = std::numeric_limits<int>::max();
    int maxAnnotationsPerLine = 6;
    int maxLanes = 3;
};

struct EditorAnnotationDisplayOptions {
    bool enabled = true;
    int maxAnnotationsPerLine = 6;
    int maxLanes = 3;

    EditorAnnotationDisplayOptions normalized() const
    {
        EditorAnnotationDisplayOptions result = *this;
        result.maxAnnotationsPerLine =
            qBound(1, result.maxAnnotationsPerLine, 64);
        result.maxLanes =
            qBound(1, result.maxLanes, 16);
        return result;
    }

    bool operator==(
        const EditorAnnotationDisplayOptions& other) const
    {
        return enabled == other.enabled
            && maxAnnotationsPerLine
                == other.maxAnnotationsPerLine
            && maxLanes == other.maxLanes;
    }

    bool operator!=(
        const EditorAnnotationDisplayOptions& other) const
    {
        return !(*this == other);
    }
};

struct AnnotationLayerReport {
    QList<ResolvedEditorAnnotation> annotations;
    int inputCount = 0;
    int examinedCount = 0;
    int candidateCount = 0;
    int duplicateCount = 0;
    int offscreenCount = 0;
    int densitySuppressedCount = 0;
    int conflictSuppressedCount = 0;
};

class AnnotationLayer
{
public:
    void setSourceAnnotations(
        const QString& sourceId,
        const QList<EditorAnnotation>& annotations);
    void removeSource(const QString& sourceId);
    void clear();

    bool isEmpty() const { return annotationsBySource.isEmpty(); }
    QStringList sourceIds() const;
    AnnotationLayerReport resolve(
        const AnnotationLayerQuery& query = {}) const;

    static int defaultPriority(EditorAnnotationKind kind);

private:
    struct SourceIndex {
        QList<EditorAnnotation> annotations;
        QList<int> maximumLastLineTree;
        int treeLeafCount = 0;
        int inputCount = 0;
    };

    QHash<QString, SourceIndex> annotationsBySource;
};

#endif // ANNOTATIONLAYER_H
