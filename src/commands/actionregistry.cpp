#include "actionregistry.h"

#include <QKeySequence>
#include <QSet>
#include <QStringList>

namespace {
ActionExecutionResult executeThroughHost(
    const ActionDescriptor& descriptor,
    ActionExecutionHost& host,
    const ActionInvocation& invocation)
{
    if (invocation.mode == ActionExecutionMode::DryRun
        && !descriptor.supportsDryRun) {
        ActionExecutionResult result;
        result.handled = true;
        result.failureReason =
            QStringLiteral("Action does not support dry-run.");
        return result;
    }
    return host.executeActionRoute(descriptor, invocation);
}

QString actionHistoryKey(const QString& workspaceId,
                         const QString& actionId)
{
    return workspaceId + QChar(0x1f) + actionId;
}

QVariantMap& shortcutOverridesStorage()
{
    static QVariantMap overrides;
    return overrides;
}

bool isAsciiLetterOrDigit(QChar character)
{
    const ushort value = character.unicode();
    return (value >= 'a' && value <= 'z')
           || (value >= 'A' && value <= 'Z')
           || (value >= '0' && value <= '9');
}

bool isValidActionId(const QString& id)
{
    if (id.size() < 3 || id.front() < QLatin1Char('a')
        || id.front() > QLatin1Char('z')) {
        return false;
    }

    bool hasSeparator = false;
    bool segmentHasCharacter = true;
    for (qsizetype index = 1; index < id.size(); ++index) {
        const QChar character = id.at(index);
        if (character == QLatin1Char('.')) {
            if (!segmentHasCharacter) {
                return false;
            }
            hasSeparator = true;
            segmentHasCharacter = false;
            continue;
        }
        if (!isAsciiLetterOrDigit(character)) {
            return false;
        }
        segmentHasCharacter = true;
    }
    return hasSeparator && segmentHasCharacter;
}

ActionDescriptor makeAction(const QString& id,
                            const QString& canonicalName,
                            const QString& description,
                            ActionCategory category,
                            ActionScope scope,
                            const QString& executionRoute,
                            quint32 requirements = 0,
                            const QString& unavailableReason = QString(),
                            ActionRecoveryPolicy recovery =
                                ActionRecoveryPolicy::None,
                            ActionParameterKind parameterKind =
                                ActionParameterKind::None,
                            const QString& parameterName = QString(),
                            const QString& placeholder = QString())
{
    ActionDescriptor result;
    result.id = id;
    result.canonicalName = canonicalName;
    result.description = description;
    result.category = category;
    result.scope = scope;
    result.executionRoute = executionRoute;
    result.requirementMask = requirements;
    result.unavailableReason = unavailableReason;
    result.recoveryPolicy = recovery;
    result.parameterModel.kind = parameterKind;
    result.parameterModel.name = parameterName;
    result.parameterModel.placeholder = placeholder;
    result.riskLevel =
        executionRoute.startsWith(QStringLiteral("rtledit."))
        ? ActionRiskLevel::High
        : ActionRiskLevel::Safe;
    result.supportsDryRun =
        result.riskLevel == ActionRiskLevel::High;
    result.repeatable =
        category != ActionCategory::Help
        && !id.startsWith(QStringLiteral("workspace."))
        && !id.startsWith(QStringLiteral("file."));
    result.rememberParameters =
        parameterKind != ActionParameterKind::None;
    result.execute = executeThroughHost;
    return result;
}

ActionAliasDescriptor alias(ActionSurface surface,
                            const QString& token,
                            const QString& label = QString(),
                            const QString& description = QString(),
                            const QString& defaultValue = QString(),
                            const QString& adapterKey = QString(),
                            const QString& intentKey = QString(),
                            bool triggerAdapter = true,
                            bool catalogued = false)
{
    ActionAliasDescriptor result;
    result.surface = surface;
    result.token = token;
    result.label = label;
    result.description = description;
    result.defaultValue = defaultValue;
    result.adapterKey = adapterKey;
    result.intentKey = intentKey;
    result.triggerAdapter = triggerAdapter;
    result.catalogued = catalogued;
    return result;
}

void appendCommandLayerActions(QList<ActionDescriptor>* out)
{
    const quint32 editor = ActionRequirements::Editor;
    const quint32 workspace = ActionRequirements::Workspace;
    const QString openEditor = QStringLiteral("Open an editor tab.");
    const QString openWorkspace = QStringLiteral("Open a workspace first.");

    struct Spec {
        const char* id;
        const char* name;
        const char* description;
        const char* token;
        const char* route;
        ActionCategory category;
        ActionScope scope;
        quint32 requirements;
        const char* unavailable;
        ActionParameterKind parameterKind;
        const char* parameterName;
        const char* placeholder;
        const char* shortcut = "";
        bool commandLayer = true;
    };
    const QList<Spec> specs = {
        {"navigation.goLine",
         "Go to Line",
         "Jump to a 1-based line in the current module.",
         "go <number>",
         "editor.navigation.goLine",
         ActionCategory::Navigate,
         ActionScope::Module,
         editor,
         "Open an editor tab.",
         ActionParameterKind::PositiveInteger,
         "line",
         "<number>",
         "Ctrl+G"},
        {"navigation.goModule",
         "Go to Module",
         "Open the module picker.",
         "go module",
         "editor.navigation.goModule",
         ActionCategory::Navigate,
         ActionScope::Workspace,
         workspace,
         "Open a workspace first.",
         ActionParameterKind::None,
         "",
         ""},
        {"navigation.goPackage",
         "Go to Package",
         "Open the package picker.",
         "go package",
         "editor.navigation.goPackage",
         ActionCategory::Navigate,
         ActionScope::Workspace,
         workspace,
         "Open a workspace first.",
         ActionParameterKind::None,
         "",
         ""},
        {"navigation.goEndmodule",
         "Go to Endmodule",
         "Move to the final endmodule of the current module.",
         "go endmodule",
         "editor.navigation.goEndmodule",
         ActionCategory::Navigate,
         ActionScope::Module,
         editor,
         "Open an editor tab.",
         ActionParameterKind::None,
         "",
         ""},
        {ActionIds::RepeatLastAction,
         "Repeat Last Action",
         "Repeat the most recent successful Action; high-risk Actions "
         "re-enter dry-run preview.",
         "repeat action",
         "action.repeatLast",
         ActionCategory::Refactor,
         ActionScope::Application,
         0,
         "",
         ActionParameterKind::None,
         "",
         ""},
        {"edit.clearAssignmentRhs",
         "Clear Assignment Right-Hand Sides",
         "Clear selected assignment right-hand sides and create slots.",
         "clear right",
         "editor.structure.clearAssignmentRhs",
         ActionCategory::Refactor,
         ActionScope::Editor,
         editor,
         "Select one or more assignments in an editor.",
         ActionParameterKind::None,
         "",
         ""},
        {ActionIds::EditDuplicateLines,
         "Duplicate Selection or Line",
         "Duplicate the exact selection, or duplicate the current logical line when there is no selection.",
         "duplicate selection or line",
         "editor.lines.duplicate",
         ActionCategory::Refactor,
         ActionScope::Editor,
         editor,
         "Open an editable editor tab.",
         ActionParameterKind::None,
         "",
         "",
         "Ctrl+D"},
        {"edit.deleteLines",
         "Delete Lines",
         "Delete every logical line touched by the current selection.",
         "delete lines",
         "editor.lines.delete",
         ActionCategory::Refactor,
         ActionScope::Editor,
         editor,
         "Open an editable editor tab.",
         ActionParameterKind::None,
         "",
         "",
         "Ctrl+Shift+D"},
        {"edit.joinLines",
         "Join Lines",
         "Join the selected logical lines, or the current line and its successor.",
         "join lines",
         "editor.lines.join",
         ActionCategory::Refactor,
         ActionScope::Editor,
         editor,
         "Select multiple lines or place the cursor before another line.",
         ActionParameterKind::None,
         "",
         "",
         "Ctrl+Shift+J"},
        {ActionIds::EditMoveLinesUp,
         "Move Lines Up",
         "Move every logical line touched by the selection one row upward.",
         "move lines up",
         "editor.lines.moveUp",
         ActionCategory::Refactor,
         ActionScope::Editor,
         editor,
         "Open an editable editor tab.",
         ActionParameterKind::None,
         "",
         "",
         "Alt+Up"},
        {ActionIds::EditMoveLinesDown,
         "Move Lines Down",
         "Move every logical line touched by the selection one row downward.",
         "move lines down",
         "editor.lines.moveDown",
         ActionCategory::Refactor,
         ActionScope::Editor,
         editor,
         "Open an editable editor tab.",
         ActionParameterKind::None,
         "",
         "",
         "Alt+Down"},
        {"select.beginEnd",
         "Select Begin-End Contents",
         "Select complete lines inside the nearest begin-end block.",
         "select begin end",
         "editor.selection.beginEnd",
         ActionCategory::Select,
         ActionScope::Editor,
         editor,
         "Place the cursor inside a begin-end block.",
         ActionParameterKind::None,
         "",
         ""},
        {"select.signals",
         "Select Signals",
         "Enter semantic signal check-selection mode.",
         "select signals",
         "editor.mode.signalSelection",
         ActionCategory::Select,
         ActionScope::Editor,
         editor,
         "Open an editor tab.",
         ActionParameterKind::None,
         "",
         ""},
        {"select.nextSymbolOccurrence",
         "Add Next Symbol Occurrence",
         "Select the current structural symbol, then add its next occurrence.",
         "select next occurrence",
         "editor.multicursor.addNextOccurrence",
         ActionCategory::Select,
         ActionScope::Editor,
         editor,
         "Place the cursor on a SystemVerilog identifier.",
         ActionParameterKind::None,
         "",
         ""},
        {"select.allSymbolOccurrences",
         "Select All Symbol Occurrences in Scope",
         "Create a caret for every occurrence in the nearest lexical scope.",
         "select scope occurrences",
         "editor.multicursor.selectScopeOccurrences",
         ActionCategory::Select,
         ActionScope::Editor,
         editor,
         "Place the cursor on a SystemVerilog identifier.",
         ActionParameterKind::None,
         "",
         "",
         "Ctrl+Shift+L"},
        {ActionIds::SelectExpandSmart,
         "Expand Structural Selection",
         "Expand the current selection from an identifier to its hierarchy or enclosing expression.",
         "expand selection",
         "editor.selection.expandSmart",
         ActionCategory::Select,
         ActionScope::Editor,
         editor,
         "Place the cursor on an identifier or inside a parenthesized expression.",
         ActionParameterKind::None,
         "",
         "",
         "Ctrl+W"},
        {ActionIds::NavigationNextSelectedSymbolOccurrence,
         "Next Selected Symbol Occurrence",
         "Move the current identifier selection to its next document occurrence.",
         "next selected occurrence",
         "editor.navigation.nextSelectedSymbolOccurrence",
         ActionCategory::Navigate,
         ActionScope::Symbol,
         editor,
         "Select one SystemVerilog identifier.",
         ActionParameterKind::None,
         "",
         "",
         "Ctrl+E",
         false},
        {ActionIds::NavigationPreviousSelectedSymbolOccurrence,
         "Previous Selected Symbol Occurrence",
         "Move the current identifier selection to its previous document occurrence.",
         "previous selected occurrence",
         "editor.navigation.previousSelectedSymbolOccurrence",
         ActionCategory::Navigate,
         ActionScope::Symbol,
         editor,
         "Select one SystemVerilog identifier.",
         ActionParameterKind::None,
         "",
         "",
         "Ctrl+Q",
         false},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(QString::fromLatin1(spec.id),
                       QString::fromLatin1(spec.name),
                       QString::fromLatin1(spec.description),
                       spec.category,
                       spec.scope,
                       QString::fromLatin1(spec.route),
                       spec.requirements,
                       QString::fromLatin1(spec.unavailable),
                       ActionRecoveryPolicy::Explain,
                       spec.parameterKind,
                       QString::fromLatin1(spec.parameterName),
                       QString::fromLatin1(spec.placeholder));
        descriptor.defaultShortcut =
            QString::fromLatin1(spec.shortcut);
        if (spec.commandLayer) {
            descriptor.aliases.append(
                alias(ActionSurface::CommandLayer,
                      QString::fromLatin1(spec.token),
                      QString::fromLatin1(spec.token)));
        }
        if (spec.shortcut && *spec.shortcut) {
            descriptor.aliases.append(
                alias(ActionSurface::Shortcut,
                      QString::fromLatin1(spec.shortcut),
                      QString::fromLatin1(spec.name),
                      QString::fromLatin1(spec.description)));
        }
        if (descriptor.id
            == QString::fromLatin1(
                ActionIds::RepeatLastAction)) {
            descriptor.repeatable = false;
        }
        out->append(descriptor);
    }

    ActionDescriptor deleteSelection =
        makeAction(QString::fromLatin1(ActionIds::EditDeleteSelection),
                   QStringLiteral("Delete Selection"),
                   QStringLiteral(
                       "Delete the current ordinary, multi-cursor, or column selection."),
                   ActionCategory::Refactor,
                   ActionScope::Editor,
                   QStringLiteral("editor.selection.delete"),
                   editor,
                   openEditor,
                   ActionRecoveryPolicy::Explain);
    deleteSelection.aliases.append(
        alias(ActionSurface::ActionCatalog,
              QStringLiteral("F24+D"),
              QStringLiteral("Delete Selection"),
              deleteSelection.description));
    out->append(deleteSelection);

    ActionDescriptor help =
        makeAction(QStringLiteral("help.actionCatalog"),
                   QStringLiteral("Action Catalog"),
                   QStringLiteral("Show all available actions and entry aliases."),
                   ActionCategory::Help,
                   ActionScope::Application,
                   QStringLiteral("ui.actionCatalog"));
    help.aliases = {
        alias(ActionSurface::CommandLayer,
              QStringLiteral("help"),
              QStringLiteral("help")),
        alias(ActionSurface::InlineSemantic,
              QStringLiteral(";?"),
              QStringLiteral(";?"),
              QStringLiteral("semantic command help"),
              QString(),
              QStringLiteral("help"),
              QStringLiteral("semantic"),
              false),
        alias(ActionSurface::InlineTemplate,
              QStringLiteral(";;?"),
              QStringLiteral(";;?"),
              QStringLiteral("template command help"),
              QString(),
              QStringLiteral("help"),
              QStringLiteral("template"),
              false),
        alias(ActionSurface::ActionCatalog,
              QStringLiteral("actions"),
              QStringLiteral("Action Catalog")),
    };
    out->append(help);

    Q_UNUSED(openEditor);
    Q_UNUSED(openWorkspace);
}

void appendFileCommandActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* name;
        const char* description;
        const char* route;
        const char* shortcut;
        const char* adapterKey;
        ActionScope scope;
        quint32 requirements;
        const char* unavailable;
    };
    const Spec specs[] = {
        {ActionIds::FileNew,
         "New File",
         "Create a new unsaved editor document.",
         "ui.file.new",
         "Ctrl+N",
         "new_file",
         ActionScope::Application,
         0,
         "The editor tab manager is unavailable."},
        {ActionIds::FileOpen,
         "Open File",
         "Choose a file and open it in an editor tab.",
         "ui.file.open",
         "Ctrl+O",
         "open_file",
         ActionScope::Application,
         0,
         "The editor tab manager is unavailable."},
        {ActionIds::FileSave,
         "Save File",
         "Save the active editor document.",
         "ui.file.save",
         "Ctrl+S",
         "save_file",
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before saving."},
        {ActionIds::FileSaveAs,
         "Save File As",
         "Save the active editor document under a selected path.",
         "ui.file.saveAs",
         "Ctrl+Shift+S",
         "save_as",
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before saving."},
        {ActionIds::WorkspaceOpen,
         "Open Workspace",
         "Choose a directory and open it as a workspace.",
         "ui.workspace.open",
         "Ctrl+K, Ctrl+O",
         "open_direction_as_workspace",
         ActionScope::Application,
         0,
         "The workspace manager is unavailable."},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(
                QString::fromLatin1(spec.id),
                QString::fromLatin1(spec.name),
                QString::fromLatin1(spec.description),
                ActionCategory::Workspace,
                spec.scope,
                QString::fromLatin1(spec.route),
                spec.requirements,
                QString::fromLatin1(spec.unavailable),
                spec.requirements == 0
                    ? ActionRecoveryPolicy::None
                    : ActionRecoveryPolicy::Explain);
        descriptor.defaultShortcut =
            QString::fromLatin1(spec.shortcut);
        descriptor.aliases = {
            alias(
                ActionSurface::Shortcut,
                QString::fromLatin1(spec.shortcut),
                QString::fromLatin1(spec.name),
                QString::fromLatin1(spec.description),
                QString(),
                QString::fromLatin1(spec.adapterKey)),
            alias(
                ActionSurface::ActionCatalog,
                QString::fromLatin1(spec.id),
                QString::fromLatin1(spec.name),
                QString::fromLatin1(spec.description),
                QString(),
                QString(),
                QString(),
                false,
                true),
        };
        out->append(descriptor);
    }
}

struct InlineSpec {
    const char* suffix;
    const char* token;
    const char* kindKey;
    const char* canonicalName;
    const char* description;
    const char* defaultValue;
};

const QList<InlineSpec>& inlineSemanticSpecs()
{
    static const QList<InlineSpec> specs = {
        {"reg", ";r", "reg", "Search Reg Variables", "reg variables", "reg"},
        {"wire", ";w", "wire", "Search Wire Variables", "wire variables", "wire"},
        {"logic", ";l", "logic", "Search Logic Variables", "logic variables", "logic"},
        {"module", ";m", "module", "Search Module Instantiations", "module instantiations", "module_name u_module_name (\n);"},
        {"task", ";t", "task", "Search Tasks", "tasks", "task"},
        {"function", ";f", "function", "Search Functions", "functions", "function"},
        {"interface", ";i", "interface", "Search Interfaces", "interfaces", "interface"},
        {"macro", ";d", "macro", "Search Macro Definitions", "macro definitions", "`define"},
        {"localparam", ";lp", "localparam", "Search Localparams", "localparam declarations", "localparam"},
        {"parameter", ";p", "parameter", "Search Parameters", "parameter declarations", "parameter"},
        {"always", ";a", "always", "Search Always Blocks", "always blocks", "always"},
        {"continuousAssign", ";c", "continuousAssign", "Search Continuous Assignments", "continuous assignments", "assign"},
        {"typedef", ";u", "typedef", "Search Type Definitions", "type definitions", "typedef"},
        {"enumValue", ";ee", "enumValue", "Search Enum Values", "enum values", "enum_value"},
        {"enumType", ";ne", "enumType", "Search Enum Types", "enum types", "enum"},
        {"enumVariable", ";e", "enumVariable", "Search Enum Variables", "enum variables", "enum_var"},
        {"structMember", ";sm", "structMember", "Search Struct Members", "struct members", "member"},
        {"packedStructType", ";nsp", "packedStructType", "Search Packed Struct Types", "packed struct types", "struct"},
        {"unpackedStructType", ";ns", "unpackedStructType", "Search Unpacked Struct Types", "unpacked struct types", "struct"},
        {"packedStructVariable", ";sp", "packedStructVariable", "Search Packed Struct Variables", "packed struct variables", "struct"},
        {"unpackedStructVariable", ";s", "unpackedStructVariable", "Search Unpacked Struct Variables", "unpacked struct variables", "struct"},
    };
    return specs;
}

struct TemplateCatalogSpec {
    const char* token;
    const char* label;
    const char* description;
    const char* defaultValue;
    bool templateOnly = false;
    const char* actionSuffix = nullptr;
    const char* adapterKey = nullptr;
};

const QList<TemplateCatalogSpec>& templateCatalogSpecs()
{
    static const QList<TemplateCatalogSpec> specs = {
        {";;l", "logic", "logic declaration", "logic signal;"},
        {";;w", "wire", "wire declaration", "wire signal;"},
        {";;r", "reg", "reg declaration", "reg signal;"},
        {";;p", "parameter", "parameter declaration", "parameter NAME = ;"},
        {";;lp", "localparam", "localparam declaration", "localparam NAME = ;"},
        {";;c", "assign", "continuous assignment", "assign lhs = rhs;"},
        {";;a", "always", "generic always process", "always @(*) begin\n    \nend"},
        {";;ac", "always_comb", "combinational always process", "always_comb begin\n    \nend", true, "alwaysComb", "always"},
        {";;af", "always_ff", "flip-flop process variants", "always_ff @(posedge clk) begin\n    \nend", true, "alwaysFf", "always"},
        {";;m", "module", "module template", "`timescale 1ns / 1ps\nmodule name(\n);\nendmodule"},
        {";;i", "interface", "interface skeleton", "interface name();\nendinterface"},
        {";;t", "task", "task skeleton", "task automatic name();\nendtask"},
        {";;f", "function", "function skeleton", "function automatic void name();\nendfunction"},
        {";;ne", "enum type", "typedef enum", "typedef enum logic [0:0] {\n} name_e;"},
        {";;nsp", "packed struct", "packed struct type", "typedef struct packed {\n} name_t;"},
        {";;ns", "unpacked struct", "unpacked struct type", "typedef struct {\n} name_t;"},
        {";;d", "define", "define / ifdef block", "`define NAME\n`ifdef NAME\n`endif"},
    };
    return specs;
}

const TemplateCatalogSpec* templateCatalogSpec(const QString& token)
{
    for (const TemplateCatalogSpec& spec : templateCatalogSpecs()) {
        if (token == QString::fromLatin1(spec.token))
            return &spec;
    }
    return nullptr;
}

void appendInlineActions(QList<ActionDescriptor>* out)
{
    const quint32 semanticRequirements =
        ActionRequirements::Editor
        | ActionRequirements::SemanticCurrent;
    for (const InlineSpec& spec : inlineSemanticSpecs()) {
        ActionDescriptor descriptor =
            makeAction(QStringLiteral("completion.%1")
                           .arg(QString::fromLatin1(spec.suffix)),
                       QString::fromLatin1(spec.canonicalName),
                       QString::fromLatin1(spec.description),
                       ActionCategory::Inspect,
                       ActionScope::Editor,
                       QStringLiteral("completion.semantic"),
                       semanticRequirements,
                       QStringLiteral(
                           "Wait for the current semantic snapshot, then retry."),
                       ActionRecoveryPolicy::Analyze,
                       ActionParameterKind::TextQuery,
                       QStringLiteral("query"));
        descriptor.aliases.append(
            alias(ActionSurface::InlineSemantic,
                  QString::fromLatin1(spec.token),
                  QString::fromLatin1(spec.token),
                  QString::fromLatin1(spec.description),
                  QString::fromLatin1(spec.defaultValue),
                  QString::fromLatin1(spec.kindKey),
                  QStringLiteral("semantic")));
        descriptor.sharedExecutionRoute = true;
        out->append(descriptor);
    }

    ActionDescriptor visibleSymbols =
        makeAction(QStringLiteral("completion.visibleSymbols"),
                   QStringLiteral("Search Visible Symbols"),
                   QStringLiteral("current-scope visible symbols"),
                   ActionCategory::Inspect,
                   ActionScope::Editor,
                   QStringLiteral("completion.semantic"),
                   semanticRequirements,
                   QStringLiteral(
                       "Wait for the current semantic snapshot, then retry."),
                   ActionRecoveryPolicy::Analyze,
                   ActionParameterKind::TextQuery,
                   QStringLiteral("query"));
    visibleSymbols.aliases.append(
        alias(ActionSurface::InlineSemantic,
              QStringLiteral(";v"),
              QStringLiteral(";v"),
              QStringLiteral("current-scope visible symbols"),
              QStringLiteral("symbol"),
              QStringLiteral("visibleSymbol"),
              QStringLiteral("semantic")));
    visibleSymbols.sharedExecutionRoute = true;
    out->append(visibleSymbols);

    for (const InlineSpec& spec : inlineSemanticSpecs()) {
        const QString semanticToken = QString::fromLatin1(spec.token);
        const QString templateToken =
            QStringLiteral(";%1").arg(semanticToken);
        const TemplateCatalogSpec* catalog =
            templateCatalogSpec(templateToken);
        const QString label = catalog
            ? QString::fromLatin1(catalog->label)
            : templateToken;
        const QString description = catalog
            ? QString::fromLatin1(catalog->description)
            : QString::fromLatin1(spec.description);
        const QString defaultValue = catalog
            ? QString::fromLatin1(catalog->defaultValue)
            : QString::fromLatin1(spec.defaultValue);

        ActionDescriptor descriptor =
            makeAction(QStringLiteral("template.%1")
                           .arg(QString::fromLatin1(spec.suffix)),
                       QStringLiteral("Insert %1 Template").arg(label),
                       description,
                       ActionCategory::Insert,
                       ActionScope::Editor,
                       QStringLiteral("completion.template"),
                       ActionRequirements::Editor,
                       QStringLiteral("Open an editor tab."),
                       ActionRecoveryPolicy::Explain,
                       ActionParameterKind::TemplateSeed,
                       QStringLiteral("seed"));
        descriptor.aliases.append(
            alias(ActionSurface::InlineTemplate,
                  templateToken,
                  catalog ? label : templateToken,
                  description,
                  defaultValue,
                  QString::fromLatin1(spec.kindKey),
                  QStringLiteral("template"),
                  true,
                  catalog != nullptr));
        descriptor.sharedExecutionRoute = true;
        out->append(descriptor);
    }

    for (const TemplateCatalogSpec& catalog :
         templateCatalogSpecs()) {
        if (!catalog.templateOnly)
            continue;

        const QString token = QString::fromLatin1(catalog.token);
        const QString actionSuffix =
            catalog.actionSuffix
            ? QString::fromLatin1(catalog.actionSuffix)
            : token.mid(2);
        const QString adapterKey =
            catalog.adapterKey
            ? QString::fromLatin1(catalog.adapterKey)
            : token.mid(2);
        const QString label = QString::fromLatin1(catalog.label);
        const QString description =
            QString::fromLatin1(catalog.description);
        ActionDescriptor descriptor =
            makeAction(QStringLiteral("template.%1")
                           .arg(actionSuffix),
                       QStringLiteral("Insert %1 Template").arg(label),
                       description,
                       ActionCategory::Insert,
                       ActionScope::Editor,
                       QStringLiteral("completion.template"),
                       ActionRequirements::Editor,
                       QStringLiteral("Open an editor tab."),
                       ActionRecoveryPolicy::Explain,
                       ActionParameterKind::TemplateSeed,
                       QStringLiteral("seed"));
        descriptor.aliases.append(
            alias(ActionSurface::InlineTemplate,
                  token,
                  label,
                  description,
                  QString::fromLatin1(catalog.defaultValue),
                  adapterKey,
                  QStringLiteral("template"),
                  true,
                  true));
        descriptor.sharedExecutionRoute = true;
        out->append(descriptor);
    }

    ActionDescriptor include =
        makeAction(QStringLiteral("completion.headerInclude"),
                   QStringLiteral("Search Header Includes"),
                   QStringLiteral("header includes"),
                   ActionCategory::Insert,
                   ActionScope::Workspace,
                   QStringLiteral("completion.headerInclude"),
                   ActionRequirements::Editor
                       | ActionRequirements::Workspace,
                   QStringLiteral("Open an editor in a workspace."),
                   ActionRecoveryPolicy::Explain,
                   ActionParameterKind::TextQuery,
                   QStringLiteral("query"));
    include.aliases.append(
        alias(ActionSurface::InlineSemantic,
              QStringLiteral(";h"),
              QStringLiteral(";h"),
              QStringLiteral("header includes"),
              QStringLiteral("`include \"...\""),
              QStringLiteral("user"),
              QStringLiteral("headerInclude")));
    out->append(include);

    ActionDescriptor packageImport =
        makeAction(QStringLiteral("completion.packageImport"),
                   QStringLiteral("Search Package Imports"),
                   QStringLiteral("package imports"),
                   ActionCategory::Insert,
                   ActionScope::Workspace,
                   QStringLiteral("completion.packageImport"),
                   ActionRequirements::Editor
                       | ActionRequirements::Workspace
                       | ActionRequirements::SemanticCurrent,
                   QStringLiteral(
                       "Open an analyzed editor in a workspace."),
                   ActionRecoveryPolicy::Analyze,
                   ActionParameterKind::TextQuery,
                   QStringLiteral("query"));
    packageImport.aliases.append(
        alias(ActionSurface::InlineSemantic,
              QStringLiteral(";pk"),
              QStringLiteral(";pk"),
              QStringLiteral("package imports"),
              QStringLiteral("import package_name::*;"),
              QStringLiteral("package"),
              QStringLiteral("packageImport")));
    out->append(packageImport);
}

void appendSourceActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* name;
        const char* description;
        const char* route;
        ActionCategory category;
        ActionScope scope;
        quint32 requirements;
        const char* shortcut = "";
    };
    const quint32 semanticSymbol =
        ActionRequirements::Editor
        | ActionRequirements::SemanticCurrent
        | ActionRequirements::Symbol;
    const QList<Spec> specs = {
        {"source.goToDefinition",
         "Go to Definition",
         "Navigate to the indexed definition of the current symbol.",
         "editor.source.goToDefinition",
         ActionCategory::Navigate,
         ActionScope::Symbol,
         semanticSymbol,
         "F12"},
        {"insight.signalKernelGraph",
         "Signal Kernel Graph",
         "Inspect the signal kernel graph for the current signal.",
         "insight.signalKernel.showSymbol",
         ActionCategory::Inspect,
         ActionScope::Symbol,
         semanticSymbol},
        {"insight.signalUsageHotspot",
         "Signal Usage Hotspot",
         "Inspect usage hotspots for the current signal.",
         "insight.signalUsageHotspot.showSymbol",
         ActionCategory::Inspect,
         ActionScope::Symbol,
         semanticSymbol},
        {"insight.stateTransitionGraph",
         "State Transition Graph",
         "Inspect state transitions for the current state symbol.",
         "insight.stateTransition.showSymbol",
         ActionCategory::Inspect,
         ActionScope::Symbol,
         semanticSymbol},
        {"insight.moduleBlockDiagram",
         "Module Block Diagram",
         "Inspect the block diagram for the current module or signal.",
         "insight.moduleBlock.showSymbol",
         ActionCategory::Inspect,
         ActionScope::Module,
         semanticSymbol},
    };
    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(QString::fromLatin1(spec.id),
                       QString::fromLatin1(spec.name),
                       QString::fromLatin1(spec.description),
                       spec.category,
                       spec.scope,
                       QString::fromLatin1(spec.route),
                       spec.requirements,
                       QStringLiteral(
                           "Select an indexed symbol in the current editor."),
                       ActionRecoveryPolicy::Explain);
        descriptor.defaultShortcut =
            QString::fromLatin1(spec.shortcut);
        if (descriptor.id != QStringLiteral("source.goToDefinition")) {
            descriptor.aliases.append(
                alias(ActionSurface::ContextMenu,
                      QString::fromLatin1(spec.id),
                      QString::fromLatin1(spec.name)));
        }
        out->append(descriptor);
    }

    ActionDescriptor expose =
        makeAction(QStringLiteral("refactor.exposeSignalToTop"),
                   QStringLiteral("Expose Signal to Top"),
                   QStringLiteral(
                       "Plan, preview, and apply a signal exposure through the active hierarchy."),
                   ActionCategory::Refactor,
                   ActionScope::Hierarchy,
                   QStringLiteral("rtledit.signal.exposeToTop"),
                   ActionRequirements::Editor
                       | ActionRequirements::Workspace
                       | ActionRequirements::SemanticCurrent
                       | ActionRequirements::Symbol
                       | ActionRequirements::Hierarchy,
                   QStringLiteral(
                       "Select an analyzed active top / instance."),
                   ActionRecoveryPolicy::SelectHierarchy,
                   ActionParameterKind::HierarchySelection,
                   QStringLiteral("hierarchy"));
    expose.aliases.append(
        alias(ActionSurface::ContextMenu,
              QStringLiteral("refactor.exposeSignalToTop"),
              QStringLiteral("Expose Signal to Top...")));
    out->append(expose);
}

void appendEditorContextMenuActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* name;
        const char* description;
        const char* route;
        ActionCategory category;
        ActionScope scope;
        const char* unavailable;
        const char* shortcut = "";
    };
    const QList<Spec> specs = {
        {"edit.undo",
         "Undo",
         "Undo the most recent editor transaction.",
         "editor.standard.undo",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Nothing to undo.",
         "Ctrl+Z"},
        {"edit.redo",
         "Redo",
         "Redo the most recently undone editor transaction.",
         "editor.standard.redo",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Nothing to redo.",
         "Ctrl+Y"},
        {"edit.cut",
         "Cut",
         "Cut the current text selection, or the complete current line.",
         "editor.standard.cut",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Open an editable editor tab.",
         "Ctrl+X"},
        {"edit.copy",
         "Copy",
         "Copy the current text selection, or the complete current line.",
         "editor.standard.copy",
         ActionCategory::Inspect,
         ActionScope::Editor,
         "Open an editor tab.",
         "Ctrl+C"},
        {"edit.paste",
         "Paste",
         "Paste clipboard text at the current cursor.",
         "editor.standard.paste",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "The clipboard has no text that can be pasted.",
         "Ctrl+V"},
        {"select.all",
         "Select All",
         "Select the complete editor document.",
         "editor.standard.selectAll",
         ActionCategory::Select,
         ActionScope::Editor,
         "Open an editor tab.",
         "Ctrl+A"},
        {"edit.find",
         "Find",
         "Open the editor find flow.",
         "editor.edit.find",
         ActionCategory::Inspect,
         ActionScope::Editor,
         "Open an editor tab.",
         "Ctrl+F"},
        {"edit.replace",
         "Replace",
         "Open the editor replace flow.",
         "editor.edit.replace",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Open an editor tab.",
         "Ctrl+H"},
        {ActionIds::EditToggleSelectionCase,
         "Toggle Selection Case",
         "Toggle the letter case of every character in the current selection.",
         "editor.edit.toggleSelectionCase",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Select editable text."},
        {ActionIds::EditReplaceSelectionWithSpaces,
         "Replace Selection with Spaces",
         "Replace selected text with the same number of spaces while preserving line breaks.",
         "editor.edit.replaceSelectionWithSpaces",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Select editable text."},
        {ActionIds::RefactorOrganizeSignalDeclarations,
         "Organize Signal Declarations",
         "Move top-level signal declarations into the declaration section of the current module.",
         "editor.structure.organizeSignalDeclarations",
         ActionCategory::Refactor,
         ActionScope::Module,
         "Place the cursor in a module with movable signal declarations."},
        {ActionIds::PinloomLinkSelection,
         "Link Code to Pinloom",
         "Attach the current symbol, always block, or continuous assign block to a Pinloom item.",
         "ui.pinloom.linkSelection",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Place the cursor on a symbol or inside an always/assign block in an open workspace."},
        {ActionIds::PinloomOpenLinkedContent,
         "Open Pinloom Bindings",
         "Open every Pinloom item attached to the current semantic code anchor.",
         "ui.pinloom.openLinkedContent",
         ActionCategory::Inspect,
         ActionScope::Editor,
         "Place the cursor inside code linked to Pinloom."},
        {ActionIds::PinloomToggleBindingMarkers,
         "Toggle Pinloom Binding Markers",
         "Show or hide Pinloom binding badges beside source code.",
         "ui.pinloom.toggleBindingMarkers",
         ActionCategory::Inspect,
         ActionScope::Editor,
         "Open an editor tab."},
        {"refactor.createSignalDefinition",
         "Create Signal Definition",
         "Create a declaration for the undeclared signal at the context cursor.",
         "editor.structure.createSignalDefinition",
         ActionCategory::Refactor,
         ActionScope::Symbol,
         "Place the cursor on an undeclared signal use."},
        {"refactor.editInstanceSlots",
         "Edit Instance Slots",
         "Enter structured slot editing for the current module instance.",
         "editor.structure.editInstanceSlots",
         ActionCategory::Refactor,
         ActionScope::Symbol,
         "Place the cursor on an instance with editable actuals."},
        {"refactor.createAssignmentQueue",
         "Create Assignment Queue",
         "Create assignment rows from the checked signal selection.",
         "editor.structure.createAssignmentQueue",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Select at least one signal."},
        {"format.commentLines",
         "Comment Lines",
         "Comment the selected lines or current line.",
         "editor.format.commentLines",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab.",
         "Ctrl+/"},
        {"format.uncommentLines",
         "Uncomment Lines",
         "Uncomment the selected lines or current line.",
         "editor.format.uncommentLines",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab.",
         "Ctrl+Shift+/"},
        {"format.indentLines",
         "Indent Lines",
         "Indent the selected lines or current line.",
         "editor.format.indentLines",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab.",
         "Ctrl+]"},
        {"format.unindentLines",
         "Unindent Lines",
         "Unindent the selected lines or current line.",
         "editor.format.unindentLines",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab.",
         "Ctrl+["},
        {"format.document",
         "Format Document",
         "Format the complete current document.",
         "editor.format.document",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab.",
         "Ctrl+Shift+I"}
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(QString::fromLatin1(spec.id),
                       QString::fromLatin1(spec.name),
                       QString::fromLatin1(spec.description),
                       spec.category,
                       spec.scope,
                       QString::fromLatin1(spec.route),
                       ActionRequirements::Editor,
                       QString::fromLatin1(spec.unavailable));
        descriptor.defaultShortcut =
            QString::fromLatin1(spec.shortcut);
        QString label = descriptor.canonicalName;
        if (descriptor.id == QStringLiteral("edit.find")
            || descriptor.id == QStringLiteral("edit.replace")) {
            label += QStringLiteral("...");
        }
        if (descriptor.id
            == QStringLiteral("refactor.createSignalDefinition")) {
            label += QStringLiteral("...");
        }
        if (descriptor.id
            == QString::fromLatin1(ActionIds::PinloomLinkSelection)) {
            label += QStringLiteral("...");
        }
        const bool standardEditorAction =
            descriptor.id == QStringLiteral("edit.undo")
            || descriptor.id == QStringLiteral("edit.redo")
            || descriptor.id == QStringLiteral("edit.cut")
            || descriptor.id == QStringLiteral("edit.copy")
            || descriptor.id == QStringLiteral("edit.paste")
            || descriptor.id == QStringLiteral("select.all")
            || descriptor.id == QStringLiteral("edit.find");
        const bool hiddenFromContextMenu =
            descriptor.id == QStringLiteral("edit.replace")
            || descriptor.category == ActionCategory::Format;
        if (!standardEditorAction && !hiddenFromContextMenu) {
            descriptor.aliases.append(
                alias(ActionSurface::ContextMenu,
                      descriptor.id,
                      label));
        }
        out->append(descriptor);
    }
}

void appendPackageActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* suffix;
        const char* token;
        const char* label;
        const char* description;
    };
    const QList<Spec> specs = {
        {"parameter", "parameter", "parameter", "package parameter declaration"},
        {"localparam", "localparam", "localparam", "package localparam declaration"},
        {"typedefEnum", "typedef_enum", "typedef enum", "package typedef enum"},
        {"typedefStruct", "typedef_struct", "typedef struct", "package typedef struct"},
        {"typedefStructPacked", "typedef_struct_packed", "typedef struct packed", "package typedef struct packed"},
        {"function", "function", "function", "package function declaration"},
    };
    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(QStringLiteral("package.insert.%1")
                           .arg(QString::fromLatin1(spec.suffix)),
                       QStringLiteral("Insert %1 in Package")
                           .arg(QString::fromLatin1(spec.label)),
                       QString::fromLatin1(spec.description),
                       ActionCategory::Insert,
                       ActionScope::Package,
                       QStringLiteral("editor.packageToolInsert"),
                       ActionRequirements::Editor
                           | ActionRequirements::Package,
                       QStringLiteral(
                           "Place the cursor in a syntactically valid package."),
                       ActionRecoveryPolicy::Explain,
                       ActionParameterKind::PackageTool,
                       QStringLiteral("kind"));
        descriptor.aliases.append(
            alias(ActionSurface::PackageTools,
                  QString::fromLatin1(spec.token),
                  QString::fromLatin1(spec.label),
                  QString::fromLatin1(spec.description),
                  QString(),
                  QString::fromLatin1(spec.suffix),
                  QStringLiteral("packageTool")));
        descriptor.sharedExecutionRoute = true;
        out->append(descriptor);
    }
}

void appendWorkspaceFileActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* canonicalName;
        const char* description;
        const char* route;
        const char* label;
        const char* objectName;
        ActionParameterKind parameterKind;
        const char* parameterName;
        bool highRisk;
    };
    const Spec specs[] = {
        {ActionIds::WorkspaceFileCreate,
         "New File",
         "Create an empty file inside the active workspace.",
         "workspace.file.create",
         "New File...",
         "workspaceFileCreateAction",
         ActionParameterKind::TextQuery,
         "name",
         false},
        {ActionIds::WorkspaceDirectoryCreate,
         "New Directory",
         "Create a directory inside the active workspace.",
         "workspace.directory.create",
         "New Directory...",
         "workspaceDirectoryCreateAction",
         ActionParameterKind::TextQuery,
         "name",
         false},
        {ActionIds::WorkspacePathRename,
         "Rename File or Directory",
         "Preview and rename a file or directory inside the active workspace.",
         "workspace.path.rename",
         "Rename...",
         "workspacePathRenameAction",
         ActionParameterKind::TextQuery,
         "name",
         true},
        {ActionIds::WorkspacePathDelete,
         "Move File or Directory to Trash",
         "Preview and move a file or directory to the operating-system Trash or Recycle Bin.",
         "workspace.path.delete",
         "Move to Trash...",
         "workspacePathDeleteAction",
         ActionParameterKind::FilePath,
         "path",
         true},
        {ActionIds::WorkspacePathCopy,
         "Copy Full Path",
         "Copy the normalized absolute path of a workspace file or directory.",
         "workspace.path.copy",
         "Copy Full Path",
         "workspacePathCopyAction",
         ActionParameterKind::FilePath,
         "path",
         false},
        {ActionIds::WorkspacePathReveal,
         "Show in File Explorer",
         "Reveal a workspace file or directory in the system file manager.",
         "workspace.path.reveal",
         "Show in File Explorer",
         "workspacePathRevealAction",
         ActionParameterKind::FilePath,
         "path",
         false},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(QString::fromLatin1(spec.id),
                       QString::fromLatin1(spec.canonicalName),
                       QString::fromLatin1(spec.description),
                       ActionCategory::Workspace,
                       ActionScope::Workspace,
                       QString::fromLatin1(spec.route),
                       ActionRequirements::Workspace,
                       QStringLiteral("Open a workspace first."),
                       ActionRecoveryPolicy::Explain,
                       spec.parameterKind,
                       QString::fromLatin1(spec.parameterName));
        descriptor.aliases.append(
            alias(ActionSurface::ContextMenu,
                  QString::fromLatin1(spec.id),
                  QString::fromLatin1(spec.label),
                  QString::fromLatin1(spec.description),
                  QString(),
                  QString::fromLatin1(spec.objectName)));
        if (spec.highRisk) {
            descriptor.riskLevel =
                ActionRiskLevel::High;
            descriptor.supportsDryRun = true;
        }
        out->append(descriptor);
    }
}

