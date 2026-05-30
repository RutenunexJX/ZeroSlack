#include "symbolanalyzer.h"
#include "slangmanager.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "completionmanager.h"
#include <QtConcurrent/QtConcurrent>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QApplication>
#include <QEventLoop>
#include <utility>

SymbolAnalyzer::SymbolAnalyzer(QObject *parent)
    : QObject(parent)
{
    m_slangManager = new SlangManager();
    workspaceAnalysisWatcher = new QFutureWatcher<QList<sym_list::SymbolInfo>>(this);
    connect(workspaceAnalysisWatcher, &QFutureWatcher<QList<sym_list::SymbolInfo>>::finished,
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
    delete m_slangManager;
    m_slangManager = nullptr;
}

void SymbolAnalyzer::analyzeOpenTabs(TabManager* tabManager)
{
    if (!tabManager) return;

    emit analysisStarted("open_tabs");

    sym_list* symbolList = sym_list::getInstance();
    QStringList svFiles = tabManager->getOpenSystemVerilogFiles();
    int symbolsFromOpenFiles = 0;

    for (const QString& fileName : std::as_const(svFiles)) {
        QString content = tabManager->getPlainTextFromOpenFile(fileName);
        if (content.isNull()) continue;
        QList<sym_list::SymbolInfo> list = m_slangManager->extractSymbols(fileName, content);
        symbolList->setSymbolsForFile(fileName, list, content);
        symbolsFromOpenFiles += list.size();
    }

    emit analysisCompleted("open_tabs", symbolsFromOpenFiles);
}

static QHash<QString, QList<sym_list::SymbolInfo>> groupSymbolsByFile(const QList<sym_list::SymbolInfo>& list)
{
    QHash<QString, QList<sym_list::SymbolInfo>> byFile;
    for (const sym_list::SymbolInfo& s : list) {
        if (!s.fileName.isEmpty())
            byFile[s.fileName].append(s);
    }
    return byFile;
}

void SymbolAnalyzer::analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) return;

    emit analysisStarted(workspaceManager->getWorkspacePath());

    QStringList svFiles = workspaceManager->getSystemVerilogFiles();
    const int totalFiles = svFiles.size();
    if (totalFiles == 0) {
        CompletionManager::getInstance()->forceRefreshSymbolCaches();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(workspaceManager->getWorkspacePath(), 0);
        return;
    }

    QList<sym_list::SymbolInfo> allSymbols = m_slangManager->extractWorkspaceSymbols(svFiles);
    if (isCancelled && isCancelled()) {
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(workspaceManager->getWorkspacePath(), 0);
        return;
    }

    QHash<QString, QList<sym_list::SymbolInfo>> byFile = groupSymbolsByFile(allSymbols);
    sym_list* symbolList = sym_list::getInstance();
    int filesAnalyzed = 0;
    int totalSymbolsFound = 0;
    for (auto it = byFile.begin(); it != byFile.end(); ++it) {
        symbolList->setSymbolsForFile(it.key(), it.value());
        filesAnalyzed++;
        totalSymbolsFound += it.value().size();
    }
    emit batchProgress(filesAnalyzed, totalFiles, byFile.isEmpty() ? QString() : byFile.keys().first());

    CompletionManager::getInstance()->forceRefreshSymbolCaches();
    emit batchAnalysisCompleted(filesAnalyzed, totalSymbolsFound);
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

    QFuture<QList<sym_list::SymbolInfo>> future = QtConcurrent::run([this, svFiles, totalFiles, isCancelled]() {
        QList<sym_list::SymbolInfo> result = m_slangManager->extractWorkspaceSymbols(svFiles);
        return result;
    });

    workspaceAnalysisWatcher->setProperty("workspacePath", workspacePath);
    workspaceAnalysisWatcher->setProperty("totalFiles", totalFiles);
    workspaceAnalysisWatcher->setFuture(future);
}

void SymbolAnalyzer::onWorkspaceAnalysisFinished()
{
    if (!workspaceAnalysisWatcher) return;
    if (workspaceAnalysisWatcher->isCanceled())
        return;

    QList<sym_list::SymbolInfo> list = workspaceAnalysisWatcher->result();
    QString workspacePath = workspaceAnalysisWatcher->property("workspacePath").toString();

    QHash<QString, QList<sym_list::SymbolInfo>> byFile = groupSymbolsByFile(list);
    sym_list* symbolList = sym_list::getInstance();
    int filesAnalyzed = 0;
    int totalSymbolsFound = 0;
    for (auto it = byFile.begin(); it != byFile.end(); ++it) {
        symbolList->setSymbolsForFile(it.key(), it.value());
        filesAnalyzed++;
        totalSymbolsFound += it.value().size();
    }

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

    QList<sym_list::SymbolInfo> list = m_slangManager->extractSymbols(filePath, content);
    sym_list* symbolList = sym_list::getInstance();
    symbolList->setSymbolsForFile(filePath, list, content);
    emit analysisCompleted(filePath, list.size());
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
    QList<sym_list::SymbolInfo> list = m_slangManager->extractSymbols(fileName, content);
    sym_list* sym = sym_list::getInstance();
    sym->setSymbolsForFile(fileName, list, content);
    emit analysisCompleted(fileName, list.size());
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
