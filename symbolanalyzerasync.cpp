#include "symbolanalyzer.h"

#include "slangmanager.h"
#include "semanticindexsnapshot.h"
#include "symbolanalyzerworkspace.h"
#include "workspacemanager.h"

#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {
QString normalizedAsyncAnalysisFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool readAsyncAnalysisFile(const QString& fileName, QString* content)
{
    if (!content)
        return false;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QFile::Text))
        return false;
    QTextStream stream(&file);
    *content = stream.readAll();
    return stream.status() == QTextStream::Ok;
}
}

SymbolAnalyzer::SymbolAnalyzer(QObject *parent)
    : QObject(parent)
{
    semanticAnalysisThreadPool.setMaxThreadCount(1);
    semanticAnalysisThreadPool.setThreadPriority(QThread::LowPriority);
    semanticAnalysisThreadPool.setObjectName(
        QStringLiteral("ZeroSlackSemanticAnalysis"));
    semanticRetirementThreadPool.setMaxThreadCount(1);
    semanticRetirementThreadPool.setThreadPriority(QThread::LowestPriority);
    semanticRetirementThreadPool.setObjectName(
        QStringLiteral("ZeroSlackSemanticRetirement"));
    workspacePublicationTimer = new QTimer(this);
    workspacePublicationTimer->setSingleShot(true);
    connect(workspacePublicationTimer,
            &QTimer::timeout,
            this,
            &SymbolAnalyzer::publishPendingWorkspaceAnalysis);
}

SymbolAnalyzer::~SymbolAnalyzer()
{
    shutdown();
}

void SymbolAnalyzer::setWorkspaceWorkerStartGateForTesting(
    WorkspaceWorkerStartGateForTesting gate)
{
    workspaceWorkerStartGateForTesting = std::move(gate);
}

void SymbolAnalyzer::setPublicationRetirementGateForTesting(
    PublicationRetirementGateForTesting gate)
{
    publicationRetirementGateForTesting = std::move(gate);
}

int SymbolAnalyzer::pendingPublicationRetirementsForTesting() const
{
    return pendingPublicationRetirements->load(std::memory_order_acquire);
}

int SymbolAnalyzer::publicationRetirementEnqueueCountForTesting() const
{
    return publicationRetirementEnqueueCount.load(std::memory_order_acquire);
}

int SymbolAnalyzer::rejectedPublicationRetirementsForTesting() const
{
    return rejectedPublicationRetirements.load(std::memory_order_acquire);
}

