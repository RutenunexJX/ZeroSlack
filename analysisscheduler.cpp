#include "analysisscheduler.h"

#include "semanticindex.h"
#include "symbolrelationshipengine.h"
#include "symbolanalyzer.h"

#include <QtConcurrent/QtConcurrent>
#include <QFile>
#include <QFuture>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <utility>

namespace {
QList<SemanticRelationship> toSemanticRelationships(
    const QVector<RelationshipToAdd>& relationships)
{
    QList<SemanticRelationship> result;
    result.reserve(relationships.size());
    for (const RelationshipToAdd& relationship : relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        result.append({relationship.fromId, relationship.toId, relationship.type});
    }
    return result;
}
}

AnalysisScheduler::AnalysisScheduler(QObject* parent)
    : QObject(parent)
{
    diagnosticsRefreshTimer = new QTimer(this);
    diagnosticsRefreshTimer->setSingleShot(true);
    diagnosticsRefreshTimer->setInterval(100);
    connect(diagnosticsRefreshTimer, &QTimer::timeout, this, [this]() {
        const QString fileName = pendingDiagnosticsRefreshFileName;
        pendingDiagnosticsRefreshFileName.clear();
        emit diagnosticsRefreshRequested(fileName);
    });

    relationshipRefreshTimer = new QTimer(this);
    relationshipRefreshTimer->setSingleShot(true);
    relationshipRefreshTimer->setInterval(400);
    connect(relationshipRefreshTimer, &QTimer::timeout, this, [this]() {
        emit relationshipDataRefreshRequested();
    });

    singleFileRelationshipWatcher =
        new QFutureWatcher<SingleFileRelationshipAnalysisResult>(this);
    connect(singleFileRelationshipWatcher,
            &QFutureWatcher<SingleFileRelationshipAnalysisResult>::finished,
            this,
            [this]() {
                if (!singleFileRelationshipWatcher)
                    return;
                if (singleFileRelationshipWatcher->isCanceled())
                    return;
                const SingleFileRelationshipAnalysisResult result =
                    singleFileRelationshipWatcher->result();
                if (!applySingleFileRelationshipResult(result))
                    return;
                emit relationshipAnalysisProgress(
                    result.fileName, result.relationships.size());
                emit relationshipAnalysisFinished(result);
            });

    workspaceRelationshipWatcher = new QFutureWatcher<WorkspaceRelationshipAnalysisResult>(this);
    connect(workspaceRelationshipWatcher,
            &QFutureWatcher<WorkspaceRelationshipAnalysisResult>::finished,
            this,
            [this]() {
                if (!workspaceRelationshipWatcher)
                    return;
                if (workspaceRelationshipWatcher->isCanceled()) {
                    emit workspaceRelationshipAnalysisCancelled();
                    return;
                }
                const WorkspaceRelationshipAnalysisResult result =
                    workspaceRelationshipWatcher->result();
                if (!applyWorkspaceRelationshipResult(result))
                    return;
                const int totalFiles = result.totalFiles > 0
                    ? result.totalFiles
                    : result.fileRelationships.size();
                int processedFiles = 0;
                for (const auto& pair : result.fileRelationships) {
                    ++processedFiles;
                    emit relationshipAnalysisProgress(pair.first, pair.second.size());
                    emit workspaceRelationshipAnalysisProgress(pair.first,
                                                               pair.second.size(),
                                                               processedFiles,
                                                               totalFiles);
                }
                emit workspaceRelationshipAnalysisFinished(result);
            });
}

AnalysisScheduler::~AnalysisScheduler()
{
    cancelRelationshipAnalysis();
    cancelWorkspaceRelationshipAnalysis();
    for (QTimer* timer : openFileAnalysisTimers)
        timer->deleteLater();
    for (QTimer* timer : fileChangeDebounceTimers)
        timer->deleteLater();
    for (QTimer* timer : relationshipAnalysisTimers)
        timer->deleteLater();
}

