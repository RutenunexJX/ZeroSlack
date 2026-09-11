#ifndef SLANGSYMBOLCOLLECTOR_H
#define SLANGSYMBOLCOLLECTOR_H

#include "semanticindex.h"
#include "effectivevalueservice.h"

#include <QList>
#include <functional>

namespace slang::ast {
class Compilation;
}

namespace slang_symbols {

void collectSymbolRecords(slang::ast::Compilation& compilation,
                          QList<SemanticSymbolRecord>& outList,
                          std::function<bool()> isCancelled = nullptr,
                          QList<EffectiveValueFact>* effectiveValueFacts = nullptr);

} // namespace slang_symbols

#endif // SLANGSYMBOLCOLLECTOR_H
