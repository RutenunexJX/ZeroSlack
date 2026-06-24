#include "completionsemanticquery.h"

#include "completioncommandkindadapter.h"
#include "completionservice.h"
#include "semanticindexcompletionfilters.h"

#include <QSet>
#include <algorithm>

namespace {
using namespace semantic_index_completion;

QString stableDedupeKeyForCompletionRecord(const SemanticSymbolRecord& record)
{
    const QString stableKey = symbolStableKeyText(record.stableKey);
    if (!stableKey.isEmpty())
        return stableKey;
    return QStringLiteral("%1|%2|%3")
        .arg(record.owner.name,
             QString::number(static_cast<int>(record.declarationKind)),
             record.name);
}
}

QList<SemanticSymbolRecord> CompletionSemanticQuery::commandSymbolRecords(
    SemanticIndex* semanticIndex,
    const CommandCompletionQuery& query)
{
    if (!semanticIndex)
        return {};

    const bool directModuleContext =
        completionCommandKindRequiresModuleContext(query.commandKind);

    if (directModuleContext) {
        if (query.moduleName.isEmpty())
            return {};

        semanticIndex->refreshStructTypedefEnumForFile(
            query.fileName, query.documentText);

        return semanticIndex->getModuleContextSymbolRecordsByType(
            query.moduleName,
            query.fileName,
            query.commandKind,
            query.prefix);
    }

    return semanticIndex->getCommandCompletionSymbolRecords(
        query.moduleName,
        query.commandKind,
        query.prefix);
}

QList<SemanticSymbolRecord> CompletionSemanticQuery::typedSymbolRecords(
    SemanticIndex* semanticIndex,
    CompletionCommandKind commandKind,
    const QString& prefix)
{
    if (!semanticIndex)
        return {};

    QList<SemanticSymbolRecord> result;
    QSet<QString> seenStableKeys;
    for (const SemanticSymbolRecord& record : semanticIndex->getSymbolRecords()) {
        const QString dedupeKey = stableDedupeKeyForCompletionRecord(record);
        if (dedupeKey.isEmpty() || seenStableKeys.contains(dedupeKey))
            continue;
        if (!semanticCompletionNameMatches(record.name, prefix))
            continue;

        if (!completionCommandKindMatchesTypedRecord(record, commandKind)) {
            continue;
        }

        seenStableKeys.insert(dedupeKey);
        result.append(record);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolRecord& left,
                        const SemanticSymbolRecord& right) {
        const int leftBandPriority =
            semanticSymbolAnalysisBandSortPriority(left);
        const int rightBandPriority =
            semanticSymbolAnalysisBandSortPriority(right);
        if (leftBandPriority != rightBandPriority)
            return leftBandPriority < rightBandPriority;
        return QString::compare(left.name,
                                right.name,
                                Qt::CaseInsensitive) < 0;
    });
    return result;
}

QStringList CompletionSemanticQuery::enumValueCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& enumTypeName)
{
    return semanticIndex
        ? semanticIndex->getEnumValueCompletionNames(prefix, enumTypeName)
        : QStringList();
}

QString CompletionSemanticQuery::enumTypeForVariable(
    SemanticIndex* semanticIndex,
    const QString& variableName,
    const QString& moduleName)
{
    return semanticIndex
        ? semanticIndex->enumTypeForVariable(variableName, moduleName)
        : QString();
}

QStringList CompletionSemanticQuery::modulePortCompletions(
    SemanticIndex* semanticIndex,
    const QString& prefix,
    const QString& moduleTypeName)
{
    return semanticIndex
        ? semanticIndex->getModulePortCompletionNames(prefix, moduleTypeName)
        : QStringList();
}

QString CompletionSemanticQuery::currentModuleAt(
    SemanticIndex* semanticIndex,
    const QString& fileName,
    int cursorPosition)
{
    return semanticIndex
        ? semanticIndex->currentModuleAt(fileName, cursorPosition)
        : QString();
}

QString CompletionSemanticQuery::structTypeForVariable(
    SemanticIndex* semanticIndex,
    const QString& variableName,
    const QString& moduleName)
{
    return semanticIndex
        ? semanticIndex->getStructTypeForVariable(variableName, moduleName)
        : QString();
}
