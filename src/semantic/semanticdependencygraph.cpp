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

QList<TSNode> directNamedChildrenOf(TSNode node)
{
    QList<TSNode> result;
    if (ts_node_is_null(node))
        return result;
    const uint32_t count = ts_node_named_child_count(node);
    result.reserve(static_cast<qsizetype>(count));
    for (uint32_t i = 0; i < count; ++i)
        result.append(ts_node_named_child(node, i));
    return result;
}

TSNode firstDirectNamedChildOfType(TSNode node, const char* expected)
{
    for (const TSNode child : directNamedChildrenOf(node)) {
        const char* type = ts_node_type(child);
        if (type && std::strcmp(type, expected) == 0)
            return child;
    }
    return {};
}

TSNode firstDirectIdentifier(TSNode node)
{
    for (const TSNode child : directNamedChildrenOf(node)) {
        if (identifierType(typeOf(child)))
            return child;
    }
    return {};
}

bool containsNodeType(TSNode node, const char* expected)
{
    if (ts_node_is_null(node))
        return false;
    const char* type = ts_node_type(node);
    if (type && std::strcmp(type, expected) == 0)
        return true;
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        if (containsNodeType(ts_node_child(node, i), expected))
            return true;
    }
    return false;
}

bool collectAssociations(
    const QString& text,
    TSNode container,
    const char* namedType,
    const char* orderedType,
    const char* nameField,
    QList<SemanticModuleAssociationFact>* output,
    QString* failureReason)
{
    if (ts_node_is_null(container))
        return true;
    if (!output || ts_node_has_error(container)) {
        if (failureReason)
            *failureReason = QStringLiteral("association syntax is incomplete");
        return false;
    }
    if (containsNodeType(container, ".*")) {
        if (failureReason)
            *failureReason = QStringLiteral("wildcard associations cannot define a safe stub contract");
        return false;
    }

    bool sawNamed = false;
    bool sawOrdered = false;
    int position = 0;
    for (const TSNode child : directNamedChildrenOf(container)) {
        const QString childType = typeOf(child);
        const bool named = childType == QLatin1String(namedType);
        const bool ordered = childType == QLatin1String(orderedType);
        if (!named && !ordered) {
            if (failureReason)
                *failureReason = QStringLiteral("association list contains an unsupported syntax node");
            return false;
        }
        sawNamed = sawNamed || named;
        sawOrdered = sawOrdered || ordered;
        if (sawNamed && sawOrdered) {
            if (failureReason)
                *failureReason = QStringLiteral("named and positional associations are mixed");
            return false;
        }
        if (ts_node_has_error(child)) {
            if (failureReason)
                *failureReason = QStringLiteral("association syntax is incomplete");
            return false;
        }

        SemanticModuleAssociationFact fact;
        fact.position = position++;
        if (named) {
            TSNode formal{};
            if (nameField) {
                formal = ts_node_child_by_field_name(
                    child,
                    nameField,
                    static_cast<uint32_t>(std::strlen(nameField)));
            }
            if (ts_node_is_null(formal))
                formal = firstDirectIdentifier(child);
            fact.name = textOf(text, formal).trimmed();
            if (fact.name.isEmpty()) {
                if (failureReason)
                    *failureReason = QStringLiteral("a named association has no formal name");
                return false;
            }
        }
        output->append(std::move(fact));
    }
    return true;
}

void appendFailure(QString* destination, const QString& failure)
{
    if (!destination || failure.isEmpty())
        return;
    if (!destination->isEmpty())
        destination->append(QStringLiteral("; "));
    destination->append(failure);
}

