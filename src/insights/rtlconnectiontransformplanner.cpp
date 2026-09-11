#include "rtlconnectiontransformplanner.h"

#include "semanticindexsnapshot.h"
#include "tsdocument.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr char kActionId[] = "rtl.connection.transform";

struct ConnectionNode {
    TSNode node{};
    TSNode expression{};
    QString formalName;
    bool named = false;
};

struct ConnectionSyntax {
    TSNode instantiation{};
    TSNode hierarchy{};
    TSNode connections{};
    TSNode closeParen{};
    QList<ConnectionNode> items;
    int commaCount = 0;
    bool named = false;
    bool ordered = false;
};

struct CharacterRange {
    int start = -1;
    int end = -1;
};

struct FormalFact {
    SemanticSymbolRecord record;
    SemanticElaboratedSymbolInfo type;
    SemanticDeclaredTypeFacts declaredType;
};

struct ActualFact {
    SemanticSymbolRecord record;
    SemanticElaboratedSymbolInfo type;
    SemanticDeclaredTypeFacts declaredType;
};

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

std::string utf8String(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(
        bytes.constData(),
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

int nodeStartChar(TSNode node)
{
    return ts_node_is_null(node)
        ? -1
        : static_cast<int>(ts_node_start_byte(node) / 2u);
}

int nodeEndChar(TSNode node)
{
    return ts_node_is_null(node)
        ? -1
        : static_cast<int>(ts_node_end_byte(node) / 2u);
}

std::optional<CharacterRange> obsoleteConnectionRange(
    const QString& text,
    const ConnectionSyntax& syntax,
    const ConnectionNode& item,
    bool keepPrecedingDelimiter)
{
    const int itemStart = nodeStartChar(item.node);
    const int itemEnd = nodeEndChar(item.node);
    if (itemStart < 0 || itemEnd <= itemStart)
        return std::nullopt;
    if (keepPrecedingDelimiter)
        return CharacterRange{itemStart, itemEnd};

    const uint32_t count = ts_node_child_count(syntax.connections);
    int itemChild = -1;
    for (uint32_t index = 0; index < count; ++index) {
        if (ts_node_eq(ts_node_child(syntax.connections, index), item.node)) {
            itemChild = static_cast<int>(index);
            break;
        }
    }
    if (itemChild < 0)
        return std::nullopt;

    int start = itemStart;
    int end = itemEnd;
    for (int index = itemChild + 1;
         index < static_cast<int>(count); ++index) {
        const TSNode child = ts_node_child(syntax.connections,
                                          static_cast<uint32_t>(index));
        if (nodeTypeIs(child, ",")) {
            end = nodeEndChar(child);
            while (end < text.size()
                   && (text.at(end) == QLatin1Char(' ')
                       || text.at(end) == QLatin1Char('\t'))) {
                ++end;
            }
            return CharacterRange{start, end};
        }
        if (ts_node_is_named(child)
            && !nodeTypeIs(child, "one_line_comment")
            && !nodeTypeIs(child, "block_comment"))
            break;
    }

    for (int index = itemChild - 1; index >= 0; --index) {
        const TSNode child = ts_node_child(syntax.connections,
                                          static_cast<uint32_t>(index));
        if (nodeTypeIs(child, ",")) {
            start = nodeStartChar(child);
            while (start > 0
                   && (text.at(start - 1) == QLatin1Char(' ')
                       || text.at(start - 1) == QLatin1Char('\t'))) {
                --start;
            }
            return CharacterRange{start, end};
        }
        if (ts_node_is_named(child)
            && !nodeTypeIs(child, "one_line_comment")
            && !nodeTypeIs(child, "block_comment"))
            break;
    }
    return CharacterRange{start, end};
}

QList<CharacterRange> mergedCharacterRanges(
    QList<CharacterRange> ranges)
{
    std::sort(ranges.begin(), ranges.end(),
              [](const CharacterRange& left,
                 const CharacterRange& right) {
                  return left.start < right.start
                      || (left.start == right.start
                          && left.end < right.end);
              });
    QList<CharacterRange> result;
    for (const CharacterRange& range : std::as_const(ranges)) {
        if (range.start < 0 || range.end <= range.start)
            continue;
        if (!result.isEmpty() && range.start <= result.last().end) {
            result.last().end = qMax(result.last().end, range.end);
        } else {
            result.append(range);
        }
    }
    return result;
}

QString nodeText(const QString& text, TSNode node)
{
    const int start = nodeStartChar(node);
    const int end = nodeEndChar(node);
    if (start < 0 || end < start || end > text.size())
        return {};
    return text.mid(start, end - start);
}

TSNode childByField(TSNode node, const char* field)
{
    return ts_node_is_null(node) || !field
        ? TSNode{}
        : ts_node_child_by_field_name(
              node, field, std::strlen(field));
}

QList<TSNode> directNamedChildren(TSNode node)
{
    QList<TSNode> result;
    const uint32_t count = ts_node_named_child_count(node);
    result.reserve(static_cast<qsizetype>(count));
    for (uint32_t index = 0; index < count; ++index)
        result.append(ts_node_named_child(node, index));
    return result;
}

QList<TSNode> directNamedChildrenOfType(
    TSNode node,
    const char* expected)
{
    QList<TSNode> result;
    for (const TSNode child : directNamedChildren(node)) {
        if (nodeTypeIs(child, expected))
            result.append(child);
    }
    return result;
}

TSNode firstDirectNamedChildOfType(
    TSNode node,
    const char* expected)
{
    for (const TSNode child : directNamedChildren(node)) {
        if (nodeTypeIs(child, expected))
            return child;
    }
    return {};
}

bool commentNode(TSNode node)
{
    return nodeTypeIs(node, "one_line_comment")
        || nodeTypeIs(node, "block_comment");
}

bool unsupportedAssociationSubtree(TSNode root)
{
    QList<TSNode> pending{root};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        if (nodeTypeIs(node, "attribute_instance")
            || nodeTypeIs(node, "text_macro_usage")
            || nodeTypeIs(node, "text_macro_definition")
            || nodeTypeIs(node, "conditional_compilation_directive")
            || commentNode(node)
            || ts_node_is_missing(node)
            || ts_node_is_error(node)) {
            return true;
        }
        const uint32_t count = ts_node_named_child_count(node);
        for (uint32_t index = 0; index < count; ++index)
            pending.append(ts_node_named_child(node, index));
    }
    return false;
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

RtlConnectionTransformReport rejected(
    RtlConnectionTransformFailure failure,
    const QString& message)
{
    RtlConnectionTransformReport report;
    report.failure = failure;
    report.message = message;
    return report;
}

rtledit::SourcePosition utf8PositionAt(
    const QString& text,
    int charOffset)
{
    const int bounded = qBound(0, charOffset, text.size());
    int line = 0;
    int lineStart = 0;
    for (int index = 0; index < bounded; ++index) {
        if (text.at(index) != QLatin1Char('\n'))
            continue;
        ++line;
        lineStart = index + 1;
    }
    const QByteArray columnBytes =
        text.mid(lineStart, bounded - lineStart).toUtf8();
    return {
        static_cast<std::size_t>(line),
        static_cast<std::size_t>(columnBytes.size())};
}

rtledit::SourceRange utf8Range(
    const QString& text,
    int startChar,
    int endChar)
{
    const int start = qBound(0, startChar, text.size());
    const int end = qBound(start, endChar, text.size());
    return {
        utf8PositionAt(text, start),
        utf8PositionAt(text, end)};
}

bool sameMachineType(
    const SemanticElaboratedSymbolInfo& left,
    const SemanticElaboratedSymbolInfo& right)
{
    return left.available == right.available
        && left.fixedSize == right.fixedSize
        && left.integral == right.integral
        && left.unpackedArray == right.unpackedArray
        && left.interfaceType == right.interfaceType
        && left.signedIntegral == right.signedIntegral
        && left.bitWidth == right.bitWidth
        && left.interfaceName == right.interfaceName
        && left.modportName == right.modportName;
}

std::optional<SemanticElaboratedSymbolInfo> exactInstanceType(
    const SemanticSymbolRecord& record,
    const QString& instancePath,
    bool* differsAcrossInstances)
{
    if (differsAcrossInstances)
        *differsAcrossInstances = false;

    const auto& instances =
        record.presentation.instanceInfoByPath;
    if (instances.isEmpty())
        return std::nullopt;
    const auto selected = instances.constFind(instancePath);
    if (selected == instances.constEnd()
        || !selected->available) {
        return std::nullopt;
    }
    for (auto it = instances.constBegin();
         it != instances.constEnd(); ++it) {
        if (!it->available
            || !sameMachineType(selected.value(), it.value())) {
            if (differsAcrossInstances)
                *differsAcrossInstances = true;
            break;
        }
    }
    return selected.value();
}

std::optional<SemanticDeclaredTypeFacts> exactDeclaredType(
    const SemanticSymbolRecord& record,
    const QString& instancePath,
    bool* differsAcrossInstances)
{
    if (differsAcrossInstances)
        *differsAcrossInstances = false;
    const auto& instances =
        record.presentation.declaredTypeFactsByPath;
    if (instances.isEmpty())
        return std::nullopt;
    const auto selected = instances.constFind(instancePath);
    if (selected == instances.constEnd()
        || !selected->complete) {
        return std::nullopt;
    }
    for (auto it = instances.constBegin();
         it != instances.constEnd(); ++it) {
        if (!it->complete
            || it->rootTypeId
                   != selected->rootTypeId
            || it->canonicalTypeId
                   != selected->canonicalTypeId
            || it->declarationShapeId
                   != selected->declarationShapeId
            || it->signednessKnown
                   != selected->signednessKnown
            || it->signedIntegral
                   != selected->signedIntegral) {
            if (differsAcrossInstances)
                *differsAcrossInstances = true;
            break;
        }
    }
    return selected.value();
}

QStringList elaboratedPathsForSourceInstance(
    const SemanticIndexSnapshot& snapshot,
    const QString& targetModuleName,
    const QString& instanceName)
{
    QSet<QString> paths;
    const QString suffix = QLatin1Char('.') + instanceName;
    for (const SemanticSymbolRecord& record :
         snapshot.symbolRecordsView()) {
        if (record.owner.name != targetModuleName
            || record.declarationKind
                   != SymbolTaxonomy::DeclarationKind::Port) {
            continue;
        }
        for (const QString& path :
             record.presentation.instanceInfoByPath.keys()) {
            if (path.endsWith(suffix))
                paths.insert(path);
        }
        for (const QString& path :
             record.presentation.declaredTypeFactsByPath.keys()) {
            if (path.endsWith(suffix))
                paths.insert(path);
        }
    }
    QStringList result = paths.values();
    std::sort(result.begin(), result.end());
    return result;
}

bool sourceMappedName(
    const QString& text,
    const SemanticSymbolRecord& record)
{
    return record.location.position >= 0
        && record.location.length > 0
        && record.location.position + record.location.length
               <= text.size()
        && text.mid(record.location.position,
                    record.location.length)
               == record.name;
}

bool plainSystemVerilogIdentifier(const QString& name)
{
    if (name.isEmpty())
        return false;
    const auto letterOrUnderscore = [](QChar character) {
        const ushort value = character.unicode();
        return (value >= 'A' && value <= 'Z')
            || (value >= 'a' && value <= 'z')
            || character == QLatin1Char('_');
    };
    if (!letterOrUnderscore(name.front()))
        return false;
    for (qsizetype index = 1;
         index < name.size(); ++index) {
        const QChar character = name.at(index);
        const ushort value = character.unicode();
        if (!letterOrUnderscore(character)
            && !(value >= '0' && value <= '9')
            && character != QLatin1Char('$')) {
            return false;
        }
    }
    return true;
}

std::optional<ConnectionSyntax> connectionSyntaxAt(
    const TSDocument& document,
    const SemanticSymbolRecord& instance,
    RtlConnectionTransformFailure* failure,
    QString* message)
{
    const QString& text = document.text();
    if (!sourceMappedName(text, instance)) {
        if (failure)
            *failure =
                RtlConnectionTransformFailure::
                    InstanceSemanticMismatch;
        if (message) {
            *message = QStringLiteral(
                "The Slang instance declaration no longer maps to "
                "the current Tree-sitter buffer.");
        }
        return std::nullopt;
    }

    const uint32_t byte =
        static_cast<uint32_t>(instance.location.position) * 2u;
    TSNode node = ts_node_descendant_for_byte_range(
        document.rootNode(), byte, byte);
    TSNode instantiation =
        ancestorOfType(node, "module_instantiation");
    if (ts_node_is_null(instantiation)
        || ts_node_has_error(instantiation)) {
        if (failure)
            *failure =
                RtlConnectionTransformFailure::InstanceNotFound;
        if (message) {
            *message = QStringLiteral(
                "Tree-sitter did not resolve a complete module "
                "instantiation at the Slang source anchor.");
        }
        return std::nullopt;
    }

    const TSNode typeNode =
        childByField(instantiation, "instance_type");
    if (ts_node_is_null(typeNode)
        || nodeText(text, typeNode)
               != instance.type.resolvedTypeName) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    InstanceSemanticMismatch;
        }
        if (message) {
            *message = QStringLiteral(
                "Slang and Tree-sitter disagree about the instantiated "
                "module type.");
        }
        return std::nullopt;
    }

    TSNode selectedHierarchy{};
    for (const TSNode hierarchy :
         directNamedChildrenOfType(
             instantiation, "hierarchical_instance")) {
        const TSNode nameOfInstance =
            firstDirectNamedChildOfType(
                hierarchy, "name_of_instance");
        TSNode nameNode{};
        if (!ts_node_is_null(nameOfInstance)) {
            for (const TSNode child :
                 directNamedChildren(nameOfInstance)) {
                if (nodeTypeIs(child, "simple_identifier")
                    || nodeTypeIs(
                        child, "escaped_identifier")) {
                    nameNode = child;
                    break;
                }
            }
        }
        if (ts_node_is_null(nameNode)
            || nodeStartChar(nameNode)
                   != instance.location.position
            || nodeText(text, nameNode) != instance.name) {
            continue;
        }
        if (!ts_node_is_null(selectedHierarchy)) {
            if (failure) {
                *failure =
                    RtlConnectionTransformFailure::
                        InstanceSemanticMismatch;
            }
            if (message) {
                *message = QStringLiteral(
                    "The source anchor maps to more than one "
                    "hierarchical instance.");
            }
            return std::nullopt;
        }
        selectedHierarchy = hierarchy;
    }
    if (ts_node_is_null(selectedHierarchy)
        || ts_node_has_error(selectedHierarchy)) {
        if (failure)
            *failure =
                RtlConnectionTransformFailure::InstanceNotFound;
        if (message) {
            *message = QStringLiteral(
                "The selected hierarchical instance has no exact "
                "Tree-sitter source node.");
        }
        return std::nullopt;
    }

    const TSNode connections =
        firstDirectNamedChildOfType(
            selectedHierarchy, "list_of_port_connections");
    if (ts_node_is_null(connections)
        || ts_node_has_error(connections)) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    UnsupportedConnectionSyntax;
        }
        if (message) {
            *message = QStringLiteral(
                "The instance port connection list is incomplete.");
        }
        return std::nullopt;
    }

    ConnectionSyntax result;
    result.instantiation = instantiation;
    result.hierarchy = selectedHierarchy;
    result.connections = connections;

    const uint32_t hierarchyChildCount =
        ts_node_child_count(selectedHierarchy);
    for (uint32_t index = 0;
         index < hierarchyChildCount; ++index) {
        const TSNode child =
            ts_node_child(selectedHierarchy, index);
        if (nodeTypeIs(child, ")"))
            result.closeParen = child;
    }
    if (ts_node_is_null(result.closeParen)) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    UnsupportedConnectionSyntax;
        }
        if (message) {
            *message = QStringLiteral(
                "The instance closing parenthesis is unavailable.");
        }
        return std::nullopt;
    }

    const uint32_t childCount =
        ts_node_child_count(connections);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child =
            ts_node_child(connections, index);
        if (!ts_node_is_named(child)) {
            if (nodeTypeIs(child, ","))
                ++result.commaCount;
            continue;
        }
        if (commentNode(child))
            continue;

        ConnectionNode item;
        item.node = child;
        if (nodeTypeIs(child, "ordered_port_connection")) {
            result.ordered = true;
            item.expression =
                firstDirectNamedChildOfType(
                    child, "expression");
            if (ts_node_is_null(item.expression)
                || unsupportedAssociationSubtree(child)) {
                if (failure) {
                    *failure =
                        RtlConnectionTransformFailure::
                            UnsupportedConnectionSyntax;
                }
                if (message) {
                    *message = QStringLiteral(
                        "An ordered connection contains an attribute, "
                        "directive, macro, comment, or incomplete "
                        "expression.");
                }
                return std::nullopt;
            }
        } else if (nodeTypeIs(
                       child, "named_port_connection")) {
            result.named = true;
            item.named = true;
            const TSNode portName =
                childByField(child, "port_name");
            if (ts_node_is_null(portName)) {
                if (failure) {
                    *failure =
                        RtlConnectionTransformFailure::
                            WildcardConnection;
                }
                if (message) {
                    *message = QStringLiteral(
                        "Wildcard .* connections cannot be transformed "
                        "safely.");
                }
                return std::nullopt;
            }
            item.formalName = nodeText(text, portName);
            item.expression =
                childByField(child, "connection");
            if (unsupportedAssociationSubtree(child)) {
                if (failure) {
                    *failure =
                        RtlConnectionTransformFailure::
                            UnsupportedConnectionSyntax;
                }
                if (message) {
                    *message = QStringLiteral(
                        "A named connection contains an attribute, "
                        "directive, macro, comment, or incomplete "
                        "expression.");
                }
                return std::nullopt;
            }
        } else {
            if (failure) {
                *failure =
                    RtlConnectionTransformFailure::
                        UnsupportedConnectionSyntax;
            }
            if (message) {
                *message = QStringLiteral(
                    "The connection list contains an unsupported "
                    "Tree-sitter node.");
            }
            return std::nullopt;
        }
        result.items.append(item);
    }

    if (result.named && result.ordered) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::MixedConnectionStyle;
        }
        if (message) {
            *message = QStringLiteral(
                "Mixed ordered and named connections are rejected.");
        }
        return std::nullopt;
    }
    if (result.ordered
        && result.commaCount
               != qMax(
                   0,
                   static_cast<int>(
                       result.items.size()) - 1)) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    UnsupportedConnectionSyntax;
        }
        if (message) {
            *message = QStringLiteral(
                "Empty or trailing ordered connection slots are not "
                "safe to renumber.");
        }
        return std::nullopt;
    }
    return result;
}

