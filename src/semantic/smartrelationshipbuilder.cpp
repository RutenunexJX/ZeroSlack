#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <exception>
#include <utility>

struct SmartRelationshipBuilder::WorkerLease::State
{
    QMutex mutex;
    QWaitCondition idle;
    int activeWorkers = 0;
    bool destroying = false;
};

SmartRelationshipBuilder::WorkerLease::WorkerLease(
    SmartRelationshipBuilder* builder,
    std::shared_ptr<State> state)
    : leasedBuilder(builder)
    , lifetimeState(std::move(state))
{
}

SmartRelationshipBuilder::WorkerLease::~WorkerLease()
{
    if (!acquired || !lifetimeState)
        return;

    QMutexLocker locker(&lifetimeState->mutex);
    --lifetimeState->activeWorkers;
    if (lifetimeState->activeWorkers == 0)
        lifetimeState->idle.wakeAll();
}

SmartRelationshipBuilder::SmartRelationshipBuilder(
    SymbolRelationshipEngine* engine,
    SlangManager* slangManager,
    SymbolRecordProvider symbolRecordProvider,
    QObject *parent)
    : QObject(parent),
      relationshipEngine(engine),
      m_slangManager(slangManager),
      m_symbolRecordProvider(std::move(symbolRecordProvider)),
      workerLifetimeState(std::make_shared<WorkerLease::State>())
{
}
SmartRelationshipBuilder::~SmartRelationshipBuilder()
{
    const std::shared_ptr<WorkerLease::State> state = workerLifetimeState;
    if (!state)
        return;

    {
        QMutexLocker locker(&state->mutex);
        state->destroying = true;
    }
    cancelled.store(true, std::memory_order_release);

    QMutexLocker locker(&state->mutex);
    while (state->activeWorkers > 0)
        state->idle.wait(&state->mutex);
}

std::shared_ptr<SmartRelationshipBuilder::WorkerLease>
SmartRelationshipBuilder::acquireWorkerLease()
{
    const std::shared_ptr<WorkerLease::State> state = workerLifetimeState;
    if (!state)
        return {};

    auto lease = std::shared_ptr<WorkerLease>(new WorkerLease(this, state));
    QMutexLocker locker(&state->mutex);
    if (state->destroying)
        return {};
    ++state->activeWorkers;
    lease->acquired = true;
    return lease;
}

QVector<RelationshipToAdd> SmartRelationshipBuilder::computeRelationships(
    const QString& fileName,
    const QString& content,
    const QList<SemanticSymbolRecord>& fileSymbolRecords,
    const SemanticIndexSnapshot* snapshot)
{
    return computeRelationships(fileName,
                                content,
                                fileSymbolRecords,
                                snapshot,
                                QStringList(),
                                QHash<QString, QString>());
}

QVector<RelationshipToAdd> SmartRelationshipBuilder::computeRelationships(
    const QString& fileName,
    const QString& content,
    const QList<SemanticSymbolRecord>& fileSymbolRecords,
    const SemanticIndexSnapshot* snapshot,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    const RelationshipExtractionInfo* precomputedRelationshipInfo)
{
    QVector<RelationshipToAdd> result;
    if (checkCancellation(fileName))
        return result;
    if (fileSymbolRecords.isEmpty())
        return result;

    try {
        AnalysisContext context;
        setupAnalysisContextFromRecords(fileName,
                                        fileSymbolRecords,
                                        snapshot,
                                        context);
        context.includeDirs = includeDirs;
        context.defines = defines;
        if (precomputedRelationshipInfo) {
            context.relationshipInfo = *precomputedRelationshipInfo;
            context.relationshipInfoLoaded = true;
        }

        collectResults = &result;

        analyzeModuleInstantiations(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        analyzeVariableAssignments(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        analyzeVariableReferences(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        analyzeTaskFunctionCalls(content, context);
        if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

        if (enableAdvancedAnalysis) {
            analyzeAlwaysBlocks(content, context);
            if (checkCancellation(fileName)) { collectResults = nullptr; return result; }

            analyzeClockResetRelationships(content, context);
            if (checkCancellation(fileName)) { collectResults = nullptr; return result; }
        }

        collectResults = nullptr;
    } catch (const std::exception& error) {
        collectResults = nullptr;
        emit analysisError(
            fileName,
            QStringLiteral("Relationship analysis failed: %1")
                .arg(QString::fromUtf8(error.what())));
    } catch (...) {
        collectResults = nullptr;
        emit analysisError(
            fileName,
            QStringLiteral("Relationship analysis failed with an unknown exception."));
    }
    return result;
}

QHash<QString, RelationshipExtractionInfo>
SmartRelationshipBuilder::extractWorkspaceRelationshipInfo(
    const QStringList& filePaths,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines) const
{
    if (!m_slangManager)
        return {};
    return m_slangManager->extractWorkspaceRelationshipInfo(filePaths,
                                                            includeDirs,
                                                            defines,
                                                            [this]() {
                                                                return isCancelled();
                                                            });
}

QHash<QString, RelationshipExtractionInfo>
SmartRelationshipBuilder::extractOverlayWorkspaceRelationshipInfo(
    const QHash<QString, QString>& fileContents,
    const QStringList& includeDirs,
    const QHash<QString, QString>& defines,
    const QStringList& orderedFilePaths) const
{
    if (!m_slangManager)
        return {};
    return m_slangManager->extractOverlayWorkspaceRelationshipInfo(
        fileContents,
        includeDirs,
        defines,
        [this]() {
            return isCancelled();
        },
        orderedFilePaths);
}

void SmartRelationshipBuilder::cancelAnalysis()
{
    cancelled.store(true);
    emit analysisCancelled();
}

void SmartRelationshipBuilder::resetCancellation()
{
    cancelled.store(false);
}

bool SmartRelationshipBuilder::checkCancellation(const QString& currentFile)
{
    if (cancelled.load()) {
        if (!currentFile.isEmpty()) {
            emit analysisError(currentFile, "Analysis cancelled by user");
        }
        return true;
    }
    return false;
}
