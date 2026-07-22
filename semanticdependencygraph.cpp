#include "semanticdependencygraph.h"

#include "tsdocument.h"

#include <QDir>
#include <QFileInfo>
#include <QQueue>

#include <algorithm>
#include <cstring>

namespace {
QString typeOf(TSNode node)
{
    const char* type = ts_node_type(node);
    return type ? QString::fromLatin1(type) : QString();
}

QString textOf(const QString& text, TSNode node)
{
    const int start = static_cast<int>(ts_node_start_byte(node) / 2u);
    const int end = static_cast<int>(ts_node_end_byte(node) / 2u);
    if (start < 0 || end < start || start > text.size())
        return QString();
    return text.mid(start, qMin(end, text.size()) - start);
}

bool identifierType(const QString& type)
{
    return type == QLatin1String("simple_identifier")
        || type == QLatin1String("escaped_identifier")
        || type == QLatin1String("text_macro_identifier");
}

bool hasAncestorType(TSNode node, const char* expected)
{
    node = ts_node_parent(node);
    while (!ts_node_is_null(node)) {
        const char* type = ts_node_type(node);
        if (type && std::strcmp(type, expected) == 0)
            return true;
        node = ts_node_parent(node);
    }
    return false;
}

QString firstIdentifier(const QString& text, TSNode node)
{
    if (ts_node_is_null(node))
        return QString();
    if (identifierType(typeOf(node)))
        return textOf(text, node).trimmed();

    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        const QString result = firstIdentifier(
            text, ts_node_named_child(node, i));
        if (!result.isEmpty())
            return result;
    }
    return QString();
}

QString identifierField(const QString& text,
                        TSNode node,
                        const char* fieldName)
{
    const TSNode field = ts_node_child_by_field_name(
        node, fieldName, static_cast<uint32_t>(std::strlen(fieldName)));
    return ts_node_is_null(field) ? QString()
                                  : firstIdentifier(text, field);
}

bool hasScopeResolutionToken(TSNode node)
{
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        const TSNode child = ts_node_child(node, i);
        const char* type = ts_node_type(child);
        if (type && std::strcmp(type, "::") == 0)
            return true;
        if (hasScopeResolutionToken(child))
            return true;
    }
    return false;
}

QString semanticDeclarationIdentifier(const QString& text,
                                      TSNode node,
                                      const QString& type)
{
    if (type == QLatin1String("module_declaration")
        || type == QLatin1String("interface_declaration")
        || type == QLatin1String("package_declaration")
        || type == QLatin1String("function_declaration")
        || type == QLatin1String("function_body_declaration")
        || type == QLatin1String("task_declaration")
        || type == QLatin1String("task_body_declaration")) {
        const QString name = identifierField(text, node, "name");
        if (!name.isEmpty())
            return name;
    }
    if (type == QLatin1String("type_declaration")) {
        const QString name = identifierField(text, node, "type_name");
        if (!name.isEmpty())
            return name;
    }
    if (type == QLatin1String("module_instantiation")
        || type == QLatin1String("interface_instantiation")
        || type == QLatin1String("program_instantiation")) {
        const QString name = identifierField(text, node, "instance_type");
        if (!name.isEmpty())
            return name;
    }
    return firstIdentifier(text, node);
}

QString includeName(const QString& text, TSNode node)
{
    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        const TSNode child = ts_node_named_child(node, i);
        const QString type = typeOf(child);
        if (type.contains(QLatin1String("string"))
            || type.contains(QLatin1String("path"))) {
            QString value = textOf(text, child).trimmed();
            if (value.size() >= 2
                && ((value.front() == QLatin1Char('"')
                     && value.back() == QLatin1Char('"'))
                    || (value.front() == QLatin1Char('<')
                        && value.back() == QLatin1Char('>')))) {
                value = value.mid(1, value.size() - 2);
            }
            if (!value.isEmpty())
                return value;
        }
        const QString nested = includeName(text, child);
        if (!nested.isEmpty())
            return nested;
    }

    // The grammar represents some compiler-directive paths as anonymous
    // tokens. This fallback is scoped to an AST-confirmed include node.
    QString value = textOf(text, node).trimmed();
    const int quoteStart = value.indexOf(QLatin1Char('"'));
    const int quoteEnd = value.lastIndexOf(QLatin1Char('"'));
    if (quoteStart >= 0 && quoteEnd > quoteStart)
        return value.mid(quoteStart + 1, quoteEnd - quoteStart - 1);
    const int angleStart = value.indexOf(QLatin1Char('<'));
    const int angleEnd = value.lastIndexOf(QLatin1Char('>'));
    if (angleStart >= 0 && angleEnd > angleStart)
        return value.mid(angleStart + 1, angleEnd - angleStart - 1);
    return QString();
}