void appendNavigationContextActions(
    QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* canonicalName;
        const char* description;
        const char* route;
        const char* label;
        ActionCategory category;
    };
    const Spec specs[] = {
        {ActionIds::NavigationDesignGoInstantiation,
         "Go to Hierarchy Instantiation",
         "Open the source location that instantiates the selected design-hierarchy node.",
         "navigation.design.goInstantiation",
         "Go to Instantiation",
         ActionCategory::Navigate},
        {ActionIds::NavigationDesignGoDefinition,
         "Go to Hierarchy Module Definition",
         "Open the module definition represented by the selected design-hierarchy node.",
         "navigation.design.goDefinition",
         "Go to Module Definition",
         ActionCategory::Navigate},
        {ActionIds::NavigationDesignSetTop,
         "Set Design Top",
         "Use the selected module as the explicit design-hierarchy root.",
         "navigation.design.setTop",
         "Set as Design Top",
         ActionCategory::Workspace},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(
                QString::fromLatin1(spec.id),
                QString::fromLatin1(
                    spec.canonicalName),
                QString::fromLatin1(
                    spec.description),
                spec.category,
                ActionScope::Hierarchy,
                QString::fromLatin1(spec.route),
                ActionRequirements::Workspace
                    | ActionRequirements::Hierarchy,
                QStringLiteral(
                    "Select an available design-hierarchy node in an open workspace."),
                ActionRecoveryPolicy::SelectHierarchy,
                ActionParameterKind::HierarchySelection,
                QStringLiteral("hierarchyNode"));
        descriptor.repeatable = false;
        descriptor.rememberParameters = false;
        descriptor.aliases = {
            alias(
                ActionSurface::ContextMenu,
                descriptor.id,
                QString::fromLatin1(spec.label),
                descriptor.description),
            alias(
                ActionSurface::ActionCatalog,
                descriptor.id,
                QString::fromLatin1(
                    spec.canonicalName),
                descriptor.description,
                QString(),
                QString(),
                QString(),
                false,
                true),
        };
        out->append(descriptor);
    }
}

void appendWaveSimulationActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* canonicalName;
        const char* description;
        const char* route;
        const char* contextLabel;
        const char* contextAdapterKey;
        const char* menuAdapterKey;
        ActionScope scope;
        quint32 requirements;
        bool menu;
    };
    const Spec specs[] = {
        {ActionIds::WaveSimulationRunCurrentContext,
         "Run Wave Simulation",
         "Compile and simulate the current module; an enclosing always block narrows the initial observation scope.",
         "waveSimulation.runCurrentContext",
         "Run Wave Simulation",
         "runWaveSimulationContextAction",
         "runWaveSimulationAction",
         ActionScope::Module,
         ActionRequirements::Workspace
             | ActionRequirements::Editor
             | ActionRequirements::SemanticCurrent,
         true},
        {ActionIds::WaveSimulationObserveSignal,
         "Observe Signal in Wave Simulation",
         "Compile the current module and add the selected signal to the initial observation set.",
         "waveSimulation.observeSignal",
         "Observe Signal in Wave Simulation",
         "observeSignalInWaveSimulationAction",
         "",
         ActionScope::Symbol,
         ActionRequirements::Workspace
             | ActionRequirements::Editor
             | ActionRequirements::SemanticCurrent
             | ActionRequirements::Symbol,
         false},
        {ActionIds::WaveSimulationRevealSignalInResult,
         "Reveal Signal in Wave Result",
         "Locate the selected semantic signal in an open Wave Simulation result.",
         "waveSimulation.revealSignalInResult",
         "Reveal Signal in Wave Result",
         "revealSignalInWaveSimulationResultAction",
         "",
         ActionScope::Symbol,
         ActionRequirements::Workspace
             | ActionRequirements::Editor
             | ActionRequirements::SemanticCurrent
             | ActionRequirements::Symbol,
         false},
        {ActionIds::WaveSimulationRunDesignInstance,
         "Run Instance in Wave Simulation",
         "Compile and simulate the module represented by the selected design-hierarchy instance.",
         "waveSimulation.runDesignInstance",
         "Run Instance in Wave Simulation",
         "runDesignInstanceInWaveSimulationAction",
         "",
         ActionScope::Hierarchy,
         ActionRequirements::Workspace
             | ActionRequirements::SemanticCurrent
             | ActionRequirements::Hierarchy,
         false},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(QString::fromLatin1(spec.id),
                       QString::fromLatin1(spec.canonicalName),
                       QString::fromLatin1(spec.description),
                       ActionCategory::Inspect,
                       spec.scope,
                       QString::fromLatin1(spec.route),
                       spec.requirements,
                       QStringLiteral(
                           "Open an analyzed SystemVerilog workspace and select a compatible context."),
                       ActionRecoveryPolicy::Analyze);
        descriptor.repeatable = false;
        descriptor.rememberParameters = false;
        descriptor.aliases = {
            alias(ActionSurface::ContextMenu,
                  descriptor.id,
                  QString::fromLatin1(spec.contextLabel),
                  descriptor.description,
                  QString(),
                  QString::fromLatin1(
                      spec.contextAdapterKey)),
            alias(ActionSurface::ActionCatalog,
                  descriptor.id,
                  descriptor.canonicalName,
                  descriptor.description,
                  QString(),
                  QString(),
                  QString(),
                  false,
                  true),
        };
        if (spec.menu) {
            descriptor.aliases.append(
                alias(ActionSurface::Menu,
                      descriptor.id,
                      descriptor.canonicalName,
                      descriptor.description,
                      QString(),
                      QString::fromLatin1(
                          spec.menuAdapterKey)));
        }
        out->append(descriptor);
    }
}

