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
    , tabManager(tabManager)
    , navigationManager(navigationManager)
{
}

void NavigationCommandCoordinator::connectSignals()
{
    if (signalsConnected || !navigationManager)
        return;

    connect(navigationManager,
            &NavigationManager::navigationRequested,
            this,
            [this](const QString& filePath, int lineNumber) {
                navigateToFileAndLine(filePath, lineNumber);
            });
    connect(navigationManager,
            &NavigationManager::symbolNavigationRequested,
            this,
            &NavigationCommandCoordinator::navigateToSymbol);

    signalsConnected = true;
}

void NavigationCommandCoordinator::navigateToFileAndLine(
    const QString& filePath,
    int lineNumber,
    int columnNumber)
{
    if (!tabManager || filePath.isEmpty())
        return;

    if (!tabManager->activateOpenFile(filePath)) {
        if (!tabManager->openFileInTab(filePath))
            return;
    }

    if (lineNumber <= 0)
        return;

    navigateEditorToLine(tabManager->getCurrentEditor(), lineNumber, columnNumber);
}

void NavigationCommandCoordinator::navigateEditorToLine(
    MyCodeEditor* editor,
    int lineNumber,
    int columnNumber)
{
    if (!editor)
        return;

    const SourceLineNavigationTarget target =
        SourceNavigationService::getInstance()->lineNavigationTarget(
            lineNumber,
            columnNumber);
    if (!target.matched)
        return;

    editor->applyLineNavigationTarget(target);
}

void NavigationCommandCoordinator::navigateToSymbol(const sym_list::SymbolInfo& symbol)
{
    navigateToFileAndLine(symbol.fileName, symbol.startLine);
}
