#ifndef DIAGNOSTICPUBLICATIONPOLICY_H
#define DIAGNOSTICPUBLICATIONPOLICY_H

#include "semanticindex.h"

#include <QList>
#include <QtGlobal>
#include <algorithm>

struct DiagnosticPublicationSelection {
    QList<SemanticDiagnostic> diagnostics;
    int producedCount = 0;
    int publishedCount = 0;
    int suppressedCount = 0;
};

class DiagnosticPublicationPolicy
{
public:
    static DiagnosticPublicationSelection select(
        const QList<SemanticDiagnostic>& diagnostics,
        int maxDiagnostics)
    {
        DiagnosticPublicationSelection result;
        result.producedCount = diagnostics.size();
        result.diagnostics = diagnostics;
        std::stable_sort(
            result.diagnostics.begin(),
            result.diagnostics.end(),
            [](const SemanticDiagnostic& left,
               const SemanticDiagnostic& right) {
                return severityRank(left.severity)
                    < severityRank(right.severity);
            });

        const int limit = qMax(1, maxDiagnostics);
        if (result.diagnostics.size() > limit)
            result.diagnostics.resize(limit);
        result.publishedCount = result.diagnostics.size();
        result.suppressedCount =
            result.producedCount - result.publishedCount;
        return result;
    }

private:
    static int severityRank(SemanticDiagnostic::Severity severity)
    {
        switch (severity) {
        case SemanticDiagnostic::Error:
            return 0;
        case SemanticDiagnostic::Warning:
            return 1;
        case SemanticDiagnostic::Info:
        default:
            return 2;
        }
    }
};

#endif // DIAGNOSTICPUBLICATIONPOLICY_H
