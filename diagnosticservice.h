#ifndef DIAGNOSTICSERVICE_H
#define DIAGNOSTICSERVICE_H

#include "semanticindex.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <memory>

struct DiagnosticQuery {
    QString fileName;
    QStringList workspaceFiles;
    bool workspaceFilesOnly = false;
    bool includeInfo = true;
    bool includeWarnings = true;
    bool includeErrors = true;
};

enum class DiagnosticPanelScope {
    CurrentFile,
    WorkspaceFiles,
    AllFiles
};

enum class DiagnosticSeverityFilter {
    All,
    Errors,
    Warnings,
    Info
};

struct DiagnosticPanelQueryOptions {
    DiagnosticPanelScope scope = DiagnosticPanelScope::CurrentFile;
    DiagnosticSeverityFilter severity = DiagnosticSeverityFilter::All;
    QString requestedFileName;
    QString currentFileName;
    QStringList workspaceFiles;
};

struct DiagnosticResult {
    SemanticDiagnostic diagnostic;
    QString severityDisplayName;
    QString fileDisplayName;
    QString lineDisplayName;
    QString columnDisplayName;
    QString messageDisplayName;
    QString ownerDisplayName;
};

struct DiagnosticFileGroup {
    QString fileName;
    QString fileKey;
    QString displayName;
    QList<DiagnosticResult> diagnostics;
    int count = 0;
};

struct DiagnosticReport {
    QList<DiagnosticResult> diagnostics;
    QList<DiagnosticFileGroup> fileGroups;
    int totalCount = 0;
    QMap<QString, int> fileCounts;
    QMap<SemanticDiagnostic::Severity, int> severityCounts;
    QMap<SemanticDiagnostic::Owner, int> ownerCounts;
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
    DiagnosticQuery queryForPanel(const DiagnosticPanelQueryOptions& options) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<DiagnosticService> instance;

    SemanticIndex* semanticIndex() const;
    bool severityMatches(SemanticDiagnostic::Severity severity,
                         const DiagnosticQuery& query) const;
    static DiagnosticQuery normalizedQuery(const DiagnosticQuery& query);
    static QString normalizedFileName(const QString& fileName);
    static QStringList normalizedFileNames(const QStringList& fileNames);
    static QString severityDisplayName(SemanticDiagnostic::Severity severity);
    static QString diagnosticFileDisplayName(const QString& fileName);
    static QString diagnosticLineDisplayName(int line);
    static QString diagnosticColumnDisplayName(int column);
    static QString diagnosticMessageDisplayName(const QString& message);
    static QString diagnosticOwnerDisplayName(SemanticDiagnostic::Owner owner);
};

#endif // DIAGNOSTICSERVICE_H
