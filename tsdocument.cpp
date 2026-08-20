#include "tsdocument.h"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <limits>
#include <QElapsedTimer>
#include <QSet>

#include <slang/parsing/LexerFacts.h>

extern "C" TSLanguage *tree_sitter_systemverilog();

namespace {
constexpr int kSmallAdditionReserveCharacters = 4 * 1024;
constexpr int kLargeAdditionReserveCharacters = 64 * 1024;
constexpr int kLargeTextCharacters = 2 * 1024 * 1024;
}

TSUTF16Text::TSUTF16Text()
{
    m_additions.reserve(kSmallAdditionReserveCharacters);
}

const QString& TSUTF16Text::bufferFor(const Piece& piece) const
{
    return piece.buffer == BufferKind::Original
        ? m_original
        : m_additions;
}

int TSUTF16Text::pieceIndexAt(int logicalIndex) const
{
    Q_ASSERT(logicalIndex >= 0 && logicalIndex < m_size);
    const auto upper =
        std::upper_bound(m_pieceStarts.cbegin(),
                         m_pieceStarts.cend(),
                         logicalIndex);
    const int index =
        static_cast<int>(upper - m_pieceStarts.cbegin()) - 1;
    Q_ASSERT(index >= 0 && index < m_pieces.size());
    return index;
}

void TSUTF16Text::appendPiece(QVector<Piece>* pieces,
                              BufferKind buffer,
                              int offset,
                              int length)
{
    if (!pieces || length <= 0)
        return;

    if (!pieces->isEmpty()) {
        Piece& previous = pieces->last();
        if (previous.buffer == buffer
            && previous.offset + previous.length == offset) {
            previous.length += length;
            return;
        }
    }
    pieces->append(Piece{buffer, offset, length});
}

void TSUTF16Text::appendLogicalRange(QVector<Piece>* pieces,
                                     int position,
                                     int length) const
{
    if (!pieces || length <= 0)
        return;

    const int endPosition = position + length;
    int pieceIndex = pieceIndexAt(position);
    int logicalPosition = position;
    while (logicalPosition < endPosition
           && pieceIndex < m_pieces.size()) {
        const Piece& piece = m_pieces.at(pieceIndex);
        const int pieceStart = m_pieceStarts.at(pieceIndex);
        const int offsetInPiece = logicalPosition - pieceStart;
        const int copiedLength =
            qMin(endPosition - logicalPosition,
                 piece.length - offsetInPiece);
        appendPiece(pieces,
                    piece.buffer,
                    piece.offset + offsetInPiece,
                    copiedLength);
        logicalPosition += copiedLength;
        ++pieceIndex;
    }
    Q_ASSERT(logicalPosition == endPosition);
}

void TSUTF16Text::rebuildPieceStarts()
{
    m_pieceStarts.clear();
    m_pieceStarts.reserve(m_pieces.size());
    int position = 0;
    for (const Piece& piece : std::as_const(m_pieces)) {
        Q_ASSERT(piece.length > 0);
        m_pieceStarts.append(position);
        position += piece.length;
    }
    Q_ASSERT(position == m_size);
}

void TSUTF16Text::setText(const QString& text)
{
    m_original = text;
    m_additions = QString();
    m_additions.reserve(
        text.size() > kLargeTextCharacters
            ? kLargeAdditionReserveCharacters
            : kSmallAdditionReserveCharacters);
    m_pieces.clear();
    if (!text.isEmpty()) {
        m_pieces.append(Piece{
            BufferKind::Original,
            0,
            static_cast<int>(text.size())});
    }
    m_size = text.size();
    rebuildPieceStarts();
    m_materialized = text;
    m_materializedValid = true;
    m_metrics = {};
}

void TSUTF16Text::replace(int position,
                          int removedLength,
                          const QString& insertedText)
{
    const int boundedPosition = qBound(0, position, m_size);
    const int boundedRemovedLength =
        qBound(0, removedLength, m_size - boundedPosition);
    if (m_metrics.editCount == 0) {
        m_metrics.firstEditPosition = boundedPosition;
        // Retained as a compatibility diagnostic: in piece-table storage the
        // edit boundary itself replaces the former gap position.
        m_metrics.firstEditGapStart = boundedPosition;
    }

    const int removedEnd =
        boundedPosition + boundedRemovedLength;
    QVector<Piece> replacement;
    replacement.reserve(m_pieces.size() + 2);
    appendLogicalRange(&replacement, 0, boundedPosition);
    if (!insertedText.isEmpty()) {
        const int additionOffset = m_additions.size();
        m_additions.append(insertedText);
        appendPiece(&replacement,
                    BufferKind::Additions,
                    additionOffset,
                    insertedText.size());
    }
    appendLogicalRange(&replacement,
                       removedEnd,
                       m_size - removedEnd);

    m_pieces = std::move(replacement);
    m_size += insertedText.size() - boundedRemovedLength;
    rebuildPieceStarts();
    m_materialized.clear();
    m_materializedValid = false;
    ++m_metrics.editCount;
}

QChar TSUTF16Text::at(int position) const
{
    Q_ASSERT(position >= 0 && position < m_size);
    const int pieceIndex = pieceIndexAt(position);
    const Piece& piece = m_pieces.at(pieceIndex);
    return bufferFor(piece).at(
        piece.offset + position - m_pieceStarts.at(pieceIndex));
}

void TSUTF16Text::copyRange(int position,
                            int length,
                            QChar* destination) const
{
    if (length <= 0)
        return;

    const int endPosition = position + length;
    int pieceIndex = pieceIndexAt(position);
    int logicalPosition = position;
    int destinationOffset = 0;
    while (logicalPosition < endPosition
           && pieceIndex < m_pieces.size()) {
        const Piece& piece = m_pieces.at(pieceIndex);
        const int pieceStart = m_pieceStarts.at(pieceIndex);
        const int offsetInPiece = logicalPosition - pieceStart;
        const int copiedLength =
            qMin(endPosition - logicalPosition,
                 piece.length - offsetInPiece);
        std::memcpy(
            destination + destinationOffset,
            bufferFor(piece).constData()
                + piece.offset + offsetInPiece,
            static_cast<size_t>(copiedLength) * sizeof(QChar));
        logicalPosition += copiedLength;
        destinationOffset += copiedLength;
        ++pieceIndex;
    }
    Q_ASSERT(logicalPosition == endPosition);
}

QString TSUTF16Text::mid(int position, int length) const
{
    const int boundedPosition = qBound(0, position, m_size);
    const int available = m_size - boundedPosition;
    const int boundedLength =
        length < 0 ? available : qBound(0, length, available);
    if (boundedLength == 0)
        return QString();

    QString result;
    result.resize(boundedLength);
    copyRange(boundedPosition, boundedLength, result.data());
    return result;
}

QString TSUTF16Text::left(int length) const
{
    return mid(0, qBound(0, length, m_size));
}

int TSUTF16Text::indexOf(const QString& value, int from) const
{
    int start = from;
    if (start < 0)
        start = qMax(0, m_size + start);
    if (value.isEmpty())
        return start <= m_size ? start : -1;
    if (start < 0 || start > m_size - value.size())
        return -1;

    QVector<int> prefix(value.size(), 0);
    for (int index = 1, matched = 0;
         index < value.size();) {
        if (value.at(index) == value.at(matched)) {
            prefix[index++] = ++matched;
        } else if (matched > 0) {
            matched = prefix.at(matched - 1);
        } else {
            ++index;
        }
    }

    int matched = 0;
    int logicalPosition = start;
    int pieceIndex = pieceIndexAt(start);
    int offsetInPiece =
        start - m_pieceStarts.at(pieceIndex);
    while (pieceIndex < m_pieces.size()) {
        const Piece& piece = m_pieces.at(pieceIndex);
        const QString& buffer = bufferFor(piece);
        for (int local = offsetInPiece;
             local < piece.length;
             ++local, ++logicalPosition) {
            const QChar current =
                buffer.at(piece.offset + local);
            while (matched > 0
                   && current != value.at(matched)) {
                matched = prefix.at(matched - 1);
            }
            if (current == value.at(matched))
                ++matched;
            if (matched == value.size())
                return logicalPosition - value.size() + 1;
        }
        ++pieceIndex;
        offsetInPiece = 0;
    }
    return -1;
}

int TSUTF16Text::lastIndexOf(QChar value, int from) const
{
    if (m_size == 0)
        return -1;
    int position = from < 0 ? m_size - 1 : qMin(from, m_size - 1);
    int pieceIndex = pieceIndexAt(position);
    int offsetInPiece =
        position - m_pieceStarts.at(pieceIndex);
    while (pieceIndex >= 0) {
        const Piece& piece = m_pieces.at(pieceIndex);
        const QString& buffer = bufferFor(piece);
        for (int local = offsetInPiece; local >= 0; --local) {
            if (buffer.at(piece.offset + local) == value)
                return m_pieceStarts.at(pieceIndex) + local;
        }
        --pieceIndex;
        if (pieceIndex >= 0)
            offsetInPiece = m_pieces.at(pieceIndex).length - 1;
    }
    return -1;
}

const QString& TSUTF16Text::materialized() const
{
    if (m_materializedValid)
        return m_materialized;

    m_materialized.resize(m_size);
    copyRange(0, m_size, m_materialized.data());
    m_materializedValid = true;
    ++m_metrics.materializationCount;
    return m_materialized;
}

const char* TSUTF16Text::read(uint32_t byteOffset,
                              uint32_t* bytesRead) const
{
    ++m_metrics.inputReadCount;
    if (!bytesRead)
        return nullptr;
    *bytesRead = 0;
    if ((byteOffset & 1u) != 0u)
        return nullptr;

    const uint32_t characterOffset = byteOffset / 2u;
    if (characterOffset >= static_cast<uint32_t>(m_size))
        return nullptr;

    const int logicalOffset = static_cast<int>(characterOffset);
    const auto isHighSurrogate = [](char16_t value) {
        return value >= 0xd800u && value <= 0xdbffu;
    };
    const auto isLowSurrogate = [](char16_t value) {
        return value >= 0xdc00u && value <= 0xdfffu;
    };

    const int pieceIndex = pieceIndexAt(logicalOffset);
    const Piece& piece = m_pieces.at(pieceIndex);
    const int pieceStart = m_pieceStarts.at(pieceIndex);
    const int offsetInPiece = logicalOffset - pieceStart;
    const QString& buffer = bufferFor(piece);
    int contiguousCharacters = piece.length - offsetInPiece;
    const bool pieceSplitsSurrogate =
        pieceIndex + 1 < m_pieces.size()
        && isHighSurrogate(
            buffer.at(piece.offset + piece.length - 1).unicode())
        && isLowSurrogate(at(pieceStart + piece.length).unicode());
    if (pieceSplitsSurrogate
        && offsetInPiece + 1 == piece.length) {
        m_boundaryRead[0] = static_cast<char16_t>(
            buffer.at(piece.offset + offsetInPiece).unicode());
        m_boundaryRead[1] = static_cast<char16_t>(
            at(logicalOffset + 1).unicode());
        *bytesRead = 4u;
        return reinterpret_cast<const char*>(
            m_boundaryRead.data());
    }
    if (pieceSplitsSurrogate)
        --contiguousCharacters;
    const uint32_t boundedCharacters =
        qMin(static_cast<uint32_t>(contiguousCharacters),
             std::numeric_limits<uint32_t>::max() / 2u);
    *bytesRead = boundedCharacters * 2u;
    return reinterpret_cast<const char*>(
        buffer.constData() + piece.offset + offsetInPiece);
}

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

namespace {
const char* readTSUTF16Text(void* payload,
                            uint32_t byteOffset,
                            TSPoint,
                            uint32_t* bytesRead)
{
    const auto* text =
        static_cast<const TSUTF16Text*>(payload);
    return text ? text->read(byteOffset, bytesRead)
                : nullptr;
}
}

void TSDocument::reparse(TSTree* oldTree)
{
    const TSInput input{
        &m_text,
        readTSUTF16Text,
        TSInputEncodingUTF16LE,
        nullptr
    };
    TSTree* newTree =
        ts_parser_parse(m_parser, oldTree, input);

    if (m_tree)
        ts_tree_delete(m_tree);
    m_tree = newTree;
}

void TSDocument::setText(const QString& text)
{
    m_text.setText(text);
    reparse(nullptr);
    m_hasPendingEdits = false;
    m_hasDeferredSyntaxEdits = false;
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

bool isSimpleIdentifierText(const QString& text)
{
    if (text.isEmpty()
        || (!text.at(0).isLetter()
            && text.at(0) != QLatin1Char('_'))) {
        return false;
    }
    for (int index = 1; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (!character.isLetterOrNumber()
            && character != QLatin1Char('_')
            && character != QLatin1Char('$')) {
            return false;
        }
    }

    static const QSet<QString> reservedKeywords = [] {
        QSet<QString> keywords;
        const auto* keywordTable =
            slang::parsing::LexerFacts::getKeywordTable(
                slang::parsing::KeywordVersion::v1800_2023);
        keywords.reserve(
            static_cast<qsizetype>(keywordTable->size()));
        for (const auto& entry : *keywordTable) {
            keywords.insert(QString::fromLatin1(
                entry.first.data(),
                static_cast<qsizetype>(entry.first.size())));
        }
        return keywords;
    }();
    return !reservedKeywords.contains(text);
}

bool preservesSimpleIdentifierStructure(
    const TSTree* tree,
    const TSUTF16Text& text,
    const DocumentChange& change)
{
    if (!tree || !change.changesText()
        || change.removedText.contains(QLatin1Char('\n'))
        || change.insertedText.contains(QLatin1Char('\n'))
        || change.position < 0
        || change.oldEnd() > text.size()) {
        return false;
    }

    const int probe = change.oldEnd() > change.position
        ? change.oldEnd() - 1
        : change.position - 1;
    if (probe < 0 || probe >= text.size())
        return false;

    const uint32_t probeByte = static_cast<uint32_t>(probe) * 2u;
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(tree), probeByte, probeByte);
    while (!ts_node_is_null(node)
           && std::strcmp(ts_node_type(node),
                          "simple_identifier") != 0) {
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node))
        return false;

    const int nodeStart = static_cast<int>(
        ts_node_start_byte(node) / 2u);
    const int nodeEnd = static_cast<int>(
        ts_node_end_byte(node) / 2u);
    if (change.position < nodeStart
        || change.oldEnd() != nodeEnd) {
        return false;
    }

    const QString oldIdentifier = text.mid(
        nodeStart, nodeEnd - nodeStart);
    if (!isSimpleIdentifierText(oldIdentifier)
        || oldIdentifier.mid(change.position - nodeStart,
                             change.removedLength)
               != change.removedText) {
        return false;
    }
    QString newIdentifier = oldIdentifier;
    newIdentifier.replace(change.position - nodeStart,
                          change.removedLength,
                          change.insertedText);
    return isSimpleIdentifierText(newIdentifier);
}

QList<TSChangedRange> localChangedRanges(
    const DocumentChange& change)
{
    if (!change.changesText())
        return {};
    TSChangedRange range;
    range.startChar = change.position;
    range.endChar = change.newEnd();
    range.startLine = change.startLine;
    range.endLine = qMax(change.startLine, change.newEndLine);
    return {range};
}
} // namespace

QList<TSChangedRange> TSDocument::applyEdit(
    const DocumentChange& change,
    bool deferSyntaxReparse)
{
    const int position = qBound(0, change.position, m_text.size());
    const int removedLength = qBound(
        0, change.removedLength, m_text.size() - position);
    const bool structurePreserving =
        !deferSyntaxReparse
        && preservesSimpleIdentifierStructure(
            m_tree, m_text, change);

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

    QElapsedTimer phaseTimer;
    phaseTimer.start();
    if (m_tree)
        ts_tree_edit(m_tree, &edit);
    m_text.m_metrics.treeEditNanoseconds +=
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed());
    phaseTimer.restart();
    m_text.replace(position, removedLength, change.insertedText);
    m_text.m_metrics.storageEditNanoseconds +=
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed());

    phaseTimer.restart();
    QList<TSChangedRange> changedRanges =
        localChangedRanges(change);
    m_text.m_metrics.changedRangeNanoseconds +=
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed());
    if (deferSyntaxReparse || structurePreserving) {
        m_hasPendingEdits = true;
        if (deferSyntaxReparse) {
            m_hasDeferredSyntaxEdits = true;
            ++m_text.m_metrics.deferredSyntaxEditCount;
        } else {
            ++m_text.m_metrics.structurePreservingEditCount;
        }
        return changedRanges;
    }

    const TSInput input{
        &m_text,
        readTSUTF16Text,
        TSInputEncodingUTF16LE,
        nullptr
    };
    phaseTimer.restart();
    TSTree* newTree =
        ts_parser_parse(m_parser, m_tree, input);
    ++m_text.m_metrics.syntaxParseCount;
    m_text.m_metrics.parseNanoseconds +=
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed());

    phaseTimer.restart();
    if (m_tree && newTree) {
        changedRanges.clear();
        uint32_t rangeCount = 0;
        TSRange* ranges = ts_tree_get_changed_ranges(
            m_tree, newTree, &rangeCount);
        changedRanges.reserve(static_cast<int>(rangeCount));
        for (uint32_t index = 0; index < rangeCount; ++index) {
            TSChangedRange range;
            range.startChar = static_cast<int>(
                ranges[index].start_byte / 2u);
            range.endChar = static_cast<int>(
                ranges[index].end_byte / 2u);
            range.startLine = static_cast<int>(
                ranges[index].start_point.row);
            range.endLine = static_cast<int>(
                ranges[index].end_point.row);
            changedRanges.append(range);
        }
        std::free(ranges);
    }
    m_text.m_metrics.changedRangeNanoseconds +=
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed());

    phaseTimer.restart();
    if (m_tree && newTree)
        ts_tree_delete(m_tree);
    m_text.m_metrics.treeDeleteNanoseconds +=
        static_cast<std::uint64_t>(
            phaseTimer.nsecsElapsed());
    if (newTree) {
        m_tree = newTree;
        m_hasPendingEdits = false;
        m_hasDeferredSyntaxEdits = false;
    } else {
        m_hasPendingEdits = true;
    }
    return changedRanges;
}

