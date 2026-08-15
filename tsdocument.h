#ifndef TSDOCUMENT_H
#define TSDOCUMENT_H

#include <QString>
#include <QList>
#include <QVector>

#include <array>
#include <cstdint>

#include "documentchange.h"
#include "packagetoolservice.h"

extern "C" {
#include <tree_sitter/api.h>
}

struct TSTextStorageMetrics {
    std::uint64_t editCount = 0;
    std::uint64_t materializationCount = 0;
    std::uint64_t inputReadCount = 0;
    std::uint64_t movedCharacterCount = 0;
    int firstEditPosition = -1;
    int firstEditGapStart = -1;
    std::uint64_t treeEditNanoseconds = 0;
    std::uint64_t storageEditNanoseconds = 0;
    std::uint64_t parseNanoseconds = 0;
    std::uint64_t changedRangeNanoseconds = 0;
    std::uint64_t treeDeleteNanoseconds = 0;
    std::uint64_t syntaxParseCount = 0;
    std::uint64_t structurePreservingEditCount = 0;
    std::uint64_t deferredSyntaxEditCount = 0;
    std::uint64_t deferredSyntaxFlushCount = 0;
    std::uint64_t highlightBlockCount = 0;
    std::uint64_t highlightNanoseconds = 0;
};

// UTF-16 piece-table storage used directly by Tree-sitter's TSInput callback.
// Edits rebuild only piece metadata and append inserted text, so an edit never
// moves the unchanged body of a large document. A contiguous QString is
// materialized only for explicit whole-text callers.
class TSUTF16Text
{
public:
    TSUTF16Text();

    void setText(const QString& text);
    void replace(int position,
                 int removedLength,
                 const QString& insertedText);

    int size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }
    QChar at(int position) const;
    QString mid(int position, int length = -1) const;
    QString left(int length) const;
    int indexOf(const QString& value, int from = 0) const;
    int lastIndexOf(QChar value, int from = -1) const;

    const QString& materialized() const;
    operator QString() const { return materialized(); }

    const char* read(uint32_t byteOffset,
                     uint32_t* bytesRead) const;

    TSTextStorageMetrics metricsForTest() const
    {
        return m_metrics;
    }
    void resetMetricsForTest() const
    {
        m_metrics = {};
    }

    friend bool operator==(const TSUTF16Text& left,
                           const QString& right)
    {
        return left.materialized() == right;
    }
    friend bool operator==(const QString& left,
                           const TSUTF16Text& right)
    {
        return left == right.materialized();
    }
    friend bool operator!=(const TSUTF16Text& left,
                           const QString& right)
    {
        return !(left == right);
    }
    friend bool operator!=(const QString& left,
                           const TSUTF16Text& right)
    {
        return !(left == right);
    }

private:
    friend class TSDocument;

    enum class BufferKind {
        Original,
        Additions
    };

    struct Piece {
        BufferKind buffer = BufferKind::Original;
        int offset = 0;
        int length = 0;
    };

    const QString& bufferFor(const Piece& piece) const;
    int pieceIndexAt(int logicalIndex) const;
    static void appendPiece(QVector<Piece>* pieces,
                            BufferKind buffer,
                            int offset,
                            int length);
    void appendLogicalRange(QVector<Piece>* pieces,
                            int position,
                            int length) const;
    void rebuildPieceStarts();
    void copyRange(int position,
                   int length,
                   QChar* destination) const;

    QString m_original;
    QString m_additions;
    QVector<Piece> m_pieces;
    QVector<int> m_pieceStarts;
    int m_size = 0;
    mutable QString m_materialized;
    mutable bool m_materializedValid = true;
    mutable std::array<char16_t, 2> m_boundaryRead{};
    mutable TSTextStorageMetrics m_metrics;
};

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

struct TSCustomFoldMarker {
    int line = -1;
    int column = 0;
    bool startsRange = false;
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

enum class TSModuleEndNavigationStatus {
    Ok,
    NoCurrentModule,
    NoEndmodule
};

struct TSChangedRange {
    int startChar = 0;
    int endChar = 0;
    int startLine = 0;
    int endLine = 0;
};

struct TSNumericLiteralTarget {
    int startChar = -1;
    int endChar = -1;
    QString text;
    QString evaluationText;
    bool stringLiteral = false;

