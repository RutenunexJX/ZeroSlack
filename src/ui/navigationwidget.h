#ifndef NAVIGATIONWIDGET_H
#define NAVIGATIONWIDGET_H

#include "zeroslackexport.h"

#include <QWidget>
#include <QPointer>
#include <QAbstractButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QCheckBox>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QSet>
#include <QStringList>
#include <QHash>
#include "hierarchyservice.h"
#include "symboloutlinemodel.h"
#include "workspaceconfigurationservice.h"
#include "designcandidates.h"
#include <functional>
#include <QTreeView>
#include <QJsonObject>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
class DesignHierarchyModel;

class EditorSplitController;

class ZEROSLACK_API NavigationWidget : public QWidget
{
    Q_OBJECT

public:
    enum NavigationTab {
        FileTab = 0,
        DesignTab = 1
    };
    enum FileTreeItemKind {
        PlaceholderItem = 0,
        FileItem = 1,
        DirectoryItem = 2,
        VirtualSourceGroupItem = 3
    };
    static constexpr int FileTreeKindRole =
        Qt::UserRole + 2;
    static constexpr int DesignTopRole = Qt::UserRole + 3;

    explicit NavigationWidget(QWidget *parent = nullptr);
    ~NavigationWidget();

    void attachPageRegistry(EditorSplitController* registry);
    void setActiveTab(NavigationTab tab, bool activate = true);
    int activeTab() const;
    void focusSearch();
    QString searchFilter(NavigationTab tab) const;
    void setSearchFilter(NavigationTab tab, const QString& filter);

    void setWorkspaceRoot(const QString& workspaceRoot);
    void setVirtualSourceGroups(
        const QList<WorkspaceVirtualSourceGroup>& groups);
    void registerWorkspacePath(const QString& path,
                               bool directory);
    void unregisterWorkspacePath(const QString& path,
                                 bool recursive);
    void renameWorkspacePath(const QString& sourcePath,
                             const QString& targetPath,
                             bool recursive);
    void updateFileHierarchy(const QStringList& files);
    void updateFileHierarchy(
        const QStringList& files,
        const QList<WorkspaceVirtualSourceGroup>& groups);
    void updateDesignHierarchy(const DesignHierarchyReport& report);
    void clearDesignHierarchy();
    void setDesignProjectExpanded(const QString& projectId, bool expanded);
    bool selectDesignInstance(const QString& instancePath,bool reveal=false);
    bool selectDesignModule(const QString& module,const QString& definitionFile={});
    void updateDesignSummary(const DesignHierarchyReport& report, bool structureChanged = true);
    void setDesignHierarchyPending(bool pending);
    void setDesignCandidateContext(const QStringList& files, bool hasProjects, bool scanning);
    void updateDesignCandidates(const QString& workspace, const QList<DesignFileCandidate>& files,
                                const QStringList& likelyTops);
    void beginDesignProjectSelection();
    void finishDesignProjectSelection(const QString& issue, const QString& file, const QString& module);
    void setSimulationTargetProvider(std::function<QPair<QString,QString>()> provider);
    void refreshSimulationTop();
    QJsonObject designViewState() const;
    void restoreDesignViewState(const QJsonObject& state);

    void highlightFile(const QString& filePath);

    void refreshThemePresentation();
    QIcon symbolIconForTest(SymbolOutlineIconKind iconKind);
    int iconCacheEntryCountForTest() const;

signals:
    void documentActionRequested(const QString& action);
    void fileDoubleClicked(const QString& filePath);
    void fileContextMenuRequested(const QString& filePath, const QPoint& globalPos);
    void fileTreeNodeContextMenuRequested(
        const QString& path,
        bool directory,
        const QPoint& globalPos);
    void designNodeContextMenuRequested(const DesignHierarchyNode& node,
                                        const QPoint& globalPos);
    void designNodeDoubleClicked(const DesignHierarchyNode& node);
    void designInstanceSelected(const QString& instancePath);
    void designProjectActivated(const QString& projectId);
    void designExpansionChanged(const QString& projectId, bool expanded);
    void designViewStateChanged(const QString& workspace, const QJsonObject& state);
    void designModuleChosen(const QString& file, const QString& module);
    void viewChanged(int newTabIndex);
    void searchFilterChanged(int tabIndex, const QString& filter);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onTabChanged(int index);
    void onSearchTextChanged(int index, const QString& text);
    void processFileTreePopulationChunk();
    void onFileTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onDesignTreeDoubleClicked(const QModelIndex& index);
    void onFileTreeContextMenuRequested(const QPoint& pos);
    void onDesignTreeContextMenuRequested(const QPoint& pos);
private:
    std::function<QPair<QString,QString>()> simulationTargetProvider;
    void refreshTabLabels();
    QVBoxLayout* mainLayout = nullptr;
    QTabWidget* tabWidget = nullptr;
    QLineEdit* searchLineEdit = nullptr;
    QLineEdit* designSearchLineEdit = nullptr;
    QPointer<EditorSplitController> pageRegistry;

