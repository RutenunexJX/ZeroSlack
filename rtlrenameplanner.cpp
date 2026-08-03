#include "rtlrenameplanner.h"

#include "saferenameservice.h"
#include "semanticindexsnapshot.h"
#include "tsdocument.h"
#include "workspaceedittransactionservice.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using DeclarationKind = SymbolTaxonomy::DeclarationKind;
using CollectorKind = SymbolTaxonomy::CollectorKind;
using OwnerScope = SymbolTaxonomy::SymbolOwnerScope;

enum class RenameKind {
    Port,
    Parameter,
    Localparam
};

enum class DeclarationRole {
    None,
    Port,
    Parameter,
    Localparam,
    Variable,
    Net,
    FormalArgument,
    Other
};

struct TreeIdentifier {
    TSNode node{};
    int start = -1;
    int end = -1;
    QString text;

    bool valid() const
    {
        return !ts_node_is_null(node)
            && start >= 0 && end > start && !text.isEmpty();
    }
};

struct TreeAssociation {
    enum class Kind {
        Port,
        Parameter
    };

    Kind kind = Kind::Port;
    QString ownerName;
    QString moduleType;
    QString instanceName;
    QString formalName;
    int formalStart = -1;
    int formalEnd = -1;
    int instanceStart = -1;
    int instanceEnd = -1;
    bool named = false;
    bool implicitPort = false;
};

struct TreeInstance {
    QString ownerName;
    QString moduleType;
    QString instanceName;
    int start = -1;
    int end = -1;
    bool hasOrderedPorts = false;
    bool hasOrderedParameters = false;
    bool complete = true;
    QList<TreeAssociation> associations;
};

struct TreeDocumentFacts {
    QList<TreeInstance> instances;
};

struct PendingEdit {
    QString fileName;
    std::uint64_t revision = 0;
    QString documentText;
    int start = -1;
    int end = -1;
    QString expectedText;
    QString newText;
    QString anchorName;
    QString description;
};

struct BoundDocument {
    QString normalizedFileName;
    const RtlRenameDocumentSnapshot* captured = nullptr;
};

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

std::string utf8String(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(),
                       static_cast<std::size_t>(bytes.size()));
}

QString fromUtf8String(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

bool nodeTypeIs(TSNode node, const char* expected)
{
    if (ts_node_is_null(node) || !expected)
        return false;
    const char* type = ts_node_type(node);
    return type && std::strcmp(type, expected) == 0;
}

int nodeStart(TSNode node)
{
    return ts_node_is_null(node)
        ? -1
        : static_cast<int>(ts_node_start_byte(node) / 2u);
}

int nodeEnd(TSNode node)
{
    return ts_node_is_null(node)
        ? -1
        : static_cast<int>(ts_node_end_byte(node) / 2u);
}

QString nodeText(const QString& text, TSNode node)
{
    const int start = nodeStart(node);
    const int end = nodeEnd(node);
    if (start < 0 || end <= start || end > text.size())
        return {};
    return text.mid(start, end - start);
}

TSNode fieldChild(TSNode node, const char* field)
{
    return ts_node_is_null(node) || !field
        ? TSNode{}
        : ts_node_child_by_field_name(
              node, field, static_cast<uint32_t>(std::strlen(field)));
}

QList<TSNode> namedChildren(TSNode node)
{
    QList<TSNode> result;
    const uint32_t count = ts_node_named_child_count(node);
    result.reserve(static_cast<qsizetype>(count));
    for (uint32_t index = 0; index < count; ++index)
        result.append(ts_node_named_child(node, index));
    return result;
}

QList<TSNode> children(TSNode node)
{
    QList<TSNode> result;
    const uint32_t count = ts_node_child_count(node);
    result.reserve(static_cast<qsizetype>(count));
    for (uint32_t index = 0; index < count; ++index)
        result.append(ts_node_child(node, index));
    return result;
}

bool identifierNode(TSNode node)
{
    return nodeTypeIs(node, "simple_identifier")
        || nodeTypeIs(node, "escaped_identifier");
}

QList<TSNode> identifierDescendants(TSNode root)
{
    QList<TSNode> result;
    QList<TSNode> pending{root};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        if (identifierNode(node)) {
            result.prepend(node);
            continue;
        }
        const QList<TSNode> direct = namedChildren(node);
        for (const TSNode child : direct)
            pending.append(child);
    }
    std::sort(result.begin(), result.end(),
              [](TSNode left, TSNode right) {
                  if (nodeStart(left) != nodeStart(right))
                      return nodeStart(left) < nodeStart(right);
                  return nodeEnd(left) < nodeEnd(right);
              });
    return result;
}

TSNode firstIdentifierDescendant(TSNode root)
{
    const QList<TSNode> identifiers =
        identifierDescendants(root);
    return identifiers.isEmpty()
        ? TSNode{} : identifiers.constFirst();
}

bool sameNode(TSNode left, TSNode right)
{
    return !ts_node_is_null(left)
        && !ts_node_is_null(right)
        && ts_node_eq(left, right);
}

TSNode ancestorOfType(TSNode node, const char* expected)
{
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, expected))
            return node;
        node = ts_node_parent(node);
    }
    return {};
}

TSNode nearestAncestorMatching(
    TSNode node,
    const QList<QByteArray>& names)
{
    while (!ts_node_is_null(node)) {
        const char* type = ts_node_type(node);
        for (const QByteArray& name : names) {
            if (type && name == type)
                return node;
        }
        node = ts_node_parent(node);
    }
    return {};
}

QString declarationName(const QString& text, TSNode declaration)
{
    TSNode name = fieldChild(declaration, "name");
    if (!ts_node_is_null(name))
        return nodeText(text, name);

    for (const TSNode child : namedChildren(declaration)) {
        const char* type = ts_node_type(child);
        if (!type || !std::strstr(type, "_header"))
            continue;
        name = fieldChild(child, "name");
        if (!ts_node_is_null(name))
            return nodeText(text, name);
        const TSNode first = firstIdentifierDescendant(child);
        if (!ts_node_is_null(first))
            return nodeText(text, first);
    }
    return {};
}

bool ownerNodeType(RenameKind kind, TSNode node)
{
    if (kind == RenameKind::Port)
        return nodeTypeIs(node, "module_declaration")
            || nodeTypeIs(node, "interface_declaration")
            || nodeTypeIs(node, "program_declaration");
    return nodeTypeIs(node, "module_declaration")
        || nodeTypeIs(node, "interface_declaration")
        || nodeTypeIs(node, "program_declaration")
        || nodeTypeIs(node, "package_declaration");
}

QList<TSNode> ownerDeclarations(
    const TSDocument& document,
    const QString& ownerName,
    RenameKind kind)
{
    QList<TSNode> result;
    QList<TSNode> pending{document.rootNode()};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        if (ownerNodeType(kind, node)
            && declarationName(document.text(), node)
                   == ownerName) {
            result.prepend(node);
            continue;
        }
        for (const TSNode child : namedChildren(node))
            pending.append(child);
    }
    return result;
}

bool nodeContains(TSNode outer, TSNode inner)
{
    return !ts_node_is_null(outer)
        && !ts_node_is_null(inner)
        && nodeStart(outer) <= nodeStart(inner)
        && nodeEnd(outer) >= nodeEnd(inner);
}

