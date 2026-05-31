#ifndef SYMBOLANALYZER_H
#define SYMBOLANALYZER_H

#include <QObject>
#include <QStringList>
#include <QFutureWatcher>
#include <QList>
#include <QVector>
#include <functional>
#include "syminfo.h"

class SlangManager;
class TabManager;
class WorkspaceManager;

struct WorkspaceFileAnalysis {
    QString fileName;
    QString content;
    QList<sym_list::SymbolInfo> symbols;
};

struct WorkspaceAnalysisResult {
    QVector<WorkspaceFileAnalysis> files;
    int totalSymbols = 0;
};

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
    /** 交互路径：Slang 重解析在后台线程进行，结果在主线程写回 sym_list，避免编辑时 UI 冻结。 */
    void analyzeFileContentAsync(const QString& fileName, const QString& content);

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

    SlangManager* m_slangManager = nullptr;
    // 阶段 A：后台工作区分析，返回每个文件的符号和内容，主线程写回 sym_list 并初始化增量状态。
    QFutureWatcher<WorkspaceAnalysisResult>* workspaceAnalysisWatcher = nullptr;

    QStringList filterSystemVerilogFiles(const QStringList& files) const;
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
