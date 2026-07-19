#ifndef COMPLETIONTYPES_H
#define COMPLETIONTYPES_H

#include "semanticindex.h"

#include <QList>
#include <QString>

enum class CompletionCommandKind {
    User,
    Reg,
    Wire,
    Logic,
    Module,
    Task,
    Function,
    Interface,
    Package,
    Macro,
    Localparam,
    Parameter,
    AlwaysProcess,
    ContinuousAssign,
    Typedef,
    EnumValue,
    EnumType,
    EnumVariable,
    StructMember,
    PackedStructType,
    UnpackedStructType,
    PackedStructVariable,
    UnpackedStructVariable
};

struct CommandCompletionQuery {
    QString prefix;
    QString fileName;
    QString moduleName;
    QString documentText;
    int cursorLine = -1;
    int cursorPosition = -1;
    CompletionCommandKind commandKind = CompletionCommandKind::User;
};

enum class CompletionActivationAction {
    None,
    ReplaceLine,
    ReplaceCommandInput,
    ExecuteEditorAction
};

struct CodeTemplateSlot {
    QString name;
    int start = -1;
    int length = 0;
};

using CodeTemplateSlotList = QList<CodeTemplateSlot>;

struct CompletionActivationQuery {
    bool selectable = false;
    QString itemText;
    QString defaultValue;
    int selectionStart = -1;
    int selectionLength = 0;
    CodeTemplateSlotList templateSlots;
};

struct CompletionActivationState {
    CompletionActivationAction action = CompletionActivationAction::None;
    QString text;
    int selectionStart = -1;
    int selectionLength = 0;
    CodeTemplateSlotList templateSlots;
    bool clearCommandMode = false;
    bool hidePopup = false;
};

enum class CompletionPopupKeyAction {
    None,
    Consume,
    ForwardToPopup,
    SelectPreviousSelectable,
    ActivateCurrent,
    ActivateCurrentOrFirstSelectable,
    HidePopupAndClearCommand
};

struct CompletionPopupKeyQuery {
    int key = 0;
    bool currentIndexValid = false;
    bool hasRows = false;
};

struct CompletionPopupKeyState {
    CompletionPopupKeyAction action = CompletionPopupKeyAction::None;
};

struct CommandSymbolPresentation {
    QString defaultValue;
    QString typeDescription;
};

struct CommandSymbolCompletionItem {
    QString text;
    QString defaultValue;
    QString description;
    QString uniqueKey;
    int selectionStart = -1;
    int selectionLength = 0;
    CodeTemplateSlotList templateSlots;
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    SymbolTaxonomy::DeclarationKind declarationKind =
        SymbolTaxonomy::DeclarationKind::Unknown;
    SymbolTaxonomy::SymbolUsageRole usageRole =
        SymbolTaxonomy::SymbolUsageRole::Unknown;
    SymbolTaxonomy::SymbolOwnerScope ownerScope =
        SymbolTaxonomy::SymbolOwnerScope::Unknown;
    SymbolTaxonomy::SourceRole sourceRole =
        SymbolTaxonomy::SourceRole::Unknown;
    QString analysisBandDisplayName;
    SemanticAnalysisBandMetadata analysisBand;
    int score = 0;
};

struct CommandModeCommand {
    QString prefix;
    CompletionCommandKind kind = CompletionCommandKind::User;
    QString description;
    QString defaultValue;
};

enum class InlineCommandIntent {
    SemanticCompletion,
    CodeTemplate,
    EditorAction,
    HeaderInclude,
    PackageImport
};

struct InlineCommandDescriptor {
    QString prefix;
    InlineCommandIntent intent = InlineCommandIntent::SemanticCompletion;
    CompletionCommandKind semanticKind = CompletionCommandKind::User;
    QString label;
    QString description;
    QString defaultValue;
};

struct InlineCommandMatch {
    bool matched = false;
    bool helpRequested = false;
    InlineCommandIntent intent = InlineCommandIntent::SemanticCompletion;
    int prefixPosition = -1;
    int endPosition = -1;
    QString commandToken;
    QString input;
    InlineCommandDescriptor descriptor;
};

struct CodeTemplateItem {
    QString commandToken;
    QString label;
    QString description;
    QString defaultValue;
    QString insertText;
    int selectionStart = -1;
    int selectionLength = 0;
    CodeTemplateSlotList templateSlots;
};

struct CommandModeMatch {
    bool matched = false;
    bool helpRequested = false;
    InlineCommandIntent intent = InlineCommandIntent::SemanticCompletion;
    int prefixPosition = -1;
    QString input;
    CommandModeCommand command;
    InlineCommandDescriptor descriptor;
};

struct CommandModeInputState {
    bool matched = false;
    bool helpRequested = false;
    InlineCommandIntent intent = InlineCommandIntent::SemanticCompletion;
    int prefixPosition = -1;
    QString input;
    CommandModeCommand command;
    InlineCommandDescriptor descriptor;
};

struct CommandModeCompletionQuery {
    QString lineUpToCursor;
    QString fileName;
    QString moduleName;
    QString documentText;
    int cursorLine = -1;
    int cursorPosition = -1;
    bool hasExplicitMatch = false;
    InlineCommandMatch explicitMatch;
};

struct CommandModeCompletionState {
    bool matched = false;
    bool helpRequested = false;
    bool hidePopup = false;
    bool showCompletions = false;
    InlineCommandIntent intent = InlineCommandIntent::SemanticCompletion;
    int prefixPosition = -1;
    QString input;
    QString completionPrefix;
    QString headerText;
    CommandModeCommand command;
    InlineCommandDescriptor descriptor;
    CompletionCommandKind commandKind = CompletionCommandKind::User;
    QList<CommandModeCommand> helpCommands;
    QList<InlineCommandDescriptor> helpDescriptors;
    QList<CodeTemplateItem> templateItems;
    QList<SemanticSymbolRecord> symbolRecords;
    QList<SymbolStableKey> symbolStableKeys;
};

#endif // COMPLETIONTYPES_H
