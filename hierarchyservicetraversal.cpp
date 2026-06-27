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

bool designFileScopeContains(const QSet<QString>& fileScope,
                             const QString& fileName)
{
    if (fileScope.isEmpty())
        return true;
    const QString normalized = normalizedDesignHierarchyFileName(fileName);
    return fileScope.contains(normalized)
        || fileScope.contains(normalized.toCaseFolded());
}

QStringList uniqueSortedStringList(QSet<QString> values)
{
    QStringList result = values.values();
    std::sort(result.begin(), result.end(), [](const QString& lhs, const QString& rhs) {
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });
    return result;
}

void appendDesignModuleRecord(
    QHash<QString, SemanticSymbolRecord>* modulesByName,
    QList<SemanticSymbolRecord>* modules,
    const SemanticSymbolRecord& record,
    const QSet<QString>& fileScope = {})
{
    if (!modulesByName || !modules)
        return;
    if (!isDesignModuleDeclaration(record) || record.name.isEmpty())
        return;
    if (!designFileScopeContains(fileScope, record.location.fileName))
        return;

    const auto existing = modulesByName->constFind(record.name);
    if (existing != modulesByName->constEnd()) {
        const SemanticSymbolRecord& existingRecord = existing.value();
        if (existingRecord.location.startLine > 0
            && (record.location.startLine <= 0
                || existingRecord.location.startLine <= record.location.startLine)) {
            return;
        }
        for (SemanticSymbolRecord& module : *modules) {
            if (module.name == record.name) {
                module = record;
                break;
            }
        }
        modulesByName->insert(record.name, record);
        return;
    }

    modulesByName->insert(record.name, record);
    modules->append(record);
}

QString designModuleTypeForRelationship(
    const RelationshipResult& relationship)
{
    const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
    const bool targetIsModule = isDesignModuleDeclaration(targetRecord);
    QString moduleType = targetIsModule
        ? targetRecord.name
        : designModuleTypeForInstance(targetRecord,
                                      relationship.toStableKey.symbolName);
    if (moduleType.isEmpty())
        moduleType = targetRecord.name;
    return moduleType;
}

QString designAccessPathDisplayName(const QString& accessPath)
{
    const int suffix = accessPath.lastIndexOf(QLatin1Char('@'));
    if (suffix > 0)
        return accessPath.left(suffix);
    return accessPath;
}

QString designInstanceNameForRelationship(
    const RelationshipResult& relationship,
    const SemanticSymbolRecord& targetRecord,
    bool targetIsInstance,
    const QString& moduleType)
{
    const QString accessName =
        designAccessPathDisplayName(relationship.toAccessPath).trimmed();
    if (!accessName.isEmpty())
        return accessName;
    if (targetIsInstance && !targetRecord.name.isEmpty())
        return targetRecord.name;
    if (!targetRecord.name.isEmpty()
        && !isDesignModuleDeclaration(targetRecord)) {
        return targetRecord.name;
    }
    return moduleType;
}

QString designRelationshipFileName(
    const RelationshipResult& relationship,
    const SemanticSymbolRecord& targetRecord)
{
    if (!relationship.evidenceRange.fileName.isEmpty())
        return relationship.evidenceRange.fileName;
    return targetRecord.location.fileName;
}

int designRelationshipLine(
    const RelationshipResult& relationship,
    const SemanticSymbolRecord& targetRecord)
{
    return relationship.evidenceRange.line > 0
        ? relationship.evidenceRange.line
        : targetRecord.location.startLine;
}

int designRelationshipColumn(
    const RelationshipResult& relationship,
    const SemanticSymbolRecord& targetRecord)
{
    return relationship.evidenceRange.column > 0
        ? relationship.evidenceRange.column
        : targetRecord.location.startColumn;
}

bool designRelationshipLess(const RelationshipResult& lhs,
                            const RelationshipResult& rhs)
{
    const QString leftFile =
        designRelationshipFileName(lhs, lhs.toSymbolRecord);
    const QString rightFile =
        designRelationshipFileName(rhs, rhs.toSymbolRecord);
    const int fileCompare =
        QString::compare(leftFile, rightFile, Qt::CaseInsensitive);
    if (fileCompare != 0)
        return fileCompare < 0;

    const int leftLine =
        designRelationshipLine(lhs, lhs.toSymbolRecord);
    const int rightLine =
        designRelationshipLine(rhs, rhs.toSymbolRecord);
    if (leftLine != rightLine)
        return leftLine < rightLine;

    const int leftColumn =
        designRelationshipColumn(lhs, lhs.toSymbolRecord);
    const int rightColumn =
        designRelationshipColumn(rhs, rhs.toSymbolRecord);
    if (leftColumn != rightColumn)
        return leftColumn < rightColumn;

    return QString::compare(lhs.toAccessPath,
                            rhs.toAccessPath,
                            Qt::CaseInsensitive) < 0;
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
    const QStringList modules = inferDesignTopModules();
    return modules.isEmpty() ? QString() : modules.first();
}

