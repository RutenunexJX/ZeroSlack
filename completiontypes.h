#ifndef COMPLETIONTYPES_H
#define COMPLETIONTYPES_H

#include "semanticindex.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

struct CompletionQuery {
    QString prefix;
    QString fileName;
    QString moduleName;
    QString structTypeNameForMember;
    int cursorLine = -1;
    int cursorPosition = -1;
};

struct CompletionResult {
    QStringList names;
    struct SemanticCompletionItem {
        QString label;
        QString insertText;
        QString typeDisplayName;
        QString ownerScopeName;
        QString sourceRoleDisplayName;
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
    };
    QList<SemanticCompletionItem> items;
};

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
    CompletionCommandKind commandKind = CompletionCommandKind::User;
};

struct ContextCompletionQuery {
    QString prefix;
    QString currentModule;
    QString context;
    bool relationshipCompletionsEnabled = true;
};

struct CompletionTriggerQuery {
    QString lineUpToCursor;
    QString moduleName;
    bool commandModeActive = false;
};

struct CompletionTriggerState {
    bool continueCompletion = false;
    bool hidePopup = false;
};

enum class CompletionActivationMode {
    EditorWord,
    CommandMode,
    AlternateMode
};

enum class CompletionActivationAction {
    None,
    ReplaceWord,
    ReplaceLine,
    ReplaceCommandInput,
    ExecuteAlternateCommand,
    ExecuteEditorAction
};

struct CompletionActivationQuery {
    bool selectable = false;
    CompletionActivationMode mode = CompletionActivationMode::EditorWord;
    QString itemText;
    QString defaultValue;
    int selectionStart = -1;
    int selectionLength = 0;
};

struct CompletionActivationState {
    CompletionActivationAction action = CompletionActivationAction::None;
    QString text;
    int selectionStart = -1;
    int selectionLength = 0;
    bool clearCommandMode = false;
    bool hidePopup = false;
};

enum class CompletionPopupKeyAction {
    None,
    Consume,
    ForwardToPopup,
    ActivateCurrent,
    ActivateCurrentOrFirstSelectable,
    HidePopup,
    HidePopupAndClearCommand,
    HidePopupAndClearAlternate,
    BackspaceAlternateInput
};

struct CompletionPopupKeyQuery {
    int key = 0;
    CompletionActivationMode mode = CompletionActivationMode::EditorWord;
    bool currentIndexValid = false;
    bool hasRows = false;
    bool alternateBufferEmpty = true;
};

struct CompletionPopupKeyState {
    CompletionPopupKeyAction action = CompletionPopupKeyAction::None;
};

struct EditorCompletionQuery {
    QString lineUpToCursor;
    QString wordPrefix;
    QString fileName;
    QString moduleName;
    int cursorLine = -1;
    int cursorPosition = -1;
};

struct EditorCompletionState {
    bool available = false;
    QString prefix;
    int replacementStartColumn = -1;
    CompletionResult completion;
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
    EditorAction
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
    bool exitRequested = false;
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
};

struct CommandModeCompletionState {
    bool matched = false;
    bool helpRequested = false;
    bool exitRequested = false;
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