bool apiDeclarationType(const QString& type)
{
    static const QSet<QString> types = {
        QStringLiteral("typedef"),
        QStringLiteral("type_declaration"),
        QStringLiteral("enum_name_declaration"),
        QStringLiteral("struct_union"),
        QStringLiteral("function_declaration"),
        QStringLiteral("function_body_declaration"),
        QStringLiteral("function_prototype"),
        QStringLiteral("task_declaration"),
        QStringLiteral("task_body_declaration"),
        QStringLiteral("task_prototype"),
        QStringLiteral("parameter_declaration"),
        QStringLiteral("local_parameter_declaration"),
        QStringLiteral("type_parameter_declaration"),
        QStringLiteral("modport_declaration"),
    };
    return types.contains(type);
}

void collectFacts(const QString& text,
                  TSNode node,
                  SemanticFileDependencyFacts* facts)
{
    if (!facts || ts_node_is_null(node))
        return;
    const QString type = typeOf(node);
    const QString identifier =
        semanticDeclarationIdentifier(text, node, type);

    if (type == QLatin1String("module_declaration")
        || type == QLatin1String("interface_declaration")) {
        if (!identifier.isEmpty())
            facts->moduleDeclarations.insert(identifier);
    } else if (type == QLatin1String("package_declaration")) {
        if (!identifier.isEmpty())
            facts->packageDeclarations.insert(identifier);
    } else if (type == QLatin1String("text_macro_definition")) {
        if (!identifier.isEmpty())
            facts->macroDefinitions.insert(identifier);
    } else if (type == QLatin1String("module_instantiation")
               || type == QLatin1String("interface_instantiation")
               || type == QLatin1String("program_instantiation")) {
        if (!identifier.isEmpty())
            facts->instantiatedModules.insert(identifier);
    } else if (type == QLatin1String("package_import_item")
               || type == QLatin1String("package_scope")) {
        if (!identifier.isEmpty())
            facts->importedPackages.insert(identifier);
    } else if ((type == QLatin1String("class_type")
                || type == QLatin1String("class_scope"))
               && hasScopeResolutionToken(node)) {
        // The grammar represents package-qualified type names such as
        // p::word_t as class_type. Record the AST-proven qualifier and later
        // bind it only when it matches a declared package.
        if (!identifier.isEmpty())
            facts->importedPackages.insert(identifier);
    } else if (type == QLatin1String("text_macro_usage")) {
        if (!identifier.isEmpty())
            facts->macroUses.insert(identifier);
    } else if (type == QLatin1String("include_compiler_directive")
               || type == QLatin1String("include_statement")) {
        const QString name = includeName(text, node);
        if (!name.isEmpty())
            facts->includeNames.append(name);
    }

    const bool macroConditionIdentifier =
        (type == QLatin1String("simple_identifier")
         || type == QLatin1String("escaped_identifier"))
        && hasAncestorType(node, "ifdef_condition");
    if (macroConditionIdentifier) {
        const QString macroName = textOf(text, node).trimmed();
        if (!macroName.isEmpty())
            facts->macroUses.insert(macroName);
    }

    if (apiDeclarationType(type) && !identifier.isEmpty())
        facts->apiDeclarations.insert(identifier);
    if (identifierType(type) && !macroConditionIdentifier) {
        const QString reference = textOf(text, node).trimmed();
        if (!reference.isEmpty())
            facts->symbolReferences.insert(reference);
    }

    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collectFacts(text, ts_node_named_child(node, i), facts);
}

template <typename T>
void rememberDeclarations(QHash<QString, QSet<QString>>* filesByName,
                          const QString& file,
                          const T& names)
{
    if (!filesByName)
        return;
    for (const QString& name : names) {
        if (!name.isEmpty())
            (*filesByName)[name].insert(file);
    }
}
}

