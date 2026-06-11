#include "editorcoordinator.h"

#include "filecommandcoordinator.h"
#include "modemanager.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QMessageBox>

EditorCoordinator::EditorCoordinator(TabManager* tabManager,
                                     ModeManager* modeManager,
                                     QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , modeManager(modeManager)
{
}

void EditorCoordinator::setWorkflowDependencies(
    WorkspaceManager* newWorkspaceManager,
    FileCommandCoordinator* newFileCommandCoordinator,
    NavigationCommandCoordinator* newNavigationCommandCoordinator,
    SemanticPanelRefreshCoordinator* newSemanticPanelRefresh)
{
    workspaceManager = newWorkspaceManager;
    fileCommandCoordinator = newFileCommandCoordinator;
    navigationCommandCoordinator = newNavigationCommandCoordinator;
    semanticPanelRefresh = newSemanticPanelRefresh;
}

void EditorCoordinator::connectSignals()
{
    if (signalsConnected || !tabManager || !modeManager)
        return;

    connect(tabManager, &TabManager::tabCreated,
            this, &EditorCoordinator::attachEditor);
    connect(tabManager, &TabManager::activeTabChanged,
            this, &EditorCoordinator::handleActiveEditorChanged);
    connect(modeManager, &ModeManager::modeChanged,
            this, [this](ModeManager::AppMode) {
                applyAlternateModeToOpenEditors();
            });

    signalsConnected = true;
}

void EditorCoordinator::attachEditor(MyCodeEditor* editor)
{
    if (!editor)
        return;

    applyAlternateMode(editor);
    connect(editor, &MyCodeEditor::definitionJumpRequested,
            this, [this](const QString&, const QString& file, int line) {
                if (navigationCommandCoordinator)
                    navigationCommandCoordinator->navigateToFileAndLine(file, line);
            });
    connect(editor, &MyCodeEditor::alternateCommandActionRequested,
            this, [this, editor](AlternateCommandAction action) {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->executeAlternateCommand(editor, action);
            });
    connect(editor, &MyCodeEditor::includeOpenRequested,
            this, [this, editor](const QString& includePath,
                                 const QString& currentFile) {
                handleIncludeOpenRequested(editor, includePath, currentFile);
            });
    connect(editor, &MyCodeEditor::referenceSearchRequested,
            this, [this](const QString& symbolName,
                         const QString& fileName,
                         const QString& moduleName) {
                if (semanticPanelRefresh) {
                    semanticPanelRefresh->showReferencesForSymbol(
                        symbolName, fileName, moduleName);
                }
            });
    connect(editor, &MyCodeEditor::relationshipBrowseRequested,
            this, [this](const QString& symbolName,
                         const QString& fileName,
                         const QString& moduleName) {
                if (semanticPanelRefresh) {
                    semanticPanelRefresh->showRelationshipsForSymbol(
                        symbolName, fileName, moduleName);
                }
            });
}

void EditorCoordinator::applyAlternateMode(MyCodeEditor* editor) const
{
    if (!editor || !modeManager)
        return;
    editor->setAlternateModeEnabled(modeManager->getCurrentMode() == ModeManager::AlternateMode);
}

void EditorCoordinator::applyAlternateModeToOpenEditors() const
{
    if (!tabManager)
        return;

    for (int i = 0; i < tabManager->editorCount(); ++i)
        applyAlternateMode(tabManager->getEditorAt(i));
}

void EditorCoordinator::handleIncludeOpenRequested(
    MyCodeEditor* editor,
    const QString& includePath,
    const QString& currentFile) const
{
    if (includePath.isEmpty())
        return;

    const QString targetPath = workspaceManager
        ? workspaceManager->resolveIncludePath(includePath, currentFile)
        : QString();
    if (targetPath.isEmpty()) {
        QMessageBox::warning(editor,
                             tr("Include not found"),
                             tr("Can not locate include file:\n%1").arg(includePath));
        return;
    }

    if (tabManager)
        tabManager->openFileInTab(targetPath);
}

void EditorCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    applyAlternateMode(editor);
    if (semanticPanelRefresh)
        semanticPanelRefresh->handleActiveEditorChanged(editor);
}
