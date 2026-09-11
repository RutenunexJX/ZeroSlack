#ifndef SYMBOLANALYZERWORKSPACE_H
#define SYMBOLANALYZERWORKSPACE_H

#include "symbolanalyzer.h"

#include <functional>

namespace SymbolAnalyzerWorkspace {

WorkspaceAnalysisResult buildWorkspaceAnalysisResult(
    const QStringList& svFiles,
    const QList<SemanticSymbolRecord>& allRecords,
    const QList<EffectiveValueFact>& effectiveValueFacts,
    std::function<bool()> isCancelled,
    const QHash<QString, QString>& analyzedFileContents = {});

} // namespace SymbolAnalyzerWorkspace

#endif // SYMBOLANALYZERWORKSPACE_H