bool portCollector(SymbolTaxonomy::CollectorKind kind)
{
    using Kind = SymbolTaxonomy::CollectorKind;
    return kind == Kind::PortInput
        || kind == Kind::PortOutput
        || kind == Kind::PortInout
        || kind == Kind::PortRef
        || kind == Kind::PortInterface
        || kind == Kind::PortInterfaceModport;
}

std::optional<QList<FormalFact>> formalFacts(
    const SemanticIndexSnapshot& snapshot,
    const SemanticSymbolRecord& instance,
    const QString& selectedInstancePath,
    RtlConnectionTransformFailure* failure,
    QString* message)
{
    const QString targetModule =
        instance.type.resolvedTypeName;
    QList<SemanticSymbolRecord> definitions;
    for (const SemanticSymbolRecord& candidate :
         snapshot.getSymbolRecordsByName(targetModule)) {
        if (candidate.declarationKind
                == SymbolTaxonomy::DeclarationKind::Module
            && candidate.name == targetModule) {
            definitions.append(candidate);
        }
    }
    if (definitions.size() != 1) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    AmbiguousModuleDefinition;
        }
        if (message) {
            *message = QStringLiteral(
                "Slang did not publish exactly one target module "
                "definition.");
        }
        return std::nullopt;
    }
    const QString definitionFile =
        normalizedFileName(
            definitions.constFirst().location.fileName);

    QList<SemanticSymbolRecord> ports;
    for (const SemanticSymbolRecord& candidate :
         snapshot.getSymbolRecordsByOwner(targetModule)) {
        if (candidate.declarationKind
                != SymbolTaxonomy::DeclarationKind::Port
            || !portCollector(candidate.collectorKind)
            || normalizedFileName(
                   candidate.location.fileName)
                   != definitionFile) {
            continue;
        }
        ports.append(candidate);
    }
    std::sort(
        ports.begin(), ports.end(),
        [](const SemanticSymbolRecord& left,
           const SemanticSymbolRecord& right) {
            if (left.location.position
                != right.location.position) {
                return left.location.position
                    < right.location.position;
            }
            return left.name < right.name;
        });

    QSet<QString> names;
    QSet<int> positions;
    QList<FormalFact> result;
    for (const SemanticSymbolRecord& port : ports) {
        if (!plainSystemVerilogIdentifier(port.name)
            || names.contains(port.name)
            || positions.contains(
                port.location.position)) {
            if (failure) {
                *failure =
                    RtlConnectionTransformFailure::
                        AmbiguousFormal;
            }
            if (message) {
                *message = QStringLiteral(
                    "Slang formal names or declaration-order anchors "
                    "are ambiguous.");
            }
            return std::nullopt;
        }
        names.insert(port.name);
        positions.insert(port.location.position);

        bool differs = false;
        const auto type = exactInstanceType(
            port, selectedInstancePath, &differs);
        if (differs) {
            if (failure) {
                *failure =
                    RtlConnectionTransformFailure::
                        MultiInstanceTypeDifference;
            }
            if (message) {
                *message = QStringLiteral(
                    "The target module has different Slang formal "
                    "types across elaborated instances.");
            }
            return std::nullopt;
        }
        if (!type) {
            if (failure) {
                *failure =
                    RtlConnectionTransformFailure::
                        MissingFormalType;
            }
            if (message) {
                *message = QStringLiteral(
                    "Slang did not publish an exact machine type for "
                    "every formal in the selected instance.");
            }
            return std::nullopt;
        }

        FormalFact fact;
        fact.record = port;
        fact.type = *type;
        bool declaredTypeDiffers = false;
        const auto declaredType =
            exactDeclaredType(
                port,
                selectedInstancePath,
                &declaredTypeDiffers);
        if (declaredTypeDiffers) {
            if (failure) {
                *failure =
                    RtlConnectionTransformFailure::
                        MultiInstanceTypeDifference;
            }
            if (message) {
                *message = QStringLiteral(
                    "The target module has different Slang declared "
                    "type shapes across elaborated instances.");
            }
            return std::nullopt;
        }
        if (!declaredType) {
            if (failure) {
                *failure =
                    RtlConnectionTransformFailure::
                        MissingFormalType;
            }
            if (message) {
                *message = QStringLiteral(
                    "Slang did not publish complete declared-type facts "
                    "for every formal.");
            }
            return std::nullopt;
        }
        fact.declaredType = *declaredType;
        result.append(std::move(fact));
    }
    return result;
}

