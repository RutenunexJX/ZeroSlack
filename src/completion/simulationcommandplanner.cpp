#include "simulationcommandplanner.h"
#include "editorfileidentity.h"
#include "simulationcommandprefix.h"
#include "slangmanager.h"
#include "tsdocument.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace
{
using Kind = SymbolTaxonomy::DeclarationKind;
bool nodeIs(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && qstrcmp(ts_node_type(node), type) == 0;
}
struct Site
{
    bool procedural = false, function = false, moduleItem = false;
    int declaration = -1;
};
Site parseSite(const GlobalControlQueryContext &context, int position, int length)
{
    const QString probe = QStringLiteral("__template_probe();");
    auto text = context.documentText;
    text.replace(position, length, probe);
    TSDocument syntax;
    syntax.setText(text);
    auto node = ts_node_descendant_for_byte_range(syntax.rootNode(), position * 2, position * 2 + 2);
    Site site;
    bool statement = false;
    auto child = node;
    for (; !ts_node_is_null(node); child = node, node = ts_node_parent(node))
    {
        if (nodeIs(node, "ERROR") || ts_node_is_missing(node))
            break;
        if (nodeIs(node, "subroutine_call_statement") && !ts_node_has_error(node) &&
            ts_node_start_byte(node) == position * 2 && ts_node_end_byte(node) == (position + probe.size()) * 2)
            statement = true;
        if (nodeIs(node, "function_declaration"))
            site.function = site.procedural = true;
        if (nodeIs(node, "initial_construct") || nodeIs(node, "always_construct") || nodeIs(node, "task_declaration"))
            site.procedural = true;
        if (nodeIs(node, "module_declaration"))
        {
            if (nodeIs(child, "module_item") && ts_node_named_child_count(child) == 1)
                child = ts_node_named_child(child, 0);
            if (nodeIs(child, "initial_construct") || nodeIs(child, "always_construct") ||
                nodeIs(child, "task_declaration"))
                site.declaration = ts_node_start_byte(child) / 2;
            const int margin = text.lastIndexOf('\n', qMax(0, site.declaration - 1)) + 1;
            if (site.declaration >= margin && text.mid(margin, site.declaration - margin).trimmed().isEmpty())
                site.declaration = margin;
            break;
        }
    }
    site.procedural = site.procedural && statement;
    if (site.procedural || site.function)
        return site;

    // A module item must occupy a syntax gap, not a recovered expression,
    // declaration, port, comment or string. Confirm both the original gap
    // and the exact initial construct parsed at that location.
    text = context.documentText;
    text.replace(position, length, QString(length, QLatin1Char(' ')));
    syntax.setText(text);
    node = ts_node_descendant_for_byte_range(syntax.rootNode(), position * 2, position * 2);
    if (!nodeIs(node, "module_declaration"))
        return {};
    const QString moduleProbe = QStringLiteral("initial begin __template_probe(); end");
    text.replace(position, length, moduleProbe);
    syntax.setText(text);
    node = ts_node_descendant_for_byte_range(syntax.rootNode(), position * 2, (position + moduleProbe.size()) * 2);
    if (!nodeIs(node, "initial_construct") || ts_node_has_error(node) || ts_node_start_byte(node) != position * 2 ||
        ts_node_end_byte(node) != (position + moduleProbe.size()) * 2)
        return {};
    auto parent = ts_node_parent(node);
    if (nodeIs(parent, "module_item"))
        parent = ts_node_parent(parent);
    if (!nodeIs(parent, "module_declaration"))
        return {};
    site.moduleItem = true;
    site.declaration = position;
    const int margin = text.lastIndexOf('\n', qMax(0, position - 1)) + 1;
    if (text.mid(margin, position - margin).trimmed().isEmpty())
        site.declaration = margin;
    return site;
}
Site siteFor(const GlobalControlQueryContext& context, int position, int length)
{
    // Palette filtering visits every candidate for the same captured document.
    // Keep one immutable syntax result per thread, not one parse per candidate.
    struct Cache { QString text; int position = -1, length = -1; Site site; };
    static thread_local Cache cache;
    if (cache.position != position || cache.length != length || cache.text != context.documentText) {
        cache = {context.documentText, position, length, parseSite(context, position, length)};
    }
    return cache.site;
}
bool snippetMatches(const QString& snippet, const QString& content)
{
    if (snippet.trimmed().isEmpty()) return true;
    const QString prefix = content == "declarations" ? QStringLiteral("module __template;\n")
        : QStringLiteral("module __template; initial begin __probe();\n");
    TSDocument syntax;
    syntax.setText(prefix + snippet + (content == "declarations" ? "\nendmodule" : "\nend endmodule"));
    if (syntax.hasError()) return false;
    auto root = syntax.rootNode();
    if (content != "declarations") return true;
    for (uint32_t i = 0; i < ts_node_named_child_count(root); ++i) {
        const auto module = ts_node_named_child(root, i);
        if (!nodeIs(module, "module_declaration")) continue;
        for (uint32_t j = 0; j < ts_node_named_child_count(module); ++j) {
            auto item = ts_node_named_child(module, j);
            if (ts_node_start_byte(item) < prefix.size() * 2 || nodeIs(item, "comment")) continue;
            while (!nodeIs(item, "data_declaration") && !nodeIs(item, "net_declaration")
                && !nodeIs(item, "parameter_declaration") && !nodeIs(item, "local_parameter_declaration")
                && !nodeIs(item, "type_declaration") && ts_node_named_child_count(item) == 1)
                item = ts_node_named_child(item, 0);
            if (!nodeIs(item, "data_declaration") && !nodeIs(item, "net_declaration")
                && !nodeIs(item, "parameter_declaration") && !nodeIs(item, "local_parameter_declaration")
                && !nodeIs(item, "type_declaration")) return false;
        }
    }
    return true;
}
struct Builder
{
    QString text;
    CodeTemplateSlotList placeholders;
    QHash<QString, int> stops;
    void add(const QString &value)
    {
        text += value;
    }
    void slot(const QString &name, const QString &value, const QVariantMap &input)
    {
        if (!stops.contains(name))
            stops.insert(name, stops.size() + 1);
        placeholders << CodeTemplateSlot{name, int(text.size()), int(value.size()), stops.value(name), true, input};
        text += value;
    }
};
QVariantMap inputFor(const QVariantMap &configuration, const QString &name, const QVariantMap &fallback)
{
    const auto inputs = configuration.value("inputs").toMap();
    return inputs.contains(name) ? inputs.value(name).toMap() : fallback;
}
QString initial(const QVariantMap &input, const QString &fallback = {})
{
    return input.value("default", fallback).toString();
}
} // namespace
QString SimulationCommandPlanner::stringLiteral(const QString &value)
{
    QString result = "\"";
    for (const auto c : value)
    {
        if (c == '\\' || c == '"')
            result += QLatin1Char('\\') + c;
        else if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else
            result += c;
    }
    return result + '"';
}
bool SimulationCommandPlanner::sourcesCurrent(const std::shared_ptr<const SemanticAnalysisInput> &input)
{
    if (!input)
        return false;
    SemanticAnalysisRequest request;
    SemanticInputCapture capture(request);
    for (auto it = input->sources.cbegin(); it != input->sources.cend(); ++it)
        if (!it->overridden && capture.source(it.key()) != it.value())
            return false;
    return true;
}
QString SimulationCommandPlanner::placementError(const GlobalControlQueryContext& context, const QVariantMap& command, const QString& content, const QString& snippet)
{
    return placement(context, command, content, snippet).reason;
}
SimulationPlacement SimulationCommandPlanner::placement(const GlobalControlQueryContext& context, const QVariantMap& command, const QString& content, const QString& snippet)
{
    using Status = SimulationPlacementStatus;
    const int start = context.replacementStart >= 0 ? context.replacementStart : context.cursorPosition;
    if (start < 0) return {Status::Unavailable, QStringLiteral("Select an insertion position.")};
    if (context.replacementLength < 0 || start > context.documentText.size()
        || context.replacementLength > context.documentText.size() - start)
        return {Status::Unavailable, QStringLiteral("The insertion position changed. Reopen the template search.")};
    if (!context.templateBoundaryError.isEmpty())
        return {Status::Incompatible, context.templateBoundaryError};
    const auto kind = command.value("kind", "snippet").toString();
    const auto placement = command.value("placement").toString();
    const auto type = command.value("content").toString();
    if (!content.isEmpty() && !type.isEmpty() && type != content)
        return {Status::Incompatible, QStringLiteral("This template does not match the container content type.")};
    if (content == "declarations" && (kind == "driver" || kind == "print" || kind == "task" || kind == "call"))
        return {Status::Incompatible, QStringLiteral("This container accepts declarations.")};
    if (!content.isEmpty() && kind == "snippet" && !snippetMatches(snippet, content))
        return {Status::Incompatible, QStringLiteral("The template syntax is not valid %1 content.").arg(content)};
    const auto site = siteFor(context, start, context.replacementLength);
    if ((content == "statements" || placement == "statement") && (!site.procedural || site.function))
        return {Status::Incompatible, QStringLiteral("This template needs a procedural statement position.")};
    if ((content == "declarations" || placement == "module") && !site.moduleItem)
        return {Status::Incompatible, QStringLiteral("This template needs a module declaration position.")};
    if (kind == "driver" && ((!site.procedural && !site.moduleItem) || site.function))
        return {Status::Incompatible, QStringLiteral("Insert a driver at a module-item gap or a procedural statement position, outside a function.")};
    if ((kind == "print" || kind == "task") && (!site.procedural || site.function))
        return {Status::Incompatible, QStringLiteral("Insert this command at a procedural statement position, outside a function.")};
    if (context.readOnly) return {Status::Unavailable, QStringLiteral("This document is read-only.")};
    return {};
}

