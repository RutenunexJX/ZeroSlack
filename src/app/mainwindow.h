#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "zeroslackexport.h"

#include <QMainWindow>
#include "actionregistry.h"
#include "annotationlayer.h"
#include "liveinsightscontextview.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>


class AnalysisProgressCoordinator;
class EditorAppearanceSettings;
class SettingsCenterPanel;
class SettingsCenterService;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;
class WorkspaceSessionCoordinator;
class WorkspaceSwitcher;
class RtlActionCoordinator;
class NavigationCommandCoordinator;
class NavigationManager;
class NavigationPaneCoordinator;
class NotificationCenter;
class PanelLayoutController;
class AnalysisCoordinator;
class AnalysisScheduler;
class CommandLayerCoordinator;
class ContextWorkspaceController;
class LiveInsightSession;
class LiveInsightToolPage;
class PinloomCodeLinkCoordinator;
class PinloomHostClient;
class EditorCoordinator;
class EditorActionContextService;
class FileCommandCoordinator;
class DocumentReviewCoordinator;
class GlobalControlCoordinator;
class SemanticDockCoordinator;
class SemanticRuntimeCoordinator;
class TemporaryEditorSearchProvider;
class ScopedReplaceWorkflow;
class WorkspaceEditDocumentManager;
enum class LiveInsightKind : quint8;
class QAction;
class QDialog;
class QDockWidget;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QTabWidget;
class QTreeWidget;
class QVBoxLayout;
class QWidget;
struct CrashRecoveryCandidate;
struct ExternalDocumentConflictReview;
struct DocumentChange;
struct EditorActionContext;
struct EditorActionContextQuery;
struct EditorSemanticContext;
struct UserTemplateLoadReport;
struct SemanticAnalysisTelemetry;
struct SettingsCenterSnapshot;
struct ScopedSearchPanelContext;
enum class ScopedSearchScope;
struct ContextResource;
struct LiveInsightToolContext;


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class ZEROSLACK_API MainWindow : public QMainWindow,
                   private ActionExecutionHost
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
    NotificationCenter* notificationCenterForTesting() const;
    bool revealSuiteSource(const QString& filePath,
                           int lineNumber = 1,
                           int columnNumber = 1);

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void semanticUiRefreshTelemetry(
        const SemanticAnalysisTelemetry& telemetry);

