#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "zeroslackexport.h"

#include <QMainWindow>
#include "actionregistry.h"
#include "annotationlayer.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>

#include "packagetoolservice.h"

class AnalysisProgressCoordinator;
class EditorAppearanceSettings;
class FormatterSettings;
class SettingsCenterPanel;
class SettingsCenterService;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;
class WorkspaceSessionCoordinator;
class RtlActionCoordinator;
class NavigationCommandCoordinator;
class NavigationManager;
class NavigationPaneCoordinator;
class NotificationCenter;
class PanelLayoutController;
class AnalysisCoordinator;
class AnalysisScheduler;
class CommandLayerCoordinator;
class EditorCoordinator;
class EditorActionContextService;
class InsightFocusController;
class FileCommandCoordinator;
class FoldBlockShelfModel;
class FoldBlockShelfPanel;
class GlobalControlCoordinator;
class SemanticDockCoordinator;
class SemanticRuntimeCoordinator;
class TemporaryEditorDrawerController;
class TemporaryEditorSearchProvider;
class ScopedReplaceWorkflow;
class WorkspaceEditDocumentManager;
class WaveSimulationCoordinator;
class WaveSimulationResultNavigationCoordinator;
class WaveEmbeddedWorkspaceLoader;
class QAction;
class QDialog;
class QDockWidget;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;
class QWidget;
struct CrashRecoveryCandidate;
struct ExternalDocumentConflictReview;
struct DocumentChange;
struct EditorActionContext;
struct EditorActionContextQuery;
struct EditorSemanticContext;
struct EditorModeSnapshot;
struct UserTemplateLoadReport;
struct SemanticAnalysisTelemetry;
struct SettingsCenterSnapshot;
struct ScopedSearchPanelContext;
struct WaveSimulationObservationRequest;
struct WaveSimulationObservationScopeRequest;

struct EditorActionContextChipWriteCounts {
    std::uint64_t text = 0;
    std::uint64_t toolTip = 0;
    std::uint64_t accessibleDescription = 0;
    std::uint64_t semanticStateProperty = 0;
    std::uint64_t hierarchyBoundProperty = 0;
    std::uint64_t styleSheet = 0;
    std::uint64_t visible = 0;

