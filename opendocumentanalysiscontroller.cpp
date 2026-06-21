#include "opendocumentanalysiscontroller.h"

#include "documentmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

#include <QRegularExpression>
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

void OpenDocumentAnalysisController::handleDocumentEdited(
    const DocumentSnapshot& snapshot,
    int relationshipDelayMs)
{
    if (snapshot.fileName.isEmpty())
        return;

    const QString content = contentForOpenFile(snapshot.fileName);
    if (content.isNull())
        return;

    if (isWorkspaceOpen()) {
        const QString cachedContent =
            SemanticIndex::getInstance()->getCachedFileContent(snapshot.fileName);
        if (!cachedContent.isEmpty()
            && !hasNonWhitespaceChange(cachedContent, content)) {
            return;
        }
    }

    emit relationshipAnalysisScheduled(snapshot.fileName,
                                       content,
                                       relationshipDelayMs);

    if (lineContainsStructuralKeyword(content, snapshot.cursorLine))
        scheduleOpenFileAnalysis(snapshot.fileName, 1000);
}

void OpenDocumentAnalysisController::analyzeOpenDocumentNow(
    const DocumentSnapshot& snapshot,
    bool skipUnchanged,
    bool requestRelationships)
{
    if (snapshot.fileName.isEmpty() || !symbolAnalyzer)
        return;

    const QString content = contentForOpenFile(snapshot.fileName);
    if (content.isEmpty())
        return;

    if (isWorkspaceOpen()) {
        const QString cachedContent =
            SemanticIndex::getInstance()->getCachedFileContent(snapshot.fileName);
        if (!cachedContent.isEmpty() && cachedContent == content) {
            emit documentRefreshRequested(snapshot.fileName);
            return;
        }
    }

    if (skipUnchanged
        && !SemanticIndex::getInstance()->contentAffectsSymbols(snapshot.fileName, content)) {
        emit documentRefreshRequested(snapshot.fileName);
        return;
    }

    symbolAnalyzer->analyzeFileContent(snapshot.fileName, content);
    emit documentRefreshRequested(snapshot.fileName);
    if (requestRelationships && !isWorkspaceOpen())
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

bool OpenDocumentAnalysisController::isDirtyOpenDocument(const QString& fileName) const
{
    if (!documentModel || fileName.isEmpty())
        return false;
    return documentModel->documentForFile(fileName).dirty;
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

bool OpenDocumentAnalysisController::hasNonWhitespaceChange(
    const QString& oldContent,
    const QString& newContent) const
{
    auto withoutWhitespace = [](const QString& text) {
        QString result;
        result.reserve(text.size());
        for (const QChar ch : text) {
            if (!ch.isSpace())
                result.append(ch);
        }
        return result;
    };

    return withoutWhitespace(oldContent) != withoutWhitespace(newContent);
}
