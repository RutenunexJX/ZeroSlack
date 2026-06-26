#include "editorcoordinator.h"

#include "editorappearancesettings.h"
#include "formattersettings.h"
#include "editorsemanticcontextservice.h"
#include "definitionpreviewservice.h"
#include "filecommandcoordinator.h"
#include "modemanager.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QMessageBox>
#include <QMenu>
#include <QSaveFile>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <utility>

namespace {
QString sourceSymbolActionText(SourceSymbolAction action)
{
    switch (action) {
    case SourceSymbolAction::FindReferences:
        return QStringLiteral("Find References");
    case SourceSymbolAction::ShowRelationships:
        return QStringLiteral("Show Relationships");
    case SourceSymbolAction::ShowSignalKernelGraph:
        return QStringLiteral("Show Signal Kernel Graph");
    }
    return QString();
}

QString normalizedEditorCoordinatorFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

bool targetIsCurrentEditorFile(
    const DefinitionNavigationTarget& target,
    const EditorSemanticContext& context)
{
    if (target.fileName.isEmpty())
        return true;

    const QString targetFile = normalizedEditorCoordinatorFileName(target.fileName);
    const QString contextFile =
        normalizedEditorCoordinatorFileName(context.fileName);
    return !targetFile.isEmpty()
        && !contextFile.isEmpty()
        && targetFile == contextFile;
}

bool isIncludeCandidateFile(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == QStringLiteral("vh")
        || suffix == QStringLiteral("svh");
}

QString includeCompletionTextForFile(const QString& fileName,
                                     const QString& workspaceRoot)
{
    QString includeText = fileName;
    if (!workspaceRoot.isEmpty())
        includeText = QDir(workspaceRoot).relativeFilePath(fileName);
    return QDir::fromNativeSeparators(includeText);
}
}

EditorCoordinator::EditorCoordinator(TabManager* tabManager,
                                     ModeManager* modeManager,
                                     QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , modeManager(modeManager)
{
    semanticRuntime.init();
}

void EditorCoordinator::WorkflowDependencies::set(
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

QString EditorCoordinator::WorkflowDependencies::resolveIncludePath(
    const QString& includePath,
    const QString& currentFile) const
{
    return workspaceManager
        ? workspaceManager->resolveIncludePath(includePath, currentFile)
        : QString();
}

QStringList
EditorCoordinator::WorkflowDependencies::includeFileCompletionCandidates(
    const QString& currentFile) const
{
    QStringList candidates;
    if (!workspaceManager)
        return candidates;

    const QString workspaceRoot = workspaceManager->getWorkspacePath();
    const QString normalizedCurrent =
        normalizedEditorCoordinatorFileName(currentFile);
    for (const QString& fileName : workspaceManager->getAllFiles()) {
        if (!isIncludeCandidateFile(fileName))
            continue;

        const QString normalizedFile =
            normalizedEditorCoordinatorFileName(fileName);
        if (!normalizedCurrent.isEmpty()
            && normalizedFile == normalizedCurrent) {
            continue;
        }

        candidates.append(includeCompletionTextForFile(fileName, workspaceRoot));
    }

    candidates.removeDuplicates();
    candidates.sort(Qt::CaseInsensitive);
    return candidates;
}

void EditorCoordinator::WorkflowDependencies::executeAlternateCommand(
    MyCodeEditor* editor,
    const QString& command) const
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->executeAlternateCommandText(editor, command);
}

void EditorCoordinator::WorkflowDependencies::navigateEditorToLine(
    MyCodeEditor* editor,
    int line,
    int column) const
{
    if (navigationCommandCoordinator)
        navigationCommandCoordinator->navigateEditorToLine(editor, line, column);
}

void EditorCoordinator::WorkflowDependencies::navigateToFileAndLine(
    const QString& fileName,
    int line,
    int column) const
{
    if (navigationCommandCoordinator)
        navigationCommandCoordinator->navigateToFileAndLine(fileName, line, column);
}

void EditorCoordinator::WorkflowDependencies::navigateBack() const
{
    if (navigationCommandCoordinator)
        navigationCommandCoordinator->navigateBack();
}

void EditorCoordinator::WorkflowDependencies::navigateForward() const
{
    if (navigationCommandCoordinator)
        navigationCommandCoordinator->navigateForward();
}

void EditorCoordinator::WorkflowDependencies::showReferencesForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    if (semanticPanelRefresh)
        semanticPanelRefresh->showReferencesForSymbol(symbolName, fileName, moduleName);
}

