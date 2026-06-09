#include "semanticpanelrefreshcoordinator.h"

#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QComboBox>

#include <utility>

SemanticPanelRefreshCoordinator::SemanticPanelRefreshCoordinator(
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    NavigationManager* navigationManager,
    NavigationCommandCoordinator* navigationCommandCoordinator,
    ProblemsPanelCoordinator* problemsPanel,
    ReferencesPanelCoordinator* referencesPanel,
    RelationshipsPanelCoordinator* relationshipsPanel)
    : tabManager(tabManager)
    , workspaceManager(workspaceManager)
    , navigationManager(navigationManager)
    , navigationCommandCoordinator(navigationCommandCoordinator)
    , problemsPanel(problemsPanel)
    , referencesPanel(referencesPanel)
    , relationshipsPanel(relationshipsPanel)
{
}

void SemanticPanelRefreshCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void SemanticPanelRefreshCoordinator::configurePanels()
{
    if (panelsConfigured)
        return;

    if (problemsPanel) {
        problemsPanel->setCurrentFileProvider([this]() {
            return currentFileName();
        });
        problemsPanel->setWorkspaceFilesProvider([this]() {
            return workspaceFiles();
        });
        problemsPanel->setNavigationHandler(
            [this](const QString& fileName, int line, int column) {
                navigateToFileAndLine(fileName, line, column);
            });
    }

    if (referencesPanel) {
        referencesPanel->setWorkspaceFilesProvider([this]() {
            return workspaceFiles();
        });
        referencesPanel->setNavigationHandler(
            [this](const QString& fileName, int line, int column) {
                navigateToFileAndLine(fileName, line, column);
            });
        referencesPanel->setStatusMessageHandler(
            [this](const QString& message, int timeoutMs) {
                showStatusMessage(message, timeoutMs);
            });
    }

    if (relationshipsPanel) {
        relationshipsPanel->setNavigationHandler(
            [this](const QString& fileName, int line, int column) {
                navigateToFileAndLine(fileName, line, column);
            });
        relationshipsPanel->setStatusMessageHandler(
            [this](const QString& message, int timeoutMs) {
                showStatusMessage(message, timeoutMs);
            });
    }

    panelsConfigured = true;
}

void SemanticPanelRefreshCoordinator::updateProblemsPanel(const QString& fileName)
{
    if (problemsPanel)
        problemsPanel->update(fileName);
}

void SemanticPanelRefreshCoordinator::showReferencesForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName)
{
    if (referencesPanel)
        referencesPanel->showReferencesForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::refreshReferencesPanel()
{
    if (referencesPanel)
        referencesPanel->refresh();
}

void SemanticPanelRefreshCoordinator::showRelationshipsForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName)
{
    if (relationshipsPanel)
        relationshipsPanel->showRelationshipsForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::refreshRelationshipsPanel()
{
    if (relationshipsPanel)
        relationshipsPanel->refresh();
}

void SemanticPanelRefreshCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    if (editor && navigationManager)
        navigationManager->onTabChanged(editor->getFileName());
    if (problemsPanelShowsCurrentFile())
        updateProblemsPanel();
}

QString SemanticPanelRefreshCoordinator::currentFileName() const
{
    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    return editor ? editor->getFileName() : QString();
}

QStringList SemanticPanelRefreshCoordinator::workspaceFiles() const
{
    return workspaceManager ? workspaceManager->getSystemVerilogFiles() : QStringList();
}

void SemanticPanelRefreshCoordinator::navigateToFileAndLine(
    const QString& fileName,
    int line,
    int column) const
{
    if (navigationCommandCoordinator)
        navigationCommandCoordinator->navigateToFileAndLine(fileName, line, column);
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
    return problemsPanel
        && problemsPanel->scopeCombo()
        && problemsPanel->scopeCombo()->currentData().toInt() == 0;
}
