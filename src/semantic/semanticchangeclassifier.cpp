#include "semanticchangeclassifier.h"

#include "tsdocument.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstring>

namespace {
QString nodeType(TSNode node)
{
    const char* type = ts_node_type(node);
    return type ? QString::fromLatin1(type) : QString();
}

bool isCommentNode(TSNode node)
{
    const QString type = nodeType(node);
    return type == QLatin1String("one_line_comment")
        || type == QLatin1String("block_comment")
        || type == QLatin1String("comment");
}

QString nodeSource(const QString& text, TSNode node)
{
    const int start = static_cast<int>(ts_node_start_byte(node) / 2u);
    const int end = static_cast<int>(ts_node_end_byte(node) / 2u);
    if (start < 0 || end < start || start > text.size())
        return QString();
    return text.mid(start, qMin(end, text.size()) - start);
}

bool bodyOnlyNode(const QString& type)
{
    static const QSet<QString> types = {
        QStringLiteral("function_statement"),
        QStringLiteral("function_statement_or_null"),
        QStringLiteral("task_statement"),
        QStringLiteral("task_statement_or_null"),
        QStringLiteral("statement"),
        QStringLiteral("statement_or_null"),
        QStringLiteral("seq_block"),
    };
    return types.contains(type);
}

void appendStructuralNode(QByteArray* bytes,
                          const QString& text,
                          TSNode node,
                          bool omitBodies)
{
    if (!bytes || ts_node_is_null(node) || isCommentNode(node))
        return;

    const QString type = nodeType(node);
    if (omitBodies && bodyOnlyNode(type))
        return;

    bytes->append('(');
    bytes->append(type.toUtf8());
    const uint32_t childCount = ts_node_child_count(node);
    if (childCount == 0) {
        bytes->append(':');
        bytes->append(nodeSource(text, node).toUtf8());
    } else {
        for (uint32_t i = 0; i < childCount; ++i)
            appendStructuralNode(bytes,
                                 text,
                                 ts_node_child(node, i),
                                 omitBodies);
    }
    bytes->append(')');
}

QByteArray structuralDigest(const TSDocument& document)
{
    QByteArray bytes;
    appendStructuralNode(&bytes,
                         document.text(),
                         document.rootNode(),
                         false);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

enum class SignatureKind {
    ModuleInterface,
    PackageApi,
    HeaderMacro,
    InstanceTopology
};

bool signatureNode(const QString& type, SignatureKind kind)
{
    static const QSet<QString> moduleInterfaceTypes = {
        QStringLiteral("module_ansi_header"),
        QStringLiteral("module_nonansi_header"),
        QStringLiteral("interface_ansi_header"),
        QStringLiteral("interface_nonansi_header"),
        QStringLiteral("port_declaration"),
        QStringLiteral("parameter_port_list"),
        QStringLiteral("parameter_declaration"),
        QStringLiteral("local_parameter_declaration"),
        QStringLiteral("type_parameter_declaration"),
        QStringLiteral("typedef"),
        QStringLiteral("type_declaration"),
        QStringLiteral("struct_union"),
        QStringLiteral("enum_name_declaration"),
        QStringLiteral("modport_declaration"),
    };
    static const QSet<QString> packageApiTypes = {
        QStringLiteral("package_declaration"),
        QStringLiteral("parameter_declaration"),
        QStringLiteral("local_parameter_declaration"),
        QStringLiteral("type_parameter_declaration"),
        QStringLiteral("typedef"),
        QStringLiteral("type_declaration"),
        QStringLiteral("struct_union"),
        QStringLiteral("enum_name_declaration"),
        QStringLiteral("function_declaration"),
        QStringLiteral("function_body_declaration"),
        QStringLiteral("function_prototype"),
        QStringLiteral("task_declaration"),
        QStringLiteral("task_body_declaration"),
        QStringLiteral("task_prototype"),
    };
    static const QSet<QString> macroTypes = {
        QStringLiteral("text_macro_definition"),
        QStringLiteral("undefine_compiler_directive"),
        QStringLiteral("undefineall_compiler_directive"),
        QStringLiteral("include_compiler_directive"),
        QStringLiteral("include_statement"),
        QStringLiteral("conditional_compilation_directive"),
        QStringLiteral("ifdef_condition"),
        QStringLiteral("ifdef_macro_expression"),
        QStringLiteral("ifdef_directive"),
        QStringLiteral("ifndef_directive"),
        QStringLiteral("elsif_directive"),
        QStringLiteral("else_directive"),
        QStringLiteral("endif_directive"),
    };
    static const QSet<QString> topologyTypes = {
        QStringLiteral("module_instantiation"),
        QStringLiteral("interface_instantiation"),
        QStringLiteral("program_instantiation"),
    };

    switch (kind) {
    case SignatureKind::ModuleInterface:
        return moduleInterfaceTypes.contains(type);
    case SignatureKind::PackageApi:
        return packageApiTypes.contains(type);
    case SignatureKind::HeaderMacro:
        return macroTypes.contains(type);
    case SignatureKind::InstanceTopology:
        return topologyTypes.contains(type);
    }
    return false;
}

bool hasAncestor(TSNode node, const char* expected)
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

bool belongsToModuleOrInterfaceScope(TSNode node)
{
    node = ts_node_parent(node);
    while (!ts_node_is_null(node)) {
        const QString type = nodeType(node);
        if (type == QLatin1String("module_declaration")
            || type == QLatin1String("interface_declaration")) {
            return true;
        }
        if (type == QLatin1String("package_declaration")
            || type == QLatin1String("function_declaration")
            || type == QLatin1String("function_body_declaration")
            || type == QLatin1String("task_declaration")
            || type == QLatin1String("task_body_declaration")
            || type == QLatin1String("class_declaration")
            || type == QLatin1String("program_declaration")
            || type == QLatin1String("checker_declaration")) {
            return false;
        }
        node = ts_node_parent(node);
    }
    return false;
}

bool belongsToSignature(TSNode node, SignatureKind kind)
{
    switch (kind) {
    case SignatureKind::PackageApi:
        return hasAncestor(node, "package_declaration")
            || nodeType(node) == QLatin1String("package_declaration");
    case SignatureKind::ModuleInterface:
        return belongsToModuleOrInterfaceScope(node);
    case SignatureKind::HeaderMacro:
    case SignatureKind::InstanceTopology:
        return true;
    }
    return false;
}

void collectSignatureTokens(QStringList* tokens,
                            const QString& text,
                            TSNode node,
                            SignatureKind kind)
{
    if (!tokens || ts_node_is_null(node) || isCommentNode(node))
        return;
    const QString type = nodeType(node);
    if (signatureNode(type, kind) && belongsToSignature(node, kind)) {
        QByteArray bytes;
        appendStructuralNode(&bytes,
                             text,
                             node,
                             false);
        tokens->append(QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                .toHex()));
        // A selected declaration already covers its descendants. Avoid
        // weighting the signature by grammar nesting details.
        return;
    }

    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        collectSignatureTokens(tokens,
                               text,
                               ts_node_named_child(node, i),
                               kind);
    }
}