void TSDocument::flushPendingEdits()
{
    if (!m_hasPendingEdits || !m_tree)
        return;
    const TSInput input{
        &m_text,
        readTSUTF16Text,
        TSInputEncodingUTF16LE,
        nullptr
    };
    QElapsedTimer timer;
    timer.start();
    TSTree* newTree = ts_parser_parse(m_parser, m_tree, input);
    ++m_text.m_metrics.syntaxParseCount;
    ++m_text.m_metrics.deferredSyntaxFlushCount;
    m_text.m_metrics.parseNanoseconds +=
        static_cast<std::uint64_t>(timer.nsecsElapsed());
    if (!newTree)
        return;

    timer.restart();
    ts_tree_delete(m_tree);
    m_text.m_metrics.treeDeleteNanoseconds +=
        static_cast<std::uint64_t>(timer.nsecsElapsed());
    m_tree = newTree;
    m_hasPendingEdits = false;
    m_hasDeferredSyntaxEdits = false;
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

bool TSDocument::isStringAt(int charOffset) const
{
    if (charOffset < 0 || m_text.isEmpty())
        return false;

    const int bounded =
        qBound(0, charOffset, m_text.size() - 1);
    const uint32_t byte =
        static_cast<uint32_t>(bounded) * 2u;
    TSNode node = ts_node_descendant_for_byte_range(
        ts_tree_root_node(m_tree), byte, byte);
    while (!ts_node_is_null(node)) {
        const char* type = ts_node_type(node);
        if (type
            && (std::strcmp(type, "string_literal") == 0
                || std::strcmp(type, "quoted_string") == 0
                || std::strcmp(type,
                               "triple_quoted_string") == 0)) {
            return true;
        }
        node = ts_node_parent(node);
    }
    return false;
}

TSNumericLiteralTarget TSDocument::numericLiteralAt(int charOffset) const
{
    TSNumericLiteralTarget target;
    if (m_text.isEmpty() || charOffset < 0 || charOffset > m_text.size())
        return target;

    int probe = qMin(charOffset, m_text.size() - 1);
    auto numericNodeAt = [this](int position) {
        const uint32_t byte = static_cast<uint32_t>(qMax(0, position)) * 2u;
        TSNode node = ts_node_descendant_for_byte_range(
            ts_tree_root_node(m_tree), byte, byte);
        while (!ts_node_is_null(node)) {
            if (classifyTokenType(ts_node_type(node),
                                  ts_node_is_named(node))
                == HlCategory::Number) {
                return node;
            }
            const char* type = ts_node_type(node);
            if (type
                && (std::strcmp(type, "one_line_comment") == 0
                    || std::strcmp(type, "block_comment") == 0
                    || std::strcmp(type, "string_literal") == 0)) {
                return TSNode{};
            }
            node = ts_node_parent(node);
        }
        return TSNode{};
    };

    auto stringNodeAt = [this](int position) {
        const uint32_t byte =
            static_cast<uint32_t>(qMax(0, position)) * 2u;
        TSNode node = ts_node_descendant_for_byte_range(
            ts_tree_root_node(m_tree), byte, byte);
        TSNode stringNode{};
        while (!ts_node_is_null(node)) {
            const char* type = ts_node_type(node);
            if (type
                && (std::strcmp(type, "one_line_comment") == 0
                    || std::strcmp(type, "block_comment") == 0
                    || std::strcmp(type,
                                   "include_compiler_directive") == 0)) {
                return TSNode{};
            }
            if (type && std::strcmp(type, "string_literal") == 0)
                stringNode = node;
            node = ts_node_parent(node);
        }
        return stringNode;
    };

    TSNode numeric = numericNodeAt(probe);
    if (ts_node_is_null(numeric)
        && charOffset > 0
        && (charOffset == m_text.size()
            || m_text.at(charOffset).isSpace())) {
        numeric = numericNodeAt(charOffset - 1);
    }
    if (ts_node_is_null(numeric)) {
        TSNode stringNode = stringNodeAt(probe);
        if (ts_node_is_null(stringNode)
            && charOffset > 0) {
            stringNode = stringNodeAt(charOffset - 1);
        }
        if (ts_node_is_null(stringNode))
            return target;

        const int tokenStart =
            static_cast<int>(ts_node_start_byte(stringNode) / 2u);
        const int tokenEnd =
            static_cast<int>(ts_node_end_byte(stringNode) / 2u);
        if (tokenStart < 0
            || tokenEnd <= tokenStart + 2
            || tokenEnd > m_text.size()) {
            return target;
        }
        const QString token =
            m_text.mid(tokenStart, tokenEnd - tokenStart);
        if (!token.startsWith(QLatin1Char('"'))
            || !token.endsWith(QLatin1Char('"'))
            || token.startsWith(QStringLiteral("\"\"\""))) {
            return target;
        }
        target.startChar = tokenStart + 1;
        target.endChar = tokenEnd - 1;
        target.text = m_text.mid(
            target.startChar,
            target.endChar - target.startChar);
        target.evaluationText = token;
        target.stringLiteral = true;
        return target;
    }

    target.startChar = static_cast<int>(ts_node_start_byte(numeric) / 2u);
    target.endChar = static_cast<int>(ts_node_end_byte(numeric) / 2u);
    if (target.endChar <= target.startChar
        || target.endChar > m_text.size()) {
        return {};
    }
    target.text = m_text.mid(target.startChar,
                            target.endChar - target.startChar);
    target.evaluationText = target.text;
    TSNode expression = ts_node_parent(numeric);
    while (!ts_node_is_null(expression)) {
        static constexpr char kOperatorField[] = "operator";
        static constexpr char kArgumentField[] = "argument";
        const TSNode operatorNode =
            ts_node_child_by_field_name(
                expression,
                kOperatorField,
                sizeof(kOperatorField) - 1);
        const TSNode argumentNode =
            ts_node_child_by_field_name(
                expression,
                kArgumentField,
                sizeof(kArgumentField) - 1);
        if (!ts_node_is_null(operatorNode)
            && !ts_node_is_null(argumentNode)) {
            const int argumentStart =
                static_cast<int>(
                    ts_node_start_byte(argumentNode) / 2u);
            const int argumentEnd =
                static_cast<int>(
                    ts_node_end_byte(argumentNode) / 2u);
            const int operatorStart =
                static_cast<int>(
                    ts_node_start_byte(operatorNode) / 2u);
            const int operatorEnd =
                static_cast<int>(
                    ts_node_end_byte(operatorNode) / 2u);
            const QString operatorText =
                m_text.mid(operatorStart,
                           operatorEnd - operatorStart);
            if (argumentStart == target.startChar
                && argumentEnd == target.endChar
                && (operatorText == QStringLiteral("-")
                    || operatorText == QStringLiteral("+"))) {
                const int expressionStart =
                    static_cast<int>(
                        ts_node_start_byte(expression) / 2u);
                const int expressionEnd =
                    static_cast<int>(
                        ts_node_end_byte(expression) / 2u);
                if (expressionStart >= 0
                    && expressionEnd > expressionStart
                    && expressionEnd <= m_text.size()) {
                    target.evaluationText =
                        m_text.mid(
                            expressionStart,
                            expressionEnd - expressionStart);
                }
            }
            break;
        }
        expression = ts_node_parent(expression);
    }
    return target;
}

namespace {
bool identifierNode(TSNode node);
bool commentOrStringNode(TSNode node);
bool nodeTypeIs(TSNode node, const char* expected);
TSNode namedNodeAt(const TSTree* tree, int charOffset, int textSize);
template <typename Text>
QString nodeText(const Text& text, TSNode node);
TSNode ancestorOfType(TSNode node, const char* expected);
int nodeStartChar(TSNode node);
int nodeEndChar(TSNode node);
QList<TSNode> directNamedChildrenOf(TSNode node);
QList<TSNode> directNamedChildrenOfType(TSNode node,
                                        const char* expected);
TSNode firstDirectNamedChildOfType(TSNode node,
                                   const char* expected);
TSNode childByField(TSNode node, const char* field);
bool nodeContainsChar(TSNode node, int position);
TSNode firstIdentifierChild(TSNode node);
TSNode expressionChildForAssociation(TSNode association,
                                     bool namedAssociation);
template <typename Text>
bool collectAssociationSlots(TSNode container,
                             const Text& text,
                             const char* namedType,
                             const char* orderedType,
                             QList<TSExpressionSlot>* outputSlots);
TSNode firstLvalueChild(TSNode assignment);
template <typename Text>
QString leadingIdentifierAt(const Text& text, int start, int end);
template <typename Text>
int closingParenStart(TSNode node, const Text& text);
template <typename Text>
int lineStartChar(const Text& text, int line);
template <typename Text>
QString lineIndentAt(const Text& text, int line);
int nodeLastLine(TSNode node);
} // namespace

TSIdentifierTarget TSDocument::identifierAt(int charOffset) const
{
    TSIdentifierTarget target;
    if (m_text.isEmpty() || charOffset < 0 || charOffset > m_text.size())
        return target;

    const auto identifierNodeAt = [this](int position) {
        TSNode node = namedNodeAt(m_tree, position, m_text.size());
        while (!ts_node_is_null(node)) {
            if (commentOrStringNode(node))
                return TSNode{};
            if (identifierNode(node))
                return node;
            node = ts_node_parent(node);
        }
        return TSNode{};
    };

    TSNode identifier = identifierNodeAt(
        qMin(charOffset, m_text.size() - 1));
    if (ts_node_is_null(identifier)
        && charOffset > 0
        && (charOffset == m_text.size()
            || m_text.at(charOffset).isSpace())) {
        identifier = identifierNodeAt(charOffset - 1);
    }
    if (ts_node_is_null(identifier))
        return target;

    target.startChar = nodeStartChar(identifier);
    target.endChar = nodeEndChar(identifier);
    if (target.endChar <= target.startChar
        || target.endChar > m_text.size()) {
        return {};
    }
    target.text = nodeText(m_text, identifier);
    return target;
}

QList<TSIdentifierTarget> TSDocument::identifiersInRange(
    int startChar,
    int endChar) const
{
    QList<TSIdentifierTarget> result;
    if (!m_tree || m_text.isEmpty())
        return result;

    const int boundedStart = qBound(0, startChar, m_text.size());
    const int boundedEnd = qBound(boundedStart, endChar, m_text.size());
    if (boundedEnd <= boundedStart)
        return result;

    QSet<QString> seenNames;
    QList<TSNode> pending{ts_tree_root_node(m_tree)};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        const int nodeStart = nodeStartChar(node);
        const int nodeEnd = nodeEndChar(node);
        if (nodeEnd <= boundedStart || nodeStart >= boundedEnd)
            continue;
        if (commentOrStringNode(node))
            continue;
        if (identifierNode(node)) {
            TSIdentifierTarget identifier;
            identifier.startChar = nodeStart;
            identifier.endChar = nodeEnd;
            identifier.text = nodeText(m_text, node);
            if (identifier.ok() && !seenNames.contains(identifier.text)) {
                seenNames.insert(identifier.text);
                result.append(std::move(identifier));
            }
            continue;
        }
        for (int index = static_cast<int>(ts_node_named_child_count(node)) - 1;
             index >= 0;
             --index) {
            pending.append(ts_node_named_child(
                node, static_cast<uint32_t>(index)));
        }
    }
    return result;
}

TSExpressionAtomTarget TSDocument::expressionAtomAt(
    int charOffset) const
{
    TSExpressionAtomTarget target;
    const TSIdentifierTarget identifier = identifierAt(charOffset);
    if (!identifier.ok() || m_text.isEmpty())
        return target;

    TSNode node = namedNodeAt(
        m_tree,
        qBound(0, charOffset, m_text.size() - 1),
        m_text.size());
    while (!ts_node_is_null(node)
           && (!identifierNode(node)
               || nodeStartChar(node) != identifier.startChar
               || nodeEndChar(node) != identifier.endChar)) {
        if (commentOrStringNode(node))
            return {};
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node))
        return {};

    TSNode atom = node;
    while (true) {
        const TSNode parent = ts_node_parent(atom);
        if (ts_node_is_null(parent) || ts_node_has_error(parent))
            break;
        const bool atomContainer = nodeTypeIs(parent, "primary")
            || nodeTypeIs(parent, "hierarchical_identifier")
            || nodeTypeIs(parent, "select")
            || nodeTypeIs(parent, "bit_select")
            || nodeTypeIs(parent, "constant_bit_select")
            || nodeTypeIs(parent, "part_select_range")
            || nodeTypeIs(parent, "indexed_range")
            || nodeTypeIs(parent, "subroutine_call")
            || nodeTypeIs(parent, "function_subroutine_call")
            || nodeTypeIs(parent, "method_call")
            || nodeTypeIs(parent, "method_call_body")
            || nodeTypeIs(parent, "system_tf_call");
        if (!atomContainer)
            break;
        atom = parent;
    }

    target.startChar = nodeStartChar(atom);
    target.endChar = nodeEndChar(atom);
    if (target.startChar < 0
        || target.endChar <= target.startChar
        || target.endChar > m_text.size()) {
        return {};
    }
    target.text = nodeText(m_text, atom);
    return target;
}