void SymbolAnalyzer::startAnalyzeProjectAsync(
    const ProjectSnapshot& project,
    std::function<bool()> isCancelled,
    const QList<OpenDocumentContent>& openDocuments)
{
    if (shutdownStarted || !project.isOpen())
        return;
    cancelWorkspacePublication();
    cancelAllFileAnalysesAndWait();
    cancelWorkspaceAnalysisAndWait();

    const QStringList svFiles = project.systemVerilogFiles;
    const QStringList includeDirs = project.includeDirs;
    const QHash<QString, QString> defines = project.defines;
    overlayWorkspaceFiles = svFiles;
    overlayWorkspaceIncludeDirs = includeDirs;
    overlayWorkspaceDefines = defines;
    const QString workspacePath = project.workspaceRoot;
    const int totalFiles = svFiles.size();
    const std::uint64_t epoch = ++workspaceEpoch;
    const std::uint64_t generation = ++workspaceAnalysisGeneration;
    QHash<QString, OpenDocumentContent> openDocumentsByKey;
    QHash<QString, std::uint64_t> documentRevisionsByFile;
    for (const OpenDocumentContent& document : openDocuments) {
        if (!isSystemVerilogFile(document.fileName)
            || document.content.isNull()) {
            continue;
        }
        const QString key =
            normalizedAsyncAnalysisFileName(document.fileName);
        if (key.isEmpty())
            continue;
        openDocumentsByKey.insert(key, document);
        documentRevisionsByFile.insert(key, document.documentRevision);
    }
    // The workspace compilation, including every captured open-buffer
    // overlay, is one semantic transaction. Advancing every dependency here
    // prevents either a later edit or a standalone overlay task from
    // accepting a mixed workspace result.
    const QStringList revisionFiles = svFiles;
    const std::uint64_t analysisRevision =
        EffectiveValueService::getInstance()->beginComputation(revisionFiles);
    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    workspaceAnalysisCancelFlag = cancellation;
    const QHash<QString, SemanticAnalysisBandMetadata> fileAnalysisBands =
        workspaceFileAnalysisBands;
    const WorkspaceWorkerStartGateForTesting workerStartGate =
        workspaceWorkerStartGateForTesting;

    emit analysisStarted(workspacePath);

    QFuture<WorkspaceAnalysisResult> future = QtConcurrent::run([svFiles, includeDirs, defines, isCancelled, cancellation, generation, epoch, analysisRevision, fileAnalysisBands, workerStartGate, openDocumentsByKey = std::move(openDocumentsByKey), documentRevisionsByFile]() {
        QElapsedTimer workerTimer;
        workerTimer.start();
        auto applyResultMetadata =
            [generation,
             epoch,
             analysisRevision,
             &fileAnalysisBands,
             &documentRevisionsByFile](
                WorkspaceAnalysisResult* result) {
                if (!result)
                    return;
                result->fileAnalysisBands = fileAnalysisBands;
                result->generation = generation;
                result->workspaceEpoch = epoch;
                result->analysisRevision = analysisRevision;
                result->documentRevisionsByFile =
                    documentRevisionsByFile;
            };
        auto cancelled = [&isCancelled, &cancellation]() {
            return cancellation->load(std::memory_order_relaxed)
                || (isCancelled && isCancelled());
        };
        if (workerStartGate)
            workerStartGate(cancelled);
        WorkspaceAnalysisResult result;
        if (cancelled()) {
            result.cancelled = true;
            result.workerElapsedMs = workerTimer.elapsed();
            applyResultMetadata(&result);
            return result;
        }

        SlangManager symbolAnalyzer;
        QList<EffectiveValueFact> effectiveValueFacts;
        QHash<QString, QString> analyzedFileContents;
        for (const QString& fileName : svFiles) {
            if (cancelled()) {
                result.cancelled = true;
                result.workerElapsedMs = workerTimer.elapsed();
                applyResultMetadata(&result);
                return result;
            }
            const QString key =
                normalizedAsyncAnalysisFileName(fileName);
            QString content;
            const auto overlay = openDocumentsByKey.constFind(key);
            if (overlay != openDocumentsByKey.constEnd()) {
                content = overlay->content;
            } else if (!readAsyncAnalysisFile(fileName, &content)) {
                // An incomplete workspace cannot be published atomically.
                result.cancelled = true;
                result.workerElapsedMs = workerTimer.elapsed();
                applyResultMetadata(&result);
                return result;
            }
            analyzedFileContents.insert(fileName, content);
        }
        QElapsedTimer stageTimer;
        stageTimer.start();
        const auto records =
            symbolAnalyzer.extractOverlayWorkspaceSymbolRecords(
                analyzedFileContents,
                includeDirs,
                defines,
                cancelled,
                &effectiveValueFacts,
                svFiles);
        const qint64 symbolExtractionMs = stageTimer.elapsed();
        if (cancelled()) {
            result.cancelled = true;
            result.symbolExtractionMs = symbolExtractionMs;
            result.workerElapsedMs = workerTimer.elapsed();
            applyResultMetadata(&result);
            return result;
        }

        stageTimer.restart();
        result =
            SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
                svFiles,
                records,
                effectiveValueFacts,
                cancelled,
                analyzedFileContents);
        result.symbolExtractionMs = symbolExtractionMs;
        result.resultAssemblyMs = stageTimer.elapsed();
        applyResultMetadata(&result);
        if (result.cancelled || cancelled()) {
            result.cancelled = true;
            result.workerElapsedMs = workerTimer.elapsed();
            return result;
        }

        SlangManager diagnosticsAnalyzer;
        stageTimer.restart();
        result.diagnostics =
            diagnosticsAnalyzer.extractOverlayWorkspaceDiagnostics(
                analyzedFileContents,
                includeDirs,
                defines,
                cancelled,
                svFiles);
        result.diagnosticsExtractionMs = stageTimer.elapsed();
        result.workerElapsedMs = workerTimer.elapsed();
        if (cancelled()) {
            result.cancelled = true;
            result.diagnostics.clear();
            result.workerElapsedMs = workerTimer.elapsed();
        }
        return result;
    });

    auto* watcher = new QFutureWatcher<WorkspaceAnalysisResult>(this);
    workspaceAnalysisWatcher = watcher;
    connect(watcher,
            &QFutureWatcher<WorkspaceAnalysisResult>::finished,
            this,
            [this,
             watcher,
             workspacePath,
             totalFiles,
             generation,
             epoch,
             analysisRevision,
             revisionFiles]() {
                const bool watcherCancelled = watcher->isCanceled();
                WorkspaceAnalysisResult result;
                if (!watcherCancelled)
                    result = watcher->result();
                if (workspaceAnalysisWatcher == watcher)
                    workspaceAnalysisWatcher = nullptr;
                watcher->deleteLater();

                if (watcherCancelled
                    || generation != workspaceAnalysisGeneration
                    || epoch != workspaceEpoch
                    || result.generation != generation
                    || result.workspaceEpoch != epoch
                    || result.analysisRevision != analysisRevision
                    || !EffectiveValueService::getInstance()
                            ->isComputationCurrent(revisionFiles,
                                                   analysisRevision)
                    || result.cancelled) {
                    emit workspaceAnalysisExpired();
                    return;
                }
                startWorkspacePublication(std::move(result),
                                          totalFiles,
                                          workspacePath);
            });
    watcher->setFuture(future);
}

