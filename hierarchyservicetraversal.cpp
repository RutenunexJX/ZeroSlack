#include "hierarchyservice.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <functional>

namespace {
QString reportNotFoundReasonDisplayName(HierarchyReportNotFoundReason reason)
{
    switch (reason) {
    case HierarchyReportNotFoundReason::None:
        return QString();
    case HierarchyReportNotFoundReason::NoRootSymbol:
        return QStringLiteral("no root symbol");
    case HierarchyReportNotFoundReason::NoHierarchy:
        return QStringLiteral("no hierarchy");
    }
    return QStringLiteral("hierarchy report unavailable");
}

QString hierarchyNodePathKey(const HierarchyNode& node)
{
    const QString stableKey = symbolStableKeyText(node.symbolStableKey);
    if (!stableKey.isEmpty())
        return stableKey;
    return QStringLiteral("local:%1").arg(node.symbolRecord.localHandle);
}

QString normalizedDesignHierarchyFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool isDesignModuleDeclaration(const SemanticSymbolRecord& record)
{
    return record.isValid()
        && SymbolTaxonomy::isModuleDeclaration(semanticMetadataForSymbolRecord(record));
}

bool isDesignInstanceDeclaration(const SemanticSymbolRecord& record)
{
    return record.isValid()
        && SymbolTaxonomy::isInstanceDeclaration(semanticMetadataForSymbolRecord(record));
}

QString designModuleTypeForInstance(const SemanticSymbolRecord& record,
                                    const QString& fallback = QString())
{
    if (!record.type.resolvedTypeName.isEmpty())
        return record.type.resolvedTypeName;
    if (!record.type.rawTypeText.isEmpty())
        return record.type.rawTypeText.trimmed();
    if (record.type.stableKey.isValid() && !record.type.stableKey.symbolName.isEmpty())
        return record.type.stableKey.symbolName;
    return fallback;
}

void addDesignParticipatingFile(DesignHierarchyReport& report, const QString& fileName)
{
    const QString normalized = normalizedDesignHierarchyFileName(fileName);
    if (!normalized.isEmpty())
        report.participatingFiles.insert(normalized);
}

QStringList uniqueSortedStringList(QSet<QString> values)
{
    QStringList result = values.values();
    std::sort(result.begin(), result.end(), [](const QString& lhs, const QString& rhs) {
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });
    return result;
}
}

QList<HierarchyNode> HierarchyService::getHierarchy(const HierarchyQuery& query) const
{
    const HierarchyQuery normalized = normalizedQuery(query);
    const SemanticSymbolRecord rootRecord = resolveSubjectSymbolRecord(normalized);
    const SymbolStableKey rootStableKey = rootRecord.stableKey;
    if (!rootStableKey.isValid())
        return {};

    const int maxDepth = normalized.maxDepth < 0 ? 0 : normalized.maxDepth;
    QList<HierarchyNode> result;
    struct WorkItem {
        HierarchyNode node;
        QSet<QString> path;
    };
    QList<WorkItem> queue;
    QSet<QString> emittedEdges;
    int nextNodeId = 0;

    HierarchyNode root;
    root.symbolRecord = rootRecord;
    root.symbolStableKey = rootStableKey;
    root.depth = 0;
    root.nodeId = nextNodeId++;
    root.parentNodeId = -1;
    root.direction = normalized.direction;
    fillDisplayMetadata(root);
    WorkItem rootItem;
    rootItem.node = root;
    rootItem.path.insert(hierarchyNodePathKey(root));
    queue.append(rootItem);

    while (!queue.isEmpty()) {
        const WorkItem current = queue.takeFirst();
        result.append(current.node);

        if (current.node.depth >= maxDepth)
            continue;

        auto appendNext = [&](QList<HierarchyNode> nextNodes,
                              HierarchyQuery::Direction edgeDirection) {
            for (HierarchyNode child : nextNodes) {
                if (!child.symbolStableKey.isValid()
                    && child.symbolRecord.localHandle < 0) {
                    continue;
                }
                const QString childPathKey = hierarchyNodePathKey(child);
                if (current.path.contains(childPathKey))
                    continue;

                const QString edgeKey = QStringLiteral("%1:%2:%3:%4")
                    .arg(current.node.nodeId)
                    .arg(static_cast<int>(edgeDirection))
                    .arg(static_cast<int>(child.viaType))
                    .arg(childPathKey);
                if (emittedEdges.contains(edgeKey))
                    continue;
                emittedEdges.insert(edgeKey);

                child.depth = current.node.depth + 1;
                child.parentStableKey = current.node.symbolStableKey;
                child.nodeId = nextNodeId++;
                child.parentNodeId = current.node.nodeId;
                child.direction = edgeDirection;
                fillDisplayMetadata(child);

                WorkItem childItem;
                childItem.node = child;
                childItem.path = current.path;
                childItem.path.insert(childPathKey);
                queue.append(childItem);
            }
        };

        HierarchyQuery childQuery = normalized;
        childQuery.symbolStableKey = current.node.symbolStableKey;
        if (normalized.direction == HierarchyQuery::Children
            || normalized.direction == HierarchyQuery::Both) {
            appendNext(getChildren(childQuery), HierarchyQuery::Children);
        }
        if (normalized.direction == HierarchyQuery::Parents
            || normalized.direction == HierarchyQuery::Both) {
            appendNext(getParents(childQuery), HierarchyQuery::Parents);
        }
    }

    return result;
}

