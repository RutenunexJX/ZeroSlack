#include "opendocumentanalysiscontroller.h"

#include "symbolanalyzer.h"

#include <QFile>
#include <QTextStream>
#include <QTimer>

void OpenDocumentAnalysisController::scheduleOpenFileAnalysis(
    const QString& fileName,
    int delayMs)
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

        if (isDirtyOpenDocument(fileName))
            return;

        symbolAnalyzer->analyzeFile(fileName);

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QFile::Text))
            return;
        const QString content = QTextStream(&file).readAll();
        emit relationshipAnalysisRequested(fileName, content);
    });
    fileChangeDebounceTimers[fileName] = timer;
    timer->start(debounceMs);
}

void OpenDocumentAnalysisController::handleDocumentClosed(const QString& fileName)
{
    cancelScheduledOpenFileAnalysis(fileName);
    if (fileChangeDebounceTimers.contains(fileName)) {
        QTimer* oldTimer = fileChangeDebounceTimers.take(fileName);
        oldTimer->stop();
        oldTimer->deleteLater();
    }
}
