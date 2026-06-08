#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "relationshipprogressdialog.h"
#include "mycodeeditor.h"
#include <QMainWindow>
#include <atomic>
#include <QDockWidget>
#include <memory>

class TabManager;
class WorkspaceManager;
class ModeManager;
class SymbolAnalyzer;
class NavigationManager;
class NavigationWidget;
class AnalysisScheduler;
struct SingleFileRelationshipAnalysisResult;
struct WorkspaceRelationshipAnalysisResult;
class SemanticIndexSnapshot;
class QComboBox;
class QTreeWidget;

class SymbolRelationshipEngine;
class SlangManager;
#include "smartrelationshipbuilder.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

    friend class MyCodeEditor;

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    std::unique_ptr<TabManager> tabManager;
    std::unique_ptr<WorkspaceManager> workspaceManager;
    std::unique_ptr<ModeManager> modeManager;
    std::unique_ptr<SymbolAnalyzer> symbolAnalyzer;
    std::unique_ptr<NavigationManager> navigationManager;
    std::unique_ptr<AnalysisScheduler> analysisScheduler;

    std::unique_ptr<SymbolRelationshipEngine> relationshipEngine;
    std::unique_ptr<SlangManager> slangManager;
    std::unique_ptr<SmartRelationshipBuilder> relationshipBuilder;

    void requestSingleFileRelationshipAnalysis(const QString& fileName, const QString& content);
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

    void onRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound);
    void onRelationshipAnalysisError(const QString& fileName, const QString& error);

private:
    Ui::MainWindow *ui;
    QString currentFile;

    QDockWidget* navigationDock;
    NavigationWidget* navigationWidget;
    QDockWidget* problemsDock = nullptr;
    QTreeWidget* problemsTree = nullptr;
    QComboBox* problemsScopeCombo = nullptr;
    QComboBox* problemsSeverityCombo = nullptr;
    QTimer* problemsRefreshTimer = nullptr;
    QString pendingProblemsFileName;
    QDockWidget* referencesDock = nullptr;
    QTreeWidget* referencesTree = nullptr;
    QComboBox* referenceScopeCombo = nullptr;
    QComboBox* referenceTypeCombo = nullptr;
    QString currentReferenceSymbolName;
    QString currentReferenceFileName;
    QString currentReferenceModuleName;
    QDockWidget* relationshipsDock = nullptr;
    QTreeWidget* relationshipsTree = nullptr;
    QComboBox* relationshipViewCombo = nullptr;
    QComboBox* relationshipDirectionCombo = nullptr;
    QComboBox* relationshipTypeCombo = nullptr;
    QComboBox* relationshipDepthCombo = nullptr;
    QString currentRelationshipSymbolName;
    QString currentRelationshipFileName;
    QString currentRelationshipModuleName;

    struct RelationshipAnalysisTracker {
        int totalFiles = 0;
        int processedFiles = 0;
        bool isActive = false;
    } relationshipAnalysisTracker;

    void onSingleFileRelationshipFinished(const SingleFileRelationshipAnalysisResult& result);

    void onWorkspaceRelationshipAnalysisFinished(const WorkspaceRelationshipAnalysisResult& result);

    RelationshipProgressDialog* progressDialog = nullptr;
    void setupProgressDialog();
    void showAnalysisProgress(const QStringList& files);
    void hideAnalysisProgress();

    std::atomic<bool> symbolAnalysisCancelled{false};

    static const int kFileChangeDebounceMs = 350;

    void setupNavigationPane();
    void setupProblemsPane();
    void setupReferencesPane();
    void setupRelationshipsPane();
    void updateProblemsPanel(const QString& fileName = QString());
    void scheduleProblemsPanelUpdate(const QString& fileName = QString());
    void connectNavigationSignals();
    void navigateToFileAndLine(const QString& filePath, int lineNumber = -1, int columnNumber = -1);
    void showReferencesForSymbol(const QString& symbolName,
                                 const QString& fileName,
                                 const QString& moduleName);
    void refreshReferencesPanel();
    void showRelationshipsForSymbol(const QString& symbolName,
                                    const QString& fileName,
                                    const QString& moduleName);
    void refreshRelationshipsPanel();

    void setupManagerConnections();
    void setupRelationshipEngine();

};

#endif // MAINWINDOW_H