bool actualCandidate(
    const SemanticSymbolRecord& record,
    const QString& name,
    const QString& ownerModule)
{
    using DeclarationKind =
        SymbolTaxonomy::DeclarationKind;
    return record.name == name
        && record.owner.kind
            == SymbolTaxonomy::SymbolOwnerScope::Module
        && record.owner.name == ownerModule
        && (record.declarationKind
                == DeclarationKind::Signal
            || record.declarationKind
                == DeclarationKind::Port);
}

std::optional<ActualFact> exactActualFact(
    const SemanticIndexSnapshot& snapshot,
    const QString& actualName,
    const QString& ownerModule,
    const QString& parentInstancePath,
    RtlConnectionTransformFailure* failure,
    QString* message)
{
    QList<SemanticSymbolRecord> candidates;
    for (const SemanticSymbolRecord& record :
         snapshot.getSymbolRecordsByName(actualName)) {
        if (actualCandidate(
                record, actualName, ownerModule)) {
            candidates.append(record);
        }
    }
    if (candidates.size() != 1) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    AmbiguousActual;
        }
        if (message) {
            *message = candidates.isEmpty()
                ? QStringLiteral(
                      "No unique Slang signal backs the requested "
                      "actual connection.")
                : QStringLiteral(
                      "More than one Slang declaration can back the "
                      "requested actual connection.");
        }
        return std::nullopt;
    }

    bool differs = false;
    const auto type = exactInstanceType(
        candidates.constFirst(),
        parentInstancePath,
        &differs);
    if (!type || differs) {
        if (failure) {
            *failure = differs
                ? RtlConnectionTransformFailure::
                      MultiInstanceTypeDifference
                : RtlConnectionTransformFailure::
                      MissingActualType;
        }
        if (message) {
            *message = differs
                ? QStringLiteral(
                      "The actual signal has different Slang types "
                      "across elaborated parent instances.")
                : QStringLiteral(
                      "Slang did not publish an exact machine type for "
                      "the actual signal.");
        }
        return std::nullopt;
    }
    bool declaredTypeDiffers = false;
    const auto declaredType =
        exactDeclaredType(
            candidates.constFirst(),
            parentInstancePath,
            &declaredTypeDiffers);
    if (!declaredType || declaredTypeDiffers) {
        if (failure) {
            *failure = declaredTypeDiffers
                ? RtlConnectionTransformFailure::
                      MultiInstanceTypeDifference
                : RtlConnectionTransformFailure::
                      MissingActualType;
        }
        if (message) {
            *message = declaredTypeDiffers
                ? QStringLiteral(
                      "The actual signal has different Slang declared "
                      "type shapes across parent instances.")
                : QStringLiteral(
                      "Slang did not publish complete declared-type "
                      "facts for the actual signal.");
        }
        return std::nullopt;
    }
    return ActualFact{
        candidates.constFirst(),
        *type,
        *declaredType};
}

