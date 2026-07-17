#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QSet>
#include <QString>
#include <memory>

#include "packagetoolservice.h"

class AnalysisProgressCoordinator;
class EditorAppearanceSettings;
class FormatterSettings;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;
class NavigationCommandCoordinator;
class NavigationManager;
class NavigationPaneCoordinator;
class AnalysisCoordinator;
class AnalysisScheduler;
class ComModeCoordinator;
class EditorCoordinator;
class FileCommandCoordinator;
class FoldBlockShelfModel;
class FoldBlockShelfPanel;
class GlobalControlCoordinator;
class SemanticDockCoordinator;
class SemanticRuntimeCoordinator;
class QDockWidget;
class QLabel;
class QMenu;
class QProgressBar;
class QTabBar;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;
struct WorkspaceSessionState;
struct UserTemplateLoadReport;

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
    std::unique_ptr<NavigationManager> navigationManager;
    std::unique_ptr<AnalysisScheduler> analysisScheduler;
    std::unique_ptr<AnalysisProgressCoordinator> analysisProgressCoordinator;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::MainWindow *ui;

    std::unique_ptr<NavigationPaneCoordinator> navigationPane;
    std::unique_ptr<SemanticRuntimeCoordinator> semanticRuntime;
    std::unique_ptr<AnalysisCoordinator> analysisCoordinator;
    std::unique_ptr<ComModeCoordinator> comModeCoordinator;
    std::unique_ptr<EditorCoordinator> editorCoordinator;
    std::unique_ptr<FileCommandCoordinator> fileCommandCoordinator;
    std::unique_ptr<FoldBlockShelfModel> foldShelfModel;
    FoldBlockShelfPanel* foldShelfPanel = nullptr;
    QDockWidget* foldShelfDock = nullptr;
    std::unique_ptr<GlobalControlCoordinator> globalControlCoordinator;
    std::unique_ptr<NavigationCommandCoordinator> navigationCommandCoordinator;
    std::unique_ptr<SemanticDockCoordinator> semanticDocks;
    std::unique_ptr<EditorAppearanceSettings> editorAppearanceSettings;
    std::unique_ptr<FormatterSettings> formatterSettings;
    QDockWidget* editorAppearanceDock = nullptr;
    QDockWidget* shellNavigationRailDock = nullptr;
    QMenu* viewMenu = nullptr;
    QMenu* workspaceMenu = nullptr;
    QMenu* toolsMenu = nullptr;
    QMenu* userTemplatesMenu = nullptr;
    QToolButton* panelsStatusButton = nullptr;
    QLabel* editorModeChip = nullptr;
    QWidget* packageToolsBar = nullptr;
    QLabel* packageToolsPackageLabel = nullptr;
    QList<QToolButton*> packageToolButtons;
    QProgressBar* workspaceProgressBar = nullptr;
    QTabBar* workspaceTabBar = nullptr;
    QTimer* activeEditorPassiveRefreshTimer = nullptr;
    QTimer* workspaceSessionSaveTimer = nullptr;
    QString pendingActiveEditorPassiveRefreshFile;
    QSet<QString> workspaceSessionCleanRoots;
    bool pendingActiveEditorPassiveRefreshAll = false;

    static const int kFileChangeDebounceMs = 350;

    void setupNavigationPane();
    void setupShellNavigationRail();
    void applyModernShellStyle();
    void setupSemanticDocks();
    void setupNavigationCommandCoordinator();
    void setupFileCommandCoordinator();
    void setupGlobalControl();
    void setupComMode();
    void setupPackageTools(QVBoxLayout* editorLayout, QWidget* parent);
    void updatePackageTools();
    void insertPackageTool(PackageToolKind kind);
    void setupFoldBlockShelf();
    void setupViewMenu();
    void setupWorkspaceMenu();
    void setupToolsMenu();
    void openGlobalUserTemplates();
    void openWorkspaceUserTemplates();
    void reloadUserTemplates();
    bool openUserTemplateFile(const QString& filePath,
                              const QString& label);
    bool ensureUserTemplateJsonFile(const QString& filePath,
                                    QString* errorMessage) const;
    QString userTemplateReloadSummary(
        const UserTemplateLoadReport& report) const;
    QString userTemplateIssueReportText(
        const UserTemplateLoadReport& report) const;
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
    void showRecentWorkspacesDialog();
    void showWorkspaceConfigurationDialog();
    void navigateDiagnostic(bool previous);
    void setDiagnosticsAnalysisState(const QString& state);
    void showFoldBlockShelf();
    void restoreFoldShelfItem(const QString& id);
    void restoreFoldShelfItemToActiveEditor(const QString& id);
    void setupEditorAppearanceSettings();
    void setupEditorCoordinator();
    WorkspaceSessionState captureWorkspaceSessionState() const;
    bool saveWorkspaceSession(bool showStatus = true);
    bool restoreWorkspaceSession();
    void cleanWorkspaceSession();
    void scheduleWorkspaceSessionSave();
    void noteWorkspaceSessionAvailability();

    void setupManagerConnections();
    void setupSemanticRuntime();
    void setupWorkspaceBar();
    void setupWorkspaceProgressIndicator();
    void refreshWorkspaceTabs();
    void closeWorkspaceTab(int index);
    void showWorkspaceTabContextMenu(const QPoint& position);
    void renameWorkspaceTab(int index);
    void refreshActiveEditorDiagnosticHighlights(
        const QString& changedFileName = QString());
    void refreshActiveEditorSemanticDecorations(
        const QString& changedFileName = QString());
    void refreshActiveEditorGhostAnnotations(
        const QString& changedFileName = QString());
    void scheduleActiveEditorPassiveRefresh(
        const QString& changedFileName = QString());
    void runActiveEditorPassiveRefresh();
    void refreshActiveEditorWavePreview();

};

#endif // MAINWINDOW_H
