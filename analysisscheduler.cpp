#include "analysisscheduler.h"

#include "semanticindex.h"
#include "symbolanalyzer.h"
#include "syminfo.h"

#include <QtConcurrent/QtConcurrent>
#include <QFile>
#include <QFuture>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>
#include <utility>

AnalysisScheduler::AnalysisScheduler(QObject* parent)
    : QObject(parent)
{
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
                emit workspaceRelationshipAnalysisFinished(workspaceRelationshipWatcher->result());
            });
}

AnalysisScheduler::~AnalysisScheduler()
{
    cancelWorkspaceRelationshipAnalysis();
    for (QTimer* timer : openFileAnalysisTimers)
        timer->deleteLater();
    for (QTimer* timer : fileChangeDebounceTimers)
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
                cancelScheduledOpenFileAnalysis(fileName);
                lastRelationshipAnalysisContent.remove(fileName);
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
    connect(projectModel, &ProjectModel::projectClosed, this, [this]() {
        workspaceSymbolAnalysisActive = false;
        activeWorkspaceProject = ProjectSnapshot();
        cancelWorkspaceRelationshipAnalysis();
        SemanticIndex::getInstance()->clearSnapshot();
    });
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

    connect(symbolAnalyzer, &SymbolAnalyzer::batchAnalysisCompleted,
            this, &AnalysisScheduler::onWorkspaceSymbolAnalysisCompleted);
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

void AnalysisScheduler::setRelationshipAnalysisCallback(
    std::function<void(const QString&, const QString&)> callback)
{
    relationshipAnalysisCallback = std::move(callback);
}

void AnalysisScheduler::setWorkspaceRelationshipAnalysisCallback(
    std::function<WorkspaceRelationshipAnalysisResult(
        const ProjectSnapshot&,
        std::shared_ptr<const SemanticIndexSnapshot>)> callback)
{
    workspaceRelationshipAnalysisCallback = std::move(callback);
}

void AnalysisScheduler::setWorkspaceRelationshipCancelCallback(std::function<void()> callback)
{
    workspaceRelationshipCancelCallback = std::move(callback);
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

void AnalysisScheduler::requestRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty() || content.isEmpty() || !relationshipAnalysisCallback)
        return;

    if (symbolAnalyzer) {
        const QString lastContent = lastRelationshipAnalysisContent.value(fileName);
        if (!lastContent.isNull() && !symbolAnalyzer->hasSignificantChanges(lastContent, content))
            return;
    }

    lastRelationshipAnalysisContent.insert(fileName, content);
    relationshipAnalysisCallback(fileName, content);
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
    emit workspaceSymbolAnalysisStarted(project, project.systemVerilogFiles.size());
    symbolAnalyzer->startAnalyzeProjectAsync(project, workspaceSymbolCancelProvider);
}

void AnalysisScheduler::requestWorkspaceRelationshipAnalysis(const ProjectSnapshot& project)
{
    if (!project.isOpen()
        || project.systemVerilogFiles.isEmpty()
        || !workspaceRelationshipAnalysisCallback
        || !workspaceRelationshipWatcher) {
        return;
    }

    cancelWorkspaceRelationshipAnalysis();

    emit workspaceRelationshipAnalysisStarted(project, project.systemVerilogFiles.size());

    const auto currentSnapshot = SemanticIndex::getInstance()->snapshot();
    const QList<SemanticDiagnostic> currentDiagnostics =
        currentSnapshot ? currentSnapshot->diagnostics() : QList<SemanticDiagnostic>();
    const auto baseSnapshot = std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(sym_list::getInstance(), currentDiagnostics));
    SemanticIndex::getInstance()->setSnapshot(baseSnapshot);

    QFuture<WorkspaceRelationshipAnalysisResult> future =
        QtConcurrent::run([callback = workspaceRelationshipAnalysisCallback, project, baseSnapshot]() {
            return callback(project, baseSnapshot);
        });
    workspaceRelationshipWatcher->setFuture(future);
}

void AnalysisScheduler::cancelWorkspaceRelationshipAnalysis()
{
    if (!workspaceRelationshipWatcher || !workspaceRelationshipWatcher->isRunning())
        return;

    if (workspaceRelationshipCancelCallback)
        workspaceRelationshipCancelCallback();

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

void AnalysisScheduler::onDocumentOpened(const DocumentSnapshot& snapshot)
{
    analyzeOpenDocumentNow(snapshot, false);
}

void AnalysisScheduler::onDocumentEdited(const DocumentSnapshot& snapshot)
{
    if (snapshot.fileName.isEmpty() || isWorkspaceOpen())
        return;

    const QString content = contentForOpenFile(snapshot.fileName);
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
        workspaceSymbolAnalysisActive = false;
        activeWorkspaceProject = ProjectSnapshot();
        cancelWorkspaceRelationshipAnalysis();
        SemanticIndex::getInstance()->clearSnapshot();
        return;
    }

    requestWorkspaceAnalysis(project);
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

    if (skipUnchanged && !sym_list::getInstance()->contentAffectsSymbols(snapshot.fileName, content)) {
        emit documentRefreshRequested(snapshot.fileName);
        return;
    }

    symbolAnalyzer->analyzeFileContent(snapshot.fileName, content);
    emit documentRefreshRequested(snapshot.fileName);
    requestRelationshipAnalysis(snapshot.fileName, content);
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
