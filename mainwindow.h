#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "relationshipprogressdialog.h"
#include "mycodeeditor.h"
#include <QMainWindow>
#include <atomic>
#include <QDockWidget>
#include <memory>

// Forward declarations for managers
class TabManager;
class WorkspaceManager;
class ModeManager;
class SymbolAnalyzer;
class NavigationManager;
class NavigationWidget;

// 🚀 NEW: Relationship system (needed for RelationshipToAdd and SmartRelationshipBuilder)
class SymbolRelationshipEngine;
#include "smartrelationshipbuilder.h"
#include <QFutureWatcher>
#include <QHash>
#include <QMap>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

    // Allow MyCodeEditor to access managers
    friend class MyCodeEditor;

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Public access to managers for MyCodeEditor
    std::unique_ptr<TabManager> tabManager;
    std::unique_ptr<WorkspaceManager> workspaceManager;
    std::unique_ptr<ModeManager> modeManager;
    std::unique_ptr<SymbolAnalyzer> symbolAnalyzer;
    std::unique_ptr<NavigationManager> navigationManager;

    // 🚀 NEW: Public access to relationship system
    std::unique_ptr<SymbolRelationshipEngine> relationshipEngine;
    std::unique_ptr<SmartRelationshipBuilder> relationshipBuilder;

    /** 请求对指定文件内容执行单文件关系分析（可被编辑器去抖后调用，内部会取消未完成任务） */
    void requestSingleFileRelationshipAnalysis(const QString& fileName, const QString& content);
    /** 延后对已打开文件做符号分析（按 fileName 去抖，替代 SymbolAnalyzer 的 scheduleIncrementalAnalysis） */
    void scheduleOpenFileAnalysis(const QString& fileName, int delayMs);
    void cancelScheduledOpenFileAnalysis(const QString& fileName);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void on_new_file_triggered();
    void on_open_file_triggered();
    void on_save_file_triggered();
    void on_save_as_triggered();
    void on_copy_triggered();
    void on_paste_triggered();
    void on_cut_triggered();
    void on_undo_triggered();
    void on_redo_triggered();
    void on_open_direction_as_workspace_triggered();

    void onNavigationRequested(const QString& filePath, int lineNumber);
    void onSymbolNavigationRequested(const sym_list::SymbolInfo& symbol);

    // 🚀 NEW: Relationship engine signal handlers
    void onRelationshipAdded(int fromSymbolId, int toSymbolId, int /*SymbolRelationshipEngine::RelationType*/ type);
    void onRelationshipsCleared();
    void onRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound);
    void onRelationshipAnalysisError(const QString& fileName, const QString& error);

    void onDebug0();
private:
    Ui::MainWindow *ui;
    QString currentFile;

    QDockWidget* navigationDock;
    NavigationWidget* navigationWidget;

    struct RelationshipAnalysisTracker {
        int totalFiles = 0;
        int processedFiles = 0;
        bool isActive = false;
    } relationshipAnalysisTracker;

    // 🚀 异步关系分析：单文件与批量
    QFutureWatcher<QVector<RelationshipToAdd>>* relationshipSingleFileWatcher = nullptr;
    QString pendingRelationshipFileName;
    /** 阶段 C：上次对该文件做关系分析时的内容，用于 hasSignificantChanges 去抖 */
    QHash<QString, QString> lastRelationshipAnalysisContent;
    void onSingleFileRelationshipFinished();

    QFutureWatcher<QVector<QPair<QString, QVector<RelationshipToAdd>>>>* relationshipBatchWatcher = nullptr;
    void onBatchRelationshipFinished();

    RelationshipProgressDialog* progressDialog = nullptr;
    void setupProgressDialog();
    void showAnalysisProgress(const QStringList& files);
    void hideAnalysisProgress();

    /** 阶段1（符号分析）取消标志，由进度对话框取消按钮设置；atomic 供后台线程安全读取 */
    std::atomic<bool> symbolAnalysisCancelled{false};

    /** fileChanged 防抖：保存时 QFileSystemWatcher 常会触发两次，短时间内的重复只分析一次 */
    QMap<QString, QTimer*> fileChangeDebounceTimers;
    static const int kFileChangeDebounceMs = 350;

    /** 已打开标签的延后符号分析（按 fileName 去抖，超时后从 TabManager 取内容调用 analyzeFileContent） */
    QMap<QString, QTimer*> openFileAnalysisTimers;

    /** 推迟 relationshipAdded 后的导航刷新，避免主线程在符号分析持写锁时读 sym_list 阻塞 */
    QTimer* relationshipRefreshDeferTimer = nullptr;

    void setupNavigationPane();
    void connectNavigationSignals();
    void navigateToFileAndLine(const QString& filePath, int lineNumber = -1);

    void setupManagerConnections();

    // 🚀 NEW: Relationship system setup
    void setupRelationshipEngine();

    QPushButton* debugButton;
    void setupDebugButton();
};

#endif // MAINWINDOW_H
