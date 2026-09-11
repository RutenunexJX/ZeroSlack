#include "editoractioncontextservice.h"

#include "hierarchyservice.h"
#include "projectmodel.h"
#include "semanticindex.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <utility>

namespace {
std::atomic<std::uint64_t> workspaceFileNormalizationPasses{0};
std::atomic<std::uint64_t> workspaceFileSortPasses{0};
std::atomic<std::uint64_t> hierarchyCacheRebuilds{0};

QString shownOr(const QString& value, const QString& fallback)
{
    return value.trimmed().isEmpty() ? fallback : value;
}
QString normalizedActionContextPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(path).absoluteFilePath()));
}

QSet<QString> normalizedWorkspaceFiles(const QStringList& files,
                                       QStringList* sortedFiles)
{
    ++workspaceFileNormalizationPasses;
    QSet<QString> normalized;
    for (const QString& file : files) {
        const QString path = normalizedActionContextPath(file);
        if (!path.isEmpty())
            normalized.insert(path);
    }
    if (sortedFiles) {
        *sortedFiles = normalized.values();
        ++workspaceFileSortPasses;
        std::sort(sortedFiles->begin(), sortedFiles->end(),
                  [](const QString& left, const QString& right) {
            const int insensitive = QString::compare(
                left, right, Qt::CaseInsensitive);
            return insensitive == 0 ? left < right : insensitive < 0;
        });
    }
    return normalized;
}

std::uint64_t workspaceContextIdentity(
    const QString& workspacePath,
    const QString& configuredTopModule,
    const QStringList& sortedFiles)
{
    constexpr std::uint64_t offsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t fingerprint = offsetBasis;
    auto append = [&](const QString& value) {
        for (const QChar character : value) {
            fingerprint ^= character.unicode();
            fingerprint *= prime;
        }
        fingerprint ^= 0xffffU;
        fingerprint *= prime;
    };
    append(workspacePath);
    append(configuredTopModule);
    for (const QString& file : sortedFiles)
        append(file);
    return fingerprint;
}

QString hierarchyLookupKey(const EditorActionContextQuery& query,
                           const EditorActionWorkspaceContext& workspace,
                           std::uint64_t snapshotRevision)
{
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5\x1f%6")
        .arg(QString::number(snapshotRevision),
             QString::number(workspace.revision),
             QString::number(workspace.identity),
             workspace.configuredTopModule,
             query.editorContext.moduleName,
             normalizedActionContextPath(query.editorContext.fileName));
}

QString hierarchyCandidateKey(
    const EditorHierarchyBindingCandidate& candidate)
{
    return QStringLiteral("%1\x1f%2\x1f%3")
        .arg(candidate.activeTopModule,
             candidate.instancePath,
             normalizedActionContextPath(candidate.definitionFile));
}
}

HierarchyInstanceContext EditorHierarchyBindingCandidate::binding() const
{
    return {workspacePath, activeTopModule, instancePath};
}

QString EditorHierarchyBindingCandidate::displayText() const
{
    return QStringLiteral("%1 / %2")
        .arg(activeTopModule, instancePath);
}

bool EditorHierarchyBindingCandidate::matches(
    const HierarchyInstanceContext& context) const
{
    return activeTopModule == context.activeTopModule
        && instancePath == context.instancePath;
}

bool EditorHierarchyBindingCandidate::operator==(
    const EditorHierarchyBindingCandidate& other) const
{
    return workspacePath == other.workspacePath
        && activeTopModule == other.activeTopModule
        && instancePath == other.instancePath
        && moduleName == other.moduleName
        && definitionFile == other.definitionFile;
}

bool EditorActionContext::hasEditor() const
{
    return !fileName.trimmed().isEmpty();
}

bool EditorActionContext::hierarchyBound() const
{
    return resolvedHierarchy.isBound();
}