void AnalysisScheduler::setDocumentModel(DocumentModel* model)
{
    if (documentModel == model)
        return;
    if (documentModel)
        disconnect(documentModel, nullptr, this, nullptr);

    documentModel = model;
    if (!documentModel)
        return;

    connect(documentModel, &DocumentModel::documentOpened,
            this, &AnalysisScheduler::onDocumentOpened);
    connect(documentModel, &DocumentModel::documentEdited,
            this, &AnalysisScheduler::onDocumentEdited);
    connect(documentModel, &DocumentModel::documentSaved,
            this, &AnalysisScheduler::onDocumentSaved);
    connect(documentModel, &DocumentModel::documentClosed,
            this, [this](const QString&, const QString& fileName) {
                handleDocumentClosed(fileName);
            });
}

void AnalysisScheduler::setProjectModel(ProjectModel* model)
{
    if (projectModel == model)
        return;
    if (projectModel)
        disconnect(projectModel, nullptr, this, nullptr);

    projectModel = model;
    if (!projectModel)
        return;

    connect(projectModel, &ProjectModel::projectChanged,
            this, &AnalysisScheduler::onProjectChanged);
    connect(projectModel, &ProjectModel::projectClosed,
            this, &AnalysisScheduler::clearProjectSemanticState);
    projectSemanticStateCleared = !projectModel->isOpen();
}

void AnalysisScheduler::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);

    symbolAnalyzer = analyzer;
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer, &SymbolAnalyzer::analysisCompleted,
            this, [this](const QString& fileName, int) {
                scheduleDiagnosticsRefresh(fileName);
            });
    connect(symbolAnalyzer,
            &SymbolAnalyzer::batchAnalysisCompleted,
            this,
            [this](int filesAnalyzed, int totalSymbols) {
                scheduleDiagnosticsRefresh(QString());
                onWorkspaceSymbolAnalysisCompleted(filesAnalyzed, totalSymbols);
            });
}

void AnalysisScheduler::setOpenFileContentProvider(std::function<QString(const QString&)> provider)
{
    openFileContentProvider = std::move(provider);
}

void AnalysisScheduler::setWorkspaceOpenProvider(std::function<bool()> provider)
{
    workspaceOpenProvider = std::move(provider);
}

void AnalysisScheduler::setWorkspaceSymbolCancelProvider(std::function<bool()> provider)
{
    workspaceSymbolCancelProvider = std::move(provider);
}

void AnalysisScheduler::setRelationshipEngine(SymbolRelationshipEngine* engine)
{
    if (relationshipEngine == engine)
        return;
    if (relationshipEngine)
        disconnect(relationshipEngine, nullptr, this, nullptr);

    relationshipEngine = engine;
    if (!relationshipEngine) {
        if (relationshipRefreshTimer)
            relationshipRefreshTimer->stop();
        return;
    }

    connect(relationshipEngine,
            &SymbolRelationshipEngine::relationshipAdded,
            this,
            [this](int, int, SymbolRelationshipEngine::RelationType) {
                emit relationshipDataInvalidated();
                scheduleRelationshipDataRefresh();
            });
    connect(relationshipEngine,
            &SymbolRelationshipEngine::relationshipsCleared,
            this,
            [this]() {
                if (relationshipRefreshTimer)
                    relationshipRefreshTimer->stop();
                emit relationshipDataInvalidated();
                emit relationshipDataRefreshRequested();
            });
}

void AnalysisScheduler::setRelationshipBuilder(SmartRelationshipBuilder* builder)
{
    if (relationshipBuilder == builder)
        return;
    if (relationshipBuilder)
        disconnect(relationshipBuilder, nullptr, this, nullptr);

    relationshipBuilder = builder;
    if (!relationshipBuilder)
        return;

    connect(relationshipBuilder,
            &SmartRelationshipBuilder::analysisError,
            this,
            [this](const QString& fileName, const QString& error) {
                emit relationshipAnalysisError(fileName, error);
            });
    connect(relationshipBuilder,
            &SmartRelationshipBuilder::analysisCancelled,
            this,
            [this]() {
                emit relationshipAnalysisCancelled();
            });
}