void EditorCoordinator::WorkflowDependencies::showRelationshipsForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    if (semanticPanelRefresh)
        semanticPanelRefresh->showRelationshipsForSymbol(symbolName, fileName, moduleName);
}

void EditorCoordinator::WorkflowDependencies::showSignalKernelGraphForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath) const
{
    if (semanticPanelRefresh) {
        semanticPanelRefresh->showSignalKernelGraphForSymbol(symbolName,
                                                             fileName,
                                                             moduleName,
                                                             signalAccessPath);
    }
}

void EditorCoordinator::WorkflowDependencies::handleActiveEditorChanged(
    MyCodeEditor* editor) const
{
    if (semanticPanelRefresh)
        semanticPanelRefresh->handleActiveEditorChanged(editor);
}

bool EditorCoordinator::WorkflowDependencies::canNavigate() const
{
    return navigationCommandCoordinator != nullptr;
}

bool EditorCoordinator::WorkflowDependencies::hasSemanticPanelRefresh() const
{
    return semanticPanelRefresh != nullptr;
}

void EditorCoordinator::SemanticRuntime::init()
{
    service = EditorSemanticContextService::getInstance();
}

EditorSemanticContextService*
EditorCoordinator::SemanticRuntime::contextService() const
{
    return service
        ? service
        : EditorSemanticContextService::getInstance();
}

void EditorCoordinator::setWorkflowDependencies(
    WorkspaceManager* newWorkspaceManager,
    FileCommandCoordinator* newFileCommandCoordinator,
    NavigationCommandCoordinator* newNavigationCommandCoordinator,
    SemanticPanelRefreshCoordinator* newSemanticPanelRefresh)
{
    dependencies.set(newWorkspaceManager,
                     newFileCommandCoordinator,
                     newNavigationCommandCoordinator,
                     newSemanticPanelRefresh);
}

void EditorCoordinator::setAppearanceSettings(
    EditorAppearanceSettings* settings)
{
    if (appearanceSettings == settings)
        return;

    if (appearanceSettingsConnection)
        disconnect(appearanceSettingsConnection);

    appearanceSettings = settings;
    if (appearanceSettings) {
        appearanceSettingsConnection = connect(
            appearanceSettings,
            &EditorAppearanceSettings::settingsChanged,
            this,
            [this](const EditorAppearanceOptions&) {
                applyAppearanceToOpenEditors();
            });
    } else {
        appearanceSettingsConnection = QMetaObject::Connection();
    }

    applyAppearanceToOpenEditors();
}

void EditorCoordinator::setFormatterSettings(FormatterSettings* settings)
{
    if (formatterSettings == settings)
        return;

    if (formatterSettingsConnection)
        disconnect(formatterSettingsConnection);
    if (formatterFormatOnSaveConnection)
        disconnect(formatterFormatOnSaveConnection);

    formatterSettings = settings;
    if (formatterSettings) {
        formatterSettingsConnection = connect(
            formatterSettings,
            &FormatterSettings::settingsChanged,
            this,
            [this](FormatterProfile) {
                applyFormatterSettingsToOpenEditors();
            });
        formatterFormatOnSaveConnection = connect(
            formatterSettings,
            &FormatterSettings::formatOnSaveChanged,
            this,
            [this](bool) {
                applyFormatterSettingsToOpenEditors();
            });
    } else {
        formatterSettingsConnection = QMetaObject::Connection();
        formatterFormatOnSaveConnection = QMetaObject::Connection();
    }

    applyFormatterSettingsToOpenEditors();
}

void EditorCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void EditorCoordinator::setFoldShelfRequestedHandler(std::function<void()> handler)
{
    foldShelfRequestedHandler = std::move(handler);
}

void EditorCoordinator::setFoldShelfItemConsumedHandler(
    std::function<void(const QString&)> handler)
{
    foldShelfItemConsumedHandler = std::move(handler);
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

    DefinitionPreviewService::getInstance()->setDocumentModel(
        tabManager->getDocumentModel());
    signalsConnected = true;
}

