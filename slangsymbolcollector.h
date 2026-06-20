#ifndef SLANGSYMBOLCOLLECTOR_H
#define SLANGSYMBOLCOLLECTOR_H

#include "semanticindex.h"

#include <QList>

namespace slang::ast {
class Compilation;
}

namespace slang_symbols {

void collectSymbolRecords(slang::ast::Compilation& compilation,
                          QList<SemanticSymbolRecord>& outList);

} // namespace slang_symbols

#endif // SLANGSYMBOLCOLLECTOR_H
