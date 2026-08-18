#include "editorcoordinator.h"

#include "actionregistry.h"
#include "editorappearancesettings.h"
#include "formattersettings.h"
#include "editoractioncontextservice.h"
#include "editorcontextmenumodel.h"
#include "editorsemanticcontextservice.h"
#include "exposesignaltotoppreview.h"
#include "exposesignaltotopservice.h"
#include "definitionpreviewservice.h"
#include "filecommandcoordinator.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "packagetoolservice.h"
#include "semanticpanelrefreshcoordinator.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "workspacemanager.h"
#include "workspaceeditdocumentmanager.h"

#include <QActionGroup>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QIODevice>
#include <QKeySequence>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QTextCursor>

#include <utility>

namespace {
QString sourceSymbolActionId(SourceSymbolAction action)
{
    switch (action) {
    case SourceSymbolAction::GoToDefinition:
        return QStringLiteral("source.goToDefinition");
    case SourceSymbolAction::ShowSignalKernelGraph:
        return QStringLiteral("insight.signalKernelGraph");
    case SourceSymbolAction::ShowSignalUsageHotspot:
        return QStringLiteral("insight.signalUsageHotspot");
    case SourceSymbolAction::ShowStateTransitionGraph:
        return QStringLiteral("insight.stateTransitionGraph");
    case SourceSymbolAction::ShowModuleBlockDiagram:
        return QStringLiteral("insight.moduleBlockDiagram");
    }
    return QString();
}

QString exposeSignalToTopActionText()
{
    const ActionDescriptor* descriptor =
        findActionById(
            QStringLiteral("refactor.exposeSignalToTop"));
    return descriptor
        ? descriptor->canonicalName
        : QStringLiteral("Expose Signal to Top");
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

QVariantMap withEditorActionTarget(
    MyCodeEditor* editor,
    QVariantMap parameters = {})
{
    if (!editor)
        return parameters;
    const QString viewId =
        editor->property("editorViewId").toString();
    if (!viewId.isEmpty()) {
        parameters.insert(
            QStringLiteral("editorViewId"), viewId);
    }
    return parameters;
}

}

EditorCoordinator::EditorCoordinator(TabManager* tabManager,
                                     QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , ownedActionContextService(
          std::make_unique<EditorActionContextService>())
    , actionContextService(ownedActionContextService.get())
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

    QStringList includeDirs =
        workspaceManager->workspaceConfiguration().includeDirs;
    includeDirs.append(workspaceManager->getWorkspacePath());
    const QList<HeaderIncludeCandidate> headerCandidates =
        PackageToolService::headerIncludeCandidates(
            workspaceManager->getAllFiles(),
            currentFile,
            includeDirs);
    candidates.reserve(headerCandidates.size());
    for (const HeaderIncludeCandidate& candidate : headerCandidates)
        candidates.append(candidate.includePath);
    return candidates;
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

void EditorCoordinator::WorkflowDependencies::navigateToFileAndLineWithContext(
    const QString& fileName,
    int line,
    int column,
    const HierarchyInstanceContext& instanceContext) const
{
    if (navigationCommandCoordinator) {
        navigationCommandCoordinator->navigateToFileAndLineWithContext(
            fileName,
            line,
            column,
            instanceContext);
    }
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

void EditorCoordinator::WorkflowDependencies::showSignalUsageHotspotForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName,
    const QString& signalAccessPath) const
{
    if (semanticPanelRefresh) {
        semanticPanelRefresh->showSignalUsageHotspotForSymbol(symbolName,
                                                              fileName,
                                                              moduleName,
                                                              signalAccessPath);
    }
}

void EditorCoordinator::WorkflowDependencies::showStateTransitionGraphForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    if (semanticPanelRefresh) {
        semanticPanelRefresh->showStateTransitionGraphForSymbol(symbolName,
                                                                fileName,
                                                                moduleName);
    }
}

void EditorCoordinator::WorkflowDependencies::showModuleBlockDiagramForSymbol(
    const QString& symbolName,
    const QString& fileName,
    const QString& moduleName) const
{
    if (semanticPanelRefresh) {
        semanticPanelRefresh->showModuleBlockDiagramForSymbol(symbolName,
                                                              fileName,
                                                              moduleName);
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

    formatterSettings = settings;
    if (formatterSettings) {
        formatterSettingsConnection = connect(
            formatterSettings,
            &FormatterSettings::settingsChanged,
            this,
            [this](FormatterProfile) {
                applyFormatterSettingsToOpenEditors();
            });
    } else {
        formatterSettingsConnection = QMetaObject::Connection();
    }

    applyFormatterSettingsToOpenEditors();
}

void EditorCoordinator::setAnnotationDisplayOptions(
    const EditorAnnotationDisplayOptions& options)
{
    const EditorAnnotationDisplayOptions normalized =
        options.normalized();
    if (annotationDisplayOptions == normalized)
        return;
    annotationDisplayOptions = normalized;
    applyAnnotationDisplayOptionsToOpenEditors();
}

void EditorCoordinator::setStatusMessageHandler(
    std::function<void(const QString&, int)> handler)
{
    statusMessageHandler = std::move(handler);
}

void EditorCoordinator::setModeStateHandler(
    std::function<void(const EditorModeSnapshot&)> handler)
{
    modeStateHandler = std::move(handler);
}

void EditorCoordinator::setActionContextService(
    EditorActionContextService* service)
{
    actionContextService =
        service ? service : ownedActionContextService.get();
}

void EditorCoordinator::setActionContextQueryProvider(
    std::function<EditorActionContextQuery(
        const EditorSemanticContext&)> provider)
{
    actionContextQueryProvider = std::move(provider);
}

void EditorCoordinator::setRegisteredActionRequestHandler(
    std::function<void(const QString&,
                       const QVariantMap&)> handler)
{
    registeredActionRequestHandler =
        std::move(handler);
}

void EditorCoordinator::setFoldShelfItemConsumedHandler(
    std::function<void(const QString&)> handler)
{
    foldShelfItemConsumedHandler = std::move(handler);
}

void EditorCoordinator::connectSignals()
{
    if (signalsConnected || !tabManager)
        return;

    connect(tabManager, &TabManager::tabCreated,
            this, &EditorCoordinator::attachEditor);
    connect(tabManager,
            &TabManager::auxiliaryViewCreated,
            this,
            &EditorCoordinator::attachEditor);
    connect(tabManager, &TabManager::activeTabChanged,
            this, &EditorCoordinator::handleActiveEditorChanged);
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
    applyAnnotationDisplayOptions(editor);
    connect(editor, &MyCodeEditor::sourceNavigationRequested,
            this, [this, editor](const EditorSourceNavigationTarget& target,
                                 const EditorSemanticContext& context) {
                handleSourceNavigationRequested(editor, target, context);
            });
    connect(editor, &MyCodeEditor::sourceSymbolActionRequested,
            this, [this, editor](SourceSymbolAction action,
                                 const EditorSemanticContext& context) {
                if (registeredActionRequestHandler) {
                    QVariantMap parameters;
                    parameters.insert(
                        QStringLiteral("cursorPosition"),
                        context.cursorPosition);
                    registeredActionRequestHandler(
                        sourceSymbolActionId(action),
                        withEditorActionTarget(
                            editor, parameters));
                    return;
                }
                handleSourceSymbolActionRequested(
                    action, context);
            });
    connect(editor,
            &MyCodeEditor::registeredActionRequested,
            this,
            [this, editor](const QString& actionId,
                           const QVariantMap& parameters,
                           bool* handled) {
                if (handled)
                    *handled = false;
                if (!registeredActionRequestHandler)
                    return;
                if (handled)
                    *handled = true;
                registeredActionRequestHandler(
                    actionId,
                    withEditorActionTarget(
                        editor, parameters));
            });
    connect(editor, &MyCodeEditor::sourceSymbolContextMenuRequested,
            this, [this, editor](QMenu* menu,
                                const EditorSemanticContext& context) {
                handleSourceSymbolContextMenuRequested(
                    menu, editor, context);
            });
    connect(editor,
            &MyCodeEditor::definitionPreviewNavigationRequested,
            this,
            [this, editor](const QString& fileName, int line, int column) {
                handleDefinitionPreviewNavigationRequested(
                    fileName,
                    line,
                    column,
                    editor->hierarchyInstanceContext());
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
    connect(editor,
            &MyCodeEditor::editorModeStateChanged,
            this,
            [this, editor](const EditorModeSnapshot& snapshot) {
                if (modeStateHandler
                    && tabManager
                    && tabManager->getCurrentEditor() == editor) {
                    modeStateHandler(snapshot);
                }
            });
    connect(editor, &MyCodeEditor::formatterProfileChanged,
            this, [this](FormatterProfile profile) {
                if (!formatterSettings || applyingFormatterSettings)
                    return;
                formatterSettings->setProfile(profile);
            });
    connect(editor, &MyCodeEditor::fontZoomRequested,
            this, [this](int steps) {
                if (!appearanceSettings || steps == 0)
                    return;

                const EditorAppearanceOptions options =
                    appearanceSettings->options();
                appearanceSettings->setFontSizePt(
                    options.fontSizePt + steps);
            });
    connect(editor, &MyCodeEditor::foldShelfItemConsumed,
            this, [this](const QString& id) {
                if (foldShelfItemConsumedHandler)
                    foldShelfItemConsumedHandler(id);
            });
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
    applyingFormatterSettings = false;
}

void EditorCoordinator::applyAnnotationDisplayOptions(
    MyCodeEditor* editor) const
{
    if (!editor)
        return;
    editor->setAnnotationDisplayOptions(
        annotationDisplayOptions);
}

EditorSemanticContextService* EditorCoordinator::contextService() const
{
    return semanticRuntime.contextService();
}

void EditorCoordinator::applyAppearanceToOpenEditors() const
{
    if (!tabManager)
        return;

    for (MyCodeEditor* editor :
         tabManager->openEditors()) {
        applyAppearance(editor);
    }
    for (MyCodeEditor* editor :
         tabManager->auxiliaryViews()) {
        applyAppearance(editor);
    }
}

void EditorCoordinator::applyFormatterSettingsToOpenEditors() const
{
    if (!tabManager)
        return;

    for (MyCodeEditor* editor :
         tabManager->openEditors()) {
        applyFormatterSettings(editor);
    }
    for (MyCodeEditor* editor :
         tabManager->auxiliaryViews()) {
        applyFormatterSettings(editor);
    }
}

void EditorCoordinator::
applyAnnotationDisplayOptionsToOpenEditors() const
{
    if (!tabManager)
        return;

    for (MyCodeEditor* editor :
         tabManager->openEditors()) {
        applyAnnotationDisplayOptions(editor);
    }
    for (MyCodeEditor* editor :
         tabManager->auxiliaryViews()) {
        applyAnnotationDisplayOptions(editor);
    }
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

    if (dependencies.canNavigate()) {
        dependencies.navigateToFileAndLineWithContext(
            targetPath,
            -1,
            -1,
            editor ? editor->hierarchyInstanceContext()
                   : HierarchyInstanceContext());
    }
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
    QString targetDirectory = workspaceRoot;
    const QString currentFile =
        normalizedEditorCoordinatorFileName(request.currentFileName);
    if (!currentFile.isEmpty()) {
        const QFileInfo currentInfo(currentFile);
        const QString currentDirectory =
            currentInfo.absoluteDir().absolutePath();
        if (!currentDirectory.isEmpty())
            targetDirectory = currentDirectory;
    }

    const QString filePath = QDir(targetDirectory).absoluteFilePath(fileName);
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
    dependencies.navigateToFileAndLineWithContext(
        targetFile,
        target.line,
        target.column,
        context.hierarchyInstance);
}

void EditorCoordinator::handleDefinitionPreviewNavigationRequested(
    const QString& fileName,
    int line,
    int column,
    const HierarchyInstanceContext& instanceContext) const
{
    if (!dependencies.canNavigate() || fileName.isEmpty() || line <= 0)
        return;

    dependencies.navigateToFileAndLineWithContext(fileName,
                                                  line,
                                                  column,
                                                  instanceContext);
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
    QString failureReason;
    if (!executeSourceSymbolActionRequested(
            action, context, &failureReason)
        && statusMessageHandler) {
        statusMessageHandler(
            failureReason.isEmpty()
                ? QStringLiteral("Symbol action unavailable")
                : failureReason,
            3000);
    }
}

bool EditorCoordinator::executeSourceSymbolActionRequested(
    SourceSymbolAction action,
    const EditorSemanticContext& context,
    QString* failureReason) const
{
    const auto fail = [failureReason](const QString& reason) {
        if (failureReason)
            *failureReason = reason;
        return false;
    };
    const EditorSourceSymbolActionRequestState requestState =
        contextService()->sourceSymbolActionRequestState(action, context);
    if (!requestState.available) {
        return fail(
            requestState.unavailableReason.isEmpty()
                ? QStringLiteral("Symbol action unavailable")
                : requestState.unavailableReason);
    }

    switch (requestState.action) {
    case SourceSymbolAction::GoToDefinition: {
        if (!dependencies.canNavigate())
            return fail(QStringLiteral("Navigation unavailable"));
        const DefinitionNavigationTarget target =
            contextService()->resolveDefinitionTarget(
                requestState.symbolName,
                context);
        if (!target.found)
            return fail(QStringLiteral("Symbol not indexed"));
        const QString targetFile =
            target.fileName.isEmpty() ? context.fileName : target.fileName;
        dependencies.navigateToFileAndLineWithContext(
            targetFile,
            target.line,
            target.column,
            context.hierarchyInstance);
        break;
    }
    case SourceSymbolAction::ShowSignalKernelGraph:
        if (!dependencies.hasSemanticPanelRefresh())
            return fail(QStringLiteral(
                "Signal Kernel Graph is unavailable"));
        dependencies.showSignalKernelGraphForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName,
            requestState.signalAccessPath);
        break;
    case SourceSymbolAction::ShowSignalUsageHotspot:
        if (!dependencies.hasSemanticPanelRefresh())
            return fail(QStringLiteral(
                "Signal Usage Hotspot is unavailable"));
        dependencies.showSignalUsageHotspotForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName,
            requestState.signalAccessPath);
        break;
    case SourceSymbolAction::ShowStateTransitionGraph:
        if (!dependencies.hasSemanticPanelRefresh())
            return fail(QStringLiteral(
                "State Transition Graph is unavailable"));
        dependencies.showStateTransitionGraphForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    case SourceSymbolAction::ShowModuleBlockDiagram:
        if (!dependencies.hasSemanticPanelRefresh())
            return fail(QStringLiteral(
                "Module Block Diagram is unavailable"));
        dependencies.showModuleBlockDiagramForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    }
    if (failureReason)
        failureReason->clear();
    return true;
}

ActionExecutionResult EditorCoordinator::executeRegisteredSourceAction(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation) const
{
    ActionExecutionResult result;
    result.handled = true;
    const auto fail = [&result](const QString& reason) {
        result.failureReason = reason;
        return result;
    };
    MyCodeEditor* editor = tabManager
        ? tabManager->getCurrentEditor()
        : nullptr;
    if (!editor)
        return fail(QStringLiteral("No editor tab is available."));

    SourceSymbolAction action =
        SourceSymbolAction::GoToDefinition;
    const QString& route = descriptor.executionRoute;
    if (route == QStringLiteral(
                     "editor.source.goToDefinition")) {
        action = SourceSymbolAction::GoToDefinition;
    } else if (route == QStringLiteral(
                            "insight.signalKernel.showSymbol")) {
        action = SourceSymbolAction::ShowSignalKernelGraph;
    } else if (route == QStringLiteral(
                            "insight.signalUsageHotspot.showSymbol")) {
        action = SourceSymbolAction::ShowSignalUsageHotspot;
    } else if (route == QStringLiteral(
                            "insight.stateTransition.showSymbol")) {
        action = SourceSymbolAction::ShowStateTransitionGraph;
    } else if (route == QStringLiteral(
                            "insight.moduleBlock.showSymbol")) {
        action = SourceSymbolAction::ShowModuleBlockDiagram;
    } else {
        return fail(QStringLiteral(
            "The editor source Action route is not registered."));
    }

    const int cursorPosition =
        invocation.parameters
            .value(QStringLiteral("cursorPosition"),
                   editor->textCursor().position())
            .toInt();
    const EditorSemanticContext context =
        editor->editorSemanticContextForPosition(
            cursorPosition, true);
    QString failureReason;
    if (!executeSourceSymbolActionRequested(
            action, context, &failureReason)) {
        return fail(failureReason);
    }
    result.succeeded = true;
    result.hasResolvedParameters = true;
    result.resolvedParameters.clear();
    return result;
}

ActionExecutionResult
EditorCoordinator::executeRegisteredExposeSignalAction(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation) const
{
    ActionExecutionResult result;
    result.handled = true;
    result.dryRun =
        invocation.mode == ActionExecutionMode::DryRun;
    const auto fail = [&result, &descriptor](
                          const QString& reason) {
        result.failureReason = reason.isEmpty()
            ? descriptor.unavailableReason
            : reason;
        return result;
    };
    if (!tabManager)
        return fail(QStringLiteral("No editor tab is available."));
    MyCodeEditor* parentEditor =
        tabManager->getCurrentEditor();
    if (!parentEditor)
        return fail(QStringLiteral("No editor tab is available."));

    const int cursorPosition =
        invocation.parameters
            .value(QStringLiteral("cursorPosition"),
                   parentEditor->textCursor().position())
            .toInt();
    EditorSemanticContext effectiveContext =
        parentEditor->editorSemanticContextForPosition(
            cursorPosition, true);
    const EditorActionContext actionContext =
        actionContextFor(effectiveContext);
    if (actionContext.hierarchyBound()) {
        effectiveContext.hierarchyInstance =
            actionContext.resolvedHierarchy;
    } else if (!actionContext
                    .hierarchyCandidates.isEmpty()) {
        int selectedIndex = -1;
        const QString rememberedInstance =
            invocation.parameters
                .value(QStringLiteral("instancePath"))
                .toString();
        const QString rememberedTop =
            invocation.parameters
                .value(QStringLiteral("activeTopModule"))
                .toString();
        for (int index = 0;
             index < actionContext
                         .hierarchyCandidates.size();
             ++index) {
            const HierarchyInstanceContext binding =
                actionContext.hierarchyCandidates
                    .at(index)
                    .binding();
            if (!rememberedInstance.isEmpty()
                && binding.instancePath
                       == rememberedInstance
                && (rememberedTop.isEmpty()
                    || binding.activeTopModule
                           == rememberedTop)) {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex < 0) {
            QStringList choices;
            choices.reserve(
                actionContext
                    .hierarchyCandidates.size());
            for (const EditorHierarchyBindingCandidate& candidate :
                 actionContext.hierarchyCandidates) {
                choices.append(candidate.displayText());
            }
            bool accepted = false;
            const QString selected =
                QInputDialog::getItem(
                    parentEditor,
                    QStringLiteral(
                        "Select hierarchy instance"),
                    QStringLiteral(
                        "Active top / instance:"),
                    choices,
                    0,
                    false,
                    &accepted);
            if (!accepted) {
                return fail(QStringLiteral(
                    "Expose Signal to Top was cancelled."));
            }
            selectedIndex = choices.indexOf(selected);
        }
        if (selectedIndex < 0
            || selectedIndex
                   >= actionContext
                          .hierarchyCandidates.size()) {
            return fail(QStringLiteral(
                "No exact hierarchy instance was selected."));
        }
        effectiveContext.hierarchyInstance =
            actionContext.hierarchyCandidates
                .at(selectedIndex)
                .binding();
        parentEditor->setHierarchyInstanceContext(
            effectiveContext.hierarchyInstance);
    } else {
        return fail(
            actionContext
                    .hierarchyResolutionReason
                    .isEmpty()
                ? QStringLiteral(
                      "No active top / instance can be resolved for the current module.")
                : actionContext
                      .hierarchyResolutionReason);
    }

    ExposeSignalToTopService service;
    QString unavailableReason;
    if (!service.canOffer(
            effectiveContext, &unavailableReason)) {
        return fail(
            unavailableReason.isEmpty()
                ? QStringLiteral(
                      "The selected object cannot be exposed to the active top.")
                : unavailableReason);
    }

    TSDocument syntax;
    syntax.setText(effectiveContext.documentText);
    const TSIdentifierTarget identifier =
        syntax.identifierAt(
            effectiveContext.cursorPosition);
    if (!identifier.ok()) {
        return fail(QStringLiteral(
            "Select a SystemVerilog signal identifier."));
    }

    ExposeSignalToTopQuery query;
    query.context = effectiveContext;
    query.exportedPortName =
        invocation.parameters
            .value(QStringLiteral("exportedPortName"))
            .toString()
            .trimmed();
    if (query.exportedPortName.isEmpty()) {
        query.exportedPortName =
            ExposeSignalToTopService::
                defaultExportedPortName(
                    identifier.text);
    }
    if (dependencies.workspaceManager) {
        for (const QString& file :
             dependencies.workspaceManager
                 ->getSystemVerilogFiles()) {
            query.workspaceFiles.insert(file);
        }
    }

    WorkspaceEditDocumentManager documents(tabManager);
    ExposeSignalToTopReport initial =
        service.plan(query, documents);
    if (initial.exportedPortName.isEmpty()) {
        initial.exportedPortName =
            query.exportedPortName;
    }

    ExposeSignalToTopPreview preview(
        initial,
        [&](const QString& portName) {
            ExposeSignalToTopQuery replanned = query;
            replanned.exportedPortName = portName;
            ExposeSignalToTopReport report =
                service.plan(replanned, documents);
            if (report.exportedPortName.isEmpty())
                report.exportedPortName = portName;
            return report;
        },
        parentEditor,
        result.dryRun
            ? ExposeSignalToTopPreview::Mode::PreviewOnly
            : ExposeSignalToTopPreview::Mode::ReviewAndApply);
    if (preview.exec()
        != ExposeSignalToTopPreview::Result::Accepted) {
        return fail(QStringLiteral(
            "Expose Signal to Top was cancelled."));
    }
    const ExposeSignalToTopReport& reviewed =
        preview.reportForApply();
    if (!reviewed.ready()) {
        return fail(
            reviewed.message.isEmpty()
                ? QStringLiteral(
                      "The Expose Signal to Top plan is blocked.")
                : reviewed.message);
    }

    result.hasResolvedParameters = true;
    result.resolvedParameters.insert(
        QStringLiteral("exportedPortName"),
        reviewed.exportedPortName);
    result.resolvedParameters.insert(
        QStringLiteral("activeTopModule"),
        effectiveContext
            .hierarchyInstance.activeTopModule);
    result.resolvedParameters.insert(
        QStringLiteral("instancePath"),
        effectiveContext
            .hierarchyInstance.instancePath);
    result.output.insert(
        QStringLiteral("exportedPortName"),
        reviewed.exportedPortName);
    result.output.insert(
        QStringLiteral("affectedFiles"),
        reviewed.affectedFiles);
    result.output.insert(
        QStringLiteral("renderedDiff"),
        reviewed.renderedDiff);

    if (result.dryRun) {
        result.succeeded = true;
        result.message = QStringLiteral(
            "Expose Signal to Top plan preview completed; no files were changed.");
        return result;
    }

    const ExposeSignalToTopApplyReport applied =
        service.apply(reviewed, documents);
    if (!applied.applied()) {
        return fail(
            applied.message.isEmpty()
                ? QStringLiteral(
                      "The workspace changed before Apply; no files were committed.")
                : applied.message);
    }
    result.succeeded = true;
    result.message =
        QStringLiteral("Exposed %1 to %2 as %3")
            .arg(identifier.text,
                 effectiveContext
                     .hierarchyInstance.activeTopModule,
                 reviewed.exportedPortName);
    if (statusMessageHandler)
        statusMessageHandler(result.message, 5000);
    return result;
}

EditorActionContext EditorCoordinator::actionContextFor(
    const EditorSemanticContext& context) const
{
    EditorActionContextQuery query;
    if (actionContextQueryProvider)
        query = actionContextQueryProvider(context);
    else
        query.editorContext = context;
    return actionContextService
        ? actionContextService->resolve(query)
        : EditorActionContext();
}

void EditorCoordinator::handleSourceSymbolContextMenuRequested(
    QMenu* menu,
    MyCodeEditor* editor,
    const EditorSemanticContext& context) const
{
    if (!menu || !editor)
        return;

    const EditorSourceSymbolContextMenuState menuState =
        contextService()->sourceSymbolContextMenuState(context);
    const SourceSymbolActionContext sourceContext =
        contextService()->sourceSymbolActionContext(context);
    const bool symbolAvailable =
        sourceContext.available
        && !sourceContext.symbolName.trimmed().isEmpty();
    ExposeSignalToTopService exposeService;
    const EditorActionContext actionContext =
        actionContextFor(context);
    EditorSemanticContext resolvedContext = context;
    if (actionContext.hierarchyBound()) {
        resolvedContext.hierarchyInstance =
            actionContext.resolvedHierarchy;
    }
    QString unavailableReason;
    const bool exposeReady = actionContext.hierarchyBound()
        && exposeService.canOffer(
            resolvedContext, &unavailableReason);
    if (!actionContext.hierarchyBound())
        unavailableReason = actionContext.hierarchyResolutionReason;

    EditorContextMenuRequest request;
    request.actionContext = actionContext;
    request.symbolAvailable = symbolAvailable;
    const bool editable = !editor->isReadOnly();
    const bool hasSelection =
        editor->textCursor().hasSelection();
    const int cursorPosition =
        context.cursorPosition >= 0
            ? context.cursorPosition
            : editor->textCursor().position();

    const auto append =
        [&request](const QString& actionId,
                   bool relevant = true,
                   bool executable = true,
                   const QString& reason = QString(),
                   bool standard = false,
                   bool enterableWhenUnavailable = false) {
            EditorContextMenuCapability capability;
            capability.actionId = actionId;
            capability.relevant = relevant;
            capability.executable = executable;
            capability.unavailableReason = reason;
            capability.standard = standard;
            capability.enterableWhenUnavailable =
                enterableWhenUnavailable;
            request.capabilities.append(capability);
        };

    append(QStringLiteral("edit.undo"),
           true,
           editor->document()->isUndoAvailable(),
           QStringLiteral("Nothing to undo."),
           true);
    append(QStringLiteral("edit.redo"),
           true,
           editor->document()->isRedoAvailable(),
           QStringLiteral("Nothing to redo."),
           true);
    append(QStringLiteral("edit.cut"),
           true,
           editable && hasSelection,
           editable
               ? QStringLiteral("Select text to cut.")
               : QStringLiteral("The editor is read-only."),
           true);
    append(QStringLiteral("edit.copy"),
           true,
           hasSelection,
           QStringLiteral("Select text to copy."),
           true);
    append(QStringLiteral("edit.paste"),
           true,
           editable && editor->canPaste(),
           editable
               ? QStringLiteral(
                     "The clipboard has no text that can be pasted.")
               : QStringLiteral("The editor is read-only."),
           true);
    append(QStringLiteral("select.all"),
           true,
           editor->document()->characterCount() > 1,
           QStringLiteral("The document is empty."),
           true);

    for (const EditorSourceSymbolMenuItemState& item :
         menuState.items) {
        bool relevant = symbolAvailable;
        switch (item.action) {
        case SourceSymbolAction::ShowSignalUsageHotspot:
        case SourceSymbolAction::ShowStateTransitionGraph:
        case SourceSymbolAction::ShowModuleBlockDiagram:
            relevant = symbolAvailable && item.enabled;
            break;
        case SourceSymbolAction::GoToDefinition:
        case SourceSymbolAction::ShowSignalKernelGraph:
            break;
        }
        append(sourceSymbolActionId(item.action),
               relevant,
               item.enabled,
               item.disabledReason);
    }
    append(QStringLiteral("navigation.goLine"));
    append(QString::fromLatin1(
        ActionIds::ViewTemporaryEditorOpen));
    append(QStringLiteral("edit.replace"),
           true,
           editable,
           QStringLiteral("The editor is read-only."));

    const EditorStructuralContextMenuState structural =
        editor->structuralContextMenuState(cursorPosition);
    append(QStringLiteral("refactor.createSignalDefinition"),
           structural.signalDefinitionAvailable);
    append(QStringLiteral("refactor.editInstanceSlots"),
           structural.instanceSlotsAvailable);
    append(QStringLiteral("refactor.exposeSignalToTop"),
           symbolAvailable,
           exposeReady,
           unavailableReason,
           false,
           true);

    append(QStringLiteral("format.commentLines"),
           true,
           editable,
           QStringLiteral("The editor is read-only."));
    append(QStringLiteral("format.uncommentLines"),
           true,
           editable,
           QStringLiteral("The editor is read-only."));
    append(QStringLiteral("format.indentLines"),
           true,
           editable,
           QStringLiteral("The editor is read-only."));
    append(QStringLiteral("format.unindentLines"),
           true,
           editable,
           QStringLiteral("The editor is read-only."));
    append(QStringLiteral("format.profile.structured"));
    append(QStringLiteral("format.profile.indentOnly"));
    append(QStringLiteral("format.selection"),
           hasSelection,
           editable,
           QStringLiteral("The editor is read-only."));
    append(QStringLiteral("format.document"),
           true,
           editable,
           QStringLiteral("The editor is read-only."));

    const EditorContextMenuModel model =
        buildEditorContextMenuModel(request);
    menu->clear();
    QActionGroup* profileGroup = new QActionGroup(menu);
    profileGroup->setExclusive(true);

    const auto execute =
        [this, editor, context, cursorPosition, menuState](
            const QString& actionId) {
            const bool registeredEditorAction =
                actionId == QStringLiteral("edit.undo")
                || actionId == QStringLiteral("edit.redo")
                || actionId == QStringLiteral("edit.cut")
                || actionId == QStringLiteral("edit.copy")
                || actionId == QStringLiteral("edit.paste")
                || actionId == QStringLiteral("select.all")
                || actionId == QStringLiteral("navigation.goLine")
                || actionId
                       == QString::fromLatin1(
                           ActionIds::ViewTemporaryEditorOpen)
                || actionId == QStringLiteral("edit.replace")
                || actionId
                       == QStringLiteral(
                           "refactor.createSignalDefinition")
                || actionId
                       == QStringLiteral(
                           "refactor.editInstanceSlots")
                || actionId.startsWith(
                    QStringLiteral("insight."))
                || actionId
                       == QStringLiteral(
                           "refactor.exposeSignalToTop")
                || actionId.startsWith(
                    QStringLiteral("format."));
            if (registeredEditorAction
                && registeredActionRequestHandler) {
                QVariantMap parameters;
                parameters.insert(
                    QStringLiteral("cursorPosition"),
                    cursorPosition);
                parameters = withEditorActionTarget(
                    editor, parameters);
                if (actionId
                    == QString::fromLatin1(
                        ActionIds::ViewTemporaryEditorOpen)) {
                    parameters.insert(
                        QStringLiteral("path"),
                        editor->documentFileName());
                    parameters.insert(
                        QStringLiteral("documentId"),
                        editor->property(
                            "sharedDocumentId"));
                    const QTextCursor selection =
                        editor->textCursor();
                    if (selection.hasSelection()) {
                        parameters.insert(
                            QStringLiteral("selectionStart"),
                            selection.selectionStart());
                        parameters.insert(
                            QStringLiteral("selectionEnd"),
                            selection.selectionEnd());
                    }
                }
                registeredActionRequestHandler(
                    actionId, parameters);
                return;
            }
            if (actionId == QStringLiteral("edit.undo")) {
                editor->undo();
            } else if (actionId == QStringLiteral("edit.redo")) {
                editor->redo();
            } else if (actionId == QStringLiteral("edit.cut")) {
                editor->cut();
            } else if (actionId == QStringLiteral("edit.copy")) {
                editor->copy();
            } else if (actionId == QStringLiteral("edit.paste")) {
                editor->paste();
            } else if (actionId == QStringLiteral("select.all")) {
                editor->selectAll();
            } else if (actionId
                       == QStringLiteral("navigation.goLine")) {
                editor->showGotoLineDialog();
            } else if (actionId == QStringLiteral("edit.replace")) {
                editor->showReplaceDialog();
            } else if (actionId
                       == QStringLiteral(
                           "refactor.createSignalDefinition")) {
                QString reason;
                if (!editor->beginSignalDefinitionEditorAt(
                        cursorPosition, &reason)
                    && !reason.isEmpty()
                    && statusMessageHandler) {
                    statusMessageHandler(reason, 5000);
                }
            } else if (actionId
                       == QStringLiteral(
                           "refactor.editInstanceSlots")) {
                QString reason;
                if (!editor->editInstanceSlotsAt(
                        cursorPosition, &reason)
                    && !reason.isEmpty()
                    && statusMessageHandler) {
                    statusMessageHandler(reason, 5000);
                }
            } else if (actionId
                       == QStringLiteral(
                           "refactor.exposeSignalToTop")) {
                handleExposeSignalToTopRequested(context);
            } else if (actionId
                       == QStringLiteral("format.commentLines")) {
                editor->commentSelectionOrLine();
            } else if (actionId
                       == QStringLiteral("format.uncommentLines")) {
                editor->uncommentSelectionOrLine();
            } else if (actionId
                       == QStringLiteral("format.indentLines")) {
                editor->indentSelectionOrLine();
            } else if (actionId
                       == QStringLiteral("format.unindentLines")) {
                editor->unindentSelectionOrLine();
            } else if (actionId
                       == QStringLiteral(
                           "format.profile.structured")) {
                editor->setFormatterProfile(
                    FormatterProfile::Structured);
            } else if (actionId
                       == QStringLiteral(
                           "format.profile.indentOnly")) {
                editor->setFormatterProfile(
                    FormatterProfile::IndentOnly);
            } else if (actionId
                       == QStringLiteral("format.selection")) {
                editor->formatSelection();
            } else if (actionId
                       == QStringLiteral("format.document")) {
                editor->formatDocument();
            } else {
                for (const EditorSourceSymbolMenuItemState& item :
                     menuState.items) {
                    if (sourceSymbolActionId(item.action)
                        == actionId) {
                        handleSourceSymbolActionRequested(
                            item.action, context);
                        break;
                    }
                }
            }
        };

    for (const EditorContextMenuSectionModel& section :
         model.sections) {
        QMenu* targetMenu = menu;
        if (section.section != EditorContextMenuSection::Standard) {
            targetMenu = menu->addMenu(section.title);
            targetMenu->setObjectName(
                QStringLiteral("editorContextMenu.%1")
                    .arg(section.title.toCaseFolded()));
        }
        for (const EditorContextMenuItem& item :
             section.items) {
            if (section.section
                    == EditorContextMenuSection::Standard
                && (item.actionId == QStringLiteral("edit.cut")
                    || item.actionId
                           == QStringLiteral("select.all"))) {
                targetMenu->addSeparator();
            }

            QAction* action = targetMenu->addAction(item.text);
            action->setObjectName(item.actionId);
            action->setProperty("actionId", item.actionId);
            if (const ActionDescriptor* descriptor =
                    findActionById(item.actionId)) {
                action->setProperty(
                    "executionRoute",
                    descriptor->executionRoute);
            }
            action->setProperty("executable", item.executable);
            action->setProperty("visibleReason",
                                item.visibleReason);
            action->setEnabled(item.enabled);
            if (!item.visibleReason.isEmpty()) {
                action->setStatusTip(item.visibleReason);
                action->setToolTip(item.visibleReason);
            }
            if (item.actionId
                == QStringLiteral("refactor.exposeSignalToTop")) {
                action->setObjectName(
                    QStringLiteral("exposeSignalToTopAction"));
                action->setProperty(
                    "actionId",
                    QStringLiteral(
                        "refactor.exposeSignalToTop"));
            }
            if (item.actionId
                == QStringLiteral(
                    "format.profile.structured")) {
                action->setCheckable(true);
                action->setActionGroup(profileGroup);
                action->setChecked(
                    editor->formatterProfile()
                    == FormatterProfile::Structured);
            } else if (item.actionId
                       == QStringLiteral(
                           "format.profile.indentOnly")) {
                action->setCheckable(true);
                action->setActionGroup(profileGroup);
                action->setChecked(
                    editor->formatterProfile()
                    == FormatterProfile::IndentOnly);
            }

            const QString shortcut =
                effectiveActionShortcut(item.actionId);
            if (!shortcut.isEmpty()) {
                action->setShortcut(
                    QKeySequence::fromString(
                        shortcut,
                        QKeySequence::PortableText));
            }

            connect(action,
                    &QAction::triggered,
                    this,
                    [execute, actionId = item.actionId]() {
                        execute(actionId);
                    });
        }
    }
}

void EditorCoordinator::populateSourceSymbolContextMenuForTest(
    QMenu* menu,
    const EditorSemanticContext& context) const
{
    handleSourceSymbolContextMenuRequested(
        menu,
        tabManager ? tabManager->getCurrentEditor() : nullptr,
        context);
}

void EditorCoordinator::handleExposeSignalToTopRequested(
    const EditorSemanticContext& context) const
{
    const ActionDescriptor* descriptor =
        findActionById(QStringLiteral(
            "refactor.exposeSignalToTop"));
    if (!descriptor)
        return;
    ActionInvocation invocation;
    invocation.parameters.insert(
        QStringLiteral("cursorPosition"),
        context.cursorPosition);
    const ActionExecutionResult result =
        executeRegisteredExposeSignalAction(
            *descriptor, invocation);
    if (!result.succeeded
        && result.failureReason
               != QStringLiteral(
                   "Expose Signal to Top was cancelled.")) {
        MyCodeEditor* parentEditor = tabManager
            ? tabManager->getCurrentEditor()
            : nullptr;
        if (parentEditor) {
            QMessageBox::information(
                parentEditor,
                exposeSignalToTopActionText(),
                result.failureReason);
        }
    }
}

void EditorCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    if (tabManager) {
        for (int i = 0; i < tabManager->editorCount(); ++i) {
            if (MyCodeEditor* openEditor = tabManager->getEditorAt(i)) {
                if (openEditor != editor) {
                    openEditor->exitInteractionModes(
                        EditorModeExitReason::TabChanged);
                }
            }
        }
    }
    if (modeStateHandler) {
        modeStateHandler(editor
                             ? editor->editorModeSnapshot()
                             : EditorModeSnapshot{});
    }
    dependencies.handleActiveEditorChanged(editor);
}
