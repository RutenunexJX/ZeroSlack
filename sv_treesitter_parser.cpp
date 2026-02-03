#include "sv_treesitter_parser.h"
#include <QDebug>
#include <cstring>
#include <QMutex>

extern "C" TSLanguage *tree_sitter_systemverilog();

/** Process-wide shared TSParser; created once, never deleted (reused for performance). */
static TSParser *sharedParser()
{
    static TSParser *s_parser = nullptr;
    static QMutex s_initMutex;
    QMutexLocker lock(&s_initMutex);
    if (!s_parser) {
        s_parser = ts_parser_new();
        TSLanguage *lang = tree_sitter_systemverilog();
        if (!ts_parser_set_language(s_parser, lang))
            qCritical("SVTreeSitterParser: Failed to set Tree-sitter SystemVerilog language");
    }
    return s_parser;
}

/** Serializes ts_parser_parse_string calls (TSParser is not thread-safe). */
static QMutex &parseMutex()
{
    static QMutex s_parseMutex;
    return s_parseMutex;
}

/** Extract node text; utf8 must be valid UTF-8 (e.g. from QString::toUtf8()) for correct QString conversion. */
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
    // 1. Try direct field "name" (common for vars and some declarations)
    TSNode nameNode = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(nameNode)) {
        QString name = nodeText(utf8, nameNode);
        if (!name.isEmpty()) return name;
    }

    // 2. Scan children for *_header (common for module/interface/program)
    uint32_t childCount = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);
        if (!type) continue;

        if (strstr(type, "_header")) {
            TSNode headerName = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(headerName)) return nodeText(utf8, headerName);

            // Fallback: first simple_identifier inside header
            uint32_t grandChildCount = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < grandChildCount; ++j) {
                TSNode grandChild = ts_node_named_child(child, j);
                const char *grandType = ts_node_type(grandChild);
                if (grandType && strcmp(grandType, "simple_identifier") == 0)
                    return nodeText(utf8, grandChild);
            }
        }
    }

    // 3. Fallback: first simple_identifier or escaped_identifier at declaration level
    for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
        TSNode child = ts_node_named_child(node, i);
        const char *childType = ts_node_type(child);
        if (childType && (strcmp(childType, "simple_identifier") == 0 || strcmp(childType, "escaped_identifier") == 0))
            return nodeText(utf8, child);
    }
    return QString();
}

static sym_list::SymbolInfo makeSymbolInfo(TSNode node, const QString &symbolName, sym_list::sym_type_e symbolType,
                                           const QByteArray &utf8, const QString &dataType,
                                           const QString &moduleScope, const QString &fileName)
{
    sym_list::SymbolInfo info;
    info.fileName = fileName;
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
    info.moduleScope = moduleScope;
    info.scopeLevel = 0;
    info.dataType = dataType;
    return info;
}

SVTreeSitterParser::SVTreeSitterParser()
    : m_parser(nullptr)
    , m_tree(nullptr)
{
    m_parser = sharedParser();
}

SVTreeSitterParser::SVTreeSitterParser(const QString &content, const QString &fileName)
    : SVTreeSitterParser()
{
    m_currentContent = content;
    m_currentFileName = fileName;
    if (!content.isEmpty()) {
        if (m_tree) {
            ts_tree_delete(m_tree);
            m_tree = nullptr;
        }
        QByteArray utf8 = content.toUtf8();
        QMutexLocker lock(&parseMutex());
        m_tree = ts_parser_parse_string(m_parser, nullptr, utf8.constData(), static_cast<uint32_t>(utf8.size()));
    }
}

SVTreeSitterParser::~SVTreeSitterParser()
{
    if (m_tree) {
        ts_tree_delete(m_tree);
        m_tree = nullptr;
    }
    // m_parser points to shared instance; do not delete
}

void SVTreeSitterParser::parse(const QString &content)
{
    m_currentContent = content;
    if (m_tree) {
        ts_tree_delete(m_tree);
        m_tree = nullptr;
    }
    QByteArray utf8 = content.toUtf8();
    {
        QMutexLocker lock(&parseMutex());
        m_tree = ts_parser_parse_string(m_parser, nullptr, utf8.constData(), static_cast<uint32_t>(utf8.size()));
    }
    if (m_tree) {
        TSNode root = ts_tree_root_node(m_tree);
        qDebug() << "SVTreeSitterParser: root type =" << ts_node_type(root);
    }
}