TSCompletionContextTarget TSDocument::completionContextAt(
    int charOffset) const
{
    TSCompletionContextTarget target;
    if (m_text.isEmpty())
        return target;
    const int cursor = qBound(0, charOffset, m_text.size());

    const TSIdentifierTarget identifier = identifierAt(cursor);
    int prefixStart = cursor;
    if (identifier.ok()
        && cursor >= identifier.startChar
        && cursor <= identifier.endChar) {
        prefixStart = identifier.startChar;
    }
    int dot = prefixStart;
    while (dot > 0 && m_text.at(dot - 1).isSpace()
           && m_text.at(dot - 1) != QLatin1Char('\n')) {
        --dot;
    }
    --dot;
    if (dot >= 0 && m_text.at(dot) == QLatin1Char('.')) {
        int baseEnd = dot;
        while (baseEnd > 0 && m_text.at(baseEnd - 1).isSpace())
            --baseEnd;
        TSNode baseNode = namedNodeAt(
            m_tree, qMax(0, baseEnd - 1), m_text.size());
        while (!ts_node_is_null(baseNode)) {
            const TSNode parent = ts_node_parent(baseNode);
            if (ts_node_is_null(parent)
                || nodeEndChar(parent) > baseEnd) {
                break;
            }
            const bool atomParent = nodeTypeIs(parent, "primary")
                || nodeTypeIs(parent, "hierarchical_identifier")
                || nodeTypeIs(parent, "select")
                || nodeTypeIs(parent, "bit_select")
                || nodeTypeIs(parent, "constant_bit_select")
                || nodeTypeIs(parent, "part_select_range")
                || nodeTypeIs(parent, "indexed_range")
                || nodeTypeIs(parent, "nonrange_select");
            if (!atomParent)
                break;
            baseNode = parent;
        }
        const int baseStart = nodeStartChar(baseNode);
        const int boundedBaseEnd = nodeEndChar(baseNode);
        if (!ts_node_is_null(baseNode)
            && baseStart >= 0
            && boundedBaseEnd > baseStart
            && boundedBaseEnd <= baseEnd) {
            QStringList path;
            const QString text = m_text.mid(
                baseStart, boundedBaseEnd - baseStart);
            int squareDepth = 0;
            int parenDepth = 0;
            for (int index = 0; index < text.size();) {
                const QChar character = text.at(index);
                if (character == QLatin1Char('[')) {
                    ++squareDepth;
                    ++index;
                    continue;
                }
                if (character == QLatin1Char(']')) {
                    squareDepth = qMax(0, squareDepth - 1);
                    ++index;
                    continue;
                }
                if (character == QLatin1Char('(')) {
                    ++parenDepth;
                    ++index;
                    continue;
                }
                if (character == QLatin1Char(')')) {
                    parenDepth = qMax(0, parenDepth - 1);
                    ++index;
                    continue;
                }
                if (squareDepth == 0 && parenDepth == 0
                    && (character.isLetter()
                        || character == QLatin1Char('_')
                        || character == QLatin1Char('$'))) {
                    const int start = index++;
                    while (index < text.size()) {
                        const QChar next = text.at(index);
                        if (!next.isLetterOrNumber()
                            && next != QLatin1Char('_')
                            && next != QLatin1Char('$')) {
                            break;
                        }
                        ++index;
                    }
                    path.append(text.mid(start, index - start));
                    continue;
                }
                ++index;
            }
            if (!path.isEmpty()) {
                target.memberAccess = true;
                target.memberPath = path;
            }
        }
    }

    TSNode node = namedNodeAt(
        m_tree,
        qBound(0, cursor == m_text.size() ? cursor - 1 : cursor,
               m_text.size() - 1),
        m_text.size());
    while (!ts_node_is_null(node)) {
        if (commentOrStringNode(node))
            break;

        if (nodeTypeIs(node, "nonblocking_assignment")
            || nodeTypeIs(node, "blocking_assignment")
            || nodeTypeIs(node, "operator_assignment")
            || nodeTypeIs(node, "variable_assignment")
            || nodeTypeIs(node, "net_assignment")) {
            const TSNode lvalue = firstLvalueChild(node);
            if (!ts_node_is_null(lvalue)
                && cursor >= nodeEndChar(lvalue)) {
                target.expectedTypeIdentifier = leadingIdentifierAt(
                    m_text, nodeStartChar(lvalue), nodeEndChar(lvalue));
                if (!target.expectedTypeIdentifier.isEmpty())
                    break;
            }
        }

        if (nodeTypeIs(node, "expression")) {
            const TSNode operatorNode = childByField(node, "operator");
            const QString operatorText = nodeText(m_text, operatorNode);
            const bool comparison = operatorText == QStringLiteral("==")
                || operatorText == QStringLiteral("!=")
                || operatorText == QStringLiteral("===")
                || operatorText == QStringLiteral("!==")
                || operatorText == QStringLiteral("==?")
                || operatorText == QStringLiteral("!=?");
            if (comparison) {
                const TSNode left = childByField(node, "left");
                const TSNode right = childByField(node, "right");
                TSNode opposite;
                if (!ts_node_is_null(left)
                    && (ts_node_is_null(right)
                        || cursor >= nodeStartChar(right))) {
                    opposite = left;
                } else if (!ts_node_is_null(right)
                           && cursor <= nodeEndChar(left)) {
                    opposite = right;
                }
                if (!ts_node_is_null(opposite)) {
                    target.expectedTypeIdentifier = leadingIdentifierAt(
                        m_text,
                        nodeStartChar(opposite),
                        nodeEndChar(opposite));
                    if (!target.expectedTypeIdentifier.isEmpty())
                        break;
                }
            }
        }

        if (nodeTypeIs(node, "case_item")) {
            TSNode caseStatement = ts_node_parent(node);
            while (!ts_node_is_null(caseStatement)
                   && !nodeTypeIs(caseStatement, "case_statement")) {
                caseStatement = ts_node_parent(caseStatement);
            }
            const TSNode caseExpression = firstDirectNamedChildOfType(
                caseStatement, "case_expression");
            if (!ts_node_is_null(caseExpression)) {
                target.expectedTypeIdentifier = leadingIdentifierAt(
                    m_text,
                    nodeStartChar(caseExpression),
                    nodeEndChar(caseExpression));
                if (!target.expectedTypeIdentifier.isEmpty())
                    break;
            }
        }
        node = ts_node_parent(node);
    }
    return target;
}

TSIdentifierOccurrenceSet
TSDocument::identifierOccurrencesAt(
    int charOffset) const
{
    TSIdentifierOccurrenceSet result;
    if (m_text.isEmpty()
        || charOffset < 0
        || charOffset > m_text.size()) {
        return result;
    }

    const auto identifierNodeAtPosition =
        [this](int position) {
            TSNode node = namedNodeAt(
                m_tree,
                qBound(0, position, m_text.size() - 1),
                m_text.size());
            while (!ts_node_is_null(node)) {
                if (commentOrStringNode(node))
                    return TSNode{};
                if (identifierNode(node))
                    return node;
                node = ts_node_parent(node);
            }
            return TSNode{};
        };

    TSNode selectedNode =
        identifierNodeAtPosition(
            qMin(charOffset, m_text.size() - 1));
    if (ts_node_is_null(selectedNode)
        && charOffset > 0) {
        selectedNode =
            identifierNodeAtPosition(charOffset - 1);
    }
    if (ts_node_is_null(selectedNode))
        return result;

    result.selected.startChar =
        nodeStartChar(selectedNode);
    result.selected.endChar =
        nodeEndChar(selectedNode);
    result.selected.text =
        nodeText(m_text, selectedNode);
    if (!result.selected.ok())
        return {};

    const auto isLexicalScope =
        [](TSNode node) {
            return nodeTypeIs(node, "seq_block")
                || nodeTypeIs(node, "par_block")
                || nodeTypeIs(
                    node, "function_declaration")
                || nodeTypeIs(
                    node, "task_declaration")
                || nodeTypeIs(
                    node, "class_declaration")
                || nodeTypeIs(
                    node, "module_declaration")
                || nodeTypeIs(
                    node, "interface_declaration")
                || nodeTypeIs(
                    node, "program_declaration")
                || nodeTypeIs(
                    node, "package_declaration")
                || nodeTypeIs(
                    node, "checker_declaration");
        };
    TSNode scope = selectedNode;
    while (!ts_node_is_null(scope)
           && !isLexicalScope(scope)) {
        scope = ts_node_parent(scope);
    }
    if (ts_node_is_null(scope))
        scope = ts_tree_root_node(m_tree);
    result.scopeStartChar =
        nodeStartChar(scope);
    result.scopeEndChar =
        nodeEndChar(scope);

    QList<TSNode> pending;
    pending.append(scope);
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        const uint32_t childCount =
            ts_node_child_count(node);
        if (childCount == 0) {
            if (identifierNode(node)
                && nodeText(m_text, node)
                       == result.selected.text) {
                TSIdentifierTarget occurrence;
                occurrence.startChar =
                    nodeStartChar(node);
                occurrence.endChar =
                    nodeEndChar(node);
                occurrence.text =
                    result.selected.text;
                if (occurrence.ok())
                    result.occurrences.prepend(
                        occurrence);
            }
            continue;
        }
        for (uint32_t index = 0;
             index < childCount;
             ++index) {
            pending.append(
                ts_node_child(node, index));
        }
    }
    std::sort(
        result.occurrences.begin(),
        result.occurrences.end(),
        [](const TSIdentifierTarget& left,
           const TSIdentifierTarget& right) {
            if (left.startChar != right.startChar)
                return left.startChar < right.startChar;
            return left.endChar < right.endChar;
        });
    return result;
}

