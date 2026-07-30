#include "actionregistry.h"

#include <QSet>
#include <QStringList>

namespace {
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
         "<number>"},
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
        {"edit.addSignalRow",
         "Add Signal Row",
         "Create a signal declaration row in the current module.",
         "add signal",
         "editor.structure.addSignalRow",
         ActionCategory::Insert,
         ActionScope::Module,
         editor,
         "Place the cursor in a module.",
         ActionParameterKind::None,
         "",
         ""},
        {"edit.addParameterRow",
         "Add Parameter Row",
         "Create a parameter row in the current module or package.",
         "add parameter",
         "editor.structure.addParameterRow",
         ActionCategory::Insert,
         ActionScope::Editor,
         editor,
         "Place the cursor in a module or package parameter scope.",
         ActionParameterKind::None,
         "",
         ""},
        {"edit.addPortRow",
         "Add Port Row",
         "Append a row to a multiline module port list.",
         "add port",
         "editor.structure.addPortRow",
         ActionCategory::Insert,
         ActionScope::Module,
         editor,
         "Place the cursor in a module with a multiline port list.",
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
        descriptor.aliases.append(
            alias(ActionSurface::CommandLayer,
                  QString::fromLatin1(spec.token),
                  QString::fromLatin1(spec.token)));
        if (descriptor.id == QStringLiteral("navigation.goLine")) {
            descriptor.aliases.append(
                alias(ActionSurface::ContextMenu,
                      descriptor.id,
                      QStringLiteral("Go to Line...")));
        }
        out->append(descriptor);
    }

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
        {";;a", "always", "always process", "always_comb begin\nend"},
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
         semanticSymbol},
        {"source.findReferences",
         "Find References",
         "Find indexed references to the current symbol.",
         "editor.source.findReferences",
         ActionCategory::Inspect,
         ActionScope::Symbol,
         semanticSymbol},
        {"source.showRelationships",
         "Show Relationships",
         "Inspect semantic relationships for the current symbol.",
         "insight.relationships.showSymbol",
         ActionCategory::Inspect,
         ActionScope::Symbol,
         semanticSymbol},
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
        descriptor.aliases.append(
            alias(ActionSurface::ContextMenu,
                  QString::fromLatin1(spec.id),
                  QString::fromLatin1(spec.name)));
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
    };
    const QList<Spec> specs = {
        {"edit.undo",
         "Undo",
         "Undo the most recent editor transaction.",
         "editor.standard.undo",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Nothing to undo."},
        {"edit.redo",
         "Redo",
         "Redo the most recently undone editor transaction.",
         "editor.standard.redo",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Nothing to redo."},
        {"edit.cut",
         "Cut",
         "Cut the current text selection.",
         "editor.standard.cut",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "Select text to cut."},
        {"edit.copy",
         "Copy",
         "Copy the current text selection.",
         "editor.standard.copy",
         ActionCategory::Inspect,
         ActionScope::Editor,
         "Select text to copy."},
        {"edit.paste",
         "Paste",
         "Paste clipboard text at the current cursor.",
         "editor.standard.paste",
         ActionCategory::Refactor,
         ActionScope::Editor,
         "The clipboard has no text that can be pasted."},
        {"select.all",
         "Select All",
         "Select the complete editor document.",
         "editor.standard.selectAll",
         ActionCategory::Select,
         ActionScope::Editor,
         "Open an editor tab."},
        {"edit.replace",
         "Replace",
         "Open the editor replace flow.",
         "editor.edit.replace",
         ActionCategory::Refactor,
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
         "Open an editable editor tab."},
        {"format.uncommentLines",
         "Uncomment Lines",
         "Uncomment the selected lines or current line.",
         "editor.format.uncommentLines",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab."},
        {"format.indentLines",
         "Indent Lines",
         "Indent the selected lines or current line.",
         "editor.format.indentLines",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab."},
        {"format.unindentLines",
         "Unindent Lines",
         "Unindent the selected lines or current line.",
         "editor.format.unindentLines",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab."},
        {"format.profile.structured",
         "Structured Profile",
         "Use the structure-aware formatter profile.",
         "editor.format.profile.structured",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editor tab."},
        {"format.profile.indentOnly",
         "Indent-Only Profile",
         "Use the indentation-only formatter profile.",
         "editor.format.profile.indentOnly",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editor tab."},
        {"format.onSave",
         "Format On Save",
         "Toggle formatting before saving the current document.",
         "editor.format.onSave",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editor tab."},
        {"format.selection",
         "Format Selection",
         "Format the current text selection.",
         "editor.format.selection",
         ActionCategory::Format,
         ActionScope::Editor,
         "Select text to format."},
        {"format.document",
         "Format Document",
         "Format the complete current document.",
         "editor.format.document",
         ActionCategory::Format,
         ActionScope::Editor,
         "Open an editable editor tab."}
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
        QString label = descriptor.canonicalName;
        if (descriptor.id == QStringLiteral("edit.replace"))
            label += QStringLiteral("...");
        if (descriptor.id
            == QStringLiteral("refactor.createSignalDefinition")) {
            label += QStringLiteral("...");
        }
        descriptor.aliases.append(
            alias(ActionSurface::ContextMenu,
                  descriptor.id,
                  label));
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
        appendInlineActions(&result);
        appendSourceActions(&result);
        appendEditorContextMenuActions(&result);
        appendPackageActions(&result);
        appendGlobalControlActions(&result);
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
            aliases.append(
                QStringLiteral("%1: %2")
                    .arg(actionSurfaceText(actionAlias.surface),
                         actionAlias.token));
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
            || descriptor.aliases.isEmpty()) {
            setReason(reason,
                      QStringLiteral("Incomplete action descriptor: %1")
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
        return QStringLiteral("F24 Command Layer");
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
    case ActionSurface::Shortcut:
        return QStringLiteral("Shortcut");
    case ActionSurface::ActionCatalog:
        return QStringLiteral("Action Catalog");
    case ActionSurface::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}
