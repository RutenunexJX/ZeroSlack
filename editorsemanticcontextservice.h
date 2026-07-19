#ifndef EDITORSEMANTICCONTEXTSERVICE_H
#define EDITORSEMANTICCONTEXTSERVICE_H

#include "completionservice.h"
#include "definitionnavigationservice.h"
#include "sourcenavigationservice.h"
#include "symbolhoverreports.h"
#include "symbolpresentationservice.h"

#include <QString>
#include <QList>
#include <cstdint>
#include <memory>
#include <functional>

struct EditorSemanticContext {
    QString fileName;
    QString moduleName;
    QString documentText;
    QString lineText;
    QString lineUpToCursor;
    int cursorLine = -1;
    int cursorPosition = -1;
    int column = -1;
    std::uint64_t documentRevision = 0;
    HierarchyInstanceContext hierarchyInstance;
};

struct EditorCompletionActivationContext {
    bool selectable = false;
    QString itemText;
    QString defaultValue;
    int selectionStart = -1;
    int selectionLength = 0;
    CodeTemplateSlotList templateSlots;
};

struct EditorCompletionPopupKeyContext {
    int key = 0;
    bool currentIndexValid = false;
    bool hasRows = false;
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
    SourceSymbolAction action = SourceSymbolAction::GoToDefinition;
    bool enabled = false;
    QString disabledReason;
};

struct EditorSourceSymbolContextMenuState {
    QList<EditorSourceSymbolMenuItemState> items;
};

struct EditorSourceSymbolActionRequestState {
    bool available = false;
    SourceSymbolAction action = SourceSymbolAction::GoToDefinition;
    QString unavailableReason;
    QString symbolName;
    QString signalAccessPath;
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
    SymbolHoverReport symbolHoverReport(
        const EditorSemanticContext& context) const;
    DefinitionPreviewReport definitionPreviewReport(
        const EditorSemanticContext& context) const;
    CommandModeCompletionQuery commandModeCompletionQuery(
        const EditorSemanticContext& context) const;
    CommandModeCompletionState commandModeCompletionState(
        const EditorSemanticContext& context) const;
    CommandModeInputState commandModeInputState(
        const EditorSemanticContext& context) const;
    CommandModeMatch commandModeMatch(
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
