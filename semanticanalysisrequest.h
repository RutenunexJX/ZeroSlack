#ifndef SEMANTICANALYSISREQUEST_H
#define SEMANTICANALYSISREQUEST_H

#include "projectmodel.h"

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <cstdint>

enum class SemanticAnalysisReason {
    Unknown,
    WorkspaceOpen,
    DocumentOpen,
    Save,
    ExternalFileChange,
    WorkspaceConfiguration,
    ExplicitRequest
};

enum class SemanticChangeImpact {
    Unknown,
    TriviaOnly,
    LocalBody,
    ModuleInterface,
    PackageApi,
    HeaderMacro,
    WorkspaceConfig,
    FullFallback
};

enum class SemanticAnalysisStage {
    Scheduling,
    Worker,
    Publication,
    Navigation
};

enum class DocumentSemanticState {
    Current,
    Dirty,
    Stale,
    Queued,
    Analyzing,
    Failed
};

enum class SemanticAnalysisRequestDisposition {
    Superseded,
    Invalidated,
    Cancelled,
    Expired
};

struct SourceTextDelta {
    int oldStart = 0;
    int oldEnd = 0;
    int newEnd = 0;

    int removedLength() const { return oldEnd - oldStart; }
    int insertedLength() const { return newEnd - oldStart; }
    int lengthDelta() const { return insertedLength() - removedLength(); }
    bool isEmpty() const
    {
        return oldStart == oldEnd && oldStart == newEnd;
    }
};

struct SemanticChangeClassification {
    SemanticChangeImpact impact = SemanticChangeImpact::Unknown;
    SourceTextDelta delta;
    bool oldTreeHasErrors = false;
    bool newTreeHasErrors = false;
    QString fallbackReason;
};

struct IncrementalAnalysisPlan {
    SemanticAnalysisReason reason = SemanticAnalysisReason::Unknown;
    SemanticChangeImpact impact = SemanticChangeImpact::Unknown;
    QString triggerFile;
    QStringList changedFiles;
    QStringList affectedFiles;
    QStringList compilationFiles;
    QStringList relationshipFiles;
    bool fullWorkspace = false;
    bool authoritativeWorkspaceReplace = false;
    QString fallbackReason;

    bool isValid() const
    {
        return impact != SemanticChangeImpact::Unknown
            && (!changedFiles.isEmpty()
                || impact == SemanticChangeImpact::WorkspaceConfig);
    }
};

struct SemanticAnalysisRequest {
    std::uint64_t generation = 0;
    SemanticAnalysisReason reason = SemanticAnalysisReason::Unknown;
    SemanticChangeImpact impactHint = SemanticChangeImpact::Unknown;
    ProjectSnapshot project;
    QString triggerFile;
    QStringList changedFiles;
    QHash<QString, QString> sourceOverrides;
    QHash<QString, std::uint64_t> documentRevisions;
    std::uint64_t expectedSnapshotRevision = 0;
    std::uint64_t computationRevision = 0;

    bool isValid() const
    {
        return generation > 0
            && (project.isOpen() || !triggerFile.isEmpty());
    }
};

struct SemanticAnalysisTelemetry {
    std::uint64_t generation = 0;
    SemanticAnalysisStage stage = SemanticAnalysisStage::Scheduling;
    SemanticAnalysisReason reason = SemanticAnalysisReason::Unknown;
    SemanticChangeImpact impact = SemanticChangeImpact::Unknown;
    QStringList files;
    QStringList changedFiles;
    qint64 workerMs = 0;
    qint64 publicationMs = 0;
    qint64 effectiveFactsMs = 0;
    qint64 snapshotInstallMs = 0;
    qint64 uiRefreshMs = 0;
    bool slangInvoked = false;
    QString detail;
};

struct DocumentSemanticStatus {
    QString fileName;
    DocumentSemanticState state = DocumentSemanticState::Current;
    std::uint64_t documentRevision = 0;
    std::uint64_t analysisGeneration = 0;
    QString error;
};

QString semanticAnalysisReasonName(SemanticAnalysisReason reason);
QString semanticChangeImpactName(SemanticChangeImpact impact);
QString semanticAnalysisStageName(SemanticAnalysisStage stage);
QString documentSemanticStateName(DocumentSemanticState state);

Q_DECLARE_METATYPE(SemanticAnalysisReason)
Q_DECLARE_METATYPE(SemanticChangeImpact)
Q_DECLARE_METATYPE(SemanticAnalysisStage)
Q_DECLARE_METATYPE(DocumentSemanticState)
Q_DECLARE_METATYPE(SemanticAnalysisRequestDisposition)
Q_DECLARE_METATYPE(SourceTextDelta)
Q_DECLARE_METATYPE(SemanticChangeClassification)
Q_DECLARE_METATYPE(IncrementalAnalysisPlan)
Q_DECLARE_METATYPE(SemanticAnalysisRequest)
Q_DECLARE_METATYPE(SemanticAnalysisTelemetry)
Q_DECLARE_METATYPE(DocumentSemanticStatus)

#endif // SEMANTICANALYSISREQUEST_H
