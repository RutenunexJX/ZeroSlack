#pragma once

#include <zeroslack/semantic/semanticanalysisresult.h>

// Disposable, versioned value data. Publication and document ownership remain
// with SymbolAnalyzer; this service performs no GUI or global-index mutation.
class SemanticWorkspaceCache
{
public:
    static QString directory(const QString& workspace, bool localFallback = false);
    static bool ownsPath(const QString& workspace, const QString& path);
    static std::optional<WorkspaceAnalysisResult> restore(
        const SemanticAnalysisRequest& request,
        const QHash<QString, SemanticAnalysisBandMetadata>& bands,
        const std::function<bool()>& cancelled = {});
    static bool store(const PublishedWorkspaceSemanticState& state,
        const QHash<QString, SemanticAnalysisBandMetadata>& bands,
        const std::function<bool()>& cancelled = {}, QString* reason = nullptr);
};
