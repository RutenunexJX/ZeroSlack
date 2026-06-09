#ifndef EDITORCOORDINATOR_H
#define EDITORCOORDINATOR_H

#include <QObject>
#include <QString>

class AnalysisCommandCoordinator;
class FileCommandCoordinator;
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
        AnalysisCommandCoordinator* analysisCommandCoordinator,
        SemanticPanelRefreshCoordinator* semanticPanelRefresh);

    void connectSignals();
    void attachEditor(MyCodeEditor* editor);

private:
    void applyAlternateMode(MyCodeEditor* editor) const;
    void applyAlternateModeToOpenEditors() const;
    void handleActiveEditorChanged(MyCodeEditor* editor);

    TabManager* tabManager = nullptr;
    ModeManager* modeManager = nullptr;
    WorkspaceManager* workspaceManager = nullptr;
    FileCommandCoordinator* fileCommandCoordinator = nullptr;
    NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;
    AnalysisCommandCoordinator* analysisCommandCoordinator = nullptr;
    SemanticPanelRefreshCoordinator* semanticPanelRefresh = nullptr;
    bool signalsConnected = false;
};

#endif // EDITORCOORDINATOR_H