void SymbolAnalyzer::cancelWorkspaceAnalysisAndWait()
{
    if (workspaceAnalysisCancelFlag)
        workspaceAnalysisCancelFlag->store(true, std::memory_order_relaxed);
    if (!workspaceAnalysisWatcher) {
        workspaceAnalysisCancelFlag.reset();
        return;
    }

    QFutureWatcher<WorkspaceAnalysisResult>* watcher =
        workspaceAnalysisWatcher;
    workspaceAnalysisWatcher = nullptr;
    disconnect(watcher, nullptr, this, nullptr);
    QFuture<WorkspaceAnalysisResult> future = watcher->future();
    watcher->cancel();
    future.waitForFinished();
    delete watcher;
    workspaceAnalysisCancelFlag.reset();
}

void SymbolAnalyzer::expireWorkspaceAnalysis()
{
    ++workspaceAnalysisGeneration;
    const bool hadPendingPublication =
        pendingWorkspacePublication != nullptr;
    cancelWorkspacePublication();
    if (workspaceAnalysisCancelFlag)
        workspaceAnalysisCancelFlag->store(true, std::memory_order_relaxed);
    if (!workspaceAnalysisWatcher || !workspaceAnalysisWatcher->isRunning()) {
        if (hadPendingPublication)
            emit workspaceAnalysisExpired();
        return;
    }

    workspaceAnalysisWatcher->future().cancel();
}

void SymbolAnalyzer::cancelWorkspaceAnalysisAndInvalidate()
{
    cancelAllAnalysesAndWait();
    EffectiveValueService::getInstance()->clearPublishedFacts();
}

