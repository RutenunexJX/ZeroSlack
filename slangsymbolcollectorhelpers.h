#ifndef SLANGSYMBOLCOLLECTORHELPERS_H
#define SLANGSYMBOLCOLLECTORHELPERS_H

#include "semanticindex.h"

#include <slang/ast/symbols/PortSymbols.h>

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

bool fillSymbolRecord(const slang::SourceManager* sm,
                      const slang::ast::Symbol& sym,
                      SemanticSymbolRecord& out,
                      QString* outOwnerName);

void applyCollectorKind(
    SemanticSymbolRecord* record,
    SymbolTaxonomy::RawCollectorKind rawKind);

void finalizeCollectedSymbolRecords(QList<SemanticSymbolRecord>* records);

void emitEnumValueRecords(const slang::SourceManager* sm,
                          const slang::ast::EnumType& et,
                          const QString& scopeKey,
                          QList<SemanticSymbolRecord>& outList);

void emitStructMemberRecords(const slang::SourceManager* sm,
                             const slang::ast::Scope& structScope,
                             const QString& scopeKey,
                             QList<SemanticSymbolRecord>& outList);

SymbolTaxonomy::RawCollectorKind variableOrNetRawCollectorKind(
    const slang::ast::Type& type);

SymbolTaxonomy::RawCollectorKind portDirectionRawCollectorKind(
    slang::ast::ArgumentDirection dir);

} // namespace slang_symbols::detail

#endif // SLANGSYMBOLCOLLECTORHELPERS_H
