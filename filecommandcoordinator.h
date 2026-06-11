#ifndef FILECOMMANDCOORDINATOR_H
#define FILECOMMANDCOORDINATOR_H

#include <QObject>
#include <QString>

enum class AlternateCommandAction;
class QAction;
class QCloseEvent;
class QWidget;
class MyCodeEditor;
class TabManager;
class WorkspaceManager;

class FileCommandCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit FileCommandCoordinator(TabManager* tabManager,
                                    WorkspaceManager* workspaceManager,
                                    QObject* parent = nullptr);

    void newFile();
    void openFile();
    void saveFile();
    void saveFileAs();
    void copy();
    void paste();
    void cut();
    void undo();
    void redo();
    void executeAlternateCommand(MyCodeEditor* editor,
                                 AlternateCommandAction action);
    void executeAlternateCommandText(MyCodeEditor* editor,
                                     const QString& command);
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
    TabManager* tabManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
};

#endif // FILECOMMANDCOORDINATOR_H