DeclarationRole declarationRole(TSNode identifier)
{
    if (!identifierNode(identifier))
        return DeclarationRole::None;

    TSNode node = identifier;
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "ansi_port_declaration")) {
            return sameNode(fieldChild(node, "port_name"),
                            identifier)
                ? DeclarationRole::Port
                : DeclarationRole::None;
        }
        if (nodeTypeIs(node, "param_assignment")) {
            if (!sameNode(firstIdentifierDescendant(node),
                          identifier)) {
                return DeclarationRole::None;
            }
            const TSNode local =
                ancestorOfType(node,
                               "local_parameter_declaration");
            return ts_node_is_null(local)
                ? DeclarationRole::Parameter
                : DeclarationRole::Localparam;
        }
        if (nodeTypeIs(node, "variable_decl_assignment")) {
            return sameNode(fieldChild(node, "name"),
                            identifier)
                ? DeclarationRole::Variable
                : DeclarationRole::None;
        }
        if (nodeTypeIs(node, "net_decl_assignment")) {
            return sameNode(firstIdentifierDescendant(node),
                            identifier)
                ? DeclarationRole::Net
                : DeclarationRole::None;
        }
        if (nodeTypeIs(node, "tf_port_item")) {
            return sameNode(fieldChild(node, "name"),
                            identifier)
                ? DeclarationRole::FormalArgument
                : DeclarationRole::None;
        }
        if (nodeTypeIs(node, "list_of_port_identifiers")
            || nodeTypeIs(
                node, "list_of_variable_port_identifiers")) {
            TSNode child = identifier;
            while (!ts_node_is_null(ts_node_parent(child))
                   && !sameNode(ts_node_parent(child), node)) {
                child = ts_node_parent(child);
            }
            return sameNode(child, identifier)
                ? DeclarationRole::Port
                : DeclarationRole::None;
        }
        if (nodeTypeIs(node, "variable_identifier_list")
            || nodeTypeIs(node, "list_of_variable_identifiers")) {
            TSNode child = identifier;
            while (!ts_node_is_null(ts_node_parent(child))
                   && !sameNode(ts_node_parent(child), node)) {
                child = ts_node_parent(child);
            }
            return sameNode(child, identifier)
                ? DeclarationRole::Variable
                : DeclarationRole::None;
        }
        if (nodeTypeIs(node, "name_of_instance")
            || nodeTypeIs(node, "module_instantiation")
            || nodeTypeIs(node, "module_declaration")
            || nodeTypeIs(node, "interface_declaration")
            || nodeTypeIs(node, "program_declaration")
            || nodeTypeIs(node, "package_declaration")
            || nodeTypeIs(node, "function_declaration")
            || nodeTypeIs(node, "task_declaration")) {
            break;
        }
        node = ts_node_parent(node);
    }
    return DeclarationRole::None;
}

DeclarationRole targetDeclarationRole(RenameKind kind)
{
    switch (kind) {
    case RenameKind::Port:
        return DeclarationRole::Port;
    case RenameKind::Parameter:
        return DeclarationRole::Parameter;
    case RenameKind::Localparam:
        return DeclarationRole::Localparam;
    }
    return DeclarationRole::None;
}

bool lexicalScopeNode(TSNode node)
{
    return nodeTypeIs(node, "seq_block")
        || nodeTypeIs(node, "par_block")
        || nodeTypeIs(node, "function_declaration")
        || nodeTypeIs(node, "task_declaration")
        || nodeTypeIs(node, "class_declaration")
        || nodeTypeIs(node, "generate_block")
        || nodeTypeIs(node, "generate_region")
        || nodeTypeIs(node, "conditional_generate_construct")
        || nodeTypeIs(node, "loop_generate_construct")
        || nodeTypeIs(node, "case_generate_construct");
}

TSNode nearestLexicalScope(TSNode node, TSNode owner)
{
    node = ts_node_parent(node);
    while (!ts_node_is_null(node) && !sameNode(node, owner)) {
        if (lexicalScopeNode(node))
            return node;
        node = ts_node_parent(node);
    }
    return {};
}

bool insideAnyScope(TSNode identifier,
                    const QList<TSNode>& scopes)
{
    for (const TSNode scope : scopes) {
        if (nodeContains(scope, identifier))
            return true;
    }
    return false;
}

bool isNamedAssociationFormal(TSNode identifier,
                              TreeAssociation::Kind* kind = nullptr,
                              TSNode* association = nullptr)
{
    TSNode node = identifier;
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "named_port_connection")) {
            TSNode formal = fieldChild(node, "port_name");
            if (ts_node_is_null(formal))
                formal = firstIdentifierDescendant(node);
            if (!sameNode(formal, identifier))
                return false;
            if (kind)
                *kind = TreeAssociation::Kind::Port;
            if (association)
                *association = node;
            return true;
        }
        if (nodeTypeIs(node, "named_parameter_assignment")) {
            const TSNode formal =
                firstIdentifierDescendant(node);
            if (!sameNode(formal, identifier))
                return false;
            if (kind)
                *kind = TreeAssociation::Kind::Parameter;
            if (association)
                *association = node;
            return true;
        }
        if (nodeTypeIs(node, "module_instantiation"))
            return false;
        node = ts_node_parent(node);
    }
    return false;
}

bool associationHasToken(TSNode association,
                         const char* tokenType)
{
    for (const TSNode child : children(association)) {
        if (nodeTypeIs(child, tokenType))
            return true;
    }
    return false;
}

bool implicitPortAssociation(TSNode association)
{
    return nodeTypeIs(association, "named_port_connection")
        && !associationHasToken(association, "(");
}

bool instanceStructuralRole(TSNode identifier)
{
    TSNode node = identifier;
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "name_of_instance"))
            return true;
        if (nodeTypeIs(node, "module_instantiation")) {
            const TSNode type = fieldChild(node, "instance_type");
            return nodeContains(type, identifier);
        }
        node = ts_node_parent(node);
    }
    return false;
}

bool typeOrLabelRole(TSNode identifier)
{
    static const QList<QByteArray> excludedAncestors{
        "data_type",
        "data_type_or_implicit",
        "integer_vector_type",
        "integer_atom_type",
        "non_integer_type",
        "module_identifier",
        "interface_identifier",
        "package_identifier",
        "class_identifier",
        "type_identifier",
        "block_identifier",
        "generate_block_identifier",
        "attribute_instance",
        "text_macro_usage",
        "text_macro_definition"};

    TSNode node = ts_node_parent(identifier);
    while (!ts_node_is_null(node)) {
        const char* type = ts_node_type(node);
        for (const QByteArray& excluded : excludedAncestors) {
            if (type && excluded == type)
                return true;
        }
        if (nodeTypeIs(node, "expression")
            || nodeTypeIs(node, "constant_expression")
            || nodeTypeIs(node, "module_declaration")
            || nodeTypeIs(node, "interface_declaration")
            || nodeTypeIs(node, "package_declaration")) {
            break;
        }
        node = ts_node_parent(node);
    }
    return false;
}

bool nonRootSelector(TSNode identifier)
{
    static const QList<QByteArray> selectorTypes{
        "hierarchical_identifier",
        "ps_or_hierarchical_identifier",
        "ps_or_hierarchical_net_identifier",
        "ps_or_hierarchical_array_identifier",
        "package_scope",
        "class_scope"};
    TSNode selector =
        nearestAncestorMatching(identifier, selectorTypes);
    if (ts_node_is_null(selector))
        return false;
    const QList<TSNode> identifiers =
        identifierDescendants(selector);
    return identifiers.size() > 1
        && !sameNode(identifiers.constFirst(), identifier);
}

