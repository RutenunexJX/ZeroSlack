#include "symbolanalyzer.h"

#include "slangmanager.h"
#include "symbolanalyzerworkspace.h"
#include "workspacemanager.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <utility>

void SymbolAnalyzer::analyzeOpenDocuments(
    const QList<OpenDocumentContent>& documents)
{
    if (shutdownStarted)
        return;
    emit analysisStarted("open_tabs");

    // Multiple open buffers can depend on one another (for example, a dirty
    // package and a module that imports it). They must be compiled and
    // published as one overlay workspace even before a ProjectModel has
    // populated overlayWorkspaceFiles.
    if (!overlayWorkspaceFiles.isEmpty() || documents.size() > 1) {
        analyzeOverlayDocumentsAsync(documents);
        return;
    }

    QStringList svFiles;
    int symbolsFromOpenFiles = 0;
    QList<SemanticDiagnostic> diagnostics;
    QStringList analyzedDocumentFiles;
    for (const OpenDocumentContent& document : documents) {
        if (isSystemVerilogFile(document.fileName)
            && !document.content.isNull()) {
            analyzedDocumentFiles.append(document.fileName);
        }
    }
    const std::uint64_t computationRevision =
        EffectiveValueService::getInstance()->beginComputation(
            analyzedDocumentFiles);

    for (const OpenDocumentContent& document : documents) {
        const QString& fileName = document.fileName;
        const QString& content = document.content;
        if (!isSystemVerilogFile(fileName) || content.isNull())
            continue;

        svFiles.append(fileName);
        SlangManager symbolAnalyzer;
        QList<EffectiveValueFact> effectiveValueFacts;
        const QList<SemanticSymbolRecord> records =
            symbolAnalyzer.extractSymbolRecords(fileName,
                                                content,
                                                {},
                                                {},
                                                &effectiveValueFacts);
        updateFileSymbols(fileName,
                          content,
                          records,
                          effectiveValueFacts,
                          computationRevision,
                          document.documentRevision);
        SlangManager diagnosticsAnalyzer;
        diagnostics.append(
            diagnosticsAnalyzer.extractDiagnostics(fileName, content));
        symbolsFromOpenFiles += records.size();
    }

    publishOpenDocumentResults(svFiles, diagnostics);
    emit analysisCompleted("open_tabs", symbolsFromOpenFiles);
}

void SymbolAnalyzer::analyzeWorkspace(
    WorkspaceManager* workspaceManager,
    std::function<bool()> isCancelled)
{
    if (shutdownStarted || !workspaceManager
        || !workspaceManager->isWorkspaceOpen())
        return;
    analyzeProject(workspaceManager->projectSnapshot(), std::move(isCancelled));
}

