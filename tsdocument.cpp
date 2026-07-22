#include "tsdocument.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <QSet>

extern "C" TSLanguage *tree_sitter_systemverilog();

HlCategory classifyTokenType(const char* type, bool isNamed)
{
    if (!type || !*type)
        return HlCategory::None;

    // Comments are "one_line_comment" / "block_comment" in tree-sitter-systemverilog.
    if (std::strcmp(type, "one_line_comment") == 0 ||
        std::strcmp(type, "block_comment") == 0)
        return HlCategory::Comment;
    if (std::strcmp(type, "string_literal") == 0)
        return HlCategory::String;

    if (!isNamed) {
        // Anonymous tokens are the grammar's string literals: WORD literals are keywords
        // (logic, reg, wire, input, begin, if, ...), SYMBOL literals are operators/punctuation
        // (";", "(", "=", "+", "<=", ...). Identifiers are NAMED (simple_identifier), never here.
        const char c = type[0];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        return word ? HlCategory::Keyword : HlCategory::Operator;
    }

    // Named control keywords are wrapped as "<kw>_keyword" (module_keyword, always_keyword, ...).
    const size_t len = std::strlen(type);
    if (len > 8 && std::strcmp(type + len - 8, "_keyword") == 0)
        return HlCategory::Keyword;
    // Numeric literal tokens: binary_number, decimal_number, hex_number, integral_number, ...
    if (std::strstr(type, "number") != nullptr)
        return HlCategory::Number;

    // Identifiers and structural/container nodes render with the default format (recurse into them).
    return HlCategory::None;
}

TSDocument::TSDocument()
{
    m_parser = ts_parser_new();
    ts_parser_set_language(m_parser, tree_sitter_systemverilog());
    reparse(nullptr);   // parse empty doc so rootNode() is always valid
}

TSDocument::~TSDocument()
{
    if (m_tree)
        ts_tree_delete(m_tree);
    if (m_parser)
        ts_parser_delete(m_parser);
}

void TSDocument::reparse(TSTree* oldTree)
{
    // QString internal storage is UTF-16 (host-endian = LE on this target). Length is in bytes.
    const char* data = reinterpret_cast<const char*>(m_text.utf16());
    const uint32_t lengthBytes = static_cast<uint32_t>(m_text.size()) * 2u;

    TSTree* newTree = ts_parser_parse_string_encoding(
        m_parser, oldTree, data, lengthBytes, TSInputEncodingUTF16LE);

    if (m_tree)
        ts_tree_delete(m_tree);
    m_tree = newTree;
}

void TSDocument::setText(const QString& text)
{
    m_text = QString(text.constData(), text.size());
    reparse(nullptr);
}

namespace {
TSPoint endPointForText(int startLine,
                        int startColumn,
                        const QString& text)
{
    const int lastNewline = text.lastIndexOf(QLatin1Char('\n'));
    const int newlineCount = text.count(QLatin1Char('\n'));
    TSPoint point;
    point.row = static_cast<uint32_t>(startLine + newlineCount);
    const int column = newlineCount == 0
        ? startColumn + text.size()
        : text.size() - lastNewline - 1;
    point.column = static_cast<uint32_t>(qMax(0, column)) * 2u;
    return point;
}
} // namespace

QList<TSChangedRange> TSDocument::applyEdit(const DocumentChange& change)
{
    QList<TSChangedRange> changedRanges;
    const int position = qBound(0, change.position, m_text.size());
    const int removedLength = qBound(
        0, change.removedLength, m_text.size() - position);

    TSInputEdit edit{};
    edit.start_byte = static_cast<uint32_t>(position) * 2u;
    edit.old_end_byte = static_cast<uint32_t>(position + removedLength) * 2u;
    edit.new_end_byte = static_cast<uint32_t>(
        position + change.insertedText.size()) * 2u;
    edit.start_point.row = static_cast<uint32_t>(qMax(0, change.startLine));
    edit.start_point.column = static_cast<uint32_t>(
        qMax(0, change.startColumn)) * 2u;
    edit.old_end_point = endPointForText(change.startLine,
                                         change.startColumn,
                                         change.removedText);
    edit.new_end_point = endPointForText(change.startLine,
                                         change.startColumn,
                                         change.insertedText);

    if (m_tree)
        ts_tree_edit(m_tree, &edit);
    m_text.replace(position, removedLength, change.insertedText);

    const char* data = reinterpret_cast<const char*>(m_text.utf16());
    const uint32_t lengthBytes = static_cast<uint32_t>(m_text.size()) * 2u;
    TSTree* newTree = ts_parser_parse_string_encoding(
        m_parser, m_tree, data, lengthBytes, TSInputEncodingUTF16LE);

    if (m_tree && newTree) {
        uint32_t rangeCount = 0;
        TSRange* ranges = ts_tree_get_changed_ranges(
            m_tree, newTree, &rangeCount);
        changedRanges.reserve(static_cast<int>(rangeCount));
        for (uint32_t index = 0; index < rangeCount; ++index) {
            TSChangedRange range;
            range.startChar = static_cast<int>(ranges[index].start_byte / 2u);
            range.endChar = static_cast<int>(ranges[index].end_byte / 2u);
            range.startLine = static_cast<int>(ranges[index].start_point.row);
            range.endLine = static_cast<int>(ranges[index].end_point.row);
            changedRanges.append(range);
        }
        std::free(ranges);
    }

    if (m_tree)
        ts_tree_delete(m_tree);
    m_tree = newTree;
    return changedRanges;
}

TSNode TSDocument::rootNode() const
{
    return ts_tree_root_node(m_tree);
}

bool TSDocument::hasError() const
{
    return ts_node_has_error(ts_tree_root_node(m_tree));
}

bool TSDocument::isCommentAt(int charOffset) const
{
    if (charOffset < 0)
        return false;

    const uint32_t b = static_cast<uint32_t>(charOffset) * 2u;
    TSNode node = ts_node_descendant_for_byte_range(ts_tree_root_node(m_tree), b, b);
    while (!ts_node_is_null(node)) {
        const char* t = ts_node_type(node);
        if (t && (std::strcmp(t, "one_line_comment") == 0 ||
                  std::strcmp(t, "block_comment") == 0)) {
            return true;
        }
        node = ts_node_parent(node);
    }
    return false;
}

namespace {
// Extract the declared name from a *_declaration node: try a "name" field, else find a "*_header"
// child and take its name field or first simple_identifier.
QString declarationName(const QString& text, TSNode declNode)
{
    auto textOf = [&text](TSNode n) -> QString {
        if (ts_node_is_null(n)) return QString();
        uint32_t s = ts_node_start_byte(n), e = ts_node_end_byte(n);
        if (s >= e) return QString();
        return text.mid(static_cast<int>(s / 2), static_cast<int>((e - s) / 2));
    };

    TSNode nameNode = ts_node_child_by_field_name(declNode, "name", 4);
    if (!ts_node_is_null(nameNode)) {
        QString n = textOf(nameNode);
        if (!n.isEmpty()) return n;
    }
    const uint32_t childCount = ts_node_named_child_count(declNode);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(declNode, i);
        const char* ct = ts_node_type(child);
        if (!ct || !std::strstr(ct, "_header"))
            continue;
        TSNode hn = ts_node_child_by_field_name(child, "name", 4);
        if (!ts_node_is_null(hn)) return textOf(hn);
        const uint32_t gc = ts_node_named_child_count(child);
        for (uint32_t j = 0; j < gc; ++j) {
            TSNode g = ts_node_named_child(child, j);
            const char* gt = ts_node_type(g);
            if (gt && std::strcmp(gt, "simple_identifier") == 0)
                return textOf(g);
        }
    }
    return QString();
}

