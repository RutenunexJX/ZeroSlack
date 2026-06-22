#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <memory>

class AnalysisProgressCoordinator;
class EditorAppearanceSettings;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;
class ModeManager;
class NavigationCommandCoordinator;
class NavigationManager;
class NavigationPaneCoordinator;
class AnalysisCoordinator;
class AnalysisScheduler;
class EditorCoordinator;
class FileCommandCoordinator;
class FoldBlockShelfModel;
class FoldBlockShelfPanel;
class GlobalControlCoordinator;
class ModeCommandCoordinator;
class SemanticDockCoordinator;
class SemanticRuntimeCoordinator;
class QDockWidget;
class QLabel;
class QMenu;
class QToolButton;

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
    std::unique_ptr<NavigationManager> navigationManager;
    std::unique_ptr<AnalysisScheduler> analysisScheduler;
    std::unique_ptr<AnalysisProgressCoordinator> analysisProgressCoordinator;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    Ui::MainWindow *ui;

    std::unique_ptr<NavigationPaneCoordinator> navigationPane;
    std::unique_ptr<SemanticRuntimeCoordinator> semanticRuntime;
    std::unique_ptr<AnalysisCoordinator> analysisCoordinator;
    std::unique_ptr<EditorCoordinator> editorCoordinator;
    std::unique_ptr<FileCommandCoordinator> fileCommandCoordinator;
    std::unique_ptr<FoldBlockShelfModel> foldShelfModel;
    FoldBlockShelfPanel* foldShelfPanel = nullptr;
    QDockWidget* foldShelfDock = nullptr;
    std::unique_ptr<GlobalControlCoordinator> globalControlCoordinator;
    std::unique_ptr<ModeCommandCoordinator> modeCommandCoordinator;
    std::unique_ptr<NavigationCommandCoordinator> navigationCommandCoordinator;
    std::unique_ptr<SemanticDockCoordinator> semanticDocks;
    std::unique_ptr<EditorAppearanceSettings> editorAppearanceSettings;
    QDockWidget* editorAppearanceDock = nullptr;
    QMenu* viewMenu = nullptr;
    QToolButton* panelsStatusButton = nullptr;
    QLabel* editorModeChip = nullptr;

    static const int kFileChangeDebounceMs = 350;

    void setupNavigationPane();
    void setupSemanticDocks();
    void setupNavigationCommandCoordinator();
    void setupFileCommandCoordinator();
    void setupModeCommandCoordinator();
    void setupGlobalControl();
    void setupFoldBlockShelf();
    void setupViewMenu();
    void addPanelViewAction(QDockWidget* dock,
                            const QString& text,
                            const QString& objectName);
    QDockWidget* dockForPanelId(const QString& panelId) const;
    void showDockWidget(QDockWidget* dock,
                        const QString& statusMessage = QString());
    void showPanelById(const QString& panelId);
    void togglePanelById(const QString& panelId);
    void resetPanelLayout();
    void setupEditorModeChip();
    void updateEditorModeChip(const QString& message);
    void setFoldShelfModeVisualActive(bool active);
    void showFoldBlockShelf();
    void restoreFoldShelfItem(const QString& id);
    void setupEditorAppearanceSettings();
    void setupEditorCoordinator();

    void setupManagerConnections();
    void setupSemanticRuntime();
    void refreshActiveEditorDiagnosticHighlights(
        const QString& changedFileName = QString());
    void refreshActiveEditorSemanticDecorations(
        const QString& changedFileName = QString());

};

#endif // MAINWINDOW_H
