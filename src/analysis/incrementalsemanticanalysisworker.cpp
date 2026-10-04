#include "incrementalsemanticanalysisworker.h"
#include "triviapositionmap.h"

#include "incrementalanalysisplanservice.h"
#include "semanticchangeclassifier.h"
#include "semanticindexsnapshot.h"
#include "semanticsourceremap.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzerworkspace.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>

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

bool snapshotInputsCoveredBy(const SemanticIndexSnapshot& snapshot,
                             const SemanticAnalysisInput& input)
{
    // A retained workspace can contain a later standalone overlay. Its whole
    // snapshot is restorable only when every owned input was recaptured too.
    for (const QString& file : snapshot.symbolFiles())
        if (!input.sources.contains(file))
            return false;
    const auto& contents = snapshot.fileContentsView();
    for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
        const auto source = input.sources.constFind(it.key());
        if (source == input.sources.cend() || !source->readable || source->text != it.value())
            return false;
    }
    for (const auto& diagnostic : snapshot.rawDiagnostics())
        if (!diagnostic.fileName.isEmpty()
            && !input.sources.contains(normalizedWorkerPath(diagnostic.fileName)))
            return false;
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

template<typename Value>
QHash<QString, const Value*> indexByNormalizedPath(
    const QHash<QString, Value>& values)
{
    QHash<QString, const Value*> indexed;
    indexed.reserve(values.size());
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString key = normalizedWorkerPath(it.key());
        // Keep the same first match as the former per-file linear lookup.
        // The owner stays immutable throughout this analysis request.
        if (!indexed.contains(key))
            indexed.insert(key, &it.value());
    }
    return indexed;
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

    // The engine is a query adapter over the authoritative shard indexes.
    // There is no second workspace-wide graph to rebuild or synchronize.
    result->preparedRelationshipState = std::make_shared<
        SymbolRelationshipEngine::PreparedRelationshipState>();
    result->preparedRelationshipState->snapshot = result->preparedSnapshot;
    result->relationshipDeltaPrepared = true;
    result->relationshipStateBuildMs = timer.elapsed();
}
}