bool isFoldableSyntaxNode(const char* type)
{
    if (!type)
        return false;
    return std::strcmp(type, "module_declaration") == 0
        || std::strcmp(type, "interface_declaration") == 0
        || std::strcmp(type, "package_declaration") == 0
        || std::strcmp(type, "class_declaration") == 0
        || std::strcmp(type, "function_declaration") == 0
        || std::strcmp(type, "task_declaration") == 0
        || std::strcmp(type, "seq_block") == 0
        || std::strcmp(type, "case_statement") == 0
        || std::strcmp(type, "conditional_generate_construct") == 0
        || std::strcmp(type, "loop_generate_construct") == 0
        || std::strcmp(type, "case_generate_construct") == 0
        || std::strcmp(type, "generate_region") == 0
        || std::strcmp(type, "par_block") == 0
        || std::strcmp(type, "struct_union") == 0
        || std::strcmp(type, "enum_name_declaration") == 0;
}

QString foldSyntaxLabel(const char* type)
{
    if (!type)
        return QStringLiteral("...");
    QString label = QString::fromLatin1(type);
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    return label;
}

QString nodeText(const QString& text, TSNode node)
{
    if (ts_node_is_null(node))
        return QString();
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (end <= start)
        return QString();
    return text.mid(static_cast<int>(start / 2),
                    static_cast<int>((end - start) / 2));
}

bool nodeTypeIs(TSNode node, const char* expected)
{
    if (ts_node_is_null(node) || !expected)
        return false;
    const char* type = ts_node_type(node);
    return type && std::strcmp(type, expected) == 0;
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

bool isRtlContainerDeclaration(TSNode node)
{
    return nodeTypeIs(node, "module_declaration")
        || nodeTypeIs(node, "interface_declaration")
        || nodeTypeIs(node, "program_declaration");
}

TSNode rtlContainerAncestor(TSNode node)
{
    while (!ts_node_is_null(node)) {
        if (isRtlContainerDeclaration(node))
            return node;
        node = ts_node_parent(node);
    }
    return {};
}

QString rtlContainerKindText(TSNode node)
{
    if (nodeTypeIs(node, "interface_declaration"))
        return QStringLiteral("interface");
    if (nodeTypeIs(node, "program_declaration"))
        return QStringLiteral("program");
    return QStringLiteral("module");
}

TSNode directNamedChildOfType(TSNode node, const char* expected)
{
    if (ts_node_is_null(node))
        return {};
    const uint32_t childCount = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(node, i);
        if (nodeTypeIs(child, expected))
            return child;
    }
    return {};
}

int nodeStartChar(TSNode node)
{
    return static_cast<int>(ts_node_start_byte(node) / 2);
}

int nodeEndChar(TSNode node)
{
    return static_cast<int>(ts_node_end_byte(node) / 2);
}

int nodeStartLine(TSNode node)
{
    return static_cast<int>(ts_node_start_point(node).row);
}

int nodeEndLine(TSNode node)
{
    return static_cast<int>(ts_node_end_point(node).row);
}

QString leadingIdentifierAt(const QString& text, int start, int end)
{
    int pos = qBound(0, start, text.size());
    const int limit = qBound(pos, end, text.size());
    while (pos < limit && text.at(pos).isSpace())
        ++pos;

    const int identifierStart = pos;
    while (pos < limit) {
        const QChar ch = text.at(pos);
        const ushort value = ch.unicode();
        const bool asciiAlpha =
            (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
        const bool asciiDigit = value >= '0' && value <= '9';
        if (!(asciiAlpha || asciiDigit || ch == QLatin1Char('_')))
            break;
        ++pos;
    }
    return pos > identifierStart
        ? text.mid(identifierStart, pos - identifierStart)
        : QString();
}

int firstNonSpaceChar(const QString& text, int start, int end)
{
    int pos = qBound(0, start, text.size());
    const int limit = qBound(pos, end, text.size());
    while (pos < limit && text.at(pos).isSpace())
        ++pos;
    return pos;
}

int lastNonSpaceChar(const QString& text, int start, int end)
{
    const int boundedStart = qBound(0, start, text.size());
    int pos = qBound(boundedStart, end, text.size());
    while (pos > boundedStart && text.at(pos - 1).isSpace())
        --pos;
    return pos - 1;
}

TSNode namedNodeAtChar(TSTree* tree, int charOffset, int textSize)
{
    if (!tree)
        return {};
    const int bounded = qBound(0, charOffset, qMax(0, textSize - 1));
    const uint32_t byte = static_cast<uint32_t>(bounded) * 2u;
    return ts_node_named_descendant_for_byte_range(ts_tree_root_node(tree),
                                                   byte,
                                                   byte);
}

int lineStartChar(const QString& text, int line)
{
    if (line <= 0)
        return 0;
    int currentLine = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('\n'))
            continue;
        ++currentLine;
        if (currentLine == line)
            return i + 1;
    }
    return text.size();
}

int lineEndChar(const QString& text, int line)
{
    const int start = lineStartChar(text, line);
    for (int i = start; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            return i;
    }
    return text.size();
}

QString lineIndentAt(const QString& text, int line)
{
    const int start = lineStartChar(text, line);
    const int end = lineEndChar(text, line);
    int pos = start;
    while (pos < end) {
        const QChar ch = text.at(pos);
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            break;
        ++pos;
    }
    return text.mid(start, pos - start);
}

bool lineIsBlank(const QString& text, int line)
{
    if (line < 0)
        return false;
    const int start = lineStartChar(text, line);
    const int end = lineEndChar(text, line);
    for (int pos = start; pos < end; ++pos) {
        const QChar ch = text.at(pos);
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            return false;
    }
    return true;
}

int previousNonBlankLine(const QString& text, int line)
{
    for (int current = line; current >= 0; --current) {
        if (!lineIsBlank(text, current))
            return current;
    }
    return -1;
}

int nodeLastLine(TSNode node)
{
    const TSPoint start = ts_node_start_point(node);
    const TSPoint end = ts_node_end_point(node);
    int row = static_cast<int>(end.row);
    if (end.column == 0 && row > static_cast<int>(start.row))
        --row;
    return row;
}

struct PortParenRange {
    TSNode openParen{};
    TSNode closeParen{};
};

PortParenRange findPortListParens(const QString& text, TSNode header)
{
    PortParenRange range;
    const uint32_t childCount = ts_node_child_count(header);
    int closeIndex = -1;
    for (int i = static_cast<int>(childCount) - 1; i >= 0; --i) {
        TSNode child = ts_node_child(header, static_cast<uint32_t>(i));
        if (nodeText(text, child) == QStringLiteral(")")) {
            range.closeParen = child;
            closeIndex = i;
            break;
        }
    }
    if (closeIndex < 0)
        return range;

    for (int i = closeIndex - 1; i >= 0; --i) {
        TSNode child = ts_node_child(header, static_cast<uint32_t>(i));
        if (nodeText(text, child) == QStringLiteral("(")) {
            range.openParen = child;
            break;
        }
    }
    return range;
}

bool portListContainsOnlyClearChildren(TSNode portList)
{
    const uint32_t childCount = ts_node_named_child_count(portList);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(portList, i);
        if (nodeTypeIs(child, "ansi_port_declaration")
            || nodeTypeIs(child, "attribute_instance")) {
            continue;
        }
        return false;
    }
    return true;
}

QList<TSNode> ansiPortDeclarations(TSNode portList)
{
    QList<TSNode> result;
    const uint32_t childCount = ts_node_named_child_count(portList);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(portList, i);
        if (nodeTypeIs(child, "ansi_port_declaration")
            && nodeEndChar(child) > nodeStartChar(child)) {
            result.append(child);
        }
    }
    return result;
}

bool isParameterPortEntryNode(TSNode node)
{
    return nodeTypeIs(node, "parameter_port_declaration")
        || nodeTypeIs(node, "list_of_param_assignments");
}

