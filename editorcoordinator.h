#ifndef EDITORCOORDINATOR_H
#define EDITORCOORDINATOR_H

#include <QObject>
#include <QString>

class FileCommandCoordinator;
struct EditorSemanticContext;
struct EditorSourceNavigationTarget;
enum class SourceSymbolAction;
class ModeManager;
class MyCodeEditor;
class NavigationCommandCoordinator;
class QMenu;
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
    void handleSourceNavigationRequested(
        MyCodeEditor* editor,
        const EditorSourceNavigationTarget& target,
        const EditorSemanticContext& context) const;
    void handleSourceSymbolActionRequested(
        SourceSymbolAction action,
        const EditorSemanticContext& context) const;
    void handleSourceSymbolContextMenuRequested(
        QMenu* menu,
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