bool exactSimpleIdentifier(
    const TSDocument& syntax,
    TSNode expression,
    QString* identifier)
{
    if (!identifier || ts_node_is_null(expression))
        return false;
    const int start = nodeStartChar(expression);
    const int end = nodeEndChar(expression);
    if (start < 0 || end <= start)
        return false;
    const TSIdentifierTarget target =
        syntax.identifierAt(start);
    if (!target.ok()
        || target.startChar != start
        || target.endChar != end) {
        return false;
    }
    *identifier = target.text;
    return true;
}

bool castableMachineType(
    const SemanticElaboratedSymbolInfo& type)
{
    return type.available
        && type.fixedSize
        && type.integral
        && !type.unpackedArray
        && !type.interfaceType
        && type.bitWidth > 0;
}

std::optional<QString> renderedFormalCastType(
    const FormalFact& formal)
{
    const auto& facts = formal.declaredType;
    if (!facts.complete
        || facts.declarationBaseText.isEmpty()
        || !facts.typedefChain.isEmpty()
        || !facts.constants.complete
        || !facts.signednessKnown
        || facts.signedIntegral
               != formal.type.signedIntegral) {
        return std::nullopt;
    }

    QString result = facts.declarationBaseText;
    for (const SemanticTypeDimensionFact& dimension :
         facts.dimensions) {
        if (dimension.kind
                != SemanticTypeDimensionKind::Packed
            || !dimension.complete) {
            return std::nullopt;
        }
        if (!dimension.emitInDeclaration)
            continue;
        if (dimension.declarationText.isEmpty())
            return std::nullopt;
        result += QLatin1Char(' ');
        result += dimension.declarationText;
    }
    return result;
}

std::optional<QString> connectionActual(
    const TSDocument& syntax,
    const SemanticIndexSnapshot& snapshot,
    const SemanticSymbolRecord& instance,
    const RtlConnectionTransformRequest& request,
    const FormalFact& formal,
    TSNode expression,
    bool connectingMissingPort,
    bool* castInserted,
    RtlConnectionTransformFailure* failure,
    QString* message)
{
    if (castInserted)
        *castInserted = false;
    if (ts_node_is_null(expression))
        return QString();

    const QString original =
        nodeText(syntax.text(), expression);
    if (request.castPolicy
        == RtlExplicitCastPolicy::
            PreserveExistingExpression) {
        if (!connectingMissingPort)
            return original;
    }

    QString actualName;
    if (!exactSimpleIdentifier(
            syntax, expression, &actualName)) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    CastNotProvable;
        }
        if (message) {
            *message = QStringLiteral(
                "An explicit cast was requested, but the actual is not "
                "one exact Tree-sitter identifier.");
        }
        return std::nullopt;
    }
    const auto actual = exactActualFact(
        snapshot,
        actualName,
        instance.owner.name,
        request.parentInstancePath,
        failure,
        message);
    if (!actual)
        return std::nullopt;

    if (!castableMachineType(formal.type)
        || !castableMachineType(actual->type)) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    CastNotProvable;
        }
        if (message) {
            *message = QStringLiteral(
                "Slang did not prove fixed-size integral formal and "
                "actual types.");
        }
        return std::nullopt;
    }
    const bool sameType =
        formal.type.bitWidth == actual->type.bitWidth
        && formal.type.signedIntegral
               == actual->type.signedIntegral;
    if (sameType)
        return original;

    if (request.castPolicy
            != RtlExplicitCastPolicy::
                InsertWhenRequired
        || formal.record.collectorKind
            != SymbolTaxonomy::CollectorKind::PortInput) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    CastNotProvable;
        }
        if (message) {
            *message = QStringLiteral(
                "A width or signedness conversion is required, but a "
                "safe input-formal cast was not requested or cannot be "
                "used for this port direction.");
        }
        return std::nullopt;
    }

    const auto typeText =
        renderedFormalCastType(formal);
    if (!typeText) {
        if (failure) {
            *failure =
                RtlConnectionTransformFailure::
                    CastNotProvable;
        }
        if (message) {
            *message = QStringLiteral(
                "Slang did not provide a complete visible built-in "
                "target type for an explicit cast.");
        }
        return std::nullopt;
    }
    if (castInserted)
        *castInserted = true;
    return *typeText
        + QStringLiteral("'(")
        + original
        + QLatin1Char(')');
}

int lineStartChar(const QString& text, int line)
{
    if (line <= 0)
        return 0;
    int currentLine = 0;
    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) != QLatin1Char('\n'))
            continue;
        ++currentLine;
        if (currentLine == line)
            return index + 1;
    }
    return text.size();
}

QString lineIndentAt(const QString& text, int line)
{
    const int start = lineStartChar(text, line);
    int end = start;
    while (end < text.size()
           && (text.at(end) == QLatin1Char(' ')
               || text.at(end) == QLatin1Char('\t'))) {
        ++end;
    }
    return text.mid(start, end - start);
}

QString newlineBefore(const QString& text, int charOffset)
{
    return charOffset >= 2
            && text.mid(charOffset - 2, 2)
                   == QStringLiteral("\r\n")
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");
}

rtledit::WorkspaceTextEdit textEdit(
    const QString& fileName,
    rtledit::DocumentVersion version,
    const QString& text,
    int start,
    int end,
    const QString& replacement)
{
    return {
        utf8String(fileName),
        version,
        utf8Range(text, start, end),
        utf8String(text.mid(start, end - start)),
        utf8String(replacement)};
}

rtledit::TextEditProvenance provenance(
    std::size_t editIndex,
    const QString& anchorName,
    const QString& description,
    std::uint64_t generation,
    const SemanticSymbolRecord& instance,
    const QString& instancePath,
    const QString& fileName,
    const rtledit::SourceRange& range)
{
    rtledit::TextEditProvenance result;
    result.editIndex = editIndex;
    result.actionId = kActionId;
    result.anchorName = utf8String(anchorName);
    result.description = utf8String(description);
    result.anchor.source =
        rtledit::AnchorResolutionSource::TreeSitter;
    result.anchor.resolver =
        "ZeroSlack.RtlConnectionTransformPlanner";
    result.anchor.semanticSnapshotId =
        std::to_string(generation);
    result.sourceInstancePath =
        utf8String(instancePath);
    result.sourceFilePath = utf8String(fileName);
    result.sourceRange = range;
    result.signalQualifiedName =
        utf8String(instance.owner.name
                   + QLatin1Char('.') + instance.name);
    return result;
}

bool hasSlangErrorForFile(
    const SemanticIndexSnapshot& snapshot,
    const QString& fileName)
{
    const QString normalized = normalizedFileName(fileName);
    for (const SemanticDiagnostic& diagnostic :
         snapshot.getDiagnostics()) {
        if (diagnostic.severity
                == SemanticDiagnostic::Error
            && normalizedFileName(diagnostic.fileName)
                   == normalized) {
            return true;
        }
    }
    return false;
}

bool hasUnexpectedSlangErrorForFile(
    const SemanticIndexSnapshot& snapshot,
    const QString& fileName,
    const QList<ConnectionNode>& allowedObsoleteConnections,
    const QString& synchronizedModuleName,
    const QSet<QString>& synchronizedFormalNames)
{
    const QString normalized = normalizedFileName(fileName);
    for (const SemanticDiagnostic& diagnostic :
         snapshot.getDiagnostics()) {
        if (diagnostic.severity != SemanticDiagnostic::Error
            || normalizedFileName(diagnostic.fileName) != normalized) {
            continue;
        }

        bool expected = false;
        if (!synchronizedModuleName.isEmpty()) {
            const QString obsoletePortSuffix =
                QStringLiteral("does not exist in '%1'")
                    .arg(synchronizedModuleName);
            if (diagnostic.message.contains(obsoletePortSuffix,
                                            Qt::CaseSensitive)) {
                expected = true;
            }
            if (!expected) {
                for (const QString& formalName :
                     synchronizedFormalNames) {
                    if (diagnostic.message
                            == QStringLiteral("port '%1' has no connection")
                                   .arg(formalName)) {
                        expected = true;
                        break;
                    }
                }
            }
        }
        for (const ConnectionNode& connection :
             allowedObsoleteConnections) {
            const int start = nodeStartChar(connection.node);
            const int end = nodeEndChar(connection.node);
            bool rangeMatches = false;
            for (const SemanticSourceRange& range : diagnostic.ranges) {
                if (range.position >= start && range.position < end) {
                    rangeMatches = true;
                    break;
                }
            }
            const int startLine = static_cast<int>(
                                      ts_node_start_point(connection.node).row)
                + 1;
            const int endLine = static_cast<int>(
                                    ts_node_end_point(connection.node).row)
                + 1;
            const bool lineAndNameMatch =
                diagnostic.line >= startLine
                && diagnostic.line <= endLine
                && diagnostic.message.contains(
                    connection.formalName, Qt::CaseSensitive);
            if (rangeMatches || lineAndNameMatch) {
                expected = true;
                break;
            }
        }
        if (!expected)
            return true;
    }
    return false;
}

} // namespace

