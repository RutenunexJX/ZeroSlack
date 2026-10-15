#pragma once
#include "semanticindex.h"
#include <functional>
namespace slang::ast { class Compilation; }
namespace slang_symbols {
void populateSignalRoles(slang::ast::Compilation &compilation,
                         QList<SemanticSymbolRecord> &records,
                         const std::function<bool()> &cancelled);
}
