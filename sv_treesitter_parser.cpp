#include "sv_treesitter_parser.h"
#include <QDebug>
#include <cstring>

extern "C" TSLanguage *tree_sitter_systemverilog();

static QString nodeText(const QByteArray &utf8, TSNode node)
{
    if (utf8.isEmpty()) return QString();
    if (ts_node_is_null(node)) return QString();
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (start >= static_cast<uint32_t>(utf8.size()) || end > static_cast<uint32_t>(utf8.size()) || start >= end)
        return QString();
    return QString::fromUtf8(utf8.mid(static_cast<int>(start), static_cast<int>(end - start)));
}

static QString nameFromDeclarationNode(const QByteArray &utf8, TSNode node)
{
    TSNode nameNode = ts_node_child_by_field_name(node, "name", 4);
    QString name = nodeText(utf8, nameNode);
    if (!name.isEmpty()) return name;
    // ANSI/Non-ANSI: module name is on the header child (module_ansi_header / module_nonansi_header)
    for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
        TSNode child = ts_node_named_child(node, i);
        const char *childType = ts_node_type(child);
        if (!childType) continue;
        if (strcmp(childType, "module_ansi_header") == 0 || strcmp(childType, "module_nonansi_header") == 0) {
            name = nodeText(utf8, ts_node_child_by_field_name(child, "name", 4));
            if (!name.isEmpty()) return name;
        }
    }
    for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
        TSNode child = ts_node_named_child(node, i);
        const char *childType = ts_node_type(child);
        if (childType && (strcmp(childType, "simple_identifier") == 0 || strcmp(childType, "escaped_identifier") == 0))
            return nodeText(utf8, child);
    }
    return QString();
}

static sym_list::SymbolInfo makeSymbolInfo(TSNode node, const QString &symbolName, sym_list::sym_type_e symbolType,
                                           const QByteArray &utf8, const QString &dataType = QString())
{
    sym_list::SymbolInfo info;
    info.fileName.clear();
    info.symbolName = symbolName;
    info.symbolType = symbolType;
    TSPoint startPt = ts_node_start_point(node);
    TSPoint endPt = ts_node_end_point(node);
    info.startLine = static_cast<int>(startPt.row) + 1;
    info.startColumn = static_cast<int>(startPt.column);
    info.endLine = static_cast<int>(endPt.row) + 1;
    info.endColumn = static_cast<int>(endPt.column);
    info.position = static_cast<int>(ts_node_start_byte(node));
    info.length = static_cast<int>(ts_node_end_byte(node) - ts_node_start_byte(node));
    info.symbolId = 0;
    info.moduleScope.clear();
    info.scopeLevel = 0;
    info.dataType = dataType;
    return info;
}

SVTreeSitterParser::SVTreeSitterParser()
    : m_parser(nullptr)
    , m_tree(nullptr)
{
    m_parser = ts_parser_new();
    TSLanguage *lang = tree_sitter_systemverilog();
    if (!ts_parser_set_language(m_parser, lang)) {
        qCritical("SVTreeSitterParser: Failed to set Tree-sitter SystemVerilog language");
    }
}

SVTreeSitterParser::~SVTreeSitterParser()
{
    if (m_tree) {
        ts_tree_delete(m_tree);
        m_tree = nullptr;
    }
    if (m_parser) {
        ts_parser_delete(m_parser);
        m_parser = nullptr;
    }
}

void SVTreeSitterParser::parse(const QString &content)
{
    m_currentContent = content;
    if (m_tree) {
        ts_tree_delete(m_tree);
        m_tree = nullptr;
    }
    QByteArray utf8 = content.toUtf8();
    m_tree = ts_parser_parse_string(m_parser, nullptr, utf8.constData(), static_cast<uint32_t>(utf8.size()));
    if (m_tree) {
        TSNode root = ts_tree_root_node(m_tree);
        qDebug() << "SVTreeSitterParser: root type =" << ts_node_type(root);
    }
}

QList<sym_list::SymbolInfo> SVTreeSitterParser::getSymbols()
{
    QList<sym_list::SymbolInfo> out;
    if (!m_tree || m_currentContent.isEmpty()) {
        qDebug() << "SVTreeSitterParser::getSymbols: no tree or content, returning" << out.size() << "symbols";
        return out;
    }

    QByteArray utf8 = m_currentContent.toUtf8();
    TSNode root = ts_tree_root_node(m_tree);
    TSTreeCursor cursor = ts_tree_cursor_new(root);

    auto visit = [&](TSNode node) {
        const char *type = ts_node_type(node);
        if (!type) return;

        if (strcmp(type, "module_declaration") == 0) {
            QString name = nameFromDeclarationNode(utf8, node);
            out.append(makeSymbolInfo(node, name, sym_list::sym_module, utf8));
            return;
        }
        if (strcmp(type, "program_declaration") == 0) {
            QString name = nameFromDeclarationNode(utf8, node);
            out.append(makeSymbolInfo(node, name, sym_list::sym_module, utf8));
            return;
        }
        if (strcmp(type, "package_declaration") == 0) {
            QString name = nameFromDeclarationNode(utf8, node);
            out.append(makeSymbolInfo(node, name, sym_list::sym_package, utf8));
            return;
        }
        if (strcmp(type, "interface_declaration") == 0) {
            QString name = nameFromDeclarationNode(utf8, node);
            out.append(makeSymbolInfo(node, name, sym_list::sym_interface, utf8));
            return;
        }
        if (strcmp(type, "class_declaration") == 0) {
            QString name = nameFromDeclarationNode(utf8, node);
            out.append(makeSymbolInfo(node, name, sym_list::sym_user, utf8));
            return;
        }
        if (strcmp(type, "module_instantiation") == 0) {
            TSNode typeNode = ts_node_child_by_field_name(node, "instance_type", 14);
            QString typeName = nodeText(utf8, typeNode);
            for (uint32_t i = 0, n = ts_node_child_count(node); i < n; ++i) {
                TSNode child = ts_node_child(node, i);
                if (ts_node_is_null(child)) continue;
                const char *childType = ts_node_type(child);
                if (!childType || strcmp(childType, "hierarchical_instance") != 0) continue;
                bool foundName = false;
                TSNode nameOfInst;
                for (uint32_t j = 0, m = ts_node_child_count(child); j < m; ++j) {
                    TSNode c = ts_node_child(child, j);
                    if (ts_node_type(c) && strcmp(ts_node_type(c), "name_of_instance") == 0) {
                        nameOfInst = c;
                        foundName = true;
                        break;
                    }
                }
                if (!foundName) continue;
                TSNode instNameNode = ts_node_child_by_field_name(nameOfInst, "instance_name", 14);
                QString instanceName = nodeText(utf8, instNameNode);
                if (instanceName.isEmpty()) continue;
                out.append(makeSymbolInfo(child, instanceName, sym_list::sym_inst, utf8, typeName));
            }
        }
    };

    for (;;) {
        TSNode cur = ts_tree_cursor_current_node(&cursor);
        if (!ts_node_is_null(cur))
            visit(cur);
        if (ts_tree_cursor_goto_first_child(&cursor))
            continue;
        if (ts_tree_cursor_goto_next_sibling(&cursor))
            continue;
        for (;;) {
            if (!ts_tree_cursor_goto_parent(&cursor))
                goto done;
            if (ts_tree_cursor_goto_next_sibling(&cursor))
                break;
        }
    }
done:
    ts_tree_cursor_delete(&cursor);
    qDebug() << "SVTreeSitterParser::getSymbols: found" << out.size() << "symbols";
    return out;
}
