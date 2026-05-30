#ifndef TSDOCUMENT_H
#define TSDOCUMENT_H

#include <QString>
#include <QByteArray>

extern "C" {
#include <tree_sitter/api.h>
}

// Persistent, per-document Tree-sitter model: keeps a live parse tree plus the document's UTF-8
// buffer and supports incremental re-parse on edits. This is the foundation of the "real-time
// syntactic layer" (highlighting, live outline / scope) in the Slang + Tree-sitter architecture.
//
// Threading: not thread-safe; intended to live with its editor on the UI thread. Each instance
// owns its own TSParser, so multiple documents don't contend.
class TSDocument
{
public:
    TSDocument();
    ~TSDocument();

    TSDocument(const TSDocument&) = delete;
    TSDocument& operator=(const TSDocument&) = delete;

    // Full (re)parse of the entire text from scratch.
    void setText(const QString& text);

    // Incremental edit. Caller supplies the span being replaced (byte + row/col, in the CURRENT
    // buffer before the edit) and the full new text. Uses ts_tree_edit + incremental parse so the
    // unchanged majority of the tree is reused.
    void applyEdit(uint32_t startByte, uint32_t oldEndByte, uint32_t newEndByte,
                   TSPoint startPoint, TSPoint oldEndPoint, TSPoint newEndPoint,
                   const QString& newFullText);

    TSNode rootNode() const;                 // always valid (empty doc parses to an empty tree)
    bool hasError() const;                   // tree contains ERROR / MISSING nodes (half-typed code)
    const QByteArray& utf8() const { return m_utf8; }

    // Convenience: type name of the smallest named node spanning [startByte, endByte).
    const char* namedNodeTypeAt(uint32_t byteOffset) const;

private:
    void reparse(TSTree* oldTree);

    TSParser* m_parser = nullptr;  // owned
    TSTree*   m_tree   = nullptr;  // owned
    QByteArray m_utf8;             // current document bytes (UTF-8)
};

#endif // TSDOCUMENT_H
