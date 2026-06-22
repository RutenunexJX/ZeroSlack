#ifndef NAVIGATIONWIDGET_H
#define NAVIGATIONWIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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
        ModuleTab = 1,
        SymbolTab = 2,
        DesignTab = 3
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
    void onFileTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onModuleTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onSymbolTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onDesignTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onFileTreeContextMenuRequested(const QPoint& pos);
    void onModuleTreeContextMenuRequested(const QPoint& pos);
    void onDesignTreeContextMenuRequested(const QPoint& pos);

private:
    QVBoxLayout* mainLayout;
    QTabWidget* tabWidget;
    QLineEdit* searchLineEdit;

    QWidget* fileTab;
    QTreeWidget* fileTreeWidget;
    QVBoxLayout* fileTabLayout;

    QWidget* moduleTab;
    QTreeWidget* moduleTreeWidget;
    QVBoxLayout* moduleTabLayout;

    QWidget* symbolTab;
    QTreeWidget* symbolTreeWidget;
    QVBoxLayout* symbolTabLayout;

    QWidget* designTab;
    QTreeWidget* designTreeWidget;
    QVBoxLayout* designTabLayout;
    QLabel* designTopLabel;
    QPushButton* designClearButton;
    QPushButton* designRefreshButton;

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

    void setupUI();
    void setupFileTab();
    void setupModuleTab();
    void setupSymbolTab();
    void setupDesignTab();
    void setupConnections();

    void populateFileTree();
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
    bool fileParticipatesInDesign(const QString& filePath) const;
    void refreshDesignHeader();
    void expandCurrentFileNodes();
    QTreeWidgetItem* findFileItemByPath(const QString& filePath);
    QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text, int column = 0);
};

#endif // NAVIGATIONWIDGET_H
