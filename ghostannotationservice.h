#ifndef GHOSTANNOTATIONSERVICE_H
#define GHOSTANNOTATIONSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QString>
#include <memory>

enum class GhostAnnotationPlacement {
    LeftOfAnchor,
    RightOfAnchor,
    RightOfLine
};

enum class GhostAnnotationKind {
    FormalPort,
    ParameterValue,
    ParameterOverride,
    SignalWidth,
    EnumValue,
    ArraySummary,
    PartSelect,
    GenerateLoop,
    ConcatenationWidth
};

struct GhostAnnotation {
    GhostAnnotationKind kind = GhostAnnotationKind::FormalPort;
    GhostAnnotationPlacement placement = GhostAnnotationPlacement::RightOfLine;
    QString text;
    int line = 0; // 1-based
    int anchorPosition = -1;
    int anchorLength = 0;

    bool isValid() const
    {
        return !text.isEmpty() && anchorPosition >= 0 && anchorLength >= 0;
    }
};

struct GhostAnnotationQuery {
    QString fileName;
    QString documentText;
};

struct GhostAnnotationReport {
    QList<GhostAnnotation> annotations;
};

struct GhostNumericLiteralQuery {
    QString documentText;
    int cursorPosition = -1;
};

struct GhostNumericLiteralReport {
    bool available = false;
    QString displayText;
    int startPosition = -1;
    int endPosition = -1;
};

class GhostAnnotationService
{
public:
    static GhostAnnotationService* getInstance();

    explicit GhostAnnotationService(SemanticIndex* semanticIndex = nullptr);
    ~GhostAnnotationService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    GhostAnnotationReport annotationsForDocument(
        const GhostAnnotationQuery& query) const;
    GhostNumericLiteralReport numericLiteralAt(
        const GhostNumericLiteralQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<GhostAnnotationService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // GHOSTANNOTATIONSERVICE_H
