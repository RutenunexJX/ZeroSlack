#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include <QObject>
#include <QPoint>
#include <QByteArray>
#include <QStringList>
#include <QHash>
#include <QList>
#include <memory>
#include "actionregistry.h"
#include "hierarchyservice.h"
#include "semanticanalysisrequest.h"
#include "symbolpresentationservice.h"

class NavigationWidget;
class NavigationService;
class TabManager;
class WorkspaceManager;
class WorkspaceFileOperationService;
struct WorkspaceFileOperationPlan;

struct DesignHierarchyContextAction {
    QString actionId;
    QString label;
    QString executionRoute;
    bool enabled = false;
    bool separatorBefore = false;
};

class NavigationManager :
    public QObject,
    public ActionExecutionHost
{
    Q_OBJECT

public:
    enum NavigationView {
        FileHierarchyView,
        DesignHierarchyView
    };

    explicit NavigationManager(QObject *parent = nullptr);
    ~NavigationManager();

    // Core navigation operations
    void setNavigationWidget(NavigationWidget* widget);
    void setNavigationService(NavigationService* service);
    void setActiveView(NavigationView view);
    void setSearchFilter(const QString &filter);
    void setSearchFilter(NavigationView view, const QString& filter);

    // Manager connections
    void connectToTabManager(TabManager* tabManager);
    void connectToWorkspaceManager(WorkspaceManager* workspaceManager);

    // Data refresh operations
    void refreshFileHierarchy();
    void refreshDesignHierarchy(bool force = false);
    void warmDesignHierarchyCache();
    void refreshCurrentView();
    void setSemanticAnalysisContext(
        const SemanticAnalysisTelemetry& telemetry);

    // Navigation operations
    void navigateToFile(const QString& filePath, int lineNumber = -1);
    void setDesignTop(const QString& moduleName);
    void clearDesignTop();
    QString selectedDesignTopModule() const;
    QList<DesignHierarchyContextAction>
    designNodeContextActions(
        const DesignHierarchyNode& node) const;
    ActionExecutionResult requestDesignNodeAction(
        const QString& actionId,
        const DesignHierarchyNode& node);

    // Context operations
    void highlightCurrentFileInTree();
    void syncWithActiveEditor();
    WorkspaceFileOperationService*
    fileOperationServiceForTesting() const;
    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override;

signals:
    void navigationRequested(const QString& filePath, int lineNumber);
    void instanceNavigationRequested(
        const QString& filePath,
        int lineNumber,
        const HierarchyInstanceContext& instanceContext);
    void dataRefreshed(NavigationView view);
    void navigationTelemetry(const SemanticAnalysisTelemetry& telemetry);
    void workspaceFileOperationCompleted(
        const QString& actionId,
        const QString& path);
    void workspaceFileOperationFailed(
        const QString& actionId,
        const QString& path,
        const QString& failureReason);

public slots:
    void onTabChanged(const QString& fileName);
    void onWorkspaceChanged(const QString& workspacePath);
    void onSymbolAnalysisCompleted(const QString& fileName, int symbolCount);
    void onBatchSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols);

private slots:
    void onFileTreeDoubleClicked(const QString& filePath);
    void onFileContextMenuRequested(const QString& filePath, const QPoint& globalPos);
    void onFileTreeNodeContextMenuRequested(
        const QString& path,
        bool directory,
        const QPoint& globalPos);
    void onDesignNodeContextMenuRequested(const DesignHierarchyNode& node,
                                          const QPoint& globalPos);
    void onDesignNodeDoubleClicked(const DesignHierarchyNode& node);

    void onViewChanged(int index);

private:
    struct NavigationContext {
        QString currentFileName;
        QString currentWorkspacePath;
        QString fileSearchFilter;
        QString designSearchFilter;

        void setCurrentFileName(const QString& fileName);
        void setCurrentWorkspacePath(const QString& workspacePath);
        void clearCurrentWorkspacePath();
        void setSearchFilter(NavigationView view, const QString& filter);
        QString searchFilter(NavigationView view) const;
    };

    struct NavigationCaches {
        QStringList fileList;
        DesignHierarchyReport designHierarchy;
        QString fileHierarchyFilter;
        QString designTopModule;
        QStringList designRootModules;
        QStringList designFileScope;
        QByteArray designStructureFingerprint;
        std::uint64_t designSnapshotGeneration = 0;
        bool fileListValid = false;
        bool fileHierarchyValid = false;
        bool designHierarchyValid = false;
        bool designTopInferred = true;

        void clearFileList();
        void clearDesignHierarchy();
    };

    struct DesignHierarchyCacheEntry {
        DesignHierarchyReport hierarchy;
        QString topModule;
        QStringList rootModules;
        QStringList fileScope;
        QByteArray structureFingerprint;
        std::uint64_t snapshotGeneration = 0;
        bool hierarchyValid = false;
        bool topInferred = true;
    };

    NavigationWidget* navigationWidget = nullptr;
    NavigationService* navigationService = nullptr;
    NavigationView currentView = FileHierarchyView;

    TabManager* connectedTabManager = nullptr;
    WorkspaceManager* connectedWorkspaceManager = nullptr;
    std::unique_ptr<WorkspaceFileOperationService>
        fileOperationService;

    NavigationContext context;
    NavigationCaches caches;
    QHash<QString, DesignHierarchyCacheEntry> designHierarchyCacheByScope;
    bool designHierarchyWidgetValid = false;
    SemanticAnalysisTelemetry semanticAnalysisContext;

    // Helper methods
    void setupConnections();
    bool updateFileHierarchyData();
    bool updateDesignHierarchyData(bool force = false);
    bool shouldRefreshCache() const;
    QStringList getSystemVerilogFiles() const;
    QStringList getFileHierarchyFiles() const;
    QString designHierarchyCacheKey() const;
    void saveDesignHierarchyCache();
    void restoreDesignHierarchyCache();
    void invalidateCurrentDesignHierarchyCache();
    void navigateToDesignNodeFile(const QString& filePath,
                                  int lineNumber,
                                  const DesignHierarchyNode& node);
    void executeFileTreeAction(
        const QString& actionId,
        const QString& path,
        bool directory);
    void refreshAfterFileOperation(
        const WorkspaceFileOperationPlan& plan);
    void updateFileCacheAfterOperation(
        const WorkspaceFileOperationPlan& plan);
};

#endif // NAVIGATIONMANAGER_H
