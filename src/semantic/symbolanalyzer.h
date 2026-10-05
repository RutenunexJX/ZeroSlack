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
#include <vector>
#include "semanticanalysisresult.h"
#include "effectivevalueservice.h"
#include "semanticanalysisrequest.h"
#include "semanticdependencygraph.h"
#include "semanticanalysisinput.h"
#include "projectmodel.h"
#include "semanticindex.h"
#include "relationshipanalysisworker.h"

class WorkspaceManager;
class SemanticIndexSnapshot;
class QTimer;
template <typename T>
class QFutureWatcher;

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

struct SemanticPublicationRetirementPayload {
    SemanticIndexRetirementPayload semanticIndex;
    std::shared_ptr<EffectiveValueService::RetiredFactsState> effectiveFacts;
    std::unique_ptr<WorkspaceAnalysisResult> analysisResult;
    SemanticDependencyGraph dependencyGraph;
    std::shared_ptr<const PublishedWorkspaceSemanticState> workspaceState;

    bool isEmpty() const
    {
        return semanticIndex.isEmpty() && !effectiveFacts && !analysisResult
            && dependencyGraph.isEmpty() && !workspaceState;
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
    void startSemanticAnalysisAsync(const SemanticAnalysisRequest& request,
                                    std::function<bool()> externalCancellation = {});
    std::uint64_t nextCompatibilityRequestGeneration() { return ++compatibilityAnalysisGeneration; }
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
    // A finished watcher still owns the slot until its GUI completion handler
    // retires it. Cancellation of the logical request does not free this slot.
    bool hasWorkspaceAnalysisInFlight() const
    {
        return workspaceAnalysisWatcher || pendingWorkspacePublication || workspacePublicationActive
            || pendingPublicationRetirements->load(std::memory_order_acquire)
                >= maximumPendingRetirements;
    }
    void expireWorkspaceAnalysis();
    // Broadcast cancellation without joining worker threads. Shutdown callers
    // use this before waiting on any analysis family so a saturated global
    // QThreadPool cannot leave a queued worker behind a blocked one.
    void requestCancelAllAnalyses();
    void cancelAllAnalysesAndWait();
    // Terminal shutdown. Closes every analysis / publication entry before
    // joining workers and the retirement pool; the analyzer cannot be reused.
    void shutdown();
    // Clear visible index state now, then dispatch the former snapshot to the
    // owned retirement pool after this GUI turn. Shutdown drains both queues.
    void clearSemanticIndex();
    bool bindPublishedRelationships(SymbolRelationshipEngine* engine,
                                    const SemanticSnapshotToken& publication);
    void forgetWorkspace(const QString& workspaceRoot);
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
    void relationshipAnalysisCommitted(const SemanticAnalysisRequest& request,
                                      const WorkspaceRelationshipAnalysisResult& publication);
    void semanticAnalysisCommitted(const SemanticAnalysisRequest& request,
                                   const SemanticSnapshotToken& publication);
    void semanticInputWatchPathsChanged(const QString& root, const QStringList& files, const QStringList& directories);
    void fileAnalysisRejected(const QString& fileName, std::uint64_t documentRevision, const QString& reason);
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
    static void prepareWorkspaceDiagnostics(WorkspaceAnalysisResult* result, int limit);
    static constexpr int maximumRetainedWorkspaces = 3;
    static constexpr qsizetype maximumRetainedWorkspaceBytes = 768 * 1024 * 1024;
    static constexpr int maximumPendingRetirements = 4;
    QHash<QString, std::shared_ptr<const PublishedWorkspaceSemanticState>> retainedWorkspaces;
    QStringList retainedWorkspaceLru;
    std::shared_ptr<const PublishedWorkspaceSemanticState> activeWorkspaceState;
    void rememberWorkspace(std::shared_ptr<const PublishedWorkspaceSemanticState> state);
    void forgetRetainedState(const QString& key);
    // Analysis state tracking
    QHash<QString, QString> lastAnalyzedContent;
    QHash<QString, std::shared_ptr<std::atomic_bool>>
        fileAnalysisCancelFlags;
    struct PendingFileDocument {
        OpenDocumentContent document;
        bool standalone = false;
        std::uint64_t epoch = 0;
    };
    static constexpr int maximumPendingFileDocuments = 32;
    static constexpr qsizetype maximumPendingFileBytes = 64 * 1024 * 1024;
    std::optional<SemanticAnalysisRequest> pendingCompatibilityRequest;
    std::function<bool()> pendingCompatibilityCancellation;
    std::uint64_t compatibilityAnalysisGeneration = 0;
    QHash<QString, PendingFileDocument> pendingFileDocuments;
    QStringList pendingFileOrder;
    void launchPendingFileAnalysis();
    void retireWorkspaceResult(WorkspaceAnalysisResult result);
    ProjectSnapshot overlayProject;
    QHash<QString, SemanticAnalysisBandMetadata> workspaceFileAnalysisBands;
    QThreadPool semanticAnalysisThreadPool;
    QThreadPool semanticRetirementThreadPool;
    QTimer* workspaceRetirementTimer = nullptr;
    std::vector<SemanticPublicationRetirementPayload> deferredWorkspaceRetirements;
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
    // The watcher owns this accepted request until normal result handoff or a
    // cancelling join. A future's result may be absent after cancellation.
    std::optional<SemanticAnalysisRequest> workspaceAnalysisRequest;
    std::shared_ptr<std::atomic_bool> workspaceAnalysisCancelFlag;
    WorkspaceWorkerStartGateForTesting workspaceWorkerStartGateForTesting;
    QTimer* workspacePublicationTimer = nullptr;
    std::unique_ptr<WorkspaceAnalysisResult> pendingWorkspacePublication;
    bool workspacePublicationActive = false;
    QString pendingWorkspacePublicationPath;
    int pendingWorkspacePublicationTotalFiles = 0;

    SemanticAnalysisRequest projectRequest(const ProjectSnapshot& project, const QList<OpenDocumentContent>& documents);
    SemanticAnalysisRequest documentRequest(const QList<OpenDocumentContent>& documents, bool standalone,
                                             bool includePublishedDocuments = false);
    void runSemanticAnalysisBlocking(const SemanticAnalysisRequest& request, std::function<bool()> cancelled = {});
    bool isWorkspacePublicationCurrent(const WorkspaceAnalysisResult& result,
                                       bool checkExternalCancellation = true) const;
    bool startWorkspacePublication(WorkspaceAnalysisResult result,
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
    void flushDeferredWorkspaceRetirements();
    void waitForPublicationRetirements();
    void cancelWorkspaceAnalysisAndWait();
    void analyzeOverlayDocumentsAsync(
        const QList<OpenDocumentContent>& documents, bool standalone = false);
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
