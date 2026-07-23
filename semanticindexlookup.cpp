#include "semanticindex.h"

#include "semanticindexlookuphelpers.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <QSet>
#include <algorithm>
#include <utility>

using namespace semantic_index_lookup;

namespace {

SemanticDefinitionResult combinedDefinitionMissEvidence(
    const SemanticDefinitionResult& local,
    const SemanticDefinitionResult& global)
{
    SemanticDefinitionResult combined;
    combined.inspectedCandidateCount =
        local.inspectedCandidateCount + global.inspectedCandidateCount;
    combined.matchingNameCandidateCount =
        local.matchingNameCandidateCount + global.matchingNameCandidateCount;
    combined.typeCompatibleCandidateCount =
        local.typeCompatibleCandidateCount + global.typeCompatibleCandidateCount;
    combined.visibleCandidateCount =
        local.visibleCandidateCount + global.visibleCandidateCount;

    if (combined.inspectedCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::NoCandidateSymbols;
    } else if (combined.matchingNameCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::NoMatchingName;
    } else if (combined.typeCompatibleCandidateCount == 0) {
        combined.missReason = SemanticDefinitionMissReason::StructMemberTypeMismatch;
    } else {
        combined.missReason = SemanticDefinitionMissReason::NotVisibleInContext;
    }
    return combined;
}

SymbolTaxonomy::SemanticMetadata metadataForRecord(
    const SemanticSymbolRecord& record)
{
    return semanticMetadataForSymbolRecord(record);
}

bool definitionRecordVisibleInContext(
    const SemanticSymbolRecord& record,
    const SemanticQueryContext& context,
    const SemanticIndex* index)
{
    const SymbolTaxonomy::SemanticMetadata metadata = metadataForRecord(record);
    if (record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible)
        return index && index->packageVisibleRecordImported(record, context);

    return SymbolTaxonomy::isDefinitionVisibleInContext(
        metadata,
        record.owner.name,
        context.moduleName);
}

int definitionRecordContextPriorityAdjustment(
    const SemanticSymbolRecord& record,
    const QString& moduleName)
{
    if (!moduleName.isEmpty() && record.owner.name == moduleName)
        return -100;
    if (record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible)
        return -20;
    return 0;
}

SemanticQueryContext semanticContextForDefinitionQuery(
    const SemanticDefinitionQuery& query)
{
    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    context.cursorLine = query.cursorLine;
    context.cursorPosition = query.cursorPosition;
    context.importedPackageNames = query.importedPackageNames;
    return context;
}

struct CompilationUnitQueryPosition {
    QString identity;
    std::uint64_t sourceOrder = 0;
    std::uint64_t sourceOffset = 0;

    bool isValid() const
    {
        return !identity.isEmpty() && sourceOrder != 0;
    }
};

CompilationUnitQueryPosition compilationUnitPositionForContext(
    const QList<SemanticSymbolRecord>& fileRecords,
    const SemanticQueryContext& context)
{
    CompilationUnitQueryPosition fallback;
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.compilationUnitFileName.isEmpty()
            || record.compilationUnitSourceOrder == 0) {
            continue;
        }
        CompilationUnitQueryPosition candidate{
            record.compilationUnitFileName,
            record.compilationUnitSourceOrder,
            record.compilationUnitSourceOffset,
        };
        if (!fallback.isValid()
            || candidate.sourceOrder < fallback.sourceOrder
            || (candidate.sourceOrder == fallback.sourceOrder
                && candidate.sourceOffset < fallback.sourceOffset)) {
            fallback = candidate;
        }
        if (!context.moduleName.isEmpty()
            && record.name == context.moduleName
            && SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))) {
            return candidate;
        }
    }
    return fallback;
}

bool packageImportFactVisibleForContext(
    const SemanticSymbolRecord& fact,
    const SemanticQueryContext& context,
    const CompilationUnitQueryPosition& queryPosition)
{
    if (fact.collectorKind
        != SymbolTaxonomy::CollectorKind::PackageImport) {
        return false;
    }
    if (!fact.owner.name.isEmpty()
        && fact.owner.name != context.moduleName) {
        return false;
    }
    const bool sameFile =
        normalizedLookupFileName(fact.location.fileName)
        == normalizedLookupFileName(context.fileName);
    if (fact.owner.name.isEmpty() && !sameFile) {
        const QString unitFile = fact.compilationUnitFileName.isEmpty()
            ? fact.location.fileName
            : fact.compilationUnitFileName;
        if (!queryPosition.isValid()) {
            if (normalizedLookupFileName(unitFile)
                != normalizedLookupFileName(context.fileName)) {
                return false;
            }
        } else if (normalizedLookupFileName(unitFile)
                       != normalizedLookupFileName(queryPosition.identity)
                   || fact.compilationUnitSourceOrder == 0
                   || fact.compilationUnitSourceOrder
                       > queryPosition.sourceOrder
                   || (fact.compilationUnitSourceOrder
                           == queryPosition.sourceOrder
                       && fact.compilationUnitSourceOffset
                           >= queryPosition.sourceOffset)) {
            return false;
        }
    }
    if (context.cursorLine > 0
        && sameFile
        && fact.location.startLine > context.cursorLine) {
        return false;
    }
    return !fact.name.isEmpty();
}

