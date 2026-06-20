#include "semanticindexlookuphelpers.h"

#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>

namespace semantic_index_lookup {

QString normalizedLookupFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool symbolSearchTypeMatches(const SemanticSymbolRecord& record,
                             const QList<SymbolTaxonomy::DeclarationKind>& declarationKinds,
                             SymbolTaxonomy::SymbolSearchIntent intent)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::SemanticMetadata{
            record.declarationKind,
            record.usageRole,
            record.owner.kind,
            record.visibility,
            record.sourceRole,
            record.rawCollectorKind,
            record.owner.interfaceLike};
    if (!declarationKinds.isEmpty()) {
        return declarationKinds.contains(record.declarationKind);
    }
    return SymbolTaxonomy::matchesSearchIntent(metadata, intent);
}

bool semanticDefinitionRecordMatches(const SemanticSymbolRecord& record,
                                     const QString& searchWord)
{
    if (record.name != searchWord)
        return false;

    return SymbolTaxonomy::isDefinitionCandidate(
        SymbolTaxonomy::SemanticMetadata{
            record.declarationKind,
            record.usageRole,
            record.owner.kind,
            record.visibility,
            record.sourceRole,
            record.rawCollectorKind,
            record.owner.interfaceLike});
}

int semanticDefinitionTypePriority(const SemanticSymbolRecord& record)
{
    return SymbolTaxonomy::definitionPriority(
        SymbolTaxonomy::SemanticMetadata{
            record.declarationKind,
            record.usageRole,
            record.owner.kind,
            record.visibility,
            record.sourceRole,
            record.rawCollectorKind,
            record.owner.interfaceLike});
}

bool semanticDefinitionSkipForStructMemberType(
    const SemanticSymbolRecord& record,
    const SemanticDefinitionQuery& query)
{
    if (query.structTypeNameForMember.isEmpty())
        return false;
    return (record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Interface
            || record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Struct)
        && record.owner.name != query.structTypeNameForMember;
}

int symbolSearchMatchScore(const QString& symbolName,
                           const SemanticSymbolSearchQuery& query)
{
    if (symbolName.isEmpty())
        return 0;
    if (query.text.isEmpty())
        return 1;

    const Qt::CaseSensitivity sensitivity =
        query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (QString::compare(symbolName, query.text, sensitivity) == 0)
        return 100;
    if (query.exactMatch)
        return 0;
    if (symbolName.startsWith(query.text, sensitivity))
        return 75;
    if (symbolName.contains(query.text, sensitivity))
        return 50;
    return 0;
}

} // namespace semantic_index_lookup
