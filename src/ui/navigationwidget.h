#ifndef NAVIGATIONWIDGET_H
#define NAVIGATIONWIDGET_H

#include "zeroslackexport.h"

#include <QWidget>
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

class ZEROSLACK_API NavigationWidget : public QWidget
{
    Q_OBJECT

public:
    enum NavigationTab {
        FileTab = 0,
        DesignTab = 1,
        SourceFilesTab = 2
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

    void setActiveTab(NavigationTab tab);
    int activeTab() const;
    void setSourceFilesWidget(QWidget* widget);
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
    void updateDesignSummary(const DesignHierarchyReport& report, bool structureChanged = true);
    void setDesignHierarchyPending(bool pending);
    void setDesignCandidateContext(const QStringList& files, bool hasProjects, bool scanning);
    void updateDesignCandidates(const QString& workspace, const QList<DesignFileCandidate>& files,
                                const QStringList& likelyTops);
    void beginDesignProjectSelection();
    void finishDesignProjectSelection(const QString& issue, const QString& file, const QString& module);

    void highlightFile(const QString& filePath);

    void refreshThemePresentation();
    QIcon symbolIconForTest(SymbolOutlineIconKind iconKind);
    int iconCacheEntryCountForTest() const;

signals:
    void fileDoubleClicked(const QString& filePath);
    void fileContextMenuRequested(const QString& filePath, const QPoint& globalPos);
    void fileTreeNodeContextMenuRequested(
        const QString& path,
        bool directory,
        const QPoint& globalPos);
    void designNodeContextMenuRequested(const DesignHierarchyNode& node,
                                        const QPoint& globalPos);
    void designNodeDoubleClicked(const DesignHierarchyNode& node);
    void designProjectActivated(const QString& projectId);
    void designExpansionChanged(const QString& projectId, bool expanded);
    void addDesignProjectRequested();
    void designModuleChosen(const QString& file, const QString& module);
    void viewChanged(int newTabIndex);
    void searchFilterChanged(int tabIndex, const QString& filter);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onTabChanged(int index);
    void onSearchTextChanged(const QString& text);
    void processFileTreePopulationChunk();
    void onFileTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onDesignTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onFileTreeContextMenuRequested(const QPoint& pos);
    void onDesignTreeContextMenuRequested(const QPoint& pos);

private:
    void refreshTabLabels();
    QVBoxLayout* mainLayout = nullptr;
    QTabWidget* tabWidget = nullptr;
    QLineEdit* searchLineEdit = nullptr;

    QWidget* fileTab = nullptr;
    QTreeWidget* fileTreeWidget = nullptr;
    QVBoxLayout* fileTabLayout = nullptr;

    QWidget* designTab = nullptr;
    QTreeWidget* designTreeWidget = nullptr;
    QVBoxLayout* designTabLayout = nullptr;
    QTreeWidget* designCandidateTree = nullptr;
    QLabel* designStateLabel = nullptr;
    QLabel* designSelectionError = nullptr;
    QAbstractButton* designBackButton = nullptr;
    QAbstractButton* designClearSearchButton = nullptr;
    QList<DesignFileCandidate> designCandidates;
    QStringList designLikelyTops;
    QString designAddSearchFilter;
    QString pendingDesignFile, pendingDesignModule;
    bool designAdding = true;
    bool designSelectionExplicit = false;
    bool designHasProjects = false;
    bool designScanning = false;
    bool designAnalysisPending = false;
    int savedDesignScroll = 0;
    QString savedDesignSelection;

    QStringList currentFileList;
    DesignHierarchyReport currentDesignHierarchy;
    QHash<int, DesignHierarchyNode> designItemPayloads;
    int nextDesignItemPayloadId = 1;
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
    void setupDesignSelector();
    void populateDesignCandidates();
    void updateDesignPresentation();
    void cancelDesignProjectSelection();
    void activateDesignCandidate(QTreeWidgetItem* item);
    void revealPendingDesignProject();
    QTreeWidgetItem* createFileItem(const QString& filePath);
    QTreeWidgetItem* createDesignItem(const DesignHierarchyNode& node);
    QIcon getFileIcon(const QString& filePath);
    QIcon getDirectoryIcon(bool virtualGroup = false);
    QIcon getSymbolIcon(SymbolOutlineIconKind iconKind);
    void refreshFileTreeIcons();
    void refreshDesignTreeIcons();
    void applyDesignItemDimming(QTreeWidgetItem* item, bool dimmed);
    QString normalizedFileItemPath(const QString& filePath) const;
    QString searchFilterForIndex(int index) const;
    void setStoredSearchFilter(int index, const QString& filter);
    void expandCurrentFileNodes();
    QTreeWidgetItem* findFileItemByPath(const QString& filePath);
    QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text, int column = 0);
};

#endif // NAVIGATIONWIDGET_H