TSAssignmentNavigationTarget
TSDocument::assignmentNavigationTarget(
    int charOffset,
    bool previous) const
{
    TSAssignmentNavigationTarget target;
    const TSIdentifierTarget selected =
        identifierAt(charOffset);
    if (!selected.ok())
        return target;

    target.identifier = selected.text;
    target.sourceChar = selected.startChar;

    TSNode selectedNode = namedNodeAt(
        m_tree,
        selected.startChar,
        m_text.size());
    while (!ts_node_is_null(selectedNode)
           && !identifierNode(selectedNode)) {
        selectedNode = ts_node_parent(selectedNode);
    }
    if (ts_node_is_null(selectedNode))
        return target;

    const auto isAssignmentScope =
        [](TSNode node) {
            return nodeTypeIs(
                       node,
                       "function_declaration")
                || nodeTypeIs(
                       node,
                       "task_declaration")
                || nodeTypeIs(
                       node,
                       "class_declaration")
                || nodeTypeIs(
                       node,
                       "module_declaration")
                || nodeTypeIs(
                       node,
                       "interface_declaration")
                || nodeTypeIs(
                       node,
                       "program_declaration")
                || nodeTypeIs(
                       node,
                       "package_declaration")
                || nodeTypeIs(
                       node,
                       "checker_declaration");
        };
    TSNode scope = selectedNode;
    while (!ts_node_is_null(scope)
           && !isAssignmentScope(scope)) {
        scope = ts_node_parent(scope);
    }
    if (ts_node_is_null(scope))
        scope = ts_tree_root_node(m_tree);
    const int scopeStart = nodeStartChar(scope);
    const int scopeEnd = nodeEndChar(scope);

    QList<TSIdentifierTarget> occurrences;
    QList<TSNode> pending = {scope};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        const uint32_t childCount =
            ts_node_child_count(node);
        if (childCount == 0) {
            if (identifierNode(node)
                && nodeText(m_text, node)
                       == selected.text) {
                TSIdentifierTarget occurrence;
                occurrence.startChar =
                    nodeStartChar(node);
                occurrence.endChar =
                    nodeEndChar(node);
                occurrence.text = selected.text;
                if (occurrence.ok())
                    occurrences.prepend(occurrence);
            }
            continue;
        }
        for (uint32_t index = 0;
             index < childCount;
             ++index) {
            pending.append(
                ts_node_child(node, index));
        }
    }

    QList<int> assignmentPositions;
    QSet<int> seenAssignments;
    for (const TSIdentifierTarget& occurrence :
         std::as_const(occurrences)) {
        TSNode node = namedNodeAt(
            m_tree,
            occurrence.startChar,
            m_text.size());
        while (!ts_node_is_null(node)
               && !identifierNode(node)) {
            node = ts_node_parent(node);
        }
        if (ts_node_is_null(node))
            continue;

        TSNode assignment = node;
        while (!ts_node_is_null(assignment)) {
            if (nodeTypeIs(assignment, "net_assignment")
                || nodeTypeIs(
                    assignment, "variable_assignment")
                || nodeTypeIs(
                    assignment, "blocking_assignment")
                || nodeTypeIs(
                    assignment, "nonblocking_assignment")
                || nodeTypeIs(
                    assignment, "operator_assignment")) {
                break;
            }
            if (nodeStartChar(assignment)
                    < scopeStart
                || nodeEndChar(assignment)
                    > scopeEnd) {
                assignment = {};
                break;
            }
            assignment = ts_node_parent(assignment);
        }
        if (ts_node_is_null(assignment)
            || ts_node_has_error(assignment)) {
            continue;
        }

        const TSNode lvalue =
            firstLvalueChild(assignment);
        if (ts_node_is_null(lvalue)
            || occurrence.startChar < nodeStartChar(lvalue)
            || occurrence.endChar > nodeEndChar(lvalue)) {
            continue;
        }

        const int assignmentStart =
            nodeStartChar(assignment);
        if (seenAssignments.contains(assignmentStart))
            continue;
        seenAssignments.insert(assignmentStart);
        assignmentPositions.append(occurrence.startChar);
    }

    std::sort(assignmentPositions.begin(),
              assignmentPositions.end());
    target.assignmentCount = assignmentPositions.size();
    if (assignmentPositions.isEmpty()) {
        target.status =
            TSAssignmentNavigationStatus::NoAssignment;
        return target;
    }

    int selectedIndex = -1;
    if (previous) {
        for (int index = assignmentPositions.size() - 1;
             index >= 0;
             --index) {
            if (assignmentPositions.at(index)
                < target.sourceChar) {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex < 0) {
            selectedIndex = assignmentPositions.size() - 1;
            target.wrapped = true;
        }
    } else {
        for (int index = 0;
             index < assignmentPositions.size();
             ++index) {
            if (assignmentPositions.at(index)
                > target.sourceChar) {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex < 0) {
            selectedIndex = 0;
            target.wrapped = true;
        }
    }

    target.targetChar =
        assignmentPositions.at(selectedIndex);
    target.status = TSAssignmentNavigationStatus::Ok;
    return target;
}

TSConditionalBranchNavigationTarget
TSDocument::conditionalBranchNavigationTarget(
    int charOffset,
    bool previous) const
{
    TSConditionalBranchNavigationTarget target;
    if (m_text.isEmpty()
        || charOffset < 0
        || charOffset > m_text.size()) {
        return target;
    }

    struct Directive {
        int startChar = -1;
        int endChar = -1;
        QString text;
    };
    QList<Directive> directives;
    QList<TSNode> pending = {ts_tree_root_node(m_tree)};
    while (!pending.isEmpty()) {
        const TSNode node = pending.takeLast();
        if (nodeTypeIs(
                node,
                "conditional_compilation_directive")) {
            Directive directive;
            directive.startChar = nodeStartChar(node);
            directive.endChar = nodeEndChar(node);

            QList<TSNode> leaves = {node};
            while (!leaves.isEmpty()) {
                const TSNode candidate =
                    leaves.takeLast();
                const uint32_t childCount =
                    ts_node_child_count(candidate);
                if (childCount == 0) {
                    const QString token =
                        nodeText(m_text, candidate);
                    if (token == QStringLiteral("`ifdef")
                        || token
                            == QStringLiteral("`ifndef")
                        || token
                            == QStringLiteral("`elsif")
                        || token
                            == QStringLiteral("`else")
                        || token
                            == QStringLiteral("`endif")) {
                        directive.startChar =
                            nodeStartChar(candidate);
                        directive.endChar =
                            nodeEndChar(candidate);
                        directive.text = token;
                        break;
                    }
                    continue;
                }
                for (uint32_t reverse = childCount;
                     reverse > 0;
                     --reverse) {
                    leaves.append(
                        ts_node_child(
                            candidate,
                            reverse - 1));
                }
            }
            if (!directive.text.isEmpty())
                directives.append(directive);
            continue;
        }

        const uint32_t childCount =
            ts_node_child_count(node);
        for (uint32_t index = 0;
             index < childCount;
             ++index) {
            pending.append(ts_node_child(node, index));
        }
    }
    std::sort(
        directives.begin(),
        directives.end(),
        [](const Directive& left,
           const Directive& right) {
            return left.startChar < right.startChar;
        });

    struct ConditionalGroup {
        int openChar = -1;
        int closeChar = -1;
        int closeEndChar = -1;
        int depth = 0;
        QList<int> branchStartChars;
        QStringList branchDirectives;
    };
    QList<ConditionalGroup> groups;
    QList<int> stack;
    bool sawIncompleteGroup = false;
    for (const Directive& directive :
         std::as_const(directives)) {
        if (directive.text == QStringLiteral("`ifdef")
            || directive.text
                == QStringLiteral("`ifndef")) {
            ConditionalGroup group;
            group.openChar = directive.startChar;
            group.depth = stack.size();
            group.branchStartChars.append(
                directive.startChar);
            group.branchDirectives.append(
                directive.text);
            groups.append(group);
            stack.append(groups.size() - 1);
            continue;
        }

        if (stack.isEmpty())
            continue;
        ConditionalGroup& group =
            groups[stack.constLast()];
        if (directive.text == QStringLiteral("`elsif")
            || directive.text
                == QStringLiteral("`else")) {
            group.branchStartChars.append(
                directive.startChar);
            group.branchDirectives.append(
                directive.text);
            continue;
        }
        if (directive.text == QStringLiteral("`endif")) {
            group.branchStartChars.append(
                directive.startChar);
            group.branchDirectives.append(
                directive.text);
            group.closeChar = directive.startChar;
            group.closeEndChar = directive.endChar;
            stack.removeLast();
        }
    }
    if (!stack.isEmpty())
        sawIncompleteGroup = true;

    const int boundedSource =
        qBound(0, charOffset, m_text.size());
    int selectedGroup = -1;
    for (int index = 0; index < groups.size(); ++index) {
        const ConditionalGroup& group = groups.at(index);
        if (group.closeChar < 0
            || boundedSource < group.openChar
            || boundedSource > group.closeEndChar) {
            continue;
        }
        if (selectedGroup < 0
            || group.depth
                > groups.at(selectedGroup).depth
            || (group.depth
                    == groups.at(selectedGroup).depth
                && group.closeEndChar - group.openChar
                    < groups.at(selectedGroup).closeEndChar
                        - groups.at(selectedGroup).openChar)) {
            selectedGroup = index;
        }
    }

    target.sourceChar = boundedSource;
    if (selectedGroup < 0) {
        target.status = sawIncompleteGroup
            ? TSConditionalBranchNavigationStatus::
                  IncompleteConditionalGroup
            : TSConditionalBranchNavigationStatus::
                  NoConditionalGroup;
        return target;
    }

    const ConditionalGroup& group =
        groups.at(selectedGroup);
    target.branchStartChars =
        group.branchStartChars;
    target.branchDirectives =
        group.branchDirectives;

    int selectedIndex = -1;
    if (previous) {
        for (int index =
                 group.branchStartChars.size() - 1;
             index >= 0;
             --index) {
            if (group.branchStartChars.at(index)
                < boundedSource) {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex < 0) {
            selectedIndex =
                group.branchStartChars.size() - 1;
            target.wrapped = true;
        }
    } else {
        for (int index = 0;
             index < group.branchStartChars.size();
             ++index) {
            if (group.branchStartChars.at(index)
                > boundedSource) {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex < 0) {
            selectedIndex = 0;
            target.wrapped = true;
        }
    }

    target.targetIndex = selectedIndex;
    target.targetChar =
        group.branchStartChars.at(selectedIndex);
    target.targetDirective =
        group.branchDirectives.at(selectedIndex);
    target.status =
        TSConditionalBranchNavigationStatus::Ok;
    return target;
}

TSExpressionSuffixTarget TSDocument::expressionSuffixTarget(
    int startChar,
    int endChar) const
{
    TSExpressionSuffixTarget target;
    if (!m_tree || m_text.isEmpty())
        return target;
    int start = qBound(0, startChar, m_text.size());
    int end = qBound(start, endChar, m_text.size());
    while (start < end && m_text.at(start).isSpace())
        ++start;
    while (end > start && m_text.at(end - 1).isSpace())
        --end;
    if (end <= start)
        return target;

    TSNode covering = namedNodeAt(m_tree, start, m_text.size());
    while (!ts_node_is_null(covering)
           && (nodeStartChar(covering) > start
               || nodeEndChar(covering) < end)) {
        covering = ts_node_parent(covering);
    }
    if (ts_node_is_null(covering) || ts_node_has_error(covering)
        || commentOrStringNode(covering)) {
        return target;
    }

    QList<TSNode> leaves;
    const std::function<void(TSNode)> collectLeaves =
        [&](TSNode node) {
            if (ts_node_is_null(node)
                || nodeEndChar(node) <= start
                || nodeStartChar(node) >= end) {
                return;
            }
            const uint32_t count = ts_node_child_count(node);
            if (count == 0) {
                if (nodeStartChar(node) >= start
                    && nodeEndChar(node) <= end) {
                    leaves.append(node);
                }
                return;
            }
            for (uint32_t index = 0; index < count; ++index)
                collectLeaves(ts_node_child(node, index));
        };
    collectLeaves(covering);
    std::sort(leaves.begin(), leaves.end(),
              [](TSNode left, TSNode right) {
        return nodeStartChar(left) < nodeStartChar(right);
    });
    if (leaves.isEmpty())
        return target;

    int parenDepth = 0;
    int braceDepth = 0;
    int bracketDepth = 0;
    int suffixIndex = -1;
    for (int index = 0; index < leaves.size(); ++index) {
        const QString token = nodeText(m_text, leaves.at(index));
        if (token == QStringLiteral("[")
            && parenDepth == 0
            && braceDepth == 0
            && bracketDepth == 0) {
            suffixIndex = index;
            break;
        }
        if (token == QStringLiteral("("))
            ++parenDepth;
        else if (token == QStringLiteral(")"))
            parenDepth = std::max(0, parenDepth - 1);
        else if (token == QStringLiteral("{"))
            ++braceDepth;
        else if (token == QStringLiteral("}"))
            braceDepth = std::max(0, braceDepth - 1);
        else if (token == QStringLiteral("["))
            ++bracketDepth;
        else if (token == QStringLiteral("]"))
            bracketDepth = std::max(0, bracketDepth - 1);
    }

    bool terminalChain = suffixIndex >= 0;
    bracketDepth = 0;
    for (int index = suffixIndex;
         terminalChain && index < leaves.size();
         ++index) {
        const QString token = nodeText(m_text, leaves.at(index));
        if (bracketDepth == 0) {
            if (token != QStringLiteral("[")) {
                terminalChain = false;
                break;
            }
            ++bracketDepth;
        } else if (token == QStringLiteral("[")) {
            ++bracketDepth;
        } else if (token == QStringLiteral("]")) {
            --bracketDepth;
        }
    }
    terminalChain = terminalChain && bracketDepth == 0;

    target.baseStartChar = start;
    target.baseEndChar = terminalChain
        ? nodeStartChar(leaves.at(suffixIndex)) : end;
    target.suffixStartChar = target.baseEndChar;
    target.suffixEndChar = terminalChain ? end : target.baseEndChar;
    return target.ok() ? target : TSExpressionSuffixTarget{};
}

TSStructuralNavigationTarget TSDocument::structuralNavigationTarget(
    int charOffset,
    TSStructuralNavigationDirection direction) const
{
    struct Field {
        TSStructuralNavigationTarget target;
        int ordinal = 0;
    };

    if (!m_tree || m_text.isEmpty()
        || charOffset < 0 || charOffset > m_text.size()) {
        return {};
    }

    const int cursor = qBound(0, charOffset, m_text.size());
    TSNode leaf = namedNodeAt(
        m_tree, qMin(cursor, m_text.size() - 1), m_text.size());
    if (ts_node_is_null(leaf) && cursor > 0) {
        leaf = namedNodeAt(m_tree, cursor - 1, m_text.size());
    }
    if (ts_node_is_null(leaf) || commentOrStringNode(leaf))
        return {};

    const auto matchesAny = [](TSNode node,
                               std::initializer_list<const char*> types) {
        for (const char* type : types) {
            if (nodeTypeIs(node, type))
                return true;
        }
        return false;
    };
    const auto trimRange = [this](int* start, int* end) {
        if (!start || !end)
            return;
        *start = qBound(0, *start, m_text.size());
        *end = qBound(*start, *end, m_text.size());
        while (*start < *end && m_text.at(*start).isSpace())
            ++*start;
        while (*end > *start && m_text.at(*end - 1).isSpace())
            --*end;
    };
    const auto appendRange = [&](QList<Field>* fields,
                                 TSStructuralFieldRole role,
                                 int start,
                                 int end,
                                 TSNode item,
                                 TSNode list,
                                 int ordinal = 0) {
        if (!fields || role == TSStructuralFieldRole::Unknown)
            return;
        trimRange(&start, &end);
        if (end <= start)
            return;
        Field field;
        field.target.startChar = start;
        field.target.endChar = end;
        field.target.itemStartChar = ts_node_is_null(item)
            ? start : nodeStartChar(item);
        field.target.itemEndChar = ts_node_is_null(item)
            ? end : nodeEndChar(item);
        field.target.listStartChar = ts_node_is_null(list)
            ? field.target.itemStartChar : nodeStartChar(list);
        field.target.listEndChar = ts_node_is_null(list)
            ? field.target.itemEndChar : nodeEndChar(list);
        field.target.role = role;
        field.ordinal = ordinal;
        fields->append(field);
    };
    const auto appendNode = [&](QList<Field>* fields,
                                TSStructuralFieldRole role,
                                TSNode node,
                                TSNode item,
                                TSNode list,
                                int ordinal = 0) {
        if (!ts_node_is_null(node)) {
            appendRange(fields, role,
                        nodeStartChar(node), nodeEndChar(node),
                        item, list, ordinal);
        }
    };

    const std::function<void(TSNode,
                             std::initializer_list<const char*>,
                             QList<TSNode>*)> collectDescendants =
        [&](TSNode node,
            std::initializer_list<const char*> types,
            QList<TSNode>* output) {
            if (ts_node_is_null(node) || !output)
                return;
            const uint32_t count = ts_node_named_child_count(node);
            for (uint32_t index = 0; index < count; ++index) {
                const TSNode child = ts_node_named_child(node, index);
                if (matchesAny(child, types))
                    output->append(child);
                collectDescendants(child, types, output);
            }
        };
    const auto firstDescendant = [&](TSNode node,
                                     std::initializer_list<const char*> types) {
        QList<TSNode> matches;
        collectDescendants(node, types, &matches);
        return matches.isEmpty() ? TSNode{} : matches.constFirst();
    };
    const auto directChild = [&](TSNode node,
                                 std::initializer_list<const char*> types) {
        for (const TSNode child : directNamedChildrenOf(node)) {
            if (matchesAny(child, types))
                return child;
        }
        return TSNode{};
    };

    const auto associationFields = [&](TSNode item,
                                       TSNode list,
                                       TSStructuralFieldRole formalRole,
                                       TSStructuralFieldRole actualRole) {
        QList<Field> result;
        const bool named = nodeTypeIs(item, "named_port_connection")
            || nodeTypeIs(item, "named_parameter_assignment");
        if (named) {
            TSNode formal = nodeTypeIs(item, "named_port_connection")
                ? childByField(item, "port_name") : TSNode{};
            if (ts_node_is_null(formal))
                formal = firstIdentifierChild(item);
            appendNode(&result, formalRole, formal, item, list);
        }
        TSNode actual = nodeTypeIs(item, "named_port_connection")
            ? childByField(item, "connection") : TSNode{};
        if (ts_node_is_null(actual))
            actual = expressionChildForAssociation(item, named);
        appendNode(&result, actualRole, actual, item, list);
        return result;
    };

    const auto declarationFields = [&](TSNode item,
                                       TSNode list,
                                       TSNode declaration) {
        QList<Field> result;
        if (ts_node_is_null(item) || ts_node_has_error(item))
            return result;

        if (nodeTypeIs(item, "ansi_port_declaration")) {
            TSNode header = directChild(
                item, {"variable_port_header", "net_port_header",
                       "interface_port_header"});
            TSNode directionNode = firstDescendant(
                header, {"port_direction"});
            appendNode(&result,
                       TSStructuralFieldRole::DeclarationDirection,
                       directionNode, item, list);

            QList<TSNode> packed;
            collectDescendants(header, {"packed_dimension"}, &packed);
            int typeStart = nodeStartChar(header);
            if (!ts_node_is_null(directionNode))
                typeStart = nodeEndChar(directionNode);
            int typeEnd = packed.isEmpty()
                ? nodeEndChar(header) : nodeStartChar(packed.constFirst());
            appendRange(&result,
                        TSStructuralFieldRole::DeclarationType,
                        typeStart, typeEnd, item, list);
            for (int index = 0; index < packed.size(); ++index) {
                appendNode(&result,
                           TSStructuralFieldRole::PackedDimension,
                           packed.at(index), item, list, index);
            }

            TSNode name = childByField(item, "port_name");
            appendNode(&result, TSStructuralFieldRole::Name,
                       name, item, list);
            QList<TSNode> unpacked;
            collectDescendants(
                item,
                {"unpacked_dimension", "unsized_dimension",
                 "associative_dimension", "queue_dimension"},
                &unpacked);
            int unpackedOrdinal = 0;
            for (const TSNode dimension : std::as_const(unpacked)) {
                if (!ts_node_is_null(name)
                    && nodeStartChar(dimension) >= nodeEndChar(name)) {
                    appendNode(&result,
                               TSStructuralFieldRole::UnpackedDimension,
                               dimension, item, list, unpackedOrdinal++);
                }
            }
            for (const TSNode child : directNamedChildrenOf(item)) {
                if (!ts_node_is_null(name)
                    && nodeStartChar(child) >= nodeEndChar(name)
                    && matchesAny(child,
                                  {"constant_expression", "expression"})) {
                    appendNode(&result,
                               TSStructuralFieldRole::Initializer,
                               child, item, list);
                    break;
                }
            }
            return result;
        }

        TSNode name = childByField(item, "name");
        if (ts_node_is_null(name))
            name = firstIdentifierChild(item);

        TSNode typeNode = directChild(
            declaration,
            {"data_type_or_implicit", "data_type", "implicit_data_type",
             "net_port_type", "variable_port_type"});
        if (ts_node_is_null(typeNode)) {
            typeNode = firstDescendant(
                declaration,
                {"data_type_or_implicit", "data_type",
                 "implicit_data_type"});
        }
        QList<TSNode> packed;
        collectDescendants(typeNode, {"packed_dimension"}, &packed);
        if (!ts_node_is_null(typeNode)) {
            const int typeEnd = packed.isEmpty()
                ? nodeEndChar(typeNode)
                : nodeStartChar(packed.constFirst());
            appendRange(&result,
                        TSStructuralFieldRole::DeclarationType,
                        nodeStartChar(typeNode), typeEnd, item, list);
        }
        for (int index = 0; index < packed.size(); ++index) {
            appendNode(&result,
                       TSStructuralFieldRole::PackedDimension,
                       packed.at(index), item, list, index);
        }
        appendNode(&result, TSStructuralFieldRole::Name,
                   name, item, list);

        QList<TSNode> unpacked;
        collectDescendants(
            item,
            {"unpacked_dimension", "unsized_dimension",
             "associative_dimension", "queue_dimension"},
            &unpacked);
        int unpackedOrdinal = 0;
        for (const TSNode dimension : std::as_const(unpacked)) {
            if (!ts_node_is_null(name)
                && nodeStartChar(dimension) >= nodeEndChar(name)) {
                appendNode(&result,
                           TSStructuralFieldRole::UnpackedDimension,
                           dimension, item, list, unpackedOrdinal++);
            }
        }
        for (const TSNode child : directNamedChildrenOf(item)) {
            if (!ts_node_is_null(name)
                && nodeStartChar(child) >= nodeEndChar(name)
                && matchesAny(child,
                              {"expression", "constant_expression",
                               "param_expression", "class_new",
                               "dynamic_array_new"})) {
                appendNode(&result,
                           TSStructuralFieldRole::Initializer,
                           child, item, list);
                break;
            }
        }
        return result;
    };

    const auto fieldsForList = [&](TSNode list) {
        QList<Field> fields;
        if (ts_node_is_null(list) || ts_node_has_error(list))
            return fields;
        const QList<TSNode> items = directNamedChildrenOf(list);
        for (const TSNode item : items) {
            if (commentOrStringNode(item))
                continue;
            if (nodeTypeIs(item, "ansi_port_declaration")) {
                fields.append(declarationFields(item, list, item));
            } else if (nodeTypeIs(item, "named_port_connection")
                       || nodeTypeIs(item, "ordered_port_connection")) {
                fields.append(associationFields(
                    item, list,
                    TSStructuralFieldRole::PortFormal,
                    TSStructuralFieldRole::PortActual));
            } else if (nodeTypeIs(item, "named_parameter_assignment")
                       || nodeTypeIs(item, "ordered_parameter_assignment")) {
                fields.append(associationFields(
                    item, list,
                    TSStructuralFieldRole::ParameterFormal,
                    TSStructuralFieldRole::ParameterActual));
            } else if (nodeTypeIs(item, "variable_decl_assignment")
                       || nodeTypeIs(item, "net_decl_assignment")
                       || nodeTypeIs(item, "param_assignment")) {
                fields.append(declarationFields(
                    item, list, ts_node_parent(list)));
            } else if (nodeTypeIs(list, "list_of_arguments")) {
                appendNode(&fields, TSStructuralFieldRole::CallArgument,
                           item, item, list);
            } else if (nodeTypeIs(item,
                                  "parameter_port_declaration")) {
                TSNode assignments = firstDescendant(
                    item, {"list_of_param_assignments"});
                if (!ts_node_is_null(assignments)) {
                    for (const TSNode assignment
                         : directNamedChildrenOf(assignments)) {
                        fields.append(declarationFields(
                            assignment, list, item));
                    }
                }
            }
        }
        return fields;
    };

    TSNode instantiation{};
    TSNode declaration{};
    TSNode ternary{};
    TSNode conditional{};
    TSNode argumentList{};
    TSNode current = leaf;
    while (!ts_node_is_null(current)) {
        if (ts_node_has_error(current))
            return {};
        if (ts_node_is_null(instantiation)
            && nodeTypeIs(current, "module_instantiation")) {
            instantiation = current;
        }
        if (ts_node_is_null(declaration)
            && matchesAny(current,
                          {"ansi_port_declaration", "data_declaration",
                           "net_declaration", "parameter_port_declaration",
                           "parameter_declaration",
                           "local_parameter_declaration"})) {
            declaration = current;
        }
        if (ts_node_is_null(ternary)
            && nodeTypeIs(current, "conditional_expression")) {
            ternary = current;
        }
        if (ts_node_is_null(conditional)
            && nodeTypeIs(current, "conditional_statement")) {
            conditional = current;
        }
        if (ts_node_is_null(argumentList)
            && nodeTypeIs(current, "list_of_arguments")) {
            argumentList = current;
        }
        current = ts_node_parent(current);
    }

    QList<Field> fields;
    if (!ts_node_is_null(instantiation)) {
        const TSNode moduleType =
            childByField(instantiation, "instance_type");
        appendNode(&fields, TSStructuralFieldRole::ModuleType,
                   moduleType, instantiation, instantiation);

        const TSNode parameterValue = directChild(
            instantiation, {"parameter_value_assignment"});
        const TSNode parameterList = directChild(
            parameterValue, {"list_of_parameter_value_assignments"});
        if (!ts_node_is_null(parameterList))
            fields.append(fieldsForList(parameterList));

        TSNode hierarchy{};
        for (const TSNode candidate
             : directNamedChildrenOfType(instantiation,
                                         "hierarchical_instance")) {
            if (nodeContainsChar(candidate,
                                 qMin(cursor, m_text.size() - 1))) {
                hierarchy = candidate;
                break;
            }
            if (ts_node_is_null(hierarchy))
                hierarchy = candidate;
        }
        const TSNode instanceNameContainer = directChild(
            hierarchy, {"name_of_instance"});
        appendNode(&fields, TSStructuralFieldRole::InstanceName,
                   firstIdentifierChild(instanceNameContainer),
                   hierarchy, instantiation);
        const TSNode ports = directChild(
            hierarchy, {"list_of_port_connections"});
        if (!ts_node_is_null(ports))
            fields.append(fieldsForList(ports));
    } else if (!ts_node_is_null(ternary)) {
        QList<TSNode> children;
        for (const TSNode child : directNamedChildrenOf(ternary)) {
            if (nodeTypeIs(child, "attribute_instance"))
                continue;
            children.append(child);
        }
        if (children.size() >= 3) {
            appendNode(&fields, TSStructuralFieldRole::TernaryCondition,
                       children.at(0), ternary, ternary);
            appendNode(&fields, TSStructuralFieldRole::TernaryTrue,
                       children.at(1), ternary, ternary);
            appendNode(&fields, TSStructuralFieldRole::TernaryFalse,
                       children.at(2), ternary, ternary);
        }
    } else if (!ts_node_is_null(argumentList)) {
        fields = fieldsForList(argumentList);
    } else if (!ts_node_is_null(declaration)) {
        if (nodeTypeIs(declaration, "ansi_port_declaration")) {
            const TSNode list = ts_node_parent(declaration);
            fields = declarationFields(declaration, list, declaration);
        } else {
            TSNode list = firstDescendant(
                declaration,
                {"list_of_variable_decl_assignments",
                 "list_of_net_decl_assignments",
                 "list_of_param_assignments"});
            if (!ts_node_is_null(list)) {
                const QList<Field> listFields = fieldsForList(list);
                TSNode selectedItem{};
                for (const TSNode item : directNamedChildrenOf(list)) {
                    if (nodeContainsChar(item,
                                         qMin(cursor,
                                              m_text.size() - 1))) {
                        selectedItem = item;
                        break;
                    }
                }
                if (ts_node_is_null(selectedItem)
                    && !directNamedChildrenOf(list).isEmpty()) {
                    selectedItem = directNamedChildrenOf(list).constFirst();
                }
                for (const Field& field : listFields) {
                    if (field.target.itemStartChar
                            == nodeStartChar(selectedItem)
                        && field.target.itemEndChar
                            == nodeEndChar(selectedItem)) {
                        fields.append(field);
                    }
                }
            } else if (nodeTypeIs(declaration,
                                  "parameter_port_declaration")) {
                fields = fieldsForList(ts_node_parent(declaration));
            }
        }
    } else if (!ts_node_is_null(conditional)) {
        const TSNode predicate = directChild(
            conditional, {"cond_predicate"});
        TSNode operandRoot = predicate;
        while (!ts_node_is_null(operandRoot)) {
            QList<TSNode> children;
            for (const TSNode child : directNamedChildrenOf(operandRoot)) {
                if (!nodeTypeIs(child, "attribute_instance"))
                    children.append(child);
            }
            if (children.size() != 1)
                break;
            operandRoot = children.constFirst();
        }
        QList<TSNode> operands;
        for (const TSNode child : directNamedChildrenOf(operandRoot)) {
            if (!nodeTypeIs(child, "attribute_instance"))
                operands.append(child);
        }
        if (operands.isEmpty() && !ts_node_is_null(operandRoot))
            operands.append(operandRoot);
        for (int index = 0; index < operands.size(); ++index) {
            appendNode(&fields, TSStructuralFieldRole::ConditionOperand,
                       operands.at(index), conditional, conditional, index);
        }
    }

    if (fields.isEmpty())
        return {};
    std::sort(fields.begin(), fields.end(),
              [](const Field& left, const Field& right) {
        if (left.target.startChar != right.target.startChar)
            return left.target.startChar < right.target.startChar;
        if (left.target.endChar != right.target.endChar)
            return left.target.endChar < right.target.endChar;
        return static_cast<int>(left.target.role)
            < static_cast<int>(right.target.role);
    });

    int originIndex = -1;
    int closestDistance = std::numeric_limits<int>::max();
    for (int index = 0; index < fields.size(); ++index) {
        const Field& field = fields.at(index);
        if (cursor >= field.target.startChar
            && cursor <= field.target.endChar) {
            originIndex = index;
            break;
        }
        const int distance = cursor < field.target.startChar
            ? field.target.startChar - cursor
            : cursor - field.target.endChar;
        if (distance < closestDistance) {
            closestDistance = distance;
            originIndex = index;
        }
    }
    if (originIndex < 0)
        return {};

    if (direction == TSStructuralNavigationDirection::PreviousField
        || direction == TSStructuralNavigationDirection::NextField) {
        const int targetIndex = originIndex
            + (direction == TSStructuralNavigationDirection::PreviousField
                   ? -1 : 1);
        return targetIndex >= 0 && targetIndex < fields.size()
            ? fields.at(targetIndex).target
            : TSStructuralNavigationTarget{};
    }

    const Field origin = fields.at(originIndex);
    TSNode list = namedNodeAt(
        m_tree,
        qMin(origin.target.listStartChar, m_text.size() - 1),
        m_text.size());
    while (!ts_node_is_null(list)
           && (nodeStartChar(list) != origin.target.listStartChar
               || nodeEndChar(list) != origin.target.listEndChar)) {
        list = ts_node_parent(list);
    }
    if (ts_node_is_null(list))
        return {};

    QList<Field> vertical = fieldsForList(list);
    std::sort(vertical.begin(), vertical.end(),
              [](const Field& left, const Field& right) {
        if (left.target.itemStartChar != right.target.itemStartChar)
            return left.target.itemStartChar < right.target.itemStartChar;
        if (left.target.startChar != right.target.startChar)
            return left.target.startChar < right.target.startChar;
        return left.ordinal < right.ordinal;
    });

    QList<Field> matching;
    for (const Field& field : std::as_const(vertical)) {
        if (field.target.role == origin.target.role
            && field.ordinal == origin.ordinal) {
            matching.append(field);
        }
    }
    int currentItem = -1;
    for (int index = 0; index < matching.size(); ++index) {
        if (matching.at(index).target.itemStartChar
                == origin.target.itemStartChar
            && matching.at(index).target.itemEndChar
                == origin.target.itemEndChar) {
            currentItem = index;
            break;
        }
    }
    if (currentItem < 0)
        return {};
    const int targetItem = currentItem
        + (direction == TSStructuralNavigationDirection::PreviousItem
               ? -1 : 1);
    return targetItem >= 0 && targetItem < matching.size()
        ? matching.at(targetItem).target
        : TSStructuralNavigationTarget{};
}

TSInstantiationTarget TSDocument::instantiationAt(int charOffset) const
{
    TSInstantiationTarget target;
    if (m_text.isEmpty() || charOffset < 0 || charOffset > m_text.size())
        return target;

    const int probe = qMin(charOffset, m_text.size() - 1);
    TSNode node = namedNodeAt(m_tree, probe, m_text.size());
    TSNode instantiation = ancestorOfType(node, "module_instantiation");
    if (ts_node_is_null(instantiation)
        && charOffset > 0) {
        node = namedNodeAt(m_tree, charOffset - 1, m_text.size());
        instantiation = ancestorOfType(node, "module_instantiation");
    }
    if (ts_node_is_null(instantiation)
        || ts_node_has_error(instantiation)) {
        return target;
    }

    const TSNode typeNode =
        childByField(instantiation, "instance_type");
    const QList<TSNode> hierarchies =
        directNamedChildrenOfType(instantiation,
                                  "hierarchical_instance");
    if (ts_node_is_null(typeNode) || hierarchies.isEmpty())
        return target;

    TSNode hierarchy{};
    for (const TSNode candidate : hierarchies) {
        if (nodeContainsChar(candidate, probe)) {
            if (!ts_node_is_null(hierarchy))
                return target;
            hierarchy = candidate;
        }
    }
    if (ts_node_is_null(hierarchy)) {
        if (hierarchies.size() != 1)
            return target;
        hierarchy = hierarchies.first();
    }
    if (ts_node_has_error(hierarchy))
        return target;

    const TSNode nameOfInstance =
        firstDirectNamedChildOfType(hierarchy,
                                    "name_of_instance");
    const TSNode nameNode = firstIdentifierChild(nameOfInstance);
    if (ts_node_is_null(nameNode))
        return target;

    target.startChar = nodeStartChar(instantiation);
    target.endChar = nodeEndChar(instantiation);
    target.moduleType = nodeText(m_text, typeNode);
    target.instanceName = nodeText(m_text, nameNode);

    const TSNode parameterValue =
        firstDirectNamedChildOfType(
            instantiation, "parameter_value_assignment");
    if (!ts_node_is_null(parameterValue)) {
        const TSNode assignments =
            firstDirectNamedChildOfType(
                parameterValue,
                "list_of_parameter_value_assignments");
        if (ts_node_is_null(assignments)
            || !collectAssociationSlots(
                assignments,
                m_text,
                "named_parameter_assignment",
                "ordered_parameter_assignment",
                &target.parameterActuals)) {
            return {};
        }
    }

    const TSNode connections =
        firstDirectNamedChildOfType(hierarchy,
                                    "list_of_port_connections");
    if (!ts_node_is_null(connections)
        && !collectAssociationSlots(
            connections,
            m_text,
            "named_port_connection",
            "ordered_port_connection",
            &target.portActuals)) {
        return {};
    }

    return target.ok() ? target : TSInstantiationTarget{};
}

TSNamedPortConnectionTarget TSDocument::namedPortConnectionTarget(
    int charOffset,
    const QString& formalName) const
{
    TSNamedPortConnectionTarget target;
    if (m_text.isEmpty() || formalName.isEmpty()
        || charOffset < 0 || charOffset > m_text.size()) {
        target.status = TSNamedPortConnectionStatus::NoInstantiation;
        return target;
    }

    const int probe = qMin(charOffset, m_text.size() - 1);
    TSNode node = namedNodeAt(m_tree, probe, m_text.size());
    TSNode instantiation = ancestorOfType(node, "module_instantiation");
    if (ts_node_is_null(instantiation) && charOffset > 0) {
        node = namedNodeAt(m_tree, charOffset - 1, m_text.size());
        instantiation = ancestorOfType(node, "module_instantiation");
    }
    if (ts_node_is_null(instantiation)
        || ts_node_has_error(instantiation)) {
        target.status = TSNamedPortConnectionStatus::NoInstantiation;
        return target;
    }

    const TSNode typeNode = childByField(instantiation, "instance_type");
    const QList<TSNode> hierarchies =
        directNamedChildrenOfType(instantiation,
                                  "hierarchical_instance");
    if (ts_node_is_null(typeNode) || hierarchies.isEmpty()) {
        target.status = TSNamedPortConnectionStatus::NoInstantiation;
        return target;
    }

    TSNode hierarchy{};
    for (const TSNode candidate : hierarchies) {
        if (!nodeContainsChar(candidate, probe))
            continue;
        if (!ts_node_is_null(hierarchy)) {
            target.status =
                TSNamedPortConnectionStatus::NoClearConnectionPoint;
            return target;
        }
        hierarchy = candidate;
    }
    if (ts_node_is_null(hierarchy)) {
        if (hierarchies.size() != 1) {
            target.status =
                TSNamedPortConnectionStatus::NoClearConnectionPoint;
            return target;
        }
        hierarchy = hierarchies.constFirst();
    }
    if (ts_node_has_error(hierarchy)) {
        target.status =
            TSNamedPortConnectionStatus::NoClearConnectionPoint;
        return target;
    }

    const TSNode nameOfInstance =
        firstDirectNamedChildOfType(hierarchy, "name_of_instance");
    const TSNode nameNode = firstIdentifierChild(nameOfInstance);
    const TSNode connections =
        firstDirectNamedChildOfType(hierarchy,
                                    "list_of_port_connections");
    if (ts_node_is_null(nameNode) || ts_node_is_null(connections)
        || ts_node_has_error(connections)) {
        target.status =
            TSNamedPortConnectionStatus::NoClearConnectionPoint;
        return target;
    }

    target.moduleType = nodeText(m_text, typeNode);
    target.instanceName = nodeText(m_text, nameNode);

    QList<TSExpressionSlot> connectionSlots;
    if (!collectAssociationSlots(connections,
                                 m_text,
                                 "named_port_connection",
                                 "ordered_port_connection",
                                 &connectionSlots)) {
        target.status =
            TSNamedPortConnectionStatus::NoClearConnectionPoint;
        return target;
    }
    for (const TSExpressionSlot& slot : connectionSlots) {
        if (slot.name.isEmpty()) {
            target.status =
                TSNamedPortConnectionStatus::PositionalConnections;
            return target;
        }
        if (slot.name != formalName)
            continue;
        target.status =
            TSNamedPortConnectionStatus::AlreadyConnected;
        target.existingActual =
            m_text.mid(slot.startChar,
                       slot.endChar - slot.startChar);
        return target;
    }

    const int closeParen = closingParenStart(hierarchy, m_text);
    if (closeParen < 0) {
        target.status =
            TSNamedPortConnectionStatus::NoClearConnectionPoint;
        return target;
    }

    QList<TSNode> associations;
    for (const TSNode child : directNamedChildrenOf(connections)) {
        if (nodeTypeIs(child, "named_port_connection")) {
            associations.append(child);
            continue;
        }
        if (commentOrStringNode(child))
            continue;
        target.status =
            TSNamedPortConnectionStatus::NoClearConnectionPoint;
        return target;
    }

    const int closeLine =
        static_cast<int>(ts_node_start_point(
            [&]() {
                const uint32_t count = ts_node_child_count(hierarchy);
                for (uint32_t index = 0; index < count; ++index) {
                    const TSNode child = ts_node_child(hierarchy, index);
                    if (nodeStartChar(child) == closeParen)
                        return child;
                }
                return TSNode{};
            }()).row);
    if (closeLine < 0) {
        target.status =
            TSNamedPortConnectionStatus::NoClearConnectionPoint;
        return target;
    }

    if (!associations.isEmpty()) {
        const TSNode last = associations.constLast();
        bool hasComma = false;
        const uint32_t count = ts_node_child_count(connections);
        for (uint32_t index = 0; index < count; ++index) {
            const TSNode child = ts_node_child(connections, index);
            if (nodeStartChar(child) < nodeEndChar(last)
                || nodeStartChar(child) >= closeParen) {
                continue;
            }
            if (nodeText(m_text, child) == QStringLiteral(",")) {
                hasComma = true;
                break;
            }
        }

        const int lastLine = nodeLastLine(last);
        if (lastLine < closeLine) {
            target.needsTrailingComma = !hasComma;
            target.trailingCommaInsertChar =
                target.needsTrailingComma ? nodeEndChar(last) : -1;
            target.insertChar = lineStartChar(m_text, closeLine);
            target.prefix = lineIndentAt(m_text, lastLine);
            target.suffix =
                target.insertChar >= 2
                        && m_text.mid(target.insertChar - 2, 2)
                               == QStringLiteral("\r\n")
                    ? QStringLiteral("\r\n")
                    : QStringLiteral("\n");
        } else {
            target.insertChar = closeParen;
            target.prefix =
                hasComma ? QStringLiteral(" ")
                         : QStringLiteral(", ");
        }
    } else {
        const int openLine =
            static_cast<int>(ts_node_start_point(connections).row);
        if (openLine < closeLine) {
            target.insertChar = lineStartChar(m_text, closeLine);
            target.prefix =
                lineIndentAt(m_text, closeLine)
                + QStringLiteral("    ");
            target.suffix =
                target.insertChar >= 2
                        && m_text.mid(target.insertChar - 2, 2)
                               == QStringLiteral("\r\n")
                    ? QStringLiteral("\r\n")
                    : QStringLiteral("\n");
        } else {
            target.insertChar = closeParen;
        }
    }

    target.status = target.insertChar >= 0
        ? TSNamedPortConnectionStatus::Ok
        : TSNamedPortConnectionStatus::NoClearConnectionPoint;
    return target;
}

TSUndefinedSignalContext TSDocument::undefinedSignalContextAt(
    int charOffset) const
{
    TSUndefinedSignalContext context;
    context.identifier = identifierAt(charOffset);
    if (!context.identifier.ok())
        return {};

    TSNode identifier = namedNodeAt(
        m_tree, context.identifier.startChar, m_text.size());
    while (!ts_node_is_null(identifier)
           && !identifierNode(identifier)) {
        identifier = ts_node_parent(identifier);
    }
    if (ts_node_is_null(identifier))
        return {};

    TSNode node = identifier;
    while (!ts_node_is_null(node)) {
        if (commentOrStringNode(node)
            || nodeTypeIs(node, "text_macro_usage")
            || nodeTypeIs(node, "text_macro_definition")) {
            return {};
        }
        if (nodeTypeIs(node, "named_port_connection")) {
            const TSNode connection =
                childByField(node, "connection");
            if (ts_node_is_null(connection)
                || !nodeContainsChar(
                    connection, context.identifier.startChar)) {
                return {};
            }
            TSNode formal = childByField(node, "port_name");
            if (ts_node_is_null(formal))
                formal = firstIdentifierChild(node);
            if (ts_node_is_null(formal))
                return {};

            context.kind =
                TSUndefinedSignalContextKind::NamedPortActual;
            context.formalName = nodeText(m_text, formal);
            context.formalStartChar = nodeStartChar(formal);
            context.instantiation =
                instantiationAt(context.identifier.startChar);
            return context.instantiation.ok()
                ? context : TSUndefinedSignalContext{};
        }

        if (nodeTypeIs(node, "nonblocking_assignment")
            || nodeTypeIs(node, "blocking_assignment")
            || nodeTypeIs(node, "operator_assignment")
            || nodeTypeIs(node, "variable_assignment")) {
            const TSNode lvalue = firstLvalueChild(node);
            if (!ts_node_is_null(lvalue)
                && nodeContainsChar(
                    lvalue, context.identifier.startChar)) {
                context.kind =
                    TSUndefinedSignalContextKind::
                        ProceduralAssignmentLhs;
                return context;
            }
            return {};
        }
        node = ts_node_parent(node);
    }
    return {};
}

namespace {
// Extract the declared name from a *_declaration node: try a "name" field, else find a "*_header"
// child and take its name field or first simple_identifier.
template <typename Text>
QString declarationName(const Text& text, TSNode declNode)
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

template <typename Text>
QString nodeText(const Text& text, TSNode node)
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

bool identifierNode(TSNode node)
{
    return nodeTypeIs(node, "simple_identifier")
        || nodeTypeIs(node, "escaped_identifier");
}

bool commentOrStringNode(TSNode node)
{
    return nodeTypeIs(node, "one_line_comment")
        || nodeTypeIs(node, "block_comment")
        || nodeTypeIs(node, "string_literal");
}

TSNode namedNodeAt(const TSTree* tree,
                   int charOffset,
                   int textSize)
{
    if (!tree || textSize <= 0)
        return {};
    const int position = qBound(0, charOffset, textSize - 1);
    const uint32_t byte = static_cast<uint32_t>(position) * 2u;
    return ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(tree), byte, byte);
}

QList<TSNode> directNamedChildrenOf(TSNode node)
{
    QList<TSNode> children;
    const uint32_t count = ts_node_named_child_count(node);
    children.reserve(static_cast<int>(count));
    for (uint32_t index = 0; index < count; ++index)
        children.append(ts_node_named_child(node, index));
    return children;
}

QList<TSNode> directNamedChildrenOfType(TSNode node,
                                        const char* expected)
{
    QList<TSNode> children;
    for (const TSNode child : directNamedChildrenOf(node)) {
        if (nodeTypeIs(child, expected))
            children.append(child);
    }
    return children;
}

TSNode firstDirectNamedChildOfType(TSNode node,
                                   const char* expected)
{
    for (const TSNode child : directNamedChildrenOf(node)) {
        if (nodeTypeIs(child, expected))
            return child;
    }
    return {};
}

TSNode childByField(TSNode node, const char* field)
{
    return ts_node_child_by_field_name(
        node, field, static_cast<uint32_t>(std::strlen(field)));
}

bool nodeContainsChar(TSNode node, int position)
{
    return !ts_node_is_null(node)
        && position >= nodeStartChar(node)
        && position <= nodeEndChar(node);
}

TSNode firstIdentifierChild(TSNode node)
{
    for (const TSNode child : directNamedChildrenOf(node)) {
        if (identifierNode(child))
            return child;
    }
    return {};
}

TSNode expressionChildForAssociation(TSNode association,
                                     bool namedAssociation)
{
    if (namedAssociation) {
        TSNode expression = childByField(association, "connection");
        if (!ts_node_is_null(expression))
            return expression;
    }

    for (const TSNode child : directNamedChildrenOf(association)) {
        if (commentOrStringNode(child)
            || nodeTypeIs(child, "attribute_instance")
            || (namedAssociation && identifierNode(child))) {
            continue;
        }
        return child;
    }
    return {};
}

template <typename Text>
int closingParenStart(TSNode node, const Text& text)
{
    int result = -1;
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode child = ts_node_child(node, index);
        if (nodeText(text, child) == QStringLiteral(")"))
            result = nodeStartChar(child);
    }
    return result;
}

template <typename Text>
bool collectAssociationSlots(TSNode container,
                             const Text& text,
                             const char* namedType,
                             const char* orderedType,
                             QList<TSExpressionSlot>* outputSlots)
{
    if (!outputSlots || ts_node_is_null(container)
        || ts_node_has_error(container)) {
        return false;
    }

    bool sawNamed = false;
    bool sawOrdered = false;
    for (const TSNode child : directNamedChildrenOf(container)) {
        const bool named = nodeTypeIs(child, namedType);
        const bool ordered = nodeTypeIs(child, orderedType);
        if (!named && !ordered) {
            if (commentOrStringNode(child))
                continue;
            return false;
        }
        sawNamed = sawNamed || named;
        sawOrdered = sawOrdered || ordered;
        if (sawNamed && sawOrdered)
            return false;

        TSExpressionSlot slot;
        const TSNode expression =
            expressionChildForAssociation(child, named);
        if (!ts_node_is_null(expression)) {
            slot.startChar = nodeStartChar(expression);
            slot.endChar = nodeEndChar(expression);
        } else if (named) {
            const int close = closingParenStart(child, text);
            if (close < 0)
                return false;
            slot.startChar = close;
            slot.endChar = close;
        } else {
            return false;
        }

        if (named) {
            TSNode formal = childByField(child, "port_name");
            if (ts_node_is_null(formal))
                formal = firstIdentifierChild(child);
            slot.name = nodeText(text, formal);
        }
        if (!slot.ok())
            return false;
        outputSlots->append(slot);
    }
    return true;
}

TSNode firstLvalueChild(TSNode assignment)
{
    for (const TSNode child : directNamedChildrenOf(assignment)) {
        if (nodeTypeIs(child, "net_lvalue")
            || nodeTypeIs(child, "variable_lvalue")
            || nodeTypeIs(child, "nonrange_variable_lvalue")
            || nodeTypeIs(child, "hierarchical_identifier")) {
            return child;
        }
        if (nodeTypeIs(child, "operator_assignment"))
            return firstLvalueChild(child);
    }
    return {};
}

template <typename Text>
QString leadingIdentifierAt(const Text& text, int start, int end)
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

template <typename Text>
int firstNonSpaceChar(const Text& text, int start, int end)
{
    int pos = qBound(0, start, text.size());
    const int limit = qBound(pos, end, text.size());
    while (pos < limit && text.at(pos).isSpace())
        ++pos;
    return pos;
}

template <typename Text>
int lastNonSpaceChar(const Text& text, int start, int end)
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

template <typename Text>
int lineStartChar(const Text& text, int line)
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

template <typename Text>
int lineEndChar(const Text& text, int line)
{
    const int start = lineStartChar(text, line);
    for (int i = start; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            return i;
    }
    return text.size();
}

template <typename Text>
QString lineIndentAt(const Text& text, int line)
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

template <typename Text>
bool lineIsBlank(const Text& text, int line)
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

template <typename Text>
int previousNonBlankLine(const Text& text, int line)
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

template <typename Text>
PortParenRange findPortListParens(const Text& text, TSNode header)
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
            || nodeTypeIs(child, "attribute_instance")
            || nodeTypeIs(child, "one_line_comment")
            || nodeTypeIs(child, "block_comment")) {
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

template <typename Text>
bool trailingCommaStateOnPortLine(const Text& text,
                                  TSNode portDecl,
                                  int portLine,
                                  bool* hasComma,
                                  int* commaInsertChar)
{
    if (!hasComma || !commaInsertChar)
        return false;

    const int lineEnd = lineEndChar(text, portLine);
    int structuralEnd = lineEnd;
    for (TSNode sibling = ts_node_next_named_sibling(portDecl);
         !ts_node_is_null(sibling);
         sibling = ts_node_next_named_sibling(sibling)) {
        if (!nodeTypeIs(sibling, "one_line_comment")
            && !nodeTypeIs(sibling, "block_comment")) {
            break;
        }
        const int commentLine =
            static_cast<int>(ts_node_start_point(sibling).row);
        if (commentLine > portLine)
            break;
        if (commentLine == portLine)
            structuralEnd =
                std::min(structuralEnd, nodeStartChar(sibling));
    }

    const int lineStart = lineStartChar(text, portLine);
    int scanStart =
        qBound(lineStart, nodeEndChar(portDecl), structuralEnd);
    int nodeTrimmedEnd = scanStart;
    while (nodeTrimmedEnd > lineStart
           && text.at(nodeTrimmedEnd - 1).isSpace()) {
        --nodeTrimmedEnd;
    }
    if (nodeTrimmedEnd > lineStart
        && text.at(nodeTrimmedEnd - 1) == QLatin1Char(',')) {
        int pos = scanStart;
        while (pos < structuralEnd && text.at(pos).isSpace())
            ++pos;
        if (pos != structuralEnd)
            return false;
        *hasComma = true;
        *commaInsertChar = -1;
        return true;
    }

    int pos = scanStart;
    while (pos < structuralEnd && text.at(pos).isSpace())
        ++pos;

    if (pos >= structuralEnd) {
        int trimmedEnd = structuralEnd;
        while (trimmedEnd > scanStart && text.at(trimmedEnd - 1).isSpace())
            --trimmedEnd;
        *hasComma = false;
        *commaInsertChar = trimmedEnd;
        return true;
    }

    if (text.at(pos) != QLatin1Char(','))
        return false;

    ++pos;
    while (pos < structuralEnd && text.at(pos).isSpace())
        ++pos;
    if (pos != structuralEnd)
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
            directNamedChildOfType(
                node, "list_of_net_decl_assignments"));
    }
    if (nodeTypeIs(node, "data_declaration")) {
        return !ts_node_is_null(
            directNamedChildOfType(
                node, "list_of_variable_decl_assignments"));
    }
    return false;
}