TreeIdentifier treeIdentifier(const QString& text,
                              TSNode node)
{
    return {node, nodeStart(node), nodeEnd(node),
            nodeText(text, node)};
}

QList<TreeIdentifier> matchingIdentifiers(
    TSNode root,
    const QString& text,
    const QString& name)
{
    QList<TreeIdentifier> result;
    for (const TSNode node : identifierDescendants(root)) {
        if (nodeText(text, node) == name)
            result.append(treeIdentifier(text, node));
    }
    return result;
}

QString moduleTypeForInstantiation(
    const QString& text,
    TSNode instantiation)
{
    TSNode type = fieldChild(instantiation, "instance_type");
    if (!ts_node_is_null(type))
        return nodeText(text, type);
    for (const TSNode child : namedChildren(instantiation)) {
        if (identifierNode(child))
            return nodeText(text, child);
    }
    return {};
}

QString instanceNameForHierarchy(const QString& text,
                                 TSNode hierarchy)
{
    for (const TSNode child : namedChildren(hierarchy)) {
        if (!nodeTypeIs(child, "name_of_instance"))
            continue;
        const TSNode name = firstIdentifierDescendant(child);
        if (!ts_node_is_null(name))
            return nodeText(text, name);
    }
    return {};
}

QString enclosingOwnerName(const QString& text,
                           TSNode node)
{
    node = ts_node_parent(node);
    while (!ts_node_is_null(node)) {
        if (nodeTypeIs(node, "module_declaration")
            || nodeTypeIs(node, "interface_declaration")
            || nodeTypeIs(node, "program_declaration")) {
            return declarationName(text, node);
        }
        node = ts_node_parent(node);
    }
    return {};
}

void collectAssociations(
    const QString& text,
    TSNode container,
    TreeAssociation::Kind kind,
    const QString& owner,
    const QString& moduleType,
    const QString& instanceName,
    int instanceStart,
    int instanceEnd,
    TreeInstance* output)
{
    if (!output || ts_node_is_null(container)
        || ts_node_has_error(container)) {
        if (output)
            output->complete = false;
        return;
    }

    for (const TSNode association : namedChildren(container)) {
        const bool named =
            kind == TreeAssociation::Kind::Port
                ? nodeTypeIs(
                      association, "named_port_connection")
                : nodeTypeIs(
                      association,
                      "named_parameter_assignment");
        const bool ordered =
            kind == TreeAssociation::Kind::Port
                ? nodeTypeIs(
                      association, "ordered_port_connection")
                : nodeTypeIs(
                      association,
                      "ordered_parameter_assignment");
        if (ordered) {
            if (kind == TreeAssociation::Kind::Port)
                output->hasOrderedPorts = true;
            else
                output->hasOrderedParameters = true;
            continue;
        }
        if (!named) {
            output->complete = false;
            continue;
        }

        TSNode formal =
            kind == TreeAssociation::Kind::Port
                ? fieldChild(association, "port_name")
                : TSNode{};
        if (ts_node_is_null(formal))
            formal = firstIdentifierDescendant(association);
        if (ts_node_is_null(formal)) {
            output->complete = false;
            continue;
        }

        TreeAssociation item;
        item.kind = kind;
        item.ownerName = owner;
        item.moduleType = moduleType;
        item.instanceName = instanceName;
        item.formalName = nodeText(text, formal);
        item.formalStart = nodeStart(formal);
        item.formalEnd = nodeEnd(formal);
        item.instanceStart = instanceStart;
        item.instanceEnd = instanceEnd;
        item.named = true;
        item.implicitPort =
            kind == TreeAssociation::Kind::Port
            && implicitPortAssociation(association);
        output->associations.append(std::move(item));
    }
}

TreeDocumentFacts collectTreeDocumentFacts(
    const TSDocument& document)
{
    TreeDocumentFacts result;
    const QString& text = document.text();
    QList<TSNode> pending{document.rootNode()};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        if (!nodeTypeIs(node, "module_instantiation")) {
            for (const TSNode child : namedChildren(node))
                pending.append(child);
            continue;
        }

        const QString moduleType =
            moduleTypeForInstantiation(text, node);
        TSNode parameterValues{};
        QList<TSNode> hierarchies;
        for (const TSNode child : namedChildren(node)) {
            if (nodeTypeIs(
                    child, "parameter_value_assignment")) {
                parameterValues = child;
            } else if (nodeTypeIs(
                           child, "hierarchical_instance")) {
                hierarchies.append(child);
            }
        }

        TSNode parameterAssignments{};
        if (!ts_node_is_null(parameterValues)) {
            for (const TSNode child :
                 namedChildren(parameterValues)) {
                if (nodeTypeIs(
                        child,
                        "list_of_parameter_value_assignments")) {
                    parameterAssignments = child;
                    break;
                }
            }
        }

        for (const TSNode hierarchy : hierarchies) {
            TreeInstance instance;
            instance.ownerName =
                enclosingOwnerName(text, node);
            instance.moduleType = moduleType;
            instance.instanceName =
                instanceNameForHierarchy(text, hierarchy);
            instance.start = nodeStart(hierarchy);
            instance.end = nodeEnd(hierarchy);
            if (instance.ownerName.isEmpty()
                || instance.moduleType.isEmpty()
                || instance.instanceName.isEmpty()
                || instance.start < 0
                || instance.end <= instance.start) {
                instance.complete = false;
            }

            if (!ts_node_is_null(parameterAssignments)) {
                collectAssociations(
                    text,
                    parameterAssignments,
                    TreeAssociation::Kind::Parameter,
                    instance.ownerName,
                    instance.moduleType,
                    instance.instanceName,
                    instance.start,
                    instance.end,
                    &instance);
            }

            TSNode ports{};
            for (const TSNode child :
                 namedChildren(hierarchy)) {
                if (nodeTypeIs(
                        child,
                        "list_of_port_connections")) {
                    ports = child;
                    break;
                }
            }
            if (!ts_node_is_null(ports)) {
                collectAssociations(
                    text,
                    ports,
                    TreeAssociation::Kind::Port,
                    instance.ownerName,
                    instance.moduleType,
                    instance.instanceName,
                    instance.start,
                    instance.end,
                    &instance);
            }
            result.instances.append(std::move(instance));
        }
    }
    std::sort(
        result.instances.begin(), result.instances.end(),
        [](const TreeInstance& left,
           const TreeInstance& right) {
            return left.start < right.start;
        });
    return result;
}

std::optional<RenameKind> renameKindForRecord(
    const SemanticSymbolRecord& record)
{
    if (record.declarationKind == DeclarationKind::Port)
        return RenameKind::Port;
    if (record.declarationKind == DeclarationKind::Parameter)
        return RenameKind::Parameter;
    if (record.declarationKind == DeclarationKind::Localparam)
        return RenameKind::Localparam;
    return std::nullopt;
}