HierarchyReport HierarchyService::getHierarchyReport(const HierarchyQuery& query) const
{
    const HierarchyQuery normalized = normalizedQuery(query);
    HierarchyReport report;
    report.rootSymbolRecord = resolveSubjectSymbolRecord(normalized);
    report.rootStableKey = report.rootSymbolRecord.stableKey;
    if (!report.rootStableKey.isValid()) {
        report.notFoundReason = HierarchyReportNotFoundReason::NoRootSymbol;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
        return report;
    }

    HierarchyQuery resolvedQuery = normalized;
    if (!resolvedQuery.symbolStableKey.isValid())
        resolvedQuery.symbolStableKey = report.rootStableKey;
    report.nodes = getHierarchy(resolvedQuery);
    report.totalCount = report.nodes.size();
    QMap<HierarchyQuery::Direction, int> rootDirectionGroupIndexes;
    for (const HierarchyNode& node : report.nodes) {
        report.depthCounts[node.depth]++;
        if (node.depth <= 0)
            continue;
        report.directionCounts[node.direction]++;
        report.typeCounts[node.viaType]++;
        if (node.parentNodeId == 0) {
            report.rootDirectionCounts[node.direction]++;
            if (!rootDirectionGroupIndexes.contains(node.direction)) {
                HierarchyRootDirectionGroup group;
                group.direction = node.direction;
                group.displayName = directionDisplayName(node.direction);
                rootDirectionGroupIndexes.insert(node.direction,
                                                 report.rootDirectionGroups.size());
                report.rootDirectionGroups.append(group);
            }
            HierarchyRootDirectionGroup& group =
                report.rootDirectionGroups[rootDirectionGroupIndexes.value(node.direction)];
            group.nodes.append(node);
            group.count++;
        }
    }
    if (report.totalCount <= 1) {
        report.notFoundReason = HierarchyReportNotFoundReason::NoHierarchy;
        report.notFoundReasonDisplayName =
            reportNotFoundReasonDisplayName(report.notFoundReason);
    } else {
        report.notFoundReason = HierarchyReportNotFoundReason::None;
    }
    return report;
}

QStringList HierarchyService::modulesDefinedInFile(const QString& fileName) const
{
    const QString normalizedFileName = normalizedDesignHierarchyFileName(fileName);
    if (normalizedFileName.isEmpty())
        return {};

    QSet<QString> moduleNames;
    const QList<SemanticSymbolRecord> records = semanticIndex()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (!isDesignModuleDeclaration(record))
            continue;
        if (normalizedDesignHierarchyFileName(record.location.fileName) != normalizedFileName)
            continue;
        if (!record.name.isEmpty())
            moduleNames.insert(record.name);
    }
    return uniqueSortedStringList(moduleNames);
}