template <typename Text>
QStringList signalDeclarationNames(const Text& text,
                                   TSNode declaration)
{
    QStringList names;
    TSNode declaratorList{};
    const char* declaratorType = nullptr;
    if (nodeTypeIs(declaration, "data_declaration")) {
        declaratorList = directNamedChildOfType(
            declaration,
            "list_of_variable_decl_assignments");
        declaratorType = "variable_decl_assignment";
    } else if (nodeTypeIs(declaration, "net_declaration")) {
        declaratorList = directNamedChildOfType(
            declaration,
            "list_of_net_decl_assignments");
        declaratorType = "net_decl_assignment";
    }
    if (ts_node_is_null(declaratorList) || !declaratorType)
        return names;

    const uint32_t count =
        ts_node_named_child_count(declaratorList);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode declarator =
            ts_node_named_child(declaratorList, index);
        if (!nodeTypeIs(declarator, declaratorType))
            continue;
        TSNode name = childByField(declarator, "name");
        if (ts_node_is_null(name))
            name = firstIdentifierChild(declarator);
        if (!identifierNode(name))
            return {};
        names.append(nodeText(text, name));
    }
    return names;
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

bool isStandaloneCommentNode(TSNode node)
{
    return nodeTypeIs(node, "one_line_comment")
        || nodeTypeIs(node, "block_comment");
}

