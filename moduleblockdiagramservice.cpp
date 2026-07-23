#include "moduleblockdiagramservice.h"

#include "semanticindex.h"
#include "symboltaxonomy.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <functional>

std::unique_ptr<ModuleBlockDiagramService>
    ModuleBlockDiagramService::instance = nullptr;

namespace {
QString moduleBlockNotFoundReasonDisplayName(
    ModuleBlockDiagramNotFoundReason reason)
{
    switch (reason) {
    case ModuleBlockDiagramNotFoundReason::None:
        return QString();
    case ModuleBlockDiagramNotFoundReason::NoRootModule:
        return QStringLiteral("no root module");
    case ModuleBlockDiagramNotFoundReason::NoModuleContainment:
        return QStringLiteral("no module containment");
    }
    return QStringLiteral("module block diagram unavailable");
}

bool isModuleBlockDefinition(const SemanticSymbolRecord& record)
{
    if (!record.isValid())
        return false;

    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    return metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Module;
}

bool isModuleBlockInterfaceDefinition(const SemanticSymbolRecord& record)
{
    if (!record.isValid())
        return false;
    return semanticMetadataForSymbolRecord(record).declarationKind
        == SymbolTaxonomy::DeclarationKind::Interface;
}

QSet<QString> moduleBlockInterfaceTypeNames(SemanticIndex* index)
{
    QSet<QString> names;
    if (!index)
        return names;
    const QList<SemanticSymbolRecord> records = index->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (isModuleBlockInterfaceDefinition(record)
            && !record.name.trimmed().isEmpty()) {
            names.insert(record.name.trimmed());
        }
    }
    return names;
}

QString moduleBlockTypeNameForInstance(const SemanticSymbolRecord& record)
{
    if (!record.type.rawTypeText.isEmpty()) {
        const QString rawType =
            SymbolTaxonomy::interfaceTypeName(record.type.rawTypeText.trimmed());
        if (!rawType.isEmpty())
            return rawType;
    }
    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;
    if (record.type.stableKey.isValid()
        && !record.type.stableKey.symbolName.isEmpty()) {
        return record.type.stableKey.symbolName;
    }
    return QString();
}

QString normalizedModuleBlockAccessName(QString accessPath)
{
    accessPath = accessPath.trimmed();
    const int suffix = accessPath.lastIndexOf(QLatin1Char('@'));
    if (suffix > 0)
        accessPath.truncate(suffix);
    return accessPath.trimmed();
}

RtlInsightCodeLink moduleBlockCodeLinkForRange(
    const SemanticSourceRange& range,
    const QString& fallbackFileName = QString())
{
    if (range.fileName.isEmpty() || range.line <= 0)
        return {};
    QString fileName = range.fileName;
    const QFileInfo info(fileName);
    const QFileInfo fallbackInfo(fallbackFileName);
    if (info.isRelative()
        && !fallbackFileName.isEmpty()
        && !fallbackInfo.fileName().isEmpty()
        && QString::compare(info.fileName(),
                            fallbackInfo.fileName(),
                            Qt::CaseInsensitive)
            == 0) {
        fileName = fallbackFileName;
    } else if (!fileName.isEmpty()) {
        fileName = QDir::cleanPath(info.absoluteFilePath());
    }
    return RtlInsightLink::fromFileLine(fileName,
                                        range.line,
                                        range.column);
}

RtlInsightCodeLink moduleBlockCodeLinkForRecord(
    const SemanticSymbolRecord& record)
{
    if (!record.isValid())
        return {};
    if (record.location.fileName.isEmpty())
        return {};
    const QString fileName =
        QDir::cleanPath(QFileInfo(record.location.fileName).absoluteFilePath());
    return RtlInsightLink::fromFileLine(fileName,
                                        record.location.startLine,
                                        record.location.startColumn);
}

SemanticSymbolRecord moduleDefinitionForRecord(
    SemanticIndex* index,
    const SemanticSymbolRecord& record)
{
    if (isModuleBlockDefinition(record))
        return record;

    if (!record.isValid())
        return {};

    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    if (!SymbolTaxonomy::isInstanceDeclaration(metadata))
        return {};

    const QString moduleTypeName = moduleBlockTypeNameForInstance(record);
    if (moduleTypeName.isEmpty() || !index)
        return {};

    const QList<SemanticSymbolRecord> candidates =
        index->findDefinitionRecords(moduleTypeName);
    for (const SemanticSymbolRecord& candidate : candidates) {
        if (isModuleBlockDefinition(candidate))
            return candidate;
    }
    return {};
}

