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
#include <QMenu>

namespace {
QString sourceSymbolActionText(SourceSymbolAction action)
{
    switch (action) {
    case SourceSymbolAction::FindReferences:
        return QStringLiteral("Find References");
    case SourceSymbolAction::ShowRelationships:
        return QStringLiteral("Show Relationships");
    }
    return QString();
}
}

EditorCoordinator::EditorCoordinator(TabManager* tabManager,
                                     ModeManager* modeManager,
                                     QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , modeManager(modeManager)
    , semanticContextService(EditorSemanticContextService::getInstance())
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

    editor->setSemanticContextService(contextService());
    applyAlternateMode(editor);
    connect(editor, &MyCodeEditor::alternateCommandRequested,
            this, [this, editor](const QString& command) {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->executeAlternateCommandText(
                        editor, command);
            });
    connect(editor, &MyCodeEditor::sourceNavigationRequested,
            this, [this, editor](const EditorSourceNavigationTarget& target,
                                 const EditorSemanticContext& context) {
                handleSourceNavigationRequested(editor, target, context);
            });
    connect(editor, &MyCodeEditor::sourceSymbolActionRequested,
            this, &EditorCoordinator::handleSourceSymbolActionRequested);
    connect(editor, &MyCodeEditor::sourceSymbolContextMenuRequested,
            this, [this](QMenu* menu, const EditorSemanticContext& context) {
                handleSourceSymbolContextMenuRequested(menu, context);
            });
}

void EditorCoordinator::applyAlternateMode(MyCodeEditor* editor) const
{
    if (!editor || !modeManager)
        return;
    editor->setAlternateModeEnabled(modeManager->getCurrentMode() == ModeManager::AlternateMode);
}

EditorSemanticContextService* EditorCoordinator::contextService() const
{
    return semanticContextService
        ? semanticContextService
        : EditorSemanticContextService::getInstance();
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
        contextService()->resolveDefinitionTarget(
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

void EditorCoordinator::handleSourceNavigationRequested(
    MyCodeEditor* editor,
    const EditorSourceNavigationTarget& target,
    const EditorSemanticContext& context) const
{
    const EditorSourceNavigationClickState clickState =
        contextService()->sourceNavigationClickState(target);
    if (!clickState.acceptEvent)
        return;

    if (clickState.action == EditorSourceNavigationClickAction::OpenInclude) {
        handleIncludeOpenRequested(editor, clickState.text, context.fileName);
        return;
    }

    if (clickState.action
        == EditorSourceNavigationClickAction::NavigateToDefinition) {
        handleDefinitionNavigationRequested(editor, clickState.text, context);
    }
}

void EditorCoordinator::handleSourceSymbolActionRequested(
    SourceSymbolAction action,
    const EditorSemanticContext& context) const
{
    if (!semanticPanelRefresh)
        return;

    const EditorSourceSymbolActionRequestState requestState =
        contextService()->sourceSymbolActionRequestState(action, context);
    if (!requestState.available)
        return;

    switch (requestState.action) {
    case SourceSymbolAction::FindReferences:
        semanticPanelRefresh->showReferencesForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    case SourceSymbolAction::ShowRelationships:
        semanticPanelRefresh->showRelationshipsForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    }
}

void EditorCoordinator::handleSourceSymbolContextMenuRequested(
    QMenu* menu,
    const EditorSemanticContext& context) const
{
    if (!menu)
        return;

    const EditorSourceSymbolContextMenuState menuState =
        contextService()->sourceSymbolContextMenuState(context);

    menu->addSeparator();
    for (const EditorSourceSymbolMenuItemState& item : menuState.items) {
        QAction* action = menu->addAction(sourceSymbolActionText(item.action));
        action->setEnabled(item.enabled);
        connect(action, &QAction::triggered, this, [this, context, item]() {
            handleSourceSymbolActionRequested(item.action, context);
        });
    }
}

void EditorCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    applyAlternateMode(editor);
    if (semanticPanelRefresh)
        semanticPanelRefresh->handleActiveEditorChanged(editor);
}
