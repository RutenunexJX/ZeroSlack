#include "editorcoordinator.h"

#include "analysiscommandcoordinator.h"
#include "filecommandcoordinator.h"
#include "modemanager.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

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
    AnalysisCommandCoordinator* newAnalysisCommandCoordinator,
    SemanticPanelRefreshCoordinator* newSemanticPanelRefresh)
{
    workspaceManager = newWorkspaceManager;
    fileCommandCoordinator = newFileCommandCoordinator;
    navigationCommandCoordinator = newNavigationCommandCoordinator;
    analysisCommandCoordinator = newAnalysisCommandCoordinator;
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
    editor->setIncludePathResolver(
        [this](const QString& includePath, const QString& currentFile) {
            return workspaceManager
                ? workspaceManager->resolveIncludePath(includePath, currentFile)
                : QString();
        });
    editor->setFileOpenHandler([this](const QString& filePath) {
        return tabManager && tabManager->openFileInTab(filePath);
    });
    connect(editor, &MyCodeEditor::definitionJumpRequested,
            this, [this](const QString&, const QString& file, int line) {
                if (navigationCommandCoordinator)
                    navigationCommandCoordinator->navigateToFileAndLine(file, line);
            });
    connect(editor, &MyCodeEditor::relationshipAnalysisRequested,
            this, [this](const QString& fileName, const QString& content) {
                if (analysisCommandCoordinator) {
                    analysisCommandCoordinator->requestSingleFileRelationshipAnalysis(
                        fileName, content);
                }
            });
    connect(editor, &MyCodeEditor::saveFileRequested,
            this, [this]() {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->saveFile();
            });
    connect(editor, &MyCodeEditor::saveFileAsRequested,
            this, [this]() {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->saveFileAs();
            });
    connect(editor, &MyCodeEditor::openFileRequested,
            this, [this]() {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->openFile();
            });
    connect(editor, &MyCodeEditor::newFileRequested,
            this, [this]() {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->newFile();
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

void EditorCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    applyAlternateMode(editor);
    if (semanticPanelRefresh)
        semanticPanelRefresh->handleActiveEditorChanged(editor);
}
