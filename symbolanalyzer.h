#ifndef SYMBOLANALYZER_H
#define SYMBOLANALYZER_H

#include <QObject>
#include <QElapsedTimer>
#include <QStringList>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVector>
#include <cstdint>
#include <functional>
#include <memory>
#include "projectmodel.h"
#include "semanticindex.h"

class WorkspaceManager;
class QTimer;
template <typename T>
class QFutureWatcher;

struct OpenDocumentContent {
    QString fileName;
    QString content;
};

struct WorkspaceFileAnalysis {
    QString fileName;
    QString content;
    QList<SemanticSymbolRecord> symbolRecords;
};

struct WorkspaceAnalysisResult {
    QVector<WorkspaceFileAnalysis> files;
    QList<SemanticDiagnostic> diagnostics;
    QStringList protectedFiles;
    QList<int> priorityPublicationCheckpoints;
    QHash<QString, SemanticAnalysisBandMetadata> fileAnalysisBands;
    bool cancelled = false;
    int totalSymbols = 0;
    std::uint64_t generation = 0;
    qint64 workerElapsedMs = 0;
    qint64 symbolExtractionMs = 0;
    qint64 resultAssemblyMs = 0;
    qint64 diagnosticsExtractionMs = 0;
};

struct WorkspaceAnalysisTelemetry {
    QString workspaceRoot;
    int totalFiles = 0;
    int filesAnalyzed = 0;
    int totalSymbols = 0;
    int diagnostics = 0;
    qint64 workerElapsedMs = 0;
    qint64 symbolExtractionMs = 0;
    qint64 resultAssemblyMs = 0;
    qint64 diagnosticsExtractionMs = 0;
    qint64 publicationMs = 0;
    qint64 publicationUpdateMs = 0;
    qint64 checkpointSnapshotMs = 0;
    qint64 finalSnapshotMs = 0;
    qint64 totalElapsedMs = 0;
};

struct FileAnalysisResult {
    QString fileName;
    QString content;
    QString contentHash;
    QList<SemanticSymbolRecord> symbolRecords;
    QList<SemanticDiagnostic> diagnostics;
    std::uint64_t generation = 0;
};

class SymbolAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit SymbolAnalyzer(QObject *parent = nullptr);
    ~SymbolAnalyzer();

    // Analysis modes
    void analyzeOpenDocuments(const QList<OpenDocumentContent>& documents);
    void analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled = nullptr);
    void analyzeProject(const ProjectSnapshot& project, std::function<bool()> isCancelled = nullptr);
    void startAnalyzeWorkspaceAsync(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled = nullptr);
    void startAnalyzeProjectAsync(const ProjectSnapshot& project, std::function<bool()> isCancelled = nullptr);
    void analyzeFile(const QString& filePath);
    void analyzeFileContent(const QString& fileName, const QString& content);
    void analyzeFileContentAsync(const QString& fileName, const QString& content);
    void setWorkspaceProtectedFiles(const QStringList& fileNames);
    void setWorkspacePriorityFileCount(int fileCount);
    void setWorkspacePriorityPublicationCheckpoints(
        const QList<int>& checkpoints);
    void setWorkspaceFileAnalysisBands(
        const QHash<QString, SemanticAnalysisBandMetadata>& bands);
    void expireWorkspaceAnalysis();
    void cancelWorkspaceAnalysisAndInvalidate();

    // Utility
    bool isAnalysisNeeded(const QString& fileName, const QString& content) const;
    bool hasSignificantChanges(const QString& oldContent, const QString& newContent) const;
    void invalidateCache();

signals:
    void analysisStarted(const QString& fileName);
    void analysisCompleted(const QString& fileName, int symbolsFound);
    void batchAnalysisCompleted(int filesAnalyzed, int totalSymbols);
    void batchProgress(int filesDone, int totalFiles, const QString& currentFileName);
    void workspaceAnalysisExpired();
    void workspaceAnalysisTelemetry(const WorkspaceAnalysisTelemetry& telemetry);

private slots:
    void onWorkspaceAnalysisFinished();
    void processWorkspacePublicationChunk();

private:
    // Analysis state tracking
    QHash<QString, QString> lastAnalyzedContent;
    QHash<QString, std::uint64_t> fileAnalysisGenerations;
    QStringList workspaceProtectedFiles;
    QList<int> workspacePriorityPublicationCheckpoints;
    QHash<QString, SemanticAnalysisBandMetadata> workspaceFileAnalysisBands;
    std::uint64_t workspaceAnalysisGeneration = 0;

    QFutureWatcher<WorkspaceAnalysisResult>* workspaceAnalysisWatcher = nullptr;
    QTimer* workspacePublicationTimer = nullptr;
    std::unique_ptr<WorkspaceAnalysisResult> pendingWorkspacePublication;
    QString pendingWorkspacePublicationPath;
    int pendingWorkspacePublicationTotalFiles = 0;
    int pendingWorkspacePublicationIndex = 0;
    int pendingWorkspacePublicationFilesAnalyzed = 0;
    int pendingWorkspacePublicationPlannedVisited = 0;
    int pendingWorkspacePublicationLastSnapshotFiles = 0;
    int pendingWorkspacePublicationNextCheckpoint = 0;
    QElapsedTimer pendingWorkspacePublicationTimer;
    qint64 pendingWorkspacePublicationUpdateMs = 0;
    qint64 pendingWorkspacePublicationCheckpointSnapshotMs = 0;
    qint64 pendingWorkspacePublicationFinalSnapshotMs = 0;
    QSet<QString> pendingWorkspacePublicationProtectedFiles;
    QList<int> pendingWorkspacePublicationCheckpoints;
    QStringList pendingWorkspacePublicationAnalyzedFiles;
    QList<SemanticDiagnostic> pendingWorkspacePublicationDiagnostics;

    void publishOpenDocumentResults(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics);
    void updateFileSymbols(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& symbolRecords);
    void publishFileAnalysisResult(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& symbolRecords,
        const QList<SemanticDiagnostic>& diagnostics);
    int publishWorkspaceAnalysisResult(
        const WorkspaceAnalysisResult& result,
        int totalFiles,
        WorkspaceAnalysisTelemetry* telemetry = nullptr);
    void startWorkspacePublication(WorkspaceAnalysisResult result,
                                   int totalFiles,
                                   const QString& workspacePath);
    void cancelWorkspacePublication();
    void emitWorkspaceAnalysisTelemetry(
        const QString& workspacePath,
        const WorkspaceAnalysisResult& result,
        int totalFiles,
        int filesAnalyzed,
        qint64 publicationMs,
        qint64 publicationUpdateMs,
        qint64 checkpointSnapshotMs,
        qint64 finalSnapshotMs);
    QString contentHash(const QString& content) const;
    void cancelWorkspaceAnalysisAndWait();
    QStringList filterSystemVerilogFiles(const QStringList& files) const;
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