void AnalysisScheduler::scheduleOpenFileAnalysis(const QString& fileName, int delayMs)
{
    if (fileName.isEmpty() || !symbolAnalyzer)
        return;

    cancelScheduledOpenFileAnalysis(fileName);

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    connect(timer, &QTimer::timeout, this, [this, fileName, timer]() {
        const QString content = contentForOpenFile(fileName);
        if (!content.isNull())
            symbolAnalyzer->analyzeFileContentAsync(fileName, content);
        if (openFileAnalysisTimers.value(fileName) == timer)
            openFileAnalysisTimers.remove(fileName);
        timer->deleteLater();
    });
    openFileAnalysisTimers[fileName] = timer;
    timer->start();
}

void AnalysisScheduler::cancelScheduledOpenFileAnalysis(const QString& fileName)
{
    auto it = openFileAnalysisTimers.find(fileName);
    if (it == openFileAnalysisTimers.end())
        return;

    if (it.value()) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    openFileAnalysisTimers.erase(it);
}

void AnalysisScheduler::scheduleRelationshipAnalysis(const QString& fileName,
                                                     const QString& content,
                                                     int delayMs)
{
    if (fileName.isEmpty() || content.isEmpty() || !relationshipBuilder)
        return;

    const QString pendingContent =
        pendingRelationshipAnalysisContent.value(fileName);
    const QString lastContent = pendingContent.isNull()
        ? lastRelationshipAnalysisContent.value(fileName)
        : pendingContent;
    if (!lastContent.isNull()
        && !contentDiffersBeyondWhitespace(lastContent, content)) {
        return;
    }

    pendingRelationshipAnalysisContent.insert(fileName, content);

    if (relationshipAnalysisTimers.contains(fileName)) {
        QTimer* oldTimer = relationshipAnalysisTimers.take(fileName);
        oldTimer->stop();
        oldTimer->deleteLater();
    }

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    connect(timer, &QTimer::timeout, this, [this, fileName, timer]() {
        if (relationshipAnalysisTimers.value(fileName) == timer)
            relationshipAnalysisTimers.remove(fileName);
        timer->deleteLater();

        const QString content = contentForOpenFile(fileName);
        pendingRelationshipAnalysisContent.remove(fileName);
        if (!content.isNull())
            requestRelationshipAnalysis(fileName, content);
    });
    relationshipAnalysisTimers[fileName] = timer;
    timer->start();
}

void AnalysisScheduler::requestRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty()
        || content.isEmpty()
        || !relationshipBuilder
        || !singleFileRelationshipWatcher) {
        return;
    }

    if (symbolAnalyzer) {
        const QString lastContent = lastRelationshipAnalysisContent.value(fileName);
        if (!lastContent.isNull() && !symbolAnalyzer->hasSignificantChanges(lastContent, content))
            return;
    }

    lastRelationshipAnalysisContent.insert(fileName, content);
    cancelRelationshipAnalysis();

    relationshipBuilder->resetCancellation();
    const auto baseSnapshot =
        SemanticIndex::getInstance()->beginRelationshipAnalysisSnapshot();

    QFuture<SingleFileRelationshipAnalysisResult> future =
        QtConcurrent::run([this, fileName, content, baseSnapshot]() {
            return analyzeSingleFileRelationships(fileName, content, baseSnapshot);
        });
    singleFileRelationshipWatcher->setFuture(future);
}

void AnalysisScheduler::cancelRelationshipAnalysis()
{
    if (!singleFileRelationshipWatcher || !singleFileRelationshipWatcher->isRunning())
        return;

    if (relationshipBuilder)
        relationshipBuilder->cancelAnalysis();

    QFuture<SingleFileRelationshipAnalysisResult> future =
        singleFileRelationshipWatcher->future();
    singleFileRelationshipWatcher->cancel();
    future.waitForFinished();
}

void AnalysisScheduler::requestWorkspaceAnalysis(const ProjectSnapshot& project)
{
    if (!project.isOpen() || !symbolAnalyzer)
        return;

    SemanticIndex::getInstance()->clearSnapshot();
    if (project.systemVerilogFiles.isEmpty())
        return;

    activeWorkspaceProject = project;
    workspaceSymbolAnalysisActive = true;
    scheduleDiagnosticsRefresh(QString());
    emit workspaceSymbolAnalysisStarted(project, project.systemVerilogFiles.size());
    symbolAnalyzer->startAnalyzeProjectAsync(project, workspaceSymbolCancelProvider);
}