QString EditorActionContext::semanticStateText() const
{
    switch (semanticState) {
    case EditorActionSemanticState::Current:
        return QStringLiteral("current");
    case EditorActionSemanticState::Stale:
        return QStringLiteral("stale");
    case EditorActionSemanticState::Analyzing:
        return QStringLiteral("analyzing");
    case EditorActionSemanticState::Failed:
        return QStringLiteral("failed");
    case EditorActionSemanticState::Unavailable:
        return QStringLiteral("unavailable");
    }
    return QStringLiteral("unavailable");
}

QString EditorActionContext::compactText() const
{
    if (!hasEditor())
        return QStringLiteral("Context: no editor | semantic unavailable | instance unbound");

    const QString scope = !moduleName.isEmpty()
        ? QStringLiteral("module %1").arg(moduleName)
        : !packageName.isEmpty()
            ? QStringLiteral("package %1").arg(packageName)
            : QStringLiteral("scope unavailable");
    const QString instance = hierarchyBound()
        ? QStringLiteral("%1 / %2")
              .arg(resolvedHierarchy.activeTopModule,
                   resolvedHierarchy.instancePath)
        : hierarchySelectionRequired
            ? QStringLiteral("instance choose (%1)")
                  .arg(hierarchyCandidates.size())
            : QStringLiteral("instance unbound");
    return QStringLiteral("Context: %1 | semantic %2 | %3")
        .arg(scope, semanticStateText(), instance);
}

QString EditorActionContext::detailText() const
{
    const QString hierarchyText = hierarchyBound()
        ? QStringLiteral("%1 / %2")
              .arg(resolvedHierarchy.activeTopModule,
                   resolvedHierarchy.instancePath)
        : QStringLiteral("unbound");
    QString detail = QStringLiteral(
        "Workspace: %1\nFile: %2\nModule: %3\nPackage: %4\n"
        "Syntax revision: %5\nSemantic snapshot: %6 (%7)\n"
        "Hierarchy: %8")
        .arg(shownOr(workspacePath, QStringLiteral("unavailable")),
             shownOr(fileName, QStringLiteral("unavailable")),
             shownOr(moduleName, QStringLiteral("none")),
             shownOr(packageName, QStringLiteral("none")))
        .arg(syntaxRevision)
        .arg(semanticSnapshotRevision)
        .arg(semanticStateText(), hierarchyText);
    if (!hierarchyResolutionReason.isEmpty())
        detail += QStringLiteral("\nHierarchy resolution: %1")
                      .arg(hierarchyResolutionReason);
    if (!hierarchyCandidates.isEmpty()) {
        detail += QStringLiteral("\nHierarchy candidates:");
        for (const EditorHierarchyBindingCandidate& candidate :
             hierarchyCandidates) {
            detail += QStringLiteral("\n  %1").arg(candidate.displayText());
        }
    }
    if (!semanticError.isEmpty())
        detail += QStringLiteral("\nSemantic error: %1").arg(semanticError);
    return detail;
}

EditorActionContextService::EditorActionContextService(
    SemanticIndex* semanticIndex,
    HierarchyService* hierarchyService)
    : index(semanticIndex), hierarchy(hierarchyService)
{
}

void EditorActionContextService::setSemanticIndex(
    SemanticIndex* semanticIndex)
{
    index = semanticIndex;
    invalidateHierarchyCache();
}

void EditorActionContextService::invalidateHierarchyCache()
{
    cachedHierarchyRevision = 0;
    cachedHierarchyKey.clear();
    cachedHierarchyCandidates.clear();
    cachedHierarchyReason.clear();
}

void EditorActionContextService::setHierarchyService(
    HierarchyService* hierarchyService)
{
    hierarchy = hierarchyService;
    invalidateHierarchyCache();
}

