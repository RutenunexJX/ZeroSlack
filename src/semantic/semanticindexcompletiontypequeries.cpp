#include "semanticindex.h"
#include "completioncommandkindadapter.h"
#include "semanticindexcompletionfilters.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <utility>

using namespace semantic_index_completion;

namespace {
QString commandCompletionOwnerName(const SemanticQueryContext& context)
{
    return !context.moduleName.isEmpty()
        ? context.moduleName
        : context.packageName;
}

bool commandCompletionScopeVisibleForRecord(
    const SemanticSymbolRecord& record,
    CompletionCommandKind requestedKind,
    const SemanticQueryContext& context)
{
    const QString ownerName = commandCompletionOwnerName(context);
    const bool useGlobalScope = ownerName.isEmpty()
        || completionCommandKindIsAlwaysGlobalCommand(requestedKind);
    if (useGlobalScope)
        return record.owner.name.isEmpty();

    if (record.owner.name == ownerName)
        return true;
    return completionCommandKindIsPackageVisibleCommand(requestedKind)
        && record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible;
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
    return getCommandCompletionSymbolRecordGroups(
        context, {commandKind}, prefix).value(0);
}

QList<QList<SemanticSymbolRecord>> SemanticIndex::getCommandCompletionSymbolRecordGroups(
    const SemanticQueryContext& context,
    const QList<CompletionCommandKind>& commandKinds,
    const QString& prefix) const
{
    QList<QList<SemanticSymbolRecord>> results(commandKinds.size());
    QHash<QString, QList<SemanticSymbolRecord>> recordsByOwner;
    QList<SemanticSymbolRecord> importedRecords;
    bool importsLoaded = false;
    const QString ownerName = commandCompletionOwnerName(context);
    const auto scope = resolveScopeRecord(context);
    const auto scopedRecords = scope.isValid() ? getDeclarationMemberRecords(scope)
                                               : QList<SemanticSymbolRecord>{};
    QSet<QString> scopedKeys;
    for (const auto& record : scopedRecords)
        scopedKeys.insert(symbolStableKeyText(record.stableKey));
    const bool hasScopeDeclarations = !getSymbolRecordsByName(ownerName).isEmpty();
    for (qsizetype i = 0; i < commandKinds.size(); ++i) {
        auto& result = results[i];
        const CompletionCommandKind commandKind = commandKinds.at(i);
        QSet<QString> seenNames;
        if (ownerName.isEmpty()
            && !completionCommandKindIsGlobalCommand(commandKind))
            continue;

        const bool useGlobalScope = ownerName.isEmpty()
            || completionCommandKindIsAlwaysGlobalCommand(commandKind);
        const QString recordOwner = useGlobalScope ? QString() : ownerName;
        auto prepared = recordsByOwner.constFind(recordOwner);
        if (prepared == recordsByOwner.cend())
            prepared = recordsByOwner.insert(recordOwner, getSymbolRecordsByOwner(recordOwner));
        QList<SemanticSymbolRecord> records = prepared.value();
        if (!useGlobalScope
            && completionCommandKindIsPackageVisibleCommand(commandKind)) {
            if (!importsLoaded) {
                importedRecords = getVisibleImportedPackageRecords(context);
                importsLoaded = true;
            }
            records.append(importedRecords);
        }
        QHash<QString, QList<SemanticSymbolRecord>> importedRecordsByName;
        for (const SemanticSymbolRecord& record : std::as_const(records)) {
            if (!commandCompletionScopeVisibleForRecord(
                    record,
                    commandKind,
                    context)
                || !completionCommandKindMatchesCommandRecord(record, commandKind)
                || !semanticCompletionNameMatches(record.name, prefix)) {
                continue;
            }

            if (!useGlobalScope && record.owner.name == ownerName && hasScopeDeclarations
                && !scopedKeys.contains(symbolStableKeyText(record.stableKey)))
                continue;
            const QString key = record.name;
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
    }
    return results;
}
