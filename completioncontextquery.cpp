#include "completioncontextquery.h"

#include "completioncontexthelper.h"
#include "completionmatcher.h"
#include "completionsemanticquery.h"
#include "completionsymbolquery.h"
#include "semanticindex.h"

#include <Qt>
#include <algorithm>

namespace {

QStringList commandKindCompletions(
    SemanticIndex* semanticIndex,
    const QString& moduleName,
    const QString& prefix,
    CompletionCommandKind kind)
{
    CommandCompletionQuery query;
    query.prefix = prefix;
    query.moduleName = moduleName;
    query.commandKind = kind;
    return CompletionSymbolQuery::namesFromRecords(
        CompletionSemanticQuery::commandSymbolRecords(semanticIndex, query));
}

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
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::Task));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::Function));
    }

    if (context.contains(QStringLiteral("typedef"))
        || context.contains(QStringLiteral("type"))) {
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::Typedef));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::EnumType));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix,
            CompletionCommandKind::PackedStructType));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix,
            CompletionCommandKind::UnpackedStructType));
    }

    if (context.contains(QStringLiteral("reg"))
        || context.contains(QStringLiteral("wire"))
        || context.contains(QStringLiteral("logic"))
        || context.contains(QStringLiteral("var"))) {
        result.append(CompletionMatcher::svKeywordCompletions(prefix));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::EnumType));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix,
            CompletionCommandKind::PackedStructType));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix,
            CompletionCommandKind::UnpackedStructType));
    }

    if (result.isEmpty()
        || context == QLatin1String("general")
        || context.isEmpty()) {
        if (!currentModule.isEmpty()) {
            result.append(commandKindCompletions(
                semanticIndex, currentModule, prefix, CompletionCommandKind::Reg));
            result.append(commandKindCompletions(
                semanticIndex, currentModule, prefix, CompletionCommandKind::Wire));
            result.append(commandKindCompletions(
                semanticIndex, currentModule, prefix, CompletionCommandKind::Logic));
        }

        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::Module));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::EnumType));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix,
            CompletionCommandKind::PackedStructType));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::Task));
        result.append(commandKindCompletions(
            semanticIndex, QString(), prefix, CompletionCommandKind::Function));
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