QString HierarchyService::inferDesignTopModule(
    const QSet<QString>& fileScope) const
{
    const QStringList modules = inferDesignTopModules(fileScope);
    return modules.isEmpty() ? QString() : modules.first();
}

QStringList HierarchyService::inferDesignTopModules() const
{
    return inferDesignTopModules({});
}

QStringList HierarchyService::inferDesignTopModules(
    const QSet<QString>& fileScope) const
{
    QHash<QString, SemanticSymbolRecord> modulesByName;
    QList<SemanticSymbolRecord> modules;
    const QList<SemanticSymbolRecord> records = semanticIndex()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records)
        appendDesignModuleRecord(&modulesByName, &modules, record, fileScope);

    if (modules.isEmpty())
        return {};

    QSet<QString> instantiatedModules;
    QHash<QString, int> outgoingInstantiationCounts;
    QHash<QString, QStringList> childrenByModule;
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
            const QString moduleType =
                designModuleTypeForRelationship(relationship);
            if (moduleType.isEmpty() || !modulesByName.contains(moduleType))
                continue;
            if (moduleType == module.name)
                continue;

            instantiatedModules.insert(moduleType);
            outgoingInstantiationCounts[module.name] += 1;
            childrenByModule[module.name].append(moduleType);
        }
    }

    std::function<int(const QString&, QSet<QString>)> subtreeSize;
    subtreeSize = [&](const QString& moduleName, QSet<QString> path) {
        if (path.contains(moduleName))
            return 0;
        path.insert(moduleName);
        int total = 1;
        for (const QString& child : childrenByModule.value(moduleName))
            total += subtreeSize(child, path);
        return total;
    };

    struct Candidate {
        QString name;
        QString fileName;
        int line = 0;
        int childCount = 0;
        int subtreeCount = 0;
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
        candidate.subtreeCount = subtreeSize(module.name, {});
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
        if (lhs.subtreeCount != rhs.subtreeCount)
            return lhs.subtreeCount > rhs.subtreeCount;
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

    QStringList result;
    result.reserve(candidates.size());
    for (const Candidate& candidate : std::as_const(candidates))
        result.append(candidate.name);
    return result;
}

DesignHierarchyReport HierarchyService::getDesignHierarchyReport(const QString& topModule) const
{
    return getDesignHierarchyReport(QStringList{topModule}, topModule);
}

DesignHierarchyReport HierarchyService::getDesignHierarchyReport(
    const QString& topModule,
    const QSet<QString>& fileScope) const
{
    return getDesignHierarchyReport(QStringList{topModule}, topModule, fileScope);
}

DesignHierarchyReport HierarchyService::getDesignHierarchyReport(
    const QStringList& topModules,
    const QString& selectedTopModule) const
{
    return getDesignHierarchyReport(topModules, selectedTopModule, {});
}

