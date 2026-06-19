#ifndef NAVIGATIONWIDGET_H
#define NAVIGATIONWIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidgetItem>
#include <QStringList>
#include <QHash>
#include "modulehierarchymodel.h"
#include "symboloutlinemodel.h"

class NavigationWidget : public QWidget
{
    Q_OBJECT

public:
    enum NavigationTab {
        FileTab = 0,
        ModuleTab = 1,
        SymbolTab = 2
    };

    explicit NavigationWidget(QWidget *parent = nullptr);
    ~NavigationWidget();

    void setActiveTab(NavigationTab tab);
    NavigationTab getActiveTab() const;

    void updateFileHierarchy(const QStringList& files);
    void updateModuleHierarchy(const QList<ModuleHierarchyGroup>& hierarchy);
    void updateSymbolHierarchy(const QList<SymbolOutlineGroup>& symbolGroups);

    void highlightFile(const QString& filePath);
    void highlightSymbol(const QString& symbolName);
    void highlightModule(const QString& moduleName);

    void setSearchText(const QString& text);
    QString getSearchText() const;

signals:
    void fileDoubleClicked(const QString& filePath);
    void symbolRowDoubleClicked(const SymbolOutlineSymbolRow& row);
    void moduleDoubleClicked(const QString& moduleName);
    void viewChanged(int newTabIndex);
    void searchFilterChanged(const QString& filter);

private slots:
    void onTabChanged(int index);
    void onSearchTextChanged(const QString& text);
    void onFileTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onModuleTreeDoubleClicked(QTreeWidgetItem* item, int column);
    void onSymbolTreeDoubleClicked(QTreeWidgetItem* item, int column);

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

    QStringList currentFileList;
    QList<ModuleHierarchyGroup> currentModuleHierarchy;
    QList<SymbolOutlineGroup> currentSymbolHierarchy;
    QHash<int, SymbolOutlineSymbolRow> symbolItemPayloads;
    int nextSymbolItemPayloadId = 1;

    QString currentSearchFilter;
    QString currentHighlightedFile;

    void setupUI();
    void setupFileTab();
    void setupModuleTab();
    void setupSymbolTab();
    void setupConnections();

    void populateFileTree();
    void populateModuleTree();
    void populateSymbolTree();
    void applySearchFilter();
    QTreeWidgetItem* createFileItem(const QString& filePath);
    QTreeWidgetItem* createModuleItem(const QString& moduleName, const QString& fileName);
    QTreeWidgetItem* createSymbolItem(const SymbolOutlineSymbolRow& row);
    QIcon getFileIcon(const QString& filePath);
    QIcon getSymbolIcon(SymbolOutlineIconKind iconKind);
    void expandCurrentFileNodes();
    QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text, int column = 0);
};

#endif // NAVIGATIONWIDGET_H