QList<SemanticSymbolRecord> activePackageImportFacts(
    const SemanticIndex* index,
    const SemanticQueryContext& context)
{
    if (!index || context.fileName.isEmpty())
        return {};

    const QList<SemanticSymbolRecord> fileRecords =
        index->getSymbolRecords(context.fileName);
    const CompilationUnitQueryPosition queryPosition =
        compilationUnitPositionForContext(fileRecords, context);
    QList<SemanticSymbolRecord> candidates = fileRecords;
    candidates.append(index->getSymbolRecordsByDeclarationKind(
        SymbolTaxonomy::DeclarationKind::Unknown));
    if (!context.moduleName.isEmpty()) {
        candidates.append(
            index->getSymbolRecordsByOwner(context.moduleName));
    }

    QList<SemanticSymbolRecord> result;
    QSet<QString> seen;
    for (const SemanticSymbolRecord& record : std::as_const(candidates)) {
        if (!packageImportFactVisibleForContext(record,
                                                context,
                                                queryPosition))
            continue;
        QString key = symbolStableKeyText(record.stableKey);
        if (key.isEmpty()) {
            key = QStringLiteral("%1|%2|%3|%4")
                      .arg(record.location.fileName,
                           QString::number(record.location.position),
                           record.name,
                           record.type.rawTypeText);
        }
        if (record.owner.name.isEmpty()
            && record.compilationUnitSourceOrder != 0) {
            key += QStringLiteral("|compilation-unit:%1:%2")
                       .arg(record.compilationUnitSourceOrder)
                       .arg(record.compilationUnitSourceOffset);
        }
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(record);
    }
    return result;
}

bool definitionRecordIsInQueryFile(
    const SemanticSymbolRecord& record,
    const QString& queryFileName)
{
    const QString recordFileName =
        normalizedLookupFileName(record.location.fileName);
    const QString queryFile =
        normalizedLookupFileName(queryFileName);
    return !recordFileName.isEmpty()
        && !queryFile.isEmpty()
        && recordFileName == queryFile;
}

}

QList<SemanticSymbolSearchResult> SemanticIndex::searchSymbols(
    const SemanticSymbolSearchQuery& query) const
{
    QList<SemanticSymbolSearchResult> result;
    const QList<SemanticSymbolRecord> records =
        getSymbolRecords(query.fileName);
    for (const SemanticSymbolRecord& record : records) {
        if (!symbolSearchTypeMatches(record,
                                     query.declarationKinds,
                                     query.intent))
            continue;

        const int score = symbolSearchMatchScore(record.name, query);
        if (score <= 0)
            continue;

        SemanticSymbolSearchResult item;
        item.symbolRecord = record;
        item.symbolStableKey = item.symbolRecord.stableKey;
        item.score = score;
        result.append(item);
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const SemanticSymbolSearchResult& a,
                        const SemanticSymbolSearchResult& b) {
        if (a.score != b.score)
            return a.score > b.score;
        const int aBandPriority =
            semanticSymbolAnalysisBandSortPriority(a.symbolRecord);
        const int bBandPriority =
            semanticSymbolAnalysisBandSortPriority(b.symbolRecord);
        if (aBandPriority != bBandPriority)
            return aBandPriority < bBandPriority;
        if (a.symbolRecord.location.fileName != b.symbolRecord.location.fileName)
            return a.symbolRecord.location.fileName
                < b.symbolRecord.location.fileName;
        if (a.symbolRecord.location.startLine != b.symbolRecord.location.startLine)
            return a.symbolRecord.location.startLine
                < b.symbolRecord.location.startLine;
        return a.symbolRecord.name < b.symbolRecord.name;
    });

    if (query.maxResults >= 0 && result.size() > query.maxResults)
        result = result.mid(0, query.maxResults);
    return result;
}

