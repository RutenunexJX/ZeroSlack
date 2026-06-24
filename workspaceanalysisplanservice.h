#ifndef WORKSPACEANALYSISPLANSERVICE_H
#define WORKSPACEANALYSISPLANSERVICE_H

#include "documentsnapshot.h"
#include "projectmodel.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

struct WorkspaceAnalysisPlanQuery {
    ProjectSnapshot project;
    QString currentFileName;
    QList<DocumentSnapshot> openDocuments;
};

struct WorkspaceAnalysisBandSummary {
    QString label;
    QString displayName;
    int fileCount = 0;
    bool priority = false;
    int publicationCheckpoint = 0;
};

struct WorkspaceAnalysisFileBandMetadata {
    QString label;
    QString displayName;
    bool priority = false;
    int publicationCheckpoint = 0;

    bool isValid() const { return !label.isEmpty(); }
};

struct WorkspaceAnalysisPlan {
    ProjectSnapshot project;
    QString currentFileName;
    QStringList openFiles;
    QStringList protectedFiles;
    QStringList currentFilePriorityFiles;
    QStringList dirtyOpenPriorityFiles;
    QStringList cleanOpenPriorityFiles;
    QStringList backgroundFiles;
    QHash<QString, QString> fileBandsByNormalizedPath;
    QHash<QString, WorkspaceAnalysisFileBandMetadata>
        fileBandMetadataByNormalizedPath;
    QList<WorkspaceAnalysisBandSummary> bandSummaries;
    QList<int> priorityPublicationCheckpoints;
    int priorityFileCount = 0;
    int backgroundFileCount = 0;
    bool currentFileInWorkspace = false;

    bool isValid() const
    {
        return project.isOpen() && !project.systemVerilogFiles.isEmpty();
    }

    QString bandForFile(const QString& fileName) const;
    WorkspaceAnalysisFileBandMetadata bandMetadataForFile(
        const QString& fileName) const;
    QString bandSummaryText() const;
};

class WorkspaceAnalysisPlanService
{
public:
    static WorkspaceAnalysisPlanService* getInstance();

    WorkspaceAnalysisPlan planForWorkspace(
        const WorkspaceAnalysisPlanQuery& query) const;

private:
    static std::unique_ptr<WorkspaceAnalysisPlanService> instance;
};

#endif // WORKSPACEANALYSISPLANSERVICE_H
