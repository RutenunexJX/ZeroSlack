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
