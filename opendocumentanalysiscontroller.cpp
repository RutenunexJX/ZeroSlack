#include "opendocumentanalysiscontroller.h"

#include "documentmodel.h"
#include "semanticindex.h"
#include "symbolanalyzer.h"

#include <QTimer>

#include <utility>

namespace {
constexpr int kAsyncOpenDocumentAnalysisCharacters = 2 * 1024 * 1024;

bool hasNonWhitespaceChange(const QString& oldContent,
                            const QString& newContent)
{
    qsizetype oldIndex = 0;
    qsizetype newIndex = 0;
    while (true) {
        while (oldIndex < oldContent.size()
               && oldContent.at(oldIndex).isSpace()) {
            ++oldIndex;
        }
        while (newIndex < newContent.size()
               && newContent.at(newIndex).isSpace()) {
            ++newIndex;
        }

        const bool oldAtEnd = oldIndex == oldContent.size();
        const bool newAtEnd = newIndex == newContent.size();
        if (oldAtEnd || newAtEnd)
            return oldAtEnd != newAtEnd;
        if (oldContent.at(oldIndex) != newContent.at(newIndex))
            return true;

        ++oldIndex;
        ++newIndex;
    }
}
}

OpenDocumentAnalysisController::OpenDocumentAnalysisController(QObject* parent)
    : QObject(parent)
{
}

OpenDocumentAnalysisController::~OpenDocumentAnalysisController()
{
    shutdown();
}

void OpenDocumentAnalysisController::shutdown()
{
    if (shuttingDown)
        return;
    shuttingDown = true;

    const QList<QTimer*> openTimers = openFileAnalysisTimers.values();
    openFileAnalysisTimers.clear();
    const QList<QTimer*> changeTimers = fileChangeDebounceTimers.values();
    fileChangeDebounceTimers.clear();
    for (QTimer* timer : openTimers) {
        if (!timer)
            continue;
        timer->stop();
        disconnect(timer, nullptr, this, nullptr);
        timer->deleteLater();
    }
    for (QTimer* timer : changeTimers) {
        if (!timer)
            continue;
        timer->stop();
        disconnect(timer, nullptr, this, nullptr);
        timer->deleteLater();
    }

    symbolAnalyzer = nullptr;
    documentModel = nullptr;
    openFileContentProvider = {};
    workspaceOpenProvider = {};
    workspaceAnalysisActiveProvider = {};
}

void OpenDocumentAnalysisController::setDocumentModel(DocumentModel* model)
{
    documentModel = shuttingDown ? nullptr : model;
}

void OpenDocumentAnalysisController::setSymbolAnalyzer(SymbolAnalyzer* analyzer)
{
    if (symbolAnalyzer == analyzer)
        return;
    if (symbolAnalyzer)
        disconnect(symbolAnalyzer, nullptr, this, nullptr);
    symbolAnalyzer = shuttingDown ? nullptr : analyzer;
    if (!symbolAnalyzer)
        return;

    connect(symbolAnalyzer,
            &QObject::destroyed,
            this,
            [this]() {
                symbolAnalyzer = nullptr;
                for (QTimer* timer : std::as_const(openFileAnalysisTimers)) {
                    if (timer) {
                        timer->stop();
                        timer->deleteLater();
                    }
                }
                openFileAnalysisTimers.clear();
                for (QTimer* timer : std::as_const(fileChangeDebounceTimers)) {
                    if (timer) {
                        timer->stop();
                        timer->deleteLater();
                    }
                }
                fileChangeDebounceTimers.clear();
            });
}

void OpenDocumentAnalysisController::setOpenFileContentProvider(
    std::function<QString(const QString&)> provider)
{
    openFileContentProvider = shuttingDown
        ? std::function<QString(const QString&)>()
        : std::move(provider);
}

void OpenDocumentAnalysisController::setWorkspaceOpenProvider(
    std::function<bool()> provider)
{
    workspaceOpenProvider = shuttingDown
        ? std::function<bool()>()
        : std::move(provider);
}

void OpenDocumentAnalysisController::setWorkspaceAnalysisActiveProvider(
    std::function<bool()> provider)
{
    workspaceAnalysisActiveProvider = shuttingDown
        ? std::function<bool()>()
        : std::move(provider);
}

void OpenDocumentAnalysisController::handleDocumentEdited(
    const DocumentSnapshot& snapshot,
    int relationshipDelayMs)
{
    if (shuttingDown || snapshot.fileName.isEmpty())
        return;

    QPointer<OpenDocumentAnalysisController> self(this);
    const QString content = contentForOpenFile(snapshot.fileName);
    if (!self || content.isNull())
        return;

    if (content.size() > kAsyncOpenDocumentAnalysisCharacters) {
        scheduleOpenFileAnalysis(snapshot.fileName,
                                 1500,
                                 static_cast<std::uint64_t>(snapshot.textVersion));
        return;
    }

    const QString indexedContent =
        SemanticIndex::getInstance()->getCachedFileContent(snapshot.fileName);
    // Relationship analysis must react to every non-whitespace source edit,
    // not only to the small structural-keyword subset used by
    // SymbolAnalyzer::hasSignificantChanges. Compare the streams without
    // allocating normalized copies so large-file whitespace edits remain
    // cheap while ordinary identifiers, expressions, and macros still enter
    // the debounce path.
    if (indexedContent.isNull()
        || hasNonWhitespaceChange(indexedContent, content)) {
        emit relationshipAnalysisScheduled(snapshot.fileName,
                                           content,
                                           relationshipDelayMs);
        if (!self)
            return;
    }

    // Any semantic edit can affect another open buffer through constants,
    // types, dimensions, enums, or instance overrides. After the debounce,
    // analyze one immutable snapshot of all open buffers; the analyzer cancels
    // older computations and publishes only the latest complete revision set.
    scheduleOpenFileAnalysis(snapshot.fileName,
                             1000,
                             static_cast<std::uint64_t>(snapshot.textVersion));
}

