#include "navigationcommandcoordinator.h"

#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "tabmanager.h"

#include <QTextCursor>

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

    MyCodeEditor* currentEditor = tabManager->getCurrentEditor();
    if (!currentEditor)
        return;

    QTextCursor cursor = currentEditor->textCursor();
    cursor.movePosition(QTextCursor::Start);
    for (int i = 1; i < lineNumber; ++i)
        cursor.movePosition(QTextCursor::Down);
    if (columnNumber > 1) {
        cursor.movePosition(QTextCursor::Right,
                            QTextCursor::MoveAnchor,
                            columnNumber - 1);
    }
    currentEditor->setTextCursor(cursor);
    currentEditor->centerCursor();
    currentEditor->setFocus();
    currentEditor->moveMouseToCursor();
}

void NavigationCommandCoordinator::navigateToSymbol(const sym_list::SymbolInfo& symbol)
{
    navigateToFileAndLine(symbol.fileName, symbol.startLine);
}