void EditorActionContextService::updateWorkspaceContext(
    const ProjectSnapshot& project)
{
    if (workspaceContextInitialized
        && project.revision != 0
        && project.revision == lastObservedProjectRevision) {
        return;
    }

    const QString workspacePath =
        normalizedActionContextPath(project.workspaceRoot);
    const QString configuredTopModule = project.topModule.trimmed();
    QStringList sortedFiles;
    QSet<QString> workspaceFiles =
        normalizedWorkspaceFiles(project.allFiles, &sortedFiles);
    const std::uint64_t identity = workspaceContextIdentity(
        workspacePath, configuredTopModule, sortedFiles);
    workspaceContextInitialized = true;
    lastObservedProjectRevision = project.revision;
    if (cachedWorkspaceContext.workspacePath == workspacePath
        && cachedWorkspaceContext.configuredTopModule
               == configuredTopModule
        && cachedWorkspaceContext.workspaceFiles == workspaceFiles) {
        cachedWorkspaceContext.projectRevision = project.revision;
        return;
    }

    cachedWorkspaceContext.workspacePath = workspacePath;
    cachedWorkspaceContext.configuredTopModule =
        configuredTopModule;
    cachedWorkspaceContext.workspaceFiles =
        std::move(workspaceFiles);
    cachedWorkspaceContext.revision = ++workspaceRevisionCounter;
    cachedWorkspaceContext.projectRevision = project.revision;
    cachedWorkspaceContext.identity = identity;
    invalidateHierarchyCache();
}

void EditorActionContextService::clearWorkspaceContext()
{
    if (cachedWorkspaceContext.workspacePath.isEmpty()
        && cachedWorkspaceContext.configuredTopModule.isEmpty()
        && cachedWorkspaceContext.workspaceFiles.isEmpty()) {
        return;
    }
    updateWorkspaceContext(ProjectSnapshot());
}

const EditorActionWorkspaceContext&
EditorActionContextService::workspaceContext() const
{
    return cachedWorkspaceContext;
}

SemanticIndex* EditorActionContextService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

HierarchyService* EditorActionContextService::hierarchyService() const
{
    return hierarchy ? hierarchy : HierarchyService::getInstance();
}

EditorActionSemanticState EditorActionContextService::semanticStateFor(
    const EditorActionContextQuery& query)
{
    if (query.editorContext.fileName.isEmpty())
        return EditorActionSemanticState::Unavailable;
    if (query.semanticAnalysisActive
        || query.semanticStatus.state == DocumentSemanticState::Queued
        || query.semanticStatus.state == DocumentSemanticState::Analyzing) {
        return EditorActionSemanticState::Analyzing;
    }
    switch (query.semanticStatus.state) {
    case DocumentSemanticState::Dirty:
    case DocumentSemanticState::Stale:
        return EditorActionSemanticState::Stale;
    case DocumentSemanticState::Failed:
        return EditorActionSemanticState::Failed;
    case DocumentSemanticState::Current:
        return EditorActionSemanticState::Current;
    case DocumentSemanticState::Queued:
    case DocumentSemanticState::Analyzing:
        return EditorActionSemanticState::Analyzing;
    }
    return EditorActionSemanticState::Unavailable;
}