bool parameterPortListContainsOnlyClearChildren(TSNode parameterPortList)
{
    const uint32_t childCount = ts_node_named_child_count(parameterPortList);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(parameterPortList, i);
        if (isParameterPortEntryNode(child)
            || nodeTypeIs(child, "attribute_instance")) {
            continue;
        }
        return false;
    }
    return true;
}

QList<TSNode> parameterPortEntries(TSNode parameterPortList)
{
    QList<TSNode> result;
    const uint32_t childCount = ts_node_named_child_count(parameterPortList);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_named_child(parameterPortList, i);
        if (isParameterPortEntryNode(child)
            && nodeEndChar(child) > nodeStartChar(child)) {
            result.append(child);
        }
    }
    return result;
}

bool trailingCommaStateOnPortLine(const QString& text,
                                  TSNode portDecl,
                                  int portLine,
                                  bool* hasComma,
                                  int* commaInsertChar)
{
    if (!hasComma || !commaInsertChar)
        return false;

    const int lineEnd = lineEndChar(text, portLine);
    const int lineStart = lineStartChar(text, portLine);
    int scanStart = qBound(lineStart, nodeEndChar(portDecl), lineEnd);
    int nodeTrimmedEnd = scanStart;
    while (nodeTrimmedEnd > lineStart
           && text.at(nodeTrimmedEnd - 1).isSpace()) {
        --nodeTrimmedEnd;
    }
    if (nodeTrimmedEnd > lineStart
        && text.at(nodeTrimmedEnd - 1) == QLatin1Char(',')) {
        int pos = scanStart;
        while (pos < lineEnd && text.at(pos).isSpace())
            ++pos;
        if (pos != lineEnd)
            return false;
        *hasComma = true;
        *commaInsertChar = -1;
        return true;
    }

    int pos = scanStart;
    while (pos < lineEnd && text.at(pos).isSpace())
        ++pos;

    if (pos >= lineEnd) {
        int trimmedEnd = lineEnd;
        while (trimmedEnd > scanStart && text.at(trimmedEnd - 1).isSpace())
            --trimmedEnd;
        *hasComma = false;
        *commaInsertChar = trimmedEnd;
        return true;
    }

    if (text.at(pos) != QLatin1Char(','))
        return false;

    ++pos;
    while (pos < lineEnd && text.at(pos).isSpace())
        ++pos;
    if (pos != lineEnd)
        return false;

    *hasComma = true;
    *commaInsertChar = -1;
    return true;
}

bool emptyPortListHasNoNamedContent(TSNode portList)
{
    return ts_node_named_child_count(portList) == 0;
}

bool descendantNodeTypeIs(TSNode node, const char* expected)
{
    if (ts_node_is_null(node))
        return false;
    if (nodeTypeIs(node, expected))
        return true;
    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i) {
        if (descendantNodeTypeIs(ts_node_child(node, i), expected))
            return true;
    }
    return false;
}

TSNode effectiveModuleMemberNode(TSNode node)
{
    if (!nodeTypeIs(node, "module_item"))
        return node;
    if (ts_node_named_child_count(node) != 1)
        return node;
    return ts_node_named_child(node, 0);
}

TSNode effectivePackageItemNode(TSNode node)
{
    if (!nodeTypeIs(node, "package_item"))
        return node;
    if (ts_node_named_child_count(node) != 1)
        return node;
    return ts_node_named_child(node, 0);
}

bool isInternalSignalDeclaration(TSNode node)
{
    if (nodeTypeIs(node, "net_declaration")) {
        return !ts_node_is_null(
                   directNamedChildOfType(node,
                                          "list_of_net_decl_assignments"))
            && descendantNodeTypeIs(node, "wire");
    }
    if (nodeTypeIs(node, "data_declaration")) {
        return !ts_node_is_null(
                   directNamedChildOfType(node,
                                          "list_of_variable_decl_assignments"))
            && (descendantNodeTypeIs(node, "logic")
                || descendantNodeTypeIs(node, "reg"));
    }
    return false;
}

bool isParameterLikeDeclaration(TSNode node)
{
    return nodeTypeIs(node, "parameter_declaration")
        || nodeTypeIs(node, "local_parameter_declaration");
}

bool isPackageToolTypeDeclaration(PackageToolKind kind, TSNode node)
{
    if (!nodeTypeIs(node, "type_declaration"))
        return false;

    switch (kind) {
    case PackageToolKind::TypedefEnum:
        return descendantNodeTypeIs(node, "enum_name_declaration");
    case PackageToolKind::TypedefStruct:
        return descendantNodeTypeIs(node, "struct_union")
            && !descendantNodeTypeIs(node, "packed");
    case PackageToolKind::TypedefStructPacked:
        return descendantNodeTypeIs(node, "struct_union")
            && descendantNodeTypeIs(node, "packed");
    case PackageToolKind::Parameter:
    case PackageToolKind::Localparam:
    case PackageToolKind::Function:
        return false;
    }
    return false;
}

bool isPackageToolSameKind(PackageToolKind kind, TSNode node)
{
    switch (kind) {
    case PackageToolKind::Parameter:
        return nodeTypeIs(node, "parameter_declaration");
    case PackageToolKind::Localparam:
        return nodeTypeIs(node, "local_parameter_declaration");
    case PackageToolKind::TypedefEnum:
    case PackageToolKind::TypedefStruct:
    case PackageToolKind::TypedefStructPacked:
        return isPackageToolTypeDeclaration(kind, node);
    case PackageToolKind::Function:
        return nodeTypeIs(node, "function_declaration");
    }
    return false;
}

bool isModuleBodyBoundaryNode(TSNode node)
{
    return nodeTypeIs(node, "always_construct")
        || nodeTypeIs(node, "initial_construct")
        || nodeTypeIs(node, "final_construct")
        || nodeTypeIs(node, "continuous_assign")
        || nodeTypeIs(node, "generate_region")
        || nodeTypeIs(node, "conditional_generate_construct")
        || nodeTypeIs(node, "loop_generate_construct")
        || nodeTypeIs(node, "case_generate_construct")
        || nodeTypeIs(node, "module_instantiation")
        || nodeTypeIs(node, "interface_instantiation")
        || nodeTypeIs(node, "gate_instantiation")
        || nodeTypeIs(node, "function_declaration")
        || nodeTypeIs(node, "task_declaration");
}

TSNode directModuleHeader(TSNode module)
{
    TSNode header = directNamedChildOfType(module, "module_ansi_header");
    if (!ts_node_is_null(header))
        return header;
    return directNamedChildOfType(module, "module_nonansi_header");
}

int endmoduleLine(TSNode module, const QString& text)
{
    const uint32_t childCount = ts_node_child_count(module);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(module, i);
        if (nodeText(text, child) == QStringLiteral("endmodule"))
            return static_cast<int>(ts_node_start_point(child).row);
    }
    return -1;
}

int endpackageLine(TSNode package, const QString& text)
{
    const uint32_t childCount = ts_node_child_count(package);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(package, i);
        if (nodeText(text, child) == QStringLiteral("endpackage"))
            return static_cast<int>(ts_node_start_point(child).row);
    }
    return -1;
}

int seqBlockEndLine(TSNode block, const QString& text)
{
    const uint32_t childCount = ts_node_child_count(block);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(block, i);
        if (nodeText(text, child) == QStringLiteral("end"))
            return static_cast<int>(ts_node_start_point(child).row);
    }
    return nodeLastLine(block);
}

struct SignalInsertAnchor {
    bool valid = false;
    bool insertAfterLine = true;
    int line = -1;
    QString indent;
};

SignalInsertAnchor anchorAfterNode(const QString& text, TSNode node)
{
    SignalInsertAnchor anchor;
    anchor.valid = !ts_node_is_null(node);
    anchor.insertAfterLine = true;
    anchor.line = nodeLastLine(node);
    anchor.indent = lineIndentAt(text, anchor.line);
    return anchor;
}

