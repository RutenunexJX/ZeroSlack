#include "semanticanalysisrequest.h"

QString semanticAnalysisReasonName(SemanticAnalysisReason reason)
{
    switch (reason) {
    case SemanticAnalysisReason::WorkspaceOpen:
        return QStringLiteral("WorkspaceOpen");
    case SemanticAnalysisReason::DocumentOpen:
        return QStringLiteral("DocumentOpen");
    case SemanticAnalysisReason::Save:
        return QStringLiteral("Save");
    case SemanticAnalysisReason::ExternalFileChange:
        return QStringLiteral("ExternalFileChange");
    case SemanticAnalysisReason::WorkspaceConfiguration:
        return QStringLiteral("WorkspaceConfiguration");
    case SemanticAnalysisReason::ExplicitRequest:
        return QStringLiteral("ExplicitRequest");
    case SemanticAnalysisReason::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString semanticChangeImpactName(SemanticChangeImpact impact)
{
    switch (impact) {
    case SemanticChangeImpact::TriviaOnly:
        return QStringLiteral("TriviaOnly");
    case SemanticChangeImpact::LocalBody:
        return QStringLiteral("LocalBody");
    case SemanticChangeImpact::ModuleInterface:
        return QStringLiteral("ModuleInterface");
    case SemanticChangeImpact::PackageApi:
        return QStringLiteral("PackageApi");
    case SemanticChangeImpact::HeaderMacro:
        return QStringLiteral("HeaderMacro");
    case SemanticChangeImpact::WorkspaceConfig:
        return QStringLiteral("WorkspaceConfig");
    case SemanticChangeImpact::FullFallback:
        return QStringLiteral("FullFallback");
    case SemanticChangeImpact::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString semanticAnalysisStageName(SemanticAnalysisStage stage)
{
    switch (stage) {
    case SemanticAnalysisStage::Scheduling:
        return QStringLiteral("Scheduling");
    case SemanticAnalysisStage::Worker:
        return QStringLiteral("Worker");
    case SemanticAnalysisStage::Publication:
        return QStringLiteral("Publication");
    case SemanticAnalysisStage::Navigation:
        return QStringLiteral("Navigation");
    }
    return QStringLiteral("Scheduling");
}

QString documentSemanticStateName(DocumentSemanticState state)
{
    switch (state) {
    case DocumentSemanticState::Current:
        return QStringLiteral("Current");
    case DocumentSemanticState::Dirty:
        return QStringLiteral("Dirty");
    case DocumentSemanticState::Stale:
        return QStringLiteral("Stale");
    case DocumentSemanticState::Queued:
        return QStringLiteral("Queued");
    case DocumentSemanticState::Analyzing:
        return QStringLiteral("Analyzing");
    case DocumentSemanticState::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Current");
}