    QWidget* fileTab = nullptr;
    QTreeWidget* fileTreeWidget = nullptr;
    QVBoxLayout* fileTabLayout = nullptr;

    QWidget* designTab = nullptr;
    QTreeView* designTreeWidget = nullptr;
    QVBoxLayout* designTabLayout = nullptr;
    DesignHierarchyModel* designModel = nullptr;
    DesignHierarchyModel* designSearchModel = nullptr;
    QLabel* designStateLabel = nullptr;
    bool designScanning = false, designAnalysisPending = false;
    QString pendingDesignFile, pendingDesignModule;
    int lastActiveNavigationTab = FileTab;
    QTimer* designSearchTimer = nullptr;
    QTimer* designStateTimer = nullptr;
    QFutureWatcher<QList<QString>>* designSearchWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> designSearchCancellation;
    quint64 designSearchGeneration = 0;
    QSet<QString> designExpandedIds;
    QJsonObject savedDesignState;
    QHash<QString, QJsonObject> workspaceDesignStates;
    bool restoringDesignState = false;
    void startDesignSearch();
    void setDesignTreeModel(DesignHierarchyModel* model);
    void saveDesignViewState();
    void revealPendingDesignProject();

    QStringList currentFileList;
    DesignHierarchyReport currentDesignHierarchy;
    bool populatingDesignTree = false;

    QString fileSearchFilter;
    QString designSearchFilter;
    bool fileTreeRefreshPending = true;
    bool designTreeRefreshPending = true;
    QString currentHighlightedFile;
    QString fileTreeRootPath;
    QString workspaceFileTreeRootPath;
    QList<WorkspaceVirtualSourceGroup>
        virtualSourceGroups;
    QList<WorkspaceVirtualSourceGroup>
        visibleVirtualSourceGroups;
    QHash<QString, bool> explicitlyTrackedPaths;
    QHash<QString, QTreeWidgetItem*> fileItemsByNormalizedPath;
    QTimer* fileTreePopulationTimer = nullptr;
    QStringList pendingFileTreeFiles;
    QStringList visibleExplicitDirectories;
    QHash<QString, QTreeWidgetItem*> pendingFileTreeDirItems;
    int pendingFileTreeIndex = 0;
    bool pendingFileTreeClearPlaceholder = false;
    QHash<int, QIcon> fileIconCache;
    QHash<int, QIcon> symbolIconCache;

    void setupUI();
    void setupFileTab();
    void setupDesignTab();
    void setupConnections();

    void populateFileTree();
    void populateFileTreeSynchronously(const QStringList& files);
    void startAsyncFileTreePopulation(const QStringList& files);
    void cancelFileTreePopulation();
    void appendFileTreeItem(const QString& filePath,
                            QHash<QString, QTreeWidgetItem*>* dirItems);
    void appendVirtualSourceGroupItems();
    QTreeWidgetItem* appendDirectoryTreeItem(
        const QString& directoryPath,
        QHash<QString, QTreeWidgetItem*>* dirItems);
    void populateDesignTree();
    void updateDesignPresentation();
    QTreeWidgetItem* createFileItem(const QString& filePath);
    QIcon getFileIcon(const QString& filePath);
    QIcon getDirectoryIcon(bool virtualGroup = false);
    QIcon getSymbolIcon(SymbolOutlineIconKind iconKind);
    void refreshFileTreeIcons();
    void refreshDesignTreeIcons();
    QString normalizedFileItemPath(const QString& filePath) const;
    QString searchFilterForIndex(int index) const;
    void setStoredSearchFilter(int index, const QString& filter);
    void expandCurrentFileNodes();
    QTreeWidgetItem* findFileItemByPath(const QString& filePath);
    QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text, int column = 0);
};

#endif // NAVIGATIONWIDGET_H