QString moduleDisplayName(const SemanticSymbolRecord& record)
{
    return record.name.isEmpty()
        ? QStringLiteral("<unnamed>")
        : record.name;
}

ModuleBlockDiagramNode moduleBlockNodeFromRecord(
    const SemanticSymbolRecord& record,
    int nodeId,
    int parentNodeId,
    int depth,
    const QString& instanceDisplayName = QString(),
    const RtlInsightCodeLink& instanceCodeLink = {},
    bool unresolved = false,
    const QString& unresolvedReason = QString(),
    const QString& moduleTypeFallback = QString())
{
    ModuleBlockDiagramNode node;
    node.moduleSymbolRecord = record;
    node.moduleStableKey = record.stableKey;
    node.nodeId = nodeId;
    node.parentNodeId = parentNodeId;
    node.depth = depth;
    node.moduleDisplayName = record.isValid()
        ? moduleDisplayName(record)
        : (moduleTypeFallback.isEmpty()
               ? QStringLiteral("<unresolved>")
               : moduleTypeFallback);
    node.instanceDisplayName = instanceDisplayName;
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    node.moduleTypeDisplayName = unresolved
        ? QStringLiteral("Blackbox")
        : SymbolTaxonomy::symbolTypeLabel(metadata);
    node.sourceRoleDisplayName =
        unresolved
            ? unresolvedReason
            : SymbolTaxonomy::sourceRoleDisplayName(metadata.sourceRole);
    node.unresolved = unresolved;
    node.unresolvedReason = unresolvedReason;
    node.definitionCodeLink = moduleBlockCodeLinkForRecord(record);
    node.instanceCodeLink = instanceCodeLink;
    return node;
}

ModuleBlockDiagramEdge moduleBlockEdgeFromNodes(
    const ModuleBlockDiagramNode& parent,
    const ModuleBlockDiagramNode& child)
{
    ModuleBlockDiagramEdge edge;
    edge.fromNodeId = parent.nodeId;
    edge.toNodeId = child.nodeId;
    edge.fromStableKey = parent.moduleStableKey;
    edge.toStableKey = child.moduleStableKey;
    edge.relationshipType = SymbolRelationshipEngine::INSTANTIATES;
    edge.relationshipDisplayName = QStringLiteral("Instantiates");
    edge.parentModuleDisplayName = parent.moduleDisplayName;
    edge.childModuleDisplayName = child.moduleDisplayName;
    edge.childInstanceDisplayName = child.instanceDisplayName;
    edge.unresolved = child.unresolved;
    edge.unresolvedReason = child.unresolvedReason;
    edge.childDefinitionCodeLink = child.definitionCodeLink;
    edge.childInstanceCodeLink = child.instanceCodeLink;
    return edge;
}

bool isModuleBlockInstanceDeclaration(const SemanticSymbolRecord& record)
{
    if (!record.isValid())
        return false;
    return SymbolTaxonomy::isInstanceDeclaration(
        semanticMetadataForSymbolRecord(record));
}

bool recordIsOwnedByModule(const SemanticSymbolRecord& record,
                           const SemanticSymbolRecord& module)
{
    if (!record.isValid() || !module.isValid())
        return false;
    if (record.owner.stableKey.isValid()
        && module.stableKey.isValid()
        && record.owner.stableKey == module.stableKey) {
        return true;
    }
    if (record.owner.name != module.name)
        return false;
    if (record.location.fileName != module.location.fileName)
        return true;
    if (record.location.startLine <= 0 || module.location.startLine <= 0)
        return true;
    if (record.location.startLine < module.location.startLine)
        return false;
    return module.location.endLine <= 0
        || record.location.startLine <= module.location.endLine;
}

struct ModuleBlockChildInstance {
    QString moduleTypeName;
    QString instanceName;
    SemanticSymbolRecord definitionRecord;
    SemanticSymbolRecord instanceRecord;
    RtlInsightCodeLink instanceCodeLink;
    RtlInsightCodeLink definitionCodeLink;
    bool unresolved = false;
    QString unresolvedReason;
    QString dedupeKey;
};

