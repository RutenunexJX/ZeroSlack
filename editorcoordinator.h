#ifndef EDITORCOORDINATOR_H
#define EDITORCOORDINATOR_H

#include <QObject>
#include <QString>

class FileCommandCoordinator;
struct EditorSemanticContext;
class ModeManager;
class MyCodeEditor;
class NavigationCommandCoordinator;
class SemanticPanelRefreshCoordinator;
class TabManager;
class WorkspaceManager;

class EditorCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit EditorCoordinator(TabManager* tabManager,
                               ModeManager* modeManager,
                               QObject* parent = nullptr);

    void setWorkflowDependencies(
        WorkspaceManager* workspaceManager,
        FileCommandCoordinator* fileCommandCoordinator,
        NavigationCommandCoordinator* navigationCommandCoordinator,
        SemanticPanelRefreshCoordinator* semanticPanelRefresh);

    void connectSignals();
    void attachEditor(MyCodeEditor* editor);

private:
    void applyAlternateMode(MyCodeEditor* editor) const;
    void applyAlternateModeToOpenEditors() const;
    void handleIncludeOpenRequested(MyCodeEditor* editor,
                                    const QString& includePath,
                                    const QString& currentFile) const;
    void handleDefinitionNavigationRequested(
        MyCodeEditor* editor,
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    void handleActiveEditorChanged(MyCodeEditor* editor);

    TabManager* tabManager = nullptr;
    ModeManager* modeManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
    FileCommandCoordinator* fileCommandCoordinator = nullptr;
    NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;
    SemanticPanelRefreshCoordinator* semanticPanelRefresh = nullptr;
    bool signalsConnected = false;
};

#endif // EDITORCOORDINATOR_H
