#include "semanticindex.h"

std::unique_ptr<SemanticIndex> SemanticIndex::instance = nullptr;

bool SymbolStableKey::isValid() const
{
    return !symbolName.isEmpty();
}

QString SymbolStableKey::toString() const
{
    return symbolStableKeyText(*this);
}

bool SymbolStableKey::operator==(const SymbolStableKey& other) const
{
    return fileName == other.fileName
        && symbolName == other.symbolName
        && declarationKind == other.declarationKind
        && ownerScope == other.ownerScope;
}

bool SemanticSymbolLocation::isValid() const
{
    return !fileName.isEmpty() && startLine > 0;
}

bool SemanticSymbolOwner::isValid() const
{
    return kind != SymbolTaxonomy::SymbolOwnerScope::Unknown
        || !name.isEmpty()
        || stableKey.isValid();
}

bool SemanticSymbolTypeReference::isValid() const
{
    return !rawTypeText.isEmpty()
        || !resolvedTypeName.isEmpty()
        || resolvedTypeKind != SymbolTaxonomy::DeclarationKind::Unknown
        || !modportName.isEmpty()
        || stableKey.isValid();
}

bool SemanticSymbolRecord::isValid() const
{
    return stableKey.isValid() || !name.isEmpty() || localHandle >= 0;
}

SymbolTaxonomy::SemanticMetadata semanticMetadataForSymbolRecord(
    const SemanticSymbolRecord& record)
{
    SymbolTaxonomy::SemanticMetadata metadata;
    metadata.declarationKind = record.declarationKind;
    metadata.usageRole = record.usageRole;
    metadata.ownerScope = record.owner.kind;
    metadata.visibility = record.visibility;
    metadata.sourceRole = record.sourceRole;
    metadata.rawCollectorKind = record.rawCollectorKind;
    metadata.interfaceLikeOwner = record.owner.interfaceLike;
    return metadata;
}

QString symbolStableKeyText(const SymbolStableKey& key)
{
    if (!key.isValid())
        return QString();

    return QStringLiteral("%1|%2|%3|%4")
        .arg(key.fileName,
             QString::number(static_cast<int>(key.declarationKind)),
             key.ownerScope,
             key.symbolName);
}

QString semanticRelationshipStableKeyText(
    const SemanticRelationship& relationship)
{
    const QString fromKey = symbolStableKeyText(relationship.fromStableKey);
    const QString toKey = symbolStableKeyText(relationship.toStableKey);
    if (fromKey.isEmpty() || toKey.isEmpty())
        return QString();

    return QStringLiteral("%1|%2|%3")
        .arg(QString::number(static_cast<int>(relationship.type)),
             fromKey,
             toKey);
}

SemanticIndex* SemanticIndex::getInstance()
{
    if (!instance)
        instance = std::make_unique<SemanticIndex>();
    return instance.get();
}

SemanticIndex::SemanticIndex(sym_list* symbolDatabase)
    : m_symbolDatabase(symbolDatabase ? symbolDatabase : sym_list::getInstance())
{
}

SemanticIndex::~SemanticIndex() = default;

void SemanticIndex::setSymbolDatabase(sym_list* symbolDatabase)
{
    m_symbolDatabase = symbolDatabase ? symbolDatabase : sym_list::getInstance();
}

sym_list* SemanticIndex::symbolDatabase() const
{
    return m_symbolDatabase ? m_symbolDatabase : sym_list::getInstance();
}
