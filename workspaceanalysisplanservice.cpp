#include "workspaceanalysisplanservice.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>

std::unique_ptr<WorkspaceAnalysisPlanService>
    WorkspaceAnalysisPlanService::instance = nullptr;

namespace {
QString normalizedPath(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

QHash<QString, QString> normalizedToOriginal(const QStringList& files)
{
    QHash<QString, QString> byNormalized;
    for (const QString& fileName : files) {
        const QString normalized = normalizedPath(fileName);
        if (!normalized.isEmpty() && !byNormalized.contains(normalized))
            byNormalized.insert(normalized, fileName);
    }
    return byNormalized;
}

void appendIfWorkspaceFile(QStringList* out,
                           QSet<QString>* seen,
                           const QHash<QString, QString>& workspaceFiles,
                           const QString& candidate)
{
    if (!out || !seen)
        return;
    const QString normalized = normalizedPath(candidate);
    if (normalized.isEmpty()
        || seen->contains(normalized)
        || !workspaceFiles.contains(normalized)) {
        return;
    }

    seen->insert(normalized);
    out->append(workspaceFiles.value(normalized));
}

QStringList dirtyWorkspaceFiles(const QList<DocumentSnapshot>& documents,
                                const QHash<QString, QString>& workspaceFiles)
{
    QStringList files;
    QSet<QString> seen;
    for (const DocumentSnapshot& document : documents) {
        if (!document.dirty)
            continue;
        appendIfWorkspaceFile(&files, &seen, workspaceFiles, document.fileName);
    }
    return files;
}

QStringList openWorkspaceFiles(const QList<DocumentSnapshot>& documents,
                               const QHash<QString, QString>& workspaceFiles)
{
    QStringList files;
    QSet<QString> seen;
    for (const DocumentSnapshot& document : documents)
        appendIfWorkspaceFile(&files, &seen, workspaceFiles, document.fileName);
    return files;
}

struct PrioritizedWorkspaceFiles {
    QStringList currentFilePriorityFiles;
    QStringList dirtyOpenPriorityFiles;
    QStringList cleanOpenPriorityFiles;
    QStringList backgroundFiles;
    QStringList orderedFiles;
    bool currentFileInWorkspace = false;
};

PrioritizedWorkspaceFiles prioritizedSystemVerilogFiles(
    const ProjectSnapshot& project,
    const QString& currentFileName,
    const QStringList& dirtyOpenFiles,
    const QStringList& openFiles)
{
    PrioritizedWorkspaceFiles prioritized;
    const QHash<QString, QString> workspaceFiles =
        normalizedToOriginal(project.systemVerilogFiles);
    prioritized.orderedFiles.reserve(project.systemVerilogFiles.size());
    QSet<QString> seen;

    appendIfWorkspaceFile(&prioritized.currentFilePriorityFiles,
                          &seen,
                          workspaceFiles,
                          currentFileName);
    prioritized.currentFileInWorkspace =
        !prioritized.currentFilePriorityFiles.isEmpty();

    for (const QString& fileName : dirtyOpenFiles)
        appendIfWorkspaceFile(&prioritized.dirtyOpenPriorityFiles,
                              &seen,
                              workspaceFiles,
                              fileName);
    for (const QString& fileName : openFiles)
        appendIfWorkspaceFile(&prioritized.cleanOpenPriorityFiles,
                              &seen,
                              workspaceFiles,
                              fileName);

    for (const QString& fileName : project.systemVerilogFiles)
        appendIfWorkspaceFile(&prioritized.backgroundFiles,
                              &seen,
                              workspaceFiles,
                              fileName);

    prioritized.orderedFiles
        << prioritized.currentFilePriorityFiles
        << prioritized.dirtyOpenPriorityFiles
        << prioritized.cleanOpenPriorityFiles
        << prioritized.backgroundFiles;
    return prioritized;
}

QList<WorkspaceAnalysisBandSummary> bandSummaries(
    const PrioritizedWorkspaceFiles& prioritized)
{
    QList<WorkspaceAnalysisBandSummary> summaries;
    int priorityCheckpoint = 0;
    auto appendBand =
        [&summaries, &priorityCheckpoint](const QString& label,
                                          const QString& displayName,
                                          const QStringList& files,
                                          bool priority) {
            WorkspaceAnalysisBandSummary summary;
            summary.label = label;
            summary.displayName = displayName;
            summary.fileCount = files.size();
            summary.priority = priority;
            if (priority && !files.isEmpty()) {
                priorityCheckpoint += files.size();
                summary.publicationCheckpoint = priorityCheckpoint;
            }
            summaries.append(summary);
    };
    appendBand(QStringLiteral("current"),
               QStringLiteral("current"),
               prioritized.currentFilePriorityFiles,
               true);
    appendBand(QStringLiteral("dirty-open"),
               QStringLiteral("dirty"),
               prioritized.dirtyOpenPriorityFiles,
               true);
    appendBand(QStringLiteral("open"),
               QStringLiteral("open"),
               prioritized.cleanOpenPriorityFiles,
               true);
    appendBand(QStringLiteral("background"),
               QStringLiteral("background"),
               prioritized.backgroundFiles,
               false);
    return summaries;
}

QList<int> priorityPublicationCheckpoints(
    const QList<WorkspaceAnalysisBandSummary>& summaries)
{
    QList<int> checkpoints;
    for (const WorkspaceAnalysisBandSummary& summary : summaries) {
        if (summary.priority && summary.publicationCheckpoint > 0)
            checkpoints.append(summary.publicationCheckpoint);
    }
    return checkpoints;
}

void rememberBand(QHash<QString, QString>* bands,
                  const QStringList& fileNames,
                  const QString& label)
{
    if (!bands)
        return;
    for (const QString& fileName : fileNames) {
        const QString normalized = normalizedPath(fileName);
        if (!normalized.isEmpty() && !bands->contains(normalized))
            bands->insert(normalized, label);
    }
}

QHash<QString, WorkspaceAnalysisBandSummary> summariesByLabel(
    const QList<WorkspaceAnalysisBandSummary>& summaries)
{
    QHash<QString, WorkspaceAnalysisBandSummary> result;
    for (const WorkspaceAnalysisBandSummary& summary : summaries) {
        if (!summary.label.isEmpty())
            result.insert(summary.label, summary);
    }
    return result;
}

void rememberBandMetadata(
    QHash<QString, WorkspaceAnalysisFileBandMetadata>* bands,
    const QHash<QString, WorkspaceAnalysisBandSummary>& summaries,
    const QStringList& fileNames,
    const QString& label)
{
    if (!bands)
        return;

    const WorkspaceAnalysisBandSummary summary = summaries.value(label);
    WorkspaceAnalysisFileBandMetadata metadata;
    metadata.label = label;
    metadata.displayName = summary.displayName.isEmpty()
        ? label
        : summary.displayName;
    metadata.priority = summary.priority;
    metadata.publicationCheckpoint = summary.publicationCheckpoint;

    for (const QString& fileName : fileNames) {
        const QString normalized = normalizedPath(fileName);
        if (!normalized.isEmpty() && !bands->contains(normalized))
            bands->insert(normalized, metadata);
    }
}
}

WorkspaceAnalysisPlanService* WorkspaceAnalysisPlanService::getInstance()
{
    if (!instance)
        instance = std::make_unique<WorkspaceAnalysisPlanService>();
    return instance.get();
}

WorkspaceAnalysisPlan WorkspaceAnalysisPlanService::planForWorkspace(
    const WorkspaceAnalysisPlanQuery& query) const
{
    WorkspaceAnalysisPlan plan;
    plan.project = query.project;
    if (!query.project.isOpen() || query.project.systemVerilogFiles.isEmpty())
        return plan;

    const QHash<QString, QString> workspaceFiles =
        normalizedToOriginal(query.project.systemVerilogFiles);
    plan.currentFileName = normalizedPath(query.currentFileName);
    plan.protectedFiles = dirtyWorkspaceFiles(query.openDocuments,
                                              workspaceFiles);
    plan.openFiles = openWorkspaceFiles(query.openDocuments,
                                        workspaceFiles);
    const PrioritizedWorkspaceFiles prioritized =
        prioritizedSystemVerilogFiles(query.project,
                                      query.currentFileName,
                                      plan.protectedFiles,
                                      plan.openFiles);
    plan.project.systemVerilogFiles = prioritized.orderedFiles;
    plan.currentFilePriorityFiles = prioritized.currentFilePriorityFiles;
    plan.dirtyOpenPriorityFiles = prioritized.dirtyOpenPriorityFiles;
    plan.cleanOpenPriorityFiles = prioritized.cleanOpenPriorityFiles;
    plan.backgroundFiles = prioritized.backgroundFiles;
    plan.currentFileInWorkspace = prioritized.currentFileInWorkspace;
    plan.priorityFileCount =
        plan.currentFilePriorityFiles.size()
        + plan.dirtyOpenPriorityFiles.size()
        + plan.cleanOpenPriorityFiles.size();
    plan.bandSummaries = bandSummaries(prioritized);
    plan.priorityPublicationCheckpoints =
        priorityPublicationCheckpoints(plan.bandSummaries);
    plan.backgroundFileCount = plan.backgroundFiles.size();
    rememberBand(&plan.fileBandsByNormalizedPath,
                 plan.currentFilePriorityFiles,
                 QStringLiteral("current"));
    rememberBand(&plan.fileBandsByNormalizedPath,
                 plan.dirtyOpenPriorityFiles,
                 QStringLiteral("dirty-open"));
    rememberBand(&plan.fileBandsByNormalizedPath,
                 plan.cleanOpenPriorityFiles,
                 QStringLiteral("open"));
    rememberBand(&plan.fileBandsByNormalizedPath,
                 plan.backgroundFiles,
                 QStringLiteral("background"));
    const QHash<QString, WorkspaceAnalysisBandSummary> summaries =
        summariesByLabel(plan.bandSummaries);
    rememberBandMetadata(&plan.fileBandMetadataByNormalizedPath,
                         summaries,
                         plan.currentFilePriorityFiles,
                         QStringLiteral("current"));
    rememberBandMetadata(&plan.fileBandMetadataByNormalizedPath,
                         summaries,
                         plan.dirtyOpenPriorityFiles,
                         QStringLiteral("dirty-open"));
    rememberBandMetadata(&plan.fileBandMetadataByNormalizedPath,
                         summaries,
                         plan.cleanOpenPriorityFiles,
                         QStringLiteral("open"));
    rememberBandMetadata(&plan.fileBandMetadataByNormalizedPath,
                         summaries,
                         plan.backgroundFiles,
                         QStringLiteral("background"));
    return plan;
}

QString WorkspaceAnalysisPlan::bandForFile(const QString& fileName) const
{
    const QString normalized = normalizedPath(fileName);
    const QString indexedBand = fileBandsByNormalizedPath.value(normalized);
    if (!indexedBand.isEmpty())
        return indexedBand;

    auto containsFile = [&normalized](const QStringList& fileNames) {
        for (const QString& candidate : fileNames) {
            if (normalizedPath(candidate) == normalized)
                return true;
        }
        return false;
    };

    if (containsFile(currentFilePriorityFiles))
        return QStringLiteral("current");
    if (containsFile(dirtyOpenPriorityFiles))
        return QStringLiteral("dirty-open");
    if (containsFile(cleanOpenPriorityFiles))
        return QStringLiteral("open");
    if (containsFile(backgroundFiles))
        return QStringLiteral("background");
    return QString();
}

WorkspaceAnalysisFileBandMetadata WorkspaceAnalysisPlan::bandMetadataForFile(
    const QString& fileName) const
{
    const QString normalized = normalizedPath(fileName);
    const WorkspaceAnalysisFileBandMetadata indexedMetadata =
        fileBandMetadataByNormalizedPath.value(normalized);
    if (indexedMetadata.isValid())
        return indexedMetadata;

    const QString label = bandForFile(fileName);
    if (label.isEmpty())
        return {};
    for (const WorkspaceAnalysisBandSummary& summary : bandSummaries) {
        if (summary.label != label)
            continue;
        WorkspaceAnalysisFileBandMetadata metadata;
        metadata.label = summary.label;
        metadata.displayName = summary.displayName.isEmpty()
            ? summary.label
            : summary.displayName;
        metadata.priority = summary.priority;
        metadata.publicationCheckpoint = summary.publicationCheckpoint;
        return metadata;
    }

    WorkspaceAnalysisFileBandMetadata metadata;
    metadata.label = label;
    metadata.displayName = label;
    return metadata;
}

QString WorkspaceAnalysisPlan::bandSummaryText() const
{
    QList<WorkspaceAnalysisBandSummary> summaries = bandSummaries;
    if (summaries.isEmpty()) {
        summaries = {
            {QStringLiteral("current"),
             QStringLiteral("current"),
             static_cast<int>(currentFilePriorityFiles.size()),
             true,
             currentFilePriorityFiles.isEmpty()
                 ? 0
                 : static_cast<int>(currentFilePriorityFiles.size())},
            {QStringLiteral("dirty-open"),
             QStringLiteral("dirty"),
             static_cast<int>(dirtyOpenPriorityFiles.size()),
             true,
             0},
            {QStringLiteral("open"),
             QStringLiteral("open"),
             static_cast<int>(cleanOpenPriorityFiles.size()),
             true,
             0},
            {QStringLiteral("background"),
             QStringLiteral("background"),
             static_cast<int>(backgroundFiles.size()),
             false,
             0}
        };
    }

    QStringList parts;
    parts.reserve(summaries.size());
    for (const WorkspaceAnalysisBandSummary& summary : summaries) {
        parts.append(QStringLiteral("%1 %2")
                         .arg(summary.displayName)
                         .arg(summary.fileCount));
    }
    return QStringLiteral("bands %1").arg(parts.join(QStringLiteral(", ")));
}
