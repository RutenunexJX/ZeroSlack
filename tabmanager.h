#ifndef TABMANAGER_H
#define TABMANAGER_H

#include <QObject>
#include <QTabWidget>
#include <memory>
#include "documentmodel.h"
#include "mycodeeditor.h"
#include "tabdocumentqueries.h"
#include "tabfileio.h"
#include "tabopencontroller.h"
#include "tabsavecontroller.h"
#include "tabtitlecontroller.h"
#include "workspacesessionstateservice.h"

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
    // Metadata-only: text is empty and no cached editor text is copied.
    DocumentSnapshot getCurrentDocumentMetadata() const;
    DocumentSnapshot getDocumentForEditor(MyCodeEditor* editor) const;
    bool activateOpenFile(const QString& fileName);
    QString getPlainTextFromCurrentTab() const;
    QString getPlainTextFromOpenFile(const QString& fileName) const;
    QStringList getAllOpenFileNames() const;
    QStringList getOpenSystemVerilogFiles() const;
    int editorCount() const;
    DocumentModel* getDocumentModel() const;
    void refreshSemanticPresentations(
        const QString& changedFileName = QString());

    // Tab state management
    void updateTabTitle(MyCodeEditor* editor);
    void setWorkspaceScope(const QStringList& workspaceRoots,
                           const QString& activeWorkspaceRoot);
    bool closeTabsInWorkspace(const QString& workspaceRoot);
    bool hasUnsavedChanges() const;
    QList<WorkspaceSessionTabState> workspaceSessionTabs(
        const QString& workspaceRoot) const;
    QStringList restoreWorkspaceSessionTabs(
        const QString& workspaceRoot,
        const QList<WorkspaceSessionTabState>& tabs,
        QStringList* skippedFiles = nullptr);

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
    TabOpenController openController;
    TabDocumentQueries documentQueries;
    TabSaveController saveController;
    TabTitleController titleController;
    QStringList scopedWorkspaceRoots;
    QString activeWorkspaceRoot;

    void applyWorkspaceScope();
    bool editorVisibleInWorkspaceScope(MyCodeEditor* editor) const;
};

#endif // TABMANAGER_H