WorkspaceAnalysisResult IncrementalSemanticAnalysisWorker::analyze(
    const SemanticAnalysisRequest& originalRequest,
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot,
    const SemanticDependencyGraph& dependencyGraph,
    const std::function<bool()>& isCancelled,
    std::shared_ptr<const SemanticAnalysisInput> baseInput,
    std::shared_ptr<const PublishedWorkspaceSemanticState> retainedState)
{
    SemanticAnalysisRequest request = originalRequest;
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

    SemanticInputCapture input(request);
    if (!input.capture(request.project.systemVerilogFiles, cancelled)
        || !input.capture(request.changedFiles, cancelled)
        || (baseInput && !input.capture(baseInput->sources.keys(), cancelled)))
        return finishCancelled();
    QHash<QString, QString> allContents = input.contents();
    const bool replacesWorkspace = request.project.isOpen()
        && (request.reason == SemanticAnalysisReason::WorkspaceOpen
            || request.reason == SemanticAnalysisReason::WorkspaceConfiguration
            || request.impactHint == SemanticChangeImpact::WorkspaceConfig);
    if (!request.triviaOnlyGate && retainedState
        && retainedState->input && retainedState->snapshot
        && baseSnapshot == retainedState->snapshot
        && retainedState->policy.enabled == request.runtimePolicy.enabled
        && retainedState->policy.planningMode == request.runtimePolicy.planningMode
        && (!replacesWorkspace
            || snapshotInputsCoveredBy(*baseSnapshot, input.input()))
        && EffectiveValueService::getInstance()->isComputationCurrent(
            request.project.systemVerilogFiles, retainedState->analysisRevision)
        && input.input().equivalentTo(*retainedState->input)
        && input.stillMatchesDisk(cancelled)) {
        result.preparedSnapshot = retainedState->snapshot;
        result.dependencyGraph = retainedState->dependencyGraph;
        result.preparedEffectiveFactsState = retainedState->facts;
        result.analysisRevision = retainedState->analysisRevision;
        result.request.computationRevision = result.analysisRevision;
        result.totalSymbols = result.preparedSnapshot->symbolRecordCount();
        result.incrementalPlan.reason = request.reason;
        result.incrementalPlan.impact = SemanticChangeImpact::WorkspaceConfig;
        result.incrementalPlan.affectedFiles = request.project.systemVerilogFiles;
        result.incrementalPlan.changedFiles = request.changedFiles;
        result.incrementalPlan.authoritativeWorkspaceReplace = replacesWorkspace;
        result.input = input.seal();
        result.reusedWorkspace = true;
        // Common preparation reselects this snapshot's own raw diagnostics,
        // including when only the requested display limit has changed.
        prepareRelationshipDelta(&result);
        return finish();
    }
    // The selected base owns its uncapped diagnostics. A retained scope may
    // supply inputs/graph while referring to a different, older publication;
    // it must never overwrite diagnostics of the current visible baseline.
    const QHash<QString, QString> baselineSnapshotContents = baseSnapshot
        ? normalizedContents(baseSnapshot->fileContents()) : QHash<QString, QString>{};
    bool workspaceWideRequest = !baseSnapshot
        || request.reason == SemanticAnalysisReason::WorkspaceConfiguration
        || request.impactHint == SemanticChangeImpact::WorkspaceConfig
        || request.reason == SemanticAnalysisReason::WorkspaceOpen;
    QStringList semanticChangedFiles = request.changedFiles;
    if (semanticChangedFiles.isEmpty() && !request.triggerFile.isEmpty())
        semanticChangedFiles.append(request.triggerFile);
    bool externalLookupChanged = false;
    if (baseInput) {
        QSet<QString> listed;
        for (const QString& file : request.project.systemVerilogFiles)
            listed.insert(normalizedWorkerPath(file));
        for (auto it = baseInput->sources.cbegin(); it != baseInput->sources.cend(); ++it) {
            const auto& fresh = input.source(it.key());
            if (fresh.readable == it->readable && fresh.text == it->text)
                continue;
            if (!listed.contains(it.key()))
                externalLookupChanged = true;
            if (!semanticChangedFiles.contains(it.key()))
                semanticChangedFiles.append(it.key());
        }
    }
    // Read all configured sources once per request. Watch events are hints;
    // missed external changes must not leave a dependent shard current.
    for (const QString& file : request.project.systemVerilogFiles) {
        const QString key = normalizedWorkerPath(file);
        if (!input.source(key).readable) {
            result.error = QStringLiteral("Unable to capture semantic source: %1").arg(file);
            return finish();
        }
        if (baseSnapshot && baselineSnapshotContents.value(key) != allContents.value(key)
            && !semanticChangedFiles.contains(file))
            semanticChangedFiles.append(file);
    }
    QSet<QString> changedKeys;
    QStringList uniqueChanges;
    for (const QString& file : semanticChangedFiles) {
        const QString key = normalizedWorkerPath(file);
        if (!key.isEmpty() && !changedKeys.contains(key)) {
            changedKeys.insert(key);
            uniqueChanges.append(file);
        }
    }
    semanticChangedFiles = std::move(uniqueChanges);
    request.changedFiles = semanticChangedFiles;
    QHash<QString, SemanticChangeClassification> classifications;
    const QString classificationFile = request.triggerFile.isEmpty()
        ? semanticChangedFiles.value(0) : request.triggerFile;
    const QString oldText = baselineSnapshotContents.value(normalizedWorkerPath(classificationFile));
    const QString newText = allContents.value(normalizedWorkerPath(classificationFile));
    for (const QString& file : semanticChangedFiles) {
        const QString key = normalizedWorkerPath(file);
        SemanticChangeClassification current;
        if (workspaceWideRequest) {
            current.impact = SemanticChangeImpact::WorkspaceConfig;
        } else if (externalLookupChanged) {
            current.impact = SemanticChangeImpact::FullFallback;
            current.fallbackReason = QStringLiteral("An include lookup or indirect source changed");
        } else if (!baselineSnapshotContents.contains(key)) {
            current.impact = SemanticChangeImpact::FullFallback;
            current.fallbackReason = QStringLiteral("No authoritative baseline exists for the changed source");
        } else {
            current = SemanticChangeClassifier().classify(file,
                baselineSnapshotContents.value(key), allContents.value(key), cancelled);
            if (cancelled()) return finishCancelled();
            if (current.impact == SemanticChangeImpact::HeaderMacro) {
                current.impact = SemanticChangeImpact::FullFallback;
                current.fallbackReason = QStringLiteral("Preprocessor state can affect later ordered compilation-unit sources");
            }
        }
        classifications.insert(key, current);
    }
    const auto classification = classifications.value(normalizedWorkerPath(classificationFile));
    if (!workspaceWideRequest && request.triggerFile.isEmpty() && semanticChangedFiles.size() == 1)
        request.triggerFile = classificationFile;
    result.request = request;

    // Reject before dependency extraction, computation registration or Slang.
    // Even the ordinary trivia path must never publish a fallback position map.
    if ((request.triviaOnlyGate
         && (semanticChangedFiles.size() != 1
             || classification.impact != SemanticChangeImpact::TriviaOnly
             || classification.oldTreeHasErrors
             || classification.newTreeHasErrors))
        || (classification.impact == SemanticChangeImpact::TriviaOnly
            && !TriviaPositionMap(oldText, newText, classification.delta)
                    .isCompatible())) {
        result.disposition =
            SemanticAnalysisRequestDisposition::TriviaGateRejected;
        return finish();
    }

    SemanticDependencyGraph nextGraph = dependencyGraph.isValidFor(request.project)
        ? dependencyGraph.withUpdatedFiles(request.project, allContents, semanticChangedFiles, cancelled)
        : SemanticDependencyGraph::build(request.project, allContents, cancelled);
    result.dependencyGraph = nextGraph;
    result.incrementalPlan = IncrementalAnalysisPlanService().planChanges(
        request, classifications, dependencyGraph, nextGraph);
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
            EffectiveValueService::capturedFacts(
                retainedState ? retainedState->facts : nullptr, request.triggerFile, oldText);
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
            revision,
            computationRevision);
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
        if (!input.stillMatchesDisk(cancelled)) {
            result.preparedSnapshot.reset();
            result.disposition = SemanticAnalysisRequestDisposition::InputChanged;
            return cancelled() ? finishCancelled() : finish();
        }
        result.input = input.seal();
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

    // fromBuffers is one ordered compilation unit. Recompile its complete
    // context to preserve CU imports/directives/macros; publication remains a
    // dependency-planned delta. Included headers are not added as root buffers.
    result.incrementalPlan.compilationFiles = request.project.systemVerilogFiles;
    if (result.incrementalPlan.compilationFiles.isEmpty() && !request.project.isOpen())
        result.incrementalPlan.compilationFiles = request.changedFiles;
    result.incrementalPlan.compilationContextReason = QStringLiteral(
        "Preserve the existing ordered Slang compilation unit; publish only affected shards");
    result.slangInvoked = !result.incrementalPlan.compilationFiles.isEmpty();

    QElapsedTimer stageTimer;
    stageTimer.start();
    SlangManager slang;
    auto compiled = slang.analyzeCapturedWorkspace(input,
        result.incrementalPlan.compilationFiles, request.project.includeDirs,
        request.project.defines, request.project.topModule, cancelled);
    if (compiled.cancelled)
        return finishCancelled();
    if (!compiled.error.isEmpty()) {
        result.error = compiled.error;
        return finish();
    }
    const auto& allRecords = compiled.symbols;
    const auto& allFacts = compiled.effectiveFacts;
    result.symbolExtractionMs = stageTimer.elapsed();
    if (cancelled())
        return finishCancelled();

    // Only buffers actually used by this compilation become semantic files.
    // The input envelope also retains negative lookup observations for reuse
    // validation, without publishing those candidates as empty source files.
    const auto compilationContents = input.contents();
    QStringList assemblyFiles = result.incrementalPlan.compilationFiles;
    QSet<QString> rootKeys;
    for (const QString& file : assemblyFiles)
        rootKeys.insert(normalizedWorkerPath(file));
    QStringList includedFiles;
    for (auto it = compiled.includesByFile.cbegin(); it != compiled.includesByFile.cend(); ++it)
        if (!rootKeys.contains(it.key()))
            includedFiles.append(it.key());
    includedFiles.sort(Qt::CaseSensitive);
    assemblyFiles.append(includedFiles);
    for (const QString& file : includedFiles) {
        if (!result.incrementalPlan.affectedFiles.contains(file))
            result.incrementalPlan.affectedFiles.append(file);
        if (!result.incrementalPlan.relationshipFiles.contains(file))
            result.incrementalPlan.relationshipFiles.append(file);
    }
    QStringList removedIncludes;
    if (baseSnapshot) {
        for (const QString& file : baseSnapshot->symbolFiles()) {
            // A normal delta owns its captured scope, not every row currently
            // visible. Only workspace activation/configuration removes all.
            const bool belongsToScope = result.incrementalPlan.authoritativeWorkspaceReplace
                || (baseInput && baseInput->sources.contains(file)
                    && baseInput->sources.value(file).readable);
            if (belongsToScope && !rootKeys.contains(file) && !compiled.includesByFile.contains(file)) {
                removedIncludes.append(file);
                result.incrementalPlan.affectedFiles.append(file);
                result.incrementalPlan.relationshipFiles.append(file);
            }
        }
    }
    stageTimer.restart();
    WorkspaceAnalysisResult grouped =
        SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
            assemblyFiles, allRecords, allFacts, cancelled, compilationContents);
    result.resultAssemblyMs = stageTimer.elapsed();
    if (cancelled() || grouped.cancelled)
        return finishCancelled();

    const auto& allDiagnostics = compiled.diagnostics;

    QList<SemanticFileSymbolUpdate> updates;
    for (const QString& file : removedIncludes) {
        SemanticFileSymbolUpdate removal;
        removal.fileName = file;
        removal.removed = true;
        updates.append(std::move(removal));
        result.effectiveFactsByFile.insert(file, {});
        result.effectiveContentFingerprintsByFile.insert(file,
            EffectiveValueService::documentContentFingerprint({}));
    }
    QList<SemanticDiagnostic> affectedDiagnostics;
    QSet<QString> affectedFileKeys;
    affectedFileKeys.reserve(result.incrementalPlan.affectedFiles.size());
    for (const QString& fileName : result.incrementalPlan.affectedFiles)
        affectedFileKeys.insert(normalizedWorkerPath(fileName));
    for (WorkspaceFileAnalysis& fileResult : grouped.files) {
        if (!affectedFileKeys.contains(normalizedWorkerPath(fileResult.fileName))) {
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
        if (affectedFileKeys.contains(normalizedWorkerPath(diagnostic.fileName))) {
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

    const auto& infoByFile = compiled.relationships;

    stageTimer.restart();
    SmartRelationshipBuilder relationshipBuilder(
        nullptr,
        &slang,
        [&preliminary](const QString& fileName) {
            return preliminary.getSymbolRecords(fileName);
        });
    const auto contentsByKey = indexByNormalizedPath(compilationContents);
    const auto infoByKey = indexByNormalizedPath(infoByFile);
    const RelationshipExtractionInfo emptyRelationshipInfo;
    QList<SemanticRelationship> newRelationships;
    for (const QString& fileName : result.incrementalPlan.relationshipFiles) {
        if (cancelled()) {
            relationshipBuilder.cancelAnalysis();
            return finishCancelled();
        }
        const QString fileKey = normalizedWorkerPath(fileName);
        if (removedIncludes.contains(fileKey))
            continue;
        const QString* content = contentsByKey.value(fileKey, nullptr);
        const QVector<RelationshipToAdd> computed =
            relationshipBuilder.computeRelationships(
                fileName,
                content ? *content : QString(),
                preliminary.getSymbolRecords(fileName),
                &preliminary,
                request.project.includeDirs,
                request.project.defines,
                infoByKey.value(fileKey, &emptyRelationshipInfo));
        newRelationships.append(semanticRelationships(computed));
    }
    result.relationshipBuildMs = stageTimer.elapsed();
    result.preparedSnapshot =
        std::make_shared<const SemanticIndexSnapshot>(
            preliminary.withRelationshipsReplacingFiles(
                result.incrementalPlan.relationshipFiles,
                newRelationships));
    // Include buffers are now complete, including indirect and macro-generated
    // includes. Extract dependency facts from exactly the bytes Slang saw.
    QHash<QString, QString> semanticContents;
    for (const QString& file : assemblyFiles) {
        const QString key = normalizedWorkerPath(file);
        semanticContents.insert(key, compilationContents.value(key));
    }
    QStringList changedSources = removedIncludes;
    const auto knownFacts = nextGraph.fileFacts();
    for (auto it = semanticContents.cbegin(); it != semanticContents.cend(); ++it)
        if (!knownFacts.contains(it.key()))
            changedSources.append(it.key());
    for (auto it = knownFacts.cbegin(); it != knownFacts.cend(); ++it)
        if (!semanticContents.contains(it.key()) && !changedSources.contains(it.key()))
            changedSources.append(it.key());
    result.dependencyGraph = nextGraph.withUpdatedFiles(request.project, semanticContents, changedSources)
        .withObservedIncludes(compiled.includesByFile);
    result.totalSymbols = result.preparedSnapshot->symbolRecordCount();
    if (!input.stillMatchesDisk(cancelled)) {
        result.preparedSnapshot.reset();
        result.disposition = SemanticAnalysisRequestDisposition::InputChanged;
        return cancelled() ? finishCancelled() : finish();
    }
    result.input = input.seal();
    prepareRelationshipDelta(&result);
    return finish();
}
