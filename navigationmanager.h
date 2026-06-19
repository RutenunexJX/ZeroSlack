#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include <QObject>
#include <QStringList>
#include <QHash>
#include <memory>
#include "modulehierarchymodel.h"
#include "symboloutlinemodel.h"
#include "syminfo.h"

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
        SymbolHierarchyView
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
    void refreshCurrentView();

    // Navigation operations
    void navigateToFile(const QString& filePath, int lineNumber = -1);
    void navigateToSymbol(const SymbolOutlineSymbolRow& row);
    void navigateToSymbol(const sym_list::SymbolInfo& symbol);
    void navigateToModule(const QString& moduleName);

    // Search and filter
    void clearSearchFilter();

    // Context operations
    void highlightCurrentFileInTree();
    void syncWithActiveEditor();

signals:
    void navigationRequested(const QString& filePath, int lineNumber);
    void symbolRowNavigationRequested(const SymbolOutlineSymbolRow& row);
    void symbolNavigationRequested(const sym_list::SymbolInfo& symbol);
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

        void reserveDefaults();
        void clearFileList();
        void clearModuleHierarchy();
        void clearSymbolOutline();
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
    void updateFileHierarchyData();
    void updateModuleHierarchyData();
    void updateSymbolHierarchyData();
    bool shouldRefreshCache() const;
    QStringList getSystemVerilogFiles() const;
    QStringList filterFiles(const QStringList& files, const QString& filter) const;
};

#endif // NAVIGATIONMANAGER_H