DesignHierarchyReport HierarchyService::getDesignHierarchyReport(
    const QStringList& topModules,
    const QString& selectedTopModule,
    const QSet<QString>& fileScope) const
{
    DesignHierarchyReport report;
    report.snapshotGeneration = semanticIndex()->snapshotRevision();

    const QString selectedTop = selectedTopModule.trimmed();
    report.selectedTopModule = selectedTop;

    QSet<QString> seenRoots;
    QStringList roots;
    auto appendRootName = [&](const QString& moduleName) {
        const QString trimmed = moduleName.trimmed();
        if (trimmed.isEmpty() || seenRoots.contains(trimmed))
            return;
        seenRoots.insert(trimmed);
        roots.append(trimmed);
    };
    if (!selectedTop.isEmpty())
        appendRootName(selectedTop);
    for (const QString& moduleName : topModules)
        appendRootName(moduleName);

    report.rootModules = roots;
    report.topModule = selectedTop.isEmpty()
        ? (roots.isEmpty() ? QString() : roots.first())
        : selectedTop;
    if (roots.isEmpty())
        return report;

    QHash<QString, SemanticSymbolRecord> modulesByName;
    QList<SemanticSymbolRecord> modules;
    const QList<SemanticSymbolRecord> records = semanticIndex()->getSymbolRecords();
    for (const SemanticSymbolRecord& record : records)
        appendDesignModuleRecord(&modulesByName, &modules, record, fileScope);

    int nextNodeId = 0;
    auto nextId = [&]() {
        return QString::number(nextNodeId++);
    };

    auto addParticipatingFile = [&](const QString& fileName, bool inSelectedTop) {
        if (selectedTop.isEmpty() || inSelectedTop)
            addDesignParticipatingFile(report, fileName);
    };

    QSet<QString> unresolvedModules;
    QSet<QString> emittedEdges;
    std::function<void(const SemanticSymbolRecord&,
                       const QString&,
                       const QString&,
                       const QString&,
                       bool,
                       QSet<QString>)> appendChildren;
    appendChildren = [&](const SemanticSymbolRecord& parentModule,
                         const QString& parentNodeId,
                         const QString& rootNodeId,
                         const QString& rootModule,
                         bool inSelectedTop,
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
        std::sort(relationships.begin(), relationships.end(), designRelationshipLess);

        for (const RelationshipResult& relationship : relationships) {
            const SemanticSymbolRecord targetRecord = relationship.toSymbolRecord;
            if (!targetRecord.isValid())
                continue;

            const bool targetIsInstance = isDesignInstanceDeclaration(targetRecord);
            QString moduleType = designModuleTypeForRelationship(relationship);
            const SemanticSymbolRecord definitionRecord = modulesByName.value(moduleType);
            if (moduleType.isEmpty())
                continue;
            if (moduleType == parentModule.name)
                continue;

            const QString instanceName =
                designInstanceNameForRelationship(relationship,
                                                  targetRecord,
                                                  targetIsInstance,
                                                  moduleType);
            const QString instanceFile =
                normalizedDesignHierarchyFileName(
                    designRelationshipFileName(relationship, targetRecord));
            const int instanceLine =
                designRelationshipLine(relationship, targetRecord);
            const int instanceColumn =
                designRelationshipColumn(relationship, targetRecord);
            const QString relationshipKey =
                semanticRelationshipStableKeyText(relationship.relationship);
            const QString edgeKey = relationshipKey.isEmpty()
                ? QStringLiteral("%1:%2:%3:%4:%5:%6")
                      .arg(parentNodeId,
                           instanceName,
                           moduleType,
                           instanceFile,
                           QString::number(instanceLine),
                           QString::number(instanceColumn))
                : QStringLiteral("%1:%2").arg(parentNodeId, relationshipKey);
            if (emittedEdges.contains(edgeKey))
                continue;
            emittedEdges.insert(edgeKey);

            DesignHierarchyNode node;
            node.id = nextId();
            node.parentId = parentNodeId;
            node.rootId = rootNodeId;
            node.rootModule = rootModule;
            node.instanceName = instanceName;
            node.moduleType = moduleType;
            node.instanceFile = instanceFile;
            node.instanceLine = instanceLine;
            node.instanceColumn = instanceColumn;
            node.inSelectedTop = inSelectedTop;
            if (definitionRecord.isValid()) {
                node.definitionFile =
                    normalizedDesignHierarchyFileName(definitionRecord.location.fileName);
                node.definitionLine = definitionRecord.location.startLine;
                node.definitionColumn = definitionRecord.location.startColumn;
                addParticipatingFile(instanceFile, inSelectedTop);
                addParticipatingFile(definitionRecord.location.fileName, inSelectedTop);
            } else {
                node.unresolved = true;
                node.unresolvedReason = QStringLiteral("module definition not found");
                if (!moduleType.isEmpty())
                    unresolvedModules.insert(moduleType);
                addParticipatingFile(instanceFile, inSelectedTop);
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
            appendChildren(definitionRecord,
                           node.id,
                           rootNodeId,
                           rootModule,
                           inSelectedTop,
                           modulePath);
        }
    };

    for (const QString& rootModule : std::as_const(roots)) {
        const SemanticSymbolRecord topRecord = modulesByName.value(rootModule);
        if (!topRecord.isValid())
            continue;

        const bool inSelectedTop = selectedTop.isEmpty() || rootModule == selectedTop;
        DesignHierarchyNode topNode;
        topNode.id = nextId();
        topNode.rootId = topNode.id;
        topNode.rootModule = topRecord.name;
        topNode.instanceName = topRecord.name;
        topNode.moduleType = topRecord.name;
        topNode.definitionFile =
            normalizedDesignHierarchyFileName(topRecord.location.fileName);
        topNode.definitionLine = topRecord.location.startLine;
        topNode.definitionColumn = topRecord.location.startColumn;
        topNode.isTop = true;
        topNode.inSelectedTop = inSelectedTop;
        report.nodes.append(topNode);
        addParticipatingFile(topRecord.location.fileName, inSelectedTop);

        appendChildren(topRecord,
                       topNode.id,
                       topNode.id,
                       topRecord.name,
                       inSelectedTop,
                       {});
    }
    report.unresolvedModules = uniqueSortedStringList(unresolvedModules);
    return report;
}