void appendApplicationMenuActions(
    QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* canonicalName;
        const char* description;
        const char* route;
        const char* label;
        const char* objectName;
        ActionCategory category;
        ActionScope scope;
        quint32 requirements;
        const char* unavailableReason;
        const char* shortcut = "";
    };
    // Per-workspace switch entries carry a runtime workspace identity, so
    // they remain parameterized adapters instead of static menu descriptors.
    const Spec specs[] = {
        {ActionIds::ViewNavigation,
         "Toggle Navigation",
         "Show or hide the Navigation panel.",
         "ui.panel.navigation.toggle",
         "Navigation",
         "viewNavigationAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The Navigation panel is unavailable.",
         "Ctrl+1"},
        {ActionIds::ViewContextSidebar,
         "Toggle Context Sidebar",
         "Show or hide the Context sidebar without closing its sections.",
         "ui.panel.contextSidebar.toggle",
         "Context Sidebar",
         "viewContextSidebarAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "Open a Context view from the rail before showing the sidebar.",
         "Ctrl+2"},
        {ActionIds::ViewScopedSearch,
         "Search and Replace",
         "Search the current syntax block, module, file, or workspace and build a checked replacement preview.",
         "ui.panel.scopedSearch.show",
         "Search and Replace...",
         "viewScopedSearchAction",
         ActionCategory::Navigate,
         ActionScope::Workspace,
         ActionRequirements::Workspace,
         "Open a workspace before searching.",
         "Ctrl+Shift+F"},
        {ActionIds::ViewEditorSplitLeft,
         "Split Editor Left",
         "Create an editor split to the left of the current split.",
         "ui.editorLayout.split.left",
         "Split Left",
         "splitEditorLeftAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before splitting the editor."},
        {ActionIds::ViewEditorSplitRight,
         "Split Editor Right",
         "Create an editor split to the right of the current split.",
         "ui.editorLayout.split.right",
         "Split Right",
         "splitEditorRightAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before splitting the editor."},
        {ActionIds::ViewEditorSplitAbove,
         "Split Editor Above",
         "Create an editor split above the current split.",
         "ui.editorLayout.split.above",
         "Split Above",
         "splitEditorAboveAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before splitting the editor."},
        {ActionIds::ViewEditorSplitBelow,
         "Split Editor Below",
         "Create an editor split below the current split.",
         "ui.editorLayout.split.below",
         "Split Below",
         "splitEditorBelowAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before splitting the editor."},
        {ActionIds::ViewEditorSplitMaximize,
         "Maximize Current Split",
         "Toggle maximization of the current editor split.",
         "ui.editorLayout.split.toggleMaximized",
         "Maximize Current Split",
         "maximizeEditorSplitAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor split before maximizing it."},
        {ActionIds::ViewEditorSplitsEqualize,
         "Equalize Split Sizes",
         "Distribute the available editor area equally between splits.",
         "ui.editorLayout.split.equalize",
         "Equalize Split Sizes",
         "equalizeEditorSplitsAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open multiple editor splits before equalizing them."},
        {ActionIds::ViewEditorSplitMerge,
         "Merge Current Split",
         "Merge the current editor split into the remaining layout.",
         "ui.editorLayout.split.merge",
         "Merge Current Split",
         "mergeEditorSplitAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open multiple editor splits before merging one."},
        {ActionIds::ViewReopenClosedTab,
         "Reopen Closed Tab",
         "Reopen the most recently closed editor tab.",
         "ui.editorTabs.reopenClosed",
         "Reopen Closed Tab",
         "reopenClosedTabAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         0,
         "No recently closed tab is available.",
         "Ctrl+Shift+T"},
        {ActionIds::ViewGroupTabsNone,
         "Group Tabs by None",
         "Display editor tabs without automatic grouping.",
         "ui.editorTabs.group.none",
         "None",
         "groupTabsNoneAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before changing tab grouping."},
        {ActionIds::ViewGroupTabsModule,
         "Group Tabs by Module",
         "Group editor tabs by SystemVerilog module.",
         "ui.editorTabs.group.module",
         "Module",
         "groupTabsModuleAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before changing tab grouping."},
        {ActionIds::ViewGroupTabsWorkspace,
         "Group Tabs by Workspace",
         "Group editor tabs by their workspace.",
         "ui.editorTabs.group.workspace",
         "Workspace",
         "groupTabsWorkspaceAction",
         ActionCategory::Workspace,
         ActionScope::Editor,
         ActionRequirements::Editor,
         "Open an editor tab before changing tab grouping."},
        {ActionIds::ViewProblems,
         "Toggle Problems Panel",
         "Show or hide the Problems panel.",
         "ui.panel.problems.toggle",
         "Problems",
         "viewProblemsAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The Problems panel is unavailable."},
        {ActionIds::ViewActivity,
         "Toggle Activity / Output Panel",
         "Show or hide the Activity / Output panel.",
         "ui.panel.activity.toggle",
         "Activity / Output",
         "viewActivityAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The Activity / Output panel is unavailable."},
        {ActionIds::ViewWavePreview,
         "Open Wave in Live Insights",
         "Open the Wave provider in the right-side Live Insights workspace.",
         "ui.panel.wavePreview.toggle",
         "Wave in Live Insights",
         "viewWavePreviewAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The Live Insights Wave provider is unavailable."},
        {ActionIds::ViewBottomPanelCollapsed,
         "Toggle Bottom Panel Collapse",
         "Collapse or restore the bottom panel area.",
         "ui.bottomPanel.collapsed.toggle",
         "Collapse Bottom Panel",
         "toggleBottomPanelCollapsedAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The bottom panel area is unavailable.",
         "Ctrl+J"},
        {ActionIds::ViewBottomPanelPinned,
         "Toggle Active Bottom Page Pin",
         "Pin or unpin the active bottom page.",
         "ui.bottomPanel.pinned.toggle",
         "Pin Active Bottom Page",
         "pinActiveBottomPanelAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "No active bottom page is available."},
        {ActionIds::ViewBottomPanelClose,
         "Close Active Bottom Page",
         "Close the active unpinned bottom page.",
         "ui.bottomPanel.closeActive",
         "Close Active Bottom Page",
         "closeActiveBottomPanelAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "No closable bottom page is active."},
        {ActionIds::ViewFoldShelf,
         "Toggle Fold Shelf",
         "Show or hide the Fold Shelf.",
         "ui.panel.foldShelf.toggle",
         "Fold Shelf",
         "viewFoldShelfAction",
         ActionCategory::Fold,
         ActionScope::Workspace,
         0,
         "The Fold Shelf is unavailable."},
        {ActionIds::ViewSettingsCenter,
         "Settings",
         "Open the unified global and workspace Settings Center.",
         "ui.settingsCenter.show",
         "Settings",
         "viewSettingsCenterAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The Settings Center is unavailable."},
        {ActionIds::ViewResetPanelLayout,
         "Reset Panel Layout",
         "Restore the default dock and panel layout.",
         "ui.panelLayout.reset",
         "Reset Panel Layout",
         "resetPanelLayoutAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The panel layout controller is unavailable."},
        {ActionIds::WorkspaceCloseActive,
         "Close Active Workspace",
         "Close the active workspace and its workspace tabs.",
         "ui.workspace.closeActive",
         "Close Active Workspace",
         "closeActiveWorkspaceAction",
         ActionCategory::Workspace,
         ActionScope::Workspace,
         ActionRequirements::Workspace,
         "Open a workspace before closing it."},
        {ActionIds::WorkspaceConfigure,
         "Configure Workspace",
         "Edit the active workspace configuration.",
         "ui.workspace.configure",
         "Workspace Configuration...",
         "workspaceConfigurationAction",
         ActionCategory::Workspace,
         ActionScope::Workspace,
         ActionRequirements::Workspace,
         "Open a workspace before configuring it."},
        {ActionIds::WorkspaceNextDiagnostic,
         "Next Diagnostic",
         "Navigate to the next diagnostic in the active diagnostic scope.",
         "ui.diagnostics.next",
         "Next Diagnostic",
         "nextDiagnosticAction",
         ActionCategory::Navigate,
         ActionScope::Workspace,
         ActionRequirements::Workspace,
         "Open a workspace with diagnostics.",
         "F8"},
        {ActionIds::WorkspacePreviousDiagnostic,
         "Previous Diagnostic",
         "Navigate to the previous diagnostic in the active diagnostic scope.",
         "ui.diagnostics.previous",
         "Previous Diagnostic",
         "previousDiagnosticAction",
         ActionCategory::Navigate,
         ActionScope::Workspace,
         ActionRequirements::Workspace,
         "Open a workspace with diagnostics.",
         "Shift+F8"},
        {ActionIds::UserTemplatesOpenGlobal,
         "Open Global User Templates",
         "Open the global user-template JSON file.",
         "ui.userTemplates.openGlobal",
         "Open Global User Templates",
         "openGlobalUserTemplatesAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "The global user-template file cannot be opened."},
        {ActionIds::UserTemplatesOpenWorkspace,
         "Open Workspace User Templates",
         "Open the active workspace's user-template JSON file.",
         "ui.userTemplates.openWorkspace",
         "Open Workspace User Templates",
         "openWorkspaceUserTemplatesAction",
         ActionCategory::Workspace,
         ActionScope::Workspace,
         ActionRequirements::Workspace,
         "Open a workspace before opening its user templates."},
        {ActionIds::UserTemplatesReload,
         "Reload User Templates",
         "Reload global and active-workspace user templates.",
         "ui.userTemplates.reload",
         "Reload User Templates",
         "reloadUserTemplatesAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "User templates could not be reloaded."},
        {ActionIds::ReviewCrashRecovery,
         "Review Crash Recovery",
         "Review, compare, restore, or discard crash-recovery copies.",
         "ui.crashRecovery.review",
         "Review Crash Recovery...",
         "reviewCrashRecoveryAction",
         ActionCategory::Workspace,
         ActionScope::Application,
         0,
         "Crash-recovery review is unavailable."},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(
                QString::fromLatin1(spec.id),
                QString::fromLatin1(
                    spec.canonicalName),
                QString::fromLatin1(
                    spec.description),
                spec.category,
                spec.scope,
                QString::fromLatin1(spec.route),
                spec.requirements,
                QString::fromLatin1(
                    spec.unavailableReason),
                spec.requirements == 0
                    ? ActionRecoveryPolicy::None
                    : ActionRecoveryPolicy::Explain);
        descriptor.defaultShortcut =
            QString::fromLatin1(spec.shortcut);
        descriptor.aliases = {
            alias(
                ActionSurface::ActionCatalog,
                QString::fromLatin1(spec.id),
                QString::fromLatin1(
                    spec.canonicalName),
                QString::fromLatin1(
                    spec.description),
                QString(),
                QString(),
                QString(),
                false,
                true),
        };
        const bool hasVisibleMenuEntry =
            descriptor.id
                    != QString::fromLatin1(
                        ActionIds::ViewWavePreview)
            && descriptor.id
                    != QString::fromLatin1(
                        ActionIds::ViewBottomPanelPinned)
            && descriptor.id
                    != QString::fromLatin1(
                        ActionIds::ViewBottomPanelClose);
        if (hasVisibleMenuEntry) {
            descriptor.aliases.prepend(
                alias(
                    ActionSurface::Menu,
                    QString::fromLatin1(spec.id),
                    QString::fromLatin1(spec.label),
                    QString::fromLatin1(
                        spec.description),
                    QString(),
                    QString::fromLatin1(
                        spec.objectName)));
        }
        QString tabContextLabel;
        QString commandToken;
        if (descriptor.id
            == QString::fromLatin1(
                ActionIds::ViewReopenClosedTab)) {
            tabContextLabel =
                QStringLiteral("Reopen Closed Tab");
            commandToken =
                QStringLiteral("reopen closed tab");
        } else if (descriptor.id
                   == QString::fromLatin1(
                       ActionIds::ViewEditorSplitLeft)) {
            tabContextLabel = QStringLiteral("Split Left");
            commandToken = QStringLiteral("split left");
        } else if (descriptor.id
                   == QString::fromLatin1(
                       ActionIds::ViewEditorSplitRight)) {
            tabContextLabel = QStringLiteral("Split Right");
            commandToken = QStringLiteral("split right");
        } else if (descriptor.id
                   == QString::fromLatin1(
                       ActionIds::ViewEditorSplitAbove)) {
            tabContextLabel = QStringLiteral("Split Above");
            commandToken = QStringLiteral("split above");
        } else if (descriptor.id
                   == QString::fromLatin1(
                       ActionIds::ViewEditorSplitBelow)) {
            tabContextLabel = QStringLiteral("Split Below");
            commandToken = QStringLiteral("split below");
        } else if (descriptor.id
                   == QString::fromLatin1(
                       ActionIds::ViewEditorSplitMerge)) {
            tabContextLabel = QStringLiteral("Merge Group");
            commandToken = QStringLiteral("merge split");
        }
        if (!tabContextLabel.isEmpty()) {
            descriptor.aliases.append(
                alias(
                    ActionSurface::TabContextMenu,
                    descriptor.id,
                    tabContextLabel,
                    descriptor.description));
            descriptor.aliases.append(
                alias(
                    ActionSurface::CommandLayer,
                    commandToken,
                    tabContextLabel,
                    descriptor.description));
        }
        QString panelContextLabel;
        QString panelCommandToken;
        if (descriptor.id
            == QString::fromLatin1(
                ActionIds::ViewBottomPanelPinned)) {
            panelContextLabel =
                QStringLiteral("Pin Page");
            panelCommandToken =
                QStringLiteral("toggle bottom page pin");
        } else if (descriptor.id
                   == QString::fromLatin1(
                       ActionIds::ViewBottomPanelClose)) {
            panelContextLabel =
                QStringLiteral("Close Page");
            panelCommandToken =
                QStringLiteral("close bottom page");
        }
        if (!panelContextLabel.isEmpty()) {
            descriptor.repeatable = false;
            descriptor.aliases.append(
                alias(
                    ActionSurface::PanelContextMenu,
                    descriptor.id,
                    panelContextLabel,
                    descriptor.description));
            descriptor.aliases.append(
                alias(
                    ActionSurface::CommandLayer,
                    panelCommandToken,
                    panelContextLabel,
                    descriptor.description));
        }
        out->append(descriptor);
    }

    ActionDescriptor globalControl =
        makeAction(
            QString::fromLatin1(
                ActionIds::ViewGlobalControl),
            QStringLiteral("Global Control"),
            QStringLiteral(
                "Open the workspace and fold command palette."),
            ActionCategory::Navigate,
            ActionScope::Application,
            QStringLiteral("ui.globalControl.show"));
    globalControl.defaultShortcut =
        QStringLiteral("Ctrl+Space");
    globalControl.aliases.append(
        alias(
            ActionSurface::ActionCatalog,
            QString::fromLatin1(
                ActionIds::ViewGlobalControl),
            QStringLiteral("Global Control"),
            globalControl.description,
            QString(),
            QString(),
            QString(),
            false,
            true));
    out->append(globalControl);

    ActionDescriptor commandMode =
        makeAction(
            QString::fromLatin1(
                ActionIds::ViewCommandMode),
            QStringLiteral("Command Mode"),
            QStringLiteral(
                "Hold the command shortcut to search and execute registered Actions."),
            ActionCategory::Navigate,
            ActionScope::Editor,
            QStringLiteral("ui.commandMode.show"),
            ActionRequirements::Editor,
            QStringLiteral(
                "Open an editor tab before entering Command Mode."),
            ActionRecoveryPolicy::Explain);
    commandMode.defaultShortcut =
        QStringLiteral("F24");
    commandMode.repeatable = false;
    commandMode.aliases.append(
        alias(
            ActionSurface::ActionCatalog,
            QString::fromLatin1(
                ActionIds::ViewCommandMode),
            QStringLiteral("Command Mode"),
            commandMode.description,
            QString(),
            QString(),
            QString(),
            false,
            true));
    out->append(commandMode);

    ActionDescriptor columnNumbers =
        makeAction(
            QString::fromLatin1(
                ActionIds::InsertColumnNumbers),
            QStringLiteral("Column Number Tool"),
            QStringLiteral(
                "Preview and insert a number sequence across the active column selection."),
            ActionCategory::Insert,
            ActionScope::Editor,
            QStringLiteral("editor.columnNumbers.show"),
            ActionRequirements::Editor,
            QStringLiteral(
                "Create a column selection before opening the Column Number Tool."),
            ActionRecoveryPolicy::Explain);
    columnNumbers.defaultShortcut =
        QStringLiteral("Alt+C");
    columnNumbers.repeatable = false;
    columnNumbers.aliases = {
        alias(
            ActionSurface::CommandLayer,
            QStringLiteral("column number"),
            QStringLiteral("Column Number Tool"),
            columnNumbers.description),
        alias(
            ActionSurface::ActionCatalog,
            QString::fromLatin1(
                ActionIds::InsertColumnNumbers),
            QStringLiteral("Column Number Tool"),
            columnNumbers.description,
            QString(),
            QString(),
            QString(),
            false,
            true),
    };
    out->append(columnNumbers);

    ActionDescriptor deleteShelfItem =
        makeAction(
            QString::fromLatin1(
                ActionIds::FoldShelfDeleteSelected),
            QStringLiteral("Delete Selected Fold Shelf Item"),
            QStringLiteral(
                "Delete the selected Fold Shelf item after applying moved-block safeguards."),
            ActionCategory::Fold,
            ActionScope::Workspace,
            QStringLiteral("ui.foldShelf.deleteSelected"),
            ActionRequirements::Workspace,
            QStringLiteral(
                "Open a workspace and select a Fold Shelf item before deleting it."),
            ActionRecoveryPolicy::Explain);
    deleteShelfItem.defaultShortcut =
        QStringLiteral("Delete");
    deleteShelfItem.repeatable = false;
    deleteShelfItem.aliases = {
        alias(
            ActionSurface::CommandLayer,
            QStringLiteral("fold shelf delete"),
            QStringLiteral("Delete Selected Fold Shelf Item"),
            deleteShelfItem.description),
        alias(
            ActionSurface::ActionCatalog,
            QString::fromLatin1(
                ActionIds::FoldShelfDeleteSelected),
            QStringLiteral("Delete Selected Fold Shelf Item"),
            deleteShelfItem.description,
            QString(),
            QString(),
            QString(),
            false,
            true),
    };
    out->append(deleteShelfItem);

    struct TabActionSpec {
        const char* id;
        const char* name;
        const char* description;
        const char* route;
        const char* label;
        const char* command;
    };
    const TabActionSpec tabActions[] = {
        {ActionIds::ViewEditorTabClose,
         "Close Tab",
         "Close the selected editor view after applying lock and unsaved-document safeguards.",
         "ui.editorTabs.close",
         "Close",
         "close tab"},
        {ActionIds::ViewEditorTabCloseOthers,
         "Close Other Tabs",
         "Close all unlocked editor views in the selected group except the selected view.",
         "ui.editorTabs.closeOthers",
         "Close Others",
         "close other tabs"},
        {ActionIds::ViewEditorTabCloseRight,
         "Close Tabs to the Right",
         "Close all unlocked editor views to the right of the selected view.",
         "ui.editorTabs.closeRight",
         "Close to the Right",
         "close tabs right"},
        {ActionIds::ViewEditorTabCloseAll,
         "Close All Tabs",
         "Close all unlocked editor views after one atomic unsaved-document review.",
         "ui.editorTabs.closeAll",
         "Close All",
         "close all tabs"},
        {ActionIds::ViewEditorTabDuplicate,
         "Duplicate Editor View",
         "Create another view of the selected shared Document in the same group.",
         "ui.editorTabs.duplicateView",
         "Duplicate View",
         "duplicate view"},
        {ActionIds::ViewTemporaryEditorOpen,
         "Open in Temporary Editor",
         "Open the selected complete shared Document in the editor-region temporary drawer without changing the split layout.",
         "ui.temporaryEditor.open",
         "Open in Temporary Editor",
         "open in temporary editor"},
        {ActionIds::ViewEditorTabToggleLocked,
         "Toggle Tab Lock",
         "Lock or unlock the selected editor view without changing its shared Document.",
         "ui.editorTabs.toggleLocked",
         "Lock Tab",
         "toggle tab lock"},
    };
    for (const TabActionSpec& spec : tabActions) {
        const QString actionId =
            QString::fromLatin1(spec.id);
        const bool temporaryEditorAction =
            actionId == QString::fromLatin1(
                ActionIds::ViewTemporaryEditorOpen);
        ActionDescriptor descriptor =
            makeAction(
                actionId,
                QString::fromLatin1(spec.name),
                QString::fromLatin1(spec.description),
                temporaryEditorAction
                    ? ActionCategory::Navigate
                    : ActionCategory::Workspace,
                ActionScope::Editor,
                QString::fromLatin1(spec.route),
                temporaryEditorAction
                    ? 0u
                    : ActionRequirements::Editor,
                temporaryEditorAction
                    ? QStringLiteral(
                          "Select a file or source location before running this Action.")
                    : QStringLiteral(
                          "Open or select an editor tab before running this Action."),
                ActionRecoveryPolicy::Explain);
        descriptor.repeatable = false;
        descriptor.aliases = {
            alias(
                ActionSurface::TabContextMenu,
                descriptor.id,
                QString::fromLatin1(spec.label),
                descriptor.description),
            alias(
                ActionSurface::CommandLayer,
                QString::fromLatin1(spec.command),
                QString::fromLatin1(spec.label),
                descriptor.description),
            alias(
                ActionSurface::ActionCatalog,
                descriptor.id,
                QString::fromLatin1(spec.name),
                descriptor.description,
                QString(),
                QString(),
                QString(),
                false,
                true),
        };
        if (temporaryEditorAction) {
            descriptor.aliases.prepend(
                alias(
                    ActionSurface::ContextMenu,
                    descriptor.id,
                    QString::fromLatin1(spec.label),
                    descriptor.description));
        }
        out->append(descriptor);
    }
}

void appendRtlEditMenuActions(
    QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* name;
        const char* description;
        const char* route;
        const char* label;
        const char* objectName;
    };
    const Spec specs[] = {
        {ActionIds::RtlConnectInstancePair,
         "Connect Instance Pair",
         "Select two concrete module instances and build a structural "
         "High+Diff connection transaction from the current signal.",
         "rtledit.instancePair.connect",
         "Connect Instance Pair...",
         "connectInstancePairAction"},
        {ActionIds::RtlPropagateMultipleSignals,
         "Propagate Multiple Signals",
         "Build one atomic High+Diff transaction for the editor's "
         "selected signals, optional ancestor, and optional port group.",
         "rtledit.signal.propagateBatch",
         "Propagate Selected Signals...",
         "propagateMultipleSignalsAction"},
    };
    const quint32 requirements =
        ActionRequirements::Editor
        | ActionRequirements::Workspace
        | ActionRequirements::SemanticCurrent
        | ActionRequirements::Symbol
        | ActionRequirements::Hierarchy;

    ActionDescriptor rename =
        makeAction(
            QString::fromLatin1(ActionIds::RtlRename),
            QStringLiteral("Rename SystemVerilog Symbol"),
            QStringLiteral(
                "Rename the selected SystemVerilog symbol. Single-file "
                "renames apply atomically; cross-file and structural "
                "renames use a High+Diff transaction."),
            ActionCategory::Refactor,
            ActionScope::Symbol,
            QStringLiteral("rtledit.rename"),
            ActionRequirements::Editor
                | ActionRequirements::Workspace
                | ActionRequirements::SemanticCurrent
                | ActionRequirements::Symbol,
            QStringLiteral(
                "Open a current analyzed SystemVerilog editor and select "
                "a module or interface port, parameter, or localparam."),
            ActionRecoveryPolicy::Analyze,
            ActionParameterKind::None);
    rename.defaultShortcut = QStringLiteral("Ctrl+R");
    rename.riskLevel = ActionRiskLevel::High;
    rename.supportsDryRun = true;
    rename.repeatable = true;
    rename.rememberParameters = false;
    rename.aliases = {
        alias(
            ActionSurface::Menu,
            rename.id,
            QStringLiteral("Rename SystemVerilog Symbol..."),
            rename.description,
            QString(),
            QStringLiteral("rtlRenameAction")),
        alias(
            ActionSurface::ActionCatalog,
            rename.id,
            rename.canonicalName,
            rename.description,
            QString(),
            QString(),
            QString(),
            false,
            true),
        alias(
            ActionSurface::CommandLayer,
            QStringLiteral("rename rtl symbol"),
            rename.canonicalName,
            rename.description),
    };
    out->append(rename);

    ActionDescriptor connectionTransform =
        makeAction(
            QString::fromLatin1(
                ActionIds::RtlConnectionTransform),
            QStringLiteral("Synchronize Instance Connections"),
            QStringLiteral(
                "Build one structured High+Diff transaction that safely "
                "synchronizes all source instances with the current Slang "
                "formal ports, removes obsolete connections, converts ordered "
                "connections, and inserts only provable casts."),
            ActionCategory::Refactor,
            ActionScope::Hierarchy,
            QStringLiteral("rtledit.connection.transform"),
            requirements,
            QStringLiteral(
                "Open a current analyzed SystemVerilog editor, bind an "
                "exact hierarchy instance, and select a module instance."),
            ActionRecoveryPolicy::Analyze,
            ActionParameterKind::HierarchySelection,
            QStringLiteral("connectionTransform"),
            QStringLiteral(
                "scope, missing-port, obsolete-port, and explicit-cast options"));
    connectionTransform.riskLevel = ActionRiskLevel::High;
    connectionTransform.supportsDryRun = true;
    connectionTransform.repeatable = true;
    connectionTransform.rememberParameters = true;
    connectionTransform.aliases = {
        alias(
            ActionSurface::Menu,
            connectionTransform.id,
            QStringLiteral("Synchronize Instance Connections..."),
            connectionTransform.description,
            QString(),
            QStringLiteral("rtlConnectionTransformAction")),
        alias(
            ActionSurface::ActionCatalog,
            connectionTransform.id,
            connectionTransform.canonicalName,
            connectionTransform.description,
            QString(),
            QString(),
            QString(),
            false,
            true),
        alias(
            ActionSurface::CommandLayer,
            QStringLiteral("synchronize instance connections"),
            connectionTransform.canonicalName,
            connectionTransform.description),
    };
    out->append(connectionTransform);

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(
                QString::fromLatin1(spec.id),
                QString::fromLatin1(spec.name),
                QString::fromLatin1(spec.description),
                ActionCategory::Refactor,
                ActionScope::Hierarchy,
                QString::fromLatin1(spec.route),
                requirements,
                QStringLiteral(
                    "Open a current analyzed SystemVerilog editor "
                    "with a concrete hierarchy and signal selection."),
                ActionRecoveryPolicy::Analyze,
                ActionParameterKind::HierarchySelection,
                QStringLiteral("selection"),
                QStringLiteral(
                    "instance paths, ancestor, and port-group options"));
        descriptor.riskLevel = ActionRiskLevel::High;
        descriptor.supportsDryRun = true;
        descriptor.repeatable = true;
        descriptor.rememberParameters = true;
        descriptor.aliases = {
            alias(
                ActionSurface::Menu,
                descriptor.id,
                QString::fromLatin1(spec.label),
                descriptor.description,
                QString(),
                QString::fromLatin1(spec.objectName)),
            alias(
                ActionSurface::ActionCatalog,
                descriptor.id,
                descriptor.canonicalName,
                descriptor.description,
                QString(),
                QString(),
                QString(),
                false,
                true),
            alias(
                ActionSurface::CommandLayer,
                descriptor.id
                        == QString::fromLatin1(
                            ActionIds::RtlConnectInstancePair)
                    ? QStringLiteral("connect instance pair")
                    : QStringLiteral("propagate selected signals"),
                descriptor.canonicalName,
                descriptor.description),
        };
        out->append(descriptor);
    }
}

void appendGlobalControlActions(QList<ActionDescriptor>* out)
{
    ActionDescriptor foldRegion =
        makeAction(QStringLiteral("fold.region"),
                   QStringLiteral("Fold Region"),
                   QStringLiteral(
                       "Fold Region - mark a custom fold block in the active editor"),
                   ActionCategory::Fold,
                   ActionScope::Editor,
                   QStringLiteral("globalControl.foldRegion"),
                   ActionRequirements::Editor,
                   QStringLiteral("Open an editor tab."),
                   ActionRecoveryPolicy::Explain);
    foldRegion.aliases.append(
        alias(ActionSurface::GlobalControl,
              QStringLiteral("fd r"),
              QStringLiteral("fd r")));
    out->append(foldRegion);

    ActionDescriptor foldShelf =
        makeAction(QStringLiteral("fold.shelf"),
                   QStringLiteral("Fold Shelf"),
                   QStringLiteral(
                       "Fold Shelf - drag custom fold blocks to or from the shelf"),
                   ActionCategory::Fold,
                   ActionScope::Workspace,
                   QStringLiteral("globalControl.foldShelf"),
                   ActionRequirements::Workspace,
                   QStringLiteral("Open a workspace first."),
                   ActionRecoveryPolicy::Explain);
    foldShelf.aliases.append(
        alias(ActionSurface::GlobalControl,
              QStringLiteral("fd s"),
              QStringLiteral("fd s")));
    out->append(foldShelf);

    ActionDescriptor openWorkspace =
        makeAction(QStringLiteral("workspace.openCount"),
                   QStringLiteral("Open Workspaces"),
                   QStringLiteral("Open one or more workspaces."),
                   ActionCategory::Workspace,
                   ActionScope::Application,
                   QStringLiteral("globalControl.openWorkspaces"),
                   0,
                   QString(),
                   ActionRecoveryPolicy::None,
                   ActionParameterKind::PositiveInteger,
                   QStringLiteral("count"),
                   QStringLiteral("<num>"));
    openWorkspace.aliases = {
        alias(ActionSurface::GlobalControl,
              QStringLiteral("ow <num>"),
              QStringLiteral("ow <num>")),
        alias(ActionSurface::GlobalControl,
              QStringLiteral("ow 1"),
              QStringLiteral("ow 1"),
              QStringLiteral("Open 1 workspace")),
        alias(ActionSurface::GlobalControl,
              QStringLiteral("ow 2"),
              QStringLiteral("ow 2"),
              QStringLiteral("Open 2 workspaces")),
    };
    out->append(openWorkspace);

    ActionDescriptor recent =
        makeAction(QStringLiteral("workspace.recent"),
                   QStringLiteral("Recent Workspaces"),
                   QStringLiteral("Open the recent-workspace list."),
                   ActionCategory::Workspace,
                   ActionScope::Application,
                   QStringLiteral("globalControl.recentWorkspaces"));
    recent.aliases.append(
        alias(ActionSurface::GlobalControl,
              QStringLiteral("ow r"),
              QStringLiteral("ow r"),
              QStringLiteral("Recent Workspaces")));
    out->append(recent);

    struct SessionSpec {
        const char* id;
        const char* token;
        const char* name;
        const char* description;
        const char* route;
    };
    const QList<SessionSpec> sessions = {
        {"workspace.session.save",
         "ow s save",
         "Save Local Workspace Session",
         "Save tabs, cursors, layout, and scan cache to local AppData; "
         "portable project configuration is unchanged",
         "globalControl.workspaceSession.save"},
        {"workspace.session.restore",
         "ow s restore",
         "Restore Local Workspace Session",
         "Restore local tabs, cursors, layout, and scan cache; "
         "legacy .zs is imported read-only when needed",
         "globalControl.workspaceSession.restore"},
        {"workspace.session.clean",
         "ow s clean",
         "Clear Local Workspace Session",
         "Clear this workspace's local UI/session partition; "
         "project.json and legacy .zs are unchanged",
         "globalControl.workspaceSession.clean"},
    };
    for (const SessionSpec& spec : sessions) {
        ActionDescriptor descriptor =
            makeAction(QString::fromLatin1(spec.id),
                       QString::fromLatin1(spec.name),
                       QString::fromLatin1(spec.description),
                       ActionCategory::Workspace,
                       ActionScope::Workspace,
                       QString::fromLatin1(spec.route),
                       ActionRequirements::Workspace,
                       QStringLiteral("Open a workspace first."),
                       ActionRecoveryPolicy::Explain);
        descriptor.aliases.append(
            alias(ActionSurface::GlobalControl,
                  QString::fromLatin1(spec.token),
                  QString::fromLatin1(spec.token),
                  QString::fromLatin1(spec.description)));
        out->append(descriptor);
    }
}

void appendGraphViewActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* canonicalName;
        const char* description;
        const char* route;
        const char* label;
        const char* objectName;
        const char* unavailableReason;
    };
    const Spec specs[] = {
        {ActionIds::GraphViewFit,
         "Fit Graph View",
         "Fit all active graph content into the current viewport.",
         "insight.graphView.fit",
         "Fit",
         "graphViewFitAction",
         "Render graph content before fitting the view."},
        {ActionIds::GraphViewZoomIn,
         "Zoom In Graph View",
         "Increase the active graph view zoom level.",
         "insight.graphView.zoomIn",
         "Zoom In",
         "graphViewZoomInAction",
         "Render graph content before zooming the view."},
        {ActionIds::GraphViewZoomOut,
         "Zoom Out Graph View",
         "Decrease the active graph view zoom level.",
         "insight.graphView.zoomOut",
         "Zoom Out",
         "graphViewZoomOutAction",
         "Render graph content before zooming the view."},
        {ActionIds::GraphViewCenterCurrent,
         "Center Current Graph Item",
         "Center the active graph on the current source location or selected item.",
         "insight.graphView.centerCurrent",
         "Center Current",
         "graphViewCenterCurrentAction",
         "Render graph content and select an item or current source location first."},
        {ActionIds::GraphViewResetLayout,
         "Reset Graph Layout",
         "Restore the active graph view zoom and panel layout defaults.",
         "insight.graphView.resetLayout",
         "Reset Layout",
         "graphViewResetLayoutAction",
         "Render graph content before resetting its layout."},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(
                QString::fromLatin1(spec.id),
                QString::fromLatin1(spec.canonicalName),
                QString::fromLatin1(spec.description),
                ActionCategory::Inspect,
                ActionScope::Application,
                QString::fromLatin1(spec.route),
                ActionRequirements::GraphContent,
                QString::fromLatin1(spec.unavailableReason),
                ActionRecoveryPolicy::Explain);
        descriptor.repeatable = false;
        descriptor.aliases = {
            alias(
                ActionSurface::GraphPanel,
                descriptor.id,
                QString::fromLatin1(spec.label),
                descriptor.description,
                QString(),
                QString::fromLatin1(spec.objectName)),
            alias(
                ActionSurface::ActionCatalog,
                descriptor.id,
                QString::fromLatin1(spec.canonicalName),
                descriptor.description,
                QString(),
                QString(),
                QString(),
                false,
                true),
        };
        out->append(descriptor);
    }
}

void appendGraphSelectionActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* canonicalName;
        const char* description;
        const char* route;
        const char* label;
        const char* objectName;
        const char* unavailableReason;
        ActionCategory category;
        ActionScope scope;
    };
    const Spec specs[] = {
        {ActionIds::GraphJumpSelected,
         "Jump to Selected Graph Item",
         "Open the source location represented by the selected graph item.",
         "insight.graph.jumpSelected",
         "Jump",
         "rtlGraphJumpAction",
         "Render a graph and select an item with a source location first.",
         ActionCategory::Navigate,
         ActionScope::Module},
        {ActionIds::GraphFocusSelected,
         "Focus Selected Graph Item",
         "Center the RTL Insights graph on the selected item.",
         "insight.graph.focusSelected",
         "Focus",
         "rtlGraphFocusAction",
         "Render a graph and select an item first.",
         ActionCategory::Inspect,
         ActionScope::Module},
        {ActionIds::GraphSetTopSelected,
         "Set Selected Module as Graph Top",
         "Rebuild the module block diagram with the selected resolved module as its top.",
         "insight.graph.setTopSelected",
         "Set Top",
         "rtlGraphSetTopAction",
         "Render a module block graph and select a resolved module first.",
         ActionCategory::Navigate,
         ActionScope::Hierarchy},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(
                QString::fromLatin1(spec.id),
                QString::fromLatin1(spec.canonicalName),
                QString::fromLatin1(spec.description),
                spec.category,
                spec.scope,
                QString::fromLatin1(spec.route),
                ActionRequirements::GraphContent,
                QString::fromLatin1(spec.unavailableReason),
                ActionRecoveryPolicy::Explain);
        descriptor.repeatable = false;
        descriptor.aliases = {
            alias(
                ActionSurface::GraphPanel,
                descriptor.id,
                QString::fromLatin1(spec.label),
                descriptor.description,
                QString(),
                QString::fromLatin1(spec.objectName)),
            alias(
                ActionSurface::ActionCatalog,
                descriptor.id,
                QString::fromLatin1(spec.canonicalName),
                descriptor.description,
                QString(),
                QString(),
                QString(),
                false,
                true),
        };
        out->append(descriptor);
    }
}