TSNode moduleItemPayload(TSNode node)
{
    if (!nodeTypeIs(node, "module_item"))
        return node;

    TSNode payload{};
    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode child = ts_node_named_child(node, index);
        if (nodeTypeIs(child, "attribute_instance")
            || isStandaloneCommentNode(child)) {
            continue;
        }
        if (!ts_node_is_null(payload))
            return node;
        payload = child;
    }
    return ts_node_is_null(payload) ? node : payload;
}

TSNode moduleItemSignalDeclaration(TSNode node)
{
    if (isInternalSignalDeclaration(node))
        return node;
    if (!nodeTypeIs(node, "module_item"))
        return {};

    TSNode declaration{};
    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t index = 0; index < count; ++index) {
        const TSNode child = ts_node_named_child(node, index);
        if (!isInternalSignalDeclaration(child))
            continue;
        if (!ts_node_is_null(declaration))
            return {};
        declaration = child;
    }
    return declaration;
}

bool isSignalOrganizationPreamble(TSNode node)
{
    if (isStandaloneCommentNode(node))
        return true;
    const TSNode payload = moduleItemPayload(node);
    if (isStandaloneCommentNode(payload))
        return true;
    const bool wrappedDataPreamble =
        nodeTypeIs(payload, "data_declaration")
        && (nodeTypeIs(
                directNamedChildOfType(payload, "type_declaration"),
                "type_declaration")
            || nodeTypeIs(
                directNamedChildOfType(
                    payload, "package_import_declaration"),
                "package_import_declaration")
            || nodeTypeIs(
                directNamedChildOfType(payload, "nettype_declaration"),
                "nettype_declaration"));
    return isParameterLikeDeclaration(payload)
        || wrappedDataPreamble
        || nodeTypeIs(payload, "package_import_declaration")
        || nodeTypeIs(payload, "type_declaration")
        || nodeTypeIs(payload, "nettype_declaration")
        || nodeTypeIs(payload, "genvar_declaration")
        || nodeTypeIs(payload, "port_declaration")
        || nodeTypeIs(payload, "timeunits_declaration")
        || nodeTypeIs(payload, "let_declaration")
        || nodeTypeIs(payload, "attribute_instance")
        || nodeTypeIs(payload, "pragma")
        || nodeTypeIs(payload, "include_compiler_directive")
        || nodeTypeIs(payload, "line_compiler_directive")
        || nodeTypeIs(payload, "file_or_line_compiler_directive")
        || nodeTypeIs(payload, "default_nettype_compiler_directive");
}

bool isSignalOrganizationBarrier(TSNode node)
{
    const TSNode payload = moduleItemPayload(node);
    if (nodeTypeIs(payload, "conditional_compilation_directive"))
        return true;
    if (isSignalOrganizationPreamble(node)
        && !isStandaloneCommentNode(node)
        && !isStandaloneCommentNode(payload)) {
        return true;
    }
    const char* type = ts_node_type(payload);
    return type
        && QString::fromLatin1(type).endsWith(
            QStringLiteral("_compiler_directive"));
}

TSNode directModuleHeader(TSNode module)
{
    TSNode header = directNamedChildOfType(module, "module_ansi_header");
    if (!ts_node_is_null(header))
        return header;
    return directNamedChildOfType(module, "module_nonansi_header");
}

template <typename Text>
int endmoduleLine(TSNode module, const Text& text)
{
    const uint32_t childCount = ts_node_child_count(module);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(module, i);
        if (nodeText(text, child) == QStringLiteral("endmodule"))
            return static_cast<int>(ts_node_start_point(child).row);
    }
    return -1;
}

template <typename Text>
int endpackageLine(TSNode package, const Text& text)
{
    const uint32_t childCount = ts_node_child_count(package);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(package, i);
        if (nodeText(text, child) == QStringLiteral("endpackage"))
            return static_cast<int>(ts_node_start_point(child).row);
    }
    return -1;
}

template <typename Text>
int seqBlockEndLine(TSNode block, const Text& text)
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

template <typename Text>
SignalInsertAnchor anchorAfterNode(const Text& text, TSNode node)
{
    SignalInsertAnchor anchor;
    anchor.valid = !ts_node_is_null(node);
    anchor.insertAfterLine = true;
    anchor.line = nodeLastLine(node);
    anchor.indent = lineIndentAt(text, anchor.line);
    return anchor;
}