bool moduleBlockChildIsInterfaceLike(const ModuleBlockChildInstance& child,
                                     const QSet<QString>& interfaceNames)
{
    if (isModuleBlockInterfaceDefinition(child.definitionRecord)
        || isModuleBlockInterfaceDefinition(child.instanceRecord)) {
        return true;
    }
    const SemanticSymbolTypeReference type = child.instanceRecord.type;
    if (type.resolvedTypeKind == SymbolTaxonomy::DeclarationKind::Interface
        || type.stableKey.declarationKind
               == SymbolTaxonomy::DeclarationKind::Interface) {
        return true;
    }
    const QString resolved = type.resolvedTypeName.trimmed();
    if (!resolved.isEmpty() && interfaceNames.contains(resolved))
        return true;
    const QString rawInterface =
        SymbolTaxonomy::interfaceTypeName(type.rawTypeText.trimmed());
    if (!rawInterface.isEmpty() && interfaceNames.contains(rawInterface))
        return true;
    const QString moduleType =
        SymbolTaxonomy::interfaceTypeName(child.moduleTypeName.trimmed());
    return !moduleType.isEmpty() && interfaceNames.contains(moduleType);
}

QString moduleBlockChildDedupeKey(const QString& moduleTypeName,
                                  const QString& instanceName,
                                  const RtlInsightCodeLink& instanceCodeLink)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(moduleTypeName,
             instanceName,
             instanceCodeLink.fileName,
             QString::number(instanceCodeLink.line),
             QString::number(instanceCodeLink.column));
}

bool moduleBlockChildLess(const ModuleBlockChildInstance& lhs,
                          const ModuleBlockChildInstance& rhs)
{
    const int fileCompare =
        QString::compare(lhs.instanceCodeLink.fileName,
                         rhs.instanceCodeLink.fileName,
                         Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;
    if (lhs.instanceCodeLink.line != rhs.instanceCodeLink.line)
        return lhs.instanceCodeLink.line < rhs.instanceCodeLink.line;
    if (lhs.instanceCodeLink.column != rhs.instanceCodeLink.column)
        return lhs.instanceCodeLink.column < rhs.instanceCodeLink.column;
    const int moduleCompare =
        QString::compare(lhs.moduleTypeName,
                         rhs.moduleTypeName,
                         Qt::CaseInsensitive);
    if (moduleCompare != 0)
        return moduleCompare < 0;
    return QString::compare(lhs.instanceName,
                            rhs.instanceName,
                            Qt::CaseInsensitive) < 0;
}

SemanticSymbolRecord moduleDefinitionForTypeName(SemanticIndex* index,
                                                 const QString& moduleTypeName,
                                                 const QString& fileName = QString())
{
    if (!index || moduleTypeName.trimmed().isEmpty())
        return {};

    SemanticQueryContext context;
    context.fileName = fileName;
    const QList<SemanticSymbolRecord> candidates =
        index->findDefinitionRecords(moduleTypeName.trimmed(), context);
    for (const SemanticSymbolRecord& candidate : candidates) {
        if (isModuleBlockDefinition(candidate))
            return candidate;
    }
    return {};
}

QString moduleTypeNameForRelationship(const RelationshipResult& relationship)
{
    const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
    if (isModuleBlockDefinition(targetRecord))
        return targetRecord.name;
    const QString typeName = moduleBlockTypeNameForInstance(targetRecord);
    if (!typeName.isEmpty())
        return typeName;
    if (relationship.toStableKey.isValid()
        && !relationship.toStableKey.symbolName.isEmpty()) {
        return relationship.toStableKey.symbolName;
    }
    return targetRecord.name;
}

QString instanceNameForRelationship(const RelationshipResult& relationship,
                                    const QString& moduleTypeName)
{
    QString instanceName =
        normalizedModuleBlockAccessName(relationship.toAccessPath);
    if (instanceName.isEmpty()
        && isModuleBlockInstanceDeclaration(relationship.toSymbolRecord)) {
        instanceName = relationship.toSymbolRecord.name;
    }
    return instanceName.isEmpty() ? moduleTypeName : instanceName;
}

RtlInsightCodeLink instanceCodeLinkForRelationship(
    const RelationshipResult& relationship)
{
    RtlInsightCodeLink link =
        moduleBlockCodeLinkForRange(
            relationship.evidenceRange,
            relationship.fromSymbolRecord.location.fileName);
    if (!link.fileName.isEmpty())
        return link;
    if (isModuleBlockInstanceDeclaration(relationship.toSymbolRecord))
        return moduleBlockCodeLinkForRecord(relationship.toSymbolRecord);
    return {};
}
}