QString SemanticDependencyGraph::normalizedPath(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString path = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

QString SemanticDependencyGraph::projectKey(const ProjectSnapshot& project)
{
    QStringList files;
    for (const QString& file : project.systemVerilogFiles)
        files.append(normalizedPath(file));
    QStringList includes;
    for (const QString& include : project.includeDirs)
        includes.append(normalizedPath(include));
    QStringList defineKeys = project.defines.keys();
    defineKeys.sort(Qt::CaseSensitive);
    QStringList defineParts;
    for (const QString& key : defineKeys)
        defineParts.append(key + QLatin1Char('=') + project.defines.value(key));
    return QStringLiteral("%1\n%2\n%3\n%4\n%5")
        .arg(normalizedPath(project.workspaceRoot),
             files.join(QLatin1Char('\n')),
             includes.join(QLatin1Char('\n')),
             defineParts.join(QLatin1Char('\n')),
             project.topModule);
}

SemanticFileDependencyFacts SemanticDependencyGraph::extractFacts(
    const QString& fileName,
    const QString& content)
{
    SemanticFileDependencyFacts facts;
    facts.fileName = fileName;
    TSDocument document;
    document.setText(content);
    facts.parseError = document.hasError();
    collectFacts(content, document.rootNode(), &facts);
    facts.includeNames.removeDuplicates();
    return facts;
}

SemanticDependencyGraph SemanticDependencyGraph::build(
    const ProjectSnapshot& project,
    const QHash<QString, QString>& contents)
{
    SemanticDependencyGraph graph;
    graph.graphProject = project;
    graph.projectIdentity = projectKey(project);
    for (const QString& fileName : project.systemVerilogFiles) {
        const QString key = normalizedPath(fileName);
        if (key.isEmpty())
            continue;
        graph.originalPathByKey.insert(key, fileName);
        QString content;
        for (auto it = contents.constBegin(); it != contents.constEnd(); ++it) {
            if (normalizedPath(it.key()) == key) {
                content = it.value();
                break;
            }
        }
        graph.factsByFile.insert(key, extractFacts(fileName, content));
    }
    graph.rebuildEdges();
    return graph;
}

SemanticDependencyGraph SemanticDependencyGraph::withUpdatedFile(
    const ProjectSnapshot& project,
    const QString& fileName,
    const QString& content) const
{
    if (!isValidFor(project)) {
        QHash<QString, QString> contents;
        contents.insert(fileName, content);
        return build(project, contents);
    }

    SemanticDependencyGraph result = *this;
    const QString key = normalizedPath(fileName);
    if (!key.isEmpty()) {
        result.originalPathByKey.insert(key, fileName);
        result.factsByFile.insert(key, extractFacts(fileName, content));
    }
    result.rebuildEdges();
    return result;
}

bool SemanticDependencyGraph::isValidFor(const ProjectSnapshot& project) const
{
    return !projectIdentity.isEmpty()
        && projectIdentity == projectKey(project)
        && factsByFile.size() == project.systemVerilogFiles.size();
}

bool SemanticDependencyGraph::hasParseError(const QString& fileName) const
{
    return factsByFile.value(normalizedPath(fileName)).parseError;
}

void SemanticDependencyGraph::addDependency(
    const QString& dependentFile,
    const QString& dependencyFile,
    SemanticDependencyKind kind)
{
    const QString from = normalizedPath(dependentFile);
    const QString to = normalizedPath(dependencyFile);
    if (from.isEmpty() || to.isEmpty() || from == to)
        return;
    dependencies[from][to] |= kind;
    dependents[to][from] |= kind;
}

QString SemanticDependencyGraph::resolveInclude(
    const QString& sourceFile,
    const QString& includeName) const
{
    if (includeName.isEmpty())
        return QString();
    QStringList candidates;
    candidates.append(QFileInfo(sourceFile).dir().absoluteFilePath(includeName));
    for (const QString& directory : graphProject.includeDirs)
        candidates.append(QDir(directory).absoluteFilePath(includeName));
    for (const QString& candidate : candidates) {
        const QString key = normalizedPath(candidate);
        if (factsByFile.contains(key))
            return originalPathByKey.value(key, candidate);
    }
    return QString();
}

void SemanticDependencyGraph::rebuildEdges()
{
    dependencies.clear();
    dependents.clear();
    topFile.clear();

    QHash<QString, QSet<QString>> modules;
    QHash<QString, QSet<QString>> packages;
    QHash<QString, QSet<QString>> macros;
    QHash<QString, QSet<QString>> api;
    for (auto it = factsByFile.constBegin(); it != factsByFile.constEnd(); ++it) {
        rememberDeclarations(&modules, it.key(), it->moduleDeclarations);
        rememberDeclarations(&packages, it.key(), it->packageDeclarations);
        rememberDeclarations(&macros, it.key(), it->macroDefinitions);
        rememberDeclarations(&api, it.key(), it->apiDeclarations);
    }

    if (!graphProject.topModule.isEmpty()) {
        const QSet<QString> topFiles = modules.value(graphProject.topModule);
        if (!topFiles.isEmpty()) {
            const QString key = *topFiles.constBegin();
            topFile = originalPathByKey.value(key, key);
        }
    }

    for (auto it = factsByFile.constBegin(); it != factsByFile.constEnd(); ++it) {
        const QString dependentKey = it.key();
        const QString dependentFile = originalPathByKey.value(dependentKey,
                                                               it->fileName);
        for (const QString& name : it->includeNames) {
            const QString dependency = resolveInclude(dependentFile, name);
            if (!dependency.isEmpty())
                addDependency(dependentFile, dependency,
                              SemanticDependencyKind::Include);
        }
        for (const QString& name : it->macroUses) {
            for (const QString& dependencyKey : macros.value(name)) {
                addDependency(dependentFile,
                              originalPathByKey.value(dependencyKey,
                                                      dependencyKey),
                              SemanticDependencyKind::Macro);
            }
        }
        for (const QString& name : it->importedPackages) {
            for (const QString& dependencyKey : packages.value(name)) {
                addDependency(dependentFile,
                              originalPathByKey.value(dependencyKey,
                                                      dependencyKey),
                              SemanticDependencyKind::Package);
            }
        }
        for (const QString& name : it->instantiatedModules) {
            for (const QString& dependencyKey : modules.value(name)) {
                addDependency(dependentFile,
                              originalPathByKey.value(dependencyKey,
                                                      dependencyKey),
                              SemanticDependencyKind::Instantiation);
            }
        }
        for (const QString& name : it->symbolReferences) {
            for (const QString& dependencyKey : api.value(name)) {
                addDependency(dependentFile,
                              originalPathByKey.value(dependencyKey,
                                                      dependencyKey),
                              SemanticDependencyKind::TypeOrApi);
            }
        }
    }

    // Preserve active-top reachability as an explicit dependency kind. The
    // ordinary instantiation edges remain the source of truth; this derived
    // closure lets planners and diagnostics distinguish active-design impact
    // from unrelated module declarations without reparsing hierarchy in UI
    // code.
    if (!topFile.isEmpty()) {
        const QString topKey = normalizedPath(topFile);
        QSet<QString> activeDescendants;
        QQueue<QString> queue;
        queue.enqueue(topKey);
        while (!queue.isEmpty()) {
            const QString current = queue.dequeue();
            const auto edges = dependencies.value(current);
            for (auto edge = edges.constBegin(); edge != edges.constEnd(); ++edge) {
                if (!(edge.value() & SemanticDependencyKind::Instantiation)
                    || activeDescendants.contains(edge.key())) {
                    continue;
                }
                activeDescendants.insert(edge.key());
                queue.enqueue(edge.key());
            }
        }
        for (const QString& descendant : std::as_const(activeDescendants)) {
            addDependency(topFile,
                          originalPathByKey.value(descendant, descendant),
                          SemanticDependencyKind::ActiveTop);
        }
    }
}

QStringList SemanticDependencyGraph::orderedFiles(
    const QSet<QString>& normalizedFiles) const
{
    QStringList result;
    QSet<QString> remaining = normalizedFiles;
    for (const QString& file : graphProject.systemVerilogFiles) {
        const QString key = normalizedPath(file);
        if (remaining.remove(key))
            result.append(file);
    }
    QStringList extras;
    for (const QString& key : std::as_const(remaining))
        extras.append(originalPathByKey.value(key, key));
    extras.sort(Qt::CaseInsensitive);
    result.append(extras);
    return result;
}

QStringList SemanticDependencyGraph::dependenciesOf(
    const QStringList& files,
    SemanticDependencyKinds kinds,
    bool recursive) const
{
    QSet<QString> result;
    QQueue<QString> queue;
    for (const QString& file : files)
        queue.enqueue(normalizedPath(file));
    QSet<QString> visited;
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        if (current.isEmpty() || visited.contains(current))
            continue;
        visited.insert(current);
        const auto edges = dependencies.value(current);
        for (auto it = edges.constBegin(); it != edges.constEnd(); ++it) {
            if (!(it.value() & kinds))
                continue;
            if (!result.contains(it.key())) {
                result.insert(it.key());
                if (recursive)
                    queue.enqueue(it.key());
            }
        }
    }
    return orderedFiles(result);
}

QStringList SemanticDependencyGraph::dependentsOf(
    const QStringList& files,
    SemanticDependencyKinds kinds,
    bool recursive) const
{
    QSet<QString> result;
    QQueue<QString> queue;
    for (const QString& file : files)
        queue.enqueue(normalizedPath(file));
    QSet<QString> visited;
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        if (current.isEmpty() || visited.contains(current))
            continue;
        visited.insert(current);
        const auto edges = dependents.value(current);
        for (auto it = edges.constBegin(); it != edges.constEnd(); ++it) {
            if (!(it.value() & kinds))
                continue;
            if (!result.contains(it.key())) {
                result.insert(it.key());
                if (recursive)
                    queue.enqueue(it.key());
            }
        }
    }
    return orderedFiles(result);
}