template <typename Text>
SignalInsertAnchor anchorBeforeLine(const Text& text,
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

template <typename Text>
bool customFoldMarkerForNode(const Text& text,
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

template <typename Text>
void collectCustomFoldMarkers(const Text& text,
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

template <typename Text>
void collectFoldNodes(const Text& text,
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
QString TSDocument::enclosingPackageName(int charOffset) const
{
    const uint32_t b = static_cast<uint32_t>(qMax(0, charOffset)) * 2u;
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(m_tree), b, b);
    while (!ts_node_is_null(node)) {
        const char* type = ts_node_type(node);
        if (type && std::strcmp(type, "package_declaration") == 0)
            return declarationName(m_text, node);
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

TSSignalDeclarationOrganizationPlan
TSDocument::signalDeclarationOrganizationPlan(int charOffset) const
{
    TSSignalDeclarationOrganizationPlan plan;
    if (!m_tree || m_text.isEmpty())
        return plan;

    const int bounded = qBound(0, charOffset, m_text.size() - 1);
    TSNode node = namedNodeAt(m_tree, bounded, m_text.size());
    TSNode module = ancestorOfType(node, "module_declaration");
    if (ts_node_is_null(module) && bounded > 0) {
        node = namedNodeAt(m_tree, bounded - 1, m_text.size());
        module = ancestorOfType(node, "module_declaration");
    }
    if (ts_node_is_null(module))
        return plan;
    if (ts_node_has_error(module)) {
        plan.status =
            TSSignalDeclarationOrganizationStatus::ModuleHasSyntaxError;
        return plan;
    }

    const TSNode header = directModuleHeader(module);
    if (ts_node_is_null(header)) {
        plan.status =
            TSSignalDeclarationOrganizationStatus::UnsafeLayout;
        plan.failureReason = QStringLiteral(
            "The current module header cannot be identified safely.");
        return plan;
    }

    struct ModuleEntry {
        TSNode node{};
        TSNode signalDeclaration{};
        bool comment = false;
    };
    QList<ModuleEntry> entries;
    bool seenHeader = false;
    const uint32_t childCount = ts_node_named_child_count(module);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child = ts_node_named_child(module, index);
        if (ts_node_eq(child, header)) {
            seenHeader = true;
            continue;
        }
        if (!seenHeader || nodeEndChar(child) <= nodeEndChar(header))
            continue;
        ModuleEntry entry;
        entry.node = child;
        entry.signalDeclaration = moduleItemSignalDeclaration(child);
        entry.comment = isStandaloneCommentNode(child);
        entries.append(entry);
    }

    QList<int> signalIndexes;
    int bodyIndex = entries.size();
    for (int index = 0; index < entries.size(); ++index) {
        const ModuleEntry& entry = entries.at(index);
        if (!ts_node_is_null(entry.signalDeclaration)) {
            signalIndexes.append(index);
            continue;
        }
        if (bodyIndex == entries.size()
            && !isSignalOrganizationPreamble(entry.node)) {
            bodyIndex = index;
        }
    }
    plan.declarationCount = signalIndexes.size();
    if (signalIndexes.isEmpty()) {
        plan.status =
            TSSignalDeclarationOrganizationStatus::NoSignalDeclarations;
        return plan;
    }

    bool hasLateSignal = false;
    for (const int signalIndex : signalIndexes) {
        if (signalIndex >= bodyIndex) {
            hasLateSignal = true;
            break;
        }
    }
    if (hasLateSignal) {
        for (int index = bodyIndex; index < entries.size(); ++index) {
            if (isSignalOrganizationBarrier(entries.at(index).node)) {
                const TSNode payload =
                    moduleItemPayload(entries.at(index).node);
                plan.status =
                    TSSignalDeclarationOrganizationStatus::UnsafeLayout;
                plan.failureReason = QStringLiteral(
                    "A %1 boundary prevents safe declaration movement.")
                    .arg(QString::fromLatin1(ts_node_type(payload)));
                return plan;
            }
        }
    }

    struct TextRange {
        int start = -1;
        int end = -1;
    };
    QList<TextRange> declarationRanges;
    declarationRanges.reserve(signalIndexes.size());
    QSet<int> claimedCommentIndexes;
    for (const int signalIndex : signalIndexes) {
        const ModuleEntry& signal = entries.at(signalIndex);
        int startLine = nodeStartLine(signal.node);
        int endLine = nodeLastLine(signal.node);

        for (int commentIndex = signalIndex - 1;
             commentIndex >= 0
             && entries.at(commentIndex).comment
             && !claimedCommentIndexes.contains(commentIndex);
             --commentIndex) {
            const TSNode comment = entries.at(commentIndex).node;
            if (nodeLastLine(comment) + 1 != startLine)
                break;
            const int commentLineStart =
                lineStartChar(m_text, nodeStartLine(comment));
            if (!m_text.mid(commentLineStart,
                            nodeStartChar(comment) - commentLineStart)
                     .trimmed()
                     .isEmpty()) {
                break;
            }
            startLine = nodeStartLine(comment);
            claimedCommentIndexes.insert(commentIndex);
        }

        if (signalIndex + 1 < entries.size()
            && entries.at(signalIndex + 1).comment
            && !claimedCommentIndexes.contains(signalIndex + 1)) {
            const TSNode comment = entries.at(signalIndex + 1).node;
            if (nodeStartLine(comment) == endLine
                && m_text.mid(nodeEndChar(signal.node),
                              nodeStartChar(comment)
                                  - nodeEndChar(signal.node))
                       .trimmed()
                       .isEmpty()) {
                endLine = nodeLastLine(comment);
                claimedCommentIndexes.insert(signalIndex + 1);
            }
        }

        const int declarationLineStart =
            lineStartChar(m_text, nodeStartLine(signal.node));
        const int start = lineStartChar(m_text, startLine);
        int end = lineEndChar(m_text, endLine);
        if (end < m_text.size()
            && m_text.at(end) == QLatin1Char('\n')) {
            ++end;
        }
        if (!m_text.mid(declarationLineStart,
                        nodeStartChar(signal.node)
                            - declarationLineStart)
                 .trimmed()
                 .isEmpty()) {
            plan.status =
                TSSignalDeclarationOrganizationStatus::UnsafeLayout;
            plan.failureReason = QStringLiteral(
                "A signal declaration shares its line with unrelated syntax.");
            return plan;
        }
        declarationRanges.append(TextRange{start, end});
    }

    std::sort(declarationRanges.begin(), declarationRanges.end(),
              [](const TextRange& lhs, const TextRange& rhs) {
                  return lhs.start < rhs.start;
              });
    for (int index = 1; index < declarationRanges.size(); ++index) {
        if (declarationRanges.at(index - 1).end
            > declarationRanges.at(index).start) {
            plan.status =
                TSSignalDeclarationOrganizationStatus::UnsafeLayout;
            plan.failureReason = QStringLiteral(
                "Attached declaration ranges overlap.");
            return plan;
        }
    }

    int anchorIndex = bodyIndex;
    while (anchorIndex > 0
           && entries.at(anchorIndex - 1).comment
           && !claimedCommentIndexes.contains(anchorIndex - 1)) {
        --anchorIndex;
    }
    int anchor = -1;
    if (anchorIndex < entries.size()) {
        anchor = lineStartChar(
            m_text, nodeStartLine(entries.at(anchorIndex).node));
    } else {
        const int endLine = endmoduleLine(module, m_text);
        if (endLine < 0) {
            plan.status =
                TSSignalDeclarationOrganizationStatus::UnsafeLayout;
            plan.failureReason = QStringLiteral(
                "The end of the current module cannot be identified safely.");
            return plan;
        }
        anchor = lineStartChar(m_text, endLine);
    }

    const int replaceStart = qMin(anchor, declarationRanges.first().start);
    const int replaceEnd = qMax(anchor, declarationRanges.last().end);
    QString replacement = m_text.mid(replaceStart,
                                     replaceEnd - replaceStart);
    QString declarationBlock;
    int removedBeforeAnchor = 0;
    for (const TextRange& range : declarationRanges) {
        declarationBlock += m_text.mid(range.start,
                                       range.end - range.start);
        if (range.end <= anchor)
            removedBeforeAnchor += range.end - range.start;
    }
    for (int index = declarationRanges.size() - 1; index >= 0; --index) {
        const TextRange& range = declarationRanges.at(index);
        replacement.remove(range.start - replaceStart,
                           range.end - range.start);
    }
    const int adjustedAnchor =
        anchor - replaceStart - removedBeforeAnchor;
    if (adjustedAnchor < 0 || adjustedAnchor > replacement.size()) {
        plan.status =
            TSSignalDeclarationOrganizationStatus::UnsafeLayout;
        plan.failureReason = QStringLiteral(
            "A stable declaration insertion point cannot be derived.");
        return plan;
    }
    replacement.insert(adjustedAnchor, declarationBlock);

    const QString original = m_text.mid(replaceStart,
                                        replaceEnd - replaceStart);
    if (replacement == original) {
        plan.status =
            TSSignalDeclarationOrganizationStatus::AlreadyOrganized;
        return plan;
    }

    plan.status = TSSignalDeclarationOrganizationStatus::Ok;
    plan.replaceStartChar = replaceStart;
    plan.replaceEndChar = replaceEnd;
    plan.replacementText = replacement;
    plan.movedDeclarationCount = declarationRanges.size();
    return plan;
}

TSSignalInsertTarget TSDocument::blockSignalInsertTarget(
    int charOffset) const
{
    TSSignalInsertTarget target;
    if (!m_tree || m_text.isEmpty())
        return target;

    const int bounded =
        qBound(0, charOffset, m_text.size() - 1);
    TSNode node = namedNodeAt(m_tree, bounded, m_text.size());
    TSNode block = ancestorOfType(node, "seq_block");
    if (ts_node_is_null(block) || ts_node_has_error(block))
        return target;

    TSNode lastDeclaration{};
    TSNode firstStatement{};
    const uint32_t childCount =
        ts_node_named_child_count(block);
    for (uint32_t index = 0; index < childCount; ++index) {
        const TSNode child =
            ts_node_named_child(block, index);
        if (nodeTypeIs(child, "block_item_declaration")) {
            lastDeclaration = child;
            continue;
        }
        if (nodeTypeIs(child, "statement_or_null")) {
            firstStatement = child;
            break;
        }
    }

    SignalInsertAnchor anchor;
    if (!ts_node_is_null(lastDeclaration)) {
        if (!ts_node_is_null(firstStatement)
            && nodeStartLine(firstStatement)
                   <= nodeLastLine(lastDeclaration)) {
            return target;
        }
        anchor = anchorAfterNode(m_text, lastDeclaration);
    } else if (!ts_node_is_null(firstStatement)) {
        const int statementLine = nodeStartLine(firstStatement);
        const int beginLine = nodeStartLine(block);
        if (statementLine <= beginLine)
            return target;
        anchor = anchorBeforeLine(
            m_text,
            statementLine,
            lineIndentAt(m_text, statementLine));
    } else {
        const int endLine = seqBlockEndLine(block, m_text);
        const int beginLine = nodeStartLine(block);
        if (endLine <= beginLine)
            return target;
        anchor = anchorBeforeLine(
            m_text,
            endLine,
            lineIndentAt(m_text, endLine)
                + QStringLiteral("    "));
    }

    if (!anchor.valid || anchor.line < 0)
        return target;
    if (anchor.insertAfterLine) {
        const int insertChar =
            lineEndChar(m_text, anchor.line);
        target.status = TSSignalInsertStatus::Ok;
        target.insertChar = insertChar;
        target.insertText =
            QLatin1Char('\n') + anchor.indent;
        target.caretCharAfterEdit =
            insertChar + 1 + anchor.indent.size();
        return target;
    }

    const int insertChar =
        lineStartChar(m_text, anchor.line);
    target.status = TSSignalInsertStatus::Ok;
    target.insertChar = insertChar;
    target.insertText =
        anchor.indent + QLatin1Char('\n');
    target.caretCharAfterEdit =
        insertChar + anchor.indent.size();
    return target;
}

bool TSDocument::isSingleSignalDeclaration(
    const QString& declaration,
    const QString& identifier,
    bool blockLocal)
{
    const QString candidate = declaration.trimmed();
    if (candidate.isEmpty() || identifier.isEmpty())
        return false;

    const QString prefix = blockLocal
        ? QStringLiteral(
              "module __zs_declare_check;\n"
              "  initial begin\n"
              "    ")
        : QStringLiteral(
              "module __zs_declare_check;\n"
              "  ");
    const QString suffix = blockLocal
        ? QStringLiteral(
              "\n"
              "  end\n"
              "endmodule\n")
        : QStringLiteral(
              "\n"
              "endmodule\n");

    TSDocument parsed;
    parsed.setText(prefix + candidate + suffix);
    if (parsed.hasError())
        return false;

    const TSNode root = parsed.rootNode();
    const TSNode module =
        directNamedChildOfType(root, "module_declaration");
    if (ts_node_is_null(module))
        return false;

    TSNode signalDeclaration{};
    if (blockLocal) {
        TSNode block{};
        QList<TSNode> pending{module};
        while (!pending.isEmpty()) {
            const TSNode current = pending.takeLast();
            if (nodeTypeIs(current, "seq_block")) {
                if (!ts_node_is_null(block))
                    return false;
                block = current;
                continue;
            }
            const uint32_t count =
                ts_node_named_child_count(current);
            for (uint32_t index = 0; index < count; ++index) {
                pending.append(
                    ts_node_named_child(current, index));
            }
        }
        if (ts_node_is_null(block))
            return false;

        int blockItems = 0;
        int statements = 0;
        const uint32_t count =
            ts_node_named_child_count(block);
        for (uint32_t index = 0; index < count; ++index) {
            const TSNode child =
                ts_node_named_child(block, index);
            if (nodeTypeIs(child, "block_item_declaration")) {
                ++blockItems;
                const TSNode declarationNode =
                    directNamedChildOfType(
                        child, "data_declaration");
                if (!ts_node_is_null(declarationNode))
                    signalDeclaration = declarationNode;
            } else if (nodeTypeIs(child, "statement_or_null")) {
                ++statements;
            }
        }
        if (blockItems != 1 || statements != 0)
            return false;
    } else {
        int bodyMembers = 0;
        const uint32_t count =
            ts_node_named_child_count(module);
        for (uint32_t index = 0; index < count; ++index) {
            const TSNode child =
                ts_node_named_child(module, index);
            if (nodeTypeIs(child, "module_ansi_header")
                || nodeTypeIs(child, "module_nonansi_header")) {
                continue;
            }
            const TSNode member =
                effectiveModuleMemberNode(child);
            ++bodyMembers;
            if (isInternalSignalDeclaration(member))
                signalDeclaration = member;
        }
        if (bodyMembers != 1)
            return false;
    }

    if (ts_node_is_null(signalDeclaration)
        || !isInternalSignalDeclaration(signalDeclaration)) {
        return false;
    }
    return signalDeclarationNames(
               parsed.text(), signalDeclaration)
        == QStringList{identifier};
}

TSSignalInsertTarget TSDocument::sourceBridgeInsertTarget(
    int charOffset) const
{
    TSSignalInsertTarget target;

    const int boundedCharOffset =
        qBound(0, charOffset, m_text.size());
    const uint32_t byte =
        static_cast<uint32_t>(boundedCharOffset) * 2u;
    TSNode node =
        ts_node_named_descendant_for_byte_range(
            ts_tree_root_node(m_tree), byte, byte);
    TSNode module = ancestorOfType(node, "module_declaration");
    if (ts_node_is_null(module)) {
        target.status = TSSignalInsertStatus::NoCurrentModule;
        return target;
    }
    if (ts_node_has_error(module))
        return target;

    TSNode declaration{};
    const uint32_t childCount =
        ts_node_named_child_count(module);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child =
            effectiveModuleMemberNode(
                ts_node_named_child(module, i));
        if (!isInternalSignalDeclaration(child))
            continue;
        if (boundedCharOffset >= nodeStartChar(child)
            && boundedCharOffset < nodeEndChar(child)) {
            declaration = child;
            break;
        }
    }
    if (ts_node_is_null(declaration))
        return target;

    const SignalInsertAnchor anchor =
        anchorAfterNode(m_text, declaration);
    const int moduleEndLine = endmoduleLine(module, m_text);
    if (!anchor.valid || anchor.line < 0
        || moduleEndLine < 0
        || anchor.line >= moduleEndLine) {
        return target;
    }

    const int insertChar = lineEndChar(m_text, anchor.line);
    target.status = TSSignalInsertStatus::Ok;
    target.insertChar = insertChar;
    target.insertText = QLatin1Char('\n') + anchor.indent;
    target.caretCharAfterEdit =
        insertChar + 1 + anchor.indent.size();
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
TSNode lastLeafEndingAtOrBefore(TSNode node, int charOffset)
{
    if (ts_node_is_null(node)
        || nodeStartChar(node) >= charOffset) {
        return {};
    }

    const uint32_t childCount = ts_node_child_count(node);
    if (childCount == 0) {
        return nodeEndChar(node) <= charOffset
            ? node : TSNode{};
    }
    uint32_t low = 0;
    uint32_t high = childCount;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (nodeStartChar(ts_node_child(node, middle)) < charOffset)
            low = middle + 1;
        else
            high = middle;
    }
    for (uint32_t reverse = low; reverse > 0; --reverse) {
        const TSNode child = ts_node_child(node, reverse - 1);
        if (nodeStartChar(child) >= charOffset)
            continue;
        const TSNode leaf =
            lastLeafEndingAtOrBefore(child, charOffset);
        if (!ts_node_is_null(leaf))
            return leaf;
    }
    return {};
}

TSNode leafContainingChar(TSNode root, int charOffset)
{
    if (ts_node_is_null(root)
        || charOffset < nodeStartChar(root)
        || charOffset >= nodeEndChar(root)) {
        return {};
    }
    const uint32_t byte =
        static_cast<uint32_t>(charOffset) * 2u;
    TSNode node =
        ts_node_descendant_for_byte_range(root, byte, byte);
    while (!ts_node_is_null(node)
           && ts_node_child_count(node) > 0) {
        const uint32_t count = ts_node_child_count(node);
        uint32_t low = 0;
        uint32_t high = count;
        while (low < high) {
            const uint32_t middle = low + (high - low) / 2;
            if (nodeEndChar(ts_node_child(node, middle)) <= charOffset)
                low = middle + 1;
            else
                high = middle;
        }
        TSNode containing{};
        if (low < count) {
            const TSNode candidate = ts_node_child(node, low);
            if (charOffset >= nodeStartChar(candidate)
                && charOffset < nodeEndChar(candidate)) {
                containing = candidate;
            }
        }
        if (ts_node_is_null(containing))
            break;
        node = containing;
    }
    return node;
}

template <typename Text>
bool horizontalWhitespaceOnlyBetween(const Text& text,
                                     int start,
                                     int end)
{
    const int boundedStart = qBound(0, start, text.size());
    const int boundedEnd = qBound(boundedStart, end, text.size());
    for (int index = boundedStart; index < boundedEnd; ++index) {
        if (text.at(index) != QLatin1Char(' ')
            && text.at(index) != QLatin1Char('\t')) {
            return false;
        }
    }
    return true;
}

template <typename Text>
QString leadingWhitespaceForLineAt(const Text& text, int charOffset)
{
    const int bounded = qBound(0, charOffset, text.size());
    const int newline =
        bounded > 0
            ? text.lastIndexOf(QLatin1Char('\n'), bounded - 1)
            : -1;
    const int start = newline < 0 ? 0 : newline + 1;
    int end = start;
    while (end < text.size()
           && (text.at(end) == QLatin1Char(' ')
               || text.at(end) == QLatin1Char('\t'))) {
        ++end;
    }
    return text.mid(start, end - start);
}

bool nodeTypeIn(TSNode node,
                std::initializer_list<const char*> types)
{
    if (ts_node_is_null(node))
        return false;
    for (const char* type : types) {
        if (nodeTypeIs(node, type))
            return true;
    }
    return false;
}

TSNode ancestorOfAnyType(
    TSNode node,
    std::initializer_list<const char*> types)
{
    while (!ts_node_is_null(node)) {
        if (nodeTypeIn(node, types))
            return node;
        node = ts_node_parent(node);
    }
    return {};
}

template <typename Text>
void keywordBoundaryLeaves(
    TSNode node,
    const Text& text,
    const QSet<QString>& openingKeywords,
    const QString& closingKeyword,
    TSNode* opening,
    TSNode* closing)
{
    if (ts_node_is_null(node))
        return;
    const uint32_t count = ts_node_child_count(node);
    if (count == 0) {
        const QString token = nodeText(text, node);
        if (opening && ts_node_is_null(*opening)
            && openingKeywords.contains(token)) {
            *opening = node;
        }
        if (closing && token == closingKeyword)
            *closing = node;
        return;
    }
    for (uint32_t index = 0; index < count; ++index) {
        keywordBoundaryLeaves(ts_node_child(node, index),
                              text,
                              openingKeywords,
                              closingKeyword,
                              opening,
                              closing);
    }
}

bool keywordSymbolName(const char* name)
{
    if (!name || !*name)
        return false;
    const unsigned char first =
        static_cast<unsigned char>(*name);
    if (!std::isalpha(first) && first != '_')
        return false;
    for (const char* current = name; *current; ++current) {
        const unsigned char value =
            static_cast<unsigned char>(*current);
        if (!std::isalnum(value)
            && value != '_'
            && value != '$') {
            return false;
        }
    }
    return true;
}
} // namespace

TSStructuralNewlineTarget TSDocument::structuralNewlineTarget(
    int cursorChar,
    int indentWidth) const
{
    TSStructuralNewlineTarget target;
    const int boundedCursor =
        qBound(0, cursorChar, m_text.size());
    const QString baseIndent =
        leadingWhitespaceForLineAt(m_text, boundedCursor);
    const int previousNewline =
        boundedCursor > 0
            ? m_text.lastIndexOf(QLatin1Char('\n'), boundedCursor - 1)
            : -1;
    const int lineStart = previousNewline < 0 ? 0 : previousNewline + 1;
    const QString cursorIndent = baseIndent.left(
        qBound(0, boundedCursor - lineStart, baseIndent.size()));
    const QString childIndent =
        baseIndent
        + QString(qMax(1, indentWidth), QLatin1Char(' '));

    target.insertionText =
        QStringLiteral("\n") + cursorIndent;
    target.caretOffset = target.insertionText.size();
    if (m_text.isEmpty() || boundedCursor <= 0)
        return target;

    const TSNode previous =
        lastLeafEndingAtOrBefore(
            ts_tree_root_node(m_tree), boundedCursor);
    if (ts_node_is_null(previous)
        || !horizontalWhitespaceOnlyBetween(m_text,
                                            nodeEndChar(previous),
                                            boundedCursor)
        || commentOrStringNode(previous)) {
        return target;
    }

    const QString previousText = nodeText(m_text, previous);
    if (previousText == QStringLiteral("begin")) {
        TSNode block =
            ancestorOfType(previous, "seq_block");
        TSNode opening{};
        TSNode closing{};
        if (!ts_node_is_null(block)) {
            keywordBoundaryLeaves(
                block,
                m_text,
                {QStringLiteral("begin")},
                QStringLiteral("end"),
                &opening,
                &closing);
            if (!ts_node_is_null(opening)
                && nodeStartChar(opening)
                       != nodeStartChar(previous)) {
                block = {};
                opening = {};
                closing = {};
            }
        }

        const bool hasClosing =
            !ts_node_is_null(closing)
            && nodeStartChar(closing) >= boundedCursor;
        const bool closingOnCurrentLine =
            hasClosing
            && ts_node_start_point(closing).row
                   == ts_node_end_point(previous).row;
        target.insertionText =
            QStringLiteral("\n") + childIndent;
        target.caretOffset = target.insertionText.size();
        if (closingOnCurrentLine) {
            target.insertionText +=
                QStringLiteral("\n") + baseIndent;
        } else if (!hasClosing) {
            target.insertionText +=
                QStringLiteral("\n")
                + baseIndent
                + QStringLiteral("end");
            target.insertedClosingKeyword = true;
        }
        return target;
    }

    const TSNode caseContainer =
        ancestorOfAnyType(
            previous,
            {"case_statement",
             "case_generate_construct",
             "randcase_statement"});
    const TSNode caseItem =
        ancestorOfAnyType(
            previous,
            {"case_item",
             "case_generate_item",
             "randcase_item"});
    const bool opensCaseBody =
        !ts_node_is_null(caseContainer)
        && ts_node_end_point(previous).row
               == static_cast<uint32_t>(
                   m_text.left(boundedCursor)
                       .count(QLatin1Char('\n')))
        && (previousText == QStringLiteral(")")
            || previousText == QStringLiteral("randcase"));
    const bool opensCaseItem =
        !ts_node_is_null(caseItem)
        && previousText == QStringLiteral(":");
    if (opensCaseBody || opensCaseItem) {
        target.insertionText =
            QStringLiteral("\n") + childIndent;
        target.caretOffset = target.insertionText.size();
    }
    return target;
}

TSKeywordCompletionTarget TSDocument::uniqueKeywordCompletionAt(
    int cursorChar,
    int minimumPrefixLength) const
{
    TSKeywordCompletionTarget target;
    const int boundedCursor =
        qBound(0, cursorChar, m_text.size());
    if (boundedCursor <= 0 || minimumPrefixLength < 1)
        return target;

    int lexicalStart = boundedCursor;
    while (lexicalStart > 0) {
        const QChar value = m_text.at(lexicalStart - 1);
        if (!value.isLetterOrNumber()
            && value != QLatin1Char('_')
            && value != QLatin1Char('$')) {
            break;
        }
        --lexicalStart;
    }
    const int lexicalLength = boundedCursor - lexicalStart;
    if (lexicalLength < minimumPrefixLength
        || !m_text.at(lexicalStart).isLetter()
        || (lexicalStart > 0
            && m_text.at(lexicalStart - 1)
                   == QLatin1Char('\\'))
        || isCommentAt(boundedCursor - 1)) {
        return target;
    }

    TSIdentifierTarget prefixTarget =
        identifierAt(boundedCursor - 1);
    TSNode prefixNode{};
    if (!prefixTarget.ok()
        || prefixTarget.endChar != boundedCursor) {
        const TSNode partialKeyword =
            lastLeafEndingAtOrBefore(
                ts_tree_root_node(m_tree),
                boundedCursor);
        if (ts_node_is_null(partialKeyword)
            || nodeEndChar(partialKeyword)
                   != boundedCursor
            || commentOrStringNode(partialKeyword)) {
            return target;
        }
        prefixTarget.startChar =
            nodeStartChar(partialKeyword);
        prefixTarget.endChar =
            nodeEndChar(partialKeyword);
        prefixTarget.text =
            nodeText(m_text, partialKeyword);
        prefixNode = partialKeyword;
    } else {
        prefixNode = leafContainingChar(
            ts_tree_root_node(m_tree),
            boundedCursor - 1);
    }
    if (prefixTarget.text.size() < minimumPrefixLength
        || !prefixTarget.text.at(0).isLetter()
        || prefixTarget.text.startsWith(QLatin1Char('\\'))) {
        return target;
    }
    for (int index = 0;
         index < prefixTarget.text.size();
         ++index) {
        const QChar value = prefixTarget.text.at(index);
        const bool valid =
            value.isLetter()
            || value == QLatin1Char('_')
            || (index > 0
                && (value.isDigit()
                    || value == QLatin1Char('$')));
        if (!valid)
            return target;
    }
    if (prefixTarget.startChar < 0
        || prefixTarget.endChar != boundedCursor) {
        return target;
    }

    const TSLanguage* language =
        tree_sitter_systemverilog();
    if (!language || ts_node_is_null(prefixNode))
        return target;

    QSet<QString> candidates;
    TSLookaheadIterator* lookahead =
        ts_lookahead_iterator_new(
            language,
            ts_node_parse_state(prefixNode));
    while (lookahead
           && ts_lookahead_iterator_next(lookahead)) {
        const TSSymbol symbol =
            ts_lookahead_iterator_current_symbol(
                lookahead);
        if (ts_language_symbol_type(language, symbol)
                != TSSymbolTypeAnonymous) {
            continue;
        }
        const char* name =
            ts_lookahead_iterator_current_symbol_name(
                lookahead);
        if (!keywordSymbolName(name))
            continue;
        const QString keyword =
            QString::fromLatin1(name);
        if (keyword.size() > prefixTarget.text.size()
            && keyword.startsWith(prefixTarget.text,
                                  Qt::CaseSensitive)) {
            candidates.insert(keyword);
        }
    }
    ts_lookahead_iterator_delete(lookahead);
    if (candidates.size() != 1)
        return target;
    const QString selectedKeyword =
        *candidates.constBegin();

    target.startChar = prefixTarget.startChar;
    target.endChar = prefixTarget.endChar;
    target.prefix = prefixTarget.text;
    target.keyword = selectedKeyword;
    target.suffix =
        selectedKeyword.mid(prefixTarget.text.size());
    return target;
}

TSKeywordPairTarget TSDocument::matchingKeywordPairAt(
    int cursorChar) const
{
    TSKeywordPairTarget target;
    if (m_text.isEmpty())
        return target;

    int probe = qBound(0, cursorChar, m_text.size());
    if (probe == m_text.size()
        || !m_text.at(probe).isLetter()) {
        --probe;
    }
    if (probe < 0 || !m_text.at(probe).isLetter())
        return target;
    int localStart = probe;
    while (localStart > 0
           && m_text.at(localStart - 1).isLetter()) {
        --localStart;
    }
    int localEnd = probe + 1;
    while (localEnd < m_text.size()
           && m_text.at(localEnd).isLetter()) {
        ++localEnd;
    }
    const QString localWord =
        m_text.mid(localStart, localEnd - localStart);
    if (localWord != QStringLiteral("begin")
        && localWord != QStringLiteral("end")
        && localWord != QStringLiteral("case")
        && localWord != QStringLiteral("casez")
        && localWord != QStringLiteral("casex")
        && localWord != QStringLiteral("randcase")
        && localWord != QStringLiteral("endcase")) {
        return target;
    }
    const TSNode root = ts_tree_root_node(m_tree);
    const auto keywordLeafAt =
        [this, root](int probe) {
            const TSNode leaf = leafContainingChar(
                root,
                qBound(0, probe, m_text.size() - 1));
            if (ts_node_is_null(leaf))
                return TSNode{};
            const QString text = nodeText(m_text, leaf);
            if (text == QStringLiteral("begin")
                || text == QStringLiteral("end")
                || text == QStringLiteral("case")
                || text == QStringLiteral("casez")
                || text == QStringLiteral("casex")
                || text == QStringLiteral("randcase")
                || text == QStringLiteral("endcase")) {
                return leaf;
            }
            return TSNode{};
        };
    TSNode leaf = keywordLeafAt(cursorChar);
    if (ts_node_is_null(leaf) && cursorChar > 0)
        leaf = keywordLeafAt(cursorChar - 1);
    if (ts_node_is_null(leaf))
        return target;

    const QString selected = nodeText(m_text, leaf);
    TSNode container{};
    QSet<QString> openingKeywords;
    QString closingKeyword;
    if (selected == QStringLiteral("begin")
        || selected == QStringLiteral("end")) {
        container = ancestorOfType(leaf, "seq_block");
        openingKeywords.insert(QStringLiteral("begin"));
        closingKeyword = QStringLiteral("end");
    } else if (selected == QStringLiteral("case")
               || selected == QStringLiteral("casez")
               || selected == QStringLiteral("casex")
               || selected == QStringLiteral("randcase")
               || selected == QStringLiteral("endcase")) {
        container = ancestorOfAnyType(
            leaf,
            {"case_statement",
             "case_generate_construct",
             "randcase_statement"});
        openingKeywords = {
            QStringLiteral("case"),
            QStringLiteral("casez"),
            QStringLiteral("casex"),
            QStringLiteral("randcase"),
        };
        closingKeyword = QStringLiteral("endcase");
    } else {
        return target;
    }
    if (ts_node_is_null(container)
        || ts_node_has_error(container)) {
        return target;
    }

    TSNode opening{};
    TSNode closing{};
    keywordBoundaryLeaves(container,
                          m_text,
                          openingKeywords,
                          closingKeyword,
                          &opening,
                          &closing);
    if (ts_node_is_null(opening)
        || ts_node_is_null(closing)) {
        return target;
    }

    target.openingStartChar = nodeStartChar(opening);
    target.openingEndChar = nodeEndChar(opening);
    target.closingStartChar = nodeStartChar(closing);
    target.closingEndChar = nodeEndChar(closing);
    target.openingKeyword = nodeText(m_text, opening);
    target.closingKeyword = nodeText(m_text, closing);
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
    uint32_t low = 0;
    uint32_t high = childCount;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        const TSNode child = ts_node_child(node, middle);
        if (ts_node_end_byte(child) <= startByte)
            low = middle + 1;
        else
            high = middle;
    }

    for (uint32_t index = low; index < childCount; ++index) {
        const TSNode child = ts_node_child(node, index);
        if (ts_node_start_byte(child) >= endByte)
            break;
        collectSpans(child,
                     startByte,
                     endByte,
                     blockStartChar,
                     out);
    }
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
