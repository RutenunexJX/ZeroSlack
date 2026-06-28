#include "editorcoordinator.h"

#include "editorappearancesettings.h"
#include "formattersettings.h"
#include "codetemplateservice.h"
#include "editorsemanticcontextservice.h"
#include "definitionpreviewservice.h"
#include "filecommandcoordinator.h"
#include "modemanager.h"
#include "mycodeeditor.h"
#include "navigationcommandcoordinator.h"
#include "saferenameservice.h"
#include "semanticpanelrefreshcoordinator.h"
#include "tabmanager.h"
#include "workspacemanager.h"

#include <QAbstractButton>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QIODevice>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <utility>

namespace {
enum class SafeRenameConflictChoice {
    Cancel,
    Force,
    RenameConflictFirst
};

QString sourceSymbolActionText(SourceSymbolAction action)
{
    switch (action) {
    case SourceSymbolAction::FindReferences:
        return QStringLiteral("Find References");
    case SourceSymbolAction::ShowRelationships:
        return QStringLiteral("Show Relationships");
    case SourceSymbolAction::ShowSignalKernelGraph:
        return QStringLiteral("Show Signal Kernel Graph");
    case SourceSymbolAction::ShowStateTransitionGraph:
        return QStringLiteral("Show State Transition Graph");
    case SourceSymbolAction::ShowModuleBlockDiagram:
        return QStringLiteral("Show Module Block Diagram");
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

bool promptSafeRenameName(QWidget* parent,
                          const QString& title,
                          const QString& label,
                          const QString& currentName,
                          QString* outName)
{
    if (outName)
        outName->clear();

    bool accepted = false;
    const QString newName =
        QInputDialog::getText(parent,
                              title,
                              label,
                              QLineEdit::Normal,
                              currentName,
                              &accepted)
            .trimmed();
    if (!accepted)
        return false;
    if (!SafeRenameService::isValidIdentifier(newName)) {
        QMessageBox::warning(parent,
                             title,
                             QStringLiteral("Enter a valid SystemVerilog identifier."));
        return false;
    }

    if (outName)
        *outName = newName;
    return true;
}

bool promptSafeRenameDefinitionText(QWidget* parent,
                                    const QString& symbolName,
                                    QString* outText)
{
    if (outText)
        outText->clear();

    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(
        parent,
        QStringLiteral("Create Definition"),
        QStringLiteral("Definition"),
        QStringLiteral(";;l %1").arg(symbolName),
        &accepted).trimmed();
    if (!accepted || text.isEmpty())
        return false;
    if (outText)
        *outText = text;
    return true;
}

SafeRenameConflictChoice promptSafeRenameConflictChoice(
    QWidget* parent,
    const SafeRenamePlan& plan)
{
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("Rename Symbol"));
    box.setIcon(QMessageBox::Warning);
    box.setText(
        QStringLiteral("The name \"%1\" already exists as a definition.")
            .arg(plan.newName));
    box.setInformativeText(
        QStringLiteral("Choose how to continue with the rename."));
    QAbstractButton* forceButton =
        box.addButton(QStringLiteral("Force rename"),
                      QMessageBox::AcceptRole);
    QAbstractButton* renameConflictButton =
        box.addButton(QStringLiteral("Rename conflicting definition first"),
                      QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == forceButton)
        return SafeRenameConflictChoice::Force;
    if (box.clickedButton() == renameConflictButton)
        return SafeRenameConflictChoice::RenameConflictFirst;
    return SafeRenameConflictChoice::Cancel;
}

QString safeRenamePlanStatusText(SafeRenamePlanStatus status)
{
    switch (status) {
    case SafeRenamePlanStatus::Ready:
        return QStringLiteral("Rename ready.");
    case SafeRenamePlanStatus::InvalidSymbol:
        return QStringLiteral("Select a valid SystemVerilog identifier.");
    case SafeRenamePlanStatus::InvalidNewName:
        return QStringLiteral("Enter a valid SystemVerilog identifier.");
    case SafeRenamePlanStatus::NoChange:
        return QStringLiteral("The new name is unchanged.");
    case SafeRenamePlanStatus::DefinitionNotFound:
        return QStringLiteral("No definition was found for the selected symbol.");
    case SafeRenamePlanStatus::ConflictingDefinition:
        return QStringLiteral("The new name conflicts with an existing definition.");
    case SafeRenamePlanStatus::MissingFileContent:
        return QStringLiteral("A referenced file is not available for rename.");
    case SafeRenamePlanStatus::AmbiguousEditRange:
        return QStringLiteral("A rename location could not be mapped safely.");
    }
    return QStringLiteral("Rename unavailable.");
}

QString firstCommandToken(const QString& text, QString* rest)
{
    const QString trimmed = text.trimmed();
    const int space = trimmed.indexOf(QRegularExpression(QStringLiteral("\\s")));
    if (space < 0) {
        if (rest)
            rest->clear();
        return trimmed;
    }
    if (rest)
        *rest = trimmed.mid(space + 1).trimmed();
    return trimmed.left(space);
}

QString templateTokenForDefinitionCommand(const QString& token)
{
    if (token.startsWith(QStringLiteral(";;")))
        return token;
    if (token.startsWith(QLatin1Char(';')))
        return QStringLiteral(";%1").arg(token);
    return QString();
}

QString expandedDefinitionText(const QString& input,
                               const QString& newName)
{
    QString seed;
    const QString token = firstCommandToken(input, &seed);
    const QString templateToken = templateTokenForDefinitionCommand(token);
    if (!templateToken.isEmpty()) {
        if (seed.isEmpty())
            seed = newName;
        const CodeTemplateItem item =
            CodeTemplateService::getInstance()->templateForCommand(
                templateToken,
                seed);
        const QString expanded = item.insertText.isEmpty()
            ? item.defaultValue
            : item.insertText;
        if (!expanded.trimmed().isEmpty())
            return expanded.trimmed();
    }
    return input.trimmed();
}

int lineForEditorPosition(const QString& text, int position)
{
    const int bounded = qBound(0, position, text.size());
    int line = 1;
    for (int i = 0; i < bounded; ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            ++line;
    }
    return line;
}

int lineStartPositionForLine(const QString& text, int oneBasedLine)
{
    if (oneBasedLine <= 1)
        return 0;
    int line = 1;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('\n'))
            continue;
        ++line;
        if (line == oneBasedLine)
            return i + 1;
    }
    return text.size();
}