void SymbolAnalyzer::requestCancelAllAnalyses()
{
    ++workspaceAnalysisGeneration;
    ++workspaceEpoch;
    if (workspaceAnalysisCancelFlag)
        workspaceAnalysisCancelFlag->store(true, std::memory_order_relaxed);
    for (const auto& cancellation :
         std::as_const(fileAnalysisCancelFlags)) {
        if (cancellation)
            cancellation->store(true, std::memory_order_relaxed);
    }
    overlayWorkspaceFiles.clear();
    overlayWorkspaceIncludeDirs.clear();
    overlayWorkspaceDefines.clear();
    cancelWorkspacePublication();
    if (workspaceAnalysisWatcher)
        workspaceAnalysisWatcher->future().cancel();
    for (QFutureWatcher<FileAnalysisResult>* watcher :
         std::as_const(fileAnalysisWatchers)) {
        if (watcher)
            watcher->future().cancel();
    }
}

void SymbolAnalyzer::cancelAllAnalysesAndWait()
{
    requestCancelAllAnalyses();
    cancelAllFileAnalysesAndWait();
    cancelWorkspaceAnalysisAndWait();
    waitForPublicationRetirements();
}

void SymbolAnalyzer::shutdown()
{
    if (shutdownStarted)
        return;

    // Closing this gate precedes every wait. No caller can start a new worker
    // or publication while teardown drains already-owned work.
    shutdownStarted = true;
    requestCancelAllAnalyses();
    cancelAllFileAnalysesAndWait();
    cancelWorkspaceAnalysisAndWait();

    // Worker watchers and the zero-delay publication timer have now been
    // detached or cancelled. A retirement submitted after this point would
    // indicate a real teardown ordering defect.
    publicationRetirementQueueOpen = false;
    waitForPublicationRetirements();
    publicationRetirementGateForTesting = {};
}

void SymbolAnalyzer::analyzeFileContentAsync(
    const QString& fileName,
    const QString& content,
    std::uint64_t documentRevision)
{
    if (shutdownStarted)
        return;
    analyzeOverlayDocumentsAsync(
        {{fileName, content, documentRevision}});
}

