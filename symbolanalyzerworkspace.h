#ifndef SYMBOLANALYZERWORKSPACE_H
#define SYMBOLANALYZERWORKSPACE_H

#include "symbolanalyzer.h"

#include <functional>

namespace SymbolAnalyzerWorkspace {

WorkspaceAnalysisResult buildWorkspaceAnalysisResult(
    const QStringList& svFiles,
    const QList<sym_list::SymbolInfo>& allSymbols,
    std::function<bool()> isCancelled);

} // namespace SymbolAnalyzerWorkspace

#endif // SYMBOLANALYZERWORKSPACE_H