QString indentationAtPosition(const QString& text, int position)
{
    const int lineStart = lineStartPositionForLine(
        text,
        lineForEditorPosition(text, position));
    QString indentation;
    for (int i = lineStart; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
            break;
        indentation.append(ch);
    }
    return indentation;
}

QString identifierBeforeName(const QString& definitionText,
                             const QString& name)
{
    const int namePos = definitionText.indexOf(name);
    if (namePos <= 0)
        return QString();

    int pos = namePos - 1;
    while (pos >= 0 && definitionText.at(pos).isSpace())
        --pos;
    while (pos >= 0 && definitionText.at(pos) == QLatin1Char(']')) {
        while (pos >= 0 && definitionText.at(pos) != QLatin1Char('['))
            --pos;
        if (pos >= 0)
            --pos;
        while (pos >= 0 && definitionText.at(pos).isSpace())
            --pos;
    }
    int end = pos + 1;
    while (pos >= 0
           && (definitionText.at(pos).isLetterOrNumber()
               || definitionText.at(pos) == QLatin1Char('_')
               || definitionText.at(pos) == QLatin1Char('$'))) {
        --pos;
    }
    const QString candidate = definitionText.mid(pos + 1, end - pos - 1);
    static const QSet<QString> builtins = {
        QStringLiteral("logic"),
        QStringLiteral("wire"),
        QStringLiteral("reg"),
        QStringLiteral("bit"),
        QStringLiteral("int"),
        QStringLiteral("integer"),
        QStringLiteral("signed"),
        QStringLiteral("unsigned")
    };
    return builtins.contains(candidate) ? QString() : candidate;
}

