#include "relationshipanalysiscontroller.h"

#include "semanticindex.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"

#include <QtConcurrent/QtConcurrent>
#include <QDir>
#include <QFuture>
#include <QPointer>

namespace {
QString normalizedRelationshipPath(const QString& path)
{
    QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

QString relationshipProjectKey(const ProjectSnapshot& project)
{
    QStringList files;
    files.reserve(project.systemVerilogFiles.size());
    for (const QString& file : project.systemVerilogFiles)
        files.append(normalizedRelationshipPath(file));
    files.sort(Qt::CaseSensitive);

    QStringList includeDirs;
    includeDirs.reserve(project.includeDirs.size());
    for (const QString& includeDir : project.includeDirs)
        includeDirs.append(normalizedRelationshipPath(includeDir));
    includeDirs.sort(Qt::CaseSensitive);

    QStringList defineNames = project.defines.keys();
    defineNames.sort(Qt::CaseSensitive);
    QStringList defines;
    defines.reserve(defineNames.size());
    for (const QString& name : defineNames) {
        defines.append(name + QLatin1Char('=') + project.defines.value(name));
    }

    QStringList fileExtensions = project.fileExtensions;
    fileExtensions.sort(Qt::CaseSensitive);

    return normalizedRelationshipPath(project.workspaceRoot)
        + QStringLiteral("\nfiles:") + files.join(QLatin1Char('\n'))
        + QStringLiteral("\nincludes:") + includeDirs.join(QLatin1Char('\n'))
        + QStringLiteral("\ndefines:") + defines.join(QLatin1Char('\n'))
        + QStringLiteral("\nextensions:")
        + fileExtensions.join(QLatin1Char('\n'))
        + QStringLiteral("\ntop:") + project.topModule;
}
}

RelationshipAnalysisController::RelationshipAnalysisController(QObject* parent)
    : QObject(parent)
{
}

RelationshipAnalysisController::~RelationshipAnalysisController()
{
    cancelSingleFileAnalysis();
    cancelWorkspaceAnalysis();
}

void RelationshipAnalysisController::requestSingleFileAnalysis(
    const QString& fileName,
    const QString& content)
{
    if (fileName.isEmpty()
        || content.isEmpty()
        || !relationshipBuilder) {
        return;
    }

    if (symbolAnalyzer && relationshipQueue) {
        const QString lastContent = relationshipQueue->lastContent(fileName);
        if (!lastContent.isNull()
            && !symbolAnalyzer->hasSignificantChanges(lastContent, content)) {
            return;
        }
    }

    if (relationshipQueue)
        relationshipQueue->rememberRequestedContent(fileName, content);
    QPointer<RelationshipAnalysisController> self(this);
    cancelSingleFileAnalysis();
    if (!self)
        return;
    cancelWorkspaceAnalysis();
    if (!self)
        return;
    if (singleFileWatcher || workspaceWatcher || !relationshipBuilder)
        return;

    const std::uint64_t requestGeneration =
        ++singleFileRequestGeneration;
    const QString fileKey = normalizedRelationshipPath(fileName);
    activeSingleFileRequestGeneration = requestGeneration;
    activeSingleFileKey = fileKey;

    relationshipBuilder->resetCancellation();
    const std::shared_ptr<SmartRelationshipBuilder::WorkerLease> builderLease =
        relationshipBuilder->acquireWorkerLease();
    SmartRelationshipBuilder* const builder =
        builderLease ? builderLease->builder() : nullptr;
    if (!builder) {
        activeSingleFileRequestGeneration = 0;
        activeSingleFileKey.clear();
        return;
    }
    const auto baseSnapshot =
        SemanticIndex::getInstance()->beginRelationshipAnalysisSnapshot();

    auto* watcher =
        new QFutureWatcher<SingleFileRelationshipAnalysisResult>(this);
    singleFileWatcher = watcher;
    connect(watcher,
            &QFutureWatcher<SingleFileRelationshipAnalysisResult>::finished,
            this,
            [this, watcher, requestGeneration, fileKey]() {
                if (singleFileWatcher != watcher) {
                    watcher->deleteLater();
                    return;
                }
                singleFileWatcher = nullptr;
                if (activeSingleFileRequestGeneration != requestGeneration
                    || activeSingleFileKey != fileKey) {
                    watcher->deleteLater();
                    return;
                }
                // Clear the active marker before publishing. The monotonically
                // increasing generation remains available to detect a
                // replacement request started by a synchronous observer.
                activeSingleFileRequestGeneration = 0;
                activeSingleFileKey.clear();
                QPointer<RelationshipAnalysisController> self(this);
                QPointer<QFutureWatcher<SingleFileRelationshipAnalysisResult>>
                    watcherGuard(watcher);
                handleSingleFileFinished(watcher,
                                         requestGeneration,
                                         fileKey);
                if (self && watcherGuard)
                    watcherGuard->deleteLater();
            });

    QFuture<SingleFileRelationshipAnalysisResult> future =
        QtConcurrent::run([builderLease,
                           builder,
                           fileName,
                           content,
                           baseSnapshot]() {
            return RelationshipAnalysisWorker::analyzeSingleFile(
                builder,
                fileName,
                content,
                baseSnapshot);
        });
    watcher->setFuture(future);
}

void RelationshipAnalysisController::cancelSingleFileAnalysis()
{
    if (!singleFileWatcher)
        return;

    QFutureWatcher<SingleFileRelationshipAnalysisResult>* watcher =
        singleFileWatcher;
    singleFileWatcher = nullptr;
    ++singleFileRequestGeneration;
    activeSingleFileRequestGeneration = 0;
    activeSingleFileKey.clear();
    disconnect(watcher, nullptr, this, nullptr);
    watcher->setParent(nullptr);
    const bool needsCancellation = !watcher->isFinished();
    SmartRelationshipBuilder* const builder = relationshipBuilder;
    QFuture<SingleFileRelationshipAnalysisResult> future =
        watcher->future();
    watcher->cancel();
    QPointer<RelationshipAnalysisController> self(this);
    if (needsCancellation && builder && !builder->isCancelled())
        builder->cancelAnalysis();
    // The watcher was detached before the synchronous cancellation signal, so
    // it remains valid even if an observer destroys this controller.
    if (!self) {
        future.waitForFinished();
        delete watcher;
        return;
    }
    future.waitForFinished();
    delete watcher;
}

void RelationshipAnalysisController::requestWorkspaceAnalysis(
    const ProjectSnapshot& project)
{
    if (!project.isOpen()
        || project.systemVerilogFiles.isEmpty()
        || !relationshipBuilder) {
        return;
    }

    QPointer<RelationshipAnalysisController> self(this);
    cancelSingleFileAnalysis();
    if (!self)
        return;
    cancelWorkspaceAnalysis();
    if (!self)
        return;
    if (singleFileWatcher || workspaceWatcher || !relationshipBuilder)
        return;

    const std::uint64_t requestGeneration = ++workspaceRequestGeneration;
    const QString projectKey = relationshipProjectKey(project);
    activeWorkspaceRequestGeneration = requestGeneration;
    activeWorkspaceProjectKey = projectKey;

    const auto baseSnapshot =
        SemanticIndex::getInstance()->beginRelationshipAnalysisSnapshot();
    relationshipBuilder->resetCancellation();
    const std::shared_ptr<SmartRelationshipBuilder::WorkerLease> builderLease =
        relationshipBuilder->acquireWorkerLease();
    SmartRelationshipBuilder* const builder =
        builderLease ? builderLease->builder() : nullptr;
    if (!builder) {
        activeWorkspaceRequestGeneration = 0;
        activeWorkspaceProjectKey.clear();
        return;
    }
    const WorkspaceWorkerStartGateForTesting workerStartGate =
        workspaceWorkerStartGateForTesting;

    auto* watcher =
        new QFutureWatcher<WorkspaceRelationshipAnalysisResult>(this);
    workspaceWatcher = watcher;
    connect(watcher,
            &QFutureWatcher<WorkspaceRelationshipAnalysisResult>::finished,
            this,
            [this, watcher, requestGeneration, projectKey]() {
                if (workspaceWatcher != watcher) {
                    watcher->deleteLater();
                    return;
                }
                workspaceWatcher = nullptr;
                if (activeWorkspaceRequestGeneration != requestGeneration
                    || activeWorkspaceProjectKey != projectKey) {
                    watcher->deleteLater();
                    return;
                }
                // Clear identity before any result publication can emit a
                // synchronous signal that starts a replacement request.
                activeWorkspaceRequestGeneration = 0;
                activeWorkspaceProjectKey.clear();
                QPointer<RelationshipAnalysisController> self(this);
                QPointer<QFutureWatcher<WorkspaceRelationshipAnalysisResult>>
                    watcherGuard(watcher);
                handleWorkspaceFinished(watcher,
                                        requestGeneration,
                                        projectKey);
                if (self && watcherGuard)
                    watcherGuard->deleteLater();
            });

    QFuture<WorkspaceRelationshipAnalysisResult> future =
        QtConcurrent::run([builderLease,
                           builder,
                           project,
                           baseSnapshot,
                           requestGeneration,
                           projectKey,
                           workerStartGate]() {
            if (workerStartGate) {
                workerStartGate([builderLease]() {
                    SmartRelationshipBuilder* const leasedBuilder =
                        builderLease ? builderLease->builder() : nullptr;
                    return !leasedBuilder || leasedBuilder->isCancelled();
                });
            }
            return RelationshipAnalysisWorker::analyzeWorkspace(
                builder,
                project,
                baseSnapshot,
                requestGeneration,
                projectKey);
        });
    watcher->setFuture(future);
    emit workspaceRelationshipAnalysisStarted(project,
                                              project.systemVerilogFiles.size());
}

void RelationshipAnalysisController::cancelWorkspaceAnalysis()
{
    if (!workspaceWatcher)
        return;

    QFutureWatcher<WorkspaceRelationshipAnalysisResult>* watcher =
        workspaceWatcher;
    workspaceWatcher = nullptr;
    ++workspaceRequestGeneration;
    activeWorkspaceRequestGeneration = 0;
    activeWorkspaceProjectKey.clear();
    disconnect(watcher, nullptr, this, nullptr);
    watcher->setParent(nullptr);
    const bool needsCancellation = !watcher->isFinished();
    SmartRelationshipBuilder* const builder = relationshipBuilder;
    QFuture<WorkspaceRelationshipAnalysisResult> future = watcher->future();
    watcher->cancel();
    QPointer<RelationshipAnalysisController> self(this);
    if (needsCancellation && builder && !builder->isCancelled())
        builder->cancelAnalysis();
    if (!self) {
        future.waitForFinished();
        delete watcher;
        return;
    }
    future.waitForFinished();
    delete watcher;
}

void RelationshipAnalysisController::requestCancelAllAnalyses()
{
    const bool singleFileRunning =
        singleFileWatcher && !singleFileWatcher->isFinished();
    const bool workspaceRunning =
        workspaceWatcher && !workspaceWatcher->isFinished();
    if (singleFileWatcher) {
        ++singleFileRequestGeneration;
        activeSingleFileRequestGeneration = 0;
        activeSingleFileKey.clear();
    }
    if (workspaceWatcher) {
        ++workspaceRequestGeneration;
        activeWorkspaceRequestGeneration = 0;
        activeWorkspaceProjectKey.clear();
    }
    if (singleFileRunning)
        singleFileWatcher->future().cancel();
    if (workspaceRunning)
        workspaceWatcher->future().cancel();

    SmartRelationshipBuilder* builder = relationshipBuilder;
    if ((singleFileRunning || workspaceRunning)
        && builder
        && !builder->isCancelled()) {
        // Keep this as the final operation: cancelAnalysis emits a synchronous
        // signal and a receiver is allowed to destroy this controller.
        builder->cancelAnalysis();
    }
}

void RelationshipAnalysisController::waitForAllAnalyses()
{
    QPointer<RelationshipAnalysisController> self(this);
    cancelSingleFileAnalysis();
    if (!self)
        return;
    cancelWorkspaceAnalysis();
}