void AnalysisScheduler::requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project)
{
    if (!project.isOpen()
        || project.systemVerilogFiles.isEmpty()
        || !relationshipBuilder
        || !workspaceRelationshipWatcher) {
        return;
    }

    cancelWorkspaceRelationshipAnalysis();

    emit workspaceRelationshipAnalysisStarted(project, project.systemVerilogFiles.size());

    const auto baseSnapshot =
        SemanticIndex::getInstance()->beginRelationshipAnalysisSnapshot();

    QFuture<WorkspaceRelationshipAnalysisResult> future =
        QtConcurrent::run([this, project, baseSnapshot]() {
            return analyzeWorkspaceRelationships(project, baseSnapshot);
        });
    workspaceRelationshipWatcher->setFuture(future);
}

void AnalysisScheduler::cancelWorkspaceRelationshipAnalysis()
{
    if (!workspaceRelationshipWatcher || !workspaceRelationshipWatcher->isRunning())
        return;

    if (relationshipBuilder)
        relationshipBuilder->cancelAnalysis();

    QFuture<WorkspaceRelationshipAnalysisResult> future = workspaceRelationshipWatcher->future();
    workspaceRelationshipWatcher->cancel();
    future.waitForFinished();
}

void AnalysisScheduler::handleExternalFileChanged(const QString& fileName, int debounceMs)
{
    if (fileName.isEmpty() || !symbolAnalyzer)
        return;

    if (fileChangeDebounceTimers.contains(fileName)) {
        QTimer* oldTimer = fileChangeDebounceTimers.take(fileName);
        oldTimer->stop();
        oldTimer->deleteLater();
    }

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer, fileName]() {
        fileChangeDebounceTimers.remove(fileName);
        timer->deleteLater();

        symbolAnalyzer->analyzeFile(fileName);

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QFile::Text))
            return;
        const QString content = QTextStream(&file).readAll();
        requestRelationshipAnalysis(fileName, content);
    });
    fileChangeDebounceTimers[fileName] = timer;
    timer->start(debounceMs);
}

void AnalysisScheduler::handleDocumentClosed(const QString& fileName)
{
    cancelScheduledOpenFileAnalysis(fileName);
    if (relationshipAnalysisTimers.contains(fileName)) {
        QTimer* oldTimer = relationshipAnalysisTimers.take(fileName);
        oldTimer->stop();
        oldTimer->deleteLater();
    }
    pendingRelationshipAnalysisContent.remove(fileName);
    lastRelationshipAnalysisContent.remove(fileName);
    analyzeOpenDocumentsNow();

    if (relationshipEngine && !fileName.isEmpty())
        relationshipEngine->invalidateFileRelationships(fileName);
}

void AnalysisScheduler::onDocumentOpened(const DocumentSnapshot& snapshot)
{
    analyzeOpenDocumentNow(snapshot, false);
}

void AnalysisScheduler::onDocumentEdited(const DocumentSnapshot& snapshot)
{
    if (snapshot.fileName.isEmpty())
        return;

    const QString content = contentForOpenFile(snapshot.fileName);
    if (content.isNull())
        return;

    scheduleRelationshipAnalysis(snapshot.fileName,
                                 content,
                                 kOpenDocumentRelationshipAnalysisDebounceMs);

    if (isWorkspaceOpen())
        return;

    if (lineContainsStructuralKeyword(content, snapshot.cursorLine))
        scheduleOpenFileAnalysis(snapshot.fileName, 1000);
}

void AnalysisScheduler::onDocumentSaved(const DocumentSnapshot& snapshot)
{
    analyzeOpenDocumentNow(snapshot, true);
}

void AnalysisScheduler::onProjectChanged(const ProjectSnapshot& project)
{
    if (!project.isOpen()) {
        clearProjectSemanticState();
        return;
    }

    projectSemanticStateCleared = false;
    requestWorkspaceAnalysis(project);
}