RtlConnectionTransformPlanner::
RtlConnectionTransformPlanner(
    SemanticIndex* semanticIndex)
    : index(semanticIndex)
{
}

SemanticIndex*
RtlConnectionTransformPlanner::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

RtlConnectionTransformReport
RtlConnectionTransformPlanner::plan(
    const RtlConnectionTransformRequest& request,
    const rtledit::WorkspaceDocumentManager& documents) const
{
    if (!request.synchronizeAllInstances)
        return planSingle(request, documents);

    SemanticIndex* semantic = semanticIndex();
    if (!semantic || !request.instanceStableKey.isValid()) {
        return rejected(
            RtlConnectionTransformFailure::InvalidRequest,
            QStringLiteral(
                "The all-instance synchronization request is incomplete."));
    }
    const SemanticSnapshotToken token = semantic->snapshotToken();
    if (!token.isValid()) {
        return rejected(
            RtlConnectionTransformFailure::MissingSemanticSnapshot,
            QStringLiteral(
                "No immutable Slang semantic snapshot is available."));
    }
    if (token.revision != request.expectedSemanticGeneration) {
        return rejected(
            RtlConnectionTransformFailure::StaleSemanticGeneration,
            QStringLiteral(
                "The semantic generation changed before synchronization."));
    }

    const SemanticSymbolRecord selected =
        token.snapshot->getSymbolRecordByStableKey(
            request.instanceStableKey);
    if (!selected.isValid()
        || selected.declarationKind
               != SymbolTaxonomy::DeclarationKind::Instance
        || selected.type.resolvedTypeName.isEmpty()) {
        return rejected(
            RtlConnectionTransformFailure::InstanceSemanticMismatch,
            QStringLiteral(
                "The selected symbol is not one resolved Slang module "
                "instance."));
    }

    QList<SemanticSymbolRecord> instances;
    QSet<QString> seenStableKeys;
    for (const SemanticSymbolRecord& record :
         token.snapshot->symbolRecordsView()) {
        if (record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Instance
            || record.type.resolvedTypeName
                   != selected.type.resolvedTypeName
            || record.owner.kind
                   != SymbolTaxonomy::SymbolOwnerScope::Module
            || record.type.resolvedTypeKind
                   != SymbolTaxonomy::DeclarationKind::Module
            || !record.stableKey.isValid()) {
            continue;
        }
        const QString source = token.snapshot->getCachedFileContent(
            normalizedFileName(record.location.fileName));
        if (source.isEmpty() || !sourceMappedName(source, record))
            continue;
        TSDocument syntax;
        syntax.setText(source);
        RtlConnectionTransformFailure syntaxFailure =
            RtlConnectionTransformFailure::None;
        QString syntaxMessage;
        if (!connectionSyntaxAt(syntax,
                                record,
                                &syntaxFailure,
                                &syntaxMessage)) {
            continue;
        }
        const QString key = record.stableKey.toString();
        if (seenStableKeys.contains(key))
            continue;
        seenStableKeys.insert(key);
        instances.append(record);
    }
    if (!seenStableKeys.contains(selected.stableKey.toString()))
        instances.append(selected);
    std::sort(instances.begin(), instances.end(),
              [](const SemanticSymbolRecord& left,
                 const SemanticSymbolRecord& right) {
                  const QString leftFile = normalizedFileName(
                      left.location.fileName);
                  const QString rightFile = normalizedFileName(
                      right.location.fileName);
                  if (leftFile != rightFile)
                      return leftFile < rightFile;
                  return left.location.position < right.location.position;
              });

    std::vector<rtledit::WorkspaceTextEdit> combinedEdits;
    std::vector<rtledit::TextEditProvenance> combinedProvenance;
    QSet<QString> semanticFiles;
    RtlConnectionTransformReport combined;
    combined.instanceRecord = selected;
    combined.failure = RtlConnectionTransformFailure::None;
    int changedInstances = 0;

    for (const SemanticSymbolRecord& instance : std::as_const(instances)) {
        RtlConnectionTransformRequest local = request;
        local.synchronizeAllInstances = false;
        local.instanceStableKey = instance.stableKey;

        if (instance.stableKey == selected.stableKey) {
            local.selectedInstancePath = request.selectedInstancePath;
            local.parentInstancePath = request.parentInstancePath;
        } else {
            QStringList paths =
                instance.presentation.instanceInfoByPath.keys();
            for (const QString& path :
                 instance.presentation.declaredTypeFactsByPath.keys()) {
                if (!paths.contains(path))
                    paths.append(path);
            }
            for (const QString& path :
                 elaboratedPathsForSourceInstance(
                     *token.snapshot,
                     selected.type.resolvedTypeName,
                     instance.name)) {
                if (!paths.contains(path))
                    paths.append(path);
            }
            std::sort(paths.begin(), paths.end());
            const QString suffix = QLatin1Char('.') + instance.name;
            QString selectedPath;
            for (const QString& path : std::as_const(paths)) {
                if (path.endsWith(suffix)) {
                    selectedPath = path;
                    break;
                }
            }
            if (selectedPath.isEmpty()) {
                return rejected(
                    RtlConnectionTransformFailure::
                        InstanceSemanticMismatch,
                    QStringLiteral(
                        "Slang did not provide an exact hierarchy path for "
                        "instance %1 while synchronizing module %2.")
                        .arg(instance.name,
                             selected.type.resolvedTypeName));
            }
            local.selectedInstancePath = selectedPath;
            local.parentInstancePath =
                selectedPath.left(selectedPath.size() - suffix.size());
        }

        const QString fileName =
            normalizedFileName(instance.location.fileName);
        const auto document = documents.snapshot(utf8String(fileName));
        if (!document) {
            return rejected(
                RtlConnectionTransformFailure::MissingDocument,
                QStringLiteral(
                    "The source document for instance %1 is unavailable.")
                    .arg(local.selectedInstancePath));
        }
        local.expectedDocumentRevision = document->version.value;

        RtlConnectionTransformReport report =
            planSingle(local, documents,
                       selected.type.resolvedTypeName);
        if (report.status == RtlConnectionTransformStatus::Rejected) {
            report.message = QStringLiteral("%1: %2")
                                 .arg(local.selectedInstancePath,
                                      report.message);
            return report;
        }
        if (combined.formals.isEmpty())
            combined.formals = report.formals;
        if (!report.ready())
            continue;

        const std::size_t editOffset = combinedEdits.size();
        combinedEdits.insert(combinedEdits.end(),
                             report.workspaceEdit.edits.begin(),
                             report.workspaceEdit.edits.end());
        for (rtledit::TextEditProvenance provenanceItem :
             report.workspaceEdit.provenance) {
            provenanceItem.editIndex += editOffset;
            combinedProvenance.push_back(std::move(provenanceItem));
        }
        for (const std::string& path :
             report.workspaceEdit.semanticIndexFilePaths) {
            semanticFiles.insert(fromUtf8String(path));
        }
        ++changedInstances;
    }

    if (combinedEdits.empty()) {
        combined.status = RtlConnectionTransformStatus::NoChanges;
        combined.message = QStringLiteral(
            "All instances already match the current module ports.");
        return combined;
    }

    rtledit::SemanticEditIntent intent;
    intent.kind = rtledit::SemanticEditKind::ReplaceText;
    intent.target.kind = rtledit::SemanticObjectKind::Unknown;
    intent.target.qualifiedName =
        utf8String(selected.type.resolvedTypeName);
    intent.target.ownerScope = utf8String(selected.owner.name);
    intent.target.filePath =
        utf8String(normalizedFileName(selected.location.fileName));
    intent.target.signatureHash =
        utf8String(selected.type.resolvedTypeName);
    combined.workspaceEdit = rtledit::makeWorkspaceEditPlan(
        std::move(intent),
        rtledit::RiskLevel::High,
        rtledit::PreviewPolicy::Diff,
        std::move(combinedEdits),
        std::move(combinedProvenance));
    combined.workspaceEdit.semanticSnapshot.id =
        std::to_string(token.revision);
    QStringList sortedSemanticFiles = semanticFiles.values();
    std::sort(sortedSemanticFiles.begin(), sortedSemanticFiles.end());
    for (const QString& fileName : std::as_const(sortedSemanticFiles)) {
        combined.workspaceEdit.semanticIndexFilePaths.push_back(
            utf8String(fileName));
    }
    combined.status = RtlConnectionTransformStatus::Ready;
    combined.message = QStringLiteral(
        "A High+Diff synchronization preview for %1 instance source%2 is "
        "ready.")
                           .arg(changedInstances)
                           .arg(changedInstances == 1
                                    ? QString()
                                    : QStringLiteral("s"));
    return combined;
}

