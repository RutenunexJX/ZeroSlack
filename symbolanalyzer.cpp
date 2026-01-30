#include "symbolanalyzer.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "mycodeeditor.h"
#include "completionmanager.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDebug>
#include <QRegularExpression>
#include <QApplication>
#include <QEventLoop>

SymbolAnalyzer::SymbolAnalyzer(QObject *parent)
    : QObject(parent)
{
}

SymbolAnalyzer::~SymbolAnalyzer()
{
    // Clean up any remaining timers
    for (auto it = incrementalTimers.begin(); it != incrementalTimers.end(); ++it) {
        if (it.value()) {
            it.value()->deleteLater();
        }
    }

    for (auto it = significantTimers.begin(); it != significantTimers.end(); ++it) {
        if (it.value()) {
            it.value()->deleteLater();
        }
    }

}

void SymbolAnalyzer::analyzeOpenTabs(TabManager* tabManager)
{
    if (!tabManager) return;

    emit analysisStarted("open_tabs");

    QElapsedTimer totalTimer;
    totalTimer.start();

    sym_list* symbolList = sym_list::getInstance();
    QStringList openFileNames = tabManager->getAllOpenFileNames();
    QStringList svFiles = tabManager->getOpenSystemVerilogFiles();

    // Clear existing symbols for these files
    for (const QString& fileName : qAsConst(openFileNames)) {
        symbolList->clearSymbolsForFile(fileName);
    }

    int analyzedCount = 0;
    for (const QString& fileName : qAsConst(svFiles)) {
        // Find the editor for this file
        for (int i = 0; i < 20; ++i) { // Reasonable tab limit
            MyCodeEditor* editor = tabManager->getEditorAt(i);
            if (!editor) break;

            if (editor->getFileName() == fileName) {
                symbolList->setCodeEditor(editor);
                analyzedCount++;
                break;
            }
        }
    }

    qint64 totalMs = totalTimer.elapsed();
    // Get final symbol count
    QList<sym_list::SymbolInfo> allSymbols = symbolList->getAllSymbols();
    int symbolsFromOpenFiles = 0;

    for (const sym_list::SymbolInfo& symbol : qAsConst(allSymbols)) {
        if (openFileNames.contains(symbol.fileName)) {
            symbolsFromOpenFiles++;
        }
    }

    qDebug().noquote() << "[Perf] analyzeOpenTabs"
        << "files=" << analyzedCount
        << "totalMs=" << totalMs
        << "symbols=" << symbolsFromOpenFiles;

    emit analysisCompleted("open_tabs", symbolsFromOpenFiles);
}

namespace {
    const int kWorkspaceBatchSize = 50;  // 每批处理文件数，控制内存与 UI 响应
}

void SymbolAnalyzer::analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) return;

    emit analysisStarted(workspaceManager->getWorkspacePath());

    QElapsedTimer totalTimer;
    totalTimer.start();

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
            auto tempEditor = createBackgroundEditor(filePath);
            if (!tempEditor) {
                continue;
            }

            int symbolsBefore = symbolList->getAllSymbols().size();
            symbolList->setCodeEditorIncremental(tempEditor.get());
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

    qint64 totalMs = totalTimer.elapsed();
    qDebug().noquote() << "[Perf] analyzeWorkspace"
        << "path=" << workspaceManager->getWorkspacePath()
        << "files=" << filesAnalyzed << "/" << svFiles.size()
        << "totalMs=" << totalMs
        << "symbols=" << totalSymbolsFound
        << "avgMsPerFile=" << (filesAnalyzed > 0 ? (totalMs / filesAnalyzed) : 0);

    CompletionManager::getInstance()->forceRefreshSymbolCaches();

    emit batchAnalysisCompleted(svFiles.size(), totalSymbolsFound);
    emit analysisCompleted(workspaceManager->getWorkspacePath(), totalSymbolsFound);
}

void SymbolAnalyzer::analyzeFile(const QString& filePath)
{
    if (!isSystemVerilogFile(filePath)) return;

    emit analysisStarted(filePath);

    QElapsedTimer timer;
    timer.start();

    auto tempEditor = createBackgroundEditor(filePath);
    if (!tempEditor) {
        return;
    }

    sym_list* symbolList = sym_list::getInstance();
    int symbolsBefore = symbolList->getAllSymbols().size();

    symbolList->setCodeEditorIncremental(tempEditor.get());

    int symbolsAfter = symbolList->getAllSymbols().size();
    int symbolsFound = symbolsAfter - symbolsBefore;
    qint64 ms = timer.elapsed();

    qDebug().noquote() << "[Perf] analyzeFile"
        << "file=" << filePath
        << "ms=" << ms
        << "symbols=" << symbolsFound;

    emit analysisCompleted(filePath, symbolsFound);
}

