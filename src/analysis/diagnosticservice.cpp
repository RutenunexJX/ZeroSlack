#include "diagnosticservice.h"

#include "editorfileidentity.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

std::unique_ptr<DiagnosticService> DiagnosticService::instance = nullptr;

struct DiagnosticService::PanelCache {
    SemanticSnapshotToken token;
    std::uint64_t bandRevision = 0;
    DiagnosticPanelQueryOptions options;
    std::shared_ptr<const DiagnosticPanelReport> report;
};

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

QString DiagnosticAnalysisBandGroup::summaryText() const
{
    const QString effectiveDisplayName =
        displayName.isEmpty() ? label : displayName;
    QString summary = QStringLiteral("%1 %2")
                          .arg(effectiveDisplayName,
                               diagnosticCountText(count));
    const QString severitySummary =
        diagnosticAnalysisBandSeveritySummary(*this);
    if (!severitySummary.isEmpty())
        summary += QStringLiteral(" (%1)").arg(severitySummary);
    return summary;
}

QString DiagnosticReport::analysisBandSummaryText() const
{
    if (analysisBandGroups.isEmpty())
        return QStringLiteral("diagnostic bands none");

    QStringList parts;
    for (const DiagnosticAnalysisBandGroup& group : analysisBandGroups) {
        parts.append(group.summaryText());
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
    panelCache.reset();
}

QList<DiagnosticResult> DiagnosticService::findDiagnostics(
    const DiagnosticQuery& query) const
{
    const DiagnosticQuery normalized = normalizedQuery(query);
    QList<DiagnosticResult> result;
    if (normalized.currentFileRequired && normalized.fileName.isEmpty())
        return result;
    QSet<QString> normalizedWorkspaceFiles;
    if (normalized.workspaceFilesOnly) {
        for (const QString& fileName : normalized.workspaceFiles)
            normalizedWorkspaceFiles.insert(
                EditorFileIdentity::lookupKey(fileName));
    }

    const QList<SemanticDiagnostic> diagnostics =
        semanticIndex()->getDiagnostics(normalized.fileName);
    QHash<QString, QString> sortPaths, workspaceKeys;
    QHash<QString, SemanticAnalysisBandMetadata> bands;
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (!severityMatches(diagnostic.severity, normalized))
            continue;
        if (normalized.workspaceFilesOnly) {
            auto key = workspaceKeys.constFind(diagnostic.fileName);
            if (key == workspaceKeys.cend())
                key = workspaceKeys.insert(diagnostic.fileName, EditorFileIdentity::lookupKey(diagnostic.fileName));
            if (!normalizedWorkspaceFiles.contains(key.value())) continue;
        }

        DiagnosticResult item;
        item.diagnostic = diagnostic;
        item.severityDisplayName = severityDisplayName(diagnostic.severity);
        item.fileDisplayName = diagnosticFileDisplayName(diagnostic.fileName);
        item.lineDisplayName = diagnosticLineDisplayName(diagnostic.line);
        item.columnDisplayName = diagnosticColumnDisplayName(diagnostic.column);
        item.messageDisplayName = diagnosticMessageDisplayName(diagnostic.message);
        item.ownerDisplayName = diagnosticOwnerDisplayName(diagnostic.owner);
        auto band = bands.constFind(diagnostic.fileName);
        if (band == bands.cend())
            band = bands.insert(diagnostic.fileName, normalizedDiagnosticAnalysisBand(
                semanticIndex()->analysisBandForFile(diagnostic.fileName)));
        item.analysisBand = band.value();
        item.analysisBandDisplayName =
            diagnosticAnalysisBandDisplayName(item.analysisBand);
        if (!normalized.analysisBandLabel.isEmpty()
            && item.analysisBand.label != normalized.analysisBandLabel) {
            continue;
        }
        result.append(item);
        if (!sortPaths.contains(diagnostic.fileName))
            sortPaths.insert(diagnostic.fileName, normalizedFileName(diagnostic.fileName));
    }
    std::sort(result.begin(), result.end(),
              [&sortPaths](const DiagnosticResult& lhs, const DiagnosticResult& rhs) {
                  const SemanticDiagnostic& left = lhs.diagnostic;
                  const SemanticDiagnostic& right = rhs.diagnostic;
                  const int severityCompare =
                      diagnosticSeverityRank(left.severity)
                      - diagnosticSeverityRank(right.severity);
                  if (severityCompare != 0)
                      return severityCompare < 0;

                  const int fileCompare =
                      QString::compare(sortPaths.value(left.fileName),
                                       sortPaths.value(right.fileName),
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
    return reportFromDiagnostics(findDiagnostics(query));
}

DiagnosticReport DiagnosticService::reportFromDiagnostics(const QList<DiagnosticResult>& diagnostics)
{
    DiagnosticReport report;
    report.diagnostics = diagnostics;
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

std::shared_ptr<const DiagnosticPanelReport> DiagnosticService::reportForPanel(
    const DiagnosticPanelQueryOptions& options) const
{
    const auto token = semanticIndex()->snapshotToken();
    const auto bandRevision = semanticIndex()->workspaceAnalysisBandRevision();
    if (panelCache && panelCache->token.snapshot == token.snapshot
        && panelCache->token.revision == token.revision
        && panelCache->bandRevision == bandRevision && panelCache->options == options)
        return panelCache->report;

    // Prepare display values and sort once. Current-file queries stay local;
    // broader scopes also need the independent current-file summary.
    DiagnosticQuery sourceQuery;
    if (options.scope == DiagnosticPanelScope::CurrentFile) {
        sourceQuery.fileName = options.currentFileName;
        sourceQuery.currentFileRequired = true;
    }
    const auto diagnostics = findDiagnostics(sourceQuery);
    QHash<QString, QString> identities;
    QSet<QString> workspace;
    if (options.scope == DiagnosticPanelScope::WorkspaceFiles) {
        QStringList files = options.workspaceFiles;
        for (const auto& result : diagnostics) files.append(result.diagnostic.fileName);
        files.removeDuplicates();
        identities = EditorFileIdentity::lookupKeys(files);
        for (const auto& file : options.workspaceFiles) {
            const auto key = identities.value(file);
            if (!key.isEmpty()) workspace.insert(key);
        }
    }
    auto report = std::make_shared<DiagnosticPanelReport>();
    // The snapshot already owns the current-file diagnostic bucket. Counting
    // it needs no display projection or physical resolution of unrelated files.
    const auto currentFile = normalizedFileName(options.currentFileName);
    if (!currentFile.isEmpty())
        for (const auto& diagnostic : semanticIndex()->getDiagnostics(currentFile))
            ++report->currentFileSeverityCounts[diagnostic.severity];
    const auto selection = queryForPanel(options);
    QList<DiagnosticResult> scoped, visible;
    for (const auto& result : diagnostics) {
        if (options.scope == DiagnosticPanelScope::WorkspaceFiles
            && !workspace.contains(identities.value(result.diagnostic.fileName))) continue;
        ++report->scopeSeverityCounts[result.diagnostic.severity];
        if (!severityMatches(result.diagnostic.severity, selection)) continue;
        scoped.append(result);
        if (selection.analysisBandLabel.isEmpty() || result.analysisBand.label == selection.analysisBandLabel)
            visible.append(result);
    }
    report->availableBands = reportFromDiagnostics(scoped);
    report->visible = selection.analysisBandLabel.isEmpty()
        ? report->availableBands : reportFromDiagnostics(visible);
    panelCache = std::make_unique<PanelCache>(PanelCache{token, bandRevision, options, report});
    return report;
}

DiagnosticQuery DiagnosticService::queryForPanel(
    const DiagnosticPanelQueryOptions& options) const
{
    DiagnosticQuery query;
    switch (options.scope) {
    case DiagnosticPanelScope::CurrentFile:
        query.fileName = options.currentFileName;
        query.currentFileRequired = true;
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
        const QString key = EditorFileIdentity::lookupKey(path);
        if (path.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
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