void SymbolAnalyzer::analyzeOverlayDocumentsAsync(
    const QList<OpenDocumentContent>& documents)
{
    if (shutdownStarted)
        return;
    QList<OpenDocumentContent> overlays;
    for (const OpenDocumentContent& document : documents) {
        if (!document.fileName.isEmpty()
            && isSystemVerilogFile(document.fileName)
            && !document.content.isNull()) {
            overlays.append(document);
        }
    }
    if (overlays.isEmpty())
        return;

    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    QHash<QString, OpenDocumentContent> overlaysByKey;
    QStringList overlayKeys;
    for (const OpenDocumentContent& overlay : std::as_const(overlays)) {
        const QString key =
            normalizedAsyncAnalysisFileName(overlay.fileName);
        if (key.isEmpty())
            continue;
        if (!overlaysByKey.contains(key))
            overlayKeys.append(key);
        overlaysByKey.insert(key, overlay);
    }
    overlayKeys.sort(Qt::CaseSensitive);
    if (overlayKeys.isEmpty())
        return;

    // DocumentModel stores editors in a QHash. Pick a deterministic delivery
    // key, then publish completion for every overlay below; no consumer may
    // depend on an arbitrary QHash iteration order.
    const OpenDocumentContent primaryDocument =
        overlaysByKey.value(overlayKeys.constLast());
    const QString fileName = primaryDocument.fileName;
    const QString content = primaryDocument.content;
    const std::uint64_t documentRevision =
        primaryDocument.documentRevision;
    const QString analysisKey = normalizedAsyncAnalysisFileName(fileName);

    QString requestFingerprint;
    for (const QString& key : std::as_const(overlayKeys)) {
        const OpenDocumentContent overlay = overlaysByKey.value(key);
        requestFingerprint += key;
        requestFingerprint += QLatin1Char('\n');
        requestFingerprint += QString::number(overlay.documentRevision);
        requestFingerprint += QLatin1Char('\n');
        requestFingerprint += overlay.content;
        requestFingerprint += QChar(u'\0');
        if (const auto previous = fileAnalysisCancelFlags.value(key))
            previous->store(true, std::memory_order_relaxed);
        fileAnalysisGenerations.insert(
            key, fileAnalysisGenerations.value(key, 0) + 1);
        fileAnalysisCancelFlags.insert(key, cancellation);
    }
    const std::uint64_t generation =
        fileAnalysisGenerations.value(analysisKey, 0);
    const QString expectedContentHash = contentHash(requestFingerprint);
    const std::uint64_t taskWorkspaceEpoch = workspaceEpoch;
    QHash<QString, QString> cachedContentsByKey;
    const std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot =
        SemanticIndex::getInstance()->snapshot();
    if (semanticSnapshot) {
        const QHash<QString, QString> cachedContents =
            semanticSnapshot->fileContents();
        for (auto it = cachedContents.constBegin();
             it != cachedContents.constEnd();
             ++it) {
            cachedContentsByKey.insert(
                normalizedAsyncAnalysisFileName(it.key()), it.value());
        }
    }
    QStringList workspaceFiles = overlayWorkspaceFiles;
    if (workspaceFiles.isEmpty() && semanticSnapshot) {
        workspaceFiles = semanticSnapshot->fileContents().keys();
        workspaceFiles.sort(Qt::CaseInsensitive);
    }
    QSet<QString> workspaceKeys;
    for (const QString& workspaceFile : std::as_const(workspaceFiles)) {
        workspaceKeys.insert(
            normalizedAsyncAnalysisFileName(workspaceFile));
    }
    for (const OpenDocumentContent& overlay : std::as_const(overlays)) {
        const QString key =
            normalizedAsyncAnalysisFileName(overlay.fileName);
        if (!workspaceKeys.contains(key)) {
            workspaceFiles.append(overlay.fileName);
            workspaceKeys.insert(key);
        }
    }
    if (workspaceAnalysisWatcher || pendingWorkspacePublication)
        expireWorkspaceAnalysis();
    const std::uint64_t analysisRevision =
        EffectiveValueService::getInstance()->beginComputation(
            workspaceFiles);
    const QStringList includeDirs = overlayWorkspaceIncludeDirs;
    const QHash<QString, QString> defines = overlayWorkspaceDefines;

    QHash<QString, std::uint64_t> dependencyGenerations;
    for (const QString& workspaceFile : std::as_const(workspaceFiles)) {
        const QString key = normalizedAsyncAnalysisFileName(workspaceFile);
        dependencyGenerations.insert(
            key, fileAnalysisGenerations.value(key, 0));
    }

    // Keep the parse task self-contained; the watcher owns only delivery back to this QObject.
    auto* watcher =
        new QFutureWatcher<FileAnalysisResult>(this);
    watcher->setProperty("analysisKey", analysisKey);
    watcher->setProperty("analysisKeys", overlayKeys);
    fileAnalysisWatchers.insert(watcher);
    connect(watcher,
            &QFutureWatcher<FileAnalysisResult>::finished,
            this,
            [this,
             fileName,
             analysisKey,
             expectedContentHash,
             generation,
             taskWorkspaceEpoch,
             analysisRevision,
             workspaceFiles,
             overlayKeys,
             cancellation,
             watcher]() {
                const auto result = watcher->result();
                fileAnalysisWatchers.remove(watcher);
                watcher->deleteLater();
                for (const QString& key : overlayKeys) {
                    if (fileAnalysisCancelFlags.value(key) == cancellation)
                        fileAnalysisCancelFlags.remove(key);
                }
                if (cancellation->load(std::memory_order_relaxed)
                    || result.cancelled) {
                    return;
                }
                if (fileAnalysisGenerations.value(analysisKey, 0) != generation)
                    return;
                if (result.generation != generation
                    || result.contentHash != expectedContentHash
                    || result.workspaceEpoch != taskWorkspaceEpoch
                    || workspaceEpoch != taskWorkspaceEpoch
                    || result.analysisRevision != analysisRevision) {
                    return;
                }
                for (auto it = result.dependencyGenerations.constBegin();
                     it != result.dependencyGenerations.constEnd();
                     ++it) {
                    if (fileAnalysisGenerations.value(it.key(), 0)
                        != it.value()) {
                        return;
                    }
                }
                if (!EffectiveValueService::getInstance()
                         ->isComputationCurrent(workspaceFiles,
                                                analysisRevision)) {
                    return;
                }
                publishOverlayAnalysisResult(result);

                QHash<QString, int> symbolsByFile;
                for (const WorkspaceFileAnalysis& fileResult :
                     result.workspaceFiles) {
                    symbolsByFile.insert(
                        normalizedAsyncAnalysisFileName(fileResult.fileName),
                        fileResult.symbolRecords.size());
                }
                QSet<QString> completedFiles;
                for (const QString& completedFile : result.diagnosticFiles) {
                    const QString key =
                        normalizedAsyncAnalysisFileName(completedFile);
                    if (key.isEmpty() || completedFiles.contains(key))
                        continue;
                    completedFiles.insert(key);
                    emit analysisCompleted(completedFile,
                                           symbolsByFile.value(key, 0));
                }
            });
    watcher->setFuture(QtConcurrent::run([
        fileName,
        content,
        expectedContentHash,
        generation,
        taskWorkspaceEpoch,
        analysisRevision,
        documentRevision,
        cancellation,
        includeDirs,
        defines,
        workspaceFiles,
        dependencyGenerations,
        overlaysByKey = std::move(overlaysByKey),
        cachedContentsByKey = std::move(cachedContentsByKey)]() {
        FileAnalysisResult result;
        result.fileName = fileName;
        result.content = content;
        result.contentHash = expectedContentHash;
        result.generation = generation;
        result.workspaceEpoch = taskWorkspaceEpoch;
        result.analysisRevision = analysisRevision;
        result.documentRevision = documentRevision;
        result.dependencyGenerations = dependencyGenerations;
        for (auto it = overlaysByKey.constBegin();
             it != overlaysByKey.constEnd();
             ++it) {
            result.documentRevisionsByFile.insert(
                it.key(), it->documentRevision);
            result.diagnosticFiles.append(it->fileName);
        }
        auto cancelled = [&cancellation]() {
            return cancellation->load(std::memory_order_relaxed);
        };
        if (cancelled()) {
            result.cancelled = true;
            return result;
        }
        QHash<QString, QString> overlayWorkspaceContents;
        for (const QString& workspaceFile : workspaceFiles) {
            if (cancelled()) {
                result.cancelled = true;
                return result;
            }
            const QString key =
                normalizedAsyncAnalysisFileName(workspaceFile);
            QString source;
            const auto overlay = overlaysByKey.constFind(key);
            if (overlay != overlaysByKey.constEnd()) {
                source = overlay->content;
            } else if (cachedContentsByKey.contains(key)
                       && !cachedContentsByKey.value(key).isNull()) {
                source = cachedContentsByKey.value(key);
            } else if (!readAsyncAnalysisFile(workspaceFile, &source)) {
                result.cancelled = true;
                return result;
            }
            overlayWorkspaceContents.insert(workspaceFile, source);
        }
        SlangManager symbolAnalyzer;
        QList<EffectiveValueFact> workspaceFacts;
        const QList<SemanticSymbolRecord> workspaceRecords =
            symbolAnalyzer.extractOverlayWorkspaceSymbolRecords(
                overlayWorkspaceContents,
                includeDirs,
                defines,
                cancelled,
                &workspaceFacts,
                workspaceFiles);
        if (cancelled()) {
            result.cancelled = true;
            result.symbolRecords.clear();
            result.workspaceFiles.clear();
            return result;
        }
        const WorkspaceAnalysisResult workspaceResult =
            SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
                workspaceFiles,
                workspaceRecords,
                workspaceFacts,
                cancelled,
                overlayWorkspaceContents);
        result.workspaceFiles = workspaceResult.files;
        const QString target = normalizedAsyncAnalysisFileName(fileName);
        for (const WorkspaceFileAnalysis& fileResult :
             std::as_const(result.workspaceFiles)) {
            if (normalizedAsyncAnalysisFileName(fileResult.fileName)
                != target) {
                continue;
            }
            result.symbolRecords = fileResult.symbolRecords;
            result.effectiveValueFacts = fileResult.effectiveValueFacts;
            break;
        }
        SlangManager diagnosticsAnalyzer;
        const QList<SemanticDiagnostic> workspaceDiagnostics =
            diagnosticsAnalyzer.extractOverlayWorkspaceDiagnostics(
                overlayWorkspaceContents,
                includeDirs,
                defines,
                cancelled,
                workspaceFiles);
        for (const SemanticDiagnostic& diagnostic : workspaceDiagnostics) {
            if (cancelled())
                break;
            if (overlaysByKey.contains(
                    normalizedAsyncAnalysisFileName(diagnostic.fileName))) {
                result.diagnostics.append(diagnostic);
            }
        }
        if (cancelled()) {
            result.cancelled = true;
            result.diagnostics.clear();
        }
        return result;
    }));
}

