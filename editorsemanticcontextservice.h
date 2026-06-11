#ifndef EDITORSEMANTICCONTEXTSERVICE_H
#define EDITORSEMANTICCONTEXTSERVICE_H

#include "alternatecommandservice.h"
#include "completionservice.h"
#include "definitionnavigationservice.h"
#include "sourcenavigationservice.h"

#include <QString>
#include <QList>
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

struct EditorAlternateModeKeyContext {
    int key = 0;
    QString text;
    QString buffer;
};

struct EditorAlternateModeCompletionDisplayState {
    bool updateCompletions = false;
    bool showPopup = false;
    QString normalizedInput;
    QStringList matches;
};

enum class EditorAlternateModeKeyAction {
    Consume,
    UpdateInput,
    RefreshCompletions,
    ExecuteCommand,
    ClearAndHide
};

struct EditorAlternateModeKeyState {
    EditorAlternateModeKeyAction action =
        EditorAlternateModeKeyAction::Consume;
    QString nextInput;
    QString command;
    bool hidePopup = false;
    bool clearBuffer = false;
    EditorAlternateModeCompletionDisplayState completion;
};

struct EditorSourceNavigationTarget {
    bool matched = false;
    bool jumpable = false;
    bool includeTarget = false;
    bool identifierTarget = false;
    QString text;
    int startPos = -1;
    int endPos = -1;
    int cursorPosition = -1;
};

enum class EditorSourceNavigationClickAction {
    None,
    OpenInclude,
    NavigateToDefinition
};

struct EditorSourceNavigationClickState {
    EditorSourceNavigationClickAction action =
        EditorSourceNavigationClickAction::None;
    QString text;
    int contextCursorPosition = -1;
    bool acceptEvent = false;
};

struct EditorSourceSymbolShortcutContext {
    int key = 0;
    int modifiers = 0;
    EditorSemanticContext semanticContext;
};

struct EditorSourceSymbolShortcutState {
    bool matched = false;
    bool acceptEvent = false;
    SourceSymbolAction action = SourceSymbolAction::FindReferences;
    EditorSemanticContext semanticContext;
};

struct EditorSourceSymbolMenuItemState {
    SourceSymbolAction action = SourceSymbolAction::FindReferences;
    bool enabled = false;
};

struct EditorSourceSymbolContextMenuState {
    QList<EditorSourceSymbolMenuItemState> items;
};

struct EditorSourceSymbolActionRequestState {
    bool available = false;
    SourceSymbolAction action = SourceSymbolAction::FindReferences;
    QString symbolName;
    QString fileName;
    QString moduleName;
};

class EditorSemanticContextService
{
public:
    static EditorSemanticContextService* getInstance();

    EditorSemanticContextService();
    ~EditorSemanticContextService();

    SourceSymbolActionContext sourceSymbolActionContext(
        const EditorSemanticContext& context) const;
    EditorSourceSymbolShortcutState sourceSymbolShortcutState(
        const EditorSourceSymbolShortcutContext& context) const;
    EditorSourceSymbolContextMenuState sourceSymbolContextMenuState(
        const EditorSemanticContext& context) const;
    EditorSourceSymbolActionRequestState sourceSymbolActionRequestState(
        SourceSymbolAction action,
        const EditorSemanticContext& context) const;
    SourceEditorNavigationTarget sourceNavigationTarget(
        const EditorSemanticContext& context,
        const std::function<bool(const QString&)>& canResolveIdentifier) const;
    SourceEditorNavigationTarget definitionSourceNavigationTarget(
        const EditorSemanticContext& context) const;
    EditorSourceNavigationTarget editorSourceNavigationTarget(
        const EditorSemanticContext& context,
        int blockPosition) const;
    EditorSourceNavigationClickState sourceNavigationClickState(
        const EditorSourceNavigationTarget& target) const;
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
    EditorAlternateModeCompletionDisplayState
        alternateModeCompletionDisplayState(const QString& input) const;
    EditorAlternateModeKeyState alternateModeKeyState(
        const EditorAlternateModeKeyContext& context) const;
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
