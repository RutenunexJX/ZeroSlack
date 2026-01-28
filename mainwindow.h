#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "relationshipprogressdialog.h"
#include "mycodeeditor.h"
#include <QMainWindow>
#include <QDockWidget>
#include <memory>

// Forward declarations for managers
class TabManager;
class WorkspaceManager;
class ModeManager;
class SymbolAnalyzer;
class NavigationManager;
class NavigationWidget;

// 🚀 NEW: Forward declarations for relationship system
class SymbolRelationshipEngine;
class SmartRelationshipBuilder;

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

    void onDebugPrintSymbolIds();


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


    RelationshipProgressDialog* progressDialog = nullptr;
    void setupProgressDialog();
    void showAnalysisProgress(const QStringList& files);
    void hideAnalysisProgress();

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