    bool ok() const
    {
        return startChar >= 0
            && endChar > startChar
            && !text.isEmpty();
    }
};

struct TSIdentifierTarget {
    int startChar = -1;
    int endChar = -1;
    QString text;

    bool ok() const
    {
        return startChar >= 0
            && endChar > startChar
            && !text.isEmpty();
    }
};

struct TSIdentifierOccurrenceSet {
    TSIdentifierTarget selected;
    int scopeStartChar = -1;
    int scopeEndChar = -1;
    QList<TSIdentifierTarget> occurrences;

    bool ok() const
    {
        return selected.ok()
            && scopeStartChar >= 0
            && scopeEndChar > scopeStartChar
            && !occurrences.isEmpty();
    }
};

enum class TSAssignmentNavigationStatus {
    Ok,
    NoIdentifier,
    NoAssignment
};

struct TSAssignmentNavigationTarget {
    TSAssignmentNavigationStatus status =
        TSAssignmentNavigationStatus::NoIdentifier;
    QString identifier;
    int sourceChar = -1;
    int targetChar = -1;
    int assignmentCount = 0;
    bool wrapped = false;

    bool ok() const
    {
        return status == TSAssignmentNavigationStatus::Ok
            && !identifier.isEmpty()
            && sourceChar >= 0
            && targetChar >= 0
            && assignmentCount > 0;
    }
};

enum class TSConditionalBranchNavigationStatus {
    Ok,
    NoConditionalGroup,
    IncompleteConditionalGroup
};

struct TSConditionalBranchNavigationTarget {
    TSConditionalBranchNavigationStatus status =
        TSConditionalBranchNavigationStatus::NoConditionalGroup;
    int sourceChar = -1;
    int targetChar = -1;
    int targetIndex = -1;
    QString targetDirective;
    QList<int> branchStartChars;
    QStringList branchDirectives;
    bool wrapped = false;

    bool ok() const
    {
        return status
                == TSConditionalBranchNavigationStatus::Ok
            && sourceChar >= 0
            && targetChar >= 0
            && targetIndex >= 0
            && targetIndex < branchStartChars.size()
            && branchStartChars.size()
                == branchDirectives.size();
    }
};

struct TSExpressionSlot {
    QString name;
    int startChar = -1;
    int endChar = -1;

    bool ok() const
    {
        return startChar >= 0 && endChar >= startChar;
    }
};

struct TSInstantiationTarget {
    int startChar = -1;
    int endChar = -1;
    QString moduleType;
    QString instanceName;
    QList<TSExpressionSlot> parameterActuals;
    QList<TSExpressionSlot> portActuals;

    bool ok() const
    {
        return startChar >= 0
            && endChar > startChar
            && !moduleType.isEmpty()
            && !instanceName.isEmpty();
    }
};

enum class TSNamedPortConnectionStatus {
    Ok,
    AlreadyConnected,
    NoInstantiation,
    PositionalConnections,
    NoClearConnectionPoint
};

// Exact insertion/reuse result for one named port connection. All offsets are
// UTF-16 QString coordinates obtained from the live Tree-sitter document.
struct TSNamedPortConnectionTarget {
    TSNamedPortConnectionStatus status =
        TSNamedPortConnectionStatus::NoClearConnectionPoint;
    QString moduleType;
    QString instanceName;
    QString existingActual;
    bool needsTrailingComma = false;
    int trailingCommaInsertChar = -1;
    int insertChar = -1;
    QString prefix;
    QString suffix;

    bool canInsert() const
    {
        return status == TSNamedPortConnectionStatus::Ok
            && insertChar >= 0;
    }

    bool canReuse() const
    {
        return status
            == TSNamedPortConnectionStatus::AlreadyConnected;
    }
};

enum class TSUndefinedSignalContextKind {
    None,
    NamedPortActual,
    ProceduralAssignmentLhs
};

struct TSUndefinedSignalContext {
    TSUndefinedSignalContextKind kind =
        TSUndefinedSignalContextKind::None;
    TSIdentifierTarget identifier;
    QString formalName;
    int formalStartChar = -1;
    TSInstantiationTarget instantiation;