SignalInsertAnchor anchorBeforeLine(const QString& text,
                                    int line,
                                    const QString& indent)
{
    SignalInsertAnchor anchor;
    anchor.valid = line >= 0;
    anchor.insertAfterLine = false;
    anchor.line = line;
    anchor.indent = indent;
    if (anchor.indent.isNull())
        anchor.indent = lineIndentAt(text, line);
    return anchor;
}

QString commentPayload(const QString& commentText)
{
    QString payload = commentText;
    if (payload.startsWith(QStringLiteral("//"))) {
        payload = payload.mid(2);
    } else if (payload.startsWith(QStringLiteral("/*"))) {
        payload = payload.mid(2);
        if (payload.endsWith(QStringLiteral("*/")))
            payload.chop(2);
    }
    return payload.trimmed();
}

QString firstToken(const QString& payload, int* tokenEnd)
{
    int end = 0;
    while (end < payload.size() && !payload.at(end).isSpace())
        ++end;
    if (tokenEnd)
        *tokenEnd = end;
    return payload.left(end);
}

bool customFoldMarkerForNode(const QString& text,
                             TSNode node,
                             TSCustomFoldMarker* marker)
{
    if (!marker)
        return false;
    const char* type = ts_node_type(node);
    if (!type || (std::strcmp(type, "one_line_comment") != 0
                  && std::strcmp(type, "block_comment") != 0)) {
        return false;
    }

    const QString payload = commentPayload(nodeText(text, node));
    int tokenEnd = 0;
    const QString token = firstToken(payload, &tokenEnd);
    if (token != QStringLiteral("fold")
        && token != QStringLiteral("endfold")) {
        return false;
    }

    const TSPoint point = ts_node_start_point(node);
    marker->line = static_cast<int>(point.row);
    marker->column = static_cast<int>(point.column / 2u);
    marker->startsRange = token == QStringLiteral("fold");
    marker->label = marker->startsRange
        ? payload.mid(tokenEnd).trimmed()
        : QString();
    return true;
}

void collectCustomFoldMarkers(const QString& text,
                              TSNode node,
                              QList<TSCustomFoldMarker>& markers)
{
    if (ts_node_is_null(node))
        return;
    TSCustomFoldMarker marker;
    if (customFoldMarkerForNode(text, node, &marker))
        markers.append(marker);

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        collectCustomFoldMarkers(text,
                                 ts_node_child(node, index),
                                 markers);
    }
}

void collectFoldNodes(const QString& text,
                      TSNode node,
                      QList<TSFoldRange>& syntaxRanges,
                      QList<TSFoldRange>& customRanges,
                      QList<QPair<int, QString>>& customStack)
{
    const char* type = ts_node_type(node);
    TSCustomFoldMarker marker;
    if (customFoldMarkerForNode(text, node, &marker)) {
        if (marker.startsRange) {
            customStack.append({marker.line, marker.label});
        } else {
            if (!customStack.isEmpty()) {
                const QPair<int, QString> start = customStack.takeLast();
                const int endLine = marker.line;
                if (endLine > start.first) {
                    TSFoldRange range;
                    range.startLine = start.first;
                    range.endLine = endLine;
                    range.kind = TSFoldRangeKind::Custom;
                    range.label = start.second;
                    customRanges.append(range);
                }
            }
        }
    }

    if (isFoldableSyntaxNode(type)) {
        const int startLine = static_cast<int>(ts_node_start_point(node).row);
        const int endLine = static_cast<int>(ts_node_end_point(node).row);
        if (endLine > startLine) {
            TSFoldRange range;
            range.startLine = startLine;
            range.endLine = endLine;
            range.kind = TSFoldRangeKind::Syntax;
            range.label = foldSyntaxLabel(type);
            syntaxRanges.append(range);
        }
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i)
        collectFoldNodes(text,
                         ts_node_child(node, i),
                         syntaxRanges,
                         customRanges,
                         customStack);
}

void appendSyntaxFoldNode(TSNode node,
                          QList<TSFoldRange>& ranges,
                          QSet<QString>& seen)
{
    if (ts_node_is_null(node))
        return;
    const int nodeStart = static_cast<int>(ts_node_start_point(node).row);
    const int nodeEnd = static_cast<int>(ts_node_end_point(node).row);
    const char* type = ts_node_type(node);
    if (isFoldableSyntaxNode(type) && nodeEnd > nodeStart) {
        const QString key = QStringLiteral("%1:%2:%3")
            .arg(nodeStart)
            .arg(nodeEnd)
            .arg(QString::fromLatin1(type));
        if (seen.contains(key))
            return;
        seen.insert(key);
        TSFoldRange range;
        range.startLine = nodeStart;
        range.endLine = nodeEnd;
        range.kind = TSFoldRangeKind::Syntax;
        range.label = foldSyntaxLabel(type);
        ranges.append(range);
    }
}

void collectSyntaxFoldSubtree(TSNode node,
                              QList<TSFoldRange>& ranges,
                              QSet<QString>& seen)
{
    if (ts_node_is_null(node))
        return;
    appendSyntaxFoldNode(node, ranges, seen);

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t index = 0; index < childCount; ++index) {
        collectSyntaxFoldSubtree(ts_node_child(node, index), ranges, seen);
    }
}

void collectSyntaxFoldAncestors(TSNode node,
                                QList<TSFoldRange>& ranges,
                                QSet<QString>& seen)
{
    while (!ts_node_is_null(node)) {
        appendSyntaxFoldNode(node, ranges, seen);
        node = ts_node_parent(node);
    }
}
} // namespace

QString TSDocument::enclosingModuleName(int charOffset) const
{
    const uint32_t b = static_cast<uint32_t>(charOffset) * 2u;
    TSNode node = ts_node_named_descendant_for_byte_range(ts_tree_root_node(m_tree), b, b);
    while (!ts_node_is_null(node)) {
        const char* t = ts_node_type(node);
        if (t && (std::strcmp(t, "module_declaration") == 0 ||
                  std::strcmp(t, "interface_declaration") == 0 ||
                  std::strcmp(t, "program_declaration") == 0)) {
            return declarationName(m_text, node);
        }
        node = ts_node_parent(node);
    }
    return QString();
}

