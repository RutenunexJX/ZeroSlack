#include "completioncontextquery.h"

#include "completioncontexthelper.h"
#include "completionmatcher.h"
#include "completionsymbolquery.h"
#include "semanticindex.h"
#include "symbolrelationshipengine.h"

#include <Qt>
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

QStringList CompletionContextQuery::contextAwareCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& currentModule,
    const QString& context,
    bool relationshipCompletionsEnabled)
{
    QStringList result;
    if (!semanticIndex)
        return result;

    const bool relationshipsEnabled =
        relationshipCompletionsEnabled
        && relationshipCompletionsAvailable(semanticIndex);

    if (context.contains(QLatin1Char('.'))
        || context.contains(QStringLiteral("->"))) {
        const QString structVariableName =
            CompletionContextHelper::extractStructVariable(context);
        if (!structVariableName.isEmpty()) {
            const QString structTypeName =
                semanticIndex->getStructTypeForVariable(
                    structVariableName, currentModule);
            if (!structTypeName.isEmpty()) {
                result.append(CompletionSymbolQuery::namesFromSymbols(
                    CompletionSymbolQuery::structMemberSymbols(
                        semanticIndex, structTypeName, prefix)));
                if (!result.isEmpty())
                    return result;
            }
        }
    }

    if (context.contains(QLatin1Char('='))
        || context.contains(QStringLiteral("assign"))
        || context.contains(QStringLiteral("case"))
        || context.contains(QStringLiteral("if"))) {
        const QString enumVariableName =
            CompletionContextHelper::extractEnumVariable(context);
        if (!enumVariableName.isEmpty()) {
            const QString enumTypeName =
                semanticIndex->enumTypeForVariable(
                    enumVariableName, currentModule);
            if (!enumTypeName.isEmpty()) {
                result.append(
                    semanticIndex->getEnumValueCompletionNames(prefix, enumTypeName));
            }
        }

        if (result.isEmpty())
            result.append(semanticIndex->getEnumValueCompletionNames(prefix));
    }

    if (relationshipsEnabled
        && context.contains(QLatin1Char('('))
        && (context.contains(QStringLiteral("module"))
            || context.contains(QStringLiteral("instantiation")))) {
        const QString moduleTypeName =
            CompletionContextHelper::extractModuleType(context);
        if (!moduleTypeName.isEmpty()) {
            result.append(
                semanticIndex->getModulePortCompletionNames(prefix, moduleTypeName));
        }
    }

    if (context.contains(QStringLiteral("clk"), Qt::CaseInsensitive)
        || context.contains(QStringLiteral("clock"), Qt::CaseInsensitive)
        || context.contains(QStringLiteral("always_ff"))) {
        result.append(clockDomainCompletions(semanticIndex, prefix));
    }

    if (context.contains(QStringLiteral("rst"), Qt::CaseInsensitive)
        || context.contains(QStringLiteral("reset"), Qt::CaseInsensitive)
        || context.contains(QStringLiteral("negedge"))
        || context.contains(QStringLiteral("posedge"))) {
        result.append(resetSignalCompletions(semanticIndex, prefix));
    }

    if (!currentModule.isEmpty()) {
        result.append(moduleChildCompletions(semanticIndex, currentModule, prefix));
        if (relationshipsEnabled) {
            result.append(
                relatedSymbolCompletions(semanticIndex, currentModule, prefix));
        }
    }

    if (context.contains(QStringLiteral("task"))
        || context.contains(QStringLiteral("function"))
        || context.contains(QStringLiteral("call"))) {
        result.append(
            CompletionSymbolQuery::taskFunctionCompletions(semanticIndex, prefix));
    }

    if (context.contains(QStringLiteral("typedef"))
        || context.contains(QStringLiteral("type"))) {
        result.append(
            CompletionSymbolQuery::globalSymbolsByType(
                semanticIndex, sym_list::sym_typedef, prefix));
        result.append(
            CompletionSymbolQuery::globalSymbolsByType(
                semanticIndex, sym_list::sym_enum, prefix));
        result.append(
            CompletionSymbolQuery::globalSymbolsByType(
                semanticIndex, sym_list::sym_packed_struct, prefix));
        result.append(
            CompletionSymbolQuery::globalSymbolsByType(
                semanticIndex, sym_list::sym_unpacked_struct, prefix));
    }

    if (context.contains(QStringLiteral("reg"))
        || context.contains(QStringLiteral("wire"))
        || context.contains(QStringLiteral("logic"))
        || context.contains(QStringLiteral("var"))) {
        result.append(CompletionMatcher::svKeywordCompletions(prefix));
        result.append(
            CompletionSymbolQuery::globalSymbolsByType(
                semanticIndex, sym_list::sym_enum, prefix));
        result.append(
            CompletionSymbolQuery::globalSymbolsByType(
                semanticIndex, sym_list::sym_packed_struct, prefix));
        result.append(
            CompletionSymbolQuery::globalSymbolsByType(
                semanticIndex, sym_list::sym_unpacked_struct, prefix));
    }

    if (result.isEmpty()
        || context == QLatin1String("general")
        || context.isEmpty()) {
        if (!currentModule.isEmpty()) {
            result.append(CompletionSymbolQuery::moduleSymbolsByType(
                semanticIndex, currentModule, sym_list::sym_reg, prefix));
            result.append(CompletionSymbolQuery::moduleSymbolsByType(
                semanticIndex, currentModule, sym_list::sym_wire, prefix));
            result.append(CompletionSymbolQuery::moduleSymbolsByType(
                semanticIndex, currentModule, sym_list::sym_logic, prefix));
        }

        result.append(CompletionSymbolQuery::globalSymbolsByType(
            semanticIndex, sym_list::sym_module, prefix));
        result.append(CompletionSymbolQuery::globalSymbolsByType(
            semanticIndex, sym_list::sym_enum, prefix));
        result.append(CompletionSymbolQuery::globalSymbolsByType(
            semanticIndex, sym_list::sym_packed_struct, prefix));
        result.append(CompletionSymbolQuery::globalSymbolsByType(
            semanticIndex, sym_list::sym_task, prefix));
        result.append(CompletionSymbolQuery::globalSymbolsByType(
            semanticIndex, sym_list::sym_function, prefix));
        result.append(CompletionMatcher::svKeywordCompletions(prefix));
    }

    result.removeDuplicates();

    QVector<QPair<QString, int>> scoredResults;
    scoredResults.reserve(result.size());
    for (const QString& completion : std::as_const(result)) {
        int score =
            CompletionMatcher::calculateContextMatchScore(completion, prefix);
        if (!context.isEmpty() && context != QLatin1String("general"))
            score += CompletionContextHelper::contextScore(completion, context);
        if (!currentModule.isEmpty()) {
            score += CompletionContextHelper::scopeScore(
                semanticIndex, completion, currentModule);
        }
        scoredResults.append(qMakePair(completion, score));
    }

    std::sort(scoredResults.begin(), scoredResults.end(),
              [](const QPair<QString, int>& left,
                 const QPair<QString, int>& right) {
                  if (left.second != right.second)
                      return left.second > right.second;
                  return left.first < right.first;
              });

    QStringList finalResults;
    finalResults.reserve(scoredResults.size());
    for (const auto& scored : std::as_const(scoredResults))
        finalResults.append(scored.first);

    if (finalResults.size() > 50)
        finalResults = finalResults.mid(0, 50);

    return finalResults;
}

QStringList CompletionContextQuery::moduleChildCompletions(
    SemanticIndex* semanticIndex,
    const QString& moduleName,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getRelationshipCompletionNames(
        moduleName,
        {SymbolRelationshipEngine::CONTAINS},
        true,
        prefix);
}

QStringList CompletionContextQuery::relatedSymbolCompletions(
    SemanticIndex* semanticIndex,
    const QString& symbolName,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getBidirectionalRelationshipCompletionNames(
        symbolName,
        {SymbolRelationshipEngine::REFERENCES},
        prefix);
}

QStringList CompletionContextQuery::symbolReferenceCompletions(
    SemanticIndex* semanticIndex,
    const QString& symbolName,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getRelationshipCompletionNames(
        symbolName,
        {SymbolRelationshipEngine::REFERENCES},
        false,
        prefix);
}

QStringList CompletionContextQuery::clockDomainCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getSymbolsWithOutgoingRelationshipCompletionNames(
        SymbolRelationshipEngine::CLOCKS,
        prefix);
}

QStringList CompletionContextQuery::resetSignalCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};
    return semanticIndex->getSymbolsWithOutgoingRelationshipCompletionNames(
        SymbolRelationshipEngine::RESETS,
        prefix);
}
