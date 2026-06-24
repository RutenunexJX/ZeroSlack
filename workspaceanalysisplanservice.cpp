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

QStringList prioritizedSystemVerilogFiles(
    const ProjectSnapshot& project,
    const QString& currentFileName,
    const QStringList& dirtyOpenFiles,
    const QStringList& openFiles,
    int* priorityFileCount,
    bool* currentFileInWorkspace)
{
    const QHash<QString, QString> workspaceFiles =
        normalizedToOriginal(project.systemVerilogFiles);
    QStringList ordered;
    ordered.reserve(project.systemVerilogFiles.size());
    QSet<QString> seen;

    const int beforeCurrent = ordered.size();
    appendIfWorkspaceFile(&ordered, &seen, workspaceFiles, currentFileName);
    if (currentFileInWorkspace)
        *currentFileInWorkspace = ordered.size() != beforeCurrent;

    for (const QString& fileName : dirtyOpenFiles)
        appendIfWorkspaceFile(&ordered, &seen, workspaceFiles, fileName);
    for (const QString& fileName : openFiles)
        appendIfWorkspaceFile(&ordered, &seen, workspaceFiles, fileName);

    if (priorityFileCount)
        *priorityFileCount = ordered.size();

    for (const QString& fileName : project.systemVerilogFiles)
        appendIfWorkspaceFile(&ordered, &seen, workspaceFiles, fileName);
    return ordered;
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
    plan.project.systemVerilogFiles =
        prioritizedSystemVerilogFiles(query.project,
                                      query.currentFileName,
                                      plan.protectedFiles,
                                      plan.openFiles,
                                      &plan.priorityFileCount,
                                      &plan.currentFileInWorkspace);
    plan.backgroundFileCount =
        std::max(0,
                 static_cast<int>(plan.project.systemVerilogFiles.size())
                     - plan.priorityFileCount);
    return plan;
}
