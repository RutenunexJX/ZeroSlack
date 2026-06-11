#ifndef EDITORSEMANTICCONTEXTSERVICE_H
#define EDITORSEMANTICCONTEXTSERVICE_H

#include "alternatecommandservice.h"
#include "completionservice.h"
#include "definitionnavigationservice.h"
#include "sourcenavigationservice.h"

#include <QString>
#include <memory>
#include <functional>

struct EditorSemanticContext {
    QString fileName;
    QString moduleName;
    QString documentText;
    QString lineText;
    QString lineUpToCursor;
    QString wordPrefix;
    int cursorLine = -1;
    int cursorPosition = -1;
    int column = -1;
    bool commandModeActive = false;
};

struct EditorCompletionActivationContext {
    bool selectable = false;
    bool alternateModeActive = false;
    bool commandModeActive = false;
    QString itemText;
    QString defaultValue;
};

struct EditorCompletionPopupKeyContext {
    int key = 0;
    bool alternateModeActive = false;
    bool commandModeActive = false;
    bool currentIndexValid = false;
    bool hasRows = false;
    bool alternateBufferEmpty = true;
};

struct EditorCompletionTextChangeState {
    bool commandModeActive = false;
    bool startCompletionTimer = false;
    bool hidePopup = false;
    CommandModeInputState commandInput;
    CompletionTriggerState trigger;
};

struct EditorCommandModeCompletionRefreshState {
    bool matched = false;
    bool commandModeActive = false;
    bool resetExitedByDoubleSpace = false;
    bool suppressAfterExit = false;
    bool exitRequested = false;
    bool markExitedByDoubleSpace = false;
    bool clearCommandHighlight = false;
    bool highlightCommand = false;
    bool hidePopup = false;
    bool showCompletions = false;
    CommandModeCompletionState completion;
};

class EditorSemanticContextService
{
public:
    static EditorSemanticContextService* getInstance();

    EditorSemanticContextService();
    ~EditorSemanticContextService();

    SourceSymbolActionContext sourceSymbolActionContext(
        const EditorSemanticContext& context) const;
    SourceEditorNavigationTarget sourceNavigationTarget(
        const EditorSemanticContext& context,
        const std::function<bool(const QString&)>& canResolveIdentifier) const;
    SourceEditorNavigationTarget definitionSourceNavigationTarget(
        const EditorSemanticContext& context) const;
    SourceIdentifierTarget sourceIdentifierTarget(
        const EditorSemanticContext& context) const;
    DefinitionNavigationQuery definitionNavigationQuery(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    DefinitionNavigationTarget resolveDefinitionTarget(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    bool canResolveDefinitionTarget(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    QString definitionTooltipText(
        const QString& symbolName,
        const EditorSemanticContext& context) const;
    CompletionTriggerQuery completionTriggerQuery(
        const EditorSemanticContext& context) const;
    CompletionTriggerState completionTriggerState(
        const EditorSemanticContext& context) const;
    EditorCompletionTextChangeState completionTextChangeState(
        const EditorSemanticContext& context) const;
    CompletionQuery completionQuery(const QString& prefix,
                                    const EditorSemanticContext& context) const;
    QStringList completionNames(const QString& prefix,
                                const EditorSemanticContext& context) const;
    CommandModeCompletionQuery commandModeCompletionQuery(
        const EditorSemanticContext& context) const;
    CommandModeCompletionState commandModeCompletionState(
        const EditorSemanticContext& context) const;
    EditorCommandModeCompletionRefreshState commandModeCompletionRefreshState(
        const EditorSemanticContext& context,
        bool exitedByDoubleSpace) const;
    CommandModeInputState commandModeInputState(
        const EditorSemanticContext& context) const;
    CommandModeMatch commandModeMatch(
        const EditorSemanticContext& context) const;
    AlternateCommandCompletionState alternateCommandCompletionState(
        const QString& input) const;
    EditorCompletionQuery editorCompletionQuery(
        const EditorSemanticContext& context) const;
    EditorCompletionState editorCompletionState(
        const EditorSemanticContext& context) const;
    CompletionActivationState completionActivationState(
        const EditorCompletionActivationContext& context) const;
    CompletionActivationState completionActivationState(
        const CompletionActivationQuery& query) const;
    CompletionPopupKeyState completionPopupKeyState(
        const EditorCompletionPopupKeyContext& context) const;
    CompletionPopupKeyState completionPopupKeyState(
        const CompletionPopupKeyQuery& query) const;

private:
    static std::unique_ptr<EditorSemanticContextService> instance;
};

#endif // EDITORSEMANTICCONTEXTSERVICE_H
