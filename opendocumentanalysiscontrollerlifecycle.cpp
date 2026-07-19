#include "opendocumentanalysiscontroller.h"

#include "documentmodel.h"
#include "symbolanalyzer.h"

#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <utility>

void OpenDocumentAnalysisController::scheduleOpenFileAnalysis(
    const QString& fileName,
    int delayMs,
    std::uint64_t documentRevision)
{
    if (shuttingDown || fileName.isEmpty() || !symbolAnalyzer)
        return;

    cancelScheduledOpenFileAnalysis(fileName);

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    connect(timer, &QTimer::timeout, this, [this, fileName, timer, documentRevision]() {
        QPointer<OpenDocumentAnalysisController> self(this);
        QPointer<QTimer> timerGuard(timer);
        const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;
        const auto finishTimer = [&]() {
            if (!self)
                return;
            if (openFileAnalysisTimers.value(fileName) == timerGuard)
                openFileAnalysisTimers.remove(fileName);
            if (timerGuard)
                timerGuard->deleteLater();
        };
        if (shuttingDown || !symbolAnalyzer) {
            finishTimer();
            return;
        }
        const bool workspaceOpen = isWorkspaceOpen();
        if (!self)
            return;
        const bool workspaceActive =
            workspaceOpen && isWorkspaceAnalysisActive();
        if (!self)
            return;
        if (workspaceActive) {
            finishTimer();
            return;
        }
        if (workspaceOpen && documentModel) {
            analyzeOpenDocumentsNow();
        } else {
            const QString content = contentForOpenFile(fileName);
            if (!self)
                return;
            if (!content.isNull() && analyzer)
                analyzer->analyzeFileContentAsync(fileName,
                                                  content,
                                                  documentRevision);
        }
        finishTimer();
    });
    openFileAnalysisTimers[fileName] = timer;
    timer->start();
}

void OpenDocumentAnalysisController::cancelScheduledOpenFileAnalysis(
    const QString& fileName)
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

void OpenDocumentAnalysisController::handleExternalFileChanged(
    const QString& fileName,
    int debounceMs)
{
    if (shuttingDown || fileName.isEmpty() || !symbolAnalyzer)
        return;

    if (fileChangeDebounceTimers.contains(fileName)) {
        QTimer* oldTimer = fileChangeDebounceTimers.take(fileName);
        oldTimer->stop();
        oldTimer->deleteLater();
    }

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer, fileName, debounceMs]() {
        QPointer<OpenDocumentAnalysisController> self(this);
        QPointer<QTimer> timerGuard(timer);
        const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;
        fileChangeDebounceTimers.remove(fileName);
        timerGuard->deleteLater();

        if (shuttingDown || !symbolAnalyzer)
            return;

        // A disk notification is not represented by DocumentModel. Defer it
        // until the current atomic workspace snapshot publishes instead of
        // racing and invalidating that snapshot.
        const bool workspaceOpen = isWorkspaceOpen();
        if (!self)
            return;
        const bool workspaceActive =
            workspaceOpen && isWorkspaceAnalysisActive();
        if (!self)
            return;
        if (workspaceActive) {
            handleExternalFileChanged(fileName, debounceMs);
            return;
        }

        const bool dirty = isDirtyOpenDocument(fileName);
        if (!self || dirty)
            return;

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QFile::Text))
            return;
        const QString content = QTextStream(&file).readAll();
        if (!analyzer)
            return;
        if (workspaceOpen)
            analyzer->analyzeFileContentAsync(fileName, content);
        else
            analyzer->analyzeFileContent(fileName, content);
        if (!self)
            return;
        emit relationshipAnalysisRequested(fileName, content);
    });
    fileChangeDebounceTimers[fileName] = timer;
    timer->start(debounceMs);
}

void OpenDocumentAnalysisController::handleDocumentClosed(const QString& fileName)
{
    QPointer<OpenDocumentAnalysisController> self(this);
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;
    const QPointer<DocumentModel> model = documentModel;
    const bool workspaceOpen = isWorkspaceOpen();
    if (!self)
        return;
    if (workspaceOpen) {
        for (QTimer* pending : std::as_const(openFileAnalysisTimers)) {
            if (!pending)
                continue;
            pending->stop();
            pending->deleteLater();
        }
        openFileAnalysisTimers.clear();
    } else {
        cancelScheduledOpenFileAnalysis(fileName);
    }
    if (analyzer)
        analyzer->cancelFileAnalysis(fileName);
    if (!self)
        return;
    if (fileChangeDebounceTimers.contains(fileName)) {
        QTimer* oldTimer = fileChangeDebounceTimers.take(fileName);
        oldTimer->stop();
        oldTimer->deleteLater();
    }

    const bool workspaceActive =
        workspaceOpen && isWorkspaceAnalysisActive();
    if (!self)
        return;
    if (!shuttingDown && analyzer && model
        && workspaceOpen && !workspaceActive) {
        QList<OpenDocumentContent> documents;
        for (const DocumentSnapshot& snapshot :
             model->openDocuments()) {
            if (snapshot.fileName.isEmpty()
                || snapshot.fileName == fileName) {
                continue;
            }
            const QString openContent =
                contentForOpenFile(snapshot.fileName);
            if (!self)
                return;
            if (!openContent.isNull()) {
                documents.append(
                    {snapshot.fileName,
                     openContent,
                     static_cast<std::uint64_t>(snapshot.textVersion)});
            }
        }
        QFile diskFile(fileName);
        if (diskFile.open(QIODevice::ReadOnly | QFile::Text)) {
            documents.append(
                {fileName, QTextStream(&diskFile).readAll(), 0});
        }
        if (analyzer)
            analyzer->analyzeOpenDocuments(documents);
    }
}