bool supportedOwner(const SemanticSymbolRecord& record,
                    RenameKind kind)
{
    if (record.owner.name.isEmpty())
        return false;
    if (kind == RenameKind::Port) {
        return record.owner.kind == OwnerScope::Module
            || record.owner.kind == OwnerScope::Interface;
    }
    // Instance override synchronization is only semantically complete for
    // module/interface parameters in the current index representation.
    return record.owner.kind == OwnerScope::Module
        || record.owner.kind == OwnerScope::Interface;
}

QString actionId(RenameKind kind)
{
    return kind == RenameKind::Port
        ? QStringLiteral("rtl.renamePort")
        : QStringLiteral("rtl.renameParameter");
}

rtledit::SourcePosition utf8PositionAt(
    const QString& text,
    int charOffset)
{
    const int bounded =
        qBound(0, charOffset, text.size());
    const int lineStart =
        text.lastIndexOf(
            QLatin1Char('\n'),
            qMax(0, bounded - 1))
        + 1;
    const QByteArray column =
        text.mid(lineStart, bounded - lineStart).toUtf8();
    return {
        static_cast<std::size_t>(
            text.left(lineStart)
                .count(QLatin1Char('\n'))),
        static_cast<std::size_t>(column.size())};
}

rtledit::SourceRange utf8Range(
    const QString& text,
    int start,
    int end)
{
    return {utf8PositionAt(text, start),
            utf8PositionAt(text, end)};
}

RtlRenameProposal rejected(
    RtlRenamePlanStatus status,
    const QString& message,
    const RtlRenamePlanQuery& query)
{
    RtlRenameProposal proposal;
    proposal.status = status;
    proposal.message = message;
    proposal.newName = query.newName.trimmed();
    proposal.dryRun = query.dryRun;
    proposal.blockers.append(message);
    return proposal;
}

QHash<QString, BoundDocument> bindDocuments(
    const RtlRenamePlanQuery& query)
{
    QHash<QString, BoundDocument> result;
    for (auto it = query.documents.constBegin();
         it != query.documents.constEnd(); ++it) {
        const RtlRenameDocumentSnapshot& document =
            it.value();
        const QString file = normalizedFileName(
            document.fileName.isEmpty()
                ? it.key() : document.fileName);
        if (file.isEmpty() || result.contains(file))
            continue;
        result.insert(file, {file, &document});
    }
    return result;
}

QString recordIdentity(const SemanticSymbolRecord& record)
{
    return record.stableKey.isValid()
        ? record.stableKey.toString()
        : QStringLiteral("%1:%2:%3")
              .arg(normalizedFileName(
                       record.location.fileName),
                   record.owner.name,
                   record.name);
}

QList<SemanticSymbolRecord> targetModuleDefinitions(
    const SemanticIndexSnapshot& snapshot,
    const SemanticSymbolRecord& subject)
{
    QList<SemanticSymbolRecord> result;
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecordsByName(
             subject.owner.name)) {
        if (record.declarationKind
                == DeclarationKind::Module
            || record.declarationKind
                == DeclarationKind::Interface) {
            result.append(record);
        }
    }
    return result;
}

bool definitionConflicts(
    const SemanticIndexSnapshot& snapshot,
    const SemanticSymbolRecord& subject,
    const QString& newName)
{
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecordsByName(newName)) {
        if (!record.stableKey.isValid()
            || record.stableKey
                   == subject.stableKey) {
            continue;
        }
        if (record.owner.kind == subject.owner.kind
            && record.owner.name == subject.owner.name
            && (record.usageRole
                    == SymbolTaxonomy::SymbolUsageRole::
                        Declaration
                || SymbolTaxonomy::isDefinitionCandidate(
                    semanticMetadataForSymbolRecord(
                        record)))) {
            return true;
        }
    }
    return false;
}

QList<SemanticSymbolRecord> targetInstances(
    const SemanticIndexSnapshot& snapshot,
    const QString& moduleName)
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seen;
    for (const SemanticSymbolRecord& record :
         snapshot.symbolRecordsView()) {
        if (record.collectorKind != CollectorKind::Inst)
            continue;
        const QString resolved =
            !record.type.resolvedTypeName.isEmpty()
                ? record.type.resolvedTypeName
                : record.type.rawTypeText;
        if (resolved != moduleName)
            continue;
        const QString key =
            normalizedFileName(
                record.location.fileName)
            + QLatin1Char(':')
            + QString::number(
                record.location.position)
            + QLatin1Char(':') + record.name;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(record);
    }
    return result;
}

bool semanticInstanceMatches(
    const SemanticSymbolRecord& record,
    const TreeInstance& tree)
{
    const QString resolved =
        !record.type.resolvedTypeName.isEmpty()
            ? record.type.resolvedTypeName
            : record.type.rawTypeText;
    return record.collectorKind == CollectorKind::Inst
        && record.name == tree.instanceName
        && record.owner.name == tree.ownerName
        && resolved == tree.moduleType;
}

QList<const TreeInstance*> matchingTreeInstances(
    const QList<TreeInstance>& instances,
    const SemanticSymbolRecord& record)
{
    QList<const TreeInstance*> result;
    for (const TreeInstance& instance : instances) {
        if (semanticInstanceMatches(record, instance))
            result.append(&instance);
    }
    return result;
}

QList<SemanticSymbolRecord> matchingSemanticInstances(
    const SemanticIndexSnapshot& snapshot,
    const QString& fileName,
    const TreeInstance& tree)
{
    QList<SemanticSymbolRecord> result;
    QSet<QString> seen;
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecords(fileName)) {
        if (!semanticInstanceMatches(record, tree))
            continue;
        const QString key =
            normalizedFileName(
                record.location.fileName)
            + QLatin1Char(':')
            + QString::number(
                record.location.position)
            + QLatin1Char(':') + record.name;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(record);
    }
    return result;
}

std::optional<TreeInstance> savedTreeInstance(
    const SemanticIndexSnapshot& snapshot,
    const SemanticSymbolRecord& instance)
{
    const QString text =
        snapshot.getCachedFileContent(
            instance.location.fileName);
    if (text.isEmpty())
        return std::nullopt;
    TSDocument document;
    document.setText(text);
    if (document.hasError())
        return std::nullopt;
    const TreeDocumentFacts facts =
        collectTreeDocumentFacts(document);
    const QList<const TreeInstance*> matches =
        matchingTreeInstances(
            facts.instances, instance);
    if (matches.size() != 1
        || !matches.constFirst()->complete) {
        return std::nullopt;
    }
    return *matches.constFirst();
}

