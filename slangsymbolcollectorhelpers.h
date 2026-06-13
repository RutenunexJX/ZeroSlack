#ifndef SLANGSYMBOLCOLLECTORHELPERS_H
#define SLANGSYMBOLCOLLECTORHELPERS_H

#include "syminfo.h"

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

bool fillSymbolInfo(const slang::SourceManager* sm,
                    const slang::ast::Symbol& sym,
                    sym_list::SymbolInfo& out,
                    QString* outModuleScope);

void emitEnumValues(const slang::SourceManager* sm,
                    const slang::ast::EnumType& et,
                    const QString& scopeKey,
                    QList<sym_list::SymbolInfo>& outList);

void emitStructMembers(const slang::SourceManager* sm,
                       const slang::ast::Scope& structScope,
                       const QString& scopeKey,
                       QList<sym_list::SymbolInfo>& outList);

sym_list::sym_type_e variableOrNetTypeToSymType(const slang::ast::Type& type);

sym_list::sym_type_e portDirectionToSymType(slang::ast::ArgumentDirection dir);

} // namespace slang_symbols::detail

#endif // SLANGSYMBOLCOLLECTORHELPERS_H
