#include "navigationcommandcoordinator.h"

#include "mycodeeditor.h"
#include "navigationmanager.h"
#include "sourcenavigationservice.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QTextCursor>

namespace {
QString normalizedNavigationWorkspacePath(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}
}

NavigationCommandCoordinator::NavigationCommandCoordinator(
    TabManager* tabManager,
    NavigationManager* navigationManager,
    WorkspaceManager* workspaceManager,
    QObject* parent)
    : QObject(parent)
{
    targets.set(tabManager, navigationManager, workspaceManager);
}

bool NavigationCommandCoordinator::NavigationLocation::isValid() const
{
    return !filePath.isEmpty() && lineNumber > 0;
}

bool NavigationCommandCoordinator::NavigationLocation::operator==(
    const NavigationLocation& other) const
{
    return filePath == other.filePath
        && workspacePath == other.workspacePath
        && lineNumber == other.lineNumber
        && columnNumber == other.columnNumber;
}

void NavigationCommandCoordinator::NavigationTargets::set(
    TabManager* newTabManager,
    NavigationManager* newNavigationManager,
    WorkspaceManager* newWorkspaceManager)
{
    tabManager = newTabManager;
    navigationManager = newNavigationManager;
    workspaceManager = newWorkspaceManager;
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

NavigationCommandCoordinator::NavigationLocation
NavigationCommandCoordinator::NavigationTargets::currentLocation() const
{
    NavigationLocation location;
    if (!tabManager)
        return location;

    const DocumentSnapshot document = tabManager->getCurrentDocument();
    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor || document.fileName.isEmpty())
        return location;

    const QTextCursor cursor = editor->textCursor();
    location.filePath = document.fileName;
    location.workspacePath = currentWorkspacePath();
    location.lineNumber = cursor.blockNumber() + 1;
    location.columnNumber = cursor.positionInBlock() + 1;
    return location;
}

QString NavigationCommandCoordinator::NavigationTargets::currentWorkspacePath() const
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return QString();
    return normalizedNavigationWorkspacePath(workspaceManager->getWorkspacePath());
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
    const NavigationLocation current = targets.currentLocation();
    const NavigationLocation destination{
        filePath,
        targets.currentWorkspacePath(),
        lineNumber,
        columnNumber};

    if (!targets.activateOrOpenFile(filePath))
        return;

    recordLocationBeforeNavigation(current, destination);

    if (lineNumber <= 0)
        return;

    lineResolver.applyToEditor(targets.currentEditor(),
                               lineNumber,
                               columnNumber);
}

bool NavigationCommandCoordinator::navigateToFileAndLineAndFlash(
    const QString& filePath,
    int lineNumber,
    int columnNumber)
{
    const NavigationLocation current = targets.currentLocation();
    const NavigationLocation destination{
        filePath,
        targets.currentWorkspacePath(),
        lineNumber,
        columnNumber};

    if (!targets.activateOrOpenFile(filePath))
        return false;

    recordLocationBeforeNavigation(current, destination);

    if (lineNumber <= 0)
        return false;

    MyCodeEditor* editor = targets.currentEditor();
    if (!lineResolver.applyToEditor(editor, lineNumber, columnNumber))
        return false;
    if (editor)
        editor->flashLine(lineNumber);
    return true;
}

void NavigationCommandCoordinator::revealFileAndFlashLine(
    const QString& filePath,
    int lineNumber)
{
    if (!targets.activateOrOpenFile(filePath))
        return;
    if (lineNumber <= 0)
        return;

    MyCodeEditor* editor = targets.currentEditor();
    if (editor)
        editor->flashLine(lineNumber);
}

void NavigationCommandCoordinator::navigateEditorToLine(
    MyCodeEditor* editor,
    int lineNumber,
    int columnNumber)
{
    const DocumentSnapshot document =
        targets.tabManager ? targets.tabManager->getDocumentForEditor(editor)
                           : DocumentSnapshot();
    recordCurrentLocationBeforeNavigation(
        {document.fileName,
         targets.currentWorkspacePath(),
         lineNumber,
         columnNumber});
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

void NavigationCommandCoordinator::navigateBack()
{
    pruneHistoryForCurrentWorkspace();
    if (backStack.isEmpty())
        return;

    const NavigationLocation current = targets.currentLocation();
    const NavigationLocation target = backStack.takeLast();
    if (current.isValid() && !(current == target))
        forwardStack.append(current);

    replayingHistory = true;
    applyLocation(target);
    replayingHistory = false;
}

void NavigationCommandCoordinator::navigateForward()
{
    pruneHistoryForCurrentWorkspace();
    if (forwardStack.isEmpty())
        return;

    const NavigationLocation current = targets.currentLocation();
    const NavigationLocation target = forwardStack.takeLast();
    if (current.isValid() && !(current == target))
        backStack.append(current);

    replayingHistory = true;
    applyLocation(target);
    replayingHistory = false;
}

void NavigationCommandCoordinator::recordCurrentLocationBeforeNavigation(
    const NavigationLocation& destination)
{
    recordLocationBeforeNavigation(targets.currentLocation(), destination);
}

void NavigationCommandCoordinator::recordLocationBeforeNavigation(
    const NavigationLocation& current,
    const NavigationLocation& destination)
{
    if (replayingHistory || !current.isValid() || current == destination)
        return;
    if (current.workspacePath != destination.workspacePath)
        return;

    if (backStack.isEmpty() || !(backStack.last() == current))
        backStack.append(current);
    forwardStack.clear();
}

bool NavigationCommandCoordinator::applyLocation(
    const NavigationLocation& location)
{
    if (!location.isValid())
        return false;
    if (location.workspacePath != targets.currentWorkspacePath())
        return false;
    if (!targets.activateOrOpenFile(location.filePath))
        return false;
    return lineResolver.applyToEditor(targets.currentEditor(),
                                      location.lineNumber,
                                      location.columnNumber);
}

void NavigationCommandCoordinator::pruneHistoryForCurrentWorkspace()
{
    const QString workspacePath = targets.currentWorkspacePath();
    auto prune = [&workspacePath](QVector<NavigationLocation>& stack) {
        for (int i = stack.size() - 1; i >= 0; --i) {
            if (stack.at(i).workspacePath != workspacePath)
                stack.removeAt(i);
        }
    };
    prune(backStack);
    prune(forwardStack);
}