RtlConnectionTransformReport
RtlConnectionTransformPlanner::planSingle(
    const RtlConnectionTransformRequest& request,
    const rtledit::WorkspaceDocumentManager& documents,
    const QString& synchronizedModuleName) const
{
    SemanticIndex* semantic = semanticIndex();
    if (!semantic
        || !request.instanceStableKey.isValid()
        || request.selectedInstancePath.isEmpty()
        || request.parentInstancePath.isEmpty()
        || request.expectedSemanticGeneration == 0
        || request.expectedDocumentRevision == 0
        || (!request.convertOrderedToNamed
            && !request.addMissingPorts
            && !request.removeUnknownPorts
            && request.castPolicy
                != RtlExplicitCastPolicy::
                    InsertWhenRequired)) {
        return rejected(
            RtlConnectionTransformFailure::InvalidRequest,
            QStringLiteral(
                "The connection transform request is incomplete."));
    }

    const SemanticSnapshotToken token =
        semantic->snapshotToken();
    if (!token.isValid()) {
        return rejected(
            RtlConnectionTransformFailure::
                MissingSemanticSnapshot,
            QStringLiteral(
                "No immutable Slang semantic snapshot is available."));
    }
    if (token.revision
        != request.expectedSemanticGeneration) {
        return rejected(
            RtlConnectionTransformFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The semantic generation changed before planning."));
    }

    int stableInstanceCount = 0;
    for (const SemanticSymbolRecord& record :
         token.snapshot->symbolRecordsView()) {
        if (record.stableKey
            == request.instanceStableKey) {
            ++stableInstanceCount;
        }
    }
    const SemanticSymbolRecord instance =
        token.snapshot->getSymbolRecordByStableKey(
            request.instanceStableKey);
    if (stableInstanceCount != 1
        || !instance.isValid()
        || instance.declarationKind
            != SymbolTaxonomy::DeclarationKind::Instance
        || instance.owner.kind
            != SymbolTaxonomy::SymbolOwnerScope::Module
        || instance.type.resolvedTypeKind
            != SymbolTaxonomy::DeclarationKind::Module
        || instance.type.resolvedTypeName.isEmpty()) {
        return rejected(
            RtlConnectionTransformFailure::
                InstanceSemanticMismatch,
            QStringLiteral(
                "The selected stable key is not one exact Slang module "
                "instance."));
    }
    const QString childPathSuffix =
        QLatin1Char('.') + instance.name;
    if (!request.selectedInstancePath
             .startsWith(
                 request.parentInstancePath
                 + QLatin1Char('.'))
        || !request.selectedInstancePath
                .endsWith(childPathSuffix)) {
        return rejected(
            RtlConnectionTransformFailure::
                InstanceSemanticMismatch,
            QStringLiteral(
                "The selected hierarchy path does not bind the Slang "
                "instance name beneath the requested parent path."));
    }

    const QString fileName =
        normalizedFileName(instance.location.fileName);
    const auto document =
        documents.snapshot(utf8String(fileName));
    if (!document) {
        return rejected(
            RtlConnectionTransformFailure::MissingDocument,
            QStringLiteral(
                "The instance source document is not available."));
    }
    if (document->version.value
        != request.expectedDocumentRevision) {
        return rejected(
            RtlConnectionTransformFailure::
                StaleDocumentRevision,
            QStringLiteral(
                "The source document revision changed before planning."));
    }
    const QString text =
        fromUtf8String(document->text);
    const QString semanticText =
        token.snapshot->getCachedFileContent(fileName);
    if (semanticText != text) {
        return rejected(
            RtlConnectionTransformFailure::
                StaleSemanticSource,
            QStringLiteral(
                "The Slang snapshot source differs from the current "
                "document buffer."));
    }

    TSDocument syntax;
    syntax.setText(text);
    if (syntax.hasError()) {
        return rejected(
            RtlConnectionTransformFailure::SyntaxError,
            QStringLiteral(
                "The instance source contains Tree-sitter or Slang "
                "errors."));
    }
    if (syntax.enclosingModuleName(
            instance.location.position)
        != instance.owner.name) {
        return rejected(
            RtlConnectionTransformFailure::
                InstanceSemanticMismatch,
            QStringLiteral(
                "The Slang parent module does not match the current "
                "Tree-sitter scope."));
    }

    RtlConnectionTransformFailure syntaxFailure =
        RtlConnectionTransformFailure::None;
    QString syntaxMessage;
    const auto connectionSyntax =
        connectionSyntaxAt(
            syntax, instance,
            &syntaxFailure, &syntaxMessage);
    if (!connectionSyntax)
        return rejected(syntaxFailure, syntaxMessage);
    if (connectionSyntax->ordered
        && !request.convertOrderedToNamed
        && request.addMissingPorts) {
        return rejected(
            RtlConnectionTransformFailure::
                MixedConnectionStyle,
            QStringLiteral(
                "Missing named port connections cannot be appended to "
                "an ordered connection list unless ordered-to-named "
                "conversion is enabled."));
    }

    RtlConnectionTransformFailure formalFailure =
        RtlConnectionTransformFailure::None;
    QString formalMessage;
    const auto formals = formalFacts(
        *token.snapshot, instance,
        request.selectedInstancePath,
        &formalFailure, &formalMessage);
    if (!formals)
        return rejected(formalFailure, formalMessage);
    QSet<QString> checkedSemanticFiles{fileName};
    QList<QPair<QString, rtledit::DocumentVersion>>
        protectedSemanticDocuments;
    for (const FormalFact& formal : *formals) {
        const QString formalFile =
            normalizedFileName(
                formal.record.location.fileName);
        if (formalFile.isEmpty()
            || checkedSemanticFiles.contains(formalFile)) {
            continue;
        }
        checkedSemanticFiles.insert(formalFile);
        if (hasSlangErrorForFile(
                *token.snapshot, formalFile)) {
            return rejected(
                RtlConnectionTransformFailure::SyntaxError,
                QStringLiteral(
                    "The target module source contains Slang errors."));
        }
        const auto currentFormalDocument =
            documents.snapshot(utf8String(formalFile));
        if (currentFormalDocument
            && fromUtf8String(
                   currentFormalDocument->text)
                   != token.snapshot
                          ->getCachedFileContent(
                              formalFile)) {
            return rejected(
                RtlConnectionTransformFailure::
                    StaleSemanticSource,
                QStringLiteral(
                    "The current target module source differs from "
                    "the Slang snapshot."));
        }
        if (currentFormalDocument) {
            protectedSemanticDocuments.append(
                {formalFile,
                 currentFormalDocument->version});
        }
    }
    if (connectionSyntax->ordered
        && connectionSyntax->items.size()
               > formals->size()) {
        return rejected(
            RtlConnectionTransformFailure::
                OrderedConnectionCountExceedsFormals,
            QStringLiteral(
                "The ordered connection count exceeds Slang's formal "
                "port count."));
    }

    QHash<QString, int> formalIndexByName;
    QSet<QString> synchronizedFormalNames;
    for (int index = 0; index < formals->size(); ++index)
        formalIndexByName.insert(
            formals->at(index).record.name, index);
    for (const FormalFact& formal : *formals)
        synchronizedFormalNames.insert(formal.record.name);

    QSet<QString> connectedNames;
    QList<ConnectionNode> obsoleteConnections;
    QList<ConnectionNode> retainedConnections;
    if (connectionSyntax->named) {
        for (const ConnectionNode& item :
             connectionSyntax->items) {
            if (!formalIndexByName.contains(item.formalName)) {
                if (request.removeUnknownPorts) {
                    obsoleteConnections.append(item);
                    continue;
                }
                return rejected(
                    RtlConnectionTransformFailure::
                        UnknownNamedFormal,
                    QStringLiteral(
                        "A named connection is not present in the current "
                        "Slang formal port set."));
            }
            if (connectedNames.contains(item.formalName)) {
                return rejected(
                    RtlConnectionTransformFailure::
                        UnknownNamedFormal,
                    QStringLiteral(
                        "A named connection is duplicated in the Slang "
                        "formal port set."));
            }
            connectedNames.insert(item.formalName);
            retainedConnections.append(item);
        }
    } else if (connectionSyntax->ordered) {
        for (int index = 0;
             index < connectionSyntax->items.size();
             ++index) {
            connectedNames.insert(
                formals->at(index).record.name);
        }
        retainedConnections = connectionSyntax->items;
    }

    if (hasUnexpectedSlangErrorForFile(
            *token.snapshot,
            fileName,
            request.removeUnknownPorts
                ? obsoleteConnections
                : QList<ConnectionNode>(),
            synchronizedModuleName,
            synchronizedFormalNames)) {
        return rejected(
            RtlConnectionTransformFailure::SyntaxError,
            QStringLiteral(
                "The instance source contains unrelated Slang errors; only "
                "diagnostics anchored to obsolete named ports can be "
                "repaired by synchronization."));
    }

    QList<int> missingFormalIndexes;
    if (request.addMissingPorts) {
        for (int index = 0; index < formals->size();
             ++index) {
            if (!connectedNames.contains(
                    formals->at(index).record.name)) {
                missingFormalIndexes.append(index);
            }
        }
    }

    RtlConnectionTransformReport report;
    report.instanceRecord = instance;
    report.failure = RtlConnectionTransformFailure::None;
    report.formals.reserve(formals->size());
    for (const FormalFact& formal : *formals) {
        RtlConnectionFormalView view;
        view.formalName = formal.record.name;
        view.direction = formal.record.collectorKind;
        view.originallyConnected =
            connectedNames.contains(formal.record.name);
        view.fixedSize = formal.type.fixedSize;
        view.integral = formal.type.integral;
        view.signedIntegral =
            formal.type.signedIntegral;
        view.bitWidth = formal.type.bitWidth;
        report.formals.append(std::move(view));
    }

    std::vector<rtledit::WorkspaceTextEdit> edits;
    std::vector<rtledit::TextEditProvenance>
        provenanceItems;
    bool commaMergedIntoLastReplacement = false;
    const bool preserveDelimiterForMissingPorts =
        !missingFormalIndexes.isEmpty()
        && !retainedConnections.isEmpty()
        && !connectionSyntax->items.isEmpty()
        && !obsoleteConnections.isEmpty()
        && ts_node_eq(connectionSyntax->items.constLast().node,
                      obsoleteConnections.constLast().node);

    QList<CharacterRange> obsoleteRanges;
    for (const ConnectionNode& obsolete :
         std::as_const(obsoleteConnections)) {
        const bool keepPrecedingDelimiter =
            preserveDelimiterForMissingPorts
            && ts_node_eq(obsolete.node,
                          obsoleteConnections.constLast().node);
        const auto range = obsoleteConnectionRange(
            text, *connectionSyntax, obsolete,
            keepPrecedingDelimiter);
        if (!range) {
            return rejected(
                RtlConnectionTransformFailure::
                    UnsupportedConnectionSyntax,
                QStringLiteral(
                    "An obsolete named connection has no safe "
                    "Tree-sitter delimiter range."));
        }
        obsoleteRanges.append(*range);
    }
    for (const CharacterRange& range :
         mergedCharacterRanges(std::move(obsoleteRanges))) {
        edits.push_back(textEdit(
            fileName, document->version, text,
            range.start, range.end, QString()));
        const auto& edit = edits.back();
        provenanceItems.push_back(provenance(
            provenanceItems.size(),
            QStringLiteral("connection.obsolete.remove"),
            QStringLiteral(
                "Remove named connections that are absent from the "
                "current Slang formal port set."),
            token.revision, instance,
            request.selectedInstancePath,
            fileName, edit.range));
    }

    if (connectionSyntax->ordered) {
        edits.reserve(
            static_cast<std::size_t>(
                connectionSyntax->items.size()
                + missingFormalIndexes.size() + 1));
        for (int index = 0;
             index < connectionSyntax->items.size();
             ++index) {
            const ConnectionNode& item =
                connectionSyntax->items.at(index);
            const FormalFact& formal =
                formals->at(index);
            bool castInserted = false;
            RtlConnectionTransformFailure castFailure =
                RtlConnectionTransformFailure::None;
            QString castMessage;
            const auto actual = connectionActual(
                syntax, *token.snapshot, instance,
                request, formal, item.expression,
                false, &castInserted,
                &castFailure, &castMessage);
            if (!actual)
                return rejected(castFailure, castMessage);

            RtlConnectionFormalView& view =
                report.formals[index];
            view.actualText = *actual;
            view.explicitCastInserted =
                castInserted;
            if (!request.convertOrderedToNamed) {
                if (!castInserted)
                    continue;
                const int start =
                    nodeStartChar(item.expression);
                const int end =
                    nodeEndChar(item.expression);
                edits.push_back(textEdit(
                    fileName, document->version,
                    text, start, end, *actual));
                const auto& edit = edits.back();
                provenanceItems.push_back(provenance(
                    provenanceItems.size(),
                    QStringLiteral(
                        "connection.ordered.explicit-cast"),
                    QStringLiteral(
                        "Insert a provable width or signedness cast "
                        "for ordered input formal %1 without changing "
                        "the connection style.")
                        .arg(formal.record.name),
                    token.revision, instance,
                    request.selectedInstancePath,
                    fileName, edit.range));
                continue;
            }

            QString replacement =
                QLatin1Char('.')
                + formal.record.name
                + QLatin1Char('(')
                + *actual
                + QLatin1Char(')');
            if (index
                    == connectionSyntax->items.size() - 1
                && !missingFormalIndexes.isEmpty()
                && static_cast<int>(
                       ts_node_end_point(item.node).row)
                       < static_cast<int>(
                           ts_node_start_point(
                               connectionSyntax->closeParen).row)) {
                replacement += QLatin1Char(',');
                commaMergedIntoLastReplacement = true;
            }
            const int start = nodeStartChar(item.node);
            const int end = nodeEndChar(item.node);
            edits.push_back(textEdit(
                fileName, document->version,
                text, start, end, replacement));
            const auto& edit = edits.back();
            provenanceItems.push_back(provenance(
                provenanceItems.size(),
                QStringLiteral(
                    "connection.ordered-to-named"),
                QStringLiteral(
                    "Convert ordered connection %1 to named formal %2.")
                    .arg(index)
                    .arg(formal.record.name),
                token.revision, instance,
                request.selectedInstancePath,
                fileName, edit.range));
        }
    } else if (connectionSyntax->named) {
        for (const ConnectionNode& item :
             connectionSyntax->items) {
            const int formalIndex =
                formalIndexByName.value(
                    item.formalName, -1);
            if (formalIndex < 0)
                continue;
            RtlConnectionFormalView& view =
                report.formals[formalIndex];
            if (ts_node_is_null(item.expression)) {
                view.actualText.clear();
                continue;
            }

            const FormalFact& formal =
                formals->at(formalIndex);
            bool castInserted = false;
            RtlConnectionTransformFailure castFailure =
                RtlConnectionTransformFailure::None;
            QString castMessage;
            const auto actual = connectionActual(
                syntax, *token.snapshot, instance,
                request, formal, item.expression,
                false, &castInserted,
                &castFailure, &castMessage);
            if (!actual)
                return rejected(castFailure, castMessage);

            view.actualText = *actual;
            view.explicitCastInserted =
                castInserted;
            if (!castInserted)
                continue;

            const int start =
                nodeStartChar(item.expression);
            const int end =
                nodeEndChar(item.expression);
            edits.push_back(textEdit(
                fileName, document->version,
                text, start, end, *actual));
            const auto& edit = edits.back();
            provenanceItems.push_back(provenance(
                provenanceItems.size(),
                QStringLiteral(
                    "connection.named.explicit-cast"),
                QStringLiteral(
                    "Insert a provable width or signedness cast for "
                    "named input formal %1.")
                    .arg(formal.record.name),
                token.revision, instance,
                request.selectedInstancePath,
                fileName, edit.range));
        }
    }

    QStringList missingConnections;
    for (const int formalIndex :
         std::as_const(missingFormalIndexes)) {
        const FormalFact& formal =
            formals->at(formalIndex);
        QString actualText;
        bool castInserted = false;
        if (request.missingPortPolicy
            == RtlMissingPortConnectionPolicy::
                ConnectSameNamedSignal) {
            const auto actual = exactActualFact(
                *token.snapshot,
                formal.record.name,
                instance.owner.name,
                request.parentInstancePath,
                nullptr, nullptr);
            if (!actual) {
                return rejected(
                    RtlConnectionTransformFailure::
                        AmbiguousActual,
                    QStringLiteral(
                        "A missing formal has no unique same-named "
                        "Slang signal in the parent module."));
            }
            if (!castableMachineType(formal.type)
                || !castableMachineType(actual->type)) {
                return rejected(
                    RtlConnectionTransformFailure::
                        MissingActualType,
                    QStringLiteral(
                        "A same-named missing-port connection lacks "
                        "comparable Slang machine types."));
            }
            const bool sameType =
                formal.type.bitWidth
                        == actual->type.bitWidth
                && formal.type.signedIntegral
                        == actual->type.signedIntegral
                && formal.declaredType.canonicalTypeId
                        == actual->declaredType
                               .canonicalTypeId
                && formal.declaredType.declarationShapeId
                        == actual->declaredType
                               .declarationShapeId;
            actualText = formal.record.name;
            if (!sameType) {
                if (request.castPolicy
                        != RtlExplicitCastPolicy::
                            InsertWhenRequired
                    || formal.record.collectorKind
                        != SymbolTaxonomy::CollectorKind::
                            PortInput) {
                    return rejected(
                        RtlConnectionTransformFailure::
                            CastNotProvable,
                        QStringLiteral(
                            "A same-named missing-port connection "
                            "requires an explicit, provable input cast."));
                }
                const auto typeText =
                    renderedFormalCastType(formal);
                if (!typeText) {
                    return rejected(
                        RtlConnectionTransformFailure::
                            CastNotProvable,
                        QStringLiteral(
                            "The missing-port target type cannot be "
                            "rendered from complete Slang facts."));
                }
                actualText =
                    *typeText
                    + QStringLiteral("'(")
                    + actualText
                    + QLatin1Char(')');
                castInserted = true;
            }
        }
        missingConnections.append(
            QLatin1Char('.')
            + formal.record.name
            + QLatin1Char('(')
            + actualText
            + QLatin1Char(')'));
        RtlConnectionFormalView& view =
            report.formals[formalIndex];
        view.added = true;
        view.actualText = actualText;
        view.explicitCastInserted = castInserted;
    }

    if (!missingConnections.isEmpty()) {
        const int closeParen =
            nodeStartChar(connectionSyntax->closeParen);
        const int closeLine =
            static_cast<int>(
                ts_node_start_point(
                    connectionSyntax->closeParen).row);
        QString insertion;
        int insertionChar = closeParen;
        const bool hasExisting =
            !retainedConnections.isEmpty();
        if (hasExisting) {
            const ConnectionNode& last =
                retainedConnections.constLast();
            const int lastLine =
                static_cast<int>(
                    ts_node_end_point(last.node).row);
            if (lastLine < closeLine) {
                insertionChar =
                    lineStartChar(text, closeLine);
                const QString indent =
                    lineIndentAt(text, lastLine);
                const QString newline =
                    newlineBefore(text, insertionChar);
                insertion =
                    indent
                    + missingConnections.join(
                        QStringLiteral(",")
                        + newline + indent)
                    + newline;
                if (!commaMergedIntoLastReplacement
                    && !preserveDelimiterForMissingPorts) {
                    const int commaChar =
                        nodeEndChar(last.node);
                    edits.push_back(textEdit(
                        fileName, document->version,
                        text, commaChar, commaChar,
                        QStringLiteral(",")));
                    const auto& edit = edits.back();
                    provenanceItems.push_back(
                        provenance(
                            provenanceItems.size(),
                            QStringLiteral(
                                "connection.delimiter"),
                            QStringLiteral(
                                "Insert the delimiter before missing "
                                "named connections."),
                            token.revision, instance,
                            request.selectedInstancePath,
                            fileName, edit.range));
                }
            } else {
                insertion =
                    (preserveDelimiterForMissingPorts
                         ? QStringLiteral(" ")
                         : QStringLiteral(", "))
                    + missingConnections.join(
                        QStringLiteral(", "));
            }
        } else if (closeLine
                   > static_cast<int>(
                       ts_node_start_point(
                           connectionSyntax->connections).row)) {
            insertionChar =
                lineStartChar(text, closeLine);
            const QString indent =
                lineIndentAt(text, closeLine)
                + QStringLiteral("    ");
            const QString newline =
                newlineBefore(text, insertionChar);
            insertion =
                indent
                + missingConnections.join(
                    QStringLiteral(",")
                    + newline + indent)
                + newline;
        } else {
            insertion =
                missingConnections.join(
                    QStringLiteral(", "));
        }

        edits.push_back(textEdit(
            fileName, document->version,
            text, insertionChar, insertionChar,
            insertion));
        const auto& edit = edits.back();
        provenanceItems.push_back(provenance(
            provenanceItems.size(),
            QStringLiteral(
                "connection.missing.insert"),
            QStringLiteral(
                "Insert %1 missing named port connection(s).")
                .arg(missingConnections.size()),
            token.revision, instance,
            request.selectedInstancePath,
            fileName, edit.range));
    }

    if (edits.empty()) {
        report.status =
            RtlConnectionTransformStatus::NoChanges;
        report.message = QStringLiteral(
            "The selected instance already satisfies the requested "
            "connection form.");
        return report;
    }

    if (semantic->snapshotRevision() != token.revision
        || semantic->snapshot().get()
               != token.snapshot.get()) {
        return rejected(
            RtlConnectionTransformFailure::
                SemanticSnapshotChanged,
            QStringLiteral(
                "The semantic snapshot changed while the plan was "
                "being constructed."));
    }
    const auto finalDocument =
        documents.snapshot(utf8String(fileName));
    if (!finalDocument
        || finalDocument->version
               != document->version
        || finalDocument->text
               != document->text) {
        return rejected(
            RtlConnectionTransformFailure::
                StaleDocumentRevision,
            QStringLiteral(
                "The source document changed while the plan was being "
                "constructed."));
    }
    for (const auto& protectedDocument :
         std::as_const(protectedSemanticDocuments)) {
        const auto current = documents.snapshot(
            utf8String(protectedDocument.first));
        if (!current
            || current->version
                   != protectedDocument.second
            || fromUtf8String(current->text)
                   != token.snapshot
                          ->getCachedFileContent(
                              protectedDocument.first)) {
            return rejected(
                RtlConnectionTransformFailure::
                    StaleDocumentRevision,
                QStringLiteral(
                    "A semantic dependency document changed while the "
                    "plan was being constructed."));
        }
    }

    rtledit::SemanticEditIntent intent;
    intent.kind = rtledit::SemanticEditKind::ReplaceText;
    intent.target.kind =
        rtledit::SemanticObjectKind::Unknown;
    intent.target.qualifiedName =
        utf8String(request.selectedInstancePath);
    intent.target.ownerScope =
        utf8String(instance.owner.name);
    intent.target.filePath = utf8String(fileName);
    intent.target.range = utf8Range(
        text,
        nodeStartChar(connectionSyntax->hierarchy),
        nodeEndChar(connectionSyntax->hierarchy));
    intent.target.signatureHash =
        utf8String(instance.stableKey.toString());

    report.workspaceEdit =
        rtledit::makeWorkspaceEditPlan(
            std::move(intent),
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            std::move(edits),
            std::move(provenanceItems));
    report.workspaceEdit.semanticSnapshot.id =
        std::to_string(token.revision);
    QStringList semanticFiles{
        fileName};
    for (const FormalFact& formal : *formals) {
        const QString formalFile =
            normalizedFileName(
                formal.record.location.fileName);
        if (!formalFile.isEmpty()
            && !semanticFiles.contains(formalFile)) {
            semanticFiles.append(formalFile);
        }
    }
    std::sort(
        semanticFiles.begin(), semanticFiles.end());
    for (const QString& semanticFile :
         std::as_const(semanticFiles)) {
        report.workspaceEdit.semanticIndexFilePaths
            .push_back(utf8String(semanticFile));
    }

    report.status =
        RtlConnectionTransformStatus::Ready;
    report.message = QStringLiteral(
        "A high-risk RTL connection transform preview is ready.");
    return report;
}
