#include "workspaceanalysisplanservice.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

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
    plan.backgroundFileCount = plan.backgroundFiles.size();
    return plan;
}