void SymbolAnalyzer::analyzeProject(
    const ProjectSnapshot& project,
    std::function<bool()> isCancelled)
{
    if (shutdownStarted || !project.isOpen())
        return;

    cancelWorkspacePublication();
    cancelAllFileAnalysesAndWait();
    cancelWorkspaceAnalysisAndWait();
    overlayWorkspaceFiles = project.systemVerilogFiles;
    overlayWorkspaceIncludeDirs = project.includeDirs;
    overlayWorkspaceDefines = project.defines;
    QStringList svFiles = project.systemVerilogFiles;
    const std::uint64_t epoch = ++workspaceEpoch;
    const std::uint64_t analysisRevision =
        EffectiveValueService::getInstance()->beginComputation(svFiles);

    QElapsedTimer totalTimer;
    totalTimer.start();
    emit analysisStarted(project.workspaceRoot);

    const int totalFiles = svFiles.size();
    if (totalFiles == 0) {
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }

    SlangManager symbolAnalyzer;
    QList<EffectiveValueFact> effectiveValueFacts;
    QHash<QString, QString> analyzedFileContents;
    QElapsedTimer stageTimer;
    stageTimer.start();
    const auto allRecords =
        symbolAnalyzer.extractWorkspaceSymbolRecords(svFiles,
                                                     project.includeDirs,
                                                     project.defines,
                                                     isCancelled,
                                                     &effectiveValueFacts,
                                                     &analyzedFileContents);
    const qint64 symbolExtractionMs = stageTimer.elapsed();
    if (isCancelled && isCancelled()) {
        emit workspaceAnalysisExpired();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }

    stageTimer.restart();
    WorkspaceAnalysisResult result =
        SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
            svFiles,
            allRecords,
            effectiveValueFacts,
            isCancelled,
            analyzedFileContents);
    result.symbolExtractionMs = symbolExtractionMs;
    result.resultAssemblyMs = stageTimer.elapsed();
    result.fileAnalysisBands = workspaceFileAnalysisBands;
    result.generation = ++workspaceAnalysisGeneration;
    result.workspaceEpoch = epoch;
    result.analysisRevision = analysisRevision;
    if (result.cancelled || (isCancelled && isCancelled())) {
        result.cancelled = true;
        emit workspaceAnalysisExpired();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }
    SlangManager diagnosticsAnalyzer;
    stageTimer.restart();
    result.diagnostics =
        diagnosticsAnalyzer.extractOverlayWorkspaceDiagnostics(
            analyzedFileContents,
            project.includeDirs,
            project.defines,
            isCancelled,
            svFiles);
    result.diagnosticsExtractionMs = stageTimer.elapsed();
    result.workerElapsedMs = totalTimer.elapsed();
    if (isCancelled && isCancelled()) {
        emit workspaceAnalysisExpired();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }
    WorkspaceAnalysisTelemetry telemetry;
    stageTimer.restart();
    const int filesAnalyzed =
        publishWorkspaceAnalysisResult(result, totalFiles, &telemetry);
    const qint64 publicationMs = stageTimer.elapsed();
    emitWorkspaceAnalysisTelemetry(project.workspaceRoot,
                                   result,
                                   totalFiles,
                                   filesAnalyzed,
                                   publicationMs,
                                   telemetry.publicationUpdateMs,
                                   telemetry.finalSnapshotMs);
    emit batchAnalysisCompleted(filesAnalyzed, result.totalSymbols);
    emit analysisCompleted(project.workspaceRoot, result.totalSymbols);
}

void SymbolAnalyzer::analyzeFile(const QString& filePath)
{
    if (shutdownStarted || !isSystemVerilogFile(filePath))
        return;

    emit analysisStarted(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
        emit analysisCompleted(filePath, 0);
        return;
    }
    QString content = QTextStream(&file).readAll();
    file.close();

    if (!overlayWorkspaceFiles.isEmpty()) {
        analyzeFileContentAsync(filePath, content);
        return;
    }

    SlangManager symbolAnalyzer;
    QList<EffectiveValueFact> effectiveValueFacts;
    const QList<SemanticSymbolRecord> records =
        symbolAnalyzer.extractSymbolRecords(filePath,
                                            content,
                                            {},
                                            {},
                                            &effectiveValueFacts);
    SlangManager diagnosticsAnalyzer;
    QList<SemanticDiagnostic> diagnostics =
        diagnosticsAnalyzer.extractDiagnostics(filePath, content);
    publishFileAnalysisResult(filePath,
                              content,
                              records,
                              diagnostics,
                              effectiveValueFacts);
    emit analysisCompleted(filePath, records.size());
}

void SymbolAnalyzer::analyzeFileContent(
    const QString& fileName,
    const QString& content,
    std::uint64_t documentRevision)
{
    if (shutdownStarted || fileName.isEmpty()
        || !isSystemVerilogFile(fileName))
        return;
    if (!overlayWorkspaceFiles.isEmpty()) {
        analyzeFileContentAsync(fileName, content, documentRevision);
        return;
    }
    SlangManager symbolAnalyzer;
    QList<EffectiveValueFact> effectiveValueFacts;
    const QList<SemanticSymbolRecord> records =
        symbolAnalyzer.extractSymbolRecords(fileName,
                                            content,
                                            {},
                                            {},
                                            &effectiveValueFacts);
    SlangManager diagnosticsAnalyzer;
    QList<SemanticDiagnostic> diagnostics =
        diagnosticsAnalyzer.extractDiagnostics(fileName, content);
    publishFileAnalysisResult(fileName,
                              content,
                              records,
                              diagnostics,
                              effectiveValueFacts,
                              0,
                              documentRevision);
    emit analysisCompleted(fileName, records.size());
}
