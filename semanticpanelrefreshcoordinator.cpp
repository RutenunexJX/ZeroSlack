#include "semanticpanelrefreshcoordinator.h"

#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <utility>

SemanticPanelRefreshCoordinator::SemanticPanelRefreshCoordinator(
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    NavigationManager* navigationManager,
    NavigationCommandCoordinator* navigationCommandCoordinator,
    ProblemsPanelCoordinator* problemsPanel,
    ReferencesPanelCoordinator* referencesPanel,
    RelationshipsPanelCoordinator* relationshipsPanel)
{
    dependencies.set(tabManager,
                     workspaceManager,
                     navigationManager,
                     navigationCommandCoordinator);
    panels.set(problemsPanel, referencesPanel, relationshipsPanel);
}

void SemanticPanelRefreshCoordinator::ContextDependencies::set(
    TabManager* newTabManager,
    WorkspaceManager* newWorkspaceManager,
    NavigationManager* newNavigationManager,
    NavigationCommandCoordinator* newNavigationCommandCoordinator)
{
    tabManager = newTabManager;
    workspaceManager = newWorkspaceManager;
    navigationManager = newNavigationManager;
    navigationCommandCoordinator = newNavigationCommandCoordinator;
}

QString SemanticPanelRefreshCoordinator::ContextDependencies::currentFileName() const
{
    return tabManager ? tabManager->getCurrentDocument().fileName : QString();
}

QStringList SemanticPanelRefreshCoordinator::ContextDependencies::workspaceFiles() const
{
    return workspaceManager ? workspaceManager->getSystemVerilogFiles() : QStringList();
}

void SemanticPanelRefreshCoordinator::ContextDependencies::navigateToFileAndLine(
    const QString& fileName,
    int line,
    int column) const
{
    if (navigationCommandCoordinator)
        navigationCommandCoordinator->navigateToFileAndLine(fileName, line, column);
}

void SemanticPanelRefreshCoordinator::ContextDependencies::handleActiveEditorChanged(
    MyCodeEditor* editor) const
{
    const DocumentSnapshot document =
        tabManager ? tabManager->getDocumentForEditor(editor) : DocumentSnapshot();
    if (editor && navigationManager)
        navigationManager->onTabChanged(document.fileName);
}

void SemanticPanelRefreshCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void SemanticPanelRefreshCoordinator::configurePanels()
{
    if (panels.isConfigured())
        return;

    const CurrentFileProvider currentFileProvider = [this]() {
        return currentFileName();
    };
    const WorkspaceFilesProvider workspaceFilesProvider = [this]() {
        return workspaceFiles();
    };
    const NavigationHandler navigationHandler =
        [this](const QString& fileName, int line, int column) {
            navigateToFileAndLine(fileName, line, column);
        };
    const StatusMessageHandler statusMessageHandler =
        [this](const QString& message, int timeoutMs) {
            showStatusMessage(message, timeoutMs);
        };

    panels.configureProblemsPanel(currentFileProvider,
                                  workspaceFilesProvider,
                                  navigationHandler);
    panels.configureReferencesPanel(workspaceFilesProvider,
                                    navigationHandler,
                                    statusMessageHandler);
    panels.configureRelationshipsPanel(navigationHandler,
                                       statusMessageHandler);
    panels.markConfigured();
}

void SemanticPanelRefreshCoordinator::updateProblemsPanel(const QString& fileName)
{
    panels.updateProblemsPanel(fileName);
}

void SemanticPanelRefreshCoordinator::showReferencesForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName)
{
    panels.showReferencesForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::refreshReferencesPanel()
{
    panels.refreshReferencesPanel();
}

void SemanticPanelRefreshCoordinator::showRelationshipsForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName)
{
    panels.showRelationshipsForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::refreshRelationshipsPanel()
{
    panels.refreshRelationshipsPanel();
}

void SemanticPanelRefreshCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    dependencies.handleActiveEditorChanged(editor);
    if (problemsPanelShowsCurrentFile())
        updateProblemsPanel();
}

QString SemanticPanelRefreshCoordinator::currentFileName() const
{
    return dependencies.currentFileName();
}

QStringList SemanticPanelRefreshCoordinator::workspaceFiles() const
{
    return dependencies.workspaceFiles();
}

void SemanticPanelRefreshCoordinator::navigateToFileAndLine(
    const QString& fileName,
    int line,
    int column) const
{
    dependencies.navigateToFileAndLine(fileName, line, column);
}

void SemanticPanelRefreshCoordinator::showStatusMessage(
    const QString& message,
    int timeoutMs) const
{
    if (statusMessageHandler)
        statusMessageHandler(message, timeoutMs);
}

bool SemanticPanelRefreshCoordinator::problemsPanelShowsCurrentFile() const
{
    return panels.problemsPanelShowsCurrentFile();
}
