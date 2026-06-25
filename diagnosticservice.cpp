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

SemanticAnalysisBandMetadata normalizedDiagnosticAnalysisBand(
    const SemanticAnalysisBandMetadata& metadata)
{
    if (metadata.isValid())
        return metadata;

    SemanticAnalysisBandMetadata unbanded;
    unbanded.label = QStringLiteral("unbanded");
    unbanded.displayName = QStringLiteral("unbanded");
    return unbanded;
}

QString diagnosticAnalysisBandLabel(
    const SemanticAnalysisBandMetadata& metadata)
{
    const SemanticAnalysisBandMetadata normalized =
        normalizedDiagnosticAnalysisBand(metadata);
    return normalized.label;
}

QString diagnosticAnalysisBandDisplayName(
    const SemanticAnalysisBandMetadata& metadata)
{
    const SemanticAnalysisBandMetadata normalized =
        normalizedDiagnosticAnalysisBand(metadata);
    return semanticAnalysisBandDisplayName(normalized);
}

QString normalizedDiagnosticAnalysisBandLabel(const QString& label)
{
    return label.trimmed();
}

int diagnosticAnalysisBandGroupSortPriority(
    const DiagnosticAnalysisBandGroup& group)
{
    return semanticAnalysisBandSortPriority(group.analysisBand);
}

QString diagnosticCountText(int count)
{
    return QStringLiteral("%1 %2")
        .arg(count)
        .arg(count == 1
                 ? QStringLiteral("diagnostic")
                 : QStringLiteral("diagnostics"));
}

QString diagnosticSeverityCountText(SemanticDiagnostic::Severity severity,
                                    int count)
{
    QString label;
    switch (severity) {
    case SemanticDiagnostic::Error:
        label = count == 1
            ? QStringLiteral("error")
            : QStringLiteral("errors");
        break;
    case SemanticDiagnostic::Warning:
        label = count == 1
            ? QStringLiteral("warning")
            : QStringLiteral("warnings");
        break;
    case SemanticDiagnostic::Info:
    default:
        label = QStringLiteral("info");
        break;
    }

    return QStringLiteral("%1 %2").arg(count).arg(label);
}

QString diagnosticAnalysisBandSeveritySummary(
    const DiagnosticAnalysisBandGroup& group)
{
    QStringList parts;
    const QList<SemanticDiagnostic::Severity> orderedSeverities{
        SemanticDiagnostic::Error,
        SemanticDiagnostic::Warning,
        SemanticDiagnostic::Info,
    };
    for (SemanticDiagnostic::Severity severity : orderedSeverities) {
        const int count = group.severityCounts.value(severity);
        if (count > 0)
            parts.append(diagnosticSeverityCountText(severity, count));
    }

    return parts.join(QStringLiteral(", "));
}
}

QString DiagnosticReport::analysisBandSummaryText() const
{
    if (analysisBandGroups.isEmpty())
        return QStringLiteral("diagnostic bands none");

    QStringList parts;
    for (const DiagnosticAnalysisBandGroup& group : analysisBandGroups) {
        const QString displayName =
            group.displayName.isEmpty() ? group.label : group.displayName;
        QString part = QStringLiteral("%1 %2")
                           .arg(displayName,
                                diagnosticCountText(group.count));
        const QString severitySummary =
            diagnosticAnalysisBandSeveritySummary(group);
        if (!severitySummary.isEmpty())
            part += QStringLiteral(" (%1)").arg(severitySummary);
        parts.append(part);
    }

    return QStringLiteral("diagnostic bands %1")
        .arg(parts.join(QStringLiteral(", ")));
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
    const DiagnosticQuery normalized = normalizedQuery(query);
    QList<DiagnosticResult> result;
    QSet<QString> normalizedWorkspaceFiles;
    if (normalized.workspaceFilesOnly) {
        for (const QString& fileName : normalized.workspaceFiles)
            normalizedWorkspaceFiles.insert(fileName);
    }

    const QList<SemanticDiagnostic> diagnostics =
        semanticIndex()->getDiagnostics(normalized.fileName);
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (!severityMatches(diagnostic.severity, normalized))
            continue;
        if (normalized.workspaceFilesOnly
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
        item.analysisBand = normalizedDiagnosticAnalysisBand(
            semanticIndex()->analysisBandForFile(diagnostic.fileName));
        item.analysisBandDisplayName =
            diagnosticAnalysisBandDisplayName(item.analysisBand);
        if (!normalized.analysisBandLabel.isEmpty()
            && item.analysisBand.label != normalized.analysisBandLabel) {
            continue;
        }
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
    QMap<QString, int> analysisBandGroupIndexes;
    for (const DiagnosticResult& result : report.diagnostics) {
        const SemanticDiagnostic& diagnostic = result.diagnostic;
        const QString normalized = normalizedFileName(diagnostic.fileName);
        const QString fileKey = normalized.isEmpty() ? diagnostic.fileName : normalized;
        report.fileCounts[fileKey]++;
        report.severityCounts[diagnostic.severity]++;
        report.ownerCounts[diagnostic.owner]++;

        const SemanticAnalysisBandMetadata analysisBand =
            normalizedDiagnosticAnalysisBand(result.analysisBand);
        const QString analysisBandLabel =
            diagnosticAnalysisBandLabel(analysisBand);
        report.analysisBandCounts[analysisBandLabel]++;

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

        if (!analysisBandGroupIndexes.contains(analysisBandLabel)) {
            DiagnosticAnalysisBandGroup group;
            group.analysisBand = analysisBand;
            group.label = analysisBand.label;
            group.displayName = diagnosticAnalysisBandDisplayName(analysisBand);
            analysisBandGroupIndexes.insert(analysisBandLabel,
                                            report.analysisBandGroups.size());
            report.analysisBandGroups.append(group);
        }

        DiagnosticAnalysisBandGroup& analysisBandGroup =
            report.analysisBandGroups[
                analysisBandGroupIndexes.value(analysisBandLabel)];
        analysisBandGroup.diagnostics.append(result);
        analysisBandGroup.count++;
        analysisBandGroup.severityCounts[diagnostic.severity]++;
    }
    std::sort(report.analysisBandGroups.begin(),
              report.analysisBandGroups.end(),
              [](const DiagnosticAnalysisBandGroup& lhs,
                 const DiagnosticAnalysisBandGroup& rhs) {
        const int leftPriority = diagnosticAnalysisBandGroupSortPriority(lhs);
        const int rightPriority = diagnosticAnalysisBandGroupSortPriority(rhs);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;
        return QString::compare(lhs.label,
                                rhs.label,
                                Qt::CaseInsensitive) < 0;
    });
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
    query.analysisBandLabel = options.analysisBandLabel;
    return normalizedQuery(query);
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

DiagnosticQuery DiagnosticService::normalizedQuery(const DiagnosticQuery& query)
{
    DiagnosticQuery normalized = query;
    normalized.fileName = normalizedFileName(query.fileName);
    normalized.workspaceFiles = normalizedFileNames(query.workspaceFiles);
    normalized.analysisBandLabel =
        normalizedDiagnosticAnalysisBandLabel(query.analysisBandLabel);
    return normalized;
}

QString DiagnosticService::normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QStringList DiagnosticService::normalizedFileNames(const QStringList& fileNames)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& fileName : fileNames) {
        const QString path = normalizedFileName(fileName);
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        normalized.append(path);
    }
    return normalized;
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
