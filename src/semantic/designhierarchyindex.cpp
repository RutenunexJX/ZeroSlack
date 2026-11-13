#include "hierarchyservice.h"
#include "semanticdependencygraph.h"
#include "semanticindexsnapshot.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <chrono>
#include <algorithm>

namespace {
QString fileKey(const QString& file)
{
    if (file.isEmpty()) return {};
    auto path = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(file).absoluteFilePath()));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
QString digest(const QString& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool definition(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Module
        || record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface;
}
struct Edge {
    QString name, type, target, file;
    int line = 0, column = 0;
    QHash<QString, QStringList> pathsByParent;
    bool syntaxOnly = false;
};
struct Definition {
    SemanticSymbolRecord record;
    QList<Edge> children;
    QSet<QString> paths;
    QList<SemanticSymbolRecord> parameters;
    bool incomplete = false;
};
}

QString HierarchyService::definitionIdentity(const SymbolStableKey& key)
{
    // Source positions are anchors, not identities: inserting a comment must
    // not change expansion, instance selection or graph history.
    return digest(fileKey(key.fileName) + QChar(0x1f) + key.ownerScope + QChar(0x1f)
        + key.symbolName + QChar(0x1f) + QString::number(int(key.declarationKind)));
}

DesignHierarchyReport HierarchyService::automaticDesignHierarchy(
    const QSet<QString>& fileScope, const std::function<bool()>& cancelled) const
{
    const auto report = sharedDesignHierarchy(fileScope, cancelled);
    return report ? *report : DesignHierarchyReport();
}

std::uint64_t HierarchyService::automaticBuildCountForTesting(const SemanticSnapshotToken& token)
{
    if (!token.snapshot) return 0;
    const auto cache = token.snapshot->m_hierarchyCache;
    const std::lock_guard lock(cache->mutex);
    return cache->builds;
}

std::shared_ptr<const DesignHierarchyReport> HierarchyService::sharedDesignHierarchy(
    const QSet<QString>& fileScope, const std::function<bool()>& cancelled) const
{
    const auto token = semanticIndex()->snapshotToken();
    if (!token.snapshot) return {};
    const auto stopped = [&] { return cancelled && cancelled(); };
    if (stopped()) return {};
    QSet<QString> scope;
    for (const auto& file : fileScope) scope.insert(fileKey(file));
    const auto files = token.snapshot->symbolFiles();
    const QSet<QString> publishedFiles(files.cbegin(), files.cend());
    // An explicit complete scope and the publication's full scope are identical.
    // Preserve a disjoint scope as distinct from the unrestricted query.
    if (!scope.isEmpty() && (publishedFiles - scope).isEmpty()) scope.clear();
    QStringList keys = scope.values(); std::sort(keys.begin(), keys.end());
    QString key = QString::number(token.revision) + ':';
    for (const auto& file : keys) key += QString::number(file.size()) + ':' + file;
    const auto cache = token.snapshot->m_hierarchyCache;
    std::unique_lock lock(cache->mutex);
    for (;;) {
        if (stopped()) return {};
        if (const auto found = cache->reports.constFind(key); found != cache->reports.cend()) return found.value();
        if (!cache->running.contains(key)) break;
        cache->changed.wait_for(lock, std::chrono::milliseconds(10));
    }
    cache->running.insert(key); ++cache->builds;
    lock.unlock();
    const auto release = qScopeGuard([&] {
        const std::lock_guard guard(cache->mutex);
        cache->running.remove(key); cache->changed.notify_all();
    });
    auto report = std::make_shared<DesignHierarchyReport>(
        HierarchyService(token).buildAutomaticDesignHierarchy(scope, cancelled));
    if (stopped()) return {};
    lock.lock();
    if (cache->reports.size() >= 4) cache->reports.erase(cache->reports.begin());
    cache->reports.insert(key, report);
    lock.unlock();
    return report;
}

