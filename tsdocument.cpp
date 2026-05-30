#include "tsdocument.h"

extern "C" TSLanguage *tree_sitter_systemverilog();

TSDocument::TSDocument()
{
    m_parser = ts_parser_new();
    ts_parser_set_language(m_parser, tree_sitter_systemverilog());
    // Parse an empty document so rootNode() is always valid (no null-tree special cases).
    reparse(nullptr);
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
    TSTree* newTree = ts_parser_parse_string(
        m_parser, oldTree,
        m_utf8.constData(), static_cast<uint32_t>(m_utf8.size()));
    if (m_tree)
        ts_tree_delete(m_tree);
    m_tree = newTree;
}

void TSDocument::setText(const QString& text)
{
    m_utf8 = text.toUtf8();
    // Full parse from scratch (no old tree reuse).
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
        ts_tree_edit(m_tree, &edit);          // mark the changed region in the old tree

    m_utf8 = newFullText.toUtf8();
    reparse(m_tree);                          // incremental: reuse the edited old tree
}

TSNode TSDocument::rootNode() const
{
    return ts_tree_root_node(m_tree);
}

bool TSDocument::hasError() const
{
    return ts_node_has_error(ts_tree_root_node(m_tree));
}

const char* TSDocument::namedNodeTypeAt(uint32_t byteOffset) const
{
    TSNode root = ts_tree_root_node(m_tree);
    TSNode node = ts_node_named_descendant_for_byte_range(root, byteOffset, byteOffset);
    if (ts_node_is_null(node))
        return "";
    const char* t = ts_node_type(node);
    return t ? t : "";
}
