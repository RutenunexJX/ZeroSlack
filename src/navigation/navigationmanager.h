#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H
#include "designtreeprojection.h"

#include "zeroslackexport.h"

#include <QObject>
#include <QPoint>
#include <QByteArray>
#include <QStringList>
#include <QHash>
#include <QList>
#include <functional>
#include <memory>
#include <optional>
#include <atomic>
#include <QThreadPool>
#include <QFutureWatcher>
#include "actionregistry.h"
#include "editorlocation.h"
#include "hierarchyservice.h"
#include "semanticanalysisrequest.h"
#include "symbolpresentationservice.h"
#include "designcandidates.h"

class NavigationWidget;
class NavigationService;
class TabManager;
class WorkspaceManager;
class WorkspaceFileOperationService;
class DesignProjectService;
struct WorkspaceFileOperationPlan;

struct DesignHierarchyContextAction {
    QString actionId;
    QString label;
    QString executionRoute;
    bool enabled = false;
    bool separatorBefore = false;
};

class ZEROSLACK_API NavigationManager :
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
    void connectToDesignProjects(DesignProjectService* service);
    void setSimulationHandlers(std::function<QPair<QString,QString>()> target,
                               std::function<QString(const QString&,const QString&)> select);
    void setTemporaryEditorOpenHandler(
        std::function<bool(const EditorLocation&)> handler);

    // Data refresh operations
    void refreshFileHierarchy();
    void refreshDesignHierarchy(bool force = false);
    void warmDesignHierarchyCache();
    void refreshCurrentView();
    void setSemanticAnalysisContext(
        const SemanticAnalysisTelemetry& telemetry);

    // Navigation operations
    void navigateToFile(const QString& filePath, int lineNumber = -1);
    QString setDesignTop(const QString& moduleName, const QString& definitionFile = {});
    void clearDesignTop();
    QString selectedDesignTopModule() const;
    void chooseDesignProject();
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
    void temporaryEditorOpenRequested(
        const EditorLocation& location);
    void temporaryEditorOpenFinished(
        const EditorLocation& location,
        bool succeeded,
        const QString& failureReason);
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
    std::function<QPair<QString,QString>()> simulationTarget;
    std::function<QString(const QString&,const QString&)> selectSimulationTop;
    bool eventFilter(QObject* watched, QEvent* event) override;
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
        std::shared_ptr<const DesignTreeProjection> projection;
        QPair<QString,QString> sourceTop, simulationTop;
        QString fileHierarchyFilter;
        QList<DesignHierarchyRoot> designRoots;
        QString activeProjectId;
        QStringList designFileScope;
        QByteArray designStructureFingerprint;
        QByteArray designAnchorFingerprint;
        std::uint64_t designSnapshotGeneration = 0;
        bool fileListValid = false;
        bool fileHierarchyValid = false;
        bool designHierarchyValid = false;

        void clearFileList();
        void clearDesignHierarchy();
    };

    struct DesignHierarchyCacheEntry {
        DesignHierarchyReport hierarchy;
        QList<DesignHierarchyRoot> roots;
        QString activeProjectId;
        QStringList fileScope;
        QByteArray structureFingerprint;
        QByteArray anchorFingerprint;
        std::uint64_t snapshotGeneration = 0;
        bool hierarchyValid = false;
    };

    NavigationWidget* navigationWidget = nullptr;
    NavigationService* navigationService = nullptr;
    NavigationView currentView = FileHierarchyView;

    TabManager* connectedTabManager = nullptr;
    WorkspaceManager* connectedWorkspaceManager = nullptr;
    DesignProjectService* designProjects = nullptr;
    QMetaObject::Connection workspaceDiscardConnection;
    std::unique_ptr<WorkspaceFileOperationService>
        fileOperationService;
    std::function<bool(const EditorLocation&)>
        temporaryEditorOpenHandler;

    NavigationContext context;
    NavigationCaches caches;
    QHash<QString, DesignHierarchyCacheEntry> designHierarchyCacheByScope;
    QStringList designCacheLru;
    static constexpr int maximumDesignCaches = 3;
    static constexpr qsizetype maximumRetainedDesignNodes = 100000;
    bool designHierarchyWidgetValid = false;
    SemanticAnalysisTelemetry semanticAnalysisContext;
    struct DesignWorkRequest {
        SemanticSnapshotToken token;
        ProjectSnapshot project;
        QPair<QString,QString> sourceTop, simulationTop;
        QStringList files;
        QStringList candidateFiles;
        QString selectedTop;
        QList<DesignHierarchyRoot> roots;
        QString workspace;
        SemanticAnalysisTelemetry telemetry;
        std::uint64_t generation = 0;
        DesignHierarchyReport previousHierarchy;
        QByteArray previousFingerprint;
        QByteArray previousAnchorFingerprint;
        bool force = false;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    struct DesignWorkResult {
        DesignHierarchyReport hierarchy;
        std::shared_ptr<const DesignTreeProjection> projection;
        QByteArray fingerprint;
        QByteArray anchorFingerprint;
        bool reusedStructure = false;
        QList<DesignFileCandidate> candidates;
        QStringList likelyTops;
        qint64 workerMs = 0;
        bool cancelled = false;
    };
    QThreadPool designThreadPool;
    QFutureWatcher<DesignWorkResult>* designWatcher = nullptr;
    std::optional<DesignWorkRequest> pendingDesignRequest;
    std::optional<DesignWorkRequest> activeDesignRequest;
    std::uint64_t designRequestGeneration = 0;
    bool designShuttingDown = false;
    QString pendingNavigationNode;
    bool pendingNavigationInstantiation = false;
    void launchPendingDesignWork();
    QList<DesignHierarchyRoot> currentDesignRoots() const;
    void synchronizeDesignProjects();

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