    bool ok() const
    {
        return kind != TSUndefinedSignalContextKind::None
            && identifier.ok();
    }
};

struct TSModuleEndNavigationTarget {
    TSModuleEndNavigationStatus status =
        TSModuleEndNavigationStatus::NoEndmodule;
    int caretChar = -1;

    bool ok() const
    {
        return status == TSModuleEndNavigationStatus::Ok
            && caretChar >= 0;
    }
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
    int startLine = 0; // 0-based source line
    int endLine = 0;   // 0-based source line
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
    int startLine = 0; // 0-based source line
    int endLine = 0;   // 0-based source line
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

struct TSStructuralNewlineTarget {
    QString insertionText;
    int caretOffset = -1;
    bool insertedClosingKeyword = false;

    bool ok() const
    {
        return !insertionText.isEmpty()
            && caretOffset >= 0
            && caretOffset <= insertionText.size();
    }
};

struct TSKeywordCompletionTarget {
    int startChar = -1;
    int endChar = -1;
    QString prefix;
    QString keyword;
    QString suffix;

    bool ok() const
    {
        return startChar >= 0
            && endChar > startChar
            && endChar == startChar + prefix.size()
            && keyword == prefix + suffix
            && !suffix.isEmpty();
    }
};

struct TSKeywordPairTarget {
    int openingStartChar = -1;
    int openingEndChar = -1;
    int closingStartChar = -1;
    int closingEndChar = -1;
    QString openingKeyword;
    QString closingKeyword;

