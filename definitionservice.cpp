#include "definitionservice.h"

#include "completioncontexthelper.h"
#include "symboltaxonomy.h"

#include <limits>

std::unique_ptr<DefinitionService> DefinitionService::instance = nullptr;

namespace {
SemanticDefinitionQuery toSemanticDefinitionQuery(const DefinitionQuery& query)
{
    SemanticDefinitionQuery semanticQuery;
    semanticQuery.symbolName = query.symbolName;
    semanticQuery.fileName = query.fileName;
    semanticQuery.moduleName = query.moduleName;
    semanticQuery.structTypeNameForMember = query.structTypeNameForMember;
    semanticQuery.cursorLine = query.cursorLine;
    return semanticQuery;
}

DefinitionResult toDefinitionResult(const SemanticDefinitionResult& semanticResult)
{
    DefinitionResult result;
    result.found = semanticResult.found;
    result.localFile = semanticResult.localFile;
    result.symbolRecord = semanticResult.symbolRecord;
    result.symbolStableKey = result.symbolRecord.stableKey.isValid()
        ? result.symbolRecord.stableKey
        : semanticResult.symbolStableKey;
    result.inspectedCandidateCount = semanticResult.inspectedCandidateCount;
    result.matchingNameCandidateCount = semanticResult.matchingNameCandidateCount;
    result.typeCompatibleCandidateCount = semanticResult.typeCompatibleCandidateCount;
    result.visibleCandidateCount = semanticResult.visibleCandidateCount;
    result.missReason = semanticResult.missReason;
    return result;
}

QString interfaceMemberScopeFromRecord(const SemanticSymbolRecord& record)
{
    if (!record.owner.interfaceLike)
        return QString();

    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;

    if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface)
        return record.name;

    return QString();
}

}

DefinitionService* DefinitionService::getInstance()
{
    if (!instance)
        instance = std::make_unique<DefinitionService>();
    return instance.get();
}

DefinitionService::DefinitionService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

DefinitionService::~DefinitionService() = default;

void DefinitionService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

DefinitionResult DefinitionService::resolveDefinition(const DefinitionQuery& query) const
{
    DefinitionResult empty;
    if (query.symbolName.isEmpty()) {
        empty.missReason = SemanticDefinitionMissReason::EmptySymbolName;
        return empty;
    }

    // A declaration is always resolvable at its own source token. Package
    // visibility and import rules still apply to every use outside that
    // declaration token.
    const DefinitionResult exactDeclaration =
        resolveExactDeclarationDefinition(query);
    if (exactDeclaration.found)
        return exactDeclaration;

    const DefinitionResult instancePinResult =
        resolveInstancePinDefinition(query);
    if (instancePinResult.found)
        return instancePinResult;

    const DefinitionQuery resolvedQuery = withResolvedMemberContext(query);
    return toDefinitionResult(
        semanticIndex()->resolveDefinition(toSemanticDefinitionQuery(resolvedQuery)));
}

bool DefinitionService::canResolveDefinition(const DefinitionQuery& query) const
{
    return resolveDefinition(query).found;
}

SemanticIndex* DefinitionService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

DefinitionResult DefinitionService::resolveExactDeclarationDefinition(
    const DefinitionQuery& query) const
{
    DefinitionResult result;
    if (query.fileName.isEmpty()
        || query.symbolName.isEmpty()
        || query.cursorLine <= 0
        || query.cursorColumn < 0) {
        return result;
    }

    const int oneBasedColumn = query.cursorColumn + 1;
    int bestPriority = std::numeric_limits<int>::max();
    for (const SemanticSymbolRecord& record :
         semanticIndex()->getSymbolRecords(query.fileName)) {
        ++result.inspectedCandidateCount;
        if (record.name != query.symbolName)
            continue;
        ++result.matchingNameCandidateCount;
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForSymbolRecord(record);
        if (!SymbolTaxonomy::isDefinitionCandidate(metadata))
            continue;
        ++result.typeCompatibleCandidateCount;
        if (record.location.startLine != query.cursorLine
            || record.location.startColumn <= 0) {
            continue;
        }

        const int declarationNameEnd =
            record.location.startColumn + qMax(1, record.name.size()) - 1;
        if (oneBasedColumn < record.location.startColumn
            || oneBasedColumn > declarationNameEnd) {
            continue;
        }
        ++result.visibleCandidateCount;

        const int priority = SymbolTaxonomy::definitionPriority(metadata);
        if (!result.found || priority < bestPriority) {
            result.found = true;
            result.localFile = true;
            result.symbolRecord = record;
            result.symbolStableKey = record.stableKey;
            result.missReason = SemanticDefinitionMissReason::None;
            bestPriority = priority;
        }
    }
    return result;
}

DefinitionResult DefinitionService::resolveInstancePinDefinition(
    const DefinitionQuery& query) const
{
    DefinitionResult empty;
    if (query.cursorLine <= 0 || query.cursorColumn < 0)
        return empty;

    const int oneBasedColumn = query.cursorColumn + 1;
    const QList<SemanticSymbolRecord> candidates =
        semanticIndex()->getSymbolRecords(query.fileName);
    for (const SemanticSymbolRecord& record : candidates) {
        if (record.name != query.symbolName
            || record.collectorKind != SymbolTaxonomy::CollectorKind::InstPin
            || record.location.startLine != query.cursorLine) {
            continue;
        }
        if (!query.moduleName.isEmpty()
            && !record.owner.name.isEmpty()
            && record.owner.name != query.moduleName) {
            continue;
        }

        const int endColumn = record.location.endColumn > record.location.startColumn
            ? record.location.endColumn
            : record.location.startColumn + qMax(1, record.name.size());
        if (oneBasedColumn < record.location.startColumn - 1
            || oneBasedColumn > endColumn) {
            continue;
        }

        const QString instantiatedModule = !record.type.resolvedTypeName.isEmpty()
            ? record.type.resolvedTypeName
            : record.type.rawTypeText;
        if (instantiatedModule.isEmpty())
            continue;

        SemanticDefinitionQuery portQuery;
        portQuery.symbolName = query.symbolName;
        portQuery.fileName = record.location.fileName;
        portQuery.moduleName = instantiatedModule;
        return toDefinitionResult(semanticIndex()->resolveDefinition(portQuery));
    }

    return empty;
}

DefinitionQuery DefinitionService::withResolvedMemberContext(const DefinitionQuery& query) const
{
    if (!query.structTypeNameForMember.isEmpty()
        || query.linePrefixBeforeCursor.isEmpty()) {
        return query;
    }

    DefinitionQuery resolved = query;
    QString variableName;
    QString memberPrefix;
    if (!CompletionContextHelper::tryParseStructMember(
            query.linePrefixBeforeCursor.trimmed(),
            variableName,
            memberPrefix)) {
        return resolved;
    }

    if (variableName.isEmpty())
        return resolved;

    resolved.structTypeNameForMember =
        semanticIndex()->getStructTypeForVariable(variableName, query.moduleName);
    if (!resolved.structTypeNameForMember.isEmpty())
        return resolved;

    const QList<SemanticSymbolRecord> candidates =
        semanticIndex()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : candidates) {
        if (record.name != variableName || !record.owner.interfaceLike)
            continue;

        if (!query.moduleName.isEmpty()
            && !record.owner.name.isEmpty()
            && record.owner.name != query.moduleName) {
            continue;
        }

        resolved.structTypeNameForMember = interfaceMemberScopeFromRecord(record);
        if (!resolved.structTypeNameForMember.isEmpty())
            return resolved;
    }
    return resolved;
}