QList<EditorHierarchyBindingCandidate>
EditorActionContextService::resolveHierarchyCandidates(
    const EditorActionContextQuery& query,
    const QString& workspacePath,
    QString* reason) const
{
    SemanticIndex* semantic = semanticIndex();
    HierarchyService* hierarchyApi = hierarchyService();
    const EditorActionWorkspaceContext& workspace =
        cachedWorkspaceContext;
    const std::uint64_t snapshotRevision = semantic
        ? semantic->snapshotRevision() : 0;
    const QString key = hierarchyLookupKey(
        query, workspace, snapshotRevision);
    if (cachedHierarchyRevision == snapshotRevision
        && cachedHierarchyKey == key) {
        if (reason)
            *reason = cachedHierarchyReason;
        return cachedHierarchyCandidates;
    }

    ++hierarchyCacheRebuilds;
    cachedHierarchyRevision = snapshotRevision;
    cachedHierarchyKey = key;
    cachedHierarchyCandidates.clear();
    cachedHierarchyReason.clear();
    auto finish = [&](const QString& message) {
        cachedHierarchyReason = message;
        if (reason)
            *reason = message;
        return cachedHierarchyCandidates;
    };

    if (!semantic || !hierarchyApi || snapshotRevision == 0) {
        return finish(QStringLiteral(
            "No semantic hierarchy snapshot is available; analyze the workspace."));
    }
    if (query.editorContext.moduleName.trimmed().isEmpty()) {
        return finish(query.editorContext.packageName.isEmpty()
            ? QStringLiteral(
                  "Move the cursor into a module before choosing an instance.")
            : QStringLiteral(
                  "Package scope has no hierarchy instance; choose a module signal."));
    }

    QSet<QString> fallbackFileScope;
    const QSet<QString>* fileScope = &workspace.workspaceFiles;
    if (fileScope->isEmpty() && !workspace.workspacePath.isEmpty()) {
        const QDir workspaceDirectory(workspace.workspacePath);
        for (const SemanticSymbolRecord& record : semantic->getSymbolRecords()) {
            const QString recordFile =
                normalizedActionContextPath(record.location.fileName);
            if (recordFile.isEmpty())
                continue;
            const QString relative =
                workspaceDirectory.relativeFilePath(recordFile);
            if (relative != QStringLiteral("..")
                && !relative.startsWith(QStringLiteral("../"))
                && !QDir::isAbsolutePath(relative)) {
                fallbackFileScope.insert(recordFile);
            }
        }
        fileScope = &fallbackFileScope;
    }
    if (fileScope->isEmpty()) {
        const QString currentFile = normalizedActionContextPath(
            query.editorContext.fileName);
        if (!currentFile.isEmpty())
            fallbackFileScope.insert(currentFile);
        fileScope = &fallbackFileScope;
    }

    QStringList roots = hierarchyApi->inferDesignTopModules(*fileScope);
    const QString configuredTop =
        workspace.configuredTopModule;
    if (!configuredTop.isEmpty()) {
        roots.removeAll(configuredTop);
        roots.prepend(configuredTop);
    }
    if (roots.isEmpty()) {
        return finish(QStringLiteral(
            "No analyzed design top is available; configure Active Top or analyze the workspace."));
    }

    const DesignHierarchyReport report =
        hierarchyApi->getDesignHierarchyReport(
            roots, QString(), *fileScope);
    if (report.snapshotGeneration != snapshotRevision) {
        return finish(QStringLiteral(
            "The hierarchy snapshot changed; wait for analysis to finish and retry."));
    }

    const QString currentFile = normalizedActionContextPath(
        query.editorContext.fileName);
    QSet<QString> emitted;
    for (const DesignHierarchyNode& node : report.nodes) {
        if (node.unresolved
            || node.moduleType != query.editorContext.moduleName) {
            continue;
        }
        const QString definitionFile =
            normalizedActionContextPath(node.definitionFile);
        if (!currentFile.isEmpty()
            && QString::compare(definitionFile,
                                currentFile,
                                Qt::CaseInsensitive) != 0) {
            continue;
        }

        EditorHierarchyBindingCandidate candidate;
        candidate.workspacePath = workspacePath;
        candidate.activeTopModule = node.rootModule;
        candidate.instancePath = node.instancePath;
        candidate.moduleName = node.moduleType;
        candidate.definitionFile = definitionFile;
        const QString candidateKey = hierarchyCandidateKey(candidate);
        if (candidate.activeTopModule.isEmpty()
            || candidate.instancePath.isEmpty()
            || emitted.contains(candidateKey)) {
            continue;
        }
        emitted.insert(candidateKey);
        cachedHierarchyCandidates.append(candidate);
    }

    std::sort(cachedHierarchyCandidates.begin(),
              cachedHierarchyCandidates.end(),
              [&](const EditorHierarchyBindingCandidate& left,
                  const EditorHierarchyBindingCandidate& right) {
        const bool leftConfigured =
            !configuredTop.isEmpty()
            && left.activeTopModule == configuredTop;
        const bool rightConfigured =
            !configuredTop.isEmpty()
            && right.activeTopModule == configuredTop;
        if (leftConfigured != rightConfigured)
            return leftConfigured;
        const int topCompare = QString::compare(
            left.activeTopModule,
            right.activeTopModule,
            Qt::CaseInsensitive);
        if (topCompare != 0)
            return topCompare < 0;
        return QString::compare(left.instancePath,
                                right.instancePath,
                                Qt::CaseInsensitive) < 0;
    });

    if (cachedHierarchyCandidates.isEmpty()) {
        return finish(QStringLiteral(
            "Module %1 in the current file is not present in an analyzed design instance.")
            .arg(query.editorContext.moduleName));
    }
    if (reason)
        reason->clear();
    return cachedHierarchyCandidates;
}
EditorActionContext EditorActionContextService::resolve(
    const EditorActionContextQuery& query) const
{
    EditorActionContext result;
    result.workspacePath = cachedWorkspaceContext.workspacePath.isEmpty()
        ? query.editorContext.hierarchyInstance.workspacePath
        : cachedWorkspaceContext.workspacePath;
    result.fileName = query.editorContext.fileName;
    result.moduleName = query.editorContext.moduleName;
    result.packageName = query.editorContext.packageName;
    result.syntaxRevision = query.editorContext.documentRevision;
    result.semanticSnapshotRevision = semanticIndex()
        ? semanticIndex()->snapshotRevision() : 0;
    result.semanticState = semanticStateFor(query);
    if (result.semanticSnapshotRevision == 0
        && result.semanticState == EditorActionSemanticState::Current) {
        result.semanticState = EditorActionSemanticState::Unavailable;
    }
    result.semanticError = query.semanticStatus.error;

    QString candidateReason;
    result.hierarchyCandidates = resolveHierarchyCandidates(
        query, result.workspacePath, &candidateReason);
    const HierarchyInstanceContext explicitBinding =
        query.editorContext.hierarchyInstance;
    for (const EditorHierarchyBindingCandidate& candidate :
         result.hierarchyCandidates) {
        if (!candidate.matches(explicitBinding))
            continue;
        result.resolvedHierarchy = candidate.binding();
        return result;
    }

    QList<EditorHierarchyBindingCandidate> activeTopCandidates;
    const QString configuredTop =
        cachedWorkspaceContext.configuredTopModule;
    if (!configuredTop.isEmpty()) {
        for (const EditorHierarchyBindingCandidate& candidate :
             result.hierarchyCandidates) {
            if (candidate.activeTopModule == configuredTop)
                activeTopCandidates.append(candidate);
        }
    }

    const EditorHierarchyBindingCandidate* unique = nullptr;
    if (activeTopCandidates.size() == 1)
        unique = &activeTopCandidates.constFirst();
    else if (activeTopCandidates.isEmpty()
             && result.hierarchyCandidates.size() == 1)
        unique = &result.hierarchyCandidates.constFirst();
    if (unique && !result.workspacePath.trimmed().isEmpty()) {
        result.resolvedHierarchy = unique->binding();
        result.hierarchyAutoResolved = true;
        return result;
    }

    result.resolvedHierarchy = {};
    if (result.hierarchyCandidates.size() > 1
        || activeTopCandidates.size() > 1) {
        result.hierarchySelectionRequired = true;
        result.hierarchyResolutionReason = QStringLiteral(
            "Choose one of %1 active top / instance bindings.")
            .arg(activeTopCandidates.size() > 1
                     ? activeTopCandidates.size()
                     : result.hierarchyCandidates.size());
    } else if (result.workspacePath.trimmed().isEmpty()
               && !result.hierarchyCandidates.isEmpty()) {
        result.hierarchyResolutionReason = QStringLiteral(
            "Open the containing workspace before binding an instance.");
    } else {
        result.hierarchyResolutionReason = candidateReason;
    }
    return result;
}

EditorActionContextServiceMetrics
EditorActionContextService::metricsForTesting()
{
    EditorActionContextServiceMetrics metrics;
    metrics.workspaceFileNormalizationPasses =
        workspaceFileNormalizationPasses.load();
    metrics.workspaceFileSortPasses = workspaceFileSortPasses.load();
    metrics.hierarchyCacheRebuilds = hierarchyCacheRebuilds.load();
    return metrics;
}

void EditorActionContextService::resetMetricsForTesting()
{
    workspaceFileNormalizationPasses = 0;
    workspaceFileSortPasses = 0;
    hierarchyCacheRebuilds = 0;
}