    bool ok() const
    {
        return openingStartChar >= 0
            && openingEndChar > openingStartChar
            && closingStartChar >= openingEndChar
            && closingEndChar > closingStartChar
            && !openingKeyword.isEmpty()
            && !closingKeyword.isEmpty();
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

    // Applies one UTF-16 delta to the owned text and updates the live tree.
    // A caller may defer reparsing for a transient non-language overlay; the
    // same edited tree remains the sole positional source until it is flushed.
    QList<TSChangedRange> applyEdit(
        const DocumentChange& change,
        bool deferSyntaxReparse = false);
    void flushPendingEdits();
    bool hasDeferredSyntaxEdits() const
    {
        return m_hasDeferredSyntaxEdits;
    }

    TSNode rootNode() const;                 // always valid (empty doc parses to an empty tree)
    bool hasError() const;                   // tree contains ERROR / MISSING nodes (half-typed code)
    const TSUTF16Text& text() const { return m_text; }
    TSTextStorageMetrics textStorageMetricsForTest() const
    {
        return m_text.metricsForTest();
    }
    void resetTextStorageMetricsForTest() const
    {
        m_text.resetMetricsForTest();
    }
    void recordHighlightBlockForTest(
        std::uint64_t elapsedNanoseconds) const
    {
        ++m_text.m_metrics.highlightBlockCount;
        m_text.m_metrics.highlightNanoseconds += elapsedNanoseconds;
    }

    // True if the char offset is inside a Tree-sitter comment node.
    bool isCommentAt(int charOffset) const;
    // True if the char offset is inside a Tree-sitter string node.
    bool isStringAt(int charOffset) const;

    // Exact numeric token or single-character string candidate under the
    // cursor. Comments and include-path strings are excluded structurally;
    // string candidates are validated by the shared literal evaluator.
    TSNumericLiteralTarget numericLiteralAt(int charOffset) const;

    // Exact SystemVerilog identifier under the cursor. Comment and string
    // nodes are never returned.
    TSIdentifierTarget identifierAt(int charOffset) const;
    TSIdentifierOccurrenceSet identifierOccurrencesAt(
        int charOffset) const;
    TSAssignmentNavigationTarget assignmentNavigationTarget(
        int charOffset,
        bool previous) const;
    TSConditionalBranchNavigationTarget
    conditionalBranchNavigationTarget(
        int charOffset,
        bool previous) const;

    // Complete module instantiation at the cursor and its editable parameter /
    // port actual expression spans. Incomplete or ambiguous instantiations are
    // rejected conservatively.
    TSInstantiationTarget instantiationAt(int charOffset) const;

    // Exact append point for a named port connection in the selected
    // hierarchical instance. Positional/mixed/error-containing connections
    // are rejected; an existing formal is returned for semantic compatibility
    // checking by the Action planner.
    TSNamedPortConnectionTarget namedPortConnectionTarget(
        int charOffset,
        const QString& formalName) const;

    // Syntactic contexts in which an undeclared identifier can safely be
    // offered as a local signal: a named instance-port actual or a procedural
    // assignment left-hand side.
    TSUndefinedSignalContext undefinedSignalContextAt(
        int charOffset) const;

    // Name of the nearest enclosing module / interface / program at the given char offset, derived
    // live from the parse tree (instant, error-tolerant). Empty if the offset is not inside one.
    // Replaces the Slang+regex getCurrentModuleScope for cursor-scope decisions.
    QString enclosingModuleName(int charOffset) const;

    // Name of the nearest enclosing package at the given char offset, derived
    // from the current Tree-sitter buffer snapshot.
    QString enclosingPackageName(int charOffset) const;

    // Clear ANSI module port-list append point.
    TSPortAppendTarget portAppendTarget(int charOffset) const;

    // Clear module-member insert point for adding an internal signal declaration.
    TSSignalInsertTarget signalInsertTarget(int charOffset) const;

    // Clear declaration point in the nearest sequential block. Only a
    // declaration section before the first statement is accepted.
    TSSignalInsertTarget blockSignalInsertTarget(
        int charOffset) const;

    // Parse an edited Declare Signal candidate as exactly one Tree-sitter
    // declaration for identifier. This is intentionally structural: callers
    // do not validate declarations with regular expressions or token scans.
    static bool isSingleSignalDeclaration(
        const QString& declaration,
        const QString& identifier,
        bool blockLocal);

    // Clear module-item insert point immediately after the selected internal
    // signal declaration. This keeps a generated bridge after the declaration
    // that Slang resolved, including multi-declarator declarations.
    TSSignalInsertTarget sourceBridgeInsertTarget(int charOffset) const;

    // Clear module/package-scope insert point for adding a parameter/localparam.
    TSParameterInsertTarget parameterInsertTarget(int charOffset) const;

    // Clear package-scope insert point for package-only definition tools.
    TSPackageToolInsertTarget packageToolInsertTarget(
        int charOffset,
        PackageToolKind kind) const;

    // Start of the current module's final endmodule.
    TSModuleEndNavigationTarget moduleEndNavigationTarget(
        int charOffset) const;

    // Current/selected always block range for scoped Wave Preview.
    TSAlwaysScopeTarget alwaysScopeTarget(int cursorChar,
                                          int selectionStartChar = -1,
                                          int selectionEndChar = -1) const;

    // Current/selected module/interface/program range for scoped Wave Preview.
    TSModuleScopeTarget moduleScopeTarget(int cursorChar,
                                          int selectionStartChar = -1,
                                          int selectionEndChar = -1) const;

    // Nearest begin/end block interior as complete lines for structural
    // selection commands.
    TSBeginEndInsideTarget beginEndInsideTarget(int cursorChar) const;

    // Synchronous structural input plans derived from the current unsaved
    // Tree-sitter snapshot. These APIs never consult Slang or a timer.
    TSStructuralNewlineTarget structuralNewlineTarget(
        int cursorChar,
        int indentWidth = 4) const;
    TSKeywordCompletionTarget uniqueKeywordCompletionAt(
        int cursorChar,
        int minimumPrefixLength = 3) const;
    TSKeywordPairTarget matchingKeywordPairAt(int cursorChar) const;

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
    QList<TSFoldRange> syntaxFoldingRangesForChanges(
        const QList<TSChangedRange>& changedRanges) const;
    QList<TSCustomFoldMarker> customFoldMarkers() const;
    QList<TSCustomFoldMarker> customFoldMarkersForChanges(
        const QList<TSChangedRange>& changedRanges) const;

private:
    void reparse(TSTree* oldTree);

    TSParser* m_parser = nullptr;  // owned
    TSTree*   m_tree   = nullptr;  // owned
    TSUTF16Text m_text;            // current document text (UTF-16)
    bool m_hasPendingEdits = false;
    bool m_hasDeferredSyntaxEdits = false;
};

// Map a tree-sitter token type to a highlight category. isNamed distinguishes grammar tokens
// (e.g. "module_keyword", "comment") from anonymous punctuation/operator tokens (";", "(", "=").
HlCategory classifyTokenType(const char* type, bool isNamed);

#endif // TSDOCUMENT_H
