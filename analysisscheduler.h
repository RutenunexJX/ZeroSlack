#ifndef ANALYSISSCHEDULER_H
#define ANALYSISSCHEDULER_H

#include "documentmodel.h"
#include "projectmodel.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"

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

struct WorkspaceRelationshipAnalysisResult {
    QVector<QPair<QString, QVector<RelationshipToAdd>>> fileRelationships;
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
    int totalFiles = 0;
};

struct SingleFileRelationshipAnalysisResult {
    QString fileName;
    QVector<RelationshipToAdd> relationships;
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
};

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
    void requestRelationshipAnalysis(const QString& fileName, const QString& content);
    void cancelRelationshipAnalysis();
    void requestWorkspaceAnalysis(const ProjectSnapshot& project);
    void requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project);
    void cancelWorkspaceRelationshipAnalysis();
    void handleExternalFileChanged(const QString& fileName, int debounceMs);

signals:
    void documentRefreshRequested(const QString& fileName);
    void diagnosticsRefreshRequested(const QString& fileName);
    void relationshipDataInvalidated();
    void relationshipDataRefreshRequested();
    void workspaceSymbolAnalysisStarted(const ProjectSnapshot& project, int totalFiles);
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
    ProjectModel* projectModel = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;

    std::function<QString(const QString&)> openFileContentProvider;
    std::function<bool()> workspaceOpenProvider;
    std::function<bool()> workspaceSymbolCancelProvider;
    SymbolRelationshipEngine* relationshipEngine = nullptr;
    SmartRelationshipBuilder* relationshipBuilder = nullptr;

    QMap<QString, QTimer*> openFileAnalysisTimers;
    QMap<QString, QTimer*> fileChangeDebounceTimers;
    QMap<QString, QString> lastRelationshipAnalysisContent;
    QString pendingDiagnosticsRefreshFileName;
    QTimer* diagnosticsRefreshTimer = nullptr;
    QTimer* relationshipRefreshTimer = nullptr;
    QFutureWatcher<SingleFileRelationshipAnalysisResult>* singleFileRelationshipWatcher = nullptr;
    QFutureWatcher<WorkspaceRelationshipAnalysisResult>* workspaceRelationshipWatcher = nullptr;
    ProjectSnapshot activeWorkspaceProject;
    bool workspaceSymbolAnalysisActive = false;

    void onDocumentOpened(const DocumentSnapshot& snapshot);
    void onDocumentEdited(const DocumentSnapshot& snapshot);
    void onDocumentSaved(const DocumentSnapshot& snapshot);
    void onProjectChanged(const ProjectSnapshot& project);
    void onWorkspaceSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols);
    void analyzeOpenDocumentNow(const DocumentSnapshot& snapshot, bool skipUnchanged);
    void scheduleDiagnosticsRefresh(const QString& fileName);
    void scheduleRelationshipDataRefresh();
    bool applySingleFileRelationshipResult(const SingleFileRelationshipAnalysisResult& result);
    bool applyWorkspaceRelationshipResult(const WorkspaceRelationshipAnalysisResult& result);
    SingleFileRelationshipAnalysisResult analyzeSingleFileRelationships(
        const QString& fileName,
        const QString& content,
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot) const;
    WorkspaceRelationshipAnalysisResult analyzeWorkspaceRelationships(
        const ProjectSnapshot& project,
        std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot) const;

    QString contentForOpenFile(const QString& fileName) const;
    bool isWorkspaceOpen() const;
    bool lineContainsStructuralKeyword(const QString& content, int oneBasedLine) const;
};

#endif // ANALYSISSCHEDULER_H