void OpenDocumentAnalysisController::analyzeOpenDocumentNow(
    const DocumentSnapshot& snapshot,
    bool requestRelationships)
{
    if (shuttingDown || snapshot.fileName.isEmpty() || !symbolAnalyzer)
        return;

    QPointer<OpenDocumentAnalysisController> self(this);
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;

    const QString content = contentForOpenFile(snapshot.fileName);
    if (!self || !analyzer || content.isNull())
        return;

    const bool workspaceOpen = isWorkspaceOpen();
    if (!self || !analyzer)
        return;
    if (workspaceOpen) {
        const QString indexedContent =
            SemanticIndex::getInstance()->getCachedFileContent(
                snapshot.fileName);
        if (snapshot.saved
            && !snapshot.dirty
            && !indexedContent.isNull()
            && indexedContent == content) {
            // A persisted workspace file is already represented by the
            // authoritative workspace snapshot. Opening it changes only the
            // active presentation; re-elaborating every open buffer would
            // publish an equivalent snapshot and churn Design / Ghost state.
            emit documentRefreshRequested(snapshot.fileName);
            return;
        }

        // The workspace controller has already queued a replacement atomic
        // snapshot for open / edit / save events. Starting an overlay task
        // here would invalidate that replacement and lose its completion.
        const bool workspaceAnalysisActive = isWorkspaceAnalysisActive();
        if (!self)
            return;
        if (workspaceAnalysisActive) {
            emit documentRefreshRequested(snapshot.fileName);
            return;
        }
        // Workspace semantic state is one overlay transaction. Re-submit every
        // current buffer even when this file is byte-identical so cross-file
        // dependencies and source-text revisions are captured together.
        if (documentModel)
            analyzeOpenDocumentsNow();
        else if (analyzer)
            analyzer->analyzeFileContentAsync(
                snapshot.fileName,
                content,
                static_cast<std::uint64_t>(snapshot.textVersion));
        if (!self)
            return;
        emit documentRefreshRequested(snapshot.fileName);
        return;
    }

    if (content.size() > kAsyncOpenDocumentAnalysisCharacters) {
        analyzer->analyzeFileContentAsync(
            snapshot.fileName,
            content,
            static_cast<std::uint64_t>(snapshot.textVersion));
        if (!self)
            return;
        emit documentRefreshRequested(snapshot.fileName);
        return;
    }

    analyzer->analyzeFileContent(
        snapshot.fileName,
        content,
        static_cast<std::uint64_t>(snapshot.textVersion));
    if (!self)
        return;
    emit documentRefreshRequested(snapshot.fileName);
    if (!self)
        return;
    if (requestRelationships && !isWorkspaceOpen()) {
        if (!self)
            return;
        emit relationshipAnalysisRequested(snapshot.fileName, content);
    }
}

void OpenDocumentAnalysisController::analyzeOpenDocumentsNow()
{
    if (shuttingDown || !documentModel || !symbolAnalyzer)
        return;
    QPointer<OpenDocumentAnalysisController> self(this);
    const QPointer<DocumentModel> model = documentModel;
    const QPointer<SymbolAnalyzer> analyzer = symbolAnalyzer;
    const bool workspaceOpen = isWorkspaceOpen();
    if (!self || !model || !analyzer)
        return;
    if (workspaceOpen) {
        const bool workspaceAnalysisActive = isWorkspaceAnalysisActive();
        if (!self || workspaceAnalysisActive)
            return;
    }
    if (!self)
        return;

    QList<OpenDocumentContent> openDocuments;
    for (const DocumentSnapshot& snapshot : model->openDocuments()) {
        if (snapshot.fileName.isEmpty())
            continue;

        const QString content = contentForOpenFile(snapshot.fileName);
        if (!self)
            return;
        if (content.isNull())
            continue;
        openDocuments.append(
            {snapshot.fileName,
             content,
             static_cast<std::uint64_t>(snapshot.textVersion)});
    }

    if (self && analyzer)
        analyzer->analyzeOpenDocuments(openDocuments);
}

QString OpenDocumentAnalysisController::contentForOpenFile(
    const QString& fileName) const
{
    const QPointer<DocumentModel> documents = documentModel;
    const std::function<QString(const QString&)> provider =
        openFileContentProvider;
    if (documents) {
        const QString modelText = documents->documentTextForFile(fileName);
        if (!modelText.isNull())
            return modelText;
    }

    if (provider)
        return provider(fileName);
    return QString();
}

bool OpenDocumentAnalysisController::isWorkspaceOpen() const
{
    const std::function<bool()> provider = workspaceOpenProvider;
    return provider ? provider() : false;
}

bool OpenDocumentAnalysisController::isDirtyOpenDocument(const QString& fileName) const
{
    const QPointer<DocumentModel> documents = documentModel;
    if (!documents || fileName.isEmpty())
        return false;
    return documents->documentForFile(fileName).dirty;
}

bool OpenDocumentAnalysisController::isWorkspaceAnalysisActive() const
{
    const std::function<bool()> provider = workspaceAnalysisActiveProvider;
    return provider ? provider() : false;
}