int insertionPositionForCreatedDefinition(const EditorSemanticContext& context,
                                          const QString& definitionText,
                                          const QString& newName)
{
    const QString text = context.documentText;
    const int firstUseLine = lineForEditorPosition(text, context.cursorPosition);
    int insertionLine = firstUseLine;

    const QList<SemanticSymbolRecord> records =
        SemanticIndex::getInstance()->getSymbolRecords(context.fileName);
    for (const SemanticSymbolRecord& record : records) {
        if (record.name != context.moduleName
            || record.declarationKind != SymbolTaxonomy::DeclarationKind::Module)
            continue;
        if (record.location.startLine > 0
            && record.location.startLine < firstUseLine) {
            insertionLine = qMin(insertionLine, record.location.startLine + 1);
        }
    }

    const QString typeName = identifierBeforeName(definitionText, newName);
    if (!typeName.isEmpty()) {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name != typeName)
                continue;
            const bool typeLike =
                record.declarationKind == SymbolTaxonomy::DeclarationKind::Enum
                || record.declarationKind == SymbolTaxonomy::DeclarationKind::Typedef
                || record.declarationKind == SymbolTaxonomy::DeclarationKind::Struct;
            if (!typeLike || record.location.endLine <= 0)
                continue;
            const int afterTypeLine = record.location.endLine + 1;
            if (afterTypeLine <= firstUseLine)
                insertionLine = qMax(insertionLine, afterTypeLine);
        }
    }

    return lineStartPositionForLine(text, insertionLine);
}