DesignHierarchyReport HierarchyService::buildAutomaticDesignHierarchy(
    const QSet<QString>& fileScope, const std::function<bool()>& cancelled) const
{
    DesignHierarchyReport report;
    report.snapshotGeneration = semanticIndex()->snapshotRevision();
    const auto stopped = [&] { return cancelled && cancelled(); };
    QSet<QString> scope;
    for (const auto& file : fileScope) scope.insert(fileKey(file));
    QHash<QString, Definition> definitions;
    QHash<QString, QStringList> byName;
    QStringList order;
    QSet<QString> errorFiles;
    for (const auto& diagnostic : semanticIndex()->getDiagnostics())
        if (diagnostic.severity == SemanticDiagnostic::Error) errorFiles.insert(fileKey(diagnostic.fileName));
    for (auto kind : {SymbolTaxonomy::DeclarationKind::Module, SymbolTaxonomy::DeclarationKind::Interface}) {
        for (const auto& record : semanticIndex()->getSymbolRecordsByDeclarationKind(kind)) {
            if (stopped()) return {};
            if (!scope.isEmpty() && !scope.contains(fileKey(record.location.fileName))) continue;
            const auto id = definitionIdentity(record.stableKey);
            if (definitions.contains(id)) continue;
            Definition value;
            value.record = record;
            value.paths = QSet<QString>(record.presentation.elaboratedInstancePaths.cbegin(), record.presentation.elaboratedInstancePaths.cend());
            value.incomplete = errorFiles.contains(fileKey(record.location.fileName));
            definitions.insert(id, value); byName[record.name].append(id); order.append(id);
        }
    }
    std::sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
        const auto& left = definitions[a].record;
        const auto& right = definitions[b].record;
        const int name = QString::compare(left.name, right.name, Qt::CaseInsensitive);
        return name ? name < 0 : fileKey(left.location.fileName) < fileKey(right.location.fileName);
    });
    const auto ownerId = [&](const SemanticSymbolRecord& record) {
        if (record.owner.stableKey.isValid()) {
            const auto exact = definitionIdentity(record.owner.stableKey);
            if (definitions.contains(exact)) return exact;
        }
        QString result;
        for (const auto& candidate : byName.value(record.owner.name)) {
            const auto& parent = definitions[candidate].record;
            if (fileKey(parent.location.fileName) != fileKey(record.location.fileName)) continue;
            if (!result.isEmpty()) return QString();
            result = candidate;
        }
        return result;
    };
    for (const auto& parameter : semanticIndex()->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Parameter)) {
        const auto id = ownerId(parameter);
        if (!id.isEmpty()) definitions[id].parameters.append(parameter);
    }
    const auto resolve = [&](const QString& name, const SymbolStableKey& key) {
        if (key.isValid() && definitionIdentity(key) != QString()) {
            const auto id = definitionIdentity(key);
            if (definitions.contains(id) && definitions[id].record.name == name) return id;
        }
        const auto candidates = byName.value(name);
        return candidates.size() == 1 ? candidates.first() : QString();
    };
    QHash<QString, QHash<QString, int>> edgesByName;
    QHash<QString, QSet<QString>> childNames, childAnchors;
    const auto addEdge = [&](const QString& owner, Edge edge) -> int {
        auto& known = edgesByName[owner];
        // A generated name is scope-qualified by Slang. A semantic edge and
        // its syntax fallback share the declaration anchor within this snapshot.
        const auto key = edge.name + QChar(0x1f) + edge.type + QChar(0x1f)
            + fileKey(edge.file) + ':' + QString::number(edge.line) + ':' + QString::number(edge.column);
        if (known.contains(key)) return known.value(key);
        const auto nameKey = edge.name + QChar(0x1f) + edge.type;
        childNames[owner].insert(nameKey);
        childAnchors[owner].insert(nameKey + ':' + QString::number(edge.line));
        const int index = definitions[owner].children.size();
        known.insert(key, index); definitions[owner].children.append(std::move(edge));
        return index;
    };
    for (const auto& record : semanticIndex()->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Instance)) {
        if (stopped()) return {};
        if (!SymbolTaxonomy::isInstanceDeclaration(semanticMetadataForSymbolRecord(record))) continue;
        const auto owner = ownerId(record);
        if (owner.isEmpty()) continue;
        Edge edge;
        edge.name = record.name;
        edge.type = !record.type.resolvedTypeName.isEmpty() ? record.type.resolvedTypeName : record.type.rawTypeText.trimmed();
        if (edge.type.isEmpty()) edge.type = record.type.stableKey.symbolName;
        edge.target = resolve(edge.type, record.type.stableKey);
        edge.file = record.location.fileName; edge.line = record.location.startLine; edge.column = record.location.startColumn;
        const auto& parentPaths = definitions[owner].paths;
        for (const auto& path : record.presentation.elaboratedInstancePaths) {
            auto parent = path;
            while (parent.contains('.')) {
                parent.truncate(parent.lastIndexOf('.'));
                if (parentPaths.contains(parent)) { edge.pathsByParent[parent].append(path); break; }
            }
        }
        addEdge(owner, std::move(edge));
    }
    QHash<QString, SemanticFileDependencyFacts> syntax;
    for (const auto& id : order) {
        if (stopped()) return {};
        const auto parent = definitions[id].record;
        RelationshipQuery query;
        query.symbolStableKey = parent.stableKey; query.outgoing = true;
        query.types = {SymbolRelationshipEngine::INSTANTIATES};
        for (const auto& relationship : relationshipService.findRelationships(query)) {
            const auto& target = relationship.toSymbolRecord;
            // Port references also use Instance as their declaration kind.
            // Only an instance declaration or its resolved definition owns a child.
            if (!definition(target)
                && !SymbolTaxonomy::isInstanceDeclaration(semanticMetadataForSymbolRecord(target))) continue;
            Edge edge;
            edge.type = definition(target) ? target.name : target.type.resolvedTypeName;
            if (edge.type.isEmpty()) edge.type = target.type.rawTypeText;
            edge.name = relationship.toAccessPath.section('@', 0, 0);
            if (edge.name.isEmpty()) edge.name = target.name;
            edge.file = relationship.evidenceRange.fileName;
            edge.line = relationship.evidenceRange.line; edge.column = relationship.evidenceRange.column;
            if (edge.file.isEmpty()) { edge.file = target.location.fileName; edge.line = target.location.startLine; edge.column = target.location.startColumn; }
            edge.target = resolve(edge.type, definition(target) ? target.stableKey : target.type.stableKey);
            const bool known = childNames[id].contains(edge.name + QChar(0x1f) + edge.type);
            if (!known && !edge.type.isEmpty()) addEdge(id, std::move(edge));
        }
        const auto source = parent.location.fileName;
        if (!syntax.contains(source)) syntax.insert(source, SemanticDependencyGraph::extractFacts(
            source, semanticIndex()->getCachedFileContent(source), cancelled));
        definitions[id].incomplete |= syntax[source].parseError;
        for (const auto& fact : syntax[source].moduleInstantiations) {
            if (fact.ownerName != parent.name) continue;
            if (childAnchors[id].contains(fact.instanceName + QChar(0x1f) + fact.targetName
                + ':' + QString::number(fact.sourceLine))) continue;
            Edge edge;
            edge.name = fact.instanceName; edge.type = fact.targetName;
            edge.target = resolve(edge.type, {}); edge.file = source;
            edge.line = fact.sourceLine; edge.column = fact.sourceColumn; edge.syntaxOnly = true;
            addEdge(id, std::move(edge));
        }
        auto& children = definitions[id].children;
        std::stable_sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
            if (a.line != b.line) return a.line < b.line;
            if (a.column != b.column) return a.column < b.column;
            return a.name < b.name;
        });
    }
    QSet<QString> incoming, reachable;
    for (const auto& value : std::as_const(definitions))
        for (const auto& edge : value.children) if (!edge.target.isEmpty()) incoming.insert(edge.target);
    QStringList roots;
    const auto markReachable = [&](const QString& root) {
        QStringList pending{root};
        while (!pending.isEmpty()) {
            auto id = pending.takeLast();
            if (reachable.contains(id)) continue;
            reachable.insert(id);
            for (const auto& edge : definitions[id].children)
                if (!edge.target.isEmpty()) pending.append(edge.target);
        }
    };
    for (const auto& id : order) if (!incoming.contains(id)) { roots.append(id); markReachable(id); }
    QSet<QString> cycleRoots;
    for (const auto& id : order) if (!reachable.contains(id)) {
        roots.append(id); cycleRoots.insert(id); markReachable(id);
    }
    const auto configuration = [&](const Definition& value, const QString& path, bool actual) {
        QStringList values{actual ? QStringLiteral("elaborated") : QStringLiteral("definition")};
        for (const auto& parameter : value.parameters) {
            const auto info = actual ? parameter.presentation.instanceInfoByPath.value(path)
                                     : parameter.presentation.defaultInfo;
            values.append(parameter.name + '=' + info.valueText);
        }
        std::sort(values.begin(), values.end());
        return digest(values.join(QChar(0x1f)));
    };
    QCryptographicHash fingerprint(QCryptographicHash::Sha256);
    struct Pending { QString definitionId; DesignHierarchyNode node; QSet<QString> ancestors; };
    QList<Pending> pending;
    for (auto it = roots.crbegin(); it != roots.crend(); ++it) {
        const auto& value = definitions[*it];
        DesignHierarchyNode node;
        node.isTop = true; node.instanceName = node.instancePath = node.moduleType = node.rootModule = value.record.name;
        node.projectId = *it; node.expanded = false;
        if (cycleRoots.contains(*it)) node.status = QStringLiteral("Cyclic component; no independent root");
        else if (value.incomplete) node.status = QStringLiteral("Analysis incomplete; root ownership is provisional");
        else node.status = QStringLiteral("Independent definition");
        if (byName[value.record.name].size() > 1) node.status = QStringLiteral("Multiple definitions share this name");
        pending.append({*it, node, {}});
    }
    while (!pending.isEmpty()) {
        if (stopped()) return {};
        auto work = pending.takeLast();
        auto& node = work.node;
        if (!work.definitionId.isEmpty()) {
            const auto& value = definitions[work.definitionId];
            node.definitionId = work.definitionId; node.definitionKey = value.record.stableKey;
            node.definitionFile = value.record.location.fileName;
            node.definitionLine = value.record.location.startLine; node.definitionColumn = value.record.location.startColumn;
            node.interfaceLike = value.record.declarationKind == SymbolTaxonomy::DeclarationKind::Interface;
            node.elaborated = value.paths.contains(node.instancePath);
            node.configurationId = configuration(value, node.instancePath, node.elaborated);
            if (!node.elaborated && node.status.isEmpty()) node.status = QStringLiteral("Static definition; instance elaboration unavailable");
            if (value.incomplete) node.status = QStringLiteral("Analysis incomplete");
        }
        if (node.isTop) {
            node.rootId = digest(node.definitionId + QChar(0x1f) + node.instancePath + QChar(0x1f) + node.configurationId);
            node.id = node.rootId;
            report.rootModules.append(node.moduleType);
        } else node.id = digest(node.rootId + QChar(0x1f) + node.instancePath + QChar(0x1f) + node.configurationId);
        if (report.nodeById.contains(node.id)) continue;
        const int position = report.nodes.size();
        report.nodeById.insert(node.id, position);
        report.childrenById[node.parentId].append(position);
        report.nodesByPath[node.instancePath].append(position);
        report.nodes.append(node);
        fingerprint.addData(node.id.toUtf8()); fingerprint.addData(node.parentId.toUtf8());
        if (!node.definitionFile.isEmpty()) report.participatingFiles.insert(node.definitionFile);
        if (!node.instanceFile.isEmpty()) report.participatingFiles.insert(node.instanceFile);
        if (work.definitionId.isEmpty()) { report.unresolvedModules.append(node.moduleType); continue; }
        const bool cycle = work.ancestors.contains(work.definitionId) && !node.elaborated;
        if (cycle) {
            report.nodes.last().unresolved = true;
            report.nodes.last().unresolvedReason = QStringLiteral("Cyclic instantiation");
            continue;
        }
        work.ancestors.insert(work.definitionId);
        const auto& children = definitions[work.definitionId].children;
        for (auto it = children.crbegin(); it != children.crend(); ++it) {
            const auto& edge = *it;
            QStringList paths = edge.pathsByParent.value(node.instancePath);
            // Known specialization excludes inactive generate branches. Syntax
            // fallback remains visible only where elaboration is unavailable.
            if (paths.isEmpty() && node.elaborated && !edge.pathsByParent.isEmpty()) continue;
            if (paths.isEmpty()) paths.append(node.instancePath + '.' + edge.name);
            for (auto path = paths.crbegin(); path != paths.crend(); ++path) {
                DesignHierarchyNode child;
                child.parentId = node.id; child.rootId = node.rootId; child.rootModule = node.rootModule;
                child.projectId = node.projectId; child.instancePath = *path;
                child.instanceName = path->mid(node.instancePath.size() + 1); child.moduleType = edge.type;
                child.instanceFile = edge.file; child.instanceLine = edge.line; child.instanceColumn = edge.column;
                if (edge.target.isEmpty()) {
                    child.unresolved = true;
                    child.unresolvedReason = byName.value(edge.type).size() > 1
                        ? QStringLiteral("Ambiguous definition") : QStringLiteral("Definition not found");
                } else if (edge.syntaxOnly && node.elaborated) child.status = QStringLiteral("Not present in this elaboration; declared structure");
                pending.append({edge.target, child, work.ancestors});
            }
        }
    }
    report.unresolvedModules.removeDuplicates();
    report.structureFingerprint = fingerprint.result();
    return report;
}