    std::uint64_t total() const
    {
        return text + toolTip + accessibleDescription
            + semanticStateProperty + hierarchyBoundProperty
            + styleSheet + visible;
    }
};

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
    EditorActionContextChipWriteCounts
    editorActionContextChipWriteCountsForTesting() const;
    void resetEditorActionContextChipWriteCountsForTesting();
    NotificationCenter* notificationCenterForTesting() const;

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
    std::unique_ptr<EditorCoordinator> editorCoordinator;
    std::unique_ptr<TemporaryEditorSearchProvider>
        temporaryEditorSearchProvider;
    std::unique_ptr<TemporaryEditorDrawerController>
        temporaryEditorDrawerController;
    std::unique_ptr<EditorActionContextService> editorActionContextService;
    std::unique_ptr<InsightFocusController> insightFocusController;
    std::unique_ptr<FileCommandCoordinator> fileCommandCoordinator;
    std::unique_ptr<WorkspaceSessionCoordinator>
        workspaceSessionCoordinator;
    std::unique_ptr<FoldBlockShelfModel> foldShelfModel;
    FoldBlockShelfPanel* foldShelfPanel = nullptr;
    QDockWidget* foldShelfDock = nullptr;
    std::unique_ptr<GlobalControlCoordinator> globalControlCoordinator;
    std::unique_ptr<NavigationCommandCoordinator> navigationCommandCoordinator;
    std::unique_ptr<SemanticDockCoordinator> semanticDocks;
    std::unique_ptr<RtlActionCoordinator> rtlActionCoordinator;
    std::unique_ptr<WorkspaceEditDocumentManager>
        scopedReplaceDocuments;
    std::unique_ptr<WaveSimulationCoordinator>
        waveSimulationCoordinator;
    std::unique_ptr<WaveSimulationResultNavigationCoordinator>
        waveSimulationResultNavigationCoordinator;
    std::unique_ptr<WaveEmbeddedWorkspaceLoader>
        waveEmbeddedWorkspaceLoader;
    std::unique_ptr<ScopedReplaceWorkflow>
        scopedReplaceWorkflow;
    std::unique_ptr<SettingsCenterService> settingsCenterService;
    std::unique_ptr<EditorAppearanceSettings> editorAppearanceSettings;
    std::unique_ptr<FormatterSettings> formatterSettings;
    SettingsCenterPanel* settingsCenterPanel = nullptr;
    QDockWidget* settingsCenterDock = nullptr;
    QMenu* viewMenu = nullptr;
    QMenu* workspaceMenu = nullptr;
    QMenu* openWorkspacesMenu = nullptr;
    QAction* closeActiveWorkspaceAction = nullptr;
    QMenu* toolsMenu = nullptr;
    QMenu* userTemplatesMenu = nullptr;
    QDialog* crashRecoveryReviewDialog = nullptr;
    QTreeWidget* crashRecoveryCandidateList = nullptr;
    QPlainTextEdit* crashRecoverySourceText = nullptr;
    QPlainTextEdit* crashRecoveryRecoveredText = nullptr;
    QLabel* crashRecoveryReviewStatus = nullptr;
    QPushButton* crashRecoveryRestoreButton = nullptr;
    QPushButton* crashRecoveryDiscardButton = nullptr;
    std::unique_ptr<CrashRecoveryCandidate>
        reviewedCrashRecoveryCandidate;
    QHash<QString, QString>
        crashRecoveryNotificationWorkspaces;
    QHash<QString, QString>
        externalConflictNotificationFiles;
    QHash<QString, QVariantMap>
        waveSimulationNotificationLocations;
    QHash<QString, int>
        crashRecoveryIsolatedRecordCounts;
    QSet<QString> handledCrashRecoveryCandidates;
    QString crashRecoveryReviewWorkspace;
    QWidget* externalConflictReviewBar = nullptr;
    QLabel* externalConflictReviewTitle = nullptr;
    QLabel* externalConflictReviewStatus = nullptr;
    QPlainTextEdit* externalConflictLocalText = nullptr;
    QPlainTextEdit* externalConflictDiskText = nullptr;
    QPushButton* externalConflictKeepLocalButton = nullptr;
    QPushButton* externalConflictReloadButton = nullptr;
    QPushButton* externalConflictSaveAsButton = nullptr;
    std::unique_ptr<ExternalDocumentConflictReview>
        reviewedExternalConflict;
    QPointer<QWidget> externalConflictPreviousFocus;
    QToolButton* panelsStatusButton = nullptr;
    QStackedWidget* centralContentStack = nullptr;
    QWidget* editorCentralPage = nullptr;
    QWidget* editorSplitHost = nullptr;
    QLabel* editorModeChip = nullptr;
    QLabel* editorActionContextChip = nullptr;
    QWidget* packageToolsBar = nullptr;
    QLabel* packageToolsPackageLabel = nullptr;
    QList<QToolButton*> packageToolButtons;
    MyCodeEditor* packageToolsStateEditor = nullptr;
    EditorPackageToolAvailability packageToolsState;
    bool packageToolsStateValid = false;
    QProgressBar* workspaceProgressBar = nullptr;
    QString pendingActiveEditorPassiveRefreshFile;
    QString diagnosticsAnalysisState;
    bool pendingActiveEditorPassiveRefreshAll = false;
    bool activeEditorPassiveRefreshQueued = false;
    EditorAnnotationDisplayOptions
        editorAnnotationDisplayOptions;
    std::uint64_t semanticDecorationGeneration = 0;
    std::shared_ptr<std::atomic_bool> semanticDecorationCancellation;
    EditorActionContextChipWriteCounts editorActionContextChipWriteCounts;

    static const int kFileChangeDebounceMs = 350;

    void setupNavigationPane();
    void setupNotificationCenter();
    void applyModernShellStyle(bool applyApplicationTheme = true);
    void refreshThemePresentation();
    void setupSemanticDocks();
    ScopedSearchPanelContext scopedSearchContext() const;
    void setupInsightFocusView();
    bool insightPanelVisibleOrFocused(
        const QString& panelId) const;
    void setupNavigationCommandCoordinator();
    void setupFileCommandCoordinator();
    void setupGlobalControl();
    void setupCommandLayer();
    void setupPackageTools(QVBoxLayout* editorLayout, QWidget* parent);
    void updatePackageTools();
    void updatePackageToolsForEditor(
        MyCodeEditor* editor,
        const EditorPackageToolAvailability& availability);
    void insertPackageTool(PackageToolKind kind);
    void setupFoldBlockShelf();
    void setupPanelLayoutController();
    void setupViewMenu();
    void setupWorkspaceMenu();
    void refreshWorkspaceMenuEntries();
    void activateWorkspace(int index);
    void closeActiveWorkspace();
    void setupToolsMenu();
    void setupWaveSimulation();
    void openWaveSimulationResultTab(
        const QString& resultProjectPath,
        const QString& widgetLibraryPath,
        const QString& applicationPath);
    bool startWaveSimulation(
        const QString& targetFile,
        const QString& moduleName,
        const QString& instancePath,
        const WaveSimulationObservationScopeRequest& observationScope,
        const QList<WaveSimulationObservationRequest>& observations,
        QString* failureReason);
    void setupCrashRecoveryReviewUi();
    void notifyCrashRecoveryCandidates(
        const QString& workspaceRoot,
        int candidateCount,
        int isolatedRecordCount);
    void postCrashRecoveryFailure(
        const QString& documentId,
        const QString& failureReason);
    void openCrashRecoveryReview(
        const QString& workspaceRoot = QString());
    void reloadCrashRecoveryReview(
        const QString& preferredRecoveryId = QString());
    void reviewCrashRecoverySelection();
    void applyReviewedCrashRecovery();
    void discardReviewedCrashRecovery();
    void refreshCrashRecoveryAvailability(
        const QString& workspaceRoot);
    QString crashRecoveryNotificationKey(
        const QString& workspaceRoot) const;
    QString crashRecoveryHandledKey(
        const QString& workspaceRoot,
        const QString& recoveryId) const;
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
    void setupEditorModeChip();
    void setupEditorActionContextChip();
    void refreshEditorActionContextChip();
    EditorActionContextQuery editorActionContextQuery(
        const EditorSemanticContext& editorContext);
    EditorActionContext resolveEditorActionContext(
        const EditorSemanticContext& editorContext);
    void updateEditorModeChip(const EditorModeSnapshot& snapshot);
    void setFoldShelfModeVisualActive(bool active);
    void showRecentWorkspacesDialog();
    void showWorkspaceConfigurationDialog();
    void navigateDiagnostic(bool previous);
    void setDiagnosticsAnalysisState(const QString& state);
    void refreshDiagnosticsAnalysisState();
    void showFoldBlockShelf();
    void restoreFoldShelfItem(const QString& id);
    void restoreFoldShelfItemToActiveEditor(const QString& id);
    void setupSettingsCenter();
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
    void setupExternalConflictReviewUi(
        QVBoxLayout* editorLayout,
        QWidget* parent);
    void openExternalConflictReview(
        const QString& fileName);
    void closeExternalConflictReview();
    void keepReviewedExternalConflict();
    void reloadReviewedExternalConflict();
    void saveReviewedExternalConflictAs();
    void postExternalConflictActionFailure(
        const QString& fileName,
        const QString& failureReason);
    void setupWorkspaceProgressIndicator();
    void refreshWorkspaceScope();
    void refreshActiveEditorDiagnosticHighlights(
        const QString& changedFileName = QString());
    void refreshActiveEditorSemanticDecorations(
        const QString& changedFileName = QString());
    void scheduleActiveEditorPassiveRefresh(
        const QString& changedFileName = QString());
    void runActiveEditorPassiveRefresh();
    void refreshActiveEditorWavePreview();
    void applyActiveEditorWavePreviewChange(
        MyCodeEditor* editor,
        const DocumentChange& change);

};

#endif // MAINWINDOW_H