QString HierarchyService::inferDesignTopModule() const
{
    QHash<QString, SemanticSymbolRecord> modulesByName;
    QList<SemanticSymbolRecord> modules;
    const QList<SemanticSymbolRecord> records = semanticIndex()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (!isDesignModuleDeclaration(record) || record.name.isEmpty())
            continue;

        const auto existing = modulesByName.constFind(record.name);
        if (existing != modulesByName.constEnd()) {
            const SemanticSymbolRecord& existingRecord = existing.value();
            if (existingRecord.location.startLine > 0
                && (record.location.startLine <= 0
                    || existingRecord.location.startLine <= record.location.startLine)) {
                continue;
            }
            for (SemanticSymbolRecord& module : modules) {
                if (module.name == record.name) {
                    module = record;
                    break;
                }
            }
            modulesByName.insert(record.name, record);
            continue;
        }

        modulesByName.insert(record.name, record);
        modules.append(record);
    }

    if (modules.isEmpty())
        return QString();

    QSet<QString> instantiatedModules;
    QHash<QString, int> outgoingInstantiationCounts;
    for (const SemanticSymbolRecord& module : modules) {
        if (!module.stableKey.isValid())
            continue;

        RelationshipQuery relationshipQuery;
        relationshipQuery.symbolStableKey = module.stableKey;
        relationshipQuery.outgoing = true;
        relationshipQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
        const QList<RelationshipResult> relationships =
            relationshipService.findRelationships(relationshipQuery);
        for (const RelationshipResult& relationship : relationships) {
            const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
            if (!targetRecord.isValid())
                continue;

            const bool targetIsModule = isDesignModuleDeclaration(targetRecord);
            const QString moduleType = targetIsModule
                ? targetRecord.name
                : designModuleTypeForInstance(targetRecord,
                                              relationship.toStableKey.symbolName);
            if (moduleType.isEmpty() || !modulesByName.contains(moduleType))
                continue;

            instantiatedModules.insert(moduleType);
            outgoingInstantiationCounts[module.name] += 1;
        }
    }

    struct Candidate {
        QString name;
        QString fileName;
        int line = 0;
        int childCount = 0;
        bool topLike = false;
    };

    QList<Candidate> candidates;
    auto appendCandidate = [&](const SemanticSymbolRecord& module) {
        Candidate candidate;
        candidate.name = module.name;
        candidate.fileName = normalizedDesignHierarchyFileName(
            module.location.fileName);
        candidate.line = module.location.startLine;
        candidate.childCount =
            outgoingInstantiationCounts.value(module.name);
        candidate.topLike =
            module.name.contains(QStringLiteral("top"), Qt::CaseInsensitive);
        candidates.append(candidate);
    };

    for (const SemanticSymbolRecord& module : modules) {
        if (!instantiatedModules.contains(module.name))
            appendCandidate(module);
    }
    if (candidates.isEmpty()) {
        for (const SemanticSymbolRecord& module : modules)
            appendCandidate(module);
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.childCount != rhs.childCount)
            return lhs.childCount > rhs.childCount;
        if (lhs.topLike != rhs.topLike)
            return lhs.topLike;
        const int nameCompare =
            QString::compare(lhs.name, rhs.name, Qt::CaseInsensitive);
        if (nameCompare != 0)
            return nameCompare < 0;
        const int fileCompare =
            QString::compare(lhs.fileName, rhs.fileName, Qt::CaseInsensitive);
        if (fileCompare != 0)
            return fileCompare < 0;
        return lhs.line < rhs.line;
    });

    return candidates.isEmpty() ? QString() : candidates.first().name;
}

