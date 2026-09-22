#ifndef SYMBOLANALYZER_H
#define SYMBOLANALYZER_H

#include "zeroslackexport.h"

#include <QObject>
#include <QElapsedTimer>
#include <QStringList>
#include <QHash>
#include <QList>
#include <QSet>
#include <QThreadPool>
#include <QVector>
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include "effectivevalueservice.h"
#include "semanticanalysisrequest.h"
#include "semanticdependencygraph.h"
#include "projectmodel.h"
#include "semanticindex.h"

class WorkspaceManager;
class SemanticIndexSnapshot;
class QTimer;
template <typename T>
class QFutureWatcher;

struct OpenDocumentContent {
    QString fileName;
    QString content;
    std::uint64_t documentRevision = 0;
};

struct WorkspaceFileAnalysis {
    QString fileName;
    QString content;
    QList<SemanticSymbolRecord> symbolRecords;
    QList<EffectiveValueFact> effectiveValueFacts;
};

struct WorkspaceAnalysisResult {
    QVector<WorkspaceFileAnalysis> files;
    QList<SemanticDiagnostic> diagnostics;
    QHash<QString, std::uint64_t> documentRevisionsByFile;
    QHash<QString, SemanticAnalysisBandMetadata> fileAnalysisBands;
    bool cancelled = false;
    std::optional<SemanticAnalysisRequestDisposition> disposition;
    int totalSymbols = 0;
    std::uint64_t generation = 0;
    std::uint64_t workspaceEpoch = 0;
    std::uint64_t analysisRevision = 0;
    qint64 workerElapsedMs = 0;
    qint64 symbolExtractionMs = 0;
    qint64 resultAssemblyMs = 0;
    qint64 diagnosticsExtractionMs = 0;
    qint64 relationshipExtractionMs = 0;
    qint64 relationshipBuildMs = 0;
    qint64 relationshipStateBuildMs = 0;
    int diagnosticsProduced = 0;
    int diagnosticsPublished = 0;
    int diagnosticsSuppressed = 0;
    SemanticAnalysisRequest request;
    IncrementalAnalysisPlan incrementalPlan;
    SemanticDependencyGraph dependencyGraph;
    std::shared_ptr<const SemanticIndexSnapshot> preparedSnapshot;
    QHash<QString, QList<EffectiveValueFact>> effectiveFactsByFile;
    QHash<QString, QString> effectiveContentFingerprintsByFile;
    std::shared_ptr<EffectiveValueService::PreparedFactsState>
        preparedEffectiveFactsState;
    SemanticAnalysisBandReport preparedAnalysisBandReport;
    QHash<QString, QSet<int>> preparedRelationshipHandlesByFile;
    QList<SemanticRelationship> preparedRelationships;
    std::shared_ptr<
        SymbolRelationshipEngine::PreparedRelationshipState>
        preparedRelationshipState;
    bool relationshipDeltaPrepared = false;
    QString error;
    bool slangInvoked = false;
};

struct WorkspaceAnalysisTelemetry {
    QString workspaceRoot;
    int totalFiles = 0;
    int filesAnalyzed = 0;
    int totalSymbols = 0;
    int diagnostics = 0;
    int diagnosticsProduced = 0;
    int diagnosticsSuppressed = 0;
    qint64 workerElapsedMs = 0;
    qint64 symbolExtractionMs = 0;
    qint64 resultAssemblyMs = 0;
    qint64 diagnosticsExtractionMs = 0;
    qint64 publicationMs = 0;
    qint64 publicationUpdateMs = 0;
    qint64 finalSnapshotMs = 0;
    qint64 totalElapsedMs = 0;
};

