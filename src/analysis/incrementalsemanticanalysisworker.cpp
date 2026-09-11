#include "incrementalsemanticanalysisworker.h"

#include "incrementalanalysisplanservice.h"
#include "semanticchangeclassifier.h"
#include "semanticindexsnapshot.h"
#include "semanticsourceremap.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzerworkspace.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <utility>

namespace {
QString normalizedWorkerPath(const QString& fileName)
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

bool readWorkerFile(const QString& fileName, QString* content)
{
    if (!content)
        return false;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    *content = QTextStream(&file).readAll();
    return true;
}

QHash<QString, QString> normalizedContents(
    const QHash<QString, QString>& contents)
{
    QHash<QString, QString> result;
    for (auto it = contents.constBegin(); it != contents.constEnd(); ++it) {
        const QString key = normalizedWorkerPath(it.key());
        if (!key.isEmpty())
            result.insert(key, it.value());
    }
    return result;
}

QString sourceForFile(
    const QString& fileName,
    const QHash<QString, QString>& overrides,
    const QHash<QString, QString>& snapshotContents,
    bool* ok)
{
    const QString key = normalizedWorkerPath(fileName);
    if (overrides.contains(key)) {
        if (ok)
            *ok = true;
        return overrides.value(key);
    }
    if (snapshotContents.contains(key)) {
        if (ok)
            *ok = true;
        return snapshotContents.value(key);
    }
    QString content;
    const bool read = readWorkerFile(fileName, &content);
    if (ok)
        *ok = read;
    return content;
}

bool loadContents(const QStringList& files,
                  const QHash<QString, QString>& overrides,
                  const QHash<QString, QString>& snapshotContents,
                  const std::function<bool()>& cancelled,
                  QHash<QString, QString>* contents,
                  QString* error)
{
    if (!contents)
        return false;
    for (const QString& fileName : files) {
        if (cancelled && cancelled())
            return false;
        bool ok = false;
        const QString content = sourceForFile(fileName,
                                              overrides,
                                              snapshotContents,
                                              &ok);
        if (!ok) {
            if (error) {
                *error = QStringLiteral("Unable to read semantic source: %1")
                             .arg(fileName);
            }
            return false;
        }
        contents->insert(fileName, content);
    }
    return true;
}

QString contentByFile(const QHash<QString, QString>& contents,
                      const QString& fileName)
{
    const QString target = normalizedWorkerPath(fileName);
    for (auto it = contents.constBegin(); it != contents.constEnd(); ++it) {
        if (normalizedWorkerPath(it.key()) == target)
            return it.value();
    }
    return QString();
}

bool containsFile(const QStringList& files, const QString& fileName)
{
    const QString target = normalizedWorkerPath(fileName);
    for (const QString& candidate : files) {
        if (normalizedWorkerPath(candidate) == target)
            return true;
    }
    return false;
}

QList<SemanticSymbolRecord> recordsWithRevisions(
    QList<SemanticSymbolRecord> records,
    std::uint64_t computationRevision,
    std::uint64_t documentRevision)
{
    for (SemanticSymbolRecord& record : records) {
        record.presentation.computationRevision = computationRevision;
        record.presentation.documentRevision = documentRevision;
    }
    return records;
}

QList<EffectiveValueFact> factsWithRevisions(
    QList<EffectiveValueFact> facts,
    std::uint64_t computationRevision,
    std::uint64_t documentRevision)
{
    for (EffectiveValueFact& fact : facts) {
        fact.fileName = normalizedWorkerPath(fact.fileName);
        fact.computationRevision = computationRevision;
        fact.documentRevision = documentRevision;
    }
    return facts;
}

QList<SemanticRelationship> semanticRelationships(
    const QVector<RelationshipToAdd>& relationships)
{
    QList<SemanticRelationship> result;
    result.reserve(relationships.size());
    for (const RelationshipToAdd& relationship : relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        SemanticRelationship item;
        item.fromId = relationship.fromId;
        item.toId = relationship.toId;
        item.type = relationship.type;
        item.evidenceText = relationship.context;
        item.confidence = relationship.confidence;
        item.evidenceRange = relationship.evidenceRange;
        item.fromAccessPath = relationship.fromAccessPath;
        item.toAccessPath = relationship.toAccessPath;
        item.exactValueForward = relationship.exactValueForward;
        item.provenance = RelationshipProvenance::SlangExtracted;
        result.append(std::move(item));
    }
    return result;
}

const RelationshipExtractionInfo* relationshipInfoForFile(
    const QHash<QString, RelationshipExtractionInfo>& infoByFile,
    const QString& fileName)
{
    const QString target = normalizedWorkerPath(fileName);
    for (auto it = infoByFile.constBegin(); it != infoByFile.constEnd(); ++it) {
        if (normalizedWorkerPath(it.key()) == target)
            return &it.value();
    }
    return nullptr;
}

std::uint64_t documentRevisionForFile(
    const SemanticAnalysisRequest& request,
    const QString& fileName)
{
    const QString target = normalizedWorkerPath(fileName);
    for (auto it = request.documentRevisions.constBegin();
         it != request.documentRevisions.constEnd();
         ++it) {
        if (normalizedWorkerPath(it.key()) == target)
            return it.value();
    }
    return 0;
}

void prepareRelationshipDelta(WorkspaceAnalysisResult* result)
{
    if (!result || !result->preparedSnapshot)
        return;

    QElapsedTimer timer;
    timer.start();

    // Relationship extraction and owner-scoped replacement remain
    // incremental in the prepared immutable snapshot. Build the derived query
    // graph from that final snapshot on the worker so GUI publication is a
    // pure state swap even for a one-file delta.
    result->preparedRelationshipState =
        SymbolRelationshipEngine::prepareRelationshipState(
            result->preparedSnapshot->symbolRecordsView(),
            result->preparedSnapshot->relationshipsView());
    result->relationshipDeltaPrepared = true;
    result->relationshipStateBuildMs = timer.elapsed();
}
}

