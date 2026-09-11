#include "incrementalanalysisplanservice.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <utility>

namespace {
QString normalizedPlanPath(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString path = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}

QStringList orderedUnion(const ProjectSnapshot& project,
                         const QStringList& first,
                         const QStringList& second = {})
{
    QHash<QString, QString> values;
    for (const QString& file : first) {
        const QString key = normalizedPlanPath(file);
        if (!key.isEmpty())
            values.insert(key, file);
    }
    for (const QString& file : second) {
        const QString key = normalizedPlanPath(file);
        if (!key.isEmpty() && !values.contains(key))
            values.insert(key, file);
    }

    QStringList result;
    for (const QString& file : project.systemVerilogFiles) {
        const QString key = normalizedPlanPath(file);
        if (values.contains(key)) {
            result.append(file);
            values.remove(key);
        }
    }
    QStringList extras = values.values();
    extras.sort(Qt::CaseInsensitive);
    result.append(extras);
    return result;
}

IncrementalAnalysisPlan fullPlan(
    const SemanticAnalysisRequest& request,
    SemanticChangeImpact impact,
    const QString& fallbackReason)
{
    IncrementalAnalysisPlan plan;
    plan.reason = request.reason;
    plan.impact = impact;
    plan.triggerFile = request.triggerFile;
    plan.changedFiles = request.changedFiles;
    if (plan.changedFiles.isEmpty() && !request.triggerFile.isEmpty())
        plan.changedFiles.append(request.triggerFile);
    plan.affectedFiles = request.project.systemVerilogFiles;
    if (plan.affectedFiles.isEmpty() && !request.triggerFile.isEmpty())
        plan.affectedFiles.append(request.triggerFile);
    plan.compilationFiles = plan.affectedFiles;
    plan.relationshipFiles = plan.affectedFiles;
    plan.fullWorkspace = true;
    plan.authoritativeWorkspaceReplace =
        impact == SemanticChangeImpact::WorkspaceConfig;
    plan.fallbackReason = fallbackReason;
    return plan;
}

QStringList graphDependents(
    const ProjectSnapshot& project,
    const SemanticDependencyGraph& previousGraph,
    const SemanticDependencyGraph& nextGraph,
    const QStringList& changed,
    SemanticDependencyKinds directKinds)
{
    const bool previousValid = previousGraph.isValidFor(project);
    QStringList frontier = orderedUnion(
        project,
        previousValid
            ? previousGraph.dependentsOf(changed, directKinds, false)
            : QStringList(),
        nextGraph.dependentsOf(changed, directKinds, false));
    QStringList result = frontier;
    QSet<QString> visited;
    for (const QString& fileName : changed)
        visited.insert(normalizedPlanPath(fileName));
    for (const QString& fileName : frontier)
        visited.insert(normalizedPlanPath(fileName));

    while (!frontier.isEmpty()) {
        QStringList next = orderedUnion(
            project,
            previousValid
                ? previousGraph.dependentsOf(
                      frontier, SemanticDependencyKind::All, false)
                : QStringList(),
            nextGraph.dependentsOf(
                frontier, SemanticDependencyKind::All, false));
        QStringList unvisited;
        for (const QString& fileName : next) {
            const QString key = normalizedPlanPath(fileName);
            if (key.isEmpty() || visited.contains(key))
                continue;
            visited.insert(key);
            unvisited.append(fileName);
        }
        result = orderedUnion(project, result, unvisited);
        frontier = std::move(unvisited);
    }
    return result;
}

QStringList graphDependencies(
    const ProjectSnapshot& project,
    const SemanticDependencyGraph& previousGraph,
    const SemanticDependencyGraph& nextGraph,
    const QStringList& roots,
    SemanticDependencyKinds kinds)
{
    const bool previousValid = previousGraph.isValidFor(project);
    QStringList frontier = roots;
    QStringList result;
    QSet<QString> visited;
    for (const QString& fileName : roots)
        visited.insert(normalizedPlanPath(fileName));

    while (!frontier.isEmpty()) {
        QStringList next = orderedUnion(
            project,
            previousValid
                ? previousGraph.dependenciesOf(frontier, kinds, false)
                : QStringList(),
            nextGraph.dependenciesOf(frontier, kinds, false));
        QStringList unvisited;
        for (const QString& fileName : next) {
            const QString key = normalizedPlanPath(fileName);
            if (key.isEmpty() || visited.contains(key))
                continue;
            visited.insert(key);
            unvisited.append(fileName);
        }
        result = orderedUnion(project, result, unvisited);
        frontier = std::move(unvisited);
    }
    return result;
}
}