ModuleBlockDiagramService* ModuleBlockDiagramService::getInstance()
{
    if (!instance)
        instance = std::make_unique<ModuleBlockDiagramService>();
    return instance.get();
}

ModuleBlockDiagramService::ModuleBlockDiagramService(
    SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
    , hierarchyService(index)
{
}

ModuleBlockDiagramService::~ModuleBlockDiagramService() = default;

void ModuleBlockDiagramService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
    hierarchyService.setSemanticIndex(index);
}

ModuleBlockDiagramReport
ModuleBlockDiagramService::buildModuleBlockDiagram(
    const ModuleBlockDiagramQuery& query) const
{
    ModuleBlockDiagramReport report;
    report.groupDisplayName = QStringLiteral("Module Block Diagram");

    SemanticSymbolRecord rootRecord;
    if (query.moduleStableKey.isValid())
        rootRecord = semanticIndex()->getSymbolRecordByStableKey(query.moduleStableKey);
    if (!rootRecord.isValid() && !query.moduleName.trimmed().isEmpty())
        rootRecord = moduleDefinitionForTypeName(semanticIndex(),
                                                 query.moduleName,
                                                 query.fileName);
    rootRecord = moduleDefinitionForRecord(semanticIndex(), rootRecord);
    if (!rootRecord.isValid()) {
        report.notFoundReason =
            ModuleBlockDiagramNotFoundReason::NoRootModule;
        report.notFoundReasonDisplayName =
            moduleBlockNotFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    int nextNodeId = 0;
    report.root = moduleBlockNodeFromRecord(rootRecord, nextNodeId++, -1, 0);
    report.nodes.append(report.root);
    report.found = true;

    RelationshipService relationshipService(semanticIndex());
    const QSet<QString> interfaceNames =
        moduleBlockInterfaceTypeNames(semanticIndex());
    const int maxDepth = query.maxDepth < 0 ? 0 : query.maxDepth;
    QSet<QString> emittedChildKeys;
    std::function<void(const SemanticSymbolRecord&, int, int, QSet<QString>)>
        appendChildren;
    appendChildren = [&](const SemanticSymbolRecord& parentRecord,
                         int parentNodeId,
                         int depth,
                         QSet<QString> path) {
        if (depth >= maxDepth || !parentRecord.stableKey.isValid())
            return;

        const QString parentPathKey = parentRecord.stableKey.toString();
        if (!parentPathKey.isEmpty())
            path.insert(parentPathKey);

        QList<ModuleBlockChildInstance> children;
        auto appendChild = [&](ModuleBlockChildInstance child) {
            if (child.moduleTypeName.trimmed().isEmpty())
                return;
            if (moduleBlockChildIsInterfaceLike(child, interfaceNames))
                return;
            if (child.instanceName.trimmed().isEmpty())
                child.instanceName = child.moduleTypeName;
            if (child.instanceCodeLink.fileName.isEmpty()
                && child.instanceRecord.isValid()) {
                child.instanceCodeLink =
                    moduleBlockCodeLinkForRecord(child.instanceRecord);
            }
            if (child.definitionRecord.isValid()
                && child.definitionCodeLink.fileName.isEmpty()) {
                child.definitionCodeLink =
                    moduleBlockCodeLinkForRecord(child.definitionRecord);
            }
            if (!child.definitionRecord.isValid()) {
                child.unresolved = true;
                if (child.unresolvedReason.isEmpty()) {
                    child.unresolvedReason =
                        QStringLiteral("module definition not found");
                }
            }
            for (const ModuleBlockChildInstance& existing :
                 std::as_const(children)) {
                if (existing.moduleTypeName == child.moduleTypeName
                    && existing.instanceName == child.instanceName) {
                    return;
                }
                if (child.instanceName == child.moduleTypeName
                    && existing.moduleTypeName == child.moduleTypeName
                    && existing.definitionRecord.stableKey
                        == child.definitionRecord.stableKey) {
                    return;
                }
            }
            child.dedupeKey = moduleBlockChildDedupeKey(
                child.moduleTypeName,
                child.instanceName,
                child.instanceCodeLink);
            if (emittedChildKeys.contains(
                    QStringLiteral("%1|%2").arg(parentNodeId).arg(child.dedupeKey))) {
                return;
            }
            emittedChildKeys.insert(
                QStringLiteral("%1|%2").arg(parentNodeId).arg(child.dedupeKey));
            children.append(child);
        };

        for (const SemanticSymbolRecord& record :
             semanticIndex()->getSymbolRecordsByOwner(parentRecord.name)) {
            if (!isModuleBlockInstanceDeclaration(record)
                || !recordIsOwnedByModule(record, parentRecord)) {
                continue;
            }
            ModuleBlockChildInstance child;
            child.instanceRecord = record;
            child.moduleTypeName = moduleBlockTypeNameForInstance(record);
            child.instanceName = record.name;
            child.instanceCodeLink = moduleBlockCodeLinkForRecord(record);
            child.definitionRecord =
                moduleDefinitionForTypeName(semanticIndex(),
                                            child.moduleTypeName,
                                            record.location.fileName);
            appendChild(child);
        }

        RelationshipQuery relationshipQuery;
        relationshipQuery.symbolStableKey = parentRecord.stableKey;
        relationshipQuery.outgoing = true;
        relationshipQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
        const QList<RelationshipResult> relationships =
            relationshipService.findRelationships(relationshipQuery);
        for (const RelationshipResult& relationship : relationships) {
            ModuleBlockChildInstance child;
            child.moduleTypeName = moduleTypeNameForRelationship(relationship);
            child.instanceName =
                instanceNameForRelationship(relationship, child.moduleTypeName);
            child.instanceCodeLink =
                instanceCodeLinkForRelationship(relationship);
            child.definitionRecord =
                moduleDefinitionForRecord(semanticIndex(),
                                          relationship.toSymbolRecord);
            if (!child.definitionRecord.isValid()) {
                child.definitionRecord =
                    moduleDefinitionForTypeName(semanticIndex(),
                                                child.moduleTypeName,
                                                relationship.evidenceRange.fileName);
            }
            child.definitionCodeLink =
                moduleBlockCodeLinkForRecord(child.definitionRecord);
            appendChild(child);
        }

        std::sort(children.begin(), children.end(), moduleBlockChildLess);
        for (ModuleBlockChildInstance child : std::as_const(children)) {
            const QString childPathKey =
                child.definitionRecord.stableKey.toString();
            if (child.definitionRecord.isValid()
                && !childPathKey.isEmpty()
                && path.contains(childPathKey)) {
                child.unresolved = true;
                child.unresolvedReason =
                    QStringLiteral("cyclic instantiation");
            }

            const ModuleBlockDiagramNode childNode =
                moduleBlockNodeFromRecord(child.definitionRecord,
                                          nextNodeId++,
                                          parentNodeId,
                                          depth + 1,
                                          child.instanceName,
                                          child.instanceCodeLink,
                                          child.unresolved,
                                          child.unresolvedReason,
                                          child.moduleTypeName);
            const int childIndex = report.nodes.size();
            report.nodes.append(childNode);
            report.edges.append(moduleBlockEdgeFromNodes(
                report.nodes.at(parentNodeId),
                report.nodes.at(childIndex)));
            if (child.unresolved)
                ++report.unresolvedInstanceCount;
            else
                ++report.resolvedInstanceCount;

            if (!child.unresolved && child.definitionRecord.isValid())
                appendChildren(child.definitionRecord,
                               childNode.nodeId,
                               depth + 1,
                               path);
        }
    };

    appendChildren(rootRecord, report.root.nodeId, 0, {});

    report.moduleCount = report.nodes.size();
    report.edgeCount = report.edges.size();
    if (report.edges.isEmpty()) {
        report.notFoundReason =
            ModuleBlockDiagramNotFoundReason::NoModuleContainment;
        report.notFoundReasonDisplayName =
            moduleBlockNotFoundReasonDisplayName(report.notFoundReason);
    }
    return report;
}

SemanticIndex* ModuleBlockDiagramService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
