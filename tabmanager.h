#ifndef TABMANAGER_H
#define TABMANAGER_H

#include <QObject>
#include <QTabWidget>
#include <memory>
#include "mycodeeditor.h"

class TabManager : public QObject
{
    Q_OBJECT

public:
    explicit TabManager(QTabWidget* tabWidget, QObject *parent = nullptr);
    ~TabManager();

    // Tab operations
    void createNewTab();
    bool openFileInTab(const QString& fileName);
    bool saveCurrentTab();
    bool saveAsCurrentTab();
    void closeTab(int index);

    // Tab queries
    MyCodeEditor* getCurrentEditor() const;
    MyCodeEditor* getEditorAt(int index) const;
    QString getPlainTextFromCurrentTab() const;
    QString getPlainTextFromOpenFile(const QString& fileName) const;
    QStringList getAllOpenFileNames() const;
    QStringList getOpenSystemVerilogFiles() const;

    // Tab state management
    void updateTabTitle(MyCodeEditor* editor);
    bool hasUnsavedChanges() const;

signals:
    void tabCreated(MyCodeEditor* editor);
    void tabClosed(const QString& fileName);
    void fileSaved(const QString& fileName);
    void activeTabChanged(MyCodeEditor* editor);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);

private:
    QTabWidget* tabWidget;

    // Helper methods
    std::unique_ptr<MyCodeEditor> createEditor();
    bool confirmCloseUnsaved(MyCodeEditor* editor);
    QString getDisplayName(const QString& fullPath) const;
    bool isSystemVerilogFile(const QString& fileName) const;
};

#endif // TABMANAGER_H
