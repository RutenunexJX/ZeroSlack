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
#include "modulehierarchymodel.h"
#include "symboloutlinemodel.h"

class NavigationWidget : public QWidget
{
    Q_OBJECT

public:
    enum NavigationTab {
        FileTab = 0,
        DesignTab = 1,
        ModuleTab = FileTab,
        SymbolTab = FileTab
    };

    explicit NavigationWidget(QWidget *parent = nullptr);
    ~NavigationWidget();

    void setActiveTab(NavigationTab tab);
    NavigationTab getActiveTab() const;

    void updateFileHierarchy(const QStringList& files);
    void updateModuleHierarchy(const QList<ModuleHierarchyGroup>& hierarchy);
    void updateSymbolHierarchy(const QList<SymbolOutlineGroup>& symbolGroups);
    void updateDesignHierarchy(const DesignHierarchyReport& report);
    void clearDesignHierarchy();
    void setDesignParticipatingFiles(const QSet<QString>& fileNames);

    void highlightFile(const QString& filePath);
    void highlightSymbol(const QString& symbolName);
    void highlightModule(const QString& moduleName);

    void setSearchText(const QString& text);
    QString getSearchText() const;

signals:
    void fileDoubleClicked(const QString& filePath);
    void symbolRowDoubleClicked(const SymbolOutlineSymbolRow& row);
    void moduleDoubleClicked(const QString& moduleName);
    void fileContextMenuRequested(const QString& filePath, const QPoint& globalPos);
    void moduleContextMenuRequested(const QString& moduleName, const QPoint& globalPos);
    void designNodeContextMenuRequested(const DesignHierarchyNode& node,
                                        const QPoint& globalPos);
    void designNodeDoubleClicked(const DesignHierarchyNode& node);
    void clearDesignTopRequested();
    void refreshDesignHierarchyRequested();
    void viewChanged(int newTabIndex);
    void searchFilterChanged(const QString& filter);

private slots:
    void onTabChanged(int index);
    void onSearchTextChanged(const QString& text);
    void processFileTreePopulationChunk();
    void onFileTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onModuleTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onSymbolTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onDesignTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onFileTreeContextMenuRequested(const QPoint& pos);
    void onModuleTreeContextMenuRequested(const QPoint& pos);
    void onDesignTreeContextMenuRequested(const QPoint& pos);

private:
    QVBoxLayout* mainLayout = nullptr;
    QTabWidget* tabWidget = nullptr;
    QLineEdit* searchLineEdit = nullptr;

    QWidget* fileTab = nullptr;
    QTreeWidget* fileTreeWidget = nullptr;
    QVBoxLayout* fileTabLayout = nullptr;
    QCheckBox* hideUnrelatedFilesCheckBox = nullptr;

    QWidget* moduleTab = nullptr;
    QTreeWidget* moduleTreeWidget = nullptr;
    QVBoxLayout* moduleTabLayout = nullptr;

    QWidget* symbolTab = nullptr;
    QTreeWidget* symbolTreeWidget = nullptr;
    QVBoxLayout* symbolTabLayout = nullptr;

    QWidget* designTab = nullptr;
    QTreeWidget* designTreeWidget = nullptr;
    QVBoxLayout* designTabLayout = nullptr;
    QLabel* designTopLabel = nullptr;
    QPushButton* designClearButton = nullptr;
    QPushButton* designRefreshButton = nullptr;

    QStringList currentFileList;
    QList<ModuleHierarchyGroup> currentModuleHierarchy;
    QList<SymbolOutlineGroup> currentSymbolHierarchy;
    DesignHierarchyReport currentDesignHierarchy;
    QSet<QString> designParticipatingFiles;
    QHash<int, SymbolOutlineSymbolRow> symbolItemPayloads;
    QHash<int, DesignHierarchyNode> designItemPayloads;
    int nextSymbolItemPayloadId = 1;
    int nextDesignItemPayloadId = 1;

    QString currentSearchFilter;
    QString currentHighlightedFile;
    QString fileTreeRootPath;
    bool hideUnrelatedFiles = false;
    QHash<QString, QTreeWidgetItem*> fileItemsByNormalizedPath;
    QTimer* fileTreePopulationTimer = nullptr;
    QStringList pendingFileTreeFiles;
    QHash<QString, QTreeWidgetItem*> pendingFileTreeDirItems;
    int pendingFileTreeIndex = 0;
    bool pendingFileTreeClearPlaceholder = false;
    QHash<int, QIcon> fileIconCache;
    QHash<int, QIcon> symbolIconCache;

    void setupUI();
    void setupFileTab();
    void setupModuleTab();
    void setupSymbolTab();
    void setupDesignTab();
    void setupConnections();

    void populateFileTree();
    void populateFileTreeSynchronously(const QStringList& files);
    void startAsyncFileTreePopulation(const QStringList& files);
    void cancelFileTreePopulation();
    void appendFileTreeItem(const QString& filePath,
                            QHash<QString, QTreeWidgetItem*>* dirItems);
    void refreshFileTreeDirectoryDimming();
    void populateModuleTree();
    void populateSymbolTree();
    void populateDesignTree();
    void applySearchFilter();
    QTreeWidgetItem* createFileItem(const QString& filePath);
    QTreeWidgetItem* createModuleItem(const QString& moduleName, const QString& fileName);
    QTreeWidgetItem* createSymbolItem(const SymbolOutlineSymbolRow& row);
    QTreeWidgetItem* createDesignItem(const DesignHierarchyNode& node);
    QIcon getFileIcon(const QString& filePath);
    QIcon getSymbolIcon(SymbolOutlineIconKind iconKind);
    void applyDesignFileDimming(QTreeWidgetItem* item, bool dimmed);
    void applyDesignItemDimming(QTreeWidgetItem* item, bool dimmed);
    bool fileParticipatesInDesign(const QString& filePath) const;
    QString normalizedFileItemPath(const QString& filePath) const;
    void refreshDesignHeader();
    void expandCurrentFileNodes();
    QTreeWidgetItem* findFileItemByPath(const QString& filePath);
    QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text, int column = 0);
};

#endif // NAVIGATIONWIDGET_H
