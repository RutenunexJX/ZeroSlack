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
    workspaceAnalysisWatcher = new QFutureWatcher<WorkspaceAnalysisResult>(this);
    connect(workspaceAnalysisWatcher, &QFutureWatcher<WorkspaceAnalysisResult>::finished,
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

static QString readTextFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text))
        return QString();
    QTextStream stream(&file);
    return stream.readAll();
}

static WorkspaceAnalysisResult buildWorkspaceAnalysisResult(const QStringList& svFiles,
                                                            const QList<sym_list::SymbolInfo>& allSymbols,
                                                            std::function<bool()> isCancelled)
{
    WorkspaceAnalysisResult result;
    QHash<QString, QList<sym_list::SymbolInfo>> byFile = groupSymbolsByFile(allSymbols);
    result.files.reserve(svFiles.size());

    for (const QString& filePath : svFiles) {
        if (isCancelled && isCancelled())
            break;
        WorkspaceFileAnalysis fileResult;
        fileResult.fileName = filePath;
        fileResult.content = readTextFile(filePath);
        fileResult.symbols = byFile.value(filePath);
        result.totalSymbols += fileResult.symbols.size();
        result.files.append(std::move(fileResult));
    }

    return result;
}

void SymbolAnalyzer::analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) return;
    analyzeProject(workspaceManager->projectSnapshot(), std::move(isCancelled));
}

void SymbolAnalyzer::analyzeProject(const ProjectSnapshot& project, std::function<bool()> isCancelled)
{
    if (!project.isOpen()) return;

    emit analysisStarted(project.workspaceRoot);

    QStringList svFiles = project.systemVerilogFiles;
    const int totalFiles = svFiles.size();
    if (totalFiles == 0) {
        CompletionManager::getInstance()->forceRefreshSymbolCaches();
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }

    QList<sym_list::SymbolInfo> allSymbols = m_slangManager->extractWorkspaceSymbols(svFiles);
    if (isCancelled && isCancelled()) {
        emit batchAnalysisCompleted(0, 0);
        emit analysisCompleted(project.workspaceRoot, 0);
        return;
    }

    WorkspaceAnalysisResult result = buildWorkspaceAnalysisResult(svFiles, allSymbols, isCancelled);
    sym_list* symbolList = sym_list::getInstance();
    int filesAnalyzed = 0;
    for (const WorkspaceFileAnalysis& fileResult : std::as_const(result.files)) {
        symbolList->setSymbolsForFile(fileResult.fileName, fileResult.symbols, fileResult.content);
        filesAnalyzed++;
        emit batchProgress(filesAnalyzed, totalFiles, fileResult.fileName);
    }

    CompletionManager::getInstance()->forceRefreshSymbolCaches();
    emit batchAnalysisCompleted(filesAnalyzed, result.totalSymbols);
    emit analysisCompleted(project.workspaceRoot, result.totalSymbols);
}

void SymbolAnalyzer::startAnalyzeWorkspaceAsync(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) return;
    startAnalyzeProjectAsync(workspaceManager->projectSnapshot(), std::move(isCancelled));
}

void SymbolAnalyzer::startAnalyzeProjectAsync(const ProjectSnapshot& project, std::function<bool()> isCancelled)
{
    if (!project.isOpen()) return;
    if (workspaceAnalysisWatcher && workspaceAnalysisWatcher->isRunning()) {
        workspaceAnalysisWatcher->cancel();
    }

    QStringList svFiles = project.systemVerilogFiles;
    QString workspacePath = project.workspaceRoot;
    const int totalFiles = svFiles.size();

    emit analysisStarted(workspacePath);

    QFuture<WorkspaceAnalysisResult> future = QtConcurrent::run([this, svFiles, isCancelled]() {
        QList<sym_list::SymbolInfo> result = m_slangManager->extractWorkspaceSymbols(svFiles);
        return buildWorkspaceAnalysisResult(svFiles, result, isCancelled);
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

    WorkspaceAnalysisResult result = workspaceAnalysisWatcher->result();
    QString workspacePath = workspaceAnalysisWatcher->property("workspacePath").toString();
    int totalFiles = workspaceAnalysisWatcher->property("totalFiles").toInt();

    sym_list* symbolList = sym_list::getInstance();
    int filesAnalyzed = 0;
    for (const WorkspaceFileAnalysis& fileResult : std::as_const(result.files)) {
        symbolList->setSymbolsForFile(fileResult.fileName, fileResult.symbols, fileResult.content);
        filesAnalyzed++;
        emit batchProgress(filesAnalyzed, totalFiles, fileResult.fileName);
    }

    CompletionManager::getInstance()->forceRefreshSymbolCaches();
    emit batchAnalysisCompleted(filesAnalyzed, result.totalSymbols);
    emit analysisCompleted(workspacePath, result.totalSymbols);
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

void SymbolAnalyzer::analyzeFileContentAsync(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty() || !isSystemVerilogFile(fileName)) return;

    // The expensive part is Slang parse + elaboration; run it off the UI thread. A fresh local
    // SlangManager keeps the background task self-contained (extractSymbols holds no shared state).
    auto* watcher = new QFutureWatcher<QList<sym_list::SymbolInfo>>(this);
    connect(watcher, &QFutureWatcher<QList<sym_list::SymbolInfo>>::finished, this,
            [this, fileName, content, watcher]() {
                QList<sym_list::SymbolInfo> list = watcher->result();
                watcher->deleteLater();
                // Write-back (DB + scope tree + caches) on the main thread.
                sym_list::getInstance()->setSymbolsForFile(fileName, list, content);
                emit analysisCompleted(fileName, list.size());
            });
    watcher->setFuture(QtConcurrent::run([fileName, content]() {
        SlangManager local;
        return local.extractSymbols(fileName, content);
    }));
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

    // 收集「含结构关键字的行」的集合（去序、忽略空白）。
    auto significantLines = [&](const QString& content) {
        QStringList out;
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString& raw : lines) {
            const QString line = raw.trimmed();
            if (line.isEmpty())
                continue;
            for (const QString& keyword : significantKeywords) {
                if (lineContainsKeywordAsWord(line, keyword)) {
                    out.append(line);
                    break;
                }
            }
        }
        out.sort();
        return out;
    };

    // 仅当「结构行集合」变化时才算显著变更。
    // 修复：原实现按行号逐行比较，插入/删除一个换行会使后续所有行错位，而这些行多含 reg/wire/logic
    // 等关键字，于是几乎任何编辑（哪怕只敲回车）都被误判为显著、触发关系分析。改为比较去序的结构行集合后，
    // 插入空行 / 行号平移不再触发分析；只有真正增删/修改结构声明才触发。
    return significantLines(oldContent) != significantLines(newContent);
}

bool SymbolAnalyzer::isSystemVerilogFile(const QString& fileName) const
{
    if (fileName.isEmpty()) return false;

    static const QStringList svExtensions = {"sv", "v", "vh", "svh", "vp", "svp"};
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return svExtensions.contains(suffix);
}
