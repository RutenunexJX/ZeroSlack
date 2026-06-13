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
        QList<sym_list::SymbolInfo> list =
            m_slangManager->extractSymbols(fileName, content);
        updateFileSymbols(fileName, content, list);
        diagnostics.append(
            m_slangManager->extractDiagnostics(fileName, content));
        symbolsFromOpenFiles += list.size();
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

    QList<sym_list::SymbolInfo> allSymbols =
        m_slangManager->extractWorkspaceSymbols(svFiles);
    if (isCancelled && isCancelled()) {
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }

    WorkspaceAnalysisResult result =
        SymbolAnalyzerWorkspace::buildWorkspaceAnalysisResult(
            svFiles,
            allSymbols,
            isCancelled);
    result.diagnostics = m_slangManager->extractWorkspaceDiagnostics(svFiles);
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

    QList<sym_list::SymbolInfo> list =
        m_slangManager->extractSymbols(filePath, content);
    QList<SemanticDiagnostic> diagnostics =
        m_slangManager->extractDiagnostics(filePath, content);
    publishFileAnalysisResult(filePath, content, list, diagnostics);
    emit analysisCompleted(filePath, list.size());
}

void SymbolAnalyzer::analyzeFileContent(
    const QString& fileName,
    const QString& content)
{
    if (fileName.isEmpty() || !isSystemVerilogFile(fileName))
        return;
    QList<sym_list::SymbolInfo> list =
        m_slangManager->extractSymbols(fileName, content);
    QList<SemanticDiagnostic> diagnostics =
        m_slangManager->extractDiagnostics(fileName, content);
    publishFileAnalysisResult(fileName, content, list, diagnostics);
    emit analysisCompleted(fileName, list.size());
}