/** True if node or any descendant has the given named node type (tree-sitter structure, not text). */
static bool hasDescendantOfType(TSNode node, const char *typeName)
{
    if (ts_node_is_null(node)) return false;
    const char *t = ts_node_type(node);
    if (t && strcmp(t, typeName) == 0) return true;
    for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
        if (hasDescendantOfType(ts_node_named_child(node, i), typeName)) return true;
    }
    return false;
}

/** Find first descendant with the given node type; returns that node or null. */
static TSNode findFirstDescendantOfType(TSNode node, const char *typeName)
{
    if (ts_node_is_null(node)) return node;
    const char *t = ts_node_type(node);
    if (t && strcmp(t, typeName) == 0) return node;
    for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
        TSNode found = findFirstDescendantOfType(ts_node_named_child(node, i), typeName);
        if (!ts_node_is_null(found)) return found;
    }
    return ts_node_child(node, 0); /* return any null-ish node; caller checks ts_node_is_null */
}

static void collectIdentifiersFromList(const QByteArray &utf8, TSNode listNode,
                                        QList<QPair<QString, TSNode>> &identifiers)
{
    for (uint32_t i = 0, n = ts_node_named_child_count(listNode); i < n; ++i) {
        TSNode child = ts_node_named_child(listNode, i);
        const char *t = ts_node_type(child);
        if (!t) continue;
        if (strcmp(t, "simple_identifier") == 0 || strcmp(t, "escaped_identifier") == 0) {
            QString name = nodeText(utf8, child);
            if (!name.isEmpty())
                identifiers.append(qMakePair(name, child));
        }
    }
}

/** Recursively find enum_name_declaration nodes and emit sym_enum_value for each (enum value name). */
static void collectEnumValuesInNode(const QByteArray &utf8, TSNode node, const QString &enumTypeName,
                                    const QString &scope, QList<sym_list::SymbolInfo> &out,
                                    const QString &fileName)
{
    const char *type = ts_node_type(node);
    if (type && strcmp(type, "enum_name_declaration") == 0) {
        QString name;
        TSNode idNode = ts_node_child_by_field_name(node, "enum_identifier", 15);
        if (!ts_node_is_null(idNode))
            name = nodeText(utf8, idNode);
        if (name.isEmpty()) {
            for (uint32_t j = 0, m = ts_node_named_child_count(node); j < m; ++j) {
                TSNode c = ts_node_named_child(node, j);
                const char *tc = ts_node_type(c);
                if (tc && (strcmp(tc, "simple_identifier") == 0 || strcmp(tc, "escaped_identifier") == 0)) {
                    name = nodeText(utf8, c);
                    break;
                }
            }
        }
        if (!name.isEmpty())
            out.append(makeSymbolInfo(node, name, sym_list::sym_enum_value, utf8, enumTypeName, scope, fileName));
        return;
    }
    for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
        collectEnumValuesInNode(utf8, ts_node_named_child(node, i), enumTypeName, scope, out, fileName);
}

