#ifndef SLANGCOMPILATIONCOLLECTORS_H
#define SLANGCOMPILATIONCOLLECTORS_H

#include "slangmanager.h"
namespace slang::ast { class Compilation; }
namespace slang::syntax { class SyntaxTree; }

namespace slang_collectors {
QList<SemanticDiagnostic> diagnostics(slang::ast::Compilation& compilation,
                                     slang::syntax::SyntaxTree& tree,
                                     const std::function<bool()>& cancelled);
QHash<QString, RelationshipExtractionInfo> relationships(
    slang::ast::Compilation& compilation, const std::function<bool()>& cancelled);
}

#endif