void EditorCoordinator::attachEditor(MyCodeEditor* editor)
{
    if (!editor)
        return;

    editor->setSemanticContextService(contextService());
    editor->setIncludeFileCompletionProvider(
        [this](const QString& currentFile) {
            return dependencies.includeFileCompletionCandidates(currentFile);
        });
    editor->setIncludeNewHeaderCreator(
        [this](const IncludeNewHeaderRequest& request) {
            return createIncludeNewHeader(request);
        });
    applyAppearance(editor);
    applyFormatterSettings(editor);
    applyAlternateMode(editor);
    connect(editor, &MyCodeEditor::alternateCommandRequested,
            this, [this, editor](const QString& command) {
                dependencies.executeAlternateCommand(editor, command);
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
    connect(editor,
            &MyCodeEditor::definitionPreviewNavigationRequested,
            this,
            [this](const QString& fileName, int line, int column) {
                handleDefinitionPreviewNavigationRequested(
                    fileName,
                    line,
                    column);
            });
    connect(editor, &MyCodeEditor::navigationBackRequested,
            this, [this]() {
                dependencies.navigateBack();
            });
    connect(editor, &MyCodeEditor::navigationForwardRequested,
            this, [this]() {
                dependencies.navigateForward();
            });
    connect(editor, &MyCodeEditor::editorStatusMessageRequested,
            this, [this](const QString& message) {
                if (statusMessageHandler)
                    statusMessageHandler(message, message.isEmpty() ? 0 : 5000);
            });
    connect(editor, &MyCodeEditor::formatterProfileChanged,
            this, [this](FormatterProfile profile) {
                if (!formatterSettings || applyingFormatterSettings)
                    return;
                formatterSettings->setProfile(profile);
            });
    connect(editor, &MyCodeEditor::formatOnSaveChanged,
            this, [this](bool enabled) {
                if (!formatterSettings || applyingFormatterSettings)
                    return;
                formatterSettings->setFormatOnSaveEnabled(enabled);
            });
    connect(editor, &MyCodeEditor::foldShelfRequested,
            this, [this]() {
                if (foldShelfRequestedHandler)
                    foldShelfRequestedHandler();
            });
    connect(editor, &MyCodeEditor::foldShelfItemConsumed,
            this, [this](const QString& id) {
                if (foldShelfItemConsumedHandler)
                    foldShelfItemConsumedHandler(id);
            });
}

void EditorCoordinator::applyAlternateMode(MyCodeEditor* editor) const
{
    if (!editor || !modeManager)
        return;
    editor->setAlternateModeEnabled(modeManager->getCurrentMode() == ModeManager::AlternateMode);
}

void EditorCoordinator::applyAppearance(MyCodeEditor* editor) const
{
    if (!editor || !appearanceSettings)
        return;
    editor->applyAppearanceSettings(appearanceSettings->options());
}

void EditorCoordinator::applyFormatterSettings(MyCodeEditor* editor) const
{
    if (!editor || !formatterSettings)
        return;

    applyingFormatterSettings = true;
    editor->setFormatterProfile(formatterSettings->profile());
    editor->setFormatOnSaveEnabled(formatterSettings->formatOnSaveEnabled());
    applyingFormatterSettings = false;
}

EditorSemanticContextService* EditorCoordinator::contextService() const
{
    return semanticRuntime.contextService();
}

void EditorCoordinator::applyAppearanceToOpenEditors() const
{
    if (!tabManager)
        return;

    for (int i = 0; i < tabManager->editorCount(); ++i)
        applyAppearance(tabManager->getEditorAt(i));
}

void EditorCoordinator::applyFormatterSettingsToOpenEditors() const
{
    if (!tabManager)
        return;

    for (int i = 0; i < tabManager->editorCount(); ++i)
        applyFormatterSettings(tabManager->getEditorAt(i));
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

    const QString targetPath =
        dependencies.resolveIncludePath(includePath, currentFile);
    if (targetPath.isEmpty()) {
        QMessageBox::warning(editor,
                             tr("Include not found"),
                             tr("Can not locate include file:\n%1").arg(includePath));
        return;
    }

    if (dependencies.canNavigate())
        dependencies.navigateToFileAndLine(targetPath, -1, -1);
    else if (tabManager)
        tabManager->openFileInTab(targetPath);
}

IncludeNewHeaderResult EditorCoordinator::createIncludeNewHeader(
    const IncludeNewHeaderRequest& request) const
{
    IncludeNewHeaderResult result;
    if (!dependencies.workspaceManager
        || !dependencies.workspaceManager->isWorkspaceOpen()) {
        result.errorMessage = tr("Open a workspace before creating an include file.");
        return result;
    }

    const QString stem = request.fileStem.trimmed();
    const QString extension = request.extension.trimmed().toLower();
    if (stem.isEmpty()
        || (extension != QStringLiteral("vh")
            && extension != QStringLiteral("svh"))) {
        result.errorMessage = tr("Invalid include file name or format.");
        return result;
    }

    const QString workspaceRoot = dependencies.workspaceManager->getWorkspacePath();
    const QString fileName = stem + QLatin1Char('.') + extension;
    const QString filePath = QDir(workspaceRoot).absoluteFilePath(fileName);
    const QFileInfo fileInfo(filePath);
    if (fileInfo.exists()) {
        result.errorMessage =
            tr("Include file already exists: %1").arg(fileName);
        return result;
    }

    QString body = request.templateBody;
    int cursorPosition = body.indexOf(request.cursorToken);
    if (cursorPosition >= 0)
        body.remove(cursorPosition, request.cursorToken.size());
    else
        cursorPosition = body.size();

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.errorMessage =
            tr("Can not create include file: %1").arg(fileName);
        return result;
    }
    file.write(body.toUtf8());
    if (!file.commit()) {
        result.errorMessage =
            tr("Can not save include file: %1").arg(fileName);
        return result;
    }

    if (tabManager && tabManager->openFileInTab(filePath)) {
        MyCodeEditor* openedEditor = tabManager->getCurrentEditor();
        if (openedEditor) {
            QTextCursor cursor(openedEditor->document());
            const int maxPosition =
                std::max(0, openedEditor->document()->characterCount() - 1);
            cursor.setPosition(std::clamp(cursorPosition, 0, maxPosition));
            openedEditor->setTextCursor(cursor);
            openedEditor->setFocus();
        }
    } else {
        result.errorMessage =
            tr("Created include file but could not open it: %1").arg(fileName);
        return result;
    }

    result.success = true;
    result.includePath = QDir::fromNativeSeparators(fileName);
    result.filePath = QDir::fromNativeSeparators(filePath);
    result.cursorPosition = cursorPosition;
    return result;
}

void EditorCoordinator::handleDefinitionNavigationRequested(
    MyCodeEditor* editor,
    const QString& symbolName,
    const EditorSemanticContext& context) const
{
    if (!dependencies.canNavigate() || symbolName.isEmpty())
        return;

    const DefinitionNavigationTarget target =
        contextService()->resolveDefinitionTarget(
            symbolName, context);
    if (!target.found)
        return;

    if (target.localFile && targetIsCurrentEditorFile(target, context)) {
        dependencies.navigateEditorToLine(editor, target.line, target.column);
        return;
    }

    const QString targetFile =
        target.fileName.isEmpty() ? context.fileName : target.fileName;
    dependencies.navigateToFileAndLine(targetFile, target.line, target.column);
}

void EditorCoordinator::handleDefinitionPreviewNavigationRequested(
    const QString& fileName,
    int line,
    int column) const
{
    if (!dependencies.canNavigate() || fileName.isEmpty() || line <= 0)
        return;

    dependencies.navigateToFileAndLine(fileName, line, column);
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
    if (!dependencies.hasSemanticPanelRefresh())
        return;

    const EditorSourceSymbolActionRequestState requestState =
        contextService()->sourceSymbolActionRequestState(action, context);
    if (!requestState.available)
        return;

    switch (requestState.action) {
    case SourceSymbolAction::FindReferences:
        dependencies.showReferencesForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    case SourceSymbolAction::ShowRelationships:
        dependencies.showRelationshipsForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    case SourceSymbolAction::ShowSignalKernelGraph:
        dependencies.showSignalKernelGraphForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName,
            requestState.signalAccessPath);
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
    if (tabManager) {
        for (int i = 0; i < tabManager->editorCount(); ++i) {
            if (MyCodeEditor* openEditor = tabManager->getEditorAt(i)) {
                openEditor->cancelFoldRegionMarkMode();
                openEditor->cancelFoldShelfMode();
            }
        }
    }
    applyAlternateMode(editor);
    dependencies.handleActiveEditorChanged(editor);
}