WorkspaceAnalysisResult IncrementalSemanticAnalysisWorker::analyze(
    const SemanticAnalysisRequest& request,
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
    const SemanticDependencyGraph& dependencyGraph,
    const std::function<bool()>& isCancelled)
{
    QElapsedTimer workerTimer;
    workerTimer.start();
    WorkspaceAnalysisResult result;
    result.request = request;
    result.generation = request.generation;
    result.analysisRevision = request.computationRevision;
    for (auto revision = request.documentRevisions.constBegin();
         revision != request.documentRevisions.constEnd(); ++revision) {
        result.documentRevisionsByFile.insert(
            normalizedWorkerPath(revision.key()), revision.value());
    }
    auto cancelled = [&]() { return isCancelled && isCancelled(); };
    auto finish = [&]() {
        result.workerElapsedMs = workerTimer.elapsed();
        return result;
    };
    auto finishCancelled = [&]() {
        result.cancelled = true;
        result.preparedSnapshot.reset();
        return finish();
    };

    if (!request.isValid()) {
        result.error = QStringLiteral("Invalid semantic analysis request");
        return finish();
    }

    const QHash<QString, QString> overrides =
        normalizedContents(request.sourceOverrides);
    const QHash<QString, QString> baselineSnapshotContents = baseSnapshot
        ? normalizedContents(baseSnapshot->fileContents())
        : QHash<QString, QString>();
    QHash<QString, QString> sourceSnapshotContents =
        baselineSnapshotContents;
    if (request.reason == SemanticAnalysisReason::WorkspaceOpen
        || request.reason == SemanticAnalysisReason::WorkspaceConfiguration
        || request.impactHint == SemanticChangeImpact::WorkspaceConfig) {
        sourceSnapshotContents.clear();
    } else {
        for (const QString& changedFile : request.changedFiles)
            sourceSnapshotContents.remove(normalizedWorkerPath(changedFile));
    }

    QStringList semanticChangedFiles = request.changedFiles;
    if (semanticChangedFiles.isEmpty() && !request.triggerFile.isEmpty())
        semanticChangedFiles.append(request.triggerFile);
    const bool workspaceWideRequest =
        request.impactHint == SemanticChangeImpact::WorkspaceConfig
        || request.reason == SemanticAnalysisReason::WorkspaceOpen
        || request.reason == SemanticAnalysisReason::WorkspaceConfiguration;
    QHash<QString, QString> changedTextByFile;
    if (!workspaceWideRequest) {
        for (const QString& changedFile : semanticChangedFiles) {
            bool newTextOk = false;
            const QString changedText = sourceForFile(
                changedFile,
                overrides,
                sourceSnapshotContents,
                &newTextOk);
            if (!newTextOk) {
                result.error = QStringLiteral(
                    "Unable to capture changed source: %1")
                                   .arg(changedFile);
                return finish();
            }
            changedTextByFile.insert(normalizedWorkerPath(changedFile),
                                     changedText);
        }
    }

    QString classificationFile = request.triggerFile;
    if (classificationFile.isEmpty() && !semanticChangedFiles.isEmpty())
        classificationFile = semanticChangedFiles.constFirst();
    const QString newText = changedTextByFile.value(
        normalizedWorkerPath(classificationFile));
    const QString oldText = baselineSnapshotContents.value(
        normalizedWorkerPath(classificationFile));

    SemanticChangeClassification classification;
    if (workspaceWideRequest) {
        classification.impact = SemanticChangeImpact::WorkspaceConfig;
    } else if (semanticChangedFiles.size() > 1) {
        classification.impact = SemanticChangeImpact::FullFallback;
        classification.fallbackReason = QStringLiteral(
            "Multiple pending clean changes were coalesced conservatively");
    } else if (!baseSnapshot
               || !baselineSnapshotContents.contains(
                   normalizedWorkerPath(classificationFile))) {
        classification.impact = SemanticChangeImpact::FullFallback;
        classification.fallbackReason =
            QStringLiteral("No authoritative baseline snapshot is available");
    } else {
        classification = SemanticChangeClassifier().classify(
            classificationFile, oldText, newText);
    }

    QHash<QString, QString> allContents;
    SemanticDependencyGraph nextGraph = dependencyGraph;
    if (workspaceWideRequest) {
        if (!loadContents(request.project.systemVerilogFiles,
                          overrides,
                          sourceSnapshotContents,
                          cancelled,
                          &allContents,
                          &result.error)) {
            return cancelled() ? finishCancelled() : finish();
        }
        nextGraph = SemanticDependencyGraph::build(request.project,
                                                   allContents);
    } else if (!nextGraph.isValidFor(request.project)) {
        if (!loadContents(request.project.systemVerilogFiles,
                          overrides,
                          sourceSnapshotContents,
                          cancelled,
                          &allContents,
                          &result.error)) {
            return cancelled() ? finishCancelled() : finish();
        }
        nextGraph = SemanticDependencyGraph::build(request.project,
                                                   allContents);
    } else {
        for (const QString& changedFile : semanticChangedFiles) {
            nextGraph = nextGraph.withUpdatedFile(
                request.project,
                changedFile,
                changedTextByFile.value(normalizedWorkerPath(changedFile)));
        }
    }
    result.dependencyGraph = nextGraph;
    result.incrementalPlan = IncrementalAnalysisPlanService().plan(
        request, classification, dependencyGraph, nextGraph);

    std::uint64_t computationRevision = request.computationRevision;

    if (cancelled())
        return finishCancelled();

    if (result.incrementalPlan.impact == SemanticChangeImpact::TriviaOnly) {
        if (!baseSnapshot) {
            result.error = QStringLiteral(
                "Trivia remap requires an authoritative snapshot");
            return finish();
        }
        const std::uint64_t revision =
            documentRevisionForFile(request, request.triggerFile);
        // Capture the previously published facts before registering the new
        // computation revision; registration intentionally makes older facts
        // stale for normal semantic recomputation.
        QList<EffectiveValueFact> facts =
            EffectiveValueService::getInstance()->factsForDocument(
                request.triggerFile, oldText);
        if (computationRevision == 0) {
            computationRevision =
                EffectiveValueService::getInstance()->beginComputation(
                    result.incrementalPlan.affectedFiles);
        }
        result.analysisRevision = computationRevision;
        result.request.computationRevision = computationRevision;
        result.preparedSnapshot = SemanticSourceRemapper::remapSnapshot(
            baseSnapshot,
            request.triggerFile,
            oldText,
            newText,
            classification.delta,
            revision);
        result.effectiveFactsByFile.insert(
            request.triggerFile,
            factsWithRevisions(
                SemanticSourceRemapper::remapEffectiveFacts(
                    std::move(facts),
                    request.triggerFile,
                    oldText,
                    newText,
                    classification.delta,
                    revision),
                computationRevision,
                revision));
        result.effectiveContentFingerprintsByFile.insert(
            request.triggerFile,
            EffectiveValueService::documentContentFingerprint(newText));
        result.totalSymbols = result.preparedSnapshot
            ? result.preparedSnapshot->symbolRecordCount()
            : 0;
        prepareRelationshipDelta(&result);
        return finish();
    }

    if (computationRevision == 0) {
        computationRevision =
            EffectiveValueService::getInstance()->beginComputation(
                result.incrementalPlan.affectedFiles);
    }
    result.analysisRevision = computationRevision;
    result.request.computationRevision = computationRevision;

    if (result.incrementalPlan.fullWorkspace
        && allContents.size() < request.project.systemVerilogFiles.size()) {
        if (!loadContents(request.project.systemVerilogFiles,
                          overrides,
                          sourceSnapshotContents,
                          cancelled,
                          &allContents,
                          &result.error)) {
            return cancelled() ? finishCancelled() : finish();
        }
    }

    QHash<QString, QString> compilationContents;
    if (!loadContents(result.incrementalPlan.compilationFiles,
                      overrides,
                      sourceSnapshotContents,
                      cancelled,
                      &compilationContents,
                      &result.error)) {
        return cancelled() ? finishCancelled() : finish();
    }
    result.slangInvoked = true;

    QElapsedTimer stageTimer;
    stageTimer.start();
    SlangManager symbolManager;
    QList<EffectiveValueFact> allFacts;
    const QList<SemanticSymbolRecord> allRecords =
        symbolManager.extractOverlayWorkspaceSymbolRecords(
            compilationContents,
            request.project.includeDirs,
            request.project.defines,
            cancelled,
            &allFacts,
            result.incrementalPlan.compilationFiles);
    result.symbolExtractionMs = stageTimer.elapsed();
    if (cancelled())
        return finishCancelled();

    stageTimer.restart();
    WorkspaceAnalysisResult grouped =
        SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
            result.incrementalPlan.compilationFiles,
            allRecords,
            allFacts,
            cancelled,
            compilationContents);
    result.resultAssemblyMs = stageTimer.elapsed();
    if (cancelled() || grouped.cancelled)
        return finishCancelled();

    stageTimer.restart();
    SlangManager diagnosticManager;
    const QList<SemanticDiagnostic> allDiagnostics =
        diagnosticManager.extractOverlayWorkspaceDiagnostics(
            compilationContents,
            request.project.includeDirs,
            request.project.defines,
            cancelled,
            result.incrementalPlan.compilationFiles);
    result.diagnosticsExtractionMs = stageTimer.elapsed();
    if (cancelled())
        return finishCancelled();

    QList<SemanticFileSymbolUpdate> updates;
    QList<SemanticDiagnostic> affectedDiagnostics;
    for (WorkspaceFileAnalysis& fileResult : grouped.files) {
        if (!containsFile(result.incrementalPlan.affectedFiles,
                          fileResult.fileName)) {
            continue;
        }
        const std::uint64_t documentRevision =
            documentRevisionForFile(request, fileResult.fileName);
        SemanticFileSymbolUpdate update;
        update.fileName = fileResult.fileName;
        update.content = fileResult.content;
        update.symbolRecords = recordsWithRevisions(
            std::move(fileResult.symbolRecords),
            computationRevision,
            documentRevision);
        result.totalSymbols += update.symbolRecords.size();
        updates.append(std::move(update));

        result.effectiveFactsByFile.insert(
            fileResult.fileName,
            factsWithRevisions(
                std::move(fileResult.effectiveValueFacts),
                computationRevision,
                documentRevision));
        result.effectiveContentFingerprintsByFile.insert(
            fileResult.fileName,
            EffectiveValueService::documentContentFingerprint(
                fileResult.content));
    }
    for (const SemanticDiagnostic& diagnostic : allDiagnostics) {
        if (containsFile(result.incrementalPlan.affectedFiles,
                         diagnostic.fileName)) {
            SemanticDiagnostic current = diagnostic;
            current.computationRevision = computationRevision;
            current.documentRevision =
                documentRevisionForFile(request, diagnostic.fileName);
            affectedDiagnostics.append(std::move(current));
        }
    }
    result.diagnostics = affectedDiagnostics;

    const SemanticIndexSnapshot emptySnapshot =
        SemanticIndexSnapshot::fromSymbolRecords({}, {}, {}, {});
    const SemanticIndexSnapshot& base =
        result.incrementalPlan.authoritativeWorkspaceReplace || !baseSnapshot
        ? emptySnapshot
        : *baseSnapshot;
    SemanticIndexSnapshot preliminary = base.withReplacedFiles(
        updates,
        result.incrementalPlan.affectedFiles,
        affectedDiagnostics,
        result.incrementalPlan.relationshipFiles,
        {});

    stageTimer.restart();
    SlangManager relationshipSlang;
    const QHash<QString, RelationshipExtractionInfo> infoByFile =
        relationshipSlang.extractOverlayWorkspaceRelationshipInfo(
            compilationContents,
            request.project.includeDirs,
            request.project.defines,
            cancelled,
            result.incrementalPlan.compilationFiles);
    result.relationshipExtractionMs = stageTimer.elapsed();
    if (cancelled())
        return finishCancelled();

    stageTimer.restart();
    SmartRelationshipBuilder relationshipBuilder(
        nullptr,
        &relationshipSlang,
        [&preliminary](const QString& fileName) {
            return preliminary.getSymbolRecords(fileName);
        });
    QList<SemanticRelationship> newRelationships;
    for (const QString& fileName : result.incrementalPlan.relationshipFiles) {
        if (cancelled()) {
            relationshipBuilder.cancelAnalysis();
            return finishCancelled();
        }
        const QString content = contentByFile(compilationContents, fileName);
        const QVector<RelationshipToAdd> computed =
            relationshipBuilder.computeRelationships(
                fileName,
                content,
                preliminary.getSymbolRecords(fileName),
                &preliminary,
                request.project.includeDirs,
                request.project.defines,
                relationshipInfoForFile(infoByFile, fileName));
        newRelationships.append(semanticRelationships(computed));
    }
    result.relationshipBuildMs = stageTimer.elapsed();
    result.preparedSnapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            preliminary.withRelationshipsReplacingFiles(
                result.incrementalPlan.relationshipFiles,
                newRelationships));
    prepareRelationshipDelta(&result);
    return finish();
}