QByteArray signatureDigest(const TSDocument& document, SignatureKind kind)
{
    QStringList tokens;
    collectSignatureTokens(&tokens,
                           document.text(),
                           document.rootNode(),
                           kind);
    std::sort(tokens.begin(), tokens.end());
    return QCryptographicHash::hash(tokens.join(QLatin1Char('\n')).toUtf8(),
                                    QCryptographicHash::Sha256);
}

struct StructuralLeaf {
    QString type;
    QString source;
    TSNode node{};
};

void collectStructuralLeaves(QList<StructuralLeaf>* leaves,
                             const QString& text,
                             TSNode node)
{
    if (!leaves || ts_node_is_null(node) || isCommentNode(node))
        return;
    const uint32_t count = ts_node_child_count(node);
    if (count == 0) {
        leaves->append({nodeType(node), nodeSource(text, node), node});
        return;
    }
    for (uint32_t i = 0; i < count; ++i)
        collectStructuralLeaves(leaves, text, ts_node_child(node, i));
}

bool sameStructuralLeaf(const StructuralLeaf& lhs,
                        const StructuralLeaf& rhs)
{
    return lhs.type == rhs.type && lhs.source == rhs.source;
}

bool isProvenLocalBodyLeaf(TSNode node)
{
    bool insideLocalBodyConstruct = false;
    while (!ts_node_is_null(node)) {
        const QString type = nodeType(node);
        if (type == QLatin1String("continuous_assign")
            || type == QLatin1String("continuous_assignment")
            || type == QLatin1String("always_construct")
            || type == QLatin1String("initial_construct")
            || type == QLatin1String("final_construct")) {
            insideLocalBodyConstruct = true;
        }
        if (type == QLatin1String("package_declaration")
            || type == QLatin1String("function_declaration")
            || type == QLatin1String("function_body_declaration")
            || type == QLatin1String("task_declaration")
            || type == QLatin1String("task_body_declaration")
            || type == QLatin1String("class_declaration")
            || type == QLatin1String("program_declaration")
            || type == QLatin1String("checker_declaration")) {
            return false;
        }
        if (type == QLatin1String("module_declaration")
            || type == QLatin1String("interface_declaration")) {
            return insideLocalBodyConstruct;
        }
        node = ts_node_parent(node);
    }
    return false;
}

