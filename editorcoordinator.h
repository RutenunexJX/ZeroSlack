#ifndef EDITORCOORDINATOR_H
#define EDITORCOORDINATOR_H

#include <QObject>
#include <QString>

class FileCommandCoordinator;
class EditorSemanticContextService;
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
    struct WorkflowDependencies {
        WorkspaceManager* workspaceManager = nullptr;
        FileCommandCoordinator* fileCommandCoordinator = nullptr;
        NavigationCommandCoordinator* navigationCommandCoordinator = nullptr;
        SemanticPanelRefreshCoordinator* semanticPanelRefresh = nullptr;

        void set(WorkspaceManager* workspaceManager,
                 FileCommandCoordinator* fileCommandCoordinator,
                 NavigationCommandCoordinator* navigationCommandCoordinator,
                 SemanticPanelRefreshCoordinator* semanticPanelRefresh);
        QString resolveIncludePath(const QString& includePath,
                                   const QString& currentFile) const;
        void executeAlternateCommand(MyCodeEditor* editor,
                                     const QString& command) const;
        void navigateEditorToLine(MyCodeEditor* editor,
                                  int line,
                                  int column) const;
        void navigateToFileAndLine(const QString& fileName,
                                   int line,
                                   int column) const;
        void showReferencesForSymbol(const QString& symbolName,
                                     const QString& fileName,
                                     const QString& moduleName) const;
        void showRelationshipsForSymbol(const QString& symbolName,
                                        const QString& fileName,
                                        const QString& moduleName) const;
        void handleActiveEditorChanged(MyCodeEditor* editor) const;
        bool canNavigate() const;
        bool hasSemanticPanelRefresh() const;
    };

    struct SemanticRuntime {
        EditorSemanticContextService* service = nullptr;

        void init();
        EditorSemanticContextService* contextService() const;
    };

    EditorSemanticContextService* contextService() const;
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
    WorkflowDependencies dependencies;
    SemanticRuntime semanticRuntime;
    bool signalsConnected = false;
};

#endif // EDITORCOORDINATOR_H
