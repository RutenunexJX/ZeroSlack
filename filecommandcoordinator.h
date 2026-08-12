#ifndef FILECOMMANDCOORDINATOR_H
#define FILECOMMANDCOORDINATOR_H

#include "zeroslackexport.h"

#include <QObject>
#include <QString>
#include <functional>

class QAction;
class QCloseEvent;
class QWidget;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;

class ZEROSLACK_API FileCommandCoordinator : public QObject
{
    Q_OBJECT

public:
    using WorkspaceDirectorySelector =
        std::function<QString(QWidget* dialogParent)>;

    explicit FileCommandCoordinator(TabManager* tabManager,
                                    WorkspaceManager* workspaceManager,
                                    QObject* parent = nullptr);

    void setWorkspaceDirectorySelector(WorkspaceDirectorySelector selector);

    void newFile();
    void openFile();
    void saveFile();
    void saveFileAs();
    bool saveEditor(const QString& preferredViewId,
                    bool forceSaveAs = false);
    void copy();
    void paste();
    void cut();
    void undo();
    void redo();
    void openDirectoryAsWorkspace();
    void handleCloseEvent(QCloseEvent* event, QWidget* dialogParent);
    void connectActions(QAction* newFileAction,
                        QAction* openFileAction,
                        QAction* saveFileAction,
                        QAction* saveAsAction,
                        QAction* copyAction,
                        QAction* pasteAction,
                        QAction* cutAction,
                        QAction* undoAction,
                        QAction* redoAction,
                        QAction* openWorkspaceAction);

private:
    struct CommandTargets {
        TabManager* tabManager = nullptr;
        WorkspaceManager* workspaceManager = nullptr;

        void set(TabManager* tabManager,
                 WorkspaceManager* workspaceManager);
        void createNewTab() const;
        void openFile() const;
        bool saveEditor(const QString& preferredViewId,
                        bool forceSaveAs) const;
        void openWorkspace(const QString& folderPath) const;
        bool resolvePendingDocuments(
            QWidget* dialogParent) const;
        void finalizeNormalClose() const;
        MyCodeEditor* editorActionTarget(
            const QString& preferredViewId = QString()) const;
    };

    struct EditorCommandDispatcher {
        void copy(MyCodeEditor* editor) const;
        void paste(MyCodeEditor* editor) const;
        void cut(MyCodeEditor* editor) const;
        void undo(MyCodeEditor* editor) const;
        void redo(MyCodeEditor* editor) const;
    };

    CommandTargets targets;
    EditorCommandDispatcher editorCommands;
    WorkspaceDirectorySelector workspaceDirectorySelector;
};

#endif // FILECOMMANDCOORDINATOR_H