void appendGraphExportActions(QList<ActionDescriptor>* out)
{
    struct Spec {
        const char* id;
        const char* canonicalName;
        const char* description;
        const char* route;
        const char* label;
        const char* defaultFileName;
        const char* objectName;
        const char* unavailableReason;
        ActionScope scope;
    };
    const Spec specs[] = {
        {ActionIds::GraphExportRtlInsights,
         "Export FSM / Module Block Graph",
         "Export the active FSM or module block diagram as SVG, PDF, "
         "or high-resolution PNG.",
         "graphExport.rtlInsights",
         "Export Graph...",
         "rtl-graph",
         "rtlGraphExportAction",
         "Render an FSM or module block diagram before exporting.",
         ActionScope::Module},
        {ActionIds::GraphExportSignalKernel,
         "Export Signal Kernel Graph",
         "Export the current signal kernel graph as SVG, PDF, "
         "or high-resolution PNG.",
         "graphExport.signalKernel",
         "Export Graph...",
         "signal-kernel-graph",
         "signalKernelGraphExportAction",
         "Render a signal kernel graph before exporting.",
         ActionScope::Symbol},
        {ActionIds::GraphExportUsageHotspotTrack,
         "Export Usage Hotspot Track Graph",
         "Export the current usage hotspot track graph as SVG, PDF, "
         "or high-resolution PNG.",
         "graphExport.usageHotspotTrack",
         "Export Track Graph...",
         "signal-usage-hotspot-track",
         "signalUsageHotspotExportTrackAction",
         "Render a signal usage hotspot before exporting its track graph.",
         ActionScope::Symbol},
        {ActionIds::GraphExportUsageHotspotMatrix,
         "Export Usage Hotspot Matrix Graph",
         "Export the current usage hotspot matrix graph as SVG, PDF, "
         "or high-resolution PNG.",
         "graphExport.usageHotspotMatrix",
         "Export Matrix Graph...",
         "signal-usage-hotspot-matrix",
         "signalUsageHotspotExportMatrixAction",
         "Render a signal usage hotspot before exporting its matrix graph.",
         ActionScope::Symbol},
        {ActionIds::GraphExportWavePreview,
         "Export Wave Preview",
         "Export the current wave preview as SVG, PDF, "
         "or high-resolution PNG.",
         "graphExport.wavePreview",
         "Export Preview...",
         "wave-preview",
         "wavePreviewExportAction",
         "Render a wave preview before exporting.",
         ActionScope::Editor},
    };

    for (const Spec& spec : specs) {
        ActionDescriptor descriptor =
            makeAction(QString::fromLatin1(spec.id),
                       QString::fromLatin1(spec.canonicalName),
                       QString::fromLatin1(spec.description),
                       ActionCategory::Inspect,
                       spec.scope,
                       QString::fromLatin1(spec.route),
                       ActionRequirements::GraphContent,
                       QString::fromLatin1(spec.unavailableReason),
                       ActionRecoveryPolicy::Explain,
                       ActionParameterKind::FilePath,
                       QStringLiteral("outputPath"),
                       QStringLiteral("<svg|pdf|png path>"));
        descriptor.aliases.append(
            alias(ActionSurface::GraphPanel,
                  QString::fromLatin1(spec.id),
                  QString::fromLatin1(spec.label),
                  QString::fromLatin1(spec.description),
                  QString::fromLatin1(spec.defaultFileName),
                  QString::fromLatin1(spec.objectName)));
        out->append(descriptor);
    }
}

bool tokenMatchesAlias(const QString& token,
                       const QString& aliasToken)
{
    if (token.compare(aliasToken, Qt::CaseInsensitive) == 0)
        return true;

    const QString marker = QStringLiteral("<num>");
    const int markerOffset = aliasToken.indexOf(marker);
    if (markerOffset < 0)
        return false;
    const QString prefix = aliasToken.left(markerOffset);
    const QString suffix =
        aliasToken.mid(markerOffset + marker.size());
    if (!token.startsWith(prefix, Qt::CaseInsensitive)
        || !token.endsWith(suffix, Qt::CaseInsensitive)) {
        return false;
    }
    const int numberLength =
        token.size() - prefix.size() - suffix.size();
    bool ok = false;
    const int number =
        token.mid(prefix.size(), numberLength).toInt(&ok);
    return ok && number > 0;
}

void setReason(QString* reason, const QString& value)
{
    if (reason)
        *reason = value;
}
}

bool ActionDescriptor::hasSurface(ActionSurface surface) const
{
    for (const ActionAliasDescriptor& alias : aliases) {
        if (alias.surface == surface)
            return true;
    }
    return false;
}

void ActionExecutionHistory::clear()
{
    last = RecordedInvocation();
    parametersByWorkspaceAndAction.clear();
}

void ActionExecutionHistory::recordSuccessful(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation,
    const ActionExecutionResult& result)
{
    if (!result.handled || !result.succeeded
        || result.dryRun) {
        return;
    }

    if (descriptor.rememberParameters) {
        parametersByWorkspaceAndAction.insert(
            actionHistoryKey(invocation.workspaceId, descriptor.id),
            invocation.parameters);
    }
    if (!descriptor.repeatable
        || descriptor.id
               == QString::fromLatin1(
                   ActionIds::RepeatLastAction)) {
        return;
    }
    last = RecordedInvocation{descriptor.id, invocation};
}

QVariantMap ActionExecutionHistory::rememberedParameters(
    const QString& workspaceId,
    const QString& actionId) const
{
    return parametersByWorkspaceAndAction.value(
        actionHistoryKey(workspaceId, actionId));
}

QString ActionExecutionHistory::lastActionId() const
{
    return last.actionId;
}

bool ActionExecutionHistory::hasRepeatableAction() const
{
    const ActionDescriptor* descriptor =
        findActionById(last.actionId);
    return descriptor && descriptor->repeatable;
}

ActionExecutionResult ActionExecutionHistory::repeatLast(
    ActionExecutionHost& host)
{
    const ActionDescriptor* descriptor =
        findActionById(last.actionId);
    if (!descriptor || !descriptor->repeatable) {
        ActionExecutionResult result;
        result.handled = true;
        result.failureReason =
            QStringLiteral("No repeatable action is available.");
        return result;
    }

    ActionInvocation invocation = last.invocation;
    if (descriptor->rememberParameters) {
        invocation.parameters = rememberedParameters(
            invocation.workspaceId, descriptor->id);
    }
    if (descriptor->riskLevel == ActionRiskLevel::High
        && descriptor->supportsDryRun) {
        invocation.mode = ActionExecutionMode::DryRun;
    }
    return executeAction(*descriptor, host, invocation);
}

ActionExecutionHistory& applicationActionExecutionHistory()
{
    static ActionExecutionHistory history;
    return history;
}

void resetApplicationActionExecutionHistory()
{
    applicationActionExecutionHistory().clear();
}

ActionAliasDescriptor ActionDescriptor::aliasForSurface(
    ActionSurface surface) const
{
    for (const ActionAliasDescriptor& alias : aliases) {
        if (alias.surface == surface)
            return alias;
    }
    return {};
}

const QList<ActionDescriptor>& actionRegistry()
{
    static const QList<ActionDescriptor> registry = [] {
        QList<ActionDescriptor> result;
        appendCommandLayerActions(&result);
        appendFileCommandActions(&result);
        appendInlineActions(&result);
        appendSourceActions(&result);
        appendEditorContextMenuActions(&result);
        appendPackageActions(&result);
        appendWorkspaceFileActions(&result);
        appendNavigationContextActions(&result);
        appendWaveSimulationActions(&result);
        appendApplicationMenuActions(&result);
        appendRtlEditMenuActions(&result);
        appendGraphViewActions(&result);
        appendGraphSelectionActions(&result);
        appendGraphExportActions(&result);
        appendGlobalControlActions(&result);
        for (ActionDescriptor& descriptor : result) {
            if (descriptor.defaultShortcut.isEmpty()
                || descriptor.hasSurface(ActionSurface::Shortcut)) {
                continue;
            }
            descriptor.aliases.append(
                alias(ActionSurface::Shortcut,
                      descriptor.defaultShortcut,
                      descriptor.canonicalName));
        }
        return result;
    }();
    return registry;
}

const ActionDescriptor* findActionById(const QString& id)
{
    for (const ActionDescriptor& descriptor : actionRegistry()) {
        if (descriptor.id == id)
            return &descriptor;
    }
    return nullptr;
}

const ActionAliasDescriptor* findActionAlias(
    const ActionDescriptor& descriptor,
    ActionSurface surface,
    const QString& token)
{
    for (const ActionAliasDescriptor& alias : descriptor.aliases) {
        if (alias.surface != surface)
            continue;
        if (token.isEmpty()
            || token.compare(alias.token,
                             Qt::CaseInsensitive) == 0) {
            return &alias;
        }
    }
    if (token.isEmpty())
        return nullptr;
    for (const ActionAliasDescriptor& alias : descriptor.aliases) {
        if (alias.surface == surface
            && tokenMatchesAlias(token, alias.token)) {
            return &alias;
        }
    }
    return nullptr;
}

const ActionDescriptor* findActionByAlias(ActionSurface surface,
                                          const QString& token)
{
    for (const ActionDescriptor& descriptor : actionRegistry()) {
        if (findActionAlias(descriptor, surface, token))
            return &descriptor;
    }
    return nullptr;
}

QList<const ActionDescriptor*> actionDescriptorsForSurface(
    ActionSurface surface)
{
    QList<const ActionDescriptor*> result;
    for (const ActionDescriptor& descriptor : actionRegistry()) {
        if (descriptor.hasSurface(surface))
            result.append(&descriptor);
    }
    return result;
}

QList<ActionCatalogEntry> unifiedActionCatalog()
{
    QList<ActionCatalogEntry> result;
    result.reserve(actionRegistry().size());
    for (const ActionDescriptor& descriptor : actionRegistry()) {
        QStringList aliases;
        aliases.reserve(descriptor.aliases.size());
        for (const ActionAliasDescriptor& actionAlias :
             descriptor.aliases) {
            QString token = actionAlias.token;
            if (actionAlias.surface
                == ActionSurface::Shortcut) {
                token = effectiveActionShortcut(
                    descriptor.id);
                if (token.isEmpty())
                    continue;
            }
            aliases.append(
                QStringLiteral("%1: %2")
                    .arg(actionSurfaceText(actionAlias.surface),
                         token));
        }

        ActionCatalogEntry entry;
        entry.actionId = descriptor.id;
        entry.canonicalName = descriptor.canonicalName;
        entry.category = descriptor.category;
        entry.scope = descriptor.scope;
        entry.displayText =
            QStringLiteral("[%1 / %2] | %3")
                .arg(actionCategoryText(descriptor.category),
                     actionScopeText(descriptor.scope),
                     aliases.join(QStringLiteral(" | ")));
        result.append(entry);
    }
    return result;
}