SemanticDefinitionResult SemanticIndex::resolveDefinition(
    const SemanticDefinitionQuery& query) const
{
    SemanticDefinitionResult empty;
    if (query.symbolName.isEmpty()) {
        empty.missReason = SemanticDefinitionMissReason::EmptySymbolName;
        return empty;
    }

    SemanticDefinitionResult local = bestDefinitionFromCandidates(
        getSymbolRecords(query.fileName),
        query,
        true);
    if (local.found) {
        local.localFile =
            definitionRecordIsInQueryFile(local.symbolRecord, query.fileName);
        return local;
    }
    if (local.missReason
        == SemanticDefinitionMissReason::AmbiguousImportedPackageSymbol) {
        return local;
    }

    const SemanticQueryContext context = semanticContextForDefinitionQuery(query);
    QList<SemanticSymbolRecord> globalCandidates =
        findDefinitionRecords(query.symbolName, context);
    const QString queryFile = normalizedLookupFileName(query.fileName);
    globalCandidates.erase(
        std::remove_if(globalCandidates.begin(), globalCandidates.end(),
                       [&queryFile](const SemanticSymbolRecord& record) {
                           return normalizedLookupFileName(
                                      record.location.fileName) == queryFile;
                       }),
        globalCandidates.end());
    SemanticDefinitionResult global =
        bestDefinitionFromCandidates(globalCandidates, query, false);
    if (global.found)
        return global;
    if (global.missReason
        == SemanticDefinitionMissReason::AmbiguousImportedPackageSymbol) {
        global.inspectedCandidateCount += local.inspectedCandidateCount;
        global.matchingNameCandidateCount += local.matchingNameCandidateCount;
        global.typeCompatibleCandidateCount += local.typeCompatibleCandidateCount;
        global.visibleCandidateCount += local.visibleCandidateCount;
        return global;
    }
    return combinedDefinitionMissEvidence(local, global);
}

QList<SemanticSymbolRecord> SemanticIndex::findDefinitionRecords(
    const QString& name,
    const SemanticQueryContext& context) const
{
    if (name.isEmpty())
        return {};

    // getSymbolRecordsByName merges the immutable workspace snapshot with
    // native per-document updates and removes snapshot records for files that
    // have native coverage. Going directly to m_snapshot here made global
    // definition lookup blind to the latest analyzed document records while
    // local lookup already observed them.
    QList<SemanticSymbolRecord> sorted;
    const QList<SemanticSymbolRecord> namedRecords =
        getSymbolRecordsByName(name);
    sorted.reserve(namedRecords.size());
    for (const SemanticSymbolRecord& record : namedRecords) {
        if (SymbolTaxonomy::isDefinitionCandidate(
                metadataForRecord(record))) {
            sorted.append(record);
        }
    }
    if (sorted.isEmpty())
        return {};
    const QString normalizedContextFile = normalizedLookupFileName(context.fileName);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&context, &normalizedContextFile](const SemanticSymbolRecord& a,
                                                        const SemanticSymbolRecord& b) {
        auto score = [&context, &normalizedContextFile](const SemanticSymbolRecord& s) {
            int value = 0;
            if (!normalizedContextFile.isEmpty()
                && normalizedLookupFileName(s.location.fileName) == normalizedContextFile)
                value += 100;
            if (!context.moduleName.isEmpty()
                && s.owner.name == context.moduleName)
                value += 50;
            if (SymbolTaxonomy::isGlobalDefinition(metadataForRecord(s))) {
                value += 10;
            }
            return value;
        };

        const int aScore = score(a);
        const int bScore = score(b);
        if (aScore != bScore)
            return aScore > bScore;
        const int aBandPriority =
            semanticSymbolAnalysisBandSortPriority(a);
        const int bBandPriority =
            semanticSymbolAnalysisBandSortPriority(b);
        if (aBandPriority != bBandPriority)
            return aBandPriority < bBandPriority;
        if (a.location.fileName != b.location.fileName)
            return a.location.fileName < b.location.fileName;
        if (a.location.startLine != b.location.startLine)
            return a.location.startLine < b.location.startLine;
        return a.localHandle < b.localHandle;
    });
    return sorted;
}

QSet<QString> SemanticIndex::activeImportedPackageNames(
    const SemanticQueryContext& context) const
{
    QSet<QString> result = context.importedPackageNames;
    for (const SemanticSymbolRecord& fact :
         activePackageImportFacts(this, context)) {
        result.insert(fact.name);
    }
    return result;
}

