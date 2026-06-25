#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include <QObject>
#include <QPoint>
#include <QStringList>
#include <QHash>
#include <memory>
#include "hierarchyservice.h"
#include "modulehierarchymodel.h"
#include "symboloutlinemodel.h"

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
        ModuleHierarchyView,
        SymbolHierarchyView,
        DesignHierarchyView
    };

    explicit NavigationManager(QObject *parent = nullptr);
    ~NavigationManager();

    // Core navigation operations
    void setNavigationWidget(NavigationWidget* widget);
    void setNavigationService(NavigationService* service);
    void setActiveView(NavigationView view);
    void setSearchFilter(const QString &filter);
    NavigationView getActiveView() const { return currentView; }

    // Manager connections
    void connectToTabManager(TabManager* tabManager);
    void connectToWorkspaceManager(WorkspaceManager* workspaceManager);

    // Data refresh operations
    void refreshFileHierarchy();
    void refreshModuleHierarchy();
    void refreshSymbolHierarchy();
    void refreshDesignHierarchy(bool force = false);
    void refreshCurrentView();

    // Navigation operations
    void navigateToFile(const QString& filePath, int lineNumber = -1);
    void navigateToSymbol(const SymbolOutlineSymbolRow& row);
    void navigateToModule(const QString& moduleName);
    void setDesignTop(const QString& moduleName);
    void clearDesignTop();

    // Search and filter
    void clearSearchFilter();

    // Context operations
    void highlightCurrentFileInTree();
    void syncWithActiveEditor();

signals:
    void navigationRequested(const QString& filePath, int lineNumber);
    void symbolRowNavigationRequested(const SymbolOutlineSymbolRow& row);
    void viewChanged(NavigationView newView);
    void dataRefreshed(NavigationView view);

public slots:
    void onTabChanged(const QString& fileName);
    void onWorkspaceChanged(const QString& workspacePath);
    void onSymbolAnalysisCompleted(const QString& fileName, int symbolCount);
    void onBatchSymbolAnalysisCompleted(int filesAnalyzed, int totalSymbols);

private slots:
    void onFileTreeDoubleClicked(const QString& filePath);
    void onSymbolRowTreeDoubleClicked(const SymbolOutlineSymbolRow& row);
    void onModuleTreeDoubleClicked(const QString& moduleName);
    void onFileContextMenuRequested(const QString& filePath, const QPoint& globalPos);
    void onModuleContextMenuRequested(const QString& moduleName, const QPoint& globalPos);
    void onDesignNodeContextMenuRequested(const DesignHierarchyNode& node,
                                          const QPoint& globalPos);
    void onDesignNodeDoubleClicked(const DesignHierarchyNode& node);

    void onViewChanged(int index);
    void onSearchFilterChanged(const QString &filter);

private:
    struct NavigationContext {
        QString currentFileName;
        QString currentWorkspacePath;
        QString searchFilter;

        void setCurrentFileName(const QString& fileName);
        void clearCurrentFileName();
        void setCurrentWorkspacePath(const QString& workspacePath);
        void clearCurrentWorkspacePath();
        void setSearchFilter(const QString& filter);
        void clearSearchFilter();
    };

    struct NavigationCaches {
        QStringList fileList;
        QList<ModuleHierarchyGroup> moduleHierarchy;
        QList<SymbolOutlineGroup> symbolOutline;
        DesignHierarchyReport designHierarchy;
        QString fileHierarchyFilter;
        QString moduleHierarchyFilter;
        QString symbolOutlineFileName;
        QString symbolOutlineFilter;
        QString designTopModule;
        std::uint64_t designSnapshotGeneration = 0;
        bool fileListValid = false;
        bool fileHierarchyValid = false;
        bool moduleHierarchyValid = false;
        bool symbolOutlineValid = false;
        bool designHierarchyValid = false;
        bool designTopInferred = true;

        void reserveDefaults();
        void clearFileList();
        void clearModuleHierarchy();
        void clearSymbolOutline();
        void clearDesignHierarchy();
        void clearAll();
    };

    NavigationWidget* navigationWidget = nullptr;
    NavigationService* navigationService = nullptr;
    NavigationView currentView = FileHierarchyView;

    TabManager* connectedTabManager = nullptr;
    WorkspaceManager* connectedWorkspaceManager = nullptr;

    NavigationContext context;
    NavigationCaches caches;

    // Helper methods
    void setupConnections();
    bool updateFileHierarchyData();
    bool updateModuleHierarchyData();
    bool updateSymbolHierarchyData();
    bool updateDesignHierarchyData(bool force = false);
    bool shouldRefreshCache() const;
    QStringList getSystemVerilogFiles() const;
    QStringList filterFiles(const QStringList& files, const QString& filter) const;
};

#endif // NAVIGATIONMANAGER_H
