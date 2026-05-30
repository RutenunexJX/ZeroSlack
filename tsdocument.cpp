#include "tsdocument.h"
#include <cstring>

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
    m_text = text;
    reparse(nullptr);
}

void TSDocument::applyEdit(uint32_t startByte, uint32_t oldEndByte, uint32_t newEndByte,
                           TSPoint startPoint, TSPoint oldEndPoint, TSPoint newEndPoint,
                           const QString& newFullText)
{
    TSInputEdit edit;
    edit.start_byte    = startByte;
    edit.old_end_byte  = oldEndByte;
    edit.new_end_byte  = newEndByte;
    edit.start_point   = startPoint;
    edit.old_end_point = oldEndPoint;
    edit.new_end_point = newEndPoint;

    if (m_tree)
        ts_tree_edit(m_tree, &edit);

    m_text = newFullText;
    reparse(m_tree);
}

TSNode TSDocument::rootNode() const
{
    return ts_tree_root_node(m_tree);
}

bool TSDocument::hasError() const
{
    return ts_node_has_error(ts_tree_root_node(m_tree));
}

const char* TSDocument::namedNodeTypeAt(int charOffset) const
{
    const uint32_t b = static_cast<uint32_t>(charOffset) * 2u;
    TSNode node = ts_node_named_descendant_for_byte_range(ts_tree_root_node(m_tree), b, b);
    if (ts_node_is_null(node))
        return "";
    const char* t = ts_node_type(node);
    return t ? t : "";
}

namespace {
// DFS the subtree of 'node'. When a node classifies to a highlight category, emit a span for the
// WHOLE node (clipped to the block) and stop descending — this treats "module_keyword", numbers,
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
        return;  // highlight unit — don't descend
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