void SVTreeSitterParser::visitNode(TSNode node, QStack<QString> &scopeStack,
                                   QList<sym_list::SymbolInfo> &out, const QByteArray &utf8)
{
    if (ts_node_is_null(node)) return;
    const char *type = ts_node_type(node);
    if (!type) return;

    const QString scope = scopeStack.isEmpty() ? QString() : scopeStack.top();

    if (strcmp(type, "module_declaration") == 0) {
        // Debug: print child types to help diagnose grammar/name extraction
        QStringList childTypes;
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
            const char *ct = ts_node_type(ts_node_named_child(node, i));
            if (ct) childTypes.append(QString::fromUtf8(ct));
        }
        qDebug() << "SVTreeSitterParser: module_declaration children:" << childTypes;

        QString name = nameFromDeclarationNode(utf8, node);
        if (!name.isEmpty()) {
            out.append(makeSymbolInfo(node, name, sym_list::sym_module, utf8, QString(), scope, m_currentFileName));
            scopeStack.push(name);
            for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
                visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
            scopeStack.pop();
        }
        return;
    }
    if (strcmp(type, "program_declaration") == 0) {
        QString name = nameFromDeclarationNode(utf8, node);
        if (!name.isEmpty()) {
            out.append(makeSymbolInfo(node, name, sym_list::sym_module, utf8, QString(), scope, m_currentFileName));
            scopeStack.push(name);
            for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
                visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
            scopeStack.pop();
        }
        return;
    }
    if (strcmp(type, "interface_declaration") == 0) {
        QString name = nameFromDeclarationNode(utf8, node);
        if (!name.isEmpty()) {
            out.append(makeSymbolInfo(node, name, sym_list::sym_interface, utf8, QString(), scope, m_currentFileName));
            scopeStack.push(name);
            for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
                visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
            scopeStack.pop();
        }
        return;
    }
    if (strcmp(type, "package_declaration") == 0) {
        QString name = nameFromDeclarationNode(utf8, node);
        if (!name.isEmpty())
            out.append(makeSymbolInfo(node, name, sym_list::sym_package, utf8, QString(), scope, m_currentFileName));
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
            visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
        return;
    }
    if (strcmp(type, "class_declaration") == 0) {
        QString name = nameFromDeclarationNode(utf8, node);
        if (!name.isEmpty())
            out.append(makeSymbolInfo(node, name, sym_list::sym_user, utf8, QString(), scope, m_currentFileName));
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
            visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
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
            TSNode nameOfInst = ts_node_child_by_field_name(child, "name_of_instance", 16);
            if (ts_node_is_null(nameOfInst)) continue;
            TSNode instNameNode = ts_node_child_by_field_name(nameOfInst, "instance_name", 14);
            QString instanceName = nodeText(utf8, instNameNode);
            if (instanceName.isEmpty()) continue;
            out.append(makeSymbolInfo(child, instanceName, sym_list::sym_inst, utf8, typeName, scope, m_currentFileName));
        }
        return;
    }
    if (strcmp(type, "task_declaration") == 0) {
        QString name;
        TSNode body = ts_node_child_by_field_name(node, "task_body_declaration", 21);
        if (!ts_node_is_null(body))
            name = nodeText(utf8, ts_node_child_by_field_name(body, "name", 4));
        if (name.isEmpty())
            name = nameFromDeclarationNode(utf8, node);
        if (!name.isEmpty())
            out.append(makeSymbolInfo(node, name, sym_list::sym_task, utf8, QString(), scope, m_currentFileName));
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
            visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
        return;
    }
    if (strcmp(type, "function_declaration") == 0) {
        QString name;
        TSNode body = ts_node_child_by_field_name(node, "function_body_declaration", 23);
        if (!ts_node_is_null(body))
            name = nodeText(utf8, ts_node_child_by_field_name(body, "name", 4));
        if (name.isEmpty())
            name = nameFromDeclarationNode(utf8, node);
        if (!name.isEmpty())
            out.append(makeSymbolInfo(node, name, sym_list::sym_function, utf8, QString(), scope, m_currentFileName));
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
            visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
        return;
    }
    if (strcmp(type, "always_construct") == 0) {
        TSPoint startPt = ts_node_start_point(node);
        int line = static_cast<int>(startPt.row) + 1;
        QString name = QString("always@line_%1").arg(line);
        out.append(makeSymbolInfo(node, name, sym_list::sym_always, utf8, QString(), scope, m_currentFileName));
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
            visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
        return;
    }
    if (strcmp(type, "ansi_port_declaration") == 0) {
        QString portName = nodeText(utf8, ts_node_child_by_field_name(node, "port_name", 9));
        sym_list::sym_type_e portType = sym_list::sym_port_input;
        for (uint32_t i = 0, n = ts_node_child_count(node); i < n; ++i) {
            TSNode c = ts_node_child(node, i);
            if (ts_node_type(c) && strcmp(ts_node_type(c), "port_direction") == 0) {
                // Use AST: port_direction has child node type input/output/inout/ref
                if (hasDescendantOfType(c, "input")) portType = sym_list::sym_port_input;
                else if (hasDescendantOfType(c, "output")) portType = sym_list::sym_port_output;
                else if (hasDescendantOfType(c, "inout")) portType = sym_list::sym_port_inout;
                else if (hasDescendantOfType(c, "ref")) portType = sym_list::sym_port_ref;
                break;
            }
        }
        if (!portName.isEmpty())
            out.append(makeSymbolInfo(node, portName, portType, utf8, QString(), scope, m_currentFileName));
        return;
    }
    if (strcmp(type, "port_declaration") == 0) {
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
            TSNode child = ts_node_named_child(node, i);
            const char *ct = ts_node_type(child);
            if (!ct) continue;
            sym_list::sym_type_e portType = sym_list::sym_port_input;
            if (strcmp(ct, "input_declaration") == 0) portType = sym_list::sym_port_input;
            else if (strcmp(ct, "output_declaration") == 0) portType = sym_list::sym_port_output;
            else if (strcmp(ct, "inout_declaration") == 0) portType = sym_list::sym_port_inout;
            else if (strcmp(ct, "ref_declaration") == 0) portType = sym_list::sym_port_ref;
            else { visitNode(child, scopeStack, out, utf8); continue; }
            for (uint32_t j = 0, m = ts_node_named_child_count(child); j < m; ++j) {
                TSNode c = ts_node_named_child(child, j);
                const char *tt = ts_node_type(c);
                if (tt && (strcmp(tt, "list_of_port_identifiers") == 0 || strcmp(tt, "list_of_variable_identifiers") == 0)) {
                    QList<QPair<QString, TSNode>> ids;
                    collectIdentifiersFromList(utf8, c, ids);
                    for (const auto &p : ids)
                        out.append(makeSymbolInfo(p.second, p.first, portType, utf8, QString(), scope, m_currentFileName));
                    break;
                }
            }
        }
        return;
    }
    if (strcmp(type, "data_declaration") == 0) {
        QString typeText;
        TSNode listVar = ts_node_child_by_field_name(node, "list_of_variable_decl_assignments", 30);
        if (ts_node_is_null(listVar)) {
            // Grammar may not expose field name; find by child type
            for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
                TSNode child = ts_node_named_child(node, i);
                if (ts_node_type(child) && strcmp(ts_node_type(child), "list_of_variable_decl_assignments") == 0) {
                    listVar = child;
                    break;
                }
            }
        }
        if (ts_node_is_null(listVar)) {
            return;
        }
        TSNode dataTypeNode = ts_node_child_by_field_name(node, "data_type_or_implicit", 20);
        if (ts_node_is_null(dataTypeNode)) {
            for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
                TSNode child = ts_node_named_child(node, i);
                if (ts_node_type(child) && strcmp(ts_node_type(child), "data_type_or_implicit") == 0) {
                    dataTypeNode = child;
                    break;
                }
            }
        }
        if (!ts_node_is_null(dataTypeNode))
            typeText = nodeText(utf8, dataTypeNode).trimmed().toLower();
        sym_list::sym_type_e symType = sym_list::sym_logic;
        // Use AST node types (tree-sitter grammar: reg/logic/wire are node types; net_type is rule)
        if (hasDescendantOfType(dataTypeNode, "enum_base_type"))
            symType = sym_list::sym_enum_var;
        else if (hasDescendantOfType(dataTypeNode, "reg"))
            symType = sym_list::sym_reg;
        else if (hasDescendantOfType(dataTypeNode, "logic"))
            symType = sym_list::sym_logic;
        else if (hasDescendantOfType(dataTypeNode, "net_type"))
            symType = sym_list::sym_wire;
        for (uint32_t i = 0, n = ts_node_named_child_count(listVar); i < n; ++i) {
            TSNode va = ts_node_named_child(listVar, i);
            if (ts_node_is_null(va)) continue;
            const char *vat = ts_node_type(va);
            if (!vat || strcmp(vat, "variable_decl_assignment") != 0) continue;
            QString name = nodeText(utf8, ts_node_child_by_field_name(va, "name", 4));
            if (name.isEmpty()) {
                TSNode nameNode;
                for (uint32_t j = 0, m = ts_node_named_child_count(va); j < m; ++j) {
                    TSNode c = ts_node_named_child(va, j);
                    if (ts_node_type(c) && (strcmp(ts_node_type(c), "simple_identifier") == 0 || strcmp(ts_node_type(c), "escaped_identifier") == 0)) {
                        name = nodeText(utf8, c);
                        break;
                    }
                }
            }
            if (!name.isEmpty()) {
                const char *typeLabel = (symType == sym_list::sym_reg) ? "reg" : (symType == sym_list::sym_wire) ? "wire"
                    : (symType == sym_list::sym_enum_var) ? "enum_var" : "logic";
                qDebug("TreeSitterParser: data_declaration -> %s '%s' (scope=%s) typeText='%s'", typeLabel, qPrintable(name), qPrintable(scope), qPrintable(typeText));
                out.append(makeSymbolInfo(va, name, symType, utf8, typeText, scope, m_currentFileName));
            }
        }
        // Anonymous enum { A, B } var: collect enum values (use AST, not text)
        if (!ts_node_is_null(dataTypeNode) && hasDescendantOfType(dataTypeNode, "enum_base_type"))
            collectEnumValuesInNode(utf8, dataTypeNode, QString(), scope, out, m_currentFileName);
        return;
    }
    if (strcmp(type, "net_declaration") == 0) {
        TSNode listNet = ts_node_child_by_field_name(node, "list_of_net_decl_assignments", 26);
        if (ts_node_is_null(listNet)) {
            for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
                TSNode child = ts_node_named_child(node, i);
                if (ts_node_type(child) && strcmp(ts_node_type(child), "list_of_net_decl_assignments") == 0) {
                    listNet = child;
                    break;
                }
            }
        }
        if (!ts_node_is_null(listNet)) {
            for (uint32_t i = 0, n = ts_node_named_child_count(listNet); i < n; ++i) {
                TSNode na = ts_node_named_child(listNet, i);
                if (ts_node_is_null(na)) continue;
                const char *nat = ts_node_type(na);
                if (!nat || strcmp(nat, "net_decl_assignment") != 0) continue;
                QString name;
                TSNode nameField = ts_node_child_by_field_name(na, "name", 4);
                if (!ts_node_is_null(nameField))
                    name = nodeText(utf8, nameField);
                if (name.isEmpty()) {
                    for (uint32_t j = 0, m = ts_node_named_child_count(na); j < m; ++j) {
                        TSNode idNode = ts_node_named_child(na, j);
                        const char *tid = ts_node_type(idNode);
                        if (tid && (strcmp(tid, "simple_identifier") == 0 || strcmp(tid, "escaped_identifier") == 0)) {
                            name = nodeText(utf8, idNode);
                            break;
                        }
                    }
                }
                if (!name.isEmpty()) {
                    qDebug("TreeSitterParser: net_declaration -> wire '%s' (scope=%s)", qPrintable(name), qPrintable(scope));
                    out.append(makeSymbolInfo(na, name, sym_list::sym_wire, utf8, QString("wire"), scope, m_currentFileName));
                }
            }
        }
        return;
    }

    // parameter_declaration: ( _parameter_declaration_no_semicolon ";" ) -> list_of_param_assignments may be inside first child
    if (strcmp(type, "parameter_declaration") == 0) {
        TSNode listNode = ts_node_named_child(node, 0); // placeholder; overwritten if found
        bool foundList = false;
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n && !foundList; ++i) {
            TSNode child = ts_node_named_child(node, i);
            const char *ct = ts_node_type(child);
            if (ct && strcmp(ct, "list_of_param_assignments") == 0) {
                listNode = child;
                foundList = true;
                break;
            }
            for (uint32_t j = 0, m = ts_node_named_child_count(child); j < m; ++j) {
                TSNode sub = ts_node_named_child(child, j);
                if (ts_node_type(sub) && strcmp(ts_node_type(sub), "list_of_param_assignments") == 0) {
                    listNode = sub;
                    foundList = true;
                    break;
                }
            }
        }
        if (foundList) {
            for (uint32_t i = 0, n = ts_node_named_child_count(listNode); i < n; ++i) {
                TSNode pa = ts_node_named_child(listNode, i);
                if (ts_node_type(pa) && strcmp(ts_node_type(pa), "param_assignment") == 0) {
                    QString name;
                    for (uint32_t j = 0, m = ts_node_named_child_count(pa); j < m; ++j) {
                        TSNode c = ts_node_named_child(pa, j);
                        const char *tc = ts_node_type(c);
                        if (tc && (strcmp(tc, "simple_identifier") == 0 || strcmp(tc, "escaped_identifier") == 0)) {
                            name = nodeText(utf8, c);
                            break;
                        }
                    }
                    if (!name.isEmpty())
                        out.append(makeSymbolInfo(pa, name, sym_list::sym_parameter, utf8, QString(), scope, m_currentFileName));
                }
            }
        }
        return;
    }

    // local_parameter_declaration: same structure, emit sym_localparam
    if (strcmp(type, "local_parameter_declaration") == 0) {
        TSNode listNode = ts_node_named_child(node, 0);
        bool foundList = false;
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n && !foundList; ++i) {
            TSNode child = ts_node_named_child(node, i);
            const char *ct = ts_node_type(child);
            if (ct && strcmp(ct, "list_of_param_assignments") == 0) {
                listNode = child;
                foundList = true;
                break;
            }
            for (uint32_t j = 0, m = ts_node_named_child_count(child); j < m; ++j) {
                TSNode sub = ts_node_named_child(child, j);
                if (ts_node_type(sub) && strcmp(ts_node_type(sub), "list_of_param_assignments") == 0) {
                    listNode = sub;
                    foundList = true;
                    break;
                }
            }
        }
        if (foundList) {
            for (uint32_t i = 0, n = ts_node_named_child_count(listNode); i < n; ++i) {
                TSNode pa = ts_node_named_child(listNode, i);
                if (ts_node_type(pa) && strcmp(ts_node_type(pa), "param_assignment") == 0) {
                    QString name;
                    for (uint32_t j = 0, m = ts_node_named_child_count(pa); j < m; ++j) {
                        TSNode c = ts_node_named_child(pa, j);
                        const char *tc = ts_node_type(c);
                        if (tc && (strcmp(tc, "simple_identifier") == 0 || strcmp(tc, "escaped_identifier") == 0)) {
                            name = nodeText(utf8, c);
                            break;
                        }
                    }
                    if (!name.isEmpty())
                        out.append(makeSymbolInfo(pa, name, sym_list::sym_localparam, utf8, QString(), scope, m_currentFileName));
                }
            }
        }
        return;
    }

    // type_declaration: typedef data_type type_name ; -> sym_typedef (u command). dataType from data_type text (e.g. "enum").
    if (strcmp(type, "type_declaration") == 0) {
        TSNode typeNameNode = ts_node_child_by_field_name(node, "type_name", 9);
        QString typeName = nodeText(utf8, typeNameNode);
        if (typeName.isEmpty()) {
            for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
                TSNode c = ts_node_named_child(node, i);
                const char *tc = ts_node_type(c);
                if (tc && (strcmp(tc, "simple_identifier") == 0 || strcmp(tc, "escaped_identifier") == 0)) {
                    typeName = nodeText(utf8, c);
                    break;
                }
            }
        }
        QString dataTypeStr;
        for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i) {
            TSNode c = ts_node_named_child(node, i);
            if (ts_node_type(c) && strcmp(ts_node_type(c), "data_type") == 0) {
                dataTypeStr = nodeText(utf8, c).trimmed().toLower();
                break;
            }
        }
        if (!typeName.isEmpty()) {
            out.append(makeSymbolInfo(node, typeName, sym_list::sym_typedef, utf8, dataTypeStr, scope, m_currentFileName));
            // If typedef enum { A, B } name; emit sym_enum_value (use AST: enum_base_type present)
            if (hasDescendantOfType(node, "enum_base_type")) {
                collectEnumValuesInNode(utf8, node, typeName, scope, out, m_currentFileName);
            }
        }
        return;
    }

    for (uint32_t i = 0, n = ts_node_named_child_count(node); i < n; ++i)
        visitNode(ts_node_named_child(node, i), scopeStack, out, utf8);
}