QList<SemanticSymbolRecord> SemanticIndex::getVisibleImportedPackageRecords(
    const SemanticQueryContext& context) const
{
    QHash<QString, QSet<QString>> visibleMembersByPackage;
    for (const QString& packageName : context.importedPackageNames) {
        if (!packageName.isEmpty())
            visibleMembersByPackage[packageName].insert(QStringLiteral("*"));
    }
    for (const SemanticSymbolRecord& fact :
         activePackageImportFacts(this, context)) {
        if (fact.name.isEmpty() || fact.type.rawTypeText.isEmpty())
            continue;
        visibleMembersByPackage[fact.name].insert(fact.type.rawTypeText);
    }

    QList<SemanticSymbolRecord> result;
    for (auto it = visibleMembersByPackage.constBegin();
         it != visibleMembersByPackage.constEnd();
         ++it) {
        const bool wildcard = it.value().contains(QStringLiteral("*"));
        for (const SemanticSymbolRecord& record :
             getSymbolRecordsByOwner(it.key())) {
            if (record.visibility
                    != SymbolTaxonomy::SymbolVisibility::PackageVisible
                || (!wildcard && !it.value().contains(record.name))) {
                continue;
            }
            result.append(record);
        }
    }
    return result;
}

bool SemanticIndex::packageVisibleRecordImported(
    const SemanticSymbolRecord& record,
    const SemanticQueryContext& context) const
{
    if (record.visibility != SymbolTaxonomy::SymbolVisibility::PackageVisible)
        return true;
    if (record.owner.name.isEmpty())
        return false;
    if (context.importedPackageNames.contains(record.owner.name))
        return true;
    for (const SemanticSymbolRecord& fact :
         activePackageImportFacts(this, context)) {
        if (fact.name != record.owner.name)
            continue;
        if (fact.type.rawTypeText == QStringLiteral("*")
            || fact.type.rawTypeText == record.name) {
            return true;
        }
    }
    return false;
}

SemanticDefinitionResult SemanticIndex::bestDefinitionFromCandidates(
    const QList<SemanticSymbolRecord>& candidates,
    const SemanticDefinitionQuery& query,
    bool localFile) const
{
    SemanticDefinitionResult best;
    best.localFile = localFile;
    int bestPriority = 999;
    QSet<QString> bestImportedPackageOwners;
    const SemanticQueryContext context = semanticContextForDefinitionQuery(query);

    for (const SemanticSymbolRecord& record : candidates) {
        ++best.inspectedCandidateCount;
        if (!semanticDefinitionRecordMatches(record, query.symbolName))
            continue;
        ++best.matchingNameCandidateCount;
        if (semanticDefinitionSkipForStructMemberType(record, query))
            continue;
        ++best.typeCompatibleCandidateCount;
        if (!definitionRecordVisibleInContext(record, context, this)) {
            continue;
        }
        ++best.visibleCandidateCount;

        int priority = semanticDefinitionTypePriority(record)
            + definitionRecordContextPriorityAdjustment(record, query.moduleName);
        const bool importedPackageMember =
            record.visibility == SymbolTaxonomy::SymbolVisibility::PackageVisible;

        if (!best.found || priority < bestPriority) {
            best.found = true;
            best.localFile = localFile;
            best.symbolRecord = record;
            best.symbolStableKey = best.symbolRecord.stableKey;
            best.missReason = SemanticDefinitionMissReason::None;
            bestPriority = priority;
            bestImportedPackageOwners.clear();
            if (importedPackageMember)
                bestImportedPackageOwners.insert(record.owner.name);
        } else if (priority == bestPriority
                   && best.symbolRecord.visibility
                       == SymbolTaxonomy::SymbolVisibility::PackageVisible
                   && importedPackageMember) {
            bestImportedPackageOwners.insert(record.owner.name);
        }
    }

    if (best.found && bestImportedPackageOwners.size() > 1) {
        best.found = false;
        best.symbolRecord = {};
        best.symbolStableKey = {};
        best.missReason =
            SemanticDefinitionMissReason::AmbiguousImportedPackageSymbol;
        return best;
    }

    if (!best.found) {
        if (best.inspectedCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::NoCandidateSymbols;
        } else if (best.matchingNameCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::NoMatchingName;
        } else if (best.typeCompatibleCandidateCount == 0) {
            best.missReason = SemanticDefinitionMissReason::StructMemberTypeMismatch;
        } else {
            best.missReason = SemanticDefinitionMissReason::NotVisibleInContext;
        }
    }
    return best;
}