void SymbolAnalyzer::analyzeEditor(MyCodeEditor* editor, bool incremental)
{
    if (!editor) return;

    QString fileName = editor->getFileName();
    if (fileName.isEmpty()) {
        return;
    }

    emit analysisStarted(fileName);

    QElapsedTimer timer;
    timer.start();

    sym_list* symbolList = sym_list::getInstance();
    int symbolsBefore = symbolList->getAllSymbols().size();

    if (incremental) {
        symbolList->setCodeEditorIncremental(editor);
    } else {
        symbolList->setCodeEditor(editor);
    }

    int symbolsAfter = symbolList->getAllSymbols().size();
    int symbolsFound = symbolsAfter - symbolsBefore;
    qint64 ms = timer.elapsed();

    qDebug().noquote() << "[Perf] analyzeEditor"
        << (incremental ? "incremental" : "full")
        << "file=" << fileName
        << "ms=" << ms
        << "symbols=" << symbolsFound;

    emit analysisCompleted(fileName, symbolsFound);
}

void SymbolAnalyzer::scheduleIncrementalAnalysis(MyCodeEditor* editor, int delay)
{
    if (!editor) return;

    // Cancel existing timer for this editor
    cancelScheduledAnalysis(editor);

    // Create new timer
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delay);

    connect(timer, &QTimer::timeout, this, [this, editor]() {
        if (editor) { // Check if editor still exists
            analyzeEditor(editor, true); // Incremental analysis
        }
        cleanupTimer(editor);
    });

    incrementalTimers[editor] = timer;
    timer->start();
}

void SymbolAnalyzer::scheduleSignificantAnalysis(MyCodeEditor* editor, int delay)
{
    if (!editor) return;

    // Cancel existing significant timer for this editor
    if (significantTimers.contains(editor)) {
        significantTimers[editor]->stop();
        significantTimers[editor]->deleteLater();
        significantTimers.remove(editor);
    }

    // Create new timer
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delay);

    connect(timer, &QTimer::timeout, this, [this, editor]() {
        if (editor) { // Check if editor still exists
            analyzeEditor(editor, false); // Full analysis
        }
        cleanupTimer(editor);
    });

    significantTimers[editor] = timer;
    timer->start();
}

void SymbolAnalyzer::cancelScheduledAnalysis(MyCodeEditor* editor)
{
    // Cancel incremental timer
    if (incrementalTimers.contains(editor)) {
        incrementalTimers[editor]->stop();
        incrementalTimers[editor]->deleteLater();
        incrementalTimers.remove(editor);
    }

    // Cancel significant timer
    if (significantTimers.contains(editor)) {
        significantTimers[editor]->stop();
        significantTimers[editor]->deleteLater();
        significantTimers.remove(editor);
    }
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

void SymbolAnalyzer::onIncrementalAnalysisTimer()
{
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;

    // Find which editor this timer belongs to
    for (auto it = incrementalTimers.begin(); it != incrementalTimers.end(); ++it) {
        if (it.value() == timer) {
            MyCodeEditor* editor = it.key();
            if (editor) {
                analyzeEditor(editor, true);
            }
            cleanupTimer(editor);
            break;
        }
    }
}

void SymbolAnalyzer::onSignificantAnalysisTimer()
{
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;

    // Find which editor this timer belongs to
    for (auto it = significantTimers.begin(); it != significantTimers.end(); ++it) {
        if (it.value() == timer) {
            MyCodeEditor* editor = it.key();
            if (editor) {
                analyzeEditor(editor, false);
            }
            cleanupTimer(editor);
            break;
        }
    }
}

std::unique_ptr<MyCodeEditor> SymbolAnalyzer::createBackgroundEditor(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QFile::Text)) {
        return nullptr;
    }

    // Create editor with smart pointer
    auto tempEditor = std::unique_ptr<MyCodeEditor>(new MyCodeEditor());
    tempEditor->setVisible(false);

    // Load content efficiently
    QTextStream in(&file);
    tempEditor->setPlainText(in.readAll());
    tempEditor->setFileName(filePath);

    file.close();

    return tempEditor;
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

void SymbolAnalyzer::cleanupTimer(MyCodeEditor* editor)
{
    // Clean up incremental timer
    if (incrementalTimers.contains(editor)) {
        incrementalTimers[editor]->deleteLater();
        incrementalTimers.remove(editor);
    }

    // Clean up significant timer
    if (significantTimers.contains(editor)) {
        significantTimers[editor]->deleteLater();
        significantTimers.remove(editor);
    }
}

bool SymbolAnalyzer::hasSignificantChanges(const QString& oldContent, const QString& newContent) const
{
    // Check for significant keywords that would affect symbol structure
    static QStringList significantKeywords = {
        "module", "endmodule", "reg", "wire", "logic",
        "task", "endtask", "function", "endfunction"
    };

    QStringList oldLines = oldContent.split('\n');
    QStringList newLines = newContent.split('\n');

    int maxLines = qMax(oldLines.size(), newLines.size());

    for (int i = 0; i < maxLines; ++i) {
        QString oldLine = (i < oldLines.size()) ? oldLines[i].trimmed() : QString();
        QString newLine = (i < newLines.size()) ? newLines[i].trimmed() : QString();

        if (oldLine != newLine) {
            // Check if either line contains significant keywords
            for (const QString& keyword : significantKeywords) {
                QRegularExpression keywordRegex("\\b" + QRegularExpression::escape(keyword) + "\\b");
                if (oldLine.contains(keywordRegex) || newLine.contains(keywordRegex)) {
                    return true;
                }
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