QStringList associationNames(
    const TreeInstance& instance,
    TreeAssociation::Kind kind)
{
    QStringList result;
    for (const TreeAssociation& association :
         instance.associations) {
        if (association.kind == kind)
            result.append(association.formalName);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool associationBindingsUnchanged(
    const TreeInstance& saved,
    const TreeInstance& current,
    TreeAssociation::Kind kind)
{
    if (kind == TreeAssociation::Kind::Port
        && saved.hasOrderedPorts
               != current.hasOrderedPorts) {
        return false;
    }
    if (kind == TreeAssociation::Kind::Parameter
        && saved.hasOrderedParameters
               != current.hasOrderedParameters) {
        return false;
    }
    return associationNames(saved, kind)
        == associationNames(current, kind);
}

bool instancePinProof(
    const SemanticIndexSnapshot& snapshot,
    const SemanticSymbolRecord& instance,
    const TreeAssociation& association,
    const QString& formalName)
{
    const std::optional<TreeInstance> saved =
        savedTreeInstance(snapshot, instance);
    if (!saved)
        return false;
    QList<TreeAssociation> savedAssociations;
    for (const TreeAssociation& candidate :
         saved->associations) {
        if (candidate.kind
                == TreeAssociation::Kind::Port
            && candidate.formalName == formalName) {
            savedAssociations.append(candidate);
        }
    }
    if (savedAssociations.size() != 1)
        return false;
    const TreeAssociation& savedAssociation =
        savedAssociations.constFirst();

    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecords(
             instance.location.fileName)) {
        if (record.collectorKind != CollectorKind::InstPin
            || record.name != formalName
            || record.owner.name != instance.owner.name) {
            continue;
        }
        const QString resolved =
            !record.type.resolvedTypeName.isEmpty()
                ? record.type.resolvedTypeName
                : record.type.rawTypeText;
        if (resolved != association.moduleType)
            continue;
        if (record.location.position
                == savedAssociation.formalStart
            && record.location.length
                == savedAssociation.formalEnd
                       - savedAssociation.formalStart) {
            return true;
        }
    }
    return false;
}

bool appendEdit(QList<PendingEdit>* edits,
                const BoundDocument& document,
                int start,
                int end,
                const QString& newText,
                const QString& anchorName,
                const QString& description,
                QString* failure)
{
    if (!edits || !document.captured
        || start < 0 || end <= start
        || end > document.captured->text.size()) {
        if (failure)
            *failure = QStringLiteral(
                "Tree-sitter produced an invalid rename anchor.");
        return false;
    }
    const QString expected =
        document.captured->text.mid(start, end - start);
    if (expected.isEmpty()) {
        if (failure)
            *failure = QStringLiteral(
                "The rename anchor has no expected text.");
        return false;
    }
    for (const PendingEdit& existing :
         std::as_const(*edits)) {
        if (normalizedFileName(existing.fileName)
                != document.normalizedFileName) {
            continue;
        }
        if (existing.start == start
            && existing.end == end) {
            if (existing.expectedText == expected
                && existing.newText == newText) {
                return true;
            }
            if (failure)
                *failure = QStringLiteral(
                    "Two semantic rename decisions produced "
                    "different edits for the same anchor.");
            return false;
        }
        if (start < existing.end
            && existing.start < end) {
            if (failure)
                *failure = QStringLiteral(
                    "Semantic rename anchors overlap.");
            return false;
        }
    }

    edits->append(PendingEdit{
        document.captured->fileName,
        document.captured->revision,
        document.captured->text,
        start,
        end,
        expected,
        newText,
        anchorName,
        description});
    return true;
}

QList<TreeIdentifier> targetBoundIdentifiers(
    const RtlRenameDocumentSnapshot& document,
    TSNode owner,
    const QString& oldName,
    RenameKind kind,
    TreeIdentifier* targetDeclaration,
    QList<TSNode>* shadowScopes,
    QString* failure)
{
    QList<TreeIdentifier> result;
    const QList<TreeIdentifier> matches =
        matchingIdentifiers(
            owner, document.text, oldName);
    const DeclarationRole expectedRole =
        targetDeclarationRole(kind);

    QList<TreeIdentifier> declarations;
    for (const TreeIdentifier& identifier : matches) {
        if (declarationRole(identifier.node)
            == expectedRole) {
            declarations.append(identifier);
        }
    }
    if (declarations.size() != 1) {
        if (failure) {
            *failure = QStringLiteral(
                "The current Tree-sitter snapshot has %1 "
                "candidate declarations for %2.")
                    .arg(declarations.size())
                    .arg(oldName);
        }
        return {};
    }
    if (targetDeclaration)
        *targetDeclaration = declarations.constFirst();

    QList<TSNode> localShadows;
    for (const TreeIdentifier& identifier : matches) {
        if (identifier.start
                == declarations.constFirst().start
            && identifier.end
                == declarations.constFirst().end) {
            continue;
        }
        const DeclarationRole role =
            declarationRole(identifier.node);
        if (role == DeclarationRole::None)
            continue;
        const TSNode scope =
            nearestLexicalScope(identifier.node, owner);
        if (ts_node_is_null(scope)) {
            if (failure) {
                *failure = QStringLiteral(
                    "A same-scope declaration named %1 "
                    "makes the binding ambiguous.")
                        .arg(oldName);
            }
            return {};
        }
        bool duplicate = false;
        for (const TSNode existing :
             std::as_const(localShadows)) {
            duplicate = duplicate
                || sameNode(existing, scope);
        }
        if (!duplicate)
            localShadows.append(scope);
    }
    if (shadowScopes)
        *shadowScopes = localShadows;

    for (const TreeIdentifier& identifier : matches) {
        if (identifier.start
                == declarations.constFirst().start
            && identifier.end
                == declarations.constFirst().end) {
            result.append(identifier);
            continue;
        }
        if (insideAnyScope(identifier.node,
                           localShadows)) {
            continue;
        }
        if (declarationRole(identifier.node)
                != DeclarationRole::None
            || isNamedAssociationFormal(
                identifier.node)
            || instanceStructuralRole(identifier.node)
            || typeOrLabelRole(identifier.node)
            || nonRootSelector(identifier.node)) {
            continue;
        }
        result.append(identifier);
    }
    return result;
}

bool validateDocumentSnapshot(
    const BoundDocument& bound,
    const rtledit::WorkspaceDocumentManager& manager,
    QString* failure,
    RtlRenamePlanStatus* failureStatus)
{
    if (!bound.captured
        || !bound.captured->isValid()) {
        if (failure)
            *failure = QStringLiteral(
                "A required live document snapshot is invalid.");
        if (failureStatus)
            *failureStatus =
                RtlRenamePlanStatus::InvalidTreeSnapshot;
        return false;
    }
    if (bound.captured->syntax->hasError()) {
        if (failure)
            *failure = QStringLiteral(
                "A required Tree-sitter snapshot contains "
                "syntax errors.");
        if (failureStatus)
            *failureStatus =
                RtlRenamePlanStatus::SyntaxError;
        return false;
    }
    const auto live =
        manager.snapshot(
            utf8String(bound.captured->fileName));
    if (!live) {
        if (failure)
            *failure = QStringLiteral(
                "A required workspace document is unavailable.");
        if (failureStatus)
            *failureStatus =
                RtlRenamePlanStatus::
                    MissingDocumentSnapshot;
        return false;
    }
    if (live->version.value
            != bound.captured->revision
        || fromUtf8String(live->text)
               != bound.captured->text) {
        if (failure)
            *failure = QStringLiteral(
                "A document revision changed before planning.");
        if (failureStatus)
            *failureStatus =
                RtlRenamePlanStatus::
                    StaleDocumentRevision;
        return false;
    }
    return true;
}

bool validateTargetAgainstSavedSyntax(
    const SemanticIndexSnapshot& snapshot,
    const SemanticSymbolRecord& subject,
    RenameKind kind,
    QString* failure)
{
    const QString saved =
        snapshot.getCachedFileContent(
            subject.location.fileName);
    if (saved.isEmpty()) {
        if (failure) {
            *failure = QStringLiteral(
                "The semantic snapshot does not contain "
                "the subject file text.");
        }
        return false;
    }
    TSDocument syntax;
    syntax.setText(saved);
    if (syntax.hasError()) {
        if (failure) {
            *failure = QStringLiteral(
                "The semantic snapshot subject file has "
                "an invalid syntax tree.");
        }
        return false;
    }
    const TSIdentifierTarget identifier =
        syntax.identifierAt(
            subject.location.position);
    if (!identifier.ok()
        || identifier.text != subject.name) {
        if (failure) {
            *failure = QStringLiteral(
                "The Slang subject location cannot be "
                "mapped to a Tree-sitter identifier.");
        }
        return false;
    }

    TSNode node = syntax.rootNode();
    const uint32_t byte =
        static_cast<uint32_t>(
            identifier.startChar) * 2u;
    node = ts_node_named_descendant_for_byte_range(
        node, byte, byte);
    while (!ts_node_is_null(node)
           && !identifierNode(node)) {
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node)
        || declarationRole(node)
               != targetDeclarationRole(kind)) {
        if (failure) {
            *failure = QStringLiteral(
                "The Slang subject is not the expected "
                "Tree-sitter declaration kind.");
        }
        return false;
    }
    return true;
}

} // namespace

