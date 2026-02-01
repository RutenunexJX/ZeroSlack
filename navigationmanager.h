#ifndef NAVIGATIONMANAGER_H
#define NAVIGATIONMANAGER_H

#include <QObject>
#include <QStringList>
#include <QHash>
#include <memory>
#include "syminfo.h"

class NavigationWidget;
class TabManager;
class WorkspaceManager;
class SymbolAnalyzer;

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
    void setActiveView(NavigationView view);
    void setSearchFilter(const QString &filter);
    NavigationView getActiveView() const { return currentView; }

    // Manager connections
    void connectToTabManager(TabManager* tabManager);
    void connectToWorkspaceManager(WorkspaceManager* workspaceManager);
    void connectToSymbolAnalyzer(SymbolAnalyzer* symbolAnalyzer);

    // Data refresh operations
    void refreshFileHierarchy();
    void refreshModuleHierarchy();
    void refreshSymbolHierarchy();
    void refreshCurrentView();

    // Navigation operations
    void navigateToFile(const QString& filePath, int lineNumber = -1);
    void navigateToSymbol(const sym_list::SymbolInfo& symbol);
    void navigateToModule(const QString& moduleName);

    // Search and filter
    void clearSearchFilter();

    // Context operations
    void highlightCurrentFileInTree();
    void syncWithActiveEditor();

signals:
    void navigationRequested(const QString& filePath, int lineNumber);
    void symbolNavigationRequested(const sym_list::SymbolInfo& symbol);
    void viewChanged(NavigationView newView);
    void dataRefreshed(NavigationView view);

public slots:
    void onTabChanged(const QString& fileName);
    void onWorkspaceChanged(const QString& workspacePath);
    void onSymbolAnalysisCompleted(const QString& fileName, int symbolCount);

private slots:
    void onFileTreeDoubleClicked(const QString& filePath);
    void onSymbolTreeDoubleClicked(const sym_list::SymbolInfo& symbol);
    void onModuleTreeDoubleClicked(const QString& moduleName);

    void onViewChanged(int index);
    void onSearchFilterChanged(const QString &filter);

private:
    NavigationWidget* navigationWidget = nullptr;
    NavigationView currentView = FileHierarchyView;

    TabManager* connectedTabManager = nullptr;
    WorkspaceManager* connectedWorkspaceManager = nullptr;
    SymbolAnalyzer* connectedSymbolAnalyzer = nullptr;

    // Current state tracking
    QString currentFileName;
    QString currentWorkspacePath;
    QString searchFilter;

    // Data caches
    QStringList cachedFileList;
    QHash<QString, QStringList> moduleHierarchyCache;  // parent -> children
    QHash<sym_list::sym_type_e, QStringList> symbolsByTypeCache;

    // Helper methods
    void setupConnections();
    void updateFileHierarchyData();
    void updateModuleHierarchyData();
    void updateModuleHierarchyDataForFile(const QString& fileName);
    void updateSymbolHierarchyData();
    bool shouldRefreshCache() const;
    QStringList getSystemVerilogFiles() const;
    QStringList filterFiles(const QStringList& files, const QString& filter) const;
};

#endif // NAVIGATIONMANAGER_H
