#ifndef TSDOCUMENT_H
#define TSDOCUMENT_H

#include <QString>
#include <QList>
#include <QVector>

extern "C" {
#include <tree_sitter/api.h>
}

// Highlight categories produced from tree-sitter token types (see classifyTokenType).
enum class HlCategory {
    None,
    Keyword,
    Comment,
    String,
    Number,
    Operator,
    Identifier
};

// One highlight span, in block-LOCAL char coordinates (ready for QSyntaxHighlighter::setFormat).
struct HlSpan {
    int start;          // char offset within the queried block
    int length;         // char length
    HlCategory category;
};

enum class TSFoldRangeKind {
    Syntax,
    Custom
};

struct TSFoldRange {
    int startLine = -1; // 0-based QTextBlock line
    int endLine = -1;   // 0-based inclusive
    TSFoldRangeKind kind = TSFoldRangeKind::Syntax;
    QString label;
};

// Persistent, per-document Tree-sitter model: keeps a live parse tree plus the document text and
// supports incremental re-parse on edits. Foundation of the real-time syntactic layer
// (highlighting, live outline / scope) in the Slang + Tree-sitter architecture.
//
// Text is parsed as UTF-16 (Qt's native QString encoding), so tree-sitter byte offsets map to
// QString char indices as byte/2 - correct even with non-ASCII (e.g. Chinese comments), with no
// UTF-8<->UTF-16 offset bookkeeping. Assumes little-endian (Windows/x86); fine for this target.
//
// Threading: not thread-safe; lives with its editor on the UI thread. Each instance owns its own
// TSParser, so multiple documents don't contend.
class TSDocument
{
public:
    TSDocument();
    ~TSDocument();

    TSDocument(const TSDocument&) = delete;
    TSDocument& operator=(const TSDocument&) = delete;

    // Full (re)parse of the entire text from scratch.
    void setText(const QString& text);

    // Incremental edit. Byte/point fields are tree-sitter native (UTF-16 bytes; point columns in
    // bytes, i.e. 2*charColumn). Caller supplies the span being replaced (in the CURRENT buffer
    // before the edit) and the full new text. Uses ts_tree_edit + incremental parse.
    void applyEdit(uint32_t startByte, uint32_t oldEndByte, uint32_t newEndByte,
                   TSPoint startPoint, TSPoint oldEndPoint, TSPoint newEndPoint,
                   const QString& newFullText);

    // Convenience for editor integration: edit described in CHAR positions (QTextDocument coords).
    // [startChar, oldEndChar) in the CURRENT text is replaced; newFullText is the whole new text.
    // Byte offsets and TSPoints (row/col) are derived internally (current text held by this object
    // provides the pre-edit coordinates), so callers don't track the old text themselves.
    void applyEditChars(int startChar, int oldEndChar, int newEndChar, const QString& newFullText);

    TSNode rootNode() const;                 // always valid (empty doc parses to an empty tree)
    bool hasError() const;                   // tree contains ERROR / MISSING nodes (half-typed code)
    const QString& text() const { return m_text; }

    // Type name of the smallest named node at the given char offset (debug / scope helpers).
    const char* namedNodeTypeAt(int charOffset) const;

    // True if the char offset is inside a Tree-sitter comment node.
    bool isCommentAt(int charOffset) const;

    // Name of the nearest enclosing module / interface / program at the given char offset, derived
    // live from the parse tree (instant, error-tolerant). Empty if the offset is not inside one.
    // Replaces the Slang+regex getCurrentModuleScope for cursor-scope decisions.
    QString enclosingModuleName(int charOffset) const;

    // Highlight spans (block-local char coords) for the char range [blockStartChar, +blockLenChar).
    // Walks the live tree; clips tokens to the block. Multi-line tokens (block comments, strings)
    // are clipped per block.
    QVector<HlSpan> highlightSpans(int blockStartChar, int blockLenChar) const;

    // QSyntaxHighlighter block state: 1 if the block's end sits inside a block_comment that
    // continues onto the next block (so the following block must be re-highlighted), else 0.
    int blockEndCommentState(int blockStartChar, int blockLenChar) const;

    // Tree-sitter based folding ranges. Custom fold markers are extracted from
    // Tree-sitter comment nodes rather than regular expressions.
    QList<TSFoldRange> foldingRanges() const;

private:
    void reparse(TSTree* oldTree);

    TSParser* m_parser = nullptr;  // owned
    TSTree*   m_tree   = nullptr;  // owned
    QString   m_text;              // current document text (UTF-16)
};

// Map a tree-sitter token type to a highlight category. isNamed distinguishes grammar tokens
// (e.g. "module_keyword", "comment") from anonymous punctuation/operator tokens (";", "(", "=").
HlCategory classifyTokenType(const char* type, bool isNamed);

#endif // TSDOCUMENT_H
