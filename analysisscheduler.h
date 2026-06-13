#ifndef ANALYSISSCHEDULER_H
#define ANALYSISSCHEDULER_H

#include "documentmodel.h"
#include "opendocumentanalysiscontroller.h"
#include "projectmodel.h"
#include "relationshipanalysisqueue.h"
#include "relationshipresultpublisher.h"
#include "relationshipanalysisworker.h"
#include "smartrelationshipbuilder.h"
#include "workspacesymbolanalysiscontroller.h"

#include <QFutureWatcher>
#include <QObject>
#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

class SymbolAnalyzer;
class SymbolRelationshipEngine;
class QTimer;

class AnalysisScheduler : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisScheduler(QObject* parent = nullptr);
    ~AnalysisScheduler() override;

    void setDocumentModel(DocumentModel* model);
    void setProjectModel(ProjectModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setOpenFileContentProvider(std::function<QString(const QString&)> provider);
    void setWorkspaceOpenProvider(std::function<bool()> provider);
    void setWorkspaceSymbolCancelProvider(std::function<bool()> provider);
    void setRelationshipEngine(SymbolRelationshipEngine* engine);
    void setRelationshipBuilder(SmartRelationshipBuilder* builder);

    void scheduleOpenFileAnalysis(const QString& fileName, int delayMs);
    void cancelScheduledOpenFileAnalysis(const QString& fileName);
    void scheduleRelationshipAnalysis(const QString& fileName,
                                      const QString& content,
                                      int delayMs);
    void cancelScheduledRelationshipAnalysis(const QString& fileName);
    void cancelAllScheduledRelationshipAnalyses();
    bool hasScheduledRelationshipAnalysis(const QString& fileName) const;
    void requestRelationshipAnalysis(const QString& fileName, const QString& content);
    void cancelRelationshipAnalysis();
    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceRelationshipAnalysis();
    void handleExternalFileChanged(const QString& fileName, int debounceMs);
    void handleDocumentClosed(const QString& fileName);

signals:
    void documentRefreshRequested(const QString& fileName);
    void diagnosticsRefreshRequested(const QString& fileName);
    void relationshipDataInvalidated();
    void relationshipDataRefreshRequested();
    void fileSymbolAnalysisStarted(const QString& fileName);
    void fileSymbolAnalysisFinished(const QString& fileName, int symbolCount);
    void workspaceSymbolAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
    void workspaceSymbolAnalysisProgress(const QString& fileName,
                                         int filesDone,
                                         int totalFiles);
    void workspaceSymbolAnalysisFinished(const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols);
    void relationshipAnalysisProgress(const QString& fileName, int relationshipsFound);
    void relationshipAnalysisError(const QString& fileName, const QString& error);
    void relationshipAnalysisCancelled();
    void relationshipAnalysisFinished(const SingleFileRelationshipAnalysisResult& result);
    void workspaceRelationshipAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
    void workspaceRelationshipAnalysisProgress(const QString& fileName,
                                               int relationshipsFound,
                                               int processedFiles,
                                               int totalFiles);
    void workspaceRelationshipAnalysisFinished(const WorkspaceRelationshipAnalysisResult& result);
    void workspaceRelationshipAnalysisCancelled();

private:
    DocumentModel* documentModel = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    OpenDocumentAnalysisController* openDocumentAnalysis = nullptr;
    RelationshipAnalysisQueue* relationshipAnalysisQueue = nullptr;
    RelationshipResultPublisher* relationshipResultPublisher = nullptr;
    WorkspaceSymbolAnalysisController* workspaceSymbolAnalysis = nullptr;

    std::function<QString(const QString&)> openFileContentProvider;
    std::function<bool()> workspaceOpenProvider;
    std::function<bool()> workspaceSymbolCancelProvider;
    SmartRelationshipBuilder* relationshipBuilder = nullptr;

    QString pendingDiagnosticsRefreshFileName;
    QTimer* diagnosticsRefreshTimer = nullptr;
    QFutureWatcher<SingleFileRelationshipAnalysisResult>* singleFileRelationshipWatcher = nullptr;
    QFutureWatcher<WorkspaceRelationshipAnalysisResult>* workspaceRelationshipWatcher = nullptr;
    static constexpr int kOpenDocumentRelationshipAnalysisDebounceMs = 2000;

    void onDocumentOpened(const DocumentSnapshot& snapshot);
    void onDocumentEdited(const DocumentSnapshot& snapshot);
    void onDocumentSaved(const DocumentSnapshot& snapshot);
    void scheduleDiagnosticsRefresh(const QString& fileName);

    QString contentForOpenFile(const QString& fileName) const;
};

#endif // ANALYSISSCHEDULER_H
