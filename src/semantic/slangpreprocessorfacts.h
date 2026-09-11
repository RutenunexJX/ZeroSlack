#ifndef SLANGPREPROCESSORFACTS_H
#define SLANGPREPROCESSORFACTS_H

#include "semanticindex.h"

#include <QList>

namespace slang::syntax {
class SyntaxTree;
}

namespace slang_preprocessor_facts {

QList<SemanticSymbolRecord> collect(
    const slang::syntax::SyntaxTree& tree);

} // namespace slang_preprocessor_facts

#endif // SLANGPREPROCESSORFACTS_H