TSPortAppendTarget TSDocument::portAppendTarget(int charOffset) const
{
    TSPortAppendTarget target;

    const int boundedCharOffset = qBound(0, charOffset, m_text.size());
    const uint32_t byte =
        static_cast<uint32_t>(boundedCharOffset) * 2u;
    TSNode node =
        ts_node_named_descendant_for_byte_range(ts_tree_root_node(m_tree),
                                                byte,
                                                byte);
    TSNode module = ancestorOfType(node, "module_declaration");
    if (ts_node_is_null(module)) {
        target.status = TSPortAppendStatus::NoCurrentModule;
        return target;
    }

    TSNode header = directNamedChildOfType(module, "module_ansi_header");
    if (ts_node_is_null(header)) {
        TSNode nonAnsiHeader =
            directNamedChildOfType(module, "module_nonansi_header");
        TSNode emptyPortList =
            directNamedChildOfType(nonAnsiHeader, "list_of_ports");
        if (ts_node_is_null(nonAnsiHeader)
            || ts_node_is_null(emptyPortList)
            || ts_node_has_error(nonAnsiHeader)
            || ts_node_has_error(emptyPortList)
            || !emptyPortListHasNoNamedContent(emptyPortList)) {
            return target;
        }

        const PortParenRange parens =
            findPortListParens(m_text, emptyPortList);
        if (ts_node_is_null(parens.openParen)
            || ts_node_is_null(parens.closeParen)) {
            return target;
        }
        const int openLine =
            static_cast<int>(ts_node_start_point(parens.openParen).row);
        const int closeLine =
            static_cast<int>(ts_node_start_point(parens.closeParen).row);
        if (openLine >= closeLine)
            return target;

        const QString indent = lineIndentAt(m_text, closeLine)
            + QStringLiteral("    ");
        target.status = TSPortAppendStatus::Ok;
        target.insertChar = lineStartChar(m_text, closeLine);
        target.insertText = indent + QLatin1Char('\n');
        target.caretCharAfterEdit = target.insertChar + indent.size();
        return target;
    }

    TSNode portList =
        directNamedChildOfType(header, "list_of_port_declarations");
    if (ts_node_is_null(portList))
        return target;

    const PortParenRange parens = findPortListParens(m_text, portList);
    if (ts_node_is_null(parens.openParen)
        || ts_node_is_null(parens.closeParen)) {
        return target;
    }

    const int openLine =
        static_cast<int>(ts_node_start_point(parens.openParen).row);
    const int closeLine =
        static_cast<int>(ts_node_start_point(parens.closeParen).row);
    if (openLine >= closeLine)
        return target;

    if (!portListContainsOnlyClearChildren(portList)) {
        return target;
    }

    const QList<TSNode> ports = ansiPortDeclarations(portList);

    if (ports.isEmpty()) {
        if (ts_node_named_child_count(portList) > 0)
            return target;

        const QString indent = lineIndentAt(m_text, closeLine)
            + QStringLiteral("    ");
        target.status = TSPortAppendStatus::Ok;
        target.insertChar = lineStartChar(m_text, closeLine);
        target.insertText = indent + QLatin1Char('\n');
        target.caretCharAfterEdit = target.insertChar + indent.size();
        return target;
    }

    TSNode lastPort = ports.last();
    if (ts_node_has_error(lastPort))
        return target;

    const int lastPortLine = nodeLastLine(lastPort);
    if (lastPortLine <= openLine || lastPortLine >= closeLine)
        return target;

    bool hasComma = false;
    int commaInsertChar = -1;
    if (!trailingCommaStateOnPortLine(m_text,
                                      lastPort,
                                      lastPortLine,
                                      &hasComma,
                                      &commaInsertChar)) {
        return target;
    }

    const QString indent = lineIndentAt(m_text, lastPortLine);
    const int insertChar = lineEndChar(m_text, lastPortLine);
    target.status = TSPortAppendStatus::Ok;
    target.needsTrailingComma = !hasComma;
    target.trailingCommaInsertChar = commaInsertChar;
    target.insertChar = insertChar;
    target.insertText = QLatin1Char('\n') + indent;
    target.caretCharAfterEdit =
        insertChar + 1 + indent.size() + (target.needsTrailingComma ? 1 : 0);
    return target;
}

TSSignalInsertTarget TSDocument::signalInsertTarget(int charOffset) const
{
    TSSignalInsertTarget target;

    const int boundedCharOffset = qBound(0, charOffset, m_text.size());
    const uint32_t byte =
        static_cast<uint32_t>(boundedCharOffset) * 2u;
    TSNode node =
        ts_node_named_descendant_for_byte_range(ts_tree_root_node(m_tree),
                                                byte,
                                                byte);
    TSNode module = ancestorOfType(node, "module_declaration");
    if (ts_node_is_null(module)) {
        target.status = TSSignalInsertStatus::NoCurrentModule;
        return target;
    }
    if (ts_node_has_error(module))
        return target;

    TSNode header = directModuleHeader(module);
    if (ts_node_is_null(header))
        return target;

    TSNode lastSignalDecl{};
    TSNode lastParameterDecl{};
    TSNode firstBodyNode{};
    bool seenHeader = false;

    const uint32_t childCount = ts_node_named_child_count(module);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode rawChild = ts_node_named_child(module, i);
        TSNode child = effectiveModuleMemberNode(rawChild);
        if (ts_node_eq(child, header)) {
            seenHeader = true;
            continue;
        }
        if (!seenHeader)
            continue;

        if (isModuleBodyBoundaryNode(child)) {
            firstBodyNode = child;
            break;
        }
        if (isInternalSignalDeclaration(child)) {
            lastSignalDecl = child;
            continue;
        }
        if (isParameterLikeDeclaration(child)) {
            lastParameterDecl = child;
            continue;
        }
        if (nodeTypeIs(child, "attribute_instance")
            || nodeTypeIs(child, "package_import_declaration")
            || nodeTypeIs(child, "genvar_declaration")
            || nodeTypeIs(child, "include_compiler_directive")
            || nodeTypeIs(child, "line_compiler_directive")
            || nodeTypeIs(child, "file_or_line_compiler_directive")
            || nodeTypeIs(child, "default_nettype_compiler_directive")
            || nodeTypeIs(child, "pragma")) {
            continue;
        }
        if (nodeStartChar(child) >= nodeEndChar(header)) {
            firstBodyNode = child;
            break;
        }
    }

    SignalInsertAnchor anchor;
    if (!ts_node_is_null(lastSignalDecl)) {
        anchor = anchorAfterNode(m_text, lastSignalDecl);
    } else if (!ts_node_is_null(lastParameterDecl)) {
        anchor = anchorAfterNode(m_text, lastParameterDecl);
    } else {
        const int headerLastLine = nodeLastLine(header);
        int insertLine = -1;
        QString indent;
        if (!ts_node_is_null(firstBodyNode)) {
            insertLine =
                static_cast<int>(ts_node_start_point(firstBodyNode).row);
            indent = lineIndentAt(m_text, insertLine);
        } else {
            insertLine = endmoduleLine(module, m_text);
            if (insertLine < 0)
                return target;
            indent = lineIndentAt(m_text, insertLine)
                + QStringLiteral("    ");
        }
        if (insertLine <= headerLastLine)
            return target;
        anchor = anchorBeforeLine(m_text, insertLine, indent);
    }

    if (!anchor.valid || anchor.line < 0)
        return target;

    if (anchor.insertAfterLine) {
        const int insertChar = lineEndChar(m_text, anchor.line);
        target.status = TSSignalInsertStatus::Ok;
        target.insertChar = insertChar;
        target.insertText = QLatin1Char('\n') + anchor.indent;
        target.caretCharAfterEdit = insertChar + 1 + anchor.indent.size();
        return target;
    }

    const int insertChar = lineStartChar(m_text, anchor.line);
    target.status = TSSignalInsertStatus::Ok;
    target.insertChar = insertChar;
    target.insertText = anchor.indent + QLatin1Char('\n');
    target.caretCharAfterEdit = insertChar + anchor.indent.size();
    return target;
}

