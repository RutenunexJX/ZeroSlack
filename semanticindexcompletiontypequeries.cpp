#include "semanticindex.h"
#include "completioncommandkindadapter.h"
#include "semanticindexcompletionfilters.h"

#include <QSet>
#include <algorithm>

using namespace semantic_index_completion;

namespace {
bool commandCompletionScopeVisibleForRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind requestedKind,
    const QString& moduleName)
{
    const bool useGlobalScope = moduleName.isEmpty()
        || completionCommandKindIsAlwaysGlobalCommand(requestedKind);
    if (useGlobalScope)
        return record.owner.name.isEmpty();

    return record.owner.name == moduleName
        || (completionCommandKindIsPackageVisibleCommand(requestedKind)
            && record.visibility
                == SymbolTaxonomy::SymbolVisibility::PackageVisible);
}

}

QList<SemanticSymbolRecord> SemanticIndex::getCommandCompletionSymbolRecords(
    const QString& moduleName,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seenNames;
    if (moduleName.isEmpty()
        && !completionCommandKindIsGlobalCommand(commandKind))
        return result;

    const QList<SemanticSymbolRecord> records = getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (!commandCompletionScopeVisibleForRecord(
                record,
                commandKind,
                moduleName)
            || !completionCommandKindMatchesCommandRecord(record, commandKind)
            || !semanticCompletionNameMatches(record.name, prefix)) {
            continue;
        }

        const QString key = record.name.toCaseFolded();
        if (seenNames.contains(key))
            continue;
        seenNames.insert(key);
        result.append(record);
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
