#ifndef SLANGSYMBOLPRESENTATION_H
#define SLANGSYMBOLPRESENTATION_H

#include "semanticindex.h"
#include "effectivevalueservice.h"

#include <QList>
#include <functional>

namespace slang::ast {
class Compilation;
}

namespace slang_symbols {

void populateSymbolPresentations(
    slang::ast::Compilation& compilation,
    QList<SemanticSymbolRecord>& records,
    QList<EffectiveValueFact>* effectiveValueFacts = nullptr,
    const std::function<bool()>& isCancelled = nullptr);

void populateSymbolDriverSummaries(
    slang::ast::Compilation& compilation,
    QList<SemanticSymbolRecord>& records,
    const std::function<bool()>& isCancelled = nullptr);

} // namespace slang_symbols

#endif // SLANGSYMBOLPRESENTATION_H
