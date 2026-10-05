#pragma once
#include <zeroslack/semantic/effectivevalueservice.h>
#include <zeroslack/semantic/semanticanalysisrequest.h>
#include <zeroslack/semantic/semanticdependencygraph.h>
#include <zeroslack/semantic/semanticanalysisinput.h>
#include <zeroslack/semantic/relationshipanalysisworker.h>
#include <optional>
#include <memory>
#include <functional>
#include <QVector>

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

struct PublishedWorkspaceSemanticState {
    QString scopeKey;
    ProjectSnapshot project;
    SemanticAnalysisRuntimePolicy policy;
    std::shared_ptr<const SemanticAnalysisInput> input;
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    SemanticDependencyGraph dependencyGraph;
    std::shared_ptr<EffectiveValueService::PreparedFactsState> facts;
    std::uint64_t analysisRevision = 0;
    qsizetype logicalBytes = 0;
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
    std::uint64_t deliveryGeneration = 0;
    SemanticSnapshotToken basePublication;
    QString publicationProjectIdentity;
    std::function<bool()> publicationCancelled;
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
    std::shared_ptr<const SemanticAnalysisInput> input;
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
    bool reusedWorkspace = false;
    bool diagnosticsPrepared = false;
    std::shared_ptr<const PublishedWorkspaceSemanticState> publishedState;
    std::shared_ptr<WorkspaceRelationshipAnalysisResult> relationshipProjection;
    QString error;
    bool slangInvoked = false;
};

