#include "semanticindex.h"

#include "completioncommandkindadapter.h"
#include "semanticindexmodulecontexthelpers.h"
#include "symboltaxonomy.h"

#include <QSet>
#include <algorithm>
#include <limits>

using namespace semantic_index_module_context;

namespace {
QString stableDedupeKeyForModuleContextRecord(
    const SemanticSymbolRecord& record)
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

QList<SemanticSymbolRecord> SemanticIndex::getModuleContextSymbolRecordsByType(
    const QString& moduleName,
    const QString& fileName,
    CompletionCommandKind commandKind,
    const QString& prefix) const
{
    return getModuleContextSymbolRecordGroups(
        moduleName, fileName, {commandKind}, prefix).value(0);
}

QList<QList<SemanticSymbolRecord>> SemanticIndex::getModuleContextSymbolRecordGroups(
    const QString& moduleName,
    const QString& fileName,
    const QList<CompletionCommandKind>& commandKinds,
    const QString& prefix) const
{
    QList<QList<SemanticSymbolRecord>> results(commandKinds.size());
    if (moduleName.isEmpty() || fileName.isEmpty() || commandKinds.isEmpty())
        return results;

    const QString normalizedTargetFile = normalizedModuleContextFileName(fileName);
    const QList<SemanticSymbolRecord> fileRecords = getSymbolRecords(fileName);
    SemanticSymbolRecord moduleRecord;
    bool foundModule = false;
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
            && record.name == moduleName
            && normalizedModuleContextFileName(record.location.fileName)
                == normalizedTargetFile) {
            moduleRecord = record;
            foundModule = true;
            break;
        }
    }
    if (!foundModule)
        return results;

    int moduleEndLineExclusive = std::numeric_limits<int>::max();
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
            continue;
        if (record.localHandle == moduleRecord.localHandle) {
            continue;
        }
        if (record.location.startLine > moduleRecord.location.startLine
            && record.location.startLine < moduleEndLineExclusive) {
            moduleEndLineExclusive = record.location.startLine;
        }
    }

    auto inModuleRange = [&moduleRecord, moduleEndLineExclusive](
                             const SemanticSymbolRecord& record) {
        return record.location.fileName == moduleRecord.location.fileName
            && record.location.startLine > moduleRecord.location.startLine
            && record.location.startLine < moduleEndLineExclusive;
    };

    SemanticQueryContext queryContext;
    queryContext.fileName = fileName;
    queryContext.moduleName = moduleName;
    queryContext.prefix = prefix;
    queryContext.cursorLine = moduleEndLineExclusive == std::numeric_limits<int>::max()
        ? -1
        : moduleEndLineExclusive - 1;

    const QList<SemanticSymbolRecord> ownerRecords =
        getSymbolRecordsByOwner(moduleName);
    QList<SemanticSymbolRecord> rangeRecords;
    if (std::any_of(commandKinds.cbegin(), commandKinds.cend(),
                    completionCommandKindIsModuleRange)) {
        rangeRecords = fileRecords;
        rangeRecords.append(ownerRecords);
    }
    QList<SemanticSymbolRecord> importedRecords;
    for (const QString& packageName : activeImportedPackageNames(queryContext))
        importedRecords.append(getSymbolRecordsByOwner(packageName));
    QHash<QString, QHash<QString, bool>> importedVisibility;
    QList<SemanticRelationshipResult> relationships;
    bool relationshipsLoaded = false;

    for (qsizetype i = 0; i < commandKinds.size(); ++i) {
        const CompletionCommandKind commandKind = commandKinds.at(i);
        auto& result = results[i];
        QSet<QString> seenStableKeys;
        auto appendRecord = [&](const SemanticSymbolRecord& record) {
            if (record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible) {
                auto& byName = importedVisibility[record.owner.name];
                auto visible = byName.constFind(record.name);
                if (visible == byName.cend()) {
                    visible = byName.insert(record.name,
                                           packageVisibleRecordImported(record, queryContext));
                }
                if (!visible.value())
                    return;
            }
            if (!completionCommandKindMatchesModuleContextRecord(record, commandKind)) {
                return;
            }
            if (!moduleContextNameMatches(record.name, prefix))
                return;
            const QString dedupeKey =
                stableDedupeKeyForModuleContextRecord(record);
            if (dedupeKey.isEmpty() || seenStableKeys.contains(dedupeKey))
                return;
            seenStableKeys.insert(dedupeKey);
            result.append(record);
        };

        const QList<SemanticSymbolRecord>& candidateRecords =
            completionCommandKindIsModuleRange(commandKind)
                ? rangeRecords : ownerRecords;
        for (const SemanticSymbolRecord& record : candidateRecords) {
            bool isCorrectModule = false;
            if (completionCommandKindIsModuleRange(commandKind)) {
                isCorrectModule = inModuleRange(record)
                    || record.owner.name == moduleName;
            } else {
                isCorrectModule = record.owner.name == moduleName;
            }
            if (isCorrectModule)
                appendRecord(record);
        }

        for (const SemanticSymbolRecord& record : importedRecords)
            appendRecord(record);

        if (result.isEmpty()) {
            if (!relationshipsLoaded) {
                relationships = moduleRecord.stableKey.isValid()
                    ? getRelationshipResults(moduleRecord.stableKey, true)
                    : QList<SemanticRelationshipResult>();
                relationshipsLoaded = true;
            }
            for (const SemanticRelationshipResult& relationship : relationships) {
                if (relationship.relationship.type != SymbolRelationshipEngine::CONTAINS)
                    continue;
                const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
                const SymbolStableKey targetKey = targetRecord.stableKey.isValid()
                    ? targetRecord.stableKey
                    : relationship.toStableKey;
                const SemanticSymbolRecord resolvedTargetRecord = targetRecord.isValid()
                    ? targetRecord
                    : getSymbolRecordByStableKey(targetKey);
                if (resolvedTargetRecord.localHandle >= 0)
                    appendRecord(resolvedTargetRecord);
            }
        }

        sortModuleContextSymbolRecords(result);
    }
    return results;
}