void collectInstantiationFacts(
    const QString& text,
    TSNode node,
    const QString& ownerName,
    SemanticFileDependencyFacts* facts)
{
    if (!facts)
        return;
    const QString nodeType = typeOf(node);
    const QString targetName = identifierField(text, node, "instance_type");
    if (!targetName.isEmpty())
        facts->instantiatedModules.insert(targetName);

    QString constructKind;
    if (nodeType == QLatin1String("module_instantiation"))
        constructKind = QStringLiteral("module");
    else if (nodeType == QLatin1String("interface_instantiation"))
        constructKind = QStringLiteral("interface");
    else
        constructKind = QStringLiteral("program");

    QList<SemanticModuleAssociationFact> parameterAssociations;
    QString parameterFailure;
    const TSNode parameterValue = firstDirectNamedChildOfType(
        node, "parameter_value_assignment");
    const TSNode parameterList = firstDirectNamedChildOfType(
        parameterValue, "list_of_parameter_value_assignments");
    const bool parameterComplete = collectAssociations(
        text,
        parameterList,
        "named_parameter_assignment",
        "ordered_parameter_assignment",
        nullptr,
        &parameterAssociations,
        &parameterFailure);

    QList<TSNode> hierarchies;
    for (const TSNode child : directNamedChildrenOf(node)) {
        if (typeOf(child) == QLatin1String("hierarchical_instance"))
            hierarchies.append(child);
    }
    if (hierarchies.isEmpty())
        hierarchies.append(node);

    for (const TSNode hierarchy : std::as_const(hierarchies)) {
        SemanticModuleInstantiationFact fact;
        fact.ownerName = ownerName;
        fact.targetName = targetName;
        fact.constructKind = constructKind;
        const TSPoint point = ts_node_start_point(hierarchy);
        fact.sourceLine = static_cast<int>(point.row) + 1;
        fact.sourceColumn = static_cast<int>(point.column / 2u) + 1;
        fact.parameterAssociations = parameterAssociations;

        const TSNode nameContainer = firstDirectNamedChildOfType(
            hierarchy, "name_of_instance");
        const TSNode nameNode = firstDirectIdentifier(nameContainer);
        fact.instanceName = textOf(text, nameNode).trimmed();

        QString portFailure;
        const TSNode ports = firstDirectNamedChildOfType(
            hierarchy, "list_of_port_connections");
        const bool portsComplete = collectAssociations(
            text,
            ports,
            "named_port_connection",
            "ordered_port_connection",
            "port_name",
            &fact.portAssociations,
            &portFailure);

        fact.syntaxComplete = !ts_node_has_error(node)
            && !ts_node_has_error(hierarchy)
            && !ownerName.isEmpty()
            && !targetName.isEmpty()
            && !fact.instanceName.isEmpty()
            && parameterComplete
            && portsComplete;
        if (ownerName.isEmpty())
            appendFailure(&fact.failureReason, QStringLiteral("instantiation has no enclosing design declaration"));
        if (targetName.isEmpty())
            appendFailure(&fact.failureReason, QStringLiteral("instantiation type is missing"));
        if (fact.instanceName.isEmpty())
            appendFailure(&fact.failureReason, QStringLiteral("instance name is missing"));
        if (ts_node_has_error(node) || ts_node_has_error(hierarchy))
            appendFailure(&fact.failureReason, QStringLiteral("instantiation syntax is incomplete"));
        appendFailure(&fact.failureReason, parameterFailure);
        appendFailure(&fact.failureReason, portFailure);
        facts->moduleInstantiations.append(std::move(fact));
    }
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
        || type == QLatin1String("program_declaration")
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
                  SemanticFileDependencyFacts* facts,
                  const QString& ownerName = QString(),
                  bool insideIfdefCondition = false,
                  const std::function<bool()>& cancelled = {})
{
    if (!facts || ts_node_is_null(node) || (cancelled && cancelled()))
        return;
    const QString type = typeOf(node);
    const bool apiDeclaration = apiDeclarationType(type);
    const bool needsIdentifier = apiDeclaration
        || type == QLatin1String("module_declaration")
        || type == QLatin1String("interface_declaration")
        || type == QLatin1String("program_declaration")
        || type == QLatin1String("package_declaration")
        || type == QLatin1String("text_macro_definition")
        || type == QLatin1String("package_import_item")
        || type == QLatin1String("package_scope")
        || type == QLatin1String("class_type")
        || type == QLatin1String("class_scope")
        || type == QLatin1String("text_macro_usage");
    const QString identifier = needsIdentifier
        ? semanticDeclarationIdentifier(text, node, type) : QString();

    QString nestedOwner = ownerName;
    if (type == QLatin1String("module_declaration")
        || type == QLatin1String("interface_declaration")
        || type == QLatin1String("program_declaration")) {
        if (!identifier.isEmpty())
            facts->moduleDeclarations.insert(identifier);
        nestedOwner = identifier;
    } else if (type == QLatin1String("package_declaration")) {
        if (!identifier.isEmpty())
            facts->packageDeclarations.insert(identifier);
    } else if (type == QLatin1String("text_macro_definition")) {
        if (!identifier.isEmpty())
            facts->macroDefinitions.insert(identifier);
    } else if (type == QLatin1String("module_instantiation")
               || type == QLatin1String("interface_instantiation")
               || type == QLatin1String("program_instantiation")) {
        collectInstantiationFacts(text, node, ownerName, facts);
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
        && insideIfdefCondition;
    if (macroConditionIdentifier) {
        const QString macroName = textOf(text, node).trimmed();
        if (!macroName.isEmpty())
            facts->macroUses.insert(macroName);
    }

    if (apiDeclaration && !identifier.isEmpty())
        facts->apiDeclarations.insert(identifier);
    if (identifierType(type) && !macroConditionIdentifier) {
        const QString reference = textOf(text, node).trimmed();
        if (!reference.isEmpty())
            facts->symbolReferences.insert(reference);
    }

    // A cursor carries sibling/ancestor state. Looking up each child by index
    // and rediscovering every identifier's parents repeatedly scans wide RTL
    // declaration lists, making dependency extraction quadratic.
    const bool nestedIfdef = insideIfdefCondition
        || type == QLatin1String("ifdef_condition");
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    if (ts_tree_cursor_goto_first_child(&cursor)) {
        do {
            if (cancelled && cancelled()) break;
            const TSNode child = ts_tree_cursor_current_node(&cursor);
            if (ts_node_is_named(child))
                collectFacts(text, child, facts, nestedOwner, nestedIfdef, cancelled);
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
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
    return project.semanticIdentity();
}

SemanticFileDependencyFacts SemanticDependencyGraph::extractFacts(
    const QString& fileName,
    const QString& content, const std::function<bool()>& cancelled)
{
    SemanticFileDependencyFacts facts;
    facts.fileName = fileName;
    TSDocument document;
    if (!document.setText(content, cancelled)) {
        facts.parseError = true;
        return facts;
    }
    facts.parseError = document.hasError();
    collectFacts(content, document.rootNode(), &facts, {}, false, cancelled);
    facts.includeNames.removeDuplicates();
    return facts;
}

SemanticDependencyGraph SemanticDependencyGraph::build(
    const ProjectSnapshot& project,
    const QHash<QString, QString>& contents, const std::function<bool()>& cancelled)
{
    SemanticDependencyGraph graph;
    graph.graphProject = project;
    graph.projectIdentity = projectKey(project);
    for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
        const QString key = normalizedPath(it.key());
        if (key.isEmpty())
            continue;
        if (cancelled && cancelled()) return {};
        graph.originalPathByKey.insert(key, it.key());
        graph.factsByFile.insert(key, extractFacts(it.key(), it.value(), cancelled));
    }
    if (cancelled && cancelled()) return {};
    graph.rebuildEdges();
    return graph;
}

SemanticDependencyGraph SemanticDependencyGraph::withUpdatedFile(
    const ProjectSnapshot& project,
    const QString& fileName,
    const QString& content) const
{
    return withUpdatedFiles(project, {{fileName, content}}, {fileName});
}

SemanticDependencyGraph SemanticDependencyGraph::withUpdatedFiles(
    const ProjectSnapshot& project, const QHash<QString, QString>& contents,
    const QStringList& changedFiles, const std::function<bool()>& cancelled) const
{
    if (!isValidFor(project))
        return build(project, contents, cancelled);
    SemanticDependencyGraph result = *this;
    QSet<QString> affected;
    bool topChanged = false;
    for (const QString& file : changedFiles) {
        if (cancelled && cancelled()) return {};
        const QString key = normalizedPath(file);
        const auto old = result.factsByFile.value(key);
        auto content = contents.constFind(key);
        if (content == contents.cend())
            content = contents.constFind(file);
        const auto next = content == contents.cend()
            ? SemanticFileDependencyFacts{} : extractFacts(file, content.value(), cancelled);
        QSet<QString> declarations;
        if (old.moduleDeclarations != next.moduleDeclarations)
            declarations |= old.moduleDeclarations | next.moduleDeclarations;
        if (old.packageDeclarations != next.packageDeclarations)
            declarations |= old.packageDeclarations | next.packageDeclarations;
        if (old.macroDefinitions != next.macroDefinitions)
            declarations |= old.macroDefinitions | next.macroDefinitions;
        if (old.apiDeclarations != next.apiDeclarations)
            declarations |= old.apiDeclarations | next.apiDeclarations;
        for (const QString& name : declarations)
            for (const auto& user : result.usersByName.value(name)) affected.insert(user);
        topChanged |= old.moduleDeclarations != next.moduleDeclarations
            || old.instantiatedModules != next.instantiatedModules;
        if (old.includeNames != next.includeNames)
            result.observedIncludes.remove(key);
        // Literal include users may still refer to a removed provider until
        // this same capture's resolved include edges replace their rows.
        if (content == contents.cend()) {
            const auto incoming = result.dependents.value(key);
            for (auto it = incoming.cbegin(); it != incoming.cend(); ++it)
                affected.insert(it.key());
        }
        result.indexFacts(key, old, false);
        if (content == contents.cend()) {
            result.factsByFile.remove(key);
            result.originalPathByKey.remove(key);
            result.observedIncludes.remove(key);
        } else {
            result.factsByFile.insert(key, next);
            result.originalPathByKey.insert(key, file);
            result.indexFacts(key, next, true);
        }
        for (const QString& name : declarations)
            for (const auto& user : result.usersByName.value(name)) affected.insert(user);
        affected.insert(key);
    }
    // Only rows whose own facts or referenced declarations changed are rebuilt.
    for (const QString& key : affected) {
        result.removeFileEdges(key);
        if (result.factsByFile.contains(key))
            result.rebuildFileEdges(key);
    }
    if (topChanged || affected.contains(normalizedPath(topFile)))
        result.rebuildActiveTop();
    return result;
}

SemanticDependencyGraph SemanticDependencyGraph::withObservedIncludes(
    const QHash<QString, QStringList>& includesByFile) const
{
    SemanticDependencyGraph result = *this;
    for (auto it = includesByFile.cbegin(); it != includesByFile.cend(); ++it) {
        const QString key = normalizedPath(it.key());
        if (result.observedIncludes.contains(key) && result.observedIncludes.value(key) == it.value())
            continue;
        result.observedIncludes.insert(key, it.value());
        result.removeFileEdges(key);
        if (result.factsByFile.contains(key))
            result.rebuildFileEdges(key);
    }
    result.rebuildActiveTop();
    return result;
}

bool SemanticDependencyGraph::isValidFor(const ProjectSnapshot& project) const
{
    return !projectIdentity.isEmpty()
        && projectIdentity == projectKey(project)
        && std::all_of(project.systemVerilogFiles.cbegin(), project.systemVerilogFiles.cend(),
            [this](const QString& file) { return factsByFile.contains(normalizedPath(file)); });
}

bool SemanticDependencyGraph::hasParseError(const QString& fileName) const
{
    return factsByFile.value(normalizedPath(fileName)).parseError;
}

qsizetype SemanticDependencyGraph::logicalBytes() const
{
    qsizetype bytes = factsByFile.size() * 2048;
    for (auto it = factsByFile.cbegin(); it != factsByFile.cend(); ++it) {
        bytes += it->symbolReferences.size() * 128 + it->moduleInstantiations.size() * 512;
        bytes += dependencies.value(it.key()).size() * 256;
    }
    for (auto it = api.cbegin(); it != api.cend(); ++it)
        bytes += 128 + it->size() * 64;
    for (auto it = apiUsersByName.cbegin(); it != apiUsersByName.cend(); ++it)
        bytes += 128 + it->size() * 64;
    return bytes;
}

void SemanticDependencyGraph::addDependency(
    const QString& dependentKey,
    const QString& dependencyKey,
    SemanticDependencyKind kind)
{
    // All callers already have graph keys. Sharing those strings keeps a dense
    // dependency graph from allocating another pair of full paths per edge.
    if (dependentKey.isEmpty() || dependencyKey.isEmpty()
        || dependentKey == dependencyKey)
        return;
    dependencies[dependentKey][dependencyKey] |= kind;
    dependents[dependencyKey][dependentKey] |= kind;
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

void SemanticDependencyGraph::indexFacts(const QString& key,
    const SemanticFileDependencyFacts& facts, bool add)
{
    auto update = [&](QHash<QString, QStringList>& index, const QSet<QString>& names) {
        for (const QString& name : names) {
            if (name.isEmpty())
                continue;
            if (add)
                index[name].append(key);
            else {
                auto it = index.find(name);
                if (it != index.end() && it->removeAll(key) && it->isEmpty())
                    index.erase(it);
            }
        }
    };
    update(modules, facts.moduleDeclarations);
    update(packages, facts.packageDeclarations);
    update(macros, facts.macroDefinitions);
    update(api, facts.apiDeclarations);
    update(apiUsersByName, facts.symbolReferences);
    update(usersByName, facts.macroUses | facts.importedPackages
        | facts.instantiatedModules | facts.symbolReferences);
}

void SemanticDependencyGraph::removeFileEdges(const QString& key)
{
    const auto old = dependencies.take(key);
    for (auto it = old.cbegin(); it != old.cend(); ++it) {
        auto reverse = dependents.find(it.key());
        if (reverse != dependents.end()) {
            reverse->remove(key);
            if (reverse->isEmpty())
                dependents.erase(reverse);
        }
    }
}

void SemanticDependencyGraph::rebuildFileEdges(const QString& key)
{
    const auto facts = factsByFile.value(key);
    if (observedIncludes.contains(key)) {
        for (const QString& file : observedIncludes.value(key))
            addDependency(key, normalizedPath(file), SemanticDependencyKind::Include);
    } else {
        for (const QString& name : facts.includeNames) {
            const QString file = resolveInclude(originalPathByKey.value(key, key), name);
            if (!file.isEmpty())
                addDependency(key, normalizedPath(file), SemanticDependencyKind::Include);
        }
    }
    auto bind = [&](const QSet<QString>& names,
                    const QHash<QString, QStringList>& providers,
                    SemanticDependencyKind kind) {
        for (const QString& name : names)
            for (const QString& provider : providers.value(name))
                addDependency(key, provider, kind);
    };
    bind(facts.macroUses, macros, SemanticDependencyKind::Macro);
    bind(facts.importedPackages, packages, SemanticDependencyKind::Package);
    bind(facts.instantiatedModules, modules, SemanticDependencyKind::Instantiation);
    // TypeOrApi is resolved through the name incidence indexes by queries;
    // do not expand a conservative complete bipartite relation here.
}

void SemanticDependencyGraph::rebuildActiveTop()
{
    const QString oldTop = normalizedPath(topFile);
    if (!oldTop.isEmpty()) {
        auto edges = dependencies.value(oldTop);
        for (auto it = edges.cbegin(); it != edges.cend(); ++it) {
            const auto kinds = it.value() & ~SemanticDependencyKinds(SemanticDependencyKind::ActiveTop);
            if (kinds) {
                dependencies[oldTop][it.key()] = kinds;
                dependents[it.key()][oldTop] = kinds;
            } else {
                dependencies[oldTop].remove(it.key());
                dependents[it.key()].remove(oldTop);
            }
        }
    }
    topFile.clear();
    if (!graphProject.topModule.isEmpty()) {
        const auto providers = modules.value(graphProject.topModule);
        const QStringList candidates = orderedFiles(QSet<QString>(providers.cbegin(), providers.cend()));
        if (!candidates.isEmpty())
            topFile = candidates.first();
    }
    if (topFile.isEmpty())
        return;
    const QString topKey = normalizedPath(topFile);
    QSet<QString> descendants;
    QQueue<QString> queue;
    queue.enqueue(topKey);
    while (!queue.isEmpty()) {
        const auto edges = dependencies.value(queue.dequeue());
        for (auto it = edges.cbegin(); it != edges.cend(); ++it) {
            if (!(it.value() & SemanticDependencyKind::Instantiation) || descendants.contains(it.key()))
                continue;
            descendants.insert(it.key());
            queue.enqueue(it.key());
        }
    }
    for (const QString& key : descendants)
        addDependency(topKey, key, SemanticDependencyKind::ActiveTop);
}

void SemanticDependencyGraph::rebuildEdges()
{
    dependencies.clear();
    dependents.clear();
    modules.clear(); packages.clear(); macros.clear(); api.clear(); usersByName.clear();
    apiUsersByName.clear();
    topFile.clear();
    for (auto it = factsByFile.cbegin(); it != factsByFile.cend(); ++it)
        indexFacts(it.key(), it.value(), true);
    for (auto it = factsByFile.cbegin(); it != factsByFile.cend(); ++it)
        rebuildFileEdges(it.key());
    rebuildActiveTop();
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
    return reachableFiles(files, kinds, recursive, false);
}

QStringList SemanticDependencyGraph::dependentsOf(
    const QStringList& files,
    SemanticDependencyKinds kinds,
    bool recursive) const
{
    return reachableFiles(files, kinds, recursive, true);
}

QStringList SemanticDependencyGraph::reachableFiles(
    const QStringList& files, SemanticDependencyKinds kinds,
    bool recursive, bool reverse) const
{
    QSet<QString> result;
    QQueue<QString> queue;
    for (const QString& file : files)
        queue.enqueue(normalizedPath(file));
    QSet<QString> visited;
    QSet<QString> expandedApiNames;
    // The first source of a name cannot acquire a self edge. A second source
    // does make it reachable, exactly as in the expanded bipartite relation.
    QHash<QString, QString> firstApiSource;
    const auto& adjacency = reverse ? dependents : dependencies;
    const auto& apiTargets = reverse ? apiUsersByName : api;
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        if (current.isEmpty() || visited.contains(current))
            continue;
        visited.insert(current);
        auto append = [&](const QString& target) {
            if (target != current && !result.contains(target)) {
                result.insert(target);
                if (recursive)
                    queue.enqueue(target);
            }
        };
        const auto edges = adjacency.value(current);
        for (auto it = edges.constBegin(); it != edges.constEnd(); ++it) {
            if (!(it.value() & kinds))
                continue;
            append(it.key());
        }
        if (kinds & SemanticDependencyKind::TypeOrApi) {
            const auto facts = factsByFile.value(current);
            const auto& names = reverse ? facts.apiDeclarations : facts.symbolReferences;
            for (const QString& name : names) {
                const auto targets = apiTargets.value(name);
                if (!expandedApiNames.contains(name)) {
                    expandedApiNames.insert(name);
                    firstApiSource.insert(name, current);
                    for (const QString& target : targets)
                        append(target);
                } else {
                    const QString first = firstApiSource.value(name);
                    if (targets.contains(first))
                        append(first);
                }
            }
        }
    }
    return orderedFiles(result);
}
