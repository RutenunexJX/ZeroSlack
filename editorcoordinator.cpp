#include "editorcoordinator.h"

#include "modemanager.h"
#include "mycodeeditor.h"
#include "tabmanager.h"

#include <utility>

EditorCoordinator::EditorCoordinator(TabManager* tabManager,
                                     ModeManager* modeManager,
                                     QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , modeManager(modeManager)
{
}

void EditorCoordinator::setIncludePathResolver(
    std::function<QString(const QString&, const QString&)> resolver)
{
    includePathResolver = std::move(resolver);
}

void EditorCoordinator::setFileOpenHandler(std::function<bool(const QString&)> handler)
{
    fileOpenHandler = std::move(handler);
}

void EditorCoordinator::setDefinitionNavigationHandler(
    std::function<void(const QString&, int)> handler)
{
    definitionNavigationHandler = std::move(handler);
}

void EditorCoordinator::setRelationshipAnalysisHandler(
    std::function<void(const QString&, const QString&)> handler)
{
    relationshipAnalysisHandler = std::move(handler);
}

void EditorCoordinator::setSaveFileHandler(std::function<void()> handler)
{
    saveFileHandler = std::move(handler);
}

void EditorCoordinator::setSaveFileAsHandler(std::function<void()> handler)
{
    saveFileAsHandler = std::move(handler);
}

void EditorCoordinator::setOpenFileHandler(std::function<void()> handler)
{
    openFileHandler = std::move(handler);
}

void EditorCoordinator::setNewFileHandler(std::function<void()> handler)
{
    newFileHandler = std::move(handler);
}

void EditorCoordinator::setReferenceSearchHandler(
    std::function<void(const QString&, const QString&, const QString&)> handler)
{
    referenceSearchHandler = std::move(handler);
}

void EditorCoordinator::setRelationshipBrowseHandler(
    std::function<void(const QString&, const QString&, const QString&)> handler)
{
    relationshipBrowseHandler = std::move(handler);
}

void EditorCoordinator::setActiveEditorChangedHandler(
    std::function<void(MyCodeEditor*)> handler)
{
    activeEditorChangedHandler = std::move(handler);
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

    signalsConnected = true;
}

void EditorCoordinator::attachEditor(MyCodeEditor* editor)
{
    if (!editor)
        return;

    applyAlternateMode(editor);
    editor->setIncludePathResolver(
        [this](const QString& includePath, const QString& currentFile) {
            return includePathResolver
                ? includePathResolver(includePath, currentFile)
                : QString();
        });
    editor->setFileOpenHandler([this](const QString& filePath) {
        return fileOpenHandler ? fileOpenHandler(filePath) : false;
    });
    connect(editor, &MyCodeEditor::definitionJumpRequested,
            this, [this](const QString&, const QString& file, int line) {
                if (definitionNavigationHandler)
                    definitionNavigationHandler(file, line);
            });
    connect(editor, &MyCodeEditor::relationshipAnalysisRequested,
            this, [this](const QString& fileName, const QString& content) {
                if (relationshipAnalysisHandler)
                    relationshipAnalysisHandler(fileName, content);
            });
    connect(editor, &MyCodeEditor::saveFileRequested,
            this, [this]() {
                if (saveFileHandler)
                    saveFileHandler();
            });
    connect(editor, &MyCodeEditor::saveFileAsRequested,
            this, [this]() {
                if (saveFileAsHandler)
                    saveFileAsHandler();
            });
    connect(editor, &MyCodeEditor::openFileRequested,
            this, [this]() {
                if (openFileHandler)
                    openFileHandler();
            });
    connect(editor, &MyCodeEditor::newFileRequested,
            this, [this]() {
                if (newFileHandler)
                    newFileHandler();
            });
    connect(editor, &MyCodeEditor::referenceSearchRequested,
            this, [this](const QString& symbolName,
                         const QString& fileName,
                         const QString& moduleName) {
                if (referenceSearchHandler)
                    referenceSearchHandler(symbolName, fileName, moduleName);
            });
    connect(editor, &MyCodeEditor::relationshipBrowseRequested,
            this, [this](const QString& symbolName,
                         const QString& fileName,
                         const QString& moduleName) {
                if (relationshipBrowseHandler)
                    relationshipBrowseHandler(symbolName, fileName, moduleName);
            });
}

void EditorCoordinator::applyAlternateMode(MyCodeEditor* editor) const
{
    if (!editor || !modeManager)
        return;
    editor->setAlternateModeEnabled(modeManager->getCurrentMode() == ModeManager::AlternateMode);
}

void EditorCoordinator::applyAlternateModeToOpenEditors() const
{
    if (!tabManager)
        return;

    for (int i = 0; i < tabManager->editorCount(); ++i)
        applyAlternateMode(tabManager->getEditorAt(i));
}

void EditorCoordinator::handleActiveEditorChanged(MyCodeEditor* editor)
{
    applyAlternateMode(editor);
    if (activeEditorChangedHandler)
        activeEditorChangedHandler(editor);
}
