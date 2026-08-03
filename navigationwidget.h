#ifndef NAVIGATIONWIDGET_H
#define NAVIGATIONWIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QSet>
#include <QStringList>
#include <QHash>
#include "hierarchyservice.h"
#include "symboloutlinemodel.h"
#include "workspaceconfigurationservice.h"

class NavigationWidget : public QWidget
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

    explicit NavigationWidget(QWidget *parent = nullptr);
    ~NavigationWidget();

    void setActiveTab(NavigationTab tab);
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
    void setDesignParticipatingFiles(const QSet<QString>& fileNames);

    void highlightFile(const QString& filePath);

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
    void clearDesignTopRequested();
    void refreshDesignHierarchyRequested();
    void viewChanged(int newTabIndex);
    void searchFilterChanged(int tabIndex, const QString& filter);

private slots:
    void onTabChanged(int index);
    void onSearchTextChanged(const QString& text);
    void processFileTreePopulationChunk();
    void onFileTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onDesignTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onFileTreeContextMenuRequested(const QPoint& pos);
    void onDesignTreeContextMenuRequested(const QPoint& pos);

private:
    QVBoxLayout* mainLayout = nullptr;
    QTabWidget* tabWidget = nullptr;
    QLineEdit* searchLineEdit = nullptr;

    QWidget* fileTab = nullptr;
    QTreeWidget* fileTreeWidget = nullptr;
    QVBoxLayout* fileTabLayout = nullptr;
    QCheckBox* hideUnrelatedFilesCheckBox = nullptr;

    QWidget* designTab = nullptr;
    QTreeWidget* designTreeWidget = nullptr;
    QVBoxLayout* designTabLayout = nullptr;
    QLabel* designTopLabel = nullptr;
    QPushButton* designClearButton = nullptr;
    QPushButton* designRefreshButton = nullptr;

    QStringList currentFileList;
    DesignHierarchyReport currentDesignHierarchy;
    QSet<QString> designParticipatingFiles;
    QHash<int, DesignHierarchyNode> designItemPayloads;
    int nextDesignItemPayloadId = 1;

    QString fileSearchFilter;
    QString designSearchFilter;
    QString currentHighlightedFile;
    QString fileTreeRootPath;
    QString workspaceFileTreeRootPath;
    QList<WorkspaceVirtualSourceGroup>
        virtualSourceGroups;
    QList<WorkspaceVirtualSourceGroup>
        visibleVirtualSourceGroups;
    QHash<QString, bool> explicitlyTrackedPaths;
    bool hideUnrelatedFiles = false;
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
    void refreshFileTreeDirectoryDimming();
    void populateDesignTree();
    QTreeWidgetItem* createFileItem(const QString& filePath);
    QTreeWidgetItem* createDesignItem(const DesignHierarchyNode& node);
    QIcon getFileIcon(const QString& filePath);
    QIcon getSymbolIcon(SymbolOutlineIconKind iconKind);
    void applyDesignFileDimming(QTreeWidgetItem* item, bool dimmed);
    void applyDesignItemDimming(QTreeWidgetItem* item, bool dimmed);
    bool fileParticipatesInDesign(const QString& filePath) const;
    QString normalizedFileItemPath(const QString& filePath) const;
    void refreshDesignHeader();
    QString searchFilterForIndex(int index) const;
    void setStoredSearchFilter(int index, const QString& filter);
    void expandCurrentFileNodes();
    QTreeWidgetItem* findFileItemByPath(const QString& filePath);
    QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text, int column = 0);
};

#endif // NAVIGATIONWIDGET_H