TSParameterInsertTarget TSDocument::parameterInsertTarget(int charOffset) const
{
    TSParameterInsertTarget target;

    const int boundedCharOffset = qBound(0, charOffset, m_text.size());
    const uint32_t byte =
        static_cast<uint32_t>(boundedCharOffset) * 2u;
    TSNode node =
        ts_node_named_descendant_for_byte_range(ts_tree_root_node(m_tree),
                                                byte,
                                                byte);
    TSNode module = ancestorOfType(node, "module_declaration");
    TSNode package = ancestorOfType(node, "package_declaration");
    if (ts_node_is_null(module) && ts_node_is_null(package)) {
        target.status = TSParameterInsertStatus::NoCurrentParameterScope;
        return target;
    }

    auto targetFromParameterPortList =
        [this](TSNode parameterPortList) -> TSParameterInsertTarget {
            TSParameterInsertTarget listTarget;
            if (ts_node_is_null(parameterPortList)
                || ts_node_has_error(parameterPortList)) {
                return listTarget;
            }

            const PortParenRange parens =
                findPortListParens(m_text, parameterPortList);
            if (ts_node_is_null(parens.openParen)
                || ts_node_is_null(parens.closeParen)) {
                return listTarget;
            }

            const int openLine =
                static_cast<int>(ts_node_start_point(parens.openParen).row);
            const int closeLine =
                static_cast<int>(ts_node_start_point(parens.closeParen).row);
            if (openLine >= closeLine)
                return listTarget;

            if (!parameterPortListContainsOnlyClearChildren(parameterPortList))
                return listTarget;

            const QList<TSNode> entries =
                parameterPortEntries(parameterPortList);
            if (entries.isEmpty()) {
                if (ts_node_named_child_count(parameterPortList) > 0)
                    return listTarget;

                const QString indent = lineIndentAt(m_text, closeLine)
                    + QStringLiteral("    ");
                listTarget.status = TSParameterInsertStatus::Ok;
                listTarget.insertChar = lineStartChar(m_text, closeLine);
                listTarget.insertText = indent + QLatin1Char('\n');
                listTarget.caretCharAfterEdit =
                    listTarget.insertChar + indent.size();
                return listTarget;
            }

            TSNode lastEntry = entries.last();
            if (ts_node_has_error(lastEntry))
                return listTarget;

            const int lastEntryLine = nodeLastLine(lastEntry);
            if (lastEntryLine <= openLine || lastEntryLine >= closeLine)
                return listTarget;

            bool hasComma = false;
            int commaInsertChar = -1;
            if (!trailingCommaStateOnPortLine(m_text,
                                              lastEntry,
                                              lastEntryLine,
                                              &hasComma,
                                              &commaInsertChar)) {
                return listTarget;
            }

            const QString indent = lineIndentAt(m_text, lastEntryLine);
            const int insertChar = lineEndChar(m_text, lastEntryLine);
            listTarget.status = TSParameterInsertStatus::Ok;
            listTarget.needsTrailingComma = !hasComma;
            listTarget.trailingCommaInsertChar = commaInsertChar;
            listTarget.insertChar = insertChar;
            listTarget.insertText = QLatin1Char('\n') + indent;
            listTarget.caretCharAfterEdit =
                insertChar + 1 + indent.size()
                + (listTarget.needsTrailingComma ? 1 : 0);
            return listTarget;
        };

    auto targetAfterAnchor =
        [this](const SignalInsertAnchor& anchor) -> TSParameterInsertTarget {
            TSParameterInsertTarget anchorTarget;
            if (!anchor.valid || anchor.line < 0)
                return anchorTarget;

            if (anchor.insertAfterLine) {
                const int insertChar = lineEndChar(m_text, anchor.line);
                anchorTarget.status = TSParameterInsertStatus::Ok;
                anchorTarget.insertChar = insertChar;
                anchorTarget.insertText = QLatin1Char('\n') + anchor.indent;
                anchorTarget.caretCharAfterEdit =
                    insertChar + 1 + anchor.indent.size();
                return anchorTarget;
            }

            const int insertChar = lineStartChar(m_text, anchor.line);
            anchorTarget.status = TSParameterInsertStatus::Ok;
            anchorTarget.insertChar = insertChar;
            anchorTarget.insertText = anchor.indent + QLatin1Char('\n');
            anchorTarget.caretCharAfterEdit =
                insertChar + anchor.indent.size();
            return anchorTarget;
        };

    if (!ts_node_is_null(module)) {
        if (ts_node_has_error(module))
            return target;

        TSNode header = directModuleHeader(module);
        if (ts_node_is_null(header))
            return target;

        TSNode parameterPortList =
            directNamedChildOfType(header, "parameter_port_list");
        if (!ts_node_is_null(parameterPortList)) {
            const TSParameterInsertTarget listTarget =
                targetFromParameterPortList(parameterPortList);
            if (listTarget.ok())
                return listTarget;
        }

        TSNode lastParameterDecl{};
        bool seenHeader = false;

        const uint32_t childCount = ts_node_named_child_count(module);
        for (uint32_t i = 0; i < childCount; ++i) {
            TSNode rawChild = ts_node_named_child(module, i);
            TSNode child = effectiveModuleMemberNode(rawChild);
            if (ts_node_eq(child, header)) {
                seenHeader = true;
                continue;
            }
            if (!seenHeader)
                continue;

            if (isModuleBodyBoundaryNode(child))
                break;
            if (isParameterLikeDeclaration(child)) {
                lastParameterDecl = child;
                continue;
            }
            if (nodeTypeIs(child, "attribute_instance")
                || nodeTypeIs(child, "package_import_declaration")
                || nodeTypeIs(child, "genvar_declaration")
                || nodeTypeIs(child, "include_compiler_directive")
                || nodeTypeIs(child, "line_compiler_directive")
                || nodeTypeIs(child, "file_or_line_compiler_directive")
                || nodeTypeIs(child, "default_nettype_compiler_directive")
                || nodeTypeIs(child, "pragma")) {
                continue;
            }
            if (nodeStartChar(child) >= nodeEndChar(header))
                break;
        }

        if (ts_node_is_null(lastParameterDecl))
            return target;

        return targetAfterAnchor(anchorAfterNode(m_text, lastParameterDecl));
    }

    if (ts_node_has_error(package))
        return target;

    TSNode lastPackageParameter{};
    const uint32_t childCount = ts_node_named_child_count(package);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode rawChild = ts_node_named_child(package, i);
        TSNode child = effectivePackageItemNode(rawChild);
        if (isParameterLikeDeclaration(child))
            lastPackageParameter = child;
    }

    if (ts_node_is_null(lastPackageParameter))
        return target;

    const int endLine = endpackageLine(package, m_text);
    if (endLine >= 0 && nodeLastLine(lastPackageParameter) >= endLine)
        return target;

    return targetAfterAnchor(anchorAfterNode(m_text, lastPackageParameter));
}

TSPackageToolInsertTarget TSDocument::packageToolInsertTarget(
    int charOffset,
    PackageToolKind kind) const
{
    TSPackageToolInsertTarget target;

    const int textSize = m_text.size();
    if (textSize <= 0) {
        target.status = TSPackageToolInsertStatus::NoCurrentPackage;
        return target;
    }

    const int boundedCharOffset = qBound(0, charOffset, textSize - 1);
    TSNode node = namedNodeAtChar(m_tree, boundedCharOffset, textSize);

    if (!ts_node_is_null(rtlContainerAncestor(node))) {
        target.status = TSPackageToolInsertStatus::InsideRtlScope;
        return target;
    }

    TSNode package = ancestorOfType(node, "package_declaration");
    if (ts_node_is_null(package)) {
        target.status = TSPackageToolInsertStatus::NoCurrentPackage;
        return target;
    }
    if (ts_node_has_error(package)) {
        target.status = TSPackageToolInsertStatus::PackageHasSyntaxError;
        return target;
    }

    const int packageStartLine =
        static_cast<int>(ts_node_start_point(package).row);
    const int endLine = endpackageLine(package, m_text);
    if (endLine < 0) {
        target.status = TSPackageToolInsertStatus::NoEndpackage;
        return target;
    }
    if (endLine <= packageStartLine)
        return target;

    const int endpackageStart = lineStartChar(m_text, endLine);
    TSNode lastSameKind{};
    const uint32_t childCount = ts_node_named_child_count(package);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode rawChild = ts_node_named_child(package, i);
        TSNode child = effectivePackageItemNode(rawChild);
        if (nodeStartChar(child) >= endpackageStart)
            break;
        if (ts_node_has_error(child))
            continue;
        if (isPackageToolSameKind(kind, child))
            lastSameKind = child;
    }

    target.status = TSPackageToolInsertStatus::Ok;
    target.packageName = declarationName(m_text, package);

    if (!ts_node_is_null(lastSameKind)) {
        const SignalInsertAnchor anchor = anchorAfterNode(m_text, lastSameKind);
        if (!anchor.valid || anchor.line < 0 || anchor.line >= endLine) {
            target.status =
                TSPackageToolInsertStatus::NoClearPackageInsertPoint;
            return target;
        }
        target.insertAfterLine = true;
        target.insertChar = lineEndChar(m_text, anchor.line);
        target.lineIndent = anchor.indent;
        return target;
    }

    const int previousLine = previousNonBlankLine(m_text, endLine - 1);
    target.insertAfterLine = false;
    target.insertChar = endpackageStart;
    target.lineIndent =
        previousLine > packageStartLine
            ? lineIndentAt(m_text, previousLine)
            : lineIndentAt(m_text, endLine) + QStringLiteral("    ");
    return target;
}

