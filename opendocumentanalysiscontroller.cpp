#include "opendocumentanalysiscontroller.h"

#include "documentmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

OpenDocumentAnalysisController::OpenDocumentAnalysisController(QObject* parent)
    : QObject(parent)
{
}

OpenDocumentAnalysisController::~OpenDocumentAnalysisController()
{
    for (QTimer* timer : openFileAnalysisTimers) {
        if (timer)
            timer->stop();
    }
    for (QTimer* timer : fileChangeDebounceTimers) {
        if (timer)
            timer->stop();
    }
}

void OpenDocumentAnalysisController::setDocumentModel(DocumentModel* model)
{
    documentModel = model;
}

void OpenDocumentAnalysisController::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    symbolAnalyzer = analyzer;
}

void OpenDocumentAnalysisController::setOpenFileContentProvider(
    std::function<QString(const QString&)> provider)
{
    openFileContentProvider = std::move(provider);
}

void OpenDocumentAnalysisController::setWorkspaceOpenProvider(
    std::function<bool()> provider)
{
    workspaceOpenProvider = std::move(provider);
}

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

void OpenDocumentAnalysisController::handleDocumentEdited(
    const DocumentSnapshot& snapshot,
    int relationshipDelayMs)
{
    if (snapshot.fileName.isEmpty())
        return;

    const QString content = contentForOpenFile(snapshot.fileName);
    if (content.isNull())
        return;

    emit relationshipAnalysisScheduled(snapshot.fileName,
                                       content,
                                       relationshipDelayMs);

    if (isWorkspaceOpen())
        return;

    if (lineContainsStructuralKeyword(content, snapshot.cursorLine))
        scheduleOpenFileAnalysis(snapshot.fileName, 1000);
}

void OpenDocumentAnalysisController::analyzeOpenDocumentNow(
    const DocumentSnapshot& snapshot,
    bool skipUnchanged)
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
    emit relationshipAnalysisRequested(snapshot.fileName, content);
}

void OpenDocumentAnalysisController::analyzeOpenDocumentsNow()
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

QString OpenDocumentAnalysisController::contentForOpenFile(
    const QString& fileName) const
{
    if (documentModel) {
        const QString modelText = documentModel->documentTextForFile(fileName);
        if (!modelText.isNull())
            return modelText;
    }

    if (openFileContentProvider)
        return openFileContentProvider(fileName);
    return QString();
}

bool OpenDocumentAnalysisController::isWorkspaceOpen() const
{
    return workspaceOpenProvider ? workspaceOpenProvider() : false;
}

bool OpenDocumentAnalysisController::lineContainsStructuralKeyword(
    const QString& content,
    int oneBasedLine) const
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
