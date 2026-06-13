#include "completioncontextquery.h"

#include "completioncontexthelper.h"
#include "completionmatcher.h"
#include "completionsymbolquery.h"
#include "semanticindex.h"

#include <algorithm>

bool CompletionContextQuery::relationshipCompletionsAvailable(
    SemanticIndex* semanticIndex)
{
    return semanticIndex && semanticIndex->hasRelationshipFacts();
}

QVector<QPair<QString, int>> CompletionContextQuery::smartCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& fileName,
    int cursorPosition,
    bool relationshipCompletionsEnabled)
{
    if (!semanticIndex)
        return {};

    if (!relationshipCompletionsEnabled
        || !relationshipCompletionsAvailable(semanticIndex)) {
        return CompletionSymbolQuery::scoredNames(
            semanticIndex->getCompletionSymbolNames(), prefix, 20);
    }

    const QString currentModule =
        semanticIndex->currentModuleAt(fileName, cursorPosition);
    const QString context = QStringLiteral("general");
    const QStringList completions = contextAwareCompletions(
        semanticIndex,
        prefix,
        currentModule,
        context,
        true);

    QVector<QPair<QString, int>> result;
    result.reserve(completions.size());
    for (const QString& completion : completions) {
        const int baseScore =
            CompletionMatcher::calculateContextMatchScore(completion, prefix);
        const int contextScore =
            CompletionContextHelper::contextScore(completion, context);
        const int relationshipScore =
            CompletionContextHelper::relationshipScore(
                semanticIndex, completion, currentModule);
        const int scopeScore =
            CompletionContextHelper::scopeScore(
                semanticIndex, completion, currentModule);

        const int finalScore = baseScore * 0.4
            + contextScore * 0.2
            + relationshipScore * 0.3
            + scopeScore * 0.1;
        result.append(qMakePair(completion, finalScore));
    }

    std::sort(result.begin(), result.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    if (result.size() > 20)
        result = result.mid(0, 20);

    return result;
}