void AnalysisScheduler::clearProjectSemanticState()
{
    if (projectSemanticStateCleared)
        return;

    projectSemanticStateCleared = true;
    workspaceSymbolAnalysisActive = false;
    activeWorkspaceProject = ProjectSnapshot();
    cancelWorkspaceRelationshipAnalysis();
    SemanticIndex::getInstance()->clearSnapshot();

    if (relationshipEngine) {
        relationshipEngine->clearAllRelationships();
    } else {
        if (relationshipRefreshTimer)
            relationshipRefreshTimer->stop();
        emit relationshipDataInvalidated();
        emit relationshipDataRefreshRequested();
    }

    scheduleDiagnosticsRefresh(QString());
}

void AnalysisScheduler::onWorkspaceSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols)
{
    if (!workspaceSymbolAnalysisActive)
        return;

    workspaceSymbolAnalysisActive = false;
    const ProjectSnapshot project = activeWorkspaceProject;
    if (workspaceSymbolCancelProvider && workspaceSymbolCancelProvider())
        return;

    emit workspaceSymbolAnalysisFinished(project, filesAnalyzed, totalSymbols);
    requestWorkspaceRelationshipAnalysis(project);
}

void AnalysisScheduler::analyzeOpenDocumentNow(const DocumentSnapshot& snapshot, bool skipUnchanged)
{
    if (snapshot.fileName.isEmpty() || !symbolAnalyzer)
        return;

    const QString content = contentForOpenFile(snapshot.fileName);
    if (content.isEmpty())
        return;

    if (skipUnchanged
        && !SemanticIndex::getInstance()->contentAffectsSymbols(snapshot.fileName, content)) {
        emit documentRefreshRequested(snapshot.fileName);
        return;
    }

    symbolAnalyzer->analyzeFileContent(snapshot.fileName, content);
    emit documentRefreshRequested(snapshot.fileName);
    requestRelationshipAnalysis(snapshot.fileName, content);
}

void AnalysisScheduler::analyzeOpenDocumentsNow()
{
    if (!documentModel || !symbolAnalyzer)
        return;

    QList<OpenDocumentContent> documents;
    for (const DocumentSnapshot& snapshot : documentModel->openDocuments()) {
        if (snapshot.fileName.isEmpty())
            continue;

        const QString content = contentForOpenFile(snapshot.fileName);
        if (content.isNull())
            continue;
        documents.append({snapshot.fileName, content});
    }

    symbolAnalyzer->analyzeOpenDocuments(documents);
}

void AnalysisScheduler::scheduleDiagnosticsRefresh(const QString& fileName)
{
    pendingDiagnosticsRefreshFileName = fileName;
    if (diagnosticsRefreshTimer)
        diagnosticsRefreshTimer->start();
}

void AnalysisScheduler::scheduleRelationshipDataRefresh()
{
    if (relationshipRefreshTimer)
        relationshipRefreshTimer->start();
}

bool AnalysisScheduler::applySingleFileRelationshipResult(
    const SingleFileRelationshipAnalysisResult& result)
{
    if (!relationshipEngine || !relationshipBuilder)
        return false;

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    if (!semanticIndex->publishSnapshotIfCurrent(result.baseSnapshot,
                                                 result.semanticSnapshot))
        return false;

    relationshipEngine->beginUpdate();
    for (const RelationshipToAdd& relationship : result.relationships) {
        if (relationship.fromId < 0 || relationship.toId < 0)
            continue;
        relationshipEngine->addRelationship(relationship.fromId,
                                            relationship.toId,
                                            relationship.type,
                                            relationship.context,
                                            relationship.confidence);
    }
    relationshipEngine->endUpdate();

    scheduleRelationshipDataRefresh();
    return true;
}

bool AnalysisScheduler::applyWorkspaceRelationshipResult(
    const WorkspaceRelationshipAnalysisResult& result)
{
    if (!relationshipEngine || !relationshipBuilder)
        return false;

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    if (!semanticIndex->publishSnapshotIfCurrent(result.baseSnapshot,
                                                 result.semanticSnapshot))
        return false;

    relationshipEngine->beginUpdate();
    for (const auto& pair : result.fileRelationships) {
        for (const RelationshipToAdd& relationship : pair.second) {
            if (relationship.fromId < 0 || relationship.toId < 0)
                continue;
            relationshipEngine->addRelationship(relationship.fromId,
                                                relationship.toId,
                                                relationship.type,
                                                relationship.context,
                                                relationship.confidence);
        }
    }
    relationshipEngine->endUpdate();

    scheduleRelationshipDataRefresh();
    return true;
}