DesignHierarchyReport HierarchyService::getDesignHierarchyReport(const QString& topModule) const
{
    DesignHierarchyReport report;
    report.topModule = topModule;
    report.snapshotGeneration = semanticIndex()->snapshotRevision();
    if (topModule.trimmed().isEmpty())
        return report;

    QHash<QString, SemanticSymbolRecord> modulesByName;
    const QList<SemanticSymbolRecord> records = semanticIndex()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records) {
        if (!isDesignModuleDeclaration(record) || record.name.isEmpty())
            continue;
        if (modulesByName.contains(record.name)) {
            const SemanticSymbolRecord& existing = modulesByName.value(record.name);
            if (existing.location.startLine > 0
                && (record.location.startLine <= 0
                    || existing.location.startLine <= record.location.startLine)) {
                continue;
            }
        }
        modulesByName.insert(record.name, record);
    }

    const SemanticSymbolRecord topRecord = modulesByName.value(topModule);
    if (!topRecord.isValid())
        return report;

    int nextNodeId = 0;
    auto nextId = [&]() {
        return QString::number(nextNodeId++);
    };

    DesignHierarchyNode topNode;
    topNode.id = nextId();
    topNode.instanceName = topRecord.name;
    topNode.moduleType = topRecord.name;
    topNode.definitionFile = normalizedDesignHierarchyFileName(topRecord.location.fileName);
    topNode.definitionLine = topRecord.location.startLine;
    topNode.definitionColumn = topRecord.location.startColumn;
    topNode.isTop = true;
    report.nodes.append(topNode);
    addDesignParticipatingFile(report, topRecord.location.fileName);

    QSet<QString> unresolvedModules;
    QSet<QString> emittedEdges;
    std::function<void(const SemanticSymbolRecord&, const QString&, QSet<QString>)> appendChildren;
    appendChildren = [&](const SemanticSymbolRecord& parentModule,
                         const QString& parentNodeId,
                         QSet<QString> modulePath) {
        if (!parentModule.stableKey.isValid())
            return;

        const QString modulePathKey = parentModule.stableKey.toString();
        if (!modulePathKey.isEmpty())
            modulePath.insert(modulePathKey);

        RelationshipQuery relationshipQuery;
        relationshipQuery.symbolStableKey = parentModule.stableKey;
        relationshipQuery.outgoing = true;
        relationshipQuery.types = {SymbolRelationshipEngine::INSTANTIATES};
        QList<RelationshipResult> relationships =
            relationshipService.findRelationships(relationshipQuery);
        std::sort(relationships.begin(), relationships.end(),
                  [](const RelationshipResult& lhs, const RelationshipResult& rhs) {
            const SemanticSymbolLocation& left = lhs.toSymbolRecord.location;
            const SemanticSymbolLocation& right = rhs.toSymbolRecord.location;
            const int fileCompare = QString::compare(left.fileName,
                                                     right.fileName,
                                                     Qt::CaseInsensitive);
            if (fileCompare != 0)
                return fileCompare < 0;
            if (left.startLine != right.startLine)
                return left.startLine < right.startLine;
            if (left.startColumn != right.startColumn)
                return left.startColumn < right.startColumn;
            return QString::compare(lhs.toSymbolRecord.name,
                                    rhs.toSymbolRecord.name,
                                    Qt::CaseInsensitive) < 0;
        });

        for (const RelationshipResult& relationship : relationships) {
            const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
            if (!targetRecord.isValid())
                continue;

            const bool targetIsInstance = isDesignInstanceDeclaration(targetRecord);
            const bool targetIsModule = isDesignModuleDeclaration(targetRecord);
            QString moduleType = targetIsModule
                ? targetRecord.name
                : designModuleTypeForInstance(targetRecord,
                                              relationship.toStableKey.symbolName);
            if (moduleType.isEmpty())
                moduleType = targetRecord.name;
            const SemanticSymbolRecord definitionRecord = modulesByName.value(moduleType);
            if (!targetIsModule && !definitionRecord.isValid())
                continue;
            const QString edgeKey = QStringLiteral("%1:%2:%3:%4")
                .arg(parentNodeId,
                     targetRecord.name,
                     moduleType,
                     QString::number(targetRecord.location.startLine));
            if (emittedEdges.contains(edgeKey))
                continue;
            emittedEdges.insert(edgeKey);

            DesignHierarchyNode node;
            node.id = nextId();
            node.parentId = parentNodeId;
            node.instanceName = targetIsInstance
                ? targetRecord.name
                : (targetRecord.name.isEmpty() ? moduleType : targetRecord.name);
            node.moduleType = moduleType;
            node.instanceFile = normalizedDesignHierarchyFileName(targetRecord.location.fileName);
            node.instanceLine = targetRecord.location.startLine;
            node.instanceColumn = targetRecord.location.startColumn;
            if (definitionRecord.isValid()) {
                node.definitionFile =
                    normalizedDesignHierarchyFileName(definitionRecord.location.fileName);
                node.definitionLine = definitionRecord.location.startLine;
                node.definitionColumn = definitionRecord.location.startColumn;
                addDesignParticipatingFile(report, targetRecord.location.fileName);
                addDesignParticipatingFile(report, definitionRecord.location.fileName);
            } else {
                node.unresolved = true;
                node.unresolvedReason = QStringLiteral("module definition not found");
                if (!moduleType.isEmpty())
                    unresolvedModules.insert(moduleType);
                addDesignParticipatingFile(report, targetRecord.location.fileName);
            }
            report.nodes.append(node);

            if (!definitionRecord.isValid())
                continue;

            const QString childPathKey = definitionRecord.stableKey.toString();
            if (!childPathKey.isEmpty() && modulePath.contains(childPathKey)) {
                DesignHierarchyNode& cyclicNode = report.nodes.last();
                cyclicNode.unresolved = true;
                cyclicNode.unresolvedReason = QStringLiteral("cyclic instantiation");
                unresolvedModules.insert(moduleType);
                continue;
            }
            appendChildren(definitionRecord, node.id, modulePath);
        }
    };

    appendChildren(topRecord, topNode.id, {});
    report.unresolvedModules = uniqueSortedStringList(unresolvedModules);
    return report;
}
