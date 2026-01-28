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
#include "syminfo.h"

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

    // 视图切换
    void setActiveTab(NavigationTab tab);
    NavigationTab getActiveTab() const;

    // 数据更新接口
    void updateFileHierarchy(const QStringList& files);
    void updateModuleHierarchy(const QHash<QString, QStringList>& modulesByFile);
    void updateSymbolHierarchy(const QHash<sym_list::sym_type_e, QStringList>& symbolsByType);

    // 高亮和选择
    void highlightFile(const QString& filePath);
    void highlightSymbol(const QString& symbolName);
    void highlightModule(const QString& moduleName);

    // 搜索功能
    void setSearchText(const QString& text);
    QString getSearchText() const;

signals:
    void fileDoubleClicked(const QString& filePath);
    void symbolDoubleClicked(const sym_list::SymbolInfo& symbol);
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
    // UI组件
    QVBoxLayout* mainLayout;
    QTabWidget* tabWidget;
    QLineEdit* searchLineEdit;

    // 文件层次结构标签页
    QWidget* fileTab;
    QTreeWidget* fileTreeWidget;
    QVBoxLayout* fileTabLayout;

    // 模块层次结构标签页
    QWidget* moduleTab;
    QTreeWidget* moduleTreeWidget;
    QVBoxLayout* moduleTabLayout;

    // 符号层次结构标签页
    QWidget* symbolTab;
    QTreeWidget* symbolTreeWidget;
    QVBoxLayout* symbolTabLayout;

    // 数据存储
    QStringList currentFileList;
    QHash<QString, QStringList> currentModuleHierarchy;
    QHash<sym_list::sym_type_e, QStringList> currentSymbolHierarchy;

    // 内部状态
    QString currentSearchFilter;
    QString currentHighlightedFile;

    // 初始化方法
    void setupUI();
    void setupFileTab();
    void setupModuleTab();
    void setupSymbolTab();
    void setupConnections();

    // 辅助方法
    void populateFileTree();
    void populateModuleTree();
    void populateSymbolTree();
    void applySearchFilter();
    QTreeWidgetItem* createFileItem(const QString& filePath);
    QTreeWidgetItem* createModuleItem(const QString& moduleName, const QString& fileName);
    QTreeWidgetItem* createSymbolItem(const QString& symbolName, sym_list::sym_type_e symbolType);
    QString getSymbolTypeDisplayName(sym_list::sym_type_e symbolType);
    QIcon getFileIcon(const QString& filePath);
    QIcon getSymbolIcon(sym_list::sym_type_e symbolType);
    void expandCurrentFileNodes();
    QTreeWidgetItem* findItemByText(QTreeWidget* tree, const QString& text, int column = 0);
};

#endif // NAVIGATIONWIDGET_H