void SymbolAnalyzer::cancelFileAnalysis(const QString& fileName)
{
    const QString analysisKey =
        normalizedAsyncAnalysisFileName(fileName);
    if (analysisKey.isEmpty())
        return;

    fileAnalysisGenerations.insert(
        analysisKey,
        fileAnalysisGenerations.value(analysisKey, 0) + 1);
    if (const auto cancellation =
            fileAnalysisCancelFlags.value(analysisKey)) {
        cancellation->store(true, std::memory_order_relaxed);
    }

    QList<QFutureWatcher<FileAnalysisResult>*> matching;
    QSet<QString> cancelledKeys;
    for (QFutureWatcher<FileAnalysisResult>* watcher :
         std::as_const(fileAnalysisWatchers)) {
        if (!watcher)
            continue;
        QStringList watcherKeys =
            watcher->property("analysisKeys").toStringList();
        if (watcherKeys.isEmpty())
            watcherKeys.append(
                watcher->property("analysisKey").toString());
        if (watcherKeys.contains(analysisKey)) {
            matching.append(watcher);
            for (const QString& key : std::as_const(watcherKeys))
                cancelledKeys.insert(key);
        }
    }
    for (QFutureWatcher<FileAnalysisResult>* watcher : matching) {
        fileAnalysisWatchers.remove(watcher);
        disconnect(watcher, nullptr, this, nullptr);
        QFuture<FileAnalysisResult> future = watcher->future();
        watcher->cancel();
        future.waitForFinished();
        delete watcher;
    }
    for (const QString& key : std::as_const(cancelledKeys))
        fileAnalysisCancelFlags.remove(key);
}

void SymbolAnalyzer::cancelAllFileAnalysesAndWait()
{
    for (const auto& cancellation :
         std::as_const(fileAnalysisCancelFlags)) {
        if (cancellation)
            cancellation->store(true, std::memory_order_relaxed);
    }

    const QList<QFutureWatcher<FileAnalysisResult>*> watchers =
        fileAnalysisWatchers.values();
    fileAnalysisWatchers.clear();
    for (QFutureWatcher<FileAnalysisResult>* watcher : watchers) {
        if (!watcher)
            continue;
        disconnect(watcher, nullptr, this, nullptr);
        QFuture<FileAnalysisResult> future = watcher->future();
        watcher->cancel();
        future.waitForFinished();
        delete watcher;
    }
    fileAnalysisCancelFlags.clear();
}