struct FileAnalysisResult {
    QString fileName;
    QString content;
    QString contentHash;
    QList<SemanticSymbolRecord> symbolRecords;
    QList<SemanticDiagnostic> diagnostics;
    QList<EffectiveValueFact> effectiveValueFacts;
    QVector<WorkspaceFileAnalysis> workspaceFiles;
    QHash<QString, std::uint64_t> dependencyGenerations;
    QHash<QString, std::uint64_t> documentRevisionsByFile;
    QStringList diagnosticFiles;
    std::uint64_t generation = 0;
    std::uint64_t workspaceEpoch = 0;
    std::uint64_t analysisRevision = 0;
    std::uint64_t documentRevision = 0;
    bool cancelled = false;
};

struct SemanticPublicationRetirementPayload {
    SemanticIndexRetirementPayload semanticIndex;
    std::shared_ptr<EffectiveValueService::RetiredFactsState> effectiveFacts;

    bool isEmpty() const
    {
        return semanticIndex.isEmpty() && !effectiveFacts;
    }
};

class ZEROSLACK_API SymbolAnalyzer : public QObject
{
    Q_OBJECT

public:
    using WorkspaceWorkerStartGateForTesting =
        std::function<void(const std::function<bool()>& isCancelled)>;
    using PublicationRetirementGateForTesting = std::function<void()>;

    explicit SymbolAnalyzer(QObject *parent = nullptr);
    ~SymbolAnalyzer();

    // Analysis modes
    void analyzeOpenDocuments(const QList<OpenDocumentContent>& documents);
    void analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled = nullptr);
    void analyzeProject(const ProjectSnapshot& project, std::function<bool()> isCancelled = nullptr);
    void startAnalyzeProjectAsync(
        const ProjectSnapshot& project,
        std::function<bool()> isCancelled = nullptr,
        const QList<OpenDocumentContent>& openDocuments = {});
    void startSemanticAnalysisAsync(const SemanticAnalysisRequest& request);
    void analyzeFile(const QString& filePath);
    void analyzeFileContent(
        const QString& fileName,
        const QString& content,
        std::uint64_t documentRevision = 0);
    void analyzeFileContentAsync(
        const QString& fileName,
        const QString& content,
        std::uint64_t documentRevision = 0);
    void analyzeStandaloneFileContentAsync(const QString& fileName, const QString& content,
                                           std::uint64_t documentRevision = 0);
    void cancelFileAnalysis(const QString& fileName);
    void setWorkspaceFileAnalysisBands(
        const QHash<QString, SemanticAnalysisBandMetadata>& bands);
    void setMaxPublishedDiagnostics(int maxDiagnostics);
    int maxPublishedDiagnostics() const;
    void expireWorkspaceAnalysis();
    // Broadcast cancellation without joining worker threads. Shutdown callers
    // use this before waiting on any analysis family so a saturated global
    // QThreadPool cannot leave a queued worker behind a blocked one.
    void requestCancelAllAnalyses();
    void cancelAllAnalysesAndWait();
    // Terminal shutdown. Closes every analysis / publication entry before
    // joining workers and the retirement pool; the analyzer cannot be reused.
    void shutdown();
    void cancelWorkspaceAnalysisAndInvalidate();
    // Test-only gate. Runs on the worker thread and must return once the
    // supplied cancellation predicate becomes true.
    void setWorkspaceWorkerStartGateForTesting(
        WorkspaceWorkerStartGateForTesting gate);
    void setPublicationRetirementGateForTesting(
        PublicationRetirementGateForTesting gate);
    int pendingPublicationRetirementsForTesting() const;
    int publicationRetirementEnqueueCountForTesting() const;
    int rejectedPublicationRetirementsForTesting() const;

    // Utility
    bool hasSignificantChanges(const QString& oldContent, const QString& newContent) const;
    void invalidateCache();

