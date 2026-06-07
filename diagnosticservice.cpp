#include "diagnosticservice.h"

std::unique_ptr<DiagnosticService> DiagnosticService::instance = nullptr;

DiagnosticService* DiagnosticService::getInstance()
{
    if (!instance)
        instance = std::make_unique<DiagnosticService>();
    return instance.get();
}

DiagnosticService::DiagnosticService(SemanticIndex* semanticIndex)
    : index(semanticIndex ? semanticIndex : SemanticIndex::getInstance())
{
}

DiagnosticService::~DiagnosticService() = default;

void DiagnosticService::setSemanticIndex(SemanticIndex* semanticIndex)
{
    index = semanticIndex ? semanticIndex : SemanticIndex::getInstance();
}

QList<DiagnosticResult> DiagnosticService::findDiagnostics(
    const DiagnosticQuery& query) const
{
    QList<DiagnosticResult> result;
    const QList<SemanticDiagnostic> diagnostics =
        semanticIndex()->getDiagnostics(query.fileName);
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (!severityMatches(diagnostic.severity, query))
            continue;

        DiagnosticResult item;
        item.diagnostic = diagnostic;
        result.append(item);
    }
    return result;
}

bool DiagnosticService::hasDiagnostics(const DiagnosticQuery& query) const
{
    return !findDiagnostics(query).isEmpty();
}

SemanticIndex* DiagnosticService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

bool DiagnosticService::severityMatches(SemanticDiagnostic::Severity severity,
                                        const DiagnosticQuery& query) const
{
    switch (severity) {
    case SemanticDiagnostic::Info:
        return query.includeInfo;
    case SemanticDiagnostic::Warning:
        return query.includeWarnings;
    case SemanticDiagnostic::Error:
        return query.includeErrors;
    }
    return false;
}
