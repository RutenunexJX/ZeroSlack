#include "analysisscheduler.h"

#include "activitylogservice.h"

#include <QFileInfo>

void AnalysisScheduler::setDocumentModel(DocumentModel* model)
{
    if (documentModel == model)
        return;
    if (documentModel)
        disconnect(documentModel, nullptr, this, nullptr);

    documentModel = model;
    if (openDocumentAnalysis)
        openDocumentAnalysis->setDocumentModel(model);
    if (workspaceSymbolAnalysis)
        workspaceSymbolAnalysis->setDocumentModel(model);
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

void AnalysisScheduler::scheduleOpenFileAnalysis(const QString& fileName, int delayMs)
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Analyzer"),
        ActivityLogLevel::Info,
        QStringLiteral("Scheduled open-file analysis for %1 in %2 ms")
            .arg(QFileInfo(fileName).fileName())
            .arg(delayMs));
    if (openDocumentAnalysis)
        openDocumentAnalysis->scheduleOpenFileAnalysis(fileName, delayMs);
}

void AnalysisScheduler::cancelScheduledOpenFileAnalysis(const QString& fileName)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->cancelScheduledOpenFileAnalysis(fileName);
}

void AnalysisScheduler::handleExternalFileChanged(const QString& fileName, int debounceMs)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->handleExternalFileChanged(fileName, debounceMs);
}

void AnalysisScheduler::handleDocumentClosed(const QString& fileName)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->handleDocumentClosed(fileName);
    if (relationshipAnalysisQueue)
        relationshipAnalysisQueue->clearFile(fileName);
    if (openDocumentAnalysis)
        openDocumentAnalysis->analyzeOpenDocumentsNow();

    if (relationshipResultPublisher)
        relationshipResultPublisher->invalidateFileRelationships(fileName);
}

void AnalysisScheduler::onDocumentOpened(const DocumentSnapshot& snapshot)
{
    ActivityLogService::getInstance()->append(
        QStringLiteral("Editor"),
        ActivityLogLevel::Info,
        QStringLiteral("Opened %1").arg(QFileInfo(snapshot.fileName).fileName()));
    if (openDocumentAnalysis)
        openDocumentAnalysis->analyzeOpenDocumentNow(snapshot, false);
}

void AnalysisScheduler::onDocumentEdited(const DocumentSnapshot& snapshot)
{
    if (openDocumentAnalysis) {
        openDocumentAnalysis->handleDocumentEdited(
            snapshot,
            kOpenDocumentRelationshipAnalysisDebounceMs);
    }
}

void AnalysisScheduler::onDocumentSaved(const DocumentSnapshot& snapshot)
{
    if (openDocumentAnalysis)
        openDocumentAnalysis->analyzeOpenDocumentNow(snapshot, true, false);
}

QString AnalysisScheduler::contentForOpenFile(const QString& fileName) const
{
    return openDocumentAnalysis
        ? openDocumentAnalysis->contentForOpenFile(fileName)
        : QString();
}
