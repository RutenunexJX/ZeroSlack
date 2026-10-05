#include "symbolanalyzer.h"
#include "semanticindexsnapshot.h"
#include <QEventLoop>
#include <QFileInfo>
#include <QScopeGuard>

SemanticAnalysisRequest SymbolAnalyzer::projectRequest(
    const ProjectSnapshot& project, const QList<OpenDocumentContent>& documents)
{
    SemanticAnalysisRequest request;
    request.generation = ++compatibilityAnalysisGeneration;
    request.compatibilityRequest = true;
    request.reason = SemanticAnalysisReason::ExplicitRequest;
    request.impactHint = SemanticChangeImpact::WorkspaceConfig;
    request.project = project;
    request.changedFiles = project.systemVerilogFiles;
    request.runtimePolicy.maxDiagnostics = publishedDiagnosticLimit;
    QSet<QString> roots;
    for (const QString& file : project.systemVerilogFiles)
        roots.insert(SemanticInputCapture::pathKey(file));
    for (const auto& document : documents) {
        if (roots.contains(SemanticInputCapture::pathKey(document.fileName)) && !document.content.isNull()) {
            request.sourceOverrides.insert(document.fileName, document.content);
            request.documentRevisions.insert(document.fileName, document.documentRevision);
        }
    }
    return request;
}

SemanticAnalysisRequest SymbolAnalyzer::documentRequest(
    const QList<OpenDocumentContent>& documents, bool standalone, bool includePublishedDocuments)
{
    SemanticAnalysisRequest request;
    request.generation = ++compatibilityAnalysisGeneration;
    request.compatibilityRequest = true;
    request.reason = SemanticAnalysisReason::ExplicitRequest;
    request.runtimePolicy.maxDiagnostics = publishedDiagnosticLimit;
    request.project = standalone ? ProjectSnapshot{} : overlayProject;
    if (includePublishedDocuments && !standalone && request.project.systemVerilogFiles.isEmpty()) {
        // Compatibility callers can publish an explicit in-memory document set
        // before attaching a WorkspaceManager. Preserve that set as the input
        // context for an overlay; TEMP requests deliberately have no such scope.
        // Configured workspaces always use their complete project above.
        const auto snapshot = SemanticIndex::getInstance()->snapshot();
        if (snapshot) {
            request.sourceOverrides = snapshot->fileContentsView();
            request.project.systemVerilogFiles = request.sourceOverrides.keys();
            request.project.systemVerilogFiles.sort(Qt::CaseSensitive);
            for (const auto& file : request.project.systemVerilogFiles) {
                const auto records = snapshot->getSymbolRecords(file);
                if (!records.isEmpty())
                    request.documentRevisions.insert(file, records.first().presentation.documentRevision);
            }
        }
    }
    QHash<QString, OpenDocumentContent> byKey;
    for (const auto& document : documents)
        if (isSystemVerilogFile(document.fileName))
            byKey.insert(SemanticInputCapture::pathKey(document.fileName), document);
    auto keys = byKey.keys();
    keys.sort(Qt::CaseSensitive);
    QSet<QString> roots;
    for (const QString& file : request.project.systemVerilogFiles)
        roots.insert(SemanticInputCapture::pathKey(file));
    for (const auto& key : keys) {
        const auto& document = byKey[key];
        if (!roots.contains(key)) {
            request.project.systemVerilogFiles.append(document.fileName);
            roots.insert(key);
        }
        request.changedFiles.append(document.fileName);
        request.compatibilityCompletionFiles.append(document.fileName);
        if (!document.content.isNull())
            request.sourceOverrides.insert(key, document.content);
        request.documentRevisions.remove(key);
        request.documentRevisions.insert(document.fileName, document.documentRevision);
    }
    request.triggerFile = request.changedFiles.value(request.changedFiles.size() - 1);
    if (!request.project.isOpen()) {
        request.project.allFiles = request.project.systemVerilogFiles;
        for (const auto& file : request.project.systemVerilogFiles) {
            const auto directory = QFileInfo(file).absolutePath();
            if (!request.project.includeDirs.contains(directory))
                request.project.includeDirs.append(directory);
        }
    }
    return request;
}

void SymbolAnalyzer::runSemanticAnalysisBlocking(
    const SemanticAnalysisRequest& request, std::function<bool()> cancelled)
{
    if (shutdownStarted || !request.isValid() || (cancelled && cancelled()))
        return;
    // The CLI / synchronous compatibility API waits for the SAME worker and
    // transaction as asynchronous callers. Parsing and result disposal still
    // belong to the analyzer's pools. GUI editing uses the asynchronous API.
    QEventLoop loop;
    bool completed = false;
    auto finish = [&](const SemanticAnalysisRequest& delivered) {
        if (delivered.compatibilityRequest && delivered.generation == request.generation) {
            completed = true;
            loop.quit();
        }
    };
    const auto committed = connect(this, &SymbolAnalyzer::semanticAnalysisCommitted, &loop,
        [&](const auto& delivered, const auto&) { finish(delivered); });
    const auto dropped = connect(this, &SymbolAnalyzer::semanticAnalysisDropped, &loop,
        [&](const auto& delivered, auto) { finish(delivered); });
    const auto failed = connect(this, &SymbolAnalyzer::semanticAnalysisFailed, &loop,
        [&](const auto& delivered, const auto&) { finish(delivered); });
    const auto cleanup = qScopeGuard([&] { disconnect(committed); disconnect(dropped); disconnect(failed); });
    startSemanticAnalysisAsync(request, std::move(cancelled));
    if (!completed)
        loop.exec();
}

void SymbolAnalyzer::analyzeOpenDocuments(const QList<OpenDocumentContent>& documents)
{
    runSemanticAnalysisBlocking(documentRequest(documents, false));
}


void SymbolAnalyzer::analyzeProject(const ProjectSnapshot& project, std::function<bool()> cancelled)
{
    if (project.isOpen())
        runSemanticAnalysisBlocking(projectRequest(project, {}), std::move(cancelled));
}

void SymbolAnalyzer::analyzeFile(const QString& path)
{
    // A null override means capture disk on the worker; empty editor buffers
    // use a non-null empty QString and remain valid authoritative inputs.
    runSemanticAnalysisBlocking(documentRequest({{path, QString(), 0}}, false));
}

void SymbolAnalyzer::analyzeFileContent(const QString& file, const QString& content,
                                      std::uint64_t revision)
{
    runSemanticAnalysisBlocking(documentRequest(
        {{file, content.isNull() ? QStringLiteral("") : content, revision}}, false));
}
