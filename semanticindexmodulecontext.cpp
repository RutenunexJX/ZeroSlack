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
    QList<SemanticSymbolRecord> result;
    if (moduleName.isEmpty() || fileName.isEmpty())
        return result;

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
        return result;

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

    QSet<QString> seenStableKeys;
    SemanticQueryContext queryContext;
    queryContext.fileName = fileName;
    queryContext.moduleName = moduleName;
    queryContext.prefix = prefix;
    queryContext.cursorLine = moduleEndLineExclusive == std::numeric_limits<int>::max()
        ? -1
        : moduleEndLineExclusive - 1;
    auto appendRecord = [&](const SemanticSymbolRecord& record) {
        if (record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible
            && !packageVisibleRecordImported(record, queryContext)) {
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

    QList<SemanticSymbolRecord> candidateRecords =
        completionCommandKindIsModuleRange(commandKind)
            ? fileRecords
            : getSymbolRecordsByOwner(moduleName);
    if (completionCommandKindIsModuleRange(commandKind)) {
        candidateRecords.append(getSymbolRecordsByOwner(moduleName));
    }
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

    for (const QString& packageName :
         activeImportedPackageNames(queryContext)) {
        for (const SemanticSymbolRecord& record :
             getSymbolRecordsByOwner(packageName)) {
            appendRecord(record);
        }
    }

    if (result.isEmpty()) {
        const SymbolStableKey moduleStableKey = moduleRecord.stableKey;
        const QList<SemanticRelationshipResult> relationships =
            moduleStableKey.isValid()
                ? getRelationshipResults(moduleStableKey, true)
                : QList<SemanticRelationshipResult>();
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
    return result;
}
