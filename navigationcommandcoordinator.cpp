#include "navigationcommandcoordinator.h"

#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "sourcenavigationservice.h"
#include "tabmanager.h"

NavigationCommandCoordinator::NavigationCommandCoordinator(
    TabManager* tabManager,
    NavigationManager* navigationManager,
    QObject* parent)
    : QObject(parent)
{
    targets.set(tabManager, navigationManager);
}

void NavigationCommandCoordinator::NavigationTargets::set(
    TabManager* newTabManager,
    NavigationManager* newNavigationManager)
{
    tabManager = newTabManager;
    navigationManager = newNavigationManager;
}

bool NavigationCommandCoordinator::NavigationTargets::hasNavigationManager() const
{
    return navigationManager != nullptr;
}

NavigationManager*
NavigationCommandCoordinator::NavigationTargets::navigationManagerObject() const
{
    return navigationManager;
}

bool NavigationCommandCoordinator::NavigationTargets::activateOrOpenFile(
    const QString& filePath) const
{
    if (!tabManager || filePath.isEmpty())
        return false;

    return tabManager->activateOpenFile(filePath)
        || tabManager->openFileInTab(filePath);
}

MyCodeEditor* NavigationCommandCoordinator::NavigationTargets::currentEditor() const
{
    return tabManager ? tabManager->getCurrentEditor() : nullptr;
}

bool NavigationCommandCoordinator::LineNavigationResolver::applyToEditor(
    MyCodeEditor* editor,
    int lineNumber,
    int columnNumber) const
{
    if (!editor)
        return false;

    const SourceLineNavigationTarget target =
        SourceNavigationService::getInstance()->lineNavigationTarget(
            lineNumber,
            columnNumber);
    if (!target.matched)
        return false;

    editor->applyLineNavigationTarget(target);
    return true;
}

void NavigationCommandCoordinator::connectSignals()
{
    if (signalsConnected || !targets.hasNavigationManager())
        return;

    connect(targets.navigationManagerObject(),
            &NavigationManager::navigationRequested,
            this,
            [this](const QString& filePath, int lineNumber) {
                navigateToFileAndLine(filePath, lineNumber);
            });
    connect(targets.navigationManagerObject(),
            &NavigationManager::symbolRowNavigationRequested,
            this,
            qOverload<const SymbolOutlineSymbolRow&>(
                &NavigationCommandCoordinator::navigateToSymbol));

    signalsConnected = true;
}

void NavigationCommandCoordinator::navigateToFileAndLine(
    const QString& filePath,
    int lineNumber,
    int columnNumber)
{
    if (!targets.activateOrOpenFile(filePath))
        return;

    if (lineNumber <= 0)
        return;

    navigateEditorToLine(targets.currentEditor(), lineNumber, columnNumber);
}

void NavigationCommandCoordinator::navigateEditorToLine(
    MyCodeEditor* editor,
    int lineNumber,
    int columnNumber)
{
    lineResolver.applyToEditor(editor, lineNumber, columnNumber);
}

void NavigationCommandCoordinator::navigateToSymbol(
    const SymbolOutlineSymbolRow& row)
{
    if (!row.symbolRecord.isValid()
        || row.symbolRecord.location.fileName.isEmpty()) {
        return;
    }

    navigateToFileAndLine(row.symbolRecord.location.fileName,
                          row.symbolRecord.location.startLine,
                          row.symbolRecord.location.startColumn);
}
