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
bool ordinaryName(const QString& name)
{
    return QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$")).match(name).hasMatch();
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
QString signalType(QString header, Module& module, const QString& name, bool& namedType)
{
    namedType = false;
    header = withoutComments(header);
    header.remove(QRegularExpression(QStringLiteral("\\b(input|output|inout|ref|var|wire|tri)\\b")));
    header.replace(QRegularExpression(QStringLiteral("\\breg\\b")), QStringLiteral("logic"));
    header = header.simplified();
    QString tokens = header;
    tokens.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]")));
    tokens.remove(QRegularExpression(QStringLiteral("\\b(logic|bit|signed|unsigned|integer|int|shortint|longint|byte|time)\\b")));
    if (!tokens.trimmed().isEmpty() || header.contains(QChar(96))) {
        static const QRegularExpression named(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*(?:\\s*::\\s*[A-Za-z_][A-Za-z0-9_$]*)?$"));
        if (named.match(header).hasMatch()) {
            const auto imported = QRegularExpression(QStringLiteral("::\\s*(?:%1|\\*)(?=\\s*[,;])")
                .arg(QRegularExpression::escape(header)));
            bool visible = header.contains(QStringLiteral("::"));
            for (const auto& declaration : module.imports) visible = visible || imported.match(declaration).hasMatch();
            if (visible) {
                namedType = true;
                return header;
            }
            module.limitations << QStringLiteral("Port %1 uses type %2 without a visible package import. Qualify the type or write the TB manually.").arg(name, header);
        } else {
            module.limitations << QStringLiteral("Port %1 uses an unsupported type. Interfaces, macros, and composite declarations require a manual TB.").arg(name);
        }
        return header;
    }
    if (header.isEmpty() || header.startsWith(QLatin1Char('['))
        || header.startsWith(QStringLiteral("signed")) || header.startsWith(QStringLiteral("unsigned")))
        header.prepend(QStringLiteral("logic "));
    return header.trimmed();
}
void parametersFrom(TSNode scope, Module& module, const QByteArray& source, bool local = false)
{
    if (!descendants(scope, "type_parameter_declaration").isEmpty()) module.limitations << QStringLiteral("Type parameters require a manual TB.");
    for (auto assignment : descendants(scope, "param_assignment")) {
        const auto named = children(assignment);
        if (named.isEmpty()) continue;
        Parameter p;
        p.name = text(named.first(), source);
        p.local = local;
        QString raw = text(assignment, source);
        const int equals = raw.indexOf(QLatin1Char('='));
        if (equals < 0 || !ordinaryName(p.name)) {
            module.limitations << QStringLiteral("Parameter %1 has no usable default value.").arg(p.name); continue;
        }
        p.value = raw.mid(equals + 1).trimmed();
        TSNode parent = ts_node_parent(assignment);
        while (!ts_node_is_null(parent) && !kind(parent, "parameter_declaration")
               && !kind(parent, "local_parameter_declaration") && !kind(parent, "parameter_port_declaration")
               && !kind(parent, "parameter_port_list")) parent = ts_node_parent(parent);
        p.local = p.local || kind(parent, "local_parameter_declaration");
        auto type = child(parent, "data_type_or_implicit");
        if (ts_node_is_null(type)) type = child(parent, "data_type");
        p.type = text(type, source);
        if (p.value.contains(QChar(96)) || p.value.contains(QStringLiteral("::"))
            || p.type.contains(QStringLiteral("::")) || p.type.contains(QLatin1Char('[')))
            module.limitations << QStringLiteral("Parameter %1 uses a macro, package, or composite type. Write the TB manually.").arg(p.name);
        bool duplicate = false;
        for (const auto& previous : module.parameters) if (previous.name == p.name) duplicate = true;
        if (!duplicate) module.parameters << p;
    }
}
Module moduleFrom(TSNode node, const QByteArray& source, const QString& file, const QStringList& imports)
{
    Module m;
    m.imports = imports;
    m.file = file;
    m.line = int(ts_node_start_point(node).row) + 1;
    TSNode header = child(node, "module_ansi_header");
    const bool ansi = !ts_node_is_null(header);
    if (!ansi) header = child(node, "module_nonansi_header");
    for (auto c : children(header))
        if (kind(c, "package_import_declaration")) m.imports << withoutComments(text(c, source));
    m.imports.removeDuplicates();
    TSNode name = ts_node_is_null(header) ? TSNode{} : ts_node_child_by_field_name(header, "name", 4);
    if (ts_node_is_null(name)) name = ts_node_child_by_field_name(node, "name", 4);
    m.name = text(name, source);
    if (!ordinaryName(m.name)) m.limitations << QStringLiteral("Automatic TB generation does not support escaped module names.");
    if (ts_node_is_null(header) || ts_node_has_error(header)) m.limitations << QStringLiteral("The module header contains a syntax issue.");
    if (!descendants(header, "conditional_compilation_directive").isEmpty()
        || !descendants(header, "text_macro_usage").isEmpty())
        m.limitations << QStringLiteral("The module header uses preprocessor directives. Write the TB manually.");
    parametersFrom(child(header, "parameter_port_list"), m, source);
    for (auto c : children(node)) {
        if (kind(c, "parameter_declaration")) parametersFrom(c, m, source);
        if (kind(c, "local_parameter_declaration")) parametersFrom(c, m, source, true);
    }
    if (ansi) {
        QString lastDirection = QStringLiteral("input"), lastType = QStringLiteral("logic");
        bool lastNamedType = false;
        for (auto port : descendants(child(header, "list_of_port_declarations"), "ansi_port_declaration")) {
            Port p;
            const auto portName = ts_node_child_by_field_name(port, "port_name", 9);
            if (ts_node_is_null(portName)) { m.limitations << QStringLiteral("Incomplete port declaration."); continue; }
            p.name = text(portName, source);
            const auto directions = descendants(port, "port_direction");
            if (!directions.isEmpty()) {
                lastDirection = text(directions.first(), source);
                lastType = QStringLiteral("logic");
                lastNamedType = false;
            }
            p.direction = lastDirection;
            const auto begin = ts_node_start_byte(port);
            const auto nameBegin = ts_node_start_byte(portName);
            QString prefix = QString::fromUtf8(source.mid(begin, nameBegin - begin)).trimmed();
            if (!prefix.isEmpty()) lastType = signalType(prefix, m, p.name, lastNamedType);
            p.type = lastType;
            p.namedType = lastNamedType;
            if (!descendants(port, "interface_port_header").isEmpty())
                m.limitations << QStringLiteral("Port %1 is an interface. Write the TB manually.").arg(p.name);
            // A bare "name port" can denote an interface even when the AST calls it a data type.
            // Explicit directions disambiguate named data ports; a comma-only continuation inherits them.
            if (p.namedType && directions.isEmpty() && !prefix.isEmpty())
                m.limitations << QStringLiteral("Port %1 has an ambiguous named type without a direction. Specify input or output for data ports; interfaces require a manual TB.").arg(p.name);
            if (p.namedType && p.direction == QStringLiteral("inout"))
                m.limitations << QStringLiteral("Port %1 is an inout with a named type. Write the TB manually.").arg(p.name);
            if (!ordinaryName(p.name)) m.limitations << QStringLiteral("Automatic TB generation does not support escaped port names.");
            if (!descendants(port, "unpacked_dimension").isEmpty() || !descendants(port, "unsized_dimension").isEmpty()
                || !descendants(port, "queue_dimension").isEmpty() || !descendants(port, "associative_dimension").isEmpty())
                m.limitations << QStringLiteral("Port %1 is an unpacked array. Write the TB manually.").arg(p.name);
            if (p.direction == QStringLiteral("ref")) m.limitations << QStringLiteral("Ref ports require a manual TB.");
            m.ports << p;
        }
    } else {
        for (auto c : children(node)) {
            QList<TSNode> declarations;
            for (const auto* t : {"input_declaration", "output_declaration", "inout_declaration"}) {
                if (kind(c, t)) declarations << c;
                else if (kind(c, "port_declaration") || kind(c, "module_item")) declarations += descendants(c, t);
            }
            for (auto declaration : declarations) {
                QString direction = QString::fromLatin1(ts_node_type(declaration)).section(QLatin1Char('_'), 0, 0);
                TSNode list{};
                for (auto item : children(declaration))
                    if (QString::fromLatin1(ts_node_type(item)).startsWith(QStringLiteral("list_of_"))) list = item;
                if (ts_node_is_null(list)) continue;
                const auto ids = descendants(list, "simple_identifier");
                const QString prefix = QString::fromUtf8(source.mid(ts_node_start_byte(declaration),
                    ts_node_start_byte(list) - ts_node_start_byte(declaration)));
                for (auto id : ids) {
                    Port p{text(id, source), direction, {}};
                    p.type = signalType(prefix, m, p.name, p.namedType);
                    if (p.namedType && p.direction == QStringLiteral("inout"))
                        m.limitations << QStringLiteral("Port %1 is an inout with a named type. Write the TB manually.").arg(p.name);
                    m.ports << p;
                }
            }
        }
        if (m.ports.isEmpty() && !text(child(header, "list_of_ports"), source).remove(QRegularExpression(QStringLiteral("[\\s()]"))).isEmpty())
            m.limitations << QStringLiteral("The non-ANSI ports could not be parsed reliably. Write the TB manually.");
    }
    m.limitations.removeDuplicates();
    return m;
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