SingleFileRelationshipAnalysisResult AnalysisScheduler::analyzeSingleFileRelationships(
    const QString& fileName,
    const QString& content,
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot) const
{
    SingleFileRelationshipAnalysisResult result;
    result.fileName = fileName;
    result.baseSnapshot = baseSnapshot;
    result.semanticSnapshot = baseSnapshot;
    if (!relationshipBuilder || !baseSnapshot)
        return result;

    const QList<sym_list::SymbolInfo> fileSymbols = baseSnapshot->getSymbols(fileName);
    result.relationships =
        relationshipBuilder->computeRelationships(
            fileName, content, fileSymbols, baseSnapshot.get());

    result.semanticSnapshot =
        SemanticIndex::getInstance()->snapshotWithAdditionalRelationships(
            baseSnapshot,
            toSemanticRelationships(result.relationships));
    return result;
}

WorkspaceRelationshipAnalysisResult AnalysisScheduler::analyzeWorkspaceRelationships(
    const ProjectSnapshot& project,
    std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot) const
{
    WorkspaceRelationshipAnalysisResult result;
    result.baseSnapshot = baseSnapshot;
    result.semanticSnapshot = baseSnapshot;
    result.totalFiles = project.systemVerilogFiles.size();
    if (!relationshipBuilder)
        return result;

    relationshipBuilder->resetCancellation();
    const QStringList svFiles = project.systemVerilogFiles;
    result.fileRelationships.reserve(svFiles.size());
    QList<SemanticRelationship> newRelationships;
    for (const QString& filePath : svFiles) {
        if (relationshipBuilder->isCancelled())
            break;
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString content = QTextStream(&file).readAll();
        const QList<sym_list::SymbolInfo> fileSymbols =
            baseSnapshot ? baseSnapshot->getSymbols(filePath) : QList<sym_list::SymbolInfo>();
        const QVector<RelationshipToAdd> relationships =
            relationshipBuilder->computeRelationships(
                filePath, content, fileSymbols, baseSnapshot.get());
        result.fileRelationships.append({filePath, relationships});
        newRelationships.append(toSemanticRelationships(relationships));
    }
    result.semanticSnapshot =
        SemanticIndex::getInstance()->snapshotWithAdditionalRelationships(
            baseSnapshot,
            newRelationships);
    return result;
}

QString AnalysisScheduler::contentForOpenFile(const QString& fileName) const
{
    if (openFileContentProvider)
        return openFileContentProvider(fileName);
    return QString();
}

bool AnalysisScheduler::isWorkspaceOpen() const
{
    return workspaceOpenProvider ? workspaceOpenProvider() : false;
}

bool AnalysisScheduler::lineContainsStructuralKeyword(const QString& content, int oneBasedLine) const
{
    if (content.isEmpty() || oneBasedLine <= 0)
        return false;

    const QStringList lines = content.split(QLatin1Char('\n'));
    if (oneBasedLine > lines.size())
        return false;

    static const QStringList keywords = {
        QLatin1String("module"),
        QLatin1String("endmodule"),
        QLatin1String("reg"),
        QLatin1String("wire"),
        QLatin1String("logic"),
        QLatin1String("task"),
        QLatin1String("endtask"),
        QLatin1String("function"),
        QLatin1String("endfunction"),
    };

    const QString line = lines[oneBasedLine - 1];
    for (const QString& keyword : keywords) {
        const QRegularExpression word(QStringLiteral("\\b%1\\b")
                                          .arg(QRegularExpression::escape(keyword)));
        if (line.contains(word))
            return true;
    }
    return false;
}

bool AnalysisScheduler::contentDiffersBeyondWhitespace(const QString& oldContent,
                                                       const QString& newContent)
{
    auto withoutWhitespace = [](const QString& content) {
        QString compact;
        compact.reserve(content.size());
        for (QChar ch : content) {
            if (!ch.isSpace())
                compact.append(ch);
        }
        return compact;
    };

    return withoutWhitespace(oldContent) != withoutWhitespace(newContent);
}
