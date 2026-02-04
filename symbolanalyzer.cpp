#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "completionmanager.h"
#include <QtConcurrent/QtConcurrent>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QFileInfo>
#include <QThread>
#include <QApplication>
#include <QEventLoop>
#include <utility>

SymbolAnalyzer::SymbolAnalyzer(QObject *parent)
    : QObject(parent)
{
    workspaceAnalysisWatcher = new QFutureWatcher<QPair<int, int>>(this);
    connect(workspaceAnalysisWatcher, &QFutureWatcher<QPair<int, int>>::finished,
            this, &SymbolAnalyzer::onWorkspaceAnalysisFinished);
}

SymbolAnalyzer::~SymbolAnalyzer()
{
    if (workspaceAnalysisWatcher && workspaceAnalysisWatcher->isRunning()) {
        workspaceAnalysisWatcher->cancel();
    }
    if (workspaceAnalysisWatcher) {
        workspaceAnalysisWatcher->deleteLater();
        workspaceAnalysisWatcher = nullptr;
    }
}

void SymbolAnalyzer::analyzeOpenTabs(TabManager* tabManager)
{
    if (!tabManager) return;

    emit analysisStarted("open_tabs");

    sym_list* symbolList = sym_list::getInstance();
    QStringList openFileNames = tabManager->getAllOpenFileNames();
    QStringList svFiles = tabManager->getOpenSystemVerilogFiles();

    for (const QString& fileName : std::as_const(openFileNames)) {
        symbolList->clearSymbolsForFile(fileName);
    }

    for (const QString& fileName : std::as_const(svFiles)) {
        QString content = tabManager->getPlainTextFromOpenFile(fileName);
        if (!content.isNull())
            analyzeFileContent(fileName, content);
    }

    int symbolsFromOpenFiles = 0;
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();
    for (const sym_list::SymbolInfo& symbol : std::as_const(allSymbols)) {
        if (openFileNames.contains(symbol.fileName))
            symbolsFromOpenFiles++;
    }

    emit analysisCompleted("open_tabs", symbolsFromOpenFiles);
}

namespace {
    const int kWorkspaceBatchSize = 50;  // 每批处理文件数，控制内存与 UI 响应
}

void SymbolAnalyzer::analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) return;

    emit analysisStarted(workspaceManager->getWorkspacePath());

    QStringList svFiles = workspaceManager->getSystemVerilogFiles();
    const int totalFiles = svFiles.size();
    sym_list* symbolList = sym_list::getInstance();
    int totalSymbolsFound = 0;
    int filesAnalyzed = 0;
    QString lastProcessedPath;

    for (int i = 0; i < totalFiles; ) {
        const int batchEnd = qMin(i + kWorkspaceBatchSize, totalFiles);

        for (int j = i; j < batchEnd; ++j) {
            const QString& filePath = svFiles.at(j);
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
                continue;
            }
            QString content = QTextStream(&file).readAll();
            file.close();

            int symbolsBefore = symbolList->getAllSymbols().size();
            symbolList->setContentIncremental(filePath, content);
            int symbolsAfter = symbolList->getAllSymbols().size();

            totalSymbolsFound += (symbolsAfter - symbolsBefore);
            filesAnalyzed++;
            lastProcessedPath = filePath;
        }

        i = batchEnd;
        emit batchProgress(filesAnalyzed, totalFiles, lastProcessedPath);

        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        if (isCancelled && isCancelled()) {
            break;
        }
    }

    CompletionManager::getInstance()->forceRefreshSymbolCaches();

    emit batchAnalysisCompleted(svFiles.size(), totalSymbolsFound);
    emit analysisCompleted(workspaceManager->getWorkspacePath(), totalSymbolsFound);
}

void SymbolAnalyzer::startAnalyzeWorkspaceAsync(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) return;
    if (workspaceAnalysisWatcher && workspaceAnalysisWatcher->isRunning()) {
        workspaceAnalysisWatcher->cancel();
    }

    QStringList svFiles = workspaceManager->getSystemVerilogFiles();
    QString workspacePath = workspaceManager->getWorkspacePath();
    const int totalFiles = svFiles.size();

    emit analysisStarted(workspacePath);

    QFuture<QPair<int, int>> future = QtConcurrent::run([this, svFiles, totalFiles, isCancelled]() {
        sym_list* symbolList = sym_list::getInstance();
        int totalSymbolsFound = 0;
        int filesAnalyzed = 0;

        for (int j = 0; j < totalFiles; ++j) {
            if (isCancelled && isCancelled())
                break;
            const QString& filePath = svFiles.at(j);
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QFile::Text))
                continue;
            QString content = QTextStream(&file).readAll();
            file.close();

            int symbolsBefore = symbolList->getAllSymbols().size();
            symbolList->setContentIncremental(filePath, content);
            int symbolsAfter = symbolList->getAllSymbols().size();
            totalSymbolsFound += (symbolsAfter - symbolsBefore);
            filesAnalyzed++;
            emit batchProgress(filesAnalyzed, totalFiles, filePath);
        }
        return qMakePair(filesAnalyzed, totalSymbolsFound);
    });

    workspaceAnalysisWatcher->setProperty("workspacePath", workspacePath);
    workspaceAnalysisWatcher->setFuture(future);
}