TSModuleEndNavigationTarget TSDocument::moduleEndNavigationTarget(
    int charOffset) const
{
    TSModuleEndNavigationTarget target;

    const int boundedCharOffset = qBound(0, charOffset, m_text.size());
    const uint32_t byte =
        static_cast<uint32_t>(boundedCharOffset) * 2u;
    TSNode node =
        ts_node_named_descendant_for_byte_range(ts_tree_root_node(m_tree),
                                                byte,
                                                byte);
    TSNode module = ancestorOfType(node, "module_declaration");
    if (ts_node_is_null(module)) {
        target.status = TSModuleEndNavigationStatus::NoCurrentModule;
        return target;
    }
    if (ts_node_has_error(module))
        return target;

    const int endLine = endmoduleLine(module, m_text);
    if (endLine < 0)
        return target;

    target.status = TSModuleEndNavigationStatus::Ok;
    target.caretChar =
        lineStartChar(m_text, endLine) + lineIndentAt(m_text, endLine).size();
    return target;
}
TSAlwaysScopeTarget TSDocument::alwaysScopeTarget(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar) const
{
    TSAlwaysScopeTarget target;

    const int textSize = m_text.size();
    if (textSize <= 0)
        return target;

    const bool hasSelection =
        selectionStartChar >= 0 && selectionEndChar > selectionStartChar;
    int lookupStart = qBound(0, cursorChar, textSize - 1);
    int lookupEnd = lookupStart;
    if (hasSelection) {
        const int rawStart = qBound(0,
                                    qMin(selectionStartChar, selectionEndChar),
                                    textSize);
        const int rawEnd = qBound(0,
                                  qMax(selectionStartChar, selectionEndChar),
                                  textSize);
        lookupStart = firstNonSpaceChar(m_text, rawStart, rawEnd);
        lookupEnd = lastNonSpaceChar(m_text, rawStart, rawEnd);
        if (lookupStart > lookupEnd) {
            lookupStart = qBound(0, cursorChar, textSize - 1);
            lookupEnd = lookupStart;
        }
    }

    TSNode startNode = namedNodeAtChar(m_tree, lookupStart, textSize);
    TSNode always = ancestorOfType(startNode, "always_construct");
    if (ts_node_is_null(always))
        return target;

    if (hasSelection) {
        TSNode endNode = namedNodeAtChar(m_tree, lookupEnd, textSize);
        TSNode endAlways = ancestorOfType(endNode, "always_construct");
        if (ts_node_is_null(endAlways) || !ts_node_eq(always, endAlways)) {
            target.status = TSAlwaysScopeStatus::AmbiguousSelection;
            return target;
        }
    }

    target.status = TSAlwaysScopeStatus::Ok;
    target.startChar = nodeStartChar(always);
    target.endChar = nodeEndChar(always);
    target.startLine = nodeStartLine(always);
    target.endLine = nodeEndLine(always);
    target.kindText =
        leadingIdentifierAt(m_text, target.startChar, target.endChar);
    if (target.kindText.isEmpty())
        target.kindText = QStringLiteral("always");
    target.label = QStringLiteral("%1 lines %2-%3")
                       .arg(target.kindText)
                       .arg(target.startLine + 1)
                       .arg(target.endLine + 1);
    return target;
}

TSModuleScopeTarget TSDocument::moduleScopeTarget(
    int cursorChar,
    int selectionStartChar,
    int selectionEndChar) const
{
    TSModuleScopeTarget target;

    const int textSize = m_text.size();
    if (textSize <= 0)
        return target;

    const bool hasSelection =
        selectionStartChar >= 0 && selectionEndChar > selectionStartChar;
    int lookupStart = qBound(0, cursorChar, textSize - 1);
    int lookupEnd = lookupStart;
    if (hasSelection) {
        const int rawStart = qBound(0,
                                    qMin(selectionStartChar, selectionEndChar),
                                    textSize);
        const int rawEnd = qBound(0,
                                  qMax(selectionStartChar, selectionEndChar),
                                  textSize);
        lookupStart = firstNonSpaceChar(m_text, rawStart, rawEnd);
        lookupEnd = lastNonSpaceChar(m_text, rawStart, rawEnd);
        if (lookupStart > lookupEnd) {
            lookupStart = qBound(0, cursorChar, textSize - 1);
            lookupEnd = lookupStart;
        }
    }

    TSNode startNode = namedNodeAtChar(m_tree, lookupStart, textSize);
    TSNode module = rtlContainerAncestor(startNode);
    if (ts_node_is_null(module))
        return target;

    if (hasSelection) {
        TSNode endNode = namedNodeAtChar(m_tree, lookupEnd, textSize);
        TSNode endModule = rtlContainerAncestor(endNode);
        if (ts_node_is_null(endModule) || !ts_node_eq(module, endModule)) {
            target.status = TSModuleScopeStatus::AmbiguousSelection;
            return target;
        }
    }

    target.status = TSModuleScopeStatus::Ok;
    target.startChar = nodeStartChar(module);
    target.endChar = nodeEndChar(module);
    target.startLine = nodeStartLine(module);
    target.endLine = nodeEndLine(module);
    target.kindText = rtlContainerKindText(module);
    target.moduleName = declarationName(m_text, module);
    const QString displayName =
        target.moduleName.isEmpty() ? QStringLiteral("<unnamed>")
                                    : target.moduleName;
    target.label = QStringLiteral("%1 %2 lines %3-%4")
                       .arg(target.kindText, displayName)
                       .arg(target.startLine + 1)
                       .arg(target.endLine + 1);
    return target;
}

TSBeginEndInsideTarget TSDocument::beginEndInsideTarget(int cursorChar) const
{
    TSBeginEndInsideTarget target;

    const int textSize = m_text.size();
    if (textSize <= 0)
        return target;

    TSNode node =
        namedNodeAtChar(m_tree,
                        qBound(0, cursorChar, textSize - 1),
                        textSize);
    TSNode block = ancestorOfType(node, "seq_block");
    if (ts_node_is_null(block) || ts_node_has_error(block))
        return target;

    const int beginLine = static_cast<int>(ts_node_start_point(block).row);
    const int endLine = seqBlockEndLine(block, m_text);
    const int firstInsideLine = beginLine + 1;
    const int lastInsideLine = endLine - 1;
    if (firstInsideLine > lastInsideLine) {
        target.status = TSBeginEndInsideStatus::EmptyBeginEndBlock;
        return target;
    }

    target.startChar = lineStartChar(m_text, firstInsideLine);
    target.endChar = lineEndChar(m_text, lastInsideLine);
    target.startLine = firstInsideLine;
    target.endLine = lastInsideLine;
    target.status = TSBeginEndInsideStatus::Ok;
    return target;
}