IncrementalAnalysisPlan IncrementalAnalysisPlanService::plan(
    const SemanticAnalysisRequest& request,
    const SemanticChangeClassification& classification,
    const SemanticDependencyGraph& graph) const
{
    return plan(request, classification, graph, graph);
}

IncrementalAnalysisPlan IncrementalAnalysisPlanService::plan(
    const SemanticAnalysisRequest& request,
    const SemanticChangeClassification& classification,
    const SemanticDependencyGraph& previousGraph,
    const SemanticDependencyGraph& nextGraph) const
{
    if (request.runtimePolicy.planningMode
        == SemanticAnalysisPlanningMode::FullWorkspace) {
        const bool workspaceConfiguration =
            request.impactHint == SemanticChangeImpact::WorkspaceConfig
            || request.reason == SemanticAnalysisReason::WorkspaceOpen
            || request.reason
                   == SemanticAnalysisReason::WorkspaceConfiguration;
        IncrementalAnalysisPlan result = fullPlan(
            request,
            workspaceConfiguration
                ? SemanticChangeImpact::WorkspaceConfig
                : SemanticChangeImpact::FullFallback,
            QStringLiteral(
                "Full workspace analysis required by runtime policy"));
        result.authoritativeWorkspaceReplace = true;
        return result;
    }

    if (request.impactHint == SemanticChangeImpact::WorkspaceConfig
        || request.reason == SemanticAnalysisReason::WorkspaceOpen
        || request.reason == SemanticAnalysisReason::WorkspaceConfiguration) {
        return fullPlan(request,
                        SemanticChangeImpact::WorkspaceConfig,
                        QString());
    }

    if (classification.impact == SemanticChangeImpact::FullFallback) {
        return fullPlan(request,
                        SemanticChangeImpact::FullFallback,
                        classification.fallbackReason);
    }

    if (!request.project.isOpen()
        || !nextGraph.isValidFor(request.project)) {
        return fullPlan(
            request,
            SemanticChangeImpact::FullFallback,
            QStringLiteral("Dependency graph does not match workspace configuration"));
    }

    QStringList changed = request.changedFiles;
    if (changed.isEmpty() && !request.triggerFile.isEmpty())
        changed.append(request.triggerFile);
    changed = orderedUnion(request.project, changed);
    const bool previousGraphValid = previousGraph.isValidFor(request.project);
    for (const QString& file : changed) {
        if (nextGraph.hasParseError(file)
            || (previousGraphValid && previousGraph.hasParseError(file))) {
            return fullPlan(
                request,
                SemanticChangeImpact::FullFallback,
                QStringLiteral(
                    "Previous or current dependency facts contain a parse error"));
        }
    }

    IncrementalAnalysisPlan result;
    result.reason = request.reason;
    result.impact = classification.impact;
    result.triggerFile = request.triggerFile;
    result.changedFiles = changed;

    switch (classification.impact) {
    case SemanticChangeImpact::TriviaOnly:
        result.affectedFiles = changed;
        break;
    case SemanticChangeImpact::LocalBody: {
        result.affectedFiles = changed;
        const SemanticDependencyKinds contextKinds =
            SemanticDependencyKind::Instantiation
            | SemanticDependencyKind::ActiveTop;
        const QStringList instanceContext = nextGraph.dependentsOf(
            changed, contextKinds, true);
        const QStringList compilationRoots = orderedUnion(
            request.project, changed, instanceContext);
        result.compilationFiles = orderedUnion(
            request.project,
            compilationRoots,
            nextGraph.dependenciesOf(compilationRoots,
                                     SemanticDependencyKind::All,
                                     true));
        result.relationshipFiles = changed;
        break;
    }
    case SemanticChangeImpact::ModuleInterface: {
        const SemanticDependencyKinds reverseKinds =
            SemanticDependencyKind::Instantiation
            | SemanticDependencyKind::TypeOrApi;
        const QStringList reverseAffected = orderedUnion(
            request.project,
            changed,
            graphDependents(request.project,
                            previousGraph,
                            nextGraph,
                            changed,
                            reverseKinds));
        result.affectedFiles = orderedUnion(
            request.project,
            reverseAffected,
            graphDependencies(request.project,
                              previousGraph,
                              nextGraph,
                              reverseAffected,
                              SemanticDependencyKind::Instantiation));
        result.compilationFiles = orderedUnion(
            request.project,
            result.affectedFiles,
            nextGraph.dependenciesOf(result.affectedFiles,
                                     SemanticDependencyKind::All,
                                     true));
        result.relationshipFiles = result.affectedFiles;
        break;
    }
    case SemanticChangeImpact::PackageApi: {
        const SemanticDependencyKinds reverseKinds =
            SemanticDependencyKind::Package
            | SemanticDependencyKind::TypeOrApi;
        const QStringList reverseAffected = orderedUnion(
            request.project,
            changed,
            graphDependents(request.project,
                            previousGraph,
                            nextGraph,
                            changed,
                            reverseKinds));
        result.affectedFiles = orderedUnion(
            request.project,
            reverseAffected,
            graphDependencies(request.project,
                              previousGraph,
                              nextGraph,
                              reverseAffected,
                              SemanticDependencyKind::Instantiation));
        result.compilationFiles = orderedUnion(
            request.project,
            result.affectedFiles,
            nextGraph.dependenciesOf(result.affectedFiles,
                                     SemanticDependencyKind::All,
                                     true));
        result.relationshipFiles = result.affectedFiles;
        break;
    }
    case SemanticChangeImpact::HeaderMacro: {
        const SemanticDependencyKinds reverseKinds =
            SemanticDependencyKind::Include
            | SemanticDependencyKind::Macro
            | SemanticDependencyKind::Instantiation
            | SemanticDependencyKind::Package
            | SemanticDependencyKind::TypeOrApi
            | SemanticDependencyKind::ActiveTop;
        const QStringList reverseAffected = orderedUnion(
            request.project,
            changed,
            graphDependents(request.project,
                            previousGraph,
                            nextGraph,
                            changed,
                            reverseKinds));
        result.affectedFiles = orderedUnion(
            request.project,
            reverseAffected,
            graphDependencies(request.project,
                              previousGraph,
                              nextGraph,
                              reverseAffected,
                              SemanticDependencyKind::Instantiation));
        result.compilationFiles = orderedUnion(
            request.project,
            result.affectedFiles,
            nextGraph.dependenciesOf(result.affectedFiles,
                                     SemanticDependencyKind::All,
                                     true));
        result.relationshipFiles = result.affectedFiles;
        break;
    }
    case SemanticChangeImpact::WorkspaceConfig:
        return fullPlan(request,
                        SemanticChangeImpact::WorkspaceConfig,
                        QString());
    case SemanticChangeImpact::FullFallback:
        return fullPlan(request,
                        SemanticChangeImpact::FullFallback,
                        classification.fallbackReason);
    case SemanticChangeImpact::Unknown:
    default:
        return fullPlan(
            request,
            SemanticChangeImpact::FullFallback,
            QStringLiteral("Change classifier returned an unknown impact"));
    }

    return result;
}
