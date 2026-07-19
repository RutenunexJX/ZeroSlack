#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include <QObject>
#include <QPoint>
#include <QStringList>
#include <QHash>
#include <memory>
#include "hierarchyservice.h"
#include "symbolpresentationservice.h"

class NavigationWidget;
class NavigationService;
class TabManager;
class WorkspaceManager;

class NavigationManager : public QObject
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

    // Manager connections
    void connectToTabManager(TabManager* tabManager);
    void connectToWorkspaceManager(WorkspaceManager* workspaceManager);

    // Data refresh operations
    void refreshFileHierarchy();
    void refreshDesignHierarchy(bool force = false);
    void warmDesignHierarchyCache();
    void refreshCurrentView();

    // Navigation operations
    void navigateToFile(const QString& filePath, int lineNumber = -1);
    void setDesignTop(const QString& moduleName);
    void clearDesignTop();

    // Context operations
    void highlightCurrentFileInTree();
    void syncWithActiveEditor();

signals:
    void navigationRequested(const QString& filePath, int lineNumber);
    void instanceNavigationRequested(
        const QString& filePath,
        int lineNumber,
        const HierarchyInstanceContext& instanceContext);
    void dataRefreshed(NavigationView view);

public slots:
    void onTabChanged(const QString& fileName);
    void onWorkspaceChanged(const QString& workspacePath);
    void onSymbolAnalysisCompleted(const QString& fileName, int symbolCount);
    void onBatchSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols);

private slots:
    void onFileTreeDoubleClicked(const QString& filePath);
    void onFileContextMenuRequested(const QString& filePath, const QPoint& globalPos);
    void onDesignNodeContextMenuRequested(const DesignHierarchyNode& node,
                                          const QPoint& globalPos);
    void onDesignNodeDoubleClicked(const DesignHierarchyNode& node);

    void onViewChanged(int index);

private:
    struct NavigationContext {
        QString currentFileName;
        QString currentWorkspacePath;
        QString searchFilter;

        void setCurrentFileName(const QString& fileName);
        void setCurrentWorkspacePath(const QString& workspacePath);
        void clearCurrentWorkspacePath();
        void setSearchFilter(const QString& filter);
    };

    struct NavigationCaches {
        QStringList fileList;
        DesignHierarchyReport designHierarchy;
        QString fileHierarchyFilter;
        QString designTopModule;
        QStringList designRootModules;
        QStringList designFileScope;
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
        std::uint64_t snapshotGeneration = 0;
        bool hierarchyValid = false;
        bool topInferred = true;
    };

    NavigationWidget* navigationWidget = nullptr;
    NavigationService* navigationService = nullptr;
    NavigationView currentView = FileHierarchyView;

    TabManager* connectedTabManager = nullptr;
    WorkspaceManager* connectedWorkspaceManager = nullptr;

    NavigationContext context;
    NavigationCaches caches;
    QHash<QString, DesignHierarchyCacheEntry> designHierarchyCacheByScope;

    // Helper methods
    void setupConnections();
    bool updateFileHierarchyData();
    bool updateDesignHierarchyData(bool force = false);
    bool shouldRefreshCache() const;
    QStringList getSystemVerilogFiles() const;
    QStringList filterFiles(const QStringList& files, const QString& filter) const;
    QString designHierarchyCacheKey() const;
    void saveDesignHierarchyCache();
    void restoreDesignHierarchyCache();
    void invalidateCurrentDesignHierarchyCache();
    void navigateToDesignNodeFile(const QString& filePath,
                                  int lineNumber,
                                  const DesignHierarchyNode& node);
};

#endif // NAVIGATIONMANAGER_H
