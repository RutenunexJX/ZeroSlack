#ifndef TSDOCUMENT_H
#define TSDOCUMENT_H

#include <QString>
#include <QList>
#include <QVector>

#include "packagetoolservice.h"

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

enum class TSPortAppendStatus {
    Ok,
    NoCurrentModule,
    NoClearPortAppendPoint
};

struct TSPortAppendTarget {
    TSPortAppendStatus status = TSPortAppendStatus::NoClearPortAppendPoint;
    bool needsTrailingComma = false;
    int trailingCommaInsertChar = -1;
    int insertChar = -1;
    QString insertText;
    int caretCharAfterEdit = -1;

    bool ok() const { return status == TSPortAppendStatus::Ok; }
};

enum class TSSignalInsertStatus {
    Ok,
    NoCurrentModule,
    NoClearSignalInsertPoint
};

struct TSSignalInsertTarget {
    TSSignalInsertStatus status =
        TSSignalInsertStatus::NoClearSignalInsertPoint;
    int insertChar = -1;
    QString insertText;
    int caretCharAfterEdit = -1;

    bool ok() const { return status == TSSignalInsertStatus::Ok; }
};

enum class TSInstanceInsertStatus {
    Ok,
    NoCurrentModule,
    NoClearInstanceInsertPoint
};

struct TSInstanceInsertTarget {
    TSInstanceInsertStatus status =
        TSInstanceInsertStatus::NoClearInstanceInsertPoint;
    int insertChar = -1;
    QString insertText;
    int caretCharAfterEdit = -1;

    bool ok() const { return status == TSInstanceInsertStatus::Ok; }
};

enum class TSAssignInsertStatus {
    Ok,
    NoCurrentModule,
    NoClearAssignInsertPoint
};

struct TSAssignInsertTarget {
    TSAssignInsertStatus status =
        TSAssignInsertStatus::NoClearAssignInsertPoint;
    int insertChar = -1;
    QString insertText;
    int caretCharAfterEdit = -1;

    bool ok() const { return status == TSAssignInsertStatus::Ok; }
};

enum class TSParameterInsertStatus {
    Ok,
    NoCurrentParameterScope,
    NoClearParameterInsertPoint
};

struct TSParameterInsertTarget {
    TSParameterInsertStatus status =
        TSParameterInsertStatus::NoClearParameterInsertPoint;
    int insertChar = -1;
    QString insertText;
    int caretCharAfterEdit = -1;
    bool needsTrailingComma = false;
    int trailingCommaInsertChar = -1;

    bool ok() const { return status == TSParameterInsertStatus::Ok; }
};

enum class TSPackageToolInsertStatus {
    Ok,
    NoCurrentPackage,
    InsideRtlScope,
    PackageHasSyntaxError,
    NoEndpackage,
    NoClearPackageInsertPoint
};

struct TSPackageToolInsertTarget {
    TSPackageToolInsertStatus status =
        TSPackageToolInsertStatus::NoClearPackageInsertPoint;
    int insertChar = -1;
    QString lineIndent;
    QString packageName;
    bool insertAfterLine = false;

    bool ok() const { return status == TSPackageToolInsertStatus::Ok; }
};

enum class TSModuleEndInsertStatus {
    Ok,
    NoCurrentModule,
    NoClearModuleEndPoint
};

struct TSModuleEndInsertTarget {
    TSModuleEndInsertStatus status =
        TSModuleEndInsertStatus::NoClearModuleEndPoint;
    int replaceStartChar = -1;
    int replaceEndChar = -1;
    QString replacementText;
    int caretCharAfterEdit = -1;

    bool ok() const { return status == TSModuleEndInsertStatus::Ok; }
};

enum class TSAlwaysScopeStatus {
    Ok,
    NoCurrentAlways,
    AmbiguousSelection
};

struct TSAlwaysScopeTarget {
    TSAlwaysScopeStatus status = TSAlwaysScopeStatus::NoCurrentAlways;
    int startChar = -1;
    int endChar = -1;
    int startLine = 0;
    int endLine = 0;
    QString kindText;
    QString label;

    bool ok() const { return status == TSAlwaysScopeStatus::Ok; }
};

enum class TSModuleScopeStatus {
    Ok,
    NoCurrentModule,
    AmbiguousSelection
};

struct TSModuleScopeTarget {
    TSModuleScopeStatus status = TSModuleScopeStatus::NoCurrentModule;
    int startChar = -1;
    int endChar = -1;
    int startLine = 0;
    int endLine = 0;
    QString moduleName;
    QString kindText;
    QString label;

    bool ok() const { return status == TSModuleScopeStatus::Ok; }
};

enum class TSBeginEndInsideStatus {
    Ok,
    NoBeginEndBlock,
    EmptyBeginEndBlock
};

struct TSBeginEndInsideTarget {
    TSBeginEndInsideStatus status =
        TSBeginEndInsideStatus::NoBeginEndBlock;
    int startChar = -1;
    int endChar = -1;
    int startLine = -1; // 0-based QTextBlock line
    int endLine = -1;   // 0-based inclusive

    bool ok() const
    {
        return status == TSBeginEndInsideStatus::Ok
            && startChar >= 0
            && endChar >= startChar;
    }
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

    // True if the char offset is inside a Tree-sitter comment node.
    bool isCommentAt(int charOffset) const;

    // Name of the nearest enclosing module / interface / program at the given char offset, derived
    // live from the parse tree (instant, error-tolerant). Empty if the offset is not inside one.
    // Replaces the Slang+regex getCurrentModuleScope for cursor-scope decisions.
    QString enclosingModuleName(int charOffset) const;

    // Clear ANSI module port-list append point for editor-local COM commands.
    TSPortAppendTarget portAppendTarget(int charOffset) const;

    // Clear module-member insert point for adding an internal signal declaration.
    TSSignalInsertTarget signalInsertTarget(int charOffset) const;

    // Clear module-member insert point for adding a module/interface instance.
    TSInstanceInsertTarget instanceInsertTarget(int charOffset) const;

    // Clear module-member insert point for adding a continuous assign.
    TSAssignInsertTarget assignInsertTarget(int charOffset) const;

    // Clear module/package-scope insert point for adding a parameter/localparam.
    TSParameterInsertTarget parameterInsertTarget(int charOffset) const;

    // Clear package-scope insert point for package-only definition tools.
    TSPackageToolInsertTarget packageToolInsertTarget(
        int charOffset,
        PackageToolKind kind) const;

    // Clear insert point immediately before the current module's final endmodule.
    TSModuleEndInsertTarget moduleEndInsertTarget(int charOffset) const;

    // Current/selected always block range for scoped Wave Preview.
    TSAlwaysScopeTarget alwaysScopeTarget(int cursorChar,
                                          int selectionStartChar = -1,
                                          int selectionEndChar = -1) const;

    // Current/selected module/interface/program range for scoped Wave Preview.
    TSModuleScopeTarget moduleScopeTarget(int cursorChar,
                                          int selectionStartChar = -1,
                                          int selectionEndChar = -1) const;

    // Nearest begin/end block interior as complete lines for editor-local COM
    // selection commands.
    TSBeginEndInsideTarget beginEndInsideTarget(int cursorChar) const;

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
