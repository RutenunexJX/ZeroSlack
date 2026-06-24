#include "symbolanalyzer.h"

#include "slangmanager.h"
#include "symbolanalyzerworkspace.h"
#include "workspacemanager.h"

#include <QFile>
#include <QTextStream>
#include <utility>

void SymbolAnalyzer::analyzeOpenDocuments(
    const QList<OpenDocumentContent>& documents)
{
    emit analysisStarted("open_tabs");

    QStringList svFiles;
    int symbolsFromOpenFiles = 0;
    QList<SemanticDiagnostic> diagnostics;

    for (const OpenDocumentContent& document : documents) {
        const QString& fileName = document.fileName;
        const QString& content = document.content;
        if (!isSystemVerilogFile(fileName) || content.isNull())
            continue;

        svFiles.append(fileName);
        SlangManager symbolAnalyzer;
        const QList<SemanticSymbolRecord> records =
            symbolAnalyzer.extractSymbolRecords(fileName, content);
        updateFileSymbols(fileName, content, records);
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
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;
    analyzeProject(workspaceManager->projectSnapshot(), std::move(isCancelled));
}

void SymbolAnalyzer::analyzeProject(
    const ProjectSnapshot& project,
    std::function<bool()> isCancelled)
{
    if (!project.isOpen())
        return;

    emit analysisStarted(project.workspaceRoot);

    QStringList svFiles = project.systemVerilogFiles;
    const int totalFiles = svFiles.size();
    if (totalFiles == 0) {
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }

    SlangManager symbolAnalyzer;
    const auto allRecords =
        symbolAnalyzer.extractWorkspaceSymbolRecords(svFiles,
                                                     project.includeDirs,
                                                     project.defines,
                                                     isCancelled);
    if (isCancelled && isCancelled()) {
        emit workspaceAnalysisExpired();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }

    WorkspaceAnalysisResult result =
        SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
            svFiles,
            allRecords,
            isCancelled);
    result.protectedFiles = workspaceProtectedFiles;
    result.priorityPublicationCheckpoints =
        workspacePriorityPublicationCheckpoints;
    result.generation = ++workspaceAnalysisGeneration;
    if (result.cancelled || (isCancelled && isCancelled())) {
        result.cancelled = true;
        emit workspaceAnalysisExpired();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }
    SlangManager diagnosticsAnalyzer;
    result.diagnostics =
        diagnosticsAnalyzer.extractWorkspaceDiagnostics(svFiles,
                                                       project.includeDirs,
                                                       project.defines,
                                                       isCancelled);
    if (isCancelled && isCancelled()) {
        emit workspaceAnalysisExpired();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }
    const int filesAnalyzed =
        publishWorkspaceAnalysisResult(result, totalFiles);
    emit batchAnalysisCompleted(filesAnalyzed, result.totalSymbols);
    emit analysisCompleted(project.workspaceRoot, result.totalSymbols);
}

void SymbolAnalyzer::analyzeFile(const QString& filePath)
{
    if (!isSystemVerilogFile(filePath))
        return;

    emit analysisStarted(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
        emit analysisCompleted(filePath, 0);
        return;
    }
    QString content = QTextStream(&file).readAll();
    file.close();

    SlangManager symbolAnalyzer;
    const QList<SemanticSymbolRecord> records =
        symbolAnalyzer.extractSymbolRecords(filePath, content);
    SlangManager diagnosticsAnalyzer;
    QList<SemanticDiagnostic> diagnostics =
        diagnosticsAnalyzer.extractDiagnostics(filePath, content);
    publishFileAnalysisResult(filePath, content, records, diagnostics);
    emit analysisCompleted(filePath, records.size());
}

void SymbolAnalyzer::analyzeFileContent(
    const QString& fileName,
    const QString& content)
{
    if (fileName.isEmpty() || !isSystemVerilogFile(fileName))
        return;
    SlangManager symbolAnalyzer;
    const QList<SemanticSymbolRecord> records =
        symbolAnalyzer.extractSymbolRecords(fileName, content);
    SlangManager diagnosticsAnalyzer;
    QList<SemanticDiagnostic> diagnostics =
        diagnosticsAnalyzer.extractDiagnostics(fileName, content);
    publishFileAnalysisResult(fileName, content, records, diagnostics);
    emit analysisCompleted(fileName, records.size());
}