void SymbolAnalyzer::onWorkspaceAnalysisFinished()
{
    if (!workspaceAnalysisWatcher) return;
    if (workspaceAnalysisWatcher->isCanceled())
        return;

    QPair<int, int> result = workspaceAnalysisWatcher->result();
    int filesAnalyzed = result.first;
    int totalSymbolsFound = result.second;
    QString workspacePath = workspaceAnalysisWatcher->property("workspacePath").toString();

    CompletionManager::getInstance()->forceRefreshSymbolCaches();

    emit batchAnalysisCompleted(filesAnalyzed, totalSymbolsFound);
    emit analysisCompleted(workspacePath, totalSymbolsFound);
}

void SymbolAnalyzer::analyzeFile(const QString& filePath)
{
    if (!isSystemVerilogFile(filePath)) return;

    emit analysisStarted(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
        emit analysisCompleted(filePath, 0);
        return;
    }
    QString content = QTextStream(&file).readAll();
    file.close();

    sym_list* symbolList = sym_list::getInstance();
    int symbolsBefore = symbolList->getAllSymbols().size();
    symbolList->setContentIncremental(filePath, content);
    int symbolsFound = symbolList->getAllSymbols().size() - symbolsBefore;
    emit analysisCompleted(filePath, symbolsFound);
}

bool SymbolAnalyzer::isAnalysisNeeded(const QString& fileName, const QString& content) const
{
    if (!lastAnalyzedContent.contains(fileName)) return true;

    return lastAnalyzedContent[fileName] != content;
}

void SymbolAnalyzer::invalidateCache()
{
    lastAnalyzedContent.clear();
    CompletionManager::getInstance()->invalidateAllCaches();
}

void SymbolAnalyzer::analyzeFileContent(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty() || !isSystemVerilogFile(fileName)) return;
    sym_list* sym = sym_list::getInstance();
    sym->setContentIncremental(fileName, content);
    int count = sym->findSymbolsByFileName(fileName).size();
    emit analysisCompleted(fileName, count);
}

QStringList SymbolAnalyzer::filterSystemVerilogFiles(const QStringList& files) const
{
    QStringList svFiles;
    svFiles.reserve(files.size());

    for (const QString& fileName : files) {
        if (isSystemVerilogFile(fileName)) {
            svFiles.append(fileName);
        }
    }
    return svFiles;
}

static bool lineContainsKeywordAsWord(const QString& line, const QString& keyword)
{
    int pos = 0;
    while ((pos = line.indexOf(keyword, pos)) >= 0) {
        bool startOk = (pos == 0) || (!line[pos - 1].isLetterOrNumber() && line[pos - 1] != QLatin1Char('_'));
        bool endOk = (pos + keyword.size() >= line.size()) || (!line[pos + keyword.size()].isLetterOrNumber() && line[pos + keyword.size()] != QLatin1Char('_'));
        if (startOk && endOk)
            return true;
        pos += keyword.size();
    }
    return false;
}

bool SymbolAnalyzer::hasSignificantChanges(const QString& oldContent, const QString& newContent) const
{
    static const QStringList significantKeywords = {
        QLatin1String("module"), QLatin1String("endmodule"), QLatin1String("reg"), QLatin1String("wire"), QLatin1String("logic"),
        QLatin1String("task"), QLatin1String("endtask"), QLatin1String("function"), QLatin1String("endfunction")
    };

    QStringList oldLines = oldContent.split(QLatin1Char('\n'));
    QStringList newLines = newContent.split(QLatin1Char('\n'));
    int maxLines = qMax(oldLines.size(), newLines.size());

    for (int i = 0; i < maxLines; ++i) {
        QString oldLine = (i < oldLines.size()) ? oldLines[i].trimmed() : QString();
        QString newLine = (i < newLines.size()) ? newLines[i].trimmed() : QString();
        if (oldLine != newLine) {
            for (const QString& keyword : significantKeywords) {
                if (lineContainsKeywordAsWord(oldLine, keyword) || lineContainsKeywordAsWord(newLine, keyword))
                    return true;
            }
        }
    }
    return false;
}

bool SymbolAnalyzer::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty()) return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
}
