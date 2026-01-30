#ifndef SYMBOLANALYZER_H
#define SYMBOLANALYZER_H

#include <QObject>
#include <QStringList>
#include <QFutureWatcher>
#include <QPair>
#include <functional>
#include "syminfo.h"

class TabManager;
class WorkspaceManager;

class SymbolAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit SymbolAnalyzer(QObject *parent = nullptr);
    ~SymbolAnalyzer();

    // Analysis modes
    void analyzeOpenTabs(TabManager* tabManager);
    /** 工作区批量符号分析；按批读取并分析以控制内存与 UI 响应。isCancelled 可选，返回 true 时中止。 */
    void analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled = nullptr);
    /** 阶段 A：在后台线程执行工作区符号分析，不阻塞 UI；进度通过 batchProgress 等信号回传。 */
    void startAnalyzeWorkspaceAsync(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled = nullptr);
    void analyzeFile(const QString& filePath);
    /** 阶段 B：基于内容的解析，不创建 QWidget；直接对 QString 做正则解析，供工作区/单文件分析使用 */
    void analyzeFileContent(const QString& fileName, const QString& content);

    // Utility
    bool isAnalysisNeeded(const QString& fileName, const QString& content) const;
    /** 阶段 C：判断新旧内容是否在结构/定义上有显著变更（含 module/task/function 等关键字），供关系分析去抖 */
    bool hasSignificantChanges(const QString& oldContent, const QString& newContent) const;
    void invalidateCache();

signals:
    void analysisStarted(const QString& fileName);
    void analysisCompleted(const QString& fileName, int symbolsFound);
    void batchAnalysisCompleted(int filesAnalyzed, int totalSymbols);
    /** 分批进度：filesDone 已处理数，totalFiles 总数，currentFileName 当前文件（用于阶段1 进度条） */
    void batchProgress(int filesDone, int totalFiles, const QString& currentFileName);

private slots:
    void onWorkspaceAnalysisFinished();

private:
    // Analysis state tracking
    QHash<QString, QString> lastAnalyzedContent;

    // 阶段 A：后台工作区分析（不创建 QWidget，不调用 processEvents）
    QFutureWatcher<QPair<int, int>>* workspaceAnalysisWatcher = nullptr;

    // Helper methods（阶段 B：已废弃 createBackgroundEditor，改用 analyzeFileContent + 文件内容）
    QStringList filterSystemVerilogFiles(const QStringList& files) const;
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
