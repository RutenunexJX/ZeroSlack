#ifndef TABMANAGER_H
#define TABMANAGER_H

#include <QObject>
#include <QTabWidget>
#include <memory>
#include "documentmodel.h"
#include "mycodeeditor.h"
#include "tabfileio.h"

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
    DocumentSnapshot getCurrentDocument() const;
    DocumentSnapshot getDocumentForEditor(MyCodeEditor* editor) const;
    bool activateOpenFile(const QString& fileName);
    QString getPlainTextFromCurrentTab() const;
    QString getPlainTextFromOpenFile(const QString& fileName) const;
    QStringList getAllOpenFileNames() const;
    QStringList getOpenSystemVerilogFiles() const;
    int editorCount() const;
    DocumentModel* getDocumentModel() const;

    // Tab state management
    void updateTabTitle(MyCodeEditor* editor);
    bool hasUnsavedChanges() const;

signals:
    void tabCreated(MyCodeEditor* editor);
    void tabClosed(const QString& fileName);
    void fileSaved(const QString& fileName);
    void activeTabChanged(MyCodeEditor* editor);
    void activeDocumentChanged(const DocumentSnapshot& snapshot);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentTabChanged(int index);

private:
    QTabWidget* tabWidget;
    std::unique_ptr<DocumentModel> documentModel;
    TabFileIo fileIo;

    // Helper methods
    std::unique_ptr<MyCodeEditor> createEditor();
    bool saveEditorToFile(MyCodeEditor* editor, bool forceSaveAs);
    bool confirmCloseUnsaved(MyCodeEditor* editor);
};

#endif // TABMANAGER_H
