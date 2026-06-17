#include "hierarchyservice.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

std::unique_ptr<HierarchyService> HierarchyService::instance = nullptr;

static bool hierarchyNodeLess(const HierarchyNode& lhs, const HierarchyNode& rhs)
{
    if (lhs.viaType != rhs.viaType)
        return static_cast<int>(lhs.viaType) < static_cast<int>(rhs.viaType);

    const int fileCompare = QString::compare(lhs.symbol.fileName,
                                             rhs.symbol.fileName,
                                             Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    if (lhs.symbol.startLine != rhs.symbol.startLine)
        return lhs.symbol.startLine < rhs.symbol.startLine;
    if (lhs.symbol.startColumn != rhs.symbol.startColumn)
        return lhs.symbol.startColumn < rhs.symbol.startColumn;
    return QString::compare(lhs.symbol.symbolName,
                            rhs.symbol.symbolName,
                            Qt::CaseInsensitive) < 0;
}

QString normalizedHierarchyFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString HierarchyService::directionDisplayName(HierarchyQuery::Direction direction)
{
    switch (direction) {
    case HierarchyQuery::Children:
        return QStringLiteral("Outgoing");
    case HierarchyQuery::Parents:
        return QStringLiteral("Incoming");
    case HierarchyQuery::Both:
        break;
    }
    return QStringLiteral("Related");
}

QString HierarchyService::relationshipTypeDisplayName(
    SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("Contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("References");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("Instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("Calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("Inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("Implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("Assigns To");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("Reads From");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("Clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("Resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("Generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("Constrains");
    }
    return QStringLiteral("Relationship");
}

void HierarchyService::fillDisplayMetadata(HierarchyNode& node)
{
    node.directionDisplayName = directionDisplayName(node.direction);
    node.relationshipTypeDisplayName = node.depth == 0
        ? QStringLiteral("Root")
        : relationshipTypeDisplayName(node.viaType);
    node.symbolDisplayName = symbolDisplayName(node.symbol);
    node.fileDisplayName = fileDisplayName(node.symbol.fileName);
    node.lineDisplayName = lineDisplayName(node.symbol.startLine);
}

QString HierarchyService::symbolDisplayName(const sym_list::SymbolInfo& symbol)
{
    return symbol.symbolName.isEmpty()
        ? QStringLiteral("<unnamed>")
        : symbol.symbolName;
}

QString HierarchyService::fileDisplayName(const QString& fileName)
{
    QString displayName = QFileInfo(fileName).fileName();
    if (displayName.isEmpty())
        displayName = fileName;
    return displayName;
}

QString HierarchyService::lineDisplayName(int line)
{
    return QString::number(line);
}

HierarchyService* HierarchyService::getInstance()
{
    if (!instance)
        instance = std::make_unique<HierarchyService>();
    return instance.get();
}

QList<SymbolRelationshipEngine::RelationType> HierarchyService::allRelationshipTypes()
{
    return {
        SymbolRelationshipEngine::CONTAINS,
        SymbolRelationshipEngine::REFERENCES,
        SymbolRelationshipEngine::INSTANTIATES,
        SymbolRelationshipEngine::CALLS,
        SymbolRelationshipEngine::INHERITS,
        SymbolRelationshipEngine::IMPLEMENTS,
        SymbolRelationshipEngine::ASSIGNS_TO,
        SymbolRelationshipEngine::READS_FROM,
        SymbolRelationshipEngine::CLOCKS,
        SymbolRelationshipEngine::RESETS,
        SymbolRelationshipEngine::GENERATES,
        SymbolRelationshipEngine::CONSTRAINS,
    };
}

HierarchyService::HierarchyService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance()),
      relationshipService(index)
{
}

HierarchyService::~HierarchyService() = default;

void HierarchyService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    relationshipService.setSemanticIndex(index);
}

QList<HierarchyNode> HierarchyService::getChildren(const HierarchyQuery& query) const
{
    const HierarchyQuery normalized = normalizedQuery(query);
    const int id = resolveSymbolId(normalized);
    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolStableKey = normalized.symbolStableKey.isValid()
        ? normalized.symbolStableKey
        : symbolStableKeyForSymbol(semanticIndex()->getSymbolById(id));
    relationshipQuery.symbolId = id;
    relationshipQuery.outgoing = true;
    relationshipQuery.types = effectiveTypes(normalized);

    QList<HierarchyNode> result;
    const QList<RelationshipResult> relationships =
        relationshipService.findRelationships(relationshipQuery);
    for (const RelationshipResult& rel : relationships) {
        if (rel.toSymbol.symbolId < 0)
            continue;

        HierarchyNode node;
        node.symbol = rel.toSymbol;
        node.symbolStableKey = rel.toStableKey;
        node.depth = 1;
        node.parentSymbolId = rel.fromSymbol.symbolId;
        node.parentStableKey = rel.fromStableKey;
        node.direction = HierarchyQuery::Children;
        node.viaType = rel.relationship.type;
        fillDisplayMetadata(node);
        result.append(node);
    }
    std::sort(result.begin(), result.end(), hierarchyNodeLess);
    return result;
}

QList<HierarchyNode> HierarchyService::getParents(const HierarchyQuery& query) const
{
    const HierarchyQuery normalized = normalizedQuery(query);
    const int id = resolveSymbolId(normalized);
    RelationshipQuery relationshipQuery;
    relationshipQuery.symbolStableKey = normalized.symbolStableKey.isValid()
        ? normalized.symbolStableKey
        : symbolStableKeyForSymbol(semanticIndex()->getSymbolById(id));
    relationshipQuery.symbolId = id;
    relationshipQuery.outgoing = false;
    relationshipQuery.types = effectiveTypes(normalized);

    QList<HierarchyNode> result;
    const QList<RelationshipResult> relationships =
        relationshipService.findRelationships(relationshipQuery);
    for (const RelationshipResult& rel : relationships) {
        if (rel.fromSymbol.symbolId < 0)
            continue;

        HierarchyNode node;
        node.symbol = rel.fromSymbol;
        node.symbolStableKey = rel.fromStableKey;
        node.depth = 1;
        node.parentSymbolId = rel.toSymbol.symbolId;
        node.parentStableKey = rel.toStableKey;
        node.direction = HierarchyQuery::Parents;
        node.viaType = rel.relationship.type;
        fillDisplayMetadata(node);
        result.append(node);
    }
    std::sort(result.begin(), result.end(), hierarchyNodeLess);
    return result;
}

HierarchyQuery HierarchyService::queryForPanel(
    const HierarchyPanelQueryOptions& options) const
{
    HierarchyQuery query;
    query.symbolName = options.symbolName;
    query.fileName = options.fileName;
    query.moduleName = options.moduleName;
    query.maxDepth = options.maxDepth;
    switch (options.direction) {
    case HierarchyPanelDirection::Outgoing:
        query.direction = HierarchyQuery::Children;
        break;
    case HierarchyPanelDirection::Incoming:
        query.direction = HierarchyQuery::Parents;
        break;
    case HierarchyPanelDirection::All:
    default:
        query.direction = HierarchyQuery::Both;
        break;
    }
    if (options.typeFilter >= 0) {
        query.types = {
            static_cast<SymbolRelationshipEngine::RelationType>(options.typeFilter)
        };
    } else {
        query.types = allRelationshipTypes();
    }
    return normalizedQuery(query);
}

QList<HierarchyNode> HierarchyService::moduleInstantiationChildren(
    int moduleSymbolId) const
{
    HierarchyQuery query;
    query.symbolId = moduleSymbolId;
    query.maxDepth = 1;
    query.types = {SymbolRelationshipEngine::INSTANTIATES};
    return getChildren(query);
}

SemanticIndex* HierarchyService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

int HierarchyService::resolveSymbolId(const HierarchyQuery& query) const
{
    if (query.symbolStableKey.isValid())
        return semanticIndex()->findSymbolId(query.symbolStableKey);
    if (query.symbolId >= 0)
        return query.symbolId;
    if (query.symbolName.isEmpty())
        return -1;

    SemanticQueryContext context;
    context.fileName = query.fileName;
    context.moduleName = query.moduleName;
    return semanticIndex()->findSymbolId(query.symbolName, context);
}

HierarchyQuery HierarchyService::normalizedQuery(const HierarchyQuery& query)
{
    HierarchyQuery normalized = query;
    normalized.fileName = normalizedHierarchyFileName(query.fileName);
    return normalized;
}

QList<SymbolRelationshipEngine::RelationType> HierarchyService::effectiveTypes(
    const HierarchyQuery& query) const
{
    if (!query.types.isEmpty())
        return query.types;
    return {SymbolRelationshipEngine::INSTANTIATES};
}