bool RtlRenameDocumentSnapshot::isValid() const
{
    return !fileName.isEmpty()
        && revision > 0
        && syntax
        && syntax->text() == text;
}

bool RtlRenameProposal::ready() const
{
    return status == RtlRenamePlanStatus::Ready
        && transaction.ready()
        && sourceDiff.built()
        && !workspaceEdit.edits.empty();
}

RtlRenamePlanner::RtlRenamePlanner(
    SemanticIndex* semanticIndex)
    : index(semanticIndex
                ? semanticIndex
                : SemanticIndex::getInstance())
{
}

RtlRenameProposal RtlRenamePlanner::plan(
    const RtlRenamePlanQuery& query,
    const rtledit::WorkspaceDocumentManager&
        documentManager) const
{
    const QString newName = query.newName.trimmed();
    if (!query.subjectStableKey.isValid()) {
        return rejected(
            RtlRenamePlanStatus::InvalidRequest,
            QStringLiteral(
                "A stable Slang subject identity is required."),
            query);
    }
    if (!SafeRenameService::isValidIdentifier(newName)) {
        return rejected(
            RtlRenamePlanStatus::InvalidNewName,
            QStringLiteral(
                "The requested name is not a valid "
                "SystemVerilog identifier."),
            query);
    }
    if (!query.semanticToken.isValid()
        || query.semanticToken.revision == 0) {
        return rejected(
            RtlRenamePlanStatus::MissingSemanticSnapshot,
            QStringLiteral(
                "An immutable semantic snapshot generation "
                "is required."),
            query);
    }

    SemanticIndex* semantic = semanticIndex();
    const SemanticSnapshotToken current =
        semantic ? semantic->snapshotToken()
                 : SemanticSnapshotToken{};
    if (!current.isValid()
        || current.revision
               != query.semanticToken.revision
        || current.snapshot.get()
               != query.semanticToken.snapshot.get()) {
        return rejected(
            RtlRenamePlanStatus::StaleSemanticGeneration,
            QStringLiteral(
                "The semantic snapshot generation changed "
                "before planning."),
            query);
    }

    const SemanticIndexSnapshot& snapshot =
        *query.semanticToken.snapshot;
    const SemanticSymbolRecord subject =
        snapshot.getSymbolRecordByStableKey(
            query.subjectStableKey);
    if (!subject.isValid()
        || subject.stableKey
               != query.subjectStableKey) {
        return rejected(
            RtlRenamePlanStatus::SubjectNotFound,
            QStringLiteral(
                "The selected semantic declaration is no "
                "longer present in the captured snapshot."),
            query);
    }
    if (newName == subject.name) {
        return rejected(
            RtlRenamePlanStatus::NoChange,
            QStringLiteral(
                "The requested name is unchanged."),
            query);
    }

    const std::optional<RenameKind> kind =
        renameKindForRecord(subject);
    if (!kind || !supportedOwner(subject, *kind)) {
        return rejected(
            RtlRenamePlanStatus::UnsupportedSubject,
            QStringLiteral(
                "Only module/interface ports, parameters, "
                "and localparams can be planned safely."),
            query);
    }
    if (definitionConflicts(
            snapshot, subject, newName)) {
        return rejected(
            RtlRenamePlanStatus::ConflictingDefinition,
            QStringLiteral(
                "The new name conflicts with a declaration "
                "in the same semantic owner."),
            query);
    }

    const QList<SemanticSymbolRecord> moduleDefinitions =
        targetModuleDefinitions(snapshot, subject);
    if (moduleDefinitions.size() != 1) {
        return rejected(
            RtlRenamePlanStatus::AmbiguousSubject,
            QStringLiteral(
                "The owning module/interface definition is "
                "not unique in the semantic snapshot."),
            query);
    }

    QString savedSyntaxFailure;
    if (!validateTargetAgainstSavedSyntax(
            snapshot, subject, *kind,
            &savedSyntaxFailure)) {
        return rejected(
            RtlRenamePlanStatus::
                IncompleteSemanticBinding,
            savedSyntaxFailure,
            query);
    }

    const QHash<QString, BoundDocument> boundDocuments =
        bindDocuments(query);
    const QString subjectFile =
        normalizedFileName(
            subject.location.fileName);
    if (!boundDocuments.contains(subjectFile)) {
        return rejected(
            RtlRenamePlanStatus::MissingDocumentSnapshot,
            QStringLiteral(
                "The subject document snapshot is missing."),
            query);
    }

    QSet<QString> requiredFiles{subjectFile};
    if (!query.workspaceFiles.isEmpty()) {
        for (const QString& file :
             query.workspaceFiles) {
            const QString normalized =
                normalizedFileName(file);
            if (!normalized.isEmpty())
                requiredFiles.insert(normalized);
        }
    } else {
        for (auto it =
                 boundDocuments.constBegin();
             it != boundDocuments.constEnd();
             ++it) {
            requiredFiles.insert(it.key());
        }
    }
    const QList<SemanticSymbolRecord> instances =
        *kind == RenameKind::Localparam
            ? QList<SemanticSymbolRecord>{}
            : targetInstances(
                  snapshot, subject.owner.name);
    for (const SemanticSymbolRecord& instance :
         instances) {
        requiredFiles.insert(
            normalizedFileName(
                instance.location.fileName));
    }

    QHash<QString, TreeDocumentFacts> treeFacts;
    for (const QString& file :
         std::as_const(requiredFiles)) {
        if (!boundDocuments.contains(file)) {
            return rejected(
                RtlRenamePlanStatus::
                    MissingDocumentSnapshot,
                QStringLiteral(
                    "A document containing a semantic "
                    "rename binding has no live snapshot."),
                query);
        }
        QString failure;
        RtlRenamePlanStatus failureStatus =
            RtlRenamePlanStatus::
                MissingDocumentSnapshot;
        if (!validateDocumentSnapshot(
                boundDocuments.value(file),
                documentManager,
                &failure,
                &failureStatus)) {
            return rejected(
                failureStatus, failure, query);
        }
        treeFacts.insert(
            file,
            collectTreeDocumentFacts(
                *boundDocuments.value(file)
                     .captured->syntax));
    }

    const BoundDocument& subjectDocument =
        boundDocuments.value(subjectFile);
    const QList<TSNode> owners =
        ownerDeclarations(
            *subjectDocument.captured->syntax,
            subject.owner.name,
            *kind);
    if (owners.size() != 1) {
        return rejected(
            RtlRenamePlanStatus::AmbiguousStructure,
            QStringLiteral(
                "The live Tree-sitter snapshot does not "
                "contain exactly one owning declaration."),
            query);
    }

    TreeIdentifier targetDeclaration;
    QList<TSNode> shadowScopes;
    QString bindingFailure;
    const QList<TreeIdentifier> boundIdentifiers =
        targetBoundIdentifiers(
            *subjectDocument.captured,
            owners.constFirst(),
            subject.name,
            *kind,
            &targetDeclaration,
            &shadowScopes,
            &bindingFailure);
    if (!targetDeclaration.valid()
        || boundIdentifiers.isEmpty()) {
        return rejected(
            RtlRenamePlanStatus::AmbiguousStructure,
            bindingFailure.isEmpty()
                ? QStringLiteral(
                      "Tree-sitter could not prove the "
                      "subject declaration binding.")
                : bindingFailure,
            query);
    }

    QList<PendingEdit> pendingEdits;
    for (const TreeIdentifier& identifier :
         boundIdentifiers) {
        const bool declaration =
            identifier.start == targetDeclaration.start
            && identifier.end == targetDeclaration.end;
        QString failure;
        if (!appendEdit(
                &pendingEdits,
                subjectDocument,
                identifier.start,
                identifier.end,
                newName,
                declaration
                    ? QStringLiteral(
                          "declaration.name")
                    : QStringLiteral(
                          "reference.bound"),
                declaration
                    ? QStringLiteral(
                          "Rename the Slang-selected "
                          "declaration.")
                    : QStringLiteral(
                          "Rename a Tree-sitter "
                          "scope-bound reference."),
                &failure)) {
            return rejected(
                RtlRenamePlanStatus::
                    AmbiguousStructure,
                failure, query);
        }
    }

    // An implicit .name connection has one token for the child formal and
    // the parent actual. When the selected symbol is the parent actual but
    // the child formal belongs to a different module, make the actual
    // explicit instead of silently rebinding it.
    if (*kind == RenameKind::Port) {
        for (auto factsIt = treeFacts.constBegin();
             factsIt != treeFacts.constEnd();
             ++factsIt) {
            const QString& file = factsIt.key();
            for (const TreeInstance& tree :
                 factsIt->instances) {
                if (tree.ownerName
                        != subject.owner.name
                    || tree.moduleType
                        == subject.owner.name) {
                    continue;
                }
                const QList<SemanticSymbolRecord>
                    semanticMatches =
                        matchingSemanticInstances(
                            snapshot, file, tree);
                if (semanticMatches.size() != 1)
                    continue;
                for (const TreeAssociation& association :
                     tree.associations) {
                    if (association.kind
                            != TreeAssociation::Kind::Port
                        || !association.implicitPort
                        || association.formalName
                            != subject.name) {
                        continue;
                    }
                    if (!instancePinProof(
                            snapshot,
                            semanticMatches.constFirst(),
                            association,
                            subject.name)) {
                        return rejected(
                            RtlRenamePlanStatus::
                                IncompleteSemanticBinding,
                            QStringLiteral(
                                "An implicit parent actual "
                                "lacks a Slang InstPin "
                                "binding."),
                            query);
                    }
                    QString failure;
                    if (!appendEdit(
                            &pendingEdits,
                            boundDocuments.value(file),
                            association.formalStart,
                            association.formalEnd,
                            subject.name
                                + QLatin1Char('(')
                                + newName
                                + QLatin1Char(')'),
                            QStringLiteral(
                                "instance.port."
                                "implicitActual"),
                            QStringLiteral(
                                "Preserve a renamed parent "
                                "actual in an implicit named "
                                "connection."),
                            &failure)) {
                        return rejected(
                            RtlRenamePlanStatus::
                                AmbiguousStructure,
                            failure, query);
                    }
                }
            }
        }
    }

    // Every Slang-resolved instance of the owning module must still have one
    // structurally identical live instance. This rejects added/removed or
    // otherwise ambiguous unsaved instance structure.
    QSet<QString> matchedInstanceKeys;
    for (const SemanticSymbolRecord& instance :
         instances) {
        const QString file =
            normalizedFileName(
                instance.location.fileName);
        const QList<const TreeInstance*> matches =
            matchingTreeInstances(
                treeFacts.value(file).instances,
                instance);
        if (matches.size() != 1
            || !matches.constFirst()->complete) {
            return rejected(
                RtlRenamePlanStatus::
                    IncompleteSemanticBinding,
                QStringLiteral(
                    "A Slang-resolved instance cannot be "
                    "mapped uniquely to the live "
                    "Tree-sitter snapshot."),
                query);
        }
        const TreeInstance& tree =
            *matches.constFirst();
        const std::optional<TreeInstance>
            savedInstance =
                savedTreeInstance(
                    snapshot, instance);
        if (!savedInstance
            || !associationBindingsUnchanged(
                *savedInstance,
                tree,
                *kind == RenameKind::Port
                    ? TreeAssociation::Kind::Port
                    : TreeAssociation::Kind::
                          Parameter)) {
            return rejected(
                RtlRenamePlanStatus::
                    IncompleteSemanticBinding,
                QStringLiteral(
                    "A live instance association set no "
                    "longer matches the captured Slang "
                    "generation."),
                query);
        }
        const QString instanceKey =
            file + QLatin1Char(':')
            + tree.ownerName + QLatin1Char(':')
            + tree.moduleType + QLatin1Char(':')
            + tree.instanceName;
        if (matchedInstanceKeys.contains(instanceKey)) {
            return rejected(
                RtlRenamePlanStatus::
                    AmbiguousStructure,
                QStringLiteral(
                    "Multiple semantic instances map to "
                    "the same structural instance."),
                query);
        }
        matchedInstanceKeys.insert(instanceKey);

        if ((*kind == RenameKind::Port
             && tree.hasOrderedPorts)
            || (*kind == RenameKind::Parameter
                && tree.hasOrderedParameters)) {
            return rejected(
                RtlRenamePlanStatus::
                    OrderedConnection,
                QStringLiteral(
                    "Ordered instance connections cannot "
                    "be renamed safely."),
                query);
        }

        const TreeAssociation::Kind associationKind =
            *kind == RenameKind::Port
                ? TreeAssociation::Kind::Port
                : TreeAssociation::Kind::Parameter;
        for (const TreeAssociation& association :
             tree.associations) {
            if (association.kind != associationKind
                || association.formalName
                       != subject.name) {
                continue;
            }
            if (*kind == RenameKind::Port
                && !instancePinProof(
                    snapshot,
                    instance,
                    association,
                    subject.name)) {
                return rejected(
                    RtlRenamePlanStatus::
                        IncompleteSemanticBinding,
                    QStringLiteral(
                        "A named port connection lacks a "
                        "Slang InstPin binding."),
                    query);
            }

            QString replacement = newName;
            if (*kind == RenameKind::Port
                && association.implicitPort) {
                // The single token denotes both the child formal and the
                // parent actual. Preserve the old parent actual explicitly.
                replacement =
                    newName
                    + QLatin1Char('(')
                    + subject.name
                    + QLatin1Char(')');
                if (association.ownerName
                        == subject.owner.name) {
                    // Recursive instance: both bindings are the selected
                    // port, so the implicit connection remains valid.
                    replacement = newName;
                }
            }

            QString failure;
            if (!appendEdit(
                    &pendingEdits,
                    boundDocuments.value(file),
                    association.formalStart,
                    association.formalEnd,
                    replacement,
                    *kind == RenameKind::Port
                        ? (association.implicitPort
                               ? QStringLiteral(
                                     "instance.port."
                                     "implicit")
                               : QStringLiteral(
                                     "instance.port."
                                     "formal"))
                        : QStringLiteral(
                              "instance.parameter.formal"),
                    *kind == RenameKind::Port
                        ? QStringLiteral(
                              "Synchronize a Slang-bound "
                              "named port connection.")
                        : QStringLiteral(
                              "Synchronize a named "
                              "parameter override on a "
                              "Slang-resolved instance."),
                    &failure)) {
                return rejected(
                    RtlRenamePlanStatus::
                        AmbiguousStructure,
                    failure, query);
            }
        }
    }

    // The inverse check is equally important for unsaved buffers: an added
    // live instance has no Slang binding in the captured generation and must
    // not be silently omitted from a workspace-wide rename.
    if (*kind != RenameKind::Localparam) {
        for (auto factsIt = treeFacts.constBegin();
             factsIt != treeFacts.constEnd();
             ++factsIt) {
            for (const TreeInstance& tree :
                 factsIt->instances) {
                if (tree.moduleType
                    != subject.owner.name) {
                    continue;
                }
                const QList<SemanticSymbolRecord>
                    semanticMatches =
                        matchingSemanticInstances(
                            snapshot,
                            factsIt.key(),
                            tree);
                if (semanticMatches.size() != 1) {
                    return rejected(
                        RtlRenamePlanStatus::
                            IncompleteSemanticBinding,
                        QStringLiteral(
                            "A live instance is absent or "
                            "ambiguous in the captured "
                            "Slang generation."),
                        query);
                }
            }
        }
    }

    if (pendingEdits.isEmpty()) {
        return rejected(
            RtlRenamePlanStatus::AmbiguousStructure,
            QStringLiteral(
                "No verified rename edits were produced."),
            query);
    }

    std::sort(
        pendingEdits.begin(), pendingEdits.end(),
        [](const PendingEdit& left,
           const PendingEdit& right) {
            const QString leftFile =
                normalizedFileName(left.fileName);
            const QString rightFile =
                normalizedFileName(right.fileName);
            if (leftFile != rightFile)
                return leftFile < rightFile;
            return left.start < right.start;
        });

    std::vector<rtledit::WorkspaceTextEdit> edits;
    std::vector<rtledit::TextEditProvenance> provenance;
    edits.reserve(
        static_cast<std::size_t>(
            pendingEdits.size()));
    provenance.reserve(edits.capacity());
    const std::string semanticId =
        std::to_string(query.semanticToken.revision);
    const QString action = actionId(*kind);
    for (const PendingEdit& pending :
         std::as_const(pendingEdits)) {
        rtledit::WorkspaceTextEdit edit;
        edit.filePath =
            utf8String(pending.fileName);
        edit.expectedDocumentVersion = {
            pending.revision};
        edit.range = utf8Range(
            pending.documentText,
            pending.start,
            pending.end);
        edit.expectedText =
            utf8String(pending.expectedText);
        edit.newText =
            utf8String(pending.newText);
        edits.push_back(std::move(edit));

        rtledit::TextEditProvenance item;
        item.editIndex = provenance.size();
        item.actionId = utf8String(action);
        item.anchorName =
            utf8String(pending.anchorName);
        item.description =
            utf8String(pending.description);
        item.anchor.source =
            rtledit::AnchorResolutionSource::
                TreeSitter;
        item.anchor.resolver =
            "ZeroSlack.RtlRenamePlanner/TSDocument";
        item.anchor.semanticSnapshotId =
            semanticId;
        item.signalQualifiedName =
            utf8String(subject.owner.name
                       + QLatin1Char('.')
                       + subject.name);
        item.sourceFilePath =
            utf8String(pending.fileName);
        item.sourceRange = utf8Range(
            pending.documentText,
            pending.start,
            pending.end);
        provenance.push_back(std::move(item));
    }

    rtledit::SemanticEditIntent intent;
    intent.kind =
        rtledit::SemanticEditKind::ReplaceText;
    intent.target.kind =
        *kind == RenameKind::Port
            ? rtledit::SemanticObjectKind::Port
            : rtledit::SemanticObjectKind::Unknown;
    intent.target.qualifiedName =
        utf8String(subject.owner.name
                   + QLatin1Char('.')
                   + subject.name);
    intent.target.ownerScope =
        utf8String(subject.owner.name);
    intent.target.filePath =
        utf8String(subjectDocument.captured->fileName);
    intent.target.range = utf8Range(
        subjectDocument.captured->text,
        targetDeclaration.start,
        targetDeclaration.end);
    intent.target.signatureHash =
        utf8String(recordIdentity(subject));

    RtlRenameProposal proposal;
    proposal.subject = subject;
    proposal.oldName = subject.name;
    proposal.newName = newName;
    proposal.dryRun = query.dryRun;
    proposal.workspaceEdit =
        rtledit::makeWorkspaceEditPlan(
            std::move(intent),
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            std::move(edits),
            std::move(provenance));
    proposal.workspaceEdit.semanticSnapshot =
        rtledit::SemanticIndexSnapshot{
            semanticId};

    QSet<QString> semanticFiles;
    for (const PendingEdit& edit :
         std::as_const(pendingEdits)) {
        semanticFiles.insert(
            normalizedFileName(edit.fileName));
    }
    QStringList sortedFiles =
        semanticFiles.values();
    std::sort(sortedFiles.begin(),
              sortedFiles.end());
    for (const QString& file :
         std::as_const(sortedFiles)) {
        proposal.workspaceEdit
            .semanticIndexFilePaths
            .push_back(utf8String(file));
    }

    proposal.transaction =
        WorkspaceEditTransactionService::
            getInstance()->prepare(
                proposal.workspaceEdit,
                rtledit::SemanticIndexSnapshot{
                    semanticId},
                documentManager,
                query.dryRun);
    proposal.sourceDiff =
        proposal.transaction.sourceDiff;
    if (!proposal.transaction.ready()
        || !proposal.sourceDiff.built()) {
        return rejected(
            RtlRenamePlanStatus::
                TransactionPreparationFailed,
            QStringLiteral(
                "The unified workspace transaction could "
                "not build an atomic preview."),
            query);
    }

    proposal.renderedDiff =
        fromUtf8String(
            rtledit::
                renderWorkspaceEditSourceDiffHunks(
                    proposal.sourceDiff));
    QMap<QString, RtlRenameFilePreview>
        previews;
    for (const PendingEdit& edit :
         std::as_const(pendingEdits)) {
        const QString file =
            normalizedFileName(edit.fileName);
        RtlRenameFilePreview& preview =
            previews[file];
        preview.fileName = edit.fileName;
        preview.revision = edit.revision;
        ++preview.editCount;
    }
    proposal.files = previews.values();
    proposal.status = RtlRenamePlanStatus::Ready;
    proposal.message =
        QStringLiteral(
            "High-risk RTL rename proposal ready for "
            "preview.");
    return proposal;
}

SemanticIndex* RtlRenamePlanner::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}
