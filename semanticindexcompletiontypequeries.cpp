#include "semanticindex.h"
#include "completioncommandkindadapter.h"
#include "semanticindexcompletionfilters.h"

#include <QHash>
#include <QSet>
#include <algorithm>

using namespace semantic_index_completion;

namespace {
bool commandCompletionScopeVisibleForRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind requestedKind,
    const SemanticQueryContext& context,
    const SemanticIndex* index)
{
    const bool useGlobalScope = context.moduleName.isEmpty()
        || completionCommandKindIsAlwaysGlobalCommand(requestedKind);
    if (useGlobalScope)
        return record.owner.name.isEmpty();

    if (record.owner.name == context.moduleName)
        return true;
    return completionCommandKindIsPackageVisibleCommand(requestedKind)
        && record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible
        && index
        && index->packageVisibleRecordImported(record, context);
}

}

QList<SemanticSymbolRecord> SemanticIndex::getCommandCompletionSymbolRecords(
    const QString& moduleName,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    SemanticQueryContext context;
    context.moduleName = moduleName;
    context.prefix = prefix;
    return getCommandCompletionSymbolRecords(context, commandKind, prefix);
}

QList<SemanticSymbolRecord> SemanticIndex::getCommandCompletionSymbolRecords(
    const SemanticQueryContext& context,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    if (context.moduleName.isEmpty()
        && !completionCommandKindIsGlobalCommand(commandKind))
        return result;

    const bool useGlobalScope = context.moduleName.isEmpty()
        || completionCommandKindIsAlwaysGlobalCommand(commandKind);
    const QList<SemanticSymbolRecord> records =
        useGlobalScope
            ? getSymbolRecordsByOwner(QString())
            : (completionCommandKindIsPackageVisibleCommand(commandKind)
                   ? getSymbolRecords()
                   : getSymbolRecordsByOwner(context.moduleName));
    QHash<QString, QList<SemanticSymbolRecord>> importedRecordsByName;
    for (const SemanticSymbolRecord& record : records) {
        if (!commandCompletionScopeVisibleForRecord(
                record,
                commandKind,
                context,
                this)
            || !completionCommandKindMatchesCommandRecord(record, commandKind)
            || !semanticCompletionNameMatches(record.name, prefix)) {
            continue;
        }

        const QString key = record.name.toCaseFolded();
        if (record.visibility
            == SymbolTaxonomy::SymbolVisibility::PackageVisible) {
            importedRecordsByName[key].append(record);
        } else {
            if (seenNames.contains(key))
                continue;
            seenNames.insert(key);
            result.append(record);
        }
    }

    for (auto it = importedRecordsByName.constBegin();
         it != importedRecordsByName.constEnd();
         ++it) {
        if (seenNames.contains(it.key()))
            continue;
        QSet<QString> owners;
        for (const SemanticSymbolRecord& record : it.value())
            owners.insert(record.owner.name);
        if (owners.size() != 1 || it.value().isEmpty())
            continue;
        seenNames.insert(it.key());
        result.append(it.value().first());
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const SemanticSymbolRecord& left,
           const SemanticSymbolRecord& right) {
            return QString::compare(
                       left.name,
                       right.name,
                       Qt::CaseInsensitive)
                < 0;
        });
    return result;
}
