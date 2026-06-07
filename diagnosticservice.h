#ifndef DIAGNOSTICSERVICE_H
#define DIAGNOSTICSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QMap>
#include <QString>
#include <memory>

struct DiagnosticQuery {
    QString fileName;
    bool includeInfo = true;
    bool includeWarnings = true;
    bool includeErrors = true;
};

struct DiagnosticResult {
    SemanticDiagnostic diagnostic;
};

struct DiagnosticReport {
    QList<DiagnosticResult> diagnostics;
    int totalCount = 0;
    QMap<QString, int> fileCounts;
    QMap<SemanticDiagnostic::Severity, int> severityCounts;
};

class DiagnosticService
{
public:
    static DiagnosticService* getInstance();

    explicit DiagnosticService(SemanticIndex* semanticIndex = nullptr);
    ~DiagnosticService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QList<DiagnosticResult> findDiagnostics(const DiagnosticQuery& query = {}) const;
    DiagnosticReport findDiagnosticReport(const DiagnosticQuery& query = {}) const;
    bool hasDiagnostics(const DiagnosticQuery& query = {}) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<DiagnosticService> instance;

    SemanticIndex* semanticIndex() const;
    bool severityMatches(SemanticDiagnostic::Severity severity,
                         const DiagnosticQuery& query) const;
    static QString normalizedFileName(const QString& fileName);
};

#endif // DIAGNOSTICSERVICE_H
