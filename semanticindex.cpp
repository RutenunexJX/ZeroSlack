#include "semanticindex.h"

#include <QDir>
#include <QFileInfo>

std::unique_ptr<SemanticIndex> SemanticIndex::instance = nullptr;

namespace {
QString normalizedStableKeyFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}
}

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

SymbolStableKey symbolStableKeyForSymbol(const sym_list::SymbolInfo& symbol)
{
    SymbolStableKey key;
    if (symbol.symbolId < 0 && symbol.symbolName.isEmpty())
        return key;

    key.fileName = normalizedStableKeyFileName(symbol.fileName);
    key.symbolName = symbol.symbolName;
    key.declarationKind = SymbolTaxonomy::semanticMetadata(symbol).declarationKind;
    key.ownerScope = symbol.moduleScope;
    return key;
}

SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol)
{
    return semanticSymbolRecordForSymbol(symbol, {});
}

SemanticSymbolRecord semanticSymbolRecordForSymbol(
    const sym_list::SymbolInfo& symbol,
    const QSet<QString>& packageScopes)
{
    SemanticSymbolRecord record;
    if (symbol.symbolId < 0 && symbol.symbolName.isEmpty())
        return record;

    const SymbolTaxonomy::SemanticMetadata metadata =
        SymbolTaxonomy::semanticMetadata(symbol, packageScopes);

    record.stableKey = symbolStableKeyForSymbol(symbol);
    record.localHandle = symbol.symbolId;
    record.name = symbol.symbolName;
    record.location.fileName = normalizedStableKeyFileName(symbol.fileName);
    record.location.startLine = symbol.startLine;
    record.location.startColumn = symbol.startColumn;
    record.location.endLine = symbol.endLine;
    record.location.endColumn = symbol.endColumn;
    record.location.position = symbol.position;
    record.location.length = symbol.length;
    record.declarationKind = metadata.declarationKind;
    record.usageRole = metadata.usageRole;
    record.visibility = metadata.visibility;
    record.sourceRole = metadata.sourceRole;
    record.rawCollectorKind = metadata.rawCollectorKind;
    record.owner.kind = metadata.ownerScope;
    record.owner.name = symbol.moduleScope;
    record.owner.interfaceLike = metadata.interfaceLikeOwner;
    record.type.rawTypeText = symbol.dataType;
    record.type.resolvedTypeName = SymbolTaxonomy::interfaceTypeName(symbol);
    record.type.modportName = SymbolTaxonomy::interfaceModportName(symbol);
    if (!record.type.resolvedTypeName.isEmpty())
        record.type.resolvedTypeKind =
            SymbolTaxonomy::DeclarationKind::Interface;
    return record;
}

QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols)
{
    return semanticSymbolRecordsForSymbols(symbols, {});
}

QList<SemanticSymbolRecord> semanticSymbolRecordsForSymbols(
    const QList<sym_list::SymbolInfo>& symbols,
    const QSet<QString>& packageScopes)
{
    QList<SemanticSymbolRecord> records;
    records.reserve(symbols.size());
    for (const sym_list::SymbolInfo& symbol : symbols) {
        const SemanticSymbolRecord record =
            semanticSymbolRecordForSymbol(symbol, packageScopes);
        if (record.isValid())
            records.append(record);
    }
    return records;
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
