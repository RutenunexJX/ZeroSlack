#ifndef EDITORACTIONCONTEXTSERVICE_H
#define EDITORACTIONCONTEXTSERVICE_H

#include "editorsemanticcontextservice.h"
#include "semanticanalysisrequest.h"

#include <QList>
#include <QSet>
#include <QString>
#include <cstdint>

class HierarchyService;
class SemanticIndex;
struct ProjectSnapshot;

enum class EditorActionSemanticState {
    Unavailable,
    Current,
    Stale,
    Analyzing,
    Failed
};

struct EditorHierarchyBindingCandidate {
    QString workspacePath;
    QString activeTopModule;
    QString instancePath;
    QString moduleName;
    QString definitionFile;

    HierarchyInstanceContext binding() const;
    QString displayText() const;
    bool matches(const HierarchyInstanceContext& context) const;
    bool operator==(const EditorHierarchyBindingCandidate& other) const;
};

struct EditorActionContextQuery {
    EditorSemanticContext editorContext;
    DocumentSemanticStatus semanticStatus;
    bool semanticAnalysisActive = false;
};

struct EditorActionWorkspaceContext {
    QString workspacePath;
    QString configuredTopModule;
    QSet<QString> workspaceFiles;
    std::uint64_t revision = 0;
    std::uint64_t projectRevision = 0;
    std::uint64_t identity = 0;
};

struct EditorActionContext {
    QString workspacePath;
    QString fileName;
    QString moduleName;
    QString packageName;
    std::uint64_t syntaxRevision = 0;
    std::uint64_t semanticSnapshotRevision = 0;
    EditorActionSemanticState semanticState =
        EditorActionSemanticState::Unavailable;
    QString semanticError;
    HierarchyInstanceContext resolvedHierarchy;
    QList<EditorHierarchyBindingCandidate> hierarchyCandidates;
    bool hierarchyAutoResolved = false;
    bool hierarchySelectionRequired = false;
    QString hierarchyResolutionReason;

    bool hasEditor() const;
    bool hierarchyBound() const;
    QString semanticStateText() const;
    QString compactText() const;
    QString detailText() const;
};

struct EditorActionContextServiceMetrics {
    std::uint64_t workspaceFileNormalizationPasses = 0;
    std::uint64_t workspaceFileSortPasses = 0;
    std::uint64_t hierarchyCacheRebuilds = 0;
};

class EditorActionContextService
{
public:
    explicit EditorActionContextService(
        SemanticIndex* semanticIndex = nullptr,
        HierarchyService* hierarchyService = nullptr);

    void setSemanticIndex(SemanticIndex* semanticIndex);
    void setHierarchyService(HierarchyService* hierarchyService);
    void updateWorkspaceContext(const ProjectSnapshot& project);
    void clearWorkspaceContext();
    const EditorActionWorkspaceContext& workspaceContext() const;
    EditorActionContext resolve(
        const EditorActionContextQuery& query) const;
    static EditorActionContextServiceMetrics metricsForTesting();
    static void resetMetricsForTesting();

private:
    SemanticIndex* index = nullptr;
    HierarchyService* hierarchy = nullptr;
    mutable std::uint64_t cachedHierarchyRevision = 0;
    mutable QString cachedHierarchyKey;
    mutable QList<EditorHierarchyBindingCandidate> cachedHierarchyCandidates;
    mutable QString cachedHierarchyReason;
    EditorActionWorkspaceContext cachedWorkspaceContext;
    std::uint64_t workspaceRevisionCounter = 0;
    std::uint64_t lastObservedProjectRevision = 0;
    bool workspaceContextInitialized = false;

    SemanticIndex* semanticIndex() const;
    HierarchyService* hierarchyService() const;
    void invalidateHierarchyCache();
    static EditorActionSemanticState semanticStateFor(
        const EditorActionContextQuery& query);
    QList<EditorHierarchyBindingCandidate> resolveHierarchyCandidates(
        const EditorActionContextQuery& query,
        const QString& workspacePath,
        QString* reason) const;
};

#endif // EDITORACTIONCONTEXTSERVICE_H
