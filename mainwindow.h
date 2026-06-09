#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>

class AnalysisProgressCoordinator;
class AnalysisCommandCoordinator;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;
class ModeManager;
class SymbolAnalyzer;
class NavigationCommandCoordinator;
class NavigationManager;
class NavigationPaneCoordinator;
class AnalysisCoordinator;
class AnalysisScheduler;
class EditorCoordinator;
class FileCommandCoordinator;
class ModeCommandCoordinator;
class ProblemsPanelCoordinator;
class ReferencesPanelCoordinator;
class RelationshipsPanelCoordinator;
class SemanticPanelRefreshCoordinator;
class SemanticRuntimeCoordinator;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    std::unique_ptr<TabManager> tabManager;
    std::unique_ptr<WorkspaceManager> workspaceManager;
    std::unique_ptr<ModeManager> modeManager;
    std::unique_ptr<SymbolAnalyzer> symbolAnalyzer;
    std::unique_ptr<NavigationManager> navigationManager;
    std::unique_ptr<AnalysisScheduler> analysisScheduler;
    std::unique_ptr<AnalysisProgressCoordinator> analysisProgressCoordinator;

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

private:
    Ui::MainWindow *ui;

    std::unique_ptr<NavigationPaneCoordinator> navigationPane;
    std::unique_ptr<SemanticRuntimeCoordinator> semanticRuntime;
    std::unique_ptr<AnalysisCommandCoordinator> analysisCommandCoordinator;
    std::unique_ptr<AnalysisCoordinator> analysisCoordinator;
    std::unique_ptr<EditorCoordinator> editorCoordinator;
    std::unique_ptr<FileCommandCoordinator> fileCommandCoordinator;
    std::unique_ptr<ModeCommandCoordinator> modeCommandCoordinator;
    std::unique_ptr<NavigationCommandCoordinator> navigationCommandCoordinator;
    std::unique_ptr<ProblemsPanelCoordinator> problemsPanel;
    std::unique_ptr<ReferencesPanelCoordinator> referencesPanel;
    std::unique_ptr<RelationshipsPanelCoordinator> relationshipsPanel;
    std::unique_ptr<SemanticPanelRefreshCoordinator> semanticPanelRefresh;

    static const int kFileChangeDebounceMs = 350;

    void setupNavigationPane();
    void setupProblemsPane();
    void setupReferencesPane();
    void setupRelationshipsPane();
    void setupSemanticPanelRefreshCoordinator();
    void setupAnalysisCommandCoordinator();
    void setupNavigationCommandCoordinator();
    void setupFileCommandCoordinator();
    void setupModeCommandCoordinator();
    void setupEditorCoordinator();

    void setupManagerConnections();
    void setupSemanticRuntime();

};

#endif // MAINWINDOW_H
