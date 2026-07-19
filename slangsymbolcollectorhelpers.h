#ifndef SLANGSYMBOLCOLLECTORHELPERS_H
#define SLANGSYMBOLCOLLECTORHELPERS_H

#include "semanticindex.h"

#include <slang/ast/symbols/PortSymbols.h>
#include <slang/text/SourceLocation.h>

#include <QList>
#include <QString>

namespace slang {
class SourceManager;
}

namespace slang::ast {
class EnumType;
class Scope;
class Symbol;
class Type;
}

namespace slang_symbols::detail {

// Semantic identities must use the physical source buffer path. Slang's
// display name can collapse an included buffer to a basename, which is not
// unique across a workspace. Synthetic buffers fall back to the display name.
QString sourceIdentityFileName(
    const slang::SourceManager* sourceManager,
    slang::SourceLocation location);

struct QTextDocumentSourcePosition {
    QString fileName;
    int position = -1;
    int line = 0;
    int column = 0;

    bool isValid() const
    {
        return !fileName.isEmpty() && position >= 0;
    }
};

// Slang offsets count UTF-8 bytes in the original source buffer. Qt document
// offsets count UTF-16 code units after CRLF has been normalized to LF.
QTextDocumentSourcePosition qTextDocumentSourcePosition(
    const slang::SourceManager* sourceManager,
    slang::SourceLocation location);

void resetQTextDocumentSourcePositionCache(
    const slang::SourceManager* sourceManager);

bool fillSymbolRecord(const slang::SourceManager* sm,
                      const slang::ast::Symbol& sym,
                      SemanticSymbolRecord& out,
                      QString* outOwnerName);

void applyCollectorKind(
    SemanticSymbolRecord* record,
    SymbolTaxonomy::CollectorKind rawKind);

void finalizeCollectedSymbolRecords(QList<SemanticSymbolRecord>* records);

void emitEnumValueRecords(const slang::SourceManager* sm,
                          const slang::ast::EnumType& et,
                          const QString& scopeKey,
                          QList<SemanticSymbolRecord>& outList);

void emitStructMemberRecords(const slang::SourceManager* sm,
                             const slang::ast::Scope& structScope,
                             const QString& scopeKey,
                             QList<SemanticSymbolRecord>& outList);

SymbolTaxonomy::CollectorKind variableOrNetCollectorKind(
    const slang::ast::Type& type);

SymbolTaxonomy::CollectorKind portDirectionCollectorKind(
    slang::ast::ArgumentDirection dir);

} // namespace slang_symbols::detail

#endif // SLANGSYMBOLCOLLECTORHELPERS_H
