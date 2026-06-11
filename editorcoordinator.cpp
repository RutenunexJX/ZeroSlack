#include "editorcoordinator.h"

#include "editorsemanticcontextservice.h"
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
    connect(editor, &MyCodeEditor::definitionNavigationRequested,
            this, [this, editor](const QString& symbolName,
                                 const EditorSemanticContext& context) {
                handleDefinitionNavigationRequested(editor, symbolName, context);
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
    connect(editor, &MyCodeEditor::sourceSymbolActionRequested,
            this, [this](SourceSymbolAction action,
                         const SourceSymbolActionContext& context) {
                if (!semanticPanelRefresh || !context.available)
                    return;

                switch (action) {
                case SourceSymbolAction::FindReferences:
                    semanticPanelRefresh->showReferencesForSymbol(
                        context.symbolName, context.fileName, context.moduleName);
                    break;
                case SourceSymbolAction::ShowRelationships:
                    semanticPanelRefresh->showRelationshipsForSymbol(
                        context.symbolName, context.fileName, context.moduleName);
                    break;
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

void EditorCoordinator::handleDefinitionNavigationRequested(
    MyCodeEditor* editor,
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    if (!navigationCommandCoordinator || symbolName.isEmpty())
        return;

    const DefinitionNavigationTarget target =
        EditorSemanticContextService::getInstance()->resolveDefinitionTarget(
            symbolName, context);
    if (!target.found)
        return;

    if (target.localFile) {
        navigationCommandCoordinator->navigateEditorToLine(
            editor, target.line, target.column);
        return;
    }

    const QString targetFile =
        target.fileName.isEmpty() ? context.fileName : target.fileName;
    navigationCommandCoordinator->navigateToFileAndLine(
        targetFile, target.line, target.column);
}

void EditorCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    applyAlternateMode(editor);
    if (semanticPanelRefresh)
        semanticPanelRefresh->handleActiveEditorChanged(editor);
}
