#include "semanticpanelrefreshcoordinator.h"

#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "rtlinsightspanelcoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QTextCursor>

#include <utility>

namespace {

QString normalizedPanelNavigationFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool pathIsInsideWorkspace(const QString& fileName,
                           const QString& workspacePath)
{
    const QString target = normalizedPanelNavigationFileName(fileName);
    const QString workspace = normalizedPanelNavigationFileName(workspacePath);
    if (target.isEmpty() || workspace.isEmpty())
        return false;
    return target == workspace
        || target.startsWith(workspace + QLatin1Char('/'),
                             Qt::CaseInsensitive);
}

bool fileKnownToWorkspace(WorkspaceManager* workspaceManager,
                          const QString& fileName)
{
    if (!workspaceManager)
        return true;
    const QString target = normalizedPanelNavigationFileName(fileName);
    for (const QString& file : workspaceManager->getAllFiles()) {
        if (QString::compare(normalizedPanelNavigationFileName(file),
                             target,
                             Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool pathIsInsideInactiveWorkspace(WorkspaceManager* workspaceManager,
                                   const QString& fileName)
{
    if (!workspaceManager)
        return false;
    const int activeIndex = workspaceManager->activeWorkspaceIndex();
    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    for (int i = 0; i < entries.size(); ++i) {
        if (i == activeIndex)
            continue;
        if (pathIsInsideWorkspace(fileName, entries.at(i).path))
            return true;
    }
    return false;
}

QString panelNavigationFailureReason(WorkspaceManager* workspaceManager,
                                     const QString& fileName,
                                     int line,
                                     int column)
{
    if (fileName.isEmpty() || !QFileInfo::exists(fileName))
        return QStringLiteral("file missing");
    if (line <= 0 || column < -1)
        return QStringLiteral("invalid line/column");
    if (workspaceManager && workspaceManager->isWorkspaceOpen()
        && !pathIsInsideWorkspace(fileName, workspaceManager->getWorkspacePath())
        && !fileKnownToWorkspace(workspaceManager, fileName)
        && pathIsInsideInactiveWorkspace(workspaceManager, fileName)) {
        return QStringLiteral("workspace mismatch");
    }
    return QString();
}

}

SemanticPanelRefreshCoordinator::SemanticPanelRefreshCoordinator(
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    NavigationManager* navigationManager,
    NavigationCommandCoordinator* navigationCommandCoordinator,
    ProblemsPanelCoordinator* problemsPanel,
    ReferencesPanelCoordinator* referencesPanel,
    RelationshipsPanelCoordinator* relationshipsPanel,
    RtlInsightsPanelCoordinator* rtlInsightsPanel,
    SignalKernelGraphPanelCoordinator* signalKernelGraphPanel)
{
    dependencies.set(tabManager,
                     workspaceManager,
                     navigationManager,
                     navigationCommandCoordinator);
    panels.set(problemsPanel,
               referencesPanel,
               relationshipsPanel,
               rtlInsightsPanel,
               signalKernelGraphPanel);
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

bool SemanticPanelRefreshCoordinator::ContextDependencies::navigateToFileAndLine(
    const QString& fileName,
    int line,
    int column) const
{
    return navigationCommandCoordinator
        && navigationCommandCoordinator->navigateToFileAndLineAndFlash(
            fileName,
            line,
            column);
}

bool SemanticPanelRefreshCoordinator::ContextDependencies::navigateToFileAndLineAndFlash(
    const QString& fileName,
    int line,
    int column) const
{
    return navigationCommandCoordinator
        && navigationCommandCoordinator->navigateToFileAndLineAndFlash(
            fileName,
            line,
            column);
}

void SemanticPanelRefreshCoordinator::ContextDependencies::revealFileAndFlashLine(
    const QString& fileName,
    int line) const
{
    if (navigationCommandCoordinator)
        navigationCommandCoordinator->revealFileAndFlashLine(fileName, line);
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
            return navigateToFileAndLine(fileName, line, column);
        };
    const ProblemsNavigationHandler problemsNavigationHandler =
        [this](const QString& fileName, int line, int column) {
            return navigateToFileAndLineAndFlash(fileName, line, column);
        };
    const NavigationHandler revealHandler =
        [this](const QString& fileName, int line, int) {
            revealFileAndFlashLine(fileName, line);
            return true;
        };
    const StatusMessageHandler statusMessageHandler =
        [this](const QString& message, int timeoutMs) {
            showStatusMessage(message, timeoutMs);
        };

    panels.configureProblemsPanel(currentFileProvider,
                                  workspaceFilesProvider,
                                  problemsNavigationHandler,
                                  statusMessageHandler);
    panels.configureReferencesPanel(workspaceFilesProvider,
                                    navigationHandler,
                                    statusMessageHandler);
    panels.configureRelationshipsPanel(navigationHandler,
                                       [this](const QString& symbolName,
                                              const QString& fileName,
                                              const QString& moduleName) {
                                           showSignalKernelGraphForSymbol(
                                               symbolName,
                                               fileName,
                                               moduleName);
                                       },
                                       [this](const QString& symbolName,
                                              const QString& fileName,
                                              const QString& moduleName) {
                                           showStateTransitionGraphForSymbol(
                                               symbolName,
                                               fileName,
                                               moduleName);
                                       },
                                       [this](const QString& fileName,
                                              const QString& moduleName) {
                                           showModuleBlockDiagramForSymbol(
                                               moduleName,
                                               fileName,
                                               moduleName);
                                       },
                                       statusMessageHandler);
    panels.configureRtlInsightsPanel(navigationHandler,
                                     statusMessageHandler);
    panels.configureSignalKernelGraphPanel(
        dependencies.tabManager ? dependencies.tabManager->getDocumentModel() : nullptr,
        navigationHandler,
        revealHandler,
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

void SemanticPanelRefreshCoordinator::showSignalKernelGraphForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath)
{
    panels.showSignalKernelGraphForSymbol(symbolName,
                                          fileName,
                                          moduleName,
                                          signalAccessPath);
}

void SemanticPanelRefreshCoordinator::showSignalUsageHotspotForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath)
{
    panels.showSignalUsageHotspotForSymbol(symbolName,
                                           fileName,
                                           moduleName,
                                           signalAccessPath);
}

void SemanticPanelRefreshCoordinator::showStateTransitionGraphForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName)
{
    panels.showStateTransitionGraphForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::showModuleBlockDiagramForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName)
{
    panels.showModuleBlockDiagramForSymbol(symbolName, fileName, moduleName);
}

void SemanticPanelRefreshCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    dependencies.handleActiveEditorChanged(editor);
    panels.updateRtlInsightsPanel(editor ? editor->documentFileName() : QString(),
                                  editor ? editor->currentModuleName() : QString(),
                                  currentEditorWord(editor));
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

QString SemanticPanelRefreshCoordinator::currentEditorWord(MyCodeEditor* editor) const
{
    if (!editor)
        return QString();

    QTextCursor cursor = editor->textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText().trimmed();
}

bool SemanticPanelRefreshCoordinator::navigateToFileAndLine(
    const QString& fileName,
    int line,
    int column) const
{
    const QString failureReason =
        panelNavigationFailureReason(dependencies.workspaceManager,
                                     fileName,
                                     line,
                                     column);
    if (!failureReason.isEmpty()) {
        showStatusMessage(QStringLiteral("Navigation failed: %1")
                              .arg(failureReason),
                          4000);
        return false;
    }
    if (!dependencies.navigateToFileAndLine(fileName, line, column)) {
        showStatusMessage(
            QStringLiteral("Navigation failed: stale semantic snapshot or symbol no longer exists"),
            4000);
        return false;
    }
    return true;
}

bool SemanticPanelRefreshCoordinator::navigateToFileAndLineAndFlash(
    const QString& fileName,
    int line,
    int column) const
{
    return dependencies.navigateToFileAndLineAndFlash(fileName, line, column);
}

void SemanticPanelRefreshCoordinator::revealFileAndFlashLine(
    const QString& fileName,
    int line) const
{
    dependencies.revealFileAndFlashLine(fileName, line);
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