bool changeIsProvenLocalBody(const TSDocument& oldDocument,
                             const TSDocument& newDocument)
{
    QList<StructuralLeaf> oldLeaves;
    QList<StructuralLeaf> newLeaves;
    collectStructuralLeaves(&oldLeaves,
                            oldDocument.text(),
                            oldDocument.rootNode());
    collectStructuralLeaves(&newLeaves,
                            newDocument.text(),
                            newDocument.rootNode());

    int prefix = 0;
    const int commonCount = qMin(oldLeaves.size(), newLeaves.size());
    while (prefix < commonCount
           && sameStructuralLeaf(oldLeaves.at(prefix),
                                 newLeaves.at(prefix))) {
        ++prefix;
    }
    int oldEnd = oldLeaves.size();
    int newEnd = newLeaves.size();
    while (oldEnd > prefix && newEnd > prefix
           && sameStructuralLeaf(oldLeaves.at(oldEnd - 1),
                                 newLeaves.at(newEnd - 1))) {
        --oldEnd;
        --newEnd;
    }

    bool inspectedChangedLeaf = false;
    for (int i = prefix; i < oldEnd; ++i) {
        inspectedChangedLeaf = true;
        if (!isProvenLocalBodyLeaf(oldLeaves.at(i).node))
            return false;
    }
    for (int i = prefix; i < newEnd; ++i) {
        inspectedChangedLeaf = true;
        if (!isProvenLocalBodyLeaf(newLeaves.at(i).node))
            return false;
    }
    return inspectedChangedLeaf;
}

bool isHeaderFile(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix();
    return suffix.compare(QStringLiteral("svh"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("vh"), Qt::CaseInsensitive) == 0;
}
}

SourceTextDelta SemanticChangeClassifier::textDelta(
    const QString& oldText,
    const QString& newText)
{
    SourceTextDelta delta;
    const int commonLimit = qMin(oldText.size(), newText.size());
    while (delta.oldStart < commonLimit
           && oldText.at(delta.oldStart) == newText.at(delta.oldStart)) {
        ++delta.oldStart;
    }

    int oldSuffix = oldText.size();
    int newSuffix = newText.size();
    while (oldSuffix > delta.oldStart
           && newSuffix > delta.oldStart
           && oldText.at(oldSuffix - 1) == newText.at(newSuffix - 1)) {
        --oldSuffix;
        --newSuffix;
    }
    delta.oldEnd = oldSuffix;
    delta.newEnd = newSuffix;
    return delta;
}

SemanticChangeClassification SemanticChangeClassifier::classify(
    const QString& fileName,
    const QString& oldText,
    const QString& newText) const
{
    SemanticChangeClassification result;
    result.delta = textDelta(oldText, newText);
    if (oldText == newText) {
        result.impact = SemanticChangeImpact::TriviaOnly;
        return result;
    }

    TSDocument oldDocument;
    TSDocument newDocument;
    oldDocument.setText(oldText);
    newDocument.setText(newText);
    result.oldTreeHasErrors = oldDocument.hasError();
    result.newTreeHasErrors = newDocument.hasError();

    if (structuralDigest(oldDocument) == structuralDigest(newDocument)) {
        result.impact = SemanticChangeImpact::TriviaOnly;
        return result;
    }

    if (result.oldTreeHasErrors || result.newTreeHasErrors) {
        result.impact = SemanticChangeImpact::FullFallback;
        result.fallbackReason = QStringLiteral(
            "Tree-sitter could not prove a safe incremental scope");
        return result;
    }

    if (isHeaderFile(fileName)
        || signatureDigest(oldDocument, SignatureKind::HeaderMacro)
               != signatureDigest(newDocument, SignatureKind::HeaderMacro)) {
        result.impact = SemanticChangeImpact::HeaderMacro;
        return result;
    }

    if (signatureDigest(oldDocument, SignatureKind::PackageApi)
        != signatureDigest(newDocument, SignatureKind::PackageApi)) {
        result.impact = SemanticChangeImpact::PackageApi;
        return result;
    }

    if (signatureDigest(oldDocument, SignatureKind::ModuleInterface)
            != signatureDigest(newDocument,
                               SignatureKind::ModuleInterface)
        || signatureDigest(oldDocument, SignatureKind::InstanceTopology)
               != signatureDigest(newDocument,
                                  SignatureKind::InstanceTopology)) {
        result.impact = SemanticChangeImpact::ModuleInterface;
        return result;
    }

    if (changeIsProvenLocalBody(oldDocument, newDocument)) {
        result.impact = SemanticChangeImpact::LocalBody;
        return result;
    }

    result.impact = SemanticChangeImpact::FullFallback;
    result.fallbackReason = QStringLiteral(
        "AST change is outside a proven module/interface local body");
    return result;
}
