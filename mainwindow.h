#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "syminfo.h"
#include <QMainWindow>
#include <memory>

class AnalysisProgressCoordinator;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;
class ModeManager;
class SymbolAnalyzer;
class NavigationManager;
class NavigationPaneCoordinator;
class AnalysisScheduler;
class EditorCoordinator;
class ProblemsPanelCoordinator;
class ReferencesPanelCoordinator;
class RelationshipsPanelCoordinator;
struct SingleFileRelationshipAnalysisResult;

class SymbolRelationshipEngine;
class SlangManager;
class SmartRelationshipBuilder;

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

    std::unique_ptr<NavigationPaneCoordinator> navigationPane;
    std::unique_ptr<EditorCoordinator> editorCoordinator;
    std::unique_ptr<ProblemsPanelCoordinator> problemsPanel;
    std::unique_ptr<ReferencesPanelCoordinator> referencesPanel;
    std::unique_ptr<RelationshipsPanelCoordinator> relationshipsPanel;

    void onSingleFileRelationshipFinished(const SingleFileRelationshipAnalysisResult& result);

    static const int kFileChangeDebounceMs = 350;

    void setupNavigationPane();
    void setupProblemsPane();
    void setupReferencesPane();
    void setupRelationshipsPane();
    void setupEditorCoordinator();
    void updateProblemsPanel(const QString& fileName = QString());
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