SimulationCommandPlan SimulationCommandPlanner::prepare(const SimulationCommandRequest &request)
{
    SimulationCommandPlan result;
    auto context = request.context;
    if (context.fileName.isEmpty())
        context.fileName =
            QDir::temp().filePath(QStringLiteral("__zeroslack_untitled_%1.sv").arg(context.documentInstance));
    const auto config = request.item.parameters;
    const auto commandKind = config.value("kind", "snippet").toString();
    result.start = request.item.replacementStart >= 0 ? request.item.replacementStart : context.cursorPosition;
    result.length = request.item.replacementLength;
    if (context.readOnly || result.start < 0 || result.length < 0
        || result.start > context.documentText.size() || result.length > context.documentText.size() - result.start)
    {
        result.error = QStringLiteral("This insertion range is no longer editable.");
        return result;
    }
    context.replacementStart = result.start;
    context.replacementLength = result.length;
    result.error = placementError(context, config, request.item.templateContent, request.item.insertionText);
    if (!result.error.isEmpty()) return result;
    const auto site = siteFor(context, result.start, result.length);
    auto source = context.documentText;
    source.replace(result.start, result.length, QString(result.length, QLatin1Char(' ')));
    SemanticAnalysisRequest semanticRequest;
    semanticRequest.sourceOverrides = request.contents;
    semanticRequest.sourceOverrides.remove(QString());
    semanticRequest.sourceOverrides.insert(context.fileName, source);
    auto orderedFiles = request.orderedFiles;
    orderedFiles.removeAll(QString());
    if (!orderedFiles.contains(context.fileName))
        orderedFiles << context.fileName;
    if (commandKind == "driver")
    {
        const auto symbol = config.value("symbol").toString();
        const auto separator = symbol.lastIndexOf("::");
        if (separator < 0)
        {
            result.error = QStringLiteral("The generic driver has no class declaration identity.");
            return result;
        }
        const auto probe = context.fileName + QStringLiteral(".zeroslack-command-probe.sv");
        semanticRequest.sourceOverrides.insert(
            probe,
            QStringLiteral("package __zeroslack_command_probe; typedef %1#(logic [7:0]) callable_t; endpackage\n")
                .arg(symbol.left(separator)));
        orderedFiles << probe;
    }
    SemanticInputCapture capture(semanticRequest);
    SlangManager slang;
    const auto cancelled = [flag = request.cancelled] { return flag && flag->load(); };
    auto analysis = slang.analyzeCapturedWorkspace(capture, orderedFiles, request.includeDirectories, request.defines,
                                                   {}, cancelled, {true, true, false});
    if (analysis.cancelled || !analysis.error.isEmpty() || !capture.stillMatchesDisk(cancelled))
    {
        result.error = analysis.error.isEmpty()
                           ? QStringLiteral("The source changed or command preparation was cancelled.")
                           : analysis.error;
        return result;
    }
    for (const auto &diagnostic : analysis.diagnostics)
    {
        if (diagnostic.severity == SemanticDiagnostic::Error &&
            !EditorFileIdentity::same(diagnostic.fileName, context.fileName))
        {
            result.error = QStringLiteral("Resolve the package error before inserting: %1").arg(diagnostic.message);
            return result;
        }
    }
    result.captured = capture.seal();
    result.inputs = std::make_shared<TemplateInputContext>();
    result.inputs->snapshot = std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(analysis.symbols, {}, {}, semanticRequest.sourceOverrides));
    result.inputs->location.fileName = context.fileName;
    result.inputs->location.moduleName = context.moduleName;
    result.inputs->location.packageName = context.packageName;
    result.inputs->location.cursorPosition = result.start;
    result.inputs->location.cursorLine = context.cursorLine;
    result.inputs->roles = request.roles;
    const auto prefix = context.documentText.mid(result.start, result.length);
    if (prefix.startsWith('@') && prefix.size() > 1)
    {
        SemanticIndex local;
        local.setSnapshot(result.inputs->snapshot);
        const bool eventSignal = SimulationCommandPrefix::refersToValue(prefix.mid(1), local, result.inputs->location);
        if (eventSignal)
        {
            result.error = QStringLiteral("This @ expression refers to an SV event signal.");
            return result;
        }
    }
    if (commandKind == "snippet" || commandKind.isEmpty())
    {
        result.text = request.item.insertionText;
        result.placeholders = request.item.templateSlots;
    }
    else if (commandKind == "print")
    {
        if (!site.procedural)
        {
            result.error = QStringLiteral("Insert a simulation event inside a procedure.");
            return result;
        }
        Builder b;
        const auto preset = config.value("print").toMap();
        const auto level = preset.value("level", "Info").toString();
        if (!QStringList{"Info", "Warning", "Error"}.contains(level))
        {
            result.error = QStringLiteral("Unknown event level.");
            return result;
        }
        const auto condition = preset.value("condition").toString();
        if (!condition.isEmpty())
            b.add("if (" + condition + ")\n  ");
        b.add("zeroslack_events_package::emit_event($realtime / 1ps, " + stringLiteral(level) + ", " +
              stringLiteral(preset.value("tag").toString()) + ", $sformatf(\"%m\"), `__FILE__, `__LINE__, ");
        if (preset.contains("message"))
            b.add(stringLiteral(preset.value("message").toString()));
        else
            b.slot("message", "\"message\"", {{"kind", "Value"}});
        const auto fields = preset.value("fields").toList();
        if (!fields.isEmpty())
        {
            b.add(", {");
            bool first = true;
            for (const auto &row : fields)
            {
                const auto field = row.toMap();
                const auto radix = field.value("radix", "hex").toString();
                const QMap<QString, QString> formats{
                    {"hex", "%h"}, {"binary", "%b"}, {"decimal", "%0d"}, {"text", "%s"}, {"real", "%g"}};
                if (!formats.contains(radix))
                {
                    result.error = QStringLiteral("Unknown field radix.");
                    return result;
                }
                if (!first)
                    b.add(", \",\", ");
                first = false;
                b.add("zeroslack_events_package::field(" + stringLiteral(field.value("name").toString()) +
                      ", $sformatf(" + stringLiteral(formats.value(radix)) + ", ");
                if (field.value("expression").toString().isEmpty())
                {
                    const auto name = field.value("name", "value").toString();
                    b.slot(name, "__" + name + "__",
                           inputFor(config, name, {{"kind", "Signal"}, {"scope", "visible"}}));
                }
                else
                    b.add(field.value("expression").toString());
                b.add("), " + stringLiteral(radix) + ", " + stringLiteral(field.value("unit").toString()) + ")");
            }
            b.add("}");
        }
        b.add(");");
        result.text = b.text;
        result.placeholders = b.placeholders;
    }
    else
    {
        const auto symbol = config.value("symbol").toString(), sourceFile = config.value("sourceFile").toString();
        SemanticSymbolRecord callable;
        QSet<QString> identities;
        for (const auto &record : analysis.symbols)
        {
            if (!record.presentation.callableSignatureAvailable ||
                record.presentation.callableQualifiedName != symbol ||
                (!sourceFile.isEmpty() && !EditorFileIdentity::same(record.location.fileName, sourceFile)))
                continue;
            identities.insert(symbolStableKeyText(record.stableKey));
            callable = record;
        }
        if (identities.size() != 1)
        {
            result.error = QStringLiteral("Slang did not resolve one task/function declaration for %1. Check "
                                          "the package and compilation order.")
                               .arg(symbol);
            return result;
        }
        const bool task = callable.declarationKind == Kind::Task;
        const bool driver = commandKind == "driver";
        const bool callableSite = site.procedural || (driver && site.moduleItem);
        if ((task && (!callableSite || site.function)) || (callable.presentation.callableReturnsVoid && !callableSite))
        {
            result.error = QStringLiteral("The resolved callable requires a procedural statement position.");
            return result;
        }
        if ((commandKind == "task" && !task) || (commandKind == "function" && task))
        {
            result.error = QStringLiteral("The source callable kind changed; update the template binding.");
            return result;
        }
        Builder b;
        QString callName = symbol;
        QString binding, valueType;
        if (driver)
        {
            if (site.declaration < 0 || site.declaration > result.start)
            {
                result.error = QStringLiteral("The driver needs a module declaration scope.");
                return result;
            }
            int index = 1;
            do
            {
                binding = QStringLiteral("simulation_driver_%1").arg(index++);
            } while (source.contains(binding));
            valueType = binding + "_value_t";
            const bool reset = symbol.contains("reset_driver");
            auto target = inputFor(config, "signal",
                                   {{"kind", "Signal"},
                                    {"scope", "visible"},
                                    {"access", "write"},
                                    {"integral", true},
                                    {"unpackedRank", 0},
                                    {"minWidth", 1}});
            if (reset)
            {
                target.insert("maxWidth", 1);
                target.insert("role", "Reset");
            }
            auto clock = inputFor(config, "clock",
                                  {{"kind", "Signal"},
                                   {"scope", "visible"},
                                   {"role", "Clock"},
                                   {"maxWidth", 1},
                                   {"minWidth", 1},
                                   {"unpackedRank", 0}});
            // The adapter is declared at module scope, so procedural locals
            // cannot be referenced by its typedef or virtual methods.
            target.insert("moduleVariable", true);
            clock.insert("moduleVariable", true);
            b.add("typedef type(");
            b.slot("signal", initial(target, "__signal__"), target);
            b.add(") " + valueType + ";\nclass " + binding + " extends tb_signal_package::signal_driver#(" + valueType +
                  ");\n  virtual task wait_edge(input bit rising);\n    if (rising) @(posedge ");
            b.slot("clock", initial(clock, "__clock__"), clock);
            b.add("); else @(negedge ");
            b.slot("clock", initial(clock, "__clock__"), clock);
            b.add(");\n  endtask\n  virtual function void drive(input " + valueType + " value); ");
            b.slot("signal", initial(target, "__signal__"), target);
            b.add(" = value; endfunction\nendclass\n\n");
            result.instance.preservedStart = b.text.size();
            result.instance.preservedLength = result.start - site.declaration;
            result.instance.fragments << TemplateFragment{0, int(b.text.size()), true};
            b.add(context.documentText.mid(site.declaration, result.start - site.declaration));
            result.length += result.start - site.declaration;
            result.start = site.declaration;
            b.add((site.moduleItem ? QStringLiteral("initial begin\n  ") : QStringLiteral("begin\n  ")) + binding +
                  " driver;\n  tb_signal_package::status_t status;\n  driver = new;\n  ");
            const auto separator = symbol.lastIndexOf("::");
            callName = symbol.left(separator) + "#(" + valueType + ")" + symbol.mid(separator);
        }
        b.add(callName + "(");
        bool first = true;
        for (const auto &argument : callable.presentation.callableArguments)
        {
            if (!first)
                b.add(", ");
            first = false;
            b.add("." + argument.name + "(");
            if (driver && argument.name == "target")
                b.add("driver");
            else if (driver && argument.name == "status")
                b.add("status");
            else if (driver && argument.name == "cancel")
                b.add("null");
            else
            {
                QVariantMap fallback{{"kind", argument.input ? "Value" : "Signal"},
                                     {"allowEmpty", argument.hasDefault}};
                QString defaultValue = argument.hasDefault ? QString()
                                       : argument.input    ? QStringLiteral("0")
                                                           : QStringLiteral("__%1__").arg(argument.name);
                if (!argument.input)
                    fallback.insert("access", "write");
                if (driver)
                {
                    if (argument.name == "rising")
                    {
                        fallback = {{"kind", "Choice"}, {"choices", QStringList{"1'b1", "1'b0"}}};
                        defaultValue = "1'b1";
                    }
                    else if (argument.name == "cycles" || argument.name == "count")
                    {
                        fallback.insert("integer", true);
                        fallback.insert("minimum", "0");
                        defaultValue = "10";
                    }
                    else if (argument.name == "asserted_value")
                        defaultValue = "1'b1";
                    else if (argument.name == "released_value")
                        defaultValue = "1'b0";
                    else if (argument.name == "step")
                        defaultValue = "1";
                    else if (argument.name == "maximum")
                        defaultValue = "255";
                    else if (argument.name == "stop_at_ps")
                        defaultValue = "100000";
                }
                const auto input = inputFor(config, argument.name, fallback);
                b.slot(argument.name, initial(input, defaultValue), input);
            }
            b.add(")");
        }
        b.add(task || callable.presentation.callableReturnsVoid ? ");" : ")");
        if (driver)
            b.add("\nend");
        result.text = b.text;
        result.placeholders = b.placeholders;
    }
    if (result.instance.fragments.isEmpty()) result.instance.fragments << TemplateFragment{0, int(result.text.size()), false};
    else {
        const int call = result.instance.preservedStart + result.instance.preservedLength;
        result.instance.fragments << TemplateFragment{call, int(result.text.size()) - call, false};
    }
    result.instance.label = request.item.title;
    result.instance.snapshot = config;
    result.instance.snapshot.insert("templateText", request.item.insertionText);
    result.instance.snapshot.insert("schema", 1);
    result.error = TemplateInputService::dependencyError(result.placeholders);
    for (auto &slot : result.placeholders)
        slot.inputContext = result.inputs;
    return result;
}
