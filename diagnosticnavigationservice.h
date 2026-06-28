#ifndef DIAGNOSTICNAVIGATIONSERVICE_H
#define DIAGNOSTICNAVIGATIONSERVICE_H

#include "diagnosticservice.h"

struct DiagnosticNavigationQuery {
    QString currentFileName;
    int currentLine = 1;
    int currentColumn = 1;
    DiagnosticPanelScope scope = DiagnosticPanelScope::CurrentFile;
    DiagnosticSeverityFilter severity = DiagnosticSeverityFilter::All;
    QStringList workspaceFiles;
    bool previous = false;
};

struct DiagnosticNavigationResult {
    bool found = false;
    QString failureReason;
    DiagnosticResult diagnostic;
};

class DiagnosticNavigationService
{
public:
    explicit DiagnosticNavigationService(
        DiagnosticService* diagnosticService = nullptr);

    DiagnosticNavigationResult navigate(
        const DiagnosticNavigationQuery& query) const;

private:
    DiagnosticService* diagnostics = nullptr;

    DiagnosticService* diagnosticService() const;
};

#endif // DIAGNOSTICNAVIGATIONSERVICE_H
