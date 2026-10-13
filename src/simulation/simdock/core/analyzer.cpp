#include "analyzer.h"
#include <QRegularExpression>
#include <QSet>
#include <tree_sitter/api.h>
#include <cstdlib>
#include <cstring>

extern "C" const TSLanguage* tree_sitter_systemverilog();

namespace simdock {
namespace {
bool kind(TSNode n, const char* type) { return !ts_node_is_null(n) && std::strcmp(ts_node_type(n), type) == 0; }
QString text(TSNode n, const QByteArray& source)
{
    if (ts_node_is_null(n)) return {};
    return QString::fromUtf8(source.mid(ts_node_start_byte(n), ts_node_end_byte(n) - ts_node_start_byte(n))).trimmed();
}
QList<TSNode> children(TSNode n)
{
    QList<TSNode> out;
    if (ts_node_is_null(n)) return out;
    for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) out << ts_node_named_child(n, i);
    return out;
}
TSNode child(TSNode n, const char* type)
{
    for (auto c : children(n)) if (kind(c, type)) return c;
    return {};
}
QList<TSNode> descendants(TSNode n, const char* type)
{
    QList<TSNode> result;
    if (kind(n, type)) { result << n; return result; }
    for (auto c : children(n)) result += descendants(c, type);
    return result;
}
QString withoutComments(QString s)
{
    s.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption));
    s.remove(QRegularExpression(QStringLiteral("//[^\\r\\n]*")));
    return s.simplified();
}
QString firstIdentifier(TSNode node, const QByteArray& source)
{
    if (kind(node, "simple_identifier") || kind(node, "escaped_identifier")) return text(node, source);
    for (auto c : children(node)) {
        const auto name = firstIdentifier(c, source);
        if (!name.isEmpty()) return name;
    }
    return {};
}
bool hasScopeResolution(TSNode node)
{
    for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
        const auto c = ts_node_child(node, i);
        if (kind(c, "::")) return true;
    }
    return false;
}
// Adapted from ZeroSlack's SemanticDependencyGraph AST fact extraction.
void dependencyFacts(TSNode node, const QByteArray& source, SourceFile& file)
{
    if (kind(node, "module_declaration") || kind(node, "interface_declaration") || kind(node, "program_declaration")) {
        auto name = ts_node_child_by_field_name(node, "name", 4);
        if (ts_node_is_null(name)) {
            for (auto c : children(node)) {
                name = ts_node_child_by_field_name(c, "name", 4);
                if (!ts_node_is_null(name)) break;
            }
        }
        const auto id = firstIdentifier(ts_node_is_null(name) ? node : name, source);
        if (!id.isEmpty()) file.declaredUnits << id;
    } else if (kind(node, "package_declaration")) {
        auto name = ts_node_child_by_field_name(node, "name", 4);
        const auto id = firstIdentifier(ts_node_is_null(name) ? node : name, source);
        if (!id.isEmpty()) file.declaredPackages << id;
    } else if (kind(node, "module_instantiation") || kind(node, "interface_instantiation") || kind(node, "program_instantiation")) {
        const auto name = ts_node_child_by_field_name(node, "instance_type", 13);
        const auto id = firstIdentifier(name, source);
        if (!id.isEmpty()) file.instantiatedUnits << id;
    } else if (kind(node, "interface_port_header")) {
        const auto id = firstIdentifier(node, source);
        if (!id.isEmpty()) file.instantiatedUnits << id;
    } else if (kind(node, "package_import_item") || hasScopeResolution(node)) {
        const auto id = firstIdentifier(node, source);
        if (!id.isEmpty()) file.referencedPackages << id;
    }
    for (auto c : children(node)) dependencyFacts(c, source, file);
}
Module moduleFrom(TSNode node, const QByteArray& source, const QString& file, const QStringList& imports)
{
    Module module;
    module.file = file;
    module.imports = imports;
    module.line = int(ts_node_start_point(node).row) + 1;
    TSNode header = child(node, "module_ansi_header");
    if (ts_node_is_null(header)) header = child(node, "module_nonansi_header");
    TSNode name = ts_node_is_null(header) ? TSNode{} : ts_node_child_by_field_name(header, "name", 4);
    if (ts_node_is_null(name)) name = ts_node_child_by_field_name(node, "name", 4);
    module.name = text(name, source);
    module.limitations << QStringLiteral("Analyze the current DUT with Slang before creating its TB.");
    return module;
}
}

SourceFile analyzeSource(const QByteArray& source, const QString& path)
{
    SourceFile out;
    out.path = path;
    out.content = source;
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_systemverilog());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.constData(), uint32_t(source.size()));
    const TSNode root = ts_tree_root_node(tree);
    out.syntaxError = ts_node_has_error(root);
    dependencyFacts(root, source, out);
    out.declaredUnits.removeDuplicates();
    out.instantiatedUnits.removeDuplicates();
    out.declaredPackages.removeDuplicates();
    out.referencedPackages.removeDuplicates();
    for (auto module : descendants(root, "module_declaration")) {
        QStringList imports;
        for (auto c : children(root)) {
            if (ts_node_start_byte(c) >= ts_node_start_byte(module)) break;
            if (kind(c, "package_import_declaration")) imports << withoutComments(text(c, source));
        }
        out.modules << moduleFrom(module, source, path, imports);
    }
    for (auto include : descendants(root, "include_compiler_directive")) {
        const auto match = QRegularExpression(QStringLiteral("\"([^\"]+)\"")).match(text(include, source));
        if (match.hasMatch()) out.includes << match.captured(1);
        else out.dynamicInclude = true;
    }
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return out;
}

QString syntaxTree(const QByteArray& source)
{
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_systemverilog());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.constData(), uint32_t(source.size()));
    char* dump = ts_node_string(ts_tree_root_node(tree));
    const QString result = QString::fromUtf8(dump);
    std::free(dump);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return result;
}
}
