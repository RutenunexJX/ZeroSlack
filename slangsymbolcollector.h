#ifndef SLANGSYMBOLCOLLECTOR_H
#define SLANGSYMBOLCOLLECTOR_H

#include "syminfo.h"

#include <QList>

namespace slang::ast {
class Compilation;
}

namespace slang_symbols {

void collectSymbols(slang::ast::Compilation& compilation,
                    QList<sym_list::SymbolInfo>& outList);

} // namespace slang_symbols

#endif // SLANGSYMBOLCOLLECTOR_H