namespace {
// DFS the subtree of 'node'. When a node classifies to a highlight category, emit a span for the
// WHOLE node (clipped to the block) and stop descending - this treats "module_keyword", numbers,
// comments, strings and anonymous keyword/operator tokens as highlight units. Otherwise (structural
// / identifier nodes) recurse into children. Coordinates are converted to block-local chars.
void collectSpans(TSNode node, uint32_t startByte, uint32_t endByte,
                  int blockStartChar, QVector<HlSpan>& out)
{
    const uint32_t ns = ts_node_start_byte(node);
    const uint32_t ne = ts_node_end_byte(node);
    if (ne <= startByte || ns >= endByte)
        return;  // no overlap with the block

    const HlCategory cat = classifyTokenType(ts_node_type(node), ts_node_is_named(node));
    if (cat != HlCategory::None) {
        const uint32_t cs = ns > startByte ? ns : startByte;   // clip to block
        const uint32_t ce = ne < endByte ? ne : endByte;
        if (cs < ce) {
            HlSpan span;
            span.start = static_cast<int>(cs / 2) - blockStartChar;
            span.length = static_cast<int>((ce - cs) / 2);
            span.category = cat;
            if (span.length > 0)
                out.append(span);
        }
        return;  // highlight unit - do not descend
    }

    const uint32_t childCount = ts_node_child_count(node);
    for (uint32_t i = 0; i < childCount; ++i)
        collectSpans(ts_node_child(node, i), startByte, endByte, blockStartChar, out);
}
} // namespace

QVector<HlSpan> TSDocument::highlightSpans(int blockStartChar, int blockLenChar) const
{
    QVector<HlSpan> spans;
    if (blockLenChar <= 0)
        return spans;

    const uint32_t startByte = static_cast<uint32_t>(blockStartChar) * 2u;
    const uint32_t endByte = static_cast<uint32_t>(blockStartChar + blockLenChar) * 2u;

    TSNode root = ts_tree_root_node(m_tree);
    // Smallest node spanning the block, then DFS its leaves (keeps the walk local to the block).
    TSNode scope = ts_node_descendant_for_byte_range(root, startByte,
                                                     endByte > startByte ? endByte - 1 : startByte);
    if (ts_node_is_null(scope))
        scope = root;

    collectSpans(scope, startByte, endByte, blockStartChar, spans);
    return spans;
}

int TSDocument::blockEndCommentState(int blockStartChar, int blockLenChar) const
{
    const uint32_t endByte = static_cast<uint32_t>(blockStartChar + blockLenChar) * 2u;
    if (endByte == 0)
        return 0;
    TSNode node = ts_node_descendant_for_byte_range(ts_tree_root_node(m_tree),
                                                    endByte - 1, endByte - 1);
    while (!ts_node_is_null(node)) {
        const char* t = ts_node_type(node);
        if (t && std::strcmp(t, "block_comment") == 0)
            return ts_node_end_byte(node) > endByte ? 1 : 0;
        node = ts_node_parent(node);
    }
    return 0;
}

QList<TSFoldRange> TSDocument::foldingRanges() const
{
    QList<TSFoldRange> syntaxRanges;
    QList<TSFoldRange> customRanges;
    QList<QPair<int, QString>> customStack;
    collectFoldNodes(m_text,
                     ts_tree_root_node(m_tree),
                     syntaxRanges,
                     customRanges,
                     customStack);

    QSet<QString> customExtents;
    for (const TSFoldRange& range : std::as_const(customRanges)) {
        customExtents.insert(QStringLiteral("%1:%2")
                                 .arg(range.startLine)
                                 .arg(range.endLine));
    }

    QList<TSFoldRange> result = customRanges;
    for (const TSFoldRange& range : std::as_const(syntaxRanges)) {
        const QString extent = QStringLiteral("%1:%2")
            .arg(range.startLine)
            .arg(range.endLine);
        if (!customExtents.contains(extent))
            result.append(range);
    }
    std::sort(result.begin(), result.end(), [](const TSFoldRange& lhs,
                                               const TSFoldRange& rhs) {
        if (lhs.startLine != rhs.startLine)
            return lhs.startLine < rhs.startLine;
        if (lhs.kind != rhs.kind)
            return lhs.kind == TSFoldRangeKind::Custom;
        return lhs.endLine < rhs.endLine;
    });
    return result;
}

QList<TSFoldRange> TSDocument::syntaxFoldingRangesForChanges(
    const QList<TSChangedRange>& changedRanges) const
{
    QList<TSFoldRange> result;
    if (!m_tree)
        return result;
    QSet<QString> seen;
    const TSNode root = ts_tree_root_node(m_tree);
    const uint32_t documentBytes = static_cast<uint32_t>(m_text.size()) * 2u;
    for (const TSChangedRange& range : changedRanges) {
        uint32_t startByte = static_cast<uint32_t>(
            qBound(0, range.startChar, m_text.size())) * 2u;
        uint32_t endByte = static_cast<uint32_t>(
            qBound(0, range.endChar, m_text.size())) * 2u;
        if (documentBytes > 0) {
            startByte = qMin(startByte, documentBytes - 1u);
            endByte = qMin(qMax(startByte, endByte), documentBytes - 1u);
        }
        TSNode scope = ts_node_named_descendant_for_byte_range(
            root, startByte, endByte);
        if (ts_node_is_null(scope))
            scope = ts_node_descendant_for_byte_range(root,
                                                      startByte,
                                                      endByte);
        collectSyntaxFoldSubtree(scope, result, seen);
        collectSyntaxFoldAncestors(ts_node_parent(scope), result, seen);
    }
    std::sort(result.begin(), result.end(), [](const TSFoldRange& left,
                                               const TSFoldRange& right) {
        if (left.startLine != right.startLine)
            return left.startLine < right.startLine;
        return left.endLine < right.endLine;
    });
    return result;
}

QList<TSCustomFoldMarker> TSDocument::customFoldMarkers() const
{
    QList<TSCustomFoldMarker> result;
    if (!m_tree)
        return result;
    collectCustomFoldMarkers(m_text, ts_tree_root_node(m_tree), result);
    std::sort(result.begin(), result.end(),
              [](const TSCustomFoldMarker& left,
                 const TSCustomFoldMarker& right) {
                  if (left.line != right.line)
                      return left.line < right.line;
                  return left.column < right.column;
              });
    return result;
}

QList<TSCustomFoldMarker> TSDocument::customFoldMarkersForChanges(
    const QList<TSChangedRange>& changedRanges) const
{
    QList<TSCustomFoldMarker> result;
    if (!m_tree || changedRanges.isEmpty())
        return result;

    QSet<QString> seen;
    const TSNode root = ts_tree_root_node(m_tree);
    const uint32_t documentBytes = static_cast<uint32_t>(m_text.size()) * 2u;
    for (const TSChangedRange& range : changedRanges) {
        uint32_t startByte = static_cast<uint32_t>(
            qBound(0, range.startChar, m_text.size())) * 2u;
        uint32_t endByte = static_cast<uint32_t>(
            qBound(0, range.endChar, m_text.size())) * 2u;
        if (documentBytes > 0) {
            startByte = qMin(startByte, documentBytes - 1u);
            endByte = qMin(qMax(startByte, endByte), documentBytes - 1u);
        }
        TSNode scope = ts_node_named_descendant_for_byte_range(
            root, startByte, endByte);
        if (ts_node_is_null(scope)) {
            scope = ts_node_descendant_for_byte_range(root,
                                                      startByte,
                                                      endByte);
        }

        QList<TSCustomFoldMarker> local;
        collectCustomFoldMarkers(m_text, scope, local);
        for (const TSCustomFoldMarker& marker : std::as_const(local)) {
            const QString key = QStringLiteral("%1:%2:%3")
                .arg(marker.line)
                .arg(marker.column)
                .arg(marker.startsRange ? 1 : 0);
            if (!seen.contains(key)) {
                seen.insert(key);
                result.append(marker);
            }
        }
    }
    std::sort(result.begin(), result.end(),
              [](const TSCustomFoldMarker& left,
                 const TSCustomFoldMarker& right) {
                  if (left.line != right.line)
                      return left.line < right.line;
                  return left.column < right.column;
              });
    return result;
}
