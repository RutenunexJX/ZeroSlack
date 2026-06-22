#include "tsdocument.h"
#include <cstring>
#include <algorithm>
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

namespace {
// Tree-sitter point (row, column-in-bytes) at a char index. Columns are UTF-16 bytes (= 2*char).
TSPoint pointAtChar(const QString& text, int charIndex)
{
    const int n = charIndex < text.size() ? charIndex : static_cast<int>(text.size());
    uint32_t row = 0;
    int lineStart = 0;
    for (int i = 0; i < n; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++row;
            lineStart = i + 1;
        }
    }
    TSPoint p;
    p.row = row;
    p.column = static_cast<uint32_t>(charIndex - lineStart) * 2u;
    return p;
}
} // namespace

void TSDocument::applyEditChars(int startChar, int oldEndChar, int newEndChar,
                                const QString& newFullText)
{
    // m_text is still the pre-edit text here -> use it for the start / old-end points.
    const TSPoint sp  = pointAtChar(m_text, startChar);
    const TSPoint oep = pointAtChar(m_text, oldEndChar);
    const TSPoint nep = pointAtChar(newFullText, newEndChar);
    applyEdit(static_cast<uint32_t>(startChar) * 2u,
              static_cast<uint32_t>(oldEndChar) * 2u,
              static_cast<uint32_t>(newEndChar) * 2u,
              sp, oep, nep, newFullText);
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
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    if (end <= start)
        return QString();
    return text.mid(static_cast<int>(start / 2),
                    static_cast<int>((end - start) / 2));
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

void collectFoldNodes(const QString& text,
                      TSNode node,
                      QList<TSFoldRange>& syntaxRanges,
                      QList<TSFoldRange>& customRanges,
                      QList<QPair<int, QString>>& customStack)
{
    const char* type = ts_node_type(node);
    if (type && (std::strcmp(type, "one_line_comment") == 0
                 || std::strcmp(type, "block_comment") == 0)) {
        const QString payload = commentPayload(nodeText(text, node));
        int tokenEnd = 0;
        const QString token = firstToken(payload, &tokenEnd);
        if (token == QStringLiteral("fold")) {
            const QString alias = payload.mid(tokenEnd).trimmed();
            customStack.append({static_cast<int>(ts_node_start_point(node).row), alias});
        } else if (token == QStringLiteral("endfold")) {
            if (!customStack.isEmpty()) {
                const QPair<int, QString> start = customStack.takeLast();
                const int endLine = static_cast<int>(ts_node_start_point(node).row);
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