QString indentedDefinitionText(const QString& definitionText,
                               const QString& indentation)
{
    QStringList lines = definitionText.trimmed().split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (!line.trimmed().isEmpty()
            && !line.startsWith(QLatin1Char(' '))
            && !line.startsWith(QLatin1Char('\t'))) {
            line.prepend(indentation);
        }
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QList<int> wordPositionsInText(const QString& text, const QString& word)
{
    QList<int> positions;
    int position = 0;
    while ((position = text.indexOf(word, position, Qt::CaseSensitive)) >= 0) {
        const int end = position + word.size();
        const bool leftOk = position == 0
            || !(text.at(position - 1).isLetterOrNumber()
                 || text.at(position - 1) == QLatin1Char('_')
                 || text.at(position - 1) == QLatin1Char('$'));
        const bool rightOk = end >= text.size()
            || !(text.at(end).isLetterOrNumber()
                 || text.at(end) == QLatin1Char('_')
                 || text.at(end) == QLatin1Char('$'));
        if (leftOk && rightOk)
            positions.append(position);
        position = end;
    }
    return positions;
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
    connect(editor, &MyCodeEditor::sourceNavigationRequested,
            this, [this, editor](const EditorSourceNavigationTarget& target,
                                 const EditorSemanticContext& context) {
                handleSourceNavigationRequested(editor, target, context);
            });
    connect(editor, &MyCodeEditor::sourceSymbolActionRequested,
            this, &EditorCoordinator::handleSourceSymbolActionRequested);
    connect(editor, &MyCodeEditor::safeRenameRequested,
            this, [this, editor](const QString& symbolName,
                                 const EditorSemanticContext& context,
                                 bool* handled) {
                handleSafeRenameRequested(editor, symbolName, context, handled);
            });
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
    connect(editor, &MyCodeEditor::fontZoomRequested,
            this, [this](int steps) {
                if (!appearanceSettings || steps == 0)
                    return;

                const EditorAppearanceOptions options =
                    appearanceSettings->options();
                appearanceSettings->setFontSizePt(
                    options.fontSizePt + steps);
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
    case SourceSymbolAction::ShowStateTransitionGraph:
        dependencies.showStateTransitionGraphForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    case SourceSymbolAction::ShowModuleBlockDiagram:
        dependencies.showModuleBlockDiagramForSymbol(
            requestState.symbolName,
            requestState.fileName,
            requestState.moduleName);
        break;
    }
}

QHash<QString, QString> EditorCoordinator::openFileContents() const
{
    QHash<QString, QString> contents;
    if (!tabManager)
        return contents;

    for (const QString& fileName : tabManager->getAllOpenFileNames()) {
        if (fileName.isEmpty())
            continue;
        contents.insert(fileName, tabManager->getPlainTextFromOpenFile(fileName));
    }
    return contents;
}

SafeRenamePlanQuery EditorCoordinator::safeRenameQuery(
    const QString& symbolName,
    const QString& newName,
    const EditorSemanticContext& context,
    bool forceConflicts) const
{
    SafeRenamePlanQuery query;
    query.symbolName = symbolName;
    query.newName = newName;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.documentText = context.documentText;
    query.cursorPosition = context.cursorPosition;
    query.openFileContents = openFileContents();
    query.forceConflicts = forceConflicts;
    return query;
}

bool EditorCoordinator::applySafeRenamePlan(
    MyCodeEditor* originEditor,
    const SafeRenamePlan& plan) const
{
    if (!tabManager || !plan.isReady())
        return false;

    QHash<QString, MyCodeEditor*> editorsByFile;
    for (const SafeRenameFileEdits& fileEdits : plan.fileEdits) {
        if (fileEdits.fileName.isEmpty() || fileEdits.edits.isEmpty())
            continue;
        if (!tabManager->openFileInTab(fileEdits.fileName))
            return false;
        MyCodeEditor* editor = tabManager->getCurrentEditor();
        if (!editor)
            return false;

        const QString text = editor->toPlainText();
        for (const SafeRenameTextEdit& edit : fileEdits.edits) {
            if (edit.startPosition < 0
                || edit.startPosition + edit.length > text.size()
                || text.mid(edit.startPosition, edit.length) != edit.oldText) {
                QMessageBox::warning(
                    originEditor ? originEditor : editor,
                    QStringLiteral("Rename Symbol"),
                    QStringLiteral("The file changed before rename could be applied:\n%1")
                        .arg(fileEdits.fileName));
                return false;
            }
        }
        editorsByFile.insert(normalizedEditorCoordinatorFileName(fileEdits.fileName),
                             editor);
    }

    for (const SafeRenameFileEdits& fileEdits : plan.fileEdits) {
        MyCodeEditor* editor =
            editorsByFile.value(normalizedEditorCoordinatorFileName(fileEdits.fileName));
        if (!editor)
            continue;

        QList<SafeRenameTextEdit> edits = fileEdits.edits;
        std::sort(edits.begin(),
                  edits.end(),
                  [](const SafeRenameTextEdit& lhs,
                     const SafeRenameTextEdit& rhs) {
            return lhs.startPosition > rhs.startPosition;
        });

        QTextCursor cursor(editor->document());
        cursor.beginEditBlock();
        for (const SafeRenameTextEdit& edit : edits) {
            cursor.setPosition(edit.startPosition);
            cursor.setPosition(edit.startPosition + edit.length,
                               QTextCursor::KeepAnchor);
            cursor.insertText(edit.newText);
        }
        cursor.endEditBlock();
        tabManager->updateTabTitle(editor);
    }

    if (!plan.fileEdits.isEmpty()
        && !plan.fileEdits.constFirst().edits.isEmpty()
        && tabManager->openFileInTab(plan.fileEdits.constFirst().fileName)) {
        MyCodeEditor* editor = tabManager->getCurrentEditor();
        if (editor) {
            const SafeRenameTextEdit firstEdit =
                plan.fileEdits.constFirst().edits.constFirst();
            QTextCursor cursor(editor->document());
            cursor.setPosition(firstEdit.startPosition);
            cursor.setPosition(firstEdit.startPosition + firstEdit.newText.size(),
                               QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
            editor->setFocus();
        }
    }

    return true;
}

bool EditorCoordinator::createDefinitionAndRenameCurrentFile(
    MyCodeEditor* editor,
    const QString& symbolName,
    const QString& newName,
    const EditorSemanticContext& context) const
{
    if (!editor)
        return false;

    const QMessageBox::StandardButton create =
        QMessageBox::question(
            editor,
            QStringLiteral("Create Definition"),
            QStringLiteral("No definition was found for \"%1\".\nCreate one before renaming?")
                .arg(symbolName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
    if (create != QMessageBox::Yes)
        return false;

    QString definitionInput;
    if (!promptSafeRenameDefinitionText(editor, newName, &definitionInput))
        return false;

    const QString definitionText =
        expandedDefinitionText(definitionInput, newName);
    if (definitionText.trimmed().isEmpty())
        return false;

    const int insertionPosition =
        insertionPositionForCreatedDefinition(context, definitionText, newName);
    const QString indentation =
        indentationAtPosition(context.documentText, insertionPosition);
    const QString insertionText =
        indentedDefinitionText(definitionText, indentation);

    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(insertionPosition);
    cursor.insertText(insertionText);

    const QString afterInsertText = editor->toPlainText();
    const QList<int> positions =
        wordPositionsInText(afterInsertText, symbolName);
    for (int i = positions.size() - 1; i >= 0; --i) {
        cursor.setPosition(positions.at(i));
        cursor.setPosition(positions.at(i) + symbolName.size(),
                           QTextCursor::KeepAnchor);
        cursor.insertText(newName);
    }
    cursor.endEditBlock();

    const QList<int> newPositions =
        wordPositionsInText(editor->toPlainText(), newName);
    if (!newPositions.isEmpty()) {
        QTextCursor selection(editor->document());
        selection.setPosition(newPositions.constFirst());
        selection.setPosition(newPositions.constFirst() + newName.size(),
                              QTextCursor::KeepAnchor);
        editor->setTextCursor(selection);
    }
    if (tabManager)
        tabManager->updateTabTitle(editor);
    return true;
}

void EditorCoordinator::handleSafeRenameRequested(
    MyCodeEditor* editor,
    const QString& symbolName,
    const EditorSemanticContext& context,
    bool* handled)
{
    if (handled)
        *handled = true;

    QString newName;
    if (!promptSafeRenameName(editor,
                              QStringLiteral("Rename Symbol"),
                              QStringLiteral("New name"),
                              symbolName,
                              &newName)) {
        return;
    }

    SafeRenameService renameService(SemanticIndex::getInstance());
    SafeRenamePlanQuery query =
        safeRenameQuery(symbolName, newName, context, false);
    SafeRenamePlan plan = renameService.createRenamePlan(query);

    if (plan.status == SafeRenamePlanStatus::NoChange)
        return;

    if (plan.status == SafeRenamePlanStatus::ConflictingDefinition) {
        const SafeRenameConflictChoice choice =
            promptSafeRenameConflictChoice(editor, plan);
        if (choice == SafeRenameConflictChoice::Cancel)
            return;

        if (choice == SafeRenameConflictChoice::RenameConflictFirst) {
            if (plan.conflictingDefinitions.isEmpty())
                return;

            const SemanticSymbolRecord conflict =
                plan.conflictingDefinitions.constFirst();
            QString temporaryName;
            if (!promptSafeRenameName(
                    editor,
                    QStringLiteral("Rename Conflicting Definition"),
                    QStringLiteral("Temporary name"),
                    plan.newName + QStringLiteral("_renamed"),
                    &temporaryName)) {
                return;
            }

            SafeRenamePlanQuery conflictQuery;
            conflictQuery.symbolName = plan.newName;
            conflictQuery.newName = temporaryName;
            conflictQuery.fileName = conflict.location.fileName;
            conflictQuery.moduleName = conflict.owner.name;
            conflictQuery.cursorPosition = qMax(0, conflict.location.position);
            conflictQuery.openFileContents = openFileContents();
            conflictQuery.documentText =
                conflictQuery.openFileContents.value(conflict.location.fileName);
            if (conflictQuery.documentText.isEmpty()) {
                conflictQuery.documentText =
                    SemanticIndex::getInstance()->getCachedFileContent(
                        conflict.location.fileName);
            }

            SafeRenamePlan conflictPlan =
                renameService.createRenamePlan(conflictQuery);
            if (!conflictPlan.isReady()) {
                QMessageBox::warning(
                    editor,
                    QStringLiteral("Rename Conflicting Definition"),
                    safeRenamePlanStatusText(conflictPlan.status));
                return;
            }
            if (!applySafeRenamePlan(editor, conflictPlan))
                return;

            query = safeRenameQuery(symbolName, newName, context, true);
            plan = renameService.createRenamePlan(query);
        } else {
            query.forceConflicts = true;
            plan = renameService.createRenamePlan(query);
        }
    }

    if (!plan.isReady()) {
        if (plan.status == SafeRenamePlanStatus::DefinitionNotFound) {
            if (createDefinitionAndRenameCurrentFile(
                    editor,
                    symbolName,
                    newName,
                    context)
                && statusMessageHandler) {
                statusMessageHandler(
                    QStringLiteral("Created definition and renamed %1 to %2")
                        .arg(symbolName, newName),
                    3000);
            }
        } else {
            QMessageBox::warning(editor,
                                 QStringLiteral("Rename Symbol"),
                                 safeRenamePlanStatusText(plan.status));
        }
        return;
    }

    if (applySafeRenamePlan(editor, plan) && statusMessageHandler) {
        statusMessageHandler(
            QStringLiteral("Renamed %1 occurrence(s) of %2 to %3")
                .arg(plan.editCount())
                .arg(symbolName, newName),
            3000);
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
    dependencies.handleActiveEditorChanged(editor);
}