QList<sym_list::SymbolInfo> SVTreeSitterParser::parseSymbols()
{
    QList<sym_list::SymbolInfo> out;
    if (m_currentContent.isEmpty()) return out;
    // Ensure m_tree is valid before traversing (re-parse if needed)
    if (!m_tree) {
        parse(m_currentContent);
        if (!m_tree) return out;
    }
    QByteArray utf8 = m_currentContent.toUtf8();
    TSNode root = ts_tree_root_node(m_tree);
    QStack<QString> scopeStack;
    visitNode(root, scopeStack, out, utf8);

    int countReg = 0, countWire = 0, countLogic = 0;
    for (const sym_list::SymbolInfo &s : out) {
        if (s.symbolType == sym_list::sym_reg) countReg++;
        else if (s.symbolType == sym_list::sym_wire) countWire++;
        else if (s.symbolType == sym_list::sym_logic) countLogic++;
    }
    qDebug("TreeSitterParser: Found %d symbols in file %s (reg=%d wire=%d logic=%d)", out.size(), qPrintable(m_currentFileName), countReg, countWire, countLogic);
    return out;
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
    QStack<QString> scopeStack;
    visitNode(root, scopeStack, out, utf8);
    qDebug() << "SVTreeSitterParser::getSymbols: found" << out.size() << "symbols";
    return out;
}

QList<sym_list::CommentRegion> SVTreeSitterParser::takeComments()
{
    QList<sym_list::CommentRegion> result = m_commentRegions;
    m_commentRegions.clear();
    return result;
}
