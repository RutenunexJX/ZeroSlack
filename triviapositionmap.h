#ifndef TRIVIAPOSITIONMAP_H
#define TRIVIAPOSITIONMAP_H

#include "semanticanalysisrequest.h"

#include <QString>
#include <QVector>

struct SemanticLeafSpan {
    QString type;
    QString text;
    int start = 0;
    int end = 0;
};

struct TriviaLogicalPosition {
    QString tokenIdentity;
    int tokenOrdinal = -1;
    int tokenOffset = 0;
    int absolutePosition = -1;
    bool endAffinity = false;
};

enum class TriviaPositionLeafPolicy {
    SemanticOnly,
    IncludeComments,
};

// Maps positions between texts whose selected Tree-sitter leaf stream is
// identical. SemanticOnly preserves the source-remap contract; formatter
// callers include immutable comment leaves so positions inside comments
// retain their token-local offsets.
class TriviaPositionMap
{
public:
    TriviaPositionMap(const QString& oldText,
                      const QString& newText,
                      TriviaPositionLeafPolicy leafPolicy =
                          TriviaPositionLeafPolicy::SemanticOnly);
    TriviaPositionMap(const QString& oldText,
                      const QString& newText,
                      const SourceTextDelta& fallbackDelta,
                      TriviaPositionLeafPolicy leafPolicy =
                          TriviaPositionLeafPolicy::SemanticOnly);

    int map(int position, bool endAffinity) const;
    TriviaLogicalPosition capture(
        int position,
        bool endAffinity) const;

    bool isCompatible() const;
    const QVector<SemanticLeafSpan>& oldSemanticLeaves() const;
    const QVector<SemanticLeafSpan>& newSemanticLeaves() const;

private:
    QVector<SemanticLeafSpan> oldLeaves;
    QVector<SemanticLeafSpan> newLeaves;
    int oldLength = 0;
    int newLength = 0;
    SourceTextDelta fallback;
    bool compatible = false;
};

#endif // TRIVIAPOSITIONMAP_H