private:
    Ui::MainWindow *ui;

    std::unique_ptr<NavigationPaneCoordinator> navigationPane;
    std::unique_ptr<NotificationCenter> notificationCenter;
    std::unique_ptr<PanelLayoutController> panelLayoutController;
    std::unique_ptr<SemanticRuntimeCoordinator> semanticRuntime;
    std::unique_ptr<AnalysisCoordinator> analysisCoordinator;
    std::unique_ptr<CommandLayerCoordinator> commandLayerCoordinator;
    std::unique_ptr<ContextWorkspaceController>
        contextWorkspaceController;
    std::unique_ptr<LiveInsightSession> liveInsightSession;
    QHash<int, QPointer<LiveInsightToolPage>>
        liveInsightToolPages;
    std::unique_ptr<PinloomCodeLinkCoordinator>
        pinloomCodeLinkCoordinator;
    std::unique_ptr<PinloomHostClient> pinloomHostClient;
    std::unique_ptr<EditorCoordinator> editorCoordinator;
    std::unique_ptr<TemporaryEditorSearchProvider>
        temporaryEditorSearchProvider;
    std::uint64_t temporaryEditorCatalogSnapshotRevision = 0;
    std::unique_ptr<EditorActionContextService> editorActionContextService;
    std::unique_ptr<FileCommandCoordinator> fileCommandCoordinator;
    std::unique_ptr<WorkspaceSessionCoordinator>
        workspaceSessionCoordinator;
    std::unique_ptr<WorkspaceSwitcher> workspaceSwitcher;
    std::unique_ptr<GlobalControlCoordinator> globalControlCoordinator;
    std::unique_ptr<NavigationCommandCoordinator> navigationCommandCoordinator;
    std::unique_ptr<SemanticDockCoordinator> semanticDocks;
    std::unique_ptr<RtlActionCoordinator> rtlActionCoordinator;
    std::unique_ptr<WorkspaceEditDocumentManager>
        scopedReplaceDocuments;
    std::unique_ptr<ScopedReplaceWorkflow>
        scopedReplaceWorkflow;
    std::unique_ptr<SettingsCenterService> settingsCenterService;
    std::unique_ptr<EditorAppearanceSettings> editorAppearanceSettings;
    SettingsCenterPanel* settingsCenterPanel = nullptr;
    QDockWidget* settingsCenterDock = nullptr;
    QMenu* viewMenu = nullptr;
    QMenu* workspaceMenu = nullptr;
    QAction* closeActiveWorkspaceAction = nullptr;
    QMenu* toolsMenu = nullptr;
    QMenu* userTemplatesMenu = nullptr;
    std::unique_ptr<DocumentReviewCoordinator> documentReviewCoordinator;
    QStackedWidget* centralContentStack = nullptr;
    QWidget* editorCentralPage = nullptr;
    QWidget* editorSplitHost = nullptr;
    QPointer<QTabWidget> initialEditorTabs;
    QWidget* welcomePage = nullptr;
    QVBoxLayout* recentProjectsLayout = nullptr;
    bool navigationHiddenForWelcome = false;
    void setupWelcomePage();
    void refreshWelcomePage();
    QString pendingActiveEditorPassiveRefreshFile;
    QString diagnosticsAnalysisState;
    bool pendingActiveEditorPassiveRefreshAll = false;
    bool activeEditorPassiveRefreshQueued = false;
    EditorAnnotationDisplayOptions
        editorAnnotationDisplayOptions;
    std::uint64_t semanticDecorationGeneration = 0;
    std::shared_ptr<std::atomic_bool> semanticDecorationCancellation;

    static const int kFileChangeDebounceMs = 350;

    void setupNavigationPane();
    void setupNotificationCenter();
    void postActivityMessage(const QString& message, int timeoutMs = 0);
    void applyModernShellStyle(bool applyApplicationTheme = true);
    void refreshThemePresentation();
    void setupSemanticDocks();
    ScopedSearchPanelContext scopedSearchContext(ScopedSearchScope scope) const;
    void setupNavigationCommandCoordinator();
    void setupFileCommandCoordinator();
    void setupGlobalControl();
    void setupCommandLayer();
    void setupPanelLayoutController();
    void setupContextWorkspace();
    void requestLiveInsightUpdates();
    void refreshLiveInsightToolPages(int kindValue);
    void openLiveInsightFullView(
        const ContextResource& resource,
        const LiveInsightToolContext* contextOverride = nullptr);
    bool openLiveInsightFromSourceAction(
        LiveInsightKind kind,
        const QString& symbolName,
        const QString& fileName,
        const QString& moduleName,
        const QString& signalAccessPath);
    LiveInsightToolContext activeLiveInsightToolContext() const;
    LiveInsightToolContext activeLiveInsightToolContext(bool includeDocumentText) const;
    // Runs the editor-side target picker for one insight kind and hands the
    // chosen target back to the section that asked.
    bool beginLiveInsightTargetPick(
        LiveInsightKind kind,
        LiveInsightsContextView* sourceView,
        LiveInsightSession* graphSession,
        std::function<void(const LiveInsightsContextView::TargetCandidate&)>
            picked);
    void setupViewMenu();
    void setupWorkspaceMenu();
    void refreshWorkspaceActions();
    void activateWorkspace(int index);
    void closeActiveWorkspace();
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
    QDockWidget* dockForPanelId(const QString& panelId) const;
    void showDockWidget(QDockWidget* dock,
                        const QString& statusMessage = QString());
    void showPanelById(const QString& panelId);
    void resetPanelLayout();
    EditorActionContextQuery editorActionContextQuery(
        const EditorSemanticContext& editorContext);
    EditorActionContext resolveEditorActionContext(
        const EditorSemanticContext& editorContext);
    void showRecentWorkspacesDialog();
    void showWorkspaceConfigurationDialog();
    void navigateDiagnostic(bool previous);
    void setDiagnosticsAnalysisState(const QString& state);
    void refreshDiagnosticsAnalysisState();
    void setupSettingsCenter();
    void ensureSettingsCenterPanel();
    void applySettingsCenterSnapshot(
        const SettingsCenterSnapshot& snapshot);
    void applyRegisteredActionShortcuts();
    QAction* addRegistryMenuAction(
        QMenu* menu,
        const QString& actionId);
    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override;
    ActionExecutionResult executeRegisteredUiAction(
        const QString& actionId,
        const QVariantMap& parameters);
    void refreshSettingsCenterWorkspace(
        const QString& workspaceRoot);
    void setupEditorCoordinator();
    void setupRtlActionCoordinator();
    void setupWorkspaceSessionCoordinator();

    void setupManagerConnections();
    void setupSemanticRuntime();
    void setupEditorCentralArea();
    void refreshTemporaryEditorFileCatalog();
    void refreshTemporaryEditorSemanticCatalog();
    void setupWorkspaceActivity();
    void refreshWorkspaceScope();
    void refreshActiveEditorDiagnosticHighlights(
        const QString& changedFileName = QString());
    void refreshActiveEditorSemanticDecorations(
        const QString& changedFileName = QString());
    void scheduleActiveEditorPassiveRefresh(
        const QString& changedFileName = QString());
    void runActiveEditorPassiveRefresh();

};

#endif // MAINWINDOW_H