signals:
    void semanticAnalysisDropped(
        const SemanticAnalysisRequest& request,
        SemanticAnalysisRequestDisposition disposition);
    void analysisStarted(const QString& fileName);
    void analysisCompleted(const QString& fileName, int symbolsFound);
    void batchAnalysisCompleted(int filesAnalyzed, int totalSymbols);
    void batchProgress(int filesDone, int totalFiles, const QString& currentFileName);
    void workspaceAnalysisExpired();
    void workspaceAnalysisTelemetry(const WorkspaceAnalysisTelemetry& telemetry);
    void semanticAnalysisPlanPrepared(const IncrementalAnalysisPlan& plan);
    void semanticAnalysisTelemetry(const SemanticAnalysisTelemetry& telemetry);
    void semanticAnalysisFailed(const SemanticAnalysisRequest& request,
                                const QString& error);

private slots:
    void publishPendingWorkspaceAnalysis();

private:
    // Analysis state tracking
    QHash<QString, QString> lastAnalyzedContent;
    QHash<QString, std::uint64_t> fileAnalysisGenerations;
    QHash<QString, std::shared_ptr<std::atomic_bool>>
        fileAnalysisCancelFlags;
    QSet<QFutureWatcher<FileAnalysisResult>*> fileAnalysisWatchers;
    QStringList overlayWorkspaceFiles;
    QStringList overlayWorkspaceIncludeDirs;
    QHash<QString, QString> overlayWorkspaceDefines;
    QHash<QString, SemanticAnalysisBandMetadata> workspaceFileAnalysisBands;
    SemanticDependencyGraph semanticDependencyGraph;
    QThreadPool semanticAnalysisThreadPool;
    QThreadPool semanticRetirementThreadPool;
    PublicationRetirementGateForTesting publicationRetirementGateForTesting;
    std::shared_ptr<std::atomic<int>> pendingPublicationRetirements =
        std::make_shared<std::atomic<int>>(0);
    std::atomic<int> publicationRetirementEnqueueCount{0};
    std::atomic<int> rejectedPublicationRetirements{0};
    bool publicationRetirementQueueOpen = true;
    bool shutdownStarted = false;
    std::uint64_t workspaceAnalysisGeneration = 0;
    std::uint64_t workspaceEpoch = 0;
    int publishedDiagnosticLimit = 2000;

    QFutureWatcher<WorkspaceAnalysisResult>* workspaceAnalysisWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> workspaceAnalysisCancelFlag;
    WorkspaceWorkerStartGateForTesting workspaceWorkerStartGateForTesting;
    QTimer* workspacePublicationTimer = nullptr;
    std::unique_ptr<WorkspaceAnalysisResult> pendingWorkspacePublication;
    QString pendingWorkspacePublicationPath;
    int pendingWorkspacePublicationTotalFiles = 0;
    int pendingWorkspacePublicationFilesAnalyzed = 0;
    QElapsedTimer pendingWorkspacePublicationTimer;
    qint64 pendingWorkspacePublicationUpdateMs = 0;
    qint64 pendingWorkspacePublicationFinalSnapshotMs = 0;
    QList<SemanticDiagnostic> pendingWorkspacePublicationDiagnostics;

    void publishOpenDocumentResults(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics);
    void updateFileSymbols(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& symbolRecords,
        const QList<EffectiveValueFact>& effectiveValueFacts = {},
        std::uint64_t computationRevision = 0,
        std::uint64_t documentRevision = 0);
    void publishFileAnalysisResult(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& symbolRecords,
        const QList<SemanticDiagnostic>& diagnostics,
        const QList<EffectiveValueFact>& effectiveValueFacts = {},
        std::uint64_t computationRevision = 0,
        std::uint64_t documentRevision = 0);
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
        qint64 finalSnapshotMs);
    void retirePublicationState(
        SemanticPublicationRetirementPayload payload);
    void waitForPublicationRetirements();
    QString contentHash(const QString& content) const;
    void cancelWorkspaceAnalysisAndWait();
    void cancelAllFileAnalysesAndWait();
    void analyzeOverlayDocumentsAsync(
        const QList<OpenDocumentContent>& documents, bool standalone = false);
    void publishOverlayAnalysisResult(const FileAnalysisResult& result);
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