bool validateActionRegistry(const QList<ActionDescriptor>& registry,
                            QString* reason)
{
    if (registry.isEmpty()) {
        setReason(reason, QStringLiteral("Action registry is empty"));
        return false;
    }

    QSet<QString> ids;
    QSet<QString> aliases;
    QHash<QString, QString> actionByRoute;
    QSet<QString> explicitlySharedRoutes;
    QHash<QString, QString> actionByShortcut;
    for (const ActionDescriptor& descriptor : registry) {
        if (!isValidActionId(descriptor.id)) {
            setReason(reason,
                      QStringLiteral("Invalid action id: %1")
                          .arg(descriptor.id));
            return false;
        }
        if (ids.contains(descriptor.id)) {
            setReason(reason,
                      QStringLiteral("Duplicate action id: %1")
                          .arg(descriptor.id));
            return false;
        }
        ids.insert(descriptor.id);
        if (descriptor.canonicalName.trimmed().isEmpty()
            || descriptor.description.trimmed().isEmpty()
            || descriptor.category == ActionCategory::Unknown
            || descriptor.scope == ActionScope::Unknown
            || descriptor.executionRoute.trimmed().isEmpty()
            || !descriptor.execute
            || descriptor.aliases.isEmpty()) {
            setReason(reason,
                      QStringLiteral("Incomplete action descriptor: %1")
                          .arg(descriptor.id));
            return false;
        }
        if (descriptor.riskLevel == ActionRiskLevel::High
            && !descriptor.supportsDryRun) {
            setReason(reason,
                      QStringLiteral("High-risk action lacks dry-run: %1")
                          .arg(descriptor.id));
            return false;
        }
        if (descriptor.requirementMask != 0
            && descriptor.unavailableReason.trimmed().isEmpty()) {
            setReason(reason,
                      QStringLiteral("Missing unavailable reason: %1")
                          .arg(descriptor.id));
            return false;
        }
        if (descriptor.parameterModel.kind
                != ActionParameterKind::None
            && descriptor.parameterModel.name.trimmed().isEmpty()) {
            setReason(reason,
                      QStringLiteral("Missing parameter name: %1")
                          .arg(descriptor.id));
            return false;
        }

        const QString routeKey =
            descriptor.executionRoute.trimmed().toCaseFolded();
        const auto routeOwner =
            actionByRoute.constFind(routeKey);
        if (routeOwner != actionByRoute.constEnd()) {
            if (!descriptor.sharedExecutionRoute
                || !explicitlySharedRoutes.contains(routeKey)) {
                setReason(
                    reason,
                    QStringLiteral(
                        "Execution route '%1' conflicts between '%2' and '%3'.")
                        .arg(descriptor.executionRoute,
                             routeOwner.value(),
                             descriptor.id));
                return false;
            }
        } else {
            actionByRoute.insert(routeKey, descriptor.id);
            if (descriptor.sharedExecutionRoute)
                explicitlySharedRoutes.insert(routeKey);
        }

        QString normalizedShortcut;
        if (!descriptor.defaultShortcut.trimmed().isEmpty()) {
            const QKeySequence shortcut =
                QKeySequence::fromString(
                    descriptor.defaultShortcut.trimmed(),
                    QKeySequence::PortableText);
            normalizedShortcut =
                shortcut.toString(
                    QKeySequence::PortableText);
            if (shortcut.isEmpty()
                || normalizedShortcut.isEmpty()) {
                setReason(
                    reason,
                    QStringLiteral(
                        "Invalid default shortcut for action: %1")
                        .arg(descriptor.id));
                return false;
            }
            const QString shortcutKey =
                normalizedShortcut.toCaseFolded();
            const auto shortcutOwner =
                actionByShortcut.constFind(shortcutKey);
            if (shortcutOwner
                != actionByShortcut.constEnd()) {
                setReason(
                    reason,
                    QStringLiteral(
                        "Shortcut '%1' conflicts between '%2' and '%3'.")
                        .arg(normalizedShortcut,
                             shortcutOwner.value(),
                             descriptor.id));
                return false;
            }
            actionByShortcut.insert(
                shortcutKey, descriptor.id);
        }

        int shortcutAliasCount = 0;
        for (const ActionAliasDescriptor& alias : descriptor.aliases) {
            if (alias.surface == ActionSurface::Unknown
                || alias.token.trimmed().isEmpty()) {
                setReason(reason,
                          QStringLiteral("Invalid alias for action: %1")
                              .arg(descriptor.id));
                return false;
            }
            const QString key =
                QStringLiteral("%1:%2")
                    .arg(static_cast<int>(alias.surface))
                    .arg(alias.token.toCaseFolded());
            if (aliases.contains(key)) {
                setReason(reason,
                          QStringLiteral("Duplicate action alias: %1")
                              .arg(alias.token));
                return false;
            }
            aliases.insert(key);

            if (alias.surface != ActionSurface::Shortcut)
                continue;
            ++shortcutAliasCount;
            const QKeySequence aliasShortcut =
                QKeySequence::fromString(
                    alias.token.trimmed(),
                    QKeySequence::PortableText);
            const QString normalizedAlias =
                aliasShortcut.toString(
                    QKeySequence::PortableText);
            if (normalizedShortcut.isEmpty()
                || aliasShortcut.isEmpty()
                || normalizedAlias.compare(
                       normalizedShortcut,
                       Qt::CaseInsensitive) != 0) {
                setReason(
                    reason,
                    QStringLiteral(
                        "Shortcut surface disagrees with the default for action: %1")
                        .arg(descriptor.id));
                return false;
            }
        }
        if (!normalizedShortcut.isEmpty()
            && shortcutAliasCount != 1) {
            setReason(
                reason,
                QStringLiteral(
                    "Default shortcut lacks a unique Shortcut surface: %1")
                    .arg(descriptor.id));
            return false;
        }
    }

    if (reason)
        reason->clear();
    return true;
}

bool actionRegistryIsValid(QString* reason)
{
    return validateActionRegistry(actionRegistry(), reason);
}

ActionAvailabilityState evaluateActionAvailability(
    const ActionDescriptor& descriptor,
    const ActionAvailabilityContext& context)
{
    quint32 missing = 0;
    if ((descriptor.requirementMask & ActionRequirements::Editor)
        && !context.editorAvailable) {
        missing |= ActionRequirements::Editor;
    }
    if ((descriptor.requirementMask & ActionRequirements::Workspace)
        && !context.workspaceAvailable) {
        missing |= ActionRequirements::Workspace;
    }
    if ((descriptor.requirementMask
         & ActionRequirements::SemanticCurrent)
        && !context.semanticCurrent) {
        missing |= ActionRequirements::SemanticCurrent;
    }
    if ((descriptor.requirementMask & ActionRequirements::Symbol)
        && !context.symbolAvailable) {
        missing |= ActionRequirements::Symbol;
    }
    if ((descriptor.requirementMask & ActionRequirements::Package)
        && !context.packageAvailable) {
        missing |= ActionRequirements::Package;
    }
    if ((descriptor.requirementMask & ActionRequirements::Hierarchy)
        && !context.hierarchyBound) {
        missing |= ActionRequirements::Hierarchy;
    }
    if ((descriptor.requirementMask & ActionRequirements::GraphContent)
        && !context.graphContentAvailable) {
        missing |= ActionRequirements::GraphContent;
    }

    ActionAvailabilityState result;
    result.executable = missing == 0;
    if (result.executable)
        return result;

    result.reason = descriptor.unavailableReason;
    switch (descriptor.recoveryPolicy) {
    case ActionRecoveryPolicy::None:
        break;
    case ActionRecoveryPolicy::Explain:
        result.resolvable = true;
        break;
    case ActionRecoveryPolicy::Analyze:
        result.resolvable =
            (missing & ~ActionRequirements::SemanticCurrent) == 0;
        break;
    case ActionRecoveryPolicy::SelectHierarchy:
        result.resolvable =
            (missing & ~ActionRequirements::Hierarchy) == 0;
        break;
    }
    return result;
}

ActionExecutionResult executeAction(
    const ActionDescriptor& descriptor,
    ActionExecutionHost& host,
    const ActionInvocation& invocation)
{
    if (!descriptor.execute) {
        ActionExecutionResult result;
        result.handled = true;
        result.failureReason =
            QStringLiteral("Action has no execution function.");
        return result;
    }
    ActionExecutionResult result =
        descriptor.execute(descriptor, host, invocation);
    if (invocation.mode == ActionExecutionMode::DryRun)
        result.dryRun = true;
    ActionInvocation recordedInvocation = invocation;
    if (result.hasResolvedParameters)
        recordedInvocation.parameters = result.resolvedParameters;
    applicationActionExecutionHistory().recordSuccessful(
        descriptor, recordedInvocation, result);
    return result;
}

QString actionCategoryText(ActionCategory category)
{
    switch (category) {
    case ActionCategory::Navigate:
        return QStringLiteral("Navigate");
    case ActionCategory::Inspect:
        return QStringLiteral("Inspect");
    case ActionCategory::Refactor:
        return QStringLiteral("Refactor");
    case ActionCategory::Format:
        return QStringLiteral("Format");
    case ActionCategory::Insert:
        return QStringLiteral("Insert");
    case ActionCategory::Select:
        return QStringLiteral("Select");
    case ActionCategory::Workspace:
        return QStringLiteral("Workspace");
    case ActionCategory::Fold:
        return QStringLiteral("Fold");
    case ActionCategory::Help:
        return QStringLiteral("Help");
    case ActionCategory::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

QString actionScopeText(ActionScope scope)
{
    switch (scope) {
    case ActionScope::Application:
        return QStringLiteral("Application");
    case ActionScope::Workspace:
        return QStringLiteral("Workspace");
    case ActionScope::Editor:
        return QStringLiteral("Editor");
    case ActionScope::Module:
        return QStringLiteral("Module");
    case ActionScope::Package:
        return QStringLiteral("Package");
    case ActionScope::Symbol:
        return QStringLiteral("Symbol");
    case ActionScope::Hierarchy:
        return QStringLiteral("Hierarchy");
    case ActionScope::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

QString actionSurfaceText(ActionSurface surface)
{
    switch (surface) {
    case ActionSurface::CommandLayer:
        return QStringLiteral("Command Mode");
    case ActionSurface::InlineSemantic:
        return QStringLiteral("Inline semantic command");
    case ActionSurface::InlineTemplate:
        return QStringLiteral("Inline template command");
    case ActionSurface::GlobalControl:
        return QStringLiteral("Global Control");
    case ActionSurface::PackageTools:
        return QStringLiteral("Package Tools");
    case ActionSurface::ContextMenu:
        return QStringLiteral("Context menu");
    case ActionSurface::TabContextMenu:
        return QStringLiteral("Tab context menu");
    case ActionSurface::PanelContextMenu:
        return QStringLiteral("Panel context menu");
    case ActionSurface::Shortcut:
        return QStringLiteral("Shortcut");
    case ActionSurface::ActionCatalog:
        return QStringLiteral("Action Catalog");
    case ActionSurface::GraphPanel:
        return QStringLiteral("Graph panel");
    case ActionSurface::Menu:
        return QStringLiteral("Menu");
    case ActionSurface::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

bool configureActionShortcutOverrides(
    const QVariantMap& overrides,
    QStringList* issues)
{
    if (issues)
        issues->clear();

    QVariantMap normalized;
    for (auto it = overrides.cbegin();
         it != overrides.cend();
         ++it) {
        const QString actionId = it.key().trimmed();
        const ActionDescriptor* descriptor =
            findActionById(actionId);
        if (!descriptor) {
            if (issues) {
                issues->append(
                    QStringLiteral(
                        "Unknown action shortcut override was ignored: %1")
                        .arg(actionId));
            }
            continue;
        }
        if (it.value().metaType().id()
            != QMetaType::QString) {
            if (issues) {
                issues->append(
                    QStringLiteral(
                        "Shortcut override is not text: %1")
                        .arg(actionId));
            }
            return false;
        }

        const QString requested =
            it.value().toString().trimmed();
        if (requested.isEmpty()) {
            normalized.insert(actionId, QString());
            continue;
        }
        const QKeySequence sequence =
            QKeySequence::fromString(
                requested,
                QKeySequence::PortableText);
        const QString portable =
            sequence.toString(
                QKeySequence::PortableText);
        if (sequence.isEmpty() || portable.isEmpty()) {
            if (issues) {
                issues->append(
                    QStringLiteral(
                        "Invalid shortcut override for action: %1")
                        .arg(actionId));
            }
            return false;
        }
        if (actionId
                == QString::fromLatin1(
                    ActionIds::ViewCommandMode)
            && sequence.count() != 1) {
            if (issues) {
                issues->append(
                    QStringLiteral(
                        "Command Mode requires a single-stroke hold shortcut."));
            }
            return false;
        }
        normalized.insert(actionId, portable);
    }

    QHash<QString, QString> actionBySequence;
    for (const ActionDescriptor& descriptor :
         actionRegistry()) {
        const QString shortcut =
            normalized.contains(descriptor.id)
            ? normalized.value(
                  descriptor.id).toString()
            : descriptor.defaultShortcut;
        if (shortcut.isEmpty())
            continue;
        const QString key =
            shortcut.toCaseFolded();
        const auto existing =
            actionBySequence.constFind(key);
        if (existing
            != actionBySequence.constEnd()) {
            if (issues) {
                issues->append(
                    QStringLiteral(
                        "Shortcut '%1' conflicts between '%2' and '%3'.")
                        .arg(shortcut,
                             existing.value(),
                             descriptor.id));
            }
            return false;
        }
        actionBySequence.insert(
            key, descriptor.id);
    }

    shortcutOverridesStorage() = normalized;
    return true;
}

QVariantMap actionShortcutOverrides()
{
    return shortcutOverridesStorage();
}

QString effectiveActionShortcut(
    const QString& actionId)
{
    const QVariantMap& overrides =
        shortcutOverridesStorage();
    if (overrides.contains(actionId))
        return overrides.value(actionId).toString();
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    return descriptor
        ? descriptor->defaultShortcut
        : QString();
}
