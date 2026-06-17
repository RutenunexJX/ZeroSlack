#include "diagnosticservice.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

std::unique_ptr<DiagnosticService> DiagnosticService::instance = nullptr;

namespace {
int diagnosticSeverityRank(SemanticDiagnostic::Severity severity)
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
}

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
    QSet<QString> normalizedWorkspaceFiles;
    if (query.workspaceFilesOnly) {
        for (const QString& fileName : query.workspaceFiles) {
            const QString normalized = normalizedFileName(fileName);
            if (!normalized.isEmpty())
                normalizedWorkspaceFiles.insert(normalized);
        }
    }

    const QList<SemanticDiagnostic> diagnostics =
        semanticIndex()->getDiagnostics(query.fileName);
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (!severityMatches(diagnostic.severity, query))
            continue;
        if (query.workspaceFilesOnly
            && !normalizedWorkspaceFiles.contains(normalizedFileName(diagnostic.fileName))) {
            continue;
        }

        DiagnosticResult item;
        item.diagnostic = diagnostic;
        item.severityDisplayName = severityDisplayName(diagnostic.severity);
        item.fileDisplayName = diagnosticFileDisplayName(diagnostic.fileName);
        item.lineDisplayName = diagnosticLineDisplayName(diagnostic.line);
        item.columnDisplayName = diagnosticColumnDisplayName(diagnostic.column);
        item.messageDisplayName = diagnosticMessageDisplayName(diagnostic.message);
        item.ownerDisplayName = diagnosticOwnerDisplayName(diagnostic.owner);
        result.append(item);
    }
    std::sort(result.begin(), result.end(),
              [](const DiagnosticResult& lhs, const DiagnosticResult& rhs) {
                  const SemanticDiagnostic& left = lhs.diagnostic;
                  const SemanticDiagnostic& right = rhs.diagnostic;
                  const int severityCompare =
                      diagnosticSeverityRank(left.severity)
                      - diagnosticSeverityRank(right.severity);
                  if (severityCompare != 0)
                      return severityCompare < 0;

                  const int fileCompare =
                      QString::compare(DiagnosticService::normalizedFileName(left.fileName),
                                       DiagnosticService::normalizedFileName(right.fileName),
                                       Qt::CaseInsensitive);
                  if (fileCompare != 0)
                      return fileCompare < 0;
                  if (left.line != right.line)
                      return left.line < right.line;
                  if (left.column != right.column)
                      return left.column < right.column;
                  return QString::compare(left.message, right.message, Qt::CaseInsensitive) < 0;
              });
    return result;
}

DiagnosticReport DiagnosticService::findDiagnosticReport(const DiagnosticQuery& query) const
{
    DiagnosticReport report;
    report.diagnostics = findDiagnostics(query);
    report.totalCount = report.diagnostics.size();
    QMap<QString, int> fileGroupIndexes;
    for (const DiagnosticResult& result : report.diagnostics) {
        const SemanticDiagnostic& diagnostic = result.diagnostic;
        const QString normalized = normalizedFileName(diagnostic.fileName);
        const QString fileKey = normalized.isEmpty() ? diagnostic.fileName : normalized;
        report.fileCounts[fileKey]++;
        report.severityCounts[diagnostic.severity]++;
        report.ownerCounts[diagnostic.owner]++;

        if (!fileGroupIndexes.contains(fileKey)) {
            DiagnosticFileGroup group;
            group.fileName = diagnostic.fileName;
            group.fileKey = fileKey;
            group.displayName = diagnosticFileDisplayName(diagnostic.fileName);
            fileGroupIndexes.insert(fileKey, report.fileGroups.size());
            report.fileGroups.append(group);
        }

        DiagnosticFileGroup& group = report.fileGroups[fileGroupIndexes.value(fileKey)];
        group.diagnostics.append(result);
        group.count++;
    }
    return report;
}

bool DiagnosticService::hasDiagnostics(const DiagnosticQuery& query) const
{
    return !findDiagnostics(query).isEmpty();
}

DiagnosticQuery DiagnosticService::queryForPanel(
    const DiagnosticPanelQueryOptions& options) const
{
    DiagnosticQuery query;
    switch (options.scope) {
    case DiagnosticPanelScope::CurrentFile:
        query.fileName = options.requestedFileName.isEmpty()
            ? options.currentFileName
            : options.requestedFileName;
        break;
    case DiagnosticPanelScope::WorkspaceFiles:
        query.workspaceFilesOnly = true;
        query.workspaceFiles = options.workspaceFiles;
        break;
    case DiagnosticPanelScope::AllFiles:
        break;
    }

    query.includeErrors = options.severity == DiagnosticSeverityFilter::All
        || options.severity == DiagnosticSeverityFilter::Errors;
    query.includeWarnings = options.severity == DiagnosticSeverityFilter::All
        || options.severity == DiagnosticSeverityFilter::Warnings;
    query.includeInfo = options.severity == DiagnosticSeverityFilter::All
        || options.severity == DiagnosticSeverityFilter::Info;
    return query;
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

QString DiagnosticService::normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QString DiagnosticService::severityDisplayName(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

QString DiagnosticService::diagnosticFileDisplayName(const QString& fileName)
{
    QString displayName = QFileInfo(fileName).fileName();
    if (displayName.isEmpty())
        displayName = fileName;
    return displayName;
}

QString DiagnosticService::diagnosticLineDisplayName(int line)
{
    return QString::number(line);
}

QString DiagnosticService::diagnosticColumnDisplayName(int column)
{
    return QString::number(column);
}

QString DiagnosticService::diagnosticMessageDisplayName(const QString& message)
{
    return message;
}

QString DiagnosticService::diagnosticOwnerDisplayName(SemanticDiagnostic::Owner owner)
{
    switch (owner) {
    case SemanticDiagnostic::SlangCompiler:
        return QStringLiteral("Slang");
    case SemanticDiagnostic::SemanticIndexOwner:
        return QStringLiteral("Semantic index");
    case SemanticDiagnostic::UnknownOwner:
    default:
        return QStringLiteral("Unknown");
    }
}
